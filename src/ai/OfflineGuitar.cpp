// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "OfflineGuitar.h"
#include "OfflineBass.h"
#include "OfflineDrummer.h"

namespace nightshift::guitar::offline
{

using agents::Request;

namespace
{
    //==============================================================================
    // The chords as the bassist named them ("Cm", "Bbmaj7", "E9", "Ddim", "C5"), and the key
    struct Chord
    {
        int root = 0;
        std::vector<int> tones { 0, 4, 7 };
        juce::String name;

        bool has (int interval) const       { return std::find (tones.begin(), tones.end(), interval) != tones.end(); }
        int third() const                   { return has (3) ? 3 : has (4) ? 4 : (has (7) && tones.size() == 2 ? -1 : 4); }
        int seventh() const                 { return has (10) ? 10 : has (11) ? 11 : -1; }
        bool minor() const                  { return has (3); }
        bool operator== (const Chord& o) const { return root == o.root && tones == o.tones; }
    };

    int pitchClassOf (const juce::String& name, int& length)
    {
        static const juce::String letters ("C D EF G A B");
        length = 0;

        if (name.isEmpty())
            return -1;

        int pc = letters.indexOfChar (juce::CharacterFunctions::toUpperCase (name[0]));

        if (pc < 0)
            return -1;

        length = 1;

        if (name.length() > 1 && (name[1] == '#' || name[1] == 'b'))
        {
            pc += name[1] == '#' ? 1 : -1;
            length = 2;
        }

        return (pc + 12) % 12;
    }

    Chord parseChord (const juce::String& name)
    {
        Chord c;
        c.name = name;
        int length = 0;
        const int pc = pitchClassOf (name, length);

        if (pc < 0)
            return c;

        c.root = pc;
        const auto rest = name.substring (length);

        if (rest.startsWith ("dim"))                                c.tones = { 0, 3, 6 };
        else if (rest.startsWith ("m") && ! rest.startsWith ("maj")) c.tones = { 0, 3, 7 };
        else if (rest == "5")                                       c.tones = { 0, 7 };

        if (rest.contains ("maj7"))                                 c.tones.push_back (11);
        else if (rest.contains ("7") || rest.contains ("9"))        c.tones.push_back (10);

        if (rest.contains ("9"))                                    c.tones.push_back (14);

        return c;
    }

    std::vector<int> scaleOf (const juce::String& mode)
    {
        if (mode == "major")        return { 0, 2, 4, 5, 7, 9, 11 };
        if (mode == "dorian")       return { 0, 2, 3, 5, 7, 9, 10 };
        if (mode == "phrygian")     return { 0, 1, 3, 5, 7, 8, 10 };
        if (mode == "mixolydian")   return { 0, 2, 4, 5, 7, 9, 10 };
        if (mode == "lydian")       return { 0, 2, 4, 6, 7, 9, 11 };
        return { 0, 2, 3, 5, 7, 8, 10 };
    }

    struct Key
    {
        int root = 4;
        juce::String mode = "minor";
        std::vector<int> scale = scaleOf ("minor");

        bool inScale (int pitch) const
        {
            const int d = (((pitch - root) % 12) + 12) % 12;
            return std::find (scale.begin(), scale.end(), d) != scale.end();
        }

        int step (int pitch, int dir) const
        {
            for (int p = pitch + dir; std::abs (p - pitch) <= 3; p += dir)
                if (inScale (p))
                    return p;

            return pitch + dir * 2;
        }
    };

    // "C minor", "E dorian, tuned to E"
    Key keyFrom (const juce::String& text)
    {
        Key k;
        int length = 0;

        if (const int pc = pitchClassOf (text.trim(), length); pc >= 0)
            k.root = pc;

        const auto mode = text.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (",", false, false).trim().toLowerCase();

        if (mode.isNotEmpty())
            k.mode = mode;

        k.scale = scaleOf (k.mode);
        return k;
    }

    // The lowest pitch at or above `from` with this pitch class
    int atOrAbove (int pc, int from)        { return from + ((((pc - from) % 12) + 12) % 12); }

    //==============================================================================
    // Each style's guitar: its sound, the strum, where it sits against the beat
    struct StyleGuitar
    {
        const char* sound;
        float strumMs, push, loose;
    };

    const StyleGuitar& defaultsFor (const juce::String& style)
    {
        static const std::map<juce::String, StyleGuitar> table {
            { "metalcore", { "High Gain", 3.0f, 0.0f, 1.5f } }, { "deathcore", { "High Gain", 3.0f, 0.0f, 1.5f } },
            { "djent",     { "Djent", 2.5f, 0.0f, 1.0f } },     { "thrash",    { "High Gain", 3.0f, -1.0f, 2.0f } },
            { "blast",     { "High Gain", 2.5f, 0.0f, 1.5f } }, { "metal",     { "High Gain", 3.5f, 0.0f, 2.0f } },
            { "trap",      { "Clean", 0.0f, 0.0f, 3.0f } },     { "drill",     { "Clean", 0.0f, 0.0f, 3.0f } },
            { "boombap",   { "Lofi Clean", 9.0f, 6.0f, 6.0f } },{ "hiphop",    { "Clean", 8.0f, 3.0f, 4.0f } },
            { "rock",      { "Crunch", 7.0f, 0.0f, 3.0f } },    { "hardrock",  { "Crunch", 6.0f, 0.0f, 3.0f } },
            { "poppunk",   { "Punk", 4.0f, -1.0f, 2.0f } },     { "punk",      { "Punk", 4.0f, -2.0f, 2.5f } },
            { "lofi",      { "Lofi Clean", 14.0f, 9.0f, 8.0f } },{ "jazz",     { "Jazz Box", 8.0f, 0.0f, 6.0f } },
            { "ballad",    { "Jazz Box", 18.0f, 4.0f, 7.0f } }, { "neosoul",   { "Clean", 10.0f, 8.0f, 9.0f } },
            { "funk",      { "Funk Clean", 2.0f, 0.0f, 2.0f } },
            { "house",     { "Funk Clean", 2.0f, 0.0f, 1.5f } }, { "techno",    { "Clean", 0.0f, 0.0f, 1.0f } },
            { "edm",       { "Clean", 0.0f, 0.0f, 1.0f } },      { "dnb",       { "Clean", 0.0f, 0.0f, 2.0f } },
            { "dubstep",   { "Clean", 0.0f, 0.0f, 1.0f } },      { "synthwave", { "Clean", 0.0f, 0.0f, 2.0f } },
            { "reggae",    { "Clean", 4.0f, 6.0f, 5.0f } },      { "reggaeton", { "Clean", 0.0f, 0.0f, 2.0f } },
            { "afrobeats", { "Clean", 0.0f, 2.0f, 3.0f } },      { "bossa",     { "Jazz Box", 8.0f, 0.0f, 6.0f } },
            { "ambient",   { "Clean", 20.0f, 0.0f, 8.0f } },     { "cinematic", { "High Gain", 3.0f, 0.0f, 2.0f } },
            { "country",   { "Clean", 6.0f, 0.0f, 3.0f } },
        };

        if (auto it = table.find (style); it != table.end())
            return it->second;

        return table.at ("rock");
    }

