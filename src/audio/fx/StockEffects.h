// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include "StockEffect.h"

/** Nightshift's own effects, always there without a scan: EQ, Compressor, Reverb, Delay,
    Chorus and Filter. They live in channel, bus and master chains like any VST3; their
    Effect::pluginId is the identifier of the description stockfx::descriptions() gives. */
namespace nightshift
{

//==============================================================================
/** A seven-band channel EQ, like Logic's: high pass, low shelf, three bells, high shelf and
    low pass, with the output going to an analyser the editor draws behind the curve. */
class NightEQ : public StockEffect
{
public:
    explicit NightEQ (tracktion::PluginCreationInfo);
    ~NightEQ() override;

    static const char* xmlTypeName;
    juce::String getName() const override                   { return "Nightshift EQ"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void reset() override;

    enum Band { highPass, lowShelf, bell1, bell2, bell3, highShelf, lowPass, numBands };

    struct BandSettings
    {
        bool on = true;
        double freq = 1000.0, gainDb = 0.0, q = 1.0;
        bool steep = false;     // high and low pass: 24 dB/oct instead of 12
    };

    /** A band as its parameters are now. */
    BandSettings settingsFor (int band) const;

    /** The whole curve's gain at a frequency (what the editor draws), in dB. */
    double responseDb (double freq) const;
    double bandResponseDb (int band, double freq) const;

    static juce::String bandName (int band);

    /** The id of a band's frequency, gain, Q, on and slope parameters (empty if it has none). */
    static juce::String paramId (int band, const char* what);

    /** The latest `num` samples of the output (mono), for the analyser. */
    void copyAnalysis (float* dest, int num) const;
    double getSampleRate() const                            { return sampleRate; }

private:
    dsp::Biquad filters[numBands][2];
    BandSettings current[numBands];
    bool settled = false;

    // Each band's frequency, gain, Q, on and slope parameters (null where it has none)
    Param* bandParams[numBands][5] {};
    Param* outputParam = nullptr;

    static constexpr int analysisSize = 8192;
    std::vector<float> analysis = std::vector<float> (analysisSize, 0.0f);
    std::atomic<int> analysisWrite { 0 };

    void prepare() override;
    void updateFilters (bool jump);
    static void coefficientsFor (const BandSettings&, int band, double sampleRate, dsp::Coeffs& first, dsp::Coeffs& second);
};

//==============================================================================
/** A stereo-linked compressor with a soft knee, attack and release smoothed in dB, makeup
    (or automatic makeup), parallel mix, and a high pass on what it listens to. It can
    listen to another channel or bus instead (sidechain): the engine wires that track in
    when `sidechain` names one. */
class NightCompressor : public StockEffect
{
public:
    explicit NightCompressor (tracktion::PluginCreationInfo);
    ~NightCompressor() override;

    static const char* xmlTypeName;
    juce::String getName() const override                   { return "Nightshift Compressor"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    void getChannelNames (juce::StringArray* ins, juce::StringArray* outs) override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void reset() override;

    /** The channel or bus whose sound drives it (0: its own input). */
    juce::CachedValue<int> sidechain;

    /** Set by the engine once the sidechain is wired in, so channels 3 and 4 can be trusted. */
    std::atomic<bool> sidechainConnected { false };

    /** The static curve: output level for an input level, in dB. */
    static float curveDb (float inDb, float thresholdDb, float ratio, float kneeDb);
    float makeupDb() const;

    /** Since the last call: the most gain reduction (dB, positive), and the input and
        output peaks (dB). */
    float takeReductionDb()                                 { return reductionDb.exchange (0.0f); }
    float takeInputDb()                                     { return inputDb.exchange (-100.0f); }
    float takeOutputDb()                                    { return outputDb.exchange (-100.0f); }

private:
    Param *threshold, *ratio, *knee, *attack, *release, *makeup, *autoMakeup, *mix, *highPass;
    float envelopeDb = 0.0f, meanSquare = 0.0f;
    dsp::Biquad detectorFilter;
    std::atomic<float> reductionDb { 0.0f }, inputDb { -100.0f }, outputDb { -100.0f };
    float lastHighPass = -1.0f;

    void prepare() override;
};

//==============================================================================
/** An algorithmic reverb: diffusers into an eight-line feedback delay network with gently
    moving delays, damping in the loop, pre-delay, a low cut and stereo width on the wet. */
class NightReverb : public StockEffect
{
public:
    explicit NightReverb (tracktion::PluginCreationInfo);
    ~NightReverb() override;

