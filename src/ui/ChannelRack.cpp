// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "ChannelRack.h"
#include "ComputerKeyboard.h"
#include "audio/DrumKit.h"
#include "audio/fx/StockEffects.h"
#include "audio/Kits.h"
#include "audio/Instruments.h"
#include "ai/Drummer.h"
#include "audio/Nova.h"
#include "audio/GuitarSound.h"
#include "audio/MidiFiles.h"
#include <set>

namespace nightshift
{

using namespace theme;

namespace
{
    // A row: the mute swatch, the name, level and pan, then a bar of steps (16 in 4/4, 14 in
    // 7/8; or an audio track's controls). Pads share out the width left, within limits, in
    // groups of a beat
    constexpr int nameLeft = 28, nameWidth = 148, barsLeft = 188, barsWidth = 64, stepsLeft = 268;
    constexpr int padGap = 4, groupGap = 8, minPadWidth = 16, maxPadWidth = 40, padHeight = 26;

    int padWidthFor (int rowWidth, int steps, int group)
    {
        const int groups = (steps + group - 1) / group;
        const int available = rowWidth - stepsLeft - 14 - (steps - 1) * padGap - (groups - 1) * groupGap;
        return juce::jlimit (minPadWidth, maxPadWidth, available / std::max (1, steps));
    }

    int padX (int step, int padWidth, int group)
    {
        return stepsLeft + step * (padWidth + padGap) + (step / group) * groupGap;
    }

    /** Steps to a group of pads: a beat in x/4, a dotted quarter in 6/8, 9/8 and 12/8, an
        eighth in 5/8 or 7/8. */
    int stepGroupFor (const Song& song)
    {
        // The pattern's time signature, its own or the song's
        const auto* pattern = song.findPattern (song.currentPatternId);
        const auto [beats, unit] = pattern != nullptr ? song.meterOf (*pattern) : std::pair (song.meterBeats, song.meterUnit);

        if (unit == 8)
            return beats % 3 == 0 && beats > 3 ? 6 : 2;

        return 4;
    }

    juce::String volumeText (double v)
    {
        const float dbValue = channelVolumeToDb ((float) v);
        return "Volume " + juce::String (juce::roundToInt (v * 100.0)) + "%  (" + (dbValue <= -99.0f ? "-inf" : juce::String (dbValue, 1)) + " dB)";
    }

    juce::String panText (double v)
    {
        if (std::abs (v) < 0.01)
            return "Pan: centred";

        return "Pan " + juce::String (juce::roundToInt (std::abs (v) * 100.0)) + (v < 0 ? "% left" : "% right");
    }

    juce::String typeName (const Channel& c)
    {
        switch (c.type)
        {
            case InstrumentType::synth:   return "Synth";
            case InstrumentType::plugin:  return "Plugin";
            case InstrumentType::audio:   return "Audio";
            case InstrumentType::kit:     return "Drums";
            case InstrumentType::instrument: return isGuitar (c) ? "Guitar" : "Instrument";
            case InstrumentType::midiOut: return "Plugged in";
            case InstrumentType::sampler: break;
        }

        return c.sample.startsWith ("kit:") ? "Kit" : "Sample";
    }
}

//==============================================================================
class ChannelRack::Row : public juce::Component,
                         public juce::TooltipClient
{
public:
    Row (ChannelRack& r, int id) : rack (r), channelId (id)
    {
        volume.formatValue = volumeText;
        pan.formatValue = panText;
        pan.setFillColour (accentCool);

        for (auto* bar : { &volume, &pan })
        {
            addAndMakeVisible (*bar);
            bar->onDragStart = [this] { rack.song.checkpoint(); };
        }

        volume.onValueChange = [this]
        {
            if (auto c = channel())
            {
                c->volume = (float) volume.getValue();
                rack.song.notify (Song::Change::mixer);
            }
        };

        pan.onValueChange = [this]
        {
            if (auto c = channel())
            {
                c->pan = (float) pan.getValue();
                rack.song.notify (Song::Change::mixer);
            }
        };

        refresh();
    }

    Channel* channel() const    { return rack.song.findChannel (channelId); }

    void refresh()
    {
        if (auto c = channel())
        {
            volume.setValue (c->volume, juce::dontSendNotification);
            pan.setValue (c->pan, juce::dontSendNotification);
            volume.setTooltip (volumeText (c->volume));
            pan.setTooltip (panText (c->pan));
        }

        repaint();
    }

    void resized() override
    {
        const int h = getHeight();
        muteArea = { 12, (h - 22) / 2, 7, 22 };
        nameArea = { nameLeft, 0, nameWidth, h };
        volume.setBounds (barsLeft, h / 2 - 9, barsWidth, 8);
        pan.setBounds (barsLeft, h / 2 + 1, barsWidth, 8);

        stepCount = rack.song.barTicksOf (rack.song.currentPattern()) / ticksPerStep;
        stepGroup = stepGroupFor (rack.song);
        padWidth = padWidthFor (getWidth(), stepCount, stepGroup);
        stepsArea = { stepsLeft, (h - padHeight) / 2, padX (stepCount - 1, padWidth, stepGroup) + padWidth - stepsLeft, padHeight };

        // Audio channels: arm, input, monitor and an input meter in place of the steps
        auto audioRow = stepsArea.withWidth (std::max (stepsArea.getWidth(), 440));
        armArea = audioRow.removeFromLeft (36);
        audioRow.removeFromLeft (8);
        inputArea = audioRow.removeFromLeft (176);
        audioRow.removeFromLeft (8);
        monArea = audioRow.removeFromLeft (54);
        audioRow.removeFromLeft (12);
        meterArea = audioRow;
    }

    bool isAudio() const
    {
        auto c = channel();
        return c != nullptr && c->type == InstrumentType::audio;
    }

    juce::String getTooltip() override
    {
        const auto p = getMouseXYRelative();

        if (muteArea.expanded (6).contains (p))
            return "Click to mute or unmute; right-click to solo";

        if (fxArea.contains (p))
            if (auto c = channel())
            {
                juce::StringArray names;

                for (auto& e : c->effects)
                    names.add (e.name + (e.bypassed ? " (bypassed)" : ""));

                return "Effects: " + names.joinIntoString (", ") + ". Click to change them";
            }

        if (nameArea.contains (p))
            return "Click to select and hear it; double-click to load a sample (or open a plugin); right-click for more";

        if (! isAudio())
            return {};

        if (armArea.contains (p))
            return "Arm: with Record armed, Space records every armed track";

        if (inputArea.contains (p))
            return "The input this track records. On the MiniFuse, MIC/LINE/INST 1 is the front left jack: "
                   "press INST on it for guitar or bass";

        if (monArea.contains (p))
            return "Hear the input through NightShift while armed (for its effects). Leave it off and use the "
                   "MiniFuse's direct monitoring to hear yourself with no delay at all";

        if (meterArea.contains (p))
            return "Input level. Set the gain on the interface so your loudest playing peaks in the shaded zone "
                   "(-18 to -6 dB); LOW means turn it up. Click to clear the clip light";

        return {};
    }

    /** Called at the UI rate with the input's peak since last time. */
    void setInputLevel (float peak)
    {
        const float decayed = inputLevel * 0.82f;
        inputLevel = std::max (peak, decayed);

        // The peak readout holds for 1.5 s, then falls at 15 dB a second
        if (peak >= inputHold)
        {
            inputHold = peak;
            holdFrames = 45;
        }
        else if (--holdFrames < 0)
        {
            inputHold *= 0.89f;

            if (inputHold < 0.0001f)
                inputHold = 0.0f;
        }

        inputClipped = inputClipped || peak >= 0.999f;
        repaint (meterArea.expanded (2));
    }

    juce::Rectangle<float> stepBounds (int step) const
    {
        return juce::Rectangle<int> (padX (step, padWidth, stepGroup), stepsArea.getY(), padWidth, stepsArea.getHeight()).toFloat();
    }

    int stepAt (juce::Point<int> p) const
    {
        for (int i = 0; i < stepCount; ++i)
            if (stepBounds (i).expanded (padGap * 0.5f + 0.5f, 4.0f).contains (p.toFloat()))
                return i;

        return -1;
    }

    const std::vector<Note>* notes() const
    {
        return rack.song.currentPattern().findNotes (channelId);
    }

    bool showsSteps() const
    {
        auto c = channel();
        auto n = notes();
        return c != nullptr && c->type != InstrumentType::audio && (n == nullptr || steps::isStepSequence (*n, c->rootKey, rack.song.barTicksOf (rack.song.currentPattern())));
    }

    void paint (juce::Graphics& g) override
    {
        auto c = channel();

        if (c == nullptr)
            return;

        // The selected channel (the one in the Inspector): a periwinkle bar and a wash fading
        // out along the row
        if (rack.song.selectedChannelId == channelId)
        {
            g.setGradientFill (juce::ColourGradient (accentCool.withAlpha (0.18f), 0.0f, 0.0f,
                                                     accentCool.withAlpha (0.03f), getWidth() * 0.7f, 0.0f, false));
            g.fillRect (getLocalBounds());
            g.setColour (accentCool);
            g.fillRoundedRectangle (0.0f, 4.0f, 3.0f, getHeight() - 8.0f, 1.5f);
        }

        // Mute swatch: the channel's colour, hollow while muted
        auto m = muteArea.toFloat();

        if (c->muted)
        {
            g.setColour (c->colour.withAlpha (0.55f));
            g.drawRoundedRectangle (m.reduced (0.75f), 3.0f, 1.5f);
        }
        else
        {
            g.setColour (c->colour);
            g.fillRoundedRectangle (m, 3.0f);
        }

        // What it is, or with effects on it, how many (click for them)
        int typeWidth = 0;

        if (c->effects.empty())
        {
            fxArea = {};
            g.setColour (textFaint);
            g.setFont (font (15.0f));
            const auto type = typeName (*c);
            typeWidth = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), type) + 4;
            g.drawText (type, nameArea.withTrimmedRight (8), juce::Justification::centredRight, true);
        }
        else
        {
            const auto label = "FX " + juce::String ((int) c->effects.size());
            g.setFont (font (15.0f, true));
            typeWidth = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 14;
            fxArea = nameArea.withTrimmedRight (6).removeFromRight (typeWidth).withSizeKeepingCentre (typeWidth, 20);
            const bool allBypassed = std::all_of (c->effects.begin(), c->effects.end(), [] (auto& e) { return e.bypassed; });
            g.setColour (effectColour.withAlpha (allBypassed ? 0.06f : 0.14f));
            g.fillRoundedRectangle (fxArea.toFloat(), 5.0f);
            g.setColour (allBypassed ? textFaint : effectColour);
            g.drawText (label, fxArea, juce::Justification::centred, true);
        }

