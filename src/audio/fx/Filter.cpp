// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "StockEffects.h"

namespace nightshift
{

const char* NightFilter::xmlTypeName = "nightshiftFilter";

NightFilter::NightFilter (tracktion::PluginCreationInfo info)
    : StockEffect (info)
{
    mode = &addParam ("mode", "Type", 0.0f, { 0.0f, 2.0f, 1.0f },
                      [] (float v) { const int m = juce::roundToInt (v); return m == 0 ? "Low pass" : m == 1 ? "Band pass" : "High pass"; });
    cutoff = &addParam ("cutoff", "Cutoff", 2000.0f, frequencyRange (20.0f, 20000.0f), hz());
    resonance = &addParam ("resonance", "Resonance", 0.2f, { 0.0f, 1.0f }, percent());
    drive = &addParam ("drive", "Drive", 0.0f, { 0.0f, 24.0f }, db());
    steep = &addParam ("steep", "24 dB/oct", 0.0f, toggleRange(), onOff());
    mix = &addParam ("mix", "Mix", 1.0f, { 0.0f, 1.0f }, percent());
}

NightFilter::~NightFilter()
{
    notifyListenersOfDeletion();
}

void NightFilter::prepare()
{
    reset();
}

void NightFilter::reset()
{
    first.reset();
    second.reset();
    cutoffNow = -1.0;
}

double NightFilter::responseDb (double freq) const
{
    // At 24 dB/oct a plain Butterworth stage goes first and the resonance sits on the second
    const int m = juce::roundToInt (mode->get());
    const double q = qFor (resonance->get());
    const bool twoStages = steep->get() >= 0.5f;

    dsp::Svf a, b;
    a.set (cutoff->get(), twoStages ? 0.5412 : q, sampleRate);
    double total = a.magnitudeDb (m, freq, sampleRate);

    if (twoStages)
    {
        b.set (cutoff->get(), q, sampleRate);
        total += b.magnitudeDb (m, freq, sampleRate);
    }

    return total;
}

void NightFilter::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    float* left;
    float* right;

    if (! getStereo (fc, left, right))
        return;

    juce::ScopedNoDenormals noDenormals;

    const int m = juce::roundToInt (mode->get());
    const double q = qFor (resonance->get());
    const bool twoStages = steep->get() >= 0.5f;
    const double target = cutoff->get();

    if (cutoffNow < 0.0)
        cutoffNow = target;

    // Cutoff glides in octaves over ~10 ms; coefficients follow every 16 samples
    const double glide = 1.0 - std::exp (-16.0 / (0.01 * sampleRate));
    const float driveGain = juce::Decibels::decibelsToGain (drive->get());
    const bool driven = drive->get() > 0.05f;
    const float makeup = 1.0f / std::sqrt (driveGain);
    const float wet = mix->get(), dry = 1.0f - wet;

    auto output = [m] (const dsp::Svf::Out& o, double k)
    {
        // The band pass scaled so its peak sits at 0 dB whatever the resonance
        return m == 0 ? o.low : m == 1 ? o.band * k : o.high;
    };

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        if ((i & 15) == 0)
        {
            cutoffNow *= std::pow (target / cutoffNow, glide);
            first.set (cutoffNow, twoStages ? 0.5412 : q, sampleRate);

            if (twoStages)
                second.set (cutoffNow, q, sampleRate);
        }

        float x[2] = { left[i], right != nullptr ? right[i] : left[i] };

        for (int ch = 0; ch < (right != nullptr ? 2 : 1); ++ch)
        {
            double v = driven ? std::tanh (x[ch] * driveGain) * makeup : x[ch];
            v = output (first.process (v, ch), first.k);

            if (twoStages)
                v = output (second.process (v, ch), second.k);

            x[ch] = (float) (x[ch] * dry + v * wet);
        }

        left[i] = x[0];

        if (right != nullptr)
            right[i] = x[1];
    }
}

} // namespace nightshift
