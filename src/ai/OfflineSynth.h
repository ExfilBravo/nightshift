// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "SynthPart.h"
#include "Bass.h"
#include "Keys.h"
#include "Drummer.h"
#include "Request.h"

/** The offline synth player: a specialist per style on a Nova sound, playing the bassist's chords
    and locked to the drummer, in one of four jobs - a pad (held chords, each led from the last),
    an arpeggio, a lead (a melody built from a motif, its strong beats on the chord's tones), or
    stabs (chords hit with the band). Each style has its own: metalcore's trance arpeggios and
    supersaw chorus, out of the breakdown's way; deathcore's choir; djent's ambient pad floating
    over the chugs; trap's bell melody, the same rhythm every bar, answering the keys; drill's
    dark choir; boom bap's horn stabs; pop-punk's octave hook; hard rock's brass; lofi's lazy
    lead behind the beat; jazz's flute lines with their approach notes; neo-soul's Moog lead
    sliding into its notes; funk's horn section hits and riffs. No internet. */
namespace nightshift::synth::offline
{
    /** What it plays: "pad", "arp", "lead" or "stabs". */
    juce::StringArray roles();

    /** The job and Nova sound the words ask for ("bells", "a pad", "flute", "horn stabs"), each
        empty when they don't say. */
    struct Ask
    {
        juce::String role, sound;
    };

    Ask askOf (const juce::String& words);

    /** A style's own job, and its Nova sound for a job. */
    juce::String roleFor (const juce::String& style);
    juce::String soundFor (const juce::String& style, const juce::String& role);

    /** Whether a Nova sound can play the job (a one-note lead can't hold a chord; a slow pad
        can't play an arpeggio). */
    bool fits (const juce::String& sound, const juce::String& role);

    /** Where its fader goes in the style's mix, dB from unity (measured with --bandrender). */
    float levelFor (const juce::String& style, const juce::String& role);

    struct Result
    {
        Part part;
        juce::String reply;
        agents::Request request;
        drummer::Groove drums;
        bool imaginedDrums = false, imaginedBass = false;
    };

    /** `previousRole` is this channel's last job (a follow-up keeps it unless the words name
        another); `currentSound` the channel's Nova sound (kept when it can play the job). With
        `chooseSound` (a channel made for it) it picks the style's sound and fader. `keys` is the
        keys player's part, for a lead to answer rather than talk over. */
    Result write (const juce::String& prompt, const Song&, const agents::Request* previous, const juce::String& previousRole,
                  const juce::String& currentSound, const drummer::Groove* drums, const bass::Part* bass, const keys::Part* keys,
                  bool chooseSound, bool songIsBlank, juce::int64 seed);

    SynthTurn turnFor (const juce::String& prompt, const Result&);
    std::optional<agents::Request> requestOf (const SynthTurn&);
}
