// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "MasterPanel.h"
#include "Theme.h"

namespace nightshift
{

using namespace theme;

namespace
{
    // The graph's loudness range, top to bottom
    constexpr float graphTop = -3.0f, graphBottom = -36.0f;

    juce::String db (float v, int places = 1)
    {
        return (v > 0.05f ? "+" : "") + juce::String (v, places);
    }

    // What a reading's numbers mean, for the hover text
    const char* const lufsNote = "LUFS is how loud a song sounds to people (not just how high its peaks go). Streaming services "
                                 "turn anything louder than their level down to it, so being much louder only costs punch.";
}

MasterPanel::MasterPanel (Song& s, AudioEngine& a)
    : song (s), audio (a)
{
    aiButton.setButtonText ("Let the AI master it...");
    aiButton.setTooltip ("The AI mastering engineer (in the AI tab) listens to the whole song and sets the loudness, tone, punch and width for its style, and says why");
    aiButton.onClick = [this] { if (onOpenAiMaster != nullptr) onOpenAiMaster(); };
    aiButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (aiButton);

    setButton.setColour (juce::TextButton::buttonColourId, accent);
    setButton.setColour (juce::TextButton::textColourOffId, background);
    setButton.setTooltip ("Listens to the whole song, then makes it as loud as the target, with nothing going over the ceiling");
    setButton.onClick = [this] { setLoudness(); };
    setButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (setButton);

    louder.formatValue = [] (double v) { return std::abs (v) < 0.05 ? juce::String ("Louder: as mixed") : "Louder: " + db ((float) v) + " dB"; };
    louder.setTooltip ("How hard the mix is pushed into the limiter: louder, until the limiter has to squeeze. Set loudness sets it for you");
    ceiling.formatValue = [] (double v) { return "Never above " + juce::String (v, 1) + " dB"; };
    ceiling.setTooltip ("The ceiling: nothing comes out louder than this, between the samples too. -1 dB leaves room for "
                        "streaming services to convert it without distorting");

    for (auto* k : { &louder, &ceiling })
    {
        k->setColour (juce::Slider::rotarySliderFillColourId, accent);
        k->onDragStart = [this] { song.checkpoint(); };
        addAndMakeVisible (*k);
    }

    louder.onValueChange = [this]
    {
        song.mastering.pushDb = (float) louder.getValue();
        song.notify (Song::Change::mixer);
    };
    ceiling.onValueChange = [this]
    {
        song.mastering.ceilingDb = (float) ceiling.getValue();
        song.notify (Song::Change::mixer);
    };

    // The second page: tone, punch and width
    static const char* const toneTips[] {
        "Bass: the low end, from the kick and bass down (a shelf at 100 Hz). Up for weight, down if it booms",
        "Body: around 300 Hz. Down cleans up a muddy, boxy mix; up makes a thin one fuller",
        "Presence: around 3 kHz, where voices and guitars cut through. Up to bring them forward, down if it's harsh",
        "Air: the very top (a shelf at 10 kHz). Up for sparkle and openness, down if it hisses or sounds brittle" };

    for (int b = 0; b < 4; ++b)
    {
        auto& k = toneKnob[b];
        k.formatValue = [b] (double v) { return juce::String (mastering::Shaper::toneBandName (b)) + ": " + (std::abs (v) < 0.05 ? juce::String ("flat") : db ((float) v) + " dB"); };
        k.setTooltip (toneTips[b]);
        k.onValueChange = [this, b]
        {
            song.mastering.toneDb[(size_t) b] = (float) std::round (toneKnob[b].getValue() * 10.0) / 10.0f;
            song.notify (Song::Change::mixer);
        };
    }

    punchKnob.formatValue = [] (double v) { return "Punch: " + juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    punchKnob.setTooltip ("How much it tightens: the loudest moments of the lows, mids and highs each brought closer to the rest, "
                          "the level kept where it was. More sounds denser and more even; too much sounds flat");
    punchKnob.onValueChange = [this]
    {
        song.mastering.punchAmount = (float) punchKnob.getValue();
        song.notify (Song::Change::mixer);
    };

    static const char* const shareTips[] {
        "Lows (under 120 Hz): how much of the punch goes on the kick and bass. More holds the low end steady",
        "Mids: how much of the punch goes on the voices, snare and instruments",
        "Highs (over 2.5 kHz): how much of the punch goes on the cymbals and top. More tames harsh, splashy highs" };

    for (int b = 0; b < 3; ++b)
    {
        auto& k = shareKnob[b];
        k.formatValue = [b] (double v) { return juce::String (mastering::Shaper::punchBandName (b)) + ": " + juce::String (juce::roundToInt (v * 100.0)) + "% of it"; };
        k.setTooltip (shareTips[b]);
        k.onValueChange = [this, b]
        {
            song.mastering.punchShare[(size_t) b] = (float) shareKnob[b].getValue();
            song.notify (Song::Change::mixer);
        };
    }

    widthKnob.formatValue = [] (double v)
    {
        return v < 0.01 ? juce::String ("Width: mono") : std::abs (v - 1.0) < 0.01 ? juce::String ("Width: as mixed")
                                                                                 : "Width: " + juce::String (juce::roundToInt (v * 100.0)) + "%";
    };
    widthKnob.setTooltip ("How wide it sounds: the sides (whatever isn't in the middle) louder or quieter. Wider sounds bigger, "
                          "but too wide can thin out on a phone or in mono");
    widthKnob.onValueChange = [this]
    {
        song.mastering.width = (float) widthKnob.getValue();
        song.notify (Song::Change::mixer);
    };

    monoBassKnob.setRange (0.0, 300.0, 10.0);
    monoBassKnob.formatValue = [] (double v) { return v < 5.0 ? juce::String ("Mono bass: off") : "Mono bass below " + juce::String (juce::roundToInt (v)) + " Hz"; };
    monoBassKnob.setTooltip ("Keeps the bass in the middle: below this, nothing comes from the sides. Tighter low end, "
                             "and it plays right on club systems and in mono. 100-150 Hz is usual");
    monoBassKnob.onValueChange = [this]
    {
        song.mastering.monoBassHz = (float) monoBassKnob.getValue();
        song.notify (Song::Change::mixer);
    };

    for (auto* k : { &toneKnob[0], &toneKnob[1], &toneKnob[2], &toneKnob[3], &punchKnob, &shareKnob[0], &shareKnob[1], &shareKnob[2],
                     &widthKnob, &monoBassKnob })
    {
        k->setColour (juce::Slider::rotarySliderFillColourId, accentCool);
        k->onDragStart = [this] { song.checkpoint(); };
        addChildComponent (*k);
    }

    song.addListener (this);
    showPageControls();
    refreshControls();
    startTimerHz (20);
}

MasterPanel::~MasterPanel()
{
    *stillWanted = false;
    song.removeListener (this);
}

//==============================================================================
void MasterPanel::songChanged (Song::Change c)
{
    if (c == Song::Change::mixer || c == Song::Change::everything)
    {
        refreshControls();
        repaint();
    }
}

void MasterPanel::refreshControls()
{
    const auto& m = song.mastering;
    louder.setValue (m.pushDb, juce::dontSendNotification);
    ceiling.setValue (m.ceilingDb, juce::dontSendNotification);

    for (int b = 0; b < 4; ++b)
        toneKnob[b].setValue (m.toneDb[(size_t) b], juce::dontSendNotification);

    punchKnob.setValue (m.punchAmount, juce::dontSendNotification);

    for (int b = 0; b < 3; ++b)
        shareKnob[b].setValue (m.punchShare[(size_t) b], juce::dontSendNotification);

    widthKnob.setValue (m.width, juce::dontSendNotification);
    monoBassKnob.setValue (m.monoBassHz, juce::dontSendNotification);

    const auto* t = mastering::findTarget (song.mastering.target);
    const bool aiming = t != nullptr && t->lufs != 0.0f;
    setButton.setButtonText (working ? juce::String ("Listening...") : aiming ? "Set loudness for " + juce::String (t->name) : juce::String ("Set loudness"));
    setButton.setEnabled (aiming && ! working);

    // Lit only when there's something to do
    setButton.setColour (juce::TextButton::buttonColourId, setButton.isEnabled() ? accent : titleBar);
    setButton.setColour (juce::TextButton::textColourOffId, setButton.isEnabled() ? background : textFaint);

    if (status.isEmpty())
        status = aiming ? "Press Set loudness: NightShift listens to the whole song and makes it " + juce::String (t->lufs, 0) + " LUFS."
                        : juce::String ("Keeping it from clipping, nothing more. Pick what it's for to make it as loud as that needs.");
}

void MasterPanel::setMastering (std::function<void (Song::Mastering&)> change)
{
    song.checkpoint();
    change (song.mastering);
    song.notify (Song::Change::mixer);
}

void MasterPanel::chooseTarget (const juce::String& id)
{
    const auto* t = mastering::findTarget (id);

    if (t == nullptr || working)
        return;

    if (id != song.mastering.target)
    {
        // Back to just keeping it from clipping takes the push away; a target keeps it until Set loudness
        setMastering ([t] (Song::Mastering& m)
        {
            m.target = t->id;
            m.ceilingDb = t->ceilingDb;

            if (t->lufs == 0.0f)
                m.pushDb = 0.0f;
        });
    }

    status = t->lufs == 0.0f ? juce::String ("Keeping it from clipping, nothing more: the loudness stays as you mixed it.")
                             : "Press Set loudness: NightShift listens to the whole song and makes it " + juce::String (t->lufs, 0) + " LUFS.";
    refreshControls();
    repaint();
}

//==============================================================================
void MasterPanel::setLoudness()
{
    const auto* t = mastering::findTarget (song.mastering.target);

    if (working || t == nullptr || t->lufs == 0.0f)
        return;

    working = true;
    progress = 0.0f;
    status = "Listening to your song...";
    refreshControls();
    repaint();

    // The mix as it goes into the limiter: a render, here, while the cursor waits
    audio.stop();
    const auto capture = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NightShift loudness capture.wav");
    juce::MouseCursor::showWaitCursor();
    const bool captured = audio.captureMix (capture);
    juce::MouseCursor::hideWaitCursor();

    if (! captured)
    {
        working = false;
        status = "NightShift couldn't play the song through to measure it.";
        refreshControls();
        repaint();
        return;
    }

    // Then the push, worked out on a thread of its own (a few passes over the capture)
    status = "Working out how loud to make it...";
    repaint();

    const float lufs = t->lufs, ceilingDb = song.mastering.ceilingDb;
    const juce::String name = t->name;
    auto wanted = stillWanted;
    juce::Component::SafePointer<MasterPanel> safe (this);
    auto sharedProgress = std::make_shared<std::atomic<float>> (0.0f);
    progressSource = sharedProgress;

    juce::Thread::launch ([wanted, safe, sharedProgress, capture, lufs, ceilingDb, name]
    {
        const auto push = mastering::findPush (capture, lufs, ceilingDb, [wanted, sharedProgress] (float p)
        {
            *sharedProgress = p;
            return wanted->load();
        });

        capture.deleteFile();

        juce::MessageManager::callAsync ([safe, push, lufs, name]
        {
            if (safe != nullptr)
                safe->finished (push, lufs, name);
        });
    });
}

void MasterPanel::finished (const mastering::Push& push, float targetLufs, const juce::String& targetName)
{
    working = false;
    progressSource.reset();

    if (! push.ok)
    {
        status = push.problem;
        refreshControls();
        repaint();
        return;
    }

    setMastering ([&push] (Song::Mastering& m) { m.pushDb = push.pushDb; });

    // What it did, in plain words
    status = "Done: it was " + juce::String (push.beforeLufs, 1) + " LUFS, now " + juce::String (push.afterLufs, 1) + " LUFS, ready for "
             + targetName + ".";

    if (push.pushDb > 0.05f)
        status << " Pushed " << juce::String (push.pushDb, 1) << " dB louder.";
    else if (push.pushDb < -0.05f)
        status << " Turned down " << juce::String (-push.pushDb, 1) << " dB.";

    if (! push.reachedTarget && push.afterLufs < targetLufs)
        status << " That's as loud as it would go.";

    if (push.squeezeDb > 6.0f)
        status << " The limiter squeezes the loudest parts by up to " << juce::String (push.squeezeDb, 0)
               << " dB, which can flatten the punch: if it sounds squashed, try Streaming.";

    refreshControls();
    repaint();
}

//==============================================================================
void MasterPanel::timerCallback()
{
    // While it's the editor's tab (on screen or in a snapshot)
    if (getParentComponent() == nullptr || ! isVisible())
        return;

    readings = audio.getMasterLoudness();
    squeezeDb = std::max (audio.getLimiterReductionDb(), squeezeDb * 0.85f);

    // The graph starts again when playback does, as the meter does
    const bool playing = audio.isPlaying();

    if (playing && ! wasPlaying)
        history.clear();

    wasPlaying = playing;

    if (playing && (++ticks % 2) == 0)
    {
        history.push_back (readings.shortTermValid ? readings.shortTermLufs : readings.momentaryValid ? readings.momentaryLufs : -100.0f);

        if (history.size() > 300)
            history.erase (history.begin());
    }

    if (working && progressSource != nullptr)
        progress = progressSource->load();

    if (page == 1)
    {
        stereo = audio.getMasterStereo();

        for (int b = 0; b < 3; ++b)
            punchShown[b] = std::max (audio.getPunchReductionDb (b), punchShown[b] * 0.85f);
    }

    repaint();
}

//==============================================================================
void MasterPanel::setPage (int newPage)
{
    page = juce::jlimit (0, 1, newPage);
    showPageControls();
    resized();
    repaint();
}

void MasterPanel::showPageControls()
{
    aiButton.setVisible (page == 0);

    for (auto* c : std::initializer_list<juce::Component*> { &setButton, &louder, &ceiling })
        c->setVisible (page == 0);

    for (auto* c : std::initializer_list<juce::Component*> { &toneKnob[0], &toneKnob[1], &toneKnob[2], &toneKnob[3], &punchKnob,
                                                             &shareKnob[0], &shareKnob[1], &shareKnob[2], &widthKnob, &monoBassKnob })
        c->setVisible (page == 1);
}

bool MasterPanel::sectionOn (int section) const
{
    const auto& m = song.mastering;
    return section == 0 ? m.toneOn : section == 1 ? m.punchOn : m.widthOn;
}

void MasterPanel::toggleSection (int section)
{
    setMastering ([section] (Song::Mastering& m)
    {
        auto& on = section == 0 ? m.toneOn : section == 1 ? m.punchOn : m.widthOn;
        on = ! on;
    });
}

void MasterPanel::cycleWhere (int band)
{
    setMastering ([band] (Song::Mastering& m) { m.toneWhere[(size_t) band] = (m.toneWhere[(size_t) band] + 1) % 3; });
}

//==============================================================================
void MasterPanel::resized()
{
    auto r = getLocalBounds().reduced (18, 14);
    targetsArea = r.removeFromLeft (std::min (310, (int) (r.getWidth() * 0.3f)));
    r.removeFromLeft (20);
    readoutsArea = r.removeFromLeft (std::min (400, (r.getWidth() - 20) / 2));
    r.removeFromLeft (20);
    knobsArea = r.removeFromBottom (std::min (92, r.getHeight() / 2));
    r.removeFromBottom (10);
    graphArea = r;

    // What it's for: a row each, then Set loudness and what it last said
    auto t = targetsArea.withTrimmedTop (24);
    const auto& list = mastering::targets();
    const int rowHeight = juce::jlimit (30, 46, (t.getHeight() - 10 - 38 - 8 - 40 - 38) / (int) list.size());
    targetRows.clear();

    for (auto& target : list)
        targetRows.push_back ({ target.id, t.removeFromTop (rowHeight) });

    t.removeFromTop (10);
    setButton.setBounds (t.removeFromTop (38));
    t.removeFromTop (8);
    aiButton.setBounds (t.removeFromBottom (32));
    t.removeFromBottom (6);
    statusArea = t;

    // The knobs, under the graph (the squeeze bar to their right)
    auto k = knobsArea.withTrimmedTop (2);
    louder.setBounds (k.removeFromLeft (88).withHeight (58).withSizeKeepingCentre (54, 54));
    ceiling.setBounds (k.removeFromLeft (88).withHeight (58).withSizeKeepingCentre (54, 54));

    layOutShapePage();
}

void MasterPanel::layOutShapePage()
{
    // Three cards: Tone the widest, then Punch, then Width; each with its switch top right
    auto r = getLocalBounds().reduced (18, 14);
    const int gap = 16;
    cards[0] = r.removeFromLeft ((int) (r.getWidth() * 0.4f));
    r.removeFromLeft (gap);
    cards[1] = r.removeFromLeft ((r.getWidth() - gap) / 2 + 20);
    r.removeFromLeft (gap);
    cards[2] = r;

    for (int c = 0; c < 3; ++c)
        switches[c] = juce::Rectangle<int> (cards[c].getRight() - 14 - 40, cards[c].getY() + 11, 40, 22);

    constexpr int knobRow = 54, captionHeight = 34;

    // Tone: the curve, then a knob a band with its name and setting, and where it works
    auto t = cards[0].reduced (14, 12).withTrimmedTop (30);
    curveArea = t.removeFromTop (std::max (40, t.getHeight() - knobRow - captionHeight - 26 - 8));
    t.removeFromTop (8);
    const int toneColumn = t.getWidth() / 4;

    for (int b = 0; b < 4; ++b)
    {
        auto column = t.withX (t.getX() + b * toneColumn).withWidth (toneColumn);
        toneKnob[b].setBounds (column.removeFromTop (knobRow).withSizeKeepingCentre (50, 50));
        column.removeFromTop (captionHeight);
        whereChips[b] = column.removeFromTop (24).withSizeKeepingCentre (std::min (toneColumn - 6, 96), 22);
    }

    // Punch: Amount, then each band's share; under them how hard each band is squeezing
    auto p = cards[1].reduced (14, 12).withTrimmedTop (30);
    auto row = p.removeFromTop (knobRow + captionHeight);
    const int punchColumn = row.getWidth() / 4;
    punchKnob.setBounds (juce::Rectangle<int> (row.getX(), row.getY(), punchColumn, knobRow).withSizeKeepingCentre (52, 52));

    for (int b = 0; b < 3; ++b)
        shareKnob[b].setBounds (juce::Rectangle<int> (row.getX() + (b + 1) * punchColumn, row.getY(), punchColumn, knobRow).withSizeKeepingCentre (42, 42));

    p.removeFromTop (6);
    punchBars = p;

    // Width: its two knobs, then how it holds up in mono
    auto w = cards[2].reduced (14, 12).withTrimmedTop (30);
    auto widthRow = w.removeFromTop (knobRow + captionHeight);
    widthKnob.setBounds (widthRow.withWidth (widthRow.getWidth() / 2).withHeight (knobRow).withSizeKeepingCentre (52, 52));
    monoBassKnob.setBounds (widthRow.withTrimmedLeft (widthRow.getWidth() / 2).withHeight (knobRow).withSizeKeepingCentre (52, 52));
    w.removeFromTop (6);
    stereoArea = w;
}

juce::Rectangle<int> MasterPanel::getTargetBounds (const juce::String& id) const
{
    for (auto& [rowId, area] : targetRows)
        if (rowId == id)
            return area;

    return {};
}

//==============================================================================
void MasterPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    if (page == 1)
    {
        paintShapePage (g);
        return;
    }

    auto label = [&g] (const juce::String& s, juce::Rectangle<int> area)
    {
        g.setColour (textFaint);
        g.setFont (labelFont());
        g.drawText (s, area.withHeight (16), juce::Justification::centredLeft, true);
    };

    // What it's for
    label ("WHAT IS IT FOR?", targetsArea);
    const auto* chosen = mastering::findTarget (song.mastering.target);

    for (auto& [id, area] : targetRows)
    {
        const auto* t = mastering::findTarget (id);
        const bool on = chosen != nullptr && id == chosen->id;
        const auto r = area.reduced (0, 2).toFloat();

        if (on || id == hoveredTarget)
        {
            g.setColour (on ? accentCool.withAlpha (0.16f) : raised.withAlpha (0.6f));
            g.fillRoundedRectangle (r, 8.0f);
        }

        if (on)
        {
            g.setColour (accentCool.withAlpha (0.45f));
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        }

        // A round choice, filled when it's this one
        auto dot = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ r.getX() + 18.0f, r.getCentreY() });
        g.setColour (on ? accentCool : textFaint);
        g.drawEllipse (dot, 1.6f);

