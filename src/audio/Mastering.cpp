// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Mastering.h"
#include <numeric>
#include <optional>

namespace nightshift::mastering
{

const std::vector<Target>& targets()
{
    static const std::vector<Target> list {
        { "safe",      "Just keep it from clipping", "Leaves the loudness as you mixed it",                0.0f, -1.0f },
        { "streaming", "Streaming",                  "Spotify, YouTube, Tidal, Amazon Music: -14 LUFS",  -14.0f, -1.0f },
        { "apple",     "Apple Music",                "Apple Music, and podcasts: -16 LUFS",              -16.0f, -1.0f },
        { "loud",      "Loud",                       "SoundCloud, clubs, beat stores: -9 LUFS",           -9.0f, -1.0f },
    };

    return list;
}

const Target* findTarget (const juce::String& id)
{
    for (auto& t : targets())
        if (id == t.id)
            return &t;

    return nullptr;
}

//==============================================================================
void Maximizer::prepare (double rate)
{
    sampleRate = rate;
    lookahead = std::max (2, juce::roundToInt (rate * 0.003));

    // The meter's own true-peak oversampler (LoudnessMeter::prepare), so the ceiling is held as
    // the meter measures it. Its four points for a sample come out about `latency` samples after
    // the sample went in: the gain worked out from the newest four sets of them is for the sample
    // detectDelay back, the four covering it and the gaps either side
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) 2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);
    oversampler->initProcessing ((size_t) maxChunk);
    detectDelay = (int) std::ceil (oversampler->getLatencyInSamples() + 0.25f);
    scratch.setSize (2, maxChunk);
    peaks.assign ((size_t) maxChunk, 0.0f);

    delayL.assign ((size_t) (detectDelay + lookahead), 0.0f);
    delayR.assign ((size_t) (detectDelay + lookahead), 0.0f);
    queueIndex.assign ((size_t) lookahead + 1, 0);
    queueGain.assign ((size_t) lookahead + 1, 1.0f);
    held.assign ((size_t) lookahead, 1.0f);

    fastRelease = 1.0 - std::exp (-1.0 / (0.05 * rate));
    slowRelease = 1.0 - std::exp (-1.0 / (0.2 * rate));
    workingSmooth = 1.0 - std::exp (-1.0 / (0.3 * rate));
    reset();
}

void Maximizer::reset() noexcept
{
    if (oversampler != nullptr)
        oversampler->reset();

    std::fill (std::begin (recentPeaks), std::end (recentPeaks), 0.0f);
    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    delayPos = 0;
    queueHead = queueSize = 0;
    sampleIndex = 0;
    std::fill (held.begin(), held.end(), 1.0f);
    heldPos = 0;
    heldSum = lookahead;
    released = 1.0f;
    working = 1.0;
    workingCoeff = fastRelease;
    reductionDb = 0.0f;
}

void Maximizer::setGainDb (float db, bool now) noexcept
{
    targetGain = juce::Decibels::decibelsToGain (db, -120.0f);

    if (now)
    {
        gain = targetGain;
        gainStep = 0.0f;
    }
    else
    {
        gainStep = (targetGain - gain) / (float) std::max (1.0, 0.01 * sampleRate);
    }
}

void Maximizer::process (float* left, float* right, int numSamples) noexcept
{
    if (delayL.empty() || oversampler == nullptr)
        return;

    float lowest = 1.0f;

    for (int start = 0; start < numSamples; start += maxChunk)
    {
        processChunk (left + start, right != nullptr ? right + start : nullptr, std::min (maxChunk, numSamples - start));
        lowest = std::min (lowest, juce::Decibels::decibelsToGain (-reductionDb.load()));
    }

    reductionDb = -juce::Decibels::gainToDecibels (lowest);
}