    static const char* xmlTypeName;
    juce::String getName() const override                   { return "Nightshift Reverb"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    double getTailLength() const override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void reset() override;

    static constexpr int numLines = 8, numDiffusers = 4;

private:
    Param *mix, *preDelayMs, *size, *decay, *dampingHz, *lowCutHz, *width;
    double sizeNow = -1.0;
    dsp::DelayLine preDelay[2], lines[numLines], diffusers[2][numDiffusers];
    dsp::OnePole damping[numLines];
    dsp::Biquad lowCut;
    double lfoPhase[numLines] {};
    float lastDamping = -1.0f, lastLowCut = -1.0f;

    void prepare() override;
};

//==============================================================================
/** A stereo delay: in time with the song (note values, dotted and triplet) or in ms, with
    filters in the feedback, ping-pong, a little tape wobble, and width on the wet. */
class NightDelay : public StockEffect
{
public:
    explicit NightDelay (tracktion::PluginCreationInfo);
    ~NightDelay() override;

    static const char* xmlTypeName;
    juce::String getName() const override                   { return "Nightshift Delay"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    double getTailLength() const override                   { return 4.0; }
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void reset() override;

    /** The note values the synced time steps through, as beats, and their names. */
    static int numDivisions();
    static double divisionBeats (int index);
    static juce::String divisionName (int index);

    /** The delay time now, in ms (from the division and tempo when synced). */
    double delayMs (double bpm) const;
    double lastBpm() const                                  { return bpmNow.load(); }

private:
    Param *sync, *division, *timeMs, *feedback, *pingPong, *highCutHz, *lowCutHz, *mod, *width, *mix;
    dsp::DelayLine lines[2];
    dsp::Biquad highCut, lowCut;
    double currentDelay = -1.0, lfoPhase = 0.0;
    float lastHighCut = -1.0f, lastLowCut = -1.0f;
    std::atomic<double> bpmNow { 120.0 };
    tracktion::tempo::Sequence::Position tempoPosition { createPosition (edit.tempoSequence) };

    void prepare() override;
};

//==============================================================================
/** A stereo chorus: two voices a side on slow, offset LFOs, with feedback for a flanged edge. */
class NightChorus : public StockEffect
{
public:
    explicit NightChorus (tracktion::PluginCreationInfo);
    ~NightChorus() override;

    static const char* xmlTypeName;
    juce::String getName() const override                   { return "Nightshift Chorus"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void reset() override;

private:
    Param *rate, *depth, *delayMs, *feedback, *width, *mix;
    dsp::DelayLine lines[2];
    double lfoPhase = 0.0;
    float feedbackSample[2] {};

    void prepare() override;
};

//==============================================================================
/** A resonant filter: low, band or high pass, 12 or 24 dB/oct, with drive before it. */
class NightFilter : public StockEffect
{
public:
    explicit NightFilter (tracktion::PluginCreationInfo);
    ~NightFilter() override;

    static const char* xmlTypeName;
    juce::String getName() const override                   { return "Nightshift Filter"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void reset() override;

    /** The response at a frequency for the current settings, in dB. */
    double responseDb (double freq) const;
    double getSampleRate() const                            { return sampleRate; }

    static double qFor (float resonance)                    { return 0.5 + 11.5 * std::pow ((double) resonance, 2.0); }

private:
    Param *mode, *cutoff, *resonance, *drive, *steep, *mix;
    dsp::Svf first, second;
    double cutoffNow = -1.0;

    void prepare() override;
};

//==============================================================================
/** An amp, captured: a Neural Amp Modeler model (a .nam file of a real amp, pedal or whole rig, run by NAM's own
    core) with a cabinet after it (an impulse response, a .wav) when the model is the amp alone. In front, input gain
    and a noise gate (as NAM's own plugin has); after, output gain, and each model's loudness evened out to -18 dB so
    swapping amps doesn't jump in level. A model trained at another sample rate than the song's runs at its own rate
    between two resamplers (NAM's). The files' paths are kept in the effect's state, so a song opens with its amp.
    Mono in (both sides mixed), the same out of both.

    Input calibration, as NAM's plugin has it: a model that says how loud its amp was played when it was captured
    (input_level_dbu, the level of a sine at 0 dBFS there) is played as loud from the audio interface, whose own
    level (the same, for its input) is one setting for every amp (setInterfaceLevel). The input gain gains the
    difference, the interface's level less the model's. A "slimmable" model comes in sizes: smaller ones take less
    of the computer and sound a little less like the amp (setSize; kept with the song). */
class NightAmp : public StockEffect
{
public:
    explicit NightAmp (tracktion::PluginCreationInfo);
    ~NightAmp() override;