        if (on)
            g.fillEllipse (dot.reduced (4.0f));

        auto words = area.withTrimmedLeft (36).withTrimmedRight (8);
        const bool twoLines = area.getHeight() >= 40;
        g.setColour (on ? theme::text : textDim);
        g.setFont (font (17.5f, true));
        g.drawText (t->name, twoLines ? words.withTrimmedBottom (words.getHeight() / 2).withTrimmedTop (3) : words,
                    twoLines ? juce::Justification::bottomLeft : juce::Justification::centredLeft, true);

        if (twoLines)
        {
            g.setColour (on ? textDim : textFaint.brighter (0.25f));
            g.setFont (font (15.0f));
            g.drawText (t->detail, words.withTrimmedTop (words.getHeight() / 2), juce::Justification::topLeft, true);
        }
    }

    // What Set loudness said; a bar while it works
    auto s = statusArea;

    if (working)
    {
        auto bar = s.removeFromTop (6).toFloat();
        g.setColour (titleBar);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.02f, 1.0f, progress.load())), 3.0f);
        s.removeFromTop (6);
    }

    g.setColour (textDim);
    g.setFont (font (16.0f));
    g.drawFittedText (status, s, juce::Justification::topLeft, std::max (1, s.getHeight() / 19), 1.0f);

    // How loud it is
    label ("HOW LOUD IT IS", readoutsArea);
    auto grid = readoutsArea.withTrimmedTop (24);
    const int gap = 10;
    const int cellW = (grid.getWidth() - gap) / 2, cellH = (grid.getHeight() - gap) / 2;
    const float target = chosen != nullptr ? chosen->lufs : 0.0f;

    const auto now = readings.shortTermValid ? readings.shortTermLufs : readings.momentaryLufs;
    const bool nowValid = readings.shortTermValid || readings.momentaryValid;
    const auto wholeColour = target == 0.0f || ! readings.integratedValid ? theme::text
                           : std::abs (readings.integratedLufs - target) <= 1.0f ? ledOn : accent;
    const bool over = readings.truePeakDb > song.mastering.ceilingDb + 0.1f;

    paintReadout (g, { grid.getX(), grid.getY(), cellW, cellH }, "NOW", now, nowValid, "LUFS", "the last 3 seconds", theme::text);
    paintReadout (g, { grid.getX() + cellW + gap, grid.getY(), cellW, cellH }, "WHOLE SONG", readings.integratedLufs, readings.integratedValid, "LUFS",
                  target == 0.0f ? juce::String ("since you pressed Play") : "since you pressed Play; aiming for " + juce::String (target, 0), wholeColour);
    paintReadout (g, { grid.getX(), grid.getY() + cellH + gap, cellW, cellH }, "PEAK", readings.truePeakDb, readings.truePeakDb > -99.0f, "dB",
                  over ? juce::String ("over the ceiling") : juce::String ("the loudest point"), over ? danger : theme::text);
    paintReadout (g, { grid.getX() + cellW + gap, grid.getY() + cellH + gap, cellW, cellH }, "RANGE", readings.loudnessRangeLu,
                  readings.loudnessRangeValid, "dB", "from quiet parts to loud", theme::text);

    paintGraph (g);
    paintKnobsAndSqueeze (g);
}

