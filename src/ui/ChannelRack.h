// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "model/Song.h"
#include "Widgets.h"

namespace nightshift
{

/** Steps: one row per channel with its mute swatch (the channel's colour; right-click to
    solo), name, level and pan, and the 16-step sequencer for the current pattern, its
    pads lit in the channel's colour by velocity. Channels holding piano-roll melodies
    show a preview of the notes instead. The pattern and swing live in the editor's header.
*/
class ChannelRack : public juce::Component,
                    public juce::FileDragAndDropTarget,
                    public juce::DragAndDropTarget,
                    private Song::Listener
{
public:
    ChannelRack (Song&, AudioEngine&);
    ~ChannelRack() override;

    void refreshPlayhead();
    int getIdealHeight() const;

    std::function<void (int channelId)> onOpenPianoRoll;
    std::function<void (int channelId)> onOpenPluginEditor;

    /** FL's double-click on a channel's name: open it. */
    std::function<void (int channelId)> onOpenChannel;

    /** The AI drummer's window for a channel. */
    std::function<void (int channelId)> onOpenDrummer;

    /** A synth channel's sound: Nova's panel and the AI sound designer. */
    std::function<void (int channelId)> onOpenSynth;

    /** Fetch the studio drum kits; make a kit from a folder of samples. */
    std::function<void()> onGetKits, onImportKit;

    /** Fetch the piano and orchestra. */
    std::function<void()> onGetInstruments;
    std::function<void (int effectId)> onOpenEffectEditor;
    std::function<void()> onScanPlugins;

    /** Puts an effect plugin at the end of a channel's chain and opens its editor; with no
        such channel, on the master. Returns the effect's id. */
    int addEffect (int channelId, const juce::PluginDescription&);

    /** Open, bypass, move or remove the effects in a chain (a channel's, or the master's
        with channelId 0), and add more. */
    juce::PopupMenu createEffectsMenu (int channelId);

    /** One effect's menu: show its editor, bypass, move earlier or later, remove. */
    juce::PopupMenu createEffectMenu (int channelId, int effectId);

    /** The effect plugins that can be added to a chain, and a rescan. */
    juce::PopupMenu createAddEffectMenu (int channelId);

    void showAddChannelMenu();

    /** Everything a channel can be: the AI drummer, an audio track (Guitar, Bass, Vocals...),
        a drum kit, a sampled instrument, a sample, a synth sound or a plugin. Choosing one adds
        it, then passes its id to `added` (Arrange's Add track gives it a track). */
    juce::PopupMenu createAddMenu (std::function<void (int channelId)> added);

    void showInputMenu (int channelId, juce::Rectangle<int> screenArea);

    /** A channel's menu: its sample or preset, piano roll, effects, rename, colour, delete. */
    void showChannelMenu (int channelId);

    /** Adds an audio channel on the first input, armed, with its Arrange track: named `name`
        ("Guitar", "Guitar 2"...), else "Audio N". Returns its id. */
    int addAudioChannel (const juce::String& name = {});

    /** Solo: every other channel muted; again (when it's the only one playing) all back. */
    void toggleSolo (int channelId);

    /** F2: a box to rename the channel. */
    void renameChannel (int channelId);

    /** A new bus ("Bus N" if no name), with a stock effect on it all wet if a type is given
        (a reverb bus, say). Returns its id. */
    int addBus (const juce::String& name, const juce::String& effectType = {});
    void deleteBus (int busId);
    void renameBus (int busId);

    /** Solo a bus: every channel not playing or sending into it muted; again, all back. */
    void toggleBusSolo (int busId);

    /** Add a channel playing a sound from the built-in kit, a synth preset, or a plugin
        instrument (opening its editor), and select it. */
    int addKitChannel (const juce::String& soundName);
    int addSynthChannel (const juce::String& presetName);

    /** A channel playing Nightshift Guitar on one of its sounds (guitarsound::presetNames()),
        called Guitar and selected. Undoable. */
    int addGuitarChannel (const juce::String& preset);
    int addPluginChannel (const juce::PluginDescription&);

    /** A channel for a sound dragged from the sidebar (see soundDragPrefix; not an effect).
        Returns its id, or 0 if the sound isn't one it knows. */
    int addSound (const juce::String& description);

    /** Gives a channel a dragged sound's instrument in place of its own. Its notes, levels,
        effects and sends stay; it takes the sound's name unless you'd named it. One undo step.
        False for an audio channel or a sound it doesn't know. */
    bool replaceInstrument (int channelId, const juce::String& description);

    /** A dragged sound's name ("Surge XT", "Red Zeppelin"). */
    juce::String soundName (const juce::String& description) const;

    /** What a channel is called when it's named after its instrument. */
    juce::String defaultNameFor (const Channel&) const;

    /** A channel playing one of the multi-sampled kits (see drumkits), selected. Returns its id. */
    int addDrumKitChannel (const juce::String& kitId);

    /** A sampled instrument (its SFZ under SfzPlugin::folder()), as a channel named after it. */
    int addInstrumentChannel (const juce::String& path);

    /** A channel playing a synth plugged in to a MIDI output, named after it, on the first MIDI
        channel no other channel uses there, selected. Undoable. Returns its id. */
    int addMidiOutChannel (const juce::String& output);

    /** Makes a channel play a synth plugged in (its notes, levels and AI parts stay). Undoable. */
    void playOnMidiOut (int channelId, const juce::String& output);

    /** The MIDI outputs, each calling `chosen` with its name ("None plugged in" when there are none). */
    juce::PopupMenu createMidiOutMenu (std::function<void (const juce::String&)> chosen, const juce::String& ticked = {});

    /** Audio settings, where an output's timing and tempo are. */
    std::function<void()> onShowAudioSettings;

    /** Every installed instrument by category, each calling `chosen` with its path. */
    static juce::PopupMenu createInstrumentMenu (std::function<void (const juce::String&)> chosen, const juce::String& ticked = {});

    /** Nova's factory sounds by category, each calling `chosen` with its name. */
    static juce::PopupMenu createSoundMenu (std::function<void (const juce::String&)> chosen, const juce::String& ticked = {});

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    /** Sounds dragged in from the sidebar become new channels. */
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    /** Adds what a dragged sound describes (see soundDragPrefix): a new channel, or an effect
        on the channel at `rackPosition` (else the selected one). False if it can't. */
    bool addDraggedSound (const juce::String& description, juce::Point<int> rackPosition = { -1, -1 });

    static constexpr int headerHeight = 30, rowHeight = 36, footerHeight = 48;

private:
    class Row;

    Song& song;
    AudioEngine& audio;
    juce::OwnedArray<Row> rows;
    juce::Viewport viewport;
    juce::Component rowHolder;
    IconButton addButton { "Add channel", theme::icons::plusLine() };
    std::unique_ptr<juce::FileChooser> chooser;
    int playingStep = -1, dropRowIndex = -1;
    double previewPlayhead = -1.0;

    void songChanged (Song::Change) override;
    void rebuildRows();
    void selectChannel (int channelId, bool preview);
    void browseForSample (int channelId, std::function<void (int)> added = nullptr);
    void setSample (int channelId, const juce::File&);
    void addSamplerChannels (const juce::StringArray& files, std::function<void (int)> added = nullptr);
    int rowIndexAt (juce::Point<int> rackPosition) const;
    static bool isAudioFile (const juce::String& path);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRack)
};

} // namespace nightshift
