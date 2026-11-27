// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"

/** The AI keys player's part: sections of bars of notes for the left and right hands, each bar
    with its chord, which the offline keys player (OfflineKeys.h) writes over the bassist's chords
    and the drummer's kick, and Nightshift plays into patterns: chords rolled up the way a player
    does in lofi and ballads, the drums' swing, a push of its own. */
namespace nightshift::keys
{
    struct Note
    {
        int step = 0, length = 1;       // in the bar's steps (a note may ring on past the bar line)
        int pitch = 60;
        int velocity = 90;
        bool left = false;              // the left hand (roots and shells); else the right
        bool grace = false;             // a grace note slid off into the next (neo-soul's half step below)
    };

    struct Bar
    {
        int steps = 16;
        juce::String chord;
        std::vector<Note> notes;
    };

    struct Section
    {
        juce::String name;
        std::vector<Bar> bars;
        int meterBeats = 0, meterUnit = 0;      // its own time signature (the drummer's section's), else 0: the part's
    };

    struct Part
    {
        juce::String title, message;
        juce::String key;
        juce::String sound;             // the sound it chose (a Nova keys sound, or "Grand Piano"), empty to keep the channel's
        juce::String sampled;           // the sampled instrument's path when the sound is one
        float levelDb = 0.0f;           // where its fader goes (dB from unity) when it chose the sound
        int tempo = 0;
        int meterBeats = 0, meterUnit = 0;
        float swing = 0.0f;
        bool swingSixteenths = false;
        float looseMs = 0.0f, pushMs = 0.0f;
        float rollMs = 0.0f;            // a chord's notes, low to high, this far apart (0: struck together)
        std::vector<Section> sections;
        juce::StringArray arrangement;

        juce::var toJson() const;
        static std::optional<Part> fromJson (const juce::var&);
    };

    /** Plays a part into patterns for the channel, one per section (made or reused). */
    void render (Song&, int channelId, const Part&);

    /** Under the drummer's (or the bassist's, or the guitarist's) clips of the same sections in
        Arrange, else one after another from the start; on a free row. */
    void placeInArrange (Song&, int channelId, const Part&);

    std::optional<Part> shownPart (const Channel&);
    bool rerender (Song&, int channelId);

    /** A pitch's name: 60 is "C4". */
    juce::String noteName (int pitch);
}