    // The tuning: metal drops to the key (the bass's low note an octave up), djent on a 7-string,
    // thrash and metal in standard (or drop D), the rest in standard
    std::vector<int> tuningFor (const juce::String& style, const juce::String& family, int keyRoot, const Request& req)
    {
        const std::vector<int> standard { 40, 45, 50, 55, 59, 64 };

        if (family != "metal")
            return standard;

        int low = req.lowest > 0 ? req.lowest + 12 : atOrAbove (keyRoot, 33);

        // Djent on a 7-string in standard (B E A D G B E) when the key sits low enough for it
        if (style == "djent" && (keyRoot == 9 || keyRoot == 10 || keyRoot == 11) && req.lowest == 0)
        {
            std::vector<int> t;

            for (int s : { 0, 5, 10, 15, 20, 24, 29 })
                t.push_back (24 + keyRoot + s);

            return t;
        }

        if ((style == "thrash" || style == "metal") && req.lowest == 0)
            return keyRoot == 2 ? std::vector<int> { 38, 45, 50, 55, 59, 64 } : standard;

        while (low > 40) low -= 12;
        while (low < 33) low += 12;
        std::vector<int> t;

        for (int s : { 0, 7, 12, 17, 21, 26 })
            t.push_back (low + s);

        return t;
    }

    juce::String kindOf (const juce::String& name)
    {
        const auto n = name.toLowerCase();

        if (n == "head")        return "verse";
        if (n == "solos")       return "chorus";
        if (n == "pre-chorus")  return "prechorus";

        return n;
    }

    //==============================================================================
    struct BarIn
    {
        int steps = 16, index = 0, count = 1;
        std::vector<int> beats;
        int beat = 4, sixteenth = 1;
        std::vector<int> kicks, snares, crashes;
        Chord chord, next;
        bool phraseEnd = false, sectionEnd = false, songEnd = false;
        juce::String nextKind;
    };

    std::vector<int> beatStarts (int steps, int beats, int unit)
    {
        std::vector<int> groups;

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

        int total = 0;

        for (auto g : groups)
            total += g;

        std::vector<int> starts;
        int at = 0;

        for (auto g : groups)
        {
            starts.push_back (at * steps / std::max (1, total));
            at += g;
        }

        return starts;
    }

    struct Ctx
    {
        const Request& req;
        const agents::Style& style;
        juce::Random& rng;
        Key key;
        std::vector<int> tuning;
        juce::String kind;
        bool soft = false;                  // asked for this section ("soft verses")
        float energy = 0.5f, busy = 0.5f;
        double tempo = 120.0;
        juce::String note;

        int pick (int n)                    { return rng.nextInt (juce::jmax (1, n)); }
        juce::String id() const             { return style.id; }
        juce::String family() const         { return style.family; }
    };

    struct Plan
    {
        int mode = 0, arp = 0, lead = 3;
        float passing = 0.4f, scratch = 0.85f, hammers = 0.3f, slides = 0.2f;
        bool anticipate = false;
        std::vector<int> stabs;             // where the chord (or a lead note) goes, in 16ths
        juce::int64 seed = 1;
    };

    using Notes = std::vector<Note>;

    void add (Notes& ns, int step, int length, int pitch, int velocity)
    {
        Note n;
        n.step = step;
        n.length = std::max (1, length);
        n.pitch = pitch;
        n.velocity = juce::jlimit (1, 127, velocity);
        ns.push_back (n);
    }

    // A chord: every note at once, strummed down (or up); `onStrings` puts it on the strings from there up
    void chord (Notes& ns, int step, int length, const std::vector<int>& pitches, int velocity, bool up = false, bool palmMute = false, bool muted = false,
                int onStrings = -1)
    {
        for (size_t i = 0; i < pitches.size(); ++i)
        {
            const auto p = pitches[i];
            add (ns, step, length, p, velocity);
            ns.back().string = onStrings >= 0 ? onStrings + (int) i : -1;
            ns.back().up = up;
            ns.back().palmMute = palmMute;
            ns.back().muted = muted;
        }
    }

    bool takenAt (const Notes& ns, int step)
    {
        return std::any_of (ns.begin(), ns.end(), [step] (const Note& n) { return n.step == step; });
    }

    bool onKick (const BarIn& b, int step)   { return std::find (b.kicks.begin(), b.kicks.end(), step) != b.kicks.end(); }
    bool onSnare (const BarIn& b, int step)  { return std::find (b.snares.begin(), b.snares.end(), step) != b.snares.end(); }
    bool onCrash (const BarIn& b, int step)  { return std::find (b.crashes.begin(), b.crashes.end(), step) != b.crashes.end(); }

    //==============================================================================
    // Voicings: the chord's notes as each genre's guitarist plays them
    std::vector<int> power (int root, bool octave = true)
    {
        return octave ? std::vector<int> { root, root + 7, root + 12 } : std::vector<int> { root, root + 7 };
    }

    // A barre chord: the E shape from a root on the low string, the A shape from one on the next
    std::vector<int> barre (const Chord& c, int root)
    {
        const int third = c.third() >= 0 ? c.third() : 4;

        if (root < 45)
            return { root, root + 7, root + 12, root + 12 + third, root + 19, root + 24 };

        return { root, root + 7, root + 12, root + 12 + third, root + 19 };
    }

    // Jazz shells: the root, its seventh and third (and the fifth on top for four voices)
    std::vector<int> shell (const Chord& c, int root, bool four)
    {
        const int seventh = c.seventh() >= 0 ? c.seventh() : (c.minor() ? 10 : 11);
        const int third = c.third() >= 0 ? c.third() : 4;
        std::vector<int> v { root, root + seventh, root + 12 + third };

        if (four)
            v.push_back (root + 19);

        return v;
    }

    // Rootless, up the neck: third, fifth, seventh, ninth (lofi and neo-soul)
    std::vector<int> rootless (const Chord& c, int around)
    {
        const int seventh = c.seventh() >= 0 ? c.seventh() : (c.minor() ? 10 : 11);
        const int third = c.third() >= 0 ? c.third() : 4;
        const int base = atOrAbove ((c.root + third) % 12, around);
        return { base, base - third + 7 + (7 < third ? 12 : 0), base - third + seventh + (seventh < third ? 12 : 0), base - third + 14 };
    }

    // Funk's ninth on the top strings: the seventh, the ninth, the fifth above
    std::vector<int> topNinth (const Chord& c)
    {
        const int r = atOrAbove (c.root, 52);
        return { r + 10, r + 14, r + 19 };
    }

