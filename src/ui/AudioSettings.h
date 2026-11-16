// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"

namespace nightshift
{

/** Audio settings (F10): the device, driver and buffer, a live readout of what that costs
    in latency, and the recording options. Lowest latency comes from ASIO with the
    smallest buffer that plays without crackles; the readout shows dropouts as you go.
*/
class AudioSettings : public juce::Component,
                      private juce::Timer,
                      private juce::ChangeListener,
                      private juce::ComponentListener
{
public:
    explicit AudioSettings (AudioEngine&);
    ~AudioSettings() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 600, optionsHeight = 214;

private:
    AudioEngine& audio;
    juce::AudioDeviceSelectorComponent selector;
    // (the metronome and the count-in moved to the metronome's panel and Record's menu)
    juce::ToggleButton lowLatency { "Low-latency monitoring: bypass plugins that add delay while an armed track monitors" },
                       preferInterface { "Open the MiniFuse with ASIO when NightShift starts" };
    juce::TextButton useInterface { juce::String ("Use MiniFuse ") + AudioEngine::lowLatencyName };
    juce::String readout, advice, bypassed;
    bool readoutIsGood = false;
    juce::Rectangle<int> readoutArea;

    // MIDI keyboards and controllers: a switch for each, under its title and above a hint
    juce::OwnedArray<juce::ToggleButton> midiSwitches;
    juce::StringArray midiNames;
    juce::Rectangle<int> midiTitleArea, midiHintArea;
    void refreshMidiInputs();
    int midiHeight() const              { return 34 + juce::jmax (1, midiSwitches.size()) * 26 + 46; }
    int wantedHeight() const            { return selector.getHeight() + optionsHeight + midiHeight(); }

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;
    void applyOptions();
    void refreshReadout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettings)
};

} // namespace nightshift
