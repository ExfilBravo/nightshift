// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "DrumGizmo.h"

/** Multi-sampled drum kits (the AVL Drumkits, in %APPDATA%\Nightshift\Kits as SFZ): reading
    them, naming their pieces, and the instrument that plays them. */
namespace nightshift
{

namespace drumkits
{
    struct Region
    {
        int lowVelocity = 1, highVelocity = 127;
        float lowRandom = 0.0f, highRandom = 1.0f;  // alternates for the same velocity: one is picked at random
        float gain = 1.0f;                  // its volume (SFZ volume=, in dB)
        float velocityTracking = 1.0f;      // how far the velocity changes the level as well (amp_veltrack=, 0..100%)
        int sample = -1;                    // into Kit::samples
    };

    /** A kit piece on one key: a kick, a ride bell, a hi-hat closed... */
    struct Piece
    {
        int key = 0;
        juce::String name;                  // "Hat Closed", "Crash 17"
        float pan = 0.0f;                   // -1..1, from the drummer's seat
        int group = 0, offBy = 0;           // SFZ choke groups: a hit in `offBy`'s group silences this
        std::vector<Region> regions;        // velocity layers
    };

    struct Sample
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
    };

    struct Kit
    {
        juce::String id, name;              // the folder, and how it's shown ("Red Zeppelin")
        std::map<int, Piece> pieces;
        std::vector<Sample> samples;

        const Piece* findPiece (int key) const;
    };

    /** Where kits live: %APPDATA%\Nightshift\Kits. */
    juce::File folder();

    /** Tests: kits live here instead. */
    inline juce::File folderOverride;

    /** A kit (or a few) Nightshift can fetch: a DrumGizmo studio kit, converted once it's here,
        or an SFZ kit's zip, unpacked as it is. */
    struct Download
    {
        juce::String name, url;             // "CrocellKit", where it comes from
        int megabytes = 0;
        juce::StringArray kits;             // the kits it gives (their folders)
        std::vector<drumgizmo::Recipe> recipes;     // a DrumGizmo kit's conversions (none: an SFZ zip)
    };

    /** What Get the studio drum kits fetches: CrocellKit, MuldjordKit and DRSKit from
        drumgizmo.org (CC BY 4.0). Tests swap in their own. */
    std::vector<Download>& downloads();

    /** Whether every kit downloads() gives is here. */
    bool haveDownloads();

    /** Where downloads wait (and resume from) until they're converted: %LOCALAPPDATA%\Nightshift\Downloads. */
    juce::File downloadsFolder();

    /** Downloads `url` to `target` by way of `target`.part, carrying on from a part already there
        when the server can, the size taken from the headers (so a file over 4 GB checks out),
        reporting the fraction done. Returns what went wrong, or "". */
    juce::String downloadFile (const juce::String& url, const juce::File& target, std::function<void (double)> progress = {},
                               std::function<bool()> shouldStop = {}, juce::int64 expectedBytes = -1);

    /** Downloads what isn't here yet (resuming a download cut short), converts or unpacks each
        into folder() and deletes its zip, telling `progress` how far it's got (0..1) and on
        what. Blocks: call it off the message thread. Returns what went wrong, or "" once they're
        all here. */
    juce::String fetch (std::function<void (float, const juce::String&)> progress, std::function<bool()> shouldStop);

    /** Unpacks a kit's zip into folder(): only its own folder of samples and SFZ, put in place
        once it's all out. Returns what went wrong, or "". */
    juce::String unpack (const juce::File& zip, const juce::String& id);

    /** The kits there, by id (folder name), in a sensible order. */
    juce::StringArray ids();

    /** "RED_ZEPPELIN_2023_repack" -> "Red Zeppelin". */
    juce::String displayName (const juce::String& id);

    /** What a kit sounds like and suits, for the drummer choosing one ("" if unknown). */
    juce::String character (const juce::String& id);

