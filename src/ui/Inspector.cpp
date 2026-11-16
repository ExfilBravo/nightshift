// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Inspector.h"
#include "Editing.h"
#include "TextAudit.h"
#include "audio/Kits.h"
#include "audio/Instruments.h"
#include "audio/GuitarSound.h"

namespace nightshift
{

using namespace theme;

namespace
{
    constexpr int slotHeight = 28, slotGap = 4, lightWidth = 26, sendHeight = 26;

    juce::String shortName (const juce::String& pluginName)
    {
        return pluginName.startsWith ("Nightshift ") ? pluginName.fromFirstOccurrenceOf ("Nightshift ", false, false) : pluginName;
    }

    juce::String dbText (float db)
    {
        return db <= -99.0f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB";
    }

    juce::String sendDbText (float level)
    {
        const float db = channelVolumeToDb (level);
        return db <= -99.0f ? juce::String ("-inf") : (db > 0.04f ? "+" : "") + juce::String (db, 1);
    }
}

ChannelStrip::ChannelStrip (Song& s, AudioEngine& a, ChannelRack& r, Kind k)
    : song (s), audio (a), rack (r), kind (k), fader (k == Kind::master ? 0.8 : 0.78)
{
    fader.formatValue = [this] (double v)
    {
        return "Volume " + dbText (kind == Kind::master ? masterVolumeToDb ((float) v) : channelVolumeToDb ((float) v)) + " (double-click for 0 dB)";
    };
    fader.onDragStart = [this] { song.checkpoint(); };
    fader.onValueChange = [this]
    {
        if (auto v = volume())
            *v = (float) fader.getValue();

        song.notify (Song::Change::mixer);
        repaint (readout);
    };
    addAndMakeVisible (fader);

    meter.setVertical (true);
    meter.setTooltip ("Level after the effects and the fader (click to clear the clip light)");
    addAndMakeVisible (meter);

    pan.setFillColour (accentCool);
    pan.formatValue = [] (double v)
    {
        return std::abs (v) < 0.01 ? juce::String ("Pan: centre (double-click)")
                                   : "Pan " + juce::String (juce::roundToInt (std::abs (v) * 100.0)) + (v < 0 ? "% left" : "% right");
    };
    pan.onDragStart = [this] { song.checkpoint(); };
    pan.onValueChange = [this]
    {
        float* p = channel() != nullptr ? &channel()->pan : bus() != nullptr ? &bus()->pan : nullptr;

        if (p != nullptr)
        {
            *p = (float) pan.getValue();
            song.notify (Song::Change::mixer);
            repaint (panLabel);
        }
    };
    addChildComponent (pan);

    // Right-click: automate it in Arrange, or link it to a MIDI controller's knob (a channel's
    // volume and pan, the master's volume)
    fader.onRightClick = [this]
    {
        if ((kind == Kind::master || channel() != nullptr) && automation::showMenu != nullptr)
            automation::showMenu (getChannel(), "volume", kind == Kind::master ? juce::String ("the master volume") : channel()->name + "'s volume");
    };
    pan.onRightClick = [this]
    {
        if (auto c = channel(); c != nullptr && automation::showMenu != nullptr)
            automation::showMenu (c->id, "pan", c->name + "'s pan");
    };

    for (auto* b : { &pianoButton, &stepsButton })
    {
        b->setFramed (true);
        b->setIconSize (13.0f);
        addChildComponent (*b);
    }

    pianoButton.setLabel ("Piano roll");
    stepsButton.setLabel ("Steps");
    pianoButton.setTooltip ("Edit this channel's notes in the piano roll (F7)");
    stepsButton.setTooltip ("This channel's steps in the current pattern (F6)");
    pianoButton.onClick = [this] { if (onOpenPianoRoll != nullptr) onOpenPianoRoll (targetId); };
    stepsButton.onClick = [this] { if (onShowSteps != nullptr) onShowSteps (targetId); };

    refresh();
}

Channel* ChannelStrip::channel() const
{
    return kind == Kind::channel ? song.findChannel (targetId) : nullptr;
}

Bus* ChannelStrip::bus() const
{
    return kind == Kind::bus ? song.findBus (targetId) : nullptr;
}

bool ChannelStrip::exists() const
{
    return kind == Kind::master || channel() != nullptr || bus() != nullptr;
}

std::vector<Effect>* ChannelStrip::chain() const
{
    if (kind == Kind::master)
        return &song.masterEffects;

    return exists() ? song.chainFor (targetId) : nullptr;
}

float* ChannelStrip::volume() const
{
    if (kind == Kind::master)
        return &song.masterVolume;

    if (auto c = channel())
        return &c->volume;

    if (auto b = bus())
        return &b->volume;

    return nullptr;
}

juce::String ChannelStrip::name() const
{
    if (kind == Kind::master)
        return "Master";

    if (auto c = channel())
        return c->name;

    if (auto b = bus())
        return b->name;

    return {};
}

juce::Colour ChannelStrip::colour() const
{
    if (auto c = channel())
        return c->colour;

    if (auto b = bus())
        return b->colour;

    return accent;
}

void ChannelStrip::setTarget (int id)
{
    if (kind == Kind::master || id == targetId)
        return;

    targetId = id;
    refresh();
}

void ChannelStrip::setCompact (bool shouldBeCompact)
{
    compact = shouldBeCompact;
    refresh();
}

juce::String ChannelStrip::instrumentName (const Channel& c) const
{
    switch (c.type)
    {
        case InstrumentType::synth:   return c.synthPreset.isNotEmpty() ? c.synthPreset : juce::String ("NightShift Synth");
        case InstrumentType::plugin:  return c.name;
        case InstrumentType::audio:   return c.input;
        case InstrumentType::kit:     return drumkits::displayName (c.kit);
        case InstrumentType::instrument: return instruments::displayName (c.instrument);
        case InstrumentType::sampler: break;
    }

    if (c.sample.startsWith ("kit:"))
        return c.sample.fromFirstOccurrenceOf ("kit:", false, false) + " (kit)";

    return c.sample.isEmpty() ? juce::String ("No sample") : juce::File (c.sample).getFileNameWithoutExtension();
}

juce::String ChannelStrip::typeLine() const
{
    if (kind == Kind::master)
        return "The whole mix";

    if (auto b = bus())
    {
        int feeding = 0;

        for (auto& c : song.channels)
            feeding += song.feedsBus (c, b->id) ? 1 : 0;

        return "Bus" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (feeding) + (feeding == 1 ? " channel in" : " channels in");
    }

    auto c = channel();

    if (c == nullptr)
        return {};

    switch (c->type)
    {
        case InstrumentType::synth:   return "Synth";
        case InstrumentType::plugin:  return "Plugin instrument";
        case InstrumentType::audio:   return "Audio track";
        case InstrumentType::kit:     return c->drummer.turns.empty() ? "Drum kit" : "Drum kit, AI drummer";
        case InstrumentType::instrument: return isGuitar (*c) ? "Nightshift Guitar" : "Sampled instrument";
        case InstrumentType::sampler: break;
    }

    return c->sample.startsWith ("kit:") ? "Kit sound" : "Sampler";
}

bool ChannelStrip::isSoloed() const
{
    // Soloed: everything else is muted (for a bus: every channel not playing into it)
    if (auto c = channel())
    {
        bool soloed = ! c->muted;

        for (auto& other : song.channels)
            soloed = soloed && (other.id == c->id || other.muted);

        return soloed;
    }

    if (auto b = bus())
    {
        bool anyIn = false, soloed = ! b->muted;

        for (auto& c : song.channels)
        {
            const bool feeds = song.feedsBus (c, b->id);
            anyIn = anyIn || (feeds && ! c.muted);
            soloed = soloed && (feeds || c.muted);
        }

        return soloed && anyIn;
    }

    return false;
}

//==============================================================================
void ChannelStrip::refresh()
{
    auto c = channel();
    auto b = bus();
    const bool shown = exists();

    fader.setVisible (shown);
    meter.setVisible (shown);
    pan.setVisible (c != nullptr || b != nullptr);

    const bool editButtons = ! compact && c != nullptr && c->type != InstrumentType::audio;
    pianoButton.setVisible (editButtons);
    stepsButton.setVisible (editButtons);

    if (auto v = volume())
        fader.setValue (*v);

    if (c != nullptr)
        pan.setValue (c->pan);
    else if (b != nullptr)
        pan.setValue (b->pan);

    resized();
    repaint();
}

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced (compact ? 8 : 10, 0);
    header = r.removeFromTop (compact ? 50 : 60);
    auto c = channel();

