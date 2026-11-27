// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "MainComponent.h"
#include "model/Paths.h"
#include "ai/SongWriter.h"
#include "audio/GuitarSound.h"
#include "AudioSettings.h"
#include "Donate.h"
#include "Theme.h"
#include "audio/Kits.h"
#include "audio/Instruments.h"

namespace nightshift
{

using namespace theme;

MainComponent::MainComponent (Song& s, AudioEngine& a)
    : song (s), audio (a),
      toolbar (s, a),
      rack (s, a),
      pianoRoll (s, a),
      playlist (s, a),
      sidebar (s, a)
{
    addAndMakeVisible (toolbar);
    addAndMakeVisible (sidebarPanel);
    addAndMakeVisible (arrangePanel);
    addAndMakeVisible (splitter);
    addAndMakeVisible (editorPanel);
    addChildComponent (inspectorPanel);
    addChildComponent (startScreen);
    addChildComponent (toast);

    // The start screen: a template, a project or the demo replaces the blank project behind it
    startScreen.onTemplate = [this] (const juce::String& id)
    {
        audio.stop();
        currentFile = juce::File();
        templates::build (id, song, audio);
        hideStartScreen();
    };
    startScreen.onDemo = [this]
    {
        audio.stop();
        currentFile = juce::File();
        song.loadDemo();
        hideStartScreen();
    };
    startScreen.onOpen = [this]
    {
        hideStartScreen();
        openProject();
    };
    startScreen.onOpenRecent = [this] (const juce::File& file)
    {
        hideStartScreen();
        loadProjectFile (file);
    };
    startScreen.onClose = [this] { hideStartScreen(); };
    startScreen.onShowAtStartupChanged = [this] (bool show)
    {
        auto o = audio.getOptions();
        o.startScreen = show;
        audio.setOptions (o);
    };
    startScreen.onDonate = [] { donate::open(); };

    sidebarPanel.setHeaderVisible (false);
    sidebarPanel.setContent (&sidebar);

    inspectorPanel.setHeaderVisible (false);
    inspectorPanel.setContent (&inspector);

    arrangePanel.setContent (&playlist);
    inspectorButton.setFilled (false);
    inspectorButton.setTooltip ("Inspector: the selected channel's instrument, effects and levels, beside the master's (Shift+I)");
    inspectorButton.onClick = [this] { toggleInspector(); };
    arrangePanel.addToHeader (inspectorButton, 32, true, 32);

    addTrackButton.setFilled (false);
    addTrackButton.setFramed (true);
    addTrackButton.setLabel ("Add track");
    addTrackButton.setTooltip ("Add a track: a drum kit, the AI drummer, an audio track to record guitar, bass or vocals into, "
                               "an instrument, a synth or a plugin. Or drag a sound onto Arrange from the sidebar");
    addTrackButton.onClick = [this] { showAddTrackMenu (-1, addTrackButton.getScreenBounds()); };
    arrangePanel.addToHeader (addTrackButton, 140, true, 32);

    arrangeTools.addTool ("Select", icons::pointer(), "Select: move clips, and drag a box over clips to select them");
    arrangeTools.addTool ("Draw", icons::pencil(), "Draw: click empty space to paint (on an instrument's track, its pattern; "
                                                   "elsewhere the current pattern)");
    arrangeTools.addTool ("Erase", icons::eraser(), "Erase: click or drag over clips to delete them");
    arrangeTools.addTool ("Split", icons::scissors(), shortcutText ("Split: click a clip to cut it in two there "
                                                                    "(Ctrl+T cuts the selected clips at the song position)"));
    arrangeTools.setSelected (1);
    arrangeTools.onChange = [this] (int index)
    {
        playlist.setTool (index == 0 ? Playlist::Tool::select : index == 1 ? Playlist::Tool::draw
                        : index == 2 ? Playlist::Tool::erase : Playlist::Tool::split);
    };
    arrangePanel.addToHeader (arrangeTools, arrangeTools.getIdealWidth(), false, 32);

    snapChip.setIcon (icons::magnet());
    snapChip.setTooltip (shortcutText ("Snap: where clips and the loop land (hold Alt+ for finer)").replace ("+ for", " for"));
    snapChip.onClick = [this] { showSnapMenu(); };
    arrangePanel.addToHeader (snapChip, 98, false, 30);

    // Copy, paste, duplicate, split and the rest, with their keys
    arrangeEditChip.setTooltip (shortcutText ("Edit the selected clips: copy (Ctrl+C), paste (Ctrl+V), duplicate (Ctrl+B), "
                                              "split at the song position (Ctrl+T), delete"));
    arrangeEditChip.onClick = [this]
    {
        lastEditor = Editor::arrange;
        playlist.createEditMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&arrangeEditChip));
    };
    arrangePanel.addToHeader (arrangeEditChip, 84, false, 30);

    // Automation: lanes under the tracks (and the master's along the bottom)
    automationButton.setFilled (false);
    automationButton.setFramed (true);
    automationButton.setLabel ("Automation");
    automationButton.setTooltip ("Automation: lanes under the tracks (the master's along the bottom) where you draw how volume, "
                                 "pan or any knob changes over the song. Right-click a fader or knob to automate it");
    automationButton.onClick = [this]
    {
        song.showAutomation = ! song.showAutomation;
        song.notify (Song::Change::automation);
    };
    arrangePanel.addToHeader (automationButton, 136, false, 32);

    // A fader's or knob's right-click: automate it, or link a MIDI controller's knob to it
    automation::showMenu = [this] (int owner, const juce::String& param, const juce::String& name) { showControlMenu (owner, param, name); };
    automation::showMenuFor = [this] (tracktion::AutomatableParameter& p, const juce::String& name)
    {
        if (auto key = audio.automationKeyFor (p))
            showControlMenu (key->first, key->second, name);
        else
            showToast ("A bus's controls can't be automated or linked to a MIDI knob yet: a channel's and the master's can");
    };

    // A MIDI keyboard plugged in while running
    audio.onMidiInputAdded = [this] (const juce::String& name)
    {
        showToast ("MIDI keyboard ready: " + name + ". It plays the selected channel, and records when Record is armed");
    };

    playlist.onActivated = [this] { lastEditor = Editor::arrange; };
    playlist.onMessage = [this] (const juce::String& message) { showToast (message); };
    updateArrangeTitle();

    editorTabs.onChange = [this] (int index) { showEditorTab (index == 3 ? lastAgent : tabPanel (index)); };
    editorPanel.setTitleComponent (&editorTabs, editorTabs.getIdealWidth());
    editorSizeButton.setFilled (false);
    editorSizeButton.setTooltip ("Give the editor the whole height, or share it with Arrange again");
    editorSizeButton.onClick = [this]
    {
        editorMaximised = ! editorMaximised;
        maximisedForMixer = false;      // the size is yours now
        resized();
    };
    editorPanel.addToHeader (editorSizeButton, 32, true, 32);

    // The Master tab's two pages: how loud, and the shaping
    masterPages.setSelected (0);
    masterPages.setShortLabels ({ "Loudness", "Tone" });
    masterPages.setTooltip ("Loudness: what it's for and how loud it is. Tone, punch & width: shaping the sound before the limiter");
    masterPages.onChange = [this] (int page) { masterPanel.setPage (page); };
    editorPanel.addToHeader (masterPages, masterPages.getIdealWidth(), true, 30);

    // The piano roll's channel, then the pattern both editors work on
    channelChip.setTooltip ("The channel the piano roll edits: the ones with notes in this pattern are listed first");
    channelChip.onClick = [this]
    {
        pianoRoll.createChannelMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&channelChip));
    };
    editorPanel.addToHeader (channelChip, 156, false, 30);

    inLabel.setFont (font (16.5f));
    inLabel.setBorderSize ({});
    inLabel.setColour (juce::Label::textColourId, textFaint);
    inLabel.setJustificationType (juce::Justification::centred);
    inLabel.setInterceptsMouseClicks (false, false);
    editorPanel.addToHeader (inLabel, 22, false, 30);

    patternChip.setTooltip ("The pattern being edited (+ and - step through them)");
    patternChip.onClick = [this] { showPatternChipMenu(); };
    editorPanel.addToHeader (patternChip, 156, false, 30);

    swing.formatValue = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    swing.bar.formatValue = [] (double v) { return "Swing " + juce::String (juce::roundToInt (v * 100.0)) + "% (double-click for none)"; };
    swing.bar.onDragStart = [this] { song.checkpoint(); };
    swing.onValueChange = [this]
    {
        song.swing = (float) swing.bar.getValue();
        song.notify (Song::Change::transport);
    };
    editorPanel.addToHeader (swing, 210, true, 30);

    pianoSnapChip.setIcon (icons::magnet());
    pianoSnapChip.setTooltip ("Snap: where notes land and how they stretch");
    pianoSnapChip.onClick = [this] { showPianoSnapMenu(); };
    editorPanel.addToHeader (pianoSnapChip, 98, true, 30);

    // Quantize, and the rest of the piano roll's editing with its keys
    quantizeChip.setOpensMenu (false);
    quantizeChip.setTooltip (shortcutText ("Quantize (Ctrl+Q): the selected notes onto the snap, or every note when none are selected"));
    quantizeChip.onClick = [this]
    {
        lastEditor = Editor::pianoRoll;
        pianoRoll.edit (EditCommand::quantize);
    };
    editorPanel.addToHeader (quantizeChip, 104, true, 30, true);     // steps aside first: it's in Edit too

    pianoEditChip.setTooltip (shortcutText ("Edit the selected notes: copy (Ctrl+C), paste (Ctrl+V), duplicate (Ctrl+B), "
                                            "quantize (Ctrl+Q), up and down with the arrow keys"));
    pianoEditChip.onClick = [this]
    {
        lastEditor = Editor::pianoRoll;
        pianoRoll.createEditMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pianoEditChip));
    };
    editorPanel.addToHeader (pianoEditChip, 84, true, 30);

    pianoTools.addTool ("Select", icons::pointer(), shortcutText ("Select: drag a box over notes to select them (Ctrl+drag does with any tool); "
                                                                  "move and stretch them"));
    pianoTools.addTool ("Draw", icons::pencil(), "Draw: click to add a note, drag to place it");
    pianoTools.addTool ("Erase", icons::eraser(), "Erase: click or drag over notes to delete them");
    pianoTools.setSelected (1);
    pianoTools.onChange = [this] (int index)
    {
        pianoRoll.setTool (index == 0 ? PianoRoll::Tool::select : index == 1 ? PianoRoll::Tool::draw : PianoRoll::Tool::erase);
    };
    editorPanel.addToHeader (pianoTools, pianoTools.getIdealWidth(), true, 32);

    pianoRoll.onTitleChanged = [this] { updateEditorHeader(); };
    pianoRoll.onActivated = [this] { lastEditor = Editor::pianoRoll; };
    pianoRoll.onMessage = [this] (const juce::String& message) { showToast (message); };

    editorPanel.setContent (&rack);
    updateEditorHeader();

    splitter.getStart = [this] { return editorPanel.getHeight(); };
    splitter.onDrag = [this] (int h)
    {
        editorHeight = h;
        resized();
    };

    toolbar.onMenu = [this] { showMainMenu(); };
    toolbar.onExport = [this] { exportAudio(); };
    toolbar.onPlay = [this] { playOrStop(); };
    toolbar.onKey = [this] (const juce::KeyPress& k) { return keyPressed (k); };
    toolbar.onRecord = [this] { toggleRecordArm(); };
    toolbar.onRecordMenu = [this] { createRecordMenu().showMenuAsync ({}); };
    toolbar.onKeyboard = [this] { setKeyboardOn (! keyboard.isEnabled()); };

    // The PC keyboard plays the selected channel (Ableton's layout); a note keeps to the
    // channel it started on, so changing the selection mid-note can't leave it hanging
    keyboard.onNoteOn = [this] (int key, int velocity)
    {
        auto c = song.findChannel (song.selectedChannelId);

        if (c == nullptr || c->type == InstrumentType::audio)
        {
            if (juce::Time::getMillisecondCounter() - lastKeyboardHint > 10000)
            {
                lastKeyboardHint = juce::Time::getMillisecondCounter();
                showToast ("Select an instrument channel to play it from the keyboard (Shift+M switches the keyboard off).");
            }

            return;
        }

        liveNotes[key] = c->id;
        audio.liveNoteOn (c->id, key, velocity);
    };

    keyboard.onNoteOff = [this] (int key)
    {
        if (auto it = liveNotes.find (key); it != liveNotes.end())
        {
            audio.liveNoteOff (it->second, key);
            liveNotes.erase (it);
        }
    };

    keyboard.onChanged = [this]
    {
        toolbar.setKeyboard (keyboard.isEnabled(), keyboard.describe());
    };

    keyboard.onMoved = [this]
    {
        toolbar.setKeyboard (keyboard.isEnabled(), keyboard.describe());
        showToast ("Computer keyboard: " + keyboard.describe());
    };

    keyboard.setEnabled (audio.getOptions().computerKeyboard);
    toolbar.setKeyboard (keyboard.isEnabled(), keyboard.describe());
    keyboardPoll.startTimer (8);

    struct FocusReturner : public juce::MouseListener
    {
        explicit FocusReturner (MainComponent& m) : main (m) {}

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* typing = dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent());
            auto* clicked = e.eventComponent;

            if (typing == nullptr || clicked == nullptr || clicked == typing || typing->isParentOf (clicked)
                 || dynamic_cast<juce::TextEditor*> (clicked) != nullptr)
                return;

            if (clicked == &main || main.isParentOf (clicked))
                main.grabKeyboardFocus();
        }

        MainComponent& main;
    };

    focusReturner = std::make_unique<FocusReturner> (*this);
    juce::Desktop::getInstance().addGlobalMouseListener (focusReturner.get());
    toolbar.onAudioSettings = [this] { showAudioSettings(); };

    sidebar.onNewPattern = [this] { newPattern(); };
    sidebar.onSoundDragStarted = [this]
    {
        // Somewhere to drop it
        if (! isPanelVisible (Panel::rack))
            showEditorTab (Panel::rack);
    };
    sidebar.onPatternDragStarted = [this]
    {
        // Arrange, to drop it on (it's only hidden when the editor has the whole height)
        if (! isPanelVisible (Panel::playlist))
            setPanelVisible (Panel::playlist, true);
    };
    sidebar.onScanPlugins = [this] { scanPlugins(); };
    sidebar.onScanInstruments = [this] { findNewPlugins (true); };
    sidebar.onAddKitSound = [this] (const juce::String& name) { rack.addKitChannel (name); showEditorTab (Panel::rack); };
    sidebar.onAddDrumKit = [this] (const juce::String& id) { openDrummer (rack.addDrumKitChannel (id)); drummerBar.focus(); };
    sidebar.onAddSynth = [this] (const juce::String& preset) { openSynth (rack.addSynthChannel (preset)); };
    sidebar.onAddPlugin = [this] (const juce::PluginDescription& d) { rack.addPluginChannel (d); showEditorTab (Panel::rack); };
    sidebar.onAddSample = [this] (const juce::String& path) { rack.addSamplerChannel (path); showEditorTab (Panel::rack); };
    sidebar.onAddEffect = [this] (const juce::PluginDescription& d)
    {
        // On the selected channel (the master if there isn't one); the Inspector shows it
        rack.addEffect (song.selectedChannelId, d);
    };

    rack.onOpenPianoRoll = [this] (int id) { openPianoRoll (id); };
    rack.onOpenPluginEditor = [this] (int id) { openPluginEditor (id); };
    rack.onOpenEffectEditor = [this] (int id) { openEffectEditor (id); };
    rack.onOpenChannel = [this] (int id) { openChannel (id); };
    rack.onOpenDrummer = [this] (int id) { openDrummer (id); };
    rack.onScanPlugins = [this] { findNewPlugins (true); };       // "Find new instruments and effects"

    playlist.onOpenPattern = [this] (int id) { openPattern (id); };
    playlist.onOpenChannel = [this] (int id) { openChannel (id); };
    playlist.onAddSound = [this] (const juce::String& sound) { return rack.addSound (sound); };
    playlist.nameOfSound = [this] (const juce::String& sound) { return rack.soundName (sound); };
    playlist.onShowAddMenu = [this] (int at, juce::Rectangle<int> area) { showAddTrackMenu (at, area); };
    playlist.onAddEffect = [this] (int channelId, const juce::String& sound)
    {
        if (auto desc = audio.findEffectType (sound.fromFirstOccurrenceOf ("effect:", false, false)))
            rack.addEffect (channelId, *desc);
    };
    playlist.onReplaceSound = [this] (int channelId, const juce::String& sound)
    {
        auto c = song.findChannel (channelId);
        const auto before = c != nullptr ? c->name : juce::String();

        if (! rack.replaceInstrument (channelId, sound))
            return false;

        showToast (before + " now plays " + rack.soundName (sound) + ", its notes and effects as they were. " + shortcutText ("Ctrl+Z") + " puts it back");
        return true;
    };

    for (auto* strip : { &inspector.getChannelStrip(), &inspector.getMasterStrip() })
    {
        strip->onOpenEffect = [this] (int id) { openEffectEditor (id); };
        strip->onOpenInstrument = [this] (int id) { openInstrument (id); };
        strip->onOpenPianoRoll = [this] (int id) { openPianoRoll (id); };
        strip->onShowSteps = [this] (int id)
        {
            selectChannel (id);
            showEditorTab (Panel::rack);
        };
    }

    mixer.onOpenEffect = [this] (int id) { openEffectEditor (id); };
    mixer.onOpenInstrument = [this] (int id) { openInstrument (id); };
    mixer.onOpenPianoRoll = [this] (int id) { openPianoRoll (id); };
    mixer.onShowSteps = [this] (int id)
    {
        selectChannel (id);
        showEditorTab (Panel::rack);
    };
    mixer.onSelect = [this] (int id) { selectChannel (id); };

    // The AI drummer, bassist and guitarist: typed into the bar, their versions in their tabs
    drummerBar.onSend = [this] (const juce::String& words)
    {
        if (barMode == DrummerBar::Mode::bass)
        {
            showEditorTab (Panel::bass);
            bassPanel.ask (words);
        }
        else if (barMode == DrummerBar::Mode::guitar)
        {
            showEditorTab (Panel::guitar);
            guitarPanel.ask (words);
        }
        else if (barMode == DrummerBar::Mode::keys)
        {
            showEditorTab (Panel::keys);
            keysPanel.ask (words);
        }
        else if (barMode == DrummerBar::Mode::synth)
        {
            showEditorTab (Panel::synthPlayer);
            synthPlayerPanel.ask (words);
        }
        else if (barMode == DrummerBar::Mode::master)
        {
            showEditorTab (Panel::masterAi);
            masterEngineer.ask (words);
        }
        else if (barMode == DrummerBar::Mode::song)
        {
            showEditorTab (Panel::song);
            makeSong (words);
        }
        else
        {
            showEditorTab (Panel::drummer);
            drummerPanel.ask (words);
        }
    };
    drummerBar.onOpen = [this]
    {
        showEditorTab (barMode == DrummerBar::Mode::bass ? Panel::bass : barMode == DrummerBar::Mode::guitar ? Panel::guitar
                       : barMode == DrummerBar::Mode::keys ? Panel::keys : barMode == DrummerBar::Mode::synth ? Panel::synthPlayer
                       : barMode == DrummerBar::Mode::master ? Panel::masterAi : barMode == DrummerBar::Mode::song ? Panel::song : Panel::drummer);
    };
    drummerBar.onTargetClicked = [this]
    {
        if (barMode == DrummerBar::Mode::bass)          showBassTargetMenu();
        else if (barMode == DrummerBar::Mode::guitar)   showGuitarTargetMenu();
        else if (barMode == DrummerBar::Mode::keys)     showKeysTargetMenu();
        else if (barMode == DrummerBar::Mode::synth)    showSynthPlayerTargetMenu();
        else if (barMode == DrummerBar::Mode::master)   showMasterTargetMenu();
        else if (barMode == DrummerBar::Mode::song)     showEditorTab (Panel::song);
        else                                            showDrummerTargetMenu();
    };
    drummerBar.onSwitchMode = [this] { showAgentMenu(); };
    drummerBar.onDone = [this] { grabKeyboardFocus(); };
    drummerBar.setNameWidth (0);

    drummerPanel.onNeedChannel = [this] { return makeDrummerChannel(); };
    drummerPanel.onUseIdea = [this] (const juce::String& idea)
    {
        drummerBar.setText (idea);
        drummerBar.focus();
    };
    drummerPanel.onStateChanged = [this] { updateDrummerBar(); };
    drummerPanel.onAnswered = [this] { drummerBar.setText ({}); };

    bassPanel.onNeedChannel = [this] { return makeBassChannel(); };
    bassPanel.onUseIdea = [this] (const juce::String& idea)
    {
        drummerBar.setText (idea);
        drummerBar.focus();
    };
    bassPanel.onStateChanged = [this] { updateDrummerBar(); };
    bassPanel.onAnswered = [this] { drummerBar.setText ({}); };
    bassPanel.onShowPattern = [this] (int channel, int patternId)
    {
        song.currentPatternId = patternId;
        song.notify (Song::Change::currentPattern);
        openPianoRoll (channel);
    };

    guitarPanel.onNeedChannel = [this] { return makeGuitarChannel(); };
    guitarPanel.onUseIdea = [this] (const juce::String& idea)
    {
        drummerBar.setText (idea);
        drummerBar.focus();
    };
    guitarPanel.onStateChanged = [this] { updateDrummerBar(); };
    guitarPanel.onAnswered = [this] { drummerBar.setText ({}); };
    guitarPanel.onShowPattern = [this] (int channel, int patternId)
    {
        song.currentPatternId = patternId;
        song.notify (Song::Change::currentPattern);
        openPianoRoll (channel);
    };

    keysPanel.onNeedChannel = [this] (const keys::offline::Sound& sound) { return makeKeysChannel (sound); };
    keysPanel.onUseIdea = [this] (const juce::String& idea)
    {
        drummerBar.setText (idea);
        drummerBar.focus();
    };
    keysPanel.onStateChanged = [this] { updateDrummerBar(); };
    keysPanel.onAnswered = [this] { drummerBar.setText ({}); };
    keysPanel.onShowPattern = [this] (int channel, int patternId)
    {
        song.currentPatternId = patternId;
        song.notify (Song::Change::currentPattern);
        openPianoRoll (channel);
    };

    synthPlayerPanel.onNeedChannel = [this] (const juce::String& sound, const juce::String& role) { return makeSynthPlayerChannel (sound, role); };
    synthPlayerPanel.onUseIdea = [this] (const juce::String& idea)
    {
        drummerBar.setText (idea);
        drummerBar.focus();
    };
    synthPlayerPanel.onStateChanged = [this] { updateDrummerBar(); };
    synthPlayerPanel.onAnswered = [this] { drummerBar.setText ({}); };
    synthPlayerPanel.onShowPattern = [this] (int channel, int patternId)
    {
        song.currentPatternId = patternId;
        song.notify (Song::Change::currentPattern);
        openPianoRoll (channel);
    };

    masterEngineer.onUseIdea = [this] (const juce::String& idea)
    {
        drummerBar.setText (idea);
        drummerBar.focus();
    };
    masterEngineer.onStateChanged = [this] { updateDrummerBar(); };
    masterEngineer.onAnswered = [this] { drummerBar.setText ({}); };
    masterEngineer.onDone = [this] (bool mastered)
    {
        if (songPanel.isWorking())
            finishSong (mastered);
    };
    masterPanel.onOpenAiMaster = [this] { showEditorTab (Panel::masterAi); drummerBar.focus(); };

    // The song agent
    songPanel.onMake = [this] (const juce::String& words) { makeSong (words); };
    songPanel.onUseIdea = [this] (const juce::String& idea)
    {
        drummerBar.setText (idea);
        drummerBar.focus();
    };
    songPanel.onOpenAgent = [this] (const juce::String& agent)
    {
        showEditorTab (agent == "drums" ? Panel::drummer : agent == "bass" ? Panel::bass : agent == "guitar" ? Panel::guitar
                     : agent == "keys" ? Panel::keys : agent == "synth" ? Panel::synthPlayer : Panel::masterAi);
    };
    songPanel.onPlay = [this]
    {
        audio.stop();

        if (song.mode != PlayMode::song)
        {
            song.mode = PlayMode::song;
            song.notify (Song::Change::transport);
        }

        audio.setPlayheadTicks (0.0);
        playOrStop();
    };
    songPanel.currentStatus = [this] { return masterEngineer.isWorking() ? masterEngineer.getStatus() : juce::String(); };

    // The AI tab: every agent in one list
    aiPanel.setAgents ({
        { "Song",   "a whole song, one prompt",  "",              &songPanel },
        { "Drums",  "the beat",                  "F8",            &drummerPanel },
        { "Bass",   "the low end",               "Shift+F8",      &bassPanel },
        { "Guitar", "riffs and chords, as tab",  shortcutText ("Ctrl+F8"),       &guitarPanel },
        { "Keys",   "piano, Rhodes, organ",      shortcutText ("Alt+F8"),        &keysPanel },
        { "Synth",  "pads, leads, arps",         shortcutText ("Ctrl+Shift+F8"), &synthPlayerPanel },
        { "Master", "loud and ready to put out", "Shift+F12",     &masterEngineer },
    });
    aiPanel.onPick = [this] (int index) { showEditorTab (agentPanel (index)); };

    // The prompt lives in the AI tab, over whichever agent is picked (it talks to that one)
    aiPanel.setPrompt (&drummerBar, DrummerBar::height);

    // A synth channel made from the Synth tab starts on the default sound, named after it
    synthPanel.onNeedChannel = [this] { return rack.addSynthChannel (nova::defaultPreset()); };
    rack.onOpenSynth = [this] (int id) { openSynth (id); };

    // No drum kits yet: fetched from the drummer's tab, wherever it's asked for
    auto getKits = [this]
    {
        showEditorTab (Panel::drummer);
        drummerPanel.getKits();
    };
    sidebar.onGetKits = getKits;
    rack.onGetKits = getKits;
    sidebar.onImportKit = [this] { importDrumKit(); };
    sidebar.onGetInstruments = [this] { getInstruments(); };
    rack.onGetInstruments = [this] { getInstruments(); };

    // An instrument: a channel playing it, in the piano roll to write for it
    sidebar.onAddInstrument = [this] (const juce::String& path) { openPianoRoll (rack.addInstrumentChannel (path)); };
    rack.onImportKit = [this] { importDrumKit(); };
    drummerPanel.onKitsArrived = [this] { sidebar.kitsChanged(); };

    // A section chip: that pattern in the piano roll, on the drums
    drummerPanel.onShowPattern = [this] (int channel, int patternId)
    {
        song.currentPatternId = patternId;
        song.notify (Song::Change::currentPattern);
        openPianoRoll (channel);
    };

    followSelectionWithDrummer (Song::Change::everything);
    followSelectionWithBass (Song::Change::everything);
    followSelectionWithGuitar (Song::Change::everything);
    followSelectionWithKeys (Song::Change::everything);
    followSelectionWithSynthPlayer (Song::Change::everything);

    // Plugins installed since the last time: scanned now, without asking (the studio only)
    if (audio.isStudio())
        findNewPlugins (false);

    updateDrummerBar();

    song.addListener (this);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    automation::showMenu = nullptr;
    automation::showMenuFor = nullptr;
    audio.onMidiInputAdded = nullptr;
    stopTimer();
    keyboardPoll.stopTimer();
    juce::Desktop::getInstance().removeGlobalMouseListener (focusReturner.get());
    instrumentFetcher.reset();
    updateWorker.reset();
    pluginWindows.clear();
    effectWindows.clear();

    // It holds on to the engine, which goes after us
    if (audioSettingsWindow != nullptr)
        delete audioSettingsWindow.getComponent();

    song.removeListener (this);
}