    static const char* xmlTypeName;
    juce::String getName() const override                   { return "Nightshift NAM Amp"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void reset() override;
    double getLatencySeconds() override;

    /** Loads an amp model (.nam) or a cabinet (.wav); the sound changes from the next block. Returns empty, or what
        went wrong in plain words (which getProblem() then says too). */
    juce::String loadModel (const juce::File&);
    juce::String loadCabinet (const juce::File&);
    void clearModel();
    void clearCabinet();

    juce::File getModelFile() const;
    juce::File getCabinetFile() const;
    bool hasModel() const                                   { return model != nullptr; }
    bool hasCabinet() const                                 { return cabinetLoaded; }

    /** What a model's file says about itself, for the panel: its name, the gear captured ("Fender Deluxe Reverb"),
        its kind of tone, who captured it, the rate it was trained at. */
    struct Description
    {
        juce::String name, gear, toneType, modeledBy;
        double sampleRate = 0.0;
        bool levelled = false;      // it says how loud it is, so it can be evened out
        bool hasInputLevel = false; // it says how loud its amp was played (input calibration)
        double inputLevelDbu = 0.0;
    };

    Description describeModel() const                       { return description; }
    static Description describe (const juce::File& namFile);

    /** The audio interface's input level for every amp: the dBu a sine peaking at 0 dBFS has at its input (its
        manual's maximum input level). NAM's plugin starts it at 12; the studio sets it from its options. */
    static void setInterfaceLevel (double dBu);
    static double getInterfaceLevel();
    static constexpr double defaultInterfaceLevel = 12.0;

    /** What input calibration adds to the input gain now, in dB: the interface's level less the model's, when
        Match input level is on and the model says its level; else 0. */
    float calibrationDb() const;

    /** The sizes the model comes in (1 when it isn't slimmable, or there's no model), and the one it runs at:
        0 the smallest, numSizes() - 1 the whole model (where a new model starts). The size is kept in the state. */
    int numSizes() const;
    int getSize() const;
    void setSize (int index);

    /** What's wrong with the files, or empty: the model's problem (missing, not a model), then the cabinet's. */
    juce::String getProblem() const                         { return (modelProblem + "\n" + cabinetProblem).trim(); }

    /** Whether the gate has been open since the last call (for its light). */
    bool takeGateOpen()                                     { return gateWasOpen.exchange (false); }

    /** Since the last call, the loudest the amp's output got (dB), for its meter. */
    float takeOutputDb()                                    { return outputDb.exchange (-100.0f); }

private:
    struct Model;
    std::unique_ptr<Model> model;
    juce::SpinLock modelLock;
    juce::dsp::Convolution cabinet;
    bool cabinetLoaded = false;

    Param *input, *output, *gate, *evenOut, *cabinetOn, *calibrate;
    int maxBlock = 512;
    bool initialised = false;
    std::vector<float> mono, amped;
    float gateEnvelope = 0.0f, gateGain = 1.0f;
    bool gateOpen = false;
    std::atomic<bool> gateWasOpen { false };
    std::atomic<float> outputDb { -100.0f };
    Description description;
    juce::String modelProblem, cabinetProblem;

    // The model's input level, for the audio thread (set as a model comes in and goes)
    std::atomic<bool> modelHasInputLevel { false };
    std::atomic<float> modelInputLevelDbu { 0.0f };
    static std::atomic<double> interfaceLevel;

    void prepare() override;
    void configure (Model&) const;
    void loadSavedFiles (const juce::ValueTree&);
};

//==============================================================================
namespace stockfx
{
    /** Makes the engine able to create them. */
    void registerAll (tracktion::Engine&);

    /** One description per stock effect (format "Nightshift"), in the order menus show them. */
    const juce::Array<juce::PluginDescription>& descriptions();

    /** The plugin type for a stock effect's identifier string, or {} if it isn't one. */
    juce::String typeForIdentifier (const juce::String& identifier);

    /** A short line on what it does, for menus and tooltips. */
    juce::String blurb (const juce::String& xmlTypeName);
}

} // namespace nightshift
