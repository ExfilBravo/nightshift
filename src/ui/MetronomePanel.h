// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "model/Song.h"
#include "Widgets.h"

namespace nightshift
{

/** The metronome, as Neural DSP's plugins lay theirs out: a light for every beat of the bar
    (click one to cycle its accent: strong, normal, soft, off; right-click for the menu), the
    tempo with arrows, drag and Tap, the time signature with its groupings, the rhythm (pulses a
    beat), the sound, volume and pan, and Practise to hear it on its own. Here it also has the
    count-in before a take, and whether it clicks only while recording. The tempo and time
    signature are the song's. Opens from the metronome button (right-click or its corner). */
class MetronomePanel : public juce::Component,
                       private Song::Listener,
                       private juce::Timer
{
public:
    MetronomePanel (Song&, AudioEngine&);
    ~MetronomePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Keys pressed while it's open (Space plays, and so on): the studio's own handling. */
    std::function<bool (const juce::KeyPress&)> onKey;

    /** Tap tempo: the tempo is the time between the last two taps. */
    void tap (double atSeconds);

    static constexpr int width = 520, height = 396;

    // Public for tests: what you'd click
    struct BeatLights : public juce::Component,
                        public juce::SettableTooltipClient
    {
        explicit BeatLights (MetronomePanel& p) : panel (p) {}

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        /** The beat under a point, or -1. */
        int beatAt (juce::Point<int>) const;

        /** Click: the next accent round (strong, normal, soft, off). */
        void cycle (int beat);

        MetronomePanel& panel;
        juce::uint32 lastCount = 0, litAt = 0;
        int litBeat = -1, litPulse = 0;
    };

    BeatLights lights { *this };
    SegmentedControl onOff { { "Off", "On" }, SegmentedControl::Style::pill };
    juce::TextButton slower { "-" }, faster { "+" }, tapButton { "Tap" };
    ValueBox bpm { 20.0, 400.0, 0.01, 2 };
    IconButton practise { "Practise", theme::icons::play(), theme::accent };
    juce::ComboBox signature, rhythm, sound, countIn;
    Knob volume { 0.0, 1.0, 0.7 }, pan { -1.0, 1.0, 0.0, true };
    juce::ToggleButton recordingOnly { "Only while recording" };

private:
    Song& song;
    AudioEngine& audio;
    std::vector<double> taps;
    bool focusTaken = false;
    juce::Rectangle<int> lightsArea, tempoArea, knobsArea;

    void songChanged (Song::Change) override;
    void timerCallback() override;
    void syncFromEngine();
    void setOption (std::function<void (AudioEngine::Options&)>);
    void setTempo (double);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetronomePanel)
};

} // namespace nightshift
