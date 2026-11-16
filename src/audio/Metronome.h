// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

namespace nightshift
{

/** The metronome's vocabulary, after Neural DSP's: a sound set, how many pulses a beat (the
    rhythm), time signatures with the way their beats group (7/8 as 2+2+3, 3+2+2 or 2+3+2), and
    an accent for every beat of the bar. */
namespace metronome
{
    /** How a beat sounds: strong (the downbeat), normal, soft, or not at all. */
    enum Accent : int { off = 0, soft = 1, normal = 2, strong = 3 };

    /** The sound set, in menu order. */
    const juce::StringArray& soundNames();

    /** Pulses a beat: where each falls in the beat (0..1); the first is the beat itself. */
    struct Rhythm
    {
        const char* name;
        std::vector<double> pulses;
    };

    const std::vector<Rhythm>& rhythms();

    /** A time signature and how its beats group, e.g. 7/8 as { 2, 2, 3 }. A simple one (4/4)
        has each beat in a group of its own. */
    struct Signature
    {
        int beats = 4, unit = 4;
        std::vector<int> groups;

        /** "4/4", "7/8  2+2+3" */
        juce::String name() const;

        /** "2+2+3", or "" for a simple one. */
        juce::String grouping() const;

        /** "7/8" */
        juce::String meter() const      { return juce::String (beats) + "/" + juce::String (unit); }
    };

    /** Every signature on offer, the song's time signatures with their groupings. */
    const std::vector<Signature>& signatures();

    /** The signature for a time signature and grouping ("2+2+3"); the first listed for it if
        that grouping isn't one of its own. */
    Signature find (int beats, int unit, const juce::String& grouping = {});

    /** The accents a signature starts with: strong on the one, normal on the start of each
        group, soft inside a group (a simple signature: strong, then normal). */
    std::vector<int> defaultAccents (const Signature&);

    /** Accents as text, a digit a beat ("3121211"), and back (padded or cut to `beats`). */
    juce::String accentsToText (const std::vector<int>&);
    std::vector<int> accentsFromText (const juce::String&, int beats);

    constexpr int maxBeats = 16;
}

//==============================================================================
/** The metronome itself. It plays after the master chain, straight to the output: the
    master's effects, limiter and meter never hear it, and exports never contain it. It
    clicks while the song plays (when it's on), counts you in before a recording whether or
    not it's on, and can run on its own, the song stopped, to practise to.

    Settings come from the message thread; the audio thread picks them up at the next block. */
class MetronomePlugin : public tracktion::Plugin
{
public:
    explicit MetronomePlugin (tracktion::PluginCreationInfo);
    ~MetronomePlugin() override;

    static const char* xmlTypeName;

    juce::String getName() const override                   { return "Metronome"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getSelectableDescription() override        { return getName(); }
    BusLayout getBusses() const override                    { return BusLayout::singleStereoInOut(); }
    bool producesAudioWhenNoAudioInput() override           { return true; }
    double getTailLength() const override                   { return 0.3; }

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override                            {}
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

    struct Settings
    {
        bool on = false;                    // click while the song plays
        bool recordingOnly = false;         // ...but only while recording
        int sound = 0, rhythm = 0;
        float volume = 0.7f;                // 0..1, 0.8 is the sounds as made
        float pan = 0.0f;                   // -1..1
        // Practising (the song stopped) clicks at this tempo, in this time signature
        double bpm = 120.0;
        int beats = 4, unit = 4;
        std::array<int, metronome::maxBeats> accents {};

        // Playing, it clicks the song's beats where they fall in its tempo (Tracktion's tempo map,
        // whose beat is `baseUnit`): through these stretches of one time signature, each from a
        // bar's start in ticks (a pattern's own alone, in Pattern mode)
        struct Section
        {
            int start = 0, beats = 4, unit = 4;
            std::array<int, metronome::maxBeats> accents {};
            bool operator== (const Section&) const = default;
        };

        static constexpr int maxSections = 32;
        std::array<Section, maxSections> sections {};
        int numSections = 0;
        int baseUnit = 4;

        bool operator== (const Settings&) const = default;
    };

    void setSettings (const Settings&);
    Settings getSettings() const;

    /** Clicks between these edit times (seconds) whether or not it's on: the count-in before
        a take. An empty range clears it. */
    void setCountIn (double fromSeconds, double toSeconds);

    /** A take is being recorded (for "only while recording"). */
    void setRecording (bool);

    /** Clicking on its own, the song stopped. Starts on a downbeat. */
    void setPractising (bool);
    bool isPractising() const                               { return practising.load(); }

    /** The pulse heard last: the beat in the bar (from 0) and the pulse in the beat, and a count
        that goes up with every pulse, for the beat lights. */
    struct Pulse
    {
        int beat = -1, pulse = 0;
        juce::uint32 count = 0;
    };

    Pulse getLastPulse() const;

    /** A sound's hit (accent: strong, normal or soft; 0 for a pulse between beats), mono, at
        the level it plays at with the volume at 0.8. */
    static juce::AudioBuffer<float> makeHit (int sound, int accent, double sampleRate);

    /** Volume (0..1) to gain: 0.8 is unity, as the master's. */
    static float volumeToGain (float volume);

private:
    struct Voice
    {
        const juce::AudioBuffer<float>* sample = nullptr;
        int position = 0, delay = 0;
        float gain = 0.0f;
    };

    // Per sound: the downbeat's hit, the beat's, and the one between beats
    std::vector<std::array<juce::AudioBuffer<float>, 3>> hits;
    double sampleRate = 48000.0;

    mutable juce::SpinLock settingsLock;
    Settings pending, active;
    std::atomic<double> countInFrom { 0.0 }, countInTo { 0.0 };
    std::atomic<bool> recording { false }, practising { false };
    juce::int64 practiceSamples = 0;
    bool wasPractising = false;

    std::array<Voice, 8> voices;
    std::atomic<int> lastBeat { -1 }, lastPulse { 0 };
    std::atomic<juce::uint32> pulseCount { 0 };

    void schedule (double fromSeconds, double toSeconds, int numSamples, bool playing);
    void startVoice (const juce::AudioBuffer<float>&, float gain, int delay);
    void render (juce::AudioBuffer<float>&, int start, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetronomePlugin)
};

} // namespace nightshift
