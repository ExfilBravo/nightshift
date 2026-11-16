// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "OfflineBass.h"
#include "OfflineDrummer.h"
#include "audio/Nova.h"
#include <numeric>

namespace nightshift::bass::offline
{

using agents::Request;

namespace
{
    const char* const noteNames[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    const char* const flatNames[] { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    const char* const sharpNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    // A key's notes spelled as its key signature has them: sharps in G, D, A, E, B and F# major
    // (and the modes that share them), flats otherwise
    bool sharpKey (int root, const juce::String& mode)
    {
        const int offset = mode == "major" ? 0 : mode == "dorian" ? -2 : mode == "phrygian" ? -4 : mode == "mixolydian" ? 5
                         : mode == "lydian" ? -5 : 3;
        const int major = ((root + offset) % 12 + 12) % 12;
        return major == 7 || major == 2 || major == 9 || major == 4 || major == 11 || major == 6;
    }

    juce::String spell (int pc, bool sharps)        { return (sharps ? sharpNames : flatNames)[((pc % 12) + 12) % 12]; }

    //==============================================================================
    // Theory: the key, its scale, and chords by their roman numerals
    std::vector<int> scaleOf (const juce::String& mode)
    {
        if (mode == "major")        return { 0, 2, 4, 5, 7, 9, 11 };
        if (mode == "dorian")       return { 0, 2, 3, 5, 7, 9, 10 };
        if (mode == "phrygian")     return { 0, 1, 3, 5, 7, 8, 10 };
        if (mode == "mixolydian")   return { 0, 2, 4, 5, 7, 9, 10 };
        if (mode == "lydian")       return { 0, 2, 4, 6, 7, 9, 11 };
        return { 0, 2, 3, 5, 7, 8, 10 };
    }

    struct Chord
    {
        int root = 0;                       // pitch class
        std::vector<int> tones { 0, 4, 7 }; // above the root, in semitones
        juce::String name;

        bool has (int interval) const       { return std::find (tones.begin(), tones.end(), interval) != tones.end(); }
        int third() const                   { return has (3) ? 3 : has (4) ? 4 : -1; }
        int seventh() const                 { return has (10) ? 10 : has (11) ? 11 : -1; }
        bool operator== (const Chord& o) const { return root == o.root && tones == o.tones; }
    };

    // "i", "bVI", "V7", "ii7", "Imaj7", "bII", "IV9", "viio", "i5": the numeral's degree of the
    // major scale (flattened or sharpened), upper case major and lower case minor, then the extras
    Chord chordOf (const juce::String& roman, int keyRoot, bool sharps)
    {
        auto s = roman.trim();
        int shift = 0;

        while (s.startsWithChar ('b') || s.startsWithChar ('#'))
        {
            shift += s.startsWithChar ('b') ? -1 : 1;
            s = s.substring (1);
        }

        static const std::pair<const char*, int> numerals[] { { "vii", 6 }, { "iii", 2 }, { "iv", 3 }, { "vi", 5 }, { "ii", 1 }, { "v", 4 }, { "i", 0 } };
        static const int major[] { 0, 2, 4, 5, 7, 9, 11 };
        int degree = 0;
        juce::String rest = s;

        for (auto& [n, d] : numerals)
            if (s.substring (0, (int) std::strlen (n)).equalsIgnoreCase (n))
            {
                degree = d;
                rest = s.substring ((int) std::strlen (n));
                break;
            }

        const bool upper = s.isNotEmpty() && juce::CharacterFunctions::isUpperCase (s[0]);
        Chord c;
        c.root = ((keyRoot + major[degree] + shift) % 12 + 12) % 12;
        c.tones = upper ? std::vector<int> { 0, 4, 7 } : std::vector<int> { 0, 3, 7 };

        if (rest.contains ("o") || rest.contains ("dim"))   c.tones = { 0, 3, 6 };
        if (rest == "5")                                    c.tones = { 0, 7 };

        if (rest.contains ("maj7"))                         c.tones.push_back (11);
        else if (rest.contains ("7") || rest.contains ("9") || rest.contains ("13"))
            c.tones.push_back (10);

        if (rest.contains ("9"))                            c.tones.push_back (14);

        c.name = spell (c.root, sharps) + (rest == "5" ? "5" : upper ? "" : (rest.contains ("o") ? "dim" : "m"))
                 + (rest.contains ("maj7") ? "maj7" : rest.contains ("9") ? "9" : rest.contains ("7") ? "7" : "");
        return c;
    }

    struct Key
    {
        int root = 0;                       // pitch class
        juce::String mode = "minor";
        std::vector<int> scale;
        int low = 28;                       // the key's root, as low as the bass plays it
        int floor = 28;                     // nothing goes below this (a tuning's lowest string, an 808's sub)
        int bottom = 24, top = 52;          // where a line may go

        // A chord's root: the one within a fourth below to a fifth above the key's root, never
        // under the floor
        int place (int pc) const
        {
            const int start = std::max (low - 5, floor);
            return start + (((pc - start) % 12) + 12) % 12;
        }

        // The pitch with this pitch class nearest `around`, inside the range
        int nearest (int pc, int around) const
        {
            int best = place (pc);

            for (int p = bottom; p <= top; ++p)
                if (((p % 12) + 12) % 12 == pc && std::abs (p - around) < std::abs (best - around))
                    best = p;

            return best;
        }

        bool inScale (int pitch) const
        {
            const int degree = (((pitch - root) % 12) + 12) % 12;
            return std::find (scale.begin(), scale.end(), degree) != scale.end();
        }

        // The next note of the scale up (or down) from `pitch`
        int step (int pitch, int direction) const
        {
            for (int p = pitch + direction; std::abs (p - pitch) <= 3; p += direction)
                if (inScale (p))
                    return p;

            return pitch + direction * 2;
        }

        juce::String name() const           { return spell (root, sharpKey (root, mode)) + " " + mode; }
    };

    //==============================================================================
    // Each style's bass: its usual key and how low it sits, the push against the beat and how
    // loose, and the progressions its genre lives on, section by section
    struct StyleBass
    {
        int key;
        const char* mode;
        int low;
        float push, loose;
    };

    const StyleBass& defaultsFor (const juce::String& style)
    {
        static const std::map<juce::String, StyleBass> table {
            { "metalcore", { 0,  "minor",      24, 0.0f,  1.5f } },    // drop C
            { "deathcore", { 0,  "phrygian",   24, 0.0f,  1.5f } },
            { "djent",     { 11, "minor",      23, 0.0f,  1.0f } },    // B, under a 7-string's low B
            { "thrash",    { 4,  "minor",      28, -1.0f, 2.0f } },
            { "blast",     { 2,  "minor",      26, 0.0f,  1.5f } },
            { "metal",     { 4,  "minor",      28, 0.0f,  2.0f } },
            { "trap",      { 5,  "minor",      29, 0.0f,  0.0f } },    // the 808 round F1
            { "drill",     { 1,  "minor",      25, 0.0f,  0.0f } },
            { "boombap",   { 9,  "dorian",     33, 6.0f,  6.0f } },
            { "hiphop",    { 2,  "minor",      26, 3.0f,  4.0f } },
            { "rock",      { 4,  "major",      28, 0.0f,  3.0f } },
            { "hardrock",  { 9,  "mixolydian", 33, 0.0f,  3.0f } },
            { "poppunk",   { 4,  "major",      28, -1.0f, 2.0f } },
            { "punk",      { 9,  "major",      33, -2.0f, 2.5f } },
            { "lofi",      { 5,  "major",      29, 8.0f,  8.0f } },
            { "jazz",      { 10, "major",      34, -3.0f, 6.0f } },
            { "ballad",    { 3,  "major",      27, 2.0f,  7.0f } },
            { "neosoul",   { 3,  "major",      27, 6.0f,  10.0f } },
            { "funk",      { 4,  "dorian",     28, 0.0f,  2.5f } },
            { "house",     { 9,  "minor",      33, 0.0f,  1.0f } },    // A minor
            { "techno",    { 2,  "minor",      26, 0.0f,  0.5f } },
            { "edm",       { 9,  "major",      33, 0.0f,  0.5f } },    // vi-IV-I-V in A
            { "dnb",       { 5,  "minor",      29, 0.0f,  1.0f } },
            { "dubstep",   { 5,  "minor",      29, 0.0f,  0.5f } },
            { "synthwave", { 9,  "minor",      33, 0.0f,  0.5f } },
            { "reggae",    { 9,  "minor",      33, 6.0f,  5.0f } },    // behind the beat
            { "reggaeton", { 1,  "minor",      25, 0.0f,  1.0f } },
            { "afrobeats", { 4,  "major",      28, 2.0f,  3.0f } },
            { "bossa",     { 5,  "major",      29, 0.0f,  5.0f } },
            { "ambient",   { 2,  "major",      26, 0.0f,  6.0f } },
            { "cinematic", { 2,  "minor",      26, 0.0f,  2.0f } },
            { "country",   { 7,  "major",      31, 0.0f,  2.5f } },    // G
        };

        if (auto it = table.find (style); it != table.end())
            return it->second;

        return table.at ("rock");
    }

    const std::vector<juce::StringArray>& progressionsFor (const juce::String& style, const juce::String& kind)
    {
        using Options = std::vector<juce::StringArray>;
        static const std::map<juce::String, std::map<juce::String, Options>> table {
            { "metalcore", { { "intro",     { { "i" }, { "i", "bVI" } } },
                             { "verse",     { { "i", "bVI", "bIII", "bVII" }, { "i", "i", "bVI", "bVII" }, { "i", "bII", "i", "bVII" } } },
                             { "prechorus", { { "bVI", "bVII", "bVI", "bVII" } } },
                             { "chorus",    { { "bVI", "bIII", "bVII", "i" }, { "i", "bVI", "bIII", "bVII" }, { "bVI", "bVII", "i", "i" } } },
                             { "breakdown", { { "i" }, { "i", "i", "i", "bII" } } },
                             { "bridge",    { { "bVI", "bVII", "bIII", "bIII" } } } } },
            { "deathcore", { { "verse",     { { "i", "bII", "i", "bV" }, { "i", "i", "bII", "i" } } },
                             { "chorus",    { { "bVI", "bII", "i", "i" }, { "i", "bVI", "bII", "bV" } } },
                             { "breakdown", { { "i" }, { "i", "i", "bII", "i" } } } } },
            { "djent",     { { "verse",     { { "i" }, { "i", "bVI" } } },
                             { "chorus",    { { "bVI", "bIII", "bVII", "i" }, { "i", "bVI", "bIII", "bVII" } } },
                             { "breakdown", { { "i" } } } } },
            { "thrash",    { { "verse",     { { "i", "bIII", "iv", "bV" }, { "i", "i", "bVII", "bVI" } } },
                             { "chorus",    { { "i", "bVII", "bVI", "bVII" } } } } },
            { "blast",     { { "verse",     { { "i", "bVI", "bVII", "i" }, { "i", "bII", "i", "bII" } } },
                             { "chorus",    { { "bVI", "bVII", "i", "i" } } } } },
            { "metal",     { { "verse",     { { "i", "bVI", "bVII", "i" }, { "i", "i", "bVII", "bVI" } } },
                             { "chorus",    { { "bVI", "bVII", "i", "i" }, { "i", "bVII", "bVI", "V" } } } } },
            { "trap",      { { "verse",     { { "i", "bVI" }, { "i", "iv" }, { "i", "i", "bVI", "bVII" } } },
                             { "hook",      { { "i", "bVI", "iv", "V" }, { "i", "bVI", "bIII", "bVII" }, { "i", "i", "bVI", "bVII" } } } } },
            { "drill",     { { "verse",     { { "i", "bVI", "bIII", "bVII" }, { "i", "iv", "bVI", "V" } } },
                             { "hook",      { { "i", "bVI", "bIII", "bVII" }, { "i", "bVI", "iv", "V" } } } } },
            { "boombap",   { { "verse",     { { "i7", "IV7" }, { "i7", "bVII7" }, { "i7", "i7", "iv7", "iv7" } } },
                             { "hook",      { { "iv7", "i7" }, { "bVImaj7", "V7" } } } } },
            { "hiphop",    { { "verse",     { { "i", "bVI", "bVII", "i" }, { "i7", "iv7", "bVI", "V7" } } },
                             { "hook",      { { "bVI", "bVII", "i", "i" } } } } },
            { "rock",      { { "intro",     { { "I", "IV" }, { "I", "V" } } },
                             { "verse",     { { "I", "V", "vi", "IV" }, { "I", "IV", "vi", "V" }, { "I", "I", "IV", "V" } } },
                             { "chorus",    { { "IV", "I", "V", "vi" }, { "I", "V", "IV", "IV" }, { "vi", "IV", "I", "V" } } },
                             { "bridge",    { { "vi", "IV", "I", "V" }, { "IV", "V", "vi", "vi" } } } } },
            { "hardrock",  { { "verse",     { { "I", "bVII", "IV", "I" }, { "I", "I", "bVII", "IV" } } },
                             { "chorus",    { { "IV", "I", "bVII", "I" }, { "I", "bVII", "IV", "IV" } } } } },
            { "poppunk",   { { "verse",     { { "I", "V", "vi", "IV" }, { "vi", "IV", "I", "V" } } },
                             { "chorus",    { { "I", "V", "vi", "IV" }, { "IV", "I", "V", "vi" } } },
                             { "bridge",    { { "vi", "IV", "I", "V" }, { "IV", "IV", "V", "V" } } } } },
            { "punk",      { { "verse",     { { "I", "IV", "V", "IV" }, { "I", "bVII", "IV", "I" } } },
                             { "chorus",    { { "IV", "V", "I", "I" }, { "I", "IV", "V", "V" } } } } },
            { "lofi",      { { "intro",     { { "Imaj7", "IVmaj7" } } },
                             { "verse",     { { "ii7", "V7", "Imaj7", "vi7" }, { "Imaj7", "vi7", "ii7", "V7" }, { "IVmaj7", "iii7", "vi7", "ii7" } } } } },
            { "jazz",      { { "intro",     { { "ii7", "V7" } } },
                             { "verse",     { { "ii7", "V7", "Imaj7", "VI7" }, { "Imaj7", "vi7", "ii7", "V7" }, { "iii7", "VI7", "ii7", "V7" } } },
                             { "chorus",    { { "ii7", "V7", "Imaj7", "Imaj7" }, { "IVmaj7", "iv7", "iii7", "VI7" } } } } },
            { "ballad",    { { "verse",     { { "Imaj7", "vi7", "ii7", "V7" }, { "IVmaj7", "iii7", "ii7", "V7" } } } } },
            { "neosoul",   { { "verse",     { { "IVmaj7", "iii7", "vi7", "ii7" }, { "ii9", "V7", "Imaj7", "vi7" }, { "Imaj7", "IVmaj7" } } },
                             { "hook",      { { "IVmaj7", "iii7", "ii7", "Imaj7" }, { "vi9", "ii9", "IVmaj7", "V7" } } } } },
            { "funk",      { { "intro",     { { "I7" } } },
                             { "verse",     { { "I7" }, { "i7" }, { "I7", "I7", "IV7", "I7" } } },
                             { "chorus",    { { "IV7", "I7" }, { "I7", "IV7" }, { "IV7", "IV7", "I7", "I7" } } },
                             { "bridge",    { { "bVII7", "IV7" }, { "bVI7", "bVII7" } } } } },

            // The new families' (2026-11-15). House on minor sevenths, techno nearly on one chord, EDM on
            // pop's vi-IV-I-V, drum and bass and dubstep dark, synthwave's i-VI-III-VII
            { "house",     { { "intro",     { { "i7" } } },
                             { "verse",     { { "i7", "i7", "iv7", "iv7" }, { "i7", "bVII", "bVI", "bVII" } } },
                             { "chorus",    { { "i7", "bVI", "bIII", "bVII" }, { "iv7", "v7", "i7", "i7" } } },
                             { "breakdown", { { "bVI", "bVII", "i", "i" } } } } },
            { "techno",    { { "verse",     { { "i" }, { "i", "i", "bII", "i" } } },
                             { "chorus",    { { "i", "bVI", "i", "bVII" }, { "i" } } },
                             { "breakdown", { { "bVI", "bVII" } } } } },
            { "edm",       { { "verse",     { { "vi", "IV", "I", "V" }, { "IV", "V", "vi", "vi" } } },
                             { "prechorus", { { "IV", "IV", "V", "V" } } },
                             { "drop",      { { "vi", "IV", "I", "V" }, { "IV", "V", "vi", "I" } } },
                             { "breakdown", { { "IV", "V", "iii", "vi" } } } } },
            { "dnb",       { { "verse",     { { "i7", "iv7", "bVII7", "bIIImaj7" }, { "i", "bVI", "bIII", "bVII" } } },
                             { "drop",      { { "i", "i", "bVI", "bVII" }, { "i", "bVI", "iv", "V" } } },
                             { "breakdown", { { "bVImaj7", "bVII", "i7", "i7" } } } } },
            { "dubstep",   { { "verse",     { { "i" }, { "i", "bVI" } } },
                             { "prechorus", { { "bVI", "bVII" } } },
                             { "drop",      { { "i", "i", "bII", "i" }, { "i", "bVI", "bVII", "i" } } },
                             { "breakdown", { { "bVI", "bVII", "i", "i" } } } } },
            { "synthwave", { { "verse",     { { "i", "bVI", "bIII", "bVII" }, { "i", "bVII", "bVI", "bVII" } } },
                             { "chorus",    { { "bVI", "bVII", "i", "i" }, { "i", "bVI", "bIII", "bVII" } } },
                             { "bridge",    { { "iv", "bVI", "bVII", "bVII" } } } } },

            // Reggae's minor vamps, reggaeton's i-VI-III-VII, afrobeats bright and major
            { "reggae",    { { "verse",     { { "i", "bVII" }, { "i", "iv" }, { "i", "i", "bVII", "bVII" } } },
                             { "chorus",    { { "bVI", "bVII", "i", "i" }, { "iv", "i", "bVII", "i" } } },
                             { "bridge",    { { "bIII", "bVII", "iv", "V" } } } } },
            { "reggaeton", { { "verse",     { { "i", "bVI", "bIII", "bVII" } } },
                             { "hook",      { { "i", "bVI", "bIII", "bVII" }, { "iv", "bVI", "i", "bVII" } } } } },
            { "afrobeats", { { "verse",     { { "I", "vi", "IV", "V" }, { "ii", "V", "I", "I" } } },
                             { "hook",      { { "IV", "V", "iii", "vi" }, { "I", "V", "vi", "IV" } } },
                             { "bridge",    { { "IV", "iii", "ii", "V" } } } } },

            // Bossa nova's sevenths: Ipanema's I-II7-ii-V and its turnarounds
            { "bossa",     { { "intro",     { { "Imaj7", "ii7", "V7" } } },
                             { "verse",     { { "Imaj7", "II7", "ii7", "V7" }, { "ii7", "V7", "Imaj7", "Imaj7" }, { "Imaj7", "bVII7", "ii7", "V7" } } },
                             { "chorus",    { { "IVmaj7", "iv7", "iii7", "VI7" }, { "ii7", "V7", "iii7", "VI7" } } } } },

            // Ambient slow and open, two bars a chord; cinematic's epic minor
            { "ambient",   { { "intro",     { { "Imaj7" } } },
                             { "verse",     { { "Imaj7", "Imaj7", "IVmaj7", "IVmaj7" }, { "vi7", "vi7", "IVmaj7", "IVmaj7" } } },
                             { "chorus",    { { "IVmaj7", "V", "vi7", "Imaj7" } } } } },
            { "cinematic", { { "verse",     { { "i", "i", "bVI", "bVI" }, { "i", "bVI", "bIII", "bVII" } } },
                             { "prechorus", { { "iv", "iv", "bVI", "bVII" } } },
                             { "chorus",    { { "i", "bVI", "bIII", "bVII" }, { "bVI", "bVII", "i", "i" } } },
                             { "bridge",    { { "bVI", "bVII", "bIII", "V" } } } } },

            // Country's I-IV-V
            { "country",   { { "verse",     { { "I", "I", "IV", "I" }, { "I", "IV", "V", "I" } } },
                             { "chorus",    { { "IV", "I", "V", "I" }, { "I", "V", "vi", "IV" } } },
                             { "bridge",    { { "vi", "IV", "I", "V" } } } } },
        };

        static const Options fallback { { "i" } };
        auto it = table.find (style);

        if (it == table.end())
            it = table.find ("rock");

        const auto& kinds = it->second;

        for (auto k : { kind, juce::String (kind == "hook" ? "chorus" : kind == "chorus" ? "hook" : "verse"), juce::String ("verse"), juce::String ("hook") })
            if (auto found = kinds.find (k); found != kinds.end())
                return found->second;

        return fallback;
    }

    juce::String kindOf (const juce::String& sectionName)
    {
        const auto n = sectionName.toLowerCase();

        if (n == "head")        return "verse";
        if (n == "solos")       return "chorus";
        if (n == "pre-chorus")  return "prechorus";

        return n;
    }

    //==============================================================================
    // A bar to write: its steps and beats, the drummer's kick and snare in it, its chord and the next
    struct BarIn
    {
        int steps = 16, index = 0, count = 1;
        std::vector<int> beats;             // where each counted beat starts
        int beat = 4;                       // steps in the first beat
        int sixteenth = 1;                  // steps a 16th; 0 on triplet bars (jazz)
        std::vector<int> kicks, snares;
        Chord chord, next;
        bool phraseEnd = false, sectionEnd = false, songEnd = false;
        juce::String nextKind;
        int prevPitch = -1;                 // the last note of the bar before
    };

    std::vector<int> beatStarts (int steps, int beats, int unit)
    {
        std::vector<int> groups;            // in 16ths, as the drummer counts them

        if (unit == 4 || unit == 2)
            for (int b = 0; b < beats; ++b)
                groups.push_back (16 / unit);
        else if (unit == 8 && beats % 3 == 0)
            for (int b = 0; b < beats / 3; ++b)
                groups.push_back (6);
        else
            for (int left = beats; left > 0;)
            {
                const int g = left == 3 || left == 5 ? 3 : 2;
                groups.push_back (g * 16 / unit);
                left -= g;
            }

        const int total = std::accumulate (groups.begin(), groups.end(), 0);
        std::vector<int> starts;
        int at = 0;

        for (auto g : groups)
        {
            starts.push_back (at * steps / std::max (1, total));
            at += g;
        }

        return starts;
    }

    //==============================================================================
    struct Ctx
    {
        const Request& req;
        const agents::Style& style;
        juce::Random& rng;
        Key key;
        juce::String kind;
        bool soft = false;                  // asked for this section ("soft verses")
        float energy = 0.5f, busy = 0.5f;
        double tempo = 120.0;
        juce::String note;

        bool chance (float p)               { return rng.nextFloat() < p; }
        int pick (int n)                    { return rng.nextInt (juce::jmax (1, n)); }
        juce::String id() const             { return style.id; }
        juce::String family() const         { return style.family; }
    };

    // One way through a section, chosen among many: the same in every bar, so the line holds
    // together while the kick and the chords move under it
    struct Plan
    {
        int mode = 0;
        float passing = 0.3f, jump = 0.3f, ghosts = 0.2f, slides = 0.0f;
        int interval = 12;
        bool approach = true, anticipate = false;
        std::vector<int> pops;              // funk: the offbeat 16ths it pops the octave on
        juce::int64 seed = 1;
    };

    using Notes = std::vector<Note>;

    void add (Notes& ns, int step, int length, int pitch, int velocity, bool slide = false, bool ghost = false)
    {
        ns.push_back ({ step, std::max (1, length), pitch, juce::jlimit (1, 127, velocity), slide, ghost });
    }

    bool taken (const Notes& ns, int step)
    {
        return std::any_of (ns.begin(), ns.end(), [step] (const Note& n) { return n.step == step; });
    }

    void clearFrom (Notes& ns, int step)
    {
        ns.erase (std::remove_if (ns.begin(), ns.end(), [step] (const Note& n) { return n.step >= step; }), ns.end());
    }

    // One note at a time: sorted, one per step, each held at most until the next starts (the
    // last may run on past the bar line if it was written to)
    void tidy (Notes& ns)
    {
        std::stable_sort (ns.begin(), ns.end(), [] (const Note& a, const Note& b) { return a.step < b.step; });
        ns.erase (std::unique (ns.begin(), ns.end(), [] (const Note& a, const Note& b) { return a.step == b.step; }), ns.end());

        for (size_t i = 0; i + 1 < ns.size(); ++i)
            ns[i].length = juce::jlimit (1, ns[i + 1].step - ns[i].step, ns[i].length);
    }

    bool onKick (const BarIn& b, int step)
    {
        return std::find (b.kicks.begin(), b.kicks.end(), step) != b.kicks.end();
    }

    bool onSnare (const BarIn& b, int step)
    {
        return std::find (b.snares.begin(), b.snares.end(), step) != b.snares.end();
    }

    // Into the next chord from `from`: a half step away from it, or a step of the scale, from
    // the side it's coming from (the other side if that's below the bass's lowest note)
    int approachTo (const Key& key, int from, int target, bool chromatic)
    {
        int dir = target > from ? -1 : 1;

        if (target + dir * 2 < key.bottom)
            dir = 1;

        return chromatic ? target + dir : key.step (target, dir);
    }

    // A run of `count` notes of the scale ending a step from `target`, coming from `from`'s side
    std::vector<int> runInto (const Key& key, int from, int target, int count)
    {
        std::vector<int> run;
        const int dir = target > from && target - 4 >= key.bottom ? -1 : 1;
        int p = target;

        for (int i = 0; i < count; ++i)
        {
            p = key.step (p, dir);
            run.insert (run.begin(), p);
        }

        return run;
    }

    //==============================================================================
    //  Metal: the bass doubles the riff, and the riff locks to the kick
    //==============================================================================
    Notes metalBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        juce::Random r (p.seed + (b.phraseEnd ? 7 : 0));
        const auto id = c.id();
        const int root = c.key.place (b.chord.root);
        const int next = c.key.place (b.next.root);
        const int s16 = std::max (1, b.sixteenth);
        const int eighth = 2 * s16;
        const float density = (float) b.kicks.size() * s16 / (float) b.steps;
        const auto rootName = spell (b.chord.root, sharpKey (c.key.root, c.key.mode));

        auto eighths = [&] (int pitch, const juce::String& what)
        {
            for (int t = 0; t < b.steps; t += eighth)
                add (ns, t, eighth, pitch, t % b.beat == 0 ? 118 : 104);

            c.note = what;
        };

        auto lockedToKick = [&] (bool chugs)
        {
            for (size_t j = 0; j < b.kicks.size(); ++j)
            {
                const int k = b.kicks[j];
                const int gap = (j + 1 < b.kicks.size() ? b.kicks[j + 1] : b.steps) - k;
                int length = chugs && c.req.legato <= 0.3f ? (gap <= 2 * eighth ? std::min (gap, eighth) : gap - s16) : gap;

                if (c.req.legato < -0.3f)
                    length = std::min (gap, s16 + s16 / 2);

                int pitch = root;

                // The lift at the end of a phrase: the last hit on the b2 (deathcore's on the tritone)
                if (c.kind == "breakdown" && b.phraseEnd && j + 1 == b.kicks.size() && j > 0 && p.mode != 0 && b.chord.root == c.key.root)
                    pitch = root + (id == "deathcore" && p.mode == 2 ? 6 : 1);

                add (ns, k, length, pitch, j == 0 ? 122 : 112);
            }
        };

        if (c.kind == "intro" && b.kicks.size() <= 2)
        {
            add (ns, 0, b.steps, root, 104);
            c.note = "the low " + rootName + " held under the build";
        }
        else if (c.kind == "breakdown" || c.req.lockKick || c.req.pedal || (id == "djent" && c.kind != "chorus"))
        {
            lockedToKick (true);
            c.note = c.kind == "breakdown" ? "every kick doubled on the low " + rootName + ", palm-muted chugs"
                                               + juce::String (p.mode != 0 ? (id == "deathcore" && p.mode == 2 ? ", the tritone to end each phrase" : ", a b2 to lift the end of each phrase") : "")
                                           : "the kick doubled on the root, note for note";
        }
        else if (id == "thrash" && (p.mode != 1 || c.req.gallops) && b.sixteenth > 0)
        {
            for (auto beat : b.beats)
            {
                add (ns, beat, eighth, root, 116);
                add (ns, beat + eighth, s16, root, 100);
                add (ns, beat + eighth + s16, s16, root, 102);
            }

            c.note = "gallops on the root, with the kick";
        }
        else if (id == "blast" || density > 0.45f)
        {
            eighths (root, "root 8ths, picked hard under the double kick");
        }
        else if (c.kind == "chorus" || c.kind == "hook" || c.kind == "drop")
        {
            if (p.mode == 0)
            {
                eighths (root, "driving root 8ths, the chorus wide open over them");
            }
            else if (p.mode == 1)
            {
                const int half = b.steps / 2;
                add (ns, 0, half, root, 118);
                add (ns, half, half, p.jump > 0.5f ? root + 12 : root + 7, 110);
                c.note = juce::String ("half notes: the root, then up to the ") + (p.jump > 0.5f ? "octave" : "fifth");
            }
            else
            {
                add (ns, 0, b.steps, root, 118);
                c.note = "long roots ringing under the chords";
            }
        }
        else
        {
            // The verse: the root on every kick, 8ths filling in between
            lockedToKick (p.mode == 0);

            for (int t = 0; t < b.steps; t += eighth)
                if (! taken (ns, t) && r.nextFloat() < p.passing)
                    add (ns, t, eighth, root, 98);

            c.note = "the root on every kick, 8ths filling in between";
        }

        // Into the next riff: a chromatic walk down (or up) the last beat, at a phrase's or a
        // section's end; a step into the next chord otherwise
        if (! b.songEnd && b.sixteenth > 0 && c.kind != "breakdown" && (b.phraseEnd || b.sectionEnd) && b.nextKind != "breakdown")
        {
            const int from = b.steps - b.beat;
            clearFrom (ns, from);
            const int dir = next > root && next - 2 >= c.key.bottom ? -1 : 1;
            add (ns, from, eighth, next + dir * 2, 112);
            add (ns, from + eighth, eighth, next + dir, 116);
            c.note << ", a chromatic walk into each change";
        }
        else if (p.approach && ! (b.next == b.chord) && ! b.sectionEnd && c.kind != "breakdown" && b.sixteenth > 0)
        {
            clearFrom (ns, b.steps - eighth);
            add (ns, b.steps - eighth, eighth, approachTo (c.key, root, next, false), 106);
        }

        return ns;
    }

    //==============================================================================
    //  Trap and drill: the 808 is the kick's other half
    //==============================================================================
    Notes trapBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;

        if (c.kind == "intro")
        {
            c.note = "nothing in the intro: the 808 waits for the hook";
            return ns;
        }

        juce::Random r (p.seed + b.index % 2);
        const bool drill = c.id() == "drill";
        const int root = c.key.place (b.chord.root);
        const int s16 = b.sixteenth > 0 ? b.sixteenth : std::max (1, b.steps / 16);
        std::vector<int> hits;

        for (auto k : b.kicks)
            if (hits.empty() || k - hits.back() >= s16 * 2)
                hits.push_back (k);

        if (hits.empty())
        {
            if (b.index == 0)
                add (ns, 0, b.steps, root, 116);

            return ns;
        }

        int previous = b.prevPitch;
        bool slid = false, jumped = false;

        for (size_t j = 0; j < hits.size(); ++j)
        {
            const int k = hits[j];
            const int end = j + 1 < hits.size() ? hits[j + 1] : b.steps;
            const bool offbeat = k % std::max (1, b.beat) != 0;
            int pitch = root;

            if (j > 0 && r.nextFloat() < p.jump * (offbeat ? 1.0f : 0.4f))
                pitch = root + p.interval;

            // The bar's last hit up the octave (trap's signature), or a drill step of the scale
            if (j > 0 && j + 1 == hits.size() && r.nextFloat() < p.jump)
                pitch = drill ? c.key.step (root, r.nextBool() ? 1 : -1) : root + 12;

            if (c.req.octaves && j % 2 == 1)
                pitch = root + 12;

            const bool slide = j > 0 && previous >= 0 && pitch != previous && (c.req.slides || r.nextFloat() < p.slides);
            slid = slid || slide;
            jumped = jumped || pitch != root;
            add (ns, k, end - k, pitch, j == 0 ? 120 : 112, slide);
            previous = pitch;
        }

        // A phrase never ends on the one note all the way: its last hit goes up the octave
        if (b.phraseEnd && ! jumped && ns.size() >= 2)
        {
            auto& last = ns.back();
            last.pitch = root + 12;
            last.slide = c.req.slides || p.slides > 0.15f;
            slid = slid || last.slide;
            jumped = true;
        }

        c.note = drill ? juce::String ("the 808 on the kick, held to the next") + (slid ? ", sliding between notes of the scale" : ", moving between notes of the scale")
                       : juce::String ("the 808 on every kick, held to the next") + (slid ? ", sliding up to the octave" : jumped ? ", jumping up the octave at the end of the bar" : "");
        return ns;
    }

