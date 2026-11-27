// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "AudioSettings.h"
#include "TextAudit.h"
#include "Theme.h"

namespace nightshift
{

using namespace theme;

namespace
{
    bool isPreferredInterface (juce::AudioIODevice* device)
    {
        return device != nullptr && device->getTypeName() == AudioEngine::lowLatencyDriver
                && device->getName().containsIgnoreCase (AudioEngine::preferredInterface);
    }
}

AudioSettings::AudioSettings (AudioEngine& a)
    : audio (a),
      selector (a.getDeviceManager(), 0, 8, 2, 8, false, false, false, false)
{
    addAndMakeVisible (selector);

    lowLatency.setTooltip ("Plugins that look ahead (the master limiter, oversampled effects) delay everything you hear. "
                           "While you monitor an armed track they are switched off, so you hear yourself as soon as the interface allows.");
    preferInterface.setTooltip ("NightShift is set up around the Arturia MiniFuse. With this on it opens the MiniFuse on "
                                + juce::String (AudioEngine::lowLatencyName) + " at startup.");
    useInterface.setTooltip ("Switch to the MiniFuse on " + juce::String (AudioEngine::lowLatencyName) + " now, at 32 samples");

    for (auto* b : { &lowLatency, &preferInterface })
    {
        b->setColour (juce::ToggleButton::textColourId, text);
        b->setColour (juce::ToggleButton::tickColourId, accent);
        addAndMakeVisible (*b);
        b->onClick = [this] { applyOptions(); };
    }

    addAndMakeVisible (useInterface);
    useInterface.onClick = [this]
    {
        if (! audio.openPreferredInterface())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MiniFuse not found",
                                                   #if JUCE_MAC
                                                    "NightShift couldn't open the MiniFuse. Check it's plugged in and switched on.");
                                                   #else
                                                    "NightShift couldn't open the MiniFuse ASIO driver. Check the MiniFuse is "
                                                    "plugged in and that MiniFuse Control Center is installed.");
                                                   #endif
        refreshReadout();
    };

    const auto o = audio.getOptions();
    lowLatency.setToggleState (o.lowLatencyMonitoring, juce::dontSendNotification);
    preferInterface.setToggleState (o.preferInterface, juce::dontSendNotification);

    audio.getDeviceManager().addChangeListener (this);

    // The selector sizes itself to its rows (an ASIO interface lists every channel), and
    // the window follows it
    selector.setSize (width, 380);
    selector.addComponentListener (this);
    refreshMidiInputs();
    refreshMidiOutputs();
    setSize (width, wantedHeight());
    refreshReadout();
    startTimerHz (4);
}

void AudioSettings::refreshMidiInputs()
{
    // A switch for each MIDI input (they come and go as they're plugged in)
    const auto names = audio.getMidiInputNames();

    if (names == midiNames && midiSwitches.size() == names.size())
        return;

    midiNames = names;
    midiSwitches.clear();

    for (auto& name : names)
    {
        auto* b = midiSwitches.add (new juce::ToggleButton (name));
        b->setColour (juce::ToggleButton::textColourId, text);
        b->setColour (juce::ToggleButton::tickColourId, accent);
        b->setToggleState (audio.isMidiInputEnabled (name), juce::dontSendNotification);
        b->setTooltip ("On: " + name + " plays the selected channel, records when Record is armed, and its knobs can be linked to faders and knobs");
        b->onClick = [this, b, name] { audio.setMidiInputEnabled (name, b->getToggleState()); };
        addAndMakeVisible (b);
    }

    if (getHeight() != wantedHeight())
        setSize (width, wantedHeight());

    resized();
    repaint();
}

//==============================================================================
AudioSettings::OutputRow::OutputRow (AudioEngine& a, const juce::String& n)
    : audio (a), name (n)
{
    const auto s = audio.getMidiOutSettings (name);

    timing.setSliderStyle (juce::Slider::LinearHorizontal);
    timing.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    timing.setRange (-100.0, 50.0, 1.0);
    timing.setValue (s.timingMs, juce::dontSendNotification);
    timing.setDoubleClickReturnValue (true, 0.0);
    timing.setTooltip ("When its notes go out: earlier for a synth that's slow to sound (or one you hear through NightShift), "
                       "later if it's ahead. Double-click for on time.");
    timing.onValueChange = [this] { apply(); };
    addAndMakeVisible (timing);

    tempo.setColour (juce::ToggleButton::textColourId, text);
    tempo.setColour (juce::ToggleButton::tickColourId, accent);
    tempo.setToggleState (s.clock, juce::dontSendNotification);
    tempo.setTooltip ("Sends NightShift's tempo with start and stop (MIDI clock), so a drum machine or an arpeggiator plays in time with the song");
    tempo.onClick = [this] { apply(); };
    addAndMakeVisible (tempo);
}

