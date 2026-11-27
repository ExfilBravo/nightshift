// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Toolbar.h"
#include "MetronomePanel.h"

namespace nightshift
{

using namespace theme;

namespace
{
    constexpr int buttonSize = 34, groupHeight = 44, readoutLabelHeight = 18;
    const char* const readoutLabels[] = { "BAR", "TIME", "TEMPO", "TIME SIG" };
    constexpr int readoutWidths[] = { 92, 104, 116, 84 }, narrowReadoutWidths[] = { 80, 96, 106, 84 };
}

Toolbar::Toolbar (Song& s, AudioEngine& a)
    : song (s), audio (a), logoImage (logo())
{
    stopButton.setTooltip ("Stop and return to the start");
    playButton.setTooltip ("Play / stop (Space). With Record armed, Play records");
    recordButton.setTooltip ("Record: arm it, then Play (Space) records what you play on the selected channel and every armed audio track. "
                             "Armed while playing, it punches in. Right-click for the countdown, blend and more");
    automationRecordButton.setTooltip ("Record automation: while the song plays in Song mode, moving a fader, a knob, a plugin's control or a "
                                       "linked MIDI knob writes it into its lane in Arrange, from where you grab it to where you let go");
    loopButton.setTooltip ("Loop: play the loop range round and round (drag along the top of the Arrange ruler to set it)");
    metronomeButton.setTooltip (shortcutText ("Metronome on / off (Ctrl+M). Right-click for its sound, accents, rhythm, tap tempo and practise"));
    exportButton.setTooltip (shortcutText ("Export a WAV (Ctrl+R)"));
    menuButton.setTooltip ("File, edit and settings");
    modes.setTooltip ("Pattern plays the current pattern on a loop; Song plays the arrangement (Shift+L)");

    playButton.setSolidWhenOn (true);
    playButton.setIconColour (text);
    recordButton.setSolidWhenOn (true);
    recordButton.setIconColour (danger);

    for (auto* b : { &automationRecordButton, &loopButton, &metronomeButton, &keyboardButton, &exportButton, &menuButton })
        b->setFilled (false);

    exportButton.setFramed (true);
    exportButton.setLabel ("Export");

    for (auto* b : { &stopButton, &playButton, &recordButton, &automationRecordButton, &loopButton, &metronomeButton, &keyboardButton,
                     &exportButton, &menuButton })
        addAndMakeVisible (*b);

    automationRecordButton.onClick = [this] { if (onRecordAutomation != nullptr) onRecordAutomation(); refresh(); };

    keyboardButton.onClick = [this] { if (onKeyboard != nullptr) onKeyboard(); };

    // MIDI keyboards' light: shown while one is plugged in (MainComponent says)
    addChildComponent (midiLight);
    midiLight.onClick = [this] { if (onAudioSettings != nullptr) onAudioSettings(); };

    menuButton.onClick = [this] { if (onMenu != nullptr) onMenu(); };
    playButton.onClick = [this] { if (onPlay != nullptr) onPlay(); else audio.togglePlay(); refresh(); };
    stopButton.onClick = [this] { audio.stop(); refresh(); };
    recordButton.onClick = [this] { if (onRecord != nullptr) onRecord(); refresh(); };
    recordButton.onRightClick = [this] { if (onRecordMenu != nullptr) onRecordMenu(); };
    metronomeButton.onRightClick = [this] { showMetronomePanel(); };
    loopButton.onClick = [this] { toggleLoop(); };
    exportButton.onClick = [this] { if (onExport != nullptr) onExport(); };

    metronomeButton.onClick = [this]
    {
        auto o = audio.getOptions();
        o.metronome = ! o.metronome;
        audio.setOptions (o);
        refresh();
    };

    modes.onChange = [this] (int index)
    {
        const auto m = index == 0 ? PlayMode::pattern : PlayMode::song;

        if (song.mode != m)
        {
            song.mode = m;
            song.notify (Song::Change::transport);
        }
    };
    addAndMakeVisible (modes);

    tempo.setCaption ("bpm");
    tempo.onDragStart = [this] { song.checkpoint(); };
    // The tempo playing now: the start's, or in Song mode the change in effect where the song is
    tempo.onChange = [this] (double bpm) { audio.setTempoNow (bpm); };
    addAndMakeVisible (tempo);

    master.setRange (0.0, 1.0);
    master.setDoubleClickReturnValue (true, 0.8);
    master.setWantsKeyboardFocus (false);
    master.setMouseClickGrabsKeyboardFocus (false);
    master.setPopupDisplayEnabled (true, true, nullptr, 1200);
    master.setTooltip ("Master volume (double-click for 0 dB)");
    master.textFromValueFunction = [] (double v)
    {
        const float dbValue = masterVolumeToDb ((float) v);
        return "Master " + (dbValue <= -99.0f ? juce::String ("-inf") : juce::String (dbValue, 1)) + " dB";
    };
    // Recording automation: held, its moves write the master's volume lane until it's let go (the
    // wheel comes as a drag of its own each notch: a change with nothing held)
    master.onDragStart = [this]
    {
        song.checkpoint();
        masterGrabbed = master.isMouseButtonDown();
        audio.touchControl (0, "volume", true, masterGrabbed);
    };
    master.onDragEnd = [this]
    {
        if (std::exchange (masterGrabbed, false))
            audio.touchControl (0, "volume", false);
    };
    master.onValueChange = [this]
    {
        song.masterVolume = (float) master.getValue();
        song.notify (Song::Change::mixer);
    };
    addAndMakeVisible (master);

    meter.setTooltip ("Master level (click to clear the clip light)");
    addAndMakeVisible (meter);

    song.addListener (this);
    syncFromSong();
}

Toolbar::~Toolbar()
{
    song.removeListener (this);
}

void Toolbar::songChanged (Song::Change)
{
    syncFromSong();
}

void Toolbar::syncFromSong()
{
    modes.setSelected (song.mode == PlayMode::pattern ? 0 : 1);

    if (! tempo.isEditing())
        tempo.setValue (tempoNow());

    if (! master.isMouseButtonDown())
        master.setValue (audio.getAutomatedControl (0, "volume").value_or (song.masterVolume), juce::dontSendNotification);

    loopButton.setToggleState (song.hasLoop(), juce::dontSendNotification);
    repaint();
}

void Toolbar::toggleLoop()
{
    // Turning it on with no range yet loops four bars from the bar the playhead is in
    if (song.loopEnd <= song.loopStart)
    {
        const int bar = song.barAt (std::max (0.0, audio.getPlayheadTicks()));
        song.loopStart = song.barStart (bar);
        song.loopEnd = song.barStart (bar + 4);
        song.loopOn = true;
    }
    else
    {
        song.loopOn = ! song.loopOn;
    }

    song.notify (Song::Change::transport);
}

void Toolbar::setKeyboard (bool on, const juce::String& range)
{
    keyboardButton.setToggleState (on, juce::dontSendNotification);
    keyboardButton.setTooltip (on ? "Computer keyboard on (Shift+M): A S D F G H J K L ; ' are the white keys, W E T Y U O P ] the black ones, playing the selected channel, "
                                      + range + ". Z/X octave, C/V velocity. Arm Record, then Space records"
                                  : juce::String ("Computer keyboard off (Shift+M): click to play the selected channel from your computer keyboard"));
}

void Toolbar::showMetronomePanel()
{
    auto content = std::make_unique<MetronomePanel> (song, audio);
    content->onKey = onKey;
    metronomePanel = content.get();
    juce::CallOutBox::launchAsynchronously (std::move (content), metronomeButton.getScreenBounds(), nullptr);
}

void Toolbar::setKeyboardPlaying (bool playing)
{
    keyboardButton.setSolidWhenOn (playing);
}

void Toolbar::setMidi (const juce::StringArray& inputs, const juce::String& playing)
{
    midiLight.setTooltip ("MIDI: " + inputs.joinIntoString (", ")
                          + (playing.isNotEmpty() ? ". It plays " + playing + " (the selected channel)" : juce::String (". Select an instrument channel to play"))
                          + ". Click for its settings");

    if (inputs.isEmpty() == midiLight.isVisible())
    {
        midiLight.setVisible (! inputs.isEmpty());
        resized();
        repaint();
    }
}

void Toolbar::setMidiLit (bool lit)
{
    if (midiLight.lit != lit)
    {
        midiLight.lit = lit;
        midiLight.repaint();
    }
}

void Toolbar::MidiLight::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    using namespace theme;
    auto r = getLocalBounds().toFloat().reduced (1.0f, 2.0f);
    g.setColour (lit ? accentCool : (highlighted ? raised : panelLight));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (lit ? background : textDim);
    g.setFont (font (16.0f, true));
    g.drawText ("MIDI", r, juce::Justification::centred, true);
}

