// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Machines.h"
#include "DrumSynth.h"
#include "Kits.h"

namespace nightshift::drumkits::machines
{

namespace
{
    using namespace drumsynth;

    // Bump when a sound changes: the machines are written again on the next start
    constexpr int version = 2;

    // Synthesised drums are dense (long booms, little gap between the transients): 6 dB under the
    // studio kits' peaks plays them about as loud
    constexpr float levelDb = -6.0f;
    constexpr double sr = 48000.0;

    /** One drum: `hard` 0..1 is how hard it's hit, which changes the tone (the player sets the
        level); `take` makes each take a little different, as a real machine's noise is. */
    using Make = std::function<juce::AudioBuffer<float> (double hard, int take)>;

    struct Drum
    {
        const char* voice;
        Make make;
    };

    struct Machine
    {
        const char* id;
        const char* name;
        const char* description;
        std::vector<Drum> drums;
    };

    juce::int64 seedFor (const char* voice, int take)
    {
        return (juce::int64) juce::String (voice).hashCode64() + take * 7919;
    }

    /** No DC (a decaying sine leaves some), and the ring-out cut where it's 70 dB under the peak,
        with a short fade. */
    void trim (juce::AudioBuffer<float>& b)
    {
        auto dc = Filter::highPass (sr, 15.0);

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, dc (b.getSample (0, i)));

        const float floor = b.getMagnitude (0, b.getNumSamples()) * juce::Decibels::decibelsToGain (-70.0f);
        int last = b.getNumSamples() - 1;

        while (last > 0 && std::abs (b.getSample (0, last)) < floor)
            --last;

        b.setSize (1, std::min (b.getNumSamples(), last + (int) (sr * 0.01)), true);
        fadeOut (b, sr, 10.0);
    }

    //==============================================================================
    // The six square oscillators of the 808's hats and cymbal: metal, before the filters
    struct Metal
    {
        explicit Metal (double scale)
        {
            for (double hz : { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 })
                oscs.emplace_back (hz * scale, sr);
        }

        float next()
        {
            float sum = 0.0f;

            for (auto& o : oscs)
                sum += o.next();

            return sum / 6.0f;
        }

        std::vector<BlepSquare> oscs;
    };

    juce::AudioBuffer<float> metalHit (double hard, int take, const char* voice, double length, double tau, double bright,
                                       double metalScale, float noiseMix, double cutHz)
    {
        Metal metal (metalScale * (1.0 + 0.002 * take));
        Noise noise (seedFor (voice, take));
        auto band1 = Filter::bandPass (sr, 3440.0 * bright, 1.2), band2 = Filter::bandPass (sr, 7100.0 * bright, 1.4);
        auto cut1 = Filter::highPass (sr, cutHz * (0.85 + 0.15 * hard)), cut2 = Filter::highPass (sr, cutHz * (0.85 + 0.15 * hard));
        auto top = Filter::lowPass (sr, 9000.0 + 6000.0 * hard);

        return generate (sr, length, [&] (double t)
        {
            const float m = metal.next(), n = noise();
            const float body = band1 (m) * 0.6f + band2 (m) + n * noiseMix;
            return top (cut2 (cut1 (body))) * (float) (decay (t, tau) * juce::jmin (1.0, t / 0.0002));
        });
    }

    juce::AudioBuffer<float> tom (double hard, int take, const char* voice, double hz, double sweep, double tau, float noiseMix)
    {
        Noise noise (seedFor (voice, take));
        auto thump = Filter::lowPass (sr, 2500.0);
        double phase = 0.0;
        hz *= 1.0 + 0.003 * take;

        return generate (sr, tau * 5.0, [&] (double t)
        {
            phase += twoPi * hz * (1.0 + (sweep * (0.7 + 0.3 * hard)) * decay (t, 0.045)) / sr;
            const float body = (float) (std::sin (phase) * decay (t, tau));
            const float stick = thump (noise()) * (float) decay (t, 0.004) * noiseMix * (float) (0.5 + 0.5 * hard);
            return saturate (body + stick, 1.3f);
        });
    }

    juce::AudioBuffer<float> clap (double hard, int take, const char* voice, double bandHz, double tail)
    {
        Noise noise (seedFor (voice, take));
        auto lowCut = Filter::highPass (sr, 650.0);
        auto band = Filter::bandPass (sr, bandHz, 1.1);
        auto air = Filter::highPass (sr, 6000.0);
        const double spacing = 0.0085 + 0.0006 * take;

        return generate (sr, tail * 5.0, [&] (double t)
        {
            // Four hands a few milliseconds apart, then the room
            double env = 0.0;

            for (int i = 0; i < 4; ++i)
                if (const double start = i * spacing; t >= start)
                    env = std::max (env, (i == 3 ? 1.0 : 0.8) * decay (t - start, 0.0032));

            if (t >= 3 * spacing)
                env = std::max (env, 0.7 * decay (t - 3 * spacing, tail));

            const float n = noise();
            return saturate ((band (lowCut (n)) + air (n) * (float) (0.1 + 0.15 * hard)) * (float) env, 1.3f);
        });
    }

