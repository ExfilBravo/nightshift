// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "ComputerKeyboard.h"
#include "model/Song.h"

namespace nightshift
{

void ComputerKeyboard::setEnabled (bool shouldBeEnabled)
{
    if (enabled == shouldBeEnabled)
        return;

    releaseAll();
    enabled = shouldBeEnabled;

    if (onChanged != nullptr)
        onChanged();
}

juce::String ComputerKeyboard::describe() const
{
    const int top = std::min (127, lowest + (int) std::strlen (noteKeys) - 1);
    return noteName (lowest) + " to " + noteName (top) + ", velocity " + juce::String (velocity);
}

bool ComputerKeyboard::modifierHeld()
{
    const auto mods = juce::ModifierKeys::getCurrentModifiersRealtime();
    return mods.isShiftDown() || mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown();
}

bool ComputerKeyboard::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    if (! enabled || mods.isShiftDown() || mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown())
        return false;

    // The character typed; failing that the key's own code, but only a plain one (F8's code
    // ends in the same byte as W's, F11's as Z's, but has a flag far above them)
    const int code = key.getKeyCode();
    const juce::juce_wchar typed = key.getTextCharacter() != 0 ? key.getTextCharacter()
                                 : code > 0 && code < 128 ? (juce::juce_wchar) code : 0;
    const char c = typed < 128 ? (char) juce::CharacterFunctions::toLowerCase (typed) : (char) 0;

    // Its keys act in update(), as they go down; here they're only kept from being shortcuts
    return c != 0 && (std::strchr (noteKeys, c) != nullptr || std::strchr (gapKeys, c) != nullptr
                       || std::strchr (controlKeys, c) != nullptr);
}

bool ComputerKeyboard::update()
{
    if (! enabled)
        return false;

    bool involved = false;
    const bool shortcut = modifierHeld();

    for (int i = 0; noteKeys[i] != 0; ++i)
    {
        const char c = noteKeys[i];
        const bool down = isKeyDown (c);
        const bool playing = sounding.count (c) > 0;
        involved = involved || down || playing;

        if (down && ! playing && ! shortcut && lowest + i <= 127)
        {
            sounding[c] = lowest + i;

            if (onNoteOn != nullptr)
                onNoteOn (lowest + i, velocity);
        }
        else if (! down && playing)
        {
            const int note = sounding[c];
            sounding.erase (c);

            if (onNoteOff != nullptr)
                onNoteOff (note);
        }
    }

    // Octave and velocity: once as each key goes down
    for (int i = 0; controlKeys[i] != 0; ++i)
    {
        const char c = controlKeys[i];

        if (! isKeyDown (c))
        {
            controlsDown.erase (c);
            continue;
        }

        involved = true;

        if (! controlsDown.insert (c).second || shortcut)
            continue;

        if (c == 'z' || c == 'x')
        {
            const int lastStart = 127 - (int) std::strlen (noteKeys) + 1;
            lowest = juce::jlimit (0, lastStart - lastStart % 12, lowest + (c == 'x' ? 12 : -12));
        }
        else
        {
            const auto* begin = std::begin (velocities);
            const auto* end = std::end (velocities);
            const auto* now = std::lower_bound (begin, end, velocity);
            velocity = c == 'v' ? (now + 1 < end ? *(now + 1) : *(end - 1)) : (now > begin ? *(now - 1) : *begin);
        }

        if (onMoved != nullptr)
            onMoved();
    }

    return involved;
}

bool ComputerKeyboard::poll (bool typingIntoText)
{
    if (! enabled)
        return false;

    if (typingIntoText || isSystemDialogOpen() || ! isAppInFront())
    {
        // Keys held now are typing (or for another app): none of them may act once it stops
        releaseAll();
        controlsDown.clear();

        for (int i = 0; controlKeys[i] != 0; ++i)
            if (isKeyDown (controlKeys[i]))
                controlsDown.insert (controlKeys[i]);

        return false;
    }

    return update();
}

void ComputerKeyboard::releaseAll()
{
    auto notes = std::move (sounding);
    sounding.clear();

    for (auto& [c, note] : notes)
        if (onNoteOff != nullptr)
            onNoteOff (note);
}

} // namespace nightshift
