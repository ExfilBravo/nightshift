// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include <map>
#include <memory>

/** The sample browser away from the screen: the folders the sidebar's Samples lists (the user's Downloads and Music,
    then folders they've added), every audio file under each (found on a thread of its own, as a tree of folders),
    and how long a file plays. */
namespace nightshift::samples
{
    /** The audio files NightShift plays: what the sampler and an audio track take (WAV, AIFF, FLAC, Ogg, MP3). */
    bool isAudioFile (const juce::File&);

    /** The user's Downloads and Music, where they are (on Windows Downloads where Explorer has it, which may have
        been moved; a Mac's ~/Downloads and ~/Music), those that are there. */
    juce::Array<juce::File> standardFolders();

    /** Tests and snapshots list folders of their own in their place (empty: the real ones). */
    extern juce::Array<juce::File> standardFoldersOverride;

    /** NightShift's own folders of sounds, listed after Downloads and Music when there's something in them: the free
        packs ("Free packs"), the drum kits ("Drum kits") and the sampled instruments ("Piano and orchestra"), each with
        its credit. Command-line runs leave them out unless a test asks (ownFoldersShown). */
    struct OwnFolder
    {
        juce::File folder;
        juce::String name, credit;
    };

    std::vector<OwnFolder> ownFolders();
    extern bool ownFoldersShown;

    /** A folder as the list names it: Downloads and Music by those names (whatever the system calls them), any
        other by its own name. */
    juce::String displayName (const juce::File& folder);

    /** How long an audio file plays, in seconds: 0 when it can't be read. */
    double lengthInSeconds (const juce::File&);

    /** A folder as the browser shows it: the folders in it with audio somewhere under them, then its audio files,
        each A to Z (numbers in order: 2 before 10). `count` is every audio file under it. */
    struct Folder
    {
        juce::String name;
        std::vector<std::unique_ptr<Folder>> folders;
        juce::StringArray files;                    // names, with their extensions
        int count = 0;

        /** A folder under this one by its path from here ("Drum Pack/Kicks"), or null. */
        const Folder* find (const juce::String& relativePath) const;
    };

    /** Every audio file under some folders, found on a thread of its own (only when asked: nothing is read until a
        folder is looked through). Looking again keeps what it had until the new look is done. onChanged comes on the
        message thread when a look finishes. */
    class Library : private juce::Thread,
                    private juce::AsyncUpdater
    {
    public:
        Library();
        ~Library() override;

        /** Looks through these folders when they haven't been, or not in the last `maxAgeSeconds` (0: now, again). */
        void lookThrough (const juce::Array<juce::File>& folders, double maxAgeSeconds = 0.0);

        /** What's known of a folder now: null until its first look is done. */
        std::shared_ptr<const Folder> get (const juce::File& folder) const;

        /** It's waiting to be looked through, or being looked through now. */
        bool isLooking (const juce::File& folder) const;

        /** Files under a folder whose names hold `text` (any case): at most `limit` of them (its own files first, then
            each folder's, A to Z), with `total` how many there are. `relative` is the path from the folder. */
        struct Match { juce::String name, relative; };
        std::vector<Match> search (const juce::File& folder, const juce::String& text, int limit, int& total) const;

        /** Waits until nothing's being looked through, up to `ms` (tests and snapshots). False if it's still looking. */
        bool waitUntilDone (int ms) const;

        std::function<void()> onChanged;

        /** What a look takes at most from one folder, so a huge one can't stall it: entries read, audio files kept,
            folders deep. */
        static constexpr int maxEntries = 200000, maxFiles = 50000, maxDepth = 12;

    private:
        mutable juce::CriticalSection lock;
        std::map<juce::String, std::shared_ptr<const Folder>> found;     // by full path
        std::map<juce::String, double> lookedAt;                          // when each was last looked through (ms)
        juce::StringArray queue;                                          // waiting to be looked through
        juce::String current;                                             // being looked through now
        juce::WaitableEvent work;

        void run() override;
        void handleAsyncUpdate() override;
        void look (const juce::File& dir, Folder& into, int depth, int& entries, int& files);
    };
}
