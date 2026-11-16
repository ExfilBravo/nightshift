// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "OfflineSynth.h"
#include "OfflineBass.h"
#include "OfflineDrummer.h"
#include "Theory.h"
#include "audio/Nova.h"

namespace nightshift::synth::offline
{

using agents::Request;
using theory::Chord;
using theory::atOrAbove;
using theory::led;

namespace
{
    //==============================================================================
    struct StyleSynth
    {
        const char* role;           // its own job
        const char* pad;            // and its sound for each job
        const char* arp;
        const char* lead;
        const char* stabs;
        float push, loose;          // ms behind the beat (ahead, below 0), and how loosely
    };

    const StyleSynth& defaultsFor (const juce::String& style)
    {
        static const std::map<juce::String, StyleSynth> table {
            { "metalcore", { "arp",   "Night Pad",       "Sequence Saw", "Supersaw Lead", "Supersaw Lead", 0.0f,  1.5f } },
            { "deathcore", { "pad",   "Glass Choir",     "Sequence Saw", "Screamer",      "90s Stab",      0.0f,  1.5f } },
            { "djent",     { "pad",   "Aurora",          "Arp Sparkle",  "Mono Lead",     "Future Pluck",  0.0f,  1.5f } },
            { "thrash",    { "pad",   "String Machine",  "Sequence Saw", "Screamer",      "Synth Brass",   0.0f,  1.5f } },
            { "blast",     { "pad",   "Glass Choir",     "Sequence Saw", "Screamer",      "Synth Brass",   0.0f,  1.5f } },
            { "metal",     { "pad",   "String Machine",  "Sequence Saw", "Screamer",      "Synth Brass",   0.0f,  1.5f } },
            { "trap",      { "lead",  "Dark Matter",     "Arp Sparkle",  "Crystal Bell",  "90s Stab",      0.0f,  1.5f } },
            { "drill",     { "pad",   "Glass Choir",     "Arp Sparkle",  "Music Box",     "90s Stab",      0.0f,  1.5f } },
            { "boombap",   { "stabs", "Warm Analog Pad", "Kalimba",      "Lofi Lead",     "Synth Brass",   4.0f,  5.0f } },
            { "hiphop",    { "lead",  "Warm Analog Pad", "Arp Sparkle",  "Future Pluck",  "90s Stab",      2.0f,  3.0f } },
            { "rock",      { "pad",   "String Machine",  "Sequence Saw", "Mono Lead",     "Synth Brass",   0.0f,  3.0f } },
            { "hardrock",  { "stabs", "String Machine",  "Sequence Saw", "Screamer",      "Synth Brass",   0.0f,  3.0f } },
            { "poppunk",   { "lead",  "String Machine",  "Sequence Saw", "Supersaw Lead", "Supersaw Lead", -1.0f, 2.5f } },
            { "punk",      { "lead",  "String Machine",  "Sequence Saw", "Supersaw Lead", "Supersaw Lead", -1.0f, 2.5f } },
            { "lofi",      { "lead",  "Warm Analog Pad", "Kalimba",      "Lofi Lead",     "Glass Pluck",   14.0f, 9.0f } },
            { "jazz",      { "lead",  "Breath Pad",      "Harp Pluck",   "Soft Flute",    "Synth Brass",   0.0f,  6.0f } },
            { "ballad",    { "pad",   "String Machine",  "Harp Pluck",   "Soft Flute",    "Synth Brass",   4.0f,  8.0f } },
            { "neosoul",   { "lead",  "Warm Analog Pad", "Glass Pluck",  "Mono Lead",     "Synth Brass",   10.0f, 8.0f } },
            { "funk",      { "stabs", "Warm Analog Pad", "Sequence Saw", "Mono Lead",     "Synth Brass",   -2.0f, 2.5f } },
            { "house",     { "pad",   "Warm Analog Pad", "Sequence Saw", "Future Pluck",  "90s Stab",      0.0f,  1.0f } },
            { "techno",    { "arp",   "Space Drone",     "Sequence Saw", "Mono Lead",     "90s Stab",      0.0f,  0.5f } },
            { "edm",       { "lead",  "Night Pad",       "Arp Sparkle",  "Supersaw Lead", "90s Stab",      0.0f,  0.5f } },
            { "dnb",       { "pad",   "Breath Pad",      "Arp Sparkle",  "Mono Lead",     "90s Stab",      0.0f,  1.0f } },
            { "dubstep",   { "pad",   "Dark Matter",     "Sequence Saw", "Screamer",      "90s Stab",      0.0f,  0.5f } },
            { "synthwave", { "arp",   "Warm Analog Pad", "Sequence Saw", "Mono Lead",     "Synth Brass",   0.0f,  1.0f } },
            { "reggae",    { "stabs", "Warm Analog Pad", "Kalimba",      "Soft Flute",    "Synth Brass",   6.0f,  4.0f } },
            { "reggaeton", { "lead",  "Warm Analog Pad", "Arp Sparkle",  "Future Pluck",  "90s Stab",      0.0f,  1.5f } },
            { "afrobeats", { "arp",   "Warm Analog Pad", "Kalimba",      "Soft Flute",    "Synth Brass",   3.0f,  3.0f } },
            { "bossa",     { "lead",  "Breath Pad",      "Harp Pluck",   "Soft Flute",    "Synth Brass",   2.0f,  6.0f } },
            { "ambient",   { "pad",   "Space Drone",     "Glass Pluck",  "Soft Flute",    "Crystal Bell",  0.0f,  6.0f } },
            { "cinematic", { "pad",   "String Machine",  "Pizz Pluck",   "Soft Flute",    "Synth Brass",   0.0f,  2.0f } },
            { "country",   { "pad",   "String Machine",  "Harp Pluck",   "Soft Flute",    "Synth Brass",   0.0f,  4.0f } },
        };

        if (auto it = table.find (style); it != table.end())
            return it->second;

        return table.at ("rock");
    }

    //==============================================================================
    juce::String spaced (const juce::String& words)
    {
        auto t = words.toLowerCase();

        for (const char* p = ",.!?;:()\"/"; *p != 0; ++p)
            t = t.replaceCharacter ((juce::juce_wchar) *p, ' ');

        return " " + t + " ";
    }

    bool has (const juce::String& text, std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (text.contains (" " + juce::String (w) + " "))
                return true;

        return false;
    }

    // The job a sound is made for, when it's made for one
    juce::String impliedRole (const juce::String& sound)
    {
        const auto* p = nova::findFactory (sound);

        if (p == nullptr)
            return {};

        if (p->category == "Pad" || p->category == "Texture")   return "pad";
        if (p->category == "Brass")                             return "stabs";
        if (p->category == "Lead" || p->category == "Bell")     return "lead";
        if (p->category == "Arp")                               return "arp";
        return {};
    }

    bool contains (const std::vector<int>& v, int x)    { return std::find (v.begin(), v.end(), x) != v.end(); }
    int pcOf (int pitch)                                { return ((pitch % 12) + 12) % 12; }

    //==============================================================================
    struct BarIn
    {
        int steps = 16, index = 0, n16 = 16;
        std::vector<int> kicks, crashes;    // in 16ths
        std::vector<int> answer;            // where the keys' right hand starts its notes, in 16ths
        Chord chord, next;
        bool phraseEnd = false, sectionEnd = false, songEnd = false;

        // A 16th's step: in proportion, except on a triplet grid (jazz's twelve steps a bar), where
        // the and of a beat falls on its last triplet, as it's swung
        int at (int pos16) const
        {
            const int quarters = n16 / 4;

            if (n16 % 4 == 0 && quarters > 0 && steps % quarters == 0 && (steps / quarters) % 4 != 0)
            {
                const int per = steps / quarters;
                static const double within[] { 0.0, 1.0 / 3.0, 2.0 / 3.0, 5.0 / 6.0 };
                return (pos16 / 4) * per + (int) std::floor (within[pos16 % 4] * per + 1.0e-9);
            }

            return juce::roundToInt ((double) pos16 * steps / n16);
        }
        bool kickAt (int pos16) const       { return contains (kicks, pos16); }
    };

    struct Ctx
    {
        const Request& req;
        const agents::Style& style;
        juce::Random& rng;
        theory::Key key;
        juce::String role, sound, kind;
        bool legato = false;
        int n16 = 16;                       // 16ths in a bar
        float energy = 0.5f;
        juce::String note;
        std::vector<int> voicing;           // the last chord, to lead the next from
        std::vector<float> keysAt;          // how often the keys play on each 16th (0..1): a lead answers in their gaps
        int last = -1;                      // the line's last note

        int pick (int n)                    { return rng.nextInt (juce::jmax (1, n)); }
        bool chance (float p)               { return rng.nextFloat() < p; }
        juce::String id() const             { return style.id; }
        juce::String family() const         { return style.family; }
        bool chorus() const                 { return kind == "chorus" || kind == "hook" || kind == "drop"; }
    };