    // From the bottom: mute and solo (over the edit buttons), the readout, the fader, the
    // pan, the output. The master keeps the same room, so faders line up across a row
    auto bottom = r.removeFromBottom (compact ? 38 : 100);

    if (kind != Kind::master)
    {
        auto ms = bottom.removeFromTop (30).withSizeKeepingCentre (bottom.getWidth(), 24);
        muteArea = ms.removeFromLeft (ms.getWidth() / 2 - 2);
        ms.removeFromLeft (4);
        soloArea = ms;

        bottom.removeFromTop (6);
        pianoButton.setBounds (bottom.removeFromTop (28));
        bottom.removeFromTop (4);
        stepsButton.setBounds (bottom.removeFromTop (28));
    }
    else
    {
        muteArea = soloArea = {};
    }

    readout = r.removeFromBottom (22);

    // The fader with its meter beside it, centred as a pair
    faderArea = r.removeFromBottom (juce::jlimit (compact ? 84 : 110, 260, (int) (r.getHeight() * (compact ? 0.36 : 0.34))));
    const int pairX = faderArea.getCentreX() - (44 + 8 + 14) / 2;
    fader.setBounds (pairX, faderArea.getY(), 44, faderArea.getHeight());
    meter.setBounds (pairX + 52, faderArea.getY() + Fader::capHeight / 2, 14, faderArea.getHeight() - Fader::capHeight);
    r.removeFromBottom (8);

    if (kind != Kind::master)
    {
        pan.setBounds (r.removeFromBottom (12));
        panLabel = r.removeFromBottom (18);
        r.removeFromBottom (6);

        outputArea = r.removeFromBottom (28);
        outputLabel = r.removeFromBottom (18);
        r.removeFromBottom (8);
    }
    else
    {
        outputArea = outputLabel = {};
    }