void Toolbar::setProjectName (const juce::String& name, bool hasUnsavedChanges)
{
    projectName = name;
    unsaved = hasUnsavedChanges;
    repaint (brandArea);
}

void Toolbar::refresh()
{
    playButton.setToggleState (audio.isPlaying(), juce::dontSendNotification);
    metronomeButton.setToggleState (audio.getOptions().metronome, juce::dontSendNotification);

    // Armed: tinted. Recording: solid, blinking through the count-in or while it waits for the
    // first note
    const bool recording = audio.isRecording() || (audio.isRecordingNotes() && ! audio.isWaitingForInput());
    const bool blinking = audio.isCountingIn() || audio.isWaitingForInput();
    const bool blinkOff = blinking && (juce::Time::getMillisecondCounter() / 250) % 2 == 1;
    recordButton.setToggleState (audio.isRecordArmed() || recording, juce::dontSendNotification);
    recordButton.setSolidWhenOn ((recording || audio.isWaitingForInput()) && ! blinkOff);

    // Recording automation: switched on, tinted; solid while it writes (the song playing in Song mode)
    automationRecordButton.setToggleState (audio.isAutomationRecording(), juce::dontSendNotification);

    if (const bool writing = audio.isWritingAutomation(); writing != automationWriting)
    {
        automationWriting = writing;
        automationRecordButton.setSolidWhenOn (writing);
    }

    // The master volume where its automation has it while that plays (not while it's held)
    if (! master.isMouseButtonDown())
        master.setValue (audio.getAutomatedControl (0, "volume").value_or (song.masterVolume), juce::dontSendNotification);

    // What you hear lags what you play by the round trip: show it, green when it's low
    const auto info = audio.getLatencyInfo();
    juce::String newLatency;
    int newState = 0;

    latencyShort = info.driver.isEmpty() ? juce::String ("No audio") : juce::String (info.roundTripMs(), 1) + " ms";

    if (info.driver.isEmpty())
    {
        newLatency = "No audio device";
    }
    else
    {
        const auto device = info.device.containsIgnoreCase (AudioEngine::preferredInterface) ? juce::String (AudioEngine::preferredInterface)
                          : info.driver == AudioEngine::lowLatencyDriver ? juce::String (AudioEngine::lowLatencyName)
                          : juce::String (AudioEngine::systemAudioName);
        newLatency = device + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + juce::String (info.bufferSize)
                   + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + juce::String (info.roundTripMs(), 1) + " ms";
        newState = info.roundTripMs() <= 10.0 ? 2 : 1;
    }

    const int newCpu = juce::roundToInt (info.cpu * 100.0);

    if (newLatency != latencyText || newState != latencyState || newCpu != cpuPercent)
    {
        const bool resize = newLatency.length() != latencyText.length();
        latencyText = newLatency;
        latencyState = newState;
        cpuPercent = newCpu;

        if (resize)
            resized();

        repaint (latencyArea.getUnion (cpuArea));
    }

    // Bar.beat.step, and the clock to the hundredth. Beats of the time signature playing (eighths
    // in 7/8), 16ths within them; Song mode's bars as long as the time signature where they are
    const double ticks = std::max (0.0, audio.getPlayheadTicks());
    const auto [beats, unit] = audio.getMeterNow();
    const bool songMode = song.mode == PlayMode::song;
    const int beatTicks = ticksPerBeat * 4 / unit;
    const int bar = songMode ? song.barAt (ticks) : (int) std::floor (ticks / (beats * beatTicks));
    const double inBar = ticks - (songMode ? song.barStart (bar) : bar * beats * beatTicks);
    const int beat = (int) (inBar / beatTicks) + 1;
    const int step = (int) std::fmod (inBar / ticksPerStep, std::max (1.0, (double) beatTicks / ticksPerStep)) + 1;
    positionText = juce::String (bar + 1) + "." + juce::String (beat) + "." + juce::String (step);

    // The tempo where the song is (it changes as it plays through its changes)
    if (const double now = tempoNow(); ! tempo.isEditing() && std::abs (tempo.getValue() - now) > 0.004)
        tempo.setValue (now);

    if (const auto meterNow = juce::String (beats) + "/" + juce::String (unit); meterNow != meterText)
    {
        meterText = meterNow;
        repaint();
    }

    const double seconds = songMode ? song.secondsAt (ticks) : ticks / ticksPerBeat * 60.0 / song.bpm;
    const int minutes = (int) (seconds / 60.0);
    clockText = juce::String (minutes) + ":" + juce::String (seconds - minutes * 60.0, 2).paddedLeft ('0', 5);

    auto [left, right] = audio.getMasterPeak();
    meter.push (left, right);

    if (! readouts.empty())
        repaint (readouts[0].getUnion (readouts[1]));
}