juce::String AudioSettings::OutputRow::describe (int ms)
{
    if (ms == 0)
        return "On time";

    return juce::String (std::abs (ms)) + (ms < 0 ? " ms early" : " ms late");
}

void AudioSettings::OutputRow::apply()
{
    audio.setMidiOutSettings (name, { juce::roundToInt (timing.getValue()), tempo.getToggleState() });
    repaint (valueArea);
}

void AudioSettings::OutputRow::resized()
{
    // Its name, its timing (slider and what it is now), then Send tempo
    auto r = getLocalBounds();
    nameArea = r.removeFromLeft (186);
    r.removeFromLeft (8);
    timing.setBounds (r.removeFromLeft (150).withSizeKeepingCentre (150, 24));
    r.removeFromLeft (8);
    valueArea = r.removeFromLeft (100);
    r.removeFromLeft (6);
    tempo.setBounds (r.withSizeKeepingCentre (r.getWidth(), 24));
}

void AudioSettings::OutputRow::paint (juce::Graphics& g)
{
    g.setColour (text);
    g.setFont (font (16.0f));

    {
        textaudit::TheirWords theirs;       // the output's name, as its maker gave it
        g.drawText (name, nameArea, juce::Justification::centredLeft, true);
    }

    g.setColour (textDim);
    g.setFont (font (15.0f));
    g.drawText (describe (juce::roundToInt (timing.getValue())), valueArea, juce::Justification::centredLeft, true);
}

void AudioSettings::refreshMidiOutputs()
{
    // A row for each MIDI output (they come and go as they're plugged in)
    const auto names = audio.getMidiOutputNames();

    if (names == outputNames && outputRows.size() == names.size())
        return;

    outputNames = names;
    outputRows.clear();

    for (auto& name : names)
        addAndMakeVisible (outputRows.add (new OutputRow (audio, name)));

    if (getHeight() != wantedHeight())
        setSize (width, wantedHeight());

    resized();
    repaint();
}

AudioSettings::~AudioSettings()
{
    selector.removeComponentListener (this);
    audio.getDeviceManager().removeChangeListener (this);
}

void AudioSettings::componentMovedOrResized (juce::Component& c, bool, bool wasResized)
{
    if (wasResized && &c == &selector && getHeight() != wantedHeight())
        setSize (width, wantedHeight());
}

void AudioSettings::applyOptions()
{
    auto o = audio.getOptions();
    const bool wantedInterface = preferInterface.getToggleState() && ! o.preferInterface;

    o.lowLatencyMonitoring = lowLatency.getToggleState();
    o.preferInterface = preferInterface.getToggleState();
    audio.setOptions (o);

    if (wantedInterface)
        audio.openPreferredInterface();

    refreshReadout();
}

void AudioSettings::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Choosing another device by hand means you don't want the MiniFuse forced on you
    if (auto device = audio.getDeviceManager().getCurrentAudioDevice();
        device != nullptr && ! isPreferredInterface (device) && preferInterface.getToggleState())
    {
        preferInterface.setToggleState (false, juce::dontSendNotification);
        applyOptions();
    }

    refreshReadout();
}

void AudioSettings::timerCallback()
{
    refreshReadout();
    refreshMidiInputs();
    refreshMidiOutputs();
}

void AudioSettings::refreshReadout()
{
    const auto info = audio.getLatencyInfo();

    if (info.driver.isEmpty())
    {
        readout = "No audio device is open.";
        advice = "Pick the MiniFuse above, or press " + useInterface.getButtonText() + ".";
        readoutIsGood = false;
    }
    else
    {
        readout = info.driver + "  -  " + info.device + "  -  " + juce::String (info.sampleRate / 1000.0, 1) + " kHz  -  "
                + juce::String (info.bufferSize) + " samples (" + juce::String (info.toMs (info.bufferSize), 2) + " ms)\n"
                + "In " + juce::String (info.toMs (info.inputSamples), 1) + " ms + out " + juce::String (info.toMs (info.outputSamples), 1)
                + " ms = " + juce::String (info.roundTripMs(), 1) + " ms round trip      CPU "
                + juce::String (juce::roundToInt (info.cpu * 100.0)) + "%"
                + (info.xruns >= 0 ? "      dropouts " + juce::String (info.xruns) : juce::String());

        readoutIsGood = info.driver == AudioEngine::lowLatencyDriver && info.roundTripMs() <= 10.0;

        if (info.driver != AudioEngine::lowLatencyDriver)
            advice = juce::String (AudioEngine::systemAudioName) + " adds delay. For the lowest latency choose "
                   + AudioEngine::lowLatencyName + " and the MiniFuse.";
        else if (info.bufferSize > 64)
            advice = "Try 64 or 32 samples. If you hear crackles or dropouts climb, go back up one step.";
        else if (info.cpu > 0.7)
            advice = "The computer is close to its limit at this buffer; raise it one step if anything crackles.";
        else
            advice = "As low as it gets. With the MiniFuse's own direct monitoring on, you hear yourself with no delay at all.";
    }

    const auto names = audio.getLatencyBypassedPlugins();
    bypassed = names.isEmpty() ? juce::String() : "Bypassed while monitoring: " + names.joinIntoString (", ");

    useInterface.setEnabled (! isPreferredInterface (audio.getDeviceManager().getCurrentAudioDevice()));
    repaint (readoutArea);
}