//==============================================================================
MainComponent::Splitter::Splitter()
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void MainComponent::Splitter::paint (juce::Graphics& g)
{
    if (isMouseOverOrDragging())
    {
        g.setColour (raised.brighter (0.2f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().withSizeKeepingCentre (56.0f, 3.0f), 1.5f);
    }
}

void MainComponent::Splitter::mouseDown (const juce::MouseEvent&)
{
    start = getStart != nullptr ? getStart() : 0;
}

void MainComponent::Splitter::mouseDrag (const juce::MouseEvent& e)
{
    if (onDrag != nullptr)
        onDrag (start - e.getDistanceFromDragStartY());
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    startScreen.setBounds (r);
    const int toastWidth = std::min (640, getWidth() - 48);
    toast.setBounds (getWidth() - 24 - toastWidth, getHeight() - 24 - 60, toastWidth, 60);
    toolbar.setBounds (r.removeFromTop (Toolbar::height));
    r = r.withTrimmedLeft (gap).withTrimmedRight (gap).withTrimmedBottom (gap);
    r.removeFromTop (gap / 2);

    sidebarPanel.setBounds (r.removeFromLeft (sidebarWidth));
    r.removeFromLeft (gap);

    // The Inspector down the right, if there's room or it was asked for
    // The mixer shows every strip itself, so the Inspector steps aside for it unless asked back
    const bool mixerShown = editorShown && editorTab == Panel::mixer;
    const bool showInspector = mixerShown ? inspectorInMixer : inspectorChoice.value_or (getWidth() >= inspectorMinWindowWidth);
    inspectorPanel.setVisible (showInspector);
    inspectorButton.setToggleState (showInspector, juce::dontSendNotification);

    if (showInspector)
    {
        inspectorPanel.setBounds (r.removeFromRight (Inspector::width));
        r.removeFromRight (gap);
    }

    // A narrow editor gives its header chips less room
    const int chipWidth = r.getWidth() < 1000 ? 118 : 156;
    editorPanel.setHeaderItemWidth (channelChip, chipWidth);
    editorPanel.setHeaderItemWidth (patternChip, chipWidth);

    // The editor starts tall enough for the rack's channels, within reason
    if (editorHeight < 0 && r.getHeight() >= 400)
        editorHeight = juce::jlimit (260, r.getHeight() / 2, rack.getIdealHeight() + DockPanel::headerHeight + 1);

    const bool showArrange = ! (editorShown && editorMaximised);
    arrangePanel.setVisible (showArrange);
    editorPanel.setVisible (editorShown);
    splitter.setVisible (editorShown && showArrange);

    if (! editorShown)
    {
        arrangePanel.setBounds (r);
        return;
    }

    if (! showArrange)
    {
        editorPanel.setBounds (r);
        return;
    }

    const int wanted = editorHeight < 0 ? r.getHeight() / 2 : editorHeight;
    const int h = juce::jlimit (std::min (200, r.getHeight() / 3), std::max (200, r.getHeight() - 200), wanted);
    editorPanel.setBounds (r.removeFromBottom (h));
    splitter.setBounds (r.removeFromBottom (gap));
    arrangePanel.setBounds (r);
}

void MainComponent::updateArrangeTitle()
{
    const int ticks = song.songLengthInTicks();
    const int bars = ticks > 0 ? song.barAt (ticks - 1) + 1 : 0;
    const int seconds = juce::roundToInt (song.secondsAt (ticks));

    arrangePanel.setTitle ("Arrange", bars == 0 ? juce::String ("Empty")
                                                : juce::String (bars) + (bars == 1 ? " bar" : " bars")
                                                    + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "))
                                                    + juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2));
}

