// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace nightshift
{

/** Neural Amp Modeler's core behind a small door: a model from a .nam file, run at the song's sample rate (between
    NAM's own resamplers when it was trained at another). Only NamRunner.cpp includes NAM, Eigen and the Windows
    headers its resampler brings, so none of them meet JUCE's. */
class NamRunner
{
public:
    ~NamRunner();

    /** The model in a .nam file, or null with why in `error`. */
    static std::unique_ptr<NamRunner> load (const std::filesystem::path&, std::string& error);

    /** Ready to run at this rate in blocks of up to maxBlock. It prewarms the model, so not on the audio thread. */
    void prepare (double sampleRate, int maxBlock);

    /** Mono in, mono out, n no more than prepare's maxBlock. */
    void process (float* in, float* out, int n);

    double trainedRate() const;
    bool hasLoudness() const;
    double loudnessDb() const;

    /** The resamplers' delay at the song's rate (0 when it runs at its own). */
    int latencySamples() const;

private:
    NamRunner();
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace nightshift