        g.setColour (c->muted ? textFaint : text);
        g.setFont (font (17.5f, true));
        g.drawText (c->name, nameArea.withTrimmedRight (typeWidth + 12), juce::Justification::centredLeft, true);

        if (c->type == InstrumentType::audio)
            paintAudio (g, *c);
        else if (showsSteps())
            paintSteps (g, *c);
        else
            paintPreview (g, *c);
    }

    void paintSteps (juce::Graphics& g, const Channel& c)
    {
        auto list = notes();

        for (int i = 0; i < stepCount; ++i)
        {
            auto r = stepBounds (i);
            const auto* note = list != nullptr ? steps::find (*list, i, c.rootKey) : nullptr;
            const bool now = i == rack.playingStep;

            if (note != nullptr)
            {
                // Lit in the channel's own colour, brighter for a harder hit
                const float strength = juce::jmap ((float) note->velocity, 1.0f, 127.0f, 0.45f, 1.0f) * (c.muted ? 0.5f : 1.0f);
                auto lit = titleBar.interpolatedWith (c.colour, strength);

                if (now)
                {
                    lit = lit.interpolatedWith (juce::Colours::white, 0.12f);
                    g.setColour (c.colour.withAlpha (0.35f));
                    g.fillRoundedRectangle (r.expanded (2.0f), 8.0f);
                }

                g.setColour (lit);
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRoundedRectangle (r.reduced (4.0f, 0.0f).withHeight (1.0f).translated (0.0f, 1.0f), 0.5f);
            }
            else
            {
                g.setColour (now ? titleBar.brighter (0.08f) : titleBar);
                g.fillRoundedRectangle (r, 6.0f);
            }

            if (now)
            {
                g.setColour (accent.withAlpha (note != nullptr ? 0.95f : 0.7f));
                g.fillRoundedRectangle (r.getX() + 4.0f, r.getBottom() - 3.0f, r.getWidth() - 8.0f, 2.0f, 1.0f);
            }
            else if (i == hoverStep)
            {
                g.setColour (text.withAlpha (0.3f));
                g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            }
        }
    }

    void paintAudio (juce::Graphics& g, const Channel& c)
    {
        // Arm: a record button that glows when this track will record
        auto arm = armArea.toFloat();
        g.setColour (c.armed ? danger.withAlpha (0.16f) : panelLight);
        g.fillRoundedRectangle (arm, 7.0f);
        g.setColour (c.armed ? danger.withAlpha (0.45f) : hairline);
        g.drawRoundedRectangle (arm.reduced (0.5f), 7.0f, 1.0f);
        auto dot = juce::Rectangle<float> (10.0f, 10.0f).withCentre (arm.getCentre());
        g.setColour (c.armed ? danger : danger.withAlpha (0.5f));
        g.fillEllipse (dot);

        // Input picker
        auto in = inputArea.toFloat();
        g.setColour (panelLight);
        g.fillRoundedRectangle (in, 7.0f);
        g.setColour (hairlineStrong);
        g.drawRoundedRectangle (in.reduced (0.5f), 7.0f, 1.0f);
        const bool found = rack.audio.getInputNames().contains (c.input);
        g.setColour (c.input.isEmpty() || ! found ? textFaint : text);
        g.setFont (font (16.0f, true));
        g.drawFittedText (c.input.isEmpty() ? juce::String ("No input") : (found ? c.input : c.input + " (missing)"),
                          inputArea.reduced (10, 0).withTrimmedRight (16), juce::Justification::centredLeft, 1);
        auto chevron = icons::chevronDown();
        chevron.applyTransform (juce::AffineTransform::scale (1.4f).translated (in.getRight() - 22.0f, in.getCentreY() - 7.0f));
        g.setColour (textFaint);
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Monitor: hear the input through NightShift while armed
        auto mon = monArea.toFloat();
        g.setColour (c.monitor ? accentCool.withAlpha (0.14f) : panelLight);
        g.fillRoundedRectangle (mon, 7.0f);
        g.setColour (c.monitor ? accentCool.withAlpha (0.4f) : hairline);
        g.drawRoundedRectangle (mon.reduced (0.5f), 7.0f, 1.0f);
        g.setColour (c.monitor ? accentCool : textFaint);
        g.setFont (labelFont());
        g.drawText ("MON", monArea, juce::Justification::centred, true);

        // Input meter, -60..0 dB, with the zone a take should peak in (-18..-6 dB) shaded,
        // and the recent peak as a number: amber and LOW when the gain wants turning up
        auto all = meterArea.toFloat();
        auto readout = all.removeFromRight (60.0f);
        auto m = all.withTrimmedRight (6.0f).withSizeKeepingCentre (all.getWidth() - 6.0f, 8.0f);
        g.setColour (titleBar);
        g.fillRoundedRectangle (m, 4.0f);

        auto dbToX = [&m] (float db)  { return m.getX() + m.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        auto toX = [&dbToX] (float gain)  { return dbToX (juce::Decibels::gainToDecibels (gain, -60.0f)); };

        g.setColour (ledOn.withAlpha (0.14f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (dbToX (-18.0f), m.getY(), dbToX (-6.0f), m.getBottom()));

        juce::ColourGradient heat (juce::Colour (0xff4fd18a), m.getX(), 0.0f, danger, m.getRight(), 0.0f, false);
        heat.addColour (0.8, juce::Colour (0xffffcf5a));
        g.setGradientFill (heat);
        g.fillRoundedRectangle (m.withRight (std::max (m.getX(), toX (inputLevel))), 4.0f);

        if (inputHold > 0.001f)
        {
            g.setColour (text.withAlpha (0.8f));
            g.fillRect (toX (inputHold) - 1.0f, m.getY(), 1.5f, m.getHeight());
        }

        if (inputClipped)
        {
            g.setColour (danger);
            g.drawRoundedRectangle (m.expanded (1.0f), 5.0f, 1.5f);
        }

        const float holdDb = juce::Decibels::gainToDecibels (inputHold, -100.0f);
        const bool present = holdDb > -60.0f, low = present && holdDb < -30.0f;
        g.setColour (inputClipped || holdDb > -1.0f ? danger : low ? accent : present ? text : textFaint);
        g.setFont (monoFont (16.0f, true));
        g.drawText (! present ? juce::String ("--") : juce::String (juce::roundToInt (holdDb)) + (low ? " LOW" : " dB"),
                    readout.toNearestInt(), juce::Justification::centredRight, true);
    }

    void paintPreview (juce::Graphics& g, const Channel& c)
    {
        // A melody: a miniature of its notes; click to open it in the piano roll
        auto area = stepsArea.toFloat();
        g.setColour (panelLight);
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (hairlineStrong);
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);

        auto list = notes();

        if (list == nullptr || list->empty())
            return;

        const int length = rack.song.patternLength (rack.song.currentPattern());
        int lo = 127, hi = 0;

        for (auto& n : *list)
        {
            lo = std::min (lo, n.key);
            hi = std::max (hi, n.key);
        }

        lo -= 2;
        hi += 2;
        auto inner = area.reduced (6.0f, 4.0f);

        for (int bar = 1; bar * rack.song.barTicksOf (rack.song.currentPattern()) < length; ++bar)
        {
            g.setColour (hairlineStrong);
            const float x = inner.getX() + inner.getWidth() * bar * rack.song.barTicksOf (rack.song.currentPattern()) / (float) length;
            g.fillRect (x, area.getY() + 3.0f, 1.0f, area.getHeight() - 6.0f);
        }

        g.setColour (c.muted ? c.colour.withAlpha (0.5f) : c.colour);
        const float rowH = std::max (1.5f, inner.getHeight() / (float) (hi - lo + 1));

        for (auto& n : *list)
        {
            const float x = inner.getX() + inner.getWidth() * n.start / (float) length;
            const float w = std::max (1.5f, inner.getWidth() * n.length / (float) length - 1.0f);
            const float y = inner.getBottom() - (n.key - lo + 1) * rowH;
            g.fillRoundedRectangle (x, y, w, std::max (1.5f, rowH - 0.5f), 0.75f);
        }

        if (rack.previewPlayhead >= 0.0)
        {
            g.setColour (playhead);
            const float x = inner.getX() + inner.getWidth() * (float) (rack.previewPlayhead / length);
            g.fillRect (x, area.getY() + 3.0f, 1.5f, area.getHeight() - 6.0f);
        }
    }

    //==============================================================================
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int s = showsSteps() ? stepAt (e.getPosition()) : -1;

        if (s != hoverStep)
        {
            hoverStep = s;
            repaint();
        }

        const auto p = e.getPosition();
        setMouseCursor (stepsArea.contains (p) || nameArea.contains (p) || muteArea.expanded (6).contains (p)
                          ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverStep = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto c = channel();

        if (c == nullptr)
            return;

        const auto p = e.getPosition();

        if (muteArea.expanded (6).contains (p))
        {
            if (e.mods.isPopupMenu() || e.mods.isCtrlDown())
            {
                rack.toggleSolo (channelId);
                return;
            }

            rack.song.checkpoint();
            c->muted = ! c->muted;
            rack.song.notify (Song::Change::mixer);
            return;
        }

        if (fxArea.contains (p))
        {
            rack.createEffectsMenu (channelId).showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (fxArea + getScreenPosition()));
            return;
        }

        if (nameArea.contains (p))
        {
            if (e.mods.isPopupMenu())
            {
                rack.showChannelMenu (channelId);
            }
            else
            {
                rack.selectChannel (channelId, true);
                draggableName = true;      // dragging it onto Arrange gives it a track
            }

            return;
        }

        if (c->type == InstrumentType::audio)
        {
            if (armArea.contains (p))
            {
                // Arming isn't saved, so it needs no undo step
                c->armed = ! c->armed;
                rack.song.notify (Song::Change::mixer);
                rack.selectChannel (channelId, false);
            }
            else if (inputArea.contains (p))
            {
                rack.showInputMenu (channelId, inputArea + getScreenPosition());
            }
            else if (monArea.contains (p))
            {
                rack.song.checkpoint();
                c->monitor = ! c->monitor;
                rack.song.notify (Song::Change::mixer);
            }
            else if (meterArea.expanded (2).contains (p))
            {
                inputClipped = false;
                inputHold = 0.0f;
                repaint();
            }
            else
            {
                rack.selectChannel (channelId, false);
            }

            return;
        }

        if (stepsArea.contains (p))
        {
            if (! showsSteps())
            {
                rack.selectChannel (channelId, false);

                if (rack.onOpenPianoRoll != nullptr)
                    rack.onOpenPianoRoll (channelId);

                return;
            }

            const int s = stepAt (p);

            if (s < 0)
                return;

            rack.song.checkpoint();
            auto& list = rack.song.currentPattern().notesFor (channelId);
            paintValue = e.mods.isPopupMenu() ? false : steps::find (list, s, c->rootKey) == nullptr;
            painting = true;
            applyStep (s);
            return;
        }

        // Anywhere else on the row selects it
        rack.selectChannel (channelId, false);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (painting)
            applyStep (stepAt (e.getPosition()));

        if (draggableName && e.getDistanceFromDragStart() > 6)
        {
            draggableName = false;
            startDraggingChannel();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        painting = false;
        draggableName = false;
    }

    void startDraggingChannel()
    {
        auto c = channel();
        auto container = juce::DragAndDropContainer::findParentDragContainerFor (this);

        if (c == nullptr || container == nullptr)
            return;

        // A chip with the channel's colour and name, as a sound from the sidebar has
        const auto f = theme::font (17.0f, true);
        const int w = (int) juce::GlyphArrangement::getStringWidth (f, c->name) + 40, h = 30;
        juce::Image image (juce::Image::ARGB, w, h, true);
        {
            juce::Graphics g (image);
            g.setColour (theme::raised);
            g.fillRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 8.0f);
            g.setColour (c->colour);
            g.fillEllipse (12.0f, h * 0.5f - 4.0f, 8.0f, 8.0f);
            g.setColour (theme::text);
            g.setFont (f);
            g.drawText (c->name, 28, 0, w - 32, h, juce::Justification::centredLeft, true);
        }

        container->startDragging (juce::String (channelDragPrefix) + juce::String (channelId), this, juce::ScaledImage (image));
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! nameArea.contains (e.getPosition()))
            return;

        if (rack.onOpenChannel != nullptr)
        {
            rack.onOpenChannel (channelId);
            return;
        }

        if (auto c = channel())
        {
            if (c->type == InstrumentType::sampler)
                rack.browseForSample (channelId);
            else if (c->type == InstrumentType::plugin && rack.onOpenPluginEditor != nullptr)
                rack.onOpenPluginEditor (channelId);
            else
                rack.showChannelMenu (channelId);
        }
    }

    void applyStep (int s)
    {
        auto c = channel();

        if (s < 0 || c == nullptr)
            return;

        auto& list = rack.song.currentPattern().notesFor (channelId);

        if ((steps::find (list, s, c->rootKey) != nullptr) == paintValue)
            return;

        steps::set (list, s, paintValue, c->rootKey);
        rack.song.notify (Song::Change::notes);
    }

    ChannelRack& rack;
    const int channelId;