    using Rhythm = std::vector<std::pair<int, int>>;    // (16th, length in 16ths)

    struct Plan
    {
        int shape = 0, rate = 2, span = 1, lift = 0, mode = 0;
        int gate = 0;                       // arp: a 16-step mask of where it plays (0: everywhere)
        Rhythm rhythm, variation, fill;     // lead: the motif, its answer, a lick at a phrase's end
        std::vector<int> moves;             // lead: its steps through the scale
        std::vector<int> stabs;             // stabs: where they hit, in 16ths
        float slides = 0.3f, approach = 0.0f;
        bool pentatonic = false, octave = false, anticipate = false;
    };

    using Notes = std::vector<Note>;

    void add (Notes& ns, const BarIn& b, int pos16, int len16, int pitch, int velocity, bool slide = false)
    {
        if (pos16 < 0 || pos16 >= b.n16)
            return;

        Note n;
        n.step = b.at (pos16);
        n.length = std::max (1, b.at (pos16 + std::max (1, len16)) - n.step);
        n.pitch = juce::jlimit (24, 108, pitch);
        n.velocity = juce::jlimit (1, 127, velocity);
        n.slide = slide;
        ns.push_back (n);
    }

    float energyOf (const Ctx& c)
    {
        const auto& k = c.kind;

        // Asked for: a soft section as quiet as it goes (a pad, an arp in eighths), a heavy one full
        if (c.req.softIn.contains (k))
            return 0.2f;

        if (c.req.heavyIn.contains (k))
            return 1.0f;

        float e = c.chorus() ? 1.0f : k == "breakdown" ? 0.85f : k == "prechorus" ? 0.7f
                : k == "bridge" || k == "intro" || k == "outro" ? 0.6f : 0.5f;
        return juce::jlimit (0.2f, 1.0f, e + (c.req.energy - 0.5f) * 0.4f);
    }

    enum class Presence { out, fills, plays };

    // Where each job plays: out of the breakdown's way in metal and rock (a djent pad floating on,
    // a deathcore choir droning, the stabs hitting with the band), a lead answering the singer at
    // the ends of a verse's phrases, trap's melody (it's the beat's) and jazz's line all through
    Presence presence (const Ctx& c)
    {
        const auto f = c.family(), id = c.id(), k = c.kind, r = c.role;
        const bool heavy = f == "metal" || f == "rock";

        // A soft section with the keys in it is theirs: a pad steps out (it'd be as loud as a chorus's,
        // whatever it's played at) except for an ambient intro; stabs too; a lead only answers
        if (c.req.softIn.contains (k))
        {
            if ((r == "pad" && k != "intro" && ! c.keysAt.empty()) || r == "stabs")
                return Presence::out;

            if (r == "lead")
                return Presence::fills;
        }

        if (k == "breakdown")
        {
            if (r == "pad")     return id == "djent" || id == "deathcore" ? Presence::plays : Presence::out;
            if (r == "stabs")   return heavy ? Presence::plays : Presence::out;
            return heavy ? Presence::out : Presence::plays;
        }

        if (r == "lead")
        {
            if (f == "trap" || id == "jazz" || id == "ballad" || id == "lofi")
                return Presence::plays;

            return k == "verse" || k == "prechorus" ? Presence::fills : Presence::plays;
        }

        if (r == "arp")
            return k == "verse" && f == "metal" && id != "djent" ? Presence::out : Presence::plays;

        if (r == "pad" && k == "verse" && (id == "poppunk" || id == "punk"))
            return Presence::out;

        return Presence::plays;
    }

    // A chord's tones as intervals: a triad, with its seventh (and ninth, rootless) for the
    // styles that voice them
    std::vector<int> tonesOf (const Ctx& c, const Chord& ch, bool sevenths, bool rootless)
    {
        const int third = ch.third();
        const int fifth = ch.has (6) ? 6 : (ch.has (8) && ! ch.has (7) ? 8 : 7);
        int seventh = ch.seventh();

        if (seventh < 0)
            seventh = c.id() == "funk" || third != 4 ? 10 : 11;

        std::vector<int> v;

        if (! rootless)
            v.push_back (0);

        if (third >= 0)
            v.push_back (third);

        v.push_back (fifth);

        if (sevenths)
            v.push_back (seventh);

        if (rootless)
            v.push_back (14);

        return v;
    }

    std::vector<int> voice (Ctx& c, const std::vector<int>& tones, int rootPc, int centre, int lo, int hi)
    {
        auto v = led (tones, rootPc, c.voicing, centre, lo, hi);

        if (v.empty())
            for (auto t : tones)
                v.push_back (atOrAbove (rootPc, centre - 6) + t);

        std::sort (v.begin(), v.end());
        c.voicing = v;
        return v;
    }

    //==============================================================================
    //  The scale a line moves through over a chord, and the notes it lands on
    //==============================================================================
    std::vector<int> scaleOver (const Ctx& c, const Plan& p, const Chord& ch)
    {
        const auto keyPcs = [&] (const std::vector<int>& scale)
        {
            std::vector<int> pcs;

            for (auto d : scale)
                pcs.push_back ((c.key.root + d) % 12);

            return pcs;
        };

        const auto chordPcs = [&]
        {
            std::vector<int> pcs;

            for (auto t : ch.tones)
                pcs.push_back ((ch.root + t) % 12);

            return pcs;
        }();

        auto pcs = keyPcs (c.key.scale);
        int outside = 0;

        for (auto pc : chordPcs)
            outside += contains (pcs, pc) ? 0 : 1;

        if (outside >= 2)
        {
            // A borrowed chord: the parallel key's scale, when the chord is in it
            const bool major = c.key.mode == "major" || c.key.mode == "mixolydian" || c.key.mode == "lydian";
            const auto parallel = keyPcs (theory::scaleOf (major ? "minor" : "major"));

            if (std::all_of (chordPcs.begin(), chordPcs.end(), [&] (int pc) { return contains (parallel, pc); }))
                pcs = parallel;
        }

        // A chord tone off the scale bends it: the raised seventh over a major V in minor makes
        // harmonic minor, a flat second makes phrygian
        for (auto pc : chordPcs)
            if (! contains (pcs, pc))
            {
                pcs.erase (std::remove_if (pcs.begin(), pcs.end(), [&] (int x)
                {
                    return (x == (pc + 1) % 12 || x == (pc + 11) % 12) && x != c.key.root && ! contains (chordPcs, x);
                }), pcs.end());
                pcs.push_back (pc);
            }

        if (p.pentatonic)
        {
            // The pentatonic (minor's or major's) and the chord's own tones; funk's blue note too
            const bool minor = ! (c.key.mode == "major" || c.key.mode == "mixolydian" || c.key.mode == "lydian");
            std::vector<int> five = minor ? std::vector<int> { 0, 3, 5, 7, 10 } : std::vector<int> { 0, 2, 4, 7, 9 };

            if (c.id() == "funk" && minor)
                five.push_back (6);

            std::vector<int> kept;

            for (auto d : five)
                if (const int pc = (c.key.root + d) % 12; contains (pcs, pc) || (c.id() == "funk" && d == 6))
                    kept.push_back (pc);

            for (auto pc : chordPcs)
                kept.push_back (pc);

            pcs = kept;
        }

        std::sort (pcs.begin(), pcs.end());
        pcs.erase (std::unique (pcs.begin(), pcs.end()), pcs.end());
        return pcs;
    }

    // The notes a strong beat or a long note may land on: the chord's tones (a power chord's third
    // from the key), and the tensions the style sings - the ninth, minor's eleventh, major's thirteenth
    std::vector<int> targetsOver (const Ctx& c, const Chord& ch, const std::vector<int>& scale)
    {
        std::vector<int> pcs;

        for (auto t : ch.tones)
            pcs.push_back ((ch.root + t) % 12);

        if (ch.third() < 0)
            for (int t : { 3, 4 })
                if (contains (scale, (ch.root + t) % 12))
                {
                    pcs.push_back ((ch.root + t) % 12);
                    break;
                }

        const auto id = c.id(), f = c.family();
        const bool ninths = f == "lofi" || id == "funk" || id == "trap" || id == "hiphop" || id == "djent" || id == "poppunk";

        if (ninths && contains (scale, (ch.root + 2) % 12))
            pcs.push_back ((ch.root + 2) % 12);

        if (f == "lofi" || id == "funk")
        {
            if (ch.minor() && contains (scale, (ch.root + 5) % 12))
                pcs.push_back ((ch.root + 5) % 12);

            if (! ch.minor() && contains (scale, (ch.root + 9) % 12))
                pcs.push_back ((ch.root + 9) % 12);
        }

        return pcs;
    }

