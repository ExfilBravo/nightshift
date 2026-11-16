// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"

/** The AI bassist's part: sections of bars of notes (each with its chord), which the offline
    bassist (OfflineBass.h) writes locked to the drummer, and Nightshift plays into patterns you
    can edit, with the drums' swing, a push of its own and a little looseness. A slide runs the
    note before on under it, so a legato synth glides into it. */
namespace nightshift::bass
{
    struct Note
    {
        int step = 0, length = 1;       // in the bar's steps (a note may run on past the bar line)
        int pitch = 36;                 // MIDI
        int velocity = 100;
        bool slide = false;             // glides in from the note before
        bool ghost = false;             // a dead note: muted and short, felt more than heard
    };

    struct Bar
    {
        int steps = 16;
        juce::String chord;             // the chord over it, by name ("Cm7")
        std::vector<Note> notes;
    };

    struct Section
    {
        juce::String name;
        std::vector<Bar> bars;
    };

    struct Part
    {
        juce::String title, message;
        juce::String key;               // "C minor", for the reply and the tab
        juce::String sound;             // the sound it chose (a Nova patch's name), or empty to keep the channel's
        juce::var patch;                // that sound's settings (Nova patch JSON)
        int tempo = 0;                  // a tempo to set (asked for, or a blank song's), else 0
        int meterBeats = 0, meterUnit = 0;
        float swing = 0.0f;             // the drums' swing, so they sit together
        bool swingSixteenths = false;
        float looseMs = 0.0f, pushMs = 0.0f;
        std::vector<Section> sections;
        juce::StringArray arrangement;  // section names in song order

        juce::var toJson() const;

        /** Empty if it isn't a part at all. */
        static std::optional<Part> fromJson (const juce::var&);
    };

    /** Plays a part into patterns for the channel, one per section (made or reused), named after
        the channel and section. Sets the tempo and meter first if the part has them. */
    void render (Song&, int channelId, const Part&);

    /** Lays the sections out in Arrange: under the drummer's clips of the same sections where
        they are in Arrange, else one after another from the start; on a free row either way. */
    void placeInArrange (Song&, int channelId, const Part&);

    /** The part the channel's bassist shows, if any. */
    std::optional<Part> shownPart (const Channel&);

    /** Plays the shown version into its patterns again. False if nothing is shown. */
    bool rerender (Song&, int channelId);

    /** A kit channel whose drummer has written sections, the one the bassist follows: the
        selected one if it's a kit, else the first. 0 if there's none. */
    int drummerToFollow (const Song&);
}
