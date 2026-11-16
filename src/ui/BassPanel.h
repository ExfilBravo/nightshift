// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "ai/Bass.h"
#include "Widgets.h"

namespace nightshift
{

/** The AI bassist, beside the AI drummer in the editor: what was asked and what it played, each
    answer a version you can step back to; the version playing, its key, its sections (patterns to
    open and edit) and its sound; and Put in Arrange, which lays the sections under the drummer's.
    What to play is typed into the prompt bar above everything (its AI Bass side) and written at
    once by the offline bassist (bass::offline), on this computer, locked to the drummer's part. */
class BassPanel : public juce::Component,
                  private Song::Listener
{
public:
    BassPanel (Song&, AudioEngine&);
    ~BassPanel() override;

    /** The channel it writes for; 0 (or a channel that's gone) means the next request makes one. */
    void setChannel (int channelId);
    int getChannelId() const                    { return channelId; }
    Channel* channel() const;

    /** Writes a part for the channel, making a bass channel first if there's none. */
    void ask (const juce::String& prompt);
    juce::String getStatus() const              { return status; }

    /** No channel to write for: make a bass channel and return its id (0 if it can't). */
    std::function<int()> onNeedChannel;

    /** A section's pattern was clicked: show it (the studio opens the piano roll on it). */
    std::function<void (int channelId, int patternId)> onShowPattern;

    /** An idea to start from was clicked: put it in the prompt bar. */
    std::function<void (const juce::String&)> onUseIdea;

    /** Written, or the channel changed: the prompt bar follows. */
    std::function<void()> onStateChanged;

    /** A part arrived and was played into patterns. */
    std::function<void()> onAnswered;

    /** For tests: the components a click would land on. */
    juce::Component* getIdeaChip (int index)    { return ideaChips[index]; }
    juce::Component* getSectionChip (int index) { return sectionChips[index]; }
    juce::Component& getArrangeButton()         { return arrangeButton; }
    juce::Component& getSoundChip()             { return soundChip; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Conversation;

    Song& song;
    AudioEngine& audio;
    int channelId = 0;
    juce::int64 asks = 0;           // a new take each ask (the offline bassist's seed)

    std::unique_ptr<Conversation> conversation;
    juce::Viewport conversationView;
    Chip soundChip;
    IconButton previousButton { "Previous version", theme::icons::left() }, nextButton { "Next version", theme::icons::right() },
               arrangeButton { "Put in Arrange", theme::icons::playlist() };
    juce::OwnedArray<Chip> sectionChips, ideaChips;

    juce::String status;
    juce::Rectangle<int> sideArea, versionArea, introArea, statusArea;

    bool hasTurns() const;
    void refresh();
    void showVersion (int index);
    void showSoundMenu();
    void putInArrange();
    void changed();
    void songChanged (Song::Change) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassPanel)
};

} // namespace nightshift