private:
    MiniBar volume { 0.0, 1.0, 0.78 }, pan { -1.0, 1.0, 0.0, true };
    juce::Rectangle<int> muteArea, nameArea, fxArea, stepsArea, armArea, inputArea, monArea, meterArea;
    int hoverStep = -1, holdFrames = 0, padWidth = maxPadWidth, stepCount = 16, stepGroup = 4;
    float inputLevel = 0.0f, inputHold = 0.0f;
    bool paintValue = true, painting = false, inputClipped = false, draggableName = false;
};

//==============================================================================
ChannelRack::ChannelRack (Song& s, AudioEngine& a)
    : song (s), audio (a)
{
    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    addButton.setFilled (false);
    addButton.setFramed (true);
    addButton.setLabel ("Add channel");
    addButton.setTooltip ("Add a sampler, synth or plugin channel");
    addButton.onClick = [this] { showAddChannelMenu(); };
    addAndMakeVisible (addButton);

    song.addListener (this);
    rebuildRows();
}

ChannelRack::~ChannelRack()
{
    song.removeListener (this);
}

int ChannelRack::getIdealHeight() const
{
    return headerHeight + juce::jlimit (3, 14, (int) song.channels.size()) * rowHeight + footerHeight + 6;
}

void ChannelRack::songChanged (Song::Change c)
{
    if (c == Song::Change::channels || c == Song::Change::everything)
    {
        rebuildRows();
        return;
    }

    // A new time signature changes how many steps a bar has
    if (c == Song::Change::transport)
        for (auto r : rows)
            r->resized();

    for (auto r : rows)
        r->refresh();

    repaint();
}

void ChannelRack::rebuildRows()
{
    rows.clear();

    for (auto& c : song.channels)
        rowHolder.addAndMakeVisible (rows.add (new Row (*this, c.id)));

    resized();
    repaint();
}

void ChannelRack::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (headerHeight);

    auto footer = r.removeFromBottom (footerHeight);
    addButton.setBounds (footer.withTrimmedLeft (12).withSizeKeepingCentre (footer.getWidth() - 12, 32).withWidth (156));

    viewport.setBounds (r);
    rowHolder.setBounds (0, 0, std::max (r.getWidth() - (rows.size() * rowHeight > r.getHeight() ? 8 : 0), 10), rows.size() * rowHeight);

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowHeight, rowHolder.getWidth(), rowHeight);
}

