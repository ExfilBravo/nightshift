// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** A loop's tempo, for an audio file dropped into a song: so a 120 BPM drum loop in a 90 BPM song can
    follow the song's tempo (Song::addAudioFile). Found, in this order, in what the file says about itself
    (a WAV's acid chunk, an AIFF's Apple Loops chunk: its beats over its length), in its name ("120bpm",
    "120 BPM", "loop_120_Cmin"), or in its length when that's whole bars of 4/4 at a whole-number tempo
    between 70 and 180 BPM (a loop is cut to its bars). Anything else isn't known: it plays as it is. */
namespace nightshift::looptempo
{
    struct Found
    {
        double seconds = 0.0;           // how long the file plays
        double bpm = 0.0;               // its tempo in quarter notes a minute, or 0 if it isn't known
        double beats = 0.0;             // how many beats it lasts, when its tempo is known
        juce::String how;               // where the tempo came from: "file", "name" or "length"
        bool oneShot = false;           // the file says it's a single hit, not a loop (no tempo then)
    };

    /** Reads the file's header (not its audio). An unreadable file has no seconds. */
    Found find (const juce::File&);

    /** A tempo written in a name, between 40 and 300, or 0: "Drums 120bpm", "128 BPM house", "loop_95_Am". */
    double fromName (const juce::String& name);

    /** The tempo making `seconds` whole bars of 4/4 at a whole-number BPM from 70 to 180 (within a millisecond
        of it), nearest 120 if several do, or 0. `beatsOut` gets its beats. */
    double fromLength (double seconds, double* beatsOut = nullptr);
}
