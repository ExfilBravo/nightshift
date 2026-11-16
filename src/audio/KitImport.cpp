// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Kits.h"

namespace nightshift::drumkits
{

namespace
{
    /** A name in words: "BD_Punchy-01" -> "bd punchy 01", "OpenHat2" -> "open hat 2". */
    juce::StringArray wordsOf (const juce::String& name)
    {
        juce::String spaced;
        juce::juce_wchar previous = 0;

        for (auto c : name)
        {
            const bool upper = juce::CharacterFunctions::isUpperCase (c), digit = juce::CharacterFunctions::isDigit (c);
            const bool lowerBefore = juce::CharacterFunctions::isLowerCase (previous), digitBefore = juce::CharacterFunctions::isDigit (previous);

            if ((upper && lowerBefore) || (digit && ! digitBefore && previous != 0) || (! digit && digitBefore))
                spaced << ' ';

            spaced << (juce::CharacterFunctions::isLetterOrDigit (c) ? juce::CharacterFunctions::toLowerCase (c) : (juce::juce_wchar) ' ');
            previous = c;
        }

        return juce::StringArray::fromTokens (spaced, " ", "");
    }

    struct Found
    {
        juce::File file;
        juce::String voice;
    };
}

juce::String voiceForName (const juce::String& name)
{
    const auto words = wordsOf (name);
    const auto joined = words.joinIntoString ("");
    auto has = [&words] (std::initializer_list<const char*> any) { for (auto w : any) if (words.contains (w)) return true; return false; };
    auto contains = [&joined] (std::initializer_list<const char*> any) { for (auto w : any) if (joined.contains (w)) return true; return false; };

    // Most particular first: a "ride bell" is a bell before a ride, an "open hat" a hat before anything
    if (contains ({ "ridebell" }) || (has ({ "ride", "rd" }) && has ({ "bell" })))      return "ride_bell";
    if (contains ({ "china" }))                                                       return "china";
    if (contains ({ "splash" }))                                                      return "splash";
    if (contains ({ "ride" }) || has ({ "rd" }))                                      return "ride";
    if (contains ({ "crash", "cymbal" }) || has ({ "cr", "cym", "cy" }))              return "crash";

    if (contains ({ "hihat", "openhat", "closedhat" }) || has ({ "hat", "hats", "hh", "oh", "ch", "ph" }))
    {
        if (has ({ "open", "opn", "oh", "o" }) || contains ({ "openhat", "ophat" }))   return "hat_open";
        if (has ({ "pedal", "foot", "ph", "pdl" }))                                   return "hat_pedal";
        return "hat_closed";
    }

    if (contains ({ "rimshot" }))                                                     return "snare_rim";
    if (contains ({ "sidestick", "crossstick", "rimclick" }) || has ({ "rim", "rs", "xstick" }))  return "sidestick";
    if (contains ({ "clap", "snap" }) || has ({ "cp", "clp" }))                       return "clap";
    if (contains ({ "snare" }) || has ({ "sd", "snr", "sn" }))                        return "snare";

    // Words, not letters: "bottom" and "custom" aren't toms
    if (has ({ "tom", "toms", "ht", "mt", "lt", "ft" }) || contains ({ "floortom", "racktom", "hitom", "lotom", "midtom" }))
    {
        if (has ({ "hi", "high", "ht", "h", "1", "small", "rack" }))                  return "tom_high";
        if (has ({ "lo", "low", "lt", "floor", "ft", "l", "3", "4", "big" }))         return "tom_floor";
        return "tom_mid";
    }

    if (contains ({ "kick", "kik", "bassdrum" }) || has ({ "bd", "kck" }))            return "kick";
    if (contains ({ "cowbell" }) || has ({ "cb", "cow" }))                            return "cowbell";
    if (contains ({ "tamb" }))                                                        return "tambourine";
    if (contains ({ "shak", "maraca", "cabasa" }) || has ({ "ma", "shk" }))           return "shaker";
    if (contains ({ "clave", "click", "sticks" }))                                    return "stick_click";
    return {};
}

juce::String importFolder (const juce::File& source, juce::String& report)
{
    if (! source.isDirectory())
    {
        report = source.getFullPathName() + " isn't a folder.";
        return {};
    }

    // Every sound in it, on the drum its name (or its folder's) says
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::map<juce::String, std::vector<juce::File>> byVoice;
    int skipped = 0, sounds = 0;

    for (const auto& entry : juce::RangedDirectoryIterator (source, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg", juce::File::findFiles))
    {
        const auto file = entry.getFile();

        if (++sounds > 2000)
            break;

        juce::String voice = voiceForName (file.getFileNameWithoutExtension());

        for (auto folder = file.getParentDirectory(); voice.isEmpty() && folder.isAChildOf (source.getParentDirectory()) && folder != source.getParentDirectory();
             folder = folder.getParentDirectory())
            voice = voiceForName (folder.getFileName());

        if (voice.isEmpty())
            ++skipped;
        else
            byVoice[voice].push_back (file);
    }

    if (byVoice.empty())
    {
        report = sounds == 0 ? "There are no sounds in " + source.getFileName() + "."
                             : "None of the " + juce::String (sounds) + " sounds in " + source.getFileName()
                                 + " are named like drums (kick, snare, clap, hat, tom, crash, ride...).";
        return {};
    }

    // Its own folder, named after the source
    auto base = source.getFileName().toUpperCase().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _").trim().replaceCharacter (' ', '_');
    base = "MY_" + (base.isNotEmpty() ? base : juce::String ("KIT"));
    auto id = base;

    for (int n = 2; folder().getChildFile (id).exists(); ++n)
        id = base + "_" + juce::String (n);

    const auto staging = folder().getChildFile (".importing");
    staging.deleteRecursively();
    staging.getChildFile ("samples").createDirectory();

    juce::String sfz, summary;
    sfz << "// " << source.getFileName() << ": made by Nightshift from " << source.getFullPathName() << "\n\n";
    juce::StringArray kinds;

    for (auto& voice : voiceNames())
    {
        auto it = byVoice.find (voice);

        if (it == byVoice.end())
            continue;

        auto files = it->second;
        std::sort (files.begin(), files.end());

        if (files.size() > 16)
            files.resize (16);

        const auto [label, key] = pieceFor (voice);
        const bool hat = voice.startsWith ("hat_");
        sfz << "<group> key=" << key << " group_label=" << label.replaceCharacter (' ', '_') << (hat ? " group=1 off_by=1" : "") << "\n";
        int n = 0;

        for (auto& file : files)
        {
            // Each take at the level the other kits play this drum
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

            if (reader == nullptr || reader->lengthInSamples == 0)
                continue;

            juce::Range<float> ranges[2];
            reader->readMaxLevels (0, reader->lengthInSamples, ranges, (int) std::min (2u, reader->numChannels));
            const float peak = std::max ({ -ranges[0].getStart(), ranges[0].getEnd(), -ranges[1].getStart(), ranges[1].getEnd() });
            const float volume = peak > 0.0f ? juce::Decibels::gainToDecibels (peakFor (voice) / peak) : 0.0f;

            const auto name = voice + "_" + juce::String (++n).paddedLeft ('0', 2) + file.getFileExtension().toLowerCase();

            if (! file.copyFileTo (staging.getChildFile ("samples").getChildFile (name)))
            {
                staging.deleteRecursively();
                report = "Couldn't copy " + file.getFileName() + " into " + folder().getFullPathName() + ".";
                return {};
            }

            sfz << "<region> lorand=" << juce::String ((float) (n - 1) / (float) files.size(), 3) << " hirand=" << juce::String ((float) n / (float) files.size(), 3)
                << " volume=" << juce::String (volume, 2) << " sample=samples/" << name << "\n";
        }

        sfz << "\n";
        kinds.add (label.toLowerCase() + (n > 1 ? " x" + juce::String (n) : juce::String()));
    }

    staging.getChildFile (id + ".sfz").replaceWithText (sfz);
    staging.getChildFile ("kit.txt").replaceWithText (source.getFileName() + "\n"
                                                      + "the musician's own samples from " + source.getFileName() + " (" + kinds.joinIntoString (", ") + ")\n"
                                                      + "Made by Nightshift from " + source.getFullPathName() + "\n");

    if (! staging.moveFileTo (folder().getChildFile (id)))
    {
        staging.deleteRecursively();
        report = "Couldn't put the kit in " + folder().getFullPathName() + ".";
        return {};
    }

    report = "Made the " + source.getFileName() + " kit: " + kinds.joinIntoString (", ") + "."
             + (skipped > 0 ? " " + juce::String (skipped) + (skipped == 1 ? " sound wasn't" : " sounds weren't") + " named like a drum, so it's left out." : juce::String());
    return id;
}

} // namespace nightshift::drumkits