//==============================================================================
void Toolbar::resized()
{
    const int w = getWidth();
    showProject = w >= 1500;
    showCpu = w >= 1400;
    showSignature = w >= 1260;

    auto r = getLocalBounds().reduced (16, 0);
    const int cy = r.getCentreY();
    auto centred = [cy] (juce::Rectangle<int> area, int h) { return area.withHeight (h).withY (cy - h / 2); };

    // Left: brand, modes, transport, readouts
    brandArea = r.removeFromLeft (showProject ? 248 : 140);
    r.removeFromLeft (12);

    modes.setBounds (centred (r.removeFromLeft (modes.getIdealWidth()), 36));
    r.removeFromLeft (14);

    const int midiWidth = midiLight.isVisible() ? 4 + midiLightWidth : 0;
    transportArea = centred (r.removeFromLeft (4 + 7 * buttonSize + 5 * 4 + 17 + 4 + midiWidth), groupHeight);
    {
        auto t = transportArea.reduced (4, 5);
        for (auto* b : { &stopButton, &playButton, &recordButton, &automationRecordButton })
        {
            b->setBounds (t.removeFromLeft (buttonSize));
            t.removeFromLeft (4);
        }

        t.removeFromLeft (4);
        transportGapX = t.getX() + 4;
        t.removeFromLeft (9);
        loopButton.setBounds (t.removeFromLeft (buttonSize));
        t.removeFromLeft (4);
        metronomeButton.setBounds (t.removeFromLeft (buttonSize));
        t.removeFromLeft (4);
        keyboardButton.setBounds (t.removeFromLeft (buttonSize));

        if (midiLight.isVisible())
        {
            t.removeFromLeft (4);
            midiLight.setBounds (t.removeFromLeft (midiLightWidth));
        }
    }
    r.removeFromLeft (14);

    readouts.clear();
    const auto* widths = showSignature ? readoutWidths : narrowReadoutWidths;
    int readoutTotal = 0;

    for (int i = 0; i < (showSignature ? 4 : 3); ++i)
        readoutTotal += widths[i];

    readoutArea = centred (r.removeFromLeft (readoutTotal), groupHeight + 4);

    {
        auto ro = readoutArea;
        for (int i = 0; i < (showSignature ? 4 : 3); ++i)
            readouts.push_back (ro.removeFromLeft (widths[i]));
    }

    tempo.setBounds (readouts[2].reduced (14, 0).withTrimmedTop (readoutLabelHeight + 2).withHeight (22));

    // Right: menu, export, meter and master, CPU, latency
    menuButton.setBounds (centred (r.removeFromRight (34), 34));
    r.removeFromRight (8);

    const bool exportLabel = w >= 1200;
    exportButton.setLabel (exportLabel ? "Export" : "");
    exportButton.setBounds (centred (r.removeFromRight (exportLabel ? 104 : 36), 34));
    r.removeFromRight (16);

    auto meterColumn = centred (r.removeFromRight (showSignature ? 100 : 76), 32);
    meter.setBounds (meterColumn.removeFromTop (12));
    master.setBounds (meterColumn.withTrimmedTop (2).expanded (5, 0));
    r.removeFromRight (showSignature ? 14 : 10);

    cpuArea = showCpu ? r.removeFromRight (78) : juce::Rectangle<int>();

    if (showCpu)
        r.removeFromRight (8);

    // The whole story when there's room, else just the milliseconds
    auto pillWidth = [] (const juce::String& s) { return (int) juce::GlyphArrangement::getStringWidth (font (16.5f, true), s) + 40; };
    latencyCompact = pillWidth (latencyText) > r.getWidth();
    const int width = std::min (pillWidth (latencyCompact ? latencyShort : latencyText), std::max (0, r.getWidth()));
    latencyArea = centred (r.removeFromRight (width), 32);
}