    // The instrument, or an audio track's input, arm and monitor
    instrumentLabel = instrumentArea = inputArea = armArea = monArea = {};

    if (c != nullptr)
    {
        instrumentLabel = r.removeFromTop (20);

        if (c->type == InstrumentType::audio)
        {
            inputArea = r.removeFromTop (28);
            r.removeFromTop (4);
            auto row = r.removeFromTop (24);
            armArea = row.removeFromLeft (row.getWidth() / 2 - 2);
            row.removeFromLeft (4);
            monArea = row;
        }
        else
        {
            instrumentArea = r.removeFromTop (28);
        }

        r.removeFromTop (10);
    }

    // What's left: the effects, then (on a channel) its sends
    sendsLabel = sendsArea = {};

    if (c != nullptr)
    {
        const int sends = (int) c->sends.size();
        const int wanted = 20 + (sends + 1) * (sendHeight + slotGap);
        const int effectsWanted = 20 + ((int) c->effects.size() + 1) * (slotHeight + slotGap);
        const int room = std::max (20 + 2 * (sendHeight + slotGap), r.getHeight() - effectsWanted);
        sendsArea = r.removeFromBottom (std::min (wanted, room));
        sendsLabel = sendsArea.removeFromTop (20);
        r.removeFromBottom (6);
    }

    effectsLabel = r.removeFromTop (20);
    effectsArea = r;
    layoutHits();
}

void ChannelStrip::layoutHits()
{
    hits.clear();

    if (! exists())
        return;

    hits.push_back ({ Area::header, header });

    if (auto c = channel())
    {
        if (c->type == InstrumentType::audio)
        {
            hits.push_back ({ Area::input, inputArea });
            hits.push_back ({ Area::arm, armArea });
            hits.push_back ({ Area::monitor, monArea });
        }
        else
        {
            hits.push_back ({ Area::instrument, instrumentArea });
        }

        // Sends: as many as fit, then one row listing them all, then +
        auto area = sendsArea;
        const int fit = (area.getHeight() + slotGap) / (sendHeight + slotGap);
        const int count = (int) c->sends.size();
        const bool overflow = count + 1 > fit;
        const int show = overflow ? std::max (0, fit - 2) : count;

        for (int i = 0; i < show; ++i)
        {
            hits.push_back ({ Area::send, area.removeFromTop (sendHeight), c->sends[(size_t) i].busId });
            area.removeFromTop (slotGap);
        }

        if (overflow && area.getHeight() >= sendHeight)
        {
            hits.push_back ({ Area::moreSends, area.removeFromTop (sendHeight) });
            area.removeFromTop (slotGap);
        }

        if (area.getHeight() >= sendHeight)
            hits.push_back ({ Area::addSend, area.removeFromTop (sendHeight) });
    }

    if (kind != Kind::master)
    {
        hits.push_back ({ Area::output, outputArea });
        hits.push_back ({ Area::mute, muteArea });
        hits.push_back ({ Area::solo, soloArea });
    }

    if (auto effects = chain())
    {
        // As many slots as fit, then (if there are more) one that lists them all, then +
        auto area = effectsArea;
        const int fit = (area.getHeight() + slotGap) / (slotHeight + slotGap);
        const int count = (int) effects->size();
        const bool overflow = count + 1 > fit;
        const int show = overflow ? std::max (0, fit - 2) : count;

        for (int i = 0; i < show; ++i)
        {
            auto slot = area.removeFromTop (slotHeight);
            area.removeFromTop (slotGap);
            const int id = (*effects)[(size_t) i].id;
            hits.push_back ({ Area::light, slot.withWidth (lightWidth), id });
            hits.push_back ({ Area::effect, slot, id });
        }

        if (overflow && area.getHeight() >= slotHeight)
        {
            hits.push_back ({ Area::more, area.removeFromTop (slotHeight) });
            area.removeFromTop (slotGap);
        }

        if (area.getHeight() >= slotHeight)
            hits.push_back ({ Area::add, area.removeFromTop (slotHeight) });
    }
}

