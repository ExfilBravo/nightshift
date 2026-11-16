// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "StockEffects.h"

namespace nightshift
{

const char* NightDelay::xmlTypeName = "nightshiftDelay";
const char* NightChorus::xmlTypeName = "nightshiftChorus";

namespace
{
    struct Division { const char* name; double beats; };

    const Division divisions[] = { { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/16.", 0.375 },
                                   { "1/8T", 1.0 / 3.0 }, { "1/8", 0.5 }, { "1/8.", 0.75 }, { "1/4T", 2.0 / 3.0 },
                                   { "1/4", 1.0 }, { "1/4.", 1.5 }, { "1/2", 2.0 }, { "1/2.", 3.0 }, { "1/1", 4.0 } };

    constexpr double maxDelaySeconds = 4.0;

    juce::NormalisableRange<float> skewed (float low, float high, float centre)
    {
        juce::NormalisableRange<float> r (low, high);
        r.setSkewForCentre (centre);
        return r;
    }
}

//==============================================================================
NightDelay::NightDelay (tracktion::PluginCreationInfo info)
    : StockEffect (info)
{
    sync = &addParam ("sync", "Sync", 1.0f, toggleRange(), onOff());
    division = &addParam ("division", "Note value", 6.0f, { 0.0f, (float) numDivisions() - 1.0f, 1.0f },
                          [] (float v) { return divisionName (juce::roundToInt (v)); });
    timeMs = &addParam ("time", "Time", 350.0f, skewed (1.0f, 2000.0f, 300.0f), ms());
    feedback = &addParam ("feedback", "Feedback", 0.35f, { 0.0f, 0.95f }, percent());
    pingPong = &addParam ("pingPong", "Ping-pong", 0.0f, toggleRange(), onOff());
    highCutHz = &addParam ("highCut", "High cut", 7000.0f, frequencyRange (1000.0f, 20000.0f), hz());
    lowCutHz = &addParam ("lowCut", "Low cut", 120.0f, frequencyRange (20.0f, 2000.0f), hz());
    mod = &addParam ("mod", "Wobble", 0.15f, { 0.0f, 1.0f }, percent());
    width = &addParam ("width", "Width", 1.0f, { 0.0f, 1.0f }, percent());
    mix = &addParam ("mix", "Mix", 0.3f, { 0.0f, 1.0f }, percent());
}

NightDelay::~NightDelay()
{
    notifyListenersOfDeletion();
}

int NightDelay::numDivisions()                  { return (int) std::size (divisions); }

double NightDelay::divisionBeats (int index)
{
    return divisions[juce::jlimit (0, numDivisions() - 1, index)].beats;
}

juce::String NightDelay::divisionName (int index)
{
    return divisions[juce::jlimit (0, numDivisions() - 1, index)].name;
}

double NightDelay::delayMs (double bpm) const
{
    if (sync->get() >= 0.5f)
        return std::min (maxDelaySeconds * 1000.0, divisionBeats (juce::roundToInt (division->get())) * 60000.0 / std::max (20.0, bpm));

    return timeMs->get();
}

void NightDelay::prepare()
{
    for (auto& l : lines)
        l.prepare ((int) ((maxDelaySeconds + 0.05) * sampleRate));

    lastHighCut = lastLowCut = -1.0f;
    reset();
}

void NightDelay::reset()
{
    for (auto& l : lines)
        l.reset();

    highCut.reset();
    lowCut.reset();
    currentDelay = -1.0;
}

void NightDelay::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    float* left;
    float* right;

    if (! getStereo (fc, left, right))
        return;

    juce::ScopedNoDenormals noDenormals;

    tempoPosition.set (fc.editTime.getStart());
    const double bpm = tempoPosition.getTempo();
    bpmNow.store (bpm);

    if (const float c = highCutHz->get(); c != lastHighCut)
    {
        highCut.c = dsp::Coeffs::lowPass (c, 0.7071, sampleRate);
        lastHighCut = c;
    }

    if (const float c = lowCutHz->get(); c != lastLowCut)
    {
        lowCut.c = dsp::Coeffs::highPass (c, 0.7071, sampleRate);
        lastLowCut = c;
    }

    const double target = delayMs (bpm) * 0.001 * sampleRate;

    if (currentDelay < 0.0)
        currentDelay = target;

