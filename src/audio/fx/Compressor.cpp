// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "StockEffects.h"

namespace nightshift
{

const char* NightCompressor::xmlTypeName = "nightshiftCompressor";

NightCompressor::NightCompressor (tracktion::PluginCreationInfo info)
    : StockEffect (info)
{
    auto skewed = [] (float low, float high, float centre)
    {
        juce::NormalisableRange<float> r (low, high);
        r.setSkewForCentre (centre);
        return r;
    };

    threshold = &addParam ("threshold", "Threshold", -18.0f, { -60.0f, 0.0f }, db());
    ratio = &addParam ("ratio", "Ratio", 4.0f, skewed (1.0f, 20.0f, 4.0f),
                       [] (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + ":1"; });
    knee = &addParam ("knee", "Knee", 6.0f, { 0.0f, 18.0f }, [] (float v) { return juce::String (v, 1) + " dB"; });
    attack = &addParam ("attack", "Attack", 10.0f, skewed (0.1f, 100.0f, 10.0f), ms());
    release = &addParam ("release", "Release", 120.0f, skewed (10.0f, 1000.0f, 120.0f), ms());
    makeup = &addParam ("makeup", "Makeup", 0.0f, { 0.0f, 24.0f }, db());
    autoMakeup = &addParam ("autoMakeup", "Auto makeup", 0.0f, toggleRange(), onOff());
    mix = &addParam ("mix", "Mix", 1.0f, { 0.0f, 1.0f }, percent());
    highPass = &addParam ("scHighPass", "Sidechain high pass", 20.0f, frequencyRange (20.0f, 500.0f),
                          [] (float v) { return v <= 20.5f ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " Hz"; });

    sidechain.referTo (state, juce::Identifier ("sidechain"), nullptr, 0);
}

NightCompressor::~NightCompressor()
{
    notifyListenersOfDeletion();
}

void NightCompressor::getChannelNames (juce::StringArray* ins, juce::StringArray* outs)
{
    // Two more inputs for the sidechain, wired by the engine when one is chosen
    if (ins != nullptr)
        ins->addArray ({ "Left", "Right", "Sidechain Left", "Sidechain Right" });

    if (outs != nullptr)
        outs->addArray ({ "Left", "Right" });
}

float NightCompressor::curveDb (float in, float thresholdDb, float ratioValue, float kneeDb)
{
    const float over = in - thresholdDb;

    if (2.0f * over < -kneeDb)
        return in;

    if (kneeDb > 0.0f && 2.0f * std::abs (over) <= kneeDb)
    {
        const float x = over + kneeDb * 0.5f;
        return in + (1.0f / ratioValue - 1.0f) * x * x / (2.0f * kneeDb);
    }

    return thresholdDb + over / ratioValue;
}

float NightCompressor::makeupDb() const
{
    // Auto: half of what it takes off a full-scale signal
    const float automatic = autoMakeup->get() >= 0.5f ? -0.5f * curveDb (0.0f, threshold->get(), ratio->get(), knee->get()) : 0.0f;
    return makeup->get() + automatic;
}

void NightCompressor::prepare()
{
    lastHighPass = -1.0f;
    reset();
}

void NightCompressor::reset()
{
    envelopeDb = 0.0f;
    meanSquare = 0.0f;
    detectorFilter.reset();
}

void NightCompressor::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    float* left;
    float* right;

    if (! getStereo (fc, left, right))
        return;

    juce::ScopedNoDenormals noDenormals;

    // Listening to another track: its channels arrive after ours
    const float* sideLeft = nullptr;
    const float* sideRight = nullptr;
    const int channels = fc.destBuffer->getNumChannels();

    if (sidechainConnected.load() && channels >= 3)
    {
        sideLeft = fc.destBuffer->getReadPointer (2, fc.bufferStartSample);
        sideRight = channels >= 4 ? fc.destBuffer->getReadPointer (3, fc.bufferStartSample) : sideLeft;
    }

    const float hp = highPass->get();

    if (hp != lastHighPass)
    {
        detectorFilter.c = hp <= 20.5f ? dsp::Coeffs() : dsp::Coeffs::highPass (hp, 0.7071, sampleRate);
        lastHighPass = hp;
    }

    const float t = threshold->get(), r = ratio->get(), k = knee->get();
    const float attackCoeff = (float) std::exp (-1.0 / (attack->get() * 0.001 * sampleRate));
    const float releaseCoeff = (float) std::exp (-1.0 / (release->get() * 0.001 * sampleRate));
    const float makeupGain = juce::Decibels::decibelsToGain (makeupDb());
    const float wet = mix->get(), dry = 1.0f - wet;

    // An 8 ms RMS level (scaled so a sine reads as its peak), smoother than chasing every peak
    const float rmsCoeff = 1.0f - (float) std::exp (-1.0 / (0.008 * sampleRate));

    float mostReduction = 0.0f, inPeak = 0.0f, outPeak = 0.0f;

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        const float inL = left[i], inR = right != nullptr ? right[i] : inL;
        const float detL = detectorFilter.process (sideLeft != nullptr ? sideLeft[i] : inL, 0);
        const float detR = detectorFilter.process (sideRight != nullptr ? sideRight[i] : inR, 1);
        meanSquare += (std::max (detL * detL, detR * detR) - meanSquare) * rmsCoeff;
        const float level = std::sqrt (2.0f * meanSquare);
        const float levelDb = level > 1.0e-6f ? 20.0f * std::log10 (level) : -120.0f;

        // Smoothed in dB: towards more reduction at the attack rate, back at the release rate
        const float wanted = curveDb (levelDb, t, r, k) - levelDb;
        const float coeff = wanted < envelopeDb ? attackCoeff : releaseCoeff;
        envelopeDb = wanted + coeff * (envelopeDb - wanted);

        const float gain = dry + wet * juce::Decibels::decibelsToGain (envelopeDb) * makeupGain;
        const float outL = inL * gain, outR = inR * gain;
        left[i] = outL;

        if (right != nullptr)
            right[i] = outR;

        mostReduction = std::max (mostReduction, -envelopeDb);
        inPeak = std::max (inPeak, std::max (std::abs (inL), std::abs (inR)));
        outPeak = std::max (outPeak, std::max (std::abs (outL), std::abs (outR)));
    }

    if (mostReduction > reductionDb.load())
        reductionDb.store (mostReduction);

    const float inDb = juce::Decibels::gainToDecibels (inPeak, -100.0f), outDb = juce::Decibels::gainToDecibels (outPeak, -100.0f);

    if (inDb > inputDb.load())
        inputDb.store (inDb);

    if (outDb > outputDb.load())
        outputDb.store (outDb);
}

} // namespace nightshift
