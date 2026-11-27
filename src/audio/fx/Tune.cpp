// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "StockEffects.h"
#include "model/SongKey.h"

namespace nightshift
{

const char* NightTune::xmlTypeName = "nightshiftTune";

namespace
{
    constexpr double yinThreshold = 0.15;      // a dip under this is a period (YIN's absolute threshold)
    constexpr double voicedBelow = 0.25;       // and a pitch is believed when its best dip is under this
    constexpr float gateLevel = 0.003f;        // quieter than about -50 dB (RMS) has no pitch to hear
    constexpr double believeJump = 0.8;        // semitones: a jump further than this waits to be heard again
    constexpr double stayOn = 0.15;            // semitones nearer another note must be before it moves to that one
    constexpr double recentreAfter = 0.03;     // seconds with no pitch before the reading goes back to its own delay
    constexpr double noteSeconds = 0.08;       // how long the pitch is averaged over to tell which note it is
}

NightTune::NightTune (tracktion::PluginCreationInfo info) : StockEffect (info)
{
    notes = &addParam ("notes", "Notes", 0.0f, { 0.0f, 2.0f, 1.0f },
                       [] (float v)
                       {
                           const int i = juce::roundToInt (v);
                           return i == 0 ? juce::String ("The song's key") : i == 1 ? juce::String ("Every note") : juce::String ("A key of its own");
                       });

    root = &addParam ("root", "Key", 9.0f, { 0.0f, 11.0f, 1.0f }, [] (float v) { return songkey::noteName (juce::roundToInt (v)); });
    scale = &addParam ("scale", "Scale", 1.0f, { 0.0f, (float) songkey::numScales - 1.0f, 1.0f },
                       [] (float v) { return songkey::scaleName (juce::roundToInt (v)); });

    // Speed: how fast a note is pulled in, from 400 ms (natural) to at once (robotic)
    speed = &addParam ("speed", "Speed", 0.6f, { 0.0f, 1.0f },
                       [] (float v)
                       {
                           const double s = retuneSeconds (v);
                           return s <= 0.0 ? juce::String ("Robotic") : juce::String (juce::roundToInt (s * 1000.0)) + " ms";
                       });

    amount = &addParam ("amount", "Amount", 1.0f, { 0.0f, 1.0f }, percent());
    baseDelay = baseDelayFor (sampleRate);
}

NightTune::~NightTune()
{
    notifyListenersOfDeletion();
}

double NightTune::retuneSeconds (float s)
{
    return s >= 0.995f ? 0.0 : 0.4 * std::pow (1.0 - (double) s, 2.5);
}

int NightTune::baseDelayFor (double rate)
{
    // Half the longest period (a reading drifts half a period either way before it steps), and room for the drift
    // while a step crosses over: a tenth of the longest crossfade (corrections stay within a note and a half, a
    // tenth faster or slower at most), and a little for the interpolation
    const double longest = rate / lowestHz, longestFade = rate * 0.01;
    return (int) std::ceil (longest * 0.5 + longestFade * 0.1 + 4.0);
}

double NightTune::getLatencySeconds()
{
    return baseDelay / sampleRate;
}

void NightTune::prepare()
{
    // About 24 kHz to listen at (a voice's notes are all under 1.4 kHz)
    decimation = std::max (1, (int) std::round (sampleRate / 24000.0));
    analysisRate = sampleRate / decimation;
    minLag = (int) std::floor (analysisRate / highestHz);
    maxLag = (int) std::ceil (analysisRate / lowestHz);
    window = maxLag;                                                    // a period of the lowest note (17 ms)
    hop = std::max (16, (int) std::round (analysisRate * 0.0025));      // a look every 2.5 ms

    const int ringSize = juce::nextPowerOfTwo (window + maxLag + hop + 16);
    ring.assign ((size_t) ringSize, 0.0f);
    ringMask = ringSize - 1;
    frame.assign ((size_t) (window + maxLag + 1), 0.0f);
    diff.assign ((size_t) (maxLag + 2), 0.0f);
    normalised.assign ((size_t) (maxLag + 2), 1.0f);

    // Two biquads at 4 kHz (a Butterworth pair) before keeping one sample in `decimation`
    const double cutoff = std::min (4000.0, analysisRate * 0.4);
    antiAlias[0].c = dsp::Coeffs::lowPass (cutoff, 0.5412, sampleRate);
    antiAlias[1].c = dsp::Coeffs::lowPass (cutoff, 1.3066, sampleRate);

    baseDelay = baseDelayFor (sampleRate);

    for (auto& line : lines)
        line.prepare (baseDelay * 2 + (int) std::ceil (sampleRate / lowestHz) + (int) (sampleRate * 0.01) + 16);

    reset();
}

void NightTune::reset()
{
    for (auto& f : antiAlias)
        f.reset();

    std::fill (ring.begin(), ring.end(), 0.0f);

    for (auto& line : lines)
        line.reset();

    decimationCount = ringWrite = sinceAnalysis = 0;
    hasHeard = hasPending = hasSteady = false;
    period = 0.0;
    unvoicedSamples = 0;
    target = -1;
    correctionTarget = correctionNow = 0.0;
    delay = delayNext = baseDelay;
    fadeLeft = 0;
    heardNow = -1.0f;
    targetNow = -1;

    for (auto& f : history)
        f = {};
}

std::array<bool, 12> NightTune::targetNotes() const
{
    const int from = juce::roundToInt (notes->get());

    if (from == 1)
        return songkey::notesOf (-1, 0);

    if (from == 2)
        return songkey::notesOf (juce::roundToInt (root->get()), juce::roundToInt (scale->get()));

    const int r = songRoot.load();
    return songkey::notesOf (r, songMinor.load() ? songkey::minor : songkey::major);
}

juce::String NightTune::describeTarget() const
{
    const int from = juce::roundToInt (notes->get());

    if (from == 1)
        return "every note";

    if (from == 2)
        return songkey::noteName (juce::roundToInt (root->get())) + " " + songkey::scaleName (juce::roundToInt (scale->get())).toLowerCase();

    const songkey::Key key { songRoot.load(), songMinor.load() };
    return key.known() ? key.name() + ", the song's key" : juce::String ("every note: the song's key isn't known yet");
}

void NightTune::copyHistory (Frame* dest) const
{
    const int write = historyWrite.load();

    for (int i = 0; i < historySize; ++i)
        dest[i] = history[(write + i) % historySize];
}

//==============================================================================
void NightTune::listen (float x)
{
    // Low-passed, then one sample in `decimation` into the ring; a look every hop
    const float y = antiAlias[1].process (antiAlias[0].process (x, 0), 0);

    if (++decimationCount < decimation)
        return;

    decimationCount = 0;
    ring[(size_t) ringWrite] = y;
    ringWrite = (ringWrite + 1) & ringMask;

    if (++sinceAnalysis >= hop)
    {
        sinceAnalysis = 0;
        analyse();
    }
}

void NightTune::analyse()
{
    const int needed = window + maxLag;

    for (int j = 0; j < needed; ++j)
        frame[(size_t) j] = ring[(size_t) ((ringWrite - needed + j) & ringMask)];

    // Loud enough to have a note in it (the latest window)?
    float energy = 0.0f;

    for (int j = needed - window; j < needed; ++j)
        energy += frame[(size_t) j] * frame[(size_t) j];

    double lag = 0.0;

    if (std::sqrt (energy / (float) window) > gateLevel)
    {
        // YIN: the difference between the latest window and the sound `tau` earlier, each normalised by the mean of
        // those before it. (The latest: what's heard is as near as it can be to what's being read back to correct)
        const float* latest = frame.data() + maxLag;

        for (int tau = 1; tau <= maxLag; ++tau)
        {
            const float* earlier = latest - tau;
            float sum = 0.0f;

            for (int j = 0; j < window; ++j)
            {
                const float d = latest[j] - earlier[j];
                sum += d * d;
            }

            diff[(size_t) tau] = sum;
        }

        double running = 0.0;

        for (int tau = 1; tau <= maxLag; ++tau)
        {
            running += diff[(size_t) tau];
            normalised[(size_t) tau] = running > 0.0 ? (float) (diff[(size_t) tau] * tau / running) : 1.0f;
        }

        // The first dip under the threshold (to the bottom of it), else the deepest
        int best = -1;

        for (int tau = std::max (2, minLag); tau < maxLag; ++tau)
            if (normalised[(size_t) tau] < yinThreshold)
            {
                while (tau + 1 < maxLag && normalised[(size_t) tau + 1] < normalised[(size_t) tau])
                    ++tau;

                best = tau;
                break;
            }

        if (best < 0)
        {
            best = std::max (2, minLag);

            for (int tau = best + 1; tau < maxLag; ++tau)
                if (normalised[(size_t) tau] < normalised[(size_t) best])
                    best = tau;
        }

        if (normalised[(size_t) best] < voicedBelow)
        {
            // Between lags: a parabola through the difference around the dip (it's the period's own curve)
            const double a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
            const double bend = a - 2.0 * b + c;
            lag = best + (bend > 0.0 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / bend) : 0.0);
        }
    }

