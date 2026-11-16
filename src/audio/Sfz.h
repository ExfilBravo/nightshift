// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** Sampled instruments from SFZ files: a grand piano, an orchestra, any free SFZ library. The
    samples are played straight from disk (memory-mapped, their starts read in ahead), so a
    gigabyte piano doesn't need a gigabyte of memory. */
namespace nightshift
{

namespace sfz
{
    /** One sample file: memory-mapped where it can be (WAV, AIFF), else read into memory (FLAC,
        OGG). */
    struct Sample
    {
        juce::File file;
        double sampleRate = 44100.0;
        juce::int64 length = 0;
        int channels = 0;
        juce::int64 loopStart = -1, loopEnd = -1;   // from the file's own loop, if it has one
        std::unique_ptr<juce::MemoryMappedAudioFormatReader> mapped;
        juce::AudioBuffer<float> loaded;

        /** Frames [start, start + count) into left and right (silence outside the sample). */
        void read (juce::int64 start, int count, float* left, float* right) const;
    };

    enum class Loop { none, oneShot, continuous, sustain };

    /** A region, its opcodes resolved through <global>, <master> and <group>. */
    struct Region
    {
        std::shared_ptr<const Sample> sample;
        int loKey = 0, hiKey = 127, keyCenter = 60, loVel = 1, hiVel = 127;
        float loRand = 0.0f, hiRand = 1.0f;
        int seqLength = 1, seqPosition = 1;
        int swLast = -1;                    // plays only while this keyswitch is the last one pressed
        bool release = false;               // trigger=release: played when the key goes up
        float rtDecay = 0.0f;               // a release sample's fall, dB per second the key was held
        float tuneCents = 0.0f, keytrack = 100.0f;
        float gain = 1.0f, pan = 0.0f, velTrack = 1.0f;
        float attack = 0.001f, hold = 0.0f, decay = 0.0f, sustain = 1.0f, releaseTime = 0.03f;
        int group = 0, offBy = 0;
        float offTime = 0.006f;
        Loop loop = Loop::none;
        juce::int64 loopStart = 0, loopEnd = 0, offset = 0;
        int notePolyphony = 0;
    };

    struct Instrument
    {
        juce::File file;
        juce::String name, error;
        juce::StringArray missing;          // samples it names that aren't there
        std::vector<Region> regions;
        int swLoKey = -1, swHiKey = -1, swDefault = -1;
        std::map<int, juce::String> switchLabels;   // keyswitch -> its articulation ("Pizzicato")
        int loKey = 127, hiKey = 0;                  // the playable range

        bool isSwitch (int key) const               { return swLoKey >= 0 && key >= swLoKey && key <= swHiKey; }
    };

    /** An SFZ file read (cached while anything plays it): its regions, samples mapped, their
        starts read in ahead in the background. Never null; `error` says what went wrong. */
    std::shared_ptr<const Instrument> load (const juce::File& sfz);

    /** "C4" -> 60, "c#3" -> 49, "Db-1" -> 1, "60" -> 60; -1 if it isn't a key. */
    int keyFromText (const juce::String&);
}

//==============================================================================
/** Plays an SFZ instrument: velocity layers, round robins, keyswitches, loops, release
    samples, the sustain pedal, choke groups, each voice's envelope. */
class SfzPlugin : public tracktion::Plugin
{
public:
    explicit SfzPlugin (tracktion::PluginCreationInfo);
    ~SfzPlugin() override;

    static const char* xmlTypeName;
    static const char* getPluginName()                      { return "Instrument"; }

    juce::String getName() const override                   { return "Instrument"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getSelectableDescription() override        { return getName(); }
    bool isSynth() override                                 { return true; }
    bool takesMidiInput() override                          { return true; }
    bool takesAudioInput() override                         { return false; }
    bool producesAudioWhenNoAudioInput() override           { return true; }
    int getNumOutputChannelsGivenInputs (int) override      { return 2; }
    BusLayout getBusses() const override                    { return BusLayout::singleStereoInOut(); }
    bool hasNameForMidiNoteNumber (int note, int midiChannel, juce::String& name) override;

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override                            {}
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    /** The instrument: its SFZ's path under the instruments folder (or a full path). */
    juce::CachedValue<juce::String> instrumentPath;

    void setInstrument (const juce::String& path);
    std::shared_ptr<const sfz::Instrument> getInstrument() const;

    /** Where sampled instruments live: %LOCALAPPDATA%\Nightshift\Instruments. */
    static juce::File folder();
    static juce::File fileFor (const juce::String& path);

    /** Tests: instruments live here instead. */
    static inline juce::File folderOverride;

private:
    struct Voice
    {
        const sfz::Region* region = nullptr;
        int note = 0, velocity = 0, age = 0, delay = 0;
        double position = 0.0, step = 1.0;
        float left = 1.0f, right = 1.0f;
        float level = 0.0f;                 // the envelope
        int stage = 0;                      // delay, attack, hold, decay, sustain, release
        double stageTime = 0.0;
        float releaseFactor = 1.0f;
        bool keyDown = true, held = false;  // held: the key's up but the pedal holds it
        bool active = false;
    };

    static constexpr int maxVoices = 128, chunk = 256;

    juce::CriticalSection lock;
    std::shared_ptr<const sfz::Instrument> instrument;
    std::array<Voice, maxVoices> voices {};
    juce::AudioBuffer<float> scratch;
    double sampleRate = 48000.0;
    int counter = 0, keyswitch = -1;
    bool pedal = false;
    std::array<int, 128> noteVelocity {}, seqCounter {};
    std::array<double, 128> noteOnTime {};
    double clock = 0.0;
    juce::Random random;

    void noteOn (int note, int velocity, int delay);
    void noteOff (int note, int delay);
    void pedalUp (int delay);
    void trigger (int note, int velocity, bool release, double heldFor, int delay);
    void releaseVoice (Voice&);
    void render (float* left, float* right, int numSamples);
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SfzPlugin)
};

} // namespace nightshift