void MainComponent::updateEditorHeader()
{
    auto& pattern = song.currentPattern();
    patternChip.setLabel (pattern.name);
    patternChip.setSwatch (pattern.colour);
    swing.bar.setValue (song.swing);
    swing.repaint();

    auto edited = song.findChannel (pianoRoll.getChannelId());
    channelChip.setLabel (edited != nullptr ? edited->name : juce::String ("No instrument"));
    channelChip.setSwatch (edited != nullptr ? edited->colour : juce::Colours::transparentBlack);

    // Swing belongs to Steps; the channel, tools and snap to the piano roll; the mixer has
    // no pattern
    const bool steps = editorTab == Panel::rack, piano = editorTab == Panel::pianoRoll;
    swing.setVisible (steps);
    patternChip.setVisible (steps || piano);

    for (auto* c : std::initializer_list<juce::Component*> { &channelChip, &inLabel, &pianoSnapChip, &pianoTools, &pianoEditChip, &quantizeChip })
        c->setVisible (piano);

    masterPages.setVisible (editorTab == Panel::master);
    editorPanel.resized();
}

int MainComponent::snapTicks (const juce::String& name) const
{
    // A beat and a bar follow the pattern's time signature (an eighth and 7 of them in 7/8)
    const auto& pattern = const_cast<Song&> (song).currentPattern();
    if (name == "1/16")   return ticksPerStep;
    if (name == "1/32")   return ticksPerStep / 2;
    if (name == "Beat")   return song.beatTicksOf (pattern);
    if (name == "Bar")    return song.barTicksOf (pattern);
    return 1;
}

void MainComponent::applySnaps()
{
    pianoRoll.setSnap (snapTicks (pianoSnap));
    audio.setRecordSnap (snapTicks (pianoSnap));

    // Arrange's bars and beats are as long as they are where they're used
    playlist.setSnap (arrangeSnap == "Bar" ? Song::barGrid : arrangeSnap == "Beat" ? Song::beatGrid : snapTicks (arrangeSnap));
    pianoSnapChip.setLabel (pianoSnap);
    snapChip.setLabel (arrangeSnap);
}

void MainComponent::showPianoSnapMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Snap to");

    for (auto name : { "1/16", "1/32", "Beat", "Bar", "Off" })
        menu.addItem (name, true, pianoSnap == name, [this, name]
        {
            pianoSnap = name;
            applySnaps();
        });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pianoSnapChip));
}

void MainComponent::showPatternChipMenu()
{
    juce::PopupMenu menu;

    for (auto& p : song.patterns)
    {
        juce::PopupMenu::Item item (p.name);
        item.colour = p.colour;
        item.isTicked = p.id == song.currentPatternId;
        item.action = [this, id = p.id]
        {
            song.currentPatternId = id;
            song.notify (Song::Change::currentPattern);
        };
        menu.addItem (item);
    }

    menu.addSeparator();
    menu.addItem ("New pattern  (F4)", [this] { newPattern(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternChip));
}

void MainComponent::showSnapMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Snap to");

    for (auto name : { "Bar", "Beat", "1/16", "Off" })
        menu.addItem (name, true, arrangeSnap == name, [this, name]
        {
            arrangeSnap = name;
            applySnaps();
        });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&snapChip));
}

//==============================================================================
bool MainComponent::isPanelVisible (Panel p) const
{
    if (p == Panel::playlist)
        return ! (editorShown && editorMaximised);

    return editorShown && editorTab == p;
}

void MainComponent::showEditorTab (Panel tab)
{
    // The mixer needs the height: it takes the whole of it, and gives it back on the way out
    if (tab == Panel::mixer && ! editorMaximised)
    {
        editorMaximised = maximisedForMixer = true;
    }
    else if (tab != Panel::mixer && maximisedForMixer)
    {
        editorMaximised = maximisedForMixer = false;
    }

    editorTab = tab;
    editorShown = true;
    editorTabs.setSelected (tabIndex (tab));
    editorPanel.setContent (tab == Panel::rack ? static_cast<juce::Component*> (&rack)
                                               : tab == Panel::pianoRoll ? static_cast<juce::Component*> (&pianoRoll)
                                               : isAgent (tab) ? static_cast<juce::Component*> (&aiPanel)
                                               : tab == Panel::synth ? static_cast<juce::Component*> (&synthPanel)
                                               : tab == Panel::master ? static_cast<juce::Component*> (&masterPanel) : &mixer);

    // An agent: the AI tab, on it; and the prompt bar talks to it
    if (isAgent (tab))
    {
        aiPanel.show (agentIndex (tab));
        lastAgent = tab;
        setBarMode (tab == Panel::bass ? DrummerBar::Mode::bass : tab == Panel::guitar ? DrummerBar::Mode::guitar
                  : tab == Panel::keys ? DrummerBar::Mode::keys : tab == Panel::synthPlayer ? DrummerBar::Mode::synth
                  : tab == Panel::masterAi ? DrummerBar::Mode::master : tab == Panel::song ? DrummerBar::Mode::song : DrummerBar::Mode::drummer);
    }

    updateEditorHeader();
    resized();

    if (tab == Panel::pianoRoll)
        pianoRoll.centreOnNotes();

    grabKeyboardFocus();
}

void MainComponent::setPanelVisible (Panel p, bool shouldShow)
{
    if (p != Panel::playlist && shouldShow)
    {
        showEditorTab (p);
        return;
    }

    if (p == Panel::playlist)
    {
        editorMaximised = ! shouldShow && editorShown;
    }
    else if (editorTab == p)
    {
        editorShown = false;
        editorMaximised = maximisedForMixer = false;
    }

    resized();
    grabKeyboardFocus();
}

void MainComponent::togglePanel (Panel p)
{
    if (p != Panel::playlist)
    {
        setPanelVisible (p, ! isPanelVisible (p));
        return;
    }

    // Arrange on its own, or back to sharing the height with the editor
    if (editorShown && ! editorMaximised)
    {
        editorShown = false;
    }
    else
    {
        editorShown = true;
        editorMaximised = false;
    }

    resized();
    grabKeyboardFocus();
}

void MainComponent::toggleInspector()
{
    if (editorShown && editorTab == Panel::mixer)
        inspectorInMixer = ! inspectorPanel.isVisible();
    else
        inspectorChoice = ! inspectorPanel.isVisible();
    resized();
    grabKeyboardFocus();
}

void MainComponent::selectChannel (int channelId)
{
    if (song.selectedChannelId != channelId)
    {
        song.selectedChannelId = channelId;
        song.notify (Song::Change::selection);
    }
}

void MainComponent::openPianoRoll (int channelId)
{
    selectChannel (channelId);
    setPanelVisible (Panel::pianoRoll, true);
}

static bool playsIn (const Pattern& pattern, int channelId)
{
    auto notes = pattern.findNotes (channelId);
    return notes != nullptr && ! notes->empty();
}

bool MainComponent::playsMelody (const Pattern& pattern, const Channel& c) const
{
    if (c.type == InstrumentType::audio)
        return false;

    if (playsIn (pattern, c.id))
        return ! steps::isStepSequence (*pattern.findNotes (c.id), c.rootKey, song.barTicksOf (pattern));

    // Nothing written yet: synths and plugins get the piano roll, drums Steps
    return c.type != InstrumentType::sampler;
}

void MainComponent::openPattern (int patternId)
{
    auto pattern = song.findPattern (patternId);

    if (pattern == nullptr)
        return;

    if (song.currentPatternId != patternId)
    {
        song.currentPatternId = patternId;
        song.notify (Song::Change::currentPattern);
    }

    // The piano roll on the selected channel if it plays a melody here, else the first that does
    const Channel* melody = nullptr;
    const Channel* firstPlaying = nullptr;

    for (auto& c : song.channels)
    {
        if (! playsIn (*pattern, c.id))
            continue;

        if (firstPlaying == nullptr)
            firstPlaying = &c;

        if (playsMelody (*pattern, c) && (melody == nullptr || c.id == song.selectedChannelId))
            melody = &c;
    }

    if (melody != nullptr)
    {
        openPianoRoll (melody->id);
        return;
    }

    // All drums: Steps, with one of them selected
    if (firstPlaying != nullptr && ! playsIn (*pattern, song.selectedChannelId))
        selectChannel (firstPlaying->id);

    showEditorTab (Panel::rack);
}

void MainComponent::openChannel (int channelId)
{
    auto c = song.findChannel (channelId);

    if (c == nullptr)
        return;

    selectChannel (channelId);

    // Its notes: in the current pattern, else in the first pattern it plays in
    if (! playsIn (song.currentPattern(), channelId))
        for (auto& p : song.patterns)
            if (playsIn (p, channelId))
            {
                song.currentPatternId = p.id;
                song.notify (Song::Change::currentPattern);
                break;
            }

    // An audio track's strip in the Inspector is the whole story; a drum kit's instrument is
    // its drummer
    if (c->type == InstrumentType::kit)
        openDrummer (channelId);
    else if (! c->bass.turns.empty())
        openBass (channelId);
    else if (! c->guitar.turns.empty())
        openGuitar (channelId);
    else if (! c->keys.turns.empty())
        openKeys (channelId);
    else if (! c->synthPlayer.turns.empty())
        openSynthPlayer (channelId);
    else if (c->type == InstrumentType::plugin)
        openPluginEditor (channelId);
    else if (playsMelody (song.currentPattern(), *c))
        openPianoRoll (channelId);
    else if (c->type == InstrumentType::synth)
        openSynth (channelId);
    else if (c->type != InstrumentType::audio)
        showEditorTab (Panel::rack);
}

void MainComponent::openInstrument (int channelId)
{
    auto c = song.findChannel (channelId);

    if (c == nullptr)
        return;

    if (c->type == InstrumentType::kit)
        openDrummer (channelId);
    else if (c->type == InstrumentType::plugin)
        openPluginEditor (channelId);
    else if (c->type == InstrumentType::synth)
        openSynth (channelId);
    else
        rack.showChannelMenu (channelId);
}

void MainComponent::openDrummer (int channelId)
{
    if (song.findChannel (channelId) == nullptr)
        return;

    drummerPanel.setChannel (channelId);
    showEditorTab (Panel::drummer);
}

void MainComponent::openBass (int channelId)
{
    if (song.findChannel (channelId) == nullptr)
        return;

    bassNew = false;
    bassPanel.setChannel (channelId);
    showEditorTab (Panel::bass);
}

MainComponent::Panel MainComponent::tabPanel (int index)
{
    return index == 0 ? Panel::rack : index == 1 ? Panel::pianoRoll : index == 2 ? Panel::mixer : index == 3 ? Panel::song
         : index == 4 ? Panel::synth : Panel::master;
}

int MainComponent::tabIndex (Panel p)
{
    return p == Panel::rack ? 0 : p == Panel::pianoRoll ? 1 : p == Panel::mixer ? 2 : isAgent (p) ? 3 : p == Panel::synth ? 4 : 5;
}

int MainComponent::agentIndex (Panel p)
{
    return p == Panel::song ? 0 : p == Panel::drummer ? 1 : p == Panel::bass ? 2 : p == Panel::guitar ? 3 : p == Panel::keys ? 4
         : p == Panel::synthPlayer ? 5 : p == Panel::masterAi ? 6 : -1;
}

MainComponent::Panel MainComponent::agentPanel (int index)
{
    static const Panel panels[] { Panel::song, Panel::drummer, Panel::bass, Panel::guitar, Panel::keys, Panel::synthPlayer, Panel::masterAi };
    return panels[juce::jlimit (0, 6, index)];
}

int MainComponent::makeBassChannel()
{
    // A synth channel made for the bassist, called Bass, which chooses the sound for the style
    const int id = rack.addSynthChannel ("Finger Bass");

    if (auto c = song.findChannel (id))
    {
        c->name = "Bass";
        c->bass.pickSound = true;
        song.notify (Song::Change::channels);
    }

    bassNew = false;
    return id;
}

void MainComponent::followSelectionWithBass (Song::Change change)
{
    if (change == Song::Change::everything)
    {
        bassNew = false;
        bassPanel.setChannel (0);
    }

    // A channel the bassist has written for, or a synth playing a bass sound, becomes the one
    // when it's selected
    auto isBass = [] (const Channel& c)
    {
        if (! c.bass.turns.empty())
            return true;

        if (c.type != InstrumentType::synth)
            return false;

        auto factory = nova::findFactory (c.synthPreset);
        return (factory != nullptr && factory->category == "Bass") || c.name.containsIgnoreCase ("bass") || c.synthPreset.containsIgnoreCase ("bass");
    };

    if (change == Song::Change::selection || change == Song::Change::everything)
        if (auto c = song.findChannel (song.selectedChannelId); c != nullptr && isBass (*c))
        {
            bassNew = false;
            bassPanel.setChannel (c->id);
            return;
        }

    if (bassPanel.channel() != nullptr || bassNew)
        return;

    int best = 0;

    for (auto& c : song.channels)
    {
        if (! c.bass.turns.empty())
        {
            best = c.id;
            break;
        }

        if (isBass (c) && best == 0)
            best = c.id;
    }

    bassPanel.setChannel (best);
}

void MainComponent::setBarMode (DrummerBar::Mode mode)
{
    if (barMode == mode)
        return;

    barMode = mode;
    drummerBar.setMode (mode);
    updateDrummerBar();
}

void MainComponent::showAgentMenu()
{
    // The bar's name, right-clicked: which of the band to talk to
    juce::PopupMenu menu;
    menu.addSectionHeader ("Talk to");
    menu.addItem ("AI Song  (a whole song)", true, barMode == DrummerBar::Mode::song, [this] { showEditorTab (Panel::song); drummerBar.focus(); });
    menu.addItem ("AI Drummer  (F8)", true, barMode == DrummerBar::Mode::drummer, [this] { showEditorTab (Panel::drummer); drummerBar.focus(); });
    menu.addItem ("AI Bass  (Shift+F8)", true, barMode == DrummerBar::Mode::bass, [this] { showEditorTab (Panel::bass); drummerBar.focus(); });
    menu.addItem (shortcutText ("AI Guitar  (Ctrl+F8)"), true, barMode == DrummerBar::Mode::guitar, [this] { showEditorTab (Panel::guitar); drummerBar.focus(); });
    menu.addItem (shortcutText ("AI Keys  (Alt+F8)"), true, barMode == DrummerBar::Mode::keys, [this] { showEditorTab (Panel::keys); drummerBar.focus(); });
    menu.addItem (shortcutText ("AI Synth  (Ctrl+Shift+F8)"), true, barMode == DrummerBar::Mode::synth, [this] { showEditorTab (Panel::synthPlayer); drummerBar.focus(); });
    menu.addItem ("AI Master  (Shift+F12)", true, barMode == DrummerBar::Mode::master, [this] { showEditorTab (Panel::masterAi); drummerBar.focus(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&drummerBar.getNameButton()));
}

void MainComponent::openKeys (int channelId)
{
    if (song.findChannel (channelId) == nullptr)
        return;

    keysNew = false;
    keysPanel.setChannel (channelId);
    showEditorTab (Panel::keys);
}

int MainComponent::makeKeysChannel (const keys::offline::Sound& sound)
{
    // On the sound the style wants: the sampled piano (when it's here), or a Nova keys sound
    const int id = sound.sampled.isNotEmpty() ? rack.addInstrumentChannel (sound.sampled) : rack.addSynthChannel (sound.name);

    if (auto c = song.findChannel (id))
    {
        c->name = "Keys";
        c->keys.pickSound = true;
        song.notify (Song::Change::channels);
    }

    keysNew = false;
    return id;
}

void MainComponent::followSelectionWithKeys (Song::Change change)
{
    if (change == Song::Change::everything)
    {
        keysNew = false;
        keysPanel.setChannel (0);
    }

    // A keys channel selected (one the keys player wrote for, or playing a keys sound) becomes the one
    auto isKeys = [] (const Channel& c)
    {
        if (! c.keys.turns.empty())
            return true;

        if (c.type == InstrumentType::synth)
            if (auto f = nova::findFactory (c.synthPreset))
                return f->category == "Keys";

        return c.type == InstrumentType::instrument && ! isGuitar (c) && instruments::displayName (c.instrument).containsIgnoreCase ("piano");
    };

    if (change == Song::Change::selection || change == Song::Change::everything)
        if (auto c = song.findChannel (song.selectedChannelId); c != nullptr && isKeys (*c))
        {
            keysNew = false;
            keysPanel.setChannel (c->id);
            return;
        }

    if (keysPanel.channel() != nullptr || keysNew)
        return;

    int best = 0;

    for (auto& c : song.channels)
        if (! c.keys.turns.empty())
        {
            best = c.id;
            break;
        }

    keysPanel.setChannel (best);
}

void MainComponent::showKeysTargetMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("The keys player writes for");

    for (auto& c : song.channels)
    {
        if (c.keys.turns.empty() && c.type != InstrumentType::synth && c.type != InstrumentType::instrument)
            continue;

        if (isGuitar (c))
            continue;

        juce::PopupMenu::Item item (c.name);
        item.colour = c.colour;
        item.isTicked = c.id == keysPanel.getChannelId();
        item.action = [this, id = c.id] { openKeys (id); };
        menu.addItem (item);
    }

    menu.addSeparator();
    menu.addItem ("New keys (the keys player picks the sound)", true, keysPanel.channel() == nullptr, [this]
    {
        keysNew = true;
        keysPanel.setChannel (0);
        showEditorTab (Panel::keys);
        drummerBar.focus();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&drummerBar.getTargetChip()));
}

