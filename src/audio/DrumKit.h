// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

namespace nightshift::kit
{

/** Names of the built-in sounds, referenced from projects as "kit:<name>". */
juce::StringArray getSoundNames();

/** Synthesises one kit sound as a mono buffer. Deterministic for a given name. */
juce::AudioBuffer<float> render (const juce::String& name, double sampleRate);

/** Where the kit WAVs live: %APPDATA%\Nightshift\Kit. */
juce::File getFolder();

/** Writes any missing kit WAVs (24-bit, 48 kHz). Cheap when they already exist. */
void ensureWritten();

/** Writes the whole kit into the given folder, replacing what is there. */
bool writeTo (const juce::File& folder);

juce::File fileFor (const juce::String& name);

} // namespace nightshift::kit
