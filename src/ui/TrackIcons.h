// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"

/** The pictures on Arrange's track headers: drums for a drum track, a guitar for a guitar
    track and so on. A track shows the one picked for it, else one chosen from what it plays. */
namespace nightshift::trackicons
{
    struct Icon
    {
        const char* id;             // saved in the project ("drums")
        const char* name;           // shown in the menu ("Drums")
    };

    /** Every icon, in the order the menu lists them. */
    const std::vector<Icon>& all();
    bool isIcon (const juce::String& id);
    juce::String nameOf (const juce::String& id);

    /** Line art on a 24-unit square, to be stroked (about 1.6 units at 24 px). */
    juce::Path path (const juce::String& id);

    /** The icon a channel suggests: its kind of instrument, then words in its name ("Bass
        Guitar", "Violin Section"). */
    juce::String forChannel (const Channel&);

    /** The icon a track shows: its own if one was picked, else automaticFor. */
    juce::String forTrack (const Song&, int row);

    /** The icon what a track plays suggests: its channel's, drums for a pattern of drums
        only, else the pattern blocks. */
    juce::String automaticFor (const Song&, int row);

    /** Strokes an icon to fill `area`, in `colour`. */
    void draw (juce::Graphics&, const juce::String& id, juce::Rectangle<float> area, juce::Colour colour);
}