    // The nearest pitch at or around `pitch` on one of these pitch classes, in range; a tie goes
    // the way the line is moving
    int nearestIn (int pitch, const std::vector<int>& pcs, int lo, int hi, int lean = 0)
    {
        int best = juce::jlimit (lo, hi, pitch), bestCost = 1 << 20;

        for (int x = lo; x <= hi; ++x)
            if (contains (pcs, pcOf (x)))
            {
                const int d = x - pitch;
                const int cost = std::abs (d) * 2 + (lean != 0 && d != 0 && (d > 0) != (lean > 0) ? 1 : 0);

                if (cost < bestCost)
                {
                    bestCost = cost;
                    best = x;
                }
            }

        return best;
    }

    // `steps` notes of the scale up (or down) from `pitch`
    int stepIn (int pitch, const std::vector<int>& pcs, int steps)
    {
        if (steps == 0 || pcs.empty())
            return pitch;

        const int dir = steps > 0 ? 1 : -1;
        int p = pitch;

        for (int n = 0; n < std::abs (steps); ++n)
            do
                p += dir;
            while (! contains (pcs, pcOf (p)) && std::abs (p - pitch) < 24);

        return p;
    }

    //==============================================================================
    //  Rhythms
    //==============================================================================
    Rhythm rhythmOf (Ctx& c, const std::vector<int>& places, int fewest, int most, bool onOne, int longest)
    {
        std::vector<int> at;

        if (onOne)
            at.push_back (0);

        const int count = fewest + c.pick (most - fewest + 1);

        for (int guard = 0; (int) at.size() < count && guard < 200; ++guard)
            if (const int x = places[(size_t) c.pick ((int) places.size())]; x < c.n16 && ! contains (at, x))
                at.push_back (x);

        if (at.empty())
            at.push_back (0);

        std::sort (at.begin(), at.end());
        Rhythm r;

        for (size_t i = 0; i < at.size(); ++i)
        {
            const int next = i + 1 < at.size() ? at[i + 1] : c.n16;
            r.push_back ({ at[i], juce::jlimit (1, longest, next - at[i]) });
        }

        return r;
    }

    // The first half of one, the second half of the other
    Rhythm join (const Rhythm& first, const Rhythm& second, int n16, int longest)
    {
        std::vector<int> at;

        for (auto& [x, len] : first)
            if (x < n16 / 2)
                at.push_back (x);

        for (auto& [x, len] : second)
            if (x >= n16 / 2)
                at.push_back (x);

        if (at.empty())
            at.push_back (0);

        Rhythm r;

        for (size_t i = 0; i < at.size(); ++i)
            r.push_back ({ at[i], juce::jlimit (1, longest, (i + 1 < at.size() ? at[i + 1] : n16) - at[i]) });

        return r;
    }

    // A phrase's last bar: the motif's first half, its last note held to the bar line
    Rhythm cadenceOf (const Rhythm& r, int n16)
    {
        Rhythm out;

        for (auto& [x, len] : r)
            if (x < n16 / 2)
                out.push_back ({ x, len });

        if (out.empty())
            out.push_back ({ 0, 1 });

        out.back().second = n16 - out.back().first;
        return out;
    }

    //==============================================================================
    //  Pads: chords held, each led from the last
    //==============================================================================
    std::vector<Notes> padSection (Ctx& c, const Plan& p, const std::vector<BarIn>& ins)
    {
        std::vector<Notes> bars (ins.size());
        const auto id = c.id(), f = c.family();

        if (presence (c) == Presence::out)
        {
            c.note = c.kind == "breakdown" ? "laying out for the breakdown" : "laying out under the verse";
            return bars;
        }

        // Where it sits: the dark pads and deathcore's choir low, black metal's and djent's high
        int centre = 64, lo = 50, hi = 81;

        if (id == "deathcore" || id == "trap")              { centre = 60; lo = 46; hi = 74; }
        else if (id == "blast" || id == "djent")            { centre = 69; lo = 55; hi = 86; }
        else if (f == "lofi" || id == "funk")               { centre = 64; lo = 52; hi = 78; }

        centre += p.lift;

        // Deathcore's breakdown: a low choir drone under it, the root and fifth
        if (c.kind == "breakdown" && id == "deathcore")
        {
            const int r = atOrAbove (c.key.root, 40);

            for (size_t i = 0; i < ins.size(); i += 4)
                for (int x : { r, r + 7, r + 12 })
                    add (bars[i], ins[i], 0, (int) std::min<size_t> (4, ins.size() - i) * ins[i].n16, x, 62);

            c.note = "a low choir drone under the breakdown, the root and fifth";
            return bars;
        }

        const bool rootless = f == "lofi" || id == "funk";
        const bool sevenths = rootless || id == "ballad";
        const bool thin = c.kind == "verse" && f == "metal" && id != "djent";
        const bool spread = (f == "metal" || f == "rock") && id != "djent";
        const bool pushes = p.anticipate && (id == "neosoul" || id == "funk" || id == "lofi" || f == "rock");

        struct Held { int bar = -1; std::vector<size_t> notes; Chord chord; bool early = false; };
        Held held;

        auto strike = [&] (size_t i, int pos16, const Chord& ch, int velocity)
        {
            const auto& b = ins[i];
            std::vector<int> tones;

            if (thin)
                tones = { 0, 7 };
            else if (id == "djent")
                tones = ch.third() >= 0 ? std::vector<int> { 0, 2, ch.third(), 7 } : std::vector<int> { 0, 2, 7 };
            else
                tones = tonesOf (c, ch, sevenths, rootless);

            auto v = voice (c, tones, ch.root, centre, lo, hi);

            // Metal and rock spread wide: the middle of a triad up an octave; under a chorus, the
            // root doubled an octave down
            if (spread && v.size() == 3 && v[1] + 12 <= hi + 5)
                v[1] += 12;

            if (spread && c.energy >= 0.95f)
                v.push_back (atOrAbove (ch.root, v.front() - 12));

            held = {};
            held.bar = (int) i;
            held.chord = ch;

            for (auto x : v)
            {
                held.notes.push_back (bars[i].size());
                add (bars[i], b, pos16, b.n16 - pos16, x, velocity);
            }
        };

        for (size_t i = 0; i < ins.size(); ++i)
        {
            const auto& b = ins[i];
            const int velocity = juce::jlimit (30, 112, juce::roundToInt (46.0f + 52.0f * c.energy + (c.kind == "prechorus" ? (float) i * 5.0f : 0.0f)));

            // The same chord rings on (struck again at a phrase's start, unless it came in early)
            if (held.bar >= 0 && held.chord == b.chord && (b.index % 4 != 0 || held.early))
            {
                for (auto k : held.notes)
                    bars[(size_t) held.bar][k].length += b.steps;

                held.early = false;
            }
            else
                strike (i, 0, b.chord, velocity);

            // Pushed: the next chord comes in an 8th early, tied over the bar line
            if (pushes && i + 1 < ins.size() && ! (ins[i + 1].chord == b.chord))
            {
                const int cut = b.steps - b.at (b.n16 - 2);

                for (auto k : held.notes)
                    bars[(size_t) held.bar][k].length = std::max (1, bars[(size_t) held.bar][k].length - cut);

                strike (i, b.n16 - 2, ins[i + 1].chord, velocity);

                for (auto k : held.notes)
                    bars[i][k].length = cut;

                held.chord = ins[i + 1].chord;
                held.early = true;
            }
        }

        if (id == "djent")
            c.note = c.kind == "breakdown" ? "the ambient pad floating on over the chugs" : "an ambient pad of add9 chords, high and wide";
        else if (thin)
            c.note = "low and thin under the verse, the root and fifth";
        else if (f == "metal")
            c.note = c.energy >= 0.95f ? "the full chord held, spread wide, the root doubled under it" : "the chord held, spread wide";
        else if (f == "trap")
            c.note = "a dark pad, a chord a bar, each led from the last";
        else if (rootless)
            c.note = juce::String ("warm rootless sevenths and ninths, the root left to the bass") + (pushes ? ", each change pushed in an 8th early" : "");
        else if (id == "ballad")
            c.note = "strings held, each chord's notes moving as little as they can";
        else
            c.note = juce::String ("the chords held") + (spread ? ", spread wide" : "") + (pushes ? ", pushed in an 8th early" : "");

        return bars;
    }

