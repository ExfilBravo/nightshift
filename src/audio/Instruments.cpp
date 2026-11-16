// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Instruments.h"
#include "Kits.h"
#include "Sfz.h"
#include "GuitarSound.h"
#include "ZipReader.h"
#include <mutex>

namespace nightshift::instruments
{

namespace
{
    /** "ViolinEnsSusVib" -> Violin, Ens, Sus, Vib; "SViolin" -> S, Violin. */
    juce::StringArray camelWords (const juce::String& s)
    {
        juce::StringArray words;
        juce::String current;

        for (int i = 0; i < s.length(); ++i)
        {
            const auto c = s[i];
            const bool upper = juce::CharacterFunctions::isUpperCase (c);
            const bool nextLower = i + 1 < s.length() && juce::CharacterFunctions::isLowerCase (s[i + 1]);

            // A capital starts a word, unless it's in a run of capitals that isn't ending ("NV", "KS")
            if (upper && current.isNotEmpty() && (juce::CharacterFunctions::isLowerCase (current.getLastCharacter()) || nextLower))
            {
                words.add (current);
                current.clear();
            }

            if (juce::CharacterFunctions::isLetterOrDigit (c))
                current << c;
            else if (current.isNotEmpty())
            {
                words.add (current);
                current.clear();
            }
        }

        if (current.isNotEmpty())
            words.add (current);

        return words;
    }

    struct Cached
    {
        juce::Time stamp;
        std::vector<Entry> entries;
    };
}

juce::String nameFromFile (const juce::String& fileName)
{
    static const std::map<juce::String, juce::String> special {
        { "GM-StylePerc", "General MIDI Percussion" }, { "VSUpright1", "Upright Piano 2" }, { "Salamander Grand Piano V3", "Grand Piano" } };

    if (auto it = special.find (fileName); it != special.end())
        return it->second;

    static const std::map<juce::String, juce::String> playing {
        { "Sus", "Sustain" }, { "Stac", "Staccato" }, { "Spic", "Spiccato" }, { "Trem", "Tremolo" }, { "Pizz", "Pizzicato" },
        { "Vib", "Vibrato" }, { "VB", "Vibrato" }, { "NV", "No Vibrato" }, { "Exp", "Expressive" }, { "KS", "All Articulations" },
        { "Quiet", "Soft" }, { "Loud", "Loud" }, { "Pedal", "Pedals" }, { "Rolls", "Rolls" }, { "Mute", "Muted" },
        { "Harmon", "Harmon" }, { "Straight", "Straight" } };

    juce::StringArray instrument, how;
    bool section = false;

    for (auto& part : juce::StringArray::fromTokens (fileName, "-_ ", ""))
        for (auto& w : camelWords (part))
        {
            if (w == "Ens")                         section = true;
            else if (playing.count (w) > 0)         how.add (playing.at (w));
            else if (how.isEmpty())                 instrument.add (w);
            else                                    how.add (w);
        }

    auto name = instrument.joinIntoString (" ").replace ("S Violin", "Solo Violin").replace ("F Horn", "French Horn");

    if (name.startsWith ("S ") && instrument.size() > 1)
        name = "Solo " + name.substring (2);

    if (section)
        name << " Section";

    return how.isEmpty() ? name : name + ", " + how.joinIntoString (" ");
}

std::vector<Entry> list()
{
    static std::mutex mutex;
    static Cached cache;
    const std::lock_guard<std::mutex> l (mutex);

    // Each library's folder holds its SFZs at the top (their samples in folders below)
    const auto root = SfzPlugin::folder();
    juce::Time newest;

    for (auto& library : root.findChildFiles (juce::File::findDirectories, false))
        newest = std::max (newest, library.getLastModificationTime());

    if (newest == cache.stamp && ! cache.entries.empty())
        return cache.entries;

    std::vector<Entry> entries;

    for (auto& library : root.findChildFiles (juce::File::findDirectories, false))
    {
        if (library.getFileName().startsWithChar ('.'))
            continue;

        for (auto& file : library.findChildFiles (juce::File::findFiles, false, "*.sfz"))
        {
            // The category from where its samples are ("Strings\Violin Section\..."), else the library's
            juce::String category = library.getFileName().containsIgnoreCase ("piano") ? "Keys" : "Instruments";
            juce::FileInputStream in (file);

            for (int line = 0; line < 40 && ! in.isExhausted(); ++line)
                if (const auto text = in.readNextLine().trim(); text.startsWith ("default_path="))
                {
                    if (const auto first = text.fromFirstOccurrenceOf ("=", false, false).replaceCharacter ('\\', '/').upToFirstOccurrenceOf ("/", false, false).trim();
                        juce::StringArray { "Strings", "Brass", "Woodwinds", "Keys", "Percussion" }.contains (first))
                        category = first;

                    break;
                }

            entries.push_back ({ library.getFileName() + "/" + file.getFileName(), nameFromFile (file.getFileNameWithoutExtension()), category });
        }
    }

    static const juce::StringArray order { "Keys", "Strings", "Brass", "Woodwinds", "Percussion", "Instruments" };
    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
    {
        const int ca = order.indexOf (a.category), cb = order.indexOf (b.category);
        return ca != cb ? ca < cb : a.name.compareNatural (b.name) < 0;
    });

    cache = { newest, entries };
    return entries;
}

juce::String displayName (const juce::String& path)
{
    // Nightshift Guitar: "Guitar, High Gain"
    if (guitarsound::isGuitarPath (path))
        return "Guitar, " + guitarsound::presetOfPath (path);

    for (auto& e : list())
        if (e.path == path)
            return e.name;

    return path.isEmpty() ? juce::String() : nameFromFile (juce::File::createFileWithoutCheckingPath (path).getFileNameWithoutExtension());
}