void ChannelRack::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    // Column labels: level and pan, and the beats over the steps
    const int steps = song.barTicksOf (song.currentPattern()) / ticksPerStep, group = stepGroupFor (song);
    const int padWidth = padWidthFor (rowHolder.getWidth(), steps, group);
    g.setColour (textFaint);
    g.setFont (labelFont());
    g.drawText ("LEVEL / PAN", barsLeft, 4, barsWidth + 20, headerHeight - 4, juce::Justification::centredLeft, true);

    for (int i = 0; i < steps; ++i)
    {
        const int x = padX (i, padWidth, group);
        g.setFont (i % group == 0 ? monoFont (15.0f, true) : font (15.0f, true));
        g.drawText (i % group == 0 ? juce::String (i / group + 1) : juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")),
                    x, 4, padWidth, headerHeight - 4, juce::Justification::centred, true);
    }

    // Footer: add a channel, and the hints
    auto footer = getLocalBounds().removeFromBottom (footerHeight);
    g.setColour (hairline);
    g.fillRect (footer.withHeight (1));
    g.setColour (textFaint);
    g.setFont (font (16.0f));
    g.drawText (juce::String ("Drag in sounds from the sidebar, or audio files") + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "))
                    + "right-click a name for more",
                footer.withTrimmedLeft (184).reduced (8, 0), juce::Justification::centredLeft, true);
}

void ChannelRack::paintOverChildren (juce::Graphics& g)
{
    if (dropRowIndex == -2)
    {
        g.setColour (accentCool.withAlpha (0.12f));
        g.fillRect (viewport.getBounds());
        g.setColour (accentCool);
        g.drawRect (viewport.getBounds(), 2);
    }
    else if (juce::isPositiveAndBelow (dropRowIndex, rows.size()))
    {
        auto rowArea = rows[dropRowIndex]->getBoundsInParent() + viewport.getPosition() - viewport.getViewPosition();
        g.setColour (accentCool.withAlpha (0.18f));
        g.fillRect (rowArea);
        g.setColour (accentCool);
        g.drawRect (rowArea, 2);
    }
}

void ChannelRack::refreshPlayhead()
{
    int step = -1;
    double preview = -1.0;

    if (audio.isPlaying())
    {
        const double ticks = audio.getPlayheadTicks();
        double withinPattern = -1.0;

        if (song.mode == PlayMode::pattern)
        {
            withinPattern = ticks;
        }
        else if (auto pattern = song.findPattern (song.currentPatternId))
        {
            // In song mode, light the steps while the edited pattern is playing
            for (auto& clip : song.playlist)
                if (clip.patternId == pattern->id && ticks >= clip.start && ticks < song.clipEnd (clip))
                    withinPattern = ticks - clip.start + clip.offset;
        }

        if (withinPattern >= 0.0)
        {
            preview = withinPattern;
            const int s = (int) (withinPattern / ticksPerStep);
            step = s < song.barTicksOf (song.currentPattern()) / ticksPerStep ? s : -1;
        }
    }

    // Input meters on audio channels
    for (auto r : rows)
        if (r->isAudio())
            if (auto c = r->channel())
                r->setInputLevel (audio.getInputPeak (c->input));

    const bool previewMoved = preview != previewPlayhead;

    if (step != playingStep || previewMoved)
    {
        playingStep = step;
        previewPlayhead = preview;

        for (auto r : rows)
            r->repaint();
    }
}

//==============================================================================
void ChannelRack::selectChannel (int channelId, bool preview)
{
    if (song.selectedChannelId != channelId)
    {
        song.selectedChannelId = channelId;
        song.notify (Song::Change::selection);
    }

    if (preview)
        if (auto c = song.findChannel (channelId))
            audio.previewNote (channelId, c->rootKey, 100, 450);
}

void ChannelRack::showAddChannelMenu()
{
    createAddMenu (nullptr).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton));
}

juce::PopupMenu ChannelRack::createAddMenu (std::function<void (int channelId)> added)
{
    // Each choice adds its channel, then passes it on to added (Arrange makes it a track)
    auto then = [added] (int id)
    {
        if (added != nullptr && id != 0)
            added (id);
    };

    juce::PopupMenu samplerMenu, synthMenu, pluginMenu;
    samplerMenu.addItem ("From audio file...", [this, added] { browseForSample (0, added); });
    samplerMenu.addSeparator();
    samplerMenu.addSectionHeader ("NightShift kit");

    for (auto& name : kit::getSoundNames())
        samplerMenu.addItem (name, [this, name, then] { then (addKitChannel (name)); });

    synthMenu = createSoundMenu ([this, then] (const juce::String& preset) { then (addSynthChannel (preset)); });

    // Plugin instruments by developer, as the sidebar has them: a submenu each, A to Z
    const auto developers = AudioEngine::pluginsByDeveloper (audio.getKnownPlugins(), true);

    for (auto& developer : developers)
    {
        juce::PopupMenu theirs;

        for (auto& desc : developer.plugins)
            theirs.addItem (desc.name, [this, desc, then] { then (addPluginChannel (desc)); });

        pluginMenu.addSubMenu (developer.name.isNotEmpty() ? developer.name : juce::String ("Unknown developer"), theirs);
    }

    if (developers.empty())
        pluginMenu.addItem ("No instruments found yet", false, false, nullptr);

    pluginMenu.addSeparator();
    pluginMenu.addItem ("Find new instruments and effects", [this] { if (onScanPlugins != nullptr) onScanPlugins(); });

    juce::PopupMenu drumMenu;

    for (auto& id : drumkits::ids())
        drumMenu.addItem (drumkits::displayName (id), [this, id, then] { then (addDrumKitChannel (id)); });

    drumMenu.addSeparator();

    if (! drumkits::haveDownloads())
        drumMenu.addItem ("Get the studio drum kits (10.5 GB)...", [this] { if (onGetKits != nullptr) onGetKits(); });

    drumMenu.addItem ("From a folder of samples...", [this] { if (onImportKit != nullptr) onImportKit(); });

    // Audio tracks named for what's recorded on them, so they get its icon
    juce::PopupMenu audioMenu;

    for (auto name : { "Guitar", "Bass", "Vocals" })
        audioMenu.addItem (name, [this, name, then] { then (addAudioChannel (name)); });

    audioMenu.addItem ("Other", [this, then] { then (addAudioChannel()); });

    juce::PopupMenu menu;
    menu.addItem ("AI drummer (a drum kit it writes for)", [this, then]
    {
        // No kits yet: the drummer's tab fetches them first
        if (drumkits::ids().isEmpty())
        {
            if (onGetKits != nullptr)
                onGetKits();

            return;
        }

        const int id = addDrumKitChannel (drumkits::ids()[0]);

        if (auto c = song.findChannel (id))
            c->drummer.pickKit = true;      // the drummer chooses it for the first part

        then (id);

        if (onOpenDrummer != nullptr)
            onOpenDrummer (id);
    });
    menu.addSubMenu ("Audio track (record guitar, bass, vocals)", audioMenu);
    menu.addSeparator();
    menu.addSubMenu ("Drum kit", drumMenu);

    auto instrumentMenu = createInstrumentMenu ([this, then] (const juce::String& path) { then (addInstrumentChannel (path)); });

    if (! instruments::haveLibraries())
    {
        instrumentMenu.addSeparator();
        instrumentMenu.addItem ("Get the piano and orchestra (2.9 GB)...", [this] { if (onGetInstruments != nullptr) onGetInstruments(); });
    }

    menu.addSubMenu ("Instrument (piano, strings, brass...)", instrumentMenu);
    menu.addSubMenu ("Sampler", samplerMenu);
    menu.addSubMenu ("NightShift Synth", synthMenu);
    menu.addSubMenu ("Plugin instrument", pluginMenu);
    menu.addSubMenu ("Synth plugged in (MIDI out)", createMidiOutMenu ([this, then] (const juce::String& output) { then (addMidiOutChannel (output)); }));
    return menu;
}

juce::PopupMenu ChannelRack::createMidiOutMenu (std::function<void (const juce::String&)> chosen, const juce::String& ticked)
{
    juce::PopupMenu menu;
    const auto outputs = audio.getMidiOutputNames();

    for (auto& name : outputs)
        menu.addItem (name, true, name == ticked, [chosen, name] { chosen (name); });

    if (outputs.isEmpty())
        menu.addItem ("None plugged in", false, false, nullptr);

    return menu;
}

namespace
{
    /** The first MIDI channel no other channel plays on an output (a synth that plays several parts
        at once takes each on a channel of its own); 10 is General MIDI's drums, so it comes last. */
    int freeMidiChannel (const Song& song, const juce::String& output, int except)
    {
        std::set<int> used;

        for (auto& c : song.channels)
            if (c.id != except && c.type == InstrumentType::midiOut && c.midiOutDevice == output)
                used.insert (c.midiOutChannel);

        for (int ch : { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 10 })
            if (used.count (ch) == 0)
                return ch;

        return 1;
    }
}