    //==============================================================================
    //  Boom bap and hip-hop: the kick's notes, a passing note between
    //==============================================================================
    Notes hiphopBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        juce::Random r (p.seed);
        const int root = c.key.place (b.chord.root);
        const int next = c.key.place (b.next.root);
        const int s16 = b.sixteenth > 0 ? b.sixteenth : 1;
        const int eighth = 2 * s16;
        const int seventh = b.chord.seventh() >= 0 ? root + b.chord.seventh() : root + 12;

        for (size_t j = 0; j < b.kicks.size(); ++j)
        {
            int pitch = root;

            if (j > 0 && r.nextFloat() < p.jump)
                pitch = p.interval == 7 ? root + 7 : p.interval == 10 ? seventh : root + 12;

            add (ns, b.kicks[j], b.beat * 2, pitch, j == 0 ? 112 : 104);
        }

        if (b.kicks.empty() || b.kicks.front() != 0)
            add (ns, 0, b.beat * 2, root, 108);

        // A passing chord tone where the kick leaves room
        for (int t = eighth; t < b.steps; t += eighth)
        {
            const bool room = std::none_of (b.kicks.begin(), b.kicks.end(), [&] (int k) { return std::abs (k - t) < eighth; });

            if (room && ! onSnare (b, t) && ! c.req.pedal && r.nextFloat() < p.passing)
                add (ns, t, eighth, r.nextBool() ? root + 7 : seventh, 94);
        }