void MasterPanel::paintReadout (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& name, float value, bool valid,
                                const juce::String& unit, const juce::String& note, juce::Colour colour)
{
    g.setColour (panelLight);
    g.fillRoundedRectangle (area.toFloat(), 10.0f);

    auto r = area.reduced (14, 10);
    g.setColour (textFaint);
    g.setFont (labelFont());
    g.drawText (name, r.removeFromTop (16), juce::Justification::centredLeft, true);

    const auto valueFont = font (std::min (34.0f, std::max (20.0f, r.getHeight() * 0.42f)), true);
    auto line = r.removeFromTop ((int) valueFont.getHeight() + 6);
    const auto shown = valid ? juce::String (value, 1) : juce::String ("-");
    g.setColour (valid ? colour : textFaint);
    g.setFont (valueFont);
    const int valueWidth = (int) juce::GlyphArrangement::getStringWidth (valueFont, shown) + 8;
    g.drawText (shown, line.removeFromLeft (valueWidth), juce::Justification::bottomLeft, true);

    g.setColour (textDim);
    g.setFont (font (16.0f));
    g.drawText (unit, line.withTrimmedBottom (4), juce::Justification::bottomLeft, true);

    g.setColour (textDim);
    g.setFont (font (16.0f));
    g.drawFittedText (note, r, juce::Justification::topLeft, 2, 1.0f);
}

