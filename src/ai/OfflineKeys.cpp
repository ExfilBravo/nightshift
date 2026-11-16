// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "OfflineKeys.h"
#include "OfflineBass.h"
#include "OfflineDrummer.h"
#include "Theory.h"
#include "audio/Instruments.h"

namespace nightshift::keys::offline
{

using agents::Request;
using theory::Chord;
using theory::atOrAbove;
using theory::led;

namespace
{
    //==============================================================================
    struct StyleKeys
    {
        const char* sampled;        // a sampled instrument it prefers, by name, when it's here
        const char* nova;           // else (or anyway) this Nova sound
        float rollMs, push, loose, level;  // level: its fader against the style's drums (--bandrender)
    };

    const StyleKeys& defaultsFor (const juce::String& style)
    {
        static const std::map<juce::String, StyleKeys> table {
            { "metalcore", { "Grand Piano", "Felt Keys",       0.0f, 0.0f, 3.0f, 4.3f } },
            { "deathcore", { "",            "Dark Matter",     0.0f, 0.0f, 2.0f, 0.0f } },
            { "djent",     { "",            "Aurora",          0.0f, 0.0f, 2.0f, 0.0f } },
            { "thrash",    { "Grand Piano", "Felt Keys",       0.0f, 0.0f, 3.0f, 4.3f } },
            { "blast",     { "",            "Dark Matter",     0.0f, 0.0f, 2.0f, 0.0f } },
            { "metal",     { "Grand Piano", "Felt Keys",       0.0f, 0.0f, 3.0f, 4.3f } },
            { "trap",      { "Grand Piano", "Felt Keys",       0.0f, 0.0f, 2.0f, 4.3f } },
            { "drill",     { "Grand Piano", "Felt Keys",       0.0f, 0.0f, 2.0f, 4.3f } },
            { "boombap",   { "",            "Dusty Rhodes",    8.0f, 8.0f, 7.0f, 0.5f } },
            { "hiphop",    { "",            "Velvet Keys",     6.0f, 4.0f, 5.0f, 0.5f } },
            { "rock",      { "Grand Piano", "Velvet Keys",     0.0f, 0.0f, 4.0f, 0.0f } },
            { "hardrock",  { "",            "Drawbar Organ",   0.0f, 0.0f, 4.0f, -8.7f } },
            { "poppunk",   { "Grand Piano", "Velvet Keys",     0.0f, -1.0f, 3.0f, 2.4f } },
            { "punk",      { "Grand Piano", "Velvet Keys",     0.0f, -2.0f, 3.0f, 2.4f } },
            { "lofi",      { "",            "Felt Keys",       28.0f, 10.0f, 9.0f, 0.8f } },
            { "jazz",      { "Grand Piano", "Velvet Keys",     6.0f, 0.0f, 6.0f, -1.2f } },
            { "ballad",    { "Grand Piano", "Velvet Keys",     40.0f, 4.0f, 8.0f, -1.2f } },
            { "neosoul",   { "",            "Dusty Rhodes",    14.0f, 9.0f, 10.0f, 0.0f } },
            { "funk",      { "",            "Clav Funk",       0.0f, 0.0f, 2.5f, 1.9f } },
            // (Measured 2026-11-15 like the rest: the keys about 4 dB under the drums, reggae's organ 5, cinematic's
            // piano 3; bossa's and country's brushes are too quiet to measure against, so there the keys sit 3 and
            // 2 dB under the guitar, and ambient's piano is the loudest thing in it: as high as the fader goes)
            { "house",     { "Grand Piano", "Velvet Keys",     0.0f, 0.0f, 2.0f, 3.6f } },
            { "techno",    { "",            "Felt Keys",       0.0f, 0.0f, 2.0f, 0.0f } },
            { "edm",       { "Grand Piano", "Velvet Keys",     0.0f, 0.0f, 2.0f, 3.9f } },
            { "dnb",       { "",            "Dusty Rhodes",    6.0f, 0.0f, 3.0f, -0.6f } },
            { "dubstep",   { "",            "Felt Keys",       0.0f, 0.0f, 2.0f, 0.0f } },
            { "synthwave", { "",            "Velvet Keys",     4.0f, 0.0f, 2.0f, -0.2f } },
            { "reggae",    { "",            "Drawbar Organ",   0.0f, 6.0f, 4.0f, -6.3f } },
            { "reggaeton", { "Grand Piano", "Velvet Keys",     0.0f, 0.0f, 2.0f, 3.4f } },
            { "afrobeats", { "",            "Dusty Rhodes",    0.0f, 4.0f, 4.0f, -6.2f } },
            { "bossa",     { "",            "Dusty Rhodes",    8.0f, 2.0f, 6.0f, -9.4f } },
            { "ambient",   { "Grand Piano", "Felt Keys",       20.0f, 0.0f, 8.0f, 4.3f } },
            { "cinematic", { "Grand Piano", "Felt Keys",       0.0f, 0.0f, 3.0f, 4.2f } },
            { "country",   { "Upright Piano", "Velvet Keys",   0.0f, 0.0f, 4.0f, -3.8f } },
        };

        if (auto it = table.find (style); it != table.end())
            return it->second;

        return table.at ("rock");
    }

    //==============================================================================
    struct BarIn
    {
        int steps = 16, index = 0;
        std::vector<int> beats;
        int beat = 4, sixteenth = 1;
        std::vector<int> kicks, snares;
        Chord chord, next;
        bool phraseEnd = false, sectionEnd = false, songEnd = false;
    };

    struct Ctx
    {
        const Request& req;
        const agents::Style& style;
        juce::Random& rng;
        theory::Key key;
        juce::String kind;
        bool soft = false;                  // asked for this section ("soft verses")
        bool sampled = false;               // on a sampled piano (quieter for a velocity than a Nova keys sound)
        float energy = 0.5f, busy = 0.5f;
        juce::String note;
        std::vector<int> right;             // the last right-hand voicing, to lead the next from

        int pick (int n)                    { return rng.nextInt (juce::jmax (1, n)); }
        juce::String id() const             { return style.id; }
        juce::String family() const         { return style.family; }
    };