        if (p.approach && ! (b.next == b.chord) && ! taken (ns, b.steps - eighth))
            add (ns, b.steps - eighth, eighth, approachTo (c.key, root, next, false), 92);

        tidy (ns);

        // Detached: each lets go a 16th before the next
        if (p.mode == 0)
            for (auto& n : ns)
                n.length = std::max (1, n.length - s16);

        c.note = "the root on the kick, a passing chord tone between" + juce::String (c.id() == "boombap" ? ", sitting back with the swing" : "");
        return ns;
    }

    //==============================================================================
    //  Rock, hard rock, pop-punk, punk: root 8ths with the kick, walk-ups into the changes
    //==============================================================================
    Notes rockBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        juce::Random r (p.seed);
        const auto id = c.id();
        const bool punk = id == "poppunk" || id == "punk";
        const int root = c.key.place (b.chord.root);
        const int next = c.key.place (b.next.root);
        const int s16 = b.sixteenth > 0 ? b.sixteenth : 1;
        const int eighth = 2 * s16;

        int mode = p.mode;     // 0 root 8ths, 1 the kick and 8ths between, 2 quarters, 3 half notes

        if (c.kind == "chorus" || c.kind == "hook")     mode = 0;
        else if (c.kind == "bridge")                    mode = p.mode == 2 ? 3 : 2;
        else if (punk)                                  mode = 0;