    //==============================================================================
    //  Arpeggios: the chord's notes one after another, each bar starting from the chord's lowest
    //==============================================================================
    Notes arpBar (Ctx& c, const Plan& p, const BarIn& b, int nth, int count)
    {
        Notes ns;

        if (presence (c) == Presence::out)
        {
            c.note = c.kind == "breakdown" ? "laying out for the breakdown" : "laying out under the verse";
            return ns;
        }

        const auto id = c.id(), f = c.family();
        auto tones = tonesOf (c, b.chord, f == "lofi" || id == "funk", false);

        if (id == "djent" && b.chord.third() >= 0)
            tones.insert (tones.begin() + 1, 2);        // djent's add9 shimmer

        const auto v = voice (c, tones, b.chord.root, 62 + p.lift, 50, 77);
        const bool quiet = c.energy < 0.6f;
        const int span = quiet || p.shape == 3 || p.shape == 4 ? 1 : p.span;     // a pedal or trance shape stays in its octave
        std::vector<int> line = v;

        for (int o = 1; o < span; ++o)
            for (auto x : v)
                line.push_back (x + 12 * o);

        line.push_back (v.front() + 12 * span);

        std::vector<int> order;
        const int n = (int) line.size();
        static const char* const shapes[] { "up", "up and down", "broken in thirds", "over a pedal on the root", "each note then the top, trance style", "down" };

        switch (p.shape)
        {
            case 0:  order = line; break;
            case 1:  order = line; for (int k = n - 2; k > 0; --k) order.push_back (line[(size_t) k]); break;
            case 2:  for (int k = 0; k + 2 < n; ++k) { order.push_back (line[(size_t) k]); order.push_back (line[(size_t) k + 2]); } break;
            case 3:  for (int k = 1; k < n; ++k) { order.push_back (line[0]); order.push_back (line[(size_t) k]); } break;
            case 4:  for (int k = 0; k + 1 < n; ++k) { order.push_back (line[(size_t) k]); order.push_back (line.back()); } break;
            default: order.assign (line.rbegin(), line.rend()); break;
        }

        if (order.empty())
            order = line;

        // A pattern that fits the beat: four, eight or sixteen notes long, bounced back down from
        // its top to fill, or cut
        {
            const size_t target = order.size() <= 4 ? 4 : order.size() <= 8 ? 8 : 16;

            for (size_t k = order.size() - 1; order.size() < target && k > 0; --k)
                order.push_back (order[k - 1]);

            while (order.size() < target)
                order.push_back (order[order.size() % std::max<size_t> (1, line.size())]);

            order.resize (target);
        }

        int rate = p.rate;

        if (quiet && rate == 1 && f != "metal")
            rate = 2;

        // A build through an intro or a pre-chorus
        const int ramp = (c.kind == "intro" || c.kind == "prechorus") && count > 1 ? juce::roundToInt (-18.0f + 30.0f * (float) nth / (float) (count - 1)) : 0;
        const int base = 60 + juce::roundToInt (32.0f * c.energy) + ramp;

        for (int pos = 0, k = 0; pos < b.n16; pos += rate)
        {
            if (p.gate != 0 && ((p.gate >> (pos % 16)) & 1) == 0)
                continue;

            const int velocity = base + (pos % 4 == 0 ? 10 : 0) + (b.kickAt (pos) ? 6 : 0);
            add (ns, b, pos, rate, order[(size_t) (k++ % (int) order.size())], velocity);
        }

        c.note = juce::String (rate == 1 ? "a 16th-note arpeggio" : "an 8th-note arpeggio") + ", " + shapes[juce::jlimit (0, 5, p.shape)]
               + (span > 1 ? ", over two octaves" : "") + (p.gate != 0 ? ", gated in a syncopated rhythm" : "")
               + (ramp != 0 ? ", building" : "") + ", accented with the kick";
        return ns;
    }

    //==============================================================================
    //  Leads: a motif, repeated and answered, its strong beats on the chord's tones
    //==============================================================================
    struct Range { int lo, hi, centre; };

    Range leadRange (const Ctx& c)
    {
        const auto id = c.id(), f = c.family();

        if (id == "trap")                       return { 72, 91, 79 };
        if (id == "drill")                      return { 70, 88, 77 };
        if (f == "trap")                        return { 67, 86, 74 };
        if (id == "neosoul")                    return { 60, 81, 70 };
        if (id == "funk")                       return { 65, 89, 75 };
        if (id == "jazz")                       return { 64, 89, 74 };
        if (id == "ballad" || id == "lofi")     return { 64, 84, 72 };
        if (id == "djent" || id == "rock")      return { 62, 84, 72 };
        return { 66, 88, 76 };
    }

    struct Tone { int pos, len, pitch; bool slide = false; };

    std::vector<Notes> leadSection (Ctx& c, const Plan& p, const std::vector<BarIn>& ins)
    {
        std::vector<Notes> bars (ins.size());
        const auto pres = presence (c);
        const auto id = c.id(), f = c.family();

        if (pres == Presence::out)
        {
            c.note = c.kind == "breakdown" ? "laying out for the breakdown" : "laying out";
            return bars;
        }

        const auto r = leadRange (c);
        const bool loop = f == "trap";
        const int base = 66 + juce::roundToInt (32.0f * c.energy);
        int played = 0;

        for (size_t i = 0; i < ins.size(); ++i)
        {
            const auto& b = ins[i];
            const int k = b.index % 4;
            const auto scale = scaleOver (c, p, b.chord);
            const auto targets = targetsOver (c, b.chord, scale);
            Rhythm rhythm;

            const bool fill = pres == Presence::fills;

            if (fill)
            {
                if (! b.phraseEnd)
                    continue;

                rhythm = p.fill;
            }
            else if (loop)
                rhythm = k == 3 ? p.variation : p.rhythm;
            else if (id == "lofi" || id == "ballad")
                rhythm = k == 0 ? p.rhythm : k == 1 ? cadenceOf (p.rhythm, b.n16) : k == 2 ? p.variation : Rhythm {};
            else
                rhythm = k == 2 ? p.variation : (k == 3 ? cadenceOf (p.rhythm, b.n16) : p.rhythm);

            rhythm.erase (std::remove_if (rhythm.begin(), rhythm.end(), [&] (auto& x) { return x.first >= b.n16; }), rhythm.end());

            if (rhythm.empty())
                continue;

            // Where this bar's line sits: an arch over the phrase, its peak in the third bar of a
            // chorus; a loop stays where it is
            static const int arch[] { 0, 2, 5, -1 };
            const int height = r.centre + p.lift + (loop ? 0 : (c.energy >= 0.95f ? arch[k] : arch[k] / 2));
            std::vector<Tone> line;
            int prev = c.last >= 0 && ! loop ? c.last : height;

            for (size_t j = 0; j < rhythm.size(); ++j)
            {
                const auto [pos, len] = rhythm[j];
                const bool strong = pos % 8 == 0 || len >= 4;
                int pitch;

                if (j == 0)
                    pitch = nearestIn ((prev + height) / 2, targets, r.lo, r.hi);
                else
                {
                    int move = p.moves[(j - 1) % p.moves.size()];

                    if (fill && move == 0)
                        move = (int) j % 2 == 0 ? 1 : -1;

                    pitch = stepIn (prev, scale, move);

                    if (strong)
                        pitch = nearestIn (pitch, targets, r.lo, r.hi, move);
                }

                // A phrase's last note comes home: the root or the third
                if (j + 1 == rhythm.size() && k == 3 && ! loop && pres == Presence::plays)
                {
                    std::vector<int> home { b.chord.root };

                    if (b.chord.third() >= 0)
                        home.push_back ((b.chord.root + b.chord.third()) % 12);

                    pitch = nearestIn (pitch, home, r.lo, r.hi);
                }

                while (pitch > r.hi)    pitch -= 12;
                while (pitch < r.lo)    pitch += 12;

                line.push_back ({ pos, len, pitch });
                prev = pitch;
            }

            // Jazz's approach notes: a half step below (or above) the chord tone on the next beat
            if (p.approach > 0.0f)
                for (size_t j = 0; j + 1 < line.size(); ++j)
                    if (line[j + 1].pos % 4 == 0 && line[j + 1].pos - line[j].pos <= 2 && c.chance (p.approach))
                        line[j].pitch = line[j + 1].pitch + (c.chance (0.7f) ? -1 : 1);

            // A legato lead slides into its near notes; neo-soul and funk scoop up into a long
            // note from a step below
            if (c.legato)
            {
                for (size_t j = 1; j < line.size(); ++j)
                    if (line[j].pitch != line[j - 1].pitch && std::abs (line[j].pitch - line[j - 1].pitch) <= 5
                        && line[j - 1].pos + line[j - 1].len >= line[j].pos && c.chance (p.slides))
                        line[j].slide = true;

                if (id == "neosoul" || id == "funk" || c.req.slides)
                    for (size_t j = 0; j < line.size(); ++j)
                        if (line[j].len >= 4 && line[j].pos >= 1 && ! line[j].slide && c.chance (p.slides)
                            && (j == 0 || line[j - 1].pos + line[j - 1].len <= line[j].pos - 1))
                        {
                            const int below = contains (scale, pcOf (line[j].pitch - 2)) ? 2 : 1;
                            line[j].slide = true;
                            line.insert (line.begin() + (long) j, Tone { line[j].pos - 1, 1, line[j].pitch - below });
                            ++j;
                        }
            }

            for (auto& t : line)
            {
                const int velocity = base + (t.pos == 0 ? 8 : 0) + (t.len >= 4 ? 4 : 0) - (t.pos % 2 == 1 ? 6 : 0) + c.pick (5) - 2;
                add (bars[i], b, t.pos, t.len, t.pitch, velocity, t.slide);

                if (p.octave && ! c.legato)
                    add (bars[i], b, t.pos, t.len, t.pitch - 12, velocity - 8);
            }

            c.last = prev;
            ++played;
        }

        juce::String what = c.sound.containsIgnoreCase ("bell") || c.sound == "Music Box" ? "a bell melody"
                          : c.sound == "Soft Flute" ? "a flute line" : "a lead melody";

        if (pres == Presence::fills)
            c.note = what + ", answering at the end of each phrase" + (c.legato ? ", sliding" : "");
        else if (loop)
            c.note = what + ", the same rhythm every bar, following the chords, turned around in the fourth";
        else if (id == "jazz")
            c.note = "a flute line in swung 8ths, chord tones on the beats, approached from a half step away";
        else if (id == "lofi")
            c.note = "a lazy melody behind the beat, a phrase and a breath";
        else if (id == "neosoul")
            c.note = "a Moog-style lead sliding into its notes, ninths and elevenths";
        else
            c.note = what + ", a motif, repeated, answered, coming home at the end of each phrase" + (p.octave ? ", in octaves" : "")
                   + (c.legato && p.slides > 0.2f ? ", sliding" : "");

        if (played == 0)
            c.note = "laying out";

        return bars;
    }

