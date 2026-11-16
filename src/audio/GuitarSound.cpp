// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "GuitarSound.h"
#include <complex>

namespace nightshift
{

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    // An eight-string's range, so drop tunings and seven and eight strings have a string to go on:
    // F#1 B1 E2 A2 D3 G3 B3 E4
    constexpr int stringCount = 8;
    constexpr int openPitch[stringCount] { 30, 35, 40, 45, 50, 55, 59, 64 };

    //==============================================================================
    // A biquad (Robert Bristow-Johnson's cookbook), transposed direct form II
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void clear() noexcept       { z1 = z2 = 0.0f; }

        void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
        {
            b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
            a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
        }

        static Biquad lowPass (double sr, double hz, double q)
        {
            const double w = twoPi * juce::jlimit (10.0, sr * 0.45, hz) / sr, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            Biquad f;
            f.set ((1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
            return f;
        }

        static Biquad highPass (double sr, double hz, double q)
        {
            const double w = twoPi * juce::jlimit (5.0, sr * 0.45, hz) / sr, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            Biquad f;
            f.set ((1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
            return f;
        }

        static Biquad peak (double sr, double hz, double q, double db)
        {
            const double A = std::pow (10.0, db / 40.0);
            const double w = twoPi * juce::jlimit (10.0, sr * 0.45, hz) / sr, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            Biquad f;
            f.set (1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
            return f;
        }

        static Biquad lowShelf (double sr, double hz, double db)
        {
            const double A = std::pow (10.0, db / 40.0);
            const double w = twoPi * hz / sr, c = std::cos (w), s = std::sin (w), alpha = s / 2.0 * std::sqrt (2.0), sa = 2.0 * std::sqrt (A) * alpha;
            Biquad f;
            f.set (A * ((A + 1.0) - (A - 1.0) * c + sa), 2.0 * A * ((A - 1.0) - (A + 1.0) * c), A * ((A + 1.0) - (A - 1.0) * c - sa),
                   (A + 1.0) + (A - 1.0) * c + sa, -2.0 * ((A - 1.0) + (A + 1.0) * c), (A + 1.0) + (A - 1.0) * c - sa);
            return f;
        }

        static Biquad highShelf (double sr, double hz, double db)
        {
            const double A = std::pow (10.0, db / 40.0);
            const double w = twoPi * juce::jlimit (10.0, sr * 0.45, hz) / sr, c = std::cos (w), s = std::sin (w), alpha = s / 2.0 * std::sqrt (2.0),
                         sa = 2.0 * std::sqrt (A) * alpha;
            Biquad f;
            f.set (A * ((A + 1.0) + (A - 1.0) * c + sa), -2.0 * A * ((A - 1.0) + (A + 1.0) * c), A * ((A + 1.0) + (A - 1.0) * c - sa),
                   (A + 1.0) - (A - 1.0) * c + sa, 2.0 * ((A - 1.0) - (A + 1.0) * c), (A + 1.0) - (A - 1.0) * c - sa);
            return f;
        }
    };

    //==============================================================================
    // The sounds: the guitar (how bright the pick, how long it rings, where it's picked and
    // where the pickup sits), what goes into the amp, the amp, the cabinet, the room
    struct Preset
    {
        const char* name;
        const char* about;
        float brightness, sustain, pickAt, pickupAt, pickupHz, pickupQ;
        float compressDb, compressRatio, highPassHz, pushHz, pushDb;
        bool gate;
        float driveDb;
        int stages;
        float asymmetry, stageCutHz;
        float bassDb, midHz, midDb, trebleDb, presenceDb;
        int cabinet;                // 0 open-back 1x12, 1 closed 4x12, 2 a dark jazz combo
        float chorus, wobble, room;
        bool doubled;
        float levelDb;              // where it sits: about Nova's -18, measured
    };

    const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> list {
            { "Clean",      "A bright single-coil, lightly compressed, straight into a clean amp",
              0.75f, 1.0f, 0.13f, 0.11f, 5500.0f, 1.1f,   -20.0f, 3.0f, 0.0f,   0.0f,   0.0f, false,  4.0f, 1, 0.05f, 9000.0f,
              1.0f, 700.0f, -1.5f, 2.0f, 1.0f,   0, 0.06f, 0.0f, 0.14f, false, -15.8f },
            { "Jazz Box",   "A hollow body on the neck pickup, flatwound strings, warm and round",
              0.32f, 0.55f, 0.20f, 0.27f, 2200.0f, 0.8f,  -22.0f, 2.0f, 0.0f,   0.0f,   0.0f, false,  6.0f, 1, 0.08f, 6000.0f,
              3.0f, 500.0f, 1.0f, -6.0f, -3.0f,  2, 0.0f, 0.0f, 0.18f, false, -14.8f },
            { "Funk Clean", "Single coils, bright and snappy, squeezed by a compressor",
              0.9f, 0.7f, 0.10f, 0.15f, 6000.0f, 1.6f,    -28.0f, 6.0f, 0.0f,   0.0f,   0.0f, false,  3.0f, 1, 0.02f, 10000.0f,
              -2.0f, 800.0f, -3.0f, 4.0f, 3.0f,  0, 0.0f, 0.0f, 0.08f, false, -10.9f },
            { "Lofi Clean", "A warm neck pickup through chorus and a wobbly old tape",
              0.45f, 0.8f, 0.18f, 0.24f, 2800.0f, 0.9f,   -22.0f, 3.0f, 0.0f,   0.0f,   0.0f, false,  8.0f, 1, 0.10f, 5000.0f,
              2.0f, 600.0f, 0.0f, -4.0f, -3.0f,  0, 0.40f, 1.0f, 0.20f, false, -15.2f },
            { "Crunch",     "A humbucker into an amp just breaking up: classic rock",
              0.65f, 1.0f, 0.12f, 0.10f, 3500.0f, 1.0f,   0.0f, 1.0f, 60.0f,  700.0f, 2.0f, false,  20.0f, 2, 0.12f, 7000.0f,
              1.0f, 700.0f, 2.0f, 1.0f, 1.0f,    1, 0.0f, 0.0f, 0.10f, false, -26.3f },
            { "Punk",       "Bright, crunchy and tight: power chords and downstrokes",
              0.75f, 1.0f, 0.10f, 0.09f, 3800.0f, 1.1f,   0.0f, 1.0f, 90.0f,  1000.0f, 3.0f, false, 26.0f, 2, 0.10f, 7500.0f,
              0.0f, 1000.0f, 2.0f, 2.0f, 2.0f,   1, 0.0f, 0.0f, 0.07f, false, -30.0f },
            { "High Gain",  "Modern metal: a pushed high-gain amp and a 4x12, double-tracked",
              0.70f, 1.1f, 0.11f, 0.09f, 3200.0f, 1.0f,   0.0f, 1.0f, 140.0f, 750.0f, 6.0f, true,   36.0f, 3, 0.08f, 6500.0f,
              3.0f, 500.0f, -3.0f, 2.0f, 3.0f,   1, 0.0f, 0.0f, 0.05f, true, -25.5f },
            { "Djent",      "Tight, bright and gated: low chugs that stop dead, double-tracked",
              0.85f, 1.0f, 0.09f, 0.08f, 3600.0f, 1.2f,   0.0f, 1.0f, 180.0f, 800.0f, 7.0f, true,   38.0f, 3, 0.06f, 7000.0f,
              2.0f, 450.0f, -2.0f, 3.0f, 4.0f,   1, 0.0f, 0.0f, 0.04f, true, -27.0f },
        };
        return list;
    }

    const Preset& presetNamed (const juce::String& name)
    {
        for (auto& p : presets())
            if (name == p.name)
                return p;

        return presets().front();
    }

    //==============================================================================
    // The delay-line length that, with the loop's loss and stiffness filters, tunes the string:
    // their phase delay at the note is taken off what the period needs
    double tunedDelay (double sr, double hz, double lossA, double stiffness)
    {
        const double w = twoPi * hz / sr;
        const auto z = std::polar (1.0, -w);
        const auto loss = (1.0 - lossA) / (1.0 - lossA * z);
        const auto allpass = (stiffness + z) / (1.0 + stiffness * z);
        const double tau = -std::arg (loss * allpass) / w;
        return sr / hz - tau;
    }

    double lossGainAt (double sr, double hz, double lossA)
    {
        const auto z = std::polar (1.0, -twoPi * hz / sr);
        return std::abs ((1.0 - lossA) / (1.0 - lossA * z));
    }

    //==============================================================================
    // One string: a delay line around a loss filter and a stiffness allpass, plucked by a short
    // burst shaped like the pick, heard through a pickup partway along it
    struct StringVoice
    {
        int string = 0, note = -1;
        bool held = false, active = false, palmMuted = false, dead = false;
        juce::int64 pressedAt = 0;
        float velocity = 0.8f;

        std::vector<float> line;
        int mask = 0, write = 0;
        double delay = 100.0, target = 100.0, glideStep = 0.0;
        double hz = 110.0;
        float lossA = 0.3f, loopGain = 0.99f, stiffness = 0.0f, lp = 0.0f, apX = 0.0f, apY = 0.0f;
        float pickupAt = 0.1f;
        std::array<float, 2048> burst {};
        int burstLength = 0, burstPos = 0;
        std::array<float, 512> click {};
        int clickLength = 0, clickPos = 0;
        float level = 0.0f;
        int quietFor = 0;

        void prepare (double sr)
        {
            int size = 1;

            while (size < (int) (sr / 25.0) + 16)
                size <<= 1;

            line.assign ((size_t) size, 0.0f);
            mask = size - 1;
            reset();
        }

        void reset()
        {
            std::fill (line.begin(), line.end(), 0.0f);
            write = 0;
            lp = apX = apY = level = 0.0f;
            active = held = false;
            burstLength = clickLength = 0;
            note = -1;
        }

        float read (double d) const noexcept
        {
            const double at = (double) write - d;
            const double base = std::floor (at);
            const float t = (float) (at - base);
            const int i = (int) base;
            const float xm1 = line[(size_t) ((i - 1) & mask)], x0 = line[(size_t) (i & mask)],
                        x1 = line[(size_t) ((i + 1) & mask)], x2 = line[(size_t) ((i + 2) & mask)];

            // Cubic Lagrange: smooth enough to slide on, and in tune
            const float c0 = -t * (t - 1.0f) * (t - 2.0f) / 6.0f;
            const float c1 = (t + 1.0f) * (t - 1.0f) * (t - 2.0f) / 2.0f;
            const float c2 = -(t + 1.0f) * t * (t - 2.0f) / 2.0f;
            const float c3 = (t + 1.0f) * t * (t - 1.0f) / 6.0f;
            return c0 * xm1 + c1 * x0 + c2 * x1 + c3 * x2;
        }

        // How long it rings (to -60 dB): long open, short when muted or let go
        void setDecay (double sr, double seconds)
        {
            const double perPeriod = std::pow (10.0, -3.0 / (seconds * hz));
            loopGain = (float) juce::jlimit (0.0, 0.99995, perPeriod / lossGainAt (sr, hz, lossA));
        }

        float tick() noexcept
        {
            if (glideStep != 0.0)
            {
                delay += glideStep;

                if ((glideStep > 0.0 && delay >= target) || (glideStep < 0.0 && delay <= target))
                {
                    delay = target;
                    glideStep = 0.0;
                }
            }

            const float y = read (delay);

            // The loop: losses (more on the highs), the string's stiffness, the pick's burst
            lp = (1.0f - lossA) * y + lossA * lp;
            const float v = loopGain * lp;
            const float ap = stiffness * v + apX - stiffness * apY;
            apX = v;
            apY = ap;

            float in = ap;

            if (burstPos < burstLength)
                in += burst[(size_t) burstPos++];

            line[(size_t) write] = in;
            write = (write + 1) & mask;

            // Heard through the pickup: the wave less its reflection from the pickup's distance
            float out = y - 0.85f * read (std::max (2.0, delay * pickupAt));

            if (clickPos < clickLength)
                out += click[(size_t) clickPos++];

            // Has it died away?
            level = 0.999f * level + 0.001f * std::abs (out);
            quietFor = level < 1.0e-5f && burstPos >= burstLength ? quietFor + 1 : 0;

            if (! held && quietFor > 2048)
                active = false;

            return out;
        }
    };

    //==============================================================================
    // Each sound's amp: before the drive (a compressor, a tight high-pass, a mid push, a gate),
    // the drive (four times over, so it doesn't alias), the tone stack, the cabinet
    struct Amp
    {
        Biquad pickup, highPass, push, gateDetect;
        std::array<Biquad, 3> stageCut;
        float dcX = 0.0f, dcY = 0.0f;
        std::array<Biquad, 4> tone;
        std::array<Biquad, 6> cabinet;
        float compEnv = 0.0f, compAttack = 0.0f, compRelease = 0.0f;
        float gateEnv = 0.0f, gateGain = 1.0f, gateOpen = 0.0f, gateClose = 0.0f;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        const Preset* preset = &presets().front();
        double sr = 48000.0;
        float stageGain = 1.0f, level = 1.0f, bias = 0.0f;

        void prepare (double sampleRate, int maxBlock)
        {
            sr = sampleRate;
            oversampling = std::make_unique<juce::dsp::Oversampling<float>> (1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
            oversampling->initProcessing ((size_t) maxBlock);
            compAttack = (float) std::exp (-1.0 / (0.002 * sr));
            compRelease = (float) std::exp (-1.0 / (0.08 * sr));
            gateOpen = (float) std::exp (-1.0 / (0.0008 * sr));
            gateClose = (float) std::exp (-1.0 / (0.006 * sr));
            setPreset (*preset);
        }

        void setPreset (const Preset& p)
        {
            preset = &p;
            const double osRate = sr * 4.0;
            pickup = Biquad::lowPass (sr, p.pickupHz, p.pickupQ);
            highPass = Biquad::highPass (sr, p.highPassHz > 0.0f ? p.highPassHz : 20.0, 0.7);
            push = Biquad::peak (sr, p.pushHz > 0.0f ? p.pushHz : 1000.0, 0.9, p.pushDb);

            for (auto& f : stageCut)
                f = Biquad::lowPass (osRate, p.stageCutHz, 0.6);

            stageGain = (float) std::pow (10.0, p.driveDb / 20.0 / juce::jmax (1, p.stages));
            bias = p.asymmetry;

            tone[0] = Biquad::lowShelf (sr, 120.0, p.bassDb);
            tone[1] = Biquad::peak (sr, p.midHz, 0.9, p.midDb);
            tone[2] = Biquad::highShelf (sr, 2500.0, p.trebleDb);
            tone[3] = Biquad::peak (sr, 4500.0, 1.0, p.presenceDb);

            if (p.cabinet == 1)         // a closed-back 4x12
                cabinet = { Biquad::highPass (sr, 80.0, 0.9), Biquad::peak (sr, 110.0, 1.1, 4.0), Biquad::peak (sr, 1500.0, 1.5, -2.5),
                            Biquad::peak (sr, 2900.0, 1.3, 4.0), Biquad::lowPass (sr, 4800.0, 0.9), Biquad::lowPass (sr, 5600.0, 0.7) };
            else if (p.cabinet == 2)    // a small, dark jazz combo
                cabinet = { Biquad::highPass (sr, 60.0, 0.7), Biquad::peak (sr, 200.0, 0.8, 2.0), Biquad::peak (sr, 1000.0, 1.0, -1.0),
                            Biquad::lowPass (sr, 3800.0, 0.7), Biquad::lowPass (sr, 5000.0, 0.6), Biquad() };
            else                        // an open-back 1x12
                cabinet = { Biquad::highPass (sr, 70.0, 0.7), Biquad::peak (sr, 140.0, 1.0, 1.5), Biquad::peak (sr, 2400.0, 1.2, 2.5),
                            Biquad::lowPass (sr, 6500.0, 0.8), Biquad::lowPass (sr, 9000.0, 0.6), Biquad() };

            level = (float) std::pow (10.0, p.levelDb / 20.0);
        }

        void reset()
        {
            for (auto* f : { &pickup, &highPass, &push })
                f->clear();

            for (auto& f : stageCut) f.clear();
            for (auto& f : tone) f.clear();
            for (auto& f : cabinet) f.clear();

            dcX = dcY = compEnv = gateEnv = 0.0f;
            gateGain = 1.0f;

            if (oversampling != nullptr)
                oversampling->reset();
        }

        static float clip (float x, float b) noexcept       { return std::tanh (x + b) - std::tanh (b); }

        void process (float* samples, int n)
        {
            const auto& p = *preset;

            // Before the drive
            for (int i = 0; i < n; ++i)
            {
                float x = pickup.process (samples[i]);

                if (p.compressDb < 0.0f)
                {
                    const float a = std::abs (x);
                    compEnv = a > compEnv ? compAttack * compEnv + (1.0f - compAttack) * a : compRelease * compEnv + (1.0f - compRelease) * a;
                    const float db = juce::Decibels::gainToDecibels (compEnv, -100.0f);
                    const float over = juce::jmax (0.0f, db - p.compressDb);
                    x *= juce::Decibels::decibelsToGain (-over * (1.0f - 1.0f / p.compressRatio) + 6.0f);
                }

                if (p.highPassHz > 0.0f)
                    x = highPass.process (x);

                if (p.pushDb != 0.0f)
                    x = push.process (x);

                // The gate: what dies away under -56 dB stops dead (tight chugs)
                if (p.gate)
                {
                    const float a = std::abs (x);
                    gateEnv = a > gateEnv ? a : 0.9995f * gateEnv;
                    const float want = gateEnv > 0.0016f ? 1.0f : 0.0f;
                    gateGain = want > gateGain ? gateOpen * gateGain + (1.0f - gateOpen) * want : gateClose * gateGain + (1.0f - gateClose) * want;
                    x *= gateGain;
                }

                samples[i] = x;
            }

            // The drive, four times over
            float* chan[] { samples };
            juce::dsp::AudioBlock<float> block (chan, 1, (size_t) n);
            auto up = oversampling->processSamplesUp (block);
            float* u = up.getChannelPointer (0);
            const int un = (int) up.getNumSamples();

            for (int i = 0; i < un; ++i)
            {
                float x = u[i];

                for (int s = 0; s < p.stages; ++s)
                    x = stageCut[(size_t) s].process (clip (x * stageGain, bias));

                u[i] = x;
            }

            oversampling->processSamplesDown (block);

            // The tone stack, the cabinet, the level
            for (int i = 0; i < n; ++i)
            {
                float x = samples[i];

                // DC from the asymmetric drive
                const float y = x - dcX + 0.9995f * dcY;
                dcX = x;
                dcY = y;
                x = y;

                for (auto& f : tone)
                    x = f.process (x);

                for (auto& f : cabinet)
                    x = f.process (x);

                samples[i] = x * level;
            }
        }
    };
}

//==============================================================================
// A take: the strings and their amp. The heavy sounds have two, the second a few ms late, a hair
// sharp and plucked its own way, panned opposite
struct GuitarPlugin::Engine
{
    struct Take
    {
        std::array<StringVoice, stringCount> strings;
        Amp amp;
        juce::Random random;
        bool palmHeld = false, deadHeld = false;
        double detuneCents = 0.0;
        std::vector<float> buffer;
    };

    struct Pending
    {
        juce::int64 at;
        juce::MidiMessage message;
    };

    std::array<Take, 2> takes;
    std::vector<Pending> pending;
    const Preset* preset = &presets().front();
    double sr = 48000.0;
    int maxBlock = 512;
    juce::int64 clock = 0;
    juce::Reverb room;
    std::vector<float> chorusL, chorusR, wobbleLine;
    int chorusWrite = 0, wobbleWrite = 0;
    double lfo = 0.0, slowLfo = 0.0;
    std::vector<float> left, right;

    void prepare (double rate, int blockSize, juce::int64 seed)
    {
        sr = rate;
        maxBlock = juce::jmax (blockSize, 64);

        for (int t = 0; t < 2; ++t)
        {
            auto& take = takes[(size_t) t];

            for (int s = 0; s < stringCount; ++s)
            {
                take.strings[(size_t) s].string = s;
                take.strings[(size_t) s].prepare (sr);
            }

            take.amp.prepare (sr, maxBlock);
            take.random.setSeed (seed + t * 7919);
            take.detuneCents = t == 0 ? 0.0 : 5.0;
            take.buffer.assign ((size_t) maxBlock, 0.0f);
        }

        chorusL.assign (4096, 0.0f);
        chorusR.assign (4096, 0.0f);
        wobbleLine.assign (4096, 0.0f);
        left.assign ((size_t) maxBlock, 0.0f);
        right.assign ((size_t) maxBlock, 0.0f);
        pending.clear();
        pending.reserve (512);
        room.setSampleRate (sr);
        setPreset (*preset);
        reset();
    }

    void setPreset (const Preset& p)
    {
        preset = &p;

        for (auto& take : takes)
            take.amp.setPreset (p);

        juce::Reverb::Parameters settings;
        settings.roomSize = 0.32f;
        settings.damping = 0.55f;
        settings.wetLevel = p.room;
        settings.dryLevel = 1.0f;
        settings.width = 1.0f;
        room.setParameters (settings);
    }

    void reset()
    {
        for (auto& take : takes)
        {
            for (auto& s : take.strings)
                s.reset();

            take.amp.reset();
            take.palmHeld = take.deadHeld = false;
        }

        pending.clear();
        room.reset();
        std::fill (chorusL.begin(), chorusL.end(), 0.0f);
        std::fill (chorusR.begin(), chorusR.end(), 0.0f);
        std::fill (wobbleLine.begin(), wobbleLine.end(), 0.0f);
        clock = 0;
    }

    int activeStrings() const
    {
        int n = 0;

        for (auto& s : takes[0].strings)
            n += s.active ? 1 : 0;

        return n;
    }

    //==============================================================================
    void pluck (Take& take, StringVoice& v, int key, float velocity)
    {
        const auto& p = *preset;
        const double hz = 440.0 * std::pow (2.0, (key - 69 + take.detuneCents / 100.0) / 12.0);
        v.note = key;
        v.held = true;
        v.active = true;
        v.quietFor = 0;
        v.pressedAt = clock;
        v.velocity = velocity;
        v.palmMuted = take.palmHeld;
        v.dead = take.deadHeld;
        v.hz = hz;
        v.pickupAt = p.pickupAt;
        v.stiffness = openPitch[v.string] < 50 ? -0.14f : openPitch[v.string] < 55 ? -0.08f : -0.04f;

        // The pick stops what was ringing, then sets it going again
        for (auto& x : v.line)
            x *= 0.25f;

        // Muted notes are darker in the loop and die fast; open ones ring for seconds
        const float bright = juce::jlimit (0.05f, 1.0f, p.brightness);
        v.lossA = v.dead ? 0.75f : v.palmMuted ? 0.62f : 0.10f + 0.35f * (1.0f - bright);
        v.delay = v.target = juce::jmax (3.0, tunedDelay (sr, hz, v.lossA, v.stiffness));
        v.glideStep = 0.0;

        const double open = 6.0 * p.sustain * juce::jlimit (0.25, 1.4, std::pow (82.0 / hz, 0.45));
        v.setDecay (sr, v.dead ? 0.035 : v.palmMuted ? 0.22 * juce::jlimit (0.6, 1.2, std::pow (82.0 / hz, 0.3)) : open);

        // The burst: noise in a short window (shorter and brighter the harder it's picked), less
        // whatever the pick's position cancels
        const float v01 = juce::jlimit (0.05f, 1.0f, velocity);
        const int length = juce::jlimit (8, 400, (int) std::round (sr * (0.0006 + 0.0016 * (1.0 - v01))));
        const double cutoff = (1200.0 + 8000.0 * std::pow (v01, 1.5)) * (0.4 + 0.9 * bright) * (v.palmMuted ? 0.45 : 1.0);
        const float a = (float) std::exp (-twoPi * juce::jmin (cutoff, sr * 0.45) / sr);
        const int pick = juce::jlimit (1, 1000, (int) std::round (v.delay * p.pickAt));
        const float amplitude = 0.55f * std::pow (v01, 1.3f) * (v.dead ? 0.6f : 1.0f);
        std::array<float, 1024> raw {};
        float smooth = 0.0f;

        for (int i = 0; i < length; ++i)
        {
            const float window = 0.5f - 0.5f * std::cos ((float) twoPi * (float) i / (float) (length - 1));
            smooth = (1.0f - a) * (take.random.nextFloat() * 2.0f - 1.0f) * window + a * smooth;
            raw[(size_t) i] = smooth * amplitude * 3.0f;
        }

        v.burstLength = juce::jmin ((int) v.burst.size(), length + pick);

        for (int i = 0; i < v.burstLength; ++i)
            v.burst[(size_t) i] = (i < length ? raw[(size_t) i] : 0.0f) - 0.9f * (i >= pick && i - pick < length ? raw[(size_t) (i - pick)] : 0.0f);

        v.burstPos = 0;

        // A dead note's scratch: a short click of string noise under the pick
        v.clickLength = 0;

        if (v.dead)
        {
            v.clickLength = juce::jmin ((int) v.click.size(), (int) (sr * 0.004));
            Biquad band = Biquad::peak (sr, 2600.0, 1.2, 9.0);

            for (int i = 0; i < v.clickLength; ++i)
            {
                const float window = 1.0f - (float) i / (float) v.clickLength;
                v.click[(size_t) i] = band.process ((take.random.nextFloat() * 2.0f - 1.0f) * window * window) * 0.12f * v01;
            }

            v.clickPos = 0;
        }
    }

    // On the same string, no new pluck: a hammer-on or pull-off jumps, a slide glides
    void legato (StringVoice& v, Take& take, int key)
    {
        const int from = v.note;
        const double hz = 440.0 * std::pow (2.0, (key - 69 + take.detuneCents / 100.0) / 12.0);
        const double target = juce::jmax (3.0, tunedDelay (sr, hz, v.lossA, v.stiffness));
        v.note = key;
        v.hz = hz;
        v.held = true;
        v.pressedAt = clock;
        v.quietFor = 0;

        if (std::abs (key - from) <= 2)
        {
            v.delay = v.target = target;
            v.glideStep = 0.0;

            for (auto& x : v.line)
                x *= 0.85f;
        }
        else
        {
            v.target = target;
            const double samples = sr * juce::jlimit (0.04, 0.08, 0.012 * std::abs (key - from));
            v.glideStep = (target - v.delay) / samples;
        }

        const auto& p = *preset;
        const double open = 6.0 * p.sustain * juce::jlimit (0.25, 1.4, std::pow (82.0 / hz, 0.45));
        v.setDecay (sr, v.palmMuted ? 0.22 : open);
    }

    void noteOn (Take& take, int key, float velocity)
    {
        if (key == guitarsound::palmMuteKey)    { take.palmHeld = true; return; }
        if (key == guitarsound::deadNoteKey)    { take.deadHeld = true; return; }
        if (key < 24 || key > 108)              return;

        // Legato: a string still held from before (not part of this strum) that can play it,
        // near the note it's on
        StringVoice* same = nullptr;
        const juce::int64 strum = (juce::int64) (sr * 0.06);

        if (! take.deadHeld)
            for (auto& v : take.strings)
                if (v.held && v.active && ! v.dead && clock - v.pressedAt > strum && key >= openPitch[v.string] - 1
                     && key - openPitch[v.string] <= 24 && std::abs (key - v.note) <= 7)
                    if (same == nullptr || std::abs (key - v.note) < std::abs (key - same->note))
                        same = &v;

        if (same != nullptr)
        {
            legato (*same, take, key);
            return;
        }

        // A string for it: the highest that can play it and isn't held; all held, the one held
        // longest; below them all, the lowest
        StringVoice* chosen = nullptr;
        StringVoice* oldest = nullptr;

        for (int s = stringCount - 1; s >= 0; --s)
        {
            auto& v = take.strings[(size_t) s];

            if (key < openPitch[s] - 1 || key - openPitch[s] > 24)
                continue;

            if (! v.held)
            {
                chosen = &v;
                break;
            }

            if (oldest == nullptr || v.pressedAt < oldest->pressedAt)
                oldest = &v;
        }

        if (chosen == nullptr)
            chosen = oldest != nullptr ? oldest : &take.strings[0];

        pluck (take, *chosen, key, velocity);
    }

    void noteOff (Take& take, int key)
    {
        if (key == guitarsound::palmMuteKey)    { take.palmHeld = false; return; }
        if (key == guitarsound::deadNoteKey)    { take.deadHeld = false; return; }

        for (auto& v : take.strings)
            if (v.held && v.note == key)
            {
                // The fretting hand lets go: the string's damped
                v.held = false;
                v.setDecay (sr, v.dead ? 0.03 : v.palmMuted ? 0.08 : 0.12);
            }
    }

    void handle (Take& take, const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
            noteOn (take, m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())
            noteOff (take, m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            for (auto& v : take.strings)
                if (v.held)
                {
                    v.held = false;
                    v.setDecay (sr, 0.12);
                }
    }

    //==============================================================================
    // Renders one take's strings into its buffer, its events at their samples, then its amp
    void renderTake (Take& take, int n, const std::vector<std::pair<int, juce::MidiMessage>>& events)
    {
        auto* out = take.buffer.data();
        std::fill (out, out + n, 0.0f);
        size_t e = 0;

        for (int i = 0; i < n; ++i)
        {
            while (e < events.size() && events[e].first <= i)
                handle (take, events[e++].second);

            float sum = 0.0f;

            for (auto& v : take.strings)
                if (v.active)
                    sum += v.tick();

            out[i] = sum;
            ++clock;
        }

        clock -= n;
        take.amp.process (out, n);
    }

    void render (float* outL, float* outR, int n, const std::vector<std::pair<int, juce::MidiMessage>>& events)
    {
        for (int done = 0; done < n;)
        {
            const int chunk = juce::jmin (maxBlock, n - done);
            thread_local std::vector<std::pair<int, juce::MidiMessage>> first, second;
            first.clear();
            second.clear();

            for (auto& [at, m] : events)
                if (at >= done && at < done + chunk)
                {
                    first.push_back ({ at - done, m });

                    // The second take hears it a few ms late, a little differently each time
                    if (preset->doubled)
                        pending.push_back ({ clock + at - done + (juce::int64) (sr * (0.009 + 0.004 * takes[1].random.nextFloat())), m });
                }

            renderTake (takes[0], chunk, first);

            if (preset->doubled)
            {
                for (auto it = pending.begin(); it != pending.end();)
                    if (it->at < clock + chunk)
                    {
                        second.push_back ({ (int) juce::jmax ((juce::int64) 0, it->at - clock), it->message });
                        it = pending.erase (it);
                    }
                    else
                    {
                        ++it;
                    }

                std::stable_sort (second.begin(), second.end(), [] (auto& x, auto& y) { return x.first < y.first; });
                renderTake (takes[1], chunk, second);
            }

            const auto* a = takes[0].buffer.data();
            const auto* b = takes[1].buffer.data();

            for (int i = 0; i < chunk; ++i)
            {
                float l = a[i], r = a[i];

                // Double-tracked: one take each side, a little of each in the other
                if (preset->doubled)
                {
                    l = 0.85f * a[i] + 0.15f * b[i];
                    r = 0.15f * a[i] + 0.85f * b[i];
                }

                // A worn tape's wobble, then the chorus
                if (preset->wobble > 0.0f)
                {
                    slowLfo += 0.6 / sr;
                    const double depth = sr * 0.00035 * preset->wobble;
                    const double d = sr * 0.005 + depth * (std::sin (twoPi * slowLfo) + 0.4 * std::sin (twoPi * slowLfo * 0.29));
                    wobbleLine[(size_t) wobbleWrite] = 0.5f * (l + r);
                    const double at = wobbleWrite - d;
                    const int i0 = (int) std::floor (at);
                    const float t = (float) (at - i0);
                    const float w = wobbleLine[(size_t) (i0 & 4095)] * (1.0f - t) + wobbleLine[(size_t) ((i0 + 1) & 4095)] * t;
                    wobbleWrite = (wobbleWrite + 1) & 4095;
                    l = r = w;
                }

                if (preset->chorus > 0.0f)
                {
                    lfo += 0.45 / sr;
                    chorusL[(size_t) chorusWrite] = l;
                    chorusR[(size_t) chorusWrite] = r;

                    auto tap = [&] (const std::vector<float>& line, double phase)
                    {
                        const double d = sr * (0.011 + 0.0025 * std::sin (twoPi * (lfo + phase)));
                        const double at = chorusWrite - d;
                        const int i0 = (int) std::floor (at);
                        const float t = (float) (at - i0);
                        return line[(size_t) (i0 & 4095)] * (1.0f - t) + line[(size_t) ((i0 + 1) & 4095)] * t;
                    };

                    const float wl = tap (chorusL, 0.0), wr = tap (chorusR, 0.25);
                    chorusWrite = (chorusWrite + 1) & 4095;
                    l = l * (1.0f - 0.5f * preset->chorus) + wl * preset->chorus;
                    r = r * (1.0f - 0.5f * preset->chorus) + wr * preset->chorus;
                }

                outL[done + i] = l;
                outR[done + i] = r;
            }

            if (preset->room > 0.0f)
                room.processStereo (outL + done, outR + done, chunk);

            clock += chunk;
            done += chunk;
        }
    }
};

//==============================================================================
namespace guitarsound
{
    juce::StringArray presetNames()
    {
        juce::StringArray names;

        for (auto& p : presets())
            names.add (p.name);

        return names;
    }

    juce::String describe (const juce::String& preset)
    {
        for (auto& p : presets())
            if (preset == p.name)
                return p.about;

        return {};
    }

    juce::String pathFor (const juce::String& preset)       { return pathPrefix + preset; }
    bool isGuitarPath (const juce::String& path)            { return path.startsWith (pathPrefix); }

    juce::String presetOfPath (const juce::String& path)
    {
        const auto name = path.fromFirstOccurrenceOf (pathPrefix, false, false);
        return presetNames().contains (name) ? name : juce::String ("Clean");
    }

    Render render (const juce::String& preset, const std::vector<Hit>& hits, double seconds, juce::int64 seed)
    {
        constexpr double sr = 48000.0;
        constexpr int block = 512;
        Render result;
        const int total = (int) std::ceil (seconds * sr);
        result.audio.setSize (2, total);
        result.audio.clear();

        // On the heap: eight strings, twice, are more than a thread's stack wants
        auto engine = std::make_unique<GuitarPlugin::Engine>();
        engine->preset = &presetNamed (preset);
        engine->prepare (sr, block, seed);

        // Every hit as a note on and off, in time order (offs first at the same sample)
        std::vector<std::pair<juce::int64, juce::MidiMessage>> events;

        for (auto& h : hits)
        {
            const auto on = (juce::int64) std::round (h.start * sr);
            const auto off = (juce::int64) std::round ((h.start + h.length) * sr);
            events.push_back ({ on, juce::MidiMessage::noteOn (1, h.key, (juce::uint8) juce::jlimit (1, 127, h.velocity)) });
            events.push_back ({ off, juce::MidiMessage::noteOff (1, h.key) });
        }

        std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b)
        {
            return a.first != b.first ? a.first < b.first : (a.second.isNoteOff() && ! b.second.isNoteOff());
        });

        size_t next = 0;
        std::vector<std::pair<int, juce::MidiMessage>> blockEvents;

        for (int start = 0; start < total; start += block)
        {
            const int n = juce::jmin (block, total - start);
            blockEvents.clear();

            while (next < events.size() && events[next].first < start + n)
            {
                blockEvents.push_back ({ (int) juce::jmax ((juce::int64) 0, events[next].first - start), events[next].second });
                ++next;
            }

            engine->render (result.audio.getWritePointer (0, start), result.audio.getWritePointer (1, start), n, blockEvents);
            result.mostStrings = juce::jmax (result.mostStrings, engine->activeStrings());
        }

        return result;
    }

    std::vector<Hit> phrase (const juce::String& name)
    {
        std::vector<Hit> hits;

        // A strum: low string to high, 10 ms apart
        auto strum = [&hits] (std::initializer_list<int> keys, double at, double length, int velocity)
        {
            int i = 0;

            for (auto k : keys)
            {
                hits.push_back ({ k, at + 0.010 * i, length - 0.010 * i, velocity - 3 * (i % 2) });
                ++i;
            }
        };

        if (name == "chug")
        {
            // Drop C at 140: palm-muted 16ths on the low C, a C5 to open each phrase
            const double sixteenth = 60.0 / 140.0 / 4.0;
            static const int pattern[] { 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 1 };

            for (int bar = 0; bar < 4; ++bar)
            {
                const double at = bar * 16 * sixteenth;
                strum ({ 36, 43, 48 }, at, sixteenth * 3.5, 118);
                hits.push_back ({ guitarsound::palmMuteKey, at + sixteenth * 3.8, sixteenth * 12.0, 100 });

                for (int s = 4; s < 16; ++s)
                    if (pattern[s])
                        hits.push_back ({ 36, at + s * sixteenth, sixteenth * 0.8, 112 });
            }
        }
        else if (name == "riff")
        {
            // E at 120: E5 rings, palm-muted 8ths on the low E, then G5 and A5
            const double eighth = 0.25;

            for (int bar = 0; bar < 4; ++bar)
            {
                const double at = bar * 8 * eighth;
                strum ({ 40, 47, 52 }, at, eighth * 1.9, 110);
                hits.push_back ({ guitarsound::palmMuteKey, at + eighth * 1.95, eighth * 2.1, 100 });

                for (int e = 2; e < 4; ++e)
                    hits.push_back ({ 40, at + e * eighth, eighth * 0.9, 104 });

                strum ({ 43, 50, 55 }, at + 4 * eighth, eighth * 1.9, 112);
                strum ({ 45, 52, 57 }, at + 6 * eighth, eighth * 1.9, 112);
            }
        }
        else
        {
            // Em, G, D, C: open chords, a down strum each
            const int chords[4][6] { { 40, 47, 52, 55, 59, 64 }, { 43, 47, 50, 55, 59, 67 }, { 50, 57, 62, 66, 0, 0 }, { 48, 52, 55, 60, 64, 0 } };

            for (int c = 0; c < 4; ++c)
            {
                const double at = c * 1.2;
                int i = 0;

                for (int k : chords[c])
                    if (k > 0)
                    {
                        hits.push_back ({ k, at + 0.012 * i, 1.1 - 0.012 * i, 96 - 2 * i });
                        ++i;
                    }
            }
        }

        return hits;
    }
}

bool isGuitar (const Channel& c)
{
    return c.type == InstrumentType::instrument && guitarsound::isGuitarPath (c.instrument);
}

juce::String guitarPresetOf (const Channel& c)
{
    return isGuitar (c) ? guitarsound::presetOfPath (c.instrument) : juce::String();
}

//==============================================================================
const char* GuitarPlugin::xmlTypeName = "nightshiftGuitar";

GuitarPlugin::GuitarPlugin (tracktion::PluginCreationInfo info)
    : StockEffect (info), engine (std::make_unique<Engine>())
{
}

GuitarPlugin::~GuitarPlugin()
{
    notifyListenersOfDeletion();
}

void GuitarPlugin::prepare()
{
    engine->preset = &presetNamed (preset);
    engine->prepare (sampleRate, 2048, juce::Random::getSystemRandom().nextInt64());
}

void GuitarPlugin::reset()
{
    engine->reset();
}

void GuitarPlugin::setPreset (const juce::String& name)
{
    const juce::SpinLock::ScopedLockType sl (presetLock);
    preset = presetNamed (name).name;
    pendingPreset = preset;
}

void GuitarPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.bufferNumSamples == 0)
        return;

    auto& buffer = *fc.destBuffer;
    const int numSamples = fc.bufferNumSamples;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, fc.bufferStartSample, numSamples);

