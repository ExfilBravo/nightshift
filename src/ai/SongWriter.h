// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "Drummer.h"

/** The AI song agent's plan, offline: from one prompt, which of the band plays (the style's own -
    metal's guitars and synth, trap's 808, piano and bells, jazz's trio and a flute - changed by
    the words: "no keys", "with a guitar"), the song's sections and the order they're played in,
    two minutes or more: an intro, verses and choruses (hooks first in trap, Head and Solos in
    jazz, a breakdown in metal, a bridge in rock and funk), repeated until it's long enough, and an
    outro. The studio then has each agent write its part (the drummer first, the rest following its
    sections and the bassist's chords), lays it all out in Arrange and has it mastered. */
namespace nightshift::songwriter
{
    /** Who plays besides the drums ("bass", "guitar", "keys", "synth"), for a style and the words. */
    juce::StringArray lineupFor (const juce::String& style, const juce::String& words);

    /** The song's sections in the order they're played, as kinds (intro, verse, chorus...). */
    juce::StringArray formFor (const juce::String& style, const juce::String& words);

    /** The words the drummer gets: the request without its own section words, then the sections
        to write (each once, in the order they first come), and longer sections for a fast style;
        how sections are played and their own time signatures said again on their own after. */
    juce::String drummerWords (const juce::String& words, const juce::String& style);

    /** The order the drummer's sections are played in: the style's form, a verse and chorus (or
        whatever the form repeats) added before the outro until it's at least `minimumSeconds`.
        `secondsPerBar` is a bar of the groove's time signature, else of `barMeter`; a section in a
        time signature of its own lasts as long as its bars do. */
    juce::StringArray arrange (const drummer::Groove&, const juce::String& style, const juce::String& words,
                               double secondsPerBar, double minimumSeconds = 125.0, std::pair<int, int> barMeter = { 4, 4 });

    /** How long an order lasts. */
    double lengthOf (const drummer::Groove&, const juce::StringArray& order, double secondsPerBar, std::pair<int, int> barMeter = { 4, 4 });
}