void Maximizer::processChunk (float* left, float* right, int numSamples) noexcept
{
    const int length = (int) delayL.size();
    float* inL = scratch.getWritePointer (0);
    float* inR = scratch.getWritePointer (1);

    // The gain in, eased towards where it's been set
    for (int i = 0; i < numSamples; ++i)
    {
        if (gainStep != 0.0f)
        {
            gain += gainStep;

            if ((gainStep > 0.0f && gain >= targetGain) || (gainStep < 0.0f && gain <= targetGain))
            {
                gain = targetGain;
                gainStep = 0.0f;
            }
        }

        inL[i] = left[i] * gain;
        inR[i] = (right != nullptr ? right[i] : left[i]) * gain;
    }

    // Each sample's loudest point, 4x oversampled (run always, so its filters never start cold)
    {
        const float* channels[2] = { inL, inR };
        juce::dsp::AudioBlock<const float> block (channels, 2, (size_t) numSamples);
        auto up = oversampler->processSamplesUp (block);
        const float* upL = up.getChannelPointer (0);
        const float* upR = up.getChannelPointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            float p = 0.0f;

            for (int s = i * 4; s < i * 4 + 4; ++s)
                p = std::max (p, std::max (std::abs (upL[s]), std::abs (upR[s])));

            peaks[(size_t) i] = p;
        }
    }

    float lowest = 1.0f;
    const int capacity = (int) queueGain.size();

    for (int i = 0; i < numSamples; ++i)
    {
        float g = 1.0f;

        // The four newest sets of points cover the sample detectDelay back and the gaps either side
        recentPeaks[0] = recentPeaks[1];
        recentPeaks[1] = recentPeaks[2];
        recentPeaks[2] = recentPeaks[3];
        recentPeaks[3] = peaks[(size_t) i];

        if (limiting)
        {
            const float peak = std::max (std::max (recentPeaks[0], recentPeaks[1]), std::max (recentPeaks[2], recentPeaks[3]));
            const float need = peak > ceiling ? ceiling / peak : 1.0f;

            // The smallest need across the lookahead window
            while (queueSize > 0 && queueGain[(size_t) ((queueHead + queueSize - 1) % capacity)] >= need)
                --queueSize;

            const int back = (queueHead + queueSize) % capacity;
            queueIndex[(size_t) back] = sampleIndex;
            queueGain[(size_t) back] = need;
            ++queueSize;

            while (queueIndex[(size_t) queueHead] <= sampleIndex - lookahead)
            {
                queueHead = (queueHead + 1) % capacity;
                --queueSize;
            }

            const float hold = queueGain[(size_t) queueHead];

            // Down at once; back up at a pace that slows the harder it's been working
            released = hold < released ? hold : (float) (released + (hold - released) * workingCoeff);

            // Averaging over the window ramps the gain down before each peak arrives
            heldSum += released - held[(size_t) heldPos];
            held[(size_t) heldPos] = released;
            heldPos = heldPos + 1 == lookahead ? 0 : heldPos + 1;
            g = std::min (1.0f, (float) (heldSum / lookahead));

            working += (g - working) * workingSmooth;

            if ((sampleIndex & 31) == 0)
            {
                const double reduction = -20.0 * std::log10 (std::max (1.0e-6, working));
                workingCoeff = fastRelease + (slowRelease - fastRelease) * juce::jlimit (0.0, 1.0, reduction / 12.0);
            }

            if ((sampleIndex & 0xffff) == 0)
                heldSum = std::accumulate (held.begin(), held.end(), 0.0);
        }

        // The audio, as late as the gain needs it to be
        delayL[(size_t) delayPos] = inL[i];
        delayR[(size_t) delayPos] = inR[i];
        const int readPos = delayPos + 1 == length ? 0 : delayPos + 1;
        left[i] = delayL[(size_t) readPos] * g;

        if (right != nullptr)
            right[i] = delayR[(size_t) readPos] * g;

        delayPos = readPos;
        ++sampleIndex;
        lowest = std::min (lowest, g);
    }

    reductionDb = -juce::Decibels::gainToDecibels (lowest);
}

//==============================================================================
const char* Shaper::toneBandName (int band)
{
    static const char* const names[] { "Bass", "Body", "Presence", "Air" };
    return names[juce::jlimit (0, numToneBands - 1, band)];
}

const char* Shaper::punchBandName (int band)
{
    static const char* const names[] { "Lows", "Mids", "Highs" };
    return names[juce::jlimit (0, numPunchBands - 1, band)];
}

dsp::Coeffs Shaper::toneCoeffs (int band, float gainDb, double rate)
{
    switch (band)
    {
        case 0:  return dsp::Coeffs::lowShelf (100.0, gainDb, rate);
        case 1:  return dsp::Coeffs::bell (300.0, 0.8, gainDb, rate);
        case 2:  return dsp::Coeffs::bell (3000.0, 0.8, gainDb, rate);
        default: return dsp::Coeffs::highShelf (10000.0, gainDb, rate);
    }
}

bool isShaping (const Song::Mastering& m)
{
    const bool tone = m.toneOn && std::any_of (m.toneDb.begin(), m.toneDb.end(), [] (float db) { return std::abs (db) > 0.001f; });
    const bool punch = m.punchOn && m.punchAmount > 0.001f;
    const bool width = m.widthOn && (std::abs (m.width - 1.0f) > 0.001f || m.monoBassHz > 0.0f);
    return tone || punch || width;
}