//==============================================================================
void ChannelStrip::paint (juce::Graphics& g)
{
    auto c = channel();
    auto b = bus();

    if (! exists())
    {
        g.setColour (textFaint);
        g.setFont (font (17.0f, true));
        g.drawFittedText ("No channel selected", getLocalBounds().reduced (12).withHeight (60), juce::Justification::centred, 1);
        g.setFont (font (16.0f));
        g.drawFittedText ("Click a track in Arrange or a channel in Steps", getLocalBounds().reduced (16).withTrimmedTop (50).withHeight (60),
                          juce::Justification::centredTop, 3);
        return;
    }

    // The selected channel's strip in the mixer
    if (compact && c != nullptr && song.selectedChannelId == c->id)
    {
        g.setColour (accentCool.withAlpha (0.08f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f, 4.0f), 10.0f);
        g.setColour (accentCool.withAlpha (0.5f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.5f, 4.5f), 10.0f, 1.0f);
    }

    // Whose strip this is
    g.setColour (colour());
    g.fillRoundedRectangle ((float) header.getX(), (float) header.getY() + (compact ? 10.0f : 14.0f), 4.0f, compact ? 32.0f : 34.0f, 2.0f);
    // A long name steps down a size or three (to 16 px, no smaller) before it's cut short
    const auto nameArea = header.withTrimmedLeft (12).withY (header.getY() + (compact ? 8 : 12)).withHeight (22);
    float nameSize = compact ? 17.0f : 19.0f;

    while (nameSize > 16.0f && juce::GlyphArrangement::getStringWidth (font (nameSize, true), name()) > (float) nameArea.getWidth())
        nameSize -= 1.0f;

    g.setColour (text);
    g.setFont (font (nameSize, true));
    {
        textaudit::TheirWords theirs;       // the channel's name, as long as the user made it
        g.drawText (name(), nameArea, juce::Justification::centredLeft, true);
    }
    g.setColour (textFaint);
    g.setFont (font (15.0f));
    g.drawText (typeLine(), header.withTrimmedLeft (12).withY (header.getY() + (compact ? 29 : 34)).withHeight (17), juce::Justification::centredLeft, true);

    g.setColour (hairline);
    g.fillRect (0, header.getBottom() - 1, getWidth(), 1);

    // Section labels
    g.setColour (textFaint);
    g.setFont (labelFont());

    if (c != nullptr)
        g.drawText (c->type == InstrumentType::audio ? "INPUT" : "INSTRUMENT", instrumentLabel, juce::Justification::centredLeft, true);

    if (! sendsLabel.isEmpty())
        g.drawText ("SENDS", sendsLabel, juce::Justification::centredLeft, true);

    if (kind != Kind::master)
    {
        g.drawText ("OUTPUT", outputLabel, juce::Justification::centredLeft, true);
        g.drawText ("PAN", panLabel, juce::Justification::centredLeft, true);

        const float p = c != nullptr ? c->pan : b != nullptr ? b->pan : 0.0f;
        g.setFont (monoFont (15.0f, true));
        g.drawText (std::abs (p) < 0.01f ? juce::String ("C") : juce::String (juce::roundToInt (std::abs (p) * 100.0f)) + (p < 0 ? " L" : " R"),
                    panLabel, juce::Justification::centredRight, true);
        g.setFont (labelFont());
    }

    g.drawText ("EFFECTS", effectsLabel, juce::Justification::centredLeft, true);

    if (dropping)
    {
        g.setColour (effectColour.withAlpha (0.1f));
        g.fillRoundedRectangle (effectsArea.toFloat().reduced (-4.0f, 0.0f), 8.0f);
        g.setColour (effectColour.withAlpha (0.6f));
        g.drawRoundedRectangle (effectsArea.toFloat().reduced (-4.0f, 0.0f), 8.0f, 1.5f);
    }

    auto effects = chain();
    auto effectFor = [effects] (int id) -> const Effect*
    {
        if (effects != nullptr)
            for (auto& e : *effects)
                if (e.id == id)
                    return &e;

        return nullptr;
    };

    const bool soloed = isSoloed();
    const bool muted = c != nullptr ? c->muted : b != nullptr && b->muted;

    for (int i = 0; i < (int) hits.size(); ++i)
    {
        const auto& hit = hits[(size_t) i];
        auto bounds = hit.bounds.toFloat();
        const bool hover = i == hovered || (hit.area == Area::effect && hovered >= 0 && hits[(size_t) hovered].area == Area::light
                                                                         && hits[(size_t) hovered].id == hit.id);

        auto box = [&] (juce::Colour fill, juce::Colour edge)
        {
            g.setColour (fill);
            g.fillRoundedRectangle (bounds, 7.0f);
            g.setColour (edge);
            g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.0f);
        };

        auto chevron = [&]
        {
            auto path = icons::chevronDown();
            path.applyTransform (juce::AffineTransform::scale (1.3f).translated (bounds.getRight() - 20.0f, bounds.getCentreY() - 6.5f));
            g.setColour (textFaint);
            g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };

        switch (hit.area)
        {
            case Area::instrument:
            case Area::input:
            {
                box (hover ? titleBar : panelLight, hairlineStrong);
                g.setColour (text);
                g.setFont (font (16.0f, true));
                {
                    textaudit::TheirWords theirs;   // a sound's name, as long as its maker made it
                    g.drawText (instrumentName (*c), hit.bounds.reduced (10, 0).withTrimmedRight (14), juce::Justification::centredLeft, true);
                }
                chevron();
                break;
            }

            case Area::output:
            {
                box (hover ? titleBar : panelLight, hairlineStrong);
                auto target = c != nullptr ? song.findBus (c->output) : nullptr;
                g.setColour (target != nullptr ? target->colour : accent);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ bounds.getX() + 13.0f, bounds.getCentreY() }));
                g.setColour (text);
                g.setFont (font (16.0f, true));
                {
                    textaudit::TheirWords theirs;   // a bus's name
                    g.drawText (target != nullptr ? target->name : juce::String ("Master"), hit.bounds.withTrimmedLeft (22).withTrimmedRight (22),
                                juce::Justification::centredLeft, true);
                }

                if (c != nullptr)
                    chevron();
                break;
            }

            case Area::arm:
            {
                box (c->armed ? danger.withAlpha (0.16f) : (hover ? titleBar : panelLight), c->armed ? danger.withAlpha (0.45f) : hairline);
                g.setColour (c->armed ? danger : danger.withAlpha (0.5f));
                g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ bounds.getX() + 16.0f, bounds.getCentreY() }));
                g.setColour (c->armed ? text : textDim);
                g.setFont (font (15.0f, true));
                g.drawText ("Arm", hit.bounds.withTrimmedLeft (26), juce::Justification::centredLeft, true);
                break;
            }

            case Area::monitor:
                box (c->monitor ? accentCool.withAlpha (0.14f) : (hover ? titleBar : panelLight), c->monitor ? accentCool.withAlpha (0.4f) : hairline);
                g.setColour (c->monitor ? accentCool : textFaint);
                g.setFont (labelFont());
                g.drawText ("MON", hit.bounds, juce::Justification::centred, true);
                break;

            case Area::effect:
            {
                auto e = effectFor (hit.id);

                if (e == nullptr)
                    break;

                box (e->bypassed ? (hover ? titleBar : panelLight) : effectColour.withAlpha (hover ? 0.2f : 0.12f),
                     e->bypassed ? hairlineStrong : effectColour.withAlpha (hover ? 0.6f : 0.35f));
                g.setColour (e->bypassed ? textFaint : text);
                g.setFont (font (16.0f, true));
                textaudit::TheirWords theirs;   // an effect's name (a plugin's, as its maker wrote it)
                g.drawText (shortName (e->name), hit.bounds.withTrimmedLeft (lightWidth).withTrimmedRight (6), juce::Justification::centredLeft, true);
                break;
            }

            case Area::send:
            {
                auto send = c != nullptr ? Song::findSend (*c, hit.id) : nullptr;
                auto target = song.findBus (hit.id);

                if (send == nullptr || target == nullptr)
                    break;

                // The level as a bar filling the row, in the bus's colour
                box (hover ? titleBar : panelLight, hairlineStrong);
                const float fill = juce::jlimit (0.0f, 1.0f, send->level);
                g.setColour (target->colour.withAlpha (0.28f));
                g.fillRoundedRectangle (bounds.reduced (1.0f).withWidth ((bounds.getWidth() - 2.0f) * fill), 6.0f);

                auto row = hit.bounds.reduced (8, 0);
                g.setFont (monoFont (15.0f, true));
                g.setColour (textDim);
                g.drawText (sendDbText (send->level), row.removeFromRight (40), juce::Justification::centredRight, true);

                if (send->preFader)
                {
                    g.setColour (accentCool);
                    g.setFont (labelFont());
                    g.drawText ("PRE", row.removeFromRight (30), juce::Justification::centredRight, true);
                }

                g.setColour (text);
                g.setFont (font (15.0f, true));
                textaudit::TheirWords theirs;   // a bus's name
                g.drawText (target->name, row, juce::Justification::centredLeft, true);
                break;
            }

            case Area::light:
                break;      // drawn below, over its slot

            case Area::more:
            case Area::moreSends:
            {
                box (hover ? titleBar : panelLight, hairlineStrong);
                g.setColour (textDim);
                g.setFont (font (16.0f, true));
                const bool sends = hit.area == Area::moreSends;
                const int total = sends ? (int) c->sends.size() : (int) effects->size();
                const int shown = (int) std::count_if (hits.begin(), hits.end(), [sends] (auto& h) { return h.area == (sends ? Area::send : Area::effect); });
                g.drawText ("+" + juce::String (total - shown) + " more", hit.bounds, juce::Justification::centred, true);
                break;
            }

            case Area::add:
            case Area::addSend:
                box (hover ? titleBar : juce::Colours::transparentBlack, hairlineStrong);
                g.setColour (hover ? text : textDim);
                g.setFont (font (16.0f, true));
                g.drawText (hit.area == Area::add ? "+ Add effect" : "+ Send", hit.bounds, juce::Justification::centred, true);
                break;

            case Area::mute:
                box (muted ? accent.withAlpha (0.85f) : (hover ? titleBar : panelLight), hairline);
                g.setColour (muted ? background : textDim);
                g.setFont (font (16.0f, true));
                g.drawText ("Mute", hit.bounds, juce::Justification::centred, true);
                break;

            case Area::solo:
                box (soloed ? accentCool.withAlpha (0.85f) : (hover ? titleBar : panelLight), hairline);
                g.setColour (soloed ? background : textDim);
                g.setFont (font (16.0f, true));
                g.drawText ("Solo", hit.bounds, juce::Justification::centred, true);
                break;

            case Area::header:
            case Area::none:
                break;
        }
    }

    // Empty slots for more effects, faintly, down to the sends
    {
        int filled = effectsArea.getY();

        for (auto& hit : hits)
            if (hit.area == Area::effect || hit.area == Area::more || hit.area == Area::add)
                filled = std::max (filled, hit.bounds.getBottom() + slotGap);

        g.setColour (background.withAlpha (0.45f));

        for (int y = filled; y + slotHeight <= effectsArea.getBottom(); y += slotHeight + slotGap)
            g.fillRoundedRectangle (effectsArea.withY (y).withHeight (slotHeight).toFloat(), 7.0f);
    }

    // The on/off lights, over their slots
    for (auto& hit : hits)
    {
        if (hit.area != Area::light)
            continue;

        if (auto e = effectFor (hit.id))
        {
            auto dot = juce::Rectangle<float> (9.0f, 9.0f).withCentre (hit.bounds.toFloat().getCentre().translated (1.0f, 0.0f));

            if (e->bypassed)
            {
                g.setColour (textFaint);
                g.drawEllipse (dot, 1.3f);
            }
            else
            {
                g.setColour (effectColour.withAlpha (0.3f));
                g.fillEllipse (dot.expanded (3.0f));
                g.setColour (effectColour);
                g.fillEllipse (dot);
            }
        }
    }

    // The fader's value
    const float db = kind == Kind::master ? masterVolumeToDb ((float) fader.getValue()) : channelVolumeToDb ((float) fader.getValue());
    g.setColour (textDim);
    g.setFont (monoFont (16.0f, true));
    g.drawText (dbText (db), readout, juce::Justification::centred, true);
}