    struct Plan
    {
        int mode = 0, contour = 0;
        float passing = 0.4f, graces = 0.3f;
        bool anticipate = false;
        std::vector<int> stabs;             // in 16ths
        std::vector<int> motif;             // trap: the melody's 8th positions
        std::vector<int> degrees;           // and its steps through the chord's tones
        juce::int64 seed = 1;
    };

    using Notes = std::vector<Note>;

    void add (Notes& ns, int step, int length, int pitch, int velocity, bool left)
    {
        Note n;
        n.step = step;
        n.length = std::max (1, length);
        n.pitch = juce::jlimit (21, 108, pitch);
        n.velocity = juce::jlimit (1, 127, velocity);
        n.left = left;
        ns.push_back (n);
    }

    void chord (Notes& ns, int step, int length, const std::vector<int>& pitches, int velocity, bool left)
    {
        for (auto p : pitches)
            add (ns, step, length, p, velocity, left);
    }

    bool onKick (const BarIn& b, int step)   { return std::find (b.kicks.begin(), b.kicks.end(), step) != b.kicks.end(); }

    // The chord's tones as intervals: a triad, with the seventh where it has one
    std::vector<int> tonesOf (const Chord& c, bool sevenths)
    {
        const int third = c.third() >= 0 ? c.third() : (c.has (7) && c.tones.size() == 2 ? -1 : 4);
        std::vector<int> v { 0 };

        if (third >= 0)
            v.push_back (third);

        v.push_back (c.has (6) ? 6 : 7);

        if ((sevenths || c.rich) && c.seventh() >= 0)
            v.push_back (c.seventh());

        if (c.rich && c.has (14))
            v.push_back (14);

        return v;
    }

    // Rootless: the third, fifth, seventh and ninth (jazz's A and B voicings are its inversions)
    std::vector<int> rootlessTones (const Chord& c)
    {
        const int third = c.third() >= 0 ? c.third() : 4;
        const int seventh = c.seventh() >= 0 ? c.seventh() : (c.minor() ? 10 : 11);
        return { third, 7, seventh, 14 };
    }

    std::vector<int> rightHand (Ctx& c, const std::vector<int>& intervals, int rootPc, int centre, int lo, int hi)
    {
        auto v = led (intervals, rootPc, c.right, centre, lo, hi);

        // Nothing fits the range: closed, from the root just under the centre
        if (v.empty())
            for (auto i : intervals)
                v.push_back (atOrAbove (rootPc, centre - 6) + i);

        c.right = v;
        return v;
    }

    int rootLow (const Chord& ch, int from = 36)    { return atOrAbove (ch.root, from); }

    //==============================================================================
    //  Metal: the piano in the intro, big chords under the chorus, out of the way of the chugs
    //==============================================================================
    Notes metalBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        const auto id = c.id();
        const int s16 = std::max (1, b.sixteenth);
        const int eighth = 2 * s16;
        const bool pad = (id == "deathcore" || id == "blast" || id == "djent") && ! c.req.piano;
        const int root = rootLow (b.chord);

        auto arpeggio = [&]
        {
            const auto v = rightHand (c, tonesOf (b.chord, false), b.chord.root, 70, 60, 86);
            std::vector<int> line { v[0], v.size() > 1 ? v[1] : v[0], v.back(), v[0] + 12 };

            for (int t = 0, i = 0; t < b.steps; t += eighth, ++i)
                add (ns, t, eighth * 2, line[(size_t) i % line.size()], 66 + (t % b.beat == 0 ? 8 : 0), false);

            add (ns, 0, b.steps, root, 72, true);
            add (ns, 0, b.steps, root + 12, 66, true);
        };

        if (c.kind == "breakdown")
        {
            if (p.mode == 1 && b.index % 4 == 0)
            {
                const int top = atOrAbove (b.chord.root, 72);
                add (ns, 0, b.steps * 2, top + 1, 60, false);
                add (ns, 0, b.steps * 2, top + 12, 56, false);
                c.note = "an eerie high b2 at the top of each phrase, the rest left to the guitars";
            }
            else
                c.note = "laying out for the breakdown";

            return ns;
        }

        if (pad)
        {
            chord (ns, 0, b.steps, rightHand (c, tonesOf (b.chord, true), b.chord.root, 60, 50, 74), 70, false);
            add (ns, 0, b.steps, root, 64, true);
            c.note = "a dark pad held under it, a chord a bar";
            return ns;
        }

        if (c.kind == "intro" || c.kind == "bridge" || c.kind == "outro")
        {
            arpeggio();
            c.note = "a piano arpeggio, the low octave ringing under it";
        }
        else if (c.kind == "chorus" || c.kind == "hook")
        {
            chord (ns, 0, b.steps, rightHand (c, tonesOf (b.chord, false), b.chord.root, 70, 60, 82), 90, false);
            add (ns, 0, b.steps, root, 84, true);
            add (ns, 0, b.steps, root + 12, 80, true);
            c.note = "big chords held under the chorus, octaves in the left hand";
        }
        else if (p.mode == 1)
        {
            const auto v = rightHand (c, tonesOf (b.chord, false), b.chord.root, 82, 76, 96);

            for (size_t i = 0; i < b.beats.size(); i += 2)
                add (ns, b.beats[i], b.beat * 2, v[(i / 2) % v.size()], 64, false);

            c.note = "high single notes over the verse, a bell in the dark";
        }
        else
        {
            c.note = "laying out under the verse";
        }

        return ns;
    }

    //==============================================================================
    //  Trap and drill: a dark piano motif; boom bap and hip-hop: dusty chords between the kicks
    //==============================================================================
    Notes urbanBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        const auto id = c.id();
        const int s16 = b.sixteenth > 0 ? b.sixteenth : std::max (1, b.steps / 16);

