// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Samples.h"
#include "Packs.h"
#include "Kits.h"
#include "Sfz.h"

namespace nightshift::samples
{

juce::Array<juce::File> standardFoldersOverride;
bool ownFoldersShown = true;

std::vector<OwnFolder> ownFolders()
{
    std::vector<OwnFolder> found;

    if (! ownFoldersShown)
        return found;

    auto add = [&found] (const juce::File& dir, const juce::String& name, const juce::String& credit)
    {
        if (dir.isDirectory() && ! dir.findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false).isEmpty())
            found.push_back ({ dir, name, credit });
    };

    add (packs::folder(), "Free packs", "Free sample packs NightShift downloaded for you, each with its licence inside");
    add (drumkits::folder(), "Drum kits", "NightShift's own 808 and 909; the studio kits: drum samples provided by DrumGizmo.org (CC BY 4.0); "
                                          "AVL Drumkits by Glen MacArthur (CC BY-SA 3.0)");
    add (SfzPlugin::folder(), "Piano and orchestra", "Salamander Grand Piano by Alexander Holm (CC BY 3.0); Versilian Studios Chamber "
                                                     "Orchestra 2 Community Edition (CC0)");
    return found;
}

bool isAudioFile (const juce::File& f)
{
    // (a Mac's zips leave "._Kick.wav" beside each file: Finder's details of it, not audio)
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3") && ! f.getFileName().startsWith ("._");
}

namespace
{
    juce::File downloadsFolder()
    {
       #if JUCE_WINDOWS
        // Explorer keeps where Downloads is (it can be moved to another drive) under the folder's own id
        const auto path = juce::WindowsRegistry::getValue ("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
                                                           "Shell Folders\\{374DE290-123F-4565-9164-39C4925E467B}");

        if (juce::File::isAbsolutePath (path) && juce::File (path).isDirectory())
            return juce::File (path);
       #endif

        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
    }

    juce::File musicFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    }

    // Folders that are really files to the system (a Mac's apps, projects and libraries, plugins), and ones that only
    // hold other software's workings or a zip's leftovers
    bool skipFolder (const juce::File& dir)
    {
        static const juce::StringArray packages { ".app", ".band", ".logicx", ".musiclibrary", ".photoslibrary", ".bundle", ".component",
                                                  ".vst", ".vst3", ".aaxplugin", ".framework", ".plugin", ".pkg", ".mpkg" };
        const auto name = dir.getFileName();

        return packages.contains (dir.getFileExtension(), true) || name == "__MACOSX" || name == "node_modules" || name.startsWithChar ('.')
                 || dir.isSymbolicLink();
    }

    juce::String withoutExtension (const juce::String& name)
    {
        return name.containsChar ('.') ? name.upToLastOccurrenceOf (".", false, false) : name;
    }
}

juce::Array<juce::File> standardFolders()
{
    if (! standardFoldersOverride.isEmpty())
        return standardFoldersOverride;

    juce::Array<juce::File> folders;

    for (auto& f : { downloadsFolder(), musicFolder() })
        if (f.isDirectory() && ! folders.contains (f))
            folders.add (f);

    return folders;
}

juce::String displayName (const juce::File& folder)
{
    for (auto& own : ownFolders())
        if (own.folder == folder)
            return own.name;

    if (standardFoldersOverride.isEmpty())
    {
        if (folder == downloadsFolder())    return "Downloads";
        if (folder == musicFolder())        return "Music";
    }

    // (a drive of its own has no name but its path)
    return folder.getFileName().isNotEmpty() ? folder.getFileName() : folder.getFullPathName();
}

double lengthInSeconds (const juce::File& f)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    return reader != nullptr && reader->sampleRate > 0.0 ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;
}

const Folder* Folder::find (const juce::String& relativePath) const
{
    const Folder* at = this;

    for (auto& part : juce::StringArray::fromTokens (relativePath, "/\\", {}))
    {
        if (part.isEmpty())
            continue;

        const Folder* next = nullptr;

        for (auto& f : at->folders)
            if (f->name == part)
                next = f.get();

        if (next == nullptr)
            return nullptr;

        at = next;
    }

    return at;
}

//==============================================================================
Library::Library() : juce::Thread ("Samples") {}

Library::~Library()
{
    signalThreadShouldExit();
    work.signal();
    stopThread (4000);
    cancelPendingUpdate();
}

