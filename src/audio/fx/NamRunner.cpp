// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

// Neural Amp Modeler's core (MIT, Steven Atkinson) and the resampler its plugin uses (AudioDSPTools, MIT, from
// iPlug2 and WDL, whose wdltypes.h includes windows.h: hence no min/max macros, and JUCE kept out of this file)
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif

#include "NamRunner.h"

#include <algorithm>
#include <cmath>
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"

// The two things the resampler takes from iPlug2, which NightShift doesn't build
namespace iplug
{
    constexpr double PI = 3.14159265358979323846;
}

constexpr int DEFAULT_BLOCK_SIZE = 512;

#include "dsp/ResamplingContainer/ResamplingContainer.h"

namespace nightshift
{

struct NamRunner::Impl
{
    std::unique_ptr<nam::DSP> dsp;
    std::unique_ptr<::dsp::ResamplingContainer<float, 1, 12>> resampler;
    double trained = 48000.0;

    // A slimmable model (a container of models of several sizes, or a WaveNet that can drop channels):
    // where NAM's size control (0 to 1) moves from one size to the next, and the size picked
    nam::SlimmableModel* slimmable = nullptr;
    std::vector<double> breakpoints;
    int size = 0;
};

NamRunner::NamRunner() : impl (std::make_unique<Impl>()) {}
NamRunner::~NamRunner() = default;

std::unique_ptr<NamRunner> NamRunner::load (const std::filesystem::path& path, std::string& error)
{
    std::unique_ptr<NamRunner> runner (new NamRunner());

    try
    {
        nam::DspLoadOptions options;
        options.prewarm = false;    // prepare() prewarms it, at the rate it will run at
        runner->impl->dsp = nam::get_dsp (path, options);
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return {};
    }

    auto& dsp = runner->impl->dsp;

    if (dsp == nullptr)
    {
        error = "no model in it";
        return {};
    }

    if (dsp->NumInputChannels() != 1 || dsp->NumOutputChannels() != 1)
    {
        error = "it has " + std::to_string (dsp->NumInputChannels()) + " inputs and " + std::to_string (dsp->NumOutputChannels())
              + " outputs, not one of each";
        return {};
    }

    runner->impl->trained = dsp->GetExpectedSampleRate() > 0.0 ? dsp->GetExpectedSampleRate() : 48000.0;

    // It loads whole (NAM's models start at their full size)
    if ((runner->impl->slimmable = dynamic_cast<nam::SlimmableModel*> (dsp.get())) != nullptr)
        runner->impl->breakpoints = runner->impl->slimmable->GetSlimmableSizeBreakpoints();

    runner->impl->size = runner->numSizes() - 1;
    return runner;
}

void NamRunner::prepare (double sampleRate, int maxBlock)
{
    // A model that runs at any rate runs at the song's; any other at the one it was trained at, between resamplers
    if (impl->dsp->SupportsArbitrarySampleRate() || std::abs (impl->trained - sampleRate) < 1.0)
    {
        impl->resampler.reset();
        impl->dsp->Reset (sampleRate, maxBlock);
    }
    else
    {
        impl->resampler = std::make_unique<::dsp::ResamplingContainer<float, 1, 12>> (impl->trained);
        impl->resampler->Reset (sampleRate, maxBlock);
        impl->dsp->Reset (impl->trained, (int) std::ceil (maxBlock * impl->trained / sampleRate) + 2);
    }
}

void NamRunner::process (float* in, float* out, int n)
{
    float* ins[1] = { in };
    float* outs[1] = { out };

    if (impl->resampler != nullptr)
    {
        auto* net = impl->dsp.get();
        impl->resampler->ProcessBlock (ins, outs, n, [net] (float** i, float** o, int k) { net->process (i, o, k); });
    }
    else
    {
        impl->dsp->process (ins, outs, n);
    }
}

double NamRunner::trainedRate() const       { return impl->trained; }
bool NamRunner::hasLoudness() const         { return impl->dsp->HasLoudness(); }
double NamRunner::loudnessDb() const        { return impl->dsp->GetLoudness(); }
int NamRunner::latencySamples() const       { return impl->resampler != nullptr ? impl->resampler->GetLatency() : 0; }

// NAM keeps these as metadata: the trainer writes what the reamp's level was, in dBu RMS for a sine at 0 dBFS
bool NamRunner::hasInputLevel() const       { return impl->dsp->HasInputLevel(); }
double NamRunner::inputLevelDbu() const     { return impl->dsp->HasInputLevel() ? impl->dsp->GetInputLevel() : 0.0; }

int NamRunner::numSizes() const
{
    return impl->slimmable != nullptr ? (int) impl->breakpoints.size() + 1 : 1;
}

int NamRunner::getSize() const              { return impl->size; }

void NamRunner::setSize (int index)
{
    const int n = numSizes();
    index = std::clamp (index, 0, n - 1);

    if (impl->slimmable == nullptr || index == impl->size)
        return;

    // NAM's control runs 0 to 1, a size between each pair of breakpoints: the middle of a size's own
    // stretch, so rounding can't land it on a neighbour, and 1 (all of it) for the whole model
    const double from = index == 0 ? 0.0 : impl->breakpoints[(size_t) index - 1];
    const double to = index == n - 1 ? 1.0 : impl->breakpoints[(size_t) index];
    impl->slimmable->SetSlimmableSize (index == n - 1 ? 1.0 : 0.5 * (from + to));
    impl->size = index;
}

} // namespace nightshift