    juce::AudioBuffer<float> rim (double hard, int take, const char* voice, double high, double low)
    {
        Noise noise (seedFor (voice, take));
        auto band = Filter::bandPass (sr, 3000.0, 1.0);

        return generate (sr, 0.09, [&] (double t)
        {
            const double tone = std::sin (twoPi * high * t) * decay (t, 0.011) * 0.7 + std::sin (twoPi * low * t) * decay (t, 0.017) * 0.6;
            return saturate ((float) tone + band (noise()) * (float) (decay (t, 0.0015) * (0.3 + 0.3 * hard)), 2.0f);
        });
    }

    juce::AudioBuffer<float> cowbell (double hard, int)
    {
        BlepSquare a (540.0, sr), b (800.0, sr);
        auto band = Filter::bandPass (sr, 2640.0, 1.6);
        auto body = Filter::lowPass (sr, 3000.0);

        return generate (sr, 0.6, [&] (double t)
        {
            const float sq = (a.next() + b.next()) * 0.5f;
            const double env = 0.65 * decay (t, 0.009) + 0.35 * decay (t, 0.1);
            return (band (sq) * (float) (0.5 + 0.5 * hard) + body (sq) * 0.5f) * (float) env;
        });
    }

    juce::AudioBuffer<float> maracas (double hard, int take)
    {
        Noise noise (seedFor ("shaker", take));
        auto cut = Filter::highPass (sr, 4500.0 + 1500.0 * hard);

        return generate (sr, 0.08, [&] (double t)
        {
            const double env = t < 0.003 ? t / 0.003 : decay (t - 0.003, 0.018);
            return cut (noise()) * (float) env;
        });
    }

    juce::AudioBuffer<float> claves (double hard, int)
    {
        return generate (sr, 0.12, [&] (double t)
        {
            return (float) (std::sin (twoPi * 2500.0 * t) * decay (t, 0.022) * (0.8 + 0.2 * hard));
        });
    }

    //==============================================================================
    Machine eightOhEight()
    {
        return { "NS_808", "Nightshift 808",
                 "Nightshift's own 808-style drum machine, synthesised: a long booming kick, snappy snare, clap, ticking hats, cowbell: "
                 "trap, hip hop, boom bap, R&B, electro, Miami bass, synthwave, lofi",
            {
                { "kick", [] (double hard, int take)
                  {
                      Noise noise (seedFor ("808kick", take));
                      auto click = Filter::highPass (sr, 1500.0);
                      double phase = 0.0;

                      return generate (sr, 1.6, [&] (double t)
                      {
                          phase += twoPi * 51.0 * (1.0 + 0.003 * take) * (1.0 + 0.45 * decay (t, 0.009)) / sr;
                          const float body = (float) (std::sin (phase) * decay (t, 0.3));
                          const float beater = (float) (std::sin (twoPi * 1600.0 * t) * decay (t, 0.0015)) * 0.35f + click (noise()) * (float) decay (t, 0.001) * 0.2f;
                          return saturate (body + beater * (float) (0.25 + 0.75 * hard), 1.15f);
                      });
                  } },
                { "snare", [] (double hard, int take)
                  {
                      Noise noise (seedFor ("808snare", take));
                      auto high = Filter::highPass (sr, 1800.0), low = Filter::lowPass (sr, 9000.0);
                      double p1 = 0.0, p2 = 0.0;

                      return generate (sr, 0.5, [&] (double t)
                      {
                          p1 += twoPi * 180.0 * (1.0 + 0.2 * decay (t, 0.006)) / sr;
                          p2 += twoPi * 330.0 * (1.0 + 0.15 * decay (t, 0.006)) / sr;
                          const float tone = (float) ((0.6 * std::sin (p1) + 0.4 * std::sin (p2)) * decay (t, 0.045));
                          const float snappy = low (high (noise())) * (float) decay (t, 0.11) * (float) (0.45 + 0.45 * hard);
                          return saturate (tone + snappy, 1.4f);
                      });
                  } },
                { "clap", [] (double hard, int take) { return clap (hard, take, "808clap", 1050.0, 0.15); } },
                { "sidestick", [] (double hard, int take) { return rim (hard, take, "808rim", 1700.0, 480.0); } },
                { "hat_closed", [] (double hard, int take) { return metalHit (hard, take, "808ch", 0.25, 0.03, 1.0, 1.0, 0.12f, 6500.0); } },
                { "hat_pedal", [] (double hard, int take) { return metalHit (hard, take, "808ph", 0.15, 0.016, 0.9, 1.0, 0.1f, 6000.0); } },
                { "hat_open", [] (double hard, int take) { return metalHit (hard, take, "808oh", 1.3, 0.24, 1.0, 1.0, 0.12f, 6500.0); } },
                { "crash", [] (double hard, int take) { return metalHit (hard, take, "808cy", 2.6, 0.55, 0.95, 1.0, 0.2f, 3000.0); } },
                { "tom_high", [] (double hard, int take) { return tom (hard, take, "808ht", 175.0, 0.2, 0.17, 0.15f); } },
                { "tom_mid", [] (double hard, int take) { return tom (hard, take, "808mt", 128.0, 0.2, 0.2, 0.15f); } },
                { "tom_floor", [] (double hard, int take) { return tom (hard, take, "808lt", 88.0, 0.2, 0.24, 0.15f); } },
                { "cowbell", cowbell },
                { "shaker", maracas },
                { "stick_click", claves } } };
    }