const ChannelStrip::Hit* ChannelStrip::hitAt (juce::Point<int> p) const
{
    // Lights first: they sit over their slots
    for (auto& hit : hits)
        if (hit.area == Area::light && hit.bounds.contains (p))
            return &hit;

    for (auto& hit : hits)
        if (hit.bounds.contains (p))
            return &hit;

    return nullptr;
}

void ChannelStrip::mouseMove (const juce::MouseEvent& e)
{
    auto hit = hitAt (e.getPosition());
    const int index = hit != nullptr ? (int) (hit - hits.data()) : -1;

    if (index != hovered)
    {
        hovered = index;
        const bool clickable = hit != nullptr && hit->area != Area::header && ! (hit->area == Area::output && kind == Kind::bus);
        setMouseCursor (hit != nullptr && hit->area == Area::send ? juce::MouseCursor::LeftRightResizeCursor
                                                                  : clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ChannelStrip::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void ChannelStrip::mouseDown (const juce::MouseEvent& e)
{
    auto found = hitAt (e.getPosition());

    if (found == nullptr)
        return;

    const auto hit = *found;
    const int owner = kind == Kind::master ? 0 : targetId;
    const auto screenArea = localAreaToGlobal (hit.bounds);
    const auto at = juce::PopupMenu::Options().withTargetScreenArea (screenArea);
    auto c = channel();
    auto b = bus();

    switch (hit.area)
    {
        case Area::header:
            if (e.mods.isPopupMenu())
                showHeaderMenu();
            else if (onSelect != nullptr && c != nullptr)
                onSelect (c->id);
            break;

        case Area::instrument:
            if (onOpenInstrument != nullptr)
                onOpenInstrument (targetId);
            break;

        case Area::input:
            rack.showInputMenu (targetId, screenArea);
            break;

        case Area::arm:
            if (c != nullptr)
            {
                c->armed = ! c->armed;     // not saved, so no undo step
                song.notify (Song::Change::mixer);
            }
            break;

        case Area::monitor:
            if (c != nullptr)
            {
                song.checkpoint();
                c->monitor = ! c->monitor;
                song.notify (Song::Change::mixer);
            }
            break;

        case Area::light:
            if (auto effect = song.findEffect (hit.id))
            {
                song.checkpoint();
                effect->bypassed = ! effect->bypassed;
                song.notify (Song::Change::mixer);
            }
            break;

        case Area::effect:
            if (e.mods.isPopupMenu())
                rack.createEffectMenu (owner, hit.id).showMenuAsync (at);
            else if (onOpenEffect != nullptr)
                onOpenEffect (hit.id);
            break;

        case Area::more:
            rack.createEffectsMenu (owner).showMenuAsync (at);
            break;

        case Area::add:
            rack.createAddEffectMenu (owner).showMenuAsync (at);
            break;

        case Area::send:
            if (e.mods.isPopupMenu())
            {
                createSendMenu (hit.id).showMenuAsync (at);
            }
            else if (auto send = c != nullptr ? Song::findSend (*c, hit.id) : nullptr)
            {
                // Dragged sideways for its level
                song.checkpoint();
                draggingSend = hit.id;
                sendDragStart = send->level;
            }
            break;

        case Area::moreSends:
        {
            juce::PopupMenu menu;

            if (c != nullptr)
                for (auto& s : c->sends)
                    if (auto target = song.findBus (s.busId))
                        menu.addSubMenu (target->name + "  " + sendDbText (s.level) + " dB" + (s.preFader ? "  (pre)" : ""), createSendMenu (s.busId));

            menu.showMenuAsync (at);
            break;
        }

        case Area::addSend:
            showAddSendMenu (screenArea);
            break;

        case Area::output:
            if (c != nullptr)
                showOutputMenu (screenArea);
            break;

        case Area::mute:
            if (c != nullptr || b != nullptr)
            {
                song.checkpoint();
                bool& m = c != nullptr ? c->muted : b->muted;
                m = ! m;
                song.notify (Song::Change::mixer);
            }
            break;

        case Area::solo:
            if (c != nullptr)
                rack.toggleSolo (targetId);
            else if (b != nullptr)
                rack.toggleBusSolo (targetId);
            break;

        case Area::none:
            break;
    }
}

void ChannelStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingSend == 0)
        return;

    if (auto c = channel())
        if (auto send = Song::findSend (*c, draggingSend))
        {
            const float perPixel = (e.mods.isShiftDown() ? 0.2f : 1.0f) / 160.0f;
            send->level = juce::jlimit (0.0f, 1.0f, sendDragStart + e.getDistanceFromDragStartX() * perPixel);
            song.notify (Song::Change::mixer);
            repaint();
        }
}

void ChannelStrip::mouseUp (const juce::MouseEvent&)
{
    draggingSend = 0;
}

void ChannelStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto hit = hitAt (e.getPosition());

    if (hit == nullptr)
        return;

    if (hit->area == Area::send)
    {
        if (auto c = channel())
            if (auto send = Song::findSend (*c, hit->id))
            {
                song.checkpoint();
                send->level = 0.78f;
                song.notify (Song::Change::mixer);
            }
    }
    else if (hit->area == Area::header)
    {
        // FL: a channel's name opens it; a bus's renames it
        if (channel() != nullptr && onOpenInstrument != nullptr)
            onOpenInstrument (targetId);
        else if (bus() != nullptr)
            rack.renameBus (targetId);
    }
}

