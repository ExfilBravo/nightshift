// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "StockEffects.h"
#include "NamRunner.h"

namespace nightshift
{

const char* NightAmp::xmlTypeName = "nightshiftAmp";

std::atomic<double> NightAmp::interfaceLevel { NightAmp::defaultInterfaceLevel };

namespace
{
    const juce::Identifier modelProperty ("model"), cabinetProperty ("cabinet"), sizeProperty ("size"), calibrateProperty ("calibrate");

    // Where NAM's own plugin evens models' loudness out to
    constexpr double targetLoudnessDb = -18.0;

    // The gate shuts 6 dB under where it opens, so it doesn't chatter on a fading note
    constexpr float gateHysteresisDb = 6.0f;

    std::filesystem::path pathOf (const juce::File& f)
    {
       #if JUCE_WINDOWS
        return std::filesystem::path (f.getFullPathName().toWideCharPointer());
       #else
        return std::filesystem::path (f.getFullPathName().toStdString());
       #endif
    }
}

/** A model ready for the audio thread (NAM's network, behind NamRunner) and the gain that brings its loudness to the
    target. */
struct NightAmp::Model
{
    std::unique_ptr<NamRunner> net;
    float levelGain = 1.0f;
};

NightAmp::NightAmp (tracktion::PluginCreationInfo info)
    : StockEffect (info)
{
    input = &addParam ("input", "Input", 0.0f, { -24.0f, 24.0f }, db());
    gate = &addParam ("gate", "Gate", -100.0f, { -100.0f, -20.0f },
                      [] (float v) { return v <= -99.5f ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " dB"; });
    output = &addParam ("output", "Output", 0.0f, { -24.0f, 24.0f }, db());
    evenOut = &addParam ("evenOut", "Even out loudness", 1.0f, toggleRange(), onOff());
    cabinetOn = &addParam ("cabinetOn", "Cabinet", 1.0f, toggleRange(), onOff());

    // Input calibration is newer than the amp: one saved with a model before it keeps the sound it was saved
    // with (calibration off), and one added now has it on, written down so it opens again that way
    if (! state.hasProperty (calibrateProperty))
        state.setProperty (calibrateProperty, state.hasProperty (modelProperty) ? 0.0f : 1.0f, nullptr);

    calibrate = &addParam ("calibrate", "Match input level", 1.0f, toggleRange(), onOff());

    // A song saved with an amp opens with it
    loadSavedFiles (state);
}

NightAmp::~NightAmp()
{
    notifyListenersOfDeletion();
}

void NightAmp::initialise (const tracktion::PluginInitialisationInfo& info)
{
    maxBlock = std::max (32, info.blockSizeSamples);
    StockEffect::initialise (info);
}

void NightAmp::prepare()
{
    mono.assign ((size_t) maxBlock, 0.0f);
    amped.assign ((size_t) maxBlock, 0.0f);
    cabinet.prepare ({ sampleRate, (juce::uint32) maxBlock, 1 });
    reset();

    {
        // (the audio thread passes the sound through while this holds it)
        const juce::SpinLock::ScopedLockType lock (modelLock);

        if (model != nullptr)
            configure (*model);
    }

    initialised = true;
}

void NightAmp::configure (Model& m) const
{
    m.net->prepare (sampleRate, maxBlock);
}

void NightAmp::reset()
{
    gateEnvelope = 0.0f;
    gateOpen = false;
    gateGain = gate != nullptr && gate->get() > -99.5f ? 0.0f : 1.0f;
    cabinet.reset();
}

double NightAmp::getLatencySeconds()
{
    return model != nullptr ? model->net->latencySamples() / sampleRate : 0.0;
}

//==============================================================================
juce::String NightAmp::loadModel (const juce::File& file)
{
    auto fail = [this] (const juce::String& why)
    {
        modelProblem = why;
        return modelProblem;
    };

    if (! file.existsAsFile())
        return fail ("Can't find the amp model \"" + file.getFileName() + "\"" + (file.getFullPathName().isNotEmpty() ? " (it was in " + file.getParentDirectory().getFullPathName() + ")" : juce::String()));

    auto m = std::make_unique<Model>();
    std::string why;
    m->net = NamRunner::load (pathOf (file), why);

    // In plain words, not the parser's (NAM's own message goes to the log)
    if (m->net == nullptr)
    {
        DBG ("NAM: " << file.getFullPathName() << ": " << why);
        const bool newer = juce::String (why).containsIgnoreCase ("version");
        return fail ("\"" + file.getFileName() + "\" isn't an amp model NightShift can open: "
                     + (newer ? "it's from a newer Neural Amp Modeler than NightShift's" : "it isn't a .nam model, or it's damaged"));
    }

    m->levelGain = m->net->hasLoudness() ? (float) juce::Decibels::decibelsToGain (targetLoudnessDb - m->net->loudnessDb()) : 1.0f;
    const bool hasInputLevel = m->net->hasInputLevel();
    const auto inputLevel = (float) m->net->inputLevelDbu();

    if (initialised)
        configure (*m);

    const double latencyBefore = getLatencySeconds();

    {
        const juce::SpinLock::ScopedLockType lock (modelLock);
        std::swap (model, m);
    }

    // (the model it replaced goes with m, here, off the audio thread)
    modelInputLevelDbu = inputLevel;
    modelHasInputLevel = hasInputLevel;
    description = describe (file);
    description.hasInputLevel = hasInputLevel;      // as NAM read it (a container of sizes says it at its top)
    description.inputLevelDbu = inputLevel;
    modelProblem = {};
    state.setProperty (modelProperty, file.getFullPathName(), nullptr);
    state.removeProperty (sizeProperty, nullptr);   // a new model comes whole

    // Running between resamplers delays it a little: the engine lines the track up again
    if (initialised && std::abs (getLatencySeconds() - latencyBefore) > 1.0e-9)
        edit.restartPlayback();

    return {};
}

juce::String NightAmp::loadCabinet (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (file.existsAsFile() ? formats.createReaderFor (file) : nullptr);

    if (reader == nullptr || reader->lengthInSamples < 1)
    {
        cabinetProblem = file.existsAsFile() ? "\"" + file.getFileName() + "\" isn't a sound file NightShift can use as a cabinet"
                                             : "Can't find the cabinet \"" + file.getFileName() + "\"";
        return cabinetProblem;
    }

    // JUCE's convolution reads it on a thread of its own and brings it in at a block's start, at the song's rate
    cabinet.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::yes, 0,
                                 juce::dsp::Convolution::Normalise::yes);
    cabinetLoaded = true;
    cabinetProblem = {};
    state.setProperty (cabinetProperty, file.getFullPathName(), nullptr);
    return {};
}