void MainComponent::showMasterPage (int page)
{
    showEditorTab (Panel::master);
    masterPanel.setPage (page);
    masterPages.setSelected (masterPanel.getPage());
}

//==============================================================================
void MainComponent::makeSong (const juce::String& words)
{
    if (songPanel.isWorking() || masterEngineer.isWorking() || words.trim().isEmpty())
        return;

    // A new project for it, this one saved first if it has changes (as New does)
    confirmDiscard ([this, words = words.trim()]
    {
        audio.stop();
        currentFile = juce::File();
        song.loadBlank();
        showEditorTab (Panel::song);
        songPanel.started (words);
        songWords = words;

        // Once everything has caught up with the new project
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::MessageManager::callAsync ([safe, words]
        {
            if (safe != nullptr)
                safe->writeSongParts (words);
        });
    });
}

void MainComponent::writeSongParts (const juce::String& words)
{
    auto style = agents::Request::parse (words).style;

    if (style.isEmpty())
        style = "rock";

    const auto lineup = songwriter::lineupFor (style, words);

    // The drummer first: the song's sections, each once
    songPanel.step ("The drummer writes the beat for each section");
    drummerPanel.setChannel (0);
    drummerPanel.ask (songwriter::drummerWords (words, style));
    auto* drums = drummerPanel.channel();

    if (drums == nullptr || drums->drummer.turns.empty())
    {
        songPanel.failed ("The drummer needs its drum kits first: open Drums in this list to get them, then ask for the song again.");
        return;
    }

    // ...played in an order that lasts two minutes or more
    const int drumsId = drums->id;
    auto& turn = drums->drummer.turns[(size_t) drums->drummer.shown];
    auto groove = drummer::Groove::fromJson (juce::JSON::parse (turn.groove));

    if (! groove.has_value())
    {
        songPanel.failed ("The drummer's part couldn't be read back.");
        return;
    }

    const auto order = songwriter::arrange (*groove, style, words, song.secondsAt (song.ticksPerBar()));
    {
        auto v = juce::JSON::parse (turn.groove);
        juce::Array<juce::var> list;

        for (auto& name : order)
            list.add (name);

        if (auto* o = v.getDynamicObject())
            o->setProperty ("arrangement", list);

        turn.groove = juce::JSON::toString (v, true);
        groove->arrangement = order;
    }

    // The band, each following the drummer (and the bassist's chords), each on a channel of its own
    if (lineup.contains ("bass"))
    {
        songPanel.step ("The bassist plays the chords with the kick");
        bassNew = true;
        bassPanel.setChannel (0);
        bassPanel.ask (words);
    }

    if (lineup.contains ("guitar"))
    {
        songPanel.step ("The guitarist");
        guitarNew = true;
        guitarPanel.setChannel (0);
        guitarPanel.ask (words);
    }

    if (lineup.contains ("keys"))
    {
        songPanel.step ("The keys player");
        keysNew = true;
        keysPanel.setChannel (0);
        keysPanel.ask (words);
    }

    if (lineup.contains ("synth"))
    {
        songPanel.step ("The synth player");
        synthNew = true;
        synthPlayerPanel.setChannel (0);
        synthPlayerPanel.ask (words);
    }

    // All of it in Arrange: the drums in the song's order, everyone under them
    songPanel.step ("Laying it out in Arrange");
    song.playlist.clear();
    drummer::placeInArrange (song, drumsId, *groove);

    std::vector<int> ids;

    for (auto& c : song.channels)
        ids.push_back (c.id);

    for (auto id : ids)
        if (auto c = song.findChannel (id))
        {
            if (! c->bass.turns.empty())                { if (auto part = bass::shownPart (*c))     bass::placeInArrange (song, id, *part); }
            else if (! c->guitar.turns.empty())         { if (auto part = guitar::shownPart (*c))   guitar::placeInArrange (song, id, *part); }
            else if (! c->keys.turns.empty())           { if (auto part = keys::shownPart (*c))     keys::placeInArrange (song, id, *part); }
            else if (! c->synthPlayer.turns.empty())    { if (auto part = synth::shownPart (*c))    synth::placeInArrange (song, id, *part); }
        }

    song.mode = PlayMode::song;
    song.notify (Song::Change::transport);
    audio.setPlayheadTicks (0.0);

    // Mastered: the engineer hears it all and says when it's done (finishSong)
    songPanel.step ("The mastering engineer listens and masters it");
    masterEngineer.ask (words);

    if (! masterEngineer.isWorking() && songPanel.isWorking())
        finishSong (false);
}

void MainComponent::finishSong (bool mastered)
{
    // What it made, in words
    const auto* style = agents::findStyle (agents::Request::parse (songWords).style);
    const int seconds = juce::roundToInt (song.secondsAt (song.songLengthInTicks()));
    juce::String key;
    juce::StringArray players, order;

    for (auto& c : song.channels)
    {
        if (! c.drummer.turns.empty())
        {
            players.add ("drums on " + (c.type == InstrumentType::kit ? drumkits::displayName (c.kit) : c.name));

            if (auto g = drummer::Groove::fromJson (juce::JSON::parse (c.drummer.turns[(size_t) c.drummer.shown].groove)))
                order = g->arrangement;
        }
        else if (! c.bass.turns.empty())
        {
            players.add ("bass on " + c.synthPreset);

            if (auto part = bass::shownPart (c))
                key = part->key.upToFirstOccurrenceOf (",", false, false).trim();
        }
        else if (! c.guitar.turns.empty())
            players.add ("guitar on " + guitarPresetOf (c));
        else if (! c.keys.turns.empty())
            players.add ("keys on " + (c.type == InstrumentType::instrument ? instruments::displayName (c.instrument) : c.synthPreset));
        else if (! c.synthPlayer.turns.empty())
        {
            const auto part = synth::shownPart (c);
            const auto role = part.has_value() ? part->role : juce::String();
            players.add ("synth " + (role == "pad" ? juce::String ("pad") : role == "arp" ? juce::String ("arpeggio") : role == "stabs" ? juce::String ("stabs")
                                                                                                                       : juce::String ("lead")) + " on " + c.synthPreset);
        }
    }

    // Repeats said once each, in order: "Intro, Verse, Chorus x3, Breakdown, Outro"
    juce::String form;
    {
        std::map<juce::String, int> counts;

        for (auto& name : order)
            ++counts[name];

        juce::StringArray said;

        for (auto& name : order)
            if (! said.contains (name))
            {
                said.add (name);
                form << (form.isEmpty() ? "" : ", ") << name << (counts[name] > 1 ? juce::String (juce::CharPointer_UTF8 (" \xc3\x97")) + juce::String (counts[name]) : juce::String());
            }
    }

    juce::String reply;
    reply << "A " << (style != nullptr ? juce::String (style->name).toLowerCase() : juce::String ("rock")) << " song"
          << (key.isNotEmpty() ? " in " + key : juce::String()) << " at " << juce::roundToInt (song.bpm) << " bpm, "
          << seconds / 60 << ":" << juce::String (seconds % 60).paddedLeft ('0', 2) << " long, in " << order.size() << " sections: " << form << ". "
          << "Played by " << players.joinIntoString (", ") << ". ";

    if (mastered && juce::isPositiveAndBelow (song.masterShown, (int) song.masterTurns.size()))
    {
        const auto answer = juce::JSON::parse (song.masterTurns[(size_t) song.masterShown].answer);
        const auto* target = mastering::findTarget (song.mastering.target);
        reply << "Mastered" << (target != nullptr ? " for " + juce::String (target->name) : juce::String()) << ": "
              << juce::String ((float) answer.getProperty ("after", {}).getProperty ("lufs", -100.0), 1) << " LUFS, peaks "
              << juce::String ((float) answer.getProperty ("after", {}).getProperty ("peak", -100.0), 1) << " dB.";
    }
    else
        reply << "Not mastered: " << masterEngineer.getStatus();

    song.songPrompt = songWords;
    song.songReply = reply;
    song.notify (Song::Change::mixer);
    songPanel.finished();
    showEditorTab (Panel::song);
}

void MainComponent::showMasterTargetMenu()
{
    // What the master is for: picking one masters it for that, keeping its tone
    juce::PopupMenu menu;
    menu.addSectionHeader ("Master it for");

    for (auto& t : mastering::targets())
        menu.addItem (juce::String (t.name) + "  (" + t.detail + ")", true, song.mastering.target == juce::String (t.id), [this, id = juce::String (t.id), name = juce::String (t.name)]
        {
            showEditorTab (Panel::masterAi);
            masterEngineer.ask (id == "safe" ? juce::String ("just keep it from clipping") : "for " + name);
        });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&drummerBar.getTargetChip()));
}

void MainComponent::openSynthPlayer (int channelId)
{
    if (song.findChannel (channelId) == nullptr)
        return;

    synthNew = false;
    synthPlayerPanel.setChannel (channelId);
    showEditorTab (Panel::synthPlayer);
}

int MainComponent::makeSynthPlayerChannel (const juce::String& sound, const juce::String& role)
{
    // On the Nova sound the style wants for the job, named for the job
    const int id = rack.addSynthChannel (sound);

    if (auto c = song.findChannel (id))
    {
        c->name = role == "pad" ? "Synth Pad" : role == "arp" ? "Synth Arp" : role == "stabs" ? "Synth Stabs" : "Synth Lead";
        c->synthPlayer.pickSound = true;
        song.notify (Song::Change::channels);
    }

    synthNew = false;
    return id;
}

void MainComponent::followSelectionWithSynthPlayer (Song::Change change)
{
    if (change == Song::Change::everything)
    {
        synthNew = false;
        synthPlayerPanel.setChannel (0);
    }

    // A synth channel selected (one the synth player wrote for, or on a lead, pad, arpeggio, pluck,
    // bell, brass or texture sound that no other player writes for) becomes the one
    auto isSynth = [] (const Channel& c)
    {
        if (! c.synthPlayer.turns.empty())
            return true;

        if (c.type != InstrumentType::synth || ! c.bass.turns.empty() || ! c.keys.turns.empty())
            return false;

        if (auto f = nova::findFactory (c.synthPreset))
            return f->category != "Keys" && f->category != "Bass";

        return false;
    };

    if (change == Song::Change::selection || change == Song::Change::everything)
        if (auto c = song.findChannel (song.selectedChannelId); c != nullptr && isSynth (*c))
        {
            synthNew = false;
            synthPlayerPanel.setChannel (c->id);
            return;
        }

    if (synthPlayerPanel.channel() != nullptr || synthNew)
        return;

    int best = 0;

    for (auto& c : song.channels)
        if (! c.synthPlayer.turns.empty())
        {
            best = c.id;
            break;
        }

    synthPlayerPanel.setChannel (best);
}

void MainComponent::showSynthPlayerTargetMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("The synth player writes for");

    for (auto& c : song.channels)
    {
        if (c.synthPlayer.turns.empty() && (c.type != InstrumentType::synth || ! c.bass.turns.empty() || ! c.keys.turns.empty()))
            continue;

        juce::PopupMenu::Item item (c.name);
        item.colour = c.colour;
        item.isTicked = c.id == synthPlayerPanel.getChannelId();
        item.action = [this, id = c.id] { openSynthPlayer (id); };
        menu.addItem (item);
    }

    menu.addSeparator();
    menu.addItem ("A new synth (the synth player picks the sound)", true, synthPlayerPanel.channel() == nullptr, [this]
    {
        synthNew = true;
        synthPlayerPanel.setChannel (0);
        showEditorTab (Panel::synthPlayer);
        drummerBar.focus();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&drummerBar.getTargetChip()));
}

void MainComponent::openGuitar (int channelId)
{
    if (song.findChannel (channelId) == nullptr)
        return;

    guitarNew = false;
    guitarPanel.setChannel (channelId);
    showEditorTab (Panel::guitar);
}

int MainComponent::makeGuitarChannel()
{
    // A guitar channel made for the guitarist, which chooses the sound for the style
    const int id = rack.addGuitarChannel ("Clean");

    if (auto c = song.findChannel (id))
        c->guitar.pickSound = true;

    guitarNew = false;
    return id;
}

void MainComponent::followSelectionWithGuitar (Song::Change change)
{
    if (change == Song::Change::everything)
    {
        guitarNew = false;
        guitarPanel.setChannel (0);
    }

    // A guitar selected becomes the one the guitarist writes for
    if (change == Song::Change::selection || change == Song::Change::everything)
        if (auto c = song.findChannel (song.selectedChannelId); c != nullptr && (isGuitar (*c) || ! c->guitar.turns.empty()))
        {
            guitarNew = false;
            guitarPanel.setChannel (c->id);
            return;
        }

    if (guitarPanel.channel() != nullptr || guitarNew)
        return;

    int best = 0;

    for (auto& c : song.channels)
    {
        if (! c.guitar.turns.empty())
        {
            best = c.id;
            break;
        }

        if (isGuitar (c) && best == 0)
            best = c.id;
    }

    guitarPanel.setChannel (best);
}

void MainComponent::showGuitarTargetMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("The guitarist writes for");

    for (auto& c : song.channels)
    {
        if (c.guitar.turns.empty() && ! isGuitar (c))
            continue;

        juce::PopupMenu::Item item (c.name + (isGuitar (c) ? "  (" + guitarPresetOf (c) + ")" : juce::String()));
        item.colour = c.colour;
        item.isTicked = c.id == guitarPanel.getChannelId();
        item.action = [this, id = c.id] { openGuitar (id); };
        menu.addItem (item);
    }

    menu.addSeparator();
    menu.addItem ("A new guitar (the guitarist picks the sound)", true, guitarPanel.channel() == nullptr, [this]
    {
        guitarNew = true;
        guitarPanel.setChannel (0);
        showEditorTab (Panel::guitar);
        drummerBar.focus();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&drummerBar.getTargetChip()));
}

