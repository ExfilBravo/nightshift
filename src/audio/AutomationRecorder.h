// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"

namespace nightshift
{

class AudioEngine;

/** Recording automation by moving a control while the song plays: Logic's touch mode, FL's
    automation recording. Switched on (the top bar's button beside Record), with the song playing
    in Song mode, a control grabbed writes its lane from where it was grabbed to where it was let
    go: a fader, a pan bar or a send in the Mixer or the Inspector, a knob on NightShift's effects
    or Nova, a control in a plugin's own window (its moves come as Tracktion's gestures), or a MIDI
    controller's knob linked by MIDI learn, which has nothing to let go of: it counts as let go
    once it's left alone a moment (quietMs).

    What the lane had in between goes. The moves become a thinned line, a point only where the
    line would otherwise stray more than `tolerance` of the control's range from them (so a MIDI
    knob's steps come out as one smooth line), written into the lane as it goes so the lane shows
    it at once. While it's held the control is the hand's and its lane doesn't drive it; let go,
    it goes back to what the lane had there (over a sixteenth), or stays where it was left on a lane
    that had nothing. Round the loop, or after the song position jumps, the same grab writes on
    from there. Each grab is one step for undo. */
class AutomationRecorder : private juce::Timer,
                           private Song::Listener,
                           private tracktion::AutomatableParameter::Listener
{
public:
    AutomationRecorder (Song&, AudioEngine&);
    ~AutomationRecorder() override;

    void setArmed (bool);
    bool isArmed() const                            { return armed; }

    /** Armed, and the song playing in Song mode past any count-in: a control moved now is written. */
    bool isWriting() const;

    /** A control's parameter grabbed (down) or let go. `checkpoint`: make the undo step (false when
        the control made one as it was grabbed). */
    void touch (tracktion::AutomatableParameter&, bool down, bool checkpoint = true);

    /** A change with nothing held (a MIDI knob, the wheel): written until it's left alone. */
    void nudge (tracktion::AutomatableParameter&, bool checkpoint = true);

    /** Whether a lane is being written now. */
    bool isWritingLane (int owner, const juce::String& param) const;

    /** Lets go of everything: each lane written as it stands. */
    void finishAll();

    static constexpr float tolerance = 0.006f;      // of the control's range
    static constexpr int quietMs = 700;
    static constexpr int glideTicks = ticksPerStep; // back to what the lane had, once it's let go

private:
    /** A control being written: its lane, what the lane had when this pass began, and the line so far. */
    struct Touch
    {
        int owner = 0;
        juce::String param;
        tracktion::SafeSelectable<tracktion::AutomatableParameter> target;
        bool held = true;
        double lastMoveMs = 0.0, lastSampleMs = 0.0;   // its last real move, and the last time it was looked at
        std::vector<AutomationPoint> before;
        int start = 0;                              // ticks: where this pass began
        std::vector<AutomationPoint> kept, raw;     // the line's points, and the moves since its last one

        int last() const                            { return raw.empty() ? kept.back().tick : raw.back().tick; }
    };

    Song& song;
    AudioEngine& audio;
    bool armed = false, changed = false;
    std::vector<Touch> touches;
    std::vector<tracktion::SafeSelectable<tracktion::AutomatableParameter>> watched;
    int timerCalls = 0;

    Touch* find (const tracktion::AutomatableParameter&);
    void begin (tracktion::AutomatableParameter&, bool held, bool checkpoint);
    int heardTick() const;

    /** A move at `tick`: the line goes on to it, keeping its last point if the move strays. Back
        past where it had got (round the loop) or a jump on: what's written so far is let go there
        and a new pass begins. */
    void add (Touch&, int tick, float value);
    std::vector<AutomationPoint> compose (const Touch&, bool letGo) const;
    bool write (const Touch&, bool letGo);

    /** Lets go of one (by index): written, its curve put on, the parameter following it again. */
    void end (size_t index, bool atNow);
    void abandonAll();

    void watchParameters();
    void unwatchAll();

    void timerCallback() override;
    void songChanged (Song::Change) override;
    void curveHasChanged (tracktion::AutomatableParameter&) override {}
    void parameterChanged (tracktion::AutomatableParameter&, float) override;
    void parameterChangeGestureBegin (tracktion::AutomatableParameter&) override;
    void parameterChangeGestureEnd (tracktion::AutomatableParameter&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationRecorder)
};

} // namespace nightshift
