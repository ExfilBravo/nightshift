// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "SongWriter.h"
#include "Genres.h"
#include "Request.h"
#include "Theory.h"

namespace nightshift::songwriter
{

namespace
{
    juce::String spaced (const juce::String& words)
    {
        auto t = words.toLowerCase().replace ("'", "");

        for (const char* p = ",.!?;:()\"/"; *p != 0; ++p)
            t = t.replaceCharacter ((juce::juce_wchar) *p, ' ');

        return " " + t + " ";
    }

    bool has (const juce::String& text, std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (text.contains (" " + juce::String (w) + " "))
                return true;

        return false;
    }

    juce::String familyOf (const juce::String& style)
    {
        if (const auto* s = agents::findStyle (style))
            return s->family;

        return "rock";
    }

    // The words the request reads as sections (kept out of what the agents get, so the song's own
    // form is what's written)
    const char* const sectionWords[] { "intro", "introduction", "verse", "verses", "pre chorus", "prechorus", "buildup", "build up",
                                       "chorus", "choruses", "refrain", "breakdown", "breakdowns", "break down", "bridge", "middle eight",
                                       "hook", "hooks", "outro", "ending" };
}

juce::StringArray lineupFor (const juce::String& style, const juce::String& words)
{
    static const std::map<juce::String, juce::StringArray> bands {
        { "metalcore", { "bass", "guitar", "keys", "synth" } },
        { "deathcore", { "bass", "guitar", "synth" } },
        { "djent",     { "bass", "guitar", "synth" } },
        { "thrash",    { "bass", "guitar" } },
        { "blast",     { "bass", "guitar", "synth" } },
        { "metal",     { "bass", "guitar", "synth" } },
        { "trap",      { "bass", "keys", "synth" } },
        { "drill",     { "bass", "keys", "synth" } },
        { "boombap",   { "bass", "keys", "synth" } },
        { "hiphop",    { "bass", "keys", "synth" } },
        { "rock",      { "bass", "guitar", "keys" } },
        { "hardrock",  { "bass", "guitar", "keys" } },
        { "poppunk",   { "bass", "guitar", "synth" } },
        { "punk",      { "bass", "guitar" } },
        { "lofi",      { "bass", "keys", "synth" } },
        { "jazz",      { "bass", "keys", "synth" } },
        { "ballad",    { "bass", "keys", "synth" } },
        { "neosoul",   { "bass", "guitar", "keys", "synth" } },
        { "funk",      { "bass", "guitar", "keys", "synth" } },
        { "house",     { "bass", "keys", "synth" } },
        { "techno",    { "bass", "synth" } },
        { "edm",       { "bass", "keys", "synth" } },
        { "dnb",       { "bass", "keys", "synth" } },
        { "dubstep",   { "bass", "synth" } },
        { "synthwave", { "bass", "guitar", "keys", "synth" } },
        { "reggae",    { "bass", "guitar", "keys", "synth" } },
        { "reggaeton", { "bass", "guitar", "keys", "synth" } },
        { "afrobeats", { "bass", "guitar", "keys", "synth" } },
        { "bossa",     { "bass", "guitar", "keys", "synth" } },
        { "ambient",   { "bass", "keys", "synth" } },
        { "cinematic", { "bass", "guitar", "keys", "synth" } },
        { "country",   { "bass", "guitar", "keys" } },
    };

    // The words with a genre's or artist's feel after them: Sleep Token's "piano" brings the keys in
    auto lineup = bands.count (style) > 0 ? bands.at (style) : bands.at ("rock");
    const auto t = spaced (agents::withFeel (words));

    // "No keys", "without the guitar": out; "with a guitar", "add keys": in
    struct Player { const char* id; std::initializer_list<const char*> names; };
    static const Player players[] {
        { "bass",   { "bass", "bassist", "808", "808s" } },
        { "guitar", { "guitar", "guitars", "guitarist" } },
        { "keys",   { "keys", "piano", "rhodes", "organ", "clav", "keyboard", "keyboards" } },
        { "synth",  { "synth", "synths", "pad", "pads", "bells", "bell", "flute", "horns", "horn", "brass", "strings", "choir", "arp", "arpeggio" } },
    };

    auto named = [&t] (const juce::String& phrase) { return t.contains (" " + phrase + " "); };

    for (auto& player : players)
        for (auto* name : player.names)
        {
            const juce::String n (name);

            if (named ("no " + n) || named ("without " + n) || named ("without a " + n) || named ("without the " + n) || named ("no more " + n))
            {
                lineup.removeString (player.id);
                break;
            }

            // Named without a "no" before it: in
            if (named (n) && ! lineup.contains (player.id))
                lineup.add (player.id);
        }

    // In the band's order
    juce::StringArray ordered;

    for (auto* id : { "bass", "guitar", "keys", "synth" })
        if (lineup.contains (id))
            ordered.add (id);

    return ordered;
}

juce::StringArray formFor (const juce::String& style, const juce::String& words)
{
    // A form asked for (the vibe listener's, or typed: "form intro verse chorus outro"): the song as asked
    if (const auto asked = agents::Request::parse (words).form; asked.size() >= 2)
        return asked;

    const auto f = familyOf (style);
    juce::StringArray form;

    if (f == "metal")
        form = { "intro", "verse", "chorus", "verse", "chorus", "breakdown", "chorus", "outro" };
    else if (f == "trap")
        form = { "intro", "hook", "verse", "hook", "verse", "hook", "outro" };
    else if (style == "jazz" || style == "ballad")
        form = { "intro", "verse", "chorus", "chorus", "verse", "outro" };       // the head, the solos, the head again
    else if (style == "lofi")
        form = { "intro", "verse", "chorus", "verse", "chorus", "outro" };
    else if (style == "neosoul")
        form = { "intro", "verse", "hook", "verse", "hook", "bridge", "hook", "outro" };
    else if (style == "punk")
        form = { "intro", "verse", "chorus", "verse", "chorus", "verse", "chorus", "outro" };
    else if (style == "house" || style == "techno")
        form = { "intro", "verse", "chorus", "breakdown", "chorus", "outro" };
    else if (style == "edm")
        form = { "intro", "verse", "prechorus", "drop", "breakdown", "prechorus", "drop", "outro" };
    else if (style == "dnb")
        form = { "intro", "verse", "drop", "breakdown", "drop", "outro" };
    else if (style == "dubstep")
        form = { "intro", "prechorus", "drop", "breakdown", "prechorus", "drop", "outro" };
    else if (style == "reggaeton")
        form = { "intro", "hook", "verse", "hook", "verse", "hook", "outro" };
    else if (style == "afrobeats")
        form = { "intro", "verse", "hook", "verse", "hook", "bridge", "hook", "outro" };
    else if (style == "bossa" || style == "ambient")
        form = { "intro", "verse", "chorus", "verse", "chorus", "outro" };
    else if (style == "cinematic")
        form = { "intro", "verse", "prechorus", "chorus", "verse", "prechorus", "chorus", "outro" };
    else
        form = { "intro", "verse", "chorus", "verse", "chorus", "bridge", "chorus", "outro" };

    // Sections the words ask for that the form hasn't (asked for, or in a time signature of their own: "a
    // breakdown in 7/8"): in before the last chorus (or hook); an intro first, an outro last
    const auto request = agents::Request::parse (words);
    auto asked = request.sections;

    for (auto& [kind, meter] : request.meterIn)
        asked.addIfNotAlreadyThere (kind);

    for (auto& kind : asked)
    {
        if (form.contains (kind))
            continue;

        if (kind == "intro" || kind == "outro")
        {
            if (request.meterIn.count (kind) > 0)
                form.insert (kind == "intro" ? 0 : form.size(), kind);

            continue;
        }

        int at = form.size() - 1;

        for (int i = form.size() - 1; i > 0; --i)
            if (form[i] == "chorus" || form[i] == "hook")
            {
                at = i;
                break;
            }

        form.insert (at, kind);
    }

    return form;
}

juce::String drummerWords (const juce::String& words, const juce::String& style)
{
    // The request without how its sections are played (said again at the end, on its own) or its section
    // words (the song's form is what's written)...
    auto t = " " + agents::withoutSectionFeel (words);

    for (auto* w : sectionWords)
        t = t.replace (" " + juce::String (w) + " ", " ");

    // "Drop" too, unless it's a tuning ("drop C", "drop d#")
    {
        auto tokens = juce::StringArray::fromTokens (t, " ", "");
        tokens.removeEmptyStrings();

        for (int i = tokens.size(); --i >= 0;)
            if (tokens[i] == "drops" || (tokens[i] == "drop" && ! (tokens[i + 1].length() <= 2 && tokens[i + 1].isNotEmpty()
                                                                  && juce::String ("abcdefg").containsChar (tokens[i + 1][0]))))
                tokens.remove (i);

        t = " " + tokens.joinIntoString (" ") + " ";
    }

    for (auto* w : { " song ", " a song ", " track ", " a track " })
        t = t.replace (w, " ");

    // ...with each section to write before them, once, in the order they first come (first, so no word of
    // the request reads as how one is played: "ambient intro verse" would be a soft intro, and no intro);
    // longer sections for the fast styles, so the song isn't all repeats
    juce::StringArray kinds;

    for (auto& k : formFor (style, words))
        kinds.addIfNotAlreadyThere (k);

    // Sixteen bars a section where eight would leave the song all repeats (the fast styles, and the
    // ones with few sections): a 16-bar verse, as most songs have
    const auto* s = agents::findStyle (style);
    const auto request = agents::Request::parse (words);
    const int tempo = request.tempo > 0 ? request.tempo : (s != nullptr ? s->usual : 120);
    int middle = 0;

    for (auto& k : formFor (style, words))
        middle += k != "intro" && k != "outro" ? 1 : 0;

    const double eightBarSong = (middle * 8 + 8) * 4.0 * 60.0 / tempo;
    const bool longer = eightBarSong < 90.0 && request.bars == 0;

    // How the sections are played, each on its own ("soft verses, heavy half-time choruses"), and the time
    // signatures some are in ("the bridge in 7/8")
    juce::String feel;

    for (auto& k : request.softIn)      feel << ", soft " << k;
    for (auto& k : request.heavyIn)     feel << ", heavy " << k;
    for (auto& k : request.halfTimeIn)  feel << ", halftime " << k;

    for (auto& [kind, m] : request.meterIn)
        feel << ", the " << kind << " in " << m.first << "/" << m.second;

    return kinds.joinIntoString (" ") + " " + t.trim() + (longer ? " 16 bars" : "") + feel;
}

double lengthOf (const drummer::Groove& g, const juce::StringArray& order, double secondsPerBar, std::pair<int, int> barMeter)
{
    // A bar of `barMeter` (the groove's own if it names one) takes secondsPerBar; a section in a time
    // signature of its own, its bars as long as they are
    if (g.meterBeats > 0 && g.meterUnit > 0)
        barMeter = { g.meterBeats, g.meterUnit };

    const double quarters = barMeter.first * 4.0 / std::max (1, barMeter.second);
    double seconds = 0.0;

    for (auto& name : order)
        for (auto& s : g.sections)
            if (s.name == name)
                seconds += (double) s.bars.size() * secondsPerBar
                             * (s.meterBeats > 0 && s.meterUnit > 0 ? s.meterBeats * 4.0 / s.meterUnit / quarters : 1.0);

    return seconds;
}

juce::StringArray arrange (const drummer::Groove& g, const juce::String& style, const juce::String& words,
                           double secondsPerBar, double minimumSeconds, std::pair<int, int> barMeter)
{
    // Each kind's section, as the drummer named it (Head and Solos in jazz)
    std::map<juce::String, juce::String> nameOf;

    for (auto& s : g.sections)
        nameOf.emplace (theory::kindOf (s.name), s.name);

    juce::StringArray kinds;

    for (auto& k : formFor (style, words))
        if (nameOf.count (k) > 0)
            kinds.add (k);

    if (kinds.isEmpty())
        for (auto& s : g.sections)
            kinds.add (theory::kindOf (s.name));

    // What it repeats when it needs to be longer: the verse and what follows it (the chorus or hook)
    juce::StringArray cycle;

    for (int i = 0; i < kinds.size(); ++i)
        if (kinds[i] == "verse")
        {
            cycle.add (kinds[i]);

            if (i + 1 < kinds.size() && kinds[i + 1] != "outro")
                cycle.add (kinds[i + 1]);

            break;
        }

    if (cycle.isEmpty())
        for (auto& k : kinds)
            if (k != "intro" && k != "outro")
            {
                cycle.add (k);
                break;
            }

    auto toNames = [&nameOf] (const juce::StringArray& ks)
    {
        juce::StringArray names;

        for (auto& k : ks)
            names.add (nameOf.at (k));

        return names;
    };

    // Longer until it's long enough: another verse and chorus, just before the last chorus (and
    // the outro) - so the song still ends on its biggest section
    for (int guard = 0; guard < 24 && ! cycle.isEmpty() && lengthOf (g, toNames (kinds), secondsPerBar, barMeter) < minimumSeconds; ++guard)
    {
        int at = kinds.size();

        for (int i = kinds.size() - 1; i > 0; --i)
            if (kinds[i] == cycle[cycle.size() - 1])
            {
                at = i;
                break;
            }

        for (int i = cycle.size(); --i >= 0;)
            kinds.insert (at, cycle[i]);
    }

    return toNames (kinds);
}

} // namespace nightshift::songwriter