void MasterPanel::paintGraph (juce::Graphics& g)
{
    auto area = graphArea;
    g.setColour (textFaint);
    g.setFont (labelFont());
    g.drawText ("THE LAST 30 SECONDS", area.removeFromTop (16), juce::Justification::centredLeft, true);
    area.removeFromTop (8);

    g.setColour (panelLight);
    g.fillRoundedRectangle (area.toFloat(), 10.0f);
    const auto plot = area.reduced (12, 10).withTrimmedLeft (34).toFloat();

    auto yFor = [&plot] (float lufs)
    {
        return juce::jmap (juce::jlimit (graphBottom, graphTop, lufs), graphBottom, graphTop, plot.getBottom(), plot.getY());
    };

    // Lines every 6 dB, numbered
    g.setFont (font (15.0f));

    for (float v = -6.0f; v > graphBottom; v -= 6.0f)
    {
        const float y = yFor (v);
        g.setColour (gridBar.withAlpha (0.7f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (textFaint);
        g.drawText (juce::String ((int) v), juce::Rectangle<float> (plot.getX() - 36.0f, y - 8.0f, 30.0f, 16.0f), juce::Justification::centredRight, true);
    }

    // The target, a dashed line with its name
    const auto* chosen = mastering::findTarget (song.mastering.target);

    if (chosen != nullptr && chosen->lufs != 0.0f)
    {
        const float y = yFor (chosen->lufs);
        const float dashes[] = { 6.0f, 5.0f };
        g.setColour (accent.withAlpha (0.9f));
        g.drawDashedLine (juce::Line<float> (plot.getX(), y, plot.getRight(), y), dashes, 2, 1.5f);
        g.setFont (font (15.0f, true));
        g.drawText (juce::String (chosen->name) + "  " + juce::String (chosen->lufs, 0), juce::Rectangle<float> (plot.getRight() - 220.0f, y - 18.0f, 216.0f, 16.0f),
                    juce::Justification::centredRight, true);
    }

    // The loudness, right to left from now
    if (history.size() < 2)
    {
        g.setColour (textFaint);
        g.setFont (font (16.0f));
        g.drawText ("Press Play to see how loud it is", plot, juce::Justification::centred, true);
        return;
    }

    juce::Path line;
    bool started = false;
    const float step = plot.getWidth() / 299.0f;

    for (size_t i = 0; i < history.size(); ++i)
    {
        const float x = plot.getRight() - (float) (history.size() - 1 - i) * step;

        if (history[i] <= graphBottom - 20.0f)
        {
            started = false;
            continue;
        }

        if (! started)
            line.startNewSubPath (x, yFor (history[i]));
        else
            line.lineTo (x, yFor (history[i]));

        started = true;
    }

    g.setColour (accentCool);
    g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void MasterPanel::paintKnobsAndSqueeze (juce::Graphics& g)
{
    // Each knob's name and setting under it
    auto caption = [&g] (juce::Rectangle<int> knob, const juce::String& name, const juce::String& value)
    {
        auto r = juce::Rectangle<int> (knob.getCentreX() - 44, knob.getBottom() + 2, 88, 32);
        g.setColour (textFaint);
        g.setFont (labelFont());
        g.drawText (name, r.removeFromTop (15), juce::Justification::centred, true);
        g.setColour (textDim);
        g.setFont (font (15.0f));
        g.drawText (value, r, juce::Justification::centred, true);
    };

    caption (louder.getBounds(), "LOUDER", std::abs (song.mastering.pushDb) < 0.05f ? juce::String ("as mixed") : db (song.mastering.pushDb) + " dB");
    caption (ceiling.getBounds(), "NEVER ABOVE", juce::String (song.mastering.ceilingDb, 1) + " dB");

    // How hard the limiter's squeezing, as a bar
    auto r = knobsArea.withTrimmedLeft (184).reduced (6, 0);

    if (r.getWidth() < 90)
        return;

    g.setColour (textFaint);
    g.setFont (labelFont());
    g.drawText ("SQUEEZING", r.removeFromTop (18).withTrimmedTop (4), juce::Justification::centredLeft, true);
    r.removeFromTop (8);

    auto bar = r.removeFromTop (12).toFloat();
    g.setColour (titleBar);
    g.fillRoundedRectangle (bar, 6.0f);
    const float amount = juce::jlimit (0.0f, 1.0f, squeezeDb / 12.0f);

    if (amount > 0.002f)
    {
        g.setColour (squeezeDb > 6.0f ? danger : accent);
        g.fillRoundedRectangle (bar.withWidth (std::max (6.0f, bar.getWidth() * amount)), 6.0f);
    }

    r.removeFromTop (6);
    g.setColour (textDim);
    g.setFont (font (15.0f));
    g.drawFittedText (squeezeDb < 0.1f ? juce::String ("Not squeezing")
                                       : "Loudest parts down " + juce::String (squeezeDb, 1) + " dB" + (squeezeDb > 6.0f ? " (hard)" : ""),
                      r.removeFromTop (36), juce::Justification::topLeft, 2, 1.0f);
}

//==============================================================================
void MasterPanel::paintShapePage (juce::Graphics& g)
{
    static const char* const titles[] { "TONE", "PUNCH", "WIDTH" };
    const auto& m = song.mastering;

    // Each knob's name and setting under it
    auto caption = [&g] (const juce::Slider& knob, int width, const juce::String& name, const juce::String& value)
    {
        auto r = juce::Rectangle<int> (knob.getBounds().getCentreX() - width / 2, knob.getBottom() + 2, width, 32);
        g.setColour (textFaint);
        g.setFont (labelFont());
        g.drawText (name, r.removeFromTop (15), juce::Justification::centred, true);
        g.setColour (textDim);
        g.setFont (font (15.0f));
        g.drawText (value, r, juce::Justification::centred, true);
    };

    for (int c = 0; c < 3; ++c)
    {
        g.setColour (panelLight);
        g.fillRoundedRectangle (cards[c].toFloat(), 12.0f);
        g.setColour (textFaint);
        g.setFont (labelFont());
        g.drawText (titles[c], cards[c].reduced (14, 0).withY (cards[c].getY() + 12).withHeight (22), juce::Justification::centredLeft, true);

        // Its switch: on, lit; off, it's bypassed (to hear the song without it)
        const bool on = sectionOn (c);
        const auto sw = switches[c].toFloat();
        g.setColour (on ? accentCool.withAlpha (hoveredSwitch == c ? 0.95f : 0.8f) : (hoveredSwitch == c ? raised : titleBar));
        g.fillRoundedRectangle (sw, 11.0f);
        g.setColour (on ? background : textDim);
        g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ on ? sw.getRight() - 11.0f : sw.getX() + 11.0f, sw.getCentreY() }));
        g.setColour (on ? textDim : textFaint);
        g.setFont (font (15.0f, true));
        g.drawText (on ? "ON" : "OFF", sw.withX (sw.getX() - 40.0f).withWidth (34.0f).toNearestInt(), juce::Justification::centredRight, true);
    }

    // Tone
    paintToneCurve (g);
    static const char* const whereNames[] { "Everything", "Centre", "Sides" };

    for (int b = 0; b < 4; ++b)
    {
        const float v = m.toneDb[(size_t) b];
        caption (toneKnob[b], cards[0].getWidth() / 4, juce::String (mastering::Shaper::toneBandName (b)).toUpperCase(),
                 std::abs (v) < 0.05f ? juce::String ("flat") : db (v) + " dB");

        const auto chip = whereChips[b].toFloat();
        const int where = m.toneWhere[(size_t) b];
        g.setColour (hoveredChip == b ? raised : titleBar);
        g.fillRoundedRectangle (chip, 7.0f);
        g.setColour (where == 0 ? hairlineStrong : (where == 1 ? accentCool : accent).withAlpha (0.6f));
        g.drawRoundedRectangle (chip.reduced (0.5f), 7.0f, 1.0f);
        g.setColour (where == 0 ? textDim : theme::text);
        g.setFont (font (15.0f));
        g.drawText (whereNames[where], chip.toNearestInt(), juce::Justification::centred, true);
    }

    // Punch
    caption (punchKnob, cards[1].getWidth() / 4, "AMOUNT", juce::String (juce::roundToInt (m.punchAmount * 100.0f)) + "%");

    for (int b = 0; b < 3; ++b)
        caption (shareKnob[b], cards[1].getWidth() / 4, juce::String (mastering::Shaper::punchBandName (b)).toUpperCase(),
                 juce::String (juce::roundToInt (m.punchShare[(size_t) b] * 100.0f)) + "%");

    {
        auto bars = punchBars;

        if (m.punchAmount < 0.001f)
        {
            g.setColour (textFaint);
            g.setFont (font (16.0f));
            g.drawFittedText ("Turn Amount up to tighten it: the loudest moments come closer to the rest, the level stays the same.",
                              bars.withTrimmedTop (4), juce::Justification::topLeft, 3, 1.0f);
        }
        else
        {
            const int rowHeight = std::min (28, bars.getHeight() / 3);

            for (int b = 0; b < 3; ++b)
            {
                auto row = bars.removeFromTop (rowHeight);
                g.setColour (textDim);
                g.setFont (font (15.0f));
                g.drawText (mastering::Shaper::punchBandName (b), row.removeFromLeft (52), juce::Justification::centredLeft, true);
                const auto amount = punchShown[b];
                g.drawText (amount < 0.1f ? juce::String ("-") : juce::String (amount, 1) + " dB", row.removeFromRight (56), juce::Justification::centredRight, true);

                auto bar = row.withSizeKeepingCentre (row.getWidth() - 8, 10).toFloat();
                g.setColour (titleBar);
                g.fillRoundedRectangle (bar, 5.0f);

                if (amount > 0.05f)
                {
                    g.setColour (amount > 6.0f ? danger : accent);
                    g.fillRoundedRectangle (bar.withWidth (std::max (6.0f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, amount / 10.0f))), 5.0f);
                }
            }
        }
    }

    // Width
    caption (widthKnob, cards[2].getWidth() / 2, "WIDTH",
             m.width < 0.01f ? juce::String ("mono") : std::abs (m.width - 1.0f) < 0.01f ? juce::String ("as mixed")
                                                                                        : juce::String (juce::roundToInt (m.width * 100.0f)) + "%");
    caption (monoBassKnob, cards[2].getWidth() / 2, "MONO BASS",
             m.monoBassHz < 5.0f ? juce::String ("off") : "below " + juce::String (juce::roundToInt (m.monoBassHz)) + " Hz");

    {
        auto s = stereoArea;
        g.setColour (textFaint);
        g.setFont (labelFont());
        g.drawText ("IN MONO", s.removeFromTop (18), juce::Justification::centredLeft, true);
        s.removeFromTop (4);

        // Phase correlation, -1 to +1: right of the middle holds up in mono, left of it cancels
        auto track = s.removeFromTop (12).toFloat();
        g.setColour (titleBar);
        g.fillRoundedRectangle (track, 6.0f);
        g.setColour (danger.withAlpha (0.25f));
        g.fillRoundedRectangle (track.withWidth (track.getWidth() * 0.5f), 6.0f);
        g.setColour (hairlineStrong);
        g.fillRect (juce::Rectangle<float> (1.0f, track.getHeight() + 4.0f).withCentre (track.getCentre()));

        if (stereo.windowValid)
        {
            const float x = track.getX() + track.getWidth() * (juce::jlimit (-1.0f, 1.0f, stereo.correlation) + 1.0f) * 0.5f;
            g.setColour (stereo.correlation < 0.0f ? danger : ledOn);
            g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ x, track.getCentreY() }));
        }

        s.removeFromTop (6);
        juce::String words;
        auto colour = textDim;

        if (! stereo.windowValid)
        {
            words = "Play to see how it holds up when it's played in mono (one speaker, a phone, a club).";
        }
        else if (stereo.correlation < 0.0f)
        {
            words = "Parts cancel out in mono: some of the song disappears on one speaker. Bring Width down.";
            colour = danger;
        }
        else
        {
            words = "Holds up in mono" + (stereo.monoLossDb > 0.05f ? ", " + juce::String (stereo.monoLossDb, 1) + " dB quieter" : juce::String())
                    + ". " + (stereo.width > 0.35f ? "Wide." : stereo.width > 0.12f ? "Nicely spread." : "Mostly in the middle.");
        }

        g.setColour (colour);
        g.setFont (font (16.0f));
        g.drawFittedText (words, s, juce::Justification::topLeft, std::max (1, s.getHeight() / 19), 1.0f);
    }
}

