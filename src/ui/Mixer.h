// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "Inspector.h"

namespace nightshift
{

/** The mixer (F9), as in Logic: a strip for every channel in Steps' order, then the buses,
    and the master pinned on the right. The strips scroll sideways when they don't fit.
    Route a channel into a bus with its Output, or share a bus's effect with Sends. */
class Mixer : public juce::Component,
              private Song::Listener
{
public:
    Mixer (Song&, AudioEngine&, ChannelRack&);
    ~Mixer() override;

    std::function<void (int effectId)> onOpenEffect;
    std::function<void (int channelId)> onOpenInstrument, onOpenPianoRoll, onShowSteps, onSelect;

    /** Adds the channels and buses whose meters it shows. */
    void addMeteredIds (std::vector<int>&) const;

    /** At the UI rate, after the engine's meters were read. */
    void refreshMeters();

    /** The strip for a channel or bus, for tests (null if there's none). */
    ChannelStrip* findStrip (int id);
    ChannelStrip& getMasterStrip()                  { return masterStrip; }
    juce::Button& getAddBusButton()                 { return addBusButton; }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int stripWidth = 140, addColumnWidth = 168;

private:
    struct Holder : public juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override     { if (painter != nullptr) painter (g); }
    };

    Song& song;
    AudioEngine& audio;
    ChannelRack& rack;
    juce::Viewport viewport;
    Holder holder;
    juce::OwnedArray<ChannelStrip> strips;      // channels, then buses
    ChannelStrip masterStrip;
    IconButton addBusButton { "Add bus", theme::icons::plusLine() };
    std::vector<int> builtIds;
    int busesStartX = 0, addColumnX = 0;
    juce::Rectangle<int> masterArea;

    void songChanged (Song::Change) override;
    void rebuild();
    void layoutStrips();
    void wire (ChannelStrip&);
    void paintHolder (juce::Graphics&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Mixer)
};

} // namespace nightshift