void MainComponent::showBassTargetMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("The bassist writes for");

    for (auto& c : song.channels)
    {
        if (c.bass.turns.empty() && ! (c.type == InstrumentType::synth && (c.name.containsIgnoreCase ("bass") || c.synthPreset.containsIgnoreCase ("bass"))))
            continue;

        juce::PopupMenu::Item item (c.name + (c.type == InstrumentType::synth ? "  (" + c.synthPreset + ")" : juce::String()));
        item.colour = c.colour;
        item.isTicked = c.id == bassPanel.getChannelId();
        item.action = [this, id = c.id] { openBass (id); };
        menu.addItem (item);
    }

    menu.addSeparator();
    menu.addItem ("A new bass (the bassist picks the sound)", true, bassPanel.channel() == nullptr, [this]
    {
        bassNew = true;
        bassPanel.setChannel (0);
        showEditorTab (Panel::bass);
        drummerBar.focus();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&drummerBar.getTargetChip()));
}

int MainComponent::makeDrummerChannel()
{
    // A kit channel made for the drummer, which chooses the kit for the style it's asked for
    if (drumkits::ids().isEmpty())
        return 0;

    const int id = rack.addDrumKitChannel (drumkits::ids()[0]);

    if (auto c = song.findChannel (id))
        c->drummer.pickKit = true;

    return id;
}

void MainComponent::followSelectionWithDrummer (Song::Change change)
{
    // Another project: start again
    if (change == Song::Change::everything)
    {
        drummerNewKit = false;
        drummerPanel.setChannel (0);
    }

    // Selecting a drum kit makes it the one the drummer writes for
    if (change == Song::Change::selection || change == Song::Change::everything)
        if (auto c = song.findChannel (song.selectedChannelId); c != nullptr && c->type == InstrumentType::kit)
        {
            drummerNewKit = false;
            drummerPanel.setChannel (c->id);
            return;
        }

    // Keep the one it has (or the new kit asked for), unless that's gone: then one that
    // already has a part, or any kit
    if (drummerPanel.channel() != nullptr || drummerNewKit)
        return;

    int best = 0;

    for (auto& c : song.channels)
    {
        if (! c.drummer.turns.empty())
        {
            best = c.id;
            break;
        }

        if (c.type == InstrumentType::kit && best == 0)
            best = c.id;
    }

    drummerPanel.setChannel (best);
}

void MainComponent::openSynth (int channelId)
{
    if (song.findChannel (channelId) == nullptr)
        return;

    synthPanel.setChannel (channelId);
    showEditorTab (Panel::synth);
}

void MainComponent::updateDrummerBar()
{
    if (barMode == DrummerBar::Mode::song)
    {
        drummerBar.setTarget ("for a new project", accent);
        drummerBar.resized();
        return;
    }

    if (barMode == DrummerBar::Mode::master)
    {
        const auto* t = mastering::findTarget (song.mastering.target);
        drummerBar.setTarget ("for the whole song" + (t != nullptr ? juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + t->name : juce::String()), accent);
        drummerBar.resized();
        return;
    }

    if (barMode == DrummerBar::Mode::synth)
    {
        if (auto c = synthPlayerPanel.channel())
            drummerBar.setTarget ("for " + c->name + (c->type == InstrumentType::synth ? juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + c->synthPreset : juce::String()),
                                  c->colour);
        else
            drummerBar.setTarget ("for a new synth", accent);

        drummerBar.resized();
        return;
    }

    if (barMode == DrummerBar::Mode::keys)
    {
        if (auto c = keysPanel.channel())
            drummerBar.setTarget ("for " + c->name + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                                    + (c->type == InstrumentType::synth ? c->synthPreset : instruments::displayName (c->instrument)), c->colour);
        else
            drummerBar.setTarget ("for new keys", accent);

        drummerBar.resized();
        return;
    }

    if (barMode == DrummerBar::Mode::guitar)
    {
        if (auto c = guitarPanel.channel())
            drummerBar.setTarget ("for " + c->name + (isGuitar (*c) ? juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + guitarPresetOf (*c) : juce::String()),
                                  c->colour);
        else
            drummerBar.setTarget ("for a new guitar", accent);

        drummerBar.resized();
        return;
    }

    if (barMode == DrummerBar::Mode::bass)
    {
        if (auto c = bassPanel.channel())
            drummerBar.setTarget ("for " + c->name + (c->type == InstrumentType::synth ? juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + c->synthPreset : juce::String()),
                                  c->colour);
        else
            drummerBar.setTarget ("for a new bass", accent);

        drummerBar.resized();
        return;
    }

    if (auto c = drummerPanel.channel())
        drummerBar.setTarget ("for " + c->name + (c->type == InstrumentType::kit ? juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + drumkits::displayName (c->kit)
                                                                                  : juce::String (" (General MIDI)")),
                              c->colour);
    else
        drummerBar.setTarget ("for a new drum kit", accent);

    drummerBar.resized();
}

void MainComponent::showDrummerTargetMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("The drummer writes for");

    for (auto& c : song.channels)
    {
        if (c.type != InstrumentType::kit && c.drummer.turns.empty())
            continue;

        juce::PopupMenu::Item item (c.name + (c.type == InstrumentType::kit ? "  (" + drumkits::displayName (c.kit) + ")" : juce::String ("  (General MIDI)")));
        item.colour = c.colour;
        item.isTicked = c.id == drummerPanel.getChannelId();
        item.action = [this, id = c.id] { openDrummer (id); };
        menu.addItem (item);
    }

    menu.addSeparator();
    menu.addItem ("A new drum kit (the drummer picks it)", ! drumkits::ids().isEmpty(), drummerPanel.channel() == nullptr, [this]
    {
        drummerNewKit = true;
        drummerPanel.setChannel (0);
        showEditorTab (Panel::drummer);
        drummerBar.focus();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&drummerBar.getTargetChip()));
}

void MainComponent::showToast (const juce::String& message)
{
    toast.show (message);
    resized();
}

void MainComponent::Toast::show (const juce::String& message)
{
    text = message;
    setVisible (true);
    toFront (false);
    repaint();
    startTimer (8000);
}

void MainComponent::Toast::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (raised);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (r.withWidth (4.0f).reduced (0.0f, 10.0f), 2.0f);
    g.setColour (text.isEmpty() ? textFaint : theme::text);
    g.setFont (font (16.0f, true));
    g.drawFittedText (text, getLocalBounds().reduced (18, 6), juce::Justification::centredLeft, 2);
}

void MainComponent::getInstruments()
{
    if (instrumentFetcher != nullptr)
        return;

    struct Fetcher : public juce::Thread
    {
        explicit Fetcher (MainComponent& m) : juce::Thread ("Instruments"), main (&m)   { startThread(); }
        ~Fetcher() override                                                        { stopThread (20000); }

        void run() override
        {
            int lastPercent = -1;
            juce::String lastStep;
            const auto error = instruments::fetch ([&] (float done, const juce::String& what)
            {
                const int percent = juce::roundToInt (done * 100.0f);

                if (percent != lastPercent || what != lastStep)
                {
                    lastPercent = percent;
                    lastStep = what;
                    juce::MessageManager::callAsync ([m = main, text = what + "...  " + juce::String (percent) + "%"] { if (m != nullptr) m->showToast (text); });
                }
            }, [this] { return threadShouldExit(); });

            if (threadShouldExit())
                return;

            juce::MessageManager::callAsync ([m = main, error]
            {
                if (m == nullptr)
                    return;

                m->instrumentFetcher.reset();
                m->sidebar.kitsChanged();
                m->showToast (error.isNotEmpty() ? error
                                                 : "The piano and orchestra are in: " + juce::String ((int) instruments::list().size())
                                                     + " instruments, under Instruments in the sidebar.");
            });
        }

        juce::Component::SafePointer<MainComponent> main;
    };

    showToast ("Getting the piano and orchestra (2.9 GB, once)...");
    instrumentFetcher = std::make_unique<Fetcher> (*this);
}

void MainComponent::importDrumKit()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder of drum samples (a sample pack)",
                                                   juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, dialog = ComputerKeyboard::openSystemDialog()] (const juce::FileChooser& fc)
    {
        if (const auto folder = fc.getResult(); folder.isDirectory())
        {
            const auto made = song.channels.size();
            const auto report = importDrumKitFrom (folder);
            juce::AlertWindow::showMessageBoxAsync (song.channels.size() > made ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                                    song.channels.size() > made ? "Drum kit made" : "No drum kit made", report);
        }
    });
}

juce::String MainComponent::importDrumKitFrom (const juce::File& folder)
{
    juce::String report;
    const auto id = drumkits::importFolder (folder, report);

    if (id.isNotEmpty())
    {
        sidebar.kitsChanged();
        openDrummer (rack.addDrumKitChannel (id));
        drummerBar.focus();
    }

    return report;
}

void MainComponent::openPluginEditor (int channelId)
{
    auto plugin = dynamic_cast<te::ExternalPlugin*> (audio.getInstrument (channelId));

    if (plugin == nullptr)
        return;

    auto& window = pluginWindows[channelId];

    if (window == nullptr || window->getPlugin() != plugin)
        window = std::make_unique<PluginWindow> (*plugin, nullptr, [this, channelId]
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), channelId]
            {
                if (safe != nullptr)
                    safe->pluginWindows.erase (channelId);
            });
        });

    window->onKey = [this] (const juce::KeyPress& key) { return keyPressed (key); };
    window->toFront (true);
}

void MainComponent::openEffectEditor (int effectId)
{
    audio.syncNow();
    auto plugin = audio.getEffect (effectId);

    if (plugin == nullptr)
        return;

    auto& window = effectWindows[effectId];

    if (window == nullptr || window->getPlugin() != plugin)
    {
        // A stock effect gets Nightshift's own panel; a VST3 its own editor
        std::unique_ptr<juce::Component> ownPanel;

        if (auto stock = dynamic_cast<StockEffect*> (plugin))
            ownPanel = createStockPanel (*stock, song, audio);
        else if (dynamic_cast<te::ExternalPlugin*> (plugin) == nullptr)
            return;

        window = std::make_unique<PluginWindow> (*plugin, std::move (ownPanel), [this, effectId]
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), effectId]
            {
                if (safe != nullptr)
                    safe->effectWindows.erase (effectId);
            });
        });

        window->onKey = [this] (const juce::KeyPress& key) { return keyPressed (key); };
    }

    // Say whose it is: "Nightshift Vinyl - Kick", or "- Master"
    juce::String owner ("Master");

    for (auto& c : song.channels)
        for (auto& e : c.effects)
            if (e.id == effectId)
                owner = c.name;

    for (auto& b : song.buses)
        for (auto& e : b.effects)
            if (e.id == effectId)
                owner = b.name;

    window->setName (plugin->getName() + " - " + owner);
    window->toFront (true);
}

void MainComponent::closeStalePluginWindows()
{
    audio.syncNow();

    for (auto it = pluginWindows.begin(); it != pluginWindows.end();)
    {
        if (audio.getInstrument (it->first) != it->second->getPlugin())
            it = pluginWindows.erase (it);
        else
            ++it;
    }

    // An effect removed, or rebuilt when its chain changed
    for (auto it = effectWindows.begin(); it != effectWindows.end();)
    {
        if (audio.getEffect (it->first) != it->second->getPlugin())
            it = effectWindows.erase (it);
        else
            ++it;
    }
}

//==============================================================================
void MainComponent::refreshMidiLight()
{
    // The inputs switched on, and the channel they play
    juce::StringArray on;

    for (auto& name : audio.getMidiInputNames())
        if (audio.isMidiInputEnabled (name))
            on.add (name);

    const auto* playing = song.findChannel (audio.getMidiTargetChannel());
    toolbar.setMidi (on, playing != nullptr ? playing->name : juce::String());
}

