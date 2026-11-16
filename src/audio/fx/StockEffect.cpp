// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "StockEffect.h"

namespace nightshift
{

StockEffect::StockEffect (tracktion::PluginCreationInfo info)
    : tracktion::Plugin (info)
{
}

StockEffect::~StockEffect()
{
    // Each effect calls notifyListenersOfDeletion() in its own destructor first
    for (auto& p : params)
        p->value->reset();
}

void StockEffect::initialise (const tracktion::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;
    prepare();
}

void StockEffect::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    for (auto& p : params)
        p->value->setFromValueTree (v);
}

StockEffect::Param* StockEffect::findParam (const juce::String& id) const
{
    for (auto& p : params)
        if (p->id == id)
            return p.get();

    return nullptr;
}

void StockEffect::resetParams()
{
    for (auto& p : params)
        p->set (p->defaultValue);
}

StockEffect::Param& StockEffect::addParam (const juce::String& id, const juce::String& name, float defaultValue,
                                           juce::NormalisableRange<float> range, std::function<juce::String (float)> toText)
{
    auto p = std::make_unique<Param>();
    p->id = id;
    p->name = name;
    p->defaultValue = defaultValue;
    p->range = range;
    p->value = std::make_unique<tracktion::ParameterWithStateValue> (*this, id, juce::Identifier (id), name, defaultValue, range,
                                                                     std::move (toText));
    params.push_back (std::move (p));
    return *params.back();
}

bool StockEffect::getStereo (const tracktion::PluginRenderContext& fc, float*& left, float*& right)
{
    if (fc.destBuffer == nullptr || fc.bufferNumSamples == 0 || fc.destBuffer->getNumChannels() == 0)
        return false;

    left = fc.destBuffer->getWritePointer (0, fc.bufferStartSample);
    right = fc.destBuffer->getNumChannels() > 1 ? fc.destBuffer->getWritePointer (1, fc.bufferStartSample) : nullptr;
    return true;
}

juce::NormalisableRange<float> StockEffect::frequencyRange (float low, float high)
{
    juce::NormalisableRange<float> r (low, high);
    r.setSkewForCentre (std::sqrt (low * high));
    return r;
}

std::function<juce::String (float)> StockEffect::hz()
{
    return [] (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                              : juce::String (juce::roundToInt (v)) + " Hz"; };
}

std::function<juce::String (float)> StockEffect::db()
{
    return [] (float v) { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; };
}

std::function<juce::String (float)> StockEffect::ms()
{
    return [] (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s"
                                              : juce::String (v, v < 10.0f ? 1 : 0) + " ms"; };
}

std::function<juce::String (float)> StockEffect::percent()
{
    return [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
}

std::function<juce::String (float)> StockEffect::onOff()
{
    return [] (float v) { return v >= 0.5f ? juce::String ("On") : juce::String ("Off"); };
}

} // namespace nightshift