    if (lag > 0.0)
    {
        const double note = 69.0 + 12.0 * std::log2 (analysisRate / lag / 440.0);

        // A jump of more than a semitone is believed when it's heard twice running (an octave slip once is not)
        if (hasHeard && std::abs (note - heard) > believeJump && (! hasPending || std::abs (note - pending) > 0.5))
        {
            pending = note;
            hasPending = true;
        }
        else
        {
            heard = note;
            hasHeard = true;
            period = lag * decimation;
            hasPending = false;
        }

        unvoicedSamples = 0;
    }
    else
    {
        hasHeard = hasPending = false;
        period = 0.0;
    }

    // Which note it's singing: the pitch over the last 80 ms or so (a vibrato's swing, or a moment's wobble, isn't a new
    // note), at once for a jump to another
    if (hasHeard)
    {
        if (! hasSteady || std::abs (heard - steady) > believeJump)
            steady = heard;
        else
            steady += (heard - steady) * (1.0 - std::exp (-(double) hop / analysisRate / noteSeconds));

        hasSteady = true;
    }
    else
    {
        hasSteady = false;
    }

    // The note to pull to: the nearest of the key's, staying on the one it's on until another is clearly nearer; the
    // correction is from where the pitch is now, so a vibrato is ironed flat at the robotic speed
    if (hasHeard)
    {
        const auto allowed = targetNotes();
        int nearest = -1;
        double nearestDistance = 1.0e9;

        for (int k = (int) std::floor (steady) - 7; k <= (int) std::ceil (steady) + 7; ++k)
            if (allowed[(size_t) (((k % 12) + 12) % 12)] && std::abs (k - steady) < nearestDistance)
            {
                nearest = k;
                nearestDistance = std::abs (k - steady);
            }

        if (target >= 0 && allowed[(size_t) (target % 12)] && std::abs (target - steady) < nearestDistance + stayOn && std::abs (target - steady) < 1.7)
            nearest = target;

        target = nearest;
        correctionTarget = target >= 0 ? target - heard : 0.0;
    }
    else
    {
        target = -1;
        correctionTarget = 0.0;
    }

