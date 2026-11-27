// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include "Song.h"

/** The song's key, heard from its notes: what pitch correction pulls a voice to unless it's told
    another. Every melodic note in the song's patterns counts by how long it sounds (drums and the
    one-shot samples of a beat don't), and the twelve totals are held against Krumhansl and Kessler's
    profiles of each major and minor key; the closest is the key. */
namespace nightshift::songkey
{
    struct Key
    {
        int root = -1;                  // pitch class, C = 0; -1 when there are too few notes to tell
        bool minor = false;

        bool known() const              { return root >= 0; }

        /** "A minor", "E flat major"; "not known" when it isn't. */
        juce::String name() const;
        bool operator== (const Key&) const = default;
    };

    Key find (const Song&);

    /** The scales pitch correction offers, in its Scale menu's order, and their names. */
    enum Scale { major, minor, harmonicMinor, majorPentatonic, minorPentatonic, numScales };
    juce::String scaleName (int scale);

    /** Which of the twelve pitch classes are in a scale on a root (all twelve for root -1). */
    std::array<bool, 12> notesOf (int root, int scale);

    /** A pitch class's name as the menus write it: C, C sharp, D... ("C#" short). */
    juce::String noteName (int pitchClass, bool shortName = false);
}
