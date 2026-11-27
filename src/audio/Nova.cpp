// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Nova.h"
#include <string_view>

namespace nightshift
{

const char* NovaPlugin::xmlTypeName = "nightshiftNova";

namespace
{
    using Info = nova::ParamInfo;

    constexpr const char* filterModes = "Low pass|Band pass|High pass|Notch";
    constexpr const char* slopes = "12 dB|24 dB";
    constexpr const char* lfoShapes = "Sine|Triangle|Saw|Square|Random|Drift";
    constexpr const char* divisions = "4 bars|2 bars|1 bar|1/2|1/4 dotted|1/4|1/4 triplet|1/8 dotted|1/8|1/8 triplet|1/16 dotted|1/16|1/16 triplet|1/32";
    constexpr const char* onOff = "Off|On";
    constexpr const char* voiceModes = "Poly|Mono|Legato";
    constexpr const char* sources = "Off|LFO 1|LFO 2|Filter env|Mod env|Velocity|Mod wheel|Aftertouch|Key|Random";
    constexpr const char* destinations = "Off|Pitch|Osc 1 pitch|Osc 2 pitch|Osc 1 shape|Osc 2 shape|Pulse width|Osc 1 level|Osc 2 level"
                                         "|FM|Detune|Sub|Noise|Cutoff|Resonance|Drive|Volume|Pan|LFO 1 rate|LFO 2 rate";

    constexpr double beats[] = { 16.0, 8.0, 4.0, 2.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125 };

    double beatsAt (int index)
    {
        return beats[juce::jlimit (0, (int) std::size (beats) - 1, index)];
    }