    //==============================================================================
    //  Metal
    //==============================================================================
    Notes metalBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        juce::Random r (p.seed + (b.phraseEnd ? 7 : 0));
        const auto id = c.id();
        const int low = c.tuning.front();
        const int root = atOrAbove (b.chord.root, low);
        const int s16 = std::max (1, b.sixteenth);
        const int eighth = 2 * s16;
        const float density = (float) b.kicks.size() * s16 / (float) b.steps;

        auto chug = [&] (int step, int length, int velocity)
        {
            add (ns, step, length, root, velocity);
            ns.back().palmMute = true;
            ns.back().string = 0;
        };

        auto stab = [&] (int step, int length, int at)
        {
            chord (ns, step, length, power (at), 116);
        };

        // Locked to the kick: a palm-muted chug on each, the power chord ringing where the china
        // (or a crash) hits
        auto locked = [&] (bool lift)
        {
            for (size_t j = 0; j < b.kicks.size(); ++j)
            {
                const int k = b.kicks[j];
                const int gap = (j + 1 < b.kicks.size() ? b.kicks[j + 1] : b.steps) - k;

                if (lift && b.phraseEnd && j + 1 == b.kicks.size() && j > 0 && b.chord.root == c.key.root)
                    stab (k, gap, root + (id == "deathcore" ? 6 : 1));
                else if ((j == 0 && (onCrash (b, k) || p.mode == 2)) || (onCrash (b, k) && gap >= 4 * s16 && p.mode != 0))
                    stab (k, gap, root);      // the first hit, or a long one under the china, left to ring
                else
                    chug (k, gap <= 2 * eighth ? std::min (gap, eighth) : gap - s16, j == 0 ? 122 : 112);
            }
        };

        if (b.songEnd && c.kind == "outro")
        {
            stab (0, b.steps, root);
            c.note = "one power chord, left to ring";
        }
        else if (c.kind == "intro" && b.kicks.size() <= 2)
        {
            stab (0, b.steps, root);
            c.note = "power chords ringing out over the build";
        }
        else if (c.kind == "breakdown" || c.req.lockKick || (id == "djent" && c.kind != "chorus"))
        {
            locked (true);
            c.note = c.kind == "breakdown" ? "palm-muted chugs on the open low string, locked to every kick, the power chord ringing where a phrase starts, a "
                                               + juce::String (id == "deathcore" ? "tritone" : "b2") + " stab to end each phrase"
                                           : "palm-muted chugs locked to the kick";
        }
        else if (id == "blast" || (c.kind != "chorus" && density > 0.45f && id != "thrash"))
        {
            // Tremolo picking: 16ths on the power chord (or the root an octave up, black metal's way)
            for (int t = 0; t < b.steps; t += s16)
            {
                if (p.mode == 1)
                    add (ns, t, s16, root + 12, 100);
                else
                    chord (ns, t, s16, power (root, false), 100, (t / s16) % 2 == 1);
            }

            c.note = p.mode == 1 ? "tremolo picking the root an octave up, over the blast" : "tremolo-picked power chords, 16ths, over the double kick";
        }
        else if (id == "thrash" && (p.mode != 1 || c.req.gallops))
        {
            for (auto beat : b.beats)
            {
                chug (beat, eighth, 116);
                chug (beat + eighth, s16, 102);
                chug (beat + eighth + s16, s16, 104);
            }

            if (! (b.chord == b.next) || b.index == 0)
            {
                ns.erase (std::remove_if (ns.begin(), ns.end(), [] (const Note& n) { return n.step == 0; }), ns.end());
                stab (0, eighth, root);
            }

            c.note = "palm-muted gallops on the low string, a power chord to start each change";
        }
        else if (c.kind == "chorus" || c.kind == "hook" || c.kind == "drop")
        {
            if (p.mode == 0)
            {
                stab (0, b.steps, root);
                c.note = "big open power chords, ringing a bar each";
            }
            else if (p.mode == 1)
            {
                for (int t = 0; t < b.steps; t += eighth)
                    chord (ns, t, eighth, power (root), t % b.beat == 0 ? 116 : 104);

                c.note = "open power chords in 8ths, driving";
            }
            else
            {
                stab (0, b.steps / 2, root);
                stab (b.steps / 2, b.steps / 2, root);
                c.note = "half-note power chords, the chorus wide open";
            }
        }
        else
        {
            // The verse riff: chugs on the kick and the 8ths, notes higher up answering them
            for (size_t j = 0; j < b.kicks.size(); ++j)
                chug (b.kicks[j], s16 * 2, j == 0 ? 120 : 110);

            for (int t = 0; t < b.steps; t += eighth)
                if (! takenAt (ns, t) && r.nextFloat() < p.passing)
                    chug (t, eighth, 104);

            static const int leadIntervals[] { 3, 5, 7, 8, 10, 12, 1 };
            const int lead = root + leadIntervals[p.lead % 7];

            for (auto at : p.stabs)
                if (const int t = at * s16; t < b.steps && ! onKick (b, t))
                {
                    ns.erase (std::remove_if (ns.begin(), ns.end(), [t] (const Note& n) { return n.step == t; }), ns.end());

                    if (p.mode == 2)
                        stab (t, eighth, lead);
                    else
                        add (ns, t, eighth, lead + 12, 112);
                }

            if (! (b.chord == b.next) || b.index == 0)
            {
                ns.erase (std::remove_if (ns.begin(), ns.end(), [] (const Note& n) { return n.step == 0; }), ns.end());
                stab (0, eighth, root);
            }

            c.note = "palm-muted chugs on the kick, notes up the neck answering them";
        }

        return ns;
    }

    //==============================================================================
    //  Rock, hard rock, pop-punk, punk
    //==============================================================================
    Notes rockBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        juce::Random r (p.seed);
        const auto id = c.id();
        const bool punk = id == "poppunk" || id == "punk";
        const int s16 = std::max (1, b.sixteenth);
        const int eighth = 2 * s16;
        const int root = atOrAbove (b.chord.root, c.tuning.front());
        const int next = atOrAbove (b.next.root, c.tuning.front());

        if (punk)
        {
            const bool muted = c.kind == "verse" || c.kind == "prechorus" || (c.kind == "intro" && p.mode == 0);

            if (c.kind == "bridge")
            {
                const int at = atOrAbove (b.chord.root, 45);

                for (int t = 0; t < b.steps; t += eighth)
                    chord (ns, t, eighth, { at, at + 12 }, t % b.beat == 0 ? 112 : 100);

                c.note = "octave chords in 8ths for the bridge";
            }
            else
            {
                for (int t = 0; t < b.steps; t += eighth)
                    chord (ns, t, eighth, power (root, ! muted), t % b.beat == 0 ? 116 : 106, false, muted);

                c.note = muted ? "palm-muted power chords, downpicked 8ths" : "power chords wide open, downpicked 8ths";
            }

            // Into the next chord a beat early, where the song pushes
            if (p.anticipate && ! (b.chord == b.next) && ! b.sectionEnd)
            {
                ns.erase (std::remove_if (ns.begin(), ns.end(), [&] (const Note& n) { return n.step == b.steps - eighth; }), ns.end());
                chord (ns, b.steps - eighth, eighth, power (next), 116);
            }

            return ns;
        }