    Machine nineOhNine()
    {
        return { "NS_909", "Nightshift 909",
                 "Nightshift's own 909-style drum machine, synthesised: a punchy kick, bright snare, clap, sizzling hats, crash and ride: "
                 "house, techno, garage, drum and bass, electro, dance pop, synthwave",
            {
                { "kick", [] (double hard, int take)
                  {
                      Noise noise (seedFor ("909kick", take));
                      auto click = Filter::highPass (sr, 2000.0);
                      double phase = 0.0;

                      return generate (sr, 0.9, [&] (double t)
                      {
                          const double sweep = 0.75 + 0.25 * hard;
                          phase += twoPi * (50.0 * (1.0 + 0.003 * take) + 160.0 * sweep * decay (t, 0.02) + 380.0 * sweep * decay (t, 0.0028)) / sr;
                          const float body = (float) (std::sin (phase) * decay (t, 0.16) * (1.0 - juce::jlimit (0.0, 1.0, (t - 0.7) / 0.2)));
                          const float beater = click (noise()) * (float) decay (t, 0.0018) * (float) (0.2 + 0.4 * hard);
                          return saturate (body + beater, 2.2f);
                      });
                  } },
                { "snare", [] (double hard, int take)
                  {
                      Noise noise (seedFor ("909snare", take));
                      auto high = Filter::highPass (sr, 1100.0), low = Filter::lowPass (sr, 12000.0);
                      auto snap = Filter::bandPass (sr, 6000.0, 0.9);
                      double p1 = 0.0, p2 = 0.0;

                      return generate (sr, 0.55, [&] (double t)
                      {
                          p1 += twoPi * 190.0 * (1.0 + 0.4 * decay (t, 0.007)) / sr;
                          p2 += twoPi * 335.0 * (1.0 + 0.3 * decay (t, 0.007)) / sr;
                          const float n = noise();
                          const float tone = (float) ((0.55 * std::sin (p1) + 0.45 * std::sin (p2)) * decay (t, 0.035));
                          const float wires = low (high (n)) * (float) decay (t, 0.15) * (float) (0.5 + 0.4 * hard);
                          const float crack = snap (n) * (float) decay (t, 0.01) * (float) (0.2 + 0.4 * hard);
                          return saturate (tone + wires + crack, 1.6f);
                      });
                  } },
                { "clap", [] (double hard, int take) { return clap (hard, take, "909clap", 1350.0, 0.2); } },
                { "sidestick", [] (double hard, int take) { return rim (hard, take, "909rim", 1850.0, 450.0); } },
                { "hat_closed", [] (double hard, int take) { return metalHit (hard, take, "909ch", 0.25, 0.035, 1.25, 1.55, 0.45f, 7500.0); } },
                { "hat_pedal", [] (double hard, int take) { return metalHit (hard, take, "909ph", 0.15, 0.02, 1.15, 1.55, 0.4f, 7000.0); } },
                { "hat_open", [] (double hard, int take) { return metalHit (hard, take, "909oh", 1.5, 0.3, 1.25, 1.55, 0.45f, 7500.0); } },
                { "crash", [] (double hard, int take) { return metalHit (hard, take, "909cr", 3.0, 0.75, 1.1, 1.3, 0.7f, 4000.0); } },
                { "ride", [] (double hard, int take)
                  {
                      // A ping of bell partials over a wash
                      auto wash = metalHit (hard, take, "909rd", 3.0, 0.65, 1.3, 1.7, 0.5f, 5000.0);
                      auto ping = generate (sr, 3.0, [&] (double t)
                      {
                          return (float) ((std::sin (twoPi * 512.0 * t) * 0.5 + std::sin (twoPi * 740.0 * t) * 0.35 + std::sin (twoPi * 1180.0 * t) * 0.3)
                                          * decay (t, 0.55) * (0.6 + 0.4 * hard));
                      });
                      normalise (wash, -6.0f);
                      normalise (ping, -14.0f);
                      wash.addFrom (0, 0, ping, 0, 0, std::min (wash.getNumSamples(), ping.getNumSamples()));
                      return wash;
                  } },
                { "ride_bell", [] (double hard, int take)
                  {
                      auto wash = metalHit (hard, take, "909rb", 2.5, 0.5, 1.3, 1.7, 0.3f, 5000.0);
                      auto bell = generate (sr, 2.5, [&] (double t)
                      {
                          return (float) ((std::sin (twoPi * 610.0 * t) * 0.6 + std::sin (twoPi * 905.0 * t) * 0.5 + std::sin (twoPi * 1340.0 * t) * 0.35)
                                          * decay (t, 0.6));
                      });
                      normalise (wash, -10.0f);
                      normalise (bell, -4.0f);
                      bell.addFrom (0, 0, wash, 0, 0, std::min (wash.getNumSamples(), bell.getNumSamples()));
                      return bell;
                  } },
                { "tom_high", [] (double hard, int take) { return tom (hard, take, "909ht", 195.0, 0.5, 0.17, 0.35f); } },
                { "tom_mid", [] (double hard, int take) { return tom (hard, take, "909mt", 140.0, 0.5, 0.2, 0.35f); } },
                { "tom_floor", [] (double hard, int take) { return tom (hard, take, "909lt", 98.0, 0.5, 0.24, 0.35f); } },
                { "cowbell", cowbell },
                { "shaker", maracas },
                { "stick_click", claves } } };
    }

