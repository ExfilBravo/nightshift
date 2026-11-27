// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "Drummer.h"

/** What the agents that play over the bassist's chords share: a chord from its name, the key and
    its scale, where the beats fall in a bar, and the drummer's hits in a bar. */
namespace nightshift::theory
{
    struct Chord
    {
        int root = 0;                       // pitch class
        std::vector<int> tones { 0, 4, 7 }; // above the root, in semitones
        juce::String name;
        bool rich = false;                  // "soul chords": its seventh and ninth voiced wherever it's played

        bool has (int interval) const       { return std::find (tones.begin(), tones.end(), interval) != tones.end(); }
        int third() const                   { return has (3) ? 3 : has (4) ? 4 : -1; }
        int seventh() const                 { return has (10) ? 10 : has (11) ? 11 : -1; }
        bool minor() const                  { return has (3); }
        bool operator== (const Chord& o) const { return root == o.root && tones == o.tones; }
    };

    /** "Cm", "Bbmaj7", "E9", "Ddim", "C5" as the bassist names them. */
    Chord parseChord (const juce::String& name);

    struct Key
    {
        int root = 4;
        juce::String mode = "minor";
        std::vector<int> scale { 0, 2, 3, 5, 7, 8, 10 };

        bool inScale (int pitch) const;

        /** The next note of the scale up (or down) from `pitch`. */
        int step (int pitch, int direction) const;
    };

    std::vector<int> scaleOf (const juce::String& mode);

    /** "C minor", "E dorian, tuned to E". */
    Key keyFrom (const juce::String& text);

    /** The lowest pitch at or above `from` with this pitch class. */
    inline int atOrAbove (int pc, int from)    { return from + ((((pc - from) % 12) + 12) % 12); }

    /** How a bar's beats group, in 16ths, as the band counts it: a quarter note's four for each beat
        of 4/4 (and 3/4, 5/4...), 6/8 and 9/8 in dotted quarters, the odd eighths and sixteenths as
        the metronome groups them first (7/8 in 2+2+3, 5/8 in 3+2, 11/8 in 3+3+3+2), so the drums
        lean where the click does. */
    std::vector<int> meterGroups (int beats, int unit);

    /** Where each counted beat starts in a bar of `steps` (4/4 in four, 7/8 in 2+2+3 eighths...). */
    std::vector<int> beatStarts (int steps, int beats, int unit);

    /** The steps a drum is hit on in a bar of the drummer's (ghost notes left out unless asked). */
    std::vector<int> hitsIn (const drummer::Bar&, std::initializer_list<const char*> voices, bool ghostsToo = false);

    /** A chord voiced from the last: of all its inversions (closed, within lo..hi), the one whose
        notes move least from `previous`; with nothing before (or a different count of notes), the
        one nearest `centre`. Empty if none fits the range. */
    std::vector<int> led (const std::vector<int>& intervals, int rootPc, const std::vector<int>& previous, int centre, int lo, int hi);

    /** A section's kind from its name: "Head" plays as a verse, "Solos" as a chorus. */
    juce::String kindOf (const juce::String& sectionName);
}