void NightAmp::clearModel()
{
    std::unique_ptr<Model> old;

    {
        const juce::SpinLock::ScopedLockType lock (modelLock);
        std::swap (model, old);
    }

    modelHasInputLevel = false;
    description = {};
    modelProblem = {};
    state.removeProperty (modelProperty, nullptr);
    state.removeProperty (sizeProperty, nullptr);
}

//==============================================================================
void NightAmp::setInterfaceLevel (double dBu)
{
    interfaceLevel = juce::jlimit (-20.0, 40.0, dBu);
}

double NightAmp::getInterfaceLevel()
{
    return interfaceLevel.load();
}

float NightAmp::calibrationDb() const
{
    return calibrate->get() >= 0.5f && modelHasInputLevel.load() ? (float) (interfaceLevel.load() - modelInputLevelDbu.load()) : 0.0f;
}

int NightAmp::numSizes() const
{
    return model != nullptr ? model->net->numSizes() : 1;
}

int NightAmp::getSize() const
{
    return model != nullptr ? model->net->getSize() : 0;
}

void NightAmp::setSize (int index)
{
    // NAM builds a smaller network here (or readies one of its sizes) and swaps it in on the audio thread
    // at the next block, so the sound goes on meanwhile
    if (model == nullptr || model->net->numSizes() < 2)
        return;

    model->net->setSize (index);

    if (model->net->getSize() == model->net->numSizes() - 1)
        state.removeProperty (sizeProperty, nullptr);
    else
        state.setProperty (sizeProperty, model->net->getSize(), nullptr);
}

void NightAmp::clearCabinet()
{
    cabinetLoaded = false;
    cabinetProblem = {};
    state.removeProperty (cabinetProperty, nullptr);
}

juce::File NightAmp::getModelFile() const
{
    const auto path = state[modelProperty].toString();
    return path.isNotEmpty() ? juce::File (path) : juce::File();
}

juce::File NightAmp::getCabinetFile() const
{
    const auto path = state[cabinetProperty].toString();
    return path.isNotEmpty() ? juce::File (path) : juce::File();
}

void NightAmp::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    StockEffect::restorePluginStateFromValueTree (v);

    // Saved with a model before input calibration came in: as the constructor, it stays off
    if (v.hasProperty (modelProperty) && ! v.hasProperty (calibrateProperty))
        calibrate->set (0.0f);

    loadSavedFiles (v);
}

void NightAmp::loadSavedFiles (const juce::ValueTree& v)
{
    // (loading a model starts it whole, so the size it was saved at is read first)
    const auto savedSize = v[sizeProperty];

    // A missing file keeps its path (the song still names it, and says it's missing)
    if (const auto path = v[modelProperty].toString(); path.isNotEmpty() && (! hasModel() || juce::File (path) != getModelFile()))
    {
        state.setProperty (modelProperty, path, nullptr);
        loadModel (juce::File (path));
    }

    if (hasModel())
        setSize (savedSize.isVoid() ? numSizes() - 1 : (int) savedSize);

    if (const auto path = v[cabinetProperty].toString(); path.isNotEmpty() && (! hasCabinet() || juce::File (path) != getCabinetFile()))
    {
        state.setProperty (cabinetProperty, path, nullptr);
        loadCabinet (juce::File (path));
    }
}