void MasterPanel::paintToneCurve (juce::Graphics& g)
{
    const auto& m = song.mastering;
    const auto area = curveArea.toFloat();
    g.setColour (titleBar.withAlpha (0.55f));
    g.fillRoundedRectangle (area, 8.0f);
    const auto plot = area.reduced (10.0f, 8.0f);
    constexpr double rate = 48000.0;

    auto xFor = [&plot] (double hz) { return plot.getX() + plot.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
    auto yFor = [&plot] (double gain) { return juce::jmap ((float) juce::jlimit (-8.0, 8.0, gain), -8.0f, 8.0f, plot.getBottom(), plot.getY()); };

    // Flat, and 100 Hz, 1 kHz and 10 kHz
    g.setColour (gridBar);
    g.drawHorizontalLine ((int) yFor (0.0), plot.getX(), plot.getRight());
    g.setFont (font (15.0f));

    for (auto [hz, name] : { std::pair (100.0, "100"), std::pair (1000.0, "1k"), std::pair (10000.0, "10k") })
    {
        const float x = xFor (hz);
        g.setColour (gridBar.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (textFaint);
        g.drawText (name, juce::Rectangle<float> (x + 3.0f, plot.getBottom() - 14.0f, 30.0f, 14.0f), juce::Justification::centredLeft, true);
    }

    // The curve for the centre and the sides: the same unless a band works on only one of them
    auto curve = [&] (int where)
    {
        juce::Path path;

        for (int i = 0; i <= 120; ++i)
        {
            const double hz = 20.0 * std::pow (1000.0, i / 120.0);
            double total = 0.0;

            if (m.toneOn)
                for (int b = 0; b < 4; ++b)
                    if (m.toneWhere[(size_t) b] == 0 || m.toneWhere[(size_t) b] == where)
                        total += mastering::Shaper::toneCoeffs (b, m.toneDb[(size_t) b], rate).magnitudeDb (hz, rate);

            if (i == 0)
                path.startNewSubPath (xFor (hz), yFor (total));
            else
                path.lineTo (xFor (hz), yFor (total));
        }

        return path;
    };

    bool split = false;

    for (int b = 0; b < 4; ++b)
        split = split || (m.toneWhere[(size_t) b] != 0 && std::abs (m.toneDb[(size_t) b]) > 0.001f);

    g.setFont (font (15.0f, true));

    if (split)
    {
        g.setColour (accent);
        g.strokePath (curve (2), juce::PathStrokeType (2.0f));
        g.drawText ("Sides", plot.withTrimmedLeft (64).withHeight (14).toNearestInt(), juce::Justification::centredLeft, true);
    }

    g.setColour (accentCool);
    g.strokePath (curve (1), juce::PathStrokeType (2.0f));
    g.drawText (split ? "Centre" : "Everything", plot.withHeight (14).toNearestInt(), juce::Justification::centredLeft, true);
}

void MasterPanel::paintOverChildren (juce::Graphics& g)
{
    // A section switched off: dimmed under its title, knobs and all
    if (page != 1)
        return;

    for (int c = 0; c < 3; ++c)
        if (! sectionOn (c))
        {
            g.setColour (panelLight.withAlpha (0.62f));
            g.fillRoundedRectangle (cards[c].withTrimmedTop (40).toFloat(), 12.0f);
        }
}

//==============================================================================
void MasterPanel::mouseMove (const juce::MouseEvent& e)
{
    juce::String over;
    int chip = -1, sw = -1;

    if (page == 0)
    {
        for (auto& [id, area] : targetRows)
            if (area.contains (e.getPosition()))
                over = id;
    }
    else
    {
        for (int b = 0; b < 4; ++b)
            if (whereChips[b].contains (e.getPosition()))
                chip = b;

        for (int c = 0; c < 3; ++c)
            if (switches[c].expanded (40, 4).contains (e.getPosition()))
                sw = c;
    }

    if (over != hoveredTarget || chip != hoveredChip || sw != hoveredSwitch)
    {
        hoveredTarget = over;
        hoveredChip = chip;
        hoveredSwitch = sw;
        setMouseCursor (over.isNotEmpty() || chip >= 0 || sw >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void MasterPanel::mouseExit (const juce::MouseEvent&)
{
    hoveredTarget = {};
    hoveredChip = hoveredSwitch = -1;
    repaint();
}

void MasterPanel::mouseDown (const juce::MouseEvent& e)
{
    if (page == 0)
    {
        for (auto& [id, area] : targetRows)
            if (area.contains (e.getPosition()))
                chooseTarget (id);

        return;
    }

    for (int b = 0; b < 4; ++b)
        if (whereChips[b].contains (e.getPosition()))
            cycleWhere (b);

    for (int c = 0; c < 3; ++c)
        if (switches[c].expanded (40, 4).contains (e.getPosition()))
            toggleSection (c);
}

juce::String MasterPanel::getTooltip()
{
    const auto p = getMouseXYRelative();

    if (page == 1)
    {
        for (int b = 0; b < 4; ++b)
            if (whereChips[b].contains (p))
                return "Click to choose what this band works on: everything, the centre (vocals, kick, bass, snare) "
                       "or the sides (wide guitars, keys, reverb). Air on the sides opens a mix up without making the vocal hiss";

        for (int c = 0; c < 3; ++c)
            if (switches[c].expanded (40, 4).contains (p))
                return "Switch it off to hear the song without it, and on again to compare";

        if (curveArea.contains (p))
            return "What Tone does to the sound: above the line is louder at that pitch, below is quieter. Low notes on the left, high on the right";

        if (punchBars.contains (p))
            return "How hard each band is being squeezed right now. A few dB is normal; red means it's working very hard";

        if (stereoArea.contains (p))
            return "How the song holds up played in mono (one speaker, a phone, a club): the dot right of the middle is fine, "
                   "left of it means parts cancel out and disappear";

        return {};
    }

    for (auto& [id, area] : targetRows)
        if (area.contains (p))
            if (const auto* t = mastering::findTarget (id))
                return t->lufs == 0.0f ? juce::String ("Keeps anything from going over the ceiling and leaves the loudness alone")
                                       : juce::String (t->detail) + ". " + lufsNote;

    if (readoutsArea.contains (p))
        return juce::String (lufsNote) + " The peak is the loudest point, between the samples too; the range is how far the "
                                         "loudness moves between the quiet parts and the loud ones.";

    if (graphArea.contains (p))
        return "How loud the last 30 seconds were (the last 3 seconds at each point), against what it's for";

    return {};
}

} // namespace nightshift
