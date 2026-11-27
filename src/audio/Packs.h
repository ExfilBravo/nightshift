// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** Free sample packs, offered in the sidebar's Samples (its card the first time, its Free sample packs... after):
    zips NightShift downloads only when asked, checks against the size and SHA-256 written here (as Check for updates
    checks its installer) and unpacks into its own data folder (never Documents or Music, which Windows' ransomware
    protection may keep it out of), where Samples lists them as Free packs. Beside them in the same list, the libraries
    NightShift fetches its own way (the studio drum kits, the piano and orchestra), each with its credit. */
namespace nightshift::packs
{
    struct Pack
    {
        juce::String id;                    // its folder in folder() ("NightShift Sounds 1"), also the zip's top folder
        juce::String name, about;           // what it's called, and what's in it
        juce::String licence;               // its licence and who to credit, in plain words
        juce::String url, sha256;           // the zip and its SHA-256 (lower-case hex)
        juce::int64 bytes = 0;              // the zip's size
        juce::String library;               // "": a zip; "kits" or "instruments": fetched NightShift's own way
    };

    /** What the list offers: NightShift Sounds 1, then the studio drum kits and the piano and orchestra. Tests swap
        in their own. */
    std::vector<Pack>& available();

    /** Where packs go: NightShift's own data folder (Windows: %LOCALAPPDATA%\Nightshift\Packs; a Mac: Application
        Support/Nightshift/Packs). Tests set their own. */
    juce::File folder();
    extern juce::File folderOverride;

    /** It's here: its folder (a zip pack), or its library's sounds. */
    bool isInstalled (const Pack&);

    /** Any zip pack is here (Samples lists folder() as Free packs). */
    bool anyInstalled();

    /** Downloads a zip pack (drumkits::downloadFile: carrying on from a part already there when the server can),
        checks its size and SHA-256 (a file that isn't the one made is deleted, and said), unpacks it into folder()
        and deletes the zip. Blocks: off the message thread. Returns what went wrong, or "". */
    juce::String install (const Pack&, std::function<void (double)> progress, std::function<bool()> shouldStop);

    /** A pack's zip into folder()/<its id>: only what's inside its own top folder (sounds, text), never anywhere
        else, put in place once it's all out. Returns what went wrong, or "". */
    juce::String unpack (const juce::File& zip, const Pack&);

    /** "120 MB", "10.5 GB". */
    juce::String sizeText (juce::int64 bytes);
}
