// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"

/** The AI guitarist's part: sections of bars of notes, each note with the string and fret it's
    played on (so it reads as tab) and how it's played (palm-muted, a dead scratch, a slide or a
    hammer-on into it, the strum's direction). The offline guitarist (OfflineGuitar.h) writes it
    over the bassist's chords, locked to the drummer; Nightshift plays it into patterns with the
    strum spread across the strings, and the guitar's keyswitches for palm mutes and scratches. */
namespace nightshift::guitar
{
    /** The guitar's keyswitches: held while notes start, they change how those notes are played. */
    inline constexpr int palmMuteKey = 12, scratchKey = 13;

    struct Note
    {
        int step = 0, length = 1;       // in the bar's steps
        int pitch = 40;                 // MIDI
        int velocity = 100;
        int string = -1, fret = -1;     // 0 is the lowest string; -1 until it's fingered
        bool palmMute = false;          // P.M.: damped at the bridge, a chug
        bool muted = false;             // a dead note, a scratch: the strings muted by the fretting hand
        bool slide = false;             // slid into from the note before on its string
        bool hammer = false;            // hammered on (or pulled off) from the note before on its string
        bool up = false;                // an upstroke (strummed from the high string down)
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
    };

    struct Part
    {
        juce::String title, message;
        juce::String key;               // "C minor"
        std::vector<int> tuning;        // each string's open note, lowest first (6 or 7 strings)
        juce::String sound;             // the guitar sound (preset) it chose, or empty to keep the channel's
        int tempo = 0;                  // a tempo to set (asked for, or a blank song's), else 0
        int meterBeats = 0, meterUnit = 0;
        float swing = 0.0f;             // the drums' swing
        bool swingSixteenths = false;
        float looseMs = 0.0f, pushMs = 0.0f;
        float strumMs = 6.0f;           // between one string and the next in a strummed chord
        float levelDb = 0.0f;           // where its fader goes (dB from unity) when it chose the sound: its place in the style's mix
        std::vector<Section> sections;
        juce::StringArray arrangement;

        juce::var toJson() const;
        static std::optional<Part> fromJson (const juce::var&);
    };

    /** A tuning's name ("Drop C", "Standard", "7-string B standard") and its notes ("C G C F A D"). */
    juce::String tuningName (const std::vector<int>& tuning);
    juce::String tuningNotes (const std::vector<int>& tuning);

    /** Gives every note a string and fret (keeping any already set that can be played), a chord
        across strings in order, the hand staying near where it was. Notes that can't be played on
        this tuning are moved by octaves until they can. */
    void finger (std::vector<Note>& notes, const std::vector<int>& tuning, int& handFret);

    /** Plays a part into patterns for the channel, one per section (made or reused), named after
        the channel and section: chords strummed across the strings (down or up), palm mutes and
        scratches on their keyswitches, slides and hammer-ons running the note before on under them. */
    void render (Song&, int channelId, const Part&);

    /** Lays the sections out in Arrange under the drummer's (or the bassist's) clips of the same
        sections, else one after another from the start; on a free row. */
    void placeInArrange (Song&, int channelId, const Part&);

    std::optional<Part> shownPart (const Channel&);
    bool rerender (Song&, int channelId);
}