    //==============================================================================
    //  Stabs: chords hit with the band
    //==============================================================================
    Notes riffBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        // Funk's horn section in the chorus: a riff in octaves on the key's blues scale, the same
        // each bar, the fourth bar ending on a long note
        Notes ns;
        std::vector<int> blues;

        for (int d : { 0, 3, 5, 6, 7, 10 })
            blues.push_back ((c.key.root + d) % 12);

        const bool last = b.index % 4 == 3;
        int prev = nearestIn (72 + p.lift, { c.key.root, (c.key.root + 7) % 12 }, 64, 80);

        for (size_t j = 0; j < p.rhythm.size(); ++j)
        {
            const auto [pos, len] = p.rhythm[j];
            // A riff moves: no note struck twice running but its first
            int move = j == 0 ? 0 : p.moves[(j - 1) % p.moves.size()];

            if (j > 1 && move == 0)
                move = (int) j % 2 == 0 ? 2 : -1;

            int pitch = j == 0 ? prev : stepIn (prev, blues, move);

            while (pitch > 82)  pitch -= 12;
            while (pitch < 64)  pitch += 12;

            const bool end = last && j + 1 == p.rhythm.size();
            const int length = end ? b.n16 - pos : std::min (len, 2);
            add (ns, b, pos, length, pitch, 104 + (pos == 0 ? 6 : 0));
            add (ns, b, pos, length, pitch - 12, 96);
            prev = pitch;
        }

        c.note = "the horns: a riff in octaves on the blues scale, the same each bar";
        return ns;
    }

    Notes stabBar (Ctx& c, const Plan& p, const BarIn& b)
    {
        Notes ns;
        const auto id = c.id(), f = c.family();

        if (presence (c) == Presence::out)
        {
            c.note = "laying out";
            return ns;
        }

        if (id == "funk" && c.chorus())
            return riffBar (c, p, b);

        const bool horns = f == "lofi" || id == "funk" || id == "boombap" || f == "island";
        const auto tones = tonesOf (c, b.chord, horns, horns);
        const auto v = voice (c, tones, b.chord.root, 68 + p.lift, 57, 84);
        const int velocity = 84 + juce::roundToInt (24.0f * c.energy);
        std::vector<std::pair<int, int>> hits;
        bool pushNext = false;

        auto longHits = [&]
        {
            for (size_t k = 0; k < b.kicks.size(); ++k)
            {
                const int at = b.kicks[k], next = k + 1 < b.kicks.size() ? b.kicks[k + 1] : b.n16;

                if ((at == 0 || next - at >= 4) && (b.index % 2 == 0 || at == 0))
                    hits.push_back ({ at, std::min (next - at, 4) });
            }
        };

        if (f == "island")
        {
            // Reggae's and afrobeats' horns: short hits answering over two bars, the and of 2 and of 4,
            // then the and of 1 and of 3
            for (int at : b.index % 2 == 0 ? std::vector<int> { 6, 14 } : std::vector<int> { 2, 10 })
                if (at < b.n16)
                    hits.push_back ({ at, 2 });

            c.note = "horn hits on the offbeats, answering over two bars";
        }
        else if (f == "metal" || f == "electronic" || id == "poppunk" || id == "punk")
        {
            if (c.kind == "breakdown")
            {
                longHits();
                c.note = "hitting with the breakdown's long hits";
            }
            else if (c.chorus() || c.kind == "intro" || c.kind == "outro")
            {
                for (int at = 2; at < b.n16; at += 4)
                    hits.push_back ({ at, p.mode == 0 ? 1 : 2 });

                c.note = "trance stabs on the off-beat 8ths";
            }
            else if (c.kind == "prechorus")
            {
                for (int at = 0; at < b.n16; at += 4)
                    hits.push_back ({ at, 2 });

                c.note = "a stab on every beat, building";
            }
            else if (b.index % 4 == 0)
            {
                hits.push_back ({ 0, 8 });
                c.note = "a chord at the top of each phrase";
            }
        }
        else if (f == "rock")
        {
            if (c.kind == "breakdown")
            {
                longHits();
                c.note = "hitting with the band";
            }
            else if (c.chorus())
            {
                hits.push_back ({ 0, b.n16 - 2 });
                pushNext = ! (b.next == b.chord);
                c.note = "the chord held through the chorus, the next one pushed in an 8th early";
            }
            else
            {
                for (size_t k = 0; k < p.stabs.size(); ++k)
                    hits.push_back ({ p.stabs[k], std::max (1, (k + 1 < p.stabs.size() ? p.stabs[k + 1] : b.n16) - p.stabs[k] - 1) });

                c.note = id == "hardrock" ? "an 80s brass riff, the chords on the syncopations" : "band hits on the syncopations";
            }
        }
        else if (f == "trap")
        {
            // Call and response over two bars: the hits, then a pickup into the next
            if (b.index % 2 == 0)
                for (auto at : p.stabs)
                    hits.push_back ({ at, 2 });
            else if (p.mode != 0)
                pushNext = true;

            c.note = id == "boombap" ? "horn stabs, a call and a pickup answering it over two bars" : "stabs, a call and a pickup over two bars";
        }
        else if (id == "funk")
        {
            if (c.kind == "bridge")
            {
                hits.push_back ({ 0, b.n16 });
                c.note = "the horns swelling, held chords";
            }
            else
            {
                for (auto at : p.stabs)
                    if (! b.kickAt (at))
                        hits.push_back ({ at, 1 });

                c.note = "the horns: short stabs on the syncopations, between the kicks";
            }
        }
        else
        {
            // Jazz, ballads, lofi, neo-soul: pushes into the next chord on the and of four
            if (b.index == 0)
                hits.push_back ({ 0, 4 });

            pushNext = ! (b.next == b.chord) || b.index % 2 == 1;
            c.note = "pushes into each chord on the and of four";
        }

        for (auto& [at, len] : hits)
            for (auto x : v)
                add (ns, b, at, len, x, velocity + (at == 0 ? 6 : 0) + (b.kickAt (at) ? 4 : 0));

        if (pushNext && b.n16 >= 4)
        {
            const auto nv = voice (c, tonesOf (c, b.next, horns, horns), b.next.root, 68 + p.lift, 57, 84);

            for (auto x : nv)
                add (ns, b, b.n16 - 2, 4, x, velocity + 4);
        }

        return ns;
    }

    //==============================================================================
    int sixteenthOf (const BarIn& b, int step)
    {
        for (int pos = 0; pos < b.n16; ++pos)
            if (b.at (pos) >= step)
                return pos;

        return b.n16;
    }

