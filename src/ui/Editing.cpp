// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Editing.h"
#include "Theme.h"

namespace nightshift
{

std::optional<EditCommand> editCommandFor (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();

    // Cmd on a Mac, Ctrl elsewhere; Alt kept out so Alt shortcuts stay their own
    if (mods.isCommandDown() && ! mods.isAltDown())
    {
        const auto letter = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);

        if (mods.isShiftDown())
            return std::nullopt;

        switch (letter)
        {
            case 'a': return EditCommand::selectAll;
            case 'c': return EditCommand::copy;
            case 'x': return EditCommand::cut;
            case 'v': return EditCommand::paste;
            case 'b': return EditCommand::duplicate;
            case 'q': return EditCommand::quantize;
            case 't': return EditCommand::split;
            default:  return std::nullopt;
        }
    }

    if (mods.isCommandDown() || mods.isAltDown() || mods.isCtrlDown())
        return std::nullopt;

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)   return EditCommand::remove;
    if (code == juce::KeyPress::escapeKey)                                           return EditCommand::deselect;
    if (code == juce::KeyPress::upKey)      return mods.isShiftDown() ? EditCommand::octaveUp : EditCommand::up;
    if (code == juce::KeyPress::downKey)    return mods.isShiftDown() ? EditCommand::octaveDown : EditCommand::down;
    if (code == juce::KeyPress::leftKey && ! mods.isShiftDown())   return EditCommand::left;
    if (code == juce::KeyPress::rightKey && ! mods.isShiftDown())  return EditCommand::right;
    return std::nullopt;
}

juce::String editCommandKey (EditCommand c)
{
    juce::String key;

    switch (c)
    {
        case EditCommand::selectAll:   key = "Ctrl+A"; break;
        case EditCommand::deselect:    key = "Esc"; break;
        case EditCommand::copy:        key = "Ctrl+C"; break;
        case EditCommand::cut:         key = "Ctrl+X"; break;
        case EditCommand::paste:       key = "Ctrl+V"; break;
        case EditCommand::duplicate:   key = "Ctrl+B"; break;
        case EditCommand::remove:      key = "Delete"; break;
        case EditCommand::quantize:    key = "Ctrl+Q"; break;
        case EditCommand::split:       key = "Ctrl+T"; break;
        case EditCommand::up:          key = "Up"; break;
        case EditCommand::down:        key = "Down"; break;
        case EditCommand::octaveUp:    key = "Shift+Up"; break;
        case EditCommand::octaveDown:  key = "Shift+Down"; break;
        case EditCommand::left:        key = "Left"; break;
        case EditCommand::right:       key = "Right"; break;
    }

    return theme::shortcutText (key);
}

} // namespace nightshift
