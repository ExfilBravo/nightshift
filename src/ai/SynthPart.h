// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"

/** The AI synth player's part: sections of bars of notes on a Nova sound - a lead's melody, a
    pad's chords, an arpeggio, a bell's motif, brass stabs - which the offline synth player
    (OfflineSynth.h) writes over the bassist's chords and the drummer's kick. Nightshift plays it
    into patterns with the drums' swing; a slide runs the note before on under it, so a legato
    lead glides into it. */
namespace nightshift::synth
{
    struct Note
    {
        int step = 0, length = 1;
        int pitch = 72;
        int velocity = 96;
        bool slide = false;             // glides in from the note before
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
        juce::String role;              // "lead", "pad", "arp", "bells", "stabs"
        juce::String sound;             // the Nova sound it chose, or empty to keep the channel's
        float levelDb = 0.0f;           // where its fader goes (dB from unity) when it chose the sound
        int tempo = 0;
        int meterBeats = 0, meterUnit = 0;
        float swing = 0.0f;
        bool swingSixteenths = false;
        float looseMs = 0.0f, pushMs = 0.0f;
        std::vector<Section> sections;
        juce::StringArray arrangement;

        juce::var toJson() const;
        static std::optional<Part> fromJson (const juce::var&);
    };

    /** Plays a part into patterns for the channel, one per section (made or reused). */
    void render (Song&, int channelId, const Part&);

    /** Under the band's clips of the same sections in Arrange, else one after another; on a free row. */
    void placeInArrange (Song&, int channelId, const Part&);

    std::optional<Part> shownPart (const Channel&);
    bool rerender (Song&, int channelId);

    /** A Nova sound that plays one note at a time and glides (Mono Lead, Screamer, Soft Flute...). */
    bool isLegato (const juce::String& soundName);
}
