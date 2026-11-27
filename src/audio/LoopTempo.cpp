// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "LoopTempo.h"

namespace nightshift::looptempo
{

namespace
{
    /** Whether `bpm` makes `seconds` a whole number of beats (a loop is cut to its beats). */
    bool wholeBeats (double seconds, double bpm)
    {
        const double beats = seconds * bpm / 60.0;
        return beats >= 1.0 - 1.0e-6 && std::abs (beats - std::round (beats)) * 60.0 / bpm <= 0.0015;
    }
}

double fromName (const juce::String& name)
{
    // Numbers in it, each with what's either side of it
    const auto lower = name.toLowerCase();
    struct Number { double value; int start, end; };
    std::vector<Number> numbers;

    for (int i = 0; i < lower.length();)
    {
        if (! juce::CharacterFunctions::isDigit (lower[i]))
        {
            ++i;
            continue;
        }

        int end = i;

        while (end < lower.length() && (juce::CharacterFunctions::isDigit (lower[end])
                                        || (lower[end] == '.' && end + 1 < lower.length() && juce::CharacterFunctions::isDigit (lower[end + 1]))))
            ++end;

        numbers.push_back ({ lower.substring (i, end).getDoubleValue(), i, end });
        i = end;
    }

    auto plausible = [] (double v) { return v >= 40.0 && v <= 300.0; };

    // "120bpm", "120 BPM", "120_bpm", "bpm120", "BPM 120"
    for (auto& n : numbers)
    {
        const auto after = lower.substring (n.end).trimStart().trimCharactersAtStart ("_-").trimStart();
        const auto before = lower.substring (0, n.start).trimEnd().trimCharactersAtEnd ("_-:").trimEnd();

        if (plausible (n.value) && (after.startsWith ("bpm") || before.endsWith ("bpm")))
            return n.value;
    }

    // Else one number standing alone ("loop_95_Am", "128 house") that's a tempo: a digit or a letter next
    // to it makes it part of a word ("808", "Take 3", "70s"), and so does a second such number
    double alone = 0.0;
    int found = 0;

    for (auto& n : numbers)
    {
        const auto previous = n.start > 0 ? lower[n.start - 1] : (juce::juce_wchar) ' ';
        const auto next = n.end < lower.length() ? lower[n.end] : (juce::juce_wchar) ' ';
        const bool standsAlone = ! juce::CharacterFunctions::isLetterOrDigit (previous) && ! juce::CharacterFunctions::isLetterOrDigit (next)
                                  && next != '.';

        if (standsAlone && n.value >= 60.0 && n.value <= 200.0 && std::abs (n.value - std::round (n.value)) < 1.0e-9)
        {
            alone = n.value;
            ++found;
        }
    }

    return found == 1 ? alone : 0.0;
}

double fromLength (double seconds, double* beatsOut)
{
    double best = 0.0, bestBeats = 0.0;

    // Whole bars of 4/4: one bar to sixteen
    for (double beats : { 4.0, 8.0, 12.0, 16.0, 24.0, 32.0, 48.0, 64.0 })
    {
        const double bpm = std::round (beats * 60.0 / seconds);

        if (bpm < 70.0 || bpm > 180.0 || std::abs (beats * 60.0 / bpm - seconds) > 0.001)
            continue;

        if (best == 0.0 || std::abs (bpm - 120.0) < std::abs (best - 120.0))
        {
            best = bpm;
            bestBeats = beats;
        }
    }

    if (beatsOut != nullptr)
        *beatsOut = bestBeats;

    return best;
}

Found find (const juce::File& file)
{
    Found f;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return f;

    f.seconds = (double) reader->lengthInSamples / reader->sampleRate;
    const auto& meta = reader->metadataValues;

    // What the file says about itself: a single hit isn't a loop; a loop's beats over its length
    if (meta[juce::WavAudioFormat::acidOneShot] == "1" || meta[juce::AiffAudioFormat::appleOneShot] == "1")
    {
        f.oneShot = true;
        return f;
    }

    double beats = meta[juce::WavAudioFormat::acidBeats].getDoubleValue();

    if (beats <= 0.0)
        beats = meta[juce::AiffAudioFormat::appleBeats].getDoubleValue();

    if (beats > 0.0 && beats * 60.0 / f.seconds >= 20.0 && beats * 60.0 / f.seconds <= 400.0)
    {
        f.bpm = beats * 60.0 / f.seconds;
        f.beats = beats;
        f.how = "file";
        return f;
    }

    if (const double tempo = meta[juce::WavAudioFormat::acidTempo].getDoubleValue(); tempo >= 20.0 && tempo <= 400.0)
    {
        f.bpm = tempo;
        f.beats = f.seconds * tempo / 60.0;
        f.how = "file";
        return f;
    }

    // Its name: "bpm" beside a number is taken at its word; a number alone only if the loop is whole beats of it
    const auto name = file.getFileNameWithoutExtension();

    if (const double named = fromName (name); named > 0.0)
    {
        const bool saysBpm = name.toLowerCase().contains ("bpm");

        if (saysBpm || wholeBeats (f.seconds, named))
        {
            f.bpm = named;
            f.beats = f.seconds * named / 60.0;
            f.how = "name";
            return f;
        }
    }

    // Its length: whole bars of 4/4 at a whole-number tempo
    if (const double bpm = fromLength (f.seconds, &beats); bpm > 0.0)
    {
        f.bpm = bpm;
        f.beats = beats;
        f.how = "length";
    }

    return f;
}

} // namespace nightshift::looptempo