int ChannelRack::addMidiOutChannel (const juce::String& output)
{
    const int midiChannel = freeMidiChannel (song, output, 0);
    song.checkpoint();
    auto& c = song.addChannel (output, InstrumentType::midiOut);
    c.midiOutDevice = output;
    c.midiOutChannel = midiChannel;
    song.selectedChannelId = c.id;
    const int id = c.id;
    song.notify (Song::Change::channels);
    song.notify (Song::Change::selection);
    return id;
}

void ChannelRack::playOnMidiOut (int channelId, const juce::String& output)
{
    auto c = song.findChannel (channelId);

    if (c == nullptr || c->type == InstrumentType::audio)
        return;

    // Called after its old sound, it takes the output's name; one you named keeps yours. Drums go
    // on General MIDI's drum channel, the drummer's part on its keys
    const bool named = c->name != defaultNameFor (*c);
    const bool drums = c->type == InstrumentType::kit || ! c->drummer.turns.empty();
    const int midiChannel = drums ? 10 : freeMidiChannel (song, output, channelId);
    song.checkpoint();
    c->type = InstrumentType::midiOut;
    c->midiOutDevice = output;
    c->midiOutChannel = midiChannel;
    c->sample = c->synthPreset = c->pluginId = c->pluginState = c->kit = c->instrument = {};

    if (! named)
        c->name = output;

    song.notify (Song::Change::channels);

    if (! c->drummer.turns.empty())
        drummer::rerender (song, channelId);
}

juce::PopupMenu ChannelRack::createInstrumentMenu (std::function<void (const juce::String&)> chosen, const juce::String& ticked)
{
    juce::PopupMenu menu;
    std::map<juce::String, juce::PopupMenu> byCategory;
    juce::StringArray order;

    // Nightshift Guitar's sounds, always there
    juce::PopupMenu guitars;

    for (auto& name : guitarsound::presetNames())
        guitars.addItem (name + "  -  " + guitarsound::describe (name), true, guitarsound::pathFor (name) == ticked,
                         [chosen, path = guitarsound::pathFor (name)] { chosen (path); });

    menu.addSubMenu ("Guitar (NightShift)", guitars);
    menu.addSeparator();

    for (auto& e : instruments::list())
    {
        order.addIfNotAlreadyThere (e.category);
        byCategory[e.category].addItem (e.name, true, e.path == ticked, [chosen, path = e.path] { chosen (path); });
    }

    for (auto& category : order)
        menu.addSubMenu (category, byCategory[category]);

    if (order.isEmpty())
        menu.addItem ("No instruments yet", false, false, nullptr);

    return menu;
}

juce::PopupMenu ChannelRack::createSoundMenu (std::function<void (const juce::String&)> chosen, const juce::String& ticked)
{
    juce::PopupMenu menu;
    std::map<juce::String, juce::PopupMenu> byCategory;

    for (auto& p : nova::factory())
        byCategory[p.category].addItem (p.name, true, p.name == ticked, [chosen, name = p.name] { chosen (name); });

    for (auto& category : nova::categories())
        menu.addSubMenu (category, byCategory[category], true, nullptr, category == (nova::findFactory (ticked) != nullptr ? nova::findFactory (ticked)->category : juce::String()));

    return menu;
}

int ChannelRack::addInstrumentChannel (const juce::String& path)
{
    song.checkpoint();
    auto name = instruments::displayName (path).upToFirstOccurrenceOf (",", false, false);
    auto& c = song.addChannel (name.isNotEmpty() ? name : juce::String ("Instrument"), InstrumentType::instrument);
    c.instrument = path;
    song.selectedChannelId = c.id;
    song.notify (Song::Change::channels);
    song.notify (Song::Change::selection);
    return c.id;
}

int ChannelRack::addDrumKitChannel (const juce::String& kitId)
{
    song.checkpoint();
    auto& c = song.addChannel ("Drums", InstrumentType::kit);
    c.kit = kitId;
    song.selectedChannelId = c.id;
    song.notify (Song::Change::channels);
    song.notify (Song::Change::selection);
    return c.id;
}

int ChannelRack::addKitChannel (const juce::String& name)
{
    song.checkpoint();
    auto& c = song.addChannel (name, InstrumentType::sampler);
    c.sample = "kit:" + name;

    if (name == "808")
    {
        c.rootKey = 36;
        c.oneShot = false;
    }

    song.selectedChannelId = c.id;
    const int id = c.id;
    song.notify (Song::Change::channels);
    return id;
}

int ChannelRack::addSynthChannel (const juce::String& preset)
{
    song.checkpoint();
    auto& c = song.addChannel (preset, InstrumentType::synth);
    c.synthPreset = preset;
    song.selectedChannelId = c.id;
    song.notify (Song::Change::channels);
    return c.id;
}

int ChannelRack::addGuitarChannel (const juce::String& preset)
{
    song.checkpoint();
    auto& c = song.addChannel ("Guitar", InstrumentType::instrument);
    c.instrument = guitarsound::pathFor (guitarsound::presetNames().contains (preset) ? preset : juce::String ("Clean"));
    song.selectedChannelId = c.id;
    song.notify (Song::Change::channels);
    return c.id;
}

int ChannelRack::addPluginChannel (const juce::PluginDescription& desc)
{
    song.checkpoint();
    auto& c = song.addChannel (desc.name, InstrumentType::plugin);
    c.pluginId = desc.createIdentifierString();
    song.selectedChannelId = c.id;
    const int id = c.id;
    song.notify (Song::Change::channels);

    if (onOpenPluginEditor != nullptr)
        juce::MessageManager::callAsync ([this, id] { onOpenPluginEditor (id); });

    return id;
}

bool ChannelRack::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString().startsWith (soundDragPrefix);
}

void ChannelRack::itemDragEnter (const SourceDetails& details)
{
    itemDragMove (details);
}

void ChannelRack::itemDragMove (const SourceDetails& details)
{
    // An effect lands on the channel under it; a sound anywhere makes a new channel
    const bool effect = details.description.toString().startsWith (juce::String (soundDragPrefix) + "effect:");
    const int index = effect ? rowIndexAt (details.localPosition) : -2;

    if (index != dropRowIndex)
    {
        dropRowIndex = index;
        repaint();
    }
}

void ChannelRack::itemDragExit (const SourceDetails&)
{
    dropRowIndex = -1;
    repaint();
}

void ChannelRack::itemDropped (const SourceDetails& details)
{
    dropRowIndex = -1;
    addDraggedSound (details.description.toString(), details.localPosition);
    repaint();
}

bool ChannelRack::addDraggedSound (const juce::String& description, juce::Point<int> rackPosition)
{
    const auto sound = description.fromFirstOccurrenceOf (soundDragPrefix, false, false);
    const auto name = sound.fromFirstOccurrenceOf (":", false, false);

    if (sound.startsWith ("effect:"))
    {
        auto desc = audio.findEffectType (name);

        if (desc == nullptr)
            return false;

        const int row = rackPosition.x >= 0 ? rowIndexAt (rackPosition) : -1;
        addEffect (juce::isPositiveAndBelow (row, rows.size()) ? rows[row]->channelId : song.selectedChannelId, *desc);
        return true;
    }

    return addSound (description) != 0;
}

int ChannelRack::addSound (const juce::String& description)
{
    const auto sound = description.fromFirstOccurrenceOf (soundDragPrefix, false, false);
    const auto name = sound.fromFirstOccurrenceOf (":", false, false);

    if (sound.startsWith ("drumkit:") && drumkits::ids().contains (name))
        return addDrumKitChannel (name);

    if (sound.startsWith ("instrument:") && instruments::displayName (name).isNotEmpty())
        return addInstrumentChannel (name);

    if (sound.startsWith ("kit:") && kit::getSoundNames().contains (name))
        return addKitChannel (name);

    if (sound.startsWith ("synth:") && nova::findFactory (name) != nullptr)
        return addSynthChannel (name);

    if (auto desc = sound.startsWith ("plugin:") ? audio.getKnownPlugins().getTypeForIdentifierString (name) : nullptr)
        return addPluginChannel (*desc);

    // A sample from the sidebar's Samples: a sampler channel playing it
    if (sound.startsWith ("sample:"))
        return addSamplerChannel (name);

    return 0;
}

int ChannelRack::addSamplerChannel (const juce::String& path)
{
    const juce::File file (path);

    if (! juce::File::isAbsolutePath (path) || ! file.existsAsFile() || ! isAudioFile (path))
        return 0;

    song.checkpoint();
    auto& c = song.addChannel (file.getFileNameWithoutExtension(), InstrumentType::sampler);
    c.sample = file.getFullPathName();
    song.selectedChannelId = c.id;
    const int id = c.id;
    song.notify (Song::Change::channels);
    return id;
}

