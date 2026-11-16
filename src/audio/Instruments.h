// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** The sampled instruments Nightshift plays (SFZ, in SfzPlugin::folder()): what's installed,
    named and grouped for the browser, and the free libraries it fetches: Salamander Grand Piano
    (Alexander Holm, CC BY 3.0) and VSCO 2 Community Edition (Versilian Studios, CC0). */
namespace nightshift::instruments
{
    struct Entry
    {
        juce::String path;          // its SFZ, under SfzPlugin::folder()
        juce::String name;          // "Violin Section, Sustain Vibrato"
        juce::String category;      // "Strings", "Keys"...
    };

    /** Every instrument installed, grouped by category then named. */
    std::vector<Entry> list();

    /** An instrument's name from its path ("" if it isn't one). */
    juce::String displayName (const juce::String& path);

    /** "ViolinEnsSusVib" -> "Violin Section, Sustain Vibrato". */
    juce::String nameFromFile (const juce::String& fileName);

    struct Library
    {
        juce::String id, name, url, credit;
        int megabytes = 0;
        bool flacToWav = false;     // its samples are FLAC: made WAV once, to play from disk
    };

    /** What Get the piano and orchestra fetches (tests swap in their own). */
    std::vector<Library>& libraries();
    bool haveLibraries();

    /** Downloads the libraries that aren't here (carrying on from a part), unpacks each (its
        instruments and samples only), turns FLAC into WAV where asked, and deletes the zip.
        Blocks; returns what went wrong, or "". */
    juce::String fetch (std::function<void (float, const juce::String&)> progress, std::function<bool()> shouldStop);
}