    juce::ScopedNoDenormals noDenormals;

    // A new sound, from this block (never waiting on the message thread)
    {
        const juce::SpinLock::ScopedTryLockType sl (presetLock);

        if (sl.isLocked() && pendingPreset.isNotEmpty())
        {
            engine->setPreset (presetNamed (pendingPreset));
            pendingPreset = {};
        }
    }

    thread_local std::vector<std::pair<int, juce::MidiMessage>> events;
    events.clear();

    if (fc.bufferForMidiMessages != nullptr)
    {
        if (fc.bufferForMidiMessages->isAllNotesOff)
            events.push_back ({ 0, juce::MidiMessage::allNotesOff (1) });

        for (auto& m : *fc.bufferForMidiMessages)
            events.push_back ({ juce::jlimit (0, numSamples - 1, juce::roundToInt (m.getTimeStamp() * sampleRate)), m });

        std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    }

    float* left = buffer.getWritePointer (0, fc.bufferStartSample);
    thread_local std::vector<float> spare;
    float* right = nullptr;

    if (buffer.getNumChannels() > 1)
    {
        right = buffer.getWritePointer (1, fc.bufferStartSample);
    }
    else
    {
        spare.assign ((size_t) numSamples, 0.0f);
        right = spare.data();
    }

    engine->render (left, right, numSamples, events);

    if (! std::isfinite (left[0]) || ! std::isfinite (left[numSamples - 1]))
    {
        engine->reset();
        buffer.clear (fc.bufferStartSample, numSamples);
    }

    activeStrings.store (engine->activeStrings());
}

} // namespace nightshift