void Library::lookThrough (const juce::Array<juce::File>& folders, double maxAgeSeconds)
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    {
        const juce::ScopedLock sl (lock);

        for (auto& f : folders)
        {
            const auto path = f.getFullPathName();
            const auto at = lookedAt.find (path);
            const bool recent = maxAgeSeconds > 0.0 && at != lookedAt.end() && now - at->second < maxAgeSeconds * 1000.0;

            if (! recent && ! queue.contains (path) && current != path)
                queue.add (path);
        }

        if (queue.isEmpty())
            return;
    }

    // At a low priority: it's for the list, not the sound
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);

    work.signal();
}

std::shared_ptr<const Folder> Library::get (const juce::File& folder) const
{
    const juce::ScopedLock sl (lock);
    const auto it = found.find (folder.getFullPathName());
    return it != found.end() ? it->second : nullptr;
}

bool Library::isLooking (const juce::File& folder) const
{
    const juce::ScopedLock sl (lock);
    return queue.contains (folder.getFullPathName()) || current == folder.getFullPathName();
}

std::vector<Library::Match> Library::search (const juce::File& folder, const juce::String& text, int limit, int& total) const
{
    std::vector<Match> matches;
    total = 0;
    const auto tree = get (folder);

    if (tree == nullptr || text.isEmpty())
        return matches;

    // Its own files first, then each folder's, as the browser lists them
    std::function<void (const Folder&, const juce::String&)> walk = [&] (const Folder& f, const juce::String& path)
    {
        for (auto& name : f.files)
        {
            if (! withoutExtension (name).containsIgnoreCase (text))
                continue;

            if ((int) matches.size() < limit)
                matches.push_back ({ name, path + name });

            ++total;
        }

        for (auto& sub : f.folders)
            walk (*sub, path + sub->name + "/");
    };

    walk (*tree, {});
    return matches;
}

bool Library::waitUntilDone (int ms) const
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) ms;

    for (;;)
    {
        {
            const juce::ScopedLock sl (lock);

            if (queue.isEmpty() && current.isEmpty())
                return true;
        }

        if (juce::Time::getMillisecondCounter() > until)
            return false;

        juce::Thread::sleep (5);
    }
}

void Library::run()
{
    while (! threadShouldExit())
    {
        juce::String next;

        {
            const juce::ScopedLock sl (lock);

            if (! queue.isEmpty())
            {
                next = queue[0];
                queue.remove (0);
                current = next;
            }
        }

        if (next.isEmpty())
        {
            work.wait (-1);
            continue;
        }

        auto tree = std::make_shared<Folder>();
        const juce::File dir (next);
        tree->name = dir.getFileName();
        int entries = 0, files = 0;

        if (dir.isDirectory())
            look (dir, *tree, 0, entries, files);

        if (threadShouldExit())
            break;

        {
            const juce::ScopedLock sl (lock);
            found[next] = std::move (tree);
            lookedAt[next] = juce::Time::getMillisecondCounterHiRes();
            current = {};
        }

        triggerAsyncUpdate();
    }
}

void Library::look (const juce::File& dir, Folder& into, int depth, int& entries, int& files)
{
    std::vector<std::unique_ptr<Folder>> folders;
    juce::StringArray names;

    for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
    {
        if (threadShouldExit() || ++entries > maxEntries)
            break;

        const auto f = entry.getFile();

        if (entry.isDirectory())
        {
            if (depth < maxDepth && ! skipFolder (f))
            {
                auto sub = std::make_unique<Folder>();
                sub->name = f.getFileName();
                look (f, *sub, depth + 1, entries, files);

                // Only folders with sounds somewhere in them
                if (sub->count > 0)
                    folders.push_back (std::move (sub));
            }
        }
        else if (files < maxFiles && isAudioFile (f))
        {
            names.add (f.getFileName());
            ++files;
        }
    }

    // A to Z, any case, numbers in order ("Kick 2" before "Kick 10"), by the names shown ("Tone" before "Tone 2": the
    // extension only between two of the same name)
    std::sort (folders.begin(), folders.end(), [] (const auto& a, const auto& b) { return a->name.compareNatural (b->name) < 0; });
    std::sort (names.begin(), names.end(), [] (const juce::String& a, const juce::String& b)
    {
        const int byName = withoutExtension (a).compareNatural (withoutExtension (b));
        return byName != 0 ? byName < 0 : a.compareNatural (b) < 0;
    });

    into.count = names.size();

    for (auto& f : folders)
        into.count += f->count;

    into.folders = std::move (folders);
    into.files = std::move (names);
}

void Library::handleAsyncUpdate()
{
    if (onChanged != nullptr)
        onChanged();
}

} // namespace nightshift::samples
