// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** Check for updates (the ... menu): asks GitHub for NightShift's latest release and, when it's newer than this
    one, downloads this computer's installer, checks it's the very file GitHub has (its size and SHA-256), and
    starts it. Only when asked: NightShift never looks by itself, and all it sends is the request for the release. */
namespace nightshift::updates
{
    /** Where the releases are (michaeldedmondson-design/nightshift until the user renamed the account,
        2026-11-16: GitHub answers an old name with a redirect, and the check follows it). */
    inline const juce::String repository { "ExfilBravo/nightshift" };

    /** This NightShift's version, as its About says it. */
    juce::String currentVersion();

    struct Release
    {
        juce::String version;               // "0.4.2": the tag, without its "v"
        juce::String title;                 // "NightShift 0.4.2"
        juce::StringArray whatsNew;         // the release notes' "What's new" points, their headlines in plain words
        juce::String page;                  // the release's page on GitHub
        juce::String installer;             // this computer's installer's file name ("" when the release has none)
        juce::String installerUrl;
        juce::int64 installerBytes = 0;
        juce::String sha256;                // lower-case hex, when GitHub gives it
    };

    /** -1, 0 or 1 as version a is older than, the same as or newer than b, number by number: "0.4.10" comes
        after "0.4.9", "1.0" is "1.0.0", and a leading "v" doesn't count. */
    int compare (const juce::String& a, const juce::String& b);

    /** Whether a release file is this computer's installer: NightShift-Setup-<version>.exe on Windows,
        NightShift-<version>-mac.pkg on a Mac. */
    bool isInstallerForThisComputer (const juce::String& fileName);

    /** The release in GitHub's answer for the latest release (its JSON). Returns what's wrong, or "". */
    juce::String parse (const juce::String& json, Release& out);

    /** Asks GitHub for the latest release. Blocks: call it off the message thread. Returns what went wrong
        (in words for the person who asked), or "". */
    juce::String fetchLatest (Release& out);

    /** Where downloaded installers wait: "NightShift update" in the temp folder. */
    juce::File downloadFolder();

    /** Downloads the release's installer into folder (carrying on from a download cut short) and checks it's
        the file GitHub has, its size and its SHA-256, deleting it if not. Blocks. Returns what went wrong, or "". */
    juce::String download (const Release&, const juce::File& folder, juce::File& installer,
                           std::function<void (double)> progress = {}, std::function<bool()> shouldStop = {});

    /** Whether a file is the release's installer: its size, and its SHA-256 when GitHub gave one. */
    bool isTheInstaller (const Release&, const juce::File&);

    /** A file's SHA-256, lower-case hex ("" if it can't be read). */
    juce::String sha256Of (const juce::File&);

    /** The command line the Windows installer is started with: quietly (a window with its progress and no
        questions), closing NightShift if it's still open and opening it again when it's done (the setup's
        [Run] does that when given /relaunch=yes). */
    juce::String windowsInstallerArguments();

    /** Starts the installer, on Windows as windowsInstallerArguments() says, on a Mac in Installer (its own
        steps; it asks for the password). The studio quits right after. False if it couldn't be started. */
    bool startInstaller (const juce::File& installer);
}
