// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "AutomationRecorder.h"
#include "AudioEngine.h"

namespace nightshift
{

namespace
{
    double nowMs()      { return juce::Time::getMillisecondCounterHiRes(); }

    /** A value as far along its parameter's range as its control shows it (0..1, through any
        skew), for measuring how far a line strays; a parameter that's gone takes values as they are. */
    float along (const tracktion::AutomatableParameter* p, float value)
    {
        if (p == nullptr)
            return value;

        const auto& range = p->valueRange;
        return range.convertTo0to1 (juce::jlimit (range.start, range.end, value));
    }

    /** A bent line cut short (where a grab began, or where its glide back lands): bent again so
        its middle stays on the line it was part of. */
    void refit (AutomationPoint& from, const AutomationPoint& to, const AutomationLane& was)
    {
        if (from.curve == 0.0f)
            return;

        if (to.tick <= from.tick || std::abs (to.value - from.value) < 1.0e-6f)
        {
            from.curve = 0.0f;
            return;
        }

        const float middle = was.valueAt ((from.tick + to.tick) * 0.5);
        from.curve = automationcurve::fromMiddle ((middle - from.value) / (to.value - from.value));
    }
}

AutomationRecorder::AutomationRecorder (Song& s, AudioEngine& a)
    : song (s), audio (a)
{
    song.addListener (this);
}

AutomationRecorder::~AutomationRecorder()
{
    stopTimer();
    abandonAll();
    unwatchAll();
    song.removeListener (this);
}

void AutomationRecorder::setArmed (bool on)
{
    if (armed == on)
        return;

    if (! on)
        finishAll();

    armed = on;

    if (on)
    {
        watchParameters();
        startTimer (30);
    }
    else
    {
        stopTimer();
        unwatchAll();
    }
}

bool AutomationRecorder::isWriting() const
{
    return armed && song.mode == PlayMode::song && audio.isPlaying() && ! audio.isCountingIn();
}

bool AutomationRecorder::isWritingLane (int owner, const juce::String& param) const
{
    for (auto& t : touches)
        if (t.owner == owner && t.param == param)
            return true;

    return false;
}

int AutomationRecorder::heardTick() const
{
    return juce::jmax (0, juce::roundToInt (audio.getHeardTicks()));
}

AutomationRecorder::Touch* AutomationRecorder::find (const tracktion::AutomatableParameter& p)
{
    for (auto& t : touches)
        if (t.target.get() == &p)
            return &t;

    return nullptr;
}

//==============================================================================
void AutomationRecorder::touch (tracktion::AutomatableParameter& p, bool down, bool checkpoint)
{
    if (down)
    {
        begin (p, true, checkpoint);
        return;
    }

    for (size_t i = 0; i < touches.size(); ++i)
        if (touches[i].target.get() == &p)
        {
            end (i, isWriting());
            return;
        }
}

void AutomationRecorder::nudge (tracktion::AutomatableParameter& p, bool checkpoint)
{
    begin (p, false, checkpoint);
}

void AutomationRecorder::begin (tracktion::AutomatableParameter& p, bool held, bool checkpoint)
{
    if (! isWriting())
        return;

    // Already being written (a nudge carrying on, grabbed again): it goes on
    if (auto* t = find (p))
    {
        t->held = t->held || held;
        t->lastMoveMs = nowMs();
        return;
    }

    const auto key = audio.automationKeyFor (p);

    if (! key.has_value())
        return;

    const int owner = key->first;
    const auto param = key->second;

    if (checkpoint)
        song.checkpoint();

    Touch t;
    t.owner = owner;
    t.param = param;
    t.target = &p;
    t.held = held;
    t.lastMoveMs = t.lastSampleMs = nowMs();
    t.before = song.laneFor (owner, param, audio.automationName (owner, param)).points;
    t.start = heardTick();

    // Its moves heard from now (one on an effect added a moment ago isn't watched yet)
    if (std::none_of (watched.begin(), watched.end(), [&p] (auto& ref) { return ref.get() == &p; }))
    {
        p.addListener (this);
        watched.emplace_back (&p);
    }

    // The control takes over from its lane where the lane has it now, so grabbing it doesn't
    // jump; from here until it's let go the parameter is the hand's, not the lane's
    const float value = p.getCurrentValue();
    audio.takeParameterValue (owner, param, value);
    p.startRecordingStatus();

    t.moves.push_back ({ t.start, value });
    t.kept.push_back ({ t.start, value });
    touches.push_back (std::move (t));
    write (touches.back(), false);
    changed = false;
    song.notify (Song::Change::automation);

    if (audio.onAutomationWriting != nullptr)
        audio.onAutomationWriting (owner, param);
}

void AutomationRecorder::add (Touch& t, int tick, float value)
{
    const int last = t.last();
    const double sinceMs = nowMs() - t.lastSampleMs;
    t.lastSampleMs = nowMs();

    // Back past where it had got (the loop went round), or on further than the song could have
    // played since (the song position moved): what's written stands, and it writes on from here
    const double couldHavePlayed = sinceMs / 60000.0 * song.bpmAt (last) * ticksPerBeat;

    if (tick < last - 2 || tick > last + couldHavePlayed + ticksPerBeat)
    {
        write (t, true);

        if (auto* lane = song.findLane (t.owner, t.param))
            t.before = lane->points;

        t.start = tick;
        t.moves = t.kept = { { tick, value } };
        t.raw.clear();
        changed = true;
        return;
    }

    tick = std::max (tick, last);

    // Every move, one a tick: a move at the same moment as the last stands in its place (but not
    // the grab's own: a control moved the moment it's grabbed jumps there)
    if (t.moves.size() > 1 && t.moves.back().tick == tick)
        t.moves.back().value = value;
    else
        t.moves.push_back ({ tick, value });

    if (! t.raw.empty() && t.raw.back().tick == tick)
        t.raw.pop_back();

    // The line as it shows meanwhile: does a line from its last point to here pass close enough to
    // every move since? (Once it's let go, the whole pass is thinned again)
    const auto* p = t.target.get();
    const auto& anchor = t.kept.back();
    const float from = along (p, anchor.value), to = along (p, value);
    const float span = (float) std::max (1, tick - anchor.tick);
    const float slack = std::abs (to - from) / span * jitterTicks;
    bool fits = true;

    for (auto& move : t.raw)
    {
        const float onLine = from + (to - from) * (float) (move.tick - anchor.tick) / span;

        if (std::abs (along (p, move.value) - onLine) - slack > tolerance)
        {
            fits = false;
            break;
        }
    }

    // It strays: the line keeps the last move as a point and goes on from there. (Held still for
    // long, it takes one now and then, so there are never many moves to go through)
    if (! t.raw.empty() && (! fits || t.raw.size() >= 256))
    {
        t.kept.push_back (t.raw.back());
        t.raw.clear();
    }

    t.raw.push_back ({ tick, value });
    changed = true;
}

std::vector<AutomationPoint> AutomationRecorder::thin (const std::vector<AutomationPoint>& moves, const tracktion::AutomatableParameter* p)
{
    if (moves.size() <= 2)
        return moves;

    // The first and last moves stay; between any two that stay, the move furthest from the line
    // joining them stays too, if it's further than the tolerance (less what the line changes in
    // the jitter's time), and so on into each half
    std::vector<bool> keep (moves.size(), false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<size_t, size_t>> pending { { 0, moves.size() - 1 } };

    while (! pending.empty())
    {
        const auto [first, last] = pending.back();
        pending.pop_back();

        if (last <= first + 1)
            continue;

        const auto& a = moves[first];
        const auto& b = moves[last];
        const float from = along (p, a.value), to = along (p, b.value);
        const float span = (float) std::max (1, b.tick - a.tick);
        const float slack = std::abs (to - from) / span * jitterTicks;
        float furthest = 0.0f;
        size_t at = first;

        for (size_t i = first + 1; i < last; ++i)
        {
            const float onLine = b.tick == a.tick ? to : from + (to - from) * (float) (moves[i].tick - a.tick) / span;
            const float off = std::abs (along (p, moves[i].value) - onLine) - slack;

            if (off > furthest)
            {
                furthest = off;
                at = i;
            }
        }

        if (furthest > tolerance)
        {
            keep[at] = true;
            pending.push_back ({ first, at });
            pending.push_back ({ at, last });
        }
    }

    std::vector<AutomationPoint> line;

    for (size_t i = 0; i < moves.size(); ++i)
        if (keep[i])
            line.push_back (moves[i]);

    return line;
}

std::vector<AutomationPoint> AutomationRecorder::compose (const Touch& t, bool letGo) const
{
    const auto* p = t.target.get();
    auto line = letGo ? thin (t.moves, p) : t.kept;

    if (! letGo && ! t.raw.empty())
        line.push_back (t.raw.back());

    const int from = t.start, to = line.back().tick;
    AutomationLane was;
    was.points = t.before;
    std::vector<AutomationPoint> out;

    // What the lane had before the grab, right up to it: a point where it was grabbed, if the
    // control didn't start where the lane was (and the line into it bent as it was)
    for (auto& point : t.before)
        if (point.tick < from)
            out.push_back (point);

    if (! t.before.empty())
        if (const float there = was.valueAt (from); std::abs (along (p, there) - along (p, line.front().value)) > tolerance)
            out.push_back ({ from, there });

    for (size_t i = out.size(); i-- > 0;)
        if (out[i].tick < from)
        {
            refit (out[i], i + 1 < out.size() ? out[i + 1] : line.front(), was);
            break;
        }

    out.insert (out.end(), line.begin(), line.end());

    if (t.before.empty())
        return out;         // a lane that had nothing: it stays where it was left

    if (! letGo)
    {
        // Still held: what's ahead stays until it's reached
        for (auto& point : t.before)
            if (point.tick > to)
                out.push_back (point);

        return out;
    }

    // Let go: back to what the lane had, a sixteenth on, and on from there as it was
    AutomationPoint landing { to + glideTicks, was.valueAt (to + glideTicks) };
    const AutomationPoint* next = nullptr;

    for (auto& point : t.before)
    {
        if (point.tick > landing.tick)
        {
            next = &point;
            break;
        }

        landing.curve = point.curve;    // the bend of the line it lands on
    }

    if (next != nullptr)
        refit (landing, *next, was);
    else
        landing.curve = 0.0f;

    out.push_back (landing);

    for (auto& point : t.before)
        if (point.tick > landing.tick)
            out.push_back (point);

    return out;
}

bool AutomationRecorder::write (const Touch& t, bool letGo)
{
    auto* lane = song.findLane (t.owner, t.param);

    if (lane == nullptr)
        return false;

    lane->points = compose (t, letGo);
    return true;
}

void AutomationRecorder::end (size_t index, bool atNow)
{
    if (index >= touches.size())
        return;

    auto* p = touches[index].target.get();

    // Where it was let go, with the song still playing (stopped, it ends where it had got)
    if (atNow && p != nullptr)
        add (touches[index], heardTick(), p->getCurrentValue());

    const auto t = touches[index];
    touches.erase (touches.begin() + (std::ptrdiff_t) index);
    write (t, true);

    // Its curve on before the parameter follows it again
    changed = false;
    song.notify (Song::Change::automation);
    audio.syncNow();

    if (p != nullptr)
        p->resetRecordingStatus();
}

void AutomationRecorder::finishAll()
{
    const bool atNow = isWriting();

    while (! touches.empty())
        end (touches.size() - 1, atNow);
}

void AutomationRecorder::abandonAll()
{
    for (auto& t : touches)
        if (auto* p = t.target.get())
            p->resetRecordingStatus();

    touches.clear();
}

//==============================================================================
void AutomationRecorder::watchParameters()
{
    // Every parameter there is now, for a plugin's own window grabbing one (Tracktion's gestures)
    // and the moves of those being written; gone ones dropped, new ones joined
    const auto all = audio.allAutomationParameters();
    const std::set<tracktion::AutomatableParameter*> wanted (all.begin(), all.end());
    std::set<tracktion::AutomatableParameter*> have;

    std::erase_if (watched, [this, &wanted, &have] (auto& ref)
    {
        auto* p = ref.get();

        if (p == nullptr)
            return true;            // gone with its plugin, and its listeners with it

        if (wanted.count (p) == 0)
        {
            p->removeListener (this);
            return true;
        }

        have.insert (p);
        return false;
    });

    for (auto* p : all)
        if (have.insert (p).second)
        {
            p->addListener (this);
            watched.emplace_back (p);
        }
}

void AutomationRecorder::unwatchAll()
{
    for (auto& ref : watched)
        if (auto* p = ref.get())
            p->removeListener (this);

    watched.clear();
}

void AutomationRecorder::timerCallback()
{
    if (++timerCalls % 16 == 0)
        watchParameters();

    if (! isWriting())
    {
        if (! touches.empty())
            finishAll();

        return;
    }

    // Held still a moment, the line goes on level; a nudge left alone, or one whose plugin went,
    // is let go
    const int now = heardTick();
    std::vector<size_t> letGo;

    for (size_t i = 0; i < touches.size(); ++i)
    {
        auto& t = touches[i];
        auto* p = t.target.get();
        const double still = nowMs() - t.lastMoveMs;

        if (p == nullptr || (! t.held && still > quietMs))
            letGo.push_back (i);
        else if (still > holdMs)
            add (t, now, p->getCurrentValue());
    }

    for (auto i = letGo.rbegin(); i != letGo.rend(); ++i)
        end (*i, touches[*i].target.get() != nullptr);

    // What's written so far into the lanes, for all to see (one whose lane went, with its
    // channel, is dropped)
    if (changed)
    {
        changed = false;

        for (size_t i = touches.size(); i-- > 0;)
            if (! write (touches[i], false))
            {
                if (auto* p = touches[i].target.get())
                    p->resetRecordingStatus();

                touches.erase (touches.begin() + (std::ptrdiff_t) i);
            }

        song.notify (Song::Change::automation);
    }
}

void AutomationRecorder::songChanged (Song::Change c)
{
    // Another song, or an undo: what was being written isn't there to write into any more
    if (c == Song::Change::everything)
        abandonAll();
}

void AutomationRecorder::parameterChanged (tracktion::AutomatableParameter& p, float value)
{
    auto* t = find (p);

    if (t == nullptr || ! isWriting())
        return;

    // A move is a change: the song's controls are set again whenever the mixer changes, and that's
    // no move (held still, the timer carries the line on)
    const float previous = t->raw.empty() ? t->kept.back().value : t->raw.back().value;

    if (std::abs (value - previous) <= 1.0e-6f)
        return;

    t->lastMoveMs = nowMs();
    add (*t, heardTick(), value);
}

void AutomationRecorder::parameterChangeGestureBegin (tracktion::AutomatableParameter& p)
{
    touch (p, true);
}

void AutomationRecorder::parameterChangeGestureEnd (tracktion::AutomatableParameter& p)
{
    touch (p, false);
}

} // namespace nightshift
