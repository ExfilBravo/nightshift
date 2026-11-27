// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "SoundsMaker.h"
#include "audio/AudioEngine.h"
#include "audio/DrumKit.h"
#include "audio/GuitarSound.h"
#include "audio/Kits.h"
#include "audio/Machines.h"
#include "audio/Nova.h"
#include "audio/Sfz.h"
#include "ai/Drummer.h"
#include "ai/OfflineDrummer.h"
#include "ai/OfflineBass.h"
#include "ai/OfflineGuitar.h"
#include "ai/OfflineKeys.h"
#include "ai/OfflineSynth.h"
#include "model/Song.h"

namespace nightshift::soundsmaker
{

namespace
{
    constexpr double rate = 48000.0;

    // A buffer's stretch as FLAC, 16-bit (what's made here is all NightShift's own: nothing else goes in)
    bool writeFlac (const juce::AudioBuffer<float>& b, int start, int length, const juce::File& f)
    {
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
        auto writer = flac.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (b.getNumChannels())
                                                        .withBitsPerSample (16).withQualityOptionIndex (5));
        return writer != nullptr && writer->writeFromAudioSampleBuffer (b, start, length);
    }

    juce::AudioBuffer<float> readAll (const juce::File& f, double* fileRate = nullptr)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        juce::AudioBuffer<float> b;

        if (reader != nullptr)
        {
            b.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&b, 0, b.getNumSamples(), 0, true, true);

            if (fileRate != nullptr)
                *fileRate = reader->sampleRate;
        }

        return b;
    }

    // Where a one-shot's sound has died away (under -66 dB), and a short fade there
    int trimmedLength (juce::AudioBuffer<float>& b)
    {
        int last = 0;

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = b.getNumSamples(); --i > last;)
                if (std::abs (b.getSample (ch, i)) > 0.0005f)
                {
                    last = i;
                    break;
                }

        const int length = std::min (b.getNumSamples(), last + 1 + (int) (0.02 * rate));
        const int fade = std::min (length, (int) (0.01 * rate));
        b.applyGainRamp (length - fade, fade, 1.0f, 0.0f);
        return length;
    }

    // "kick_1a" -> "Kick 1a"
    juce::String pretty (const juce::String& raw)
    {
        auto s = raw.replaceCharacter ('_', ' ').trim();
        return s.substring (0, 1).toUpperCase() + s.substring (1);
    }

    double rmsDb (const juce::AudioBuffer<float>& b, int start, int length)
    {
        double sum = 0.0;

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = start; i < start + length && i < b.getNumSamples(); ++i)
                sum += (double) b.getSample (ch, i) * b.getSample (ch, i);

        return 10.0 * std::log10 (sum / std::max (1, length * b.getNumChannels()) + 1.0e-20);
    }

    // The styles whose loops it makes: the words the band reads, the name in the files, NightShift's machine for the drums
    struct LoopStyle { const char* words; const char* name; const char* kit; };

    const LoopStyle loopStyles[] = {
        { "funk", "Funk", "NS_909" }, { "neo soul", "Neo-soul", "NS_808" }, { "lofi", "Lofi", "NS_808" }, { "boom bap", "Boom bap", "NS_808" },
        { "trap", "Trap", "NS_808" }, { "house", "House", "NS_909" }, { "techno", "Techno", "NS_909" }, { "drum and bass", "Drum and bass", "NS_909" },
        { "synthwave", "Synthwave", "NS_808" }, { "reggae", "Reggae", "NS_909" }, { "reggaeton", "Reggaeton", "NS_808" },
        { "afrobeats", "Afrobeats", "NS_808" }, { "bossa nova", "Bossa nova", "NS_909" }, { "rock", "Rock", "NS_909" },
        { "pop punk", "Pop punk", "NS_909" }, { "country", "Country", "NS_909" }
    };
}