void Shaper::prepare (double rate)
{
    sampleRate = rate;
    const juce::dsp::ProcessSpec spec { rate, 1024, 2 };

    // Lows | the rest at 120 Hz, then mids | highs at 2.5 kHz; the lows go through that second
    // split's allpass too, so the three add back up to the mix with a flat response
    for (auto* f : { &lowSplit, &highSplit, &lowAlign, &sideHighPass })
        f->prepare (spec);

    lowSplit.setCutoffFrequency (120.0f);
    highSplit.setCutoffFrequency (2500.0f);
    lowAlign.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
    lowAlign.setCutoffFrequency (2500.0f);
    sideHighPass.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    sideHighPass.setCutoffFrequency (120.0f);

    // Slow to catch the lows (the kick's thump gets through), quick on the highs
    const double attackMs[] { 30.0, 15.0, 5.0 }, releaseMs[] { 250.0, 150.0, 100.0 };

    for (int b = 0; b < numPunchBands; ++b)
    {
        punch[b].attack = 1.0 - std::exp (-1.0 / (attackMs[b] * 0.001 * rate));
        punch[b].release = 1.0 - std::exp (-1.0 / (releaseMs[b] * 0.001 * rate));
    }

    averageCoeff = 1.0 - std::exp (-1.0 / (2.5 * rate));

    const auto current = settings;
    settings = {};
    set (current);
    reset();
}

void Shaper::reset() noexcept
{
    for (auto& t : tone)
        t.reset();

    for (auto* f : { &lowSplit, &highSplit, &lowAlign, &sideHighPass })
        f->reset();

    for (auto& p : punch)
    {
        p.envelope = 0.0f;
        p.powerIn = p.powerOut = 0.0;
        p.makeup = 1.0f;
    }

    for (auto& r : punchReduction)
        r = 0.0f;
}

void Shaper::set (const Song::Mastering& m) noexcept
{
    for (int b = 0; b < numToneBands; ++b)
    {
        // A band moved between everything, the centre and the sides starts afresh
        if (m.toneWhere[(size_t) b] != settings.toneWhere[(size_t) b])
            tone[b].reset();

        if (m.toneDb[(size_t) b] != settings.toneDb[(size_t) b] || tone[b].c.b0 == 1.0)
            tone[b].c = toneCoeffs (b, m.toneDb[(size_t) b], sampleRate);
    }

    // Punch: more of it lowers the threshold (from -6 dB to -18) and steepens the ratio (to 2.5:1,
    // 3.25:1 with a band's share at 150%): all of it takes 6 or 7 dB off a mix's loudest moments
    for (int b = 0; b < numPunchBands; ++b)
    {
        const float amount = juce::jlimit (0.0f, 1.5f, m.punchAmount * m.punchShare[(size_t) b]);
        punch[b].thresholdDb = -6.0f - 12.0f * std::min (1.0f, amount);
        punch[b].slope = 1.0f - 1.0f / (1.0f + 1.5f * amount);
    }

    if (m.monoBassHz > 0.0f && m.monoBassHz != settings.monoBassHz)
        sideHighPass.setCutoffFrequency (m.monoBassHz);

    settings = m;
    toneActive = m.toneOn && std::any_of (m.toneDb.begin(), m.toneDb.end(), [] (float db) { return std::abs (db) > 0.001f; });
    anyCentreOrSides = false;

    for (int b = 0; b < numToneBands; ++b)
        anyCentreOrSides = anyCentreOrSides || (m.toneWhere[(size_t) b] != 0 && std::abs (m.toneDb[(size_t) b]) > 0.001f);

    punchActive = m.punchOn && m.punchAmount > 0.001f;
    widthActive = m.widthOn && (std::abs (m.width - 1.0f) > 0.001f || m.monoBassHz > 0.0f);
}

