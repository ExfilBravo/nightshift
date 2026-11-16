// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "StockEffects.h"

namespace nightshift
{

const char* NightEQ::xmlTypeName = "nightshiftEQ";

namespace
{
    const char* const bandPrefixes[] = { "hp", "ls", "b1", "b2", "b3", "hs", "lp" };
    constexpr int subBlock = 32;
}

NightEQ::NightEQ (tracktion::PluginCreationInfo info)
    : StockEffect (info)
{
    const auto gainRange = juce::NormalisableRange<float> (-18.0f, 18.0f);
    auto qRange = juce::NormalisableRange<float> (0.2f, 10.0f);
    qRange.setSkewForCentre (1.4f);
    const auto qText = [] (float v) { return "Q " + juce::String (v, v < 10.0f ? 2 : 1); };

    addParam ("hpOn", "High pass", 0.0f, toggleRange(), onOff());
    addParam ("hpFreq", "High pass frequency", 30.0f, frequencyRange (20.0f, 1000.0f), hz());
    addParam ("hpSteep", "High pass 24 dB/oct", 0.0f, toggleRange(), onOff());

    addParam ("lsFreq", "Low shelf frequency", 100.0f, frequencyRange (20.0f, 1500.0f), hz());
    addParam ("lsGain", "Low shelf gain", 0.0f, gainRange, db());

    const float bellFreqs[] = { 250.0f, 1000.0f, 4000.0f };

    for (int i = 0; i < 3; ++i)
    {
        const auto prefix = juce::String ("b") + juce::String (i + 1);
        const auto name = "Bell " + juce::String (i + 1);
        addParam (prefix + "Freq", name + " frequency", bellFreqs[i], frequencyRange (20.0f, 20000.0f), hz());
        addParam (prefix + "Gain", name + " gain", 0.0f, gainRange, db());
        addParam (prefix + "Q", name + " Q", 1.0f, qRange, qText);
    }

    addParam ("hsFreq", "High shelf frequency", 8000.0f, frequencyRange (800.0f, 20000.0f), hz());
    addParam ("hsGain", "High shelf gain", 0.0f, gainRange, db());

    addParam ("lpOn", "Low pass", 0.0f, toggleRange(), onOff());
    addParam ("lpFreq", "Low pass frequency", 18000.0f, frequencyRange (1000.0f, 20000.0f), hz());
    addParam ("lpSteep", "Low pass 24 dB/oct", 0.0f, toggleRange(), onOff());

    outputParam = &addParam ("output", "Output", 0.0f, gainRange, db());

    // Looked up once: the audio thread reads them every few samples
    const char* const fields[] = { "Freq", "Gain", "Q", "On", "Steep" };

    for (int b = 0; b < numBands; ++b)
        for (int f = 0; f < 5; ++f)
            bandParams[b][f] = findParam (paramId (b, fields[f]));
}

NightEQ::~NightEQ()
{
    notifyListenersOfDeletion();
}

juce::String NightEQ::bandName (int band)
{
    const char* const names[] = { "High pass", "Low shelf", "Bell 1", "Bell 2", "Bell 3", "High shelf", "Low pass" };
    return juce::isPositiveAndBelow (band, (int) numBands) ? names[band] : "";
}

juce::String NightEQ::paramId (int band, const char* what)
{
    if (! juce::isPositiveAndBelow (band, (int) numBands))
        return {};

    const juce::String w (what);
    const bool pass = band == highPass || band == lowPass, bell = band >= bell1 && band <= bell3;

    if (w == "Freq" || (w == "Gain" && ! pass) || (w == "Q" && bell) || ((w == "On" || w == "Steep") && pass))
        return bandPrefixes[band] + w;

    return {};
}

NightEQ::BandSettings NightEQ::settingsFor (int band) const
{
    auto value = [this, band] (int field, float fallback)
    {
        auto p = bandParams[band][field];
        return p != nullptr ? p->get() : fallback;
    };

    BandSettings s;
    s.freq = value (0, 1000.0f);
    s.gainDb = value (1, 0.0f);
    s.q = value (2, 0.707f);
    s.on = value (3, 1.0f) >= 0.5f;
    s.steep = value (4, 0.0f) >= 0.5f;
    return s;
}

void NightEQ::coefficientsFor (const BandSettings& s, int band, double sr, dsp::Coeffs& first, dsp::Coeffs& second)
{
    first = second = {};

    if (! s.on)
        return;

    switch (band)
    {
        case highPass:
        case lowPass:
        {
            // Butterworth: one stage at 12 dB/oct, two at 24
            auto make = band == highPass ? &dsp::Coeffs::highPass : &dsp::Coeffs::lowPass;

            if (s.steep)
            {
                first = make (s.freq, 0.5412, sr);
                second = make (s.freq, 1.3066, sr);
            }
            else
            {
                first = make (s.freq, 0.7071, sr);
            }
            break;
        }

        case lowShelf:   first = dsp::Coeffs::lowShelf (s.freq, s.gainDb, sr); break;
        case highShelf:  first = dsp::Coeffs::highShelf (s.freq, s.gainDb, sr); break;
        default:         first = dsp::Coeffs::bell (s.freq, s.q, s.gainDb, sr); break;
    }
}

double NightEQ::bandResponseDb (int band, double freq) const
{
    dsp::Coeffs first, second;
    coefficientsFor (settingsFor (band), band, sampleRate, first, second);
    return first.magnitudeDb (freq, sampleRate) + second.magnitudeDb (freq, sampleRate);
}

double NightEQ::responseDb (double freq) const
{
    double total = outputParam->get();

    for (int b = 0; b < numBands; ++b)
        total += bandResponseDb (b, freq);

    return total;
}

void NightEQ::prepare()
{
    settled = false;
    reset();
}

void NightEQ::reset()
{
    for (auto& band : filters)
        for (auto& f : band)
            f.reset();

    settled = false;
}

void NightEQ::updateFilters (bool jump)
{
    // Glide towards the knobs over ~20 ms so sweeps don't zip
    const double k = jump ? 1.0 : 1.0 - std::exp (-subBlock / (0.02 * sampleRate));

    for (int b = 0; b < numBands; ++b)
    {
        const auto target = settingsFor (b);
        auto& now = current[b];
        const auto before = now;

        now.on = target.on;
        now.steep = target.steep;
        now.freq = now.freq * std::pow (target.freq / now.freq, k);
        now.gainDb += (target.gainDb - now.gainDb) * k;
        now.q += (target.q - now.q) * k;

        if (jump || before.on != now.on || before.steep != now.steep || std::abs (before.freq - now.freq) > 1.0e-3
             || std::abs (before.gainDb - now.gainDb) > 1.0e-4 || std::abs (before.q - now.q) > 1.0e-5)
            coefficientsFor (now, b, sampleRate, filters[b][0].c, filters[b][1].c);
    }
}

void NightEQ::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    float* left;
    float* right;