NightAmp::Description NightAmp::describe (const juce::File& namFile)
{
    // The .nam file is JSON: its metadata says what it is, when whoever captured it filled that in
    Description d;
    d.name = namFile.getFileNameWithoutExtension();
    const auto json = juce::JSON::parse (namFile);
    const auto meta = json.getProperty ("metadata", {});
    auto text = [&meta] (const char* key) { return meta.getProperty (key, {}).toString().replaceCharacter ('_', ' ').trim(); };

    if (text ("name").isNotEmpty())
        d.name = text ("name");

    d.gear = (text ("gear_make") + " " + text ("gear_model")).trim();
    d.toneType = text ("tone_type");
    d.modeledBy = text ("modeled_by");
    d.sampleRate = (double) json.getProperty ("sample_rate", 0.0);
    d.levelled = ! meta.getProperty ("loudness", {}).isVoid();
    d.hasInputLevel = ! meta.getProperty ("input_level_dbu", {}).isVoid();
    d.inputLevelDbu = (double) meta.getProperty ("input_level_dbu", 0.0);
    return d;
}

//==============================================================================
void NightAmp::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    float* left;
    float* right;

    if (! getStereo (fc, left, right))
        return;

    juce::ScopedNoDenormals noDenormals;

    // Input, and calibration's difference between the interface's level and the amp's (as NAM's plugin adds them)
    const float inGain = juce::Decibels::decibelsToGain (input->get() + calibrationDb());
    const float outGain = juce::Decibels::decibelsToGain (output->get());
    const float threshold = gate->get();
    const bool gated = threshold > -99.5f;
    const float openLevel = juce::Decibels::decibelsToGain (threshold);
    const float shutLevel = juce::Decibels::decibelsToGain (threshold - gateHysteresisDb);
    const float sr = (float) sampleRate;
    const float envelopeFall = std::exp (-1.0f / (0.05f * sr));                // the level it listens to: 50 ms
    const float opening = 1.0f - std::exp (-1.0f / (0.001f * sr));             // opens in 1 ms
    const float closing = 1.0f - std::exp (-1.0f / (0.06f * sr));              // closes in 60
    const bool cab = cabinetLoaded && cabinetOn->get() >= 0.5f;
    const bool even = evenOut->get() >= 0.5f;
    float peak = 0.0f;
    bool opened = ! gated;

    for (int start = 0; start < fc.bufferNumSamples; start += maxBlock)
    {
        const int n = std::min (maxBlock, fc.bufferNumSamples - start);
        float* l = left + start;
        float* r = right != nullptr ? right + start : nullptr;

        for (int i = 0; i < n; ++i)
            mono[(size_t) i] = (r != nullptr ? 0.5f * (l[i] + r[i]) : l[i]) * inGain;

        {
            const juce::SpinLock::ScopedTryLockType lock (modelLock);

            if (lock.isLocked() && model != nullptr)
            {
                model->net->process (mono.data(), amped.data(), n);

                if (even)
                    juce::FloatVectorOperations::multiply (amped.data(), model->levelGain, n);
            }
            else
            {
                // No amp (or one being swapped in): the sound as it comes
                std::copy (mono.begin(), mono.begin() + n, amped.begin());
            }
        }

        if (cab)
        {
            float* channels[1] = { amped.data() };
            juce::dsp::AudioBlock<float> block (channels, 1, (size_t) n);
            cabinet.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

        for (int i = 0; i < n; ++i)
        {
            float g = 1.0f;

            if (gated)
            {
                // It listens in front of the amp (the amp's own hiss would keep it open) and shuts what comes out
                gateEnvelope = std::max (std::abs (mono[(size_t) i]), gateEnvelope * envelopeFall);

                if (gateEnvelope > openLevel)
                    gateOpen = true;
                else if (gateEnvelope < shutLevel)
                    gateOpen = false;

                const float target = gateOpen ? 1.0f : 0.0f;
                gateGain += (target - gateGain) * (target > gateGain ? opening : closing);
                g = gateGain;
                opened = opened || gateOpen;
            }

            const float y = amped[(size_t) i] * g * outGain;
            peak = std::max (peak, std::abs (y));
            l[i] = y;

            if (r != nullptr)
                r[i] = y;
        }
    }

    if (opened)
        gateWasOpen = true;

    const float level = juce::Decibels::gainToDecibels (peak, -100.0f);
    float shown = outputDb.load();

    while (level > shown && ! outputDb.compare_exchange_weak (shown, level))
    {
    }
}

} // namespace nightshift