        if (id == "hardrock" && c.kind != "chorus" && c.kind != "bridge")
        {
            // The riff: a power chord, then the pentatonic on the low strings
            chord (ns, 0, 3 * s16, power (root), 118);
            static const int penta[] { 3, 5, 7, 10, 12, 7, 5, 3 };

            for (size_t i = 0; i < p.stabs.size(); ++i)
                if (const int t = p.stabs[i] * s16; t > 2 * s16 && t < b.steps && ! takenAt (ns, t))
                {
                    add (ns, t, s16 * 2, root + penta[(i + (size_t) p.lead) % 8], 108);
                    ns.back().palmMute = r.nextFloat() < 0.3f;
                }

            c.note = "the riff: a power chord, then the pentatonic down on the low strings";
            return ns;
        }

        int mode = p.mode;

        if (c.kind == "chorus" || c.kind == "hook")
            mode = p.mode == 2 ? 3 : 1;
        else if (c.kind == "bridge")
            mode = 4;

        if (mode == 0)
        {
            for (int t = 0; t < b.steps; t += eighth)
                chord (ns, t, eighth, power (root, false), t % b.beat == 0 ? 112 : 102, false, true);

            c.note = "palm-muted power chords in 8ths";
        }
        else if (mode == 1)
        {
            // D D U U D U, the barre chord ringing
            const auto v = barre (b.chord, root);

            for (auto [t16, up] : std::initializer_list<std::pair<int, bool>> { { 0, false }, { 4, false }, { 6, true }, { 10, true }, { 12, false }, { 14, true } })
                if (const int t = t16 * s16; t < b.steps)
                    chord (ns, t, t16 == 0 ? 4 * s16 : 2 * s16, up ? std::vector<int> (v.begin() + 2, v.end()) : v, up ? 92 : 108, up);

            c.note = "barre chords strummed, down, down, up, up, down, up";
        }
        else if (mode == 2)
        {
            for (auto beat : b.beats)
                chord (ns, beat, b.beat, power (root), beat == 0 ? 114 : 104);

            c.note = "power chords on the beat, ringing";
        }
        else if (mode == 3)
        {
            for (int t = 0; t < b.steps; t += eighth)
                chord (ns, t, eighth, power (root), t % b.beat == 0 ? 116 : 106);

            c.note = "open power chords in 8ths";
        }
        else
        {
            chord (ns, 0, b.steps, barre (b.chord, root), 104);
            c.note = "a barre chord a bar, ringing out";
        }

        if (p.anticipate && ! (b.chord == b.next) && ! b.sectionEnd && mode != 4)
        {
            ns.erase (std::remove_if (ns.begin(), ns.end(), [&] (const Note& n) { return n.step >= b.steps - eighth; }), ns.end());
            chord (ns, b.steps - eighth, eighth, power (next), 112);
        }

