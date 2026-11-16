// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "Widgets.h"

namespace nightshift
{

namespace controls { struct ParamControl; }

/** Nova, a synth channel's sound, the editor's Synth tab: the sound's name with the factory
    sounds a click away, and every control over five pages (oscillators, filter and envelopes,
    LFOs, the mod matrix and voice, effects), each section drawing what it does. */
class SynthPanel : public juce::Component,
                   private Song::Listener,
                   private juce::Timer
{
public:
    SynthPanel (Song&, AudioEngine&);
    ~SynthPanel() override;

    /** The synth channel it shows; 0 (or one that's gone) means the next request makes one. */
    void setChannel (int channelId);
    int getChannelId() const                    { return channelId; }
    Channel* channel() const;

    /** A factory sound on the channel (undoable). */
    void choosePreset (const juce::String& name);

    /** Steps through the factory sounds. */
    void stepPreset (int delta);

    static juce::StringArray pageNames()        { return { "Oscillators", "Filter", "Motion", "Mod matrix", "Effects" }; }
    void showPage (int index);
    int getPage() const                         { return page; }

    /** No synth channel to show: make one and return its id (0 if it can't). */
    std::function<int()> onNeedChannel;

    /** For tests: a parameter's control (on whichever page), the sound's name chip. */
    juce::Component* findControl (const juce::String& paramId) const;
    juce::Component& getPresetChip()            { return presetChip; }
    juce::Component& getNextButton()            { return nextButton; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Section;

    Song& song;
    AudioEngine& audio;
    int channelId = 0, page = 0;

    tracktion::Plugin::Ptr bound;               // the Nova the controls are bound to, kept alive while they are
    juce::OwnedArray<controls::ParamControl> paramControls;
    juce::OwnedArray<Section> sections;
    std::vector<std::vector<Section*>> pages;

    Chip channelChip, presetChip;
    IconButton previousButton { "Previous sound", theme::icons::left() }, nextButton { "Next sound", theme::icons::right() },
               addButton { "Add a Nova synth", theme::icons::plusLine(), theme::accent };
    SegmentedControl pageTabs { pageNames(), SegmentedControl::Style::pill };
    juce::Rectangle<int> pageArea, emptyArea;

    NovaPlugin* nova() const;
    void bindControls();
    void layoutPage();
    void refresh();
    void keepState();
    void play (Channel&, const nova::Patch&);
    void showChannelMenu();
    void songChanged (Song::Change) override;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthPanel)
};

} // namespace nightshift
