// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "ai/SynthPart.h"
#include "ai/OfflineSynth.h"
#include "Widgets.h"

namespace nightshift
{

/** The AI synth player, the last of the band: what was asked and what it played, each answer a
    version to step back to; the version playing, its key and job (a pad, an arpeggio, a lead,
    stabs), its sections (patterns to open) and its Nova sound; and Put in Arrange, under the band.
    What to play is typed into the prompt bar (its AI Synth side) and written at once by the
    offline synth player, over the bassist's chords and the drummer's kick. */
class SynthPlayerPanel : public juce::Component,
                  private Song::Listener
{
public:
    SynthPlayerPanel (Song&, AudioEngine&);
    ~SynthPlayerPanel() override;

    /** The channel it writes for; 0 (or a channel that's gone) means the next request makes one. */
    void setChannel (int channelId);
    int getChannelId() const                    { return channelId; }
    Channel* channel() const;

    /** Writes a part for the channel, making a synth channel first if there's none (on the Nova
        sound the style wants for the job). */
    void ask (const juce::String& prompt);
    juce::String getStatus() const              { return status; }

    /** No channel to write for: make one playing this sound and return its id (0 if it can't). */
    std::function<int (const juce::String& sound, const juce::String& role)> onNeedChannel;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthPlayerPanel)
};

} // namespace nightshift
