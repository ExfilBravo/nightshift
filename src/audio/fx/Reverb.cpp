// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "StockEffects.h"

namespace nightshift
{

const char* NightReverb::xmlTypeName = "nightshiftReverb";

namespace
{
    // Line lengths at size 0.5, in ms: spread out and sharing no factors, so echoes don't
    // pile up on the same moments
    constexpr double lineMs[NightReverb::numLines] = { 31.7, 37.9, 43.1, 47.3, 53.9, 59.3, 67.1, 73.7 };
    constexpr double lfoHz[NightReverb::numLines] = { 0.31, 0.43, 0.53, 0.61, 0.71, 0.83, 0.97, 1.09 };

    // Allpasses ahead of the network smear each hit into a cloud before it starts echoing
    constexpr double diffuserMs[2][NightReverb::numDiffusers] = { { 4.77, 3.59, 12.73, 9.31 }, { 4.13, 3.97, 11.21, 8.73 } };
    constexpr float diffusion = 0.62f;

    double lengthScale (double size)    { return 0.4 + 1.2 * size; }
}

NightReverb::NightReverb (tracktion::PluginCreationInfo info)
    : StockEffect (info)
{
    auto skewed = [] (float low, float high, float centre)
    {
        juce::NormalisableRange<float> r (low, high);
        r.setSkewForCentre (centre);
        return r;
    };

    mix = &addParam ("mix", "Mix", 0.3f, { 0.0f, 1.0f }, percent());
    preDelayMs = &addParam ("preDelay", "Pre-delay", 10.0f, skewed (0.0f, 200.0f, 40.0f), ms());
    size = &addParam ("size", "Size", 0.6f, { 0.0f, 1.0f }, percent());
    decay = &addParam ("decay", "Decay", 2.2f, skewed (0.2f, 12.0f, 2.0f),
                       [] (float v) { return juce::String (v, v < 10.0f ? 2 : 1) + " s"; });
    dampingHz = &addParam ("damping", "Damping", 6500.0f, frequencyRange (1000.0f, 18000.0f), hz());
    lowCutHz = &addParam ("lowCut", "Low cut", 150.0f, frequencyRange (20.0f, 1000.0f), hz());
    width = &addParam ("width", "Width", 1.0f, { 0.0f, 1.0f }, percent());
}

NightReverb::~NightReverb()
{
    notifyListenersOfDeletion();
}

double NightReverb::getTailLength() const
{
    return decay->get() + preDelayMs->get() * 0.001;
}

void NightReverb::prepare()
{
    for (auto& d : preDelay)
        d.prepare ((int) (0.21 * sampleRate));

    for (int i = 0; i < numLines; ++i)
        lines[i].prepare ((int) ((lineMs[i] * lengthScale (1.0) + 2.0) * 0.001 * sampleRate));

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < numDiffusers; ++i)
            diffusers[ch][i].prepare ((int) (diffuserMs[ch][i] * 0.001 * sampleRate) + 2);

    lastDamping = lastLowCut = -1.0f;
    reset();
}

void NightReverb::reset()
{
    for (auto& d : preDelay)
        d.reset();

    for (auto& l : lines)
        l.reset();

    for (auto& side : diffusers)
        for (auto& d : side)
            d.reset();

    for (auto& d : damping)
        d.z = 0.0f;

    lowCut.reset();

    for (int i = 0; i < numLines; ++i)
        lfoPhase[i] = i / (double) numLines;

    sizeNow = -1.0;
}

void NightReverb::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    float* left;
    float* right;

    if (! getStereo (fc, left, right))
        return;

    juce::ScopedNoDenormals noDenormals;

    if (const float d = dampingHz->get(); d != lastDamping)
    {
        for (auto& f : damping)
            f.setCutoff (d, sampleRate);

        lastDamping = d;
    }

    if (const float c = lowCutHz->get(); c != lastLowCut)
    {
        lowCut.c = dsp::Coeffs::highPass (c, 0.7071, sampleRate);
        lastLowCut = c;
    }

    const double targetSize = size->get();

    if (sizeNow < 0.0)
        sizeNow = targetSize;

    const double sizeGlide = 1.0 - std::exp (-1.0 / (0.15 * sampleRate));
    const double rt60 = decay->get();
    const int preDelaySamples = 1 + juce::roundToInt (preDelayMs->get() * 0.001 * sampleRate);
    const double modDepth = 0.00025 * sampleRate * (0.5 + targetSize);
    const float widthNow = width->get();

    float dry, wet;
    dsp::mixGains (mix->get(), dry, wet);

    // Each line's feedback for the decay time (size moves slowly, so once a block will do)
    float feedback[numLines];

    for (int l = 0; l < numLines; ++l)
        feedback[l] = (float) std::pow (10.0, -3.0 * lineMs[l] * lengthScale (sizeNow) * 0.001 / rt60);

    // Gentle on the way in, so a long tail doesn't pile up far louder than the dry sound
    const float inputGain = 0.35f;
    const double twoPi = juce::MathConstants<double>::twoPi;

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        const float inL = left[i], inR = right != nullptr ? right[i] : inL;
        sizeNow += (targetSize - sizeNow) * sizeGlide;
        const double scale = lengthScale (sizeNow) * 0.001 * sampleRate;

        // Pre-delay, then diffusion
        float x[2] = { inL, inR };

        for (int ch = 0; ch < 2; ++ch)
        {
            preDelay[ch].push (x[ch]);
            float v = preDelay[ch].tap (preDelaySamples);

            for (int d = 0; d < numDiffusers; ++d)
            {
                auto& ap = diffusers[ch][d];
                const float delayed = ap.tap ((int) (diffuserMs[ch][d] * 0.001 * sampleRate));
                const float w = v - diffusion * delayed;
                ap.push (w);
                v = diffusion * w + delayed;
            }

            x[ch] = v * inputGain;
        }

        // The network: read each line (its length drifting slightly), damp it, mix them all
        // into each other with a Hadamard matrix and feed back with each line's decay
        float y[numLines];

        for (int l = 0; l < numLines; ++l)
        {
            const double length = lineMs[l] * scale + modDepth * std::sin (twoPi * lfoPhase[l]);
            y[l] = damping[l].process (lines[l].read (length));
            lfoPhase[l] += lfoHz[l] / sampleRate;

            if (lfoPhase[l] >= 1.0)
                lfoPhase[l] -= 1.0;
        }

        float h[numLines];
        std::copy (y, y + numLines, h);

        for (int span = 1; span < numLines; span *= 2)
            for (int start = 0; start < numLines; start += span * 2)
                for (int j = start; j < start + span; ++j)
                {
                    const float a = h[j], b = h[j + span];
                    h[j] = a + b;
                    h[j + span] = a - b;
                }

        const float norm = 1.0f / std::sqrt ((float) numLines);

        for (int l = 0; l < numLines; ++l)
            lines[l].push (h[l] * norm * feedback[l] + x[l & 1]);

        // Out: different sign patterns for each side keep them apart
        float wetL = 0.5f * (y[0] - y[2] + y[4] - y[6] + 0.5f * (y[1] + y[5]));
        float wetR = 0.5f * (y[1] - y[3] + y[5] - y[7] + 0.5f * (y[2] + y[6]));
        wetL = lowCut.process (wetL, 0);
        wetR = lowCut.process (wetR, 1);

        const float mid = 0.5f * (wetL + wetR), side = 0.5f * (wetL - wetR) * widthNow;
        left[i] = inL * dry + (mid + side) * wet;

        if (right != nullptr)
            right[i] = inR * dry + (mid - side) * wet;
    }
}

} // namespace nightshift
