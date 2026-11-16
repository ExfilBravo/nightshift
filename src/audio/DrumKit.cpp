// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "DrumKit.h"
#include "model/Paths.h"
#include "DrumSynth.h"

namespace nightshift::kit
{

namespace
{
    using namespace drumsynth;

    constexpr int kitVersion = 2;
    constexpr double kitSampleRate = 48000.0;

    //==============================================================================
    juce::AudioBuffer<float> kick (double sr)
    {
        Noise noise (11);
        auto clickFilter = Filter::highPass (sr, 1800.0);
        double phase = 0.0;

        auto b = generate (sr, 0.5, [&] (double t)
        {
            // Fast chirp for the beater, slower drop for the body
            const double hz = 47.0 + 175.0 * decay (t, 0.026) + 380.0 * decay (t, 0.0035);
            phase += twoPi * hz / sr;

            const double amp = decay (t, 0.22) * (1.0 - juce::jlimit (0.0, 1.0, (t - 0.36) / 0.14));
            const float body = (float) (std::sin (phase) * amp);
            const float click = clickFilter (noise()) * (float) decay (t, 0.0022) * 0.5f;

            return saturate (body + click, 1.8f);
        });

        normalise (b, -0.5f);
        fadeOut (b, sr, 8.0);
        return b;
    }

    juce::AudioBuffer<float> snare (double sr)
    {
        Noise noise (23);
        auto bodyHigh = Filter::highPass (sr, 1500.0);
        auto bodyLow = Filter::lowPass (sr, 10000.0);
        auto snapBand = Filter::bandPass (sr, 5000.0, 0.8);
        double p1 = 0.0, p2 = 0.0;

        auto b = generate (sr, 0.42, [&] (double t)
        {
            p1 += twoPi * 185.0 * (1.0 + 0.35 * decay (t, 0.008)) / sr;
            p2 += twoPi * 332.0 * (1.0 + 0.25 * decay (t, 0.008)) / sr;

            const float tone = (float) ((0.62 * std::sin (p1) + 0.38 * std::sin (p2)) * decay (t, 0.05));
            const float n = noise();
            const float wires = bodyLow (bodyHigh (n)) * (float) (decay (t, 0.12) * juce::jmin (1.0, t / 0.0004));
            const float snap = snapBand (n) * (float) decay (t, 0.012);

            return saturate (tone * 0.9f + wires * 0.65f + snap * 0.35f, 1.5f);
        });

        normalise (b, -1.0f);
        fadeOut (b, sr, 10.0);
        return b;
    }

    juce::AudioBuffer<float> clap (double sr)
    {
        Noise noise (37);
        auto lowCut = Filter::highPass (sr, 700.0);
        auto band = Filter::bandPass (sr, 1250.0, 1.1);
        auto air = Filter::highPass (sr, 6000.0);

        const double burstTimes[] = { 0.0, 0.0085, 0.017, 0.0255 };
        const double burstLevels[] = { 0.9, 0.75, 0.8, 1.0 };

        auto b = generate (sr, 0.6, [&] (double t)
        {
            double env = 0.0;

            for (int i = 0; i < 4; ++i)
                if (t >= burstTimes[i])
                    env = std::max (env, burstLevels[i] * decay (t - burstTimes[i], 0.0032));

            if (t >= burstTimes[3])
                env = std::max (env, 0.75 * decay (t - burstTimes[3], 0.14));

            const float n = noise();
            return saturate ((band (lowCut (n)) + air (n) * 0.18f) * (float) env, 1.3f);
        });

        normalise (b, -1.0f);
        fadeOut (b, sr, 15.0);
        return b;
    }

