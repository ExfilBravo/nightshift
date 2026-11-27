// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

namespace nightshift::mp3
{

/** The bit rates Export offers an MP3 at, best first: 320 kbps is the default. */
const std::vector<int>& bitRates();

/** The version of LAME built in, as it says it ("3.100"). */
juce::String lameVersion();

/** Encodes an audio file (a WAV NightShift rendered) as an MP3 with LAME, built into NightShift: `kbps`
    at a constant bit rate, joint stereo (mono stays mono), at LAME's high quality setting, with its tag
    at the front (the exact length, so players don't add silence) and, given one, the title as an ID3
    tag. Returns an empty string when it's written, or what went wrong. `progress` (0..1) is called as
    it goes; returning false from it stops the encoding. */
juce::String encode (const juce::File& source, const juce::File& mp3, int kbps, const juce::String& title = {},
                     std::function<bool (float)> progress = nullptr);

} // namespace nightshift::mp3
