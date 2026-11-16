// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Metronome.h"
#include "DrumSynth.h"
#include "../model/Song.h"

namespace nightshift
{

namespace metronome
{
    const juce::StringArray& soundNames()
    {
        static const juce::StringArray names { "Classic", "Woodblock", "Sticks", "Clave", "Cowbell", "Hi-hat", "Beep" };
        return names;
    }

    const std::vector<Rhythm>& rhythms()
    {
        static const std::vector<Rhythm> list = {
            { "Beats",           { 0.0 } },
            { "2 a beat",        { 0.0, 0.5 } },
            { "Triplets",        { 0.0, 1.0 / 3.0, 2.0 / 3.0 } },
            { "4 a beat",        { 0.0, 0.25, 0.5, 0.75 } },
            { "5 a beat",        { 0.0, 0.2, 0.4, 0.6, 0.8 } },
            { "6 a beat",        { 0.0, 1.0 / 6.0, 2.0 / 6.0, 3.0 / 6.0, 4.0 / 6.0, 5.0 / 6.0 } },
            { "Swing",           { 0.0, 2.0 / 3.0 } },
            { "Dotted",          { 0.0, 0.75 } },
            { "Gallop",          { 0.0, 0.5, 0.75 } },
            { "Reverse gallop",  { 0.0, 0.25, 0.5 } }
        };

        return list;
    }

    juce::String Signature::grouping() const
    {
        juce::StringArray parts;

        for (auto g : groups)
            parts.add (juce::String (g));

        return parts.joinIntoString ("+");
    }

    juce::String Signature::name() const
    {
        return groups.empty() ? meter() : meter() + "  " + grouping();
    }

    const std::vector<Signature>& signatures()
    {
        // The song's time signatures (Song::meters), the odd ones in each of their usual groupings
        static const std::vector<Signature> list = {
            { 4, 4, {} }, { 3, 4, {} }, { 2, 4, {} },
            { 5, 4, { 3, 2 } }, { 5, 4, { 2, 3 } },
            { 6, 4, {} }, { 6, 4, { 3, 3 } },
            { 7, 4, { 4, 3 } }, { 7, 4, { 3, 4 } }, { 7, 4, { 2, 2, 3 } },
            { 6, 8, { 3, 3 } },
            { 9, 8, { 3, 3, 3 } }, { 9, 8, { 2, 2, 2, 3 } },
            { 12, 8, { 3, 3, 3, 3 } },
            { 5, 8, { 3, 2 } }, { 5, 8, { 2, 3 } },
            { 7, 8, { 2, 2, 3 } }, { 7, 8, { 3, 2, 2 } }, { 7, 8, { 2, 3, 2 } },
            { 11, 8, { 3, 3, 3, 2 } }, { 11, 8, { 2, 2, 3, 2, 2 } },
            { 13, 8, { 3, 3, 3, 2, 2 } }, { 13, 8, { 2, 2, 2, 3, 2, 2 } },
            { 7, 16, { 2, 2, 3 } }, { 7, 16, { 3, 2, 2 } },
            { 13, 16, { 3, 3, 3, 2, 2 } },
            { 15, 16, { 3, 3, 3, 3, 3 } }, { 15, 16, { 4, 4, 4, 3 } }
        };

        return list;
    }

    Signature find (int beats, int unit, const juce::String& grouping)
    {
        const Signature* first = nullptr;

        for (auto& s : signatures())
        {
            if (s.beats != beats || s.unit != unit)
                continue;

            if (s.grouping() == grouping)
                return s;

            if (first == nullptr)
                first = &s;
        }

        return first != nullptr ? *first : Signature { beats, unit, {} };
    }

    std::vector<int> defaultAccents (const Signature& s)
    {
        std::vector<int> accents ((size_t) juce::jlimit (1, maxBeats, s.beats), normal);
        accents[0] = strong;

        if (s.groups.empty())
            return accents;

        std::fill (accents.begin() + 1, accents.end(), soft);
        int at = 0;

        for (auto g : s.groups)
        {
            if (at > 0 && at < (int) accents.size())
                accents[(size_t) at] = normal;

            at += g;
        }

        return accents;
    }