void ChannelStrip::showOutputMenu (juce::Rectangle<int> screenArea)
{
    auto c = channel();

    if (c == nullptr)
        return;

    const int id = c->id;
    auto routeTo = [this, id] (int busId)
    {
        return [this, id, busId]
        {
            if (auto ch = song.findChannel (id); ch != nullptr && ch->output != busId)
            {
                song.checkpoint();
                ch->output = busId;
                song.notify (Song::Change::mixer);
            }
        };
    };

    juce::PopupMenu menu;
    menu.addSectionHeader ("Play into");
    menu.addItem ("Master", true, c->output == 0, routeTo (0));

    for (auto& b : song.buses)
    {
        juce::PopupMenu::Item item (b.name);
        item.colour = b.colour;
        item.isTicked = c->output == b.id;
        item.action = routeTo (b.id);
        menu.addItem (item);
    }

    menu.addSeparator();
    menu.addItem ("New bus", [this, id]
    {
        const int busId = rack.addBus ({});

        if (auto ch = song.findChannel (id))
        {
            ch->output = busId;
            song.notify (Song::Change::mixer);
        }
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea));
}

void ChannelStrip::showAddSendMenu (juce::Rectangle<int> screenArea)
{
    auto c = channel();

    if (c == nullptr)
        return;

    const int id = c->id;
    auto addSend = [this, id] (int busId)
    {
        if (auto ch = song.findChannel (id); ch != nullptr && Song::findSend (*ch, busId) == nullptr && ch->output != busId)
        {
            song.checkpoint();
            ch->sends.push_back ({ busId, 0.78f, false });
            song.notify (Song::Change::mixer);
        }
    };

    juce::PopupMenu menu;
    menu.addSectionHeader ("Send to");

    for (auto& b : song.buses)
    {
        juce::PopupMenu::Item item (b.name);
        item.colour = b.colour;
        item.isEnabled = Song::findSend (*c, b.id) == nullptr && c->output != b.id;
        item.action = [addSend, busId = b.id] { addSend (busId); };
        menu.addItem (item);
    }

    if (! song.buses.empty())
        menu.addSeparator();

    // A shared reverb or delay, the usual reason for a send, in one go
    menu.addItem ("New reverb bus", [this, addSend] { addSend (rack.addBus ("Reverb", "nightshiftReverb")); });
    menu.addItem ("New delay bus", [this, addSend] { addSend (rack.addBus ("Delay", "nightshiftDelay")); });
    menu.addItem ("New empty bus", [this, addSend] { addSend (rack.addBus ({})); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea));
}