juce::String Toolbar::getTooltip()
{
    if (latencyArea.contains (lastMouse))
        return latencyText + " round trip (buffer size and latency). Click for audio settings (F10).";

    if (brandArea.contains (lastMouse))
        return "File, edit and settings";

    if (showSignature && readouts.size() > 3 && readouts[3].contains (lastMouse))
        return "The time signature: click to change it (7/8, 6/8, 5/4...)";

    return {};
}

void Toolbar::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // Brand: the moon, the name, and the project
    {
        auto b = brandArea;
        auto mark = b.removeFromLeft (30).withSizeKeepingCentre (30, 30);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logoImage, mark.toFloat());
        b.removeFromLeft (11);

        g.setColour (text);
        g.setFont (font (20.0f, true));
        const int nameWidth = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), "NightShift") + 2;
        g.drawText ("NightShift", b.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);

        if (showProject)
        {
            b.removeFromLeft (13);
            g.setColour (hairlineStrong);
            g.fillRect (b.getX(), b.getCentreY() - 9, 1, 18);
            b.removeFromLeft (13);

            g.setColour (textFaint);
            g.setFont (font (17.5f));
            const int projectWidth = std::min (b.getWidth() - 14, (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), projectName) + 2);
            g.drawText (projectName, b.removeFromLeft (projectWidth), juce::Justification::centredLeft, true);

            if (unsaved)
            {
                g.setColour (accent);
                g.fillEllipse ((float) b.getX() + 5.0f, (float) b.getCentreY() - 2.5f, 5.0f, 5.0f);
            }
        }
    }

    auto group = [&g] (juce::Rectangle<int> area)
    {
        auto f = area.toFloat().reduced (0.5f);
        g.setColour (panelLight);
        g.fillRoundedRectangle (f, 11.0f);
        g.setColour (hairline);
        g.drawRoundedRectangle (f, 11.0f, 1.0f);
    };

    // Transport, with a divider between the playing buttons and the toggles
    group (transportArea);
    g.setColour (hairlineStrong);
    g.fillRect (transportGapX, transportArea.getCentreY() - 10, 1, 20);

    // Readouts: a small label over each value
    group (readoutArea);

    for (int i = 0; i < (int) readouts.size(); ++i)
    {
        auto ro = readouts[(size_t) i].reduced (14, 0);

        if (i > 0)
        {
            g.setColour (hairline);
            g.fillRect (readouts[(size_t) i].getX(), readoutArea.getY() + 1, 1, readoutArea.getHeight() - 2);
        }

        g.setColour (textFaint);
        g.setFont (labelFont());
        g.drawText (readoutLabels[i], ro.withTrimmedTop (7).withHeight (12), juce::Justification::centredLeft, true);

        auto value = ro.withTrimmedTop (readoutLabelHeight + 2).withHeight (22);
        g.setFont (monoFont (22.0f, true));

        if (i == 0)
        {
            g.setColour (accent);
            g.drawText (positionText, value, juce::Justification::centredLeft, true);
        }
        else if (i == 1)
        {
            g.setColour (text);
            g.drawText (clockText, value, juce::Justification::centredLeft, true);
        }
        else if (i == 3)
        {
            g.setColour (text);
            g.drawText (meterText.isNotEmpty() ? meterText : song.meterName(), value, juce::Justification::centredLeft, true);
        }
    }

    // Audio interface and latency, as a pill: green when it's low enough to play through
    if (! latencyArea.isEmpty())
    {
        const auto tint = latencyState == 2 ? ledOn : latencyState == 1 ? accent : danger;
        auto p = latencyArea.toFloat().reduced (0.5f);
        g.setColour (tint.withAlpha (0.10f));
        g.fillRoundedRectangle (p, p.getHeight() * 0.5f);
        g.setColour (tint.withAlpha (0.2f));
        g.drawRoundedRectangle (p, p.getHeight() * 0.5f, 1.0f);

        auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ p.getX() + 15.0f, p.getCentreY() });
        g.setColour (tint.withAlpha (0.35f));
        g.fillEllipse (dot.expanded (3.0f));
        g.setColour (tint);
        g.fillEllipse (dot);

        g.setColour (tint.interpolatedWith (juce::Colours::white, 0.45f));
        g.setFont (font (16.5f, true));
        g.drawText (latencyCompact ? latencyShort : latencyText, latencyArea.withTrimmedLeft (26).withTrimmedRight (10),
                    juce::Justification::centredLeft, true);
    }

    if (showCpu)
    {
        g.setFont (font (16.5f));
        g.setColour (textFaint);
        g.drawText ("CPU", cpuArea.withWidth (34), juce::Justification::centredLeft, true);
        g.setColour (textDim);
        g.setFont (monoFont (16.5f, true));
        g.drawText (juce::String (cpuPercent) + "%", cpuArea.withTrimmedLeft (34), juce::Justification::centredLeft, true);
    }
}