void MainComponent::showControlMenu (int owner, const juce::String& param, const juce::String& name)
{
    juce::PopupMenu menu;
    menu.addItem ("Automate " + name + " in Arrange", [this, owner, param] { showAutomationFor (owner, param); });

    // MIDI learn: the next knob or fader moved on a controller drives it
    const bool anyMidi = ! audio.getMidiInputNames().isEmpty();
    menu.addItem (anyMidi ? juce::String ("Link to a knob on your MIDI controller...") : juce::String ("Link to a MIDI controller knob (plug one in first)"),
                  anyMidi, false, [this, owner, param, name]
    {
        audio.learnMidiController (owner, param, [this, name] (const juce::String& what)
        {
            showToast (name + " is linked to " + what + ": move it to change " + name);
        });

        showToast ("Move a knob or fader on your MIDI controller to link it to " + name + " (Esc cancels)");
    });

    if (auto link = song.findControllerLink (owner, param))
        menu.addItem ("Forget its MIDI link (CC " + juce::String (link->controller) + " on " + link->device + ")", [this, owner, param]
        {
            std::erase_if (song.controllerLinks, [owner, param] (auto& l) { return l.owner == owner && l.param == param; });
            song.notify (Song::Change::mixer);
        });

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void MainComponent::showAutomationFor (int owner, const juce::String& param)
{
    if (owner != 0 && song.findChannel (owner) == nullptr)
        return;

    if (owner != 0)
        playlist.addTrackFor (owner);

    const auto name = audio.automationName (owner, param);
    song.laneFor (owner, param, name);
    song.showAutomation = true;
    song.notify (Song::Change::automation);

    // Arrange on screen, if the editor had all the height
    if (editorMaximised)
    {
        editorMaximised = maximisedForMixer = false;
        resized();
    }

    showToast (name + (owner == 0 ? " is along the bottom of Arrange" : " is under its track in Arrange")
               + ": click its lane to add points. Song mode plays them");
}

void MainComponent::songChanged (Song::Change c)
{
    // Automation's button lit while its lanes show
    if (c == Song::Change::automation || c == Song::Change::everything)
        automationButton.setToggleState (song.showAutomation, juce::dontSendNotification);

    // The bar says what the master is for
    if ((c == Song::Change::mixer || c == Song::Change::everything) && barMode == DrummerBar::Mode::master)
        updateDrummerBar();

    if (c == Song::Change::channels || c == Song::Change::everything || (c == Song::Change::mixer && ! effectWindows.empty()))
        closeStalePluginWindows();

    // The drummer follows a selected drum kit, the Synth tab a selected synth; the bar says
    // who it writes for
    if (c == Song::Change::selection || c == Song::Change::channels || c == Song::Change::everything)
    {
        followSelectionWithDrummer (c);
        followSelectionWithBass (c);
        followSelectionWithGuitar (c);
        followSelectionWithKeys (c);
        followSelectionWithSynthPlayer (c);

        if (c == Song::Change::everything)
            synthPanel.setChannel (0);

        if (auto ch = song.findChannel (song.selectedChannelId); ch != nullptr && ch->type == InstrumentType::synth
              && c != Song::Change::channels && ch->id != synthPanel.getChannelId())
            synthPanel.setChannel (ch->id);

        updateDrummerBar();
    }

    if (c == Song::Change::playlist || c == Song::Change::transport || c == Song::Change::patterns
         || c == Song::Change::notes || c == Song::Change::everything)
        updateArrangeTitle();

    if (c == Song::Change::currentPattern || c == Song::Change::patterns || c == Song::Change::transport || c == Song::Change::everything)
        updateEditorHeader();

    // Bars and beats follow the time signature (the piano roll's, its pattern's)
    if (c == Song::Change::transport || c == Song::Change::everything || c == Song::Change::currentPattern || c == Song::Change::patterns)
        applySnaps();

    // Grow the editor with the rack's channels, within reason
    if (c == Song::Change::channels && editorShown && editorTab == Panel::rack && editorHeight > 0)
    {
        const int wanted = rack.getIdealHeight() + DockPanel::headerHeight + 1;

        if (editorHeight < wanted)
        {
            editorHeight = std::min (wanted, getHeight() / 2);
            resized();
        }
    }
}

void MainComponent::timerCallback()
{
    // The MIDI light: lit a moment after each note or knob; which keyboards, now and then
    const auto lastMidi = audio.getLastMidiActivity();
    toolbar.setMidiLit (lastMidi != 0 && juce::Time::getMillisecondCounter() - lastMidi < 150);

    if (++midiListTicks >= 30)
    {
        midiListTicks = 0;
        refreshMidiLight();
    }

    toolbar.refresh();
    rack.refreshPlayhead();
    pianoRoll.refreshPlayhead();
    playlist.refreshPlayhead();

    // Every strip on screen is metered, each read once a tick
    std::vector<int> metered;

    const bool mixerShown = isPanelVisible (Panel::mixer);

    if (inspectorPanel.isVisible())
        metered.push_back (inspector.getChannelStrip().getChannel());

    if (mixerShown)
        mixer.addMeteredIds (metered);

    audio.readMeters (metered);

    if (inspectorPanel.isVisible())
        inspector.refreshMeters();

    if (mixerShown)
        mixer.refreshMeters();

    // Takes go in a folder beside the project, once it has a name
    if (recordingsFileFor != currentFile)
    {
        recordingsFileFor = currentFile;
        audio.setProjectFile (currentFile);
    }

    if (auto notice = audio.takeRecordingFolderNotice(); notice.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Takes saved elsewhere", notice);

    if (auto title = getWindowTitle(); title != lastTitle)
    {
        lastTitle = title;
        toolbar.setProjectName (currentFile.existsAsFile() ? currentFile.getFileNameWithoutExtension() : juce::String ("Untitled"),
                                song.hasUnsavedChanges());

        if (onTitleChanged != nullptr)
            onTitleChanged();
    }
}

juce::String MainComponent::getWindowTitle() const
{
    const auto name = currentFile.existsAsFile() ? currentFile.getFileNameWithoutExtension() : juce::String ("Untitled");
    return "NightShift - " + name + (song.hasUnsavedChanges() ? " *" : "");
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;

    // Space plays and stops, as in FL (and records while Record is armed); Shift+Space too
    if (key == juce::KeyPress::spaceKey
         || key == juce::KeyPress (juce::KeyPress::spaceKey, shift, 0)) { playOrStop(); return true; }

    // Plain keys are the PC keyboard's; every letter shortcut takes Shift, Ctrl or Alt
    if (! typingIntoText() && keyboard.keyPressed (key))
        return true;

    // Escape while waiting for a MIDI knob to link: not linked after all
    if (key == juce::KeyPress::escapeKey && audio.isLearningMidi())
    {
        audio.cancelMidiLearn();
        showToast ("Not linked to a MIDI knob");
        return true;
    }

    // The editing keys (Ctrl+C, Ctrl+V, Delete, the arrows...) go to the editor clicked last,
    // when neither had them first
    if (! typingIntoText())
        if (auto command = editCommandFor (key))
        {
            if (lastEditor == Editor::pianoRoll && isPanelVisible (Panel::pianoRoll) && pianoRoll.edit (*command))
                return true;

            if (lastEditor == Editor::arrange && isPanelVisible (Panel::playlist) && playlist.edit (*command))
                return true;
        }

    if (key == juce::KeyPress ('m', shift, 0))                 { setKeyboardOn (! keyboard.isEnabled()); return true; }
    if (key == juce::KeyPress ('s', cmd, 0))                   { saveProject (false); return true; }
    if (key == juce::KeyPress ('s', cmd | shift, 0))           { saveProject (true); return true; }
    if (key == juce::KeyPress ('o', cmd, 0))                   { openProject(); return true; }
    if (key == juce::KeyPress ('n', cmd, 0))                   { newProject(); return true; }
    if (key == juce::KeyPress ('r', cmd, 0))                   { exportAudio(); return true; }
    if (key == juce::KeyPress ('z', cmd, 0))                   { song.undo(); return true; }
    if (key == juce::KeyPress ('z', cmd | shift, 0)
         || key == juce::KeyPress ('y', cmd, 0))               { song.redo(); return true; }
    for (int tab = 0; tab < 6; ++tab)
        if (key == juce::KeyPress ('1' + tab, cmd, 0))
        {
            // Cmd (Ctrl) + 1 to 6: the editor's tabs, for a Mac, whose F-keys are its own
            showEditorTab (tab == 3 ? lastAgent : tabPanel (tab));
            return true;
        }

    if (key == juce::KeyPress (juce::KeyPress::F5Key))         { togglePanel (Panel::playlist); return true; }
    if (key == juce::KeyPress (juce::KeyPress::F6Key))         { togglePanel (Panel::rack); return true; }
    if (key == juce::KeyPress (juce::KeyPress::F7Key))         { togglePanel (Panel::pianoRoll); return true; }
    if (key == juce::KeyPress (juce::KeyPress::F9Key))         { togglePanel (Panel::mixer); return true; }
    if (key == juce::KeyPress (juce::KeyPress::F11Key))        { togglePanel (Panel::synth); return true; }
    if (key == juce::KeyPress (juce::KeyPress::F12Key))        { togglePanel (Panel::master); return true; }

    if (key == juce::KeyPress (juce::KeyPress::F12Key, juce::ModifierKeys::shiftModifier, 0))
    {
        // The mastering engineer, ready to type to
        if (isPanelVisible (Panel::masterAi))
            togglePanel (Panel::masterAi);
        else
        {
            showEditorTab (Panel::masterAi);
            drummerBar.focus();
        }

        return true;
    }

    if (key == juce::KeyPress (juce::KeyPress::F8Key, juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        // The synth player's tab, ready to type into
        if (isPanelVisible (Panel::synthPlayer))
            togglePanel (Panel::synthPlayer);
        else
        {
            showEditorTab (Panel::synthPlayer);
            drummerBar.focus();
        }

        return true;
    }

    if (key == juce::KeyPress (juce::KeyPress::F8Key, juce::ModifierKeys::altModifier, 0))
    {
        // The keys player's tab, ready to type into
        if (isPanelVisible (Panel::keys))
            togglePanel (Panel::keys);
        else
        {
            showEditorTab (Panel::keys);
            drummerBar.focus();
        }

        return true;
    }

    if (key == juce::KeyPress (juce::KeyPress::F8Key, juce::ModifierKeys::commandModifier, 0))
    {
        // The guitarist's tab, ready to type into
        if (isPanelVisible (Panel::guitar))
        {
            togglePanel (Panel::guitar);
        }
        else
        {
            showEditorTab (Panel::guitar);
            drummerBar.focus();
        }

        return true;
    }

    if (key == juce::KeyPress (juce::KeyPress::F8Key, juce::ModifierKeys::shiftModifier, 0))
    {
        // The bassist's tab, ready to type into
        if (isPanelVisible (Panel::bass))
        {
            togglePanel (Panel::bass);
        }
        else
        {
            showEditorTab (Panel::bass);
            drummerBar.focus();
        }

        return true;
    }

    if (key == juce::KeyPress (juce::KeyPress::F8Key))
    {
        // The drummer's tab, ready to type into
        if (isPanelVisible (Panel::drummer))
        {
            togglePanel (Panel::drummer);
        }
        else
        {
            showEditorTab (Panel::drummer);
            drummerBar.focus();
        }

        return true;
    }
    if (key == juce::KeyPress (juce::KeyPress::F4Key))         { newPattern(); return true; }
    if (key == juce::KeyPress (juce::KeyPress::F10Key))        { showAudioSettings(); return true; }
    if (key == juce::KeyPress ('k', cmd, 0))                   { sidebar.focusSearch(); return true; }
    if (key == juce::KeyPress ('i', shift, 0))                 { toggleInspector(); return true; }
    if (key == juce::KeyPress (juce::KeyPress::F2Key))         { rack.renameChannel (song.selectedChannelId); return true; }

    if (key == juce::KeyPress ('m', cmd, 0))
    {
        auto o = audio.getOptions();
        o.metronome = ! o.metronome;
        audio.setOptions (o);
        return true;
    }

    if (key == juce::KeyPress ('l', shift, 0))
    {
        song.mode = song.mode == PlayMode::song ? PlayMode::pattern : PlayMode::song;
        song.notify (Song::Change::transport);
        return true;
    }

    // Shift with + or -: the next or previous pattern (Shift turns = into +, and - into _)
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    const auto typed = key.getTextCharacter();

    if (mods.isShiftDown() && ! mods.isCtrlDown() && ! mods.isAltDown())
    {
        if (typed == '+' || code == '+' || code == '=' || code == juce::KeyPress::numberPadAdd)
        {
            stepPattern (1);
            return true;
        }

        if (typed == '_' || code == '_' || code == '-' || code == juce::KeyPress::numberPadSubtract)
        {
            stepPattern (-1);
            return true;
        }
    }

    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    return pollKeyboard();
}

bool MainComponent::pollKeyboard()
{
    const bool involved = keyboard.poll (typingIntoText());

    // The keyboard button lights up while notes are held: it's hearing you
    if (const bool lit = keyboard.getNotesHeld() > 0; lit != keyboardLit)
    {
        keyboardLit = lit;
        toolbar.setKeyboardPlaying (lit);
    }

    return involved;
}

bool MainComponent::typingIntoText() const
{
    return dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr;
}

void MainComponent::setKeyboardOn (bool on)
{
    keyboard.setEnabled (on);
    auto o = audio.getOptions();
    o.computerKeyboard = on;
    audio.setOptions (o);
    showToast (on ? "Computer keyboard on: A S D F G H J K L ; ' are the white keys, W E T Y U O P ] the black ones, playing the selected channel ("
                      + keyboard.describe() + "). Z/X octave, C/V velocity. Arm Record, then Space records. Shift+M turns it off."
                  : juce::String ("Computer keyboard off. Shift+M turns it back on."));
}

void MainComponent::playOrStop()
{
    audio.setRecordSnap (snapTicks (pianoSnap));

    // A project with no audio track yet and nothing else to record: the first take makes one
    // on the first input, armed (as R used to)
    if (audio.isRecordArmed() && ! audio.isPlaying() && ! audio.isWaitingForInput())
    {
        bool hasAudioChannel = false;

        for (auto& c : song.channels)
            hasAudioChannel = hasAudioChannel || c.type == InstrumentType::audio;

        const auto* selected = song.findChannel (song.selectedChannelId);
        const bool notesToRecord = selected != nullptr && selected->type != InstrumentType::audio;

        if (! hasAudioChannel && ! notesToRecord && ! audio.getInputNames().isEmpty())
        {
            rack.addAudioChannel();
            audio.syncNow();
        }
    }

    if (auto notice = audio.togglePlay(); notice.isNotEmpty())
        showToast (notice);

    toolbar.refresh();
}

juce::PopupMenu MainComponent::createAddTrackMenu (int insertAt)
{
    auto menu = rack.createAddMenu ([this, insertAt] (int channelId) { playlist.addTrackFor (channelId, insertAt); });
    menu.addSeparator();
    menu.addItem ("Empty track (paint patterns on it)", [this, insertAt]
    {
        song.checkpoint();
        song.insertTrack ({}, insertAt);
        song.notify (Song::Change::playlist);
    });

    return menu;
}

void MainComponent::showAddTrackMenu (int insertAt, juce::Rectangle<int> area)
{
    auto options = juce::PopupMenu::Options();
    createAddTrackMenu (insertAt).showMenuAsync (area.isEmpty() ? options.withMousePosition() : options.withTargetScreenArea (area));
}

void MainComponent::toggleRecordArm()
{
    audio.setRecordSnap (snapTicks (pianoSnap));
    const bool arming = ! audio.isRecordArmed();
    auto notice = audio.setRecordArmed (arming);

    if (notice.isEmpty() && arming)
    {
        const auto what = audio.describeRecordTargets();
        notice = what.isEmpty() ? juce::String ("Record armed. Select an instrument channel to record what you play, or arm an audio track; then press Space.")
                                : "Record armed: Space records " + what + ". Right-click Record for the countdown, blend and more.";
    }

    if (notice.isNotEmpty())
        showToast (notice);

    toolbar.refresh();
}

juce::PopupMenu MainComponent::createRecordMenu()
{
    juce::PopupMenu menu;
    const auto o = audio.getOptions();
    auto set = [this] (std::function<void (AudioEngine::Options&)> change)
    {
        auto changed = audio.getOptions();
        change (changed);
        audio.setOptions (changed);
    };

    menu.addSectionHeader ("Recording");
    menu.addItem (audio.isRecordArmed() ? "Disarm Record" : "Arm Record", [this] { toggleRecordArm(); });
    menu.addSeparator();

    juce::PopupMenu countdown;

    for (int bars : { 0, 1, 2 })
        countdown.addItem (bars == 0 ? "Off" : bars == 1 ? "1 bar" : "2 bars", true, o.countInBars == bars,
                           [set, bars] { set ([bars] (auto& x) { x.countInBars = bars; }); });

    menu.addSubMenu ("Countdown before recording", countdown);
    menu.addItem ("Start when I play the first note", true, o.startOnInput,
                  [set, v = ! o.startOnInput] { set ([v] (auto& x) { x.startOnInput = v; }); });
    menu.addItem ("Blend: keep the notes already in the pattern", true, o.blendNotes,
                  [set, v = ! o.blendNotes] { set ([v] (auto& x) { x.blendNotes = v; }); });

    juce::PopupMenu snap;

    for (auto name : { "1/16", "1/32", "Beat", "Bar", "Off" })
        snap.addItem (juce::String (name) + (juce::String (name) == "Off" ? " (as played)" : ""), true, pianoSnap == name, [this, name]
        {
            pianoSnap = name;
            applySnaps();
        });

    menu.addSubMenu ("Snap recorded notes to (" + pianoSnap + ")", snap);
    menu.addSeparator();
    menu.addItem ("Metronome only while recording", true, o.metronomeRecordingOnly,
                  [set, v = ! o.metronomeRecordingOnly] { set ([v] (auto& x) { x.metronomeRecordingOnly = v; }); });
    menu.addItem ("Metronome...", [this] { toolbar.showMetronomePanel(); });
    return menu;
}

void MainComponent::stepPattern (int delta)
{
    const int index = song.indexOfPattern (song.currentPattern().id) + delta;

    if (juce::isPositiveAndBelow (index, (int) song.patterns.size()))
    {
        song.currentPatternId = song.patterns[(size_t) index].id;
        song.notify (Song::Change::currentPattern);
    }
}

void MainComponent::newPattern()
{
    song.checkpoint();
    auto& pattern = song.addPattern();

    // Made in Song mode where the time signature isn't the start's: in that one, for the part there
    if (song.mode == PlayMode::song)
        song.fitPatternMeter (pattern, std::max (0.0, audio.getPlayheadTicks()));

    song.currentPatternId = pattern.id;
    song.notify (Song::Change::patterns);
    song.notify (Song::Change::currentPattern);
}

//==============================================================================
void MainComponent::showMainMenu()
{
    juce::PopupMenu menu;

    // An item with its key at the right, dimmer (not spaced out by hand: that never lines up)
    auto withKey = [&menu] (const juce::String& name, const juce::String& key, std::function<void()> action)
    {
        juce::PopupMenu::Item item (name);
        item.shortcutKeyDescription = shortcutText (key);
        item.action = std::move (action);
        menu.addItem (std::move (item));
    };

    withKey ("New project...", "Ctrl+N", [this] { newProject(); });
    withKey ("Open...", "Ctrl+O", [this] { openProject(); });
    withKey ("Save", "Ctrl+S", [this] { saveProject (false); });
    withKey ("Save as...", "Ctrl+Shift+S", [this] { saveProject (true); });
    menu.addItem ("Load the demo song", [this] { confirmDiscard ([this] { currentFile = juce::File(); song.loadDemo(); }); });
    menu.addSeparator();
    withKey ("Export WAV...", "Ctrl+R", [this] { exportAudio(); });
    menu.addSeparator();
    withKey ("Undo", "Ctrl+Z", [this] { song.undo(); });
    withKey ("Redo", "Ctrl+Y", [this] { song.redo(); });
    menu.addSeparator();
    menu.addSubMenu ("Master effects" + (song.masterEffects.empty() ? juce::String() : "  (" + juce::String ((int) song.masterEffects.size()) + ")"),
                     rack.createEffectsMenu (0));
    menu.addSubMenu ("Time signature", Toolbar::createMeterMenu (song, audio));
    menu.addSubMenu ("Recording", createRecordMenu());
    menu.addItem ("Metronome...", [this] { toolbar.showMetronomePanel(); });
    withKey ("Master: how loud it is...", "F12", [this] { showEditorTab (Panel::master); });
    withKey ("Audio settings...", "F10", [this] { showAudioSettings(); });
    menu.addItem ("Find new instruments and effects", [this] { findNewPlugins (true); });
    menu.addItem ("Plugin list and folders...", [this] { scanPlugins(); });
    menu.addSeparator();
    menu.addItem ("Check for updates...", [this] { checkForUpdates(); });
    menu.addItem ("About NightShift", [this] { showAbout(); });

    menu.showMenuAsync ({});
}

//==============================================================================
namespace
{
    /** Check for updates' work on a thread of its own: asking GitHub, then the download. */
    struct UpdateWorker : public juce::Thread
    {
        explicit UpdateWorker (std::function<void (UpdateWorker&)> j) : juce::Thread ("Updates"), job (std::move (j))   { startThread(); }
        ~UpdateWorker() override        { stopThread (4000); }
        void run() override             { job (*this); }

        std::function<void (UpdateWorker&)> job;
    };

    /** The installer's download: how far, and Cancel. */
    class UpdateProgressWindow : public juce::AlertWindow
    {
    public:
        UpdateProgressWindow (const juce::String& title, juce::Component* over)
            : juce::AlertWindow (title, "Downloading the installer from GitHub. It's checked before it runs.", juce::MessageBoxIconType::NoIcon, over)
        {
            addProgressBarComponent (*progress);
            addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        }

        std::shared_ptr<double> progress = std::make_shared<double> (0.0);     // the download writes it, the bar reads it
    };

    /** A message and OK (Enter or Esc), over the studio: an AlertWindow of its own, open at once, as the other
        questions are (showMessageBoxAsync opens a moment later, which the text check's picture never waits for). */
    void tell (juce::Component* over, juce::MessageBoxIconType icon, const juce::String& title, const juce::String& message)
    {
        auto* window = new juce::AlertWindow (title, message, icon, over);
        window->addButton ("OK", 0, juce::KeyPress (juce::KeyPress::returnKey), juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, nullptr, true);
    }
}

void MainComponent::checkForUpdates()
{
    if (updateWorker != nullptr)
        return;

    showToast ("Checking for updates...");
    juce::Component::SafePointer<MainComponent> self (this);

    updateWorker = std::make_unique<UpdateWorker> ([self] (UpdateWorker& worker)
    {
        updates::Release release;
        const auto error = updates::fetchLatest (release);

        if (worker.threadShouldExit())
            return;

        juce::MessageManager::callAsync ([self, release, error]
        {
            if (self == nullptr)
                return;

            self->updateWorker.reset();
            self->updateChecked (release, error);
        });
    });
}

void MainComponent::updateChecked (const updates::Release& release, const juce::String& error)
{
    toast.setVisible (false);
    const auto current = updates::currentVersion();

    if (error.isNotEmpty())
    {
        tell (this, juce::MessageBoxIconType::WarningIcon, "Couldn't check for updates", error);
        return;
    }

    if (updates::compare (release.version, current) <= 0)
    {
        tell (this, juce::MessageBoxIconType::InfoIcon, "You are up to date", "NightShift " + current + " is the newest version.");
        return;
    }

    juce::String message ("You have NightShift " + current + ".");

    if (! release.whatsNew.isEmpty())
    {
        message << "\n\nWhat's new:";

        for (auto& point : release.whatsNew)
            message << "\n" << juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2  ")) << point;
    }

    // Out, but the installer for this computer isn't up yet (GitHub is still building it)
    if (release.installer.isEmpty())
    {
        message << "\n\nIts installer for this computer isn't on GitHub yet. Try again in a little while.";
        auto* window = new juce::AlertWindow (release.title + " is out", message, juce::MessageBoxIconType::InfoIcon, this);
        window->addButton ("Open its page", 1);
        window->addButton ("Close", 0, juce::KeyPress (juce::KeyPress::returnKey), juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([page = release.page] (int result)
        {
            if (result == 1 && page.startsWithIgnoreCase ("https://"))
                juce::URL (page).launchInDefaultBrowser();
        }), true);
        return;
    }

   #if JUCE_MAC
    message << "\n\nInstall now closes NightShift and opens the installer: follow its steps, then open NightShift again.";
   #else
    message << "\n\nInstall now closes NightShift, installs " + release.title + " and opens it again.";
   #endif

    auto* window = new juce::AlertWindow (release.title + " is out", message, juce::MessageBoxIconType::QuestionIcon, this);
    window->addButton ("Install now", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Not now", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // Lit, as the start screen's main buttons are
    if (auto* b = window->getButton ("Install now"))
    {
        b->setColour (juce::TextButton::buttonColourId, accent);
        b->setColour (juce::TextButton::textColourOffId, background);
    }

    juce::Component::SafePointer<MainComponent> self (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([self, release] (int result)
    {
        // The song saved (or let go) first: the installer closes NightShift
        if (result == 1 && self != nullptr)
            self->confirmDiscard ([self, release] { if (self != nullptr) self->installUpdate (release); });
    }), true);
}

void MainComponent::installUpdate (const updates::Release& release)
{
    if (updateWorker != nullptr)
        return;

    auto* window = new UpdateProgressWindow ("Getting " + release.title, this);
    auto progress = window->progress;
    auto cancelled = std::make_shared<std::atomic<bool>> (false);
    juce::Component::SafePointer<juce::AlertWindow> shown (window);
    juce::Component::SafePointer<MainComponent> self (this);

    window->enterModalState (true, juce::ModalCallbackFunction::create ([cancelled] (int result)
    {
        if (result == 0)        // Cancel (the download's end closes it with 1)
            cancelled->store (true);
    }), true);

    updateWorker = std::make_unique<UpdateWorker> ([self, shown, release, progress, cancelled] (UpdateWorker& worker)
    {
        juce::File installer;
        const auto error = updates::download (release, updates::downloadFolder(), installer,
                                              [progress] (double done) { *progress = done; },
                                              [&worker, cancelled] { return cancelled->load() || worker.threadShouldExit(); });

        if (worker.threadShouldExit())
            return;

        juce::MessageManager::callAsync ([self, shown, installer, error, cancelled, title = release.title]
        {
            if (shown != nullptr)
                shown->exitModalState (1);

            if (self == nullptr)
                return;

            self->updateWorker.reset();

            if (cancelled->load())
                return;

            if (error.isNotEmpty())
            {
                tell (self, juce::MessageBoxIconType::WarningIcon, "Couldn't get " + title, error);
                return;
            }

            if (! updates::startInstaller (installer))
            {
                tell (self, juce::MessageBoxIconType::WarningIcon, "Couldn't start the installer",
                      "It's in " + installer.getParentDirectory().getFullPathName() + ": open " + installer.getFileName() + " from there.");
                return;
            }

            // The installer has it from here (the song's been saved or let go already)
            juce::JUCEApplication::getInstance()->quit();
        });
    });
}

namespace
{
    /** About's contents, drawn here at a width of their own (an AlertWindow's message squeezes long
        lines into a narrow column): what NightShift is, NightShift is free (with the donation pitch),
        the keys as two columns of key caps, the mouse, credits. */
    class AboutBody : public juce::Component
    {
    public:
        AboutBody()                         { setSize (aboutWidth, layOut (nullptr)); }

        static constexpr int aboutWidth = 760;
        void paint (juce::Graphics& g) override  { layOut (&g); }

    private:
        struct Key { const char* keys; const char* does; };

        static constexpr int rowHeight = 27;

        /** Draws (when given somewhere to) and returns the height it takes. */
        int layOut (juce::Graphics* g) const
        {
            using namespace theme;
            const int w = getWidth() > 0 ? getWidth() : aboutWidth;
            int y = 0;

            // As many lines as the words take at this width (a fixed count cut the last one off when the
            // text grew), 1.3 lines apart
            auto paragraph = [&] (const juce::String& s, juce::Font f, juce::Colour c)
            {
                juce::GlyphArrangement words;
                words.addJustifiedText (f, s, 0.0f, (float) y + f.getAscent(), (float) w, juce::Justification::left, f.getHeight() * 0.3f);
                int lines = 1;

                for (int i = 1; i < words.getNumGlyphs(); ++i)
                    if (words.getGlyph (i).getBaselineY() > words.getGlyph (i - 1).getBaselineY() + 1.0f)
                        ++lines;

                if (g != nullptr)
                {
                    g->setColour (c);
                    words.draw (*g);
                }

                y += (int) std::ceil (f.getHeight() * 1.3f) * lines;
            };

            auto label = [&] (const juce::String& s)
            {
                if (g != nullptr)
                {
                    g->setColour (textFaint);
                    g->setFont (labelFont());
                    g->drawText (s, 0, y, w, 18, juce::Justification::centredLeft, true);
                }

                y += 26;
            };

            paragraph ("A pattern-first music studio.   " + licence::copyright, font (17.0f), text);
            y += 14;

            // NightShift is free: the pitch, in a warm strip (Donate... is under it all)
            if (donate::isSet())
            {
                const juce::Rectangle<int> strip (0, y, w, 56);

                if (g != nullptr)
                {
                    g->setColour (accent.withAlpha (0.07f));
                    g->fillRoundedRectangle (strip.toFloat(), 10.0f);
                    g->setColour (accent.withAlpha (0.32f));
                    g->drawRoundedRectangle (strip.toFloat().reduced (0.5f), 10.0f, 1.0f);

                    auto inner = strip.reduced (16, 0);
                    auto heart = icons::heart();
                    heart.applyTransform (heart.getTransformToScaleToFit (inner.removeFromLeft (22).withSizeKeepingCentre (22, 20).toFloat(), true));
                    g->setColour (accent);
                    g->fillPath (heart);
                    inner.removeFromLeft (14);

                    auto lines = inner.withSizeKeepingCentre (inner.getWidth(), 38);
                    g->setColour (text);
                    g->setFont (font (16.5f, true));
                    g->drawText ("NightShift is free", lines.removeFromTop (19), juce::Justification::centredLeft, true);
                    g->setColour (textDim);
                    g->setFont (font (16.0f));
                    g->drawText (donate::pitch, lines, juce::Justification::centredLeft, true);
                }

                y += 56 + 18;
            }

            label ("PLAYING");
            paragraph ("The computer keyboard plays notes: A S D F G H J K L ; ' are the white keys, W E T Y U O P ] the black. "
                       "Z and X move an octave, C and V change velocity, Shift+M turns it on or off.",
                       font (16.0f), textDim);
            y += 12;

            label ("KEYS");
            // One key a row, in two columns of the same length (the right one used to stop where the left did,
            // and F11, F12 and Shift+I never showed)
            static const Key left[] { { "Space", "Play / stop (records when armed)" }, { "Shift+L", "Pattern / song" },
                                      { "Ctrl+M", "Metronome (right-click: the rest)" }, { "F4", "New pattern" },
                                      { "Shift + / -", "Next / previous pattern" }, { "F2", "Rename the selected channel" },
                                      { "Ctrl+K", "Find a sound" }, { "Ctrl+N", "New project" }, { "Ctrl+O", "Open a project" },
                                      { "Ctrl+S", "Save" }, { "Ctrl+R", "Export" }, { "Ctrl+Z", "Undo" }, { "Ctrl+Y", "Redo" },
                                      { "Shift+I", "Inspector" } };
            static const Key right[] { { "F5", "Arrange on its own" }, { "F6", "Steps" }, { "F7", "Piano roll" },
                                       { "F8", "AI drums" }, { "Shift+F8", "AI bass" }, { "Ctrl+F8", "AI guitar" }, { "Alt+F8", "AI keys" },
                                       { "Ctrl+Shift+F8", "AI synth" }, { "Shift+F12", "AI mastering" }, { "Ctrl+1 to 6", "The editor's tabs" },
                                       { "F9", "Mixer" }, { "F10", "Audio settings" },
#if JUCE_MAC
                                       { "Cmd+5", "Synth: Nova's sound" },     // F11 is the Mac's own Show Desktop
#else
                                       { "F11", "Synth: Nova's sound" },
#endif
                                       { "F12", "Master: how loud, ready to put out" } };

            const int half = w / 2;
            const auto capFont = font (15.0f, true);

            auto column = [&] (const Key* keys, int count, int x)
            {
                // The caps as wide as the column's widest, what they do after them
                int capWidth = 0;

                for (int i = 0; i < count; ++i)
                    capWidth = std::max (capWidth, (int) std::ceil (juce::GlyphArrangement::getStringWidth (capFont, shortcutText (keys[i].keys))) + 16);

                for (int i = 0; i < count && g != nullptr; ++i)
                {
                    const int top = y + i * rowHeight;
                    const auto label = shortcutText (keys[i].keys);
                    const int capW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (capFont, label)) + 16;
                    const auto cap = juce::Rectangle<int> (x, top + 2, capW, rowHeight - 5).toFloat();
                    g->setColour (raised);
                    g->fillRoundedRectangle (cap, 5.0f);
                    g->setColour (hairlineStrong);
                    g->drawRoundedRectangle (cap.reduced (0.5f), 5.0f, 1.0f);
                    g->setColour (text);
                    g->setFont (capFont);
                    g->drawText (label, cap.toNearestInt(), juce::Justification::centred, true);

                    g->setColour (textDim);
                    g->setFont (font (16.0f));
                    g->drawText (keys[i].does, x + capWidth + 10, top, half - capWidth - 22, rowHeight, juce::Justification::centredLeft, true);
                }
            };

            column (left, (int) std::size (left), 0);
            column (right, (int) std::size (right), half);
            y += rowHeight * (int) std::max (std::size (left), std::size (right));

            y += 12;
            paragraph ("Click a track or a channel to select it; double-click a clip to edit it, or a name to open its instrument.",
                       font (16.0f), textDim);
            y += 14;

            label ("EDITING");
            paragraph (shortcutText ("Click a note or a clip to select it, Shift+click to add more, or drag a box over them with Select "
                                     "(Ctrl+drag with any tool). Ctrl+C, Ctrl+X and Ctrl+V copy, cut and paste; Ctrl+B duplicates; "
                                     "Ctrl+A selects all; Delete deletes. Notes: Ctrl+Q quantizes them onto the snap; Up and Down move "
                                     "them a semitone (Shift: an octave); Left and Right a snap step. Clips: drag an edge to trim one; "
                                     "the split tool or Ctrl+T cuts it in two; a recording's corner dots fade it in and out."),
                       font (16.0f), textDim);
            y += 14;

            label ("AUTOMATION");
            paragraph ("Automation in Arrange's header shows a lane under each track and the master's at the bottom: click to add a "
                       "point, drag to move it, right-click to delete it. Right-click a fader or a knob to automate it. Song mode plays it.",
                       font (16.0f), textDim);
            y += 14;

            label ("MIDI KEYBOARDS");
            paragraph ("A USB MIDI keyboard plays the selected channel as soon as it's plugged in, with velocity, the sustain "
                       "pedal, pitch bend and the mod wheel; arm Record and press Play to record it into the pattern. Audio "
                       "settings lists each keyboard. Right-click a fader or a knob to link it to a knob on your controller.",
                       font (16.0f), textDim);
            y += 14;

            label ("TEMPO AND TIME SIGNATURE CHANGES");
            paragraph ("Right-click Arrange's ruler to change the tempo or the time signature from there. With Automation on, the "
                       "Tempo lane at the top takes points: click to add one, drag to move it, right-click one to glide to it. "
                       "TEMPO and TIME SIG in the top bar show and change what's playing where the song is.",
                       font (16.0f), textDim);
            y += 14;

            label ("CREDITS");
            paragraph ("Studio drum kits: drum samples provided by DrumGizmo.org (CC BY 4.0). AVL Drumkits by Glen MacArthur "
                       "(bandshed.net), CC BY-SA 3.0. Salamander Grand Piano by Alexander Holm, CC BY 3.0. Versilian Studios "
                       "Chamber Orchestra 2 Community Edition, CC0. The NAM Amp runs Neural Amp Modeler's core and "
                       "AudioDSPTools by Steven Atkinson (MIT), with Eigen (MPL 2.0) and nlohmann/json (MIT). On a Mac the "
                       "text is Selawik by Microsoft (SIL Open Font License 1.1), Segoe UI's open stand-in. The AI drummer's "
                       "feel is learned from the Groove MIDI Dataset by Jon Gillick, Adam Roberts, Jesse Engel, Douglas Eck "
                       "and David Bamman (Google Magenta), CC BY 4.0. The 808, 909, Nova and the AI drummer are NightShift's "
                       "own, and the AI drummer writes on this computer, offline.",
                       font (15.0f), textFaint);
            y += 14;

            label ("LICENCE");
            paragraph (licence::notice + " Built on Tracktion Engine (GPLv3) and JUCE (AGPLv3).", font (16.0f), textDim);

            return y + 8;
        }
    };

    /** AboutBody in a view that scrolls when the screen hasn't room for all of it (a laptop's), with
        room under it for the AlertWindow's buttons: it puts them a share of its height up from the
        bottom, which for a tall window reaches into what's above. */
    class AboutHolder : public juce::Component
    {
    public:
        explicit AboutHolder (int room)
        {
            view.setViewedComponent (&body, false);
            view.setScrollBarsShown (true, false);
            view.setSingleStepSizes (16, 48);
            addAndMakeVisible (view);

            const int shown = std::min (body.getHeight(), std::max (320, room));
            const int bar = shown < body.getHeight() ? view.getScrollBarThickness() + 8 : 0;
            setSize (body.getWidth() + bar, shown + buttonRoom);
        }

        void resized() override     { view.setBounds (getLocalBounds().withTrimmedBottom (buttonRoom)); }

    private:
        static constexpr int buttonRoom = 48;
        AboutBody body;
        juce::Viewport view;
    };

    /** About NightShift: an AlertWindow (its title, buttons, Enter and Esc) holding AboutBody. */
    class AboutWindow : public juce::AlertWindow
    {
    public:
        AboutWindow (juce::Component* over, int screenHeight)
            : juce::AlertWindow ("NightShift " JUCE_APPLICATION_VERSION_STRING, {}, juce::MessageBoxIconType::NoIcon, over),
              holder ((screenHeight > 0 ? screenHeight : screenHeightFor (over)) - 240)
        {
            addCustomComponent (&holder);
        }

    private:
        /** The height of the screen it opens on, less the taskbar (or the menu bar and dock): what
            the window, its title and buttons with it, has to fit in. */
        static int screenHeightFor (juce::Component* over)
        {
            auto& displays = juce::Desktop::getInstance().getDisplays();
            const auto* display = over != nullptr ? displays.getDisplayForRect (over->getScreenBounds()) : displays.getPrimaryDisplay();
            return display != nullptr ? (int) display->userBounds.getHeight() : 100000;
        }

        AboutHolder holder;
    };
}

void MainComponent::showAbout (int screenHeight)
{
    // Donate... (when there's a page to go to), Licence... and Close; Enter and Esc close it
    auto* window = new AboutWindow (this, screenHeight);
    enum { closeButton = 0, donateButton, licenceButton };

    if (donate::isSet())
    {
        window->addButton ("Donate...", donateButton);

        // Lit, as the start screen's Donate is
        if (auto* b = window->getButton ("Donate..."))
        {
            b->setColour (juce::TextButton::buttonColourId, accent);
            b->setColour (juce::TextButton::textColourOffId, background);
        }
    }

    window->addButton ("Licence...", licenceButton);
    window->addButton ("Close", closeButton, juce::KeyPress (juce::KeyPress::returnKey), juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([] (int result)
    {
        if (result == donateButton)
            donate::open();
        else if (result == licenceButton)
            licence::open();
    }), true);
}

void MainComponent::confirmDiscard (std::function<void()> carryOn)
{
    if (! song.hasUnsavedChanges())
    {
        carryOn();
        return;
    }

    // Save (Enter), Save as..., Don't save, Cancel (Esc). Saving carries on only once it's
    // written, so closing never loses work to a failed or abandoned save
    enum { cancel = 0, save, saveAs, dontSave };
    const auto name = currentFile != juce::File() ? "\"" + currentFile.getFileNameWithoutExtension() + "\"" : juce::String ("This project");

    auto* window = new juce::AlertWindow ("Save changes?", name + " has changes that haven't been saved.",
                                          juce::MessageBoxIconType::QuestionIcon, this);
    window->addButton (currentFile != juce::File() ? "Save" : "Save...", save, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Save as...", saveAs);
    window->addButton ("Don't save", dontSave);
    window->addButton ("Cancel", cancel, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> self (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([self, carryOn] (int result)
    {
        if (self == nullptr)
            return;

        if (result == save || result == saveAs)
            self->saveProject (result == saveAs, carryOn);
        else if (result == dontSave)
            carryOn();
    }), true);
}

void MainComponent::newProject()
{
    // A blank project, and the start screen over it for a template (as Logic's New does)
    confirmDiscard ([this]
    {
        audio.stop();
        currentFile = juce::File();
        song.loadBlank();
        showStartScreen();
    });
}

void MainComponent::showStartScreen()
{
    auto o = audio.getOptions();

    // The first time it shows (NightShift's first open, as a rule): NightShift is free, would you
    // donate? Asked once only; About has Donate after that
    const bool askForDonation = donate::isSet() && ! o.donationAsked;
    startScreen.setDonationShown (askForDonation);

    if (askForDonation)
    {
        o.donationAsked = true;
        audio.setOptions (o);
    }

    startScreen.setRecent (o.recentProjects);
    startScreen.setShowAtStartup (o.startScreen);
    startScreen.setBounds (getLocalBounds());
    startScreen.setVisible (true);
    startScreen.toFront (false);
    toast.toFront (false);

    juce::Component::SafePointer<StartScreen> screen (&startScreen);
    juce::MessageManager::callAsync ([screen]
    {
        if (screen != nullptr && screen->isShowing())
            screen->grabKeyboardFocus();
    });
}

void MainComponent::hideStartScreen()
{
    startScreen.setVisible (false);

    if (isShowing())
        grabKeyboardFocus();
}

void MainComponent::rememberProject (const juce::File& file)
{
    auto o = audio.getOptions();
    o.recentProjects.removeString (file.getFullPathName(), true);
    o.recentProjects.insert (0, file.getFullPathName());
    o.recentProjects.removeRange (8, o.recentProjects.size());
    audio.setOptions (o);
}

static juce::File projectsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Nightshift");
}

void MainComponent::openProject()
{
    confirmDiscard ([this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open project", projectsFolder(), "*.nsproj");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, dialog = ComputerKeyboard::openSystemDialog()] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile())
                loadProjectFile (fc.getResult());
        });
    });
}

void MainComponent::openProjectFile (const juce::File& file)
{
    // A request that arrives while a dialog is up (the unsaved-changes prompt, say) is
    // dropped rather than stacking a second prompt whose answer would hit the wrong project
    if (juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0)
        return;

    confirmDiscard ([this, file] { loadProjectFile (file); });
}

void MainComponent::loadProjectFile (const juce::File& file)
{
    audio.stop();

    if (song.load (file))
    {
        currentFile = file;
        rememberProject (file);
    }
    else
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't open project",
                                                file.getFileName() + " isn't a NightShift project.");
}

void MainComponent::saveProject (bool saveAs, std::function<void()> saved)
{
    auto write = [this, saved] (juce::File file)
    {
        audio.flushPluginStates();

        if (song.save (file))
        {
            currentFile = file;
            song.markSaved();
            rememberProject (file);

            if (saved != nullptr)
                saved();
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save",
                                                    "NightShift couldn't write " + file.getFullPathName()
                                                   #if JUCE_MAC
                                                    + "\n\nIf macOS refused NightShift access to Documents or the Desktop, allow it in "
                                                      "System Settings > Privacy & Security > Files and Folders, or save somewhere else.");
                                                   #else
                                                    + "\n\nIf Windows Security's Controlled folder access is on, it blocks "
                                                      "apps it doesn't know from Documents and the Desktop. Allow NightShift "
                                                      "there, or save somewhere else.");
                                                   #endif
        }
    };

    if (! saveAs && currentFile != juce::File())
    {
        write (currentFile);
        return;
    }

    projectsFolder().createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Save project", projectsFolder().getChildFile ("Untitled.nsproj"), "*.nsproj");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
                          [write, dialog = ComputerKeyboard::openSystemDialog()] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file != juce::File())
            write (file.withFileExtension ("nsproj"));
    });
}