        if (c.req.lockKick)
            mode = 1;

        if (mode == 0)
        {
            for (int t = 0; t < b.steps; t += eighth)
            {
                const bool pop = id == "hardrock" && t % b.beat == eighth && (t / b.beat) % 2 == 1 && p.jump > 0.5f;
                add (ns, t, eighth, pop || (c.req.octaves && (t / eighth) % 2 == 1) ? root + 12 : root, t % b.beat == 0 ? 114 : (punk ? 108 : 100));
            }

            c.note = punk ? "downpicked root 8ths, flat out" : id == "hardrock" ? "root 8ths with the octave on the off-beats" : "root 8ths, driving with the kick";
        }
        else if (mode == 1)
        {
            for (size_t j = 0; j < b.kicks.size(); ++j)
                add (ns, b.kicks[j], eighth * 2, root, 112);

            for (int t = 0; t < b.steps; t += eighth)
                if (! taken (ns, t) && r.nextFloat() < p.passing)
                    add (ns, t, eighth, root, 98);

            c.note = "with the kick, 8ths filling between";
        }
        else if (mode == 2)
        {
            for (auto beat : b.beats)
                add (ns, beat, b.beat, beat == 0 || r.nextFloat() > p.jump * 0.5f ? root : root + 7, beat == 0 ? 112 : 102);

            c.note = "quarter notes on the root, a fifth now and then";
        }
        else
        {
            add (ns, 0, b.steps / 2, root, 110);
            add (ns, b.steps / 2, b.steps / 2, r.nextBool() ? root + 7 : root, 100);
            c.note = "half notes, opening the bridge out";
        }

        // A walk-up into the next part at a phrase's end, a step into each change otherwise
        if (! b.songEnd && b.sixteenth > 0 && (b.phraseEnd || b.sectionEnd) && (punk || p.approach))
        {
            const int from = b.steps - 3 * eighth;
            clearFrom (ns, from);
            const auto run = runInto (c.key, root, next, 3);

            for (int i = 0; i < 3; ++i)
                add (ns, from + i * eighth, eighth, run[(size_t) i], 108 + i * 3);

            c.note << ", walking up into each part";
        }
        else if (p.approach && ! (b.next == b.chord) && b.sixteenth > 0)
        {
            clearFrom (ns, b.steps - eighth);
            add (ns, b.steps - eighth, eighth, approachTo (c.key, root, next, false), 104);
        }

