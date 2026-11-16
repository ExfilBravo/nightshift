// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "fx/StockEffect.h"

/** Nova, Nightshift's own synth: two oscillators that morph sine to triangle to saw to square,
    each with up to eight unison voices, FM, a sub and noise; a resonant filter with drive;
    three envelopes, two LFOs that can follow the song's tempo and a six-slot mod matrix;
    poly, mono and legato with glide; drive, chorus, delay and reverb built in.

    Every setting is a named parameter, so a sound is just the settings that differ from the
    init sound (a Patch), which is what the factory sounds are and what the AI sound designer
    writes. */
namespace nightshift
{

namespace nova
{
    /** A parameter as patches, the editor and the AI sound designer know it. */
    struct ParamInfo
    {
        const char* id;
        const char* name;               // "Cutoff"
        const char* section;            // "Filter"
        float min, max, defaultValue;
        float step;                     // 1 for whole numbers and choices, else 0
        float centre;                   // the value half way round a knob (0: evenly spread)
        const char* unit;               // "Hz", "s", "dB", "st", "cents", "oct", "%", "wave", "amount", "choice"...
        const char* choices;            // "Low pass|Band pass|..." for a choice, else ""
        const char* help;               // what it does, for the AI sound designer and tooltips

        juce::StringArray choiceNames() const     { return juce::StringArray::fromTokens (choices, "|", {}); }
        bool isChoice() const                       { return *choices != 0; }
    };

    /** Every parameter, in the editor's order. */
    const std::vector<ParamInfo>& params();
    const ParamInfo* findInfo (const juce::String& id);

    /** How a value reads: "1.2 kHz", "Saw", "-6.0 dB", "+3.0 oct". */
    juce::String valueText (const ParamInfo&, float value);

    /** The note values LFOs and the delay step through, in beats (quarter notes). */
    double divisionBeats (int index);

    /** A sound: its name, its category and the settings that differ from the init sound. */
    struct Patch
    {
        juce::String name, category;
        std::map<juce::String, float> settings;

        float get (const juce::String& id) const;

        /** {"name", "category", "settings": {id: value}}. */
        juce::var toVar() const;
        juce::String toJson() const             { return juce::JSON::toString (toVar(), true); }

        /** Values are clamped to their ranges; unknown ids are dropped, and a choice can be
            given by name ("Low pass"). Empty if it isn't a patch at all. */
        static std::optional<Patch> fromVar (const juce::var&);
        static std::optional<Patch> fromJson (const juce::String& json)   { return fromVar (juce::JSON::parse (json)); }
    };

    /** The factory sounds, by category. */
    const std::vector<Patch>& factory();
    const Patch* findFactory (const juce::String& name);
    juce::StringArray factoryNames();
    juce::StringArray categories();

    /** The sound a new synth channel starts with. */
    juce::String defaultPreset();

    /** A short phrase in a category's idiom (a bass line, a lead line, chords), the way sounds
        are auditioned and their levels measured: keys, start and length in beats at 120 BPM. */
    struct Note { int key; double start, beats; };
    std::vector<Note> phraseFor (const juce::String& category);

    /** How loud a sound plays its category's phrase: K-weighted, the loudest 400 ms, in dB.
        Runs Nova on its own (no engine), so any thread will do. */
    float loudness (const Patch&);

    /** The sound with its volume set so it plays at `targetDb` (the factory sounds sit near -18). */
    Patch levelled (Patch, float targetDb = -18.0f);
}

//==============================================================================
class NovaPlugin : public StockEffect
{
public:
    explicit NovaPlugin (tracktion::PluginCreationInfo);
    ~NovaPlugin() override;

    static const char* xmlTypeName;
    static const char* getPluginName()                      { return "Nova"; }

    juce::String getName() const override                   { return "Nova"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    bool isSynth() override                                 { return true; }
    bool takesMidiInput() override                          { return true; }
    bool takesAudioInput() override                         { return false; }
    bool producesAudioWhenNoAudioInput() override           { return true; }
    double getTailLength() const override;

    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void reset() override;

    /** Every parameter from a patch; those it leaves out go back to the init sound. In effect
        for the very next block. */
    void applyPatch (const nova::Patch&);

    /** The settings now, as a patch (only those away from their defaults). */
    nova::Patch currentPatch() const;

    /** How many voices are sounding, for the editor. */
    int getActiveVoices() const                             { return activeVoices.load(); }

    /** Tests only: when set, every Nova seeds its randomness (unison copies' start points, noise,
        random LFOs, the random mod source) with it each time it's prepared to play, as
        nova::loudness seeds its own, so a render through the engine and that measurement play the
        very same notes. Left unset, as in the studio, each Nova is random its own way. */
    static inline std::optional<juce::int64> seedForTests;

private:
    struct Engine;
    std::unique_ptr<Engine> engine;
    friend float nova::loudness (const nova::Patch&);
    std::vector<Param*> byIndex;
    std::atomic<int> activeVoices { 0 };
    tracktion::tempo::Sequence::Position tempoPosition { createPosition (edit.tempoSequence) };

    void prepare() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NovaPlugin)
};

} // namespace nightshift