void AudioSettings::resized()
{
    auto r = getLocalBounds();
    selector.setBounds (r.removeFromTop (selector.getHeight()));

    r.reduce (16, 6);
    readoutArea = r.removeFromTop (74);
    r.removeFromTop (6);

    // As wide as its words need (a Mac's "Core Audio" is longer than "ASIO"), and 150 at least
    auto row = r.removeFromTop (26);
    const auto words = juce::GlyphArrangement::getStringWidth (getLookAndFeel().getTextButtonFont (useInterface, row.getHeight()),
                                                               useInterface.getButtonText());
    useInterface.setBounds (row.removeFromRight (std::max (150, (int) std::ceil (words) + 24)));

    r.removeFromTop (8);
    lowLatency.setBounds (r.removeFromTop (24));
    r.removeFromTop (2);
    preferInterface.setBounds (r.removeFromTop (24));

    // MIDI keyboards and controllers, then synths plugged in, at the bottom
    auto bottom = getLocalBounds();
    auto outs = bottom.removeFromBottom (midiOutHeight()).reduced (16, 0);
    auto midi = bottom.removeFromBottom (midiHeight()).reduced (16, 0);
    midiTitleArea = midi.removeFromTop (34);
    midiHintArea = midi.removeFromBottom (46);

    for (auto* b : midiSwitches)
        b->setBounds (midi.removeFromTop (26));

    outTitleArea = outs.removeFromTop (34);
    outHintArea = outs.removeFromBottom (46);

    for (auto* row : outputRows)
        row->setBounds (outs.removeFromTop (32));
}

void AudioSettings::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    g.setColour (outline);
    g.drawHorizontalLine (selector.getBottom(), 0.0f, (float) getWidth());

    auto r = readoutArea.toFloat();
    g.setColour (juce::Colour (0xff0b0d11));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (readoutIsGood ? ledOn.withAlpha (0.5f) : outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

    auto inner = readoutArea.reduced (12, 6);
    g.setColour (readoutIsGood ? ledOn : accent);
    g.setFont (monoFont (15.0f, true));
    g.drawFittedText (readout, inner.removeFromTop (34), juce::Justification::topLeft, 2);
    g.setColour (textDim);
    g.setFont (font (15.0f));
    g.drawFittedText (advice + (bypassed.isNotEmpty() ? "\n" + bypassed : juce::String()), inner, juce::Justification::topLeft, 2);

    // MIDI keyboards and controllers
    g.setColour (outline);
    g.drawHorizontalLine (midiTitleArea.getY(), 0.0f, (float) getWidth());
    g.setColour (textFaint);
    g.setFont (labelFont());
    g.drawText ("MIDI KEYBOARDS AND CONTROLLERS", midiTitleArea.withTrimmedTop (8), juce::Justification::centredLeft, true);

    g.setFont (font (16.0f));

    if (midiSwitches.isEmpty())
    {
        g.setColour (textDim);
        g.drawText ("None plugged in yet: a USB keyboard shows up here when you plug it in.",
                    midiTitleArea.withY (midiTitleArea.getBottom()).withHeight (26), juce::Justification::centredLeft, true);
    }

    g.setColour (textFaint);
    g.drawFittedText ("They play the selected channel and record when Record is armed. Right-click a fader or a knob "
                      "in NightShift to link it to a knob on your controller.",
                      midiHintArea, juce::Justification::centredLeft, 2, 1.0f);

    // Synths plugged in
    g.setColour (outline);
    g.drawHorizontalLine (outTitleArea.getY(), 0.0f, (float) getWidth());
    g.setColour (textFaint);
    g.setFont (labelFont());
    g.drawText ("MIDI OUT: SYNTHS PLUGGED IN", outTitleArea.withTrimmedTop (8), juce::Justification::centredLeft, true);
    g.setFont (font (16.0f));

    if (outputRows.isEmpty())
    {
        g.setColour (textDim);
        g.drawText ("None plugged in yet: a synth on USB or an interface's MIDI out shows up here.",
                    outTitleArea.withY (outTitleArea.getBottom()).withHeight (32), juce::Justification::centredLeft, true);
    }

    g.setColour (textFaint);
    g.drawFittedText ("Choose one as a channel's instrument to play it. Notes late? Move its timing earlier. "
                      "Send tempo keeps its arpeggiator or a drum machine in time.",
                      outHintArea, juce::Justification::centredLeft, 2, 1.0f);
}

} // namespace nightshift