        return ns;
    }

    //==============================================================================
    //  Lofi, jazz and neo-soul
    //==============================================================================

    // Walking: the root on the one, chord tones between, a chromatic step (or the next chord's
    // fifth) into the change; or in two, the root and the fifth a half note each
    Notes walkingBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        juce::Random r (p.seed + b.index);
        const int beats = (int) b.beats.size();
        const int around = b.prevPitch >= 0 ? b.prevPitch : c.key.low + 5;
        const int one = c.key.nearest (b.chord.root, around);
        const int target = c.key.nearest (b.next.root, one);
        const bool inTwo = c.id() == "ballad" || c.kind == "intro" || (! c.req.walking && p.mode == 1 && c.kind == "verse");

        if (inTwo || beats < 3)
        {
            const int half = b.beats[(size_t) beats / 2];
            add (ns, 0, half, one, 100);
            add (ns, half, b.steps - half, p.approach ? approachTo (c.key, one, target, true) : one + 7 <= c.key.top ? one + 7 : one - 5, 92);
            c.note = "in two: the root and then a step into the change, a half note each";
            return ns;
        }

        std::vector<int> line ((size_t) beats);
        line[0] = one;

        // The last beat leads into the next bar: a half step from it, or the next chord's fifth
        line[(size_t) beats - 1] = r.nextFloat() < 0.75f ? approachTo (c.key, one, target, true)
                                                         : (target + 7 <= c.key.top && std::abs (target + 7 - one) < std::abs (target - 5 - one) ? target + 7 : target - 5);

        // Between: the smoothest way from the one to the approach through chord tones (or steps
        // of the scale), never the same note twice running, never the next chord's root early
        std::vector<int> pool;

        for (int pitch = std::max (c.key.bottom, one - 9); pitch <= std::min (c.key.top, one + 9); ++pitch)
        {
            const int interval = (((pitch - b.chord.root) % 12) + 12) % 12;
            const bool chordTone = std::any_of (b.chord.tones.begin(), b.chord.tones.end(), [interval] (int t) { return t % 12 == interval; });

            if (chordTone || c.key.inScale (pitch))
                pool.push_back (pitch);
        }

        auto isChordTone = [&] (int pitch)
        {
            const int interval = (((pitch - b.chord.root) % 12) + 12) % 12;
            return std::any_of (b.chord.tones.begin(), b.chord.tones.end(), [interval] (int t) { return t % 12 == interval; });
        };

        const int approach = line[(size_t) beats - 1];
        const int middle = beats - 2;
        std::vector<int> bestMiddle ((size_t) middle, one);
        float bestCost = 1.0e9f;

        auto cost = [&] (const std::vector<int>& path)
        {
            float total = 0.0f;
            int prev = one, prevDir = 0;

            for (size_t i = 0; i <= path.size(); ++i)
            {
                const int pitch = i < path.size() ? path[i] : approach;
                const int d = pitch - prev;

                if (d == 0)                                             total += 10.0f;
                if (std::abs (d) > 5)                                   total += (float) (std::abs (d) - 5) * 2.0f;
                total += (float) std::abs (d) * 0.3f;

                const int dir = d > 0 ? 1 : d < 0 ? -1 : 0;

                if (prevDir != 0 && dir != 0 && dir != prevDir)         total += 1.5f;      // no zig-zag

                if (i < path.size())
                {
                    if (! isChordTone (pitch))                          total += i + 1 == path.size() ? 0.4f : 0.8f;
                    if (((pitch - target) % 12 + 12) % 12 == 0)         total += 4.0f;      // the next root, early
                }

                prevDir = dir;
                prev = pitch;
            }

            return total + r.nextFloat() * 0.6f;
        };

        if (middle == 1)
        {
            for (auto a : pool)
                if (const float k = cost ({ a }); k < bestCost)
                {
                    bestCost = k;
                    bestMiddle = { a };
                }
        }
        else if (middle == 2)
        {
            for (auto a : pool)
                for (auto z : pool)
                    if (const float k = cost ({ a, z }); k < bestCost)
                    {
                        bestCost = k;
                        bestMiddle = { a, z };
                    }
        }

        else
        {
            // Longer bars (5/4): a beat at a time, each the best next step
            std::vector<int> path;

            for (int i = 0; i < middle; ++i)
            {
                int bestPitch = one;
                float bestStep = 1.0e9f;

                for (auto a : pool)
                {
                    auto trial = path;
                    trial.push_back (a);

                    if (const float k = cost (trial); k < bestStep)
                    {
                        bestStep = k;
                        bestPitch = a;
                    }
                }

                path.push_back (bestPitch);
            }

            bestMiddle = path;
        }

        for (int i = 0; i < middle; ++i)
            line[(size_t) i + 1] = bestMiddle[(size_t) i];

        for (int i = 0; i < beats; ++i)
        {
            const int at = b.beats[(size_t) i];
            const int end = i + 1 < beats ? b.beats[(size_t) i + 1] : b.steps;
            add (ns, at, end - at, line[(size_t) i], i == 0 ? 102 : 92 - (i % 2) * 4);
        }

        // Now and then the skip note: a ghosted triplet pickup into the next bar
        if (b.steps % 3 == 0 && b.sixteenth == 0 && r.nextFloat() < 0.18f && ! b.sectionEnd)
        {
            ns.back().length = b.steps - 1 - ns.back().step;
            add (ns, b.steps - 1, 1, ns.back().pitch, 64, false, true);
        }

        c.note = "walking quarters: the root on the one, chord tones between, a chromatic step into each change";
        return ns;
    }

    Notes lofiBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        if (c.id() == "jazz" || c.id() == "ballad" || c.req.walking)
            return walkingBar (c, p, b);

        Notes ns;
        juce::Random r (p.seed);
        const bool soul = c.id() == "neosoul";
        const int root = c.key.place (b.chord.root);
        const int next = c.key.place (b.next.root);
        const int s16 = b.sixteenth > 0 ? b.sixteenth : 1;
        const int fifth = root + 7, third = b.chord.third() >= 0 ? root + b.chord.third() : root + 7;
        const int seventh = b.chord.seventh() >= 0 ? root + b.chord.seventh() : root + 12;

        // Neo-soul pushes the root ahead of the one: the bar before already played it
        const bool anticipated = soul && p.anticipate && b.index > 0;

        if (! anticipated)
            add (ns, 0, b.beat * 2, root, 104);

        const int ninth = root + 14 <= c.key.top ? root + 14 : root + 2;

        for (auto k : b.kicks)
            if (k > 0 && ! taken (ns, k))
            {
                const int choice = r.nextInt (4);
                const int pitch = c.req.pedal ? root : choice == 0 ? fifth : choice == 1 ? seventh : choice == 2 ? third : root + 12;
                add (ns, k, b.beat, soul && ! c.req.pedal && r.nextFloat() < 0.3f ? ninth : pitch, 94);
            }

        // A chord tone on the "and" of 3, where the kick isn't
        if (const int t = b.beat * 2 + 2 * s16; t < b.steps && ! taken (ns, t) && ! c.req.pedal && r.nextFloat() < p.passing)
            add (ns, t, b.beat, r.nextBool() ? fifth : third, 88);

        // Slides into chord tones: a whole step below, a 16th early
        if (soul && b.sixteenth > 0)
            for (size_t i = 0, written = ns.size(); i < written; ++i)
                if (const auto n = ns[i]; n.step >= s16 && (c.req.slides || r.nextFloat() < p.slides) && ! taken (ns, n.step - s16))
                {
                    ns[i].slide = true;
                    add (ns, n.step - s16, s16, n.pitch - 2, 80);
                }

        // Dead notes ghosting in before the notes
        if (soul && b.sixteenth > 0)
        {
            Notes ghosts;

            for (auto& n : ns)
                if (n.step >= s16 && ! taken (ns, n.step - s16) && r.nextFloat() < p.ghosts)
                    ghosts.push_back ({ n.step - s16, 1, n.pitch, 40, false, true });

            for (auto& g : ghosts)
                if (! taken (ns, g.step))
                    ns.push_back (g);
        }

        // Into the change: the next root pushed ahead of the one (neo-soul), or a chromatic step
        if (soul && p.anticipate && ! b.sectionEnd && b.sixteenth > 0)
        {
            clearFrom (ns, b.steps - s16);
            add (ns, b.steps - s16, s16 * 3, next, 100);
        }
        else if (p.approach && b.sixteenth > 0)
        {
            clearFrom (ns, b.steps - s16);
            add (ns, b.steps - s16, s16, approachTo (c.key, root, next, true), 84);
        }

        tidy (ns);

        c.note = soul ? juce::String ("the Dilla pocket: ") + (p.anticipate ? "the root pushed ahead of the one, " : "the root on the one, ")
                          + "slides into chord tones, ghost notes between"
                      : "warm and behind the beat: the root on the one, chord tones on the kick, a chromatic step into each change";
        return ns;
    }

    //==============================================================================
    //  Funk: on the one
    //==============================================================================
    Notes funkBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        juce::Random r (p.seed + (b.phraseEnd ? 3 : 0));
        const int root = c.key.place (b.chord.root);
        const int next = c.key.place (b.next.root);
        const int s16 = b.sixteenth > 0 ? b.sixteenth : 1;
        const int eighth = 2 * s16;
        const int octave = root + 12;
        const bool fourOnTheFloor = b.beats.size() >= 4 && std::all_of (b.beats.begin(), b.beats.end(), [&] (int beat) { return onKick (b, beat); });
        const bool long_ = c.req.legato > 0.3f;

        if ((c.kind == "chorus" || c.kind == "drop") && fourOnTheFloor && p.mode != 2)
        {
            for (int t = 0; t < b.steps; t += eighth)
                add (ns, t, eighth - (long_ ? 0 : s16 / 2), t % b.beat == 0 ? root : octave, t % b.beat == 0 ? 112 : 104);

            c.note = "disco octaves: low on the beat, the octave off it";
            return ns;
        }

        if (c.kind == "bridge")
        {
            add (ns, 0, b.steps / 2, root, 112);
            add (ns, b.steps / 2, b.steps / 2 - eighth, root + 7, 100);
            add (ns, b.steps - eighth, eighth, root - 2, 104);
            c.note = "the pocket opened out: half notes and a b7 pickup";
            return ns;
        }

        // The one, hard
        add (ns, 0, long_ ? b.beat : 3 * s16, root, 124);

        // The kick's notes: mostly the root, now and then the b7 below, the fifth, the octave
        for (auto k : b.kicks)
            if (k > 0)
            {
                const float x = r.nextFloat();
                const int pitch = c.req.pedal ? root : x < 0.6f ? root : x < 0.75f ? root - 2 : x < 0.9f ? root + 7 : octave;
                add (ns, k, long_ ? 3 * s16 : eighth, pitch, 108);
            }

        // Octave pops on the offbeat 16ths, never on the backbeat
        for (auto pos : p.pops)
            if (const int t = pos * s16; t < b.steps && ! taken (ns, t) && ! onSnare (b, t))
                add (ns, t, s16, octave, c.req.slap ? 124 : 116);

        // Dead notes ghosting in before the notes
        Notes ghosts;

        for (auto& n : ns)
            if (n.step >= s16 && ! taken (ns, n.step - s16) && ! onSnare (b, n.step - s16) && r.nextFloat() < p.ghosts)
                ghosts.push_back ({ n.step - s16, 1, n.pitch, 38, false, true });

        for (auto& g : ghosts)
            if (! taken (ns, g.step))
                ns.push_back (g);

        // Into the next one: a half step below it, or its b7
        if (p.approach && ! b.songEnd)
        {
            clearFrom (ns, b.steps - s16);
            add (ns, b.steps - s16, s16, approachTo (c.key, root, next, r.nextBool()), 100);
        }

        tidy (ns);

        // Short and percussive, but for the one
        if (! long_)
            for (auto& n : ns)
                if (n.step > 0 && ! n.ghost)
                    n.length = std::min (n.length, eighth);

        c.note = "on the one: the root hard on 1, the kick's notes"
                 + juce::String (p.pops.empty() ? "" : ", the octave popped on the offbeat 16ths") + ", dead notes ghosting in between";
        return ns;
    }

    //==============================================================================
    // Scoring a line against its style: locked to the kick as much as the genre is, about as
    // many notes as it plays, the root on the one, in range, no wild leaps, something new
    struct Targets
    {
        float lock = 0.6f, lockWeight = 3.0f;       // notes that land on a kick
        float cover = 0.6f, coverWeight = 2.0f;     // kicks that get a note
        float notes = 6.0f, notesWeight = 0.4f;     // notes a bar of 16 16ths
        bool rootOnOne = true;
    };

    Targets targetsFor (const Ctx& c)
    {
        Targets t;
        const auto f = c.family(), id = c.id();

        if (f == "metal")
        {
            t.lock = c.kind == "breakdown" ? 1.0f : 0.7f;
            t.cover = c.kind == "breakdown" ? 1.0f : 0.6f;
            t.lockWeight = c.kind == "breakdown" ? 6.0f : 2.0f;
            t.notes = c.kind == "breakdown" ? 5.0f : 7.0f;
        }
        else if (id == "trap" || id == "drill")
        {
            t.lock = 1.0f; t.lockWeight = 5.0f; t.cover = 0.85f; t.coverWeight = 4.0f; t.notes = 3.5f;
            t.rootOnOne = c.kind != "intro";
        }
        else if (f == "trap")
        {
            t.lock = 0.7f; t.cover = 0.85f; t.coverWeight = 3.0f; t.notes = 4.0f;
        }
        else if (f == "rock")
        {
            t.lock = 0.45f; t.lockWeight = 1.0f; t.cover = 0.9f; t.coverWeight = 3.0f; t.notes = id == "poppunk" || id == "punk" ? 8.0f : 7.0f;
        }
        else if (f == "funk")
        {
            t.lock = 0.45f; t.lockWeight = 1.5f; t.cover = 0.8f; t.coverWeight = 3.0f; t.notes = 7.0f + 2.0f * c.busy;
        }
        else if (id == "jazz" || id == "ballad" || c.req.walking)
        {
            t.lockWeight = 0.0f; t.coverWeight = 0.0f; t.notes = 4.0f;
        }
        else
        {
            t.lock = 0.5f; t.lockWeight = 1.5f; t.cover = 0.6f; t.notes = id == "neosoul" ? 6.0f : 4.0f;
            t.rootOnOne = id != "neosoul";
        }

        t.notes *= 0.75f + 0.5f * c.busy;
        return t;
    }

    float score (const std::vector<Notes>& bars, const std::vector<BarIn>& ins, const Ctx& c, const Targets& t, const Notes* previousSection)
    {
        float s = 0.0f;
        int last = -1;

        for (size_t i = 0; i < bars.size(); ++i)
        {
            const auto& b = ins[i];
            const auto& ns = bars[i];
            int onsets = 0, locked = 0, covered = 0;

            for (auto& n : ns)
                if (! n.ghost)
                {
                    ++onsets;
                    locked += onKick (b, n.step) ? 1 : 0;

                    if (n.pitch < c.key.bottom - 2 || n.pitch > c.key.top + 12)
                        s -= 2.0f;

                    if (last >= 0 && std::abs (n.pitch - last) > 12)
                        s -= 1.0f;

                    last = n.pitch;
                }

            for (auto k : b.kicks)
                covered += std::any_of (ns.begin(), ns.end(), [k] (const Note& n) { return n.step == k && ! n.ghost; }) ? 1 : 0;

            if (onsets > 0 && ! b.kicks.empty())
                s -= std::abs ((float) locked / (float) onsets - t.lock) * t.lockWeight;

            if (! b.kicks.empty())
                s -= std::abs ((float) covered / (float) b.kicks.size() - t.cover) * t.coverWeight;

            const float per16 = (float) onsets * 16.0f / (float) (b.sixteenth > 0 ? b.steps / b.sixteenth : 16);
            s -= std::abs (per16 - t.notes) * t.notesWeight;

            // Each style's signature, where it has one: drill's slides, trap's leap at the end of
            // a bar (now and then, not on every note), funk's pops and dead notes
            {
                int slides = 0, away = 0, ghosts = 0, high = 0;

                for (auto& n : ns)
                {
                    slides += n.slide ? 1 : 0;
                    ghosts += n.ghost ? 1 : 0;
                    away += ! n.ghost && ((n.pitch % 12) + 12) % 12 != b.chord.root ? 1 : 0;
                    high += ! n.ghost && n.pitch >= c.key.place (b.chord.root) + 12 ? 1 : 0;
                }

                if (c.id() == "drill" && b.kicks.size() >= 2)
                    s += slides > 0 ? 1.2f : -0.6f;
                else if (c.id() == "trap" && b.phraseEnd)
                    s += away > 0 && away <= 2 ? 0.6f : 0.0f;
                else if (c.family() == "funk" && c.kind != "bridge" && c.kind != "chorus")
                    s += (ghosts > 0 ? 0.5f : -0.3f) + (high > 0 ? 0.5f : -0.3f);
                else if (c.id() == "neosoul")
                    s += slides > 0 || ghosts > 0 ? 0.5f : 0.0f;
            }

            if (t.rootOnOne)
            {
                const bool rootFirst = ! ns.empty() && ns.front().step == 0 && ((ns.front().pitch % 12) + 12) % 12 == b.chord.root;
                s += rootFirst ? 1.5f : -1.5f;
            }
        }

        // Something of its own in each section: not the last one's line again
        if (previousSection != nullptr && ! bars.empty())
        {
            int same = 0;

            for (auto& n : bars.front())
                same += std::any_of (previousSection->begin(), previousSection->end(), [&n] (const Note& o) { return o.step == n.step; }) ? 1 : 0;

            if (same == (int) bars.front().size() && same == (int) previousSection->size())
                s -= 1.0f;
        }

        return s / (float) std::max<size_t> (1, bars.size());
    }

    Plan makePlan (Ctx& c)
    {
        Plan p;
        p.seed = c.rng.nextInt64();
        p.mode = c.pick (3);
        p.passing = c.req.pedal ? 0.0f : 0.1f + 0.5f * c.rng.nextFloat() * (0.5f + c.busy);
        p.jump = c.req.pedal ? 0.0f : c.rng.nextFloat() * (c.req.octaves ? 1.0f : 0.7f);
        p.ghosts = 0.15f + 0.4f * c.rng.nextFloat() * (0.5f + c.busy);
        p.slides = c.id() == "drill" ? 0.5f + 0.4f * c.rng.nextFloat() : c.id() == "neosoul" ? 0.3f * c.rng.nextFloat() : 0.25f * c.rng.nextFloat();
        p.approach = c.rng.nextFloat() < 0.75f;
        p.anticipate = c.rng.nextFloat() < 0.65f;

        static const int trapLeaps[] { 12, 12, 7, 10 }, drillLeaps[] { 3, 5, 7, -2, 12 }, others[] { 12, 7, 10 };
        p.interval = c.id() == "drill" ? drillLeaps[c.pick (5)] : c.family() == "trap" && c.id() != "boombap" && c.id() != "hiphop" ? trapLeaps[c.pick (4)] : others[c.pick (3)];

        if (c.req.octaves)
            p.interval = 12;

        // Funk's pops: one to three of the offbeat 16ths (more for slap, or busier)
        static const int offbeats[] { 6, 14, 10, 7, 11, 3, 15 };
        const int pops = c.req.slap ? 3 : 1 + c.pick (c.busy > 0.6f ? 3 : 2);

        for (int i = 0; i < pops; ++i)
            if (const int at = offbeats[c.pick (7)]; std::find (p.pops.begin(), p.pops.end(), at) == p.pops.end())
                p.pops.push_back (at);

        return p;
    }

    int unitOf (const BarIn& b)     { return std::max (1, b.sixteenth > 0 ? b.sixteenth : b.steps / 16); }

    // Electronic: house's offbeat bass (or octaves bouncing in eighths), techno's rolling sixteenths
    // between the kicks, EDM's pumping eighths, drum and bass's long Reese, dubstep's wobble in its drop,
    // synthwave's driving eighths with the octave on the offbeats
    Notes electronicBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        juce::Random r (p.seed + b.index % 2);
        const auto id = c.id();
        const int root = c.key.place (b.chord.root);
        const int u = unitOf (b), eighth = 2 * u;
        const bool drop = c.kind == "chorus" || c.kind == "drop" || c.kind == "hook";

        if (c.kind == "intro" && id != "techno" && id != "synthwave")
        {
            c.note = "out of the intro";
            return ns;
        }

        if (id == "dnb")
        {
            add (ns, 0, b.steps / 2, root, 110);
            add (ns, b.steps / 2, b.steps - b.steps / 2, b.phraseEnd && r.nextBool() ? root + 12 : root, 104);
            c.note = "a long Reese under each chord";
            return ns;
        }

        if (id == "dubstep")
        {
            // Held back until the drop (a Nova bass is as loud played softly, so holding back means fewer, shorter
            // notes): the build a sub on the one, the breakdown a held one
            if (! drop)
            {
                const bool build = c.kind == "prechorus";
                add (ns, 0, build ? b.beat : b.steps, root, 100);
                c.note = build ? "a short sub on the one, holding back for the drop" : "a held sub under the breakdown";
                return ns;
            }

            // The wobble: the root in sixteenths and eighths with a gap for the snare on 3, the octave answering
            static const std::vector<std::vector<std::array<int, 3>>> wobbles {      // 16th, length, octave
                { { 0, 3, 0 }, { 3, 3, 0 }, { 6, 2, 1 }, { 10, 2, 0 }, { 12, 2, 0 }, { 14, 2, 1 } },
                { { 0, 2, 0 }, { 2, 2, 0 }, { 4, 4, 1 }, { 10, 3, 0 }, { 13, 3, 0 } },
                { { 0, 4, 0 }, { 4, 2, 1 }, { 6, 2, 0 }, { 11, 2, 0 }, { 13, 3, 1 } } };

            for (auto& w : wobbles[(size_t) p.mode % wobbles.size()])
                if (w[0] * u < b.steps)
                    add (ns, w[0] * u, std::min (w[1] * u, b.steps - w[0] * u), root + 12 * w[2], w[0] == 0 ? 118 : 108);

            c.note = "the wobble, leaving room for the snare on 3";
            return ns;
        }

        if (id == "techno")
        {
            for (int s = 0; s < b.steps; s += u)
                if (s % b.beat != 0)
                    add (ns, s, u, r.nextFloat() < 0.15f ? root + 12 : root, s % eighth == 0 ? 112 : 96, r.nextFloat() < 0.1f);

            c.note = "rolling sixteenths between the kicks";
            return ns;
        }

        if (id == "synthwave")
        {
            for (int s = 0, i = 0; s < b.steps; s += eighth, ++i)
                add (ns, s, eighth, i % 2 == 1 ? root + 12 : root, i % 2 == 0 ? 112 : 100);

            c.note = "driving eighths, the octave on the offbeats";
            return ns;
        }

        if (id == "edm")
        {
            // The verse and the build the root on each beat, short (held back so the drop lands); the drop pumping
            // eighths; the breakdown is the piano's
            if (c.kind == "breakdown")
            {
                c.note = "out: the breakdown is the piano's";
                return ns;
            }

            if (! drop)
            {
                for (auto at : b.beats)
                    add (ns, at, u, root, 100);

                c.note = c.kind == "prechorus" ? "the root on each beat, short, holding back for the drop" : "the root on each beat, short";
                return ns;
            }

            for (int s = 0; s < b.steps; s += eighth)
                add (ns, s, eighth, root, s % b.beat == 0 ? 96 : 116);

            c.note = "pumping eighths, the offbeats harder";
            return ns;
        }

        // House: the offbeat bass, out of the kick's way, or octaves bouncing
        const bool bounce = p.mode == 1;

        for (auto at : b.beats)
        {
            if (bounce)
            {
                add (ns, at, u, root, 100);
                add (ns, at + eighth, eighth, root + 12, 112);
            }
            else if (at + eighth < b.steps)
                add (ns, at + eighth, eighth, root, 116);
        }

        c.note = bounce ? "octaves bouncing in eighths" : "the offbeat bass: on every and, out of the kick's way";
        return ns;
    }

    // Reggae: deep and melodic round the drop on 3 (the root, fifth, octave and third); reggaeton: the 808
    // on the dembow; afrobeats: short and syncopated, bouncing between the root and the fifth
    Notes islandBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        const auto id = c.id();
        const int root = c.key.place (b.chord.root);
        const int third = b.chord.third() >= 0 ? root + b.chord.third() : root + 7;
        const int u = unitOf (b);
        auto degree = [&] (int d) { return d == 0 ? root : d == 1 ? root + 7 : d == 2 ? root + 12 : third; };

        if (c.kind == "intro" && id != "reggae")
        {
            c.note = "out of the intro";
            return ns;
        }

        if (id == "reggaeton")
        {
            static const int at[] { 0, 3, 6, 8, 11, 14 };

            for (int i = 0; i < 6 && at[i] * u < b.steps; ++i)
            {
                const int end = i + 1 < 6 ? std::min (b.steps, at[i + 1] * u) : b.steps;
                add (ns, at[i] * u, end - at[i] * u, i == 5 && b.phraseEnd ? root + 12 : root, i % 3 == 0 ? 118 : 108);
            }

            c.note = "the 808 on the dembow";
            return ns;
        }

        using Line = std::vector<std::pair<int, int>>;     // 16th, degree (0 root, 1 fifth, 2 octave, 3 third)
        static const std::vector<Line> afro { { { 0, 0 }, { 3, 1 }, { 6, 0 }, { 10, 2 }, { 12, 1 } },
                                              { { 0, 0 }, { 4, 0 }, { 7, 1 }, { 10, 0 }, { 14, 3 } },
                                              { { 0, 0 }, { 3, 0 }, { 8, 1 }, { 11, 2 }, { 14, 1 } } };
        static const std::vector<Line> reggae { { { 0, 0 }, { 6, 0 }, { 8, 1 }, { 10, 2 }, { 12, 1 } },
                                                { { 2, 0 }, { 4, 0 }, { 8, 3 }, { 10, 1 }, { 14, 0 } },
                                                { { 0, 0 }, { 3, 0 }, { 6, 1 }, { 8, 2 }, { 12, 3 }, { 14, 1 } } };
        const bool afrobeats = id == "afrobeats";
        const auto& line = (afrobeats ? afro : reggae)[(size_t) p.mode % 3];

        for (size_t i = 0; i < line.size() && line[i].first * u < b.steps; ++i)
        {
            const int at = line[i].first * u;
            const int end = i + 1 < line.size() ? std::min (b.steps, line[i + 1].first * u) : b.steps;
            add (ns, at, afrobeats ? std::min (2 * u, end - at) : end - at, degree (line[i].second), i == 0 ? 114 : 104);
        }

        c.note = afrobeats ? "short and syncopated, bouncing between the root and the fifth" : "deep and melodic round the drop on 3";
        return ns;
    }

    // Bossa nova: the root on 1 held a dotted quarter, the fifth (below, when there's room) on the "and"
    // of 2 and again on 3, the root leading into the next bar: the surdo's bumba
    Notes latinBar (Ctx& c, const BarIn& b)
    {
        Notes ns;
        const int root = c.key.place (b.chord.root);
        const int fifthUp = root + (b.chord.has (6) ? 6 : 7);
        const int fifth = fifthUp - 12 >= c.key.floor ? fifthUp - 12 : fifthUp;
        const int u = unitOf (b);

        if (b.steps >= 16 * u)
        {
            add (ns, 0, 6 * u, root, 108);
            add (ns, 6 * u, 2 * u, fifth, 96);
            add (ns, 8 * u, 6 * u, fifth, 104);
            add (ns, 14 * u, b.steps - 14 * u, root, 96);
        }
        else
        {
            for (size_t i = 0; i < b.beats.size(); ++i)
                add (ns, b.beats[i], (i + 1 < b.beats.size() ? b.beats[i + 1] : b.steps) - b.beats[i], i % 2 == 0 ? root : fifth, 100);
        }

        c.note = "the bossa's bass: the root and the fifth, dotted, leaning into each bar";
        return ns;
    }

    // Ambient: a long soft root under each chord (out of the intro); cinematic: the same in its quiet
    // parts, an ostinato of driving eighths in its big ones and its build
    Notes ambientBar (Ctx& c, const BarIn& b)
    {
        Notes ns;
        const int root = c.key.place (b.chord.root);
        const bool big = c.req.heavyIn.contains (c.kind) || c.kind == "chorus" || c.kind == "drop" || c.kind == "hook" || c.kind == "prechorus";

        if (c.id() == "cinematic" && big)
        {
            const int eighth = 2 * unitOf (b);

            for (int s = 0; s < b.steps; s += eighth)
                add (ns, s, eighth, root, s % b.beat == 0 ? 112 : 100);

            c.note = "an ostinato of driving eighths";
            return ns;
        }

        if (c.kind == "intro" && c.id() == "ambient")
        {
            c.note = "out of the intro";
            return ns;
        }

        add (ns, 0, b.steps, root, 84);
        c.note = "a long soft root under each chord";
        return ns;
    }

    // Country: boom-chick, the root on 1 and the fifth on 3 (below when there's room); at the end of a
    // phrase, a walk up (or down) the scale to the next chord
    Notes countryBar (Ctx& c, const Plan&, const BarIn& b)
    {
        Notes ns;
        const int root = c.key.place (b.chord.root);
        const int fifth = root - 5 >= c.key.floor ? root - 5 : root + 7;
        const int half = b.steps / 2;

        if (b.phraseEnd && ! (b.next == b.chord) && b.beats.size() >= 4)
        {
            const int target = c.key.place (b.next.root);
            int pitch = root;
            add (ns, 0, b.beats[1], root, 108);

            for (size_t i = 1; i < b.beats.size(); ++i)
            {
                pitch = c.key.step (pitch, target >= pitch ? 1 : -1);
                add (ns, b.beats[i], (i + 1 < b.beats.size() ? b.beats[i + 1] : b.steps) - b.beats[i], pitch, 100);
            }

            c.note = "boom-chick, walking to each new chord";
            return ns;
        }

        add (ns, 0, half, root, 110);
        add (ns, half, b.steps - half, fifth, 100);
        c.note = "boom-chick: the root on 1, the fifth on 3";
        return ns;
    }

    Notes barFor (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        const auto f = c.family(), id = c.id();

        // The end of the song: one note, left to ring
        if (b.songEnd && c.kind == "outro")
        {
            add (ns, 0, b.steps, c.key.place (b.chord.root), 112);
            return ns;
        }

        // A soft section. Metal's bass sits it out with the guitars: the keys' left hand has the root,
        // and a Nova bass is as loud played softly as hard (measured: a held root kept the quiet verse
        // within 3 dB of the chorus). Other styles keep a short root at the top of each bar
        if (c.soft)
        {
            if (f == "metal")
            {
                c.note = "out: the quiet part is the keys'";
                return ns;
            }

            add (ns, 0, std::max (1, b.beat), c.key.place (b.chord.root), 60);
            c.note = "a short, soft root at the top of each bar";
            return ns;
        }

        if (c.req.walking && id != "trap" && id != "drill")
            ns = walkingBar (c, p, b);
        else if (f == "metal")
            ns = metalBar (c, p, b);
        else if (id == "trap" || id == "drill")
            ns = trapBar (c, p, b);
        else if (f == "trap")
            ns = hiphopBar (c, p, b);
        else if (f == "rock" && id == "country")
            ns = countryBar (c, p, b);
        else if (f == "rock")
            ns = rockBar (c, p, b);
        else if (f == "funk")
            ns = funkBar (c, p, b);
        else if (f == "electronic")
            ns = electronicBar (c, p, b);
        else if (f == "island")
            ns = islandBar (c, p, b);
        else if (f == "latin")
            ns = latinBar (c, b);
        else if (f == "ambient")
            ns = ambientBar (c, b);
        else
            ns = lofiBar (c, p, b);

        tidy (ns);

        // Inside its range: an octave at a time
        for (auto& n : ns)
        {
            while (n.pitch < c.key.bottom)         n.pitch += 12;
            while (n.pitch > c.key.top + 12)       n.pitch -= 12;
        }

        return ns;
    }

    //==============================================================================
    std::vector<int> hitsIn (const drummer::Bar& bar, const juce::String& voice, bool ghostsToo)
    {
        std::vector<int> hits;

        for (auto& lane : bar.lanes)
            if (lane.voice == voice)
                for (int i = 0; i < lane.hits.length(); ++i)
                    if (lane.hits[i] != '.' && (ghostsToo || (lane.hits[i] != 'g' && lane.hits[i] != 'G')))
                        hits.push_back (i);

        return hits;
    }

    BassTurn makeTurn (const juce::String& prompt, const Result& r)
    {
        BassTurn turn;
        turn.prompt = prompt.trim();
        turn.reply = r.reply;
        turn.part = juce::JSON::toString (r.part.toJson(), true);

        auto* answer = new juce::DynamicObject();
        answer->setProperty ("offline", true);
        answer->setProperty ("request", r.request.toVar());
        turn.answer = juce::JSON::toString (juce::var (answer), true);
        return turn;
    }
}