        return ns;
    }

    //==============================================================================
    //  Trap, drill, boom bap, hip-hop: a clean loop
    //==============================================================================
    Notes urbanBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        juce::Random r (p.seed);
        const auto id = c.id();
        const auto v = rootless (b.chord, id == "trap" || id == "drill" ? 50 : 55);

        if (id == "trap" || id == "drill")
        {
            // An arpeggio up, down, or both, between about G3 and E5; in triplets where the bar is
            std::vector<int> tones { v[0], v[1], v[2], v[3], v[1] + 12 };
            std::sort (tones.begin(), tones.end());
            tones.erase (std::unique (tones.begin(), tones.end()), tones.end());
            const bool triplets = b.sixteenth == 0 || b.steps % 3 == 0;
            const int each = triplets ? std::max (1, b.steps / 12) : std::max (1, b.sixteenth) * (c.kind == "intro" ? 4 : 2);
            int i = 0;

            for (int t = 0; t < b.steps; t += each, ++i)
            {
                const int n = (int) tones.size();
                int at = p.arp == 0 ? i % n : p.arp == 1 ? n - 1 - i % n : p.arp == 2 ? (i / (n - 1)) % 2 == 0 ? i % (n - 1) : n - 1 - i % (n - 1)
                                                                                      : (i % 2 == 0 ? 0 : 1 + (i / 2) % (n - 1));
                add (ns, t, each * 2, tones[(size_t) juce::jlimit (0, n - 1, at)], 74 + (t % b.beat == 0 ? 10 : 0));
            }

            c.note = juce::String ("a clean arpeggio ") + (p.arp == 0 ? "climbing" : p.arp == 1 ? "falling" : p.arp == 2 ? "up and down" : "picking between the low note and the rest")
                     + (triplets ? " in triplets" : " in 8ths") + ", ringing";
            return ns;
        }

        // Boom bap and hip-hop: jazzy stabs between the kicks, an answer at the end of the bar
        const int s16 = std::max (1, b.sixteenth);

        for (size_t i = 0; i < p.stabs.size() && i < 3; ++i)
            if (const int t = p.stabs[i] * s16; t < b.steps && ! takenAt (ns, t))
                chord (ns, t, 3 * s16, v, i == 0 ? 92 : 84, i % 2 == 1);

        if (r.nextFloat() < p.passing && ! takenAt (ns, b.steps - 2 * s16))
            add (ns, b.steps - 2 * s16, 2 * s16, v.back() + 1 - 2, 80);

        c.note = "jazzy stabs between the kicks, an answer at the end of the bar";
        return ns;
    }

    //==============================================================================
    //  Jazz, ballads, lofi, neo-soul
    //==============================================================================
    Notes jazzBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        juce::Random r (p.seed + b.index);
        const auto id = c.id();

        if (id == "jazz")
        {
            const int root = atOrAbove (b.chord.root, 40);
            const auto v = shell (b.chord, root < 43 ? root + 12 : root, false);
            const int beats = (int) b.beats.size();
            const int beatLen = b.beat;

            if (p.mode == 0 || c.kind == "intro")
            {
                for (int i = 0; i < beats; ++i)
                    chord (ns, b.beats[(size_t) i], std::max (1, beatLen * 2 / 3), v, i % 2 == 1 ? 86 : 78);

                c.note = "comping four to the bar in shells, short and even, a little more on 2 and 4";
            }
            else if (p.mode == 1)
            {
                chord (ns, 0, beatLen, v, 88);

                if (beats >= 2)
                    chord (ns, b.beats[1] + beatLen * 2 / 3, beatLen, v, 84, true);

                c.note = "the Charleston: the chord on 1 and just before 3, in shells";
            }
            else
            {
                if (beats >= 2)
                    chord (ns, b.beats[1], beatLen / 2 + 1, v, 82);

                const auto next = shell (b.next, atOrAbove (b.next.root, 43), false);
                chord (ns, b.steps - 1, 2, next, 88, true);
                c.note = "comping sparse: on 2, then the next chord pushed ahead of the one";
            }

            return ns;
        }

        if (id == "ballad")
        {
            const auto v = shell (b.chord, atOrAbove (b.chord.root, 40), true);

            for (int i = 0; i < (int) v.size() && i < (int) b.beats.size(); ++i)
                add (ns, b.beats[(size_t) i], b.steps - b.beats[(size_t) i], v[(size_t) i], 76 - i * 3);

            c.note = "the chord broken up a note a beat, each left ringing";
            return ns;
        }

        const int s16 = std::max (1, b.sixteenth);
        const auto v = rootless (b.chord, id == "neosoul" ? 55 : 47);

        if (id == "lofi")
        {
            chord (ns, 0, 8 * s16, v, 74, true);

            for (int t : { 10, 12, 14 })
                if (const int at = t * s16; at < b.steps && r.nextFloat() < p.passing)
                    add (ns, at, 2 * s16, v[(size_t) (t / 2) % v.size()], 64);

            if (r.nextFloat() < p.hammers && b.steps >= 16)
            {
                add (ns, 13 * s16, s16, v.back() - 2, 60);
                add (ns, 14 * s16, 2 * s16, v.back(), 66);
                ns.back().hammer = true;
            }

            c.note = "soft rootless chords strummed up, a few notes picked after, a hammer-on now and then";
            return ns;
        }

        // Neo-soul: the chord (or the next one pushed ahead), a syncopated stab, hammer-ons on top
        const bool anticipated = p.anticipate && b.index > 0;

        if (! anticipated)
            chord (ns, 0, 5 * s16, v, 80);

        const int stab = (p.mode == 0 ? 6 : 10) * s16;

        if (stab < b.steps)
            chord (ns, stab, 2 * s16, v, 72, true);

        if (r.nextFloat() < p.hammers && 12 * s16 < b.steps)
        {
            add (ns, 11 * s16, s16, v.back() - 2, 62);
            add (ns, 12 * s16, 3 * s16, v.back(), 70);
            ns.back().hammer = true;
        }

        if (r.nextFloat() < p.slides && 3 * s16 < b.steps && ! takenAt (ns, 2 * s16))
        {
            add (ns, 2 * s16, s16, v[1] - 1, 64);
            add (ns, 3 * s16, 2 * s16, v[1], 72);
            ns.back().slide = true;
        }

        if (p.anticipate && ! b.sectionEnd)
            chord (ns, b.steps - s16, 3 * s16, rootless (b.next, 55), 80);

        c.note = juce::String ("ninths and elevens up the neck") + (p.anticipate ? ", the next chord pushed ahead of the one" : "")
                 + ", hammer-ons and slides on top";
        return ns;
    }

    //==============================================================================
    //  Funk: the 16th scratch
    //==============================================================================
    Notes funkBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        juce::Random r (p.seed + (b.phraseEnd ? 3 : 0));
        const int s16 = std::max (1, b.sixteenth);
        const auto v = topNinth (b.chord);
        const bool fourOnTheFloor = b.beats.size() >= 4 && std::all_of (b.beats.begin(), b.beats.end(), [&] (int beat) { return onKick (b, beat); });

        if ((c.kind == "chorus" || c.kind == "drop") && fourOnTheFloor)
        {
            for (int t = 0; t < b.steps; t += s16)
            {
                const bool offbeat = (t / s16) % 4 == 2;
                chord (ns, t, s16, v, offbeat ? 112 : 58, (t / s16) % 2 == 1, false, ! offbeat, (int) c.tuning.size() - 3);
            }

            c.note = "disco chucks on the off-beats, scratching in between";
            return ns;
        }

        if (c.kind == "bridge")
        {
            // A single-note line on the low strings, the 16ths half palm-muted
            const int root = atOrAbove (b.chord.root, 45);
            static const int line[] { 0, 10, 7, 0, 12, 10, 7, 5 };

            for (int t = 0, i = 0; t < b.steps; t += s16 * 2, ++i)
            {
                add (ns, t, s16 * 2, root + line[(i + p.lead) % 8] - (line[(i + p.lead) % 8] > 7 ? 12 : 0), t % b.beat == 0 ? 112 : 98);
                ns.back().palmMute = i % 2 == 1;
            }

            c.note = "a single-note line down on the low strings";
            return ns;
        }

        // The hand never stops: 16ths, down and up, the strings muted but for the stabs
        for (int t = 0; t < b.steps; t += s16)
        {
            const bool stab = std::find (p.stabs.begin(), p.stabs.end(), t / s16) != p.stabs.end();
            const bool up = (t / s16) % 2 == 1;

            // The ninth's shape on the top three strings, one fret across them
            const int top = (int) c.tuning.size() - 3;

            if (stab)
                chord (ns, t, c.req.legato > 0.3f ? 3 * s16 : s16 + s16 / 2, v, onSnare (b, t) || t == 0 ? 120 : 112, up, false, false, top);
            else if (r.nextFloat() < p.scratch * (c.kind == "intro" ? 0.6f : 1.0f))
                chord (ns, t, s16, v, 52 + r.nextInt (12), up, false, true, top);
        }

        c.note = "the 16th chicken-scratch, ninth-chord stabs cutting through on the syncopations";
        return ns;
    }

    //==============================================================================
    // Scoring a part against its style
    float score (const std::vector<Notes>& bars, const std::vector<BarIn>& ins, const Ctx& c, const Notes* previousSection)
    {
        float s = 0.0f;
        const auto f = c.family(), id = c.id();

        for (size_t i = 0; i < bars.size(); ++i)
        {
            const auto& b = ins[i];
            const auto& ns = bars[i];
            std::set<int> onsets;
            int muted = 0, pm = 0, total = 0, locked = 0, unplayable = 0;

            for (auto& n : ns)
            {
                ++total;
                onsets.insert (n.step);
                muted += n.muted ? 1 : 0;
                pm += n.palmMute ? 1 : 0;
                unplayable += n.string < 0 ? 1 : 0;
            }

            for (auto t : onsets)
                locked += onKick (b, t) ? 1 : 0;

            s -= (float) unplayable * 3.0f;

            if (f == "metal" && (c.kind == "breakdown" || c.kind == "verse") && ! onsets.empty())
            {
                s += (float) locked / (float) onsets.size() * 2.0f;
                s += total > 0 && (float) pm / (float) total > 0.4f ? 1.0f : 0.0f;
            }
            else if (f == "funk" && c.kind != "bridge" && total > 0)
            {
                s += (float) muted / (float) total > 0.4f ? 1.5f : -1.0f;
                const int stabs = (int) onsets.size() - muted / 3;
                s -= std::abs ((float) stabs - (3.0f + 2.0f * c.busy)) * 0.3f;
            }
            else if (f == "rock" && ! onsets.empty())
            {
                s -= std::abs ((float) onsets.size() - 6.0f - 2.0f * c.busy) * 0.2f;
            }

            // The chord's root (or its stab) where the bar starts, unless it was pushed ahead
            if (id != "neosoul" && id != "lofi" && f != "funk" && id != "trap" && id != "drill" && c.kind != "intro")
                s += std::any_of (ns.begin(), ns.end(), [&] (const Note& n) { return n.step == 0 && ((n.pitch % 12) + 12) % 12 == b.chord.root; }) ? 1.0f : -1.0f;
        }

        if (previousSection != nullptr && ! bars.empty() && bars.front().size() == previousSection->size())
            s -= 0.5f;

        return s / (float) std::max<size_t> (1, bars.size());
    }

    Plan makePlan (Ctx& c)
    {
        Plan p;
        p.seed = c.rng.nextInt64();
        p.mode = c.pick (3);
        p.arp = c.pick (4);
        p.lead = c.pick (7);
        p.passing = 0.2f + 0.6f * c.rng.nextFloat() * (0.5f + c.busy);
        p.scratch = 0.7f + 0.3f * c.rng.nextFloat();
        p.hammers = 0.2f + 0.5f * c.rng.nextFloat();
        p.slides = c.req.slides ? 0.8f : 0.3f * c.rng.nextFloat();
        p.anticipate = c.rng.nextFloat() < 0.5f;

        // Where the stabs go: funk's syncopations (the one, the backbeats, the "a"s and "e"s),
        // metal's answers on the off-beats, hip-hop's spaces between the kicks
        static const int funk[] { 0, 3, 4, 6, 7, 10, 12, 14, 15 }, offbeats[] { 6, 10, 14, 3, 7, 11, 15 }, spaces[] { 2, 6, 10, 14, 7, 11 },
                         pentas[] { 4, 6, 8, 10, 12, 13, 14 };
        const auto f = c.family();

        if (f == "funk")
        {
            p.stabs.push_back (0);
            const int extra = 2 + c.pick (c.busy > 0.6f ? 4 : 3);

            for (int i = 0; i < extra; ++i)
                if (const int at = funk[1 + c.pick (8)]; std::find (p.stabs.begin(), p.stabs.end(), at) == p.stabs.end())
                    p.stabs.push_back (at);
        }
        else
        {
            const int* from = f == "metal" ? offbeats : c.id() == "hardrock" ? pentas : spaces;
            const int size = f == "metal" ? 7 : c.id() == "hardrock" ? 7 : 6;
            const int count = f == "metal" ? 1 + c.pick (2) : c.id() == "hardrock" ? 4 + c.pick (3) : 2 + c.pick (2);

            for (int i = 0; i < count; ++i)
                if (const int at = from[c.pick (size)]; std::find (p.stabs.begin(), p.stabs.end(), at) == p.stabs.end())
                    p.stabs.push_back (at);

            std::sort (p.stabs.begin(), p.stabs.end());
        }

        return p;
    }

    int unitOf (const BarIn& b)     { return std::max (1, b.sixteenth > 0 ? b.sixteenth : b.steps / 16); }

    // The chord's top notes, high on the neck: a skank's, a chop's, a swell's
    std::vector<int> highChord (const Chord& ch, int from)
    {
        auto v = barre (ch, atOrAbove (ch.root, from));
        return std::vector<int> (v.end() - std::min<std::ptrdiff_t> (3, (std::ptrdiff_t) v.size()), v.end());
    }

    // Electronic: out of the way. Synthwave's clean guitar picks the chord in eighths; house's chops the
    // offbeats short; techno, EDM, drum and bass and dubstep leave it to the synths
    Notes electronicBar (Ctx& c, const BarIn& b)
    {
        Notes ns;
        const auto id = c.id();
        const int u = unitOf (b), eighth = 2 * u;

        if (id == "synthwave")
        {
            const auto tones = barre (b.chord, atOrAbove (b.chord.root, c.tuning.front() + 12));

            for (int s = 0, i = 0; s < b.steps; s += eighth, ++i)
                add (ns, s, eighth, tones[(size_t) (1 + i % (int) (tones.size() - 1))], i % 2 == 0 ? 92 : 80);

            c.note = "the chord picked in eighths, clean";
            return ns;
        }

        if (id == "house")
        {
            const auto chord = highChord (b.chord, 55);

            for (auto at : b.beats)
                if (at + eighth < b.steps)
                    for (auto pitch : chord)
                        add (ns, at + eighth, u, pitch, 84);

            c.note = "short chops on the offbeats";
            return ns;
        }

        c.note = "out: the synths have it";
        return ns;
    }

    // Reggae's skank: a short high chord on 2 and 4 (doubled, the "and" after each, in a chorus);
    // reggaeton: single plucks on the dembow; afrobeats' highlife: single notes up the chord in sixteenths
    Notes islandBar (Ctx& c, const BarIn& b)
    {
        Notes ns;
        const auto id = c.id();
        const int u = unitOf (b);
        const auto chord = highChord (b.chord, 57);
        const bool hook = c.kind == "chorus" || c.kind == "hook" || c.kind == "drop";

        if (id == "reggaeton")
        {
            static const int at[] { 0, 3, 6, 8, 11, 14 };

            for (int i = 0; i < 6 && at[i] * u < b.steps; ++i)
                add (ns, at[i] * u, u, chord[(size_t) i % chord.size()], 82);

            c.note = "plucks on the dembow";
            return ns;
        }

        if (id == "afrobeats")
        {
            static const int at[] { 0, 2, 3, 5, 6, 8, 10, 11, 13, 14 };
            const auto tones = barre (b.chord, atOrAbove (b.chord.root, 52));

            for (int i = 0; i < 10 && at[i] * u < b.steps; ++i)
                add (ns, at[i] * u, u, tones[(size_t) (2 + i % std::max (1, (int) tones.size() - 2))], i % 3 == 0 ? 90 : 78);

            c.note = "highlife: single notes up the chord in sixteenths";
            return ns;
        }

        for (size_t i = 1; i < b.beats.size(); i += 2)
        {
            for (auto pitch : chord)
                add (ns, b.beats[i], u, pitch, 96);

            if (hook && b.beats[i] + 2 * u < b.steps)
                for (auto pitch : chord)
                    add (ns, b.beats[i] + 2 * u, u, pitch, 84);
        }

        c.note = hook ? "the skank doubled: on 2 and 4 and the and after each" : "the skank: a short high chord on 2 and 4";
        return ns;
    }

    // Bossa nova: the thumb on the root (1) and the fifth (3), the fingers on the chord's shell (its
    // seventh and third) on the off-beats: the a of 1, the and of 2, the and of 3, the a of 4
    Notes latinBar (Ctx& c, const BarIn& b)
    {
        Notes ns;
        const int u = unitOf (b);
        const int root = atOrAbove (b.chord.root, c.tuning.front());
        const auto s = shell (b.chord, root, false);

        if (b.steps >= 16 * u)
        {
            add (ns, 0, 8 * u, root, 84);
            add (ns, 8 * u, 8 * u, root + 7, 78);

            for (int at : { 3, 6, 10, 13 })
                for (size_t i = 1; i < s.size(); ++i)
                    add (ns, at * u, 2 * u, s[i] + 12, 72);
        }
        else
        {
            for (auto at : b.beats)
                for (auto pitch : s)
                    add (ns, at, u * 2, pitch + 12, 72);
        }

        c.note = "bossa: the thumb on the root and the fifth, the chord's shell on the off-beats";
        return ns;
    }

    // Ambient: swells, the chord's top notes held a bar, softly; cinematic's big parts: a power chord held
    // under the toms
    Notes ambientBar (Ctx& c, const BarIn& b)
    {
        Notes ns;
        const bool big = c.req.heavyIn.contains (c.kind) || c.kind == "chorus" || c.kind == "drop" || c.kind == "hook";

        if (c.id() == "cinematic" && big)
        {
            for (auto pitch : power (atOrAbove (b.chord.root, c.tuning.front())))
                add (ns, 0, b.steps, pitch, 104);

            c.note = "a power chord held under the toms";
            return ns;
        }

        for (auto pitch : highChord (b.chord, 59))
            add (ns, 0, b.steps, pitch, 58);

        c.note = "swells: the chord's top notes held a bar, softly";
        return ns;
    }

    // Country: boom-chick, a bass note on 1 and 3 (the root, then the fifth) and the chord strummed on 2
    // and 4
    Notes countryBar (Ctx& c, const BarIn& b)
    {
        Notes ns;
        const int root = atOrAbove (b.chord.root, c.tuning.front());
        auto chord = barre (b.chord, root);
        chord.erase (chord.begin());

        for (size_t i = 0; i < b.beats.size(); ++i)
        {
            const int at = b.beats[i], until = i + 1 < b.beats.size() ? b.beats[i + 1] : b.steps;

            if (i % 2 == 0)
                add (ns, at, until - at, i == 0 ? root : root + 7, 92);
            else
                for (auto pitch : chord)
                    add (ns, at, until - at, pitch, 84);
        }

        c.note = "boom-chick: a bass note on 1 and 3, the chord strummed on 2 and 4";
        return ns;
    }

    Notes barFor (Ctx& c, const Plan& p, const BarIn& b)
    {
        const auto f = c.family();

        // A soft section: metal's guitars sit it out (the keys and synth carry Sleep Token's quiet
        // verses); any other style picks the chord gently, a note a beat, up through its shape
        if (c.soft)
        {
            Notes ns;

            if (f == "metal")
            {
                c.note = "out: the soft part is the keys'";
                return ns;
            }

            const int root = atOrAbove (b.chord.root, c.tuning.front());
            const auto tones = barre (b.chord, root);

            for (size_t i = 0; i < b.beats.size(); ++i)
            {
                const int at = b.beats[i];
                const int until = i + 1 < b.beats.size() ? b.beats[i + 1] : b.steps;
                add (ns, at, until - at, tones[(size_t) (1 + (int) i % (int) (tones.size() - 1))], i == 0 ? 76 : 66);
            }

            c.note = "the chord picked a note a beat, quietly";
            return ns;
        }

        if (f == "metal")                       return metalBar (c, p, b);
        if (f == "rock" && c.id() == "country") return countryBar (c, b);
        if (f == "rock")                        return rockBar (c, p, b);
        if (f == "trap")                        return urbanBar (c, p, b);
        if (f == "funk")                        return funkBar (c, p, b);
        if (f == "electronic")                  return electronicBar (c, b);
        if (f == "island")                      return islandBar (c, b);
        if (f == "latin")                       return latinBar (c, b);
        if (f == "ambient")                     return ambientBar (c, b);
        return jazzBar (c, p, b);
    }

    std::vector<int> hitsIn (const drummer::Bar& bar, std::initializer_list<const char*> voices, bool ghostsToo)
    {
        std::vector<int> hits;

        for (auto& lane : bar.lanes)
            for (auto v : voices)
                if (lane.voice == v)
                    for (int i = 0; i < lane.hits.length(); ++i)
                        if (lane.hits[i] != '.' && (ghostsToo || lane.hits[i] != 'g'))
                            hits.push_back (i);

        std::sort (hits.begin(), hits.end());
        hits.erase (std::unique (hits.begin(), hits.end()), hits.end());
        return hits;
    }
}

