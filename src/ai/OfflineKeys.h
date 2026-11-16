// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "Keys.h"
#include "Bass.h"
#include "Drummer.h"
#include "Request.h"

/** The offline keys player: a specialist per style playing the bassist's chords, locked to the
    drummer, the way a session keyboardist who plays nothing else would - pop-rock's pumping 8th
    triads over octaves in the left hand; the organ held under hard rock; trap's dark piano motif
    following the chords; boom bap's dusty Rhodes stabs; jazz comping in rootless voicings (the
    Charleston, the anticipations), left to the bassist for roots; ballads rolled; lofi's felt
    piano, soft and rolled, a few notes answering; neo-soul's Rhodes ninths pushed ahead of the
    one with grace notes sliding into them; funk's clav stabs interlocking with the left hand;
    metal's piano arpeggios in the intro, big chords under the chorus, laying out for the
    breakdown. Every chord is voice-led from the last (the nearest inversion). No internet. */
namespace nightshift::keys::offline
{
    /** A style's sound: a Nova keys sound, or a sampled instrument (its path) when it's here. */
    struct Sound
    {
        juce::String name, sampled;
    };

    Sound soundFor (const juce::String& style);

    /** Where its fader goes in the style's mix, dB from unity (measured with --bandrender). */
    float levelFor (const juce::String& style);

    struct Result
    {
        Part part;
        juce::String reply;
        agents::Request request;
        drummer::Groove drums;
        bool imaginedDrums = false, imaginedBass = false;
    };

    Result write (const juce::String& prompt, const Song&, const agents::Request* previous, const drummer::Groove* drums,
                  const bass::Part* bass, bool chooseSound, bool songIsBlank, juce::int64 seed);

    KeysTurn turnFor (const juce::String& prompt, const Result&);
    std::optional<agents::Request> requestOf (const KeysTurn&);
}