//==============================================================================
// How far under (or over) each sound's own level the bass sits, so it lands a little under the
// style's drums: measured bass alone against the mix with --bassrender
static const std::map<juce::String, float>& trims()
{
    // Measured 2026-11-06 against the kit each style's drummer picks (rock's and hard rock's
    // studio kits sit well under the others, so their bass comes down furthest); the aim, bass
    // alone against the drums alone: 1.5 dB under for metal, rock and funk, 1 for trap, hip-hop,
    // lofi and neo-soul, 2 for jazz
    static const std::map<juce::String, float> t {
        { "metalcore", -1.4f }, { "deathcore", -2.9f }, { "djent", -1.2f }, { "thrash", -4.4f }, { "blast", -3.3f }, { "metal", -5.1f },
        { "trap", 0.0f }, { "drill", -0.3f }, { "boombap", -2.2f }, { "hiphop", -2.6f },
        { "rock", -8.9f }, { "hardrock", -10.6f }, { "poppunk", -6.7f }, { "punk", -2.0f },
        { "lofi", -3.9f }, { "jazz", -5.4f }, { "ballad", -5.4f }, { "neosoul", -3.8f }, { "funk", -0.7f },
        // (Measured 2026-11-15: a dB under the drums for house, EDM, drum and bass, reggaeton and afrobeats, 1.5 for
        // techno and synthwave, level with dubstep's, half a dB under reggae's, 2 under cinematic's toms; bossa's and
        // country's brushes are too quiet to measure against, so there the bass sits 2 dB under the guitar, and in
        // ambient 6 under the piano)
        { "house", 0.1f }, { "techno", 1.9f }, { "edm", -1.1f }, { "dnb", -0.1f }, { "dubstep", -5.2f }, { "synthwave", -2.7f },
        { "reggae", -5.8f }, { "reggaeton", -2.5f }, { "afrobeats", 0.5f }, { "bossa", -8.6f }, { "ambient", -17.3f },
        { "cinematic", -6.5f }, { "country", -9.3f },
    };
    return t;
}