    if (! getStereo (fc, left, right))
        return;

    juce::ScopedNoDenormals noDenormals;

    if (! settled)
    {
        for (int b = 0; b < numBands; ++b)
            current[b] = settingsFor (b);

        updateFilters (true);
        settled = true;
    }

    const float outGain = juce::Decibels::decibelsToGain (outputParam->get());
    int write = analysisWrite.load (std::memory_order_relaxed);

    for (int start = 0; start < fc.bufferNumSamples; start += subBlock)
    {
        updateFilters (false);
        const int end = std::min (fc.bufferNumSamples, start + subBlock);

        for (int i = start; i < end; ++i)
        {
            float l = left[i], r = right != nullptr ? right[i] : l;

            for (int b = 0; b < numBands; ++b)
            {
                l = filters[b][1].process (filters[b][0].process (l, 0), 0);
                r = filters[b][1].process (filters[b][0].process (r, 1), 1);
            }

            l *= outGain;
            r *= outGain;
            left[i] = l;

            if (right != nullptr)
                right[i] = r;

            analysis[(size_t) write] = 0.5f * (l + r);
            write = (write + 1) & (analysisSize - 1);
        }
    }

    analysisWrite.store (write, std::memory_order_release);
}

void NightEQ::copyAnalysis (float* dest, int num) const
{
    num = std::min (num, analysisSize);
    const int write = analysisWrite.load (std::memory_order_acquire);

    for (int i = 0; i < num; ++i)
        dest[i] = analysis[(size_t) ((write - num + i) & (analysisSize - 1))];
}

} // namespace nightshift