    float score (const std::vector<Notes>& bars, const std::vector<BarIn>& ins, const Ctx& c, const Plan& p)
    {
        float s = 0.0f;
        int lastTop = -1, count = 0, top = -1, tops = 0;
        const auto id = c.id(), f = c.family();
        const auto r = leadRange (c);

        for (size_t i = 0; i < bars.size(); ++i)
        {
            const auto& b = ins[i];
            const auto scale = scaleOver (c, p, b.chord);
            const auto targets = targetsOver (c, b.chord, scale);
            int prev = -1, inBar = 0;

            for (auto& n : bars[i])
            {
                // An octave doubling isn't a note of the line
                if (std::any_of (bars[i].begin(), bars[i].end(), [&] (const Note& o) { return o.step == n.step && o.pitch == n.pitch + 12; }))
                    continue;

                if (n.pitch < 36 || n.pitch > 96)
                    s -= 2.0f;

                if (c.role == "lead")
                {
                    const int pos = sixteenthOf (b, n.step);
                    const int len = sixteenthOf (b, n.step + n.length) - pos;
                    const bool strong = pos % 8 == 0 || len >= 4;
                    const bool target = contains (targets, pcOf (n.pitch));

                    if (strong)
                        s += target ? 1.0f : -1.2f;

                    // Held on a half step above the chord: the note to avoid
                    if (len >= 4 && ! target)
                        for (auto t : targets)
                            if (pcOf (n.pitch) == (t + 1) % 12)
                                s -= 1.5f;

                    if (prev >= 0)
                    {
                        const int d = std::abs (n.pitch - prev);
                        s += d == 0 ? -0.05f : d <= 2 ? 0.25f : d <= 5 ? 0.05f : d == 12 ? -0.1f : d <= 7 ? -0.3f : -0.9f;
                    }

                    if (n.pitch < r.lo || n.pitch > r.hi)
                        s -= 2.0f;

                    // Trap's melody and lofi's answer the keys rather than talk over them
                    if ((f == "trap" || id == "lofi") && contains (b.answer, pos))
                        s -= 0.4f;

                    if (n.pitch > top)
                    {
                        top = n.pitch;
                        tops = 1;
                    }
                    else if (n.pitch == top)
                        ++tops;

                    // Neo-soul's and funk's Moog lead slides into its notes
                    if (n.slide && c.legato && (id == "neosoul" || id == "funk"))
                        s += 0.5f;

                    prev = n.pitch;
                }
                else if (c.role == "stabs")
                {
                    const int pos = sixteenthOf (b, n.step);

                    if (f == "rock" || f == "metal")
                        s += b.kickAt (pos) ? 0.2f : 0.0f;
                    else if (id == "funk" && ! c.chorus())
                        s -= b.kickAt (pos) ? 0.5f : 0.0f;
                }

                ++count;
                ++inBar;
            }

            if (c.role == "lead" && inBar > 0)
            {
                // A motif with a shape: three notes or more of it different
                std::set<int> pitches;

                for (auto& n : bars[i])
                    pitches.insert (n.pitch);

                if (inBar >= 3 && pitches.size() < 3)
                    s -= 0.8f;

                const float want = id == "trap" || id == "drill" ? 5.5f : id == "lofi" || id == "ballad" ? 2.5f : id == "jazz" ? 6.0f : 4.0f;
                s -= std::abs ((float) inBar - want) * 0.15f;
            }

            // The top voice moves smoothly from bar to bar
            if (c.role != "lead")
            {
                int t = -1;

                for (auto& n : bars[i])
                    if (n.step == 0)
                        t = std::max (t, n.pitch);

                if (t >= 0 && lastTop >= 0)
                    s -= std::max (0.0f, (float) std::abs (t - lastTop) - 4.0f) * 0.3f;

                if (t >= 0)
                    lastTop = t;
            }
        }

        // A chorus's line has one peak
        if (c.role == "lead" && c.chorus() && tops == 1)
            s += 1.0f;

        return s / (float) std::max (1, count) * 4.0f;
    }

    Plan makePlan (Ctx& c)
    {
        Plan p;
        const auto id = c.id(), f = c.family();
        p.mode = c.pick (2);
        p.lift = (c.pick (5) - 2) * 2;
        p.anticipate = c.chance (0.6f);

        // Arpeggios: metal and trance in 16ths, the rest in 8ths; funk's and trap's gated
        p.shape = f == "metal" && c.chance (0.6f) ? (c.chance (0.5f) ? 1 : 4) : c.pick (6);
        p.rate = (f == "metal" && id != "djent") || id == "funk" || (id == "rock" && c.chance (0.5f)) ? 1 : 2;
        p.span = c.chorus() || c.chance (0.4f) ? 2 : 1;

        if (id == "funk" || (f == "trap" && c.chance (0.5f)))
        {
            p.rate = 1;
            p.gate = 1;

            for (int k = 1; k < 16; ++k)
                if (c.chance (id == "funk" ? 0.72f : 0.55f))
                    p.gate |= 1 << k;
        }

        // The lead's motif: the style's own rhythm
        const std::vector<int> eighths { 2, 4, 6, 8, 10, 12, 14 };
        std::vector<int> places = eighths;
        int fewest = 4, most = 6, longest = 6;
        bool onOne = true;

        if (id == "trap")                   { places = { 2, 3, 4, 6, 7, 8, 10, 11, 12, 14 }; fewest = 4; most = 7; longest = 3; }
        else if (id == "drill")             { places = { 3, 6, 8, 10, 11, 12, 14 }; fewest = 4; most = 6; longest = 3; }
        else if (f == "trap")               { places = { 2, 3, 4, 6, 8, 10, 11, 12, 14 }; fewest = 3; most = 5; longest = 4; }
        else if (id == "lofi")              { places = { 3, 4, 6, 8, 10, 11 }; fewest = 2; most = 4; longest = 8; onOne = c.chance (0.6f); }
        else if (id == "ballad")            { places = { 4, 6, 8, 10, 12 }; fewest = 2; most = 3; longest = 8; }
        else if (id == "jazz")              { fewest = 5; most = 7; longest = 4; onOne = c.chance (0.5f); }
        else if (id == "neosoul")           { places = { 3, 4, 6, 7, 8, 10, 12, 14 }; fewest = 3; most = 5; longest = 6; onOne = c.chance (0.7f); }
        else if (id == "funk")              { places = { 3, 4, 6, 7, 10, 11, 12, 14, 15 }; fewest = 4; most = 6; longest = 4; }
        else if (f == "metal")              { fewest = 3; most = 6; }

        // Trap's bells and lofi's lead answer the keys: their rhythm in the gaps the keys leave
        if ((f == "trap" || id == "lofi") && ! c.keysAt.empty())
        {
            auto often = [&c] (int x) { return juce::isPositiveAndBelow (x, (int) c.keysAt.size()) ? c.keysAt[(size_t) x] : 0.0f; };
            std::vector<int> gaps;

            for (auto x : places)
                if (often (x) < 0.3f)
                    gaps.push_back (x);

            // Too few gaps: the places the keys play on least
            if ((int) gaps.size() < most)
            {
                gaps = places;
                std::stable_sort (gaps.begin(), gaps.end(), [&] (int a, int b) { return often (a) < often (b); });
                gaps.resize ((size_t) std::min ((int) gaps.size(), most + 1));
            }

            places = gaps;
            onOne = onOne && often (0) < 0.3f;
        }

        p.rhythm = rhythmOf (c, places, fewest, most, onOne, longest);
        p.variation = join (p.rhythm, rhythmOf (c, places, fewest, most, onOne, longest), c.n16, longest);

        std::vector<int> late;

        for (auto x : places)
            if (x >= c.n16 / 2)
                late.push_back (x);

        if (late.empty())
            late = { c.n16 / 2 };

        p.fill = rhythmOf (c, late, 3, 4, false, longest);
        p.fill.back().second = std::max (p.fill.back().second, c.n16 - p.fill.back().first);

        // Its shape through the scale: mostly steps, a leap filled back by a step the other way;
        // trap's repeated notes
        int lastMove = 0;

        for (int i = 0; i < 8; ++i)
        {
            int move;

            if (std::abs (lastMove) >= 3)
                move = lastMove > 0 ? -1 : 1;
            else if (f == "trap")
            {
                static const int pool[] { -2, -1, -1, 0, 0, 1, 1, 2, 3 };
                move = pool[c.pick (9)];
            }
            else
            {
                static const int pool[] { -2, -1, -1, -1, 0, 1, 1, 1, 2, 3, -3 };
                move = pool[c.pick (11)];
            }

            p.moves.push_back (move);
            lastMove = move;
        }

        p.pentatonic = id == "lofi" || id == "funk" || (id == "neosoul" && c.chance (0.7f))
                    || (f == "rock" && c.chance (0.5f)) || (id == "trap" && c.chance (0.3f));
        p.approach = id == "jazz" ? 0.55f : id == "neosoul" ? 0.15f : 0.0f;
        p.slides = (id == "neosoul" || id == "funk" ? 0.45f : id == "jazz" || id == "ballad" ? 0.08f : 0.2f) + (c.req.slides ? 0.3f : 0.0f);
        p.octave = ! c.legato && (c.req.octaves || ((id == "poppunk" || id == "punk" || id == "metalcore") && c.chorus()));

        // Stabs: where they hit
        if (id == "funk")
        {
            static const int syncopations[] { 3, 6, 7, 10, 11, 14, 15 };

            for (int i = 0, n = 1 + c.pick (2); i < n; ++i)
                if (const int at = syncopations[c.pick (7)]; ! contains (p.stabs, at))
                    p.stabs.push_back (at);
        }
        else if (f == "rock")
        {
            static const std::vector<std::vector<int>> riffs { { 0, 6, 10 }, { 0, 6, 12 }, { 0, 3, 6, 10 }, { 0, 6, 8, 14 }, { 0, 4, 6, 10, 12 } };
            p.stabs = riffs[(size_t) c.pick ((int) riffs.size())];
        }
        else
        {
            static const std::vector<std::vector<int>> calls { { 0, 3 }, { 0, 7 }, { 0, 10 }, { 0, 6 }, { 0 } };
            p.stabs = calls[(size_t) c.pick ((int) calls.size())];
        }

        std::sort (p.stabs.begin(), p.stabs.end());
        p.stabs.erase (std::remove_if (p.stabs.begin(), p.stabs.end(), [&] (int x) { return x >= c.n16; }), p.stabs.end());
        return p;
    }