juce::String ChannelRack::soundName (const juce::String& description) const
{
    const auto sound = description.fromFirstOccurrenceOf (soundDragPrefix, false, false);
    const auto name = sound.fromFirstOccurrenceOf (":", false, false);

    if (sound.startsWith ("drumkit:"))     return drumkits::displayName (name);
    if (sound.startsWith ("instrument:"))  return instruments::displayName (name).upToFirstOccurrenceOf (",", false, false);
    if (sound.startsWith ("effect:"))      return audio.findEffectType (name) != nullptr ? audio.findEffectType (name)->name : juce::String();
    if (sound.startsWith ("sample:"))      return juce::File (name).getFileNameWithoutExtension();

    if (sound.startsWith ("plugin:"))
        if (auto desc = audio.getKnownPlugins().getTypeForIdentifierString (name))
            return desc->name;

    return name;
}

juce::String ChannelRack::defaultNameFor (const Channel& c) const
{
    switch (c.type)
    {
        case InstrumentType::kit:         return "Drums";
        case InstrumentType::instrument:  return instruments::displayName (c.instrument).upToFirstOccurrenceOf (",", false, false);
        case InstrumentType::synth:       return c.synthPreset;
        case InstrumentType::audio:       return c.name;
        case InstrumentType::midiOut:     return c.midiOutDevice.isNotEmpty() ? c.midiOutDevice : juce::String ("Synth");

        case InstrumentType::plugin:
            if (auto desc = audio.getKnownPlugins().getTypeForIdentifierString (c.pluginId))
                return desc->name;

            return c.name;

        case InstrumentType::sampler:
            return c.sample.startsWith ("kit:") ? c.sample.fromFirstOccurrenceOf ("kit:", false, false)
                                                : juce::File (c.sample).getFileNameWithoutExtension();
    }

    return c.name;
}

bool ChannelRack::replaceInstrument (int channelId, const juce::String& description)
{
    auto c = song.findChannel (channelId);

    if (c == nullptr || c->type == InstrumentType::audio)
        return false;

    const auto sound = description.fromFirstOccurrenceOf (soundDragPrefix, false, false);
    const auto name = sound.fromFirstOccurrenceOf (":", false, false);

    // The new instrument, on a copy of the channel: everything else (its notes, levels,
    // effects, sends, the drummer's conversation) stays
    Channel next = *c;
    next.sample = next.synthPreset = next.pluginId = next.pluginState = next.kit = next.instrument = next.midiOutDevice = {};
    next.rootKey = defaultRootKey;
    next.oneShot = true;

    if (sound.startsWith ("drumkit:") && drumkits::ids().contains (name))
    {
        next.type = InstrumentType::kit;
        next.kit = name;
    }
    else if (sound.startsWith ("instrument:") && instruments::displayName (name).isNotEmpty())
    {
        next.type = InstrumentType::instrument;
        next.instrument = name;
    }
    else if (sound.startsWith ("kit:") && kit::getSoundNames().contains (name))
    {
        next.type = InstrumentType::sampler;
        next.sample = "kit:" + name;

        if (name == "808")
        {
            next.rootKey = 36;
            next.oneShot = false;
        }
    }
    else if (sound.startsWith ("synth:") && nova::findFactory (name) != nullptr)
    {
        next.type = InstrumentType::synth;
        next.synthPreset = name;
    }
    else if (auto desc = sound.startsWith ("plugin:") ? audio.getKnownPlugins().getTypeForIdentifierString (name) : nullptr)
    {
        next.type = InstrumentType::plugin;
        next.pluginId = desc->createIdentifierString();
    }
    else
    {
        return false;
    }

    // A channel still called after its old sound takes the new one's name; one you named keeps yours
    if (c->name == defaultNameFor (*c))
        next.name = defaultNameFor (next);

    song.checkpoint();
    *c = next;
    song.notify (Song::Change::channels);

    if (next.type == InstrumentType::plugin && onOpenPluginEditor != nullptr)
        juce::MessageManager::callAsync ([this, channelId] { onOpenPluginEditor (channelId); });

    return true;
}

//==============================================================================
int ChannelRack::addEffect (int channelId, const juce::PluginDescription& desc)
{
    song.checkpoint();
    // A channel's or bus's chain, else the master's
    auto found = song.chainFor (channelId);
    auto& chain = found != nullptr ? *found : song.masterEffects;

    Effect e;
    e.id = song.createId();
    e.pluginId = desc.createIdentifierString();
    e.name = desc.name;
    chain.push_back (e);
    song.notify (Song::Change::mixer);

    if (onOpenEffectEditor != nullptr)
        juce::MessageManager::callAsync ([this, id = e.id] { onOpenEffectEditor (id); });

    return e.id;
}

juce::PopupMenu ChannelRack::createEffectsMenu (int channelId)
{
    juce::PopupMenu menu;
    auto found = song.chainFor (channelId);
    const auto& chain = found != nullptr ? *found : song.masterEffects;

    for (int i = 0; i < (int) chain.size(); ++i)
        menu.addSubMenu (juce::String (i + 1) + ".  " + chain[(size_t) i].name + (chain[(size_t) i].bypassed ? "  (bypassed)" : ""),
                         createEffectMenu (channelId, chain[(size_t) i].id));

    if (! chain.empty())
        menu.addSeparator();

    menu.addSubMenu ("Add effect", createAddEffectMenu (channelId));
    return menu;
}

juce::PopupMenu ChannelRack::createEffectMenu (int channelId, int effectId)
{
    // The chain is looked up again when an item runs: menus outlive the edits they show
    auto edit = [this, effectId] (std::function<void (std::vector<Effect>&, size_t)> change)
    {
        return [this, effectId, change]
        {
            std::vector<Effect>* chain = nullptr;

            if (auto e = song.findEffect (effectId, &chain))
            {
                song.checkpoint();
                change (*chain, (size_t) std::distance (chain->data(), e));
                song.notify (Song::Change::mixer);
            }
        };
    };

    std::vector<Effect>* chain = nullptr;
    auto effect = song.findEffect (effectId, &chain);
    juce::PopupMenu menu;

    if (effect == nullptr)
        return menu;

    juce::ignoreUnused (channelId);
    const auto index = (size_t) std::distance (chain->data(), effect);
    menu.addSectionHeader (effect->name);
    menu.addItem ("Show editor", [this, effectId] { if (onOpenEffectEditor != nullptr) onOpenEffectEditor (effectId); });
    menu.addItem ("Bypass", true, effect->bypassed, edit ([] (auto& c, size_t i) { c[i].bypassed = ! c[i].bypassed; }));
    menu.addItem ("Move earlier in the chain", index > 0, false, edit ([] (auto& c, size_t i) { if (i > 0) std::swap (c[i], c[i - 1]); }));
    menu.addItem ("Move later in the chain", index + 1 < chain->size(), false,
                  edit ([] (auto& c, size_t i) { if (i + 1 < c.size()) std::swap (c[i], c[i + 1]); }));
    menu.addSeparator();
    menu.addItem ("Remove", edit ([] (auto& c, size_t i) { c.erase (c.begin() + (std::ptrdiff_t) i); }));
    return menu;
}

juce::PopupMenu ChannelRack::createAddEffectMenu (int channelId)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("NightShift");

    for (auto& desc : stockfx::descriptions())
        menu.addItem (desc.name.fromFirstOccurrenceOf ("Nightshift ", false, false), [this, channelId, desc] { addEffect (channelId, desc); });

    // Plugin effects by developer, as the sidebar has them: a submenu each, A to Z
    menu.addSectionHeader ("Plugins");
    const auto developers = AudioEngine::pluginsByDeveloper (audio.getKnownPlugins(), false);

    for (auto& developer : developers)
    {
        juce::PopupMenu theirs;

        for (auto& desc : developer.plugins)
            theirs.addItem (desc.name, [this, channelId, desc] { addEffect (channelId, desc); });

        menu.addSubMenu (developer.name.isNotEmpty() ? developer.name : juce::String ("Unknown developer"), theirs);
    }

    if (developers.empty())
        menu.addItem ("No plugin effects found yet", false, false, nullptr);

    menu.addSeparator();
    menu.addItem ("Find new instruments and effects", [this] { if (onScanPlugins != nullptr) onScanPlugins(); });
    return menu;
}

void ChannelRack::toggleSolo (int channelId)
{
    auto c = song.findChannel (channelId);

    if (c == nullptr)
        return;

    // Everything else off; again, when it's already the only one playing, all back on
    bool alreadySolo = ! c->muted;

    for (auto& other : song.channels)
        if (other.id != channelId && ! other.muted)
            alreadySolo = false;

    song.checkpoint();

    for (auto& other : song.channels)
        other.muted = alreadySolo ? false : other.id != channelId;

    song.notify (Song::Change::mixer);
}