        if (id == "trap" || id == "drill")
        {
            // The motif: its rhythm and its steps through the chord's tones, the same each bar,
            // following the chords; up an octave to end a phrase
            const auto tones = tonesOf (b.chord, true);
            const int base = atOrAbove (b.chord.root, 60);
            const bool triplets = b.sixteenth == 0 || b.steps % 3 == 0;
            const int unit = triplets ? std::max (1, b.steps / 12) : s16 * 2;

            for (size_t i = 0; i < p.motif.size(); ++i)
            {
                const int t = p.motif[i] * unit;

                if (t >= b.steps)
                    continue;

                const int degree = p.degrees[i % p.degrees.size()];
                int pitch = base + tones[(size_t) (degree % (int) tones.size())] + 12 * (degree / (int) tones.size());

                if (id == "drill" && degree == 1)
                    pitch = base + 1;           // the b2, drill's colour

                if (b.phraseEnd && i + 1 == p.motif.size())
                    pitch += 12;

                const int next = i + 1 < p.motif.size() ? p.motif[i + 1] * unit : b.steps;
                add (ns, t, std::max (unit, next - t), pitch, 72 + (i == 0 ? 8 : 0), false);
            }

            if (c.kind != "intro")
            {
                const int root = rootLow (b.chord, 40);
                add (ns, 0, b.steps, root, 66, true);
                add (ns, 0, b.steps, root + 7, 60, true);
            }

            c.note = id == "drill" ? "a dark piano line through the chord, the b2 in it, held fifths under it"
                                   : "a dark piano motif following the chords, held fifths in the left hand";
            return ns;
        }

        const int root = rootLow (b.chord);
        add (ns, 0, b.beat * 2, root, 74, true);

        for (size_t i = 0; i < p.stabs.size() && i < 3; ++i)
            if (const int t = p.stabs[i] * s16; t < b.steps)
                chord (ns, t, 3 * s16, rightHand (c, rootlessTones (b.chord), b.chord.root, 62, 52, 74), i == 0 ? 82 : 74, false);

