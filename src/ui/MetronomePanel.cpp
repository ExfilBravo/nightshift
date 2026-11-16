// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "MetronomePanel.h"

namespace nightshift
{

using namespace theme;

namespace
{
    constexpr int lightsHeight = 92;

    juce::String accentName (int level)
    {
        switch (level)
        {
            case metronome::strong: return "Strong";
            case metronome::normal: return "Normal";
            case metronome::soft:   return "Soft";
            default:                return "Off";
        }
    }

    void label (juce::Graphics& g, const juce::String& words, juce::Rectangle<int> over)
    {
        g.setColour (textFaint);
        g.setFont (labelFont());
        g.drawText (words, over.withY (over.getY() - 17).withHeight (14), juce::Justification::centredLeft, true);
    }
}

//==============================================================================
MetronomePanel::MetronomePanel (Song& s, AudioEngine& a)
    : song (s), audio (a)
{
    addAndMakeVisible (lights);
    lights.setTooltip ("Click a beat to change its accent (strong, normal, soft, off); right-click for the list");

    onOff.setTooltip (shortcutText ("Click while the song plays (Ctrl+M). It always counts you in before a take"));
    onOff.onChange = [this] (int index) { setOption ([index] (auto& o) { o.metronome = index == 1; }); };
    addAndMakeVisible (onOff);

    bpm.setCaption ("bpm");
    bpm.onDragStart = [this] { song.checkpoint(); };
    bpm.onChange = [this] (double v) { setTempo (v); };
    addAndMakeVisible (bpm);

    slower.setTooltip ("1 bpm slower");
    faster.setTooltip ("1 bpm faster");
    tapButton.setTooltip ("Tap the beat: the tempo is the time between your last two taps");

    for (auto* b : { &slower, &faster, &tapButton })
    {
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    slower.onClick = [this] { song.checkpoint(); setTempo (std::round (audio.getTempoNow()) - 1.0); };
    faster.onClick = [this] { song.checkpoint(); setTempo (std::round (audio.getTempoNow()) + 1.0); };
    tapButton.onClick = [this] { tap (juce::Time::getMillisecondCounterHiRes() / 1000.0); };

    practise.setFramed (true);
    practise.setLabel ("Practise");
    practise.setTooltip ("The metronome on its own, the song stopped, to play along to");
    practise.onClick = [this]
    {
        audio.setPractising (! audio.isPractising());
        syncFromEngine();
    };
    addAndMakeVisible (practise);

    for (auto& sig : metronome::signatures())
        signature.addItem (sig.name(), signature.getNumItems() + 1);

    for (auto& r : metronome::rhythms())
        rhythm.addItem (r.name, rhythm.getNumItems() + 1);

    sound.addItemList (metronome::soundNames(), 1);
    countIn.addItemList ({ "No count-in", "1 bar", "2 bars" }, 1);

    signature.setTooltip ("The time signature playing (where the song is), and how its beats group (7/8 as 2+2+3, 3+2+2 or 2+3+2): the groups set the accents");
    rhythm.setTooltip ("How many clicks a beat");
    sound.setTooltip ("The metronome's sound");
    countIn.setTooltip ("Clicks before a take starts, so you come in on the one (also in Record's right-click menu)");

    signature.onChange = [this]
    {
        const int index = signature.getSelectedItemIndex();

        if (! juce::isPositiveAndBelow (index, (int) metronome::signatures().size()))
            return;

        const auto sig = metronome::signatures()[(size_t) index];

        if (const auto [beats, unit] = audio.getMeterNow(); sig.beats != beats || sig.unit != unit)
        {
            song.checkpoint();
            audio.setMeterNow (sig.beats, sig.unit);
        }

        audio.setClickGrouping (sig);
        lights.repaint();
    };

    rhythm.onChange = [this] { setOption ([v = rhythm.getSelectedItemIndex()] (auto& o) { o.clickRhythm = juce::jmax (0, v); }); lights.repaint(); };
    sound.onChange = [this] { setOption ([v = sound.getSelectedItemIndex()] (auto& o) { o.clickSound = juce::jmax (0, v); }); };
    countIn.onChange = [this] { setOption ([v = countIn.getSelectedItemIndex()] (auto& o) { o.countInBars = juce::jlimit (0, 2, v); }); };

    for (auto* c : { &signature, &rhythm, &sound, &countIn })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (*c);
    }

    volume.formatValue = [] (double v)
    {
        const float gain = MetronomePlugin::volumeToGain ((float) v);
        return "Volume " + (gain <= 0.0f ? juce::String ("-inf") : juce::String (juce::Decibels::gainToDecibels (gain), 1)) + " dB";
    };
    volume.setDoubleClickReturnValue (true, 0.7);
    volume.onValueChange = [this] { setOption ([v = (float) volume.getValue()] (auto& o) { o.clickVolume = v; }); };

    pan.formatValue = [] (double v)
    {
        const int amount = juce::roundToInt (std::abs (v) * 100.0);
        return amount == 0 ? juce::String ("Pan centre") : "Pan " + juce::String (v < 0.0 ? "L " : "R ") + juce::String (amount);
    };
    pan.onValueChange = [this] { setOption ([v = (float) pan.getValue()] (auto& o) { o.clickPan = v; }); };

    for (auto* k : { &volume, &pan })
        addAndMakeVisible (*k);

    recordingOnly.setColour (juce::ToggleButton::textColourId, text);
    recordingOnly.setColour (juce::ToggleButton::tickColourId, accent);
    recordingOnly.setWantsKeyboardFocus (false);
    recordingOnly.setTooltip ("While it's on, click only while a take records (counting in happens either way)");
    recordingOnly.onClick = [this] { setOption ([v = recordingOnly.getToggleState()] (auto& o) { o.metronomeRecordingOnly = v; }); };
    addAndMakeVisible (recordingOnly);

    setWantsKeyboardFocus (true);
    setSize (width, height);
    syncFromEngine();
    song.addListener (this);
    startTimerHz (30);
}

MetronomePanel::~MetronomePanel()
{
    song.removeListener (this);
}

void MetronomePanel::setOption (std::function<void (AudioEngine::Options&)> change)
{
    auto o = audio.getOptions();
    change (o);
    audio.setOptions (o);
}

void MetronomePanel::setTempo (double newBpm)
{
    // The tempo playing now (where the song is, once it changes along the song)
    audio.setTempoNow (newBpm);
}

void MetronomePanel::tap (double at)
{
    // A pause of more than two seconds starts a new run of taps
    if (! taps.empty() && at - taps.back() > 2.0)
        taps.clear();

    taps.push_back (at);

    if (taps.size() < 2)
        return;

    const double interval = taps[taps.size() - 1] - taps[taps.size() - 2];

    if (interval <= 0.0)
        return;

    if (taps.size() == 2)
        song.checkpoint();

    setTempo (std::round (60.0 / interval * 100.0) / 100.0);
}

void MetronomePanel::songChanged (Song::Change c)
{
    if (c == Song::Change::transport || c == Song::Change::everything)
        syncFromEngine();
}

void MetronomePanel::syncFromEngine()
{
    const auto o = audio.getOptions();
    onOff.setSelected (o.metronome ? 1 : 0);
    bpm.setValue (audio.getTempoNow());

    const auto current = audio.getClickSignature();
    const auto& list = metronome::signatures();

    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].beats == current.beats && list[i].unit == current.unit && list[i].groups == current.groups)
            signature.setSelectedItemIndex ((int) i, juce::dontSendNotification);

    rhythm.setSelectedItemIndex (o.clickRhythm, juce::dontSendNotification);
    sound.setSelectedItemIndex (o.clickSound, juce::dontSendNotification);
    countIn.setSelectedItemIndex (o.countInBars, juce::dontSendNotification);
    volume.setValue (o.clickVolume, juce::dontSendNotification);
    pan.setValue (o.clickPan, juce::dontSendNotification);
    recordingOnly.setToggleState (o.metronomeRecordingOnly, juce::dontSendNotification);

    const bool practising = audio.isPractising();
    practise.setToggleState (practising, juce::dontSendNotification);
    practise.setSolidWhenOn (true);
    practise.setLabel (practising ? "Stop" : "Practise");
    repaint();
}

