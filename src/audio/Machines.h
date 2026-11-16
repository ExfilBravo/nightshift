// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** Nightshift's own drum machines, synthesised (no samples to fetch, nothing to license): an
    808 and a 909 after the classic circuits, each drum in three layers that hit harder in
    tone as well as level, with two takes a layer. Written into drumkits::folder() as SFZ kits
    the first time, and again whenever the synthesis changes. */
namespace nightshift::drumkits::machines
{
    /** The machines' kit folders ("NS_808", "NS_909"). */
    juce::StringArray ids();

    /** Writes any machine that isn't there (or is from older synthesis). Quick: well under a
        second for both. Returns what went wrong, or "". */
    juce::String ensure();
}
