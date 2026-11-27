// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

namespace nightshift::freezing
{

/** Set by the main window (MainComponent::mayEditNotes): asked before something rewrites a channel's notes
    (the AI band's players). A frozen channel's can't change, so it asks to unfreeze it and says false. */
inline std::function<bool (int channelId)> mayEditNotes;

inline bool mayEdit (int channelId)     { return mayEditNotes == nullptr || mayEditNotes (channelId); }

} // namespace nightshift::freezing
