// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
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
*/
class Sidebar : public juce::Component,
                public juce::TooltipClient,
                private Song::Listener,
                private juce::ChangeListener
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
    enum class Kind { pattern, group, kit, synth, plugin, scan, effect, drumkit, instrument, developer };
    static constexpr int getKitsRow = 1, importKitRow = 2, getInstrumentsRow = 3;  // scan rows' ids: fetch the studio kits, a kit from a folder, fetch the instruments

    struct Row
    {
        Kind kind;
        juce::String name, detail;
        juce::Colour colour;
        int id = 0;                         // the pattern's id, or the group's index
        juce::PluginDescription plugin;
        juce::String key;                   // a drum kit's folder, an instrument's SFZ, a developer as its plugins say it
        int depth = 0;                      // 1: a developer in a plugin group, or one of its plugins
        juce::String tip;                   // a developer's: its whole name, as its plugins give it (a long one is cut short in the row)
    };

    Song& song;
    AudioEngine& audio;
    juce::TextEditor search;
    IconButton scanButton { "Find new instruments", theme::icons::refresh() };    // plain words: no "VST3", no "scan" (the user, 2026-11-06)
    std::vector<Row> patternRows, soundRows;
    bool groupOpen[7] {};                   // all shut when NightShift opens (the user, 2026-11-06)
    juce::StringArray developersOpen;       // "group|developer", in both plugin groups: none when NightShift opens (the user, 2026-11-15)
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
                                                     || r.kind == Kind::instrument; }
    void moveHighlight (int delta);
    void startDraggingSound (const Row&);
    void startDraggingPattern (const Row&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sidebar)
};

} // namespace nightshift
