// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** Studio drum kits from DrumGizmo (drumgizmo.org, CC BY 4.0): every hit of every drum recorded
    on all the kit's mics at once. Nightshift mixes each hit down to stereo once, after the
    download, and keeps the kit as SFZ: velocity layers from the hits' loudness, a few alternates
    in each, the hi-hats choking each other. */
namespace nightshift::drumkits::drumgizmo
{
    /** One Nightshift kit made from a DrumGizmo kit. */
    struct Recipe
    {
        juce::String id;                    // the kit's folder in drumkits::folder() ("CROCELL_KIT")
        juce::String name, description;     // how it's shown, and what it suits (for the drummer)
        juce::String kitFile;               // the DrumGizmo kit inside the zip ("CrocellKit/CrocellKit_full.xml")

        /** Our drums (drumkits::voiceNames()) and the DrumGizmo instruments that play them; several
            joined by | share the hits (a double kick's two drums). */
        std::vector<std::pair<juce::String, juce::String>> instruments;

        int maxHits = 24;                   // per drum, spread across its dynamics
    };

    /** Converts a downloaded DrumGizmo kit's zip into `destination` (a kit folder, made or
        replaced), telling `progress` how far it's got (0..1). Blocks. Returns what went wrong,
        or "". */
    juce::String convert (const juce::File& zip, const Recipe&, const juce::File& destination,
                          std::function<void (float)> progress, std::function<bool()> shouldStop);
}
