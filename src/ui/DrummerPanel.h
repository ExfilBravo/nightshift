// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "ai/Drummer.h"
#include "ai/OfflineDrummer.h"
#include "Widgets.h"

namespace nightshift
{

/** The AI drummer, the editor's fourth tab: the conversation with it (each answer a version
    you can step back to), the version playing with its sections (patterns you can open,
    edit and lay out in Arrange), the kit, and swing, looseness and dynamics, which reshape
    it without asking again. What to play is typed into the DrummerBar above everything, and
    written at once by the offline drummer (drummer::offline), on this computer.
*/
class DrummerPanel : public juce::Component,
                     private Song::Listener
{
public:
    DrummerPanel (Song&, AudioEngine&);
    ~DrummerPanel() override;

    /** The channel it writes for; 0 (or a channel that's gone) means the next request makes one. */
    void setChannel (int channelId);
    int getChannelId() const                    { return channelId; }
    Channel* channel() const;

    /** Writes a part for the channel from a prompt, making a kit channel first if there's none.
        With no kits at all it keeps the prompt and writes once they're fetched. */
    void ask (const juce::String& prompt);
    juce::String getStatus() const              { return status; }
    bool isError() const                        { return statusIsError; }
    juce::String getPending() const             { return pending; }

    /** Downloads the free AVL kits that aren't here yet (a card shows how far it's got), then
        sends what was waiting for them. */
    void getKits();
    bool isFetchingKits() const                 { return kitFetcher != nullptr; }
    juce::String getKitNote() const             { return kitNote; }

    /** The kits arrived: the sidebar lists them. */
    std::function<void()> onKitsArrived;

    /** No channel to write for: make a drum kit channel and return its id (0 if it can't). */
    std::function<int()> onNeedChannel;

    /** A section's pattern was clicked: show it (the studio opens the piano roll on it). */
    std::function<void (int channelId, int patternId)> onShowPattern;

    /** An idea to start from was clicked: put it in the prompt bar. */
    std::function<void (const juce::String&)> onUseIdea;

    /** Written, or the channel changed: the prompt bar follows. */
    std::function<void()> onStateChanged;

    /** An answer arrived and was played into patterns. */
    std::function<void()> onAnswered;

    /** For tests: the components a click would land on. */
    juce::Component& getKitsButton()            { return kitsButton; }
    juce::Component* getIdeaChip (int index)    { return starterChips[index]; }
    juce::Component* getSectionChip (int index) { return sectionChips[index]; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Conversation;
    class KitFetcher;

    Song& song;
    AudioEngine& audio;
    int channelId = 0;

    std::unique_ptr<Conversation> conversation;
    juce::Viewport conversationView;
    Chip kitChip;
    IconButton previousButton { "Previous version", theme::icons::left() }, nextButton { "Next version", theme::icons::right() },
               arrangeButton { "Put in Arrange", theme::icons::playlist() },
               kitsButton { "Get the free drum kits", theme::icons::exportLine(), theme::accent };
    std::unique_ptr<KitFetcher> kitFetcher;
    juce::OwnedArray<Chip> sectionChips, starterChips;
    LabelledBar swing { "Swing", 0.0, 1.0, 0.0 }, looseness { "Looseness", 0.0, 2.0, 1.0 }, dynamics { "Dynamics", 0.0, 2.0, 1.0 };
    juce::TextButton writtenSwing { "As written" };
    juce::String status, pending, kitNote;
    bool statusIsError = false, kitsMissing = false, kitNoteIsError = false, offerStudio = false;
    juce::int64 asks = 0;           // a new take each ask (the offline drummer's seed)
    juce::Rectangle<int> sideArea, versionArea, feelArea, statusArea, card, introArea, kitNoteArea, offerArea;

    std::optional<drummer::Groove> shownGroove() const;
    bool hasTurns() const;
    void refresh();
    void showVersion (int index);
    void showKitMenu();
    void kitProgress (float, const juce::String&);
    void kitsFetched (const juce::String& error);
    void paintOffer (juce::Graphics&);
    void played (Channel&, const DrummerTurn&);
    void changed();
    void songChanged (Song::Change) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrummerPanel)
};

//==============================================================================
/** The AI tab's prompt, over whichever agent is picked: say what the drummer (or the bassist, the
    guitarist...) should play, or what to change, and press Enter. It shows who it writes for (click
    to pick another channel, or a new one). */
class DrummerBar : public juce::Component
{
public:
    DrummerBar();

    /** Who it talks to: the AI drummer, bassist, guitarist or keys player. */
    enum class Mode { drummer, bass, guitar, keys, synth, master, song };
    void setMode (Mode);
    Mode getMode() const                        { return mode; }

    /** Its name was clicked while showing the other AI's: swap to that one. */
    std::function<void()> onSwitchMode;

    static constexpr int height = 50;

    /** Enter or Write it, with what was typed. */
    std::function<void (const juce::String&)> onSend;

    /** Its name: the Drummer tab. */
    std::function<void()> onOpen;

    /** The "for ..." chip: which channel it writes for. */
    std::function<void()> onTargetClicked;

    /** Escape: back to the studio's keys. */
    std::function<void()> onDone;

    void setTarget (const juce::String& label, juce::Colour swatch);

    /** Its name's width; 0 hides it (in the AI tab, the agents' list says who it talks to). */
    void setNameWidth (int width)               { nameWidth = width; resized(); }
    void setText (const juce::String&);
    juce::String getText() const                { return prompt.getText(); }
    void focus()                                { prompt.grabKeyboardFocus(); }
    juce::TextEditor& getEditor()               { return prompt; }
    Chip& getTargetChip()                       { return target; }
    juce::Component& getNameButton()            { return nameButton; }
    juce::Component& getWriteButton()           { return writeButton; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Mode mode = Mode::drummer;
    IconButton nameButton { "AI Drummer", theme::icons::sparkle(), theme::accent },
               writeButton { "Write it", theme::icons::right(), theme::accent };
    juce::TextEditor prompt;
    Chip target;
    int nameWidth = 160;

    void send();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrummerBar)
};

} // namespace nightshift