juce::String make (const juce::File& folder, const juce::String& only, std::function<void (const juce::String&)> say, Made& made)
{
    auto tell = [&say] (const juce::String& s) { if (say != nullptr) say (s); };
    const auto pack = folder.getChildFile (packName);
    const auto work = folder.getChildFile ("work");
    work.deleteRecursively();

    if (! pack.createDirectory() || ! work.createDirectory())
        return "Couldn't make " + pack.getFullPathName();

    // Only NightShift's own: its machines in a kit folder of their own, and no sampled instruments
    drumkits::folderOverride = work.getChildFile ("kits");
    SfzPlugin::folderOverride = work.getChildFile ("no instruments");
    SfzPlugin::folderOverride.createDirectory();

    if (auto problem = drumkits::machines::ensure(); problem.isNotEmpty())
        return problem;

    //==============================================================================
    if (only.isEmpty() || only == "drums")
    {
        for (auto& id : drumkits::machines::ids())
        {
            const auto machine = id.fromFirstOccurrenceOf ("NS_", false, false);

            for (auto& wav : drumkits::folder().getChildFile (id).getChildFile ("samples").findChildFiles (juce::File::findFiles, false, "*.wav"))
            {
                double fileRate = rate;
                auto b = readAll (wav, &fileRate);

                if (b.getNumSamples() > 0 && std::abs (fileRate - rate) < 1.0)
                    writeFlac (b, 0, trimmedLength (b), pack.getChildFile ("Drums/" + machine + "/" + machine + " " + pretty (wav.getFileNameWithoutExtension()) + ".flac"));
            }

            tell ("Drums: the " + machine);
        }

        const auto kitDir = work.getChildFile ("kit");

        if (kit::writeTo (kitDir))
            for (auto& wav : kitDir.findChildFiles (juce::File::findFiles, false, "*.wav"))
            {
                double fileRate = rate;
                auto b = readAll (wav, &fileRate);

                if (b.getNumSamples() > 0 && std::abs (fileRate - rate) < 1.0)
                    writeFlac (b, 0, trimmedLength (b), pack.getChildFile ("Drums/NightShift kit/" + pretty (wav.getFileNameWithoutExtension()) + ".flac"));
            }

        tell ("Drums: the NightShift kit");
    }

    //==============================================================================
    if (only.isEmpty() || only == "synths")
    {
        Song song;
        song.loadBlank();
        song.bpm = 120.0;
        const int synthId = song.addChannel ("Synth", InstrumentType::synth).id;
        song.tracks[0].channelId = synthId;
        auto* pattern = song.findPattern (song.currentPatternId);
        song.playlist.clear();
        song.playlist.push_back ({ pattern->id, 0, 0 });
        song.mode = PlayMode::song;
        song.findChannel (synthId)->synthPreset = nova::factory().front().name;
        song.notify (Song::Change::everything);
        AudioEngine audio (song, false);
        audio.syncNow();
        const auto wav = work.getChildFile ("note.wav");

        for (auto& preset : nova::factory())
        {
            const auto category = preset.category.isNotEmpty() ? preset.category : juce::String ("Other");
            const bool low = category.containsIgnoreCase ("bass");
            const bool long_ = category.containsIgnoreCase ("pad") || category.containsIgnoreCase ("drone") || category.containsIgnoreCase ("string");

            for (int key : { low ? 36 : 60, low ? 43 : 67 })
            {
                song.findChannel (synthId)->synthPreset = preset.name;
                song.findPattern (song.currentPatternId)->notes[synthId] = { Note { 0, long_ ? ticksPerBeat * 4 : ticksPerBeat * 2, key, 100 } };
                song.notify (Song::Change::everything);
                audio.syncNow();

                if (! audio.renderToFile (wav, long_ ? 4.0 : 2.5, 24))
                    return "Couldn't render " + preset.name;

                auto b = readAll (wav);

                if (b.getNumSamples() > 0 && rmsDb (b, 0, b.getNumSamples()) > -70.0)
                {
                    // Named as NightShift's piano roll names it (C5 is middle C)
                    const auto note = juce::MidiMessage::getMidiNoteName (key, true, true, 5);
                    writeFlac (b, 0, trimmedLength (b), pack.getChildFile ("Synths/" + category + "/" + preset.name + " " + note + ".flac"));
                }
            }

            tell ("Synths: " + preset.name);
        }
    }

    //==============================================================================
    if (only.isEmpty() || only == "loops")
    {
        const auto wav = work.getChildFile ("loop.wav");

        for (auto& style : loopStyles)
        {
            const juce::String words (style.words);
            const juce::int64 seed = 7;
            Song song;
            song.loadBlank();

            const int drumsId = song.addChannel ("Drums", InstrumentType::kit).id;
            const int bassId = song.addChannel ("Bass", InstrumentType::synth).id;
            const int guitarId = song.addChannel ("Guitar", InstrumentType::instrument).id;
            song.findChannel (drumsId)->kit = style.kit;
            song.findChannel (bassId)->synthPreset = "Finger Bass";

            auto groove = drummer::offline::write (words, song, nullptr, drummer::kitNamesFor (*song.findChannel (drumsId)), true, {}, true, seed).groove;
            song.findChannel (drumsId)->kit = style.kit;       // (NightShift's own machine, whatever kit the drummer named)
            drummer::render (song, drumsId, groove, drummer::keyMapFor (*song.findChannel (drumsId)));
            const auto b = bass::offline::write (words, song, nullptr, &groove, true, true, seed);
            const auto g = guitar::offline::write (words, song, nullptr, &groove, &b.part, true, true, seed);
            song.findChannel (guitarId)->instrument = guitarsound::pathFor (g.part.sound);
            const auto kp = keys::offline::write (words, song, nullptr, &groove, &b.part, true, true, seed);
            const int keysId = song.addChannel ("Keys", InstrumentType::synth).id;
            song.findChannel (keysId)->synthPreset = kp.part.sound.isNotEmpty() ? kp.part.sound : juce::String ("Velvet Keys");
            const auto sp = synth::offline::write (words, song, nullptr, {}, {}, &groove, &b.part, &kp.part, true, true, seed);
            const int synthId = song.addChannel ("Synth", InstrumentType::synth).id;
            song.findChannel (synthId)->synthPreset = sp.part.sound;

            AudioEngine audio (song, false);
            audio.syncNow();

            if (auto patch = nova::Patch::fromVar (b.part.patch))
                audio.applySynthPatch (bassId, *patch);

            bass::render (song, bassId, b.part);
            guitar::render (song, guitarId, g.part);
            keys::render (song, keysId, kp.part);
            synth::render (song, synthId, sp.part);
            drummer::placeInArrange (song, drumsId, groove);
            bass::placeInArrange (song, bassId, b.part);
            guitar::placeInArrange (song, guitarId, g.part);
            keys::placeInArrange (song, keysId, kp.part);
            synth::placeInArrange (song, synthId, sp.part);
            song.mode = PlayMode::song;
            song.notify (Song::Change::everything);

            // Four bars from the first chorus (else the first section), where the drummer's clip for it starts
            std::vector<std::pair<int, juce::String>> sections;

            for (auto& clip : song.playlist)
                for (auto& [name, patternId] : song.findChannel (drumsId)->drummer.patterns)
                    if (clip.patternId == patternId)
                        sections.emplace_back (clip.start, name);

            std::sort (sections.begin(), sections.end());
            int from = sections.empty() ? 0 : sections.front().first;
            juce::String section = sections.empty() ? juce::String ("start") : sections.front().second;

            for (auto& [start, name] : sections)
                if (name.containsIgnoreCase ("chorus"))
                {
                    from = start;
                    section = name;
                    break;
                }

            const int bars = 4;
            const int bpm = juce::roundToInt (song.bpm);
            const double start = song.secondsAt (from), end = song.secondsAt (from + bars * song.ticksPerBar());
            const auto key = b.part.key.isNotEmpty() ? " " + b.part.key : juce::String();

            struct Part { const char* name; int id; bool keyed; };

            for (auto part : { Part { "drums", drumsId, false }, Part { "bass", bassId, true }, Part { "keys", keysId, true },
                               Part { "guitar", guitarId, true }, Part { "synth", synthId, true } })
            {
                for (auto& c : song.channels)
                    c.muted = c.id != part.id;

                song.notify (Song::Change::mixer);
                audio.syncNow();

                if (! audio.renderToFile (wav, 1.0, 24))
                    return "Couldn't render " + juce::String (style.name) + " " + part.name;

                auto rendered = readAll (wav);
                const int first = (int) std::round (start * rate), length = (int) std::round ((end - start) * rate);

                if (rendered.getNumSamples() < first + length || rmsDb (rendered, first, length) < -50.0)
                    continue;       // (a part that doesn't play in this style)

                writeFlac (rendered, first, length, pack.getChildFile ("Loops/" + juce::String (style.name) + "/" + style.name + " " + part.name + " "
                                                                       + juce::String (bpm) + "bpm" + (part.keyed ? key : juce::String()) + ".flac"));
            }

            tell ("Loops: " + juce::String (style.name) + " at " + juce::String (bpm) + " bpm" + key + " (from its " + section + ")");
        }
    }

    //==============================================================================
    // Its licence (CC0) and what's in it
    pack.getChildFile ("LICENSE.txt").replaceWithText (
        "NightShift Sounds 1\r\n\r\n"
        "CC0 1.0 Universal (public domain dedication)\r\n\r\n"
        "The person who made this pack (NightWriter, the author of NightShift) has dedicated it to the public domain by waiving all of\r\n"
        "his or her rights to it worldwide under copyright law, including all related and neighbouring rights, to the extent allowed\r\n"
        "by law. You can copy, change, share and perform the sounds, even for commercial purposes, all without asking permission\r\n"
        "and without giving credit.\r\n\r\n"
        "The full legal text: https://creativecommons.org/publicdomain/zero/1.0/legalcode\r\n\r\n"
        "Every sound in it was made by NightShift itself (its synthesised 808 and 909, its built-in kit, its Nova synth, NightShift\r\n"
        "Guitar's modelled strings and its AI band's parts): no one else's samples are in it.\r\n");

    pack.getChildFile ("README.txt").replaceWithText (
        "NightShift Sounds 1: free sounds made by NightShift (CC0, see LICENSE.txt)\r\n\r\n"
        "Drums: NightShift's synthesised 808 and 909 (every drum in three layers, two takes each) and its built-in kit.\r\n"
        "Synths: notes of every Nova sound, named with the note (C5 is middle C, as in NightShift's piano roll).\r\n"
        "Loops: the AI band in many styles, each part on its own, four bars, named with the tempo and key, so a loop dropped\r\n"
        "into a NightShift song follows the song's tempo.\r\n\r\n"
        "Made with: Nightshift --makesounds <folder>\r\n");

    work.deleteRecursively();

    //==============================================================================
    // Zipped: the sounds as they are (FLAC doesn't shrink), the words deflated
    made.zip = folder.getChildFile (zipName);
    made.zip.deleteFile();
    juce::ZipFile::Builder builder;
    auto files = pack.findChildFiles (juce::File::findFiles, true);
    files.sort();

    for (auto& f : files)
    {
        builder.addFile (f, f.hasFileExtension ("txt") ? 9 : 0, juce::String (packName) + "/" + f.getRelativePathFrom (pack).replaceCharacter ('\\', '/'));
        ++made.files;
    }

    {
        juce::FileOutputStream out (made.zip);

        if (! out.openedOk() || ! builder.writeToStream (out, nullptr))
            return "Couldn't write " + made.zip.getFullPathName();
    }

    made.bytes = made.zip.getSize();
    juce::FileInputStream in (made.zip);
    made.sha256 = juce::SHA256 (in).toHexString().toLowerCase();
    return {};
}

} // namespace nightshift::soundsmaker