    #define NOVA_OSC(n, levelDefault, levelHelp) \
        { "osc" #n "_level",   "Level",       "Osc " #n, 0, 1, levelDefault, 0, 0, "%", "", levelHelp }, \
        { "osc" #n "_shape",   "Shape",       "Osc " #n, 0, 3, 2, 0, 0, "wave", "", "0 sine, 1 triangle, 2 saw, 3 square; values between morph from one to the next (2.5 is half saw, half square)" }, \
        { "osc" #n "_pw",      "Pulse width", "Osc " #n, 0.05f, 0.95f, 0.5f, 0, 0, "%", "", "The square's width: 0.5 is hollow, narrower is thin and nasal (heard as the shape nears square)" }, \
        { "osc" #n "_octave",  "Octave",      "Osc " #n, -3, 3, 0, 1, 0, "oct", "", "Octaves up or down" }, \
        { "osc" #n "_semi",    "Semitones",   "Osc " #n, -12, 12, 0, 1, 0, "st", "", "Semitones up or down (7 is a fifth above)" }, \
        { "osc" #n "_fine",    "Fine",        "Osc " #n, -100, 100, 0, 0, 0, "cents", "", "Fine tuning in cents (a few cents between the oscillators thickens)" }, \
        { "osc" #n "_unison",  "Unison",      "Osc " #n, 1, 8, 1, 1, 0, "voices", "", "Copies stacked per note, detuned and spread: 1 is clean and focused, 5 to 8 is a supersaw" }, \
        { "osc" #n "_detune",  "Detune",      "Osc " #n, 0, 1, 0.2f, 0, 0, "%", "", "How far the unison copies are detuned: 0.1 a gentle chorus, 0.3 supersaw, 0.6 and up wild" }, \
        { "osc" #n "_width",   "Width",       "Osc " #n, 0, 1, 0.7f, 0, 0, "%", "", "How wide the unison copies spread across the stereo field" }

    #define NOVA_ENV(prefix, section, a, d, s, r, what) \
        { prefix "_attack",  "Attack",  section, 0, 10, a, 0, 0.5f, "s", "", "Seconds " what " takes to rise" }, \
        { prefix "_decay",   "Decay",   section, 0.001f, 10, d, 0, 1, "s", "", "Seconds " what " takes to fall to its sustain level" }, \
        { prefix "_sustain", "Sustain", section, 0, 1, s, 0, 0, "%", "", "Where " what " stays while the key is held" }, \
        { prefix "_release", "Release", section, 0.001f, 10, r, 0, 1, "s", "", "Seconds " what " takes to fall after the key is let go" }

    #define NOVA_LFO(n, rate, retrigger) \
        { "lfo" #n "_shape",     "Shape",     "LFO " #n, 0, 5, 0, 1, 0, "choice", lfoShapes, "0 sine, 1 triangle, 2 saw (falling), 3 square, 4 random steps, 5 smooth random drift" }, \
        { "lfo" #n "_rate",      "Rate",      "LFO " #n, 0.02f, 40, rate, 0, 3, "Hz", "", "Cycles per second when not synced (5 to 6 for vibrato)" }, \
        { "lfo" #n "_sync",      "Sync",      "LFO " #n, 0, 1, 0, 1, 0, "choice", onOff, "1 to follow the song's tempo, a cycle every lfo" #n "_division" }, \
        { "lfo" #n "_division",  "Division",  "LFO " #n, 0, 13, 5, 1, 0, "choice", divisions, "When synced, one cycle lasts: 0 4 bars, 1 2 bars, 2 1 bar, 3 1/2, 4 dotted 1/4, 5 1/4, 6 1/4 triplet, 7 dotted 1/8, 8 1/8, 9 1/8 triplet, 10 dotted 1/16, 11 1/16, 12 1/16 triplet, 13 1/32" }, \
        { "lfo" #n "_retrigger", "Retrigger", "LFO " #n, 0, 1, retrigger, 1, 0, "choice", onOff, "1 starts the LFO afresh with each note; 0 lets one LFO run freely under every note" }, \
        { "lfo" #n "_fade",      "Fade in",   "LFO " #n, 0, 5, 0, 0, 1, "s", "", "Seconds the LFO takes to fade in after a note starts (delayed vibrato)" }

    #define NOVA_MOD(n) \
        { "mod" #n "_source", "Source",      "Mod " #n, 0, 9, 0, 1, 0, "choice", sources, "What moves it: 0 off, 1 LFO 1, 2 LFO 2, 3 filter envelope, 4 mod envelope, 5 velocity, 6 mod wheel, 7 aftertouch, 8 key (the note, 0 at middle C, +1 two octaves up), 9 random (a new value each note)" }, \
        { "mod" #n "_dest",   "Destination", "Mod " #n, 0, 19, 0, 1, 0, "choice", destinations, "What it moves: 0 off, 1 pitch, 2 osc 1 pitch, 3 osc 2 pitch, 4 osc 1 shape, 5 osc 2 shape, 6 pulse width, 7 osc 1 level, 8 osc 2 level, 9 FM, 10 detune, 11 sub, 12 noise, 13 cutoff, 14 resonance, 15 drive, 16 volume, 17 pan, 18 LFO 1 rate, 19 LFO 2 rate" }, \
        { "mod" #n "_amount", "Amount",      "Mod " #n, -1, 1, 0, 0, 0, "amount", "", "How far, -1 to 1, of the destination's full range: pitch 12 semitones, shape 3, cutoff 8 octaves, LFO rate 4 octaves, others their whole 0-1 range (0.02 on pitch from LFO 1 is a gentle vibrato)" }

    constexpr Info table[] = {
        NOVA_OSC (1, 0.8f, "Osc 1's volume"),
        NOVA_OSC (2, 0.0f, "Osc 2's volume (at 0 it can still drive FM)"),

        { "fm",          "FM",         "Mix", 0, 1, 0, 0, 0, "%", "", "Osc 2 bends osc 1's phase: 0.1 to 0.3 for electric piano and bell tones, more for metallic and harsh. Works at any osc 2 level; osc 2's tuning sets the timbre (+12, +19 or +24 st are classic)" },
        { "sub_level",   "Sub",        "Mix", 0, 1, 0, 0, 0, "%", "", "A sine an octave below osc 1, for weight" },
        { "noise_level", "Noise",      "Mix", 0, 1, 0, 0, 0, "%", "", "White noise, for breath, air and percussive attacks" },
        { "noise_tone",  "Noise tone", "Mix", 0, 1, 0.7f, 0, 0, "%", "", "Dark (0) to bright (1) noise" },

        { "filter_mode",     "Mode",       "Filter", 0, 3, 0, 1, 0, "choice", filterModes, "0 low pass, 1 band pass, 2 high pass, 3 notch" },
        { "filter_slope",    "Slope",      "Filter", 0, 1, 1, 1, 0, "choice", slopes, "0 for 12 dB/oct (gentle), 1 for 24 dB/oct (steep, the classic analog sound)" },
        { "filter_cutoff",   "Cutoff",     "Filter", 20, 20000, 20000, 0, 1000, "Hz", "", "Where the filter starts cutting, in Hz (20000 is wide open)" },
        { "filter_reso",     "Resonance",  "Filter", 0, 1, 0.1f, 0, 0, "%", "", "Emphasis at the cutoff: 0.3 adds bite, 0.6 and up squelches, 0.9 whistles" },
        { "filter_drive",    "Drive",      "Filter", 0, 1, 0, 0, 0, "%", "", "Saturation into the filter: warmth at 0.2, grit at 0.6 and up" },
        { "filter_env",      "Env amount", "Filter", -8, 8, 0, 0, 0, "oct", "", "Octaves the filter envelope moves the cutoff (negative closes it)" },
        { "filter_keytrack", "Key track",  "Filter", 0, 1, 0, 0, 0, "%", "", "How much the cutoff follows the note (1: fully, so high notes are as bright as low ones)" },
        { "filter_velocity", "Velocity",   "Filter", 0, 1, 0, 0, 0, "%", "", "How much playing harder opens the filter (1: up to 4 octaves)" },

        NOVA_ENV ("fenv", "Filter env", 0.005f, 0.5f, 0.0f, 0.3f, "the filter envelope"),
        NOVA_ENV ("amp", "Amp env", 0.002f, 0.5f, 1.0f, 0.25f, "the volume"),
        { "amp_velocity", "Velocity", "Amp env", 0, 1, 0.5f, 0, 0, "%", "", "How much playing harder makes it louder" },
        NOVA_ENV ("env3", "Mod env", 0.005f, 0.4f, 0.0f, 0.3f, "the mod envelope (for the mod matrix)"),

        NOVA_LFO (1, 5.0f, 1.0f),
        NOVA_LFO (2, 0.5f, 0.0f),

        NOVA_MOD (1), NOVA_MOD (2), NOVA_MOD (3), NOVA_MOD (4), NOVA_MOD (5), NOVA_MOD (6),

        { "voice_mode", "Mode",       "Voice", 0, 2, 0, 1, 0, "choice", voiceModes, "0 poly (chords), 1 mono (one note at a time, each retriggers), 2 legato (one note; overlapping notes glide without retriggering)" },
        { "polyphony",  "Voices",     "Voice", 1, 16, 16, 1, 0, "voices", "", "How many notes can sound at once in poly" },
        { "glide",      "Glide",      "Voice", 0, 2, 0, 0, 0.3f, "s", "", "Seconds to slide from one note's pitch to the next" },
        { "bend_range", "Bend range", "Voice", 0, 24, 2, 1, 0, "st", "", "Semitones the pitch wheel bends" },

        { "drive",          "Drive",     "FX", 0, 1, 0, 0, 0, "%", "", "Saturation on the whole sound after the filter" },
        { "chorus_mix",     "Chorus",    "FX", 0, 1, 0, 0, 0, "%", "", "How much chorus: width and shimmer (0.3 to 0.5 for pads and keys)" },
        { "chorus_rate",    "Rate",      "FX", 0.05f, 5, 0.5f, 0, 0.8f, "Hz", "", "The chorus's speed" },
        { "chorus_depth",   "Depth",     "FX", 0, 1, 0.5f, 0, 0, "%", "", "How far the chorus sways" },
        { "delay_mix",      "Delay",     "FX", 0, 1, 0, 0, 0, "%", "", "How loud the echoes are (0.15 to 0.3 is usual)" },
        { "delay_time",     "Time",      "FX", 0, 13, 7, 1, 0, "choice", divisions, "The echo's note value, in time with the song (same list as LFO divisions; 7 dotted 1/8 is a classic)" },
        { "delay_feedback", "Feedback",  "FX", 0, 0.95f, 0.35f, 0, 0, "%", "", "How many echoes: 0.2 a few, 0.6 many" },
        { "delay_tone",     "Tone",      "FX", 0, 1, 0.6f, 0, 0, "%", "", "Darker (0) to brighter (1) echoes" },
        { "delay_pingpong", "Ping-pong", "FX", 0, 1, 1, 1, 0, "choice", onOff, "1 bounces the echoes left and right" },
        { "reverb_mix",     "Reverb",    "FX", 0, 1, 0, 0, 0, "%", "", "How much reverb (0.15 a touch, 0.4 lush, 0.7 drenched)" },
        { "reverb_size",    "Size",      "FX", 0, 1, 0.6f, 0, 0, "%", "", "Small room (0) to huge hall (1)" },
        { "reverb_decay",   "Decay",     "FX", 0.2f, 12, 2.5f, 0, 2, "s", "", "Seconds the reverb rings" },
        { "reverb_tone",    "Tone",      "FX", 0, 1, 0.6f, 0, 0, "%", "", "Darker (0) to brighter (1) reverb" },

        { "volume", "Volume", "Output", -30, 6, -6, 0, 0, "dB", "", "The sound's overall level" },
    };

    #undef NOVA_OSC
    #undef NOVA_ENV
    #undef NOVA_LFO
    #undef NOVA_MOD

    constexpr int numParams = (int) std::size (table);

    consteval int at (std::string_view id)
    {
        for (int i = 0; i < numParams; ++i)
            if (std::string_view (table[i].id) == id)
                return i;

        throw "no such parameter";
    }

    // Where each parameter is in a block's settings
    namespace ix
    {
        enum OscParam { level, shape, pw, octave, semi, fine, unison, detune, width };
        enum EnvParam { attack, decay, sustain, release };
        enum LfoParam { lfoShape, rate, sync, division, retrigger, fade };

        constexpr int osc[2] = { at ("osc1_level"), at ("osc2_level") };
        constexpr int env[3] = { at ("amp_attack"), at ("fenv_attack"), at ("env3_attack") };
        constexpr int lfo[2] = { at ("lfo1_shape"), at ("lfo2_shape") };
        constexpr int mod = at ("mod1_source"), numMods = 6;

        static_assert (at ("osc2_width") == osc[1] + width && at ("env3_release") == env[2] + release
                       && at ("lfo2_fade") == lfo[1] + fade && at ("mod6_amount") == mod + numMods * 3 - 1);

        constexpr int fm = at ("fm"), sub = at ("sub_level"), noise = at ("noise_level"), noiseTone = at ("noise_tone");
        constexpr int filterMode = at ("filter_mode"), filterSlope = at ("filter_slope"), cutoff = at ("filter_cutoff"),
                      reso = at ("filter_reso"), filterDrive = at ("filter_drive"), filterEnv = at ("filter_env"),
                      keytrack = at ("filter_keytrack"), filterVelocity = at ("filter_velocity");
        constexpr int ampVelocity = at ("amp_velocity");
        constexpr int voiceMode = at ("voice_mode"), polyphony = at ("polyphony"), glide = at ("glide"), bendRange = at ("bend_range");
        constexpr int drive = at ("drive"), chorusMix = at ("chorus_mix"), chorusRate = at ("chorus_rate"), chorusDepth = at ("chorus_depth"),
                      delayMix = at ("delay_mix"), delayTime = at ("delay_time"), delayFeedback = at ("delay_feedback"),
                      delayTone = at ("delay_tone"), pingPong = at ("delay_pingpong"), reverbMix = at ("reverb_mix"),
                      reverbSize = at ("reverb_size"), reverbDecay = at ("reverb_decay"), reverbTone = at ("reverb_tone"),
                      volume = at ("volume");
    }

    enum Source { srcOff, srcLfo1, srcLfo2, srcFilterEnv, srcModEnv, srcVelocity, srcModWheel, srcAftertouch, srcKey, srcRandom, numSources };

    enum Dest { dOff, dPitch, dOsc1Pitch, dOsc2Pitch, dOsc1Shape, dOsc2Shape, dPulseWidth, dOsc1Level, dOsc2Level, dFm, dDetune, dSub,
                dNoise, dCutoff, dResonance, dDrive, dVolume, dPan, dLfo1Rate, dLfo2Rate, numDests };

    juce::NormalisableRange<float> rangeFor (const Info& p)
    {
        if (std::string_view (p.unit) == "amount")
            return { p.min, p.max, 0.0f, 0.5f, true };      // finer near zero, either way

        juce::NormalisableRange<float> r (p.min, p.max, p.step);

        if (p.centre > p.min && p.centre < p.max)
            r.setSkewForCentre (p.centre);

        return r;
    }

    //==============================================================================
    // Oscillators. The phase runs 0..1; every wave crosses zero rising at phase 0 so morphing
    // between them never cancels. PolyBLEP and PolyBLAMP round off the steps and corners
    // that would otherwise alias.
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    constexpr int maxUnison = 8, maxVoices = 32, controlStep = 8;

    inline double blep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0;
        }

        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }

        return 0.0;
    }

    inline double blamp (double t, double dt) noexcept
    {
        if (t < dt)
        {
            const double x = 1.0 - t / dt;
            return x * x * x / 6.0;
        }

        if (t > 1.0 - dt)
        {
            const double x = 1.0 - (1.0 - t) / dt;
            return x * x * x / 6.0;
        }

        return 0.0;
    }

    inline double wrap (double t) noexcept      { return t - std::floor (t); }
    inline double wrapOnce (double t) noexcept  { return t >= 1.0 ? t - 1.0 : t; }     // for 0..2

    inline float waveAt (int shape, double t, double dt, float pw) noexcept
    {
        switch (shape)
        {
            case 0:
                return std::sin ((float) (twoPi * t));

            case 1:
            {
                const double u = wrapOnce (t + 0.25);
                return (float) (1.0 - 4.0 * std::abs (u - 0.5) + 8.0 * dt * (blamp (u, dt) - blamp (wrapOnce (u + 0.5), dt)));
            }

            case 2:
            {
                const double u = wrapOnce (t + 0.5);
                return (float) (2.0 * u - 1.0 - blep (u, dt));
            }

            default:
            {
                const double naive = t < pw ? 1.0 : -1.0;
                return (float) (naive + blep (t, dt) - blep (wrapOnce (t + 1.0 - pw), dt) - (2.0 * pw - 1.0));
            }
        }
    }

    /** A morph position 0..3 as the wave below it and how far toward the next. */
    struct Morph
    {
        int index = 2;
        float frac = 0.0f;

        void set (float shape) noexcept
        {
            index = std::min (2, (int) shape);
            frac = shape - (float) index;

            if (frac > 0.999f && index == 2)
            {
                index = 3;
                frac = 0.0f;
            }
        }

        float at (double t, double dt, float pw) const noexcept
        {
            const float a = waveAt (index, t, dt, pw);
            return frac < 0.0005f ? a : a + (waveAt (index + 1, t, dt, pw) - a) * frac;
        }
    };

    inline float softClip (float x) noexcept
    {
        // Untouched below 0.9; above, eases toward 1 so nothing leaves past full scale
        const float a = std::abs (x);

        if (a <= 0.9f)
            return x;

        return std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) * 10.0f), x);
    }

    inline float tanhFast (float x) noexcept
    {
        x = juce::jlimit (-4.5f, 4.5f, x);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    //==============================================================================
    struct EnvShape
    {
        float attackCoef = 1.0f, decayCoef = 0.0f, sustain = 1.0f, releaseCoef = 0.0f;

        void set (float a, float d, float s, float r, double sr, float minAttack) noexcept
        {
            // The attack heads for 1.3 and stops at 1 (a curve like an analog one) in `a` seconds;
            // decay and release are exponential, 40 dB down in their time
            attackCoef = (float) (1.0 - std::exp (-1.466 / (std::max (a, minAttack) * sr)));
            decayCoef = (float) std::exp (-4.6 / (std::max (d, 0.001f) * sr));
            sustain = s;
            releaseCoef = (float) std::exp (-4.6 / (std::max (r, 0.001f) * sr));
        }
    };

    struct Envelope
    {
        enum Stage { idle, attack, decay, sustain, release };
        Stage stage = idle;
        float level = 0.0f;

        void trigger() noexcept         { stage = attack; }
        void letGo() noexcept           { if (stage != idle) stage = release; }

        float next (const EnvShape& e, float releaseCoef) noexcept
        {
            switch (stage)
            {
                case attack:
                    level += (1.3f - level) * e.attackCoef;

                    if (level >= 1.0f)
                    {
                        level = 1.0f;
                        stage = decay;
                    }
                    break;

                case decay:
                    level = e.sustain + (level - e.sustain) * e.decayCoef;

                    if (std::abs (level - e.sustain) < 1.0e-4f)
                        stage = sustain;
                    break;

                case sustain:
                    level += (e.sustain - level) * 0.002f;
                    break;

                case release:
                    level *= releaseCoef;

                    if (level < 1.0e-5f)
                    {
                        level = 0.0f;
                        stage = idle;
                    }
                    break;

                case idle:
                    break;
            }

            return level;
        }
    };

    struct Lfo
    {
        double phase = 0.0;
        float held = 0.0f, from = 0.0f, to = 0.0f;

        void restart (juce::Random& r) noexcept
        {
            phase = 0.0;
            from = held = r.nextFloat() * 2.0f - 1.0f;
            to = r.nextFloat() * 2.0f - 1.0f;
        }

        void advance (double cycles, juce::Random& r) noexcept
        {
            phase += cycles;

            if (phase >= 1.0)
            {
                phase -= std::floor (phase);
                from = to;
                held = to = r.nextFloat() * 2.0f - 1.0f;
            }
        }

        float value (int shape) const noexcept
        {
            const float p = (float) phase;

            switch (shape)
            {
                case 0:  return (float) std::sin (twoPi * phase);
                case 1:  return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
                case 2:  return 1.0f - 2.0f * p;
                case 3:  return p < 0.5f ? 1.0f : -1.0f;
                case 4:  return held;
                default: return from + (to - from) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * p));
            }
        }
    };

    struct Event
    {
        int sample = 0;
        juce::MidiMessage message;
    };
}

//==============================================================================
struct NovaPlugin::Engine
{
    double sr = 48000.0, bpm = 120.0;
    std::array<float, numParams> s {};
    EnvShape shapes[3];
    float killCoef = 0.0f;

    struct Voice
    {
        bool active = false, released = false, killed = false, held = false;
        int note = 60;
        float velocity = 1.0f, random = 0.0f;
        juce::uint32 age = 0;
        double pitch = 60.0, target = 60.0, time = 0.0;

        double phase[2][maxUnison] {}, inc[2][maxUnison] {}, subPhase = 0.0, subInc = 0.0;
        float gainL[2][maxUnison] {}, gainR[2][maxUnison] {};
        int voices[2] { 1, 1 };
        Morph morph[2];
        float pw[2] { 0.5f, 0.5f }, level[2] {}, fmDepth = 0.0f, sub = 0.0f, noise = 0.0f;
        float noiseCoef = 0.0f, noiseZ = 0.0f;
        float driveGain = 1.0f, driveNorm = 1.0f, gain = 1.0f, gainNow = 0.0f, panL = 1.0f, panR = 1.0f;
        int mode = 0;
        bool steep = true;
        dsp::Svf filters[2];
        Envelope env[3];
        Lfo lfo[2];
        float lfoRateMod[2] {};
        int countdown = 0, elapsed = 0;     // samples to the next control update, and since the last
    };

    std::array<Voice, maxVoices> voices {};
    Lfo freeLfo[2];
    juce::Random random;

    // The free-running LFOs' own: they turn whether notes play or not, so drawing from `random` made each
    // note's random start points depend on how long Nova had been running (a render's run-in, say)
    juce::Random freeRandom;
    juce::uint32 counter = 0;
    std::vector<int> stack;             // keys held, oldest first (mono and legato)
    double lastPitch = -1.0;
    float bend = 0.0f, modWheel = 0.0f, aftertouch = 0.0f;
    bool pedal = false;

    // Effects
    dsp::DelayLine chorusLines[2], delayLines[2];
    double chorusPhase = 0.0, delayNow = -1.0;
    dsp::OnePole delayLow[2], delayHigh[2];
    float lastDelayTone = -1.0f;

    static constexpr int numLines = 8, numDiffusers = 4;
    dsp::DelayLine reverbPre[2], lines[numLines], diffusers[2][numDiffusers];
    dsp::OnePole damping[numLines];
    double lfoPhase[numLines] {};
    float lastDamping = -1.0f;
    bool reverbClear = true;

    float volumeNow = -1.0f;
    float dcIn[2] {}, dcOut[2] {}, dcCoef = 0.999f;

    static constexpr double lineMs[numLines] = { 31.7, 37.9, 43.1, 47.3, 53.9, 59.3, 67.1, 73.7 };
    static constexpr double lineLfoHz[numLines] = { 0.31, 0.43, 0.53, 0.61, 0.71, 0.83, 0.97, 1.09 };
    static constexpr double diffuserMs[2][numDiffusers] = { { 4.77, 3.59, 12.73, 9.31 }, { 4.13, 3.97, 11.21, 8.73 } };

    static double lengthScale (double size)     { return 0.4 + 1.2 * size; }

    void prepare (double sampleRate)
    {
        sr = sampleRate;

        for (auto& l : chorusLines)
            l.prepare ((int) (0.03 * sr));

        for (auto& l : delayLines)
            l.prepare ((int) (10.0 * sr));

        for (auto& d : reverbPre)
            d.prepare ((int) (0.03 * sr));

        for (int i = 0; i < numLines; ++i)
            lines[i].prepare ((int) ((lineMs[i] * lengthScale (1.0) + 2.0) * 0.001 * sr));

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < numDiffusers; ++i)
                diffusers[ch][i].prepare ((int) (diffuserMs[ch][i] * 0.001 * sr) + 2);

        killCoef = (float) std::exp (-1.0 / (0.003 * sr));
        dcCoef = (float) (1.0 - twoPi * 8.0 / sr);
        reset();
    }

    void reset()
    {
        for (auto& v : voices)
            v = Voice();

        stack.clear();
        lastPitch = -1.0;
        bend = modWheel = aftertouch = 0.0f;
        pedal = false;

        for (auto& l : chorusLines)
            l.reset();

        for (auto& l : delayLines)
            l.reset();

        for (int ch = 0; ch < 2; ++ch)
        {
            delayLow[ch].z = delayHigh[ch].z = 0.0f;
            reverbPre[ch].reset();

            for (auto& d : diffusers[ch])
                d.reset();
        }

        for (auto& l : lines)
            l.reset();

        for (auto& d : damping)
            d.z = 0.0f;

        for (int i = 0; i < numLines; ++i)
            lfoPhase[i] = i / (double) numLines;

        delayNow = -1.0;
        dcIn[0] = dcIn[1] = dcOut[0] = dcOut[1] = 0.0f;
        lastDelayTone = lastDamping = -1.0f;
        reverbClear = true;
        volumeNow = -1.0f;
    }

    float p (int index) const noexcept          { return s[(size_t) index]; }
    int choice (int index) const noexcept       { return juce::roundToInt (s[(size_t) index]); }

    //==============================================================================
    void readSettings (const std::vector<Param*>& params)
    {
        for (int i = 0; i < numParams; ++i)
            s[(size_t) i] = params[(size_t) i]->get();

        settingsChanged();
    }

    void readSettings (const nova::Patch& patch)
    {
        for (int i = 0; i < numParams; ++i)
            s[(size_t) i] = patch.get (table[i].id);

        settingsChanged();
    }

    void settingsChanged()
    {
        for (int e = 0; e < 3; ++e)
        {
            const int base = ix::env[e];
            shapes[e].set (p (base + ix::attack), p (base + ix::decay), p (base + ix::sustain), p (base + ix::release), sr,
                           e == 0 ? 0.0006f : 0.0f);
        }
    }

    double lfoRate (int l) const
    {
        const int base = ix::lfo[l];

        if (choice (base + ix::sync) != 0)
            return bpm / 60.0 / beatsAt (choice (base + ix::division));

        return p (base + ix::rate);
    }

    //==============================================================================
    void handle (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber());
        else if (m.isPitchWheel())
            bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isSustainPedalOn())
            pedal = true;
        else if (m.isSustainPedalOff())
            pedalUp();
        else if (m.isControllerOfType (1))
            modWheel = (float) m.getControllerValue() / 127.0f;
        else if (m.isChannelPressure())
            aftertouch = (float) m.getChannelPressureValue() / 127.0f;
        else if (m.isAftertouch())
            aftertouch = (float) m.getAfterTouchValue() / 127.0f;
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            allOff();
    }

    void allOff()
    {
        for (auto& v : voices)
            kill (v);

        stack.clear();
        pedal = false;
    }

    void kill (Voice& v)
    {
        if (! v.active)
            return;

        v.killed = v.released = true;

        for (auto& e : v.env)
            e.letGo();
    }

    void release (Voice& v)
    {
        v.released = true;
        v.held = false;

        for (auto& e : v.env)
            e.letGo();
    }

    bool sounding (const Voice& v) const        { return v.active && ! v.released; }

    void start (Voice& v, int note, float velocity, double fromPitch)
    {
        const bool wasActive = v.active;
        v.active = true;
        v.released = v.killed = v.held = false;
        v.note = note;
        v.velocity = velocity;
        v.age = ++counter;
        v.pitch = fromPitch;
        v.target = note;
        v.time = 0.0;
        v.random = random.nextFloat() * 2.0f - 1.0f;
        v.countdown = v.elapsed = 0;

        for (auto& e : v.env)
            e.trigger();

        for (int l = 0; l < 2; ++l)
            if (choice (ix::lfo[l] + ix::retrigger) != 0)
                v.lfo[l].restart (random);

        if (! wasActive)
        {
            // A single oscillator starts at the same point for a key every time (a consistent
            // attack), but a different point from its neighbours, so a chord's notes don't all
            // peak together; unison copies start scattered, as analog ones would be
            for (int o = 0; o < 2; ++o)
            {
                const int n = juce::jlimit (1, maxUnison, choice (ix::osc[o] + ix::unison));

                for (int u = 0; u < maxUnison; ++u)
                    v.phase[o][u] = n > 1 ? random.nextDouble() : wrap (note * 0.618034 + o * 0.25);
            }

            v.subPhase = 0.0;
            v.noiseZ = 0.0f;
            v.gainNow = 0.0f;

            for (auto& f : v.filters)
                f.reset();

            for (auto& e : v.env)
                e.level = 0.0f;
        }
    }

    void noteOn (int note, float velocity)
    {
        const int mode = choice (ix::voiceMode);
        const bool gliding = p (ix::glide) > 0.0005f;

        if (mode == 0)
        {
            // Poly: past the voice limit, the oldest note gives way (fading out fast)
            const int limit = juce::jlimit (1, maxVoices, choice (ix::polyphony));
            int count = 0;

            for (auto& v : voices)
                count += sounding (v) ? 1 : 0;

            while (count >= limit)
            {
                Voice* oldest = nullptr;

                for (auto& v : voices)
                    if (sounding (v) && (oldest == nullptr || v.age < oldest->age))
                        oldest = &v;

                if (oldest == nullptr)
                    break;

                kill (*oldest);
                --count;
            }

            Voice* free = nullptr;

            for (auto& v : voices)
                if (! v.active)
                {
                    free = &v;
                    break;
                }

            // Every slot busy fading: take the quietest
            if (free == nullptr)
            {
                for (auto& v : voices)
                    if (free == nullptr || v.env[0].level < free->env[0].level)
                        free = &v;

                free->active = false;
            }

            start (*free, note, velocity, gliding && lastPitch >= 0.0 ? lastPitch : (double) note);
            lastPitch = note;
            return;
        }

        // Mono and legato: one voice, the newest key wins
        std::erase (stack, note);
        stack.push_back (note);
        auto& v = voices[0];

        for (size_t i = 1; i < voices.size(); ++i)
            kill (voices[i]);

        if (mode == 2 && sounding (v))
        {
            v.note = note;
            v.target = note;

            if (! gliding)
                v.pitch = note;
        }
        else
        {
            start (v, note, velocity, gliding && v.active ? v.pitch : (double) note);
        }

        lastPitch = note;
    }

    void noteOff (int note)
    {
        if (choice (ix::voiceMode) == 0)
        {
            for (auto& v : voices)
                if (sounding (v) && v.note == note)
                {
                    if (pedal)
                        v.held = true;
                    else
                        release (v);
                }

            return;
        }

        std::erase (stack, note);
        auto& v = voices[0];

        if (! sounding (v) || v.note != note)
            return;

        if (! stack.empty())
        {
            // Back to the key still held
            v.note = stack.back();
            v.target = v.note;

            if (p (ix::glide) <= 0.0005f)
                v.pitch = v.target;

            lastPitch = v.note;
        }
        else if (pedal)
        {
            v.held = true;
        }
        else
        {
            release (v);
        }
    }

    void pedalUp()
    {
        pedal = false;

        for (auto& v : voices)
            if (v.held && sounding (v))
                release (v);
    }

    //==============================================================================
    /** The mod matrix and everything that follows from it, once every few samples. */
    void control (Voice& v, const float* freeLfoValues)
    {
        const int length = v.elapsed;
        const double seconds = length / sr;
        v.time += seconds;

        // LFOs: each note's own, or the free-running pair
        float lfoValues[2];

        for (int l = 0; l < 2; ++l)
        {
            const int base = ix::lfo[l];
            float value;

            if (choice (base + ix::retrigger) != 0)
            {
                value = v.lfo[l].value (choice (base + ix::lfoShape));
                v.lfo[l].advance (lfoRate (l) * std::exp2 (4.0f * v.lfoRateMod[l]) * seconds, random);
            }
            else
            {
                value = freeLfoValues[l];
            }

            if (const float fade = p (base + ix::fade); fade > 0.001f)
                value *= (float) std::min (1.0, v.time / fade);

            lfoValues[l] = value;
        }

        float src[numSources];
        src[srcOff] = 0.0f;
        src[srcLfo1] = lfoValues[0];
        src[srcLfo2] = lfoValues[1];
        src[srcFilterEnv] = v.env[1].level;
        src[srcModEnv] = v.env[2].level;
        src[srcVelocity] = v.velocity;
        src[srcModWheel] = modWheel;
        src[srcAftertouch] = aftertouch;
        src[srcKey] = (float) (v.pitch - 60.0) / 24.0f;
        src[srcRandom] = v.random;

        float d[numDests] {};

        for (int m = 0; m < ix::numMods; ++m)
        {
            const int base = ix::mod + m * 3;
            const int source = juce::jlimit (0, numSources - 1, choice (base));
            const int dest = juce::jlimit (0, numDests - 1, choice (base + 1));

            if (source != srcOff && dest != dOff)
                d[dest] += src[source] * p (base + 2);
        }

        v.lfoRateMod[0] = d[dLfo1Rate];
        v.lfoRateMod[1] = d[dLfo2Rate];

        // Glide toward the note
        if (const float glide = p (ix::glide); glide > 0.0005f)
            v.pitch = v.target + (v.pitch - v.target) * std::exp (-length / (glide / 4.6 * sr));
        else
            v.pitch = v.target;

        const double basePitch = v.pitch + bend * p (ix::bendRange) + d[dPitch] * 12.0f;

        for (int o = 0; o < 2; ++o)
        {
            const int base = ix::osc[o];
            const double oscPitch = basePitch + 12.0 * choice (base + ix::octave) + choice (base + ix::semi)
                                     + p (base + ix::fine) * 0.01 + d[o == 0 ? dOsc1Pitch : dOsc2Pitch] * 12.0f;
            const double freq = 440.0 * std::exp2 ((oscPitch - 69.0) / 12.0);
            const int n = juce::jlimit (1, maxUnison, choice (base + ix::unison));
            const float det = juce::jlimit (0.0f, 1.0f, p (base + ix::detune) + d[dDetune]);
            const float cents = 60.0f * det * det + 20.0f * det;
            const float width = p (base + ix::width);
            const float norm = 1.0f / std::sqrt ((float) n);

            v.voices[o] = n;

            for (int u = 0; u < n; ++u)
            {
                const float x = n > 1 ? 2.0f * u / (n - 1) - 1.0f : 0.0f;
                v.inc[o][u] = std::min (0.45, freq * std::exp2 (x * cents / 1200.0f) / sr);

                const float angle = (x * width + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                v.gainL[o][u] = std::cos (angle) * juce::MathConstants<float>::sqrt2 * norm;
                v.gainR[o][u] = std::sin (angle) * juce::MathConstants<float>::sqrt2 * norm;
            }

            v.morph[o].set (juce::jlimit (0.0f, 3.0f, p (base + ix::shape) + d[o == 0 ? dOsc1Shape : dOsc2Shape] * 3.0f));
            v.pw[o] = juce::jlimit (0.05f, 0.95f, p (base + ix::pw) + d[dPulseWidth] * 0.45f);
            v.level[o] = juce::jlimit (0.0f, 1.0f, p (base + ix::level) + d[o == 0 ? dOsc1Level : dOsc2Level]);

            if (o == 0)
                v.subInc = std::min (0.45, freq * 0.5 / sr);
        }

        const float fm = juce::jlimit (0.0f, 1.0f, p (ix::fm) + d[dFm]);
        v.fmDepth = 1.5f * fm * fm;
        v.sub = juce::jlimit (0.0f, 1.0f, p (ix::sub) + d[dSub]);
        v.noise = juce::jlimit (0.0f, 1.0f, p (ix::noise) + d[dNoise]);

        const double noiseHz = 200.0 * std::exp2 (7.0 * p (ix::noiseTone));
        v.noiseCoef = noiseHz >= sr * 0.45 ? 0.0f : (float) std::exp (-twoPi * noiseHz / sr);

        // The filter: cutoff in octaves, moved by the envelope, the key, velocity and the matrix
        const double octaves = std::log2 (p (ix::cutoff)) + v.env[1].level * p (ix::filterEnv)
                               + p (ix::keytrack) * (v.pitch - 60.0) / 12.0
                               + v.velocity * 4.0 * p (ix::filterVelocity) + d[dCutoff] * 8.0f;
        const double cutoff = juce::jlimit (20.0, std::min (20000.0, sr * 0.45), std::exp2 (octaves));
        const double q = 0.7071 * std::pow (35.0, (double) juce::jlimit (0.0f, 1.0f, p (ix::reso) + d[dResonance]));

        v.mode = juce::jlimit (0, 3, choice (ix::filterMode));
        v.steep = choice (ix::filterSlope) != 0;

        if (v.steep)
        {
            v.filters[0].set (cutoff, 0.7071, sr);
            v.filters[1].set (cutoff, q, sr);
        }
        else
        {
            v.filters[0].set (cutoff, q, sr);
        }

        const float drive = juce::jlimit (0.0f, 1.0f, p (ix::filterDrive) + d[dDrive]);
        v.driveGain = 1.0f + 15.0f * drive * drive;
        v.driveNorm = 1.0f / std::sqrt (v.driveGain);

        // Loudness: velocity, the matrix, then pan
        const float av = p (ix::ampVelocity);
        v.gain = (1.0f - av + av * v.velocity * v.velocity) * std::max (0.0f, 1.0f + d[dVolume]);

        const float pan = juce::jlimit (-1.0f, 1.0f, d[dPan]);
        const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        v.panL = std::cos (angle) * juce::MathConstants<float>::sqrt2;
        v.panR = std::sin (angle) * juce::MathConstants<float>::sqrt2;
    }

    float filter (Voice& v, float x, int ch) noexcept
    {
        auto pick = [&v] (const dsp::Svf::Out& o, double in, const dsp::Svf& f)
        {
            switch (v.mode)
            {
                case 1:  return o.band * f.k;           // band pass, unity at the peak
                case 2:  return o.high;
                case 3:  return in - f.k * o.band;      // notch
                default: return o.low;
            }
        };

        double y = pick (v.filters[0].process (x, ch), x, v.filters[0]);

        if (v.steep)
            y = pick (v.filters[1].process (y, ch), y, v.filters[1]);

        return (float) y;
    }

    /** Up to controlStep samples of one stack of unison copies, added into l and r (and its
        middle copy's raw wave into `tap`, for FM). */
    static void renderStack (Voice& v, int o, int length, float* l, float* r, const float* phaseMod, float* tap, bool heard) noexcept
    {
        const int tapVoice = v.voices[o] / 2;
        const float level = v.level[o], pw = v.pw[o];
        const auto& morph = v.morph[o];

        for (int u = 0; u < v.voices[o]; ++u)
        {
            const bool tapped = tap != nullptr && u == tapVoice;

            if (! heard && ! tapped)
                continue;

            double ph = v.phase[o][u];
            const double inc = v.inc[o][u];
            const float gl = heard ? v.gainL[o][u] * level : 0.0f, gr = heard ? v.gainR[o][u] * level : 0.0f;

            for (int i = 0; i < length; ++i)
            {
                const float w = morph.at (phaseMod != nullptr ? wrap (ph + phaseMod[i]) : ph, inc, pw);

                if (tapped)
                    tap[i] = w;

                l[i] += w * gl;
                r[i] += w * gr;

                if ((ph += inc) >= 1.0)
                    ph -= 1.0;
            }

            v.phase[o][u] = ph;
        }
    }

    void renderVoice (Voice& v, float* left, float* right, int length)
    {
        jassert (length <= controlStep);
        float l[controlStep] {}, r[controlStep] {}, modulator[controlStep] {};
        const bool fmOn = v.fmDepth > 0.00001f;

        // Osc 2 first: it's what bends osc 1's phase
        if (v.level[1] > 0.0001f || fmOn)
            renderStack (v, 1, length, l, r, nullptr, fmOn ? modulator : nullptr, v.level[1] > 0.0001f);

        if (fmOn)
            for (int i = 0; i < length; ++i)
                modulator[i] *= v.fmDepth;

        if (v.level[0] > 0.0001f)
            renderStack (v, 0, length, l, r, fmOn ? modulator : nullptr, nullptr, true);

        const float releaseCoef = v.killed ? killCoef : shapes[0].releaseCoef;
        const bool driven = v.driveGain > 1.001f;

        for (int i = 0; i < length; ++i)
        {
            const float amp = v.env[0].next (shapes[0], releaseCoef);
            v.env[1].next (shapes[1], shapes[1].releaseCoef);
            v.env[2].next (shapes[2], shapes[2].releaseCoef);

            float sl = l[i], sr = r[i];

            if (v.sub > 0.0001f)
            {
                const float w = std::sin ((float) (twoPi * v.subPhase)) * v.sub;
                sl += w;
                sr += w;
            }

            if ((v.subPhase += v.subInc) >= 1.0)
                v.subPhase -= 1.0;

            if (v.noise > 0.0001f)
            {
                const float white = random.nextFloat() * 2.0f - 1.0f;
                v.noiseZ = white + v.noiseCoef * (v.noiseZ - white);
                const float n = v.noiseZ * v.noise * (1.0f + 1.5f * v.noiseCoef);
                sl += n;
                sr += n;
            }

            if (driven)
            {
                sl = tanhFast (sl * v.driveGain) * v.driveNorm;
                sr = tanhFast (sr * v.driveGain) * v.driveNorm;
            }

            sl = filter (v, sl, 0);
            sr = filter (v, sr, 1);

            v.gainNow += (v.gain - v.gainNow) * 0.02f;
            const float g = amp * v.gainNow;
            left[i] += sl * g * v.panL;
            right[i] += sr * g * v.panR;
        }

        // Done once silent: let go and faded, or holding at a sustain of nothing
        const auto& e = v.env[0];

        if (e.stage == Envelope::idle || (e.stage == Envelope::sustain && shapes[0].sustain < 1.0e-4f && e.level < 1.0e-4f))
            v.active = false;
    }

    //==============================================================================
    void render (float* left, float* right, int numSamples, const std::vector<Event>& events)
    {
        size_t next = 0;

        for (int pos = 0; pos < numSamples; pos += controlStep)
        {
            const int length = std::min (controlStep, numSamples - pos);

            while (next < events.size() && events[next].sample < pos + length)
                handle (events[next++].message);

            float freeValues[2];

            for (int l = 0; l < 2; ++l)
            {
                freeValues[l] = freeLfo[l].value (choice (ix::lfo[l] + ix::lfoShape));
                freeLfo[l].advance (lfoRate (l) * length / sr, freeRandom);
            }

            for (auto& v : voices)
            {
                for (int done = 0; done < length && v.active;)
                {
                    if (v.countdown <= 0)
                    {
                        control (v, freeValues);
                        v.countdown = controlStep;
                        v.elapsed = 0;
                    }

                    const int n = std::min (length - done, v.countdown);
                    renderVoice (v, left + pos + done, right + pos + done, n);
                    v.countdown -= n;
                    v.elapsed += n;
                    done += n;
                }
            }
        }

        effects (left, right, numSamples);
    }

    void effects (float* left, float* right, int numSamples)
    {
        // Drive
        if (const float drive = p (ix::drive); drive > 0.001f)
        {
            const float g = 1.0f + 12.0f * drive * drive, norm = 1.0f / std::sqrt (g);

            for (int i = 0; i < numSamples; ++i)
            {
                left[i] = tanhFast (left[i] * g) * norm;
                right[i] = tanhFast (right[i] * g) * norm;
            }
        }

        chorus (left, right, numSamples);
        delay (left, right, numSamples);
        reverb (left, right, numSamples);

        const float target = juce::Decibels::decibelsToGain (p (ix::volume));

        if (volumeNow < 0.0f)
            volumeNow = target;

        // Out through a DC blocker (drive and short notes can leave a little offset)
        float* sides[2] = { left, right };

        for (int i = 0; i < numSamples; ++i)
        {
            volumeNow += (target - volumeNow) * 0.002f;

            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = sides[ch][i];
                dcOut[ch] = x - dcIn[ch] + dcCoef * dcOut[ch];
                dcIn[ch] = x;
                sides[ch][i] = softClip (dcOut[ch] * volumeNow);
            }
        }
    }

    void chorus (float* left, float* right, int numSamples)
    {
        const float mix = p (ix::chorusMix);
        const double rate = p (ix::chorusRate) / sr;
        const double baseMs = 7.0, depthMs = 5.0 * p (ix::chorusDepth);
        const float dry = 1.0f - 0.35f * mix, wet = 0.8f * mix;

        for (int i = 0; i < numSamples; ++i)
        {
            chorusLines[0].push (left[i]);
            chorusLines[1].push (right[i]);

            if (mix > 0.001f)
            {
                const double a = std::sin (twoPi * chorusPhase), b = std::sin (twoPi * (chorusPhase + 0.25));
                const float wl = chorusLines[0].read ((baseMs + depthMs * (0.5 + 0.5 * a)) * 0.001 * sr);
                const float wr = chorusLines[1].read ((baseMs + depthMs * (0.5 + 0.5 * b)) * 0.001 * sr);
                left[i] = left[i] * dry + wl * wet;
                right[i] = right[i] * dry + wr * wet;
            }

            if ((chorusPhase += rate) >= 1.0)
                chorusPhase -= 1.0;
        }
    }

    void delay (float* left, float* right, int numSamples)
    {
        const float mix = p (ix::delayMix);

        if (const float tone = p (ix::delayTone); tone != lastDelayTone)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                delayLow[ch].setCutoff (1000.0 * std::exp2 (4.0 * tone), sr);
                delayHigh[ch].setCutoff (150.0, sr);
            }

            lastDelayTone = tone;
        }

        const double target = juce::jlimit (64.0, 9.9 * sr, beatsAt (choice (ix::delayTime)) * 60.0 / bpm * sr);

        if (delayNow < 0.0)
            delayNow = target;

        const float feedback = p (ix::delayFeedback);
        const bool pingPong = choice (ix::pingPong) != 0;

        for (int i = 0; i < numSamples; ++i)
        {
            delayNow += (target - delayNow) * 0.0005;

            float echo[2];

            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = delayLines[ch].read (delayNow);
                const float low = delayLow[ch].process (x);
                echo[ch] = low - delayHigh[ch].process (low);
            }

            if (pingPong)
            {
                delayLines[0].push (0.5f * (left[i] + right[i]) * (mix > 0.0005f ? 1.0f : 0.0f) + echo[1] * feedback);
                delayLines[1].push (echo[0] * feedback);
            }
            else
            {
                delayLines[0].push ((mix > 0.0005f ? left[i] : 0.0f) + echo[0] * feedback);
                delayLines[1].push ((mix > 0.0005f ? right[i] : 0.0f) + echo[1] * feedback);
            }

            left[i] += echo[0] * mix;
            right[i] += echo[1] * mix;
        }
    }

    void reverb (float* left, float* right, int numSamples)
    {
        const float mix = p (ix::reverbMix);

        if (mix < 0.0005f)
        {
            // Off: clear it once so turning it back on doesn't replay an old tail
            if (! reverbClear)
            {
                for (auto& l : lines)
                    l.reset();

                for (int ch = 0; ch < 2; ++ch)
                {
                    reverbPre[ch].reset();

                    for (auto& d : diffusers[ch])
                        d.reset();
                }

                for (auto& d : damping)
                    d.z = 0.0f;

                reverbClear = true;
            }

            return;
        }

        reverbClear = false;

        if (const float tone = p (ix::reverbTone); tone != lastDamping)
        {
            for (auto& f : damping)
                f.setCutoff (1500.0 * std::exp2 (3.5 * tone), sr);

            lastDamping = tone;
        }

        const double size = p (ix::reverbSize), scale = lengthScale (size) * 0.001 * sr;
        const double rt60 = p (ix::reverbDecay);
        const double modDepth = 0.00025 * sr * (0.5 + size);
        const int preDelay = 1 + (int) (0.012 * sr);
        const float diffusion = 0.62f, inputGain = 0.35f, norm = 1.0f / std::sqrt ((float) numLines);

        float feedback[numLines];

        for (int l = 0; l < numLines; ++l)
            feedback[l] = (float) std::pow (10.0, -3.0 * lineMs[l] * lengthScale (size) * 0.001 / rt60);

        for (int i = 0; i < numSamples; ++i)
        {
            float x[2] = { left[i], right[i] };

            for (int ch = 0; ch < 2; ++ch)
            {
                reverbPre[ch].push (x[ch]);
                float v = reverbPre[ch].tap (preDelay);

                for (int d = 0; d < numDiffusers; ++d)
                {
                    auto& ap = diffusers[ch][d];
                    const float delayed = ap.tap ((int) (diffuserMs[ch][d] * 0.001 * sr));
                    const float w = v - diffusion * delayed;
                    ap.push (w);
                    v = diffusion * w + delayed;
                }

                x[ch] = v * inputGain;
            }

            float y[numLines], h[numLines];

            for (int l = 0; l < numLines; ++l)
            {
                y[l] = h[l] = damping[l].process (lines[l].read (lineMs[l] * scale + modDepth * std::sin (twoPi * lfoPhase[l])));

                if ((lfoPhase[l] += lineLfoHz[l] / sr) >= 1.0)
                    lfoPhase[l] -= 1.0;
            }

            for (int span = 1; span < numLines; span *= 2)
                for (int start = 0; start < numLines; start += span * 2)
                    for (int j = start; j < start + span; ++j)
                    {
                        const float a = h[j], b = h[j + span];
                        h[j] = a + b;
                        h[j + span] = a - b;
                    }

            for (int l = 0; l < numLines; ++l)
                lines[l].push (h[l] * norm * feedback[l] + x[l & 1]);

            left[i] += 0.5f * (y[0] - y[2] + y[4] - y[6] + 0.5f * (y[1] + y[5])) * mix;
            right[i] += 0.5f * (y[1] - y[3] + y[5] - y[7] + 0.5f * (y[2] + y[6])) * mix;
        }
    }
};

//==============================================================================
namespace nova
{
    const std::vector<ParamInfo>& params()
    {
        static const std::vector<ParamInfo> list (std::begin (table), std::end (table));
        return list;
    }

    const ParamInfo* findInfo (const juce::String& id)
    {
        for (auto& p : params())
            if (id == p.id)
                return &p;

        return nullptr;
    }

    double divisionBeats (int index)
    {
        return beatsAt (index);
    }

    std::vector<Note> phraseFor (const juce::String& category)
    {
        if (category == "Bass")
            return { { 36, 0.0, 1.0 }, { 43, 1.0, 1.0 }, { 41, 2.0, 1.5 } };

        if (category == "Lead")
            return { { 72, 0.0, 1.0 }, { 74, 1.0, 1.0 }, { 76, 2.0, 1.5 } };

        if (category == "Texture")
            return { { 48, 0.0, 8.0 }, { 55, 0.0, 8.0 } };

        return { { 60, 0.0, 1.75 }, { 64, 0.0, 1.75 }, { 67, 0.0, 1.75 }, { 57, 2.0, 1.5 }, { 60, 2.0, 1.5 }, { 64, 2.0, 1.5 } };
    }

    float loudness (const Patch& patch)
    {
        constexpr double rate = 48000.0, bpm = 120.0;
        constexpr int block = 256;

        // Seeded, so a sound measures the same every time
        NovaPlugin::Engine engine;
        engine.random.setSeed (1);
        engine.freeRandom.setSeed (2);      // as a seeded NovaPlugin seeds them
        engine.prepare (rate);
        engine.bpm = bpm;
        engine.readSettings (patch);

        // The phrase as note ons and offs, by sample
        const auto notes = phraseFor (patch.category);
        std::vector<std::pair<int, juce::MidiMessage>> timeline;
        double end = 0.0;

        for (auto& n : notes)
        {
            const auto beatSamples = 60.0 / bpm * rate;
            timeline.push_back ({ (int) (n.start * beatSamples), juce::MidiMessage::noteOn (1, n.key, (juce::uint8) 100) });
            timeline.push_back ({ (int) ((n.start + n.beats) * beatSamples), juce::MidiMessage::noteOff (1, n.key) });
            end = std::max (end, (n.start + n.beats) * 60.0 / bpm);
        }

        std::stable_sort (timeline.begin(), timeline.end(), [] (auto& a, auto& b) { return a.first < b.first; });

        // K-weighted (a high pass, then a lift above 1.5 kHz): the loudest 400 ms while it plays
        dsp::Biquad highPass, shelf;
        highPass.c = dsp::Coeffs::highPass (60.0, 0.5, rate);
        shelf.c = dsp::Coeffs::highShelf (1500.0, 4.0, rate);

        const int total = (int) (end * rate), window = (int) (0.4 * rate), hop = (int) (0.1 * rate);
        std::vector<double> squared ((size_t) total + block, 0.0);
        std::vector<float> left (block), right (block);
        std::vector<Event> events;
        size_t next = 0;

        for (int done = 0; done < total; done += block)
        {
            events.clear();

            while (next < timeline.size() && timeline[next].first < done + block)
            {
                events.push_back ({ std::max (0, timeline[next].first - done), timeline[next].second });
                ++next;
            }

            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);
            engine.render (left.data(), right.data(), block, events);

            for (int i = 0; i < block; ++i)
            {
                const float l = shelf.process (highPass.process (left[(size_t) i], 0), 0);
                const float r = shelf.process (highPass.process (right[(size_t) i], 1), 1);
                squared[(size_t) (done + i)] = 0.5 * ((double) l * l + (double) r * r);
            }
        }

        double loudest = 1.0e-20;

        for (int start = 0; start + window <= total; start += hop)
            loudest = std::max (loudest, std::accumulate (squared.begin() + start, squared.begin() + start + window, 0.0) / window);

        return (float) (10.0 * std::log10 (loudest));
    }

    Patch levelled (Patch patch, float targetDb)
    {
        // Twice: the soft clip and drive mean a level change isn't quite linear
        for (int pass = 0; pass < 2; ++pass)
        {
            const float change = targetDb - loudness (patch);

            if (! std::isfinite (change) || std::abs (change) < 0.3f)
                break;

            patch.settings["volume"] = juce::jlimit (-30.0f, 6.0f, patch.get ("volume") + change);
        }

        return patch;
    }

    juce::String valueText (const ParamInfo& p, float v)
    {
        const juce::String unit (p.unit);

        if (p.isChoice())
            return p.choiceNames()[juce::jlimit (0, (int) p.max, juce::roundToInt (v))];

        if (unit == "wave")
        {
            static const char* names[] = { "Sine", "Triangle", "Saw", "Square" };
            const int below = std::min (2, (int) v);
            const float frac = v - (float) below;

            if (frac < 0.02f || frac > 0.98f)
                return names[juce::roundToInt (v)];

            return juce::String (names[below]) + " > " + names[below + 1] + " " + juce::String (juce::roundToInt (frac * 100.0f)) + "%";
        }

        if (unit == "Hz")
            return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (v, v < 10.0f ? 2 : 0) + " Hz";

        if (unit == "s")
            return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s";

        if (unit == "dB")
            return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB";

        if (unit == "%")
            return juce::String (juce::roundToInt (v * 100.0f)) + "%";

        if (unit == "amount")
            return (v > 0.004f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + "%";

        if (unit == "oct")
            return p.step > 0.0f ? (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v))
                                 : (v > 0.04f ? "+" : "") + juce::String (v, 1) + " oct";

        if (unit == "st")
            return (v > 0.0f && p.min < 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " st";

        if (unit == "cents")
            return (v > 0.4f ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct";

        return juce::String (juce::roundToInt (v));
    }

    float Patch::get (const juce::String& id) const
    {
        if (auto it = settings.find (id); it != settings.end())
            return it->second;

        auto info = findInfo (id);
        return info != nullptr ? info->defaultValue : 0.0f;
    }

    juce::var Patch::toVar() const
    {
        auto* values = new juce::DynamicObject();

        // In the editor's order, so files read top to bottom like the synth
        for (auto& p : params())
            if (auto it = settings.find (p.id); it != settings.end())
                values->setProperty (p.id, std::round ((double) it->second * 10000.0) / 10000.0);

        auto* o = new juce::DynamicObject();
        o->setProperty ("name", name);
        o->setProperty ("category", category);
        o->setProperty ("settings", juce::var (values));
        return juce::var (o);
    }

    std::optional<Patch> Patch::fromVar (const juce::var& v)
    {
        auto* values = v.getProperty ("settings", {}).getDynamicObject();

        if (values == nullptr)
            return {};

        Patch patch;
        patch.name = v.getProperty ("name", {}).toString().trim();
        patch.category = v.getProperty ("category", {}).toString().trim();

        for (auto& setting : values->getProperties())
        {
            auto info = findInfo (setting.name.toString());
            const auto& value = setting.value;

            if (info == nullptr)
                continue;

            float number;

            if (value.isString() && info->isChoice())
            {
                const int index = info->choiceNames().indexOf (value.toString().trim(), true);

                if (index < 0)
                    continue;

                number = (float) index;
            }
            else if (value.isString())
            {
                number = value.toString().getFloatValue();
            }
            else
            {
                number = (float) (double) value;
            }

            if (! std::isfinite (number))
                continue;

            number = juce::jlimit (info->min, info->max, number);

            if (info->step > 0.0f)
                number = std::round (number);

            patch.settings[info->id] = number;
        }

        return patch;
    }
}

//==============================================================================
NovaPlugin::NovaPlugin (tracktion::PluginCreationInfo info)
    : StockEffect (info), engine (std::make_unique<Engine>())
{
    for (auto& p : nova::params())
    {
        const auto* infoPtr = &p;
        byIndex.push_back (&addParam (p.id, p.name, p.defaultValue, rangeFor (p),
                                      [infoPtr] (float v) { return nova::valueText (*infoPtr, v); }));
    }
}

NovaPlugin::~NovaPlugin()
{
    notifyListenersOfDeletion();
}

void NovaPlugin::prepare()
{
    engine->prepare (sampleRate);

    if (seedForTests.has_value())
    {
        engine->random.setSeed (*seedForTests);
        engine->freeRandom.setSeed (*seedForTests + 1);
    }
}

void NovaPlugin::reset()
{
    engine->reset();
}

double NovaPlugin::getTailLength() const
{
    double tail = findParam ("amp_release")->get() * 2.0;

    if (findParam ("reverb_mix")->get() > 0.0005f)
        tail += findParam ("reverb_decay")->get();

    if (findParam ("delay_mix")->get() > 0.0005f)
        tail += 3.0;

    return tail;
}

void NovaPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.bufferNumSamples == 0)
        return;

    auto& buffer = *fc.destBuffer;
    const int numSamples = fc.bufferNumSamples;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, fc.bufferStartSample, numSamples);

    juce::ScopedNoDenormals noDenormals;
    tempoPosition.set (fc.editTime.getStart());
    engine->bpm = juce::jlimit (20.0, 999.0, tempoPosition.getTempo());
    engine->readSettings (byIndex);

    // This block's MIDI, each at its sample
    thread_local std::vector<Event> events;
    events.clear();

    if (fc.bufferForMidiMessages != nullptr)
    {
        if (fc.bufferForMidiMessages->isAllNotesOff)
            engine->allOff();

        for (auto& m : *fc.bufferForMidiMessages)
            events.push_back ({ juce::jlimit (0, numSamples - 1, juce::roundToInt (m.getTimeStamp() * sampleRate)), m });

        std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.sample < b.sample; });
    }

    float* left = buffer.getWritePointer (0, fc.bufferStartSample);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, fc.bufferStartSample) : nullptr;

    thread_local std::vector<float> spare;

    if (right == nullptr)
    {
        spare.assign ((size_t) numSamples, 0.0f);
        right = spare.data();
    }

    engine->render (left, right, numSamples, events);

    // Something went badly wrong (it shouldn't): silence rather than noise
    if (! std::isfinite (left[0]) || ! std::isfinite (left[numSamples - 1]))
    {
        engine->reset();
        buffer.clear (fc.bufferStartSample, numSamples);
    }

    int count = 0;

    for (auto& v : engine->voices)
        count += v.active ? 1 : 0;

    activeVoices.store (count);
}

void NovaPlugin::applyPatch (const nova::Patch& patch)
{
    for (size_t i = 0; i < byIndex.size(); ++i)
        byIndex[i]->set (patch.get (table[i].id));
}

nova::Patch NovaPlugin::currentPatch() const
{
    nova::Patch patch;

    for (size_t i = 0; i < byIndex.size(); ++i)
        if (const float v = byIndex[i]->get(); std::abs (v - table[i].defaultValue) > 1.0e-5f)
            patch.settings[table[i].id] = v;

    return patch;
}

} // namespace nightshift