    juce::String accentsToText (const std::vector<int>& accents)
    {
        juce::String text;

        for (auto a : accents)
            text << juce::jlimit (0, 3, a);

        return text;
    }

    std::vector<int> accentsFromText (const juce::String& text, int beats)
    {
        std::vector<int> accents;

        for (auto c : text)
            if (c >= '0' && c <= '3' && (int) accents.size() < beats)
                accents.push_back ((int) (c - '0'));

        while ((int) accents.size() < beats)
            accents.push_back (normal);

        return accents;
    }
}

//==============================================================================
namespace
{
    using namespace drumsynth;

    /** A hit before levelling: variant 0 the downbeat's (higher, brighter), 1 a beat's, 2 a
        pulse between beats (lower, shorter). */
    juce::AudioBuffer<float> synthesise (int sound, int variant, double sr)
    {
        const bool down = variant == 0, between = variant == 2;

        switch (sound)
        {
            case 1:     // Woodblock: two wooden modes and the knock of the stick
            {
                const double hz = 880.0 * (down ? 1.33 : between ? 0.8 : 1.0);
                Noise noise (11 + variant);
                auto knock = Filter::bandPass (sr, hz * 3.1, 1.5);

                return generate (sr, 0.15, [&] (double t)
                {
                    const double body = std::sin (twoPi * hz * t) * decay (t, between ? 0.018 : 0.028)
                                      + 0.45 * std::sin (twoPi * hz * 2.37 * t) * decay (t, 0.011);
                    return (float) body + knock (noise()) * (float) decay (t, 0.0015) * 0.6f;
                });
            }

            case 2:     // Sticks: two drumsticks clicked together
            {
                Noise noise (23 + variant);
                auto band = Filter::bandPass (sr, down ? 3600.0 : between ? 2600.0 : 3000.0, 1.3);
                const double a = down ? 2350.0 : between ? 1800.0 : 2050.0, b = a * 1.47;

                return generate (sr, 0.08, [&] (double t)
                {
                    const double modes = 0.6 * std::sin (twoPi * a * t) * decay (t, 0.010) + 0.4 * std::sin (twoPi * b * t) * decay (t, 0.007);
                    return (float) modes + band (noise()) * (float) decay (t, 0.003) * 1.2f;
                });
            }

            case 3:     // Clave: one hard wooden ring
            {
                const double hz = down ? 3000.0 : between ? 2100.0 : 2500.0;

                return generate (sr, 0.12, [&] (double t)
                {
                    return (float) (std::sin (twoPi * hz * t) * decay (t, between ? 0.014 : 0.022) * juce::jmin (1.0, t / 0.0003));
                });
            }

            case 4:     // Cowbell: the 808's two squares, the downbeat a third up
            {
                const double scale = down ? 1.26 : between ? 0.89 : 1.0;
                BlepSquare a (540.0 * scale, sr), b (800.0 * scale, sr);
                auto band = Filter::bandPass (sr, 2640.0 * scale, 1.6);
                auto body = Filter::lowPass (sr, 3000.0);
                const double tail = between ? 0.04 : 0.1;

                return generate (sr, 0.4, [&] (double t)
                {
                    const float sq = (a.next() + b.next()) * 0.5f;
                    const double env = 0.65 * decay (t, 0.009) + 0.35 * decay (t, tail);
                    return (band (sq) + body (sq) * 0.5f) * (float) env;
                });
            }

            case 5:     // Hi-hat: the 808's six squares of metal, half open on the downbeat
            {
                std::vector<BlepSquare> metal;

                for (double hz : { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 })
                    metal.emplace_back (hz, sr);

                Noise noise (37 + variant);
                auto band1 = Filter::bandPass (sr, 3440.0, 1.2), band2 = Filter::bandPass (sr, 7100.0, 1.4);
                auto cut1 = Filter::highPass (sr, 7000.0), cut2 = Filter::highPass (sr, 7000.0);
                const double tau = down ? 0.11 : between ? 0.02 : 0.035;

                return generate (sr, tau * 6.0, [&] (double t)
                {
                    float m = 0.0f;

                    for (auto& o : metal)
                        m += o.next() / 6.0f;

                    const float body = band1 (m) * 0.6f + band2 (m) + noise() * 0.12f;
                    return cut2 (cut1 (body)) * (float) (decay (t, tau) * juce::jmin (1.0, t / 0.0002));
                });
            }

            case 6:     // Beep: a digital square, an octave up on the downbeat
            {
                BlepSquare square (down ? 2000.0 : 1000.0, sr);
                auto soften = Filter::lowPass (sr, 7000.0);
                const double length = down ? 0.06 : between ? 0.025 : 0.045;

                return generate (sr, length + 0.005, [&] (double t)
                {
                    const double env = juce::jmin (1.0, t / 0.002) * juce::jlimit (0.0, 1.0, (length - t) / 0.003);
                    return soften (square.next()) * (float) env;
                });
            }

            default:    // Classic: the electronic metronome's beep, a fifth up on the downbeat
            {
                const double hz = 1046.5 * (down ? 1.5 : between ? 0.75 : 1.0);

                return generate (sr, 0.1, [&] (double t)
                {
                    const double env = juce::jmin (1.0, t / 0.0005) * decay (t, between ? 0.012 : 0.02);
                    return (float) ((std::sin (twoPi * hz * t) + 0.2 * std::sin (twoPi * 2.0 * hz * t)) * env);
                });
            }
        }
    }