namespace
{
    // How an export can be saved, in plain words
    struct ExportQuality { int bits; const char* name; const char* shortName; };

    const ExportQuality exportQualities[] {
        { 24, "Best quality (24-bit): for more mixing or mastering later",         "24-bit" },
        { 16, "CD quality (16-bit, smoothed with dither): CDs and most uploads",    "16-bit with dither" },
        { 32, "Studio float (32-bit): nothing lost, even above 0 dB",               "32-bit float" },
    };
}

void MainComponent::exportAudio()
{
    // How it should be saved first, then where
    audio.stop();
    auto* window = new juce::AlertWindow ("Export WAV", "How should it be saved?", juce::MessageBoxIconType::NoIcon, this);
    window->setName ("Export WAV");
    juce::StringArray names;
    int current = 0;

    for (int i = 0; i < (int) std::size (exportQualities); ++i)
    {
        names.add (exportQualities[i].name);

        if (exportQualities[i].bits == audio.getOptions().exportBits)
            current = i;
    }

    window->addComboBox ("quality", names);
    window->getComboBoxComponent ("quality")->setSelectedItemIndex (current, juce::dontSendNotification);
    window->addButton ("Choose where to save...", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
    {
        if (result != 1)
            return;

        const int index = juce::jlimit (0, (int) std::size (exportQualities) - 1, window->getComboBoxComponent ("quality")->getSelectedItemIndex());
        auto o = audio.getOptions();
        o.exportBits = exportQualities[index].bits;
        audio.setOptions (o);
        chooseExportFile (o.exportBits);
    }), true);
}

