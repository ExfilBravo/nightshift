// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "model/Song.h"

/** What a new project can start from, on the start screen: a track per part, each with its
    instrument (or an audio track to record into), at the tempo the style is usually played. */
namespace nightshift::templates
{
    struct Template
    {
        juce::String id, name, description;
        juce::StringArray icons;        // its tracks' icons, top to bottom (see trackicons)
        double bpm = 120.0;
    };

    /** In the order the start screen shows them; "empty" first. */
    const std::vector<Template>& all();

    /** Makes the song a new, unsaved project from a template (a blank one for an id it
        doesn't know). Drum tracks use the kit the style suits if it's here (studio kits for a
        band, the 808 for beats, the 909 for electronic), else the first kit there is. */
    void build (const juce::String& id, Song&, AudioEngine&);
}
