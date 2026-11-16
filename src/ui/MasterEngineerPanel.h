// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "ai/MasteringEngineer.h"
#include "Widgets.h"

namespace nightshift
{

/** The Master tab's first page: the AI mastering engineer. Master it (or words in the prompt bar,
    its AI Master side) and it listens to the whole song and sets the Master tab for the song's
    style, saying what it heard and what it did; each answer a version to step back to. Down the
    right: the version playing, the style and loudness it's for, before and after (how loud, the
    peaks, the tone against the style's), and Listen - Mastered, Before, or After at the same
    loudness as Before, to hear what it changed rather than only that it's louder. */
class MasterEngineerPanel : public juce::Component,
                            private Song::Listener,
                            private juce::Timer
{
public:
    MasterEngineerPanel (Song&, AudioEngine&);
    ~MasterEngineerPanel() override;

    /** Masters the song (a capture of the raw mix here, then the listening and working out on a
        thread of its own). Empty words: for the song's style, from scratch. */
    void ask (const juce::String& words);
    bool isWorking() const                          { return working; }
    juce::String getStatus() const                  { return status; }

    /** Puts the settings of a version back. */
    void showVersion (int index);

    /** 0 as mastered, 1 before, 2 after at the same loudness. */
    void setListen (int mode);
    int getListen() const                           { return listen; }

    /** The style it masters for when the words don't name one: its last version's, else the drummer's. */
    juce::String songStyle() const;

    /** An idea to start from was clicked: put it in the prompt bar. */
    std::function<void (const juce::String&)> onUseIdea;

    /** Mastered, or a version shown: the prompt bar follows. */
    std::function<void()> onStateChanged;
    std::function<void()> onAnswered;

    /** Finished, mastered or not. */
    std::function<void (bool mastered)> onDone;

    juce::Button& getMasterButton()                 { return masterButton; }
    juce::Component* getIdeaChip (int index)        { return ideaChips[index]; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Conversation;

    Song& song;
    AudioEngine& audio;

    std::unique_ptr<Conversation> conversation;
    juce::Viewport conversationView;
    juce::TextButton masterButton;
    IconButton previousButton { "Previous version", theme::icons::left() }, nextButton { "Next version", theme::icons::right() };
    SegmentedControl listenSwitch { { "Mastered", "Before", "After" }, SegmentedControl::Style::pill };
    juce::OwnedArray<Chip> ideaChips;

    bool working = false;
    int listen = 0;
    juce::String status;
    std::shared_ptr<std::atomic<float>> progress;
    std::shared_ptr<std::atomic<bool>> stillWanted = std::make_shared<std::atomic<bool>> (true);

    juce::Rectangle<int> sideArea, versionArea, factsArea, toneArea, listenLabelArea, statusArea, introArea;

    bool hasTurns() const                           { return ! song.masterTurns.empty(); }
    juce::var shownAnswer() const;
    void finished (const juce::String& words, const engineer::Result&);
    void refresh();
    void changed();
    void songChanged (Song::Change) override;
    void timerCallback() override;
    void paintSide (juce::Graphics&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterEngineerPanel)
};

} // namespace nightshift
