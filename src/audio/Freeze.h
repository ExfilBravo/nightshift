// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"

namespace nightshift::freeze
{

/** Whether a pattern has notes for a channel. */
bool hasNotes (const Pattern&, int channelId);

/** The Arrange rows a channel's notes are on: those of the pattern clips whose patterns have notes for it. */
std::set<int> rowsOf (const Song&, int channelId);

/** What a channel's Song mode audio is rendered from, as a short code that changes when any of it
    does: the pattern clips that play its notes (where, on which row, how much of their pattern) and
    those notes, the tempo and its changes, the time signature, swing, its instrument and effects (which,
    and whether bypassed), and the automation of its instrument's and effects' controls (baked into the
    audio; its volume, pan and sends stay live). */
juce::String songStamp (const Song&, const Channel&);

/** The same for a pattern's loop, as Pattern mode plays it: its notes for the channel, its length and
    time signature, the start's tempo, swing, the instrument and effects. */
juce::String patternStamp (const Song&, const Channel&, const Pattern&);

/** Whether a frozen channel's audio still matches the song for the mode it's in (Pattern mode: the
    pattern being played), its files there. A channel not frozen never does. */
bool isCurrent (const Song&, const Channel&);

/** Every file a channel's freeze plays from. */
juce::StringArray filesOf (const Freeze&);

} // namespace nightshift::freeze
