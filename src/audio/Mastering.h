// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"
#include "fx/Dsp.h"

/** Mastering, round one: how loud a song is (Tracktion's EBU R128 LoudnessMeter), a limiter that
    makes it louder without anything going over a true-peak ceiling (Maximizer, the master
    limiter's insides), and working out how hard to push the mix into it to reach a target
    loudness (findPush), from a capture of the mix as it goes in. */
namespace nightshift::mastering
{
    /** What a song can aim for, in plain words, with the loudness it means. */
    struct Target
    {
        const char* id;
        const char* name;
        const char* detail;
        float lufs;                 // 0 for "safe": no loudness to aim for
        float ceilingDb;
    };

    const std::vector<Target>& targets();
    const Target* findTarget (const juce::String& id);

    using Readings = tracktion::LoudnessMeter::Readings;

    //==============================================================================
    /** The limiter at the end of the mix: a gain into it, then nothing out above the ceiling,
        measured between the samples too - 4x oversampled with the very filters the loudness
        meter's true peak uses (JUCE's half-band equiripple FIRs), so the ceiling holds as the
        meter reads it. 3 ms of lookahead lets it turn down before a peak arrives; it lets go
        quickly after a lone hit and slowly when it's working hard all the time, so a pushed mix
        doesn't pump. Stereo linked. Real-time safe once prepared. */
    class Maximizer
    {
    public:
        Maximizer() = default;

        /** Allocates: never while processing. */
        void prepare (double sampleRate);
        void reset() noexcept;

        /** The gain into it, in dB, eased to over 10 ms so a move never clicks (`now`: at once). */
        void setGainDb (float db, bool now = false) noexcept;
        void setCeilingDb (float db) noexcept           { ceiling = juce::Decibels::decibelsToGain (db); }

        /** Off, it only applies the gain and the delay: the mix as it goes in. */
        void setLimiting (bool on) noexcept             { limiting = on; }

        int getLatencySamples() const noexcept          { return detectDelay + lookahead - 1; }

        /** In place; `right` may be null for mono. */
        void process (float* left, float* right, int numSamples) noexcept;

        /** How far it turned down in the last process() call, in dB (0 when it didn't). */
        float getGainReductionDb() const noexcept       { return reductionDb.load(); }

    private:
        static constexpr int maxChunk = 1024;

        double sampleRate = 48000.0;
        int lookahead = 144;

        // The peaks: the input oversampled 4x, each sample's four points' loudest, and the last
        // four of those (around the sample detectDelay back, the one whose gain is worked out)
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
        juce::AudioBuffer<float> scratch;
        std::vector<float> peaks;
        float recentPeaks[4] {};
        int detectDelay = 5;

        std::vector<float> delayL, delayR;              // the audio, waiting while the gain gets ready
        int delayPos = 0;

        // The smallest gain needed across the lookahead window: a monotonic queue in a ring
        std::vector<juce::int64> queueIndex;
        std::vector<float> queueGain;
        int queueHead = 0, queueSize = 0;
        juce::int64 sampleIndex = 0;

        std::vector<float> held;                        // the released gain over the window, averaged
        int heldPos = 0;
        double heldSum = 0.0;

        float ceiling = 0.891f, released = 1.0f;
        float gain = 1.0f, targetGain = 1.0f, gainStep = 0.0f;
        double fastRelease = 0.0, slowRelease = 0.0, workingSmooth = 0.0, workingCoeff = 0.0, working = 1.0;
        bool limiting = true;
        std::atomic<float> reductionDb { 0.0f };

        void processChunk (float* left, float* right, int numSamples) noexcept;
    };

    //==============================================================================
    /** The Master tab's shaping, before the limiter, in that order: Tone (a four-band EQ whose
        bands can each work on everything, the centre or the sides), Punch (lows, mids and highs
        compressed on their own, each level kept where it was on average, so it tightens rather
        than turns down) and Width (the sides louder or quieter, and the bass kept in the middle).
        Settings are Song::Mastering's; anything left at its default costs nothing. Real-time safe
        once prepared. */
    class Shaper
    {
    public:
        /** The tone bands' names, frequencies and filters (for the panel's curve too). */
        static constexpr int numToneBands = 4, numPunchBands = 3;
        static const char* toneBandName (int band);
        static const char* punchBandName (int band);
        static dsp::Coeffs toneCoeffs (int band, float gainDb, double sampleRate);

        void prepare (double sampleRate);
        void reset() noexcept;

        /** From the processing thread: the settings it works to from now on. */
        void set (const Song::Mastering&) noexcept;

        void process (float* left, float* right, int numSamples) noexcept;

        /** How far each punch band turned its loudest moments down in the last block, in dB. */
        float getPunchReductionDb (int band) const noexcept     { return punchReduction[(size_t) band].load(); }

    private:
        struct PunchBand
        {
            double attack = 0.0, release = 0.0;
            double powerIn = 0.0, powerOut = 0.0;       // a few seconds' average, before and after
            float envelope = 0.0f, thresholdDb = 0.0f, slope = 0.0f, makeup = 1.0f;
        };

        double sampleRate = 48000.0;
        Song::Mastering settings;
        bool toneActive = false, punchActive = false, widthActive = false;
        bool anyCentreOrSides = false;

        dsp::Biquad tone[numToneBands];
        juce::dsp::LinkwitzRileyFilter<float> lowSplit, highSplit, lowAlign, sideHighPass;
        PunchBand punch[numPunchBands];
        double averageCoeff = 0.0;
        std::array<std::atomic<float>, numPunchBands> punchReduction {};

        void processPunch (float& l, float& r, float* lowest) noexcept;
    };

    /** Whether a song's settings shape it at all (Tone, Punch or Width away from doing nothing). */
    bool isShaping (const Song::Mastering&);

    //==============================================================================
    /** How loud an audio file is (the whole of it), as the meter measures a song. */
    Readings measureFile (const juce::File&);

    /** How hard to push a mix (captured as it goes into the limiter, see AudioEngine::captureMix)
        so it comes out at `targetLufs`, with nothing over `ceilingDb`. */
    struct Push
    {
        bool ok = false;
        juce::String problem;           // why not, in plain words
        float pushDb = 0.0f;
        float beforeLufs = -100.0f;     // the mix as it was
        float afterLufs = -100.0f;      // with the push and the limiter
        float truePeakDb = -100.0f;
        float squeezeDb = 0.0f;         // the most the limiter turned it down anywhere
        bool reachedTarget = false;     // false: as loud as it would go without wrecking it
        bool held = false;              // stopped short by maxSqueezeDb: louder would have squeezed the loud parts past it
    };

    /** Tries pushes until the loudness lands within 0.1 LU of the target (a few passes over the
        capture). `progress` (0..1) is called as it goes; returning false stops it. With `shape`,
        the capture is a raw one (AudioEngine::captureMix's) and goes through that shaping first.
        No push squeezes the loudest peak more than `maxSqueezeDb` (a song with quiet parts: past
        that, a loudness target is met only by raising the quiet parts to the loud ones). */
    Push findPush (const juce::File& capture, float targetLufs, float ceilingDb,
                   const std::function<bool (float)>& progress = nullptr, const Song::Mastering* shape = nullptr,
                   float maxSqueezeDb = 100.0f);
}
