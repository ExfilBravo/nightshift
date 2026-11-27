// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"

namespace nightshift
{

/** The editing the piano roll and Arrange share: picking several things, copy and paste,
    duplicate, quantize (notes), split (clips), moving what's picked. Ctrl is Cmd on a Mac. */
enum class EditCommand
{
    selectAll,      // Ctrl+A
    deselect,       // Escape
    copy,           // Ctrl+C
    cut,            // Ctrl+X
    paste,          // Ctrl+V
    duplicate,      // Ctrl+B, as in FL: a copy right after
    remove,         // Delete or Backspace
    quantize,       // Ctrl+Q: notes onto the snap
    split,          // Ctrl+T: clips in two at the song position
    up, down,       // Up and Down: a semitone (notes)
    octaveUp, octaveDown,   // Shift+Up and Shift+Down: an octave (notes)
    left, right     // Left and Right: a snap step earlier or later
};

/** The command a key press means, if it's one of them. */
std::optional<EditCommand> editCommandFor (const juce::KeyPress&);

namespace automation
{
    /** Set by the main window: a fader's or knob's right-click menu (Automate it in Arrange; link
        it to a knob on a MIDI controller, or forget the link), by the control's owner and key as
        an automation lane names them (Song::AutomationLane), or by the engine's parameter. */
    inline std::function<void (int owner, const juce::String& param, const juce::String& name)> showMenu;
    inline std::function<void (tracktion::AutomatableParameter&, const juce::String& name)> showMenuFor;

    /** Set by the main window: a knob's parameter grabbed (down) or let go, so recording
        automation writes its moves; not `held`, a change with nothing held (a switch flicked, a
        choice made), written until it's left alone a moment. */
    inline std::function<void (tracktion::AutomatableParameter&, bool down, bool held)> touchFor;

    inline void touch (tracktion::AutomatableParameter& p, bool down, bool held = true)
    {
        if (touchFor != nullptr)
            touchFor (p, down, held);
    }
}

/** Its key, as a menu shows it ("Ctrl+C", "Cmd+C" on a Mac). */
juce::String editCommandKey (EditCommand);

namespace clipboard
{
    /** Notes copied in the piano roll. Their starts count from `anchor`, the snap line at or
        before the first of them when copied, so a paste keeps any timing off the grid. */
    struct Notes
    {
        std::vector<Note> notes;
        int anchor = 0;             // where in their pattern they came from
        int span = 0;               // how far on a copy of them goes to follow them
    };

    /** Clips copied in Arrange: starts count from the earliest, tracks from the highest. */
    struct Clips
    {
        std::vector<PlaylistClip> patterns;
        std::vector<AudioClip> audio;
        int anchor = 0, topRow = 0, span = 0;
        bool isEmpty() const        { return patterns.empty() && audio.empty(); }
    };

    inline Notes notes;
    inline Clips clips;

    /** How far on a copy of things over [from, to) ticks goes to follow them: whole bars for
        more than half a bar's worth, else whole beats (a one-bar phrase repeats a bar later,
        a single hit on the next beat). */
    inline int followDistance (int from, int to, int barTicks, int beatTicks)
    {
        const int extent = std::max (1, to - from);
        const int unit = extent > barTicks / 2 ? barTicks : beatTicks;
        return std::max (unit, (extent + unit - 1) / unit * unit);
    }
}

} // namespace nightshift
