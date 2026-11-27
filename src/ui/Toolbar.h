// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "model/Song.h"
#include "Widgets.h"

namespace nightshift
{

/** The top bar: the app and project, Pattern | Song, the transport, the readouts (bar,
    time, tempo, time signature), and on the right the audio interface with its latency,
    CPU, the master meter and volume, Export and the menu. It gives up the project name,
    CPU and time signature, in that order, when the window is narrow.
*/
class Toolbar : public juce::Component,
                public juce::TooltipClient,
                private Song::Listener
{
public:
    Toolbar (Song&, AudioEngine&);
    ~Toolbar() override;

    void refresh();
    void setProjectName (const juce::String& name, bool hasUnsavedChanges);

    std::function<void()> onMenu, onExport, onPlay, onRecord, onRecordMenu, onAudioSettings, onKeyboard;

    /** The record automation switch beside Record was clicked. */
    std::function<void()> onRecordAutomation;

    /** Keys pressed in the metronome's panel, handed on (to the studio's shortcuts). */
    std::function<bool (const juce::KeyPress&)> onKey;

    /** The metronome's panel, under its button (what right-clicking it does). */
    void showMetronomePanel();
    juce::Component::SafePointer<juce::Component> metronomePanel;

    /** The PC keyboard's switch: lit while it plays the selected channel; its tooltip says
        which notes and how hard. */
    void setKeyboard (bool on, const juce::String& range);

    /** Notes are being played on it: the switch lights up fully. */
    void setKeyboardPlaying (bool);

    /** MIDI keyboards: a light beside the PC keyboard's switch, there while one is plugged in and
        lit as it plays; its tooltip names them and the channel they play. Click: Audio settings. */
    void setMidi (const juce::StringArray& inputs, const juce::String& playing);
    void setMidiLit (bool);

    /** The time signatures to choose from, ticking the one playing now; choosing one changes that
        (the song position's stretch in Song mode, the pattern's in Pattern mode). */
    static juce::PopupMenu createMeterMenu (Song&, AudioEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    static constexpr int height = 60;

private:
    Song& song;
    AudioEngine& audio;

    SegmentedControl modes { { "Pattern", "Song" }, SegmentedControl::Style::pill };
    IconButton stopButton { "Stop", theme::icons::stop() },
               playButton { "Play", theme::icons::play(), theme::accent },
               recordButton { "Record", theme::icons::record(), theme::danger },
               automationRecordButton { "Record automation", theme::icons::automation(), theme::danger },
               loopButton { "Loop", theme::icons::loop(), theme::accentCool },
               metronomeButton { "Metronome", theme::icons::metronomeLine(), theme::accentCool },
               keyboardButton { "Computer keyboard", theme::icons::keyboard(), theme::accentCool },
               exportButton { "Export", theme::icons::exportLine() },
               menuButton { "Menu", theme::icons::more() };

    struct MidiLight : public juce::Button
    {
        MidiLight() : juce::Button ("MIDI") {}
        bool lit = false;
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    MidiLight midiLight;
    static constexpr int midiLightWidth = 50;

    ValueBox tempo { 20.0, 400.0, 0.01, 2 };
    juce::Slider master { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    LevelMeter meter;
    juce::Image logoImage;

    juce::Rectangle<int> brandArea, transportArea, readoutArea, latencyArea, cpuArea;
    std::vector<juce::Rectangle<int>> readouts;
    int transportGapX = 0;
    juce::String projectName { "Untitled" }, positionText { "1.1.1" }, clockText { "0:00.00" }, latencyText, latencyShort;
    juce::String meterText;                 // the time signature playing now

    /** The tempo where the song is (Song mode) or the start's (Pattern mode). */
    double tempoNow() const;
    bool unsaved = false, showProject = true, showCpu = true, showSignature = true, latencyCompact = false;
    juce::Point<int> lastMouse;
    int latencyState = 0, cpuPercent = 0;   // latency: 0 no audio, 1 high, 2 low
    bool masterGrabbed = false;             // the master slider held by the mouse (not the wheel)
    bool automationWriting = false;         // recording automation, and the song playing in Song mode

    void songChanged (Song::Change) override;
    void syncFromSong();
    void toggleLoop();
};

} // namespace nightshift
