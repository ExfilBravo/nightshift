// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "Guitar.h"
#include "Bass.h"
#include "Drummer.h"
#include "Request.h"

/** The offline guitarist: a specialist per style playing the bassist's chords, locked to the
    drummer, the way a session guitarist who plays nothing else would - metalcore's palm-muted
    chugs on the open low string of a drop tuning, a power chord ringing on the china, dissonant
    stabs to end a phrase; djent on a 7-string; tremolo picking under a blast; pop-punk's
    palm-muted power-chord 8ths opening up in the chorus; rock's barre strums; trap's clean
    arpeggios; boom bap's jazzy stabs; jazz comping in shells (four to the bar, or the
    Charleston); lofi's soft rootless chords; neo-soul's ninths with hammer-ons; funk's 16th
    chicken-scratch with ninth-chord stabs. Every note has its string and fret, so it reads as tab.

    With no bassist (or drummer) yet it plays to the ones its style would have. No internet. */
namespace nightshift::guitar::offline
{
    struct Result
    {
        Part part;
        juce::String reply;
        agents::Request request;
        drummer::Groove drums;
        bool imaginedDrums = false, imaginedBass = false;
    };

    /** Writes a guitar part for `prompt` over `bass`'s chords (the bassist's part) and locked to
        `drums` (the drummer's), each imagined for the style when missing. `previous` is the last
        request this builds on. With `chooseSound` it picks the guitar sound for the style. */
    Result write (const juce::String& prompt, const Song&, const agents::Request* previous, const drummer::Groove* drums,
                  const bass::Part* bass, bool chooseSound, bool songIsBlank, juce::int64 seed);

    /** The guitar sound (preset) a style is played on. */
    juce::String soundFor (const juce::String& style);

    /** Where the guitar's fader goes in a style's mix, in dB from unity (what --bandrender measured). */
    float levelFor (const juce::String& style);

    GuitarTurn turnFor (const juce::String& prompt, const Result&);
    std::optional<agents::Request> requestOf (const GuitarTurn&);
}
