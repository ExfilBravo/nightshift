// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "Editing.h"
#include "audio/AudioEngine.h"
#include "audio/Kits.h"
#include "model/Song.h"

namespace nightshift
{

/** Piano roll for one channel in the current pattern: with the draw tool, click to add
    (and keep dragging to place), drag the right edge to resize, right-click to delete;
    the select tool moves and stretches, and drags a box over notes to select them (Ctrl+drag
    does with any tool; Shift+click adds one). What's selected moves, stretches, copies,
    pastes, duplicates, quantizes and transposes together (createEditMenu, and its keys). The
    erase tool deletes. A velocity lane runs underneath.

    It edits the channel selected in the rack, except audio tracks, which have no notes:
    selecting one leaves the piano roll on the last instrument. Its channel picker, tools
    and snap live in the editor's header (createChannelMenu, setTool, setSnap).
*/
class PianoRoll : public juce::Component,
                  private Song::Listener,
                  private juce::ScrollBar::Listener
{
public:
    PianoRoll (Song&, AudioEngine&);
    ~PianoRoll() override;

    void refreshPlayhead();
    void centreOnNotes();
    juce::String getTitle() const;
    std::function<void()> onTitleChanged;

    /** The channel being edited, or 0 when the song has no instruments. */
    int getChannelId() const                { return editId; }

    /** The channel picker's menu for the current pattern; choosing selects that channel. */
    juce::PopupMenu createChannelMenu();

    /** Select moves and resizes, and drags a box to select; draw also adds on empty space; erase deletes. */
    enum class Tool { select, draw, erase };
    void setTool (Tool t)                   { tool = t; }

    /** Where notes land and how they stretch, in ticks (1 for none). */
    void setSnap (int ticks)                { snap = std::max (1, ticks); }

    /** An Edit menu command on the selected notes (quantize works on all of them when none
        are selected). Paste goes `pasteAt` ticks into the pattern if given, else where the
        mouse points in the grid, else right after the notes it copied. False when there was
        nothing to do it to. */
    bool edit (EditCommand, std::optional<int> pasteAt = std::nullopt);
    bool canEdit (EditCommand) const;

    /** The Edit menu: those commands with their keys. */
    juce::PopupMenu createEditMenu();

    /** The selected notes, as indices into the edited channel's notes in this pattern. */
    const std::set<int>& getSelection() const   { return chosen; }

    /** Where a note (by index, as above) is drawn, for tests that click on it. */
    juce::Rectangle<float> boundsOfNote (int index) const
    {
        auto& list = const_cast<PianoRoll*> (this)->notes();
        return juce::isPositiveAndBelow (index, (int) list.size()) ? noteBounds (list[(size_t) index]) : juce::Rectangle<float>();
    }

    /** A line for the toast (what quantize did, say). */
    std::function<void (const juce::String&)> onMessage;

    /** Clicked: the editing keys go to whichever editor was clicked last. */
    std::function<void()> onActivated;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Drag { none, move, resize, erase, velocity, keyboard, scrub, box };

    Song& song;
    AudioEngine& audio;
    juce::ScrollBar hScroll { false }, vScroll { true };

    double pxPerTick = 0.5, scrollTicks = 0.0, topKey = 84.0;
    static constexpr int keyHeight = 14, rulerHeight = 26, keysWidth = 58, drumKeysWidth = 128, velocityHeight = 64;
    int snap = ticksPerStep, newLength = ticksPerBeat / 2;
    std::shared_ptr<const drumkits::Kit> drumKit;   // the kit whose drums name the rows (kit channels)
    bool drumPlugin = false;                         // a drum plugin the drummer writes for: General MIDI names
    Tool tool = Tool::draw;

    Drag drag = Drag::none;
    int dragIndex = -1, grabOffset = 0, lastPreviewKey = -1, keyboardKey = -1;
    int editId = 0;
    double playheadTicks = -1.0;
    juce::Rectangle<int> rulerArea, keysArea, gridArea, velocityArea;
    std::vector<Note> noNotes;

    std::set<int> chosen;                   // the selected notes
    std::set<int> boxBase;                  // what was selected before a box dragged with Shift
    std::vector<Note> dragOrigin;           // the notes as they were when a drag began
    juce::Point<float> boxStart, boxEnd;
    bool ownChange = false;                 // a change this piano roll is making (keeps the selection)

    void songChanged (Song::Change) override;
    void changed();
    void sortNotes();
    void eraseNote (int index);
    void removeChosen();
    void copyChosen();
    juce::String gridName (int ticks) const;
    void followSelection();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void updateScrollBars();

    std::vector<Note>& notes();
    const Channel* channel() const;
    juce::String drumName (int key) const;
    void updateDrums();
    void paintDrumNames (juce::Graphics&, int bottomKey, int top);
    float tickToX (double tick) const;
    double xToTick (float x) const;
    float keyToY (int key) const;
    int yToKey (float y) const;
    int snapDown (double tick) const;
    int noteAt (juce::Point<float>) const;
    int velocityNoteAt (juce::Point<float>) const;
    juce::Rectangle<float> noteBounds (const Note&) const;
    static bool isNearEnd (juce::Rectangle<float> note, juce::Point<float>);
    void preview (int key, int velocity = 100);

    void paintGrid (juce::Graphics&);
    void paintKeyboard (juce::Graphics&);
    void paintVelocity (juce::Graphics&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};

} // namespace nightshift