    /** Peak to `peakDb`, the ring-out cut where it's 60 dB down, with a short fade. */
    void finish (juce::AudioBuffer<float>& b, float peakDb, double sr)
    {
        normalise (b, peakDb);
        const float floor = juce::Decibels::decibelsToGain (peakDb - 60.0f);
        int last = b.getNumSamples() - 1;

        while (last > 0 && std::abs (b.getSample (0, last)) < floor)
            --last;

        b.setSize (1, std::min (b.getNumSamples(), last + (int) (sr * 0.004)), true);
        fadeOut (b, sr, 3.0);
    }

    constexpr float strongDb = -6.0f, normalDb = -9.0f, betweenDb = -13.0f;
    constexpr float softGain = 0.45f, softBetweenGain = 0.6f;
}

//==============================================================================
const char* MetronomePlugin::xmlTypeName = "nightshiftMetronome";

MetronomePlugin::MetronomePlugin (tracktion::PluginCreationInfo info)
    : tracktion::Plugin (info)
{
    const auto accents = metronome::defaultAccents (metronome::find (4, 4));
    std::copy (accents.begin(), accents.end(), pending.accents.begin());
    active = pending;
}

MetronomePlugin::~MetronomePlugin()
{
    notifyListenersOfDeletion();
}

float MetronomePlugin::volumeToGain (float volume)
{
    return volume <= 0.001f ? 0.0f : juce::Decibels::decibelsToGain (40.0f * std::log10 (volume / 0.8f));
}

juce::AudioBuffer<float> MetronomePlugin::makeHit (int sound, int accent, double sr)
{
    const int variant = accent == metronome::strong ? 0 : accent == metronome::off ? 2 : 1;
    auto hit = synthesise (juce::jlimit (0, metronome::soundNames().size() - 1, sound), variant, sr);
    finish (hit, variant == 0 ? strongDb : variant == 2 ? betweenDb : normalDb, sr);

    if (accent == metronome::soft)
        hit.applyGain (softGain);

    return hit;
}

void MetronomePlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    // Made once per sample rate: a graph rebuild initialises it again, and mustn't touch
    // hits the audio thread may be playing
    if (hits.empty() || sampleRate != info.sampleRate)
    {
        sampleRate = info.sampleRate;
        hits.clear();

        for (int s = 0; s < metronome::soundNames().size(); ++s)
            hits.push_back ({ makeHit (s, metronome::strong, sampleRate), makeHit (s, metronome::normal, sampleRate),
                              makeHit (s, metronome::off, sampleRate) });

        reset();
    }
}