int ChannelRack::addAudioChannel (const juce::String& name)
{
    int number = 1;

    for (auto& c : song.channels)
        if (c.type == InstrumentType::audio)
            ++number;

    // A second Guitar is "Guitar 2"
    auto chosen = name.isNotEmpty() ? name : "Audio " + juce::String (number);

    for (int n = 2; std::any_of (song.channels.begin(), song.channels.end(), [&chosen] (auto& c) { return c.name == chosen; }); ++n)
        chosen = (name.isNotEmpty() ? name : juce::String ("Audio")) + " " + juce::String (n);

    song.checkpoint();
    auto& c = song.addChannel (chosen, InstrumentType::audio);
    c.input = audio.getInputNames()[0];
    c.armed = true;
    song.selectedChannelId = c.id;
    const int id = c.id;

    // An audio track is a track from the start: its arm button and input are on it in Arrange
    Track t;
    t.channelId = id;
    song.insertTrack (t);

    song.notify (Song::Change::channels);
    song.notify (Song::Change::playlist);
    return id;
}

void ChannelRack::showInputMenu (int channelId, juce::Rectangle<int> screenArea)
{
    auto c = song.findChannel (channelId);

    if (c == nullptr)
        return;

    juce::PopupMenu menu;
    const auto inputs = audio.getInputNames();

    for (auto& name : inputs)
    {
        menu.addItem (name, true, name == c->input, [this, channelId, name]
        {
            if (auto ch = song.findChannel (channelId))
            {
                song.checkpoint();
                ch->input = name;
                song.notify (Song::Change::mixer);
            }
        });
    }

    if (inputs.isEmpty())
        menu.addItem ("No inputs open: see Audio settings (F10)", false, false, nullptr);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea));
}

void ChannelRack::showChannelMenu (int channelId)
{
    auto c = song.findChannel (channelId);

    if (c == nullptr)
        return;

    selectChannel (channelId, false);

    juce::PopupMenu menu;
    menu.addSectionHeader (c->name);
    const bool isAudio = c->type == InstrumentType::audio;

    if (isAudio)
    {
        menu.addItem ("Arm for recording", true, c->armed, [this, channelId]
        {
            if (auto ch = song.findChannel (channelId))
            {
                ch->armed = ! ch->armed;
                song.notify (Song::Change::mixer);
            }
        });

        menu.addItem ("Monitor input while armed", true, c->monitor, [this, channelId]
        {
            if (auto ch = song.findChannel (channelId))
            {
                song.checkpoint();
                ch->monitor = ! ch->monitor;
                song.notify (Song::Change::mixer);
            }
        });

        juce::PopupMenu inputMenu;

        for (auto& name : audio.getInputNames())
            inputMenu.addItem (name, true, name == c->input, [this, channelId, name]
            {
                if (auto ch = song.findChannel (channelId))
                {
                    song.checkpoint();
                    ch->input = name;
                    song.notify (Song::Change::mixer);
                }
            });

        menu.addSubMenu ("Input", inputMenu);
        menu.addSeparator();
    }
    else
    {
        menu.addItem ("Piano roll", [this, channelId] { if (onOpenPianoRoll != nullptr) onOpenPianoRoll (channelId); });
        menu.addSeparator();
    }

    if (c->type == InstrumentType::sampler)
    {
        menu.addItem ("Load sample...", [this, channelId] { browseForSample (channelId); });

        juce::PopupMenu kitMenu;

        for (auto& name : kit::getSoundNames())
            kitMenu.addItem (name, [this, channelId, name] { setSample (channelId, kit::fileFor (name)); });

        menu.addSubMenu ("NightShift kit", kitMenu);
        menu.addItem ("Play whole sample (one-shot)", true, c->oneShot, [this, channelId]
        {
            if (auto ch = song.findChannel (channelId))
            {
                song.checkpoint();
                ch->oneShot = ! ch->oneShot;
                song.notify (Song::Change::channels);
            }
        });
    }
    else if (c->type == InstrumentType::synth)
    {
        menu.addItem ("Edit the sound...", [this, channelId] { if (onOpenSynth != nullptr) onOpenSynth (channelId); });
        menu.addSubMenu ("Sound", createSoundMenu ([this, channelId] (const juce::String& preset)
        {
            if (auto ch = song.findChannel (channelId))
            {
                song.checkpoint();

                // A channel named after its sound keeps being named after it
                if (ch->name == ch->synthPreset)
                    ch->name = preset;

                ch->synthPreset = preset;
                ch->pluginState = {};
                song.notify (Song::Change::channels);
            }
        }, c->synthPreset));
    }
    else if (c->type == InstrumentType::plugin)
    {
        menu.addItem ("Show plugin editor", [this, channelId] { if (onOpenPluginEditor != nullptr) onOpenPluginEditor (channelId); });
        menu.addItem ("AI drummer (General MIDI)...", [this, channelId] { if (onOpenDrummer != nullptr) onOpenDrummer (channelId); });
    }
    else if (c->type == InstrumentType::instrument)
    {
        menu.addSubMenu ("Instrument", createInstrumentMenu ([this, channelId] (const juce::String& path)
        {
            if (auto ch = song.findChannel (channelId); ch != nullptr && ch->instrument != path)
            {
                song.checkpoint();
                ch->instrument = path;
                song.notify (Song::Change::channels);
            }
        }, c->instrument));
    }
    else if (c->type == InstrumentType::kit)
    {
        menu.addItem ("AI drummer...", [this, channelId] { if (onOpenDrummer != nullptr) onOpenDrummer (channelId); });

        juce::PopupMenu kitMenu;

        for (auto& id : drumkits::ids())
            kitMenu.addItem (drumkits::displayName (id), true, c->kit == id, [this, channelId, id]
            {
                if (auto ch = song.findChannel (channelId); ch != nullptr && ch->kit != id)
                {
                    song.checkpoint();
                    ch->kit = id;
                    song.notify (Song::Change::channels);
                    drummer::rerender (song, channelId);     // its part, on this kit's keys
                }
            });

        menu.addSubMenu ("Kit", kitMenu);
    }
    else if (c->type == InstrumentType::midiOut)
    {
        // A synth plugged in: which, on which MIDI channel, the sound it's asked for
        menu.addSubMenu ("Plays on", createMidiOutMenu ([this, channelId] (const juce::String& output)
        {
            if (auto ch = song.findChannel (channelId); ch != nullptr && ch->midiOutDevice != output)
            {
                song.checkpoint();

                if (ch->name == ch->midiOutDevice)
                    ch->name = output;      // named after it, it follows

                ch->midiOutDevice = output;
                song.notify (Song::Change::channels);
            }
        }, c->midiOutDevice));

        juce::PopupMenu channelMenu;

        for (int ch = 1; ch <= 16; ++ch)
            channelMenu.addItem ("Channel " + juce::String (ch) + (ch == 10 ? " (drums)" : ""), true, c->midiOutChannel == ch, [this, channelId, ch]
            {
                if (auto chosen = song.findChannel (channelId); chosen != nullptr && chosen->midiOutChannel != ch)
                {
                    song.checkpoint();
                    chosen->midiOutChannel = ch;
                    song.notify (Song::Change::channels);
                }
            });

        menu.addSubMenu ("MIDI channel " + juce::String (c->midiOutChannel), channelMenu);

        // General MIDI's sounds, by family (a synth of its own has its own at those numbers)
        juce::PopupMenu programMenu;
        auto setProgram = [this, channelId] (int program)
        {
            if (auto chosen = song.findChannel (channelId); chosen != nullptr && chosen->midiProgram != program)
            {
                song.checkpoint();
                chosen->midiProgram = program;
                song.notify (Song::Change::channels);
                audio.sendMidiProgram (channelId);
            }
        };

        programMenu.addItem ("None: the sound it's on", true, c->midiProgram < 0, [setProgram] { setProgram (-1); });
        const char* families[] = { "Piano", "Chromatic percussion", "Organ", "Guitar", "Bass", "Strings", "Ensemble", "Brass",
                                   "Reed", "Pipe", "Synth lead", "Synth pad", "Synth effects", "Ethnic", "Percussive", "Sound effects" };

        for (int f = 0; f < 16; ++f)
        {
            juce::PopupMenu family;

            for (int p = f * 8; p < f * 8 + 8; ++p)
                family.addItem (juce::String (p + 1) + "  " + midifile::programName (p), true, c->midiProgram == p, [setProgram, p] { setProgram (p); });

            programMenu.addSubMenu (families[f], family, true, nullptr, c->midiProgram >= f * 8 && c->midiProgram < f * 8 + 8);
        }

        menu.addSubMenu ("Sound on the synth", programMenu);
        menu.addItem ("Play a test note", [this, channelId] { audio.sendTestNote (channelId); });
        menu.addItem ("Timing and tempo...", [this] { if (onShowAudioSettings != nullptr) onShowAudioSettings(); });

        if (c->midiOutChannel == 10)
            menu.addItem ("AI drummer (General MIDI)...", [this, channelId] { if (onOpenDrummer != nullptr) onOpenDrummer (channelId); });

        menu.addSubMenu ("Play a NightShift sound instead", createSoundMenu ([this, channelId] (const juce::String& preset)
        {
            replaceInstrument (channelId, juce::String (soundDragPrefix) + "synth:" + preset);
        }));
    }

    // Any instrument can play a synth plugged in instead
    if (! isAudio && c->type != InstrumentType::midiOut)
        menu.addSubMenu ("Play it on a synth plugged in", createMidiOutMenu ([this, channelId] (const juce::String& output)
        {
            playOnMidiOut (channelId, output);
        }));

    menu.addSubMenu ("Effects" + (c->effects.empty() ? juce::String() : "  (" + juce::String ((int) c->effects.size()) + ")"),
                     createEffectsMenu (channelId));
    menu.addItem ("Rename...", [this, channelId] { renameChannel (channelId); });

    juce::PopupMenu colourMenu;

    for (int i = 0; i < 8; ++i)
    {
        const auto colour = Song::channelColour (i);
        juce::PopupMenu::Item item ("Colour " + juce::String (i + 1));
        item.colour = colour;
        item.action = [this, channelId, colour]
        {
            if (auto ch = song.findChannel (channelId))
            {
                song.checkpoint();
                ch->colour = colour;
                song.notify (Song::Change::mixer);
            }
        };
        colourMenu.addItem (item);
    }

    menu.addSubMenu ("Colour", colourMenu);
    menu.addSeparator();

    if (! isAudio)
    {
        for (int every : { 2, 4, 8 })
        {
            menu.addItem ("Fill each " + juce::String (every) + " steps", [this, channelId, every]
            {
                if (auto ch = song.findChannel (channelId))
                {
                    song.checkpoint();
                    auto& list = song.currentPattern().notesFor (channelId);
                    list.clear();

                    for (int s = 0; s < song.barTicksOf (song.currentPattern()) / ticksPerStep; s += every)
                        steps::set (list, s, true, ch->rootKey);

                    song.notify (Song::Change::notes);
                }
            });
        }

        menu.addItem ("Clear notes in this pattern", [this, channelId]
        {
            song.checkpoint();
            song.currentPattern().notesFor (channelId).clear();
            song.notify (Song::Change::notes);
        });

        menu.addSeparator();
    }

    menu.addItem ("Clone channel", [this, channelId]
    {
        if (auto ch = song.findChannel (channelId))
        {
            song.checkpoint();
            auto copy = *ch;
            copy.id = song.createId();
            copy.name = ch->name + " 2";
            auto index = song.indexOfChannel (channelId);
            song.channels.insert (song.channels.begin() + index + 1, copy);
            song.selectedChannelId = copy.id;
            song.notify (Song::Change::channels);
        }
    });

    menu.addItem ("Delete channel", [this, channelId]
    {
        song.checkpoint();
        song.removeChannel (channelId);
        song.notify (Song::Change::channels);
    });

    menu.showMenuAsync ({});
}