//==============================================================================
// Where the guitar's fader goes in each style, against its drums (measured with --bandrender:
// the guitar alone against the drums alone): a wall of guitar in metal, level with the drums;
// a dB under in pop-punk and rock; funk's rhythm part 2 under, jazz comping 3 under, the
// clean loops of trap and hip-hop and lofi's chords 2 to 3 under
float levelFor (const juce::String& style)
{
    static const std::map<juce::String, float> trims {
        { "metalcore", 4.3f }, { "deathcore", 4.3f }, { "djent", 4.3f }, { "thrash", 4.3f }, { "blast", 4.3f }, { "metal", 4.3f },
        { "poppunk", 1.5f }, { "punk", 1.5f }, { "rock", -2.9f }, { "hardrock", -2.9f },
        { "funk", 3.3f }, { "jazz", -3.8f }, { "ballad", -3.8f }, { "lofi", 0.5f }, { "neosoul", 0.0f },
        { "trap", 0.0f }, { "drill", 0.0f }, { "boombap", 0.0f }, { "hiphop", 0.0f },
        { "bossa", -3.8f }, { "ambient", -4.0f }, { "cinematic", 4.3f }, { "country", -2.9f },
    };

    const auto it = trims.find (style);
    return it != trims.end() ? it->second : 0.0f;
}

