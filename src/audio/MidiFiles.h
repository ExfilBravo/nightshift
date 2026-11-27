// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"

/** Standard MIDI files (.mid): the song's notes out to one, and one's tracks in as channels. They
    hold notes, the tempo and the time signature, and track and program names; JUCE's MidiFile
    reads and writes the bytes. */
namespace nightshift::midifile
{
    //==============================================================================
    /** A note as the song plays it: its channel, where it starts and ends in ticks from the song's
        start (swing on, cut at its clip's end, as the engine plays it). */
    struct PlayedNote
    {
        int channelId = 0;
        double start = 0.0, end = 0.0;
        int key = 60, velocity = 100;
    };

    /** Every note the song plays: Song mode's clips (`patternId` 0) or one pattern's notes as it
        loops in Pattern mode, in time order. `heardOnly`: leaves out muted channels and tracks. */
    std::vector<PlayedNote> notesAsPlayed (const Song&, int patternId, bool heardOnly);

    //==============================================================================
    /** One part of a file: a track's notes on one MIDI channel, in ticks from the song's start. */
    struct Part
    {
        juce::String name;              // the track's name, else its program's, else "Track 3"
        int midiChannel = 1;            // 1..16; 10 is General MIDI's drums
        int program = -1;               // the program it asks for (0..127), or -1
        std::vector<Note> notes;        // in time order

        bool isDrums() const            { return midiChannel == 10; }
    };

    /** What a file holds, in NightShift's terms: the tempo and time signature at the start and
        where they change, and the parts. */
    struct Contents
    {
        double bpm = 120.0;
        int meterBeats = 4, meterUnit = 4;
        std::vector<TempoChange> tempoChanges;
        std::vector<MeterChange> meterChanges;
        std::vector<Part> parts;
        juce::StringArray warnings;     // what didn't come across as it was ("3/2 plays as 4/4")
        int lengthTicks = 0;            // where its last note ends
    };

    /** Reads a .mid (type 0 or 1, timed in beats or in frames). Nothing if it isn't one, with
        `error` saying why in plain words. */
    std::optional<Contents> read (const juce::File&, juce::String& error);
    std::optional<Contents> read (juce::InputStream&, juce::String& error);

    /** General MIDI's name for a program, 0..127 ("Acoustic Grand Piano"). */
    juce::String programName (int program);

    /** A time signature NightShift has for one a file gives: the same, else one with bars as
        long, else one whose bar holds a whole number of them, else 4/4. */
    std::pair<int, int> nearestMeter (int beats, int unit);

    //==============================================================================
    /** The song's notes as it plays (`patternId` 0: Song mode's arrangement, tempo and time
        signature changes included), or one pattern's at the start's tempo in its own time
        signature, as a type 1 file: a tempo track, then a track per channel with notes, named
        after it, on a MIDI channel of its own (drums on 10, a synth plugged in on its own), its
        program first when it has one. Muted channels and tracks are left out, as an export of
        the sound leaves them out. A glide is written as a tempo a 16th. */
    juce::MidiFile write (const Song&, int patternId = 0);
    bool save (const Song&, const juce::File&, int patternId = 0);

    /** The MIDI channel each channel's notes go out on in a written file. */
    std::map<int, int> midiChannelsFor (const Song&, const std::vector<PlayedNote>&);

    //==============================================================================
    /** How a part comes in: the instrument a channel gets for it (a drum kit for drums, Nightshift
        Guitar for a guitar, a NightShift sound for anything else, by its program). */
    struct Sound
    {
        InstrumentType type = InstrumentType::synth;
        juce::String synthPreset, kit, instrument;
    };

    Sound soundFor (const Part&);

    /** Brings a file's parts into the song: a channel each on a sound that suits it, its notes in
        a pattern placed in Arrange on a track of its own from the bar its first note is in (from
        `atTick`, the bar it's dropped on). With `takeTimeline` the song takes the file's tempo and
        time signature, and their changes (from the start: `atTick` is then 0). Song mode, the
        first new channel selected. Returns the new channels' ids; the caller checkpoints. */
    std::vector<int> addToSong (Song&, const Contents&, bool takeTimeline, int atTick = 0);
}