    heardNow = hasHeard ? (float) heard : -1.0f;
    targetNow = target;

    const int w = historyWrite.load();
    history[w] = { hasHeard ? (float) heard : -1.0f, hasHeard ? (float) (heard + correctionNow) : -1.0f };
    historyWrite = (w + 1) % historySize;
}

void NightTune::startStep (double to, int length, bool equalPower)
{
    delayNext = to;
    fadeLength = fadeLeft = std::max (1, length);
    fadePower = equalPower;
}

void NightTune::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    float* left = nullptr;
    float* right = nullptr;

    // (not set up yet: initialise() hasn't been called, so there's nothing to listen or read back with)
    if (ring.empty() || ! getStereo (fc, left, right))
        return;

    // How far, and how fast: at once for the robotic sound, else a glide of Speed's time
    const double share = juce::jlimit (0.0f, 1.0f, amount->get());
    const double seconds = retuneSeconds (speed->get());
    const double glide = seconds <= 0.0 ? 1.0 : 1.0 - std::exp (-1.0 / (seconds * sampleRate));
    const int recentre = (int) (recentreAfter * sampleRate);
    const int longestFade = (int) (sampleRate * 0.01), shortestFade = (int) (sampleRate * 0.002);

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        const float l = left[i], r = right != nullptr ? right[i] : l;
        listen (right != nullptr ? 0.5f * (l + r) : l);

        if (period <= 0.0)
            unvoicedSamples = std::min (unvoicedSamples + 1, 1 << 30);

        correctionNow += (correctionTarget * share - correctionNow) * glide;
        const double ratio = correctionNow == 0.0 ? 1.0 : std::exp2 (correctionNow / 12.0);

        lines[0].push (l);
        lines[1].push (r);

        // Read back `delay` behind (a delay of 1 is the sample just pushed), crossing to the next reading in a step
        float outL, outR;

        if (fadeLeft > 0)
        {
            const float g = 1.0f - (float) fadeLeft / (float) fadeLength;
            const float a = fadePower ? std::cos (g * juce::MathConstants<float>::halfPi) : 1.0f - g;
            const float b = fadePower ? std::sin (g * juce::MathConstants<float>::halfPi) : g;
            outL = a * lines[0].read (delay + 1.0) + b * lines[0].read (delayNext + 1.0);
            outR = a * lines[1].read (delay + 1.0) + b * lines[1].read (delayNext + 1.0);
            delayNext += 1.0 - ratio;

            if (--fadeLeft == 0)
                delay = delayNext;
        }
        else
        {
            outL = lines[0].read (delay + 1.0);
            outR = lines[1].read (delay + 1.0);
        }

        // Reading faster the delay shrinks, slower it grows
        delay += 1.0 - ratio;

        if (fadeLeft == 0)
        {
            const double off = delay - baseDelay;

            if (period > 0.0 && std::abs (off) > 0.5 * period)
                startStep (delay - std::round (off / period) * period, juce::jlimit (shortestFade, longestFade, (int) period), false);
            else if (period <= 0.0 && unvoicedSamples >= recentre && std::abs (off) >= 0.5)
                startStep (baseDelay, longestFade, true);       // no note: not the same sound a period on, so equal power
        }

        left[i] = outL;

        if (right != nullptr)
            right[i] = outR;
    }
}

} // namespace nightshift
