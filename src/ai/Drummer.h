// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"

/** The AI drummer's part: step grids (sections of bars of drum lanes, one character per step
    for how hard), which the offline drummer (OfflineDrummer.h) writes and Nightshift plays like
    a drummer would: swing, each drum's push or pull, a little looseness, all into patterns you
    can edit. */
namespace nightshift::drummer
{
    struct Lane
    {
        juce::String voice;             // one of drumkits::voiceNames()
        juce::String hits;              // a character per step: '.' rest, '1'-'9' how hard, 'g' ghost, 'f' flam, 'r' drag
    };

    struct Bar
    {
        int steps = 16;
        std::vector<Lane> lanes;
    };

    struct Section
    {
        juce::String name;
        std::vector<Bar> bars;
    };

    struct Groove
    {
        juce::String title, message;
        juce::String kit;               // the kit it chose, by name ("" on a General MIDI channel)
        int tempo = 0;                  // a tempo the user asked for, else 0
        int meterBeats = 0, meterUnit = 0;
        float swing = 0.0f;             // 0 straight .. 1 full triplet swing
        bool swingSixteenths = false;   // swing the 16ths rather than the 8ths
        float looseMs = 0.0f;           // how far hits wander from the grid
        std::map<juce::String, float> pushMs;   // per drum: early (-) or late (+), in ms
        juce::String learned;           // the feel learned from real drummers it plays (LearnedFeel.h), by id, or ""
        std::vector<Section> sections;
        juce::StringArray arrangement;  // section names in song order

        /** From the drummer's JSON; lanes padded or cut to their bar's steps. Empty if it
            isn't a groove at all. */
        static std::optional<Groove> fromJson (const juce::var&);
    };

    /** The kits a channel's drummer can name: every kit for a kit channel, none for General MIDI. */
    juce::StringArray kitNamesFor (const Channel&);

    /** Velocity for a hit character, before dynamics: 1 -> 20, 9 -> 127, g -> 28. */
    int velocityFor (juce::juce_wchar);

    /** Plays a groove into patterns for the channel (one per section, made or reused), with
        the drummer's feel controls. `keyFor` maps a voice to a note (-1: the kit lacks it).
        Changes the song's time signature or tempo first if the groove asks. */
    void render (Song&, int channelId, const Groove&, std::function<int (const juce::String&)> keyFor);

    /** Lays the groove's sections out in Arrange on a free row, in the arrangement's order. */
    void placeInArrange (Song&, int channelId, const Groove&);

    /** The note each drum plays on a channel: its kit's key, or General MIDI's on a drum plugin. */
    std::function<int (const juce::String&)> keyMapFor (const Channel&);

    /** The drums the channel can play, to tell the drummer. */
    juce::StringArray voicesFor (const Channel&);

    /** Plays the shown version into its patterns again (after the kit or a feel control
        changed). Returns false if nothing is shown. */
    bool rerender (Song&, int channelId);
}