std::vector<Library>& libraries()
{
    static std::vector<Library> all {
        { "SalamanderGrandPiano", "Salamander Grand Piano", "https://codeload.github.com/sfzinstruments/SalamanderGrandPiano/zip/refs/heads/master",
          "Salamander Grand Piano by Alexander Holm (CC BY 3.0)", 714, true },
        { "VSCO-2-CE", "VSCO 2 orchestra", "https://codeload.github.com/sgossner/VSCO-2-CE/zip/refs/tags/1.1.0",
          "VSCO 2 Community Edition by Versilian Studios (CC0)", 2200, false } };
    return all;
}

bool haveLibraries()
{
    for (auto& library : libraries())
        if (! SfzPlugin::folder().getChildFile (library.id).isDirectory())
            return false;

    return true;
}

juce::String fetch (std::function<void (float, const juce::String&)> progress, std::function<bool()> shouldStop)
{
    std::vector<const Library*> wanted;
    double total = 0.0, done = 0.0;

    for (auto& library : libraries())
        if (! SfzPlugin::folder().getChildFile (library.id).isDirectory())
        {
            wanted.push_back (&library);
            total += library.megabytes;
        }

    if (wanted.empty())
        return {};

    const auto root = SfzPlugin::folder();
    const auto downloads = drumkits::downloadsFolder();

    if (! root.createDirectory() || ! downloads.createDirectory())
        return "Couldn't make " + root.getFullPathName() + ".";

    auto tell = [&] (double megabytes, const juce::String& what) { if (progress != nullptr) progress ((float) (megabytes / total), what); };

    for (auto* library : wanted)
    {
        const double size = library->megabytes;
        const auto zip = downloads.getChildFile (library->id + ".zip");
        tell (done, "Downloading the " + library->name);

        if (auto error = drumkits::downloadFile (library->url, zip, [&] (double f) { tell (done + f * size * 0.6, "Downloading the " + library->name); },
                                                 shouldStop, (juce::int64) library->megabytes * 1048576);
            error.isNotEmpty())
            return error;

        // Unpacked into a folder of its own: its instruments, their samples and includes, nothing else
        ZipReader reader (zip);

        if (! reader.isOpen())
        {
            zip.deleteFile();
            return "The " + library->name + " download isn't a zip. Try again.";
        }

        const auto staging = root.getChildFile (".unpacking");
        staging.deleteRecursively();
        juce::StringArray flacs;
        static const juce::StringArray kinds { "sfz", "sfzh", "txt", "wav", "flac", "ogg", "aif", "aiff" };

        for (int i = 0; i < reader.getNumEntries(); ++i)
        {
            if (shouldStop != nullptr && shouldStop())
                return "Stopped.";

            // Without the zip's top folder ("VSCO-2-CE-1.1.0/")
            const auto name = reader.getName (i).fromFirstOccurrenceOf ("/", false, false);
            const auto type = name.fromLastOccurrenceOf (".", false, false).toLowerCase();

            if (name.isEmpty() || name.endsWithChar ('/') || name.contains ("..") || name.containsChar (':') || name.startsWithChar ('/')
                 || ! kinds.contains (type))
                continue;

            const auto target = staging.getChildFile (name);
            target.getParentDirectory().createDirectory();
            auto in = reader.open (i);
            bool written = false;

            {
                juce::FileOutputStream out (target);
                written = in != nullptr && out.openedOk() && out.writeFromInputStream (*in, -1) >= 0;
                out.flush();
                written = written && out.getStatus().wasOk();
            }

            if (! written)
            {
                staging.deleteRecursively();
                return "Couldn't unpack the " + library->name + " into " + root.getFullPathName() + ".";
            }

            if (type == "flac")
                flacs.add (target.getFullPathName());

            if (i % 50 == 0)
                tell (done + size * (0.6 + 0.2 * i / reader.getNumEntries()), "Unpacking the " + library->name);
        }

        // FLAC made WAV once, so the samples play straight from disk
        if (library->flacToWav)
        {
            juce::FlacAudioFormat flac;
            juce::WavAudioFormat wav;

            for (int i = 0; i < flacs.size(); ++i)
            {
                if (shouldStop != nullptr && shouldStop())
                    return "Stopped.";

                const juce::File source (flacs[i]);
                std::unique_ptr<juce::AudioFormatReader> in (flac.createReaderFor (source.createInputStream().release(), true));

                if (in == nullptr)
                    continue;

                const auto target = source.withFileExtension ("wav");
                target.deleteFile();
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (target);
                auto out = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                            .withSampleRate (in->sampleRate)
                                                            .withNumChannels ((int) in->numChannels)
                                                            .withBitsPerSample (24));

                if (out == nullptr || ! out->writeFromAudioReader (*in, 0, -1))
                {
                    staging.deleteRecursively();
                    return "Couldn't turn the " + library->name + "'s samples into WAV (is the disk full?).";
                }

                out.reset();
                source.deleteFile();

                if (i % 20 == 0)
                    tell (done + size * (0.8 + 0.2 * i / flacs.size()), "Getting the " + library->name + " ready");
            }
        }

        staging.getChildFile ("library.txt").replaceWithText (library->name + "\n" + library->credit + "\n");
        const auto destination = root.getChildFile (library->id);
        destination.deleteRecursively();

        if (! staging.moveFileTo (destination))
        {
            staging.deleteRecursively();
            return "Couldn't put the " + library->name + " in " + root.getFullPathName() + ".";
        }

        zip.deleteFile();
        done += size;
    }

    tell (total, "Done");
    return {};
}

} // namespace nightshift::instruments