void Shaper::processPunch (float& l, float& r, float* lowest) noexcept
{
    float band[numPunchBands][2];
    float restL, restR;
    lowSplit.processSample (0, l, band[0][0], restL);
    lowSplit.processSample (1, r, band[0][1], restR);
    highSplit.processSample (0, restL, band[1][0], band[2][0]);
    highSplit.processSample (1, restR, band[1][1], band[2][1]);
    band[0][0] = lowAlign.processSample (0, band[0][0]);
    band[0][1] = lowAlign.processSample (1, band[0][1]);

    l = r = 0.0f;

    for (int b = 0; b < numPunchBands; ++b)
    {
        auto& p = punch[b];
        const float level = std::max (std::abs (band[b][0]), std::abs (band[b][1]));
        p.envelope += (level - p.envelope) * (float) (level > p.envelope ? p.attack : p.release);

        // Over the threshold, by the ratio, with a 6 dB soft knee
        const float over = 20.0f * std::log10 (p.envelope + 1.0e-9f) - p.thresholdDb;
        const float reduction = over <= -3.0f ? 0.0f
                              : over < 3.0f ? p.slope * (over + 3.0f) * (over + 3.0f) / 12.0f
                                            : p.slope * over;

        const float gain = std::pow (10.0f, -reduction / 20.0f);
        const float outL = band[b][0] * gain, outR = band[b][1] * gain;

        // Given back as much as it took away, by power over the last few seconds, so the band
        // keeps its level and only its loudest moments come closer to the rest
        p.powerIn += (0.5 * ((double) band[b][0] * band[b][0] + (double) band[b][1] * band[b][1]) - p.powerIn) * averageCoeff;
        p.powerOut += (0.5 * ((double) outL * outL + (double) outR * outR) - p.powerOut) * averageCoeff;

        if (p.powerIn > 1.0e-10 && p.powerOut > 1.0e-12)
            p.makeup = (float) juce::jlimit (1.0, 4.0, std::sqrt (p.powerIn / p.powerOut));

        l += outL * p.makeup;
        r += outR * p.makeup;
        lowest[b] = std::max (lowest[b], reduction);
    }
}

void Shaper::process (float* left, float* right, int numSamples) noexcept
{
    if (! toneActive && ! punchActive && ! widthActive)
    {
        for (auto& r : punchReduction)
            r = 0.0f;

        return;
    }

    float deepest[numPunchBands] { 0.0f, 0.0f, 0.0f };

    for (int i = 0; i < numSamples; ++i)
    {
        float l = left[i];
        float r = right != nullptr ? right[i] : l;

        if (toneActive)
        {
            for (int b = 0; b < numToneBands; ++b)
                if (settings.toneWhere[(size_t) b] == 0 && std::abs (settings.toneDb[(size_t) b]) > 0.001f)
                {
                    l = tone[b].process (l, 0);
                    r = tone[b].process (r, 1);
                }

            if (anyCentreOrSides)
            {
                float mid = 0.5f * (l + r), side = 0.5f * (l - r);

                for (int b = 0; b < numToneBands; ++b)
                    if (std::abs (settings.toneDb[(size_t) b]) > 0.001f)
                    {
                        if (settings.toneWhere[(size_t) b] == 1)
                            mid = tone[b].process (mid, 0);
                        else if (settings.toneWhere[(size_t) b] == 2)
                            side = tone[b].process (side, 1);
                    }

                l = mid + side;
                r = mid - side;
            }
        }

        if (punchActive)
        {
            float reductions[numPunchBands] { 0.0f, 0.0f, 0.0f };
            processPunch (l, r, reductions);

            for (int b = 0; b < numPunchBands; ++b)
                deepest[b] = std::max (deepest[b], reductions[b]);
        }

        if (widthActive)
        {
            const float mid = 0.5f * (l + r);
            float side = 0.5f * (l - r);

            if (settings.monoBassHz > 0.0f)
                side = sideHighPass.processSample (0, side);

            side *= settings.width;
            l = mid + side;
            r = mid - side;
        }

        left[i] = l;

        if (right != nullptr)
            right[i] = r;
    }

    for (int b = 0; b < numPunchBands; ++b)
        punchReduction[(size_t) b] = punchActive ? deepest[b] : 0.0f;
}

//==============================================================================
Readings measureFile (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return {};

    constexpr int chunk = 8192;
    const int channels = (int) std::min<unsigned int> (2, reader->numChannels);
    tracktion::LoudnessMeter meter;
    meter.prepare (reader->sampleRate, channels, chunk);
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, chunk);

    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += chunk)
    {
        const int n = (int) std::min<juce::int64> (chunk, reader->lengthInSamples - pos);
        reader->read (&buffer, 0, n, pos, true, true);
        meter.process (buffer.getArrayOfReadPointers(), channels, n);
    }

    meter.flush();
    return meter.getReadings();
}

