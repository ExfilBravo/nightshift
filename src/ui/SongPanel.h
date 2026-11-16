// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"
#include "Widgets.h"

namespace nightshift
{

/** The AI tab's first agent: a whole song from one prompt. What to make is typed into the prompt
    bar (its AI Song side); the studio starts a new project, has the drummer, bassist, guitarist,
    keys and synth players write their parts, lays two minutes or more out in Arrange and has the
    mastering engineer master it (onMake). Here: ideas to start from; how it's going; then what it
    made, Play from the top, and a button for each player to open its agent and change its part. */
class SongPanel : public juce::Component,
                  private Song::Listener,
                  private juce::Timer
{
public:
    explicit SongPanel (Song&);
    ~SongPanel() override;

    /** Asks the studio to make it. */
    void ask (const juce::String& words);

    /** The studio, as it goes: started, each step, finished (the song's prompt and reply set), or
        not (why). */
    void started (const juce::String& words);
    void step (const juce::String& what);
    void finished();
    void failed (const juce::String& problem);
    bool isWorking() const                          { return working; }
    juce::String getStatus() const                  { return status; }

    std::function<void (const juce::String&)> onMake;
    std::function<void (const juce::String&)> onUseIdea;
    std::function<void (const juce::String& agent)> onOpenAgent;    // "drums", "bass", "guitar", "keys", "synth", "master"
    std::function<void()> onPlay;
    std::function<juce::String()> currentStatus;    // while it works: what the agent at work is saying

    juce::Button& getPlayButton()                   { return playButton; }
    juce::Component* getIdeaChip (int index)        { return ideaChips[index]; }
    int getNumPlayerChips() const                   { return playerChips.size(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Song& song;
    juce::TextButton playButton;
    juce::OwnedArray<Chip> ideaChips, playerChips;
    juce::StringArray steps;
    juce::String asked, status;
    bool working = false;
    juce::Rectangle<int> introArea, textArea, sideArea, statusArea;

    void refresh();
    void songChanged (Song::Change) override;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongPanel)
};

} // namespace nightshift
