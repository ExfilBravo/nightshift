// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Mixer.h"

namespace nightshift
{

using namespace theme;

Mixer::Mixer (Song& s, AudioEngine& a, ChannelRack& r)
    : song (s), audio (a), rack (r), masterStrip (s, a, r, ChannelStrip::Kind::master)
{
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    holder.painter = [this] (juce::Graphics& g) { paintHolder (g); };

    masterStrip.setCompact (true);
    wire (masterStrip);
    addAndMakeVisible (masterStrip);

    addBusButton.setFilled (false);
    addBusButton.setFramed (true);
    addBusButton.setLabel ("Bus");
    addBusButton.setTooltip ("Add a bus: route channels into it with their Output, or send to it");
    addBusButton.onClick = [this] { rack.addBus ({}); };
    holder.addAndMakeVisible (addBusButton);

    song.addListener (this);
    rebuild();
}

Mixer::~Mixer()
{
    song.removeListener (this);
}

void Mixer::wire (ChannelStrip& strip)
{
    strip.onOpenEffect = [this] (int id) { if (onOpenEffect != nullptr) onOpenEffect (id); };
    strip.onOpenInstrument = [this] (int id) { if (onOpenInstrument != nullptr) onOpenInstrument (id); };
    strip.onOpenPianoRoll = [this] (int id) { if (onOpenPianoRoll != nullptr) onOpenPianoRoll (id); };
    strip.onShowSteps = [this] (int id) { if (onShowSteps != nullptr) onShowSteps (id); };
    strip.onSelect = [this] (int id) { if (onSelect != nullptr) onSelect (id); };
}

void Mixer::songChanged (Song::Change c)
{
    if (c == Song::Change::notes)
        return;

    if (c == Song::Change::channels || c == Song::Change::everything)
        rebuild();

    for (auto* strip : strips)
        strip->refresh();

    masterStrip.refresh();
    holder.repaint();
}

void Mixer::rebuild()
{
    std::vector<int> ids;

    for (auto& c : song.channels)
        ids.push_back (c.id);

    for (auto& b : song.buses)
        ids.push_back (-b.id);      // buses negative, so a channel and a bus can't swap unnoticed

    if (ids == builtIds)
        return;

    builtIds = ids;
    strips.clear();

    for (auto& c : song.channels)
    {
        auto* strip = strips.add (new ChannelStrip (song, audio, rack, ChannelStrip::Kind::channel));
        strip->setTarget (c.id);
        strip->setCompact (true);
        wire (*strip);
        holder.addAndMakeVisible (strip);
    }

    for (auto& b : song.buses)
    {
        auto* strip = strips.add (new ChannelStrip (song, audio, rack, ChannelStrip::Kind::bus));
        strip->setTarget (b.id);
        strip->setCompact (true);
        wire (*strip);
        holder.addAndMakeVisible (strip);
    }

    layoutStrips();
}

void Mixer::resized()
{
    auto r = getLocalBounds();
    masterArea = r.removeFromRight (stripWidth + 6);
    viewport.setBounds (r);
    layoutStrips();
}

void Mixer::layoutStrips()
{
    const int h = std::max (0, viewport.getHeight() - (viewport.isHorizontalScrollBarShown() ? viewport.getScrollBarThickness() : 0));
    const int channels = (int) song.channels.size();
    int x = 0;

    for (int i = 0; i < strips.size(); ++i)
    {
        // A little gap where the buses start
        if (i == channels)
        {
            x += 8;
            busesStartX = x;
        }

        strips[i]->setBounds (x, 4, stripWidth, std::max (0, h - 4));
        x += stripWidth;
    }

    if (strips.size() == channels)
        busesStartX = x + 8;

    addColumnX = x + 8;
    addBusButton.setBounds (addColumnX + 14, 16, addColumnWidth - 28, 34);
    holder.setSize (std::max (viewport.getWidth(), addColumnX + addColumnWidth), h);

    // The master the same height as the strips, so the faders line up
    masterStrip.setBounds (masterArea.withTrimmedLeft (6).withTrimmedTop (4).withHeight (std::max (0, h - 4)));
}

void Mixer::paintHolder (juce::Graphics& g)
{
    // A line between the channels and the buses
    const int channels = (int) song.channels.size();

    if (! song.buses.empty() && channels > 0)
    {
        g.setColour (hairlineStrong);
        g.fillRect (busesStartX - 5, 12, 1, holder.getHeight() - 24);
    }

    g.setColour (hairline);
    g.fillRect (addColumnX - 4, 12, 1, holder.getHeight() - 24);

    // What buses are for, until there are some
    if (song.buses.empty())
    {
        g.setColour (textFaint);
        g.setFont (font (15.0f));
        g.drawFittedText ("A bus gathers channels (set their Output to it) or shares one effect between them (a Send), "
                          "like a reverb for the whole kit.",
                          juce::Rectangle<int> (addColumnX + 14, 62, addColumnWidth - 28, 140), juce::Justification::topLeft, 8);
    }
}

void Mixer::paint (juce::Graphics& g)
{
    g.setColour (hairline);
    g.fillRect (masterStrip.getX() - 4, 12, 1, getHeight() - 24);
}

void Mixer::addMeteredIds (std::vector<int>& ids) const
{
    for (auto* strip : strips)
        if (strip->getChannel() != 0)
            ids.push_back (strip->getChannel());
}

void Mixer::refreshMeters()
{
    for (auto* strip : strips)
        strip->pushLevel (audio.getPeak (strip->getChannel()));

    masterStrip.pushLevel (audio.getLastMasterPeak());
}

ChannelStrip* Mixer::findStrip (int id)
{
    for (auto* strip : strips)
        if (strip->getChannel() == id)
            return strip;

    return nullptr;
}

} // namespace nightshift