    /** Makes a kit from a folder of one-shots (a free sample pack, say): each sound goes on the
        drum its file or folder name says it is ("BD_Punchy_01" a kick, "Hats/Open 2" an open
        hat), several of a kind become takes chosen between at random, each at the level the
        other kits play it, copied into folder() under the folder's name. Returns the new kit's
        id, or "" with `report` saying why; on success `report` says what went where. */
    juce::String importFolder (const juce::File& source, juce::String& report);

    /** The drum a sample's name says it is ("CH 01" -> hat_closed, "Tom Lo" -> tom_floor), or
        "" when it doesn't say. */
    juce::String voiceForName (const juce::String& name);

    /** How Nightshift's own kits (converted and synthesised) name and place a drum: the name
        keyForVoice matches it by, and its General MIDI key. */
    std::pair<juce::String, int> pieceFor (const juce::String& voice);

    /** How hard each drum's hardest hit peaks in Nightshift's own kits (as a gain), the way a
        sample library sets them: kick and snare hot, toms just under, cymbals and hats down
        where they sit in a mix. */
    float peakFor (const juce::String& voice);

    /** The kit with this display name, or "" if there's none. */
    juce::String idForName (const juce::String& name);

    /** The kit, loaded once and shared; null if it isn't there. */
    std::shared_ptr<const Kit> load (const juce::String& id);

    /** Reads the SFZ (and samples, unless `withSamples` is false) from a kit folder. */
    std::shared_ptr<Kit> read (const juce::File& kitFolder, bool withSamples);

    //==============================================================================
    /** The drums the AI drummer writes for, by name. A kit maps each to one of its keys;
        General MIDI is the map for drum plugins. */
    const juce::StringArray& voiceNames();

    /** The key a kit plays for a voice (with sensible stand-ins: a second crash for a missing
        china, say), or -1 if it has nothing like it. */
    int keyForVoice (const Kit&, const juce::String& voice);

    /** General MIDI's key for a voice. */
    int generalMidiKey (const juce::String& voice);

    /** The voices a kit can play (for telling the drummer what it has). */
    juce::StringArray voicesIn (const Kit&);

    /** A General MIDI drum's name, for drum plugins in the piano roll. */
    juce::String generalMidiName (int key);
}

//==============================================================================
/** Plays a drum kit: velocity layers, each piece panned, one-shot, choke groups (an open
    hat closed by the pedal, a crash grabbed by its choke). */
class DrumKitPlugin : public tracktion::Plugin
{
public:
    explicit DrumKitPlugin (tracktion::PluginCreationInfo);
    ~DrumKitPlugin() override;

    static const char* xmlTypeName;
    static const char* getPluginName()                      { return "Drum Kit"; }

    juce::String getName() const override                   { return "Drum Kit"; }
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

    /** The kit's folder name under drumkits::folder(). */
    juce::CachedValue<juce::String> kitId;

    void setKit (const juce::String& id);
    std::shared_ptr<const drumkits::Kit> getKit() const;

private:
    struct Voice
    {
        const drumkits::Sample* sample = nullptr;
        double position = 0.0, step = 1.0;
        float left = 1.0f, right = 1.0f;
        int key = 0, group = 0, offBy = 0, delay = 0, age = 0;
        float fade = 1.0f, fadeStep = 0.0f;
    };

    /** Every kit plays 3 dB down, so a full kit's hits summed mostly stay clear of the master limiter. */
    static constexpr float headroom = 0.7071f;

    juce::Random random;
    std::array<int, 128> lastSample {};     // per key, so an alternate is never the same hit twice running

    juce::CriticalSection lock;
    std::shared_ptr<const drumkits::Kit> kit;
    std::vector<Voice> voices;
    double sampleRate = 48000.0;
    int counter = 0;

    void startVoice (int key, int velocity, int delay);
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumKitPlugin)
};

} // namespace nightshift
