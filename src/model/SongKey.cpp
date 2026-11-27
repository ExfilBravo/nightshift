// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "SongKey.h"

namespace nightshift::songkey
{

namespace
{
    // Krumhansl and Kessler's key profiles: how strongly each step of the scale belongs to a major and a minor key
    constexpr double majorProfile[12] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    constexpr double minorProfile[12] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    double pearson (const double* a, const double* b)
    {
        double ma = 0.0, mb = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            ma += a[i] / 12.0;
            mb += b[i] / 12.0;
        }

        double ab = 0.0, aa = 0.0, bb = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            ab += (a[i] - ma) * (b[i] - mb);
            aa += (a[i] - ma) * (a[i] - ma);
            bb += (b[i] - mb) * (b[i] - mb);
        }

        return aa > 0.0 && bb > 0.0 ? ab / std::sqrt (aa * bb) : 0.0;
    }

    /** Whether a channel plays notes as pitches: not a drum kit, not the drummer's, not a one-shot sample (a
        beat's kick or clap, played at one key). */
    bool isMelodic (const Channel& c)
    {
        if (c.type == InstrumentType::kit || c.type == InstrumentType::audio || ! c.drummer.turns.empty())
            return false;

        return c.type != InstrumentType::sampler || ! c.oneShot;
    }
}

juce::String noteName (int pitchClass, bool shortName)
{
    static const char* const full[12] { "C", "C sharp", "D", "E flat", "E", "F", "F sharp", "G", "A flat", "A", "B flat", "B" };
    static const char* const brief[12] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    const int pc = ((pitchClass % 12) + 12) % 12;
    return shortName ? brief[pc] : full[pc];
}

juce::String Key::name() const
{
    if (! known())
        return "not known";

    // A minor key on C sharp or G sharp is named so (not D flat, A flat minor)
    auto root_ = noteName (root);

    if (minor && root == 1)  root_ = "C sharp";
    if (minor && root == 8)  root_ = "G sharp";

    return root_ + (minor ? " minor" : " major");
}

Key find (const Song& song)
{
    double weight[12] {};
    double total = 0.0;

    for (auto& pattern : song.patterns)
        for (auto& [channelId, notes] : pattern.notes)
            if (auto c = song.findChannel (channelId); c != nullptr && isMelodic (*c))
                for (auto& n : notes)
                {
                    // How long it sounds, up to a bar's worth (a held pad doesn't drown the tune)
                    const double w = std::min (n.length, ticksPerBeat * 4);
                    weight[((n.key % 12) + 12) % 12] += w;
                    total += w;
                }

    // Too little to tell from: under two beats of notes, or fewer than three different notes
    const int distinct = (int) std::count_if (std::begin (weight), std::end (weight), [] (double w) { return w > 0.0; });

    if (total < 2.0 * ticksPerBeat || distinct < 3)
        return {};

    Key best;
    double bestFit = -2.0;

    for (int minor = 0; minor < 2; ++minor)
        for (int root = 0; root < 12; ++root)
        {
            double profile[12];

            for (int i = 0; i < 12; ++i)
                profile[(root + i) % 12] = (minor != 0 ? minorProfile : majorProfile)[i];

            if (const double fit = pearson (weight, profile); fit > bestFit + 1.0e-9)
            {
                bestFit = fit;
                best = { root, minor != 0 };
            }
        }

    return best;
}

juce::String scaleName (int scale)
{
    switch (scale)
    {
        case major:            return "Major";
        case minor:            return "Minor";
        case harmonicMinor:    return "Harmonic minor";
        case majorPentatonic:  return "Major pentatonic";
        case minorPentatonic:  return "Minor pentatonic";
        default:               return {};
    }
}

std::array<bool, 12> notesOf (int root, int scale)
{
    std::array<bool, 12> in {};

    if (root < 0)
    {
        in.fill (true);
        return in;
    }

    // (fixed tables: pitch correction asks on the audio thread, which mustn't allocate)
    static constexpr int steps[numScales][8] {
        { 0, 2, 4, 5, 7, 9, 11, -1 },      // major
        { 0, 2, 3, 5, 7, 8, 10, -1 },      // (natural) minor
        { 0, 2, 3, 5, 7, 8, 11, -1 },      // harmonic minor
        { 0, 2, 4, 7, 9, -1, -1, -1 },     // major pentatonic
        { 0, 3, 5, 7, 10, -1, -1, -1 }     // minor pentatonic
    };

    for (int s : steps[juce::jlimit (0, (int) numScales - 1, scale)])
        if (s >= 0)
            in[(size_t) ((root + s) % 12)] = true;

    return in;
}

} // namespace nightshift::songkey
