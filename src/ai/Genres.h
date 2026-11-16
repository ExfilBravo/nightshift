// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** What the band knows about music beyond the styles it plays (Request.h's styles()): genres and the
    artists that define them. Each genre is played as the style nearest it, with its feel on top (its
    tempo, quiet verses and heavy half-time choruses, a tuning, who plays), written as the words a user
    would type ("soft verses, heavy half-time chorus, drop c, synth"), so the same reading of a request
    applies them. An artist is a genre plus its own touches: Sleep Token is alternative metal with keys
    and soul chords. Some genres the band doesn't play yet (house, reggae): they say so, and name the
    style they're played as for now. Everything here is the band's own knowledge: no internet. */
namespace nightshift::agents
{
    struct Genre
    {
        const char* id;
        const char* name;               // as said back: "Alternative metal"
        const char* plays;              // the style the band plays it as (styles())
        bool own;                       // false: not a genre the band plays yet; `plays` is the nearest
        int slowest, usual, fastest;    // its tempos
        const char* feel;               // words added to the request, read as if typed
        juce::StringArray words;        // its names
        const char* about;              // a line for the reply
    };

    struct Artist
    {
        const char* name;               // as said back
        const char* genre;
        const char* feel;               // more words, its own touches
        juce::StringArray words;        // other ways to write its name
    };

    const std::vector<Genre>& genres();
    const std::vector<Artist>& artists();
    const Genre* findGenre (const juce::String& id);

    /** The genre or artist named in words already spaced out for matching (Request.cpp's " word "
        form), and where: the longest name wins, the first of two as long. */
    struct Named
    {
        const Genre* genre = nullptr;
        const Artist* artist = nullptr;
        int at = -1, length = 0;

        juce::String feel() const;      // the genre's feel words and the artist's, together
    };

    Named findNamed (const juce::String& spacedWords);

    /** `words` with the feel of any genre or artist they name added, for the readers that look for
        words themselves (who plays, the song's form). */
    juce::String withFeel (const juce::String& words);

    /** A letter or digit of a word, the same on every system. JUCE's isLetterOrDigit asks the C library,
        whose "C" locale on a Mac knows only a to z, so "Beyonce" with an e acute lost it there before it
        could be folded to e (Mac fixes 4). Latin letters with accents (U+00C0 to U+024F, but not the
        multiplication and division signs) count as letters too. */
    bool isWordLetter (juce::juce_wchar c);
}