bool MetronomePanel::keyPressed (const juce::KeyPress& key)
{
    return onKey != nullptr && onKey (key);
}

void MetronomePanel::timerCallback()
{
    // Keys come here while it's open, so Space still plays
    if (! focusTaken && isShowing())
    {
        focusTaken = true;
        grabKeyboardFocus();
    }

    // Ctrl+M, the song's Play and so on change things behind it
    const auto o = audio.getOptions();

    if (onOff.getSelected() != (o.metronome ? 1 : 0) || practise.getToggleState() != audio.isPractising())
        syncFromEngine();

    if (auto* m = audio.getMetronome())
    {
        const auto pulse = m->getLastPulse();

        if (pulse.count != lights.lastCount)
        {
            lights.lastCount = pulse.count;
            lights.litBeat = pulse.beat;
            lights.litPulse = pulse.pulse;
            lights.litAt = juce::Time::getMillisecondCounter();
            lights.repaint();
        }
        else if (lights.litBeat >= 0 && juce::Time::getMillisecondCounter() - lights.litAt > 160)
        {
            lights.litBeat = -1;
            lights.repaint();
        }
    }
}

//==============================================================================
void MetronomePanel::resized()
{
    auto r = getLocalBounds().reduced (18, 14);

    auto top = r.removeFromTop (34);
    onOff.setBounds (top.removeFromRight (onOff.getIdealWidth()));
    r.removeFromTop (10);

    lightsArea = r.removeFromTop (lightsHeight);
    lights.setBounds (lightsArea);
    r.removeFromTop (14);

    // The tempo: - 120.00 bpm +, Tap, and Practise at the far end
    tempoArea = r.removeFromTop (44);
    {
        auto t = tempoArea;
        slower.setBounds (t.removeFromLeft (34).withSizeKeepingCentre (34, 34));
        t.removeFromLeft (8);
        bpm.setBounds (t.removeFromLeft (124).withSizeKeepingCentre (124, 30));
        t.removeFromLeft (8);
        faster.setBounds (t.removeFromLeft (34).withSizeKeepingCentre (34, 34));
        t.removeFromLeft (12);
        tapButton.setBounds (t.removeFromLeft (64).withSizeKeepingCentre (64, 34));
        practise.setBounds (t.removeFromRight (128).withSizeKeepingCentre (128, 36));
    }

    r.removeFromTop (30);

    // Time signature, rhythm, sound
    auto combos = r.removeFromTop (30);
    const int gap = 12, w = (combos.getWidth() - 2 * gap) / 3;
    signature.setBounds (combos.removeFromLeft (w));
    combos.removeFromLeft (gap);
    rhythm.setBounds (combos.removeFromLeft (w));
    combos.removeFromLeft (gap);
    sound.setBounds (combos);

    r.removeFromTop (28);

    // Volume and pan, the count-in, and only while recording
    knobsArea = r.removeFromTop (64);
    {
        auto k = knobsArea;
        volume.setBounds (k.removeFromLeft (64).withSizeKeepingCentre (52, 52));
        k.removeFromLeft (10);
        pan.setBounds (k.removeFromLeft (64).withSizeKeepingCentre (52, 52));
        k.removeFromLeft (24);
        countIn.setBounds (k.removeFromLeft (w - 30).withSizeKeepingCentre (w - 30, 30));
        k.removeFromLeft (gap + 6);
        recordingOnly.setBounds (k.withSizeKeepingCentre (k.getWidth(), 26));
    }
}

void MetronomePanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    auto top = getLocalBounds().reduced (18, 14).removeFromTop (34);
    g.setColour (text);
    g.setFont (font (20.0f, true));
    g.drawText ("Metronome", top, juce::Justification::centredLeft, true);

    g.setColour (textFaint);
    g.setFont (font (16.0f));
    g.drawText (audio.isPractising() ? juce::String ("practising, on its own") : "in time with the song (" + song.meterName() + ")",
                top.withTrimmedLeft (118).withTrimmedRight (onOff.getIdealWidth() + 10), juce::Justification::centredLeft, true);

    // Behind the tempo: a raised strip like the top bar's readouts
    auto strip = tempoArea.withRight (tapButton.getRight()).expanded (8, 2).toFloat();
    g.setColour (panelLight);
    g.fillRoundedRectangle (strip, 10.0f);
    g.setColour (hairline);
    g.drawRoundedRectangle (strip.reduced (0.5f), 10.0f, 1.0f);

    label (g, "TIME SIGNATURE", signature.getBounds());
    label (g, "RHYTHM", rhythm.getBounds());
    label (g, "SOUND", sound.getBounds());
    label (g, "COUNT-IN", countIn.getBounds());

    g.setColour (textFaint);
    g.setFont (labelFont());
    g.drawText ("VOLUME", volume.getBounds().withY (knobsArea.getBottom() - 2).withHeight (14).expanded (8, 0), juce::Justification::centred, true);
    g.drawText ("PAN", pan.getBounds().withY (knobsArea.getBottom() - 2).withHeight (14).expanded (8, 0), juce::Justification::centred, true);
}

//==============================================================================
int MetronomePanel::BeatLights::beatAt (juce::Point<int> p) const
{
    const int beats = panel.audio.getClickSignature().beats;

    if (beats <= 0 || ! getLocalBounds().contains (p))
        return -1;

    return juce::jlimit (0, beats - 1, p.x * beats / juce::jmax (1, getWidth()));
}

