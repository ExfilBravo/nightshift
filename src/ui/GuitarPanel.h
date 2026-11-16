// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "ai/Guitar.h"
#include "Widgets.h"

namespace nightshift
{

/** The AI guitarist, beside the drummer and the bassist in the editor: the part it plays shown as
    tab, a section at a time, to learn it from (the strings with their tuning, a fret for each
    note, P.M. over palm-muted runs, x for scratches, h and / for hammer-ons and slides, the chord
    over each bar); what it said; its versions, sound, key and tuning; its sections; and Put in
    Arrange, under the drummer's. What to play is typed into the prompt bar (its AI Guitar side)
    and written at once by the offline guitarist, over the bassist's chords and the drummer's kick. */
class GuitarPanel : public juce::Component,
                    private Song::Listener
{
public:
    GuitarPanel (Song&, AudioEngine&);
    ~GuitarPanel() override;

    void setChannel (int channelId);
    int getChannelId() const                    { return channelId; }
    Channel* channel() const;

    /** Writes a part for the channel, making a guitar channel first if there's none. */
    void ask (const juce::String& prompt);
    juce::String getStatus() const              { return status; }

    /** The section whose tab shows. */
    void showSection (const juce::String& name);
    juce::String getShownSection() const        { return shownSection; }

    std::function<int()> onNeedChannel;
    std::function<void (int channelId, int patternId)> onShowPattern;
    std::function<void (const juce::String&)> onUseIdea;
    std::function<void()> onStateChanged;
    std::function<void()> onAnswered;

    /** For tests. */
    juce::Component* getIdeaChip (int index)    { return ideaChips[index]; }
    juce::Component* getSectionChip (int index) { return sectionChips[index]; }
    juce::Component& getArrangeButton()         { return arrangeButton; }
    juce::Component& getSoundChip()             { return soundChip; }
    juce::Component& getTab();
    int getTabBarsShown() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class TabView;

    Song& song;
    AudioEngine& audio;
    int channelId = 0;
    juce::int64 asks = 0;
    juce::String shownSection;

    std::unique_ptr<TabView> tab;
    juce::Viewport tabView;
    Chip soundChip;
    IconButton previousButton { "Previous version", theme::icons::left() }, nextButton { "Next version", theme::icons::right() },
               arrangeButton { "Put in Arrange", theme::icons::playlist() };
    juce::OwnedArray<Chip> sectionChips, ideaChips;

    juce::String status;
    juce::Rectangle<int> sideArea, versionArea, replyArea, introArea, statusArea;

    bool hasTurns() const;
    void refresh();
    void showVersion (int index);
    void showSoundMenu();
    void putInArrange();
    void changed();
    void songChanged (Song::Change) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarPanel)
};

} // namespace nightshift