juce::var soundFor (const juce::String& style)
{
    struct Shape
    {
        const char* base;
        const char* name;
        std::vector<std::pair<const char*, float>> tweaks;
        float trimDb = 0.0f;        // where it sits under the style's drums (measured with --bassrender)
    };

    static const Shape metal { "Finger Bass", "Picked Metal Bass", { { "drive", 0.55f }, { "filter_cutoff", 1100.0f }, { "filter_env", 2.0f },
                                                                      { "amp_decay", 0.6f }, { "amp_sustain", 0.55f } } };
    static const Shape eightOhEight { "808 Bass", "808 Glide", { { "voice_mode", 2.0f }, { "glide", 0.09f }, { "amp_release", 0.04f } } };
    static const Shape rock { "Finger Bass", "Rock Bass", { { "drive", 0.3f }, { "filter_cutoff", 700.0f } } };
    static const Shape pick { "Finger Bass", "Pick Bass", { { "drive", 0.4f }, { "filter_cutoff", 1000.0f }, { "filter_env", 3.0f }, { "fenv_decay", 0.18f } } };
    static const std::map<juce::String, Shape> shapes {
        { "metalcore", metal }, { "deathcore", metal }, { "djent", metal }, { "thrash", metal }, { "blast", metal }, { "metal", metal },
        { "trap", eightOhEight }, { "drill", eightOhEight },
        { "boombap", { "Finger Bass", "Dusty Bass", { { "filter_cutoff", 260.0f }, { "filter_env", 1.5f }, { "amp_decay", 1.0f }, { "amp_sustain", 0.35f } } } },
        { "hiphop", { "Sub Bass", "Sub Bass", {} } },
        { "rock", rock }, { "hardrock", rock }, { "poppunk", pick }, { "punk", pick },
        { "lofi", { "Finger Bass", "Warm Lofi Bass", { { "filter_cutoff", 220.0f }, { "filter_env", 1.2f }, { "amp_decay", 1.2f }, { "amp_sustain", 0.4f } } } },
        { "jazz", { "Finger Bass", "Round Upright", { { "filter_cutoff", 260.0f }, { "filter_env", 1.6f }, { "fenv_decay", 0.12f },
                                                        { "amp_decay", 0.7f }, { "amp_sustain", 0.0f }, { "amp_release", 0.15f } } } },
        { "ballad", { "Finger Bass", "Round Upright", { { "filter_cutoff", 240.0f }, { "filter_env", 1.4f }, { "fenv_decay", 0.12f },
                                                          { "amp_decay", 0.9f }, { "amp_sustain", 0.0f }, { "amp_release", 0.2f } } } },
        { "neosoul", { "Moog Bass", "Neo Soul Moog", { { "amp_release", 0.03f }, { "glide", 0.06f } } } },
        { "funk", { "Finger Bass", "Funk Bass", { { "filter_cutoff", 650.0f }, { "filter_env", 4.0f }, { "fenv_decay", 0.12f }, { "amp_velocity", 0.85f } } } },
        { "house", { "Moog Bass", "House Bass", { { "amp_decay", 0.35f }, { "amp_sustain", 0.4f } } } },
        { "techno", { "Acid Line", "Acid Line", {} } },
        { "edm", { "Sub Bass", "Pumping Sub", {} } },
        { "dnb", { "Reese", "Reese", {} } },
        { "dubstep", { "Wobble Bass", "Wobble Bass", {} } },
        { "synthwave", { "Moog Bass", "Synthwave Bass", { { "filter_cutoff", 900.0f } } } },
        { "reggae", { "Finger Bass", "Dub Bass", { { "filter_cutoff", 240.0f }, { "filter_env", 1.0f }, { "amp_decay", 1.2f }, { "amp_sustain", 0.5f } } } },
        { "reggaeton", eightOhEight },
        { "afrobeats", { "Moog Bass", "Afro Bass", { { "amp_decay", 0.25f }, { "amp_sustain", 0.2f } } } },
        { "bossa", { "Finger Bass", "Round Upright", { { "filter_cutoff", 260.0f }, { "filter_env", 1.6f }, { "fenv_decay", 0.12f },
                                                         { "amp_decay", 0.7f }, { "amp_sustain", 0.0f }, { "amp_release", 0.15f } } } },
        { "ambient", { "Sub Bass", "Soft Sub", {} } },
        { "cinematic", { "Sub Bass", "Low Sub", {} } },
        { "country", rock },
    };

    const auto it = shapes.find (style);
    const auto& shape = it != shapes.end() ? it->second : rock;
    const auto* base = nova::findFactory (shape.base);

    if (base == nullptr)
        return {};

    auto patch = *base;
    patch.name = shape.name;
    patch.category = "Bass";

    for (auto& [id, value] : shape.tweaks)
        patch.settings[id] = value;

    // The style's own trim against its drums, on the style's own sound
    const auto trim = trims().count (style) > 0 ? trims().at (style) : 0.0f;
    patch.settings["volume"] = base->get ("volume") + trim;

    return patch.toVar();
}