    std::vector<Machine> machines()
    {
        return { eightOhEight(), nineOhNine() };
    }

    /** Three layers that hit harder in tone, two takes each, each drum at its level. */
    juce::String write (const Machine& m, const juce::File& destination)
    {
        const auto staging = destination.getSiblingFile (".writing");
        staging.deleteRecursively();
        staging.getChildFile ("samples").createDirectory();

        const double layers[] = { 0.35, 0.7, 1.0 };
        const int bounds[][2] = { { 1, 54 }, { 55, 100 }, { 101, 127 } };
        constexpr int takes = 2;

        juce::String sfz;
        sfz << "// " << m.name << ": synthesised by Nightshift\n\n";

        for (auto& drum : m.drums)
        {
            const auto [label, key] = pieceFor (drum.voice);
            const bool hat = juce::String (drum.voice).startsWith ("hat_");
            sfz << "<group> key=" << key << " group_label=" << label.replaceCharacter (' ', '_') << (hat ? " group=1 off_by=1" : "") << "\n";

            for (int l = 0; l < 3; ++l)
                for (int take = 0; take < takes; ++take)
                {
                    auto audio = drum.make (layers[l], take);
                    trim (audio);
                    normalise (audio, juce::Decibels::gainToDecibels (peakFor (drum.voice)) + levelDb);

                    const auto name = juce::String (drum.voice) + "_" + juce::String (l + 1) + juce::String::charToString ((juce::juce_wchar) ('a' + take)) + ".wav";

                    if (! writeWav (staging.getChildFile ("samples").getChildFile (name), audio, sr))
                    {
                        staging.deleteRecursively();
                        return "Couldn't write the " + juce::String (m.name) + " into " + destination.getParentDirectory().getFullPathName() + ".";
                    }

                    sfz << "<region> lovel=" << bounds[l][0] << " hivel=" << bounds[l][1]
                        << " lorand=" << juce::String ((float) take / takes, 3) << " hirand=" << juce::String ((float) (take + 1) / takes, 3)
                        << " sample=samples/" << name << "\n";
                }

            sfz << "\n";
        }

        staging.getChildFile (juce::String (m.id) + ".sfz").replaceWithText (sfz);
        staging.getChildFile ("kit.txt").replaceWithText (juce::String (m.name) + "\n" + m.description + "\n"
                                                          + "Synthesised by Nightshift: no samples.\nversion " + juce::String (version) + "\n");
        destination.deleteRecursively();

        if (! staging.moveFileTo (destination))
        {
            staging.deleteRecursively();
            return "Couldn't put the " + juce::String (m.name) + " in " + destination.getParentDirectory().getFullPathName() + ".";
        }

        return {};
    }
}

//==============================================================================
juce::StringArray ids()
{
    juce::StringArray all;

    for (auto& m : machines())
        all.add (m.id);

    return all;
}

juce::String ensure()
{
    folder().createDirectory();

    for (auto& m : machines())
    {
        const auto destination = folder().getChildFile (m.id);
        const auto info = juce::StringArray::fromLines (destination.getChildFile ("kit.txt").loadFileAsString());

        if (info.contains ("version " + juce::String (version)))
            continue;

        if (auto error = write (m, destination); error.isNotEmpty())
            return error;
    }

    return {};
}

} // namespace nightshift::drumkits::machines