    juce::AudioBuffer<float> hat (double sr, double length, double tau, juce::int64 seed)
    {
        // The classic six-oscillator metal, filtered down to its top end
        std::vector<BlepSquare> oscs;

        for (double hz : { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 })
            oscs.emplace_back (hz, sr);

        Noise noise (seed);
        auto band = Filter::bandPass (sr, 8500.0, 0.9);
        auto cut1 = Filter::highPass (sr, 6500.0);
        auto cut2 = Filter::highPass (sr, 6500.0);
        auto noiseCut = Filter::highPass (sr, 7000.0);
        auto topCut = Filter::lowPass (sr, 14000.0);   // keeps the sizzle from turning brittle

        auto b = generate (sr, length, [&] (double t)
        {
            float metal = 0.0f;

            for (auto& o : oscs)
                metal += o.next();

            metal = cut2 (cut1 (band (metal / 6.0f)));
            const double env = decay (t, tau) * juce::jmin (1.0, t / 0.00015)
                                 * (1.0 - juce::jlimit (0.0, 1.0, (t - length * 0.8) / (length * 0.2)));

            return topCut (metal + noiseCut (noise()) * 0.2f) * (float) env;
        });

        normalise (b, -2.0f);
        return b;
    }

    juce::AudioBuffer<float> rim (double sr)
    {
        Noise noise (53);
        auto band = Filter::bandPass (sr, 3000.0, 1.0);

        auto b = generate (sr, 0.1, [&] (double t)
        {
            const double tone = std::sin (twoPi * 1720.0 * t) * decay (t, 0.012) * 0.7
                              + std::sin (twoPi * 510.0 * t) * decay (t, 0.018) * 0.6;
            return saturate ((float) tone + band (noise()) * (float) decay (t, 0.0015) * 0.5f, 2.0f);
        });

        normalise (b, -2.0f);
        fadeOut (b, sr, 5.0);
        return b;
    }

    juce::AudioBuffer<float> shaker (double sr)
    {
        Noise noise (71);
        auto cut = Filter::highPass (sr, 5000.0);
        auto band = Filter::bandPass (sr, 8000.0, 0.7);
        const double attack = 0.012;

        auto b = generate (sr, 0.16, [&] (double t)
        {
            const double env = t < attack ? (t / attack) * (t / attack) : decay (t - attack, 0.045);
            return band (cut (noise())) * (float) env;
        });

        normalise (b, -3.0f);
        fadeOut (b, sr, 10.0);
        return b;
    }

    juce::AudioBuffer<float> bass808 (double sr)
    {
        const double rootHz = 65.406;   // C2
        double phase = 0.0;

        auto b = generate (sr, 2.4, [&] (double t)
        {
            phase += twoPi * rootHz * (1.0 + 0.55 * decay (t, 0.02)) / sr;
            const double amp = juce::jmin (1.0, t / 0.003) * decay (t, 1.05);
            return saturate ((float) (std::sin (phase) * amp), 2.2f);
        });

        normalise (b, -1.0f);
        fadeOut (b, sr, 60.0);
        return b;
    }
}

//==============================================================================
juce::StringArray getSoundNames()
{
    return { "Kick", "Snare", "Clap", "Hat", "Open Hat", "Rim", "Shaker", "808" };
}

juce::AudioBuffer<float> render (const juce::String& name, double sr)
{
    if (name == "Kick")      return kick (sr);
    if (name == "Snare")     return snare (sr);
    if (name == "Clap")      return clap (sr);
    if (name == "Hat")       return hat (sr, 0.14, 0.032, 89);
    if (name == "Open Hat")  return hat (sr, 0.85, 0.24, 97);
    if (name == "Rim")       return rim (sr);
    if (name == "Shaker")    return shaker (sr);
    if (name == "808")       return bass808 (sr);

    jassertfalse;
    return {};
}

juce::File getFolder()
{
    return paths::appData().getChildFile ("Kit");
}

juce::File fileFor (const juce::String& name)
{
    return getFolder().getChildFile (name + ".wav");
}

bool writeTo (const juce::File& folder)
{
    folder.createDirectory();
    bool ok = true;

    for (auto& name : getSoundNames())
        ok = writeWav (folder.getChildFile (name + ".wav"), render (name, kitSampleRate), kitSampleRate) && ok;

    return folder.getChildFile ("version.txt").replaceWithText (juce::String (kitVersion)) && ok;
}

void ensureWritten()
{
    auto folder = getFolder();
    bool upToDate = folder.getChildFile ("version.txt").loadFileAsString().getIntValue() == kitVersion;

    for (auto& name : getSoundNames())
        upToDate = upToDate && fileFor (name).existsAsFile();

    if (! upToDate)
        writeTo (folder);
}

} // namespace nightshift::kit
