// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** Makes NightShift Sounds 1, the free sample pack the sidebar's Samples offers (CC0): only NightShift's own sounds,
    no one else's samples. `Nightshift --makesounds <folder> [--only drums|synths|loops]` writes the pack's folder and
    its zip there, and says the zip's name, size and SHA-256 (the numbers Packs.cpp checks a download against).

    - Drums: NightShift's synthesised 808 and 909 (every drum, its three layers and two takes) and its built-in kit.
    - Synths: a note or two of every Nova factory sound, named with its note ("Deep Sub Bass C1").
    - Loops: the AI band in a dozen and more styles, each part on its own (drums, bass, keys, guitar, synth), four
      bars cut from its chorus at bar lines, named with the tempo and key ("Funk bass 102bpm E minor") so a loop
      dropped in a song follows its tempo (looptempo::find). The drums play NightShift's own 808 or 909, the keys
      Nova sounds (never the sampled piano), the guitar NightShift Guitar's modelled strings.
    FLAC, 48 kHz, 16-bit. One render at a time, in this process. */
namespace nightshift::soundsmaker
{
    struct Made
    {
        juce::File zip;
        juce::int64 bytes = 0;
        juce::String sha256;
        int files = 0;
    };

    /** Returns what went wrong, or ""; `say` hears each step. */
    juce::String make (const juce::File& folder, const juce::String& only, std::function<void (const juce::String&)> say, Made& made);

    /** The pack's folder name (and the zip's top folder) and the zip's name. */
    inline constexpr const char* packName = "NightShift Sounds 1";
    inline constexpr const char* zipName = "NightShift-Sounds-1.zip";
}