void MetronomePlugin::reset()
{
    for (auto& v : voices)
        v = {};
}

void MetronomePlugin::setSettings (const Settings& s)
{
    const juce::SpinLock::ScopedLockType lock (settingsLock);
    pending = s;
}

MetronomePlugin::Settings MetronomePlugin::getSettings() const
{
    const juce::SpinLock::ScopedLockType lock (settingsLock);
    return pending;
}

void MetronomePlugin::setCountIn (double fromSeconds, double toSeconds)
{
    countInTo = fromSeconds;        // empty while the two change
    countInFrom = fromSeconds;
    countInTo = toSeconds;
}

void MetronomePlugin::setRecording (bool isRecording)
{
    recording = isRecording;
}

void MetronomePlugin::setPractising (bool shouldPractise)
{
    practising = shouldPractise;
}

MetronomePlugin::Pulse MetronomePlugin::getLastPulse() const
{
    return { lastBeat.load(), lastPulse.load(), pulseCount.load() };
}

void MetronomePlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0 || hits.empty())
        return;

    {
        const juce::SpinLock::ScopedTryLockType lock (settingsLock);

        if (lock.isLocked())
            active = pending;
    }

    auto& buffer = *fc.destBuffer;
    const int start = fc.bufferStartSample, numSamples = fc.bufferNumSamples;

    // Its input is silence: it only adds its clicks to what the master plays
    buffer.clear (start, numSamples);

    if (fc.isPlaying)
    {
        wasPractising = false;
        schedule (fc.editTime.getStart().inSeconds(), fc.editTime.getEnd().inSeconds(), numSamples, true);
    }
    else if (practising.load())
    {
        // From a downbeat, the moment it starts
        if (! std::exchange (wasPractising, true))
            practiceSamples = 0;

        const double from = (double) practiceSamples / sampleRate;
        practiceSamples += numSamples;
        schedule (from, (double) practiceSamples / sampleRate, numSamples, false);
    }
    else
    {
        wasPractising = false;
    }

    render (buffer, start, numSamples);
}