        c.note = "dusty chords between the kicks, the root under them";
        return ns;
    }

    //==============================================================================
    //  Rock, hard rock, pop-punk, punk
    //==============================================================================
    Notes rockBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        const auto id = c.id();
        const int s16 = std::max (1, b.sixteenth);
        const int eighth = 2 * s16;
        const int root = rootLow (b.chord);
        const auto v = rightHand (c, tonesOf (b.chord, false), b.chord.root, 66, 57, 77);

        if (id == "hardrock")
        {
            chord (ns, 0, b.steps, v, 84, false);
            add (ns, 0, b.steps, root, 80, true);
            add (ns, 0, b.steps, root + 7, 74, true);
            c.note = "the organ held under it, a chord a bar";
            return ns;
        }

        int mode = c.kind == "chorus" || c.kind == "hook" ? 0 : c.kind == "bridge" ? 2 : p.mode;

        if (id == "poppunk" || id == "punk")
            mode = c.kind == "bridge" ? 2 : 0;

        if (mode == 0)
        {
            for (int t = 0; t < b.steps; t += eighth)
                chord (ns, t, eighth, v, t % b.beat == 0 ? 92 : 80, false);

            for (size_t i = 0; i < b.beats.size(); i += 2)
            {
                add (ns, b.beats[i], b.beat * 2, root, 88, true);
                add (ns, b.beats[i], b.beat * 2, root + 12, 82, true);
            }

            c.note = "pumping 8th-note chords, octaves on 1 and 3";
        }
        else if (mode == 1)
        {
            for (auto beat : b.beats)
                chord (ns, beat, b.beat, v, beat == 0 ? 88 : 78, false);

            add (ns, 0, b.steps, root, 80, true);
            c.note = "chords on the beat over a held root";
        }
        else
        {
            chord (ns, 0, b.steps / 2, v, 82, false);
            chord (ns, b.steps / 2, b.steps / 2, v, 76, false);
            add (ns, 0, b.steps, root, 78, true);
            add (ns, 0, b.steps, root + 12, 72, true);
            c.note = "half-note chords, letting the bridge breathe";
        }

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
            // Rootless, led from the last; the bassist has the roots
            const auto v = rightHand (c, rootlessTones (b.chord), b.chord.root, 60, 50, 72);
            const int beats = (int) b.beats.size();
            const int third = b.beat / 3 > 0 ? b.beat / 3 : 1;

            if (p.mode == 0 || c.kind == "intro")
            {
                chord (ns, 0, b.beat, v, 78, true);

                if (beats >= 2)
                    chord (ns, b.beats[1] + 2 * third, b.beat, v, 72, true);

                c.note = "comping the Charleston in rootless voicings, the bassist under them";
            }
            else if (p.mode == 1)
            {
                if (beats >= 2)
                    chord (ns, b.beats[1], b.beat - 1, v, 74, true);

                const auto next = led (rootlessTones (b.next), b.next.root, v, 60, 50, 72);
                chord (ns, b.steps - third, 2 * third, next, 80, true);
                c.note = "comping on 2 and pushing the next chord ahead of the one, rootless";
            }
            else
            {
                if (beats >= 2)
                    chord (ns, b.beats[1], b.beat / 2 + 1, v, 72, true);

                if (beats >= 3)
                    chord (ns, b.beats[2] + 2 * third, third + 1, v, 76, true);

                c.note = "sparse comping in rootless voicings, on 2 and before 4";
            }

            return ns;
        }

        const int s16 = std::max (1, b.sixteenth);
        const int root = rootLow (b.chord);

        if (id == "ballad")
        {
            const int seventh = b.chord.seventh() >= 0 ? b.chord.seventh() : 10;
            add (ns, 0, b.steps, root, 66, true);
            add (ns, 0, b.steps, root + seventh, 60, true);
            const auto v = rightHand (c, rootlessTones (b.chord), b.chord.root, 66, 56, 80);
            chord (ns, 0, b.steps, v, 62, false);

            if (b.beats.size() >= 3)
                add (ns, b.beats[2], b.steps - b.beats[2], v.back() + (r.nextBool() ? 2 : 0), 66, false);

            c.note = "the chord rolled up slowly, the root and seventh low, an answer on 3";
            return ns;
        }

        const bool soul = id == "neosoul";
        const auto v = rightHand (c, rootlessTones (b.chord), b.chord.root, soul ? 64 : 62, 52, 77);
        const bool anticipated = soul && p.anticipate && b.index > 0;

        add (ns, 0, soul ? 6 * s16 : b.steps, root, 70, true);

        if (! anticipated)
        {
            // A grace note a half step under the top, slid off into it
            if (soul && r.nextFloat() < p.graces)
            {
                add (ns, 0, 1, v.back() - 1, 56, false);
                ns.back().grace = true;
                chord (ns, std::min (1, b.steps - 1), soul ? 5 * s16 : b.steps, v, 74, false);
            }
            else
                chord (ns, 0, soul ? 5 * s16 : b.steps, v, soul ? 76 : 66, false);
        }

        if (soul)
        {
            const int restrike = (p.mode == 0 ? 6 : 10) * s16;

            if (restrike < b.steps)
                chord (ns, restrike, 2 * s16, v, 68, false);

            if (p.anticipate && ! b.sectionEnd && b.steps >= 16)
                chord (ns, b.steps - s16, 3 * s16, led (rootlessTones (b.next), b.next.root, v, 64, 52, 77), 76, false);

            c.note = juce::String ("Rhodes ninths") + (p.anticipate ? " pushed ahead of the one" : "") + ", restruck on the syncopation, grace notes sliding into the top";
            return ns;
        }

        // Lofi: a few notes answering late in the bar
        for (int t : { 10, 12, 14 })
            if (const int at = t * s16; at < b.steps && r.nextFloat() < p.passing)
                add (ns, at, 2 * s16, v[(size_t) (t / 2) % v.size()] + 12, 58, false);

        c.note = "soft felt chords rolled up, the root held, a few notes answering late in the bar";
        return ns;
    }

    //==============================================================================
    //  Funk: the clav
    //==============================================================================
    Notes funkBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        const int s16 = std::max (1, b.sixteenth);
        const int root = rootLow (b.chord, 40);
        const int third = b.chord.third() >= 0 ? b.chord.third() : 4;
        const int top = atOrAbove ((b.chord.root + third) % 12, 62);
        const std::vector<int> stab { top, top + (b.chord.seventh() >= 0 ? b.chord.seventh() : 10) - third };
        const bool fourOnTheFloor = b.beats.size() >= 4 && std::all_of (b.beats.begin(), b.beats.end(), [&] (int beat) { return onKick (b, beat); });

        if ((c.kind == "chorus" || c.kind == "drop") && fourOnTheFloor)
        {
            for (int t = 2 * s16; t < b.steps; t += 4 * s16)
                chord (ns, t, s16, stab, 100, false);

            for (auto beat : b.beats)
                add (ns, beat, s16, root, 92, true);

            c.note = "clav chords on the off-beats, the root on the beat";
            return ns;
        }

        // The right hand stabs on the syncopations, the left fills the 16ths it leaves
        for (auto at : p.stabs)
            if (const int t = at * s16; t < b.steps)
                chord (ns, t, s16, stab, t == 0 ? 110 : 100, false);

        static const int riff[] { 0, 12, 10, 7, 0, 12, 7, 10 };

        for (int t = 0, i = 0; t < b.steps; t += 2 * s16, ++i)
            if (std::find (p.stabs.begin(), p.stabs.end(), t / s16) == p.stabs.end())
                add (ns, t, s16, root + riff[i % 8], t == 0 ? 104 : 88, true);

        c.note = "the clav: stabs on the syncopations, the left hand filling the 16ths between";
        return ns;
    }

    int unitOf (const BarIn& b)     { return std::max (1, b.sixteenth > 0 ? b.sixteenth : b.steps / 16); }

    // A section that's meant to hit hardest: a chorus, a hook, a drop, or one asked to be heavy
    bool bigPart (const Ctx& c)     { return c.kind == "chorus" || c.kind == "hook" || c.kind == "drop" || c.req.heavyIn.contains (c.kind); }

    //==============================================================================
    //  Electronic: house's piano stabs, EDM's chords, drum and bass's Rhodes, synthwave's held chords;
    //  techno and dubstep leave it to the synths
    //==============================================================================
    Notes electronicBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        const auto id = c.id();
        const int u = unitOf (b);
        const int root = rootLow (b.chord, 40);

        if (id == "techno" || id == "dubstep")
        {
            c.note = "out: the synths have it";
            return ns;
        }

        if (id == "house")
        {
            if (c.kind == "intro")
            {
                c.note = "laying out for the intro, the drums alone";
                return ns;
            }

            const auto v = rightHand (c, tonesOf (b.chord, true), b.chord.root, 67, 60, 79);

            if (c.kind == "breakdown")
            {
                chord (ns, 0, b.steps, v, 72, false);
                add (ns, 0, b.steps, root + 12, 66, true);
                c.note = "the chords held through the breakdown, a bar each";
                return ns;
            }

            // One of the house piano's rhythms, the same every bar, short and bright: 3+3+4+2+4, the
            // offbeats, or a push across the bar
            static const std::vector<std::vector<int>> rhythms { { 0, 3, 6, 10, 12 }, { 2, 6, 10, 14 }, { 0, 3, 7, 10, 14 }, { 3, 6, 11, 14 } };
            const auto& at = rhythms[(size_t) p.contour % rhythms.size()];
            const bool full = bigPart (c);

            for (size_t i = 0; i < at.size(); ++i)
                if (const int t = at[i] * u; t < b.steps)
                    chord (ns, t, 2 * u, v, (full ? 98 : 84) + (i == 0 ? 6 : 0), false);

            c.note = full ? "piano stabs, all in" : "piano stabs, a little back";
            return ns;
        }

        if (id == "edm")
        {
            const auto v = rightHand (c, tonesOf (b.chord, false), b.chord.root, 67, 60, 79);

            if (c.kind == "prechorus")
            {
                // The build: the chord on each beat, louder bar by bar (the snare roll is the build's rush; the
                // drop is where it all lands)
                const int rise = juce::jlimit (0, 28, b.index * 4);

                for (auto beat : b.beats)
                    chord (ns, beat, b.beat, v, 62 + rise + (beat == 0 ? 6 : 0), false);

                add (ns, 0, b.steps, root + 12, 62 + rise, true);
                c.note = "the chord on each beat, building bar by bar";
                return ns;
            }

            if (c.kind == "drop")
            {
                for (auto beat : b.beats)
                    if (beat + 2 * u < b.steps)
                        chord (ns, beat + 2 * u, 2 * u, v, 96, false);

                c.note = "chords on the offbeats, pumping against the kick";
                return ns;
            }

            if (c.kind == "breakdown")
            {
                chord (ns, 0, b.steps, v, 76, false);
                add (ns, 0, b.steps, root + 12, 70, true);
                c.note = "the piano alone with the chords, held a bar each";
                return ns;
            }

            for (auto beat : b.beats)
                chord (ns, beat, b.beat, v, beat == 0 ? 78 : 70, false);

            add (ns, 0, b.steps, root + 12, 72, true);
            c.note = "the chords on the beat, softly";
            return ns;
        }

        if (id == "dnb")
        {
            // Liquid drum and bass: the Rhodes holding rich chords over the breaks (the third, fifth,
            // seventh and ninth), restruck on the and of 2 in the drop
            const auto v = rightHand (c, rootlessTones (b.chord), b.chord.root, 64, 54, 76);
            const int restrike = c.kind == "drop" && b.beats.size() >= 2 ? b.beats[1] + 2 * u : b.steps;

            chord (ns, 0, restrike, v, c.kind == "intro" ? 66 : 74, false);

            if (restrike < b.steps)
                chord (ns, restrike, b.steps - restrike, v, 70, false);

            add (ns, 0, b.steps, rootLow (b.chord, 48), 64, true);
            c.note = restrike < b.steps ? "the Rhodes holding ninths, restruck on the and of 2" : "the Rhodes holding ninths over the break";
            return ns;
        }

        // Synthwave: an 80s electric piano holding the chords, a bar each, struck again on 3 in the chorus
        const auto v = rightHand (c, tonesOf (b.chord, true), b.chord.root, 66, 58, 78);
        const int half = bigPart (c) && b.beats.size() >= 4 ? b.beats[2] : b.steps;

        chord (ns, 0, half, v, c.kind == "intro" ? 66 : 76, false);

        if (half < b.steps)
            chord (ns, half, b.steps - half, v, 72, false);

        add (ns, 0, b.steps, root + 12, 70, true);
        c.note = half < b.steps ? "the chords held, struck again on 3" : "the chords held, a bar each";
        return ns;
    }

    //==============================================================================
    //  Island: reggae's organ bubble, reggaeton's chords on the dembow, afrobeats' syncopated stabs
    //==============================================================================
    Notes islandBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        const auto id = c.id();
        const int u = unitOf (b);
        const bool hook = bigPart (c);

        if (id == "reggae")
        {
            // The organ's bubble: a short chord on each offbeat and a lighter one the sixteenth after,
            // skipping along under the guitar's skank
            const auto v = rightHand (c, tonesOf (b.chord, false), b.chord.root, 62, 55, 72);

            for (auto beat : b.beats)
            {
                if (beat + 2 * u < b.steps)
                    chord (ns, beat + 2 * u, u, v, hook ? 84 : 78, false);

                if (beat + 3 * u < b.steps)
                    chord (ns, beat + 3 * u, u, v, hook ? 68 : 62, false);
            }

            c.note = "the organ bubble: a short chord on each offbeat, a lighter one just after";
            return ns;
        }

        if (id == "reggaeton")
        {
            const auto v = rightHand (c, tonesOf (b.chord, false), b.chord.root, 64, 56, 76);

            if (! hook)
            {
                // The verse: the chord on 1 and 3, out of the dembow's way
                for (size_t i = 0; i < b.beats.size(); i += 2)
                {
                    const int until = i + 2 < b.beats.size() ? b.beats[i + 2] : b.steps;
                    chord (ns, b.beats[i], until - b.beats[i], v, i == 0 ? 76 : 70, false);
                }

                c.note = "the chord on 1 and 3, out of the dembow's way";
                return ns;
            }

            // The hook: the chord struck on the dembow's 3+3+2, twice a bar
            static const int at[] { 0, 3, 6, 8, 11, 14 };

            for (int i = 0; i < 6 && at[i] * u < b.steps; ++i)
                chord (ns, at[i] * u, (i % 3 == 2 ? 2 : 3) * u, v, i % 3 == 0 ? 94 : 84, false);

            c.note = "the chord on the dembow's 3+3+2";
            return ns;
        }

        // Afrobeats: short chords on a two-bar figure (3+3+4+2+4, then the offbeats)
        const auto v = rightHand (c, tonesOf (b.chord, true), b.chord.root, 66, 58, 78);
        static const std::vector<int> first { 0, 3, 6, 10, 12 }, second { 2, 6, 10, 13 };
        const auto& at = (b.index + p.mode) % 2 == 0 ? first : second;

        for (size_t i = 0; i < at.size(); ++i)
            if (const int t = at[i] * u; t < b.steps)
                chord (ns, t, 2 * u, v, (hook ? 88 : 78) + (i == 0 ? 6 : 0), false);

        c.note = "short chords on a two-bar syncopation";
        return ns;
    }

    //==============================================================================
    //  Bossa nova: Rhodes shells (the third, the seventh and the ninth) on a two-bar figure, light,
    //  between the guitar's off-beats; the bassist has the roots
    //==============================================================================
    Notes latinBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        const int u = unitOf (b);
        const auto tones = rootlessTones (b.chord);
        const auto v = rightHand (c, { tones[0], tones[2], tones[3] }, b.chord.root, 62, 54, 72);

        struct Hit { int at, length, velocity; };
        static const std::vector<Hit> first { { 0, 6, 72 }, { 10, 6, 66 } }, second { { 3, 3, 68 }, { 6, 4, 64 }, { 12, 4, 66 } };

        for (auto& h : (b.index + p.mode) % 2 == 0 ? first : second)
            if (h.at * u < b.steps)
                chord (ns, h.at * u, std::min (h.length * u, b.steps - h.at * u), v, h.velocity, false);

        c.note = "Rhodes shells (the third, the seventh and the ninth) on a two-bar figure";
        return ns;
    }

    //==============================================================================
    //  Ambient: a sparse piano, a note or two a bar from the chord, high and soft, the root low under
    //  every other bar. Cinematic: the same, until the big parts' ostinato (root, fifth, octave, fifth
    //  in eighths, low) with the chord held high over it; the prechorus builds into it
    //==============================================================================
    Notes ambientBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        juce::Random r (p.seed + b.index);
        const int u = unitOf (b);

        if (c.id() == "cinematic" && (bigPart (c) || c.kind == "prechorus"))
        {
            const int root = rootLow (b.chord, 38);
            const int fifth = b.chord.has (6) ? 6 : 7;
            const std::vector<int> line { root, root + fifth, root + 12, root + fifth };
            const bool building = c.kind == "prechorus";

            for (int t = 0, i = 0; t < b.steps; t += 2 * u, ++i)
                add (ns, t, 2 * u, line[(size_t) i % line.size()], (building ? 62 + juce::jmin (24, b.index * 3) : 88) + (t % b.beat == 0 ? 8 : 0), true);

            if (! building)
                chord (ns, 0, b.steps, rightHand (c, tonesOf (b.chord, false), b.chord.root, 74, 66, 86), 82, false);

            c.note = building ? "an ostinato in eighths, low, building" : "the ostinato in eighths, low, the chord held high over it";
            return ns;
        }

        const auto tones = tonesOf (b.chord, true);
        const int base = atOrAbove (b.chord.root, 64);
        const int first = base + tones[(size_t) r.nextInt ((int) tones.size())];
        const int second = b.beats.size() >= 3 ? b.beats[2] + (r.nextBool() ? 0 : 2 * u) : b.steps;

        add (ns, 0, second, first, 60, false);

        if (second < b.steps && r.nextFloat() < 0.65f)
            add (ns, second, b.steps - second, base + tones[(size_t) r.nextInt ((int) tones.size())] + (r.nextFloat() < 0.3f ? 12 : 0), 54, false);

        if (b.index % 2 == 0)
            add (ns, 0, b.steps, rootLow (b.chord, 36), 56, true);

        c.note = "a sparse piano, a note or two a bar, the root low under it";
        return ns;
    }

    //==============================================================================
    //  Country: honky-tonk, the right hand on 2 and 4 with a slide into the third, a run down to the
    //  root at a phrase's end; the left hand on the root and the fifth under the bassist
    //==============================================================================
    Notes countryBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        const int u = unitOf (b);
        const auto v = rightHand (c, tonesOf (b.chord, false), b.chord.root, 70, 62, 82);
        const int root = rootLow (b.chord, 36);
        const int fifth = b.chord.has (6) ? 6 : 7;
        const int third = b.chord.third() >= 0 ? b.chord.third() : 4;
        const bool run = b.phraseEnd && b.beats.size() >= 4;

        for (size_t i = 0; i < b.beats.size(); ++i)
        {
            const int at = b.beats[i], until = i + 1 < b.beats.size() ? b.beats[i + 1] : b.steps;

            if (i % 2 == 0)
                add (ns, at, until - at, i == 0 ? root : root + fifth - 12, i == 0 ? 80 : 72, true);
            else if (! (run && i == 3))
                chord (ns, at, std::max (u, (until - at) / 2), v, bigPart (c) ? 88 : 80, false);
        }

        // The slide: a grace note a half step under the top's third, slid off into it, on the one
        const int top = atOrAbove ((b.chord.root + third) % 12, 66);
        add (ns, 0, 1, top - 1, 62, false);
        ns.back().grace = true;
        add (ns, 1, std::max (1, b.beat - 1), top, 78, false);

        // A phrase's end: down the major (or minor) pentatonic to the root in eighths over beats 3 and 4
        if (run)
        {
            const std::vector<int> down = b.chord.minor() ? std::vector<int> { 10, 7, 5, 3 } : std::vector<int> { 9, 7, 4, 2 };
            const int from = atOrAbove (b.chord.root, 64);

            for (size_t i = 0; i < down.size(); ++i)
                if (const int t = b.beats[2] + (int) i * 2 * u; t < b.steps)
                    add (ns, t, 2 * u, from + down[i], i == 0 ? 82 : 74, false);
        }

        c.note = run ? "honky-tonk: chords on 2 and 4, a slide into the third, a run down at the phrase's end"
                     : "honky-tonk: chords on 2 and 4, a slide into the third";
        return ns;
    }

    //==============================================================================
    float score (const std::vector<Notes>& bars, const std::vector<BarIn>& ins, const Ctx& c)
    {
        float s = 0.0f;
        const auto f = c.family(), id = c.id();
        int lastTop = -1;

        for (size_t i = 0; i < bars.size(); ++i)
        {
            const auto& ns = bars[i];
            int top = -1, count = 0;

            for (auto& n : ns)
            {
                if (! n.left && ! n.grace && n.step == 0)
                    top = std::max (top, n.pitch);

                count += n.grace ? 0 : 1;

                if (n.pitch < 24 || n.pitch > 96)
                    s -= 2.0f;
            }

            // The top voice moves smoothly from bar to bar
            if (top >= 0 && lastTop >= 0)
                s -= std::max (0.0f, (float) std::abs (top - lastTop) - 5.0f) * 0.3f;

            if (top >= 0)
                lastTop = top;

            if (id == "trap" || id == "drill")
                s += count >= 4 ? 0.5f : -1.0f;
            else if (f == "funk" && c.kind != "chorus")
                s -= std::abs ((float) count - (8.0f + 4.0f * c.busy)) * 0.1f;

            // The left hand's root where the bar starts, wherever there is a left hand
            if (std::any_of (ns.begin(), ns.end(), [] (const Note& n) { return n.left; }) && id != "jazz")
                s += std::any_of (ns.begin(), ns.end(), [&] (const Note& n) { return n.left && n.step == 0 && n.pitch % 12 == ins[i].chord.root; }) ? 0.5f : -0.5f;
        }

        return s / (float) std::max<size_t> (1, bars.size());
    }

    Plan makePlan (Ctx& c)
    {
        Plan p;
        p.seed = c.rng.nextInt64();
        p.mode = c.pick (3);
        p.contour = c.pick (4);
        p.passing = 0.2f + 0.6f * c.rng.nextFloat() * (0.5f + c.busy);
        p.graces = 0.3f + 0.5f * c.rng.nextFloat();
        p.anticipate = c.rng.nextFloat() < 0.6f;

        if (c.family() == "funk")
        {
            static const int syncopations[] { 0, 3, 6, 7, 10, 13, 14, 15 };
            p.stabs.push_back (0);

            for (int i = 0, extra = 2 + c.pick (3); i < extra; ++i)
                if (const int at = syncopations[1 + c.pick (7)]; std::find (p.stabs.begin(), p.stabs.end(), at) == p.stabs.end())
                    p.stabs.push_back (at);
        }
        else
        {
            static const int spaces[] { 2, 6, 10, 14, 7, 11 };

            for (int i = 0, count = 2 + c.pick (2); i < count; ++i)
                if (const int at = spaces[c.pick (6)]; std::find (p.stabs.begin(), p.stabs.end(), at) == p.stabs.end())
                    p.stabs.push_back (at);
        }

        std::sort (p.stabs.begin(), p.stabs.end());

        // Trap's motif: 4 to 6 of the bar's 8 (or 12) places, the first on the one, and a shape
        // through the chord's tones (up, down, an arch, or circling)
        p.motif = { 0 };

        for (int count = 4 + c.pick (3); (int) p.motif.size() < count;)
            if (const int at = 1 + c.pick (7); std::find (p.motif.begin(), p.motif.end(), at) == p.motif.end())
                p.motif.push_back (at);

        std::sort (p.motif.begin(), p.motif.end());

        static const std::vector<std::vector<int>> shapes { { 0, 1, 2, 3, 4, 2 }, { 4, 3, 2, 1, 0, 2 }, { 0, 2, 4, 3, 1, 0 }, { 2, 1, 2, 0, 2, 3 } };
        p.degrees = shapes[(size_t) p.contour];
        return p;
    }

    // A soft section in a loud style: the piano steps forward, an arpeggio up through each chord with
    // the low root ringing under it (Sleep Token's verses, a post-rock opening, grunge's quiet half)
    Notes softBar (Ctx& c, const BarIn& b)
    {
        Notes ns;
        const int eighth = 2 * std::max (1, b.sixteenth);
        const auto v = rightHand (c, tonesOf (b.chord, false), b.chord.root, 70, 60, 86);
        std::vector<int> line { v[0], v.size() > 1 ? v[1] : v[0], v.back(), v[0] + 12 };

        // A sampled piano needs 84-92 to be heard over even the soft drums; a Nova keys sound is that loud
        // at 62-70 (measured: on Velvet Keys the quiet verse came within 3 dB of the chorus's keys)
        const int velocity = c.sampled ? 84 : 62;

        for (int t = 0, i = 0; t < b.steps; t += eighth, ++i)
            add (ns, t, eighth * 2, line[(size_t) i % line.size()], velocity + (t % b.beat == 0 ? 8 : 0), false);

        add (ns, 0, b.steps, rootLow (b.chord), velocity, true);
        c.note = "forward in the quiet part: an arpeggio up through each chord, the root ringing under it";
        return ns;
    }

    Notes barFor (Ctx& c, const Plan& p, const BarIn& b)
    {
        const auto f = c.family();

        if (c.soft && (f == "metal" || f == "rock" || f == "funk"))
            return softBar (c, b);

        auto ns = f == "metal"      ? metalBar (c, p, b)
                : f == "rock"       ? (c.id() == "country" ? countryBar (c, p, b) : rockBar (c, p, b))
                : f == "trap"       ? urbanBar (c, p, b)
                : f == "funk"       ? funkBar (c, p, b)
                : f == "electronic" ? electronicBar (c, p, b)
                : f == "island"     ? islandBar (c, p, b)
                : f == "latin"      ? latinBar (c, p, b)
                : f == "ambient"    ? ambientBar (c, p, b)
                                    : jazzBar (c, p, b);

        // Asked to be soft in any other style: the same part, played lighter
        if (c.soft)
            for (auto& n : ns)
                n.velocity = std::max (1, n.velocity * 3 / 4);

        return ns;
    }
}