void ChannelRack::renameChannel (int channelId)
{
    auto c = song.findChannel (channelId);

    if (c == nullptr)
        return;

    auto* window = new juce::AlertWindow ("Rename channel", {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", c->name);
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, channelId] (int result)
    {
        auto newName = window->getTextEditorContents ("name").trim();

        if (result == 1 && newName.isNotEmpty())
        {
            if (auto ch = song.findChannel (channelId))
            {
                song.checkpoint();
                ch->name = newName;
                song.notify (Song::Change::channels);
            }
        }
    }), true);
}

int ChannelRack::addBus (const juce::String& name, const juce::String& effectType)
{
    song.checkpoint();
    auto& b = song.addBus (name);
    const int id = b.id;

    for (auto& d : stockfx::descriptions())
    {
        if (d.fileOrIdentifier != effectType)
            continue;

        Effect e;
        e.id = song.createId();
        e.pluginId = d.createIdentifierString();
        e.name = d.name;
        b.effects.push_back (e);
    }

    song.notify (Song::Change::channels);

    // A send bus's effect all wet: the channels sending to it keep their own dry sound
    if (effectType.isNotEmpty())
    {
        audio.syncNow();

        if (auto bus = song.findBus (id); bus != nullptr && ! bus->effects.empty())
            if (auto fx = dynamic_cast<StockEffect*> (audio.getEffect (bus->effects.front().id)))
                if (auto mix = fx->findParam ("mix"))
                    mix->set (1.0f);
    }

    return id;
}

void ChannelRack::deleteBus (int busId)
{
    if (song.findBus (busId) == nullptr)
        return;

    song.checkpoint();
    song.removeBus (busId);
    song.notify (Song::Change::channels);
}

void ChannelRack::toggleBusSolo (int busId)
{
    auto b = song.findBus (busId);

    if (b == nullptr)
        return;

    // Every channel not playing into it off; again, when that's how things are, all back on
    bool already = ! b->muted;

    for (auto& c : song.channels)
        already = already && (song.feedsBus (c, busId) ? ! c.muted : c.muted);

    song.checkpoint();

    for (auto& c : song.channels)
        c.muted = already ? false : ! song.feedsBus (c, busId);

    b->muted = false;
    song.notify (Song::Change::mixer);
}

void ChannelRack::renameBus (int busId)
{
    auto b = song.findBus (busId);

    if (b == nullptr)
        return;

    auto* window = new juce::AlertWindow ("Rename bus", {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", b->name);
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, busId] (int result)
    {
        auto newName = window->getTextEditorContents ("name").trim();

        if (result == 1 && newName.isNotEmpty())
        {
            if (auto bus = song.findBus (busId))
            {
                song.checkpoint();
                bus->name = newName;
                song.notify (Song::Change::channels);
            }
        }
    }), true);
}

void ChannelRack::browseForSample (int channelId, std::function<void (int)> added)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sample",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                            | (channelId == 0 ? juce::FileBrowserComponent::canSelectMultipleItems : 0),
                          [this, channelId, added, dialog = ComputerKeyboard::openSystemDialog()] (const juce::FileChooser& fc)
    {
        if (channelId == 0)
        {
            juce::StringArray paths;

            for (auto& f : fc.getResults())
                paths.add (f.getFullPathName());

            addSamplerChannels (paths, added);
        }
        else if (fc.getResult().existsAsFile())
        {
            setSample (channelId, fc.getResult());
        }
    });
}

void ChannelRack::setSample (int channelId, const juce::File& file)
{
    auto c = song.findChannel (channelId);

    if (c == nullptr)
        return;

    song.checkpoint();
    c->type = InstrumentType::sampler;

    // Kit files are stored by name so projects survive moving machines
    const bool fromKit = file.getParentDirectory() == kit::getFolder();
    c->sample = fromKit ? "kit:" + file.getFileNameWithoutExtension() : file.getFullPathName();
    c->name = file.getFileNameWithoutExtension();
    song.notify (Song::Change::channels);
    audio.previewNote (channelId, c->rootKey, 100, 450);
}

void ChannelRack::addSamplerChannels (const juce::StringArray& files, std::function<void (int)> added)
{
    if (files.isEmpty())
        return;

    song.checkpoint();
    std::vector<int> ids;

    for (auto& path : files)
    {
        auto& c = song.addChannel (juce::File (path).getFileNameWithoutExtension(), InstrumentType::sampler);
        c.sample = path;
        song.selectedChannelId = c.id;
        ids.push_back (c.id);
    }

    song.notify (Song::Change::channels);

    if (added != nullptr)
        for (auto id : ids)
            added (id);
}

//==============================================================================
bool ChannelRack::isAudioFile (const juce::String& path)
{
    return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

bool ChannelRack::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isAudioFile (f))
            return true;

    return false;
}

int ChannelRack::rowIndexAt (juce::Point<int> p) const
{
    if (! viewport.getBounds().contains (p))
        return -1;

    const auto inHolder = rowHolder.getLocalPoint (this, p);

    for (int i = 0; i < rows.size(); ++i)
        if (rows[i]->getBounds().contains (inHolder))
            return i;

    return -2;   // over the rack, but not on a row
}

void ChannelRack::fileDragMove (const juce::StringArray& files, int x, int y)
{
    const int index = files.size() == 1 ? rowIndexAt ({ x, y }) : -2;

    if (index != dropRowIndex)
    {
        dropRowIndex = index;
        repaint();
    }
}

void ChannelRack::fileDragExit (const juce::StringArray&)
{
    dropRowIndex = -1;
    repaint();
}

void ChannelRack::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropRowIndex = -1;
    repaint();

    juce::StringArray audioFiles;

    for (auto& f : files)
        if (isAudioFile (f))
            audioFiles.add (f);

    const int index = rowIndexAt ({ x, y });

    if (audioFiles.size() == 1 && juce::isPositiveAndBelow (index, rows.size()))
        setSample (rows[index]->channelId, juce::File (audioFiles[0]));
    else
        addSamplerChannels (audioFiles);
}

} // namespace nightshift
