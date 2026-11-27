// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "model/Song.h"
#include "ChannelRack.h"
#include "DockPanel.h"
#include "Inspector.h"
#include "Mixer.h"
#include "PianoRoll.h"
#include "Playlist.h"
#include "PluginWindow.h"
#include "StockPanels.h"
#include "DrummerPanel.h"
#include "BassPanel.h"
#include "GuitarPanel.h"
#include "KeysPanel.h"
#include "SynthPlayerPanel.h"
#include "SongPanel.h"
#include "AiPanel.h"
#include "MasterEngineerPanel.h"
#include "SynthPanel.h"
#include "MasterPanel.h"
#include "ComputerKeyboard.h"
#include "Sidebar.h"
#include "StartScreen.h"
#include "Toolbar.h"
#include "app/Updates.h"

namespace nightshift
{

/** The whole studio in one window: the top bar, the AI drummer's prompt bar under it, the
    sidebar on the left, Arrange on top and the editor underneath, with Steps, the piano roll,
    the mixer and the AI drummer as its tabs, and the Inspector on the right showing the
    selected channel's strip beside the master's. Drag the gap between Arrange and the editor
    to share the height between them.
*/
class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      public juce::FileDragAndDropTarget,
                      private Song::Listener,
                      private juce::Timer
{
public:
    MainComponent (Song&, AudioEngine&);
    ~MainComponent() override;

    /** Arrange (the playlist), and the editor's tabs. Showing Steps, the piano roll or the
        mixer opens the editor on that tab; hiding Arrange gives the editor the whole height
        (the mixer takes it when it opens, and gives it back when you leave it). */
    enum class Panel { playlist, rack, pianoRoll, mixer, drummer, synth, master, bass, guitar, keys, synthPlayer, song, masterAi };

    /** The smallest size the studio works at; the window and --snapshot both keep to it. */
    static constexpr int minWidth = 1100, minHeight = 640;
    void setPanelVisible (Panel, bool);
    bool isPanelVisible (Panel) const;

    /** F5: Arrange on its own, or back to sharing. F6/F7/F8/F9: that editor tab, or away again. */
    void togglePanel (Panel);

    juce::String getWindowTitle() const;
    std::function<void()> onTitleChanged;

    /** With unsaved changes, asks first: Save, Save as..., Don't save or Cancel. Calls back
        once it's fine to carry on (saved, or you chose not to); never after Cancel, a save
        that failed or a Save as you closed without a name. */
    void confirmDiscard (std::function<void()> carryOn);

    void newProject();
    void openProject();
    void openProjectFile (const juce::File&);

    /** Saves (asking for a name if it has none, or for Save as), then calls `saved` if it did. */
    void saveProject (bool saveAs, std::function<void()> saved = nullptr);
    void exportAudio();

    /** What the export's finished message says: where, how (bits, sample rate, how much), and
        how loud it came out, warning when the peak's above -1 dB. */
    static juce::String describeExport (const juce::File&, int bits, bool wholeSong, const mastering::Readings&);

    /** Export MIDI file: the whole song's notes as Arrange plays them, or one pattern's, with the
        tempo and time signature (see midifile::write), asked which first, then where. */
    void exportMidi();

    /** Writes the song's (patternId 0) or a pattern's notes to a .mid: whether it did, and what to
        tell you (where, how many notes on how many tracks; or why not). */
    std::pair<bool, juce::String> saveMidiFile (const juce::File&, int patternId);

    /** A .mid opened (Open, or given to NightShift): a new project of its tracks as channels,
        with its tempo and time signature. Asks to save changes first when it comes by Open. */
    void openMidiFile (const juce::File&);

    /** A .mid dropped on the studio: its tracks come in as channels, placed from `atTick` (the
        bar it's dropped on in Arrange); the song takes its tempo when it has no notes yet. */
    void addMidiFile (const juce::File&, int atTick);

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;
    void showAudioSettings();
    void scanPlugins();

    /** Looks for VST3 plugins installed since the last scan (the sidebar's Scan for VST3
        instruments, and at start): the button says it's scanning, then a toast names what's new
        (when `asked`, also that nothing is). Asked, Plugin instruments opens if there are new
        instruments; at start the sounds' groups stay shut. */
    void findNewPlugins (bool asked);

    /** What that toast says (empty for nothing to say). */
    static juce::String describeNewPlugins (const juce::StringArray& names, const juce::KnownPluginList&, bool asked);
    void showMainMenu();

    /** About NightShift: what it is, the keys, credits, and Donate (see Donate.h). */
    void showAbout (int screenHeight = 0);      // 0: the screen it opens on (another: a test of a shorter one)

    /** Check for updates (the ... menu): asks GitHub for the latest release (app/Updates.h). The newest
        already: "You are up to date". A newer one: what's new, and Install now, which asks to save first,
        downloads the installer (its progress, Cancel), checks it and starts it, and NightShift closes. */
    void checkForUpdates();

    /** What it says once GitHub has answered: the release it found, or why it couldn't ask (the text
        check shows it with a made-up release). */
    void updateChecked (const updates::Release&, const juce::String& error);
    void openPianoRoll (int channelId);
    void openPluginEditor (int channelId);
    void openEffectEditor (int effectId);

    /** A channel's AI drummer: the AI Drummer tab, writing for it. */
    void openDrummer (int channelId);
    DrummerPanel& getDrummer()              { return drummerPanel; }

    /** The AI bassist's tab on a channel (Shift+F8 opens it), and the panel itself. */
    void openBass (int channelId);
    BassPanel& getBass()                    { return bassPanel; }

    /** The AI guitarist's tab on a channel (Ctrl+F8 opens it), and the panel itself. */
    void openGuitar (int channelId);
    GuitarPanel& getGuitar()                { return guitarPanel; }

    /** The AI keys player's tab on a channel (Alt+F8 opens it), and the panel itself. */
    void openKeys (int channelId);
    KeysPanel& getKeys()                    { return keysPanel; }

    /** The AI synth player's tab on a channel (Ctrl+Shift+F8 opens it), and the panel itself. */
    void openSynthPlayer (int channelId);
    SynthPlayerPanel& getSynthPlayer()      { return synthPlayerPanel; }

    /** A synth channel's sound: the Synth tab, Nova's controls and the AI sound designer. */
    void openSynth (int channelId);
    SynthPanel& getSynth()                  { return synthPanel; }
    MasterPanel& getMaster()                { return masterPanel; }

    /** The Master tab on a page (0 loudness, 1 tone, punch and width), the header's switch with it. */
    void showMasterPage (int page);

    /** The AI tab: every agent, listed down its left (the song agent, the band, the mastering engineer). */
    AiPanel& getAiPanel()                   { return aiPanel; }
    DockPanel& getSidebarPanel()            { return sidebarPanel; }

    /** An agent in the AI tab (Panel::song, drummer, bass, guitar, keys, synthPlayer, masterAi). */
    void openAgent (Panel p)                { showEditorTab (p); }
    SongPanel& getSongPanel()               { return songPanel; }
    MasterEngineerPanel& getMasterEngineer() { return masterEngineer; }

    /** The AI song agent: a new project (after asking to save this one, if it has changes), every
        part written from the words, laid out two minutes or more in Arrange, and mastered. */
    void makeSong (const juce::String& words);
    DrummerBar& getDrummerBar()             { return drummerBar; }

    /** A drum kit from a folder of samples: asks for the folder, makes the kit, adds a channel
        playing it and opens its drummer. importDrumKitFrom does all but the asking, and returns
        what it made (or why it couldn't). */
    void importDrumKit();

    /** A note in the corner for a few seconds. */
    void showToast (const juce::String& message);

    /** "What do you want to do?" over the studio: templates, Open, recent projects, the demo.
        Closing it leaves what's loaded (a blank project, when NightShift opens or after New). */
    void showStartScreen();
    void hideStartScreen();
    bool isStartScreenVisible() const       { return startScreen.isVisible(); }
    StartScreen& getStartScreen()           { return startScreen; }

    /** Puts a project at the top of the recent ones (the start screen lists them). */
    void rememberProject (const juce::File&);

    /** Downloads the piano and orchestra in the background, the corner saying how far it's got. */
    void getInstruments();
    bool isFetchingInstruments() const      { return instrumentFetcher != nullptr; }
    juce::String importDrumKitFrom (const juce::File& folder);

    /** Double-click a pattern clip: the pattern in the piano roll, on the channel playing
        its melody (the selected one if it does), or in Steps if it's all drums. */
    void openPattern (int patternId);

    /** Double-click a channel's name: select it, and open its plugin's window, or the
        piano roll for a melody, or Steps for a drum, on a pattern it plays in; the
        Inspector shows the rest. */
    void openChannel (int channelId);

    /** The Inspector's instrument slot: the plugin's window, or the channel's menu, where
        the sample or preset is chosen. */
    void openInstrument (int channelId);

    /** I: the Inspector on the right, or away. It hides itself in narrow windows until asked for. */
    void toggleInspector();
    bool isInspectorVisible() const         { return inspectorPanel.isVisible(); }
    Inspector& getInspector()               { return inspector; }
    Mixer& getMixer()                       { return mixer; }
    Playlist& getPlaylist()                 { return playlist; }
    PianoRoll& getPianoRoll()               { return pianoRoll; }

    /** Opens a parameter's automation lane in Arrange (a fader's or knob's right-click
        Automate): the lane added, automation shown, Arrange on screen, a toast saying where.
        A channel with no track gets one; a bus's and the master's lanes are along the bottom. */
    void showAutomationFor (int owner, const juce::String& param);

    /** The record automation switch (beside Record, and in its menu): on or off, saying how it works. */
    void toggleAutomationRecording();

    /** A lane has started being written (recording automation): lanes on show, a channel's under
        a track of its own. */
    void showWrittenLane (int owner);

    /** A fader's or knob's right-click menu: Automate it in Arrange, link it to a knob on a MIDI
        controller (MIDI learn), or forget that link. */
    void showControlMenu (int owner, const juce::String& param, const juce::String& name);
    ChannelRack& getRack()                  { return rack; }

    /** Play (Space): plays or stops; with Record armed, it records. */
    void playOrStop();

    /** The Record button: arms or disarms it (punching in or out while playing). */
    void toggleRecordArm();

    /** Record's right-click menu, as FL's: the countdown, start on the first note, blend, snap. */
    juce::PopupMenu createRecordMenu();

    /** Add track: anything a channel can be, as a new track (at `insertAt`, -1 for the end),
        or an empty track; shown by `area` (on screen), or at the mouse if it's empty. */
    juce::PopupMenu createAddTrackMenu (int insertAt);
    void showAddTrackMenu (int insertAt, juce::Rectangle<int> area);

    Toolbar& getToolbar()                   { return toolbar; }

    Sidebar& getSidebar()                   { return sidebar; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    /** The PC keyboard, playing the selected channel (Shift+M switches it on and off). */
    ComputerKeyboard& getComputerKeyboard()     { return keyboard; }

    static constexpr int gap = 8, sidebarWidth = 252;

    /** Narrower than this, the Inspector waits to be asked for. */
    static constexpr int inspectorMinWindowWidth = 1280;

private:
    /** Export's second step: where to save it, then the render and the finished message. */
    void chooseExportFile (int bits);

    /** The gap between Arrange and the editor, which drags to resize them. */
    struct Splitter : public juce::Component
    {
        Splitter();
        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override    { repaint(); }
        void mouseExit (const juce::MouseEvent&) override     { repaint(); }
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

        std::function<void (int)> onDrag;
        std::function<int()> getStart;
        int start = 0;
    };

    Song& song;
    AudioEngine& audio;
    Toolbar toolbar;
    ChannelRack rack;
    PianoRoll pianoRoll;
    Playlist playlist;
    Sidebar sidebar;
    Inspector inspector { song, audio, rack };
    Mixer mixer { song, audio, rack };
    DrummerPanel drummerPanel { song, audio };
    BassPanel bassPanel { song, audio };
    GuitarPanel guitarPanel { song, audio };
    KeysPanel keysPanel { song, audio };
    SynthPlayerPanel synthPlayerPanel { song, audio };
    SongPanel songPanel { song };
    MasterEngineerPanel masterEngineer { song, audio };
    AiPanel aiPanel;
    SynthPanel synthPanel { song, audio };
    MasterPanel masterPanel { song, audio };
    DrummerBar drummerBar;
    DockPanel sidebarPanel, arrangePanel { "Arrange" }, editorPanel, inspectorPanel;
    SegmentedControl editorTabs { { "Steps", "Piano roll", "Mixer", "AI", "Synth", "Master" }, SegmentedControl::Style::tabs };
    SegmentedControl masterPages { { "Loudness", "Tone, punch & width" }, SegmentedControl::Style::pill };
    IconButton automationButton { "Automation", theme::icons::automation(), theme::accentCool };
    IconButton addTrackButton { "Audio track", theme::icons::plusLine() },
               editorSizeButton { "Editor size", theme::icons::expand() },
               inspectorButton { "Inspector", theme::icons::panelRight(), theme::accentCool };
    ToolStrip arrangeTools;
    Chip snapChip { "Bar" }, patternChip, channelChip, pianoSnapChip { "1/16" };
    Chip arrangeEditChip { "Edit" }, pianoEditChip { "Edit" }, quantizeChip { "Quantize" };
    LabelledBar swing { "Swing", 0.0, 1.0, 0.0 };
    juce::Label inLabel { {}, "in" };
    ToolStrip pianoTools;
    juce::String pianoSnap { "1/16" }, arrangeSnap { "Bar" };

    // The editor the editing keys (Ctrl+C, Delete...) go to: the one clicked last
    enum class Editor { arrange, pianoRoll };
    Editor lastEditor = Editor::arrange;

    // The top bar's MIDI light: how often its list of keyboards is brought up to date
    int midiListTicks = 0;
    void refreshMidiLight();
    Splitter splitter;

    /** A note in the corner that goes by itself ("Found 7 new plugins: ..."). */
    struct Toast : public juce::Component, private juce::Timer
    {
        void show (const juce::String& message);
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override  { setVisible (false); }
        void timerCallback() override                   { stopTimer(); setVisible (false); }
        juce::String text;
    };

    Toast toast;
    StartScreen startScreen;
    std::unique_ptr<juce::Thread> instrumentFetcher;
    std::unique_ptr<juce::Thread> updateWorker;     // Check for updates' asking, then its download
    void installUpdate (const updates::Release&);
    std::map<int, std::unique_ptr<PluginWindow>> pluginWindows, effectWindows;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 700 };
    juce::Component::SafePointer<juce::DialogWindow> audioSettingsWindow;
    juce::File currentFile, recordingsFileFor;
    juce::String lastTitle;
    Panel editorTab = Panel::rack;
    int editorHeight = -1;
    bool editorShown = true, editorMaximised = false, maximisedForMixer = false;
    std::optional<bool> inspectorChoice;    // Shift+I's say; until then it fits the window
    bool inspectorInMixer = false;          // Shift+I's say while the mixer is open
    bool drummerNewKit = false;             // "a new drum kit" was picked: the next request makes one
    bool bassNew = false;                   // "a new bass" was picked: the next request makes one
    bool guitarNew = false;                 // "a new guitar" was picked: the next request makes one
    bool keysNew = false;                   // "new keys" were picked: the next request makes a channel
    bool synthNew = false;                  // "a new synth" was picked: the next request makes a channel
    DrummerBar::Mode barMode = DrummerBar::Mode::drummer;     // who the prompt bar talks to
    ComputerKeyboard keyboard;
    std::map<int, int> liveNotes;           // a note the PC keyboard is holding -> the channel it plays on
    juce::uint32 lastKeyboardHint = 0;
    bool keyboardLit = false;

    // The PC keyboard reads the keys itself many times a second, so it plays whichever of
    // Nightshift's windows is in front (a plugin's own window takes the key presses)
    juce::TimedCallback keyboardPoll { [this] { pollKeyboard(); } };

    // Clicking anywhere in the studio but a text field gives the keys back to the studio
    std::unique_ptr<juce::MouseListener> focusReturner;

    void songChanged (Song::Change) override;
    void timerCallback() override;
    void showEditorTab (Panel);
    void updateArrangeTitle();
    void showSnapMenu();
    void showPatternChipMenu();
    void showPianoSnapMenu();
    int snapTicks (const juce::String& name) const;
    void applySnaps();
    void updateEditorHeader();
    void newPattern();
    void stepPattern (int delta);
    void loadProjectFile (const juce::File&);
    void closeStalePluginWindows();
    void selectChannel (int channelId);
    bool playsMelody (const Pattern&, const Channel&) const;
    static Panel tabPanel (int index);
    static int tabIndex (Panel);
    int makeDrummerChannel();
    void followSelectionWithDrummer (Song::Change);
    int makeBassChannel();
    void followSelectionWithBass (Song::Change);
    void showBassTargetMenu();
    int makeGuitarChannel();
    int makeKeysChannel (const keys::offline::Sound&);
    void followSelectionWithKeys (Song::Change);
    void showKeysTargetMenu();
    int makeSynthPlayerChannel (const juce::String& sound, const juce::String& role);
    void followSelectionWithSynthPlayer (Song::Change);
    void showSynthPlayerTargetMenu();
    void showMasterTargetMenu();

    // The AI tab's agents, in its list's order, and the one shown last
    static int agentIndex (Panel);
    static Panel agentPanel (int index);
    static bool isAgent (Panel p)           { return agentIndex (p) >= 0; }
    Panel lastAgent = Panel::song;

    // The song agent at work
    juce::String songWords;
    void writeSongParts (const juce::String& words);
    void finishSong (bool mastered);
    void followSelectionWithGuitar (Song::Change);
    void showGuitarTargetMenu();
    void showAgentMenu();
    void setBarMode (DrummerBar::Mode);
    void updateDrummerBar();
    void showDrummerTargetMenu();
    void setKeyboardOn (bool);
    bool typingIntoText() const;
    bool pollKeyboard();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace nightshift