Result write (const juce::String& prompt, const Song& song, const Request* previous, const drummer::Groove* drums,
              bool chooseSound, bool songIsBlank, juce::int64 seed)
{
    Result result;
    auto req = Request::parse (prompt, previous);

    if (req.style.isEmpty())
    {
        req.style = "rock";
        req.understood.insert (0, "rock (no style named)");
    }

    const auto* style = agents::findStyle (req.style);

    if (style == nullptr)
        style = agents::findStyle ("rock");

    // The drums it plays with: the drummer's part, else the ones its style would have
    if (drums != nullptr && ! drums->sections.empty())
    {
        result.drums = *drums;
    }
    else
    {
        result.drums = drummer::offline::write (prompt, song, previous, {}, false, {}, songIsBlank, seed).groove;
        result.imaginedDrums = true;
    }

    const auto& g = result.drums;
    const auto& defaults = defaultsFor (style->id);
    juce::Random rng (seed);

    Key key;
    key.root = req.keyRoot >= 0 ? req.keyRoot : defaults.key;
    key.mode = req.mode.isNotEmpty() ? req.mode : juce::String (defaults.mode);
    key.scale = scaleOf (key.mode);
    // Low: a tuning's lowest note, else the key's root as near the style's usual bottom as it goes
    // (not below B0, where a bass stops being heard)
    int low = defaults.low;

    if (req.lowest > 0)
    {
        low = req.lowest;
    }
    else if (req.keyRoot >= 0)
    {
        const int up = (((req.keyRoot - defaults.low) % 12) + 12) % 12;
        low = defaults.low + (up > 6 ? up - 12 : up);

        if (low < 23)
            low += 12;
    }

    key.low = juce::jlimit (21, 40, low);

    // A drop tuning's lowest string is its key note: nothing under it. An 808 goes down to C1;
    // a bass guitar to its low E
    const auto family = juce::String (style->family);
    key.floor = family == "metal" || req.lowest > 0 ? key.low : juce::String (style->id) == "trap" || juce::String (style->id) == "drill" ? 24 : std::min (28, key.low);
    key.bottom = key.floor;
    key.top = key.low + 21;

    const int beats = g.meterBeats > 0 ? g.meterBeats : (req.meterBeats > 0 ? req.meterBeats : song.meterBeats);
    const int unit = g.meterUnit > 0 ? g.meterUnit : (req.meterUnit > 0 ? req.meterUnit : song.meterUnit);
    const int sixteenthsInBar = juce::jmax (1, beats * 16 / unit);

    Ctx c { req, *style, rng, key };
    c.busy = req.busy;
    c.tempo = req.tempo > 0 ? req.tempo : (g.tempo > 0 ? g.tempo : song.bpm);

    // The progression for each kind of section, the same each time that kind comes round
    std::map<juce::String, juce::StringArray> progressions;
    std::vector<std::vector<Chord>> chords;

    for (auto& s : g.sections)
    {
        const auto kind = kindOf (s.name);

        if (progressions.count (kind) == 0)
        {
            // The blues' twelve bars (asked for, or the blues' own), in every section: dominant
            // sevenths, the IV in the fifth bar (or the second, a quick change), the V turning it round.
            // A minor key's: the minor blues
            using Options = std::vector<juce::StringArray>;
            static const Options twelveBar { { "I7", "I7", "I7", "I7", "IV7", "IV7", "I7", "I7", "V7", "IV7", "I7", "V7" },
                                             { "I7", "IV7", "I7", "I7", "IV7", "IV7", "I7", "I7", "V7", "IV7", "I7", "V7" } };
            static const Options minorTwelveBar { { "i7", "i7", "i7", "i7", "iv7", "iv7", "i7", "i7", "bVI7", "V7", "i7", "V7" } };
            const auto& options = req.changes != "twelvebar" ? progressionsFor (style->id, kind)
                                : key.mode == "minor" || key.mode == "dorian" || key.mode == "phrygian" || key.mode == "aeolian" ? minorTwelveBar
                                                                                                                                 : twelveBar;
            progressions[kind] = options[(size_t) rng.nextInt ((int) options.size())];
        }

        std::vector<Chord> barChords;
        const auto& prog = progressions[kind];

        for (size_t b = 0; b < s.bars.size(); ++b)
            barChords.push_back (chordOf (prog[(int) (b % (size_t) prog.size())], key.root, sharpKey (key.root, key.mode)));

        chords.push_back (std::move (barChords));
    }

    juce::StringArray notes;
    std::optional<Notes> previousMain;
    int lastPitch = -1;

    for (size_t si = 0; si < g.sections.size(); ++si)
    {
        const auto& ds = g.sections[si];
        const auto kind = kindOf (ds.name);
        c.kind = kind;
        c.soft = req.softIn.contains (kind);
        c.energy = juce::jlimit (0.0f, 1.0f, req.energy + (kind == "chorus" || kind == "breakdown" || kind == "hook" ? 0.15f : kind == "intro" ? -0.1f : 0.0f));
        const bool lastSection = si + 1 == g.sections.size();

        std::vector<BarIn> ins;

        for (size_t b = 0; b < ds.bars.size(); ++b)
        {
            BarIn in;
            in.steps = ds.bars[b].steps;
            in.index = (int) b;
            in.count = (int) ds.bars.size();
            in.beats = beatStarts (in.steps, beats, unit);
            in.beat = in.beats.size() > 1 ? in.beats[1] : in.steps;
            in.sixteenth = in.steps % sixteenthsInBar == 0 ? in.steps / sixteenthsInBar : 0;
            in.kicks = hitsIn (ds.bars[b], "kick", false);
            in.snares = hitsIn (ds.bars[b], "snare", false);
            in.chord = chords[si][b];
            in.next = b + 1 < ds.bars.size() ? chords[si][b + 1] : (! lastSection && ! chords[si + 1].empty() ? chords[si + 1].front() : chords[si].front());
            in.phraseEnd = b % 4 == 3;
            in.sectionEnd = b + 1 == ds.bars.size();
            in.songEnd = in.sectionEnd && lastSection;
            in.nextKind = ! lastSection ? kindOf (g.sections[si + 1].name) : juce::String();
            ins.push_back (in);
        }

        // The best of many ways through it
        const auto targets = targetsFor (c);
        std::vector<Notes> best;
        juce::String bestNote;
        float bestScore = -1.0e9f;
        int bestLast = lastPitch;

        for (int candidate = 0; candidate < 20; ++candidate)
        {
            const auto plan = makePlan (c);
            std::vector<Notes> bars;
            int carried = lastPitch;
            juce::String note;

            for (auto in : ins)
            {
                in.prevPitch = carried;
                c.note = {};
                bars.push_back (barFor (c, plan, in));

                if (note.isEmpty())
                    note = c.note;

                for (auto& n : bars.back())
                    if (! n.ghost)
                        carried = n.pitch;
            }

            const float sc = score (bars, ins, c, targets, previousMain.has_value() ? &*previousMain : nullptr) + rng.nextFloat() * 0.3f;

            if (sc > bestScore)
            {
                bestScore = sc;
                best = std::move (bars);
                bestNote = note;
                bestLast = carried;
            }
        }

        lastPitch = bestLast;
        Section section;
        section.name = ds.name;

        for (size_t b = 0; b < best.size(); ++b)
        {
            Bar bar;
            bar.steps = ins[b].steps;
            bar.chord = ins[b].chord.name;
            bar.notes = best[b];
            section.bars.push_back (std::move (bar));
        }

        if (! best.empty())
            previousMain = best.front();

        // The chords as they go round, for the reply
        juce::StringArray names;

        for (auto& ch : chords[si])
            if (names.size() < 4)
                names.add (ch.name);

        if (bestNote.isNotEmpty())
            notes.add (ds.name + " (" + names.joinIntoString (" ") + "): " + bestNote + ".");

        result.part.sections.push_back (std::move (section));
    }

    auto& part = result.part;
    part.arrangement = g.arrangement;
    part.key = key.name() + (req.lowest > 0 ? ", tuned to " + spell (key.low % 12, sharpKey (key.root, key.mode)) : juce::String());
    part.swing = g.swing;
    part.swingSixteenths = g.swingSixteenths;
    part.looseMs = defaults.loose;

    // Locked to the kick, it sits where the kick does, then leans its own way
    const float kickPush = g.pushMs.count ("kick") > 0 ? g.pushMs.at ("kick") : 0.0f;
    part.pushMs = kickPush + defaults.push * (juce::String (style->family) == "metal" || juce::String (style->id) == "trap" ? 0.0f : 1.0f);

    if (req.tempo > 0)
        part.tempo = req.tempo;
    else if (result.imaginedDrums && g.tempo > 0)
        part.tempo = g.tempo;

    if (result.imaginedDrums && g.meterBeats > 0)
    {
        part.meterBeats = g.meterBeats;
        part.meterUnit = g.meterUnit;
    }

    if (chooseSound)
    {
        part.patch = soundFor (style->id);
        part.sound = part.patch.getProperty ("name", {}).toString();
    }

    const double tempo = part.tempo > 0 ? part.tempo : c.tempo;
    part.title = juce::String (style->name) + " bass, " + part.key;

    juce::String reply = juce::String (style->name) + " bass in " + part.key + " at " + juce::String (juce::roundToInt (tempo)) + " bpm"
                         + (part.sound.isNotEmpty() ? ", on " + part.sound : juce::String()) + ". ";
    reply << notes.joinIntoString (" ");

    if (result.imaginedDrums)
        reply << " No drums yet, so it played to the ones a " << juce::String (style->name).toLowerCase()
              << " drummer would: ask the AI drummer for them and the bass follows theirs.";

    part.message = reply;
    result.reply = reply;
    result.request = req;
    return result;
}

BassTurn turnFor (const juce::String& prompt, const Result& r)
{
    return makeTurn (prompt, r);
}

std::optional<agents::Request> requestOf (const BassTurn& turn)
{
    const auto v = juce::JSON::parse (turn.answer);

    if (! (bool) v.getProperty ("offline", false))
        return {};

    return Request::fromVar (v.getProperty ("request", {}));
}

} // namespace nightshift::bass::offline
