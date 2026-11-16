// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** The band's ears, learned by listening to itself: every style the band plays, rendered as a whole song a few times
    and heard by the vibe listener (tools/vibes/learn_ears.js), its measures averaged. The listener holds a song up
    against them to say which style it's nearest. StyleEars.cpp is generated from StyleEars.cpp.in in tools/vibes: run
    the tool again when the band's playing changes, rather than editing it. */
namespace nightshift::vibes::ears
{
    struct Ear
    {
        const char* style;              // an id of agents::styles()
        int heard;                      // how many renders it's from
        float pattern[48];              // the drums heard: kick, snare and hats on each 16th of a bar (0..1)
        float tempo;                    // bpm, as heard
        float swing;                    // 0 straight .. 1 a full triplet
        float highShare;                // the power over 2 kHz against all of it over 200 Hz (brightness)
        float heldFlatness;             // how flat the held sound's 1 to 6 kHz is (grit)
        float lowShare;                 // the power under 120 Hz against all of it (low end)
        float minorShare;               // of its bars' chords, how many minor
        float seventhShare;             // ... sevenths
        float powerShare;               // ... power chords
        float minorKeys;                // how many of its renders were heard in a minor key
    };

    const std::vector<Ear>& all();
    const Ear* find (const juce::String& style);
}
