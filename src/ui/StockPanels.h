// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "audio/fx/StockEffects.h"

namespace nightshift
{

/** Nightshift's own panel for one of its stock effects, the content of its window: a
    display (the EQ's curve over an analyser, the compressor's curve and meters, the
    reverb's decay, the delay's echoes, the chorus's sweep, the filter's response) and
    knobs, switches and fields for its parameters. Controls carry their parameter's id as
    their component ID. */
std::unique_ptr<juce::Component> createStockPanel (StockEffect&, Song&, AudioEngine&);

} // namespace nightshift