//==============================================================================
Sound soundFor (const juce::String& style)
{
    const auto& d = defaultsFor (style);

    if (juce::String (d.sampled).isNotEmpty())
        for (auto& e : instruments::list())
            if (e.name == juce::String (d.sampled))
                return { e.name, e.path };

    return { d.nova, {} };
}

float levelFor (const juce::String& style)
{
    return defaultsFor (style).level;
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
    const auto key = theory::keyFrom (bassLine.key);
    const int beats = g.meterBeats > 0 ? g.meterBeats : song.meterBeats;
    const int unit = g.meterUnit > 0 ? g.meterUnit : song.meterUnit;
    const int sixteenthsInBar = juce::jmax (1, beats * 16 / unit);

    Ctx c { req, *style, rng, key };
    c.busy = req.busy;

    // The sound it'll be on (when it chooses), for how hard a soft part is played
    c.sampled = chooseSound && soundFor (req.piano ? juce::String ("rock") : juce::String (style->id)).sampled.isNotEmpty();

    std::vector<std::vector<Chord>> chords;

    for (auto& s : g.sections)
    {
        std::vector<Chord> list;
        const bass::Section* bs = nullptr;

        for (auto& b : bassLine.sections)
            if (b.name == s.name)
                bs = &b;

        for (size_t b = 0; b < s.bars.size(); ++b)
            list.push_back (bs != nullptr && ! bs->bars.empty() ? theory::parseChord (bs->bars[std::min (b, bs->bars.size() - 1)].chord)
                                                                : Chord { key.root, { 0, 3, 7 }, {} });

        // "Soul chords": a seventh on every chord (a minor one on minor chords and on the V, a major
        // one otherwise) and a ninth, voiced wherever the keys play it
        if (req.richChords)
            for (auto& ch : list)
            {
                if (ch.seventh() < 0 && ch.third() >= 0)
                    ch.tones.push_back (ch.minor() || (ch.root - key.root + 12) % 12 == 7 ? 10 : 11);

                if (! ch.has (14))
                    ch.tones.push_back (14);

                ch.rich = true;
            }

        chords.push_back (std::move (list));
    }

    juce::StringArray notes;

    for (size_t si = 0; si < g.sections.size(); ++si)
    {
        const auto& ds = g.sections[si];
        c.kind = theory::kindOf (ds.name);
        c.soft = req.softIn.contains (c.kind);
        const bool lastSection = si + 1 == g.sections.size();
        std::vector<BarIn> ins;

        for (size_t b = 0; b < ds.bars.size(); ++b)
        {
            BarIn in;
            in.steps = ds.bars[b].steps;
            in.index = (int) b;
            in.beats = theory::beatStarts (in.steps, beats, unit);
            in.beat = in.beats.size() > 1 ? in.beats[1] : in.steps;
            in.sixteenth = in.steps % sixteenthsInBar == 0 ? in.steps / sixteenthsInBar : 0;
            in.kicks = theory::hitsIn (ds.bars[b], { "kick" });
            in.snares = theory::hitsIn (ds.bars[b], { "snare" });
            in.chord = chords[si][b];
            in.next = b + 1 < ds.bars.size() ? chords[si][b + 1] : (! lastSection && ! chords[si + 1].empty() ? chords[si + 1].front() : chords[si].front());
            in.phraseEnd = b % 4 == 3;
            in.sectionEnd = b + 1 == ds.bars.size();
            in.songEnd = in.sectionEnd && lastSection;
            ins.push_back (in);
        }

        std::vector<Notes> best;
        juce::String bestNote;
        float bestScore = -1.0e9f;
        const auto startRight = c.right;
        std::vector<int> bestRight = c.right;

        for (int candidate = 0; candidate < 20; ++candidate)
        {
            const auto plan = makePlan (c);
            c.right = startRight;
            std::vector<Notes> bars;
            juce::String note;

            for (auto& in : ins)
            {
                c.note = {};
                bars.push_back (barFor (c, plan, in));

                if (note.isEmpty())
                    note = c.note;
            }

            const float sc = score (bars, ins, c) + rng.nextFloat() * 0.3f;

            if (sc > bestScore)
            {
                bestScore = sc;
                best = std::move (bars);
                bestNote = note;
                bestRight = c.right;
            }
        }

        c.right = bestRight;
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
    part.swing = g.swing;
    part.swingSixteenths = g.swingSixteenths;
    part.looseMs = defaults.loose;
    part.pushMs = defaults.push;
    part.rollMs = defaults.rollMs;

    if (req.tempo > 0)
        part.tempo = req.tempo;
    else if (result.imaginedDrums && g.tempo > 0)
        part.tempo = g.tempo;

    if (result.imaginedDrums && g.meterBeats > 0)
    {
        part.meterBeats = g.meterBeats;
        part.meterUnit = g.meterUnit;
    }

    // The piano styles played with some weight: a trap melody or a metal intro hit firmly (on a
    // sampled piano that's also its brighter layers), jazz a little less
    // (Whole ids: a substring test gave "pop" poppunk's weight)
    const auto id = juce::String (style->id);
    // (house's and EDM's stabs bright; ambient's few notes firm enough to carry over its drone)
    const int weight = juce::StringArray { "metalcore", "thrash", "metal", "trap", "drill" }.contains (id) ? 22
                     : juce::StringArray { "rock", "poppunk", "punk" }.contains (id) ? 16
                     : id == "ambient" ? 20 : id == "jazz" ? 14 : juce::StringArray { "house", "edm" }.contains (id) ? 12
                     : id == "ballad" ? 10 : 0;

    for (auto& s : part.sections)
        for (auto& b : s.bars)
            for (auto& n : b.notes)
                if (! n.grace)
                    n.velocity = juce::jmin (118, n.velocity + weight);

    if (chooseSound)
    {
        // "Piano" asked for (Sleep Token's): the grand piano, whatever the style's own keys
        const auto sound = soundFor (req.piano ? juce::String ("rock") : juce::String (style->id));
        part.sound = sound.name;
        part.sampled = sound.sampled;
        part.levelDb = levelFor (style->id);
    }

    const double tempo = part.tempo > 0 ? part.tempo : (g.tempo > 0 ? g.tempo : song.bpm);
    part.title = juce::String (style->name) + " keys, " + part.key;

    juce::String reply = juce::String (style->name) + " keys in " + part.key + " at " + juce::String (juce::roundToInt (tempo)) + " bpm"
                         + (part.sound.isNotEmpty() ? ", on " + part.sound : juce::String()) + ". ";
    reply << notes.joinIntoString (" ");

    if (result.imaginedDrums || result.imaginedBass)
        reply << " No " << (result.imaginedDrums && result.imaginedBass ? "drums or bass" : result.imaginedDrums ? "drums" : "bass")
              << " yet, so it played to what a " << juce::String (style->name).toLowerCase() << " band would: ask the AI "
              << (result.imaginedBass ? "bassist" : "drummer") << " and the keys follow theirs.";

    part.message = reply;
    result.reply = reply;
    result.request = req;
    return result;
}

KeysTurn turnFor (const juce::String& prompt, const Result& r)
{
    KeysTurn turn;
    turn.prompt = prompt.trim();
    turn.reply = r.reply;
    turn.part = juce::JSON::toString (r.part.toJson(), true);

    auto* answer = new juce::DynamicObject();
    answer->setProperty ("offline", true);
    answer->setProperty ("request", r.request.toVar());
    turn.answer = juce::JSON::toString (juce::var (answer), true);
    return turn;
}

std::optional<agents::Request> requestOf (const KeysTurn& turn)
{
    const auto v = juce::JSON::parse (turn.answer);

    if (! (bool) v.getProperty ("offline", false))
        return {};

    return Request::fromVar (v.getProperty ("request", {}));
}

} // namespace nightshift::keys::offline