    // A changed time glides there like tape speeding up or slowing down
    const double glide = 1.0 - std::exp (-1.0 / (0.08 * sampleRate));
    const double wobbleDepth = mod->get() * 0.0015 * sampleRate;
    const double wobbleStep = 0.6 / sampleRate;
    const float fb = feedback->get(), widthNow = width->get();
    const bool pong = pingPong->get() >= 0.5f;

    float dry, wet;
    dsp::mixGains (mix->get(), dry, wet);

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        const float inL = left[i], inR = right != nullptr ? right[i] : inL;
        currentDelay += (target - currentDelay) * glide;

        const double wobble = wobbleDepth * std::sin (juce::MathConstants<double>::twoPi * lfoPhase);
        lfoPhase += wobbleStep;

        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;

        // The echoes, through the filters (so the first one has the tone too)
        const float echoL = lowCut.process (highCut.process (lines[0].read (currentDelay + wobble), 0), 0);
        const float echoR = lowCut.process (highCut.process (lines[1].read (currentDelay - wobble), 1), 1);

        if (pong)
        {
            // Both sides in on the left, then each echo crosses over
            lines[0].push (0.5f * (inL + inR) + echoR * fb);
            lines[1].push (echoL * fb);
        }
        else
        {
            lines[0].push (inL + echoL * fb);
            lines[1].push (inR + echoR * fb);
        }

        const float mid = 0.5f * (echoL + echoR), side = 0.5f * (echoL - echoR) * widthNow;
        left[i] = inL * dry + (mid + side) * wet;

        if (right != nullptr)
            right[i] = inR * dry + (mid - side) * wet;
    }
}

//==============================================================================
NightChorus::NightChorus (tracktion::PluginCreationInfo info)
    : StockEffect (info)
{
    rate = &addParam ("rate", "Rate", 0.6f, skewed (0.05f, 5.0f, 0.8f), [] (float v) { return juce::String (v, 2) + " Hz"; });
    depth = &addParam ("depth", "Depth", 0.5f, { 0.0f, 1.0f }, percent());
    delayMs = &addParam ("delay", "Delay", 9.0f, { 2.0f, 30.0f }, ms());
    feedback = &addParam ("feedback", "Feedback", 0.0f, { 0.0f, 0.7f }, percent());
    width = &addParam ("width", "Width", 1.0f, { 0.0f, 1.0f }, percent());
    mix = &addParam ("mix", "Mix", 0.5f, { 0.0f, 1.0f }, percent());
}

NightChorus::~NightChorus()
{
    notifyListenersOfDeletion();
}

void NightChorus::prepare()
{
    for (auto& l : lines)
        l.prepare ((int) (0.05 * sampleRate));

    reset();
}

void NightChorus::reset()
{
    for (auto& l : lines)
        l.reset();

    feedbackSample[0] = feedbackSample[1] = 0.0f;
    lfoPhase = 0.0;
}

void NightChorus::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    float* left;
    float* right;

    if (! getStereo (fc, left, right))
        return;

    juce::ScopedNoDenormals noDenormals;

    const double base = delayMs->get() * 0.001 * sampleRate;

    // Up to 4 ms either way, but never closer than 1 ms to no delay at all
    const double swing = std::min (depth->get() * 0.004 * sampleRate, base - 0.001 * sampleRate);
    const double step = rate->get() / sampleRate;
    const float fb = feedback->get(), widthNow = width->get();
    const double twoPi = juce::MathConstants<double>::twoPi;

    float dry, wet;
    dsp::mixGains (mix->get(), dry, wet);

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        const float inL = left[i], inR = right != nullptr ? right[i] : inL;
        const double phase = twoPi * lfoPhase;
        lfoPhase += step;

        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;

        lines[0].push (inL + feedbackSample[0] * fb);
        lines[1].push (inR + feedbackSample[1] * fb);

        // Two voices a side, the right a quarter turn behind the left
        const float wetL = 0.5f * (lines[0].read (base + swing * std::sin (phase)) + lines[0].read (base + swing * std::sin (phase + twoPi * 0.5)));
        const float wetR = 0.5f * (lines[1].read (base + swing * std::sin (phase + twoPi * 0.25))
                                    + lines[1].read (base + swing * std::sin (phase + twoPi * 0.75)));
        feedbackSample[0] = wetL;
        feedbackSample[1] = wetR;

        const float mid = 0.5f * (wetL + wetR), side = 0.5f * (wetL - wetR) * widthNow;
        left[i] = inL * dry + (mid + side) * wet;

        if (right != nullptr)
            right[i] = inR * dry + (mid - side) * wet;
    }
}

} // namespace nightshift
