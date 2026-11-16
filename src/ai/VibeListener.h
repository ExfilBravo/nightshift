// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** The AI band's ears: a song (an MP3, or a WAV, AIFF, FLAC or Ogg file) heard for its vibe, offline, on this
    computer. Its tempo and feel (swing, a kick on every beat, a half-time backbeat), its key, a chord a bar, its
    sections (where the music changes, which parts come back, how loud each is), its sound (dark or bright, clean or
    gritty, how much low end) and the band's style it's nearest (its drum pattern against the band's own drummer's,
    its tempo, its sound). From that, the song agent's words for a new song with that vibe: as close to it as asked,
    or looser, never a copy (the band writes its own parts: the vibe comes through in the style, tempo, key, chords,
    form and where it's quiet or loud). */
namespace nightshift::vibes
{
    struct Section
    {
        juce::String kind;                  // intro, verse, prechorus, chorus, bridge, breakdown, outro
        juce::String label;                 // A, B, C...: the same letter is the same music coming back
        int startBar = 0, bars = 0;
        float energyDb = 0.0f;              // its loudness against the loudest section's (0 the loudest)
        juce::StringArray chords;           // a chord a bar, as heard
    };

    struct StyleGuess
    {
        juce::String style;                 // an id of agents::styles()
        float score = 0.0f;                 // 0..1
        juce::String why;                   // what pointed to it, in words
    };

    using Pattern = std::array<std::array<float, 16>, 3>;  // the drums' strength on each 16th of a bar: low (kick), mid (snare), high (hats)

    struct Vibe
    {
        juce::String name;                  // the song's file name, without its extension
        juce::String problem;               // why it couldn't listen; empty when it did
        double seconds = 0.0;               // how much was heard (at most the first eight minutes)
        double tempo = 0.0;                 // bpm
        float tempoConfidence = 0.0f;       // 0..1
        double firstBar = 0.0;              // where its first bar starts, in seconds
        int keyRoot = 0;                    // pitch class, C = 0
        juce::String mode = "major";        // major or minor
        float keyConfidence = 0.0f;         // 0..1
        float swing = 0.0f;                 // 0 straight .. 1 a full triplet: the 8ths, or (swingSixteenths) the 16ths
        bool swingSixteenths = false;
        bool fourOnFloor = false;           // a kick on every beat
        bool halfTime = false;              // the backbeat on 3
        float brightness = 0.5f;            // 0 dark .. 1 bright
        float grit = 0.0f;                  // 0 clean .. 1 distorted
        float lowEnd = 0.5f;                // 0 light .. 1 heavy low end
        Pattern pattern {};

        // What those come from, as measured (the band's ears hold each style's: ai/StyleEars.h)
        float highShare = 0.0f;             // the power over 2 kHz against all of it over 200 Hz
        float heldFlatness = 0.0f;          // how flat, noise-like, the held sound's 1 to 6 kHz is (the drums left out)
        float lowShare = 0.0f;              // the power under 120 Hz against all of it
        float minorShare = 0.0f;            // of its bars' chords, how many are minor
        float seventhShare = 0.0f;          // ... sevenths
        float powerShare = 0.0f;            // ... power chords
        std::vector<Section> sections;
        std::vector<StyleGuess> styles;     // nearest first

        bool ok() const                     { return problem.isEmpty(); }
        int bars() const;
        juce::String keyName() const;       // "E minor"
        juce::String summary() const;       // what it heard, in words
    };

    /** Listens to a song from a file: its first eight minutes, as one channel. `progress` hears 0..1 as it goes; it
        stops early (the vibe saying why) when `shouldStop` says so. */
    Vibe listen (const juce::File&, std::function<void (float)> progress = {}, std::function<bool()> shouldStop = {});

    /** Listens to one channel of audio already read. */
    Vibe listen (const float* samples, int numSamples, double sampleRate, const juce::String& name,
                 std::function<void (float)> progress = {}, std::function<bool()> shouldStop = {});

    /** The file types it can listen to, for a file chooser ("*.mp3;*.wav;..."). */
    juce::String fileTypes();

    /** The song agent's words for a new song with a vibe: its style (or `style`, when one is given), tempo, key,
        swing, form, each kind of section's chords and how loud it is, a half time or a kick on every beat the style
        doesn't have already. `closeness` 1 keeps them as heard; lower, the tempo moves (up to 8%) and chords are
        swapped for their neighbours (the vi for the I, the ii for the IV...); under a half, the key may turn to its
        relative or move a few steps and the form may lose a verse or gain a bridge. `seed` picks which. */
    struct Plan
    {
        juce::String words;                 // for the song agent
        juce::StringArray kept, changed;    // what comes through as heard, and what was changed, in words
    };

    Plan planFrom (const Vibe&, float closeness, juce::int64 seed, const juce::String& style = {});
}
