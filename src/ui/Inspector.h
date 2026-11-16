// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "model/Song.h"
#include "ChannelRack.h"
#include "Widgets.h"

namespace nightshift
{

/** One channel strip, as in Logic: the channel's name and what it is, its instrument (or,
    for an audio track, its input, arm and monitor), every effect in its chain, its sends
    to buses and where it plays to, pan, a fader with its meter, mute and solo, and buttons
    to edit its notes. A bus's strip has its effects, output, pan, fader, mute and solo;
    the master's its effects, fader and meter.

    Effects: the light switches one on and off, a click on its name opens its window,
    right-click moves or removes it; effects dragged from the sidebar can be dropped on.
    Sends: drag one sideways for its level (double-click: 0 dB), right-click for before or
    after the fader, or to remove it.
*/
class ChannelStrip : public juce::Component,
                     public juce::TooltipClient,
                     public juce::DragAndDropTarget
{
public:
    enum class Kind { channel, bus, master };

    ChannelStrip (Song&, AudioEngine&, ChannelRack&, Kind);

    /** The channel or bus shown (the master's strip ignores it); 0 for none. */
    void setTarget (int id);
    void setChannel (int id)                        { setTarget (id); }
    int getChannel() const                          { return kind == Kind::master ? 0 : targetId; }
    Kind getKind() const                            { return kind; }

    /** In the mixer: a shorter header and no edit buttons, so a row of them fits. */
    void setCompact (bool);

    /** Values and layout again, after the song changed. */
    void refresh();

    /** Called at the UI rate with the strip's peak since last time. */
    void pushLevel (std::pair<float, float> peak)   { meter.push (peak.first, peak.second); }

    std::function<void (int effectId)> onOpenEffect;
    std::function<void (int channelId)> onOpenInstrument, onOpenPianoRoll, onShowSteps, onSelect;

    /** Where things are (empty if not showing), for tests. */
    juce::Rectangle<int> getSlotArea (int effectId) const;
    juce::Rectangle<int> getLightArea (int effectId) const;
    juce::Rectangle<int> getSendArea (int busId) const;
    juce::Rectangle<int> getMuteArea() const        { return muteArea; }
    juce::Rectangle<int> getSoloArea() const        { return soloArea; }
    juce::Rectangle<int> getOutputArea() const      { return outputArea; }
    Fader& getFader()                               { return fader; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override     { dropping = true; repaint(); }
    void itemDragExit (const SourceDetails&) override      { dropping = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

private:
    enum class Area { none, header, instrument, input, arm, monitor, effect, light, add, more, send, addSend, moreSends, output, mute, solo };

    struct Hit
    {
        Area area;
        juce::Rectangle<int> bounds;
        int id = 0;                 // the effect's id, or the send's bus
    };

    Song& song;
    AudioEngine& audio;
    ChannelRack& rack;
    const Kind kind;
    int targetId = 0;
    bool compact = false;

    Fader fader;
    LevelMeter meter;
    MiniBar pan { -1.0, 1.0, 0.0, true };
    IconButton pianoButton { "Piano roll", theme::icons::piano() }, stepsButton { "Steps", theme::icons::rack() };

    std::vector<Hit> hits;
    juce::Rectangle<int> header, instrumentLabel, instrumentArea, inputArea, armArea, monArea, effectsLabel, effectsArea,
                         sendsLabel, sendsArea, outputLabel, outputArea, panLabel, faderArea, readout, muteArea, soloArea;
    int hovered = -1, draggingSend = 0;
    float sendDragStart = 0.0f;
    bool dropping = false;

    Channel* channel() const;
    Bus* bus() const;
    bool exists() const;
    std::vector<Effect>* chain() const;
    float* volume() const;
    juce::String name() const;
    juce::Colour colour() const;
    juce::String instrumentName (const Channel&) const;
    juce::String typeLine() const;
    bool isSoloed() const;
    const Hit* hitAt (juce::Point<int>) const;
    void layoutHits();
    void showOutputMenu (juce::Rectangle<int> screenArea);
    void showAddSendMenu (juce::Rectangle<int> screenArea);
    juce::PopupMenu createSendMenu (int busId);
    void showHeaderMenu();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};

//==============================================================================
/** Logic's inspector, on the right of the studio: the selected channel's strip beside
    the master's, so what's on a channel and how loud it is are always in view. */
class Inspector : public juce::Component,
                  private Song::Listener
{
public:
    Inspector (Song&, AudioEngine&, ChannelRack&);
    ~Inspector() override;

    ChannelStrip& getChannelStrip()                 { return channelStrip; }
    ChannelStrip& getMasterStrip()                  { return masterStrip; }

    /** At the UI rate, after the engine's meters were read: the meters. */
    void refreshMeters();

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 296;

private:
    Song& song;
    AudioEngine& audio;
    ChannelStrip channelStrip, masterStrip;

    void songChanged (Song::Change) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Inspector)
};

} // namespace nightshift
