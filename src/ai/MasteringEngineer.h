// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"
#include "audio/Mastering.h"

/** The AI mastering engineer, offline: it listens to the whole mix as it comes into the master
    (no shaping, no push), hears how loud it is, how its tone balances, how much room its peaks
    take and how wide it is, and sets the Master tab - Tone, Punch, Width and the loudness - the
    way the style's masters sound: metalcore dense and bright with its low mids clean, trap's 808
    big and its hats crisp and the bass in the middle, lofi warm and dark, jazz left to breathe.
    It checks its tone moves by hearing the mix through them, corrects, works out the push into
    the limiter, and says what it did and why. A follow-up ("warmer", "louder", "less harsh", "for
    Spotify") moves on from where the settings are. No internet, no model. */
namespace nightshift::engineer
{
    /** What it hears in a mix. */
    struct Hearing
    {
        bool valid = false;
        float lufs = -100.0f, truePeakDb = -100.0f, rangeLu = 0.0f;
        std::array<float, 4> bands {};      // bass, body, presence, air: dB per octave against the mids (500 Hz to 2 kHz)
        std::array<float, 4> levels {};     // the same bands' own levels (dB per octave), for hearing a move land
        float peakToLoudness = 0.0f;        // dB: how much room the peaks take over the body of it
        float sidesDb = -100.0f;            // the sides against the centre
        float lowSidesDb = -100.0f;         // the same under 150 Hz
        float correlation = 1.0f;           // 1 mono, 0 wide, below 0 cancelling in mono
        double seconds = 0.0;
    };

    Hearing hear (const juce::AudioBuffer<float>&, double sampleRate);

    /** The same, through the Master tab's shaping (Tone, Punch, Width). */
    Hearing hearShaped (const juce::AudioBuffer<float>&, double sampleRate, const Song::Mastering&);

    /** The four tone bands' names, as the Master tab calls them. */
    const char* bandName (int band);

    /** How far (dB) a band may be from a style's balance before it's moved: masters differ. */
    float tolerance (int band);

    /** How a style's masters sound. */
    struct Profile
    {
        const char* style;
        const char* target;                 // a mastering::targets() id
        std::array<float, 4> tone;          // each band against the mids, dB per octave
        float punch;
        std::array<float, 3> shares;        // lows, mids, highs
        float width, monoBassHz;
        const char* sound;                  // in words, for the reply
    };

    const Profile& profileFor (const juce::String& style);

    /** What the words ask for: a style, a loudness, moves from where it is. */
    struct Ask
    {
        juce::String style, target;
        bool again = false;                 // "master it again", "start over": from scratch
        std::array<float, 4> tone {};       // dB to move each band
        float punch = 0.0f, width = 0.0f;
        int louder = 0;                     // steps of target up (or down)
        bool mono = false;
        juce::StringArray understood;

        bool movesOnly() const;             // nothing but moves (a follow-up on the settings as they are)
    };

    Ask askOf (const juce::String& words);

    struct Result
    {
        bool ok = false;
        juce::String problem;
        Song::Mastering settings;
        juce::String style, reply;
        Hearing before, after;              // the mix as it came in; through the shaping (before the limiter)
        float afterLufs = -100.0f, afterPeakDb = -100.0f, squeezeDb = 0.0f;
        float compareDb = 0.0f;             // the master turned down by this to sound as loud as the mix
        std::array<float, 4> wanted {};     // the style's tone

        juce::var answer() const;           // for the turn, and a follow-up
    };

    /** The whole job, on any thread but the message thread: hears the raw capture, decides from
        the words, the style (the words', else `songStyle`) and - for a follow-up - `previous` (the
        settings as they are), checks its tone, works out the push. `progress` (0..1) as it goes;
        returning false stops it. */
    Result master (const juce::String& words, const juce::File& rawCapture, const juce::String& songStyle,
                   const Song::Mastering* previous, const std::function<bool (float)>& progress = nullptr);

    /** The same, on a mix already in memory (for tests). */
    Result master (const juce::String& words, const juce::AudioBuffer<float>& mix, double sampleRate, const juce::String& songStyle,
                   const Song::Mastering* previous, const std::function<bool (float)>& progress = nullptr);

    Song::MasterTurn turnFor (const juce::String& prompt, const Result&);
}