juce::String soundFor (const juce::String& style)
{
    return defaultsFor (style).sound;
}

Result write (const juce::String& prompt, const Song& song, const Request* previous, const drummer::Groove* drums,
              const bass::Part* bassPart, bool chooseSound, bool songIsBlank, juce::int64 seed)
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

    // The drums and the bass it plays with: the band's, or the ones its style would have
    if (drums != nullptr && ! drums->sections.empty())
        result.drums = *drums;
    else
    {
        result.drums = drummer::offline::write (prompt, song, previous, {}, false, {}, songIsBlank, seed).groove;
        result.imaginedDrums = true;
    }

    bass::Part bassLine;

    if (bassPart != nullptr && ! bassPart->sections.empty())
        bassLine = *bassPart;
    else
    {
        bassLine = bass::offline::write (prompt, song, previous, &result.drums, false, songIsBlank, seed).part;
        result.imaginedBass = true;
    }

    const auto& g = result.drums;
    const auto& defaults = defaultsFor (style->id);
    juce::Random rng (seed);

    Key key = keyFrom (bassLine.key);

    if (req.keyRoot >= 0 && bassPart == nullptr)
        key.root = req.keyRoot;

    const auto tuning = tuningFor (style->id, style->family, key.root, req);
    const int beats = g.meterBeats > 0 ? g.meterBeats : song.meterBeats;
    const int unit = g.meterUnit > 0 ? g.meterUnit : song.meterUnit;
    const int sixteenthsInBar = juce::jmax (1, beats * 16 / unit);

    Ctx c { req, *style, rng, key, tuning };
    c.busy = req.busy;
    c.tempo = req.tempo > 0 ? req.tempo : (g.tempo > 0 ? g.tempo : song.bpm);

    auto chordsOf = [&] (const juce::String& sectionName, size_t bars)
    {
        std::vector<Chord> list;
        const bass::Section* bs = nullptr;

        for (auto& s : bassLine.sections)
            if (s.name == sectionName)
                bs = &s;

        for (size_t b = 0; b < bars; ++b)
            list.push_back (bs != nullptr && ! bs->bars.empty() ? parseChord (bs->bars[std::min (b, bs->bars.size() - 1)].chord) : Chord { key.root, { 0, 3, 7 }, {} });

        return list;
    };

    std::vector<std::vector<Chord>> chords;

    for (auto& s : g.sections)
        chords.push_back (chordsOf (s.name, s.bars.size()));

    juce::StringArray notes;
    std::optional<Notes> previousMain;
    int hand = 3;

    for (size_t si = 0; si < g.sections.size(); ++si)
    {
        const auto& ds = g.sections[si];
        c.kind = kindOf (ds.name);
        c.soft = req.softIn.contains (c.kind);
        c.energy = juce::jlimit (0.0f, 1.0f, req.energy + (c.kind == "chorus" || c.kind == "breakdown" ? 0.15f : c.kind == "intro" ? -0.1f : 0.0f));
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
            in.kicks = hitsIn (ds.bars[b], { "kick" }, false);
            in.snares = hitsIn (ds.bars[b], { "snare" }, false);
            in.crashes = hitsIn (ds.bars[b], { "china", "crash", "crash_2" }, false);
            in.chord = chords[si][b];
            in.next = b + 1 < ds.bars.size() ? chords[si][b + 1] : (! lastSection && ! chords[si + 1].empty() ? chords[si + 1].front() : chords[si].front());
            in.phraseEnd = b % 4 == 3;
            in.sectionEnd = b + 1 == ds.bars.size();
            in.songEnd = in.sectionEnd && lastSection;
            in.nextKind = ! lastSection ? kindOf (g.sections[si + 1].name) : juce::String();
            ins.push_back (in);
        }

        std::vector<Notes> best;
        juce::String bestNote;
        float bestScore = -1.0e9f;
        int bestHand = hand;

        for (int candidate = 0; candidate < 20; ++candidate)
        {
            const auto plan = makePlan (c);
            std::vector<Notes> bars;
            int candidateHand = hand;
            juce::String note;

            for (auto& in : ins)
            {
                c.note = {};
                auto ns = barFor (c, plan, in);
                finger (ns, tuning, candidateHand);
                bars.push_back (std::move (ns));

                if (note.isEmpty())
                    note = c.note;
            }

            const float sc = score (bars, ins, c, previousMain.has_value() ? &*previousMain : nullptr) + rng.nextFloat() * 0.3f;

            if (sc > bestScore)
            {
                bestScore = sc;
                best = std::move (bars);
                bestNote = note;
                bestHand = candidateHand;
            }
        }

        hand = bestHand;
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

        juce::StringArray names;

        for (auto& ch : chords[si])
            if (names.size() < 4 && ch.name.isNotEmpty())
                names.add (ch.name);

        if (bestNote.isNotEmpty())
            notes.add (ds.name + (names.isEmpty() ? juce::String() : " (" + names.joinIntoString (" ") + ")") + ": " + bestNote + ".");

        result.part.sections.push_back (std::move (section));
    }

    auto& part = result.part;
    part.arrangement = g.arrangement;
    part.key = bassLine.key.upToFirstOccurrenceOf (",", false, false).trim();
    part.tuning = tuning;
    part.swing = g.swing;
    part.swingSixteenths = g.swingSixteenths;
    part.looseMs = defaults.loose;
    part.strumMs = defaults.strumMs;
    part.pushMs = (g.pushMs.count ("kick") > 0 && juce::String (style->family) == "metal" ? g.pushMs.at ("kick") : 0.0f) + defaults.push;

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
        // "Clean" asked for (math rock's, midwest emo's): the clean sound, whatever the style's own
        part.sound = req.clean ? juce::String ("Clean") : juce::String (defaults.sound);
        part.levelDb = levelFor (style->id);
    }

    const double tempo = part.tempo > 0 ? part.tempo : c.tempo;
    part.title = juce::String (style->name) + " guitar, " + part.key;

    juce::String reply = juce::String (style->name) + " guitar in " + part.key + " at " + juce::String (juce::roundToInt (tempo)) + " bpm, in "
                         + tuningName (tuning) + " (" + tuningNotes (tuning) + ")" + (part.sound.isNotEmpty() ? ", on " + part.sound : juce::String()) + ". ";
    reply << notes.joinIntoString (" ");

    if (result.imaginedDrums || result.imaginedBass)
        reply << " No " << (result.imaginedDrums && result.imaginedBass ? "drums or bass" : result.imaginedDrums ? "drums" : "bass")
              << " yet, so it played to what a " << juce::String (style->name).toLowerCase() << " band would: ask the AI "
              << (result.imaginedBass ? "bassist" : "drummer") << " and the guitar follows theirs.";

    part.message = reply;
    result.reply = reply;
    result.request = req;
    return result;
}

GuitarTurn turnFor (const juce::String& prompt, const Result& r)
{
    GuitarTurn turn;
    turn.prompt = prompt.trim();
    turn.reply = r.reply;
    turn.part = juce::JSON::toString (r.part.toJson(), true);

    auto* answer = new juce::DynamicObject();
    answer->setProperty ("offline", true);
    answer->setProperty ("request", r.request.toVar());
    turn.answer = juce::JSON::toString (juce::var (answer), true);
    return turn;
}

std::optional<agents::Request> requestOf (const GuitarTurn& turn)
{
    const auto v = juce::JSON::parse (turn.answer);

    if (! (bool) v.getProperty ("offline", false))
        return {};

    return Request::fromVar (v.getProperty ("request", {}));
}

} // namespace nightshift::guitar::offline