Push findPush (const juce::File& capture, float targetLufs, float ceilingDb, const std::function<bool (float)>& progress, const Song::Mastering* shape,
               float maxSqueezeDb)
{
    Push result;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (capture));

    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        result.problem = "NightShift couldn't read the song back to measure it.";
        return result;
    }

    const double rate = reader->sampleRate;
    const auto length = reader->lengthInSamples;
    const int channels = (int) std::min<unsigned int> (2, reader->numChannels);
    constexpr int chunk = 8192;
    constexpr int maxPasses = 7;
    constexpr float expectedPasses = 4.0f;
    int pass = 0;

    // One pass: the capture through the limiter at `push`, measured as it comes out
    auto play = [&] (float push, float& squeeze) -> std::optional<Readings>
    {
        Maximizer limiter;
        limiter.prepare (rate);
        limiter.setCeilingDb (ceilingDb);
        limiter.setGainDb (push, true);

        std::unique_ptr<Shaper> shaper;

        if (shape != nullptr)
        {
            shaper = std::make_unique<Shaper>();
            shaper->prepare (rate);
            shaper->set (*shape);
        }

        tracktion::LoudnessMeter meter;
        meter.prepare (rate, channels, chunk);
        juce::AudioBuffer<float> buffer (std::max (2, (int) reader->numChannels), chunk);
        const int latency = limiter.getLatencySamples();
        const juce::int64 total = length + latency;
        squeeze = 0.0f;

        for (juce::int64 pos = 0; pos < total; pos += chunk)
        {
            const int n = (int) std::min<juce::int64> (chunk, total - pos);
            buffer.clear();
            reader->read (&buffer, 0, n, pos, true, true);      // past the end: silence, to flush the delay

            if (shaper != nullptr)
                shaper->process (buffer.getWritePointer (0), channels > 1 ? buffer.getWritePointer (1) : nullptr, n);

            limiter.process (buffer.getWritePointer (0), channels > 1 ? buffer.getWritePointer (1) : nullptr, n);
            squeeze = std::max (squeeze, limiter.getGainReductionDb());

            // What comes out first is the silence the limiter's delay started with
            const int skip = (int) juce::jlimit<juce::int64> (0, n, latency - pos);

            if (n > skip)
            {
                const float* out[2] = { buffer.getReadPointer (0, skip), buffer.getReadPointer (channels > 1 ? 1 : 0, skip) };
                meter.process (out, channels, n - skip);
            }

            const float done = std::min (0.97f, ((float) pass + (float) (pos + n) / (float) total) / expectedPasses);

            if (progress != nullptr && ! progress (done))
                return std::nullopt;
        }

        meter.flush();
        ++pass;
        return meter.getReadings();
    };

    float squeeze = 0.0f;
    const auto as = play (0.0f, squeeze);

    if (! as.has_value())
    {
        result.problem = "Stopped.";
        return result;
    }

    if (! as->integratedValid)
    {
        result.problem = "The song is silent, so there's nothing to make louder.";
        return result;
    }

    result.beforeLufs = as->integratedLufs;

    // The most it may push: the limiter turns the loudest peak down by the push plus however far that
    // peak already stands over the ceiling (or less, when it's under), so the squeeze cap is a push cap
    const float over = squeeze > 0.0f ? squeeze : as->truePeakDb - ceilingDb;
    const float most = juce::jlimit (-24.0f, 24.0f, maxSqueezeDb - over);

    // Each try from how much the last one moved it: about 1:1 until the limiter's working, less after
    float push = juce::jlimit (-24.0f, most, targetLufs - as->integratedLufs);
    float lastPush = 0.0f, lastLufs = as->integratedLufs;
    Readings best = *as;
    float bestPush = 0.0f, bestSqueeze = squeeze;

    for (int attempt = 1; attempt < maxPasses; ++attempt)
    {
        const auto r = play (push, squeeze);

        if (! r.has_value())
        {
            result.problem = "Stopped.";
            return result;
        }

        if (std::abs (targetLufs - r->integratedLufs) < std::abs (targetLufs - best.integratedLufs) || attempt == 1)
        {
            best = *r;
            bestPush = push;
            bestSqueeze = squeeze;
        }

        const float miss = targetLufs - r->integratedLufs;

        if (std::abs (miss) <= 0.1f)
            break;

        const float slope = juce::jlimit (0.05f, 1.2f, std::abs (push - lastPush) > 0.01f ? (r->integratedLufs - lastLufs) / (push - lastPush) : 1.0f);
        lastPush = push;
        lastLufs = r->integratedLufs;
        const float next = juce::jlimit (-24.0f, most, push + miss / slope);

        if (std::abs (next - push) < 0.01f)
            break;      // pushed as far as it goes

        push = next;
    }

    result.ok = true;
    result.pushDb = bestPush;
    result.afterLufs = best.integratedLufs;
    result.truePeakDb = best.truePeakDb;
    result.squeezeDb = bestSqueeze;
    result.reachedTarget = std::abs (targetLufs - best.integratedLufs) <= 0.15f;
    result.held = ! result.reachedTarget && most < 24.0f && bestPush >= most - 0.05f && best.integratedLufs < targetLufs;

    if (progress != nullptr)
        progress (1.0f);

    return result;
}

} // namespace nightshift::mastering
