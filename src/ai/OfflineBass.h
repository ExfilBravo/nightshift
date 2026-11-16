// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "Bass.h"
#include "Drummer.h"
#include "Request.h"

/** The offline bassist: a specialist per style that plays with the drummer the way a session
    bassist who plays nothing else would - metalcore's low root doubling the chug, every kick of a
    breakdown, a b2 to lift it; trap's 808 on the kick, held to the next, sliding up an octave;
    drill's slides between scale tones; boom bap's root on the kick with a passing note; rock's
    and pop-punk's driving root 8ths and walk-ups; lofi's warm chord tones with a chromatic step
    into each change; jazz's walking quarters; neo-soul's root pushed ahead of the one with
    slides and ghost notes; funk on the one, the octave popped, dead notes in between. Each
    section follows the drummer's (same names, bars and kick); with no drummer yet it writes to
    the drums its style would have.

    It plays chord progressions its genre uses, in a key (asked for, or the style's own: drop C
    for metalcore, F minor for trap, Bb for jazz...), writes many candidate lines a section and
    keeps the one that best fits its style's rules. No internet. */
namespace nightshift::bass::offline
{
    struct Result
    {
        Part part;
        juce::String reply;             // what it wrote, in a bassist's words
        agents::Request request;        // what it understood (kept, so a follow-up changes it)
        drummer::Groove drums;          // the drums it played with (the drummer's, or the ones it imagined)
        bool imaginedDrums = false;
    };

    /** Writes a bass part for `prompt`, locked to `drums` (the drummer's part) if there is one.
        `previous` is the last request this builds on (the channel's, or the drummer's for a first
        line). With `chooseSound` it picks the sound for the style. `songIsBlank`: nothing else in
        the song yet, so it may set the tempo. A different `seed` gives another take. */
    Result write (const juce::String& prompt, const Song&, const agents::Request* previous, const drummer::Groove* drums,
                  bool chooseSound, bool songIsBlank, juce::int64 seed);

    /** The sound the bassist plays a style on: a Nova factory bass, shaped for it. */
    juce::var soundFor (const juce::String& style);

    /** The conversation's turn for it, and what a turn's request was. */
    BassTurn turnFor (const juce::String& prompt, const Result&);
    std::optional<agents::Request> requestOf (const BassTurn&);
}