    std::vector<Notes> sectionFor (Ctx& c, const Plan& p, const std::vector<BarIn>& ins)
    {
        if (c.role == "pad")
            return padSection (c, p, ins);

        if (c.role == "lead")
            return leadSection (c, p, ins);

        std::vector<Notes> bars;

        for (size_t i = 0; i < ins.size(); ++i)
            bars.push_back (c.role == "arp" ? arpBar (c, p, ins[i], (int) i, (int) ins.size()) : stabBar (c, p, ins[i]));

        return bars;
    }

    juce::String describe (const juce::String& role, const juce::String& sound)
    {
        if (role == "pad")      return "a pad";
        if (role == "arp")      return "an arpeggio";
        if (role == "stabs")    return sound.containsIgnoreCase ("brass") ? "horn stabs" : "stabs";
        return sound.containsIgnoreCase ("bell") || sound == "Music Box" ? "a bell melody" : sound == "Soft Flute" ? "a flute line" : "a lead";
    }
}

//==============================================================================
juce::StringArray roles()
{
    return { "pad", "arp", "lead", "stabs" };
}

Ask askOf (const juce::String& words)
{
    const auto t = spaced (words);
    Ask a;

    if (has (t, { "pad", "pads", "chords", "held chords", "atmosphere", "ambient", "ambience", "texture", "swell", "swells" }))
        a.role = "pad";
    else if (has (t, { "arp", "arps", "arpeggio", "arpeggios", "arpeggiated", "arpeggiate", "sequence", "sequenced", "sequencer" }))
        a.role = "arp";
    else if (has (t, { "lead", "leads", "melody", "melodies", "topline", "top line", "solo", "counter melody", "countermelody", "tune" }))
        a.role = "lead";
    else if (has (t, { "stab", "stabs", "hits", "horn hits", "brass hits", "horn section", "chord stabs" }))
        a.role = "stabs";

    static const std::vector<std::pair<std::vector<const char*>, const char*>> sounds {
        { { "music box" },                                              "Music Box" },
        { { "church bell", "church bells" },                            "Church Bell" },
        { { "bell", "bells", "crystal bell", "crystal bells" },         "Crystal Bell" },
        { { "flute", "flutes" },                                        "Soft Flute" },
        { { "choir", "choirs", "voices", "vox" },                       "Glass Choir" },
        { { "strings", "string machine", "string pad" },                "String Machine" },
        { { "rave stab", "90s stab", "orchestra hit", "orch hit" },     "90s Stab" },
        { { "brass", "horn", "horns" },                                 "Synth Brass" },
        { { "supersaw", "super saw", "trance" },                        "Supersaw Lead" },
        { { "glass pluck" },                                            "Glass Pluck" },
        { { "pizz", "pizzicato" },                                      "Pizz Pluck" },
        { { "harp" },                                                   "Harp Pluck" },
        { { "kalimba" },                                                "Kalimba" },
        { { "pluck", "plucks", "plucked" },                             "Future Pluck" },
        { { "moog", "minimoog", "mono lead", "glide lead" },            "Mono Lead" },
        { { "chip", "chiptune", "8 bit", "8-bit", "nintendo" },         "Chip Lead" },
        { { "screamer", "screaming lead", "screaming" },                "Screamer" },
        { { "lofi lead", "lo-fi lead" },                                "Lofi Lead" },
        { { "drone" },                                                  "Space Drone" },
        { { "warm pad", "analog pad", "analogue pad" },                 "Warm Analog Pad" },
        { { "dark pad" },                                               "Dark Matter" },
        { { "aurora", "ambient pad" },                                  "Aurora" },
        { { "breath pad", "breathy", "breathy pad" },                   "Breath Pad" },
        { { "night pad" },                                              "Night Pad" },
        { { "sparkle", "sparkly" },                                     "Arp Sparkle" },
        { { "saw arp", "sequence saw" },                                "Sequence Saw" },
    };

    for (auto& [names, sound] : sounds)
        for (auto* w : names)
            if (has (t, { w }))
            {
                a.sound = sound;
                return a;
            }

    return a;
}

juce::String roleFor (const juce::String& style)
{
    return defaultsFor (style).role;
}

juce::String soundFor (const juce::String& style, const juce::String& role)
{
    const auto& d = defaultsFor (style);
    return role == "pad" ? d.pad : role == "arp" ? d.arp : role == "stabs" ? d.stabs : d.lead;
}

bool fits (const juce::String& sound, const juce::String& role)
{
    const auto* p = nova::findFactory (sound);

    if (p == nullptr)
        return true;

    if (juce::roundToInt (p->get ("voice_mode")) >= 1)
        return role == "lead";

    if (p->get ("amp_attack") >= 0.3f || p->category == "Texture")
        return role == "pad";

    return true;
}

float levelFor (const juce::String& style, const juce::String& role)
{
    // Measured with --bandrender against the style's drums, each job on the style's sound for it: a
    // pad 7 dB under the drums (6 for djent's, deathcore's and drill's), an arpeggio 5, stabs 4 (3
    // for boom bap's and funk's horns), a lead 3 (trap's bells 2.5, jazz's flute 2, metal's and
    // pop-punk's 4, under the guitars). Ballads against jazz's drums: their brushes are too quiet
    static const std::map<juce::String, std::array<float, 4>> faders {     // pad, arp, lead, stabs
            { "metalcore", { -6.8f, -2.8f, -4.7f, -5.2f } },
            { "deathcore", { -7.7f, -5.0f, -6.6f, -7.1f } },
            { "djent",     { -7.3f, -4.6f, -6.2f, -6.7f } },
            { "thrash",    { -6.7f, -3.0f, -4.6f, -5.1f } },
            { "blast",     { -7.8f, -4.1f, -5.7f, -6.2f } },
            { "metal",     { -7.5f, -3.8f, -5.4f, -5.9f } },
            { "trap",      { -13.2f, -2.9f, -2.5f, -4.1f } },
            { "drill",     { -11.3f, -2.0f, -2.1f, -3.2f } },
            { "boombap",   { -14.1f, -3.8f, -3.9f, -4.0f } },
            { "hiphop",    { -8.9f, 1.4f, 1.3f, 0.2f } },
            { "rock",      { -12.2f, -5.9f, -6.2f, -9.9f } },
            { "hardrock",  { -12.6f, -6.2f, -6.5f, -10.2f } },
            { "poppunk",   { -11.5f, -5.1f, -6.4f, -9.1f } },
            { "punk",      { -6.9f, -0.5f, -1.8f, -4.5f } },
            { "lofi",      { -10.2f, -3.9f, -5.5f, -4.0f } },
            { "jazz",      { -10.7f, -4.4f, -4.9f, -4.5f } },
            { "ballad",    { -14.6f, -8.3f, -9.8f, -8.4f } },
            { "neosoul",   { -9.7f, -3.4f, -4.9f, -3.5f } },
            { "funk",      { -5.7f, -0.1f, -0.7f, -3.2f } },
            { "house",     { 0.0f, 0.0f, 0.0f, 0.0f } },
            { "techno",    { 0.0f, 0.0f, 0.0f, 0.0f } },
            { "edm",       { 0.0f, 0.0f, 0.0f, 0.0f } },
            { "dnb",       { 0.0f, 0.0f, 0.0f, 0.0f } },
            { "dubstep",   { 0.0f, 0.0f, 0.0f, 0.0f } },
            { "synthwave", { 0.0f, 0.0f, 0.0f, 0.0f } },
            { "reggae",    { 0.0f, 0.0f, 0.0f, 0.0f } },
            { "reggaeton", { 0.0f, 0.0f, 0.0f, 0.0f } },
            { "afrobeats", { 0.0f, 0.0f, 0.0f, 0.0f } },
            { "bossa",     { 0.0f, 0.0f, 0.0f, 0.0f } },
            { "ambient",   { 0.0f, 0.0f, 0.0f, 0.0f } },
            { "cinematic", { 0.0f, 0.0f, 0.0f, 0.0f } },
            { "country",   { 0.0f, 0.0f, 0.0f, 0.0f } },
    };

    const auto it = faders.find (style);
    const auto& row = it != faders.end() ? it->second : faders.at ("rock");
    const int job = role == "pad" ? 0 : role == "arp" ? 1 : role == "lead" ? 2 : 3;
    return row[(size_t) job];
}