void MainComponent::chooseExportFile (int bits)
{
    const auto name = currentFile.existsAsFile() ? currentFile.getFileNameWithoutExtension() : juce::String ("NightShift export");
    chooser = std::make_unique<juce::FileChooser> ("Export WAV", projectsFolder().getChildFile (name + ".wav"), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, bits, dialog = ComputerKeyboard::openSystemDialog()] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file == juce::File())
            return;

        file = file.withFileExtension ("wav");
        juce::MouseCursor::showWaitCursor();
        const bool ok = audio.renderToFile (file, 2.0, bits);
        const auto loudness = ok ? mastering::measureFile (file) : mastering::Readings();
        juce::MouseCursor::hideWaitCursor();

        juce::AlertWindow::showMessageBoxAsync (ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                                ok ? "Export finished" : "Export failed",
                                                ok ? describeExport (file, bits, song.mode == PlayMode::song, loudness)
                                                   : "NightShift couldn't render to " + file.getFullPathName());
    });
}

juce::String MainComponent::describeExport (const juce::File& file, int bits, bool wholeSong, const mastering::Readings& loudness)
{
    juce::String quality = "24-bit";

    for (auto& q : exportQualities)
        if (q.bits == bits)
            quality = q.shortName;

    auto message = "Saved " + file.getFullPathName() + "\n" + quality + ", 48 kHz, " + (wholeSong ? "the whole song" : "the current pattern") + ".";

    if (loudness.integratedValid)
    {
        message << "\n\nLoudness " << juce::String (loudness.integratedLufs, 1) << " LUFS, peak " << juce::String (loudness.truePeakDb, 1) << " dB.";

        // Over -1 dB, the conversion to MP3 or AAC on the way to listeners can clip
        if (loudness.truePeakDb > -0.95f)
            message << "\nThe peak is above -1 dB, so streaming services may distort it a little when they convert it. "
                       "The Master tab (F12) keeps it under.";
    }

    return message;
}

void MainComponent::showAudioSettings()
{
    // One at a time: F10 again brings the open one forward
    if (audioSettingsWindow != nullptr)
    {
        audioSettingsWindow->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.content.setOwned (new AudioSettings (audio));
    audioSettingsWindow = options.launchAsync();
}

void MainComponent::scanPlugins()
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Plugins";
    options.dialogBackgroundColour = panel;
    options.useNativeTitleBar = true;
    options.resizable = true;

    auto deadMansPedal = paths::appData().getChildFile ("plugin-scan-crashes.txt");

    auto list = std::make_unique<juce::PluginListComponent> (audio.getPluginFormats(), audio.getKnownPlugins(),
                                                             deadMansPedal, nullptr, true);
    list->setSize (760, 520);
    options.content.setOwned (list.release());
    options.launchAsync();
}

void MainComponent::findNewPlugins (bool asked)
{
    sidebar.setScanning (true);

    audio.findNewPlugins ([safe = juce::Component::SafePointer<MainComponent> (this), asked] (const juce::StringArray& names)
    {
        if (safe == nullptr)
            return;

        safe->sidebar.setScanning (false);
        const auto& list = safe->audio.getKnownPlugins();

        // Only when you pressed Scan: at start the sounds' groups all stay shut. Their developers open
        // too, so the new ones show
        if (asked)
            for (auto& d : list.getTypes())
                if (d.isInstrument && names.contains (d.name))
                {
                    safe->sidebar.showPluginInstruments (names);
                    break;
                }

        if (const auto message = describeNewPlugins (names, list, asked); message.isNotEmpty())
            safe->showToast (message);
    });
}

juce::String MainComponent::describeNewPlugins (const juce::StringArray& names, const juce::KnownPluginList& list, bool asked)
{
    juce::StringArray instruments, effects;
    int have = 0;

    for (auto& d : list.getTypes())
        have += d.isInstrument ? 1 : 0;

    for (auto& name : names)
    {
        bool instrument = false;

        for (auto& d : list.getTypes())
            instrument = instrument || (d.name == name && d.isInstrument);

        (instrument ? instruments : effects).add (name);
    }

    auto some = [] (const juce::StringArray& found, const char* one, const char* many)
    {
        return juce::String (found.size()) + " new " + (found.size() == 1 ? one : many) + ": " + found.joinIntoString (", ");
    };

    if (names.isEmpty())
        return asked ? "Nothing new found (you have " + juce::String (have) + (have == 1 ? " instrument" : " instruments")
                         + "). Installed one in an unusual folder? Right-click Find new instruments to add that folder"
                     : juce::String();

    if (instruments.isEmpty())
        return "Found " + some (effects, "effect", "effects") + " (no new instruments)";

    return "Found " + some (instruments, "instrument", "instruments")
             + (effects.isEmpty() ? juce::String() : ", and " + some (effects, "effect", "effects"));
}

} // namespace nightshift
