// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Packs.h"
#include "Kits.h"
#include "Instruments.h"
#include "model/Paths.h"

namespace nightshift::packs
{

juce::File folderOverride;

namespace
{
    // NightShift Sounds 1, made by `Nightshift --makesounds <folder>` (README) and put on GitHub as the release
    // sounds-v1: the zip as made, its size and SHA-256
    constexpr const char* soundsUrl = "https://github.com/ExfilBravo/nightshift/releases/download/sounds-v1/NightShift-Sounds-1.zip";
    constexpr const char* soundsSha256 = "31d8a0aef321d4b401e3ef4dc10d33d1aabb7db2cd4ce8c6dc87518c9f8086c3";
    constexpr juce::int64 soundsBytes = 45124258;

    juce::String sha256Of (const juce::File& file)
    {
        juce::FileInputStream in (file);
        return in.openedOk() ? juce::SHA256 (in).toHexString().toLowerCase() : juce::String();
    }
}

std::vector<Pack>& available()
{
    static std::vector<Pack> list
    {
        { "NightShift Sounds 1", "NightShift Sounds 1",
          "Drum hits from NightShift's own 808 and 909, notes from its Nova synth, and loops the AI band played in many styles, "
          "each named with its tempo and key so it follows your song.",
          "CC0: free for anything, no credit needed. Made by NightShift.", soundsUrl, soundsSha256, soundsBytes, {} },
        { "Studio drum kits", "Studio drum kits",
          "Three real drum kits recorded with many microphones, from heavy rock to brushed jazz, each a channel the AI drummer can play.",
          "CC BY 4.0: credit \"Drum samples provided by DrumGizmo.org\".", {}, {}, (juce::int64) 10479 * 1048576, "kits" },
        { "Piano and orchestra", "Piano and orchestra",
          "A grand piano and a whole orchestra (strings, brass, woodwinds and percussion) to play as instruments.",
          "Salamander Grand Piano by Alexander Holm (CC BY 3.0); Versilian Studios Chamber Orchestra 2 Community Edition (CC0).",
          {}, {}, (juce::int64) 2970 * 1048576, "instruments" },
    };

    return list;
}

juce::File folder()
{
    return folderOverride != juce::File() ? folderOverride : paths::localData().getChildFile ("Packs");
}

bool isInstalled (const Pack& p)
{
    if (p.library == "kits")
        return drumkits::haveDownloads();

    if (p.library == "instruments")
        return instruments::haveLibraries();

    const auto dir = folder().getChildFile (p.id);
    return dir.isDirectory() && ! dir.findChildFiles (juce::File::findFiles, true).isEmpty();
}

bool anyInstalled()
{
    for (auto& p : available())
        if (p.library.isEmpty() && isInstalled (p))
            return true;

    // (or one a test or an older list put there)
    for (auto& dir : folder().findChildFiles (juce::File::findDirectories, false))
        if (! dir.getFileName().startsWithChar ('.'))
            return true;

    return false;
}

juce::String install (const Pack& p, std::function<void (double)> progress, std::function<bool()> shouldStop)
{
    if (p.library.isNotEmpty() || p.url.isEmpty())
        return p.name + " comes NightShift's own way, not as a zip.";

    const auto zip = folder().getChildFile (".downloads").getChildFile (juce::File::createLegalFileName (p.id) + ".zip");

    if (! zip.getParentDirectory().createDirectory())
        return "NightShift couldn't make " + folder().getFullPathName() + ".";

    if (auto problem = drumkits::downloadFile (p.url, zip, progress, shouldStop, p.bytes); problem.isNotEmpty())
        return problem;

    if (shouldStop != nullptr && shouldStop())
        return "Stopped.";

    // The very file that was made, or none of it
    if ((p.bytes > 0 && zip.getSize() != p.bytes) || sha256Of (zip) != p.sha256.toLowerCase())
    {
        zip.deleteFile();
        return "The " + p.name + " download isn't the pack as it was made (it may have been damaged on the way), so NightShift deleted it. Try again.";
    }

    auto problem = unpack (zip, p);
    zip.deleteFile();
    return problem;
}

juce::String unpack (const juce::File& zipFile, const Pack& p)
{
    juce::ZipFile zip (zipFile);

    if (zip.getNumEntries() == 0)
        return "The " + p.name + " download isn't a zip NightShift can open. Try again.";

    // Out into a folder of its own first, so a pack is never found half there
    const auto staging = folder().getChildFile (".unpacking");
    staging.deleteRecursively();
    const auto into = staging.getChildFile (p.id);
    static const juce::StringArray kinds { "wav", "flac", "aif", "aiff", "ogg", "mp3", "txt", "md" };
    int files = 0;

    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto name = zip.getEntry (i)->filename.replaceCharacter ('\\', '/');
        const auto inner = name.fromFirstOccurrenceOf (p.id + "/", false, false);

        // Only what's inside its own top folder, and nothing that would climb out of it
        if (! name.startsWith (p.id + "/") || inner.isEmpty() || inner.endsWithChar ('/') || inner.contains ("..") || inner.containsChar (':')
             || inner.startsWithChar ('/') || ! kinds.contains (inner.fromLastOccurrenceOf (".", false, false).toLowerCase()))
            continue;

        const auto target = into.getChildFile (inner);

        if (! target.isAChildOf (into) || ! target.getParentDirectory().createDirectory())
            continue;

        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
        bool written = false;
        {
            juce::FileOutputStream out (target);
            written = in != nullptr && out.openedOk() && out.writeFromInputStream (*in, -1) >= 0;
            out.flush();
        }

        if (! written)
        {
            staging.deleteRecursively();
            return "NightShift couldn't unpack " + p.name + " into " + folder().getFullPathName() + ".";
        }

        ++files;
    }

    if (files == 0)
    {
        staging.deleteRecursively();
        return "The " + p.name + " download has no sounds in it.";
    }

    const auto destination = folder().getChildFile (p.id);
    destination.deleteRecursively();
    const bool moved = into.moveFileTo (destination);
    staging.deleteRecursively();
    return moved ? juce::String() : "NightShift couldn't put " + p.name + " in " + folder().getFullPathName() + ".";
}

juce::String sizeText (juce::int64 bytes)
{
    const double mb = (double) bytes / 1048576.0;
    return mb >= 1000.0 ? juce::String (mb / 1024.0, 1) + " GB" : juce::String (juce::jmax (1, juce::roundToInt (mb))) + " MB";
}

} // namespace nightshift::packs