Result write (const juce::String& prompt, const Song& song, const Request* previous, const juce::String& previousRole,
              const juce::String& currentSound, const drummer::Groove* drums, const bass::Part* bassPart, const keys::Part* keysPart,
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

    // The job: named, else the named sound's, else this channel's last, else the style's own
    const auto ask = askOf (prompt);
    juce::String role = ask.role;

    if (role.isEmpty() && ask.sound.isNotEmpty())
        role = impliedRole (ask.sound);

    if (role.isEmpty())
        role = previousRole;

    if (role.isEmpty() || ! roles().contains (role))
        role = roleFor (style->id);

    // The sound: named, else the style's for the job when the channel was made for it, can't
    // play the job, or is one it chose for another style or job
    juce::String sound, swapped;

    if (ask.sound.isNotEmpty())
    {
        sound = ask.sound;

        if (! fits (sound, role))
            role = impliedRole (sound).isNotEmpty() ? impliedRole (sound) : juce::String ("lead");
    }
    else
    {
        const bool chosenBefore = previous != nullptr && currentSound == soundFor (previous->style, previousRole.isNotEmpty() ? previousRole : roleFor (previous->style));
        const bool changed = previous != nullptr && (previous->style != req.style || (previousRole.isNotEmpty() && previousRole != role));

        if (chooseSound || currentSound.isEmpty() || (chosenBefore && changed))
            sound = soundFor (style->id, role);
        else if (! fits (currentSound, role))
        {
            sound = soundFor (style->id, role);
            swapped = currentSound;
        }
    }

    const auto playing = sound.isNotEmpty() ? sound : currentSound;
    const auto& g = result.drums;
    const auto& defaults = defaultsFor (style->id);
    juce::Random rng (seed);
    const auto key = theory::keyFrom (bassLine.key);
    const int beats = g.meterBeats > 0 ? g.meterBeats : song.meterBeats;
    const int unit = g.meterUnit > 0 ? g.meterUnit : song.meterUnit;

    Ctx c { req, *style, rng, key };
    c.role = role;
    c.sound = playing;
    c.legato = isLegato (playing);
    c.n16 = juce::jmax (1, beats * 16 / unit);

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

        chords.push_back (std::move (list));
    }

    juce::StringArray notes;
    std::optional<Plan> songPlan;       // trap's melody is the beat's: the same motif in every section
    const bool oneMotif = c.family() == "trap" && role == "lead";

    for (size_t si = 0; si < g.sections.size(); ++si)
    {
        const auto& ds = g.sections[si];
        c.kind = theory::kindOf (ds.name);
        c.energy = energyOf (c);
        const bool lastSection = si + 1 == g.sections.size();
        const keys::Section* keysSection = nullptr;

        if (keysPart != nullptr)
            for (auto& ks : keysPart->sections)
                if (ks.name == ds.name)
                    keysSection = &ks;

        std::vector<BarIn> ins;

        for (size_t b = 0; b < ds.bars.size(); ++b)
        {
            BarIn in;
            in.steps = ds.bars[b].steps;
            in.index = (int) b;
            in.n16 = c.n16;

            const auto to16 = [&in] (int step) { return juce::roundToInt ((double) step * in.n16 / in.steps); };

            for (auto s : theory::hitsIn (ds.bars[b], { "kick" }))
                in.kicks.push_back (to16 (s));

            for (auto s : theory::hitsIn (ds.bars[b], { "china", "crash", "crash_2" }))
                in.crashes.push_back (to16 (s));

            if (keysSection != nullptr && b < keysSection->bars.size())
                for (auto& n : keysSection->bars[b].notes)
                    if (! n.left && ! n.grace)
                        in.answer.push_back (juce::roundToInt ((double) n.step * in.n16 / std::max (1, keysSection->bars[b].steps)));

            in.chord = chords[si][b];
            in.next = b + 1 < ds.bars.size() ? chords[si][b + 1] : (! lastSection && ! chords[si + 1].empty() ? chords[si + 1].front() : chords[si].front());
            in.phraseEnd = b % 4 == 3 || b + 1 == ds.bars.size();
            in.sectionEnd = b + 1 == ds.bars.size();
            in.songEnd = in.sectionEnd && lastSection;
            ins.push_back (in);
        }

        if (ins.empty())
            continue;

        // How often the keys play on each 16th: in this section's bars, or (for trap's one motif,
        // made now) in all the song's
        c.keysAt.assign ((size_t) c.n16, 0.0f);

        if (keysPart != nullptr)
        {
            int counted = 0;

            auto countBar = [&] (const keys::Bar& kb)
            {
                std::set<int> at;

                for (auto& n : kb.notes)
                    if (! n.left && ! n.grace)
                        at.insert (juce::roundToInt ((double) n.step * c.n16 / std::max (1, kb.steps)));

                for (auto x : at)
                    if (juce::isPositiveAndBelow (x, c.n16))
                        c.keysAt[(size_t) x] += 1.0f;

                ++counted;
            };

            for (auto& ks : keysPart->sections)
                if (ks.name == ds.name || (oneMotif && ! songPlan.has_value()))
                    for (auto& kb : ks.bars)
                        countBar (kb);

            for (auto& x : c.keysAt)
                x /= (float) std::max (1, counted);
        }

        std::vector<Notes> best;
        juce::String bestNote;
        float bestScore = -1.0e9f;
        const auto startVoicing = c.voicing;
        const int startLast = c.last;
        auto bestVoicing = c.voicing;
        int bestLast = c.last;

        Plan bestPlan;

        for (int candidate = 0; candidate < (oneMotif && songPlan.has_value() ? 1 : 20); ++candidate)
        {
            const auto plan = oneMotif && songPlan.has_value() ? *songPlan : makePlan (c);
            c.voicing = startVoicing;
            c.last = startLast;
            c.note = {};
            auto bars = sectionFor (c, plan, ins);
            const float sc = score (bars, ins, c, plan) + rng.nextFloat() * 0.3f;

            if (sc > bestScore)
            {
                bestScore = sc;
                best = std::move (bars);
                bestNote = c.note;
                bestVoicing = c.voicing;
                bestLast = c.last;
                bestPlan = plan;
            }
        }

        if (oneMotif && songPlan.has_value() && bestNote != "laying out")
            bestNote = "the same melody, following the chords";

        if (oneMotif && ! songPlan.has_value())
            songPlan = bestPlan;

        c.voicing = bestVoicing;
        c.last = bestLast;
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
    part.role = role;
    part.sound = sound;
    part.swing = g.swing;
    part.swingSixteenths = g.swingSixteenths;
    part.looseMs = defaults.loose;
    part.pushMs = defaults.push;

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
        part.levelDb = levelFor (style->id, role);

    const double tempo = part.tempo > 0 ? part.tempo : (g.tempo > 0 ? g.tempo : song.bpm);
    part.title = juce::String (style->name) + " synth " + role + ", " + part.key;

    juce::String reply = juce::String (style->name) + " synth in " + part.key + " at " + juce::String (juce::roundToInt (tempo)) + " bpm: "
                         + describe (role, playing) + (playing.isNotEmpty() ? " on " + playing : juce::String())
                         + (swapped.isNotEmpty() ? " (" + swapped + " can't play " + describe (role, playing) + ")" : juce::String()) + ". ";
    reply << notes.joinIntoString (" ");

    if (result.imaginedDrums || result.imaginedBass)
        reply << " No " << (result.imaginedDrums && result.imaginedBass ? "drums or bass" : result.imaginedDrums ? "drums" : "bass")
              << " yet, so it played to what a " << juce::String (style->name).toLowerCase() << " band would: ask the AI "
              << (result.imaginedBass ? "bassist" : "drummer") << " and the synth follows theirs.";

    part.message = reply;
    result.reply = reply;
    result.request = req;
    return result;
}

SynthTurn turnFor (const juce::String& prompt, const Result& r)
{
    SynthTurn turn;
    turn.prompt = prompt.trim();
    turn.reply = r.reply;
    turn.part = juce::JSON::toString (r.part.toJson(), true);

    auto* answer = new juce::DynamicObject();
    answer->setProperty ("offline", true);
    answer->setProperty ("request", r.request.toVar());
    answer->setProperty ("role", r.part.role);
    turn.answer = juce::JSON::toString (juce::var (answer), true);
    return turn;
}

std::optional<agents::Request> requestOf (const SynthTurn& turn)
{
    const auto v = juce::JSON::parse (turn.answer);

    if (! (bool) v.getProperty ("offline", false))
        return {};

    return Request::fromVar (v.getProperty ("request", {}));
}

} // namespace nightshift::synth::offline