void MetronomePlugin::schedule (double from, double to, int numSamples, bool playing)
{
    const auto& s = active;

    if (to <= from || s.beats <= 0 || s.bpm <= 0.0)
        return;

    const double cFrom = countInFrom.load(), cTo = countInTo.load();
    const bool countIn = playing && cTo > cFrom;
    const bool clicking = ! playing || (s.on && (! s.recordingOnly || recording.load()));

    if (! clicking && ! countIn)
        return;

    const auto& rhythmList = metronome::rhythms();
    const auto& pulses = rhythmList[(size_t) juce::jlimit (0, (int) rhythmList.size() - 1, s.rhythm)].pulses;
    const auto& sound = hits[(size_t) juce::jlimit (0, (int) hits.size() - 1, s.sound)];
    const float level = volumeToGain (s.volume);

    // A pulse at `t` seconds: the beat it's on in the bar, how that beat is accented, and which
    // of the beat's pulses it is (0: the beat itself)
    auto click = [&] (double t, int barBeat, int accent, size_t k)
    {
        const bool inCountIn = countIn && t >= cFrom - 1.0e-6 && t < cTo - 1.0e-6;

        if ((! clicking && ! inCountIn) || accent == metronome::off)
            return;

        const bool onBeat = k == 0;
        const auto& hit = onBeat ? sound[accent == metronome::strong ? 0 : 1] : sound[2];
        const float gain = level * (accent == metronome::soft ? (onBeat ? softGain : softBetweenGain) : 1.0f);
        const int delay = juce::jlimit (0, numSamples - 1, (int) std::llround ((t - from) * sampleRate));

        startVoice (hit, gain, delay);
        lastBeat = barBeat;
        lastPulse = (int) k;
        ++pulseCount;
    };

    if (playing && s.numSections > 0)
    {
        // The song's beats, where its tempo map puts them (Tracktion's, as the notes play): this
        // block's span in ticks, then each stretch of one time signature over it, its bars from
        // its start. Read on the audio thread, as Tracktion's own plugins do; it's swapped under
        // the audio callback lock
        const auto& tempo = edit.tempoSequence.getInternalSequence();
        const double unitTicks = ticksPerBeat * 4.0 / juce::jmax (1, s.baseUnit);
        auto toTicks = [&] (double seconds) { return tempo.toBeats (tracktion::TimePosition::fromSeconds (seconds)).inBeats() * unitTicks; };
        auto toSeconds = [&] (double ticks) { return tempo.toTime (tracktion::BeatPosition::fromBeats (ticks / unitTicks)).inSeconds(); };
        const double t0 = toTicks (from), t1 = toTicks (to);

        for (int i = 0; i < s.numSections; ++i)
        {
            const auto& section = s.sections[(size_t) i];
            const double sectionEnd = i + 1 < s.numSections ? (double) s.sections[(size_t) i + 1].start : 1.0e15;
            const double lo = i == 0 ? t0 : std::max (t0, (double) section.start), hi = std::min (t1, sectionEnd);

            if (lo >= hi)
                continue;

            const double beatTicks = ticksPerBeat * 4.0 / juce::jmax (1, section.unit);
            const int beats = juce::jlimit (1, metronome::maxBeats, section.beats);

            for (auto beat = (juce::int64) std::floor ((lo - section.start) / beatTicks); ; ++beat)
            {
                const double beatTick = section.start + (double) beat * beatTicks;

                if (beatTick >= hi)
                    break;

                const int barBeat = (int) (((beat % beats) + beats) % beats);

                for (size_t k = 0; k < pulses.size(); ++k)
                    if (const double at = beatTick + pulses[k] * beatTicks; at >= lo && at < hi)
                        click (toSeconds (at), barBeat, section.accents[(size_t) barBeat], k);
            }
        }

        return;
    }

    // Practising: steady, at its own tempo. Beats of the time signature (eighths in 7/8) a
    // second; the tempo is in quarters
    const double beatsPerSecond = s.bpm / 60.0 * s.unit / 4.0;
    const double b0 = from * beatsPerSecond, b1 = to * beatsPerSecond;
    const int beats = juce::jlimit (1, metronome::maxBeats, s.beats);

    for (auto beat = (juce::int64) std::floor (b0); beat <= (juce::int64) std::floor (b1); ++beat)
    {
        const int barBeat = (int) (((beat % beats) + beats) % beats);

        for (size_t k = 0; k < pulses.size(); ++k)
            if (const double at = (double) beat + pulses[k]; at >= b0 && at < b1)
                click (at / beatsPerSecond, barBeat, s.accents[(size_t) barBeat], k);
    }
}

void MetronomePlugin::startVoice (const juce::AudioBuffer<float>& sample, float gain, int delay)
{
    if (gain <= 0.0f)
        return;

    // A free voice, else the one furthest through its hit
    auto* chosen = &voices[0];

    for (auto& v : voices)
    {
        if (v.sample == nullptr)
        {
            chosen = &v;
            break;
        }

        if (v.position > chosen->position)
            chosen = &v;
    }

    *chosen = { &sample, 0, delay, gain };
}

void MetronomePlugin::render (juce::AudioBuffer<float>& buffer, int start, int numSamples)
{
    const float pan = juce::jlimit (-1.0f, 1.0f, active.pan);
    const float gainLeft = std::min (1.0f, 1.0f - pan), gainRight = std::min (1.0f, 1.0f + pan);
    float* left = buffer.getWritePointer (0, start);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : nullptr;

    for (auto& v : voices)
    {
        if (v.sample == nullptr)
            continue;

        const float* src = v.sample->getReadPointer (0);
        const int length = v.sample->getNumSamples();
        int i = std::exchange (v.delay, 0);

        for (; i < numSamples && v.position < length; ++i, ++v.position)
        {
            const float x = src[v.position] * v.gain;

            if (right != nullptr)
            {
                left[i] += x * gainLeft;
                right[i] += x * gainRight;
            }
            else
            {
                left[i] += x;
            }
        }

        if (v.position >= length)
            v.sample = nullptr;
    }
}

} // namespace nightshift
