// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "audio/Samples.h"
#include "model/Song.h"
#include "Widgets.h"

namespace nightshift
{

/** The left column. Patterns: click one to edit it, drag it onto a track in Arrange to put it
    in the song there, double-click to rename it, right-click for more, + for a new one. Sounds:
    drum kits, sampled instruments, the built-in kit, the synth presets, plugin instruments and
    effects, in groups that all start shut (click one to open it), narrowed by the search box
    (Ctrl+K, which shows every match whatever's shut). Plugin instruments and Plugin effects are
    by developer, each developer shut too until clicked; searching a developer's name finds all
    its plugins.
    Double-click a sound, or drag it into
    Steps, to add it as a channel. Plugin effects go on the selected channel when
    double-clicked, or on the channel they're dropped on. While searching, Up and Down
    pick a match and Enter adds it. At the bottom, Find new instruments looks for plugins
    installed since NightShift last looked (right-click it for the plugin list and its folders).
    Its words are for beginners: no "VST3", no "scan".

    Samples is a browser of the audio files on this computer: the user's Downloads and Music,
    then folders they add (Add a folder..., kept with NightShift; right-click one to take it
    away), each a row that opens in place, folders in folders, all shut at first. Its folders
    are looked through (on a thread of their own) only once Samples is opened or searched.
    The search finds files by name in all of them. Click a sample to hear it through the
    studio's output, click it again to stop; drag it onto Arrange for an audio track, or into
    Steps (or double-click it) for a sampler channel.
*/
class Sidebar : public juce::Component,
                public juce::TooltipClient,
                private Song::Listener,
                private juce::ChangeListener,
                private juce::Timer
{
public:
    Sidebar (Song&, AudioEngine&);
    ~Sidebar() override;

    std::function<void()> onNewPattern, onScanPlugins, onSoundDragStarted, onPatternDragStarted;

    /** The Drum kits group's last rows: Get the studio drum kits (until they're here), and a
        kit from a folder of your own samples. */
    std::function<void()> onGetKits, onImportKit, onGetInstruments;
    std::function<void (const juce::String& path)> onAddInstrument;

    /** The drum kits changed on disk (fetched): list them again. */
    void kitsChanged()                          { rebuildSounds(); }
    std::function<void (const juce::String&)> onAddKitSound, onAddSynth;
    std::function<void (const juce::PluginDescription&)> onAddPlugin, onAddEffect;
    std::function<void (const juce::String& kitId)> onAddDrumKit;

    /** A sample double-clicked (or Enter on it while searching): a sampler channel playing it. */
    std::function<void (const juce::String& path)> onAddSample;

    /** Samples' card (the first time it opens with no packs yet), its Free sample packs... row or its right-click menu:
        the list of free sample packs. */
    std::function<void()> onGetPacks;

    /** A pack arrived (or went): Samples lists the free packs' folder, looked through again. */
    void packsChanged();

    /** Samples' folders, top to bottom: the user's Downloads and Music, then the ones they added. */
    juce::Array<juce::File> getSampleFolders() const;

    /** Adds a folder to Samples (kept with NightShift), opens it and shows it; or takes one added away. */
    void addSampleFolder (const juce::File&);
    void removeSampleFolder (const juce::File&);

    /** Looks through Samples' folders now (as opening Samples does) and waits for it, up to `ms`, then lists what it
        found (tests, snapshots). False if it's still looking. */
    bool lookThroughSamples (int ms);

    /** Where the sidebar finds what's in Samples' folders (tests). */
    const samples::Library& getSampleLibrary() const    { return sampleLibrary; }

    /** The button at the bottom: scan for new VST3 plugins (right-click: the plugin list, as
        onScanPlugins). While a scan runs it says so and can't be pressed. */
    std::function<void()> onScanInstruments;
    void setScanning (bool);
    bool isScanning() const                     { return ! scanButton.isEnabled(); }
    IconButton& getScanButton()                 { return scanButton; }

    /** Opens Plugin instruments and scrolls to it (after a scan found some), with the developers
        of the ones it found (their names) open too. */
    void showPluginInstruments (const juce::StringArray& found = {});

    /** What the sounds list shows now, top to bottom: group names and the sounds in open groups (tests). */
    juce::StringArray getShownSounds() const;

    /** Scrolls the sounds to the row with this name (a group, a developer or a sound), only from
        the group named on when there's one (a developer can be in both plugin groups), and says
        where it is now, to click; nothing when it isn't showing (tests, --snapshot --opensounds). */
    juce::Rectangle<int> showSoundRow (const juce::String& name, const juce::String& inGroup = {});

    /** One of the sounds' group names (Plugin effects, Drum kits...). */
    static bool isGroupName (const juce::String&);

    /** Ctrl+K: into the search box, ready to type. */
    void focusSearch();

    /** Fills the search box as if typed (for --snapshot --search). */
    void setSearchText (const juce::String& t)  { search.setText (t, true); }

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getTooltip() override;

    static constexpr int sectionHeaderHeight = 40, patternRowHeight = 36, soundRowHeight = 30, footerHeight = 54;

private:
    // (a sample folder opens like a developer; a note is a line of words, nothing to click)
    enum class Kind { pattern, group, kit, synth, plugin, scan, effect, drumkit, instrument, developer, sampleFolder, sample, note, card };
    static constexpr int getKitsRow = 1, importKitRow = 2, getInstrumentsRow = 3;  // scan rows' ids: fetch the studio kits, a kit from a folder, fetch the instruments
    static constexpr int addSampleFolderRow = 4, getPacksRow = 5;                   // and add a folder to Samples, the free sample packs
    static constexpr int samplesGroup = 7;

    struct Row
    {
        Kind kind;
        juce::String name, detail;
        juce::Colour colour;
        int id = 0;                         // the pattern's id, or the group's index
        juce::PluginDescription plugin;
        juce::String key;                   // a drum kit's folder, an instrument's SFZ, a developer as its plugins say it, a sample's or sample folder's full path
        int depth = 0;                      // 1: a developer in a plugin group, or one of its plugins; a sample folder's depth in Samples
        juce::String tip;                   // a developer's: its whole name, as its plugins give it (a long one is cut short in the row)
    };

    Song& song;
    AudioEngine& audio;
    juce::TextEditor search;
    IconButton scanButton { "Find new instruments", theme::icons::refresh() };    // plain words: no "VST3", no "scan" (the user, 2026-11-06)
    std::vector<Row> patternRows, soundRows;
    bool groupOpen[8] {};                   // all shut when NightShift opens (the user, 2026-11-06)
    juce::StringArray developersOpen;       // "group|developer", in both plugin groups: none when NightShift opens (the user, 2026-11-15)
    juce::StringArray sampleFoldersOpen;    // Samples' folders shown open, by full path: none when NightShift opens
    samples::Library sampleLibrary;
    juce::File previewShown;                // the sample shown as playing
    bool packsCardShown = false;            // Samples' "Get free sample packs" card has been shown since NightShift opened
    int patternScroll = 0, soundScroll = 0, hoveredPattern = -1, hoveredSound = -1;
    int highlightedSound = -1, pressedSound = -1, pressedPattern = -1;
    bool addHovered = false, dragging = false;

    /** Up and Down in the search box move through the matches, before the box sees them. */
    struct SearchKeys : public juce::KeyListener
    {
        explicit SearchKeys (Sidebar& s) : sidebar (s) {}
        bool keyPressed (const juce::KeyPress&, juce::Component*) override;
        Sidebar& sidebar;
    } searchKeys { *this };
    juce::Rectangle<int> patternsHeader, addButtonArea, patternsArea, soundsHeader, soundsArea;

    void songChanged (Song::Change) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildPatterns();
    void rebuildSounds();
    int patternAt (juce::Point<int>) const;
    int soundAt (juce::Point<int>) const;
    int maxPatternScroll() const;
    int maxSoundScroll() const;
    void showPatternMenu (int patternId);
    void renamePattern (int patternId);
    void addSound (const Row&);

    static bool isSound (const Row& r)      { return r.kind == Kind::kit || r.kind == Kind::synth || r.kind == Kind::plugin || r.kind == Kind::effect || r.kind == Kind::drumkit
                                                     || r.kind == Kind::instrument || r.kind == Kind::sample; }
    void moveHighlight (int delta);
    void startDraggingSound (const Row&);
    void startDraggingPattern (const Row&);

    // Samples
    std::vector<Row> sampleRows (const juce::String& filter, int& count);
    void addFolderRows (std::vector<Row>&, const juce::File&, const samples::Folder*, int depth);
    void lookThroughSampleFolders (double maxAgeSeconds);
    void toggleSamplePreview (const juce::File&);
    void showSampleMenu (const Row&);
    void chooseSampleFolder();
    void timerCallback() override;
    std::unique_ptr<juce::FileChooser> folderChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sidebar)
};

} // namespace nightshift
