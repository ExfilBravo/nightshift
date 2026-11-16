// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "MasterLimiter.h"

namespace nightshift
{

const char* MasterLimiter::xmlTypeName = "nightshiftMasterLimiter";

MasterLimiter::MasterLimiter (tracktion::PluginCreationInfo info)
    : tracktion::Plugin (info)
{
    // The master volume, as the master fader has it (0.8 is 0 dB), where automation can reach it
    volume = std::make_unique<tracktion::ParameterWithStateValue> (*this, "volume", juce::Identifier ("volume"), "Master volume", 0.8f,
                                                                   juce::NormalisableRange<float> (0.0f, 1.0f),
                                                                   [] (float v)
                                                                   {
                                                                       const float db = masterVolumeToDb (v);
                                                                       return db <= -99.0f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB";
                                                                   });
}

MasterLimiter::~MasterLimiter()
{
    notifyListenersOfDeletion();
    volume->reset();
}

void MasterLimiter::setMasterVolume (float knob)
{
    if (std::abs (volume->getCurrentValue() - knob) > 1.0e-6f)
        volume->setParameter (knob, juce::dontSendNotification);
}

void MasterLimiter::initialise (const tracktion::PluginInitialisationInfo& info)
{
    shaper.prepare (info.sampleRate);
    maximizer.prepare (info.sampleRate);
    meter.prepare (info.sampleRate, 2, std::max (512, info.blockSizeSamples));
    stereo.prepare (info.sampleRate, 2, std::max (512, info.blockSizeSamples));
    latencySeconds = maximizer.getLatencySamples() / info.sampleRate;

    {
        const juce::SpinLock::ScopedLockType lock (shapeLock);
        shaper.set (pendingShape);
        shapeChanged = false;
    }

    // Straight to the settings: no ease in from silence at the start of a render
    capturingNow = capturing.load();
    masterGain.reset (info.sampleRate, 0.01);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterVolumeToDb (volume->getCurrentValue()), -120.0f));
    pushNow = capturingNow || compare.load() == 1 ? 0.0f : pushDb.load();
    compareGain.reset (info.sampleRate, 0.02);
    compareGain.setCurrentAndTargetValue (compare.load() == 2 ? juce::Decibels::decibelsToGain (compareGainDb.load()) : 1.0f);
    ceilingNow = ceilingDb.load();
    maximizer.setGainDb (pushNow, true);
    maximizer.setCeilingDb (ceilingNow);
    maximizer.setLimiting (! capturingNow);
}

void MasterLimiter::reset()
{
    shaper.reset();
    maximizer.reset();
}

void MasterLimiter::setLevels (float master, float push, float ceiling)
{
    masterDb = master;
    pushDb = push;
    ceilingDb = ceiling;
}

void MasterLimiter::setShape (const Song::Mastering& m)
{
    // Only the numbers: the target's name stays out, so the audio thread never lets go of the
    // last copy of a string (which would free it there)
    auto shape = m;
    shape.target = {};
    const juce::SpinLock::ScopedLockType lock (shapeLock);

    if (! (shape == pendingShape))
    {
        pendingShape = shape;
        shapeChanged = true;
    }
}

void MasterLimiter::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.bufferNumSamples == 0)
        return;

    auto& buffer = *fc.destBuffer;
    const int numChannels = std::min (2, buffer.getNumChannels());

    if (numChannels == 0)
        return;

    // New settings, picked up here on the audio thread (the shaping's only if it can have them
    // without waiting: otherwise next block)
    {
        const juce::SpinLock::ScopedTryLockType lock (shapeLock);

        if (lock.isLocked() && shapeChanged)
        {
            shaper.set (pendingShape);
            shapeChanged = false;
        }
    }

    capturingNow = capturing.load();
    const int comparing = capturingNow ? 0 : compare.load();
    const bool unshaped = (capturingNow && capturingRaw.load()) || comparing == 1;
    // The master volume parameter: the fader's value, or its automation's while Song mode plays
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterVolumeToDb (volume->getCurrentValue()), -120.0f));
    compareGain.setTargetValue (comparing == 2 ? juce::Decibels::decibelsToGain (compareGainDb.load()) : 1.0f);

    if (const float wanted = capturingNow || comparing == 1 ? 0.0f : pushDb.load(); wanted != pushNow)
    {
        pushNow = wanted;
        maximizer.setGainDb (pushNow);
    }

    if (const float c = ceilingDb.load(); c != ceilingNow)
    {
        ceilingNow = c;
        maximizer.setCeilingDb (ceilingNow);
    }

    maximizer.setLimiting (! capturingNow);

    float* left = buffer.getWritePointer (0, fc.bufferStartSample);
    float* right = numChannels > 1 ? buffer.getWritePointer (1, fc.bufferStartSample) : nullptr;

    // The master volume, then the shaping, then the push into the limiter
    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        const float g = masterGain.getNextValue();
        left[i] *= g;

        if (right != nullptr)
            right[i] *= g;
    }

    if (! unshaped)
        shaper.process (left, right, fc.bufferNumSamples);

    maximizer.process (left, right, fc.bufferNumSamples);

    // Comparing: the master turned down to the mix's own loudness
    if (compareGain.isSmoothing() || compareGain.getTargetValue() != 1.0f)
        for (int i = 0; i < fc.bufferNumSamples; ++i)
        {
            const float g = compareGain.getNextValue();
            left[i] *= g;

            if (right != nullptr)
                right[i] *= g;
        }

    const float* out[2] = { left, right != nullptr ? right : left };
    meter.process (out, 2, fc.bufferNumSamples);
    stereo.process (out, 2, fc.bufferNumSamples);
}

} // namespace nightshift
