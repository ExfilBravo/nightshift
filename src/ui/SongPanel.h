// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"
#include "ai/VibeListener.h"
#include "Widgets.h"

namespace nightshift
{

/** The AI tab's first agent: a whole song from one prompt. What to make is typed into the prompt
    bar (its AI Song side); the studio starts a new project, has the drummer, bassist, guitarist,
    keys and synth players write their parts, lays two minutes or more out in Arrange and has the
    mastering engineer master it (onMake). Here: ideas to start from; how it's going; then what it
    made, Play from the top, and a button for each player to open its agent and change its part.

    Or a song to go by: Listen to a song... (or a song file dropped here) and the band's ears hear
    its vibe, offline (ai/VibeListener.h). What it heard, the band's styles it's nearest to play it
    as, how close to keep to it, and Write a song with this vibe: a new song, never a copy. */
class SongPanel : public juce::Component,
                  public juce::FileDragAndDropTarget,
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

    /** Asks for a song file, then listens to it. */
    void chooseSongToListenTo();

    /** Listens to a song file for its vibe, on a thread of its own, then shows what it heard. */
    void listenTo (const juce::File&);

    /** Shows a vibe as if it had just been heard (the text check's view uses it too). */
    void showHeard (const vibes::Vibe&);

    /** The text check's view of it listening: a song's name and how far, no file, no thread. */
    void showListening (const juce::String& name, float done);

    bool isListening() const                        { return listening; }
    bool hasHeard() const                           { return heard != nullptr; }
    juce::String getVibeWords() const               { return plan.words; }

    std::function<void (const juce::String&)> onMake;
    std::function<void (const juce::String&)> onUseIdea;
    std::function<void (const juce::String& agent)> onOpenAgent;    // "drums", "bass", "guitar", "keys", "synth", "master"
    std::function<void()> onPlay;
    std::function<juce::String()> currentStatus;    // while it works: what the agent at work is saying

    juce::Button& getPlayButton()                   { return playButton; }
    juce::Button& getListenButton()                 { return listenButton; }
    juce::Button& getWriteButton()                  { return writeButton; }
    juce::Component* getIdeaChip (int index)        { return ideaChips[index]; }
    int getNumPlayerChips() const                   { return playerChips.size(); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Song& song;
    juce::TextButton playButton, listenButton, writeButton, anotherButton, stopButton;
    juce::OwnedArray<Chip> ideaChips, playerChips;
    SegmentedControl closeness { { "Close", "Some changes", "Loosely" }, SegmentedControl::Style::pill };
    std::unique_ptr<SegmentedControl> playAs;
    juce::StringArray playAsIds;
    juce::StringArray steps;
    juce::String asked, status;
    bool working = false;
    juce::Rectangle<int> introArea, textArea, sideArea, statusArea;

    // The vibe: listening (how far, and asked to stop), then what it heard and the song agent's words for it
    std::unique_ptr<juce::FileChooser> chooser;
    bool listening = false;
    juce::String listeningTo;
    std::shared_ptr<std::atomic<float>> listenProgress;
    std::shared_ptr<std::atomic<bool>> stopListening;
    std::unique_ptr<vibes::Vibe> heard;
    vibes::Plan plan;
    juce::int64 vibeSeed = 1;
    juce::Rectangle<int> summaryArea, playAsLabel, closenessLabel, planArea;

    enum class Mode { intro, working, listening, heard, made };
    Mode mode() const;

    void heardIt (const vibes::Vibe&);
    void updatePlan();
    juce::String aboutPlan() const;
    void refresh();
    void songChanged (Song::Change) override;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongPanel)
};

} // namespace nightshift