void Toolbar::mouseDown (const juce::MouseEvent& e)
{
    if (latencyArea.contains (e.getPosition()) && onAudioSettings != nullptr)
        onAudioSettings();
    else if (brandArea.contains (e.getPosition()) && onMenu != nullptr)
        onMenu();
    else if (showSignature && readouts.size() > 3 && readouts[3].contains (e.getPosition()))
        createMeterMenu (song, audio).showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (readouts[3])));
}

double Toolbar::tempoNow() const
{
    return audio.getTempoNow();
}

juce::PopupMenu Toolbar::createMeterMenu (Song& song, AudioEngine& audio)
{
    // The time signature playing now changes: in Song mode the stretch the song position is in
    // (the start's, or a change's from its bar), in Pattern mode the pattern's own if it has one
    // (else the song's at the start, which it plays in)
    const auto [nowBeats, nowUnit] = audio.getMeterNow();
    const bool songMode = song.mode == PlayMode::song;
    const int change = songMode ? song.meterChangeAt (std::max (0.0, audio.getPlayheadTicks())) : -1;
    const int changeTick = change >= 0 ? song.meterChanges[(size_t) change].tick : 0;
    const auto* pattern = song.findPattern (song.currentPatternId);
    const int patternId = pattern != nullptr ? pattern->id : 0;
    const bool ownMeter = ! songMode && pattern != nullptr && pattern->meterBeats > 0;

    auto meterItems = [&song] (juce::PopupMenu& into, int tickedBeats, int tickedUnit, std::function<void (int, int)> set)
    {
        int lastUnit = 4;

        for (auto [beats, unit] : Song::meters())
        {
            if (unit != lastUnit)
                into.addSeparator();

            lastUnit = unit;
            into.addItem (juce::String (beats) + "/" + juce::String (unit), true, tickedBeats == beats && tickedUnit == unit,
                          [&song, set, beats = beats, unit = unit]
                          {
                              song.checkpoint();
                              set (beats, unit);
                              song.notify (Song::Change::patterns);
                              song.notify (Song::Change::transport);
                          });
        }
    };

    juce::PopupMenu menu;
    menu.addSectionHeader (change >= 0 ? "Time signature from bar " + juce::String (song.barAt (changeTick) + 1)
                                       : ownMeter ? "Time signature of " + pattern->name : juce::String ("Time signature"));

    meterItems (menu, nowBeats, nowUnit, [&audio] (int beats, int unit) { audio.setMeterNow (beats, unit); });

    // In Song mode past the first bar: another time signature from the bar the song is in on
    if (songMode)
    {
        const int bar = song.barAt (std::max (0.0, audio.getPlayheadTicks()));
        const int barTick = song.barStart (bar);

        if (bar > 0 && barTick != changeTick)
        {
            juce::PopupMenu from;
            meterItems (from, 0, 0, [&song, barTick] (int beats, int unit) { song.setMeterAt (barTick, beats, unit); });
            menu.addSeparator();
            menu.addSubMenu ("From bar " + juce::String (bar + 1) + " on", from);
        }
    }

    // In Pattern mode, a time signature for this pattern alone (a 7/8 part in a 4/4 song)
    if (! songMode && pattern != nullptr)
    {
        juce::PopupMenu own;
        meterItems (own, pattern->meterBeats, pattern->meterUnit, [&song, patternId] (int beats, int unit)
        {
            if (auto* p = song.findPattern (patternId))
            {
                const bool start = beats == song.meterBeats && unit == song.meterUnit;
                p->meterBeats = start ? 0 : beats;
                p->meterUnit = start ? 0 : unit;
            }
        });

        menu.addSeparator();
        menu.addSubMenu ("Only for " + pattern->name, own);
    }

    return menu;
}

void Toolbar::mouseMove (const juce::MouseEvent& e)
{
    lastMouse = e.getPosition();
    const bool clickable = latencyArea.contains (e.getPosition()) || brandArea.contains (e.getPosition())
                            || (showSignature && readouts.size() > 3 && readouts[3].contains (e.getPosition()));
    setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

} // namespace nightshift