juce::PopupMenu ChannelStrip::createSendMenu (int busId)
{
    const int id = targetId;
    auto change = [this, id, busId] (std::function<void (Channel&, Send&)> f)
    {
        return [this, id, busId, f]
        {
            if (auto ch = song.findChannel (id))
                if (auto send = Song::findSend (*ch, busId))
                {
                    song.checkpoint();
                    f (*ch, *send);
                    song.notify (Song::Change::mixer);
                }
        };
    };

    juce::PopupMenu menu;
    auto c = channel();
    auto send = c != nullptr ? Song::findSend (*c, busId) : nullptr;

    if (send == nullptr)
        return menu;

    if (auto target = song.findBus (busId))
        menu.addSectionHeader ("Send to " + target->name);

    menu.addItem ("Before the fader", true, send->preFader, change ([] (Channel&, Send& s) { s.preFader = ! s.preFader; }));
    menu.addItem ("Back to 0 dB", change ([] (Channel&, Send& s) { s.level = 0.78f; }));
    menu.addSeparator();
    menu.addItem ("Remove send", change ([busId] (Channel& ch, Send&) { std::erase_if (ch.sends, [busId] (auto& s) { return s.busId == busId; }); }));
    return menu;
}

void ChannelStrip::showHeaderMenu()
{
    if (channel() != nullptr)
    {
        rack.showChannelMenu (targetId);
        return;
    }

    if (bus() == nullptr)
        return;

    const int id = targetId;
    juce::PopupMenu menu;
    menu.addSectionHeader (name());
    menu.addItem ("Rename...", [this, id] { rack.renameBus (id); });

    juce::PopupMenu colours;

    for (int i = 0; i < 8; ++i)
    {
        juce::PopupMenu::Item item ("Colour " + juce::String (i + 1));
        item.colour = Song::channelColour (i);
        item.action = [this, id, i]
        {
            if (auto b = song.findBus (id))
            {
                song.checkpoint();
                b->colour = Song::channelColour (i);
                song.notify (Song::Change::mixer);
            }
        };
        colours.addItem (item);
    }

    menu.addSubMenu ("Colour", colours);
    menu.addSeparator();
    menu.addItem ("Delete bus", [this, id] { rack.deleteBus (id); });
    menu.showMenuAsync ({});
}

