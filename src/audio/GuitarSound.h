// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "fx/StockEffect.h"
#include "model/Song.h"

/** Nightshift Guitar: an electric guitar played by modelling its strings, no samples. Each
    note is a string (extended Karplus-Strong: a tuned delay line with its losses, the string's
    stiffness, where it's picked and where the pickup sits, a pluck shaped by how hard), then an
    amp: a tight high-pass and a mid push before the drive on the heavy ones, stages of drive,
    a tone stack, a cabinet and a touch of room. The heavy rhythm sounds are double-tracked: a
    second take, a few ms late and a hair out of tune, panned opposite.

    Played like a guitar from MIDI: a note goes on a string by itself (the highest string that
    can play it and isn't held; a new note on a busy string replaces the old one). Key 12 (C0)
    held palm-mutes the notes that start while it's down; key 13 (C#0) makes them dead notes
    (a muted scratch). A note that starts while another on the same string is still held is
    legato: up to 2 semitones a hammer-on or pull-off (no new pluck), further a slide.

    A channel plays it as a sampled instrument does, its instrument path naming the sound:
    "nightshift:guitar/High Gain". */
namespace nightshift
{

namespace guitarsound
{
    /** "Clean", "Jazz Box", "Funk Clean", "Lofi Clean", "Crunch", "Punk", "High Gain", "Djent". */
    juce::StringArray presetNames();

    /** What each sound is, in a few words, for menus and tooltips. */
    juce::String describe (const juce::String& preset);

    /** The instrument path a channel plays a sound by, and back. */
    inline const juce::String pathPrefix { "nightshift:guitar/" };
    juce::String pathFor (const juce::String& preset);
    bool isGuitarPath (const juce::String& path);
    juce::String presetOfPath (const juce::String& path);

    /** Key 12 held: palm mute. Key 13 held: dead notes. */
    constexpr int palmMuteKey = 12, deadNoteKey = 13;

    /** A note to play offline: key, when and how long in seconds, how hard (1..127). */
    struct Hit
    {
        int key = 40;
        double start = 0.0, length = 0.5;
        int velocity = 100;
    };

    /** A sound played offline, on its own (no engine), at 48 kHz: tests, level measuring,
        --guitarrender. `mostStrings` is the most strings that sounded at once. */
    struct Render
    {
        juce::AudioBuffer<float> audio;
        int mostStrings = 0;
    };

    Render render (const juce::String& preset, const std::vector<Hit>&, double seconds, juce::int64 seed = 1);

    /** A short phrase in the guitar's idiom, for listening and measuring: "chords" (open chords
        strummed), "riff" (power chords and palm-muted 8ths), "chug" (a palm-muted drop-C chug
        with accents). */
    std::vector<Hit> phrase (const juce::String& name);
}

/** Is this channel playing Nightshift Guitar, and on which sound. */
bool isGuitar (const Channel&);
juce::String guitarPresetOf (const Channel&);

//==============================================================================
class GuitarPlugin : public StockEffect
{
public:
    explicit GuitarPlugin (tracktion::PluginCreationInfo);
    ~GuitarPlugin() override;

    static const char* xmlTypeName;
    static const char* getPluginName()                      { return "Nightshift Guitar"; }

    juce::String getName() const override                   { return "Nightshift Guitar"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    bool isSynth() override                                 { return true; }
    bool takesMidiInput() override                          { return true; }
    bool takesAudioInput() override                         { return false; }
    bool producesAudioWhenNoAudioInput() override           { return true; }
    double getTailLength() const override                   { return 3.0; }

    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void reset() override;

    /** The sound it plays (one of guitarsound::presetNames()); from the next block. */
    void setPreset (const juce::String&);
    juce::String getPreset() const                          { return preset; }

    /** How many strings are sounding, for tests and the editor. */
    int getActiveStrings() const                            { return activeStrings.load(); }

    struct Engine;

private:
    std::unique_ptr<Engine> engine;
    juce::String preset { "Clean" };
    std::atomic<int> activeStrings { 0 };
    juce::SpinLock presetLock;
    juce::String pendingPreset;

    void prepare() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarPlugin)
};

} // namespace nightshift
