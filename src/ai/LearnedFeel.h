// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** How real drummers play a style, learned from the Groove MIDI Dataset (Jon Gillick, Adam Roberts, Jesse
    Engel, Douglas Eck and David Bamman, Google Magenta; CC BY 4.0): 13.6 hours of ten drummers on an
    electronic kit, played to a click. tools/groove/learn_feel.js reads its beats and averages, per style, the
    swing, how far ahead or behind each drum sits, how loosely, and for each drum where each step of the bar
    lands and how hard; LearnedFeel.cpp is that table. The AI drummer plays a style's feel from it where there
    is one (the swing, push and looseness into its Groove, the steps' timing and accents when the groove
    is rendered), and its own where there isn't (the programmed styles, tight on purpose; lofi's and
    neo-soul's deliberately lazy pocket). */
namespace nightshift::drummer
{
    struct LearnedFeel
    {
        const char* id;                 // a style ("funk") or a genre ("disco"): the genre's comes first
        const char* from;               // who it's learned from, as said: "funk drummers"
        int grooves = 0, minutes = 0;   // the dataset's beats it's learned from, and how long they play
        int steps = 16;                 // the bar's steps it knows: 16 (16ths) or 12 (8th-note triplets)
        float swing = 0.0f;             // as Groove's: 0 straight .. 1 a full triplet; below 0, the style keeps its own
        bool sixteenths = false;        // swinging the 16ths rather than the 8ths
        float looseMs = 0.0f;           // as Groove's looseness
        std::map<juce::String, float> pushMs;                    // by drum, empty where the style keeps its own (the dataset's: kick, snare, sidestick,
        std::map<juce::String, std::vector<float>> timingMs;     // hat_closed, hat_pedal, hat_open, ride, crash, toms)
        std::map<juce::String, std::vector<float>> accent;       // each step's: ms against the push, velocity factor
    };

    const std::vector<LearnedFeel>& learnedFeels();

    /** The feel learned for a style's or genre's id, or nullptr. */
    const LearnedFeel* learnedFeel (const juce::String& id);

    /** The learned drum that plays a voice's part (a clap the snare's, a tom any tom's), or "". */
    juce::String learnedDrum (const LearnedFeel& feel, const juce::String& voice);

    struct LearnedStep
    {
        float timingMs = 0.0f;          // against the drum's push
        float accent = 1.0f;            // a velocity factor
    };

    /** Where a voice's hit at a point in the bar (in beats from the bar's start) lands and how hard, in a
        learned feel: nothing off its grid (a 32nd, a triplet in a 16th feel) or past its four beats. */
    std::optional<LearnedStep> learnedStep (const LearnedFeel& feel, const juce::String& voice, double beatInBar);
}