juce::String ChannelStrip::getTooltip()
{
    auto hit = hitAt (getMouseXYRelative());

    if (hit == nullptr)
        return {};

    switch (hit->area)
    {
        case Area::header:      return kind == Kind::bus ? "Right-click to rename, recolour or delete this bus"
                                                         : kind == Kind::channel ? "Click to select; double-click to open it; right-click for its menu" : "";
        case Area::instrument:  return "Open the instrument (a plugin's window, or the sound and preset menu)";
        case Area::input:       return "The input this track records";
        case Area::arm:         return "Arm: with Record armed, Space records every armed track";
        case Area::monitor:     return "Hear the input through NightShift (and these effects) while armed";
        case Area::light:       return "Switch this effect on or off";
        case Area::effect:      return "Click to open its window; right-click to bypass, move or remove it";
        case Area::more:        return "Every effect on this strip";
        case Area::add:         return "Add an effect (or drag one here from the sidebar)";
        case Area::send:        return "Drag sideways for how much goes to this bus (double-click: 0 dB); right-click for before the fader, or to remove it";
        case Area::moreSends:   return "Every send on this channel";
        case Area::addSend:     return "Send some of this channel to a bus too (a shared reverb, say)";
        case Area::output:      return kind == Kind::bus ? "Buses play into the master" : "Where this channel plays: the master, or a bus";
        case Area::mute:        return kind == Kind::bus ? "Mute this bus (and what plays into it)" : "Mute this channel";
        case Area::solo:        return kind == Kind::bus ? "Solo: hear only what plays into this bus (again to hear everything)"
                                                         : "Solo: hear only this channel (again to hear everything)";
        case Area::none:        break;
    }

    return {};
}

juce::Rectangle<int> ChannelStrip::getSlotArea (int effectId) const
{
    for (auto& hit : hits)
        if (hit.area == Area::effect && hit.id == effectId)
            return hit.bounds;

    return {};
}

juce::Rectangle<int> ChannelStrip::getLightArea (int effectId) const
{
    for (auto& hit : hits)
        if (hit.area == Area::light && hit.id == effectId)
            return hit.bounds;

    return {};
}

juce::Rectangle<int> ChannelStrip::getSendArea (int busId) const
{
    for (auto& hit : hits)
        if (hit.area == Area::send && hit.id == busId)
            return hit.bounds;

    return {};
}

bool ChannelStrip::isInterestedInDragSource (const SourceDetails& details)
{
    return exists() && details.description.toString().startsWith (juce::String (soundDragPrefix) + "effect:");
}

void ChannelStrip::itemDropped (const SourceDetails& details)
{
    dropping = false;
    repaint();

    const auto id = details.description.toString().fromFirstOccurrenceOf ("effect:", false, false);

    if (auto desc = audio.findEffectType (id))
        rack.addEffect (kind == Kind::master ? 0 : targetId, *desc);
}

//==============================================================================
Inspector::Inspector (Song& s, AudioEngine& a, ChannelRack& r)
    : song (s), audio (a), channelStrip (s, a, r, ChannelStrip::Kind::channel), masterStrip (s, a, r, ChannelStrip::Kind::master)
{
    addAndMakeVisible (channelStrip);
    addAndMakeVisible (masterStrip);
    channelStrip.setChannel (song.selectedChannelId);
    song.addListener (this);
}

Inspector::~Inspector()
{
    song.removeListener (this);
}

void Inspector::songChanged (Song::Change c)
{
    if (c == Song::Change::notes)
        return;

    channelStrip.setChannel (song.findChannel (song.selectedChannelId) != nullptr ? song.selectedChannelId : 0);
    channelStrip.refresh();
    masterStrip.refresh();
}

void Inspector::refreshMeters()
{
    channelStrip.pushLevel (audio.getPeak (channelStrip.getChannel()));
    masterStrip.pushLevel (audio.getLastMasterPeak());
}

void Inspector::resized()
{
    auto r = getLocalBounds().withTrimmedTop (4).withTrimmedBottom (4);
    channelStrip.setBounds (r.removeFromLeft (r.getWidth() / 2));
    masterStrip.setBounds (r.withTrimmedLeft (1));
}

void Inspector::paint (juce::Graphics& g)
{
    g.setColour (hairline);
    g.fillRect (channelStrip.getRight(), 10, 1, getHeight() - 20);
}

} // namespace nightshift
