// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "StockEffects.h"

namespace nightshift::stockfx
{

namespace
{
    struct Kind { const char* type; const char* name; const char* blurb; };

    const Kind kinds[] = {
        { NightEQ::xmlTypeName,         "Nightshift EQ",         "Seven bands: high and low pass, shelves and three bells, over an analyser" },
        { NightCompressor::xmlTypeName, "Nightshift Compressor", "Evens out levels; can duck to another channel (sidechain)" },
        { NightReverb::xmlTypeName,     "Nightshift Reverb",     "Rooms to halls: size, decay, damping, pre-delay" },
        { NightDelay::xmlTypeName,      "Nightshift Delay",      "Echoes in time with the song, ping-pong, filtered repeats" },
        { NightChorus::xmlTypeName,     "Nightshift Chorus",     "Thickens and widens with slowly moving copies" },
        { NightFilter::xmlTypeName,     "Nightshift Filter",     "Resonant low, band or high pass with drive" },
        { NightAmp::xmlTypeName,        "Nightshift NAM Amp",    "A real amp, captured: a Neural Amp Modeler model (.nam) and a cabinet" },
        { NightTune::xmlTypeName,       "Nightshift Pitch Correction", "Pulls singing or playing onto the notes of the key" },
    };
}

void registerAll (tracktion::Engine& engine)
{
    auto& pm = engine.getPluginManager();
    pm.createBuiltInType<NightEQ>();
    pm.createBuiltInType<NightCompressor>();
    pm.createBuiltInType<NightReverb>();
    pm.createBuiltInType<NightDelay>();
    pm.createBuiltInType<NightChorus>();
    pm.createBuiltInType<NightFilter>();
    pm.createBuiltInType<NightAmp>();
    pm.createBuiltInType<NightTune>();
}

const juce::Array<juce::PluginDescription>& descriptions()
{
    static const juce::Array<juce::PluginDescription> list = []
    {
        juce::Array<juce::PluginDescription> result;

        for (auto& k : kinds)
        {
            juce::PluginDescription d;
            d.name = k.name;
            d.descriptiveName = k.blurb;
            d.pluginFormatName = "Nightshift";
            d.category = "Stock";
            d.manufacturerName = "Nightshift";
            d.fileOrIdentifier = k.type;
            d.isInstrument = false;
            d.numInputChannels = 2;
            d.numOutputChannels = 2;
            d.uniqueId = d.deprecatedUid = juce::String (k.type).hashCode();
            result.add (d);
        }

        return result;
    }();

    return list;
}

juce::String typeForIdentifier (const juce::String& identifier)
{
    for (auto& d : descriptions())
        if (d.createIdentifierString() == identifier || d.fileOrIdentifier == identifier)
            return d.fileOrIdentifier;

    return {};
}

juce::String blurb (const juce::String& xmlTypeName)
{
    for (auto& k : kinds)
        if (xmlTypeName == k.type)
            return k.blurb;

    return {};
}

} // namespace nightshift::stockfx