void MetronomePanel::BeatLights::cycle (int beat)
{
    auto accents = panel.audio.getClickAccents();

    if (! juce::isPositiveAndBelow (beat, (int) accents.size()))
        return;

    // Strong, normal, soft, off, and round again
    accents[(size_t) beat] = accents[(size_t) beat] == metronome::off ? (int) metronome::strong : accents[(size_t) beat] - 1;
    panel.audio.setClickAccents (accents);
    repaint();
}

void MetronomePanel::BeatLights::mouseDown (const juce::MouseEvent& e)
{
    const int beat = beatAt (e.getPosition());

    if (beat < 0)
        return;

    if (! e.mods.isPopupMenu())
    {
        cycle (beat);
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader ("Beat " + juce::String (beat + 1));
    const auto accents = panel.audio.getClickAccents();

    for (int a : { (int) metronome::strong, (int) metronome::normal, (int) metronome::soft, (int) metronome::off })
        menu.addItem (accentName (a), true, accents[(size_t) beat] == a, [this, beat, a]
        {
            auto changed = panel.audio.getClickAccents();
            changed[(size_t) beat] = a;
            panel.audio.setClickAccents (changed);
            repaint();
        });

    menu.addSeparator();
    menu.addItem ("Reset every beat to " + panel.audio.getClickSignature().name() + "'s accents", [this]
    {
        panel.audio.setClickAccents (metronome::defaultAccents (panel.audio.getClickSignature()));
        repaint();
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> (e.x, e.y, 1, 1))));
}

void MetronomePanel::BeatLights::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0b0d12));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (hairline);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    const auto sig = panel.audio.getClickSignature();
    const auto accents = panel.audio.getClickAccents();
    const int beats = (int) accents.size();
    const auto& rhythms = metronome::rhythms();
    const int pulses = (int) rhythms[(size_t) juce::jlimit (0, (int) rhythms.size() - 1, panel.audio.getOptions().clickRhythm)].pulses.size();
    const bool lit = litBeat >= 0 && (panel.audio.isPlaying() || panel.audio.isPractising());

    if (beats <= 0)
        return;

    const float cell = r.getWidth() / (float) beats;
    const float maxDiameter = std::min (30.0f, cell - 6.0f);

    // Each group's start drawn with a little more room before it
    std::vector<bool> groupStart ((size_t) beats, false);
    int at = 0;

    for (auto groupSize : sig.groups)
    {
        if (at < beats)
            groupStart[(size_t) at] = true;

        at += groupSize;
    }

    for (int i = 0; i < beats; ++i)
    {
        const int level = accents[(size_t) i];
        const float cx = r.getX() + cell * (i + 0.5f), cy = r.getCentreY() - 8.0f;
        const float scale = level == metronome::strong ? 1.0f : level == metronome::normal ? 0.78f : 0.58f;
        const float d = maxDiameter * scale;
        auto circle = juce::Rectangle<float> (d, d).withCentre ({ cx, cy });
        const bool on = lit && litBeat == i && litPulse == 0;
        const auto colour = level == metronome::strong ? theme::accent : accentCool;

        if (level == metronome::off)
        {
            g.setColour (textFaint.withAlpha (0.5f));
            g.drawEllipse (circle.reduced (1.0f), 1.5f);
        }
        else
        {
            if (on)
            {
                g.setColour (colour.withAlpha (0.25f));
                g.fillEllipse (circle.expanded (7.0f));
            }

            g.setColour (on ? colour.brighter (0.3f) : colour.withAlpha (level == metronome::soft ? 0.35f : 0.55f));
            g.fillEllipse (circle);
        }

        // Its number, and a tick under a group's first beat
        g.setColour (on ? text : textFaint);
        g.setFont (monoFont (15.0f, true));
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (cell, 14.0f).withCentre ({ cx, r.getBottom() - 26.0f }), juce::Justification::centred, true);

        if (! sig.groups.empty() && groupStart[(size_t) i])
        {
            g.setColour (textDim.withAlpha (0.6f));
            g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, 2.0f).withCentre ({ cx, r.getBottom() - 15.0f }), 1.0f);
        }

        // The pulses between this beat and the next
        for (int p = 1; p < pulses; ++p)
        {
            const float px = cx + cell * (float) p / (float) pulses;
            const bool pulseOn = lit && litBeat == i && litPulse == p;
            g.setColour (level == metronome::off ? textFaint.withAlpha (0.25f) : pulseOn ? text : textDim.withAlpha (0.45f));
            g.fillEllipse (juce::Rectangle<float> (pulseOn ? 6.0f : 4.0f, pulseOn ? 6.0f : 4.0f).withCentre ({ px, cy }));
        }
    }
}

} // namespace nightshift
