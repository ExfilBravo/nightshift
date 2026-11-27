// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Song.h"

namespace nightshift
{

juce::String noteName (int key)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((key % 12) + 12) % 12]) + juce::String (key / 12);
}

//==============================================================================
const std::vector<Note>* Pattern::findNotes (int channelId) const
{
    auto it = notes.find (channelId);
    return it != notes.end() ? &it->second : nullptr;
}

int Pattern::lengthInTicks (int barTicks) const
{
    int maxEnd = 0;

    for (auto& [channelId, list] : notes)
        for (auto& n : list)
            maxEnd = std::max (maxEnd, n.end());

    const int bars = std::max (1, (maxEnd + barTicks - 1) / barTicks);
    return bars * barTicks;
}

bool Pattern::isEmpty() const
{
    for (auto& [channelId, list] : notes)
        if (! list.empty())
            return false;

    return true;
}

//==============================================================================
void AudioClip::setComp (double from, double to, int take)
{
    from = juce::jlimit (0.0, length, from);
    to = juce::jlimit (0.0, length, to);

    if (to - from < 1.0e-4 || ! juce::isPositiveAndBelow (take, (int) takes.size()))
        return;

    std::vector<CompSegment> result;

    for (auto& s : comp)
    {
        if (s.to <= from || s.from >= to)
        {
            result.push_back (s);
            continue;
        }

        if (s.from < from)  result.push_back ({ s.from, from, s.take });
        if (s.to > to)      result.push_back ({ to, s.to, s.take });
    }

    result.push_back ({ from, to, take });
    std::sort (result.begin(), result.end(), [] (auto& a, auto& b) { return a.from < b.from; });

    // Neighbours from the same take join up again
    comp.clear();

    for (auto& s : result)
    {
        if (! comp.empty() && comp.back().take == s.take && std::abs (comp.back().to - s.from) < 1.0e-6)
            comp.back().to = s.to;
        else if (s.to - s.from > 1.0e-6)
            comp.push_back (s);
    }
}

int AudioClip::takeAt (double t) const
{
    for (auto& s : comp)
        if (t >= s.from && t < s.to)
            return s.take;

    return -1;
}

void AudioClip::removeTake (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) takes.size()))
        return;

    takes.erase (takes.begin() + index);
    std::erase_if (comp, [index] (auto& s) { return s.take == index; });

    for (auto& s : comp)
        if (s.take > index)
            --s.take;
}

void AudioClip::keepOnlyComp()
{
    for (int i = (int) takes.size(); --i >= 0;)
        if (std::none_of (comp.begin(), comp.end(), [i] (auto& s) { return s.take == i; }))
            removeTake (i);
}

//==============================================================================
namespace steps
{
    bool isStepSequence (const std::vector<Note>& notes, int rootKey, int barTicks)
    {
        for (auto& n : notes)
            if (n.key != rootKey || n.start % ticksPerStep != 0
                 || n.length != ticksPerStep || n.start >= barTicks)
                return false;

        return true;
    }

    const Note* find (const std::vector<Note>& notes, int step, int rootKey)
    {
        for (auto& n : notes)
            if (n.start == step * ticksPerStep && n.key == rootKey)
                return &n;

        return nullptr;
    }

    void set (std::vector<Note>& notes, int step, bool on, int rootKey, int velocity)
    {
        const int start = step * ticksPerStep;

        std::erase_if (notes, [&] (const Note& n) { return n.start == start && n.key == rootKey; });

        if (on)
        {
            notes.push_back ({ start, ticksPerStep, rootKey, velocity });
            std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.start < b.start; });
        }
    }
}

//==============================================================================
Song::Song()
{
    resetToDefaults();
}

void Song::resetToDefaults()
{
    bpm = 130.0;
    swing = 0.0f;
    masterVolume = 0.8f;
    mastering = {};
    masterTurns.clear();
    masterShown = -1;
    songPrompt = songReply = {};
    channels.clear();
    patterns.clear();
    playlist.clear();
    audioClips.clear();
    tracks.clear();
    loopOn = false;
    loopStart = loopEnd = 0;
    automation.clear();
    showAutomation = false;
    controllerLinks.clear();
    masterEffects.clear();
    buses.clear();
    meterBeats = meterUnit = 4;
    tempoChanges.clear();
    meterChanges.clear();
    mode = PlayMode::pattern;
    currentPatternId = 0;
    selectedChannelId = 0;
    nextId = 1;
}

Channel* Song::findChannel (int id)
{
    for (auto& c : channels)
        if (c.id == id)
            return &c;

    return nullptr;
}

const Channel* Song::findChannel (int id) const
{
    return const_cast<Song*> (this)->findChannel (id);
}

Pattern* Song::findPattern (int id)
{
    for (auto& p : patterns)
        if (p.id == id)
            return &p;

    return nullptr;
}

const Pattern* Song::findPattern (int id) const
{
    return const_cast<Song*> (this)->findPattern (id);
}

int Song::indexOfPattern (int id) const
{
    for (int i = 0; i < (int) patterns.size(); ++i)
        if (patterns[(size_t) i].id == id)
            return i;

    return -1;
}

int Song::indexOfChannel (int id) const
{
    for (int i = 0; i < (int) channels.size(); ++i)
        if (channels[(size_t) i].id == id)
            return i;

    return -1;
}

Effect* Song::findEffect (int id, std::vector<Effect>** chain)
{
    auto search = [id, chain] (std::vector<Effect>& effects) -> Effect*
    {
        for (auto& e : effects)
            if (e.id == id)
            {
                if (chain != nullptr)
                    *chain = &effects;

                return &e;
            }

        return nullptr;
    };

    for (auto& c : channels)
        if (auto e = search (c.effects))
            return e;

    for (auto& b : buses)
        if (auto e = search (b.effects))
            return e;

    return search (masterEffects);
}

const std::vector<std::pair<int, int>>& Song::meters()
{
    // Every one a whole number of 16ths to the bar, so Steps can show it
    static const std::vector<std::pair<int, int>> list = { { 4, 4 }, { 3, 4 }, { 2, 4 }, { 5, 4 }, { 6, 4 }, { 7, 4 },
                                                           { 6, 8 }, { 9, 8 }, { 12, 8 }, { 5, 8 }, { 7, 8 }, { 11, 8 }, { 13, 8 },
                                                           { 7, 16 }, { 13, 16 }, { 15, 16 } };
    return list;
}

//==============================================================================
std::pair<int, int> Song::meterOf (const Pattern& p) const
{
    if (p.meterBeats > 0 && p.meterUnit > 0)
        return { p.meterBeats, p.meterUnit };

    return { meterBeats, meterUnit };
}

void Song::fitPatternMeter (Pattern& p, double tick) const
{
    const auto there = meterAt (tick);
    const bool start = there.beats == meterBeats && there.unit == meterUnit;
    p.meterBeats = start ? 0 : there.beats;
    p.meterUnit = start ? 0 : there.unit;
}

namespace
{
    /** A stretch of steady tempo from `from` ticks to the next one's start (the last goes on). */
    struct TempoPiece
    {
        double from = 0.0, bpm = 120.0;
    };

    double secondsPerTick (double bpm)      { return 60.0 / (ticksPerBeat * bpm); }
}

double Song::bezierBpm (double x1, double y1, double x2, double y2, double c, double x)
{
    // tracktion_Bezier.h's getBezierPoint and getBezierYFromX, step for step (in ticks where it has
    // beats: the same curve, scaled along)
    if (x1 == x2 || y1 == y2 || x <= x1)
        return y1;

    if (x >= x2)
        return y2;

    double xb, yb;

    if (y2 > y1)
    {
        const double run = x2 - x1, rise = y2 - y1;
        const double xc = x1 + run / 2, yc = y1 + rise / 2;
        xb = xc - run / 2 * -c;
        yb = yc + rise / 2 * -c;
    }
    else
    {
        const double run = x2 - x1, rise = y1 - y2;
        const double xc = x1 + run / 2, yc = y2 + rise / 2;
        xb = xc - run / 2 * -c;
        yb = yc - rise / 2 * -c;
    }

    // x along the curve is a quadratic in t: solved for t, then y at t
    const double a = x1 - 2 * xb + x2, b = -2 * x1 + 2 * xb, k = x1 - x;
    double t;

    if (a == 0)
    {
        t = -k / b;
    }
    else
    {
        t = (-b + std::sqrt (b * b - 4 * a * k)) / (2 * a);

        if (t < 0.0 || t > 1.0)
            t = (-b - std::sqrt (b * b - 4 * a * k)) / (2 * a);
    }

    return std::pow (1 - t, 2) * y1 + 2 * t * (1 - t) * yb + std::pow (t, 2) * y2;
}

double Song::glideBpm (double from, double fromBpm, double to, double toBpm, float curve, double at)
{
    if (to <= from || at <= from)
        return fromBpm;

    if (at >= to)
        return toBpm;

    return curve == 0.0f ? fromBpm + (toBpm - fromBpm) * (at - from) / (to - from)
                         : bezierBpm (from, fromBpm, to, toBpm, 0.5 * juce::jlimit (-1.0f, 1.0f, curve), at);
}

float Song::glideCurveThrough (double from, double fromBpm, double to, double toBpm, double at, double bpm)
{
    if (to <= from || at <= from || at >= to || toBpm == fromBpm)
        return 0.0f;

    // How far along the change it has got at `at`: less the more it eases in, so the curve is
    // found by halving the range it's in
    auto along = [&] (float curve) { return (glideBpm (from, fromBpm, to, toBpm, curve, at) - fromBpm) / (toBpm - fromBpm); };
    const double wanted = (bpm - fromBpm) / (toBpm - fromBpm);

    if (wanted >= along (-1.0f))
        return -1.0f;

    if (wanted <= along (1.0f))
        return 1.0f;

    float lo = -1.0f, hi = 1.0f;

    for (int i = 0; i < 40; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        (along (mid) > wanted ? lo : hi) = mid;
    }

    return 0.5f * (lo + hi);
}

float Song::tracktionCurve (const TempoChange& change)
{
    if (! change.glide)
        return 1.0f;

    constexpr float hair = 1.0f / 2048.0f;
    const float c = 0.5f * juce::jlimit (-1.0f, 1.0f, change.curve);
    return std::abs (c) >= hair ? c : (c < 0.0f ? -hair : hair);
}

// The tempo as Tracktion plays it (tracktion::tempo::Sequence), so what's drawn is where things
// sound: held until the next change, which jumps (the held tempo goes through a float there); or
// a glide in steps of a quarter of its beat (the start's time signature unit), at most 100, each
// at the tempo its curve has where it starts
static std::vector<TempoPiece> tempoPieces (const Song& song)
{
    std::vector<TempoPiece> pieces;
    const double beatTicks = ticksPerBeat * 4.0 / song.meterUnit;
    double from = 0.0, fromBpm = song.bpm;

    for (auto& next : song.tempoChanges)
    {
        if (next.tick <= from)
        {
            fromBpm = next.bpm;
            continue;
        }

        const double to = next.tick;

        if (next.glide)
        {
            const int steps = (int) std::clamp (4.0 * (to - from) / beatTicks, 1.0, 100.0);
            const double curve = Song::tracktionCurve (next);

            for (int k = 0; k < steps; ++k)
            {
                const double at = from + (to - from) * k / steps;
                pieces.push_back ({ at, Song::bezierBpm (from, fromBpm, to, next.bpm, curve, at) });
            }
        }
        else
        {
            pieces.push_back ({ from, (double) (float) fromBpm });
        }

        from = to;
        fromBpm = next.bpm;
    }

    pieces.push_back ({ from, fromBpm });
    return pieces;
}

double Song::secondsAt (double tick) const
{
    const auto pieces = tempoPieces (*this);

    if (tick <= 0.0)
        return tick * secondsPerTick (pieces.front().bpm);

    double seconds = 0.0;

    for (size_t i = 0; i < pieces.size(); ++i)
    {
        const bool last = i + 1 == pieces.size();
        const double end = last ? tick : std::min (tick, pieces[i + 1].from);
        seconds += (end - pieces[i].from) * secondsPerTick (pieces[i].bpm);

        if (last || tick <= pieces[i + 1].from)
            break;
    }

    return seconds;
}

double Song::tickAt (double seconds) const
{
    const auto pieces = tempoPieces (*this);

    if (seconds <= 0.0)
        return seconds / secondsPerTick (pieces.front().bpm);

    double at = 0.0;

    for (size_t i = 0; i < pieces.size(); ++i)
    {
        const double spt = secondsPerTick (pieces[i].bpm);

        if (i + 1 < pieces.size())
        {
            const double length = (pieces[i + 1].from - pieces[i].from) * spt;

            if (seconds > at + length)
            {
                at += length;
                continue;
            }
        }

        return pieces[i].from + (seconds - at) / spt;
    }

    return 0.0;
}

double Song::bpmAt (double tick) const
{
    const int index = tempoChangeAt (tick);
    const double held = index < 0 ? bpm : tempoChanges[(size_t) index].bpm;

    // Gliding to the next change: on the straight line between the two, or its curve
    if (const auto next = (size_t) (index + 1); next < tempoChanges.size() && tempoChanges[next].glide)
    {
        const auto& to = tempoChanges[next];
        const double from = index < 0 ? 0.0 : tempoChanges[(size_t) index].tick;

        if (to.tick > from && tick > from)
            return glideBpm (from, held, to.tick, to.bpm, to.curve, tick);
    }

    return held;
}

int Song::tempoChangeAt (double tick) const
{
    int found = -1;

    for (int i = 0; i < (int) tempoChanges.size() && tempoChanges[(size_t) i].tick <= tick; ++i)
        found = i;

    return found;
}

std::vector<MeterSection> Song::meterSections() const
{
    std::vector<MeterSection> sections { { 0, meterBeats, meterUnit, 0 } };

    for (auto& change : meterChanges)
    {
        const auto last = sections.back();

        if (change.tick <= last.start)
            continue;

        // The bar it lands in ends there, short if it has to (and still counts)
        const int bars = (change.tick - last.start + last.barTicks() - 1) / last.barTicks();
        sections.push_back ({ change.tick, change.beats, change.unit, last.firstBar + bars });
    }

    return sections;
}

MeterSection Song::meterAt (double tick) const
{
    const auto sections = meterSections();
    auto found = sections.front();

    for (auto& s : sections)
        if (s.start <= tick)
            found = s;

    return found;
}

int Song::meterChangeAt (double tick) const
{
    int found = -1;

    for (int i = 0; i < (int) meterChanges.size() && meterChanges[(size_t) i].tick <= tick; ++i)
        found = i;

    return found;
}

int Song::barStart (int bar) const
{
    const auto sections = meterSections();
    auto found = sections.front();

    for (auto& s : sections)
        if (s.firstBar <= bar)
            found = s;

    return found.start + (bar - found.firstBar) * found.barTicks();
}

int Song::barAt (double tick) const
{
    const auto s = meterAt (tick);
    return s.firstBar + (int) std::floor ((tick - s.start) / s.barTicks());
}

int Song::snapTick (double tick, int grid, int way) const
{
    if (grid == 0 || grid == 1)
        return way < 0 ? (int) std::floor (tick) : way > 0 ? (int) std::ceil (tick) : (int) std::round (tick);

    // The line at or before it, and the next
    const int bar = barAt (tick);
    const int barFrom = barStart (bar), barTo = barStart (bar + 1);
    int a = barFrom, b = barTo;

    if (grid != barGrid)
    {
        const int step = grid == beatGrid ? meterAt (tick).beatTicks() : grid;
        a = barFrom + (int) std::floor ((tick - barFrom) / step) * step;
        b = std::min (a + step, barTo);         // a bar cut short ends where it ends
    }

    if (way < 0 || tick <= a)
        return a;

    return way > 0 || tick - a >= b - tick ? b : a;
}

int Song::gridStep (int grid, double tick) const
{
    if (grid == barGrid)
        return barLength (barAt (tick));

    if (grid == beatGrid)
        return meterAt (tick).beatTicks();

    return std::max (1, grid);
}

void Song::setTempoAt (int tick, double newBpm, bool glide, float curve)
{
    newBpm = juce::jlimit (20.0, 400.0, newBpm);
    curve = juce::jlimit (-1.0f, 1.0f, curve);

    if (tick <= 0)
    {
        bpm = newBpm;
        return;
    }

    for (auto& t : tempoChanges)
    {
        if (t.tick == tick)
        {
            t.bpm = newBpm;
            t.glide = glide;
            t.curve = curve;
            return;
        }
    }

    tempoChanges.push_back ({ tick, newBpm, glide, curve });
    tidyTimeline();
}

void Song::setMeterAt (int tick, int beats, int unit)
{
    if (tick <= 0)
    {
        meterBeats = beats;
        meterUnit = unit;
    }
    else if (auto it = std::find_if (meterChanges.begin(), meterChanges.end(), [tick] (auto& m) { return m.tick == tick; });
             it != meterChanges.end())
    {
        it->beats = beats;
        it->unit = unit;
    }
    else
    {
        meterChanges.push_back ({ tick, beats, unit });
    }

    tidyTimeline();
}

void Song::removeMeterChange (int index)
{
    if (juce::isPositiveAndBelow (index, (int) meterChanges.size()))
        meterChanges.erase (meterChanges.begin() + index);

    tidyTimeline();
}

void Song::setMeterOver (int from, int to, int beats, int unit)
{
    from = std::max (0, from);

    if (to <= from)
        return;

    // What plays from `to` now, to put back after
    const auto after = meterAt (to);
    const bool changeAtEnd = std::any_of (meterChanges.begin(), meterChanges.end(), [to] (auto& m) { return m.tick == to; });
    std::erase_if (meterChanges, [from, to] (auto& m) { return m.tick > from && m.tick < to; });

    if (from == 0)
    {
        // The start's time signature: what was made in the old one stays in it
        if (beats != meterBeats || unit != meterUnit)
        {
            for (auto& p : patterns)
            {
                if (p.meterBeats <= 0 || p.meterUnit <= 0)
                {
                    p.meterBeats = meterBeats;
                    p.meterUnit = meterUnit;
                }
            }

            meterBeats = beats;
            meterUnit = unit;
        }
    }
    else
    {
        setMeterAt (from, beats, unit);
    }

    if (! changeAtEnd && (after.beats != beats || after.unit != unit))
        meterChanges.push_back ({ to, after.beats, after.unit });

    tidyTimeline();

    // A pattern now in the start's own time signature needs none of its own
    for (auto& p : patterns)
        if (p.meterBeats == meterBeats && p.meterUnit == meterUnit)
            p.meterBeats = p.meterUnit = 0;
}

void Song::fitMeterToClip (int patternId, int start)
{
    const auto* p = findPattern (patternId);

    if (p == nullptr || p->meterBeats <= 0 || p->meterUnit <= 0)
        return;

    const int beats = p->meterBeats, unit = p->meterUnit;
    const int end = start + patternLength (*p);
    const auto there = meterAt (start);
    const bool inside = std::any_of (meterChanges.begin(), meterChanges.end(), [start, end] (auto& m) { return m.tick > start && m.tick < end; });

    if (there.beats == beats && there.unit == unit && there.start <= start && ! inside
         && (start - there.start) % there.barTicks() == 0)
        return;

    setMeterOver (start, end, beats, unit);
}

void Song::tidyTimeline()
{
    auto byTick = [] (auto& a, auto& b) { return a.tick < b.tick; };
    std::erase_if (tempoChanges, [] (auto& t) { return t.tick <= 0; });

    for (auto& t : tempoChanges)
        t.curve = std::isfinite (t.curve) ? juce::jlimit (-1.0f, 1.0f, t.curve) : 0.0f;

    std::stable_sort (tempoChanges.begin(), tempoChanges.end(), byTick);
    tempoChanges.erase (std::unique (tempoChanges.begin(), tempoChanges.end(), [] (auto& a, auto& b) { return a.tick == b.tick; }),
                        tempoChanges.end());

    std::erase_if (meterChanges, [] (auto& m) { return m.tick <= 0; });
    std::stable_sort (meterChanges.begin(), meterChanges.end(), byTick);
    meterChanges.erase (std::unique (meterChanges.begin(), meterChanges.end(), [] (auto& a, auto& b) { return a.tick == b.tick; }),
                        meterChanges.end());

    // A change to the time signature already playing changes nothing: gone
    for (size_t i = 0; i < meterChanges.size();)
    {
        const auto& before = i == 0 ? MeterChange { 0, meterBeats, meterUnit } : meterChanges[i - 1];

        if (meterChanges[i].beats == before.beats && meterChanges[i].unit == before.unit)
            meterChanges.erase (meterChanges.begin() + (std::ptrdiff_t) i);
        else
            ++i;
    }
}

Bus* Song::findBus (int id)
{
    for (auto& b : buses)
        if (b.id == id)
            return &b;

    return nullptr;
}

const Bus* Song::findBus (int id) const
{
    return const_cast<Song*> (this)->findBus (id);
}

std::vector<Effect>* Song::chainFor (int ownerId)
{
    if (ownerId == 0)
        return &masterEffects;

    if (auto c = findChannel (ownerId))
        return &c->effects;

    if (auto b = findBus (ownerId))
        return &b->effects;

    return nullptr;
}

Send* Song::findSend (Channel& c, int busId)
{
    for (auto& s : c.sends)
        if (s.busId == busId)
            return &s;

    return nullptr;
}

bool Song::feedsBus (const Channel& c, int busId) const
{
    return c.output == busId || std::any_of (c.sends.begin(), c.sends.end(), [busId] (auto& s) { return s.busId == busId; });
}

Pattern& Song::currentPattern()
{
    if (auto p = findPattern (currentPatternId))
        return *p;

    if (patterns.empty())
        addPattern();

    currentPatternId = patterns.front().id;
    return patterns.front();
}

int Song::songLengthInTicks() const
{
    int end = 0;

    for (auto& clip : playlist)
        if (findPattern (clip.patternId) != nullptr)
            end = std::max (end, clipEnd (clip));

    for (auto& clip : audioClips)
        end = std::max (end, audioClipEnd (clip));

    return end;
}

float automationcurve::shape (float along, float curve)
{
    along = juce::jlimit (0.0f, 1.0f, along);
    curve = juce::jlimit (-most, most, curve);

    if (std::abs (curve) < 1.0e-4f)
        return along;

    // The power that puts the middle (1 - c) / 2 of the way: easing in climbs u^p, easing out is
    // its mirror (1 - (1 - u)^p), each 1 for a straight line
    const float power = 1.0f + std::log2 (1.0f / (1.0f - std::abs (curve)));
    return curve > 0.0f ? std::pow (along, power) : 1.0f - std::pow (1.0f - along, power);
}

float AutomationLane::valueAt (double tick) const
{
    if (points.empty())
        return 0.0f;

    if (tick <= points.front().tick)
        return points.front().value;

    for (size_t i = 1; i < points.size(); ++i)
        if (tick < points[i].tick)
        {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            const double along = (tick - a.tick) / (double) std::max (1, b.tick - a.tick);
            return a.value + (b.value - a.value) * automationcurve::shape ((float) along, a.curve);
        }

    return points.back().value;
}

AutomationLane* Song::findLane (int owner, const juce::String& param)
{
    for (auto& lane : automation)
        if (lane.owner == owner && lane.param == param)
            return &lane;

    return nullptr;
}

const AutomationLane* Song::findLane (int owner, const juce::String& param) const
{
    return const_cast<Song*> (this)->findLane (owner, param);
}

AutomationLane& Song::laneFor (int owner, const juce::String& param, const juce::String& name)
{
    if (auto lane = findLane (owner, param))
        return *lane;

    automation.push_back ({ owner, param, name, {} });
    return automation.back();
}

const ControllerLink* Song::findControllerLink (int owner, const juce::String& param) const
{
    for (auto& link : controllerLinks)
        if (link.owner == owner && link.param == param)
            return &link;

    return nullptr;
}

std::vector<const AutomationLane*> Song::lanesOf (int owner) const
{
    std::vector<const AutomationLane*> found;

    for (auto& lane : automation)
        if (lane.owner == owner)
            found.push_back (&lane);

    return found;
}

int Song::clipLength (const PlaylistClip& c) const
{
    if (c.length > 0)
        return c.length;

    auto p = findPattern (c.patternId);
    return std::max (1, (p != nullptr ? patternLength (*p) : ticksPerBar()) - c.offset);
}

void Song::setClipRange (PlaylistClip& c, int newStart, int newEnd) const
{
    newStart = std::max ({ 0, newStart, c.start - c.offset });
    newEnd = std::max (newEnd, newStart + 1);
    c.offset += newStart - c.start;
    c.start = newStart;
    c.length = newEnd - newStart;
}

void Song::setAudioRange (AudioClip& c, int startTick, double length) const
{
    // Everything in the folder moves the other way, so the audio stays put in the song (in its
    // own seconds: stretched, a tick is more or less of its audio)
    const double shift = audioSecondsAt (c, startTick);
    const double oldLength = c.length;

    // The comp's parts that reach an edge follow it out when it grows
    const bool firstAtStart = ! c.comp.empty() && c.comp.front().from < 1.0e-6;
    const bool lastAtEnd = ! c.comp.empty() && c.comp.back().to > oldLength - 1.0e-6;
    c.start = startTick;
    c.length = std::max (0.01, length);

    for (auto& t : c.takes)
    {
        t.fileOffset += shift;
        t.from -= shift;
        t.to -= shift;
    }

    std::vector<CompSegment> kept;

    for (auto s : c.comp)
    {
        s.from = std::max (0.0, s.from - shift);
        s.to = std::min (c.length, s.to - shift);

        if (s.to - s.from > 1.0e-6)
            kept.push_back (s);
    }

    // Grown back: the parts that reached its edges reach as far as their takes have audio
    if (! kept.empty())
    {
        auto& first = kept.front();
        auto& last = kept.back();

        if (shift < 0.0 && firstAtStart && juce::isPositiveAndBelow (first.take, (int) c.takes.size()))
            first.from = std::min (first.from, std::max (0.0, c.takes[(size_t) first.take].from));

        if (shift + c.length > oldLength + 1.0e-6 && lastAtEnd && juce::isPositiveAndBelow (last.take, (int) c.takes.size()))
            last.to = std::max (last.to, std::min (c.length, c.takes[(size_t) last.take].to));
    }

    c.comp = kept;
    c.fadeIn = juce::jlimit (0.0, c.length, c.fadeIn);
    c.fadeOut = juce::jlimit (0.0, c.length - c.fadeIn, c.fadeOut);
}

bool Song::splitClip (int index, int tick)
{
    if (! juce::isPositiveAndBelow (index, (int) playlist.size()))
        return false;

    const auto first = playlist[(size_t) index];
    const int end = clipEnd (first);

    if (tick <= first.start || tick >= end)
        return false;

    // A clip that played on to its pattern's end still does, from further in
    auto second = first;
    second.start = tick;
    second.offset = first.offset + (tick - first.start);
    second.length = first.length > 0 ? end - tick : 0;
    playlist[(size_t) index].length = tick - first.start;
    playlist.push_back (second);
    return true;
}

double Song::audioTickAt (const AudioClip& c, double seconds) const
{
    const double speed = juce::jlimit (AudioClip::slowest, AudioClip::fastest, c.speed);

    // Its beats on the song's: a tick is a ticksPerBeat'th of one of its beats, `speed` times faster
    if (followsTempo (c))
        return c.start + seconds * c.sourceBpm / 60.0 * ticksPerBeat / speed;

    return tickAt (secondsAt (c.start) + seconds / speed);
}

double Song::audioSecondsAt (const AudioClip& c, double tick) const
{
    const double speed = juce::jlimit (AudioClip::slowest, AudioClip::fastest, c.speed);

    if (followsTempo (c))
        return (tick - c.start) * speed * 60.0 / (c.sourceBpm * ticksPerBeat);

    return secondsBetween (c.start, tick) * speed;
}

double Song::stretchRatioAt (const AudioClip& c, double tick) const
{
    const double speed = juce::jlimit (AudioClip::slowest, AudioClip::fastest, c.speed);
    return followsTempo (c) ? speed * bpmAt (tick) / c.sourceBpm : speed;
}

bool Song::isStretched (const AudioClip& c) const
{
    constexpr double same = 1.0e-6;

    if (std::abs (c.pitch) > 1.0e-4f)
        return true;

    if (! followsTempo (c))
        return std::abs (c.speed - 1.0) > same;

    // Following the tempo it plays as recorded only where the tempo is its own all along it: at its
    // start, just before its end, at every change between, and with no glide under way inside it
    const double end = audioTickAt (c, c.length);

    for (double at : { (double) c.start, std::max ((double) c.start, end - 0.5) })
        if (std::abs (stretchRatioAt (c, at) - 1.0) > same)
            return true;

    for (size_t i = 0; i < tempoChanges.size(); ++i)
    {
        const auto& change = tempoChanges[i];
        const int from = i == 0 ? 0 : tempoChanges[i - 1].tick;
        const double before = i == 0 ? bpm : tempoChanges[i - 1].bpm;

        if (change.tick > c.start && change.tick < end && std::abs (stretchRatioAt (c, change.tick) - 1.0) > same)
            return true;

        if (change.glide && std::abs (change.bpm - before) > same && change.tick > c.start && from < end)
            return true;
    }

    return false;
}

void Song::setFollowTempo (AudioClip& c, bool on) const
{
    if (on && c.sourceBpm <= 0.0)
        c.sourceBpm = bpmAt (c.start);

    c.followTempo = on;
}

bool Song::stretchAudioTo (AudioClip& c, int startTick, double endTick) const
{
    if (endTick <= startTick || c.length <= 0.0)
        return false;

    // How fast its whole length has to go to fill the stretch: in its beats when it follows the
    // tempo, in real time when it doesn't
    const double speed = followsTempo (c) ? c.length * c.sourceBpm / 60.0 * ticksPerBeat / (endTick - startTick)
                                          : c.length / secondsBetween (startTick, endTick);

    if (speed < AudioClip::slowest - 1.0e-9 || speed > AudioClip::fastest + 1.0e-9)
        return false;

    c.start = startTick;
    c.speed = speed;
    return true;
}

AudioClip& Song::addAudioFile (const juce::File& file, int channelId, int track, int tick, double seconds, double bpm)
{
    AudioClip c;
    c.channelId = channelId;
    c.track = track;
    c.start = std::max (0, tick);
    c.length = seconds;
    c.takes = { { file.getFullPathName(), 0.0, 0.0, seconds } };
    c.comp = { { 0.0, seconds, 0 } };
    c.sourceBpm = bpm > 0.0 ? bpm : 0.0;
    c.followTempo = bpm > 0.0;
    audioClips.push_back (c);
    return audioClips.back();
}

bool Song::splitAudioClip (int index, int tick)
{
    if (! juce::isPositiveAndBelow (index, (int) audioClips.size()))
        return false;

    auto second = audioClips[(size_t) index];
    const double at = audioSecondsAt (second, tick);

    if (at <= 0.01 || at >= second.length - 0.01)
        return false;

    // Each half keeps its own outer fade; where they meet, the engine's short edge fades
    setAudioRange (second, tick, second.length - at);
    second.fadeIn = 0.0;

    auto& first = audioClips[(size_t) index];
    first.fadeOut = 0.0;
    setAudioRange (first, first.start, at);
    audioClips.push_back (second);
    return true;
}

//==============================================================================
bool Song::isTrackAudible (int row) const
{
    if (! juce::isPositiveAndBelow (row, numTracks()))
        return true;

    const bool anySoloed = std::any_of (tracks.begin(), tracks.end(), [] (auto& t) { return t.soloed; });
    return ! tracks[(size_t) row].muted && (! anySoloed || tracks[(size_t) row].soloed);
}

std::vector<int> Song::channelsOnTrack (int row) const
{
    std::vector<int> ids;
    auto add = [&ids] (int id)
    {
        if (std::find (ids.begin(), ids.end(), id) == ids.end())
            ids.push_back (id);
    };

    if (juce::isPositiveAndBelow (row, numTracks()) && findChannel (tracks[(size_t) row].channelId) != nullptr)
        add (tracks[(size_t) row].channelId);

    for (auto& c : audioClips)
        if (c.track == row && findChannel (c.channelId) != nullptr)
            add (c.channelId);

    // In the rack's order, whichever pattern on the track they're in
    for (auto& channel : channels)
        for (auto& clip : playlist)
            if (clip.track == row)
                if (auto p = findPattern (clip.patternId))
                    if (auto notes = p->findNotes (channel.id); notes != nullptr && ! notes->empty())
                        add (channel.id);

    return ids;
}

int Song::trackChannel (int row) const
{
    if (juce::isPositiveAndBelow (row, numTracks()) && findChannel (tracks[(size_t) row].channelId) != nullptr)
        return tracks[(size_t) row].channelId;

    const auto ids = channelsOnTrack (row);
    return ids.size() == 1 ? ids.front() : 0;
}

bool Song::isTrackFree (int row, int start, int end) const
{
    for (auto& clip : playlist)
        if (findPattern (clip.patternId) != nullptr && clip.track == row && clip.start < end && start < clipEnd (clip))
            return false;

    for (auto& clip : audioClips)
        if (clip.track == row && clip.start < end && start < audioClipEnd (clip))
            return false;

    return true;
}

int Song::insertTrack (Track t, int index)
{
    if (! juce::isPositiveAndBelow (index, numTracks() + 1))
        index = numTracks();

    for (auto& c : playlist)
        c.track += c.track >= index ? 1 : 0;

    for (auto& c : audioClips)
        c.track += c.track >= index ? 1 : 0;

    tracks.insert (tracks.begin() + index, std::move (t));
    return index;
}

void Song::removeTrack (int row)
{
    if (! juce::isPositiveAndBelow (row, numTracks()))
        return;

    std::erase_if (playlist, [row] (auto& c) { return c.track == row; });
    std::erase_if (audioClips, [row] (auto& c) { return c.track == row; });

    for (auto& c : playlist)
        c.track -= c.track > row ? 1 : 0;

    for (auto& c : audioClips)
        c.track -= c.track > row ? 1 : 0;

    tracks.erase (tracks.begin() + row);
}

int Song::trackForChannel (int channelId, int start, int end)
{
    int last = -1;

    for (int row = 0; row < numTracks(); ++row)
    {
        if (trackChannel (row) != channelId)
            continue;

        if (isTrackFree (row, start, end))
            return row;

        last = row;
    }

    Track t;
    t.channelId = findChannel (channelId) != nullptr ? channelId : 0;
    return insertTrack (t, last >= 0 ? last + 1 : -1);
}

void Song::coverClipsWithTracks()
{
    int rows = numTracks();

    for (auto& c : playlist)
        rows = std::max (rows, c.track + 1);

    for (auto& c : audioClips)
        rows = std::max (rows, c.track + 1);

    tracks.resize ((size_t) rows);
}

AudioClip& Song::addTakes (int channelId, double startSeconds, double length, std::vector<Take> newTakes, int compTake)
{
    // The folder starts on a whole tick; the takes' offsets absorb the remainder so the
    // audio stays exactly where it was played
    const int startTick = (int) std::ceil (tickAt (startSeconds) - 1.0e-6);
    const double shift = secondsAt (startTick) - startSeconds;

    for (auto& t : newTakes)
    {
        t.fileOffset += shift;
        t.from = std::max (0.0, t.from - shift);
        t.to -= shift;
    }

    length -= shift;
    const int endTick = (int) std::ceil (tickAt (secondsAt (startTick) + length) - 1.0e-6);

    // Recording again over a channel's folder adds to it, as in Logic (one playing at its own
    // speed: in a stretched one, a second of the new take wouldn't be a second of its audio)
    AudioClip* folder = nullptr;

    for (auto& c : audioClips)
        if (c.channelId == channelId && c.start < endTick && startTick < audioClipEnd (c) && ! isStretched (c))
            folder = &c;

    if (folder == nullptr)
    {
        AudioClip c;
        c.channelId = channelId;
        c.start = startTick;
        c.length = length;
        c.sourceBpm = bpmAt (startTick);        // played at the song's tempo, should it follow a new one later
        c.track = trackForChannel (channelId, startTick, endTick);
        audioClips.push_back (c);
        folder = &audioClips.back();
    }
    else
    {
        // Grow it to cover both; what's already there keeps its place in the song
        const int newStart = std::min (folder->start, startTick);
        const double grow = secondsBetween (newStart, folder->start);

        for (auto& t : folder->takes)
        {
            t.fileOffset -= grow;
            t.from += grow;
            t.to += grow;
        }

        for (auto& s : folder->comp)
        {
            s.from += grow;
            s.to += grow;
        }

        const double end = std::max (secondsAt (folder->start) + folder->length, secondsAt (startTick) + length);
        folder->start = newStart;
        folder->length = end - secondsAt (newStart);
    }

    const double into = secondsBetween (folder->start, startTick);
    const int first = (int) folder->takes.size();

    for (auto t : newTakes)
    {
        t.fileOffset -= into;
        t.from += into;
        t.to += into;
        folder->takes.push_back (t);
    }

    if (juce::isPositiveAndBelow (compTake, (int) newTakes.size()))
    {
        const auto& t = folder->takes[(size_t) (first + compTake)];
        folder->setComp (t.from, t.to, first + compTake);
    }

    return *folder;
}

Channel& Song::addChannel (const juce::String& name, InstrumentType type)
{
    Channel c;
    c.id = createId();
    c.name = name;
    c.type = type;
    c.colour = channelColour ((int) channels.size());
    channels.push_back (c);
    return channels.back();
}

Pattern& Song::addPattern (const juce::String& name)
{
    Pattern p;
    p.id = createId();
    p.name = name.isNotEmpty() ? name : "Pattern " + juce::String (patterns.size() + 1);
    p.colour = patternColour ((int) patterns.size());
    patterns.push_back (p);
    return patterns.back();
}

Pattern& Song::addPatternFor (int channelId)
{
    auto c = findChannel (channelId);
    const auto base = c != nullptr ? c->name : juce::String();
    auto name = base;

    for (int n = 2; name.isNotEmpty() && std::any_of (patterns.begin(), patterns.end(), [&name] (auto& p) { return p.name == name; }); ++n)
        name = base + " " + juce::String (n);

    const auto colour = c != nullptr ? c->colour : juce::Colour();
    auto& p = addPattern (name);

    if (! colour.isTransparent())
        p.colour = colour;

    return p;
}

void Song::removeChannel (int id)
{
    // Its tracks go too, unless something else is on them
    std::vector<int> itsTracks;

    for (int row = 0; row < numTracks(); ++row)
        if (tracks[(size_t) row].channelId == id)
        {
            tracks[(size_t) row].channelId = 0;
            itsTracks.push_back (row);
        }

    std::erase_if (channels, [id] (auto& c) { return c.id == id; });
    std::erase_if (automation, [id] (auto& lane) { return lane.owner == id; });
    std::erase_if (controllerLinks, [id] (auto& link) { return link.owner == id; });

    for (auto& p : patterns)
        p.notes.erase (id);

    std::erase_if (audioClips, [id] (auto& c) { return c.channelId == id; });

    for (auto row = itsTracks.rbegin(); row != itsTracks.rend(); ++row)
        if (isTrackEmpty (*row))
            removeTrack (*row);

    if (selectedChannelId == id)
        selectedChannelId = channels.empty() ? 0 : channels.front().id;
}

Bus& Song::addBus (const juce::String& name)
{
    Bus b;
    b.id = createId();
    b.name = name.isNotEmpty() ? name : "Bus " + juce::String (buses.size() + 1);
    b.colour = channelColour ((int) buses.size() * 3 + 2);
    buses.push_back (b);
    return buses.back();
}

void Song::removeBus (int id)
{
    std::erase_if (buses, [id] (auto& b) { return b.id == id; });

    for (auto& c : channels)
    {
        if (c.output == id)
            c.output = 0;

        std::erase_if (c.sends, [id] (auto& s) { return s.busId == id; });
    }

    // Its own lanes and links, and the channels' for their sends to it
    const auto sendKey = "send:" + juce::String (id);
    std::erase_if (automation, [id, &sendKey] (auto& lane) { return lane.owner == id || lane.param == sendKey; });
    std::erase_if (controllerLinks, [id, &sendKey] (auto& link) { return link.owner == id || link.param == sendKey; });
}

void Song::removePattern (int id)
{
    std::erase_if (patterns, [id] (auto& p) { return p.id == id; });
    std::erase_if (playlist, [id] (auto& c) { return c.patternId == id; });

    if (currentPatternId == id)
        currentPatternId = patterns.empty() ? 0 : patterns.front().id;
}

//==============================================================================
void Song::notify (Change c)
{
    if (c != Change::selection && c != Change::currentPattern)
        dirty = true;

    listeners.call ([c] (Listener& l) { l.songChanged (c); });
}

void Song::checkpoint()
{
    undoStack.push_back (juce::JSON::toString (toVar(), true));

    if (undoStack.size() > 200)
        undoStack.erase (undoStack.begin());

    redoStack.clear();
}

bool Song::undo()
{
    if (undoStack.empty())
        return false;

    redoStack.push_back (juce::JSON::toString (toVar(), true));
    restore (undoStack.back());
    undoStack.pop_back();
    return true;
}

bool Song::redo()
{
    if (redoStack.empty())
        return false;

    undoStack.push_back (juce::JSON::toString (toVar(), true));
    restore (redoStack.back());
    redoStack.pop_back();
    return true;
}

void Song::restore (const juce::String& json)
{
    // Undo changes the music, not where you are looking
    const auto oldMode = mode;
    const auto oldPattern = currentPatternId;
    const auto oldChannel = selectedChannelId;
    std::vector<int> armedIds;

    for (auto& c : channels)
        if (c.armed)
            armedIds.push_back (c.id);

    fromVar (juce::JSON::parse (json));

    mode = oldMode;

    for (auto id : armedIds)
        if (auto c = findChannel (id))
            c->armed = true;

    if (findPattern (oldPattern) != nullptr)
        currentPatternId = oldPattern;

    if (findChannel (oldChannel) != nullptr)
        selectedChannelId = oldChannel;

    notify (Change::everything);
}

//==============================================================================
juce::Colour Song::channelColour (int index)
{
    static const juce::uint32 palette[] = { 0xffef7a6c, 0xfff2a65a, 0xfff2cf5b, 0xff9bd77f,
                                            0xff5fd0c5, 0xff64a8ff, 0xff9d8cff, 0xffe27fd0 };
    return juce::Colour (palette[(size_t) index % std::size (palette)]);
}

juce::Colour Song::patternColour (int index)
{
    static const juce::uint32 palette[] = { 0xff5b7cfa, 0xffc06bd8, 0xff3fb6a8, 0xffe0894f,
                                            0xffd85b7a, 0xff7fb04a, 0xff4f9bd8, 0xffb59a3c };
    return juce::Colour (palette[(size_t) index % std::size (palette)]);
}

//==============================================================================
void Song::loadEmpty()
{
    resetToDefaults();

    for (auto name : { "Kick", "Clap", "Hat", "Snare" })
        addChannel (name, InstrumentType::sampler).sample = juce::String ("kit:") + name;

    currentPatternId = addPattern().id;
    selectedChannelId = channels.front().id;
    undoStack.clear();
    redoStack.clear();
    notify (Change::everything);
    dirty = false;     // after notify, which marks every change as unsaved
}

void Song::loadBlank()
{
    resetToDefaults();
    bpm = 120.0;
    tracks.push_back ({});
    currentPatternId = addPattern().id;
    mode = PlayMode::song;
    undoStack.clear();
    redoStack.clear();
    notify (Change::everything);
    dirty = false;     // after notify, which marks every change as unsaved
}

void Song::loadDemo()
{
    resetToDefaults();
    bpm = 88.0;
    swing = 0.32f;

    auto sampler = [this] (const char* name, float volume) -> Channel&
    {
        auto& c = addChannel (name, InstrumentType::sampler);
        c.sample = juce::String ("kit:") + name;
        c.volume = volume;
        return c;
    };

    auto synth = [this] (const char* name, const char* preset, float volume) -> Channel&
    {
        auto& c = addChannel (name, InstrumentType::synth);
        c.synthPreset = preset;
        c.volume = volume;
        return c;
    };

    const int kick    = sampler ("Kick", 0.60f).id;
    const int snare   = sampler ("Snare", 0.62f).id;
    const int clap    = sampler ("Clap", 0.60f).id;
    const int hat     = sampler ("Hat", 0.72f).id;
    const int openHat = sampler ("Open Hat", 0.56f).id;
    const int rim     = sampler ("Rim", 0.64f).id;
    const int shaker  = sampler ("Shaker", 0.60f).id;

    auto& bass808 = sampler ("808", 0.47f);
    bass808.rootKey = 36;            // the kit 808 is tuned to C2
    bass808.oneShot = false;         // note length shapes the 808 tail
    const int sub = bass808.id;

    const int keys = synth ("Keys", "Velvet Keys", 0.84f).id;
    const int pad  = synth ("Pad", "Night Pad", 0.52f).id;

    auto stepsOn = [] (Pattern& p, int channel, std::initializer_list<std::pair<int, int>> stepVelocities)
    {
        for (auto [step, velocity] : stepVelocities)
            steps::set (p.notesFor (channel), step, true, defaultRootKey, velocity);
    };

    //==============================================================================
    auto& beat = addPattern ("Beat");
    stepsOn (beat, kick,  { { 0, 120 }, { 7, 96 }, { 10, 112 } });
    stepsOn (beat, snare, { { 4, 112 }, { 12, 112 } });
    stepsOn (beat, clap,  { { 4, 90 }, { 12, 90 } });
    stepsOn (beat, hat,   { { 0, 100 }, { 2, 70 }, { 4, 96 }, { 6, 70 }, { 8, 100 }, { 10, 70 }, { 12, 96 }, { 14, 70 }, { 15, 52 } });
    stepsOn (beat, rim,   { { 3, 64 }, { 13, 58 } });

    auto& fill = addPattern ("Fill");
    stepsOn (fill, kick,    { { 0, 120 }, { 7, 96 }, { 10, 112 } });
    stepsOn (fill, snare,   { { 4, 112 }, { 12, 112 }, { 13, 58 }, { 14, 74 }, { 15, 96 } });
    stepsOn (fill, clap,    { { 4, 90 }, { 12, 90 } });
    stepsOn (fill, hat,     { { 0, 100 }, { 2, 70 }, { 4, 96 }, { 6, 70 }, { 8, 100 } });
    stepsOn (fill, openHat, { { 10, 84 } });

    auto& beat2 = addPattern ("Beat 2");
    stepsOn (beat2, kick,    { { 0, 120 }, { 7, 96 }, { 10, 112 }, { 15, 70 } });
    stepsOn (beat2, snare,   { { 4, 112 }, { 12, 112 } });
    stepsOn (beat2, clap,    { { 4, 90 }, { 12, 90 } });
    stepsOn (beat2, hat,     { { 0, 100 }, { 2, 70 }, { 4, 96 }, { 6, 70 }, { 8, 100 }, { 10, 70 }, { 12, 96 } });
    stepsOn (beat2, openHat, { { 14, 72 } });
    stepsOn (beat2, rim,     { { 3, 64 }, { 13, 58 } });
    stepsOn (beat2, shaker,  { { 1, 46 }, { 3, 58 }, { 5, 46 }, { 7, 58 }, { 9, 46 }, { 11, 58 }, { 13, 46 }, { 15, 58 } });

    //==============================================================================
    // VI - v - iv - i in F minor, voiced to move by step
    struct Chord { int bass; std::array<int, 4> upper; };
    const Chord chords[] = { { 49, { 60, 63, 65, 68 } },    // Dbmaj9
                             { 48, { 58, 62, 63, 67 } },    // Cm9
                             { 46, { 56, 60, 61, 65 } },    // Bbm9
                             { 41, { 56, 60, 63, 67 } } };  // Fm9

    auto& keysPattern = addPattern ("Keys");

    for (int bar = 0; bar < 4; ++bar)
    {
        const int barStart = bar * ticksPerBar();
        auto& chord = chords[bar];
        auto& keyNotes = keysPattern.notesFor (keys);

        // Main hit, lightly rolled from the bass up
        keyNotes.push_back ({ barStart, 2 * ticksPerBeat + ticksPerStep, chord.bass, 92 });

        for (int i = 0; i < 4; ++i)
            keyNotes.push_back ({ barStart + 5 + i * 5, 2 * ticksPerBeat + ticksPerStep - (5 + i * 5), chord.upper[(size_t) i], 80 - i * 3 });

        // Softer re-strike on the "and" of beat three
        for (int i = 0; i < 4; ++i)
            keyNotes.push_back ({ barStart + (int) (2.5 * ticksPerBeat) + i * 2, ticksPerBeat + ticksPerBeat / 2 - 8, chord.upper[(size_t) i], 66 });

        for (auto key : chord.upper)
            keysPattern.notesFor (pad).push_back ({ barStart, ticksPerBar() - ticksPerStep / 2, key + 12, 70 });
    }

    auto& bassPattern = addPattern ("808");
    const int roots[] = { 37, 36, 34, 41 };

    for (int bar = 0; bar < 4; ++bar)
    {
        const int barStart = bar * ticksPerBar();
        auto& n = bassPattern.notesFor (sub);
        n.push_back ({ barStart,       150, roots[bar], 112 });
        n.push_back ({ barStart + 168,  64, roots[bar], 96 });
        n.push_back ({ barStart + 240, 130, bar == 3 ? 44 : roots[bar], 106 });
    }

    //==============================================================================
    // Patterns live in a vector, so earlier references are stale by now: go by name
    auto place = [this] (const char* patternName, int track, int bar)
    {
        for (auto& p : patterns)
            if (p.name == patternName)
                playlist.push_back ({ p.id, track, bar * ticksPerBar() });
    };

    for (int bar : { 0, 4, 8 })
        place ("Keys", 0, bar);

    for (int bar : { 4, 5, 6 })    place ("Beat", 1, bar);
    place ("Fill", 1, 7);
    for (int bar : { 8, 9, 10 })   place ("Beat 2", 1, bar);
    place ("Fill", 1, 11);

    for (int bar : { 4, 8 })
        place ("808", 2, bar);

    coverClipsWithTracks();
    mode = PlayMode::song;
    currentPatternId = patterns.front().id;
    selectedChannelId = kick;
    undoStack.clear();
    redoStack.clear();
    notify (Change::everything);
    dirty = false;     // after notify, which marks every change as unsaved
}

//==============================================================================
static juce::String typeToString (InstrumentType t)
{
    switch (t)
    {
        case InstrumentType::synth:   return "synth";
        case InstrumentType::plugin:  return "plugin";
        case InstrumentType::audio:   return "audio";
        case InstrumentType::kit:     return "kit";
        case InstrumentType::instrument: return "instrument";
        case InstrumentType::midiOut: return "midiOut";
        case InstrumentType::sampler: break;
    }

    return "sampler";
}

static InstrumentType typeFromString (const juce::String& s)
{
    if (s == "synth")   return InstrumentType::synth;
    if (s == "plugin")  return InstrumentType::plugin;
    if (s == "audio")   return InstrumentType::audio;
    if (s == "kit")     return InstrumentType::kit;
    if (s == "instrument") return InstrumentType::instrument;
    if (s == "midiOut") return InstrumentType::midiOut;
    return InstrumentType::sampler;
}

static juce::var drummerToVar (const Drummer& d)
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> turns;

    for (auto& t : d.turns)
    {
        auto* turn = new juce::DynamicObject();
        turn->setProperty ("prompt", t.prompt);
        turn->setProperty ("reply", t.reply);
        turn->setProperty ("groove", t.groove);
        turn->setProperty ("answer", t.answer);
        turns.add (juce::var (turn));
    }

    auto* patterns = new juce::DynamicObject();

    for (auto& [name, id] : d.patterns)
        patterns->setProperty (name, id);

    o->setProperty ("turns", turns);
    o->setProperty ("shown", d.shown);
    o->setProperty ("swing", d.swing);
    o->setProperty ("looseness", d.looseness);
    o->setProperty ("dynamics", d.dynamics);
    o->setProperty ("patterns", juce::var (patterns));
    return juce::var (o);
}

static Drummer drummerFromVar (const juce::var& v)
{
    Drummer d;

    if (auto* turns = v.getProperty ("turns", {}).getArray())
        for (auto& t : *turns)
            d.turns.push_back ({ t.getProperty ("prompt", {}).toString(), t.getProperty ("reply", {}).toString(),
                                 t.getProperty ("groove", {}).toString(), t.getProperty ("answer", {}).toString() });

    d.shown = juce::jlimit (-1, (int) d.turns.size() - 1, (int) v.getProperty ("shown", -1));
    d.swing = (float) v.getProperty ("swing", -1.0);
    d.looseness = (float) v.getProperty ("looseness", 1.0);
    d.dynamics = (float) v.getProperty ("dynamics", 1.0);

    if (auto* patterns = v.getProperty ("patterns", {}).getDynamicObject())
        for (auto& p : patterns->getProperties())
            d.patterns[p.name.toString()] = (int) p.value;

    return d;
}

static juce::var bassToVar (const Bassist& b)
{
    juce::Array<juce::var> turns;

    for (auto& t : b.turns)
    {
        auto* turn = new juce::DynamicObject();
        turn->setProperty ("prompt", t.prompt);
        turn->setProperty ("reply", t.reply);
        turn->setProperty ("part", t.part);
        turn->setProperty ("answer", t.answer);
        turns.add (juce::var (turn));
    }

    auto* patterns = new juce::DynamicObject();

    for (auto& [name, id] : b.patterns)
        patterns->setProperty (name, id);

    auto* o = new juce::DynamicObject();
    o->setProperty ("turns", turns);
    o->setProperty ("shown", b.shown);
    o->setProperty ("patterns", juce::var (patterns));
    return juce::var (o);
}

static Bassist bassFromVar (const juce::var& v)
{
    Bassist b;

    if (auto* turns = v.getProperty ("turns", {}).getArray())
        for (auto& t : *turns)
            b.turns.push_back ({ t.getProperty ("prompt", {}).toString(), t.getProperty ("reply", {}).toString(),
                                 t.getProperty ("part", {}).toString(), t.getProperty ("answer", {}).toString() });

    b.shown = juce::jlimit (-1, (int) b.turns.size() - 1, (int) v.getProperty ("shown", -1));

    if (auto* patterns = v.getProperty ("patterns", {}).getDynamicObject())
        for (auto& p : patterns->getProperties())
            b.patterns[p.name.toString()] = (int) p.value;

    return b;
}

static juce::var guitarToVar (const Guitarist& g)
{
    juce::Array<juce::var> turns;

    for (auto& t : g.turns)
    {
        auto* turn = new juce::DynamicObject();
        turn->setProperty ("prompt", t.prompt);
        turn->setProperty ("reply", t.reply);
        turn->setProperty ("part", t.part);
        turn->setProperty ("answer", t.answer);
        turns.add (juce::var (turn));
    }

    auto* patterns = new juce::DynamicObject();

    for (auto& [name, id] : g.patterns)
        patterns->setProperty (name, id);

    auto* o = new juce::DynamicObject();
    o->setProperty ("turns", turns);
    o->setProperty ("shown", g.shown);
    o->setProperty ("patterns", juce::var (patterns));
    return juce::var (o);
}

static Guitarist guitarFromVar (const juce::var& v)
{
    Guitarist g;

    if (auto* turns = v.getProperty ("turns", {}).getArray())
        for (auto& t : *turns)
            g.turns.push_back ({ t.getProperty ("prompt", {}).toString(), t.getProperty ("reply", {}).toString(),
                                 t.getProperty ("part", {}).toString(), t.getProperty ("answer", {}).toString() });

    g.shown = juce::jlimit (-1, (int) g.turns.size() - 1, (int) v.getProperty ("shown", -1));

    if (auto* patterns = v.getProperty ("patterns", {}).getDynamicObject())
        for (auto& p : patterns->getProperties())
            g.patterns[p.name.toString()] = (int) p.value;

    return g;
}

static juce::var keysToVar (const KeysPlayer& k)
{
    juce::Array<juce::var> turns;

    for (auto& t : k.turns)
    {
        auto* turn = new juce::DynamicObject();
        turn->setProperty ("prompt", t.prompt);
        turn->setProperty ("reply", t.reply);
        turn->setProperty ("part", t.part);
        turn->setProperty ("answer", t.answer);
        turns.add (juce::var (turn));
    }

    auto* patterns = new juce::DynamicObject();

    for (auto& [name, id] : k.patterns)
        patterns->setProperty (name, id);

    auto* o = new juce::DynamicObject();
    o->setProperty ("turns", turns);
    o->setProperty ("shown", k.shown);
    o->setProperty ("patterns", juce::var (patterns));
    return juce::var (o);
}

static KeysPlayer keysFromVar (const juce::var& v)
{
    KeysPlayer k;

    if (auto* turns = v.getProperty ("turns", {}).getArray())
        for (auto& t : *turns)
            k.turns.push_back ({ t.getProperty ("prompt", {}).toString(), t.getProperty ("reply", {}).toString(),
                                 t.getProperty ("part", {}).toString(), t.getProperty ("answer", {}).toString() });

    k.shown = juce::jlimit (-1, (int) k.turns.size() - 1, (int) v.getProperty ("shown", -1));

    if (auto* patterns = v.getProperty ("patterns", {}).getDynamicObject())
        for (auto& p : patterns->getProperties())
            k.patterns[p.name.toString()] = (int) p.value;

    return k;
}

// The synth player's turns and patterns are saved the same way as the keys player's
static juce::var synthPlayerToVar (const SynthPlayer& sp)
{
    KeysPlayer k;
    k.shown = sp.shown;
    k.patterns = sp.patterns;

    for (auto& t : sp.turns)
        k.turns.push_back ({ t.prompt, t.reply, t.part, t.answer });

    return keysToVar (k);
}

static SynthPlayer synthPlayerFromVar (const juce::var& v)
{
    const auto k = keysFromVar (v);
    SynthPlayer sp;
    sp.shown = k.shown;
    sp.patterns = k.patterns;

    for (auto& t : k.turns)
        sp.turns.push_back ({ t.prompt, t.reply, t.part, t.answer });

    return sp;
}

juce::var Song::Mastering::toVar() const
{
    auto* m = new juce::DynamicObject();
    m->setProperty ("target", target);
    m->setProperty ("push", pushDb);
    m->setProperty ("ceiling", ceilingDb);

    auto* tone = new juce::DynamicObject();
    juce::Array<juce::var> bands;

    for (size_t i = 0; i < toneDb.size(); ++i)
        bands.add (juce::Array<juce::var> { toneDb[i], toneWhere[i] });

    tone->setProperty ("on", toneOn);
    tone->setProperty ("bands", bands);
    m->setProperty ("tone", juce::var (tone));

    auto* punch = new juce::DynamicObject();
    punch->setProperty ("on", punchOn);
    punch->setProperty ("amount", punchAmount);
    punch->setProperty ("shares", juce::Array<juce::var> { punchShare[0], punchShare[1], punchShare[2] });
    m->setProperty ("punch", juce::var (punch));

    auto* sides = new juce::DynamicObject();
    sides->setProperty ("on", widthOn);
    sides->setProperty ("amount", width);
    sides->setProperty ("monoBass", monoBassHz);
    m->setProperty ("width", juce::var (sides));

    return juce::var (m);
}

Song::Mastering Song::Mastering::fromVar (const juce::var& m)
{
    Mastering mastering;

    {
        mastering.target = m.getProperty ("target", "safe").toString();
        mastering.pushDb = juce::jlimit (-24.0f, 24.0f, (float) m.getProperty ("push", 0.0));
        mastering.ceilingDb = juce::jlimit (-6.0f, 0.0f, (float) m.getProperty ("ceiling", -1.0));

        const auto tone = m.getProperty ("tone", {});
        mastering.toneOn = tone.getProperty ("on", true);

        if (auto* bands = tone.getProperty ("bands", {}).getArray())
            for (int i = 0; i < std::min (bands->size(), (int) mastering.toneDb.size()); ++i)
            {
                mastering.toneDb[(size_t) i] = juce::jlimit (-6.0f, 6.0f, (float) (*bands)[i][0]);
                mastering.toneWhere[(size_t) i] = juce::jlimit (0, 2, (int) (*bands)[i][1]);
            }

        const auto punch = m.getProperty ("punch", {});
        mastering.punchOn = punch.getProperty ("on", true);
        mastering.punchAmount = juce::jlimit (0.0f, 1.0f, (float) punch.getProperty ("amount", 0.0));

        if (auto* shares = punch.getProperty ("shares", {}).getArray())
            for (int i = 0; i < std::min (shares->size(), 3); ++i)
                mastering.punchShare[(size_t) i] = juce::jlimit (0.0f, 1.5f, (float) (*shares)[i]);

        const auto width = m.getProperty ("width", {});
        mastering.widthOn = width.getProperty ("on", true);
        mastering.width = juce::jlimit (0.0f, 2.0f, (float) width.getProperty ("amount", 1.0));
        mastering.monoBassHz = juce::jlimit (0.0f, 300.0f, (float) width.getProperty ("monoBass", 0.0));
    }

    return mastering;
}

static juce::var effectsToVar (const std::vector<Effect>& effects)
{
    juce::Array<juce::var> list;

    for (auto& e : effects)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", e.id);
        o->setProperty ("plugin", e.pluginId);
        o->setProperty ("name", e.name);
        o->setProperty ("state", e.state);
        o->setProperty ("bypassed", e.bypassed);
        list.add (juce::var (o));
    }

    return list;
}

static std::vector<Effect> effectsFromVar (const juce::var& v, int& maxId)
{
    std::vector<Effect> effects;

    if (auto* list = v.getArray())
    {
        for (auto& o : *list)
        {
            Effect e;
            e.id = o.getProperty ("id", 0);
            e.pluginId = o.getProperty ("plugin", {}).toString();
            e.name = o.getProperty ("name", "Effect").toString();
            e.state = o.getProperty ("state", {}).toString();
            e.bypassed = o.getProperty ("bypassed", false);
            maxId = std::max (maxId, e.id);
            effects.push_back (e);
        }
    }

    return effects;
}

juce::var Song::toVar() const
{
    auto root = new juce::DynamicObject();
    root->setProperty ("format", "nightshift-project");
    root->setProperty ("version", 1);
    root->setProperty ("bpm", bpm);
    root->setProperty ("meter", juce::Array<juce::var> { meterBeats, meterUnit });

    // Along the song: [tick, bpm, glide] (and a glide's curve after, when it has one) and [tick, beats, unit]
    if (! tempoChanges.empty())
    {
        juce::Array<juce::var> list;

        for (auto& t : tempoChanges)
            list.add (t.curve != 0.0f ? juce::Array<juce::var> { t.tick, t.bpm, t.glide, t.curve }
                                      : juce::Array<juce::var> { t.tick, t.bpm, t.glide });

        root->setProperty ("tempoChanges", list);
    }

    if (! meterChanges.empty())
    {
        juce::Array<juce::var> list;

        for (auto& m : meterChanges)
            list.add (juce::Array<juce::var> { m.tick, m.beats, m.unit });

        root->setProperty ("meterChanges", list);
    }

    root->setProperty ("swing", swing);
    root->setProperty ("masterVolume", masterVolume);

    root->setProperty ("mastering", mastering.toVar());

    if (songPrompt.isNotEmpty())
    {
        auto* writer = new juce::DynamicObject();
        writer->setProperty ("prompt", songPrompt);
        writer->setProperty ("reply", songReply);
        root->setProperty ("song_writer", juce::var (writer));
    }

    if (! masterTurns.empty())
    {
        juce::Array<juce::var> turns;

        for (auto& t : masterTurns)
        {
            auto* turn = new juce::DynamicObject();
            turn->setProperty ("prompt", t.prompt);
            turn->setProperty ("reply", t.reply);
            turn->setProperty ("settings", t.settings);
            turn->setProperty ("answer", t.answer);
            turns.add (juce::var (turn));
        }

        auto* engineer = new juce::DynamicObject();
        engineer->setProperty ("turns", turns);
        engineer->setProperty ("shown", masterShown);
        root->setProperty ("master_engineer", juce::var (engineer));
    }

    root->setProperty ("mode", mode == PlayMode::song ? "song" : "pattern");
    root->setProperty ("currentPattern", currentPatternId);
    root->setProperty ("selectedChannel", selectedChannelId);

    juce::Array<juce::var> channelList;

    for (auto& c : channels)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("id", c.id);
        o->setProperty ("name", c.name);
        o->setProperty ("colour", c.colour.toString());
        o->setProperty ("type", typeToString (c.type));
        o->setProperty ("sample", c.sample);
        o->setProperty ("rootKey", c.rootKey);
        o->setProperty ("oneShot", c.oneShot);
        o->setProperty ("synthPreset", c.synthPreset);
        o->setProperty ("pluginId", c.pluginId);
        o->setProperty ("pluginState", c.pluginState);
        o->setProperty ("kit", c.kit);
        o->setProperty ("instrument", c.instrument);

        // A synth plugged in: [the MIDI output, its channel, the program asked for]
        if (c.type == InstrumentType::midiOut || c.midiOutDevice.isNotEmpty() || c.midiProgram >= 0)
            o->setProperty ("midiOut", juce::Array<juce::var> { c.midiOutDevice, c.midiOutChannel, c.midiProgram });

        if (! c.drummer.turns.empty())
            o->setProperty ("drummer", drummerToVar (c.drummer));

        if (! c.bass.turns.empty())
            o->setProperty ("bass", bassToVar (c.bass));

        if (! c.guitar.turns.empty())
            o->setProperty ("guitar", guitarToVar (c.guitar));

        if (! c.keys.turns.empty())
            o->setProperty ("keys", keysToVar (c.keys));

        if (! c.synthPlayer.turns.empty())
            o->setProperty ("synth_player", synthPlayerToVar (c.synthPlayer));

        o->setProperty ("input", c.input);
        o->setProperty ("monitor", c.monitor);
        o->setProperty ("volume", c.volume);
        o->setProperty ("pan", c.pan);
        o->setProperty ("muted", c.muted);
        o->setProperty ("effects", effectsToVar (c.effects));
        o->setProperty ("output", c.output);

        juce::Array<juce::var> sendList;

        for (auto& s : c.sends)
            sendList.add (juce::Array<juce::var> { s.busId, s.level, s.preFader });

        o->setProperty ("sends", sendList);
        channelList.add (juce::var (o));
    }

    root->setProperty ("channels", channelList);
    root->setProperty ("masterEffects", effectsToVar (masterEffects));

    juce::Array<juce::var> busList;

    for (auto& b : buses)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("id", b.id);
        o->setProperty ("name", b.name);
        o->setProperty ("colour", b.colour.toString());
        o->setProperty ("volume", b.volume);
        o->setProperty ("pan", b.pan);
        o->setProperty ("muted", b.muted);
        o->setProperty ("effects", effectsToVar (b.effects));
        busList.add (juce::var (o));
    }

    root->setProperty ("buses", busList);

    juce::Array<juce::var> patternList;

    for (auto& p : patterns)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("id", p.id);
        o->setProperty ("name", p.name);
        o->setProperty ("colour", p.colour.toString());

        if (p.meterBeats > 0 && p.meterUnit > 0)
            o->setProperty ("meter", juce::Array<juce::var> { p.meterBeats, p.meterUnit });

        auto noteMap = new juce::DynamicObject();

        for (auto& [channelId, list] : p.notes)
        {
            if (list.empty())
                continue;

            juce::Array<juce::var> noteArray;

            for (auto& n : list)
                noteArray.add (juce::Array<juce::var> { n.start, n.length, n.key, n.velocity });

            noteMap->setProperty (juce::String (channelId), noteArray);
        }

        o->setProperty ("notes", juce::var (noteMap));
        patternList.add (juce::var (o));
    }

    root->setProperty ("patterns", patternList);

    juce::Array<juce::var> clipList;

    // A split or trimmed clip also says where in its pattern it starts and how long it plays
    for (auto& c : playlist)
        clipList.add (c.offset != 0 || c.length != 0 ? juce::Array<juce::var> { c.patternId, c.track, c.start, c.offset, c.length }
                                                     : juce::Array<juce::var> { c.patternId, c.track, c.start });

    root->setProperty ("playlist", clipList);

    juce::Array<juce::var> audioList;

    for (auto& c : audioClips)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("channel", c.channelId);
        o->setProperty ("track", c.track);
        o->setProperty ("start", c.start);
        o->setProperty ("length", c.length);
        o->setProperty ("expanded", c.expanded);

        if (c.fadeIn > 0.0)   o->setProperty ("fadeIn", c.fadeIn);
        if (c.fadeOut > 0.0)  o->setProperty ("fadeOut", c.fadeOut);

        // Stretching: its tempo, following the song's, its speed and pitch
        if (c.sourceBpm > 0.0)                  o->setProperty ("bpm", c.sourceBpm);
        if (c.followTempo)                      o->setProperty ("followTempo", true);
        if (std::abs (c.speed - 1.0) > 1.0e-9)  o->setProperty ("speed", c.speed);
        if (c.pitch != 0.0f)                    o->setProperty ("pitch", c.pitch);

        juce::Array<juce::var> takeList, compList;

        for (auto& t : c.takes)
        {
            auto take = new juce::DynamicObject();
            take->setProperty ("file", t.file);
            take->setProperty ("offset", t.fileOffset);
            take->setProperty ("from", t.from);
            take->setProperty ("to", t.to);
            takeList.add (juce::var (take));
        }

        for (auto& s : c.comp)
            compList.add (juce::Array<juce::var> { s.from, s.to, s.take });

        o->setProperty ("takes", takeList);
        o->setProperty ("comp", compList);
        audioList.add (juce::var (o));
    }

    root->setProperty ("audioClips", audioList);
    root->setProperty ("loop", juce::Array<juce::var> { loopOn, loopStart, loopEnd });

    // Automation lanes, each point as [tick, value], or [tick, value, curve] when the line on from
    // it bends
    juce::Array<juce::var> laneList;

    for (auto& lane : automation)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("owner", lane.owner);
        o->setProperty ("param", lane.param);
        o->setProperty ("name", lane.name);
        juce::Array<juce::var> pointList;

        for (auto& p : lane.points)
            pointList.add (p.curve != 0.0f ? juce::Array<juce::var> { p.tick, p.value, p.curve } : juce::Array<juce::var> { p.tick, p.value });

        o->setProperty ("points", pointList);
        laneList.add (juce::var (o));
    }

    root->setProperty ("automation", laneList);
    root->setProperty ("showAutomation", showAutomation);

    // MIDI learn's links, each as [device, MIDI channel, CC, owner, parameter]
    juce::Array<juce::var> linkList;

    for (auto& l : controllerLinks)
        linkList.add (juce::Array<juce::var> { l.device, l.midiChannel, l.controller, l.owner, l.param });

    root->setProperty ("midiLinks", linkList);

    // Every track, top to bottom ("tracks" was the old list of named, muted or soloed rows)
    juce::Array<juce::var> trackList;

    for (auto& t : tracks)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", t.name);
        o->setProperty ("icon", t.icon);
        o->setProperty ("channel", t.channelId);
        o->setProperty ("mute", t.muted);
        o->setProperty ("solo", t.soloed);
        trackList.add (juce::var (o));
    }

    root->setProperty ("arrangeTracks", trackList);
    return juce::var (root);
}

void Song::fromVar (const juce::var& v)
{
    resetToDefaults();

    bpm = juce::jlimit (20.0, 400.0, (double) v.getProperty ("bpm", 130.0));

    if (auto* m = v.getProperty ("meter", {}).getArray(); m != nullptr && m->size() == 2)
        for (auto [beats, unit] : meters())
            if (beats == (int) (*m)[0] && unit == (int) (*m)[1])
                std::tie (meterBeats, meterUnit) = std::pair (beats, unit);

    if (auto* list = v.getProperty ("tempoChanges", {}).getArray())
        for (auto& t : *list)
            if (t.size() >= 2 && (int) t[0] > 0)
                tempoChanges.push_back ({ (int) t[0], juce::jlimit (20.0, 400.0, (double) t[1]), t.size() >= 3 && (bool) t[2],
                                          t.size() >= 4 ? juce::jlimit (-1.0f, 1.0f, (float) (double) t[3]) : 0.0f });

    if (auto* list = v.getProperty ("meterChanges", {}).getArray())
        for (auto& m : *list)
            if (m.size() >= 3 && (int) m[0] > 0)
                for (auto [beats, unit] : meters())
                    if (beats == (int) m[1] && unit == (int) m[2])
                        meterChanges.push_back ({ (int) m[0], beats, unit });

    tidyTimeline();
    swing = (float) v.getProperty ("swing", 0.0);
    masterVolume = (float) v.getProperty ("masterVolume", 0.8);

    // Projects from before mastering: the safe limiter they always had
    mastering = Mastering::fromVar (v.getProperty ("mastering", {}));
    masterTurns.clear();
    songPrompt = v.getProperty ("song_writer", {}).getProperty ("prompt", {}).toString();
    songReply = v.getProperty ("song_writer", {}).getProperty ("reply", {}).toString();

    if (auto* turns = v.getProperty ("master_engineer", {}).getProperty ("turns", {}).getArray())
        for (auto& t : *turns)
            masterTurns.push_back ({ t.getProperty ("prompt", {}).toString(), t.getProperty ("reply", {}).toString(),
                                     t.getProperty ("settings", {}).toString(), t.getProperty ("answer", {}).toString() });

    masterShown = juce::jlimit (-1, (int) masterTurns.size() - 1, (int) v.getProperty ("master_engineer", {}).getProperty ("shown", -1));

    mode = v.getProperty ("mode", "pattern").toString() == "song" ? PlayMode::song : PlayMode::pattern;
    currentPatternId = v.getProperty ("currentPattern", 0);
    selectedChannelId = v.getProperty ("selectedChannel", 0);

    int maxId = 0;

    if (auto* list = v.getProperty ("channels", {}).getArray())
    {
        for (auto& o : *list)
        {
            Channel c;
            c.id = o.getProperty ("id", 0);
            c.name = o.getProperty ("name", "Channel").toString();
            c.colour = juce::Colour::fromString (o.getProperty ("colour", "ff888888").toString());
            c.type = typeFromString (o.getProperty ("type", "sampler").toString());
            c.sample = o.getProperty ("sample", {}).toString();
            c.rootKey = o.getProperty ("rootKey", defaultRootKey);
            c.oneShot = o.getProperty ("oneShot", true);
            c.synthPreset = o.getProperty ("synthPreset", {}).toString();
            c.pluginId = o.getProperty ("pluginId", {}).toString();
            c.pluginState = o.getProperty ("pluginState", {}).toString();
            c.kit = o.getProperty ("kit", {}).toString();
            c.instrument = o.getProperty ("instrument", {}).toString();

            if (auto* out = o.getProperty ("midiOut", {}).getArray(); out != nullptr && out->size() >= 3)
            {
                c.midiOutDevice = (*out)[0].toString();
                c.midiOutChannel = juce::jlimit (1, 16, (int) (*out)[1]);
                c.midiProgram = juce::jlimit (-1, 127, (int) (*out)[2]);
            }

            c.drummer = drummerFromVar (o.getProperty ("drummer", {}));
            c.bass = bassFromVar (o.getProperty ("bass", {}));
            c.guitar = guitarFromVar (o.getProperty ("guitar", {}));
            c.keys = keysFromVar (o.getProperty ("keys", {}));
            c.synthPlayer = synthPlayerFromVar (o.getProperty ("synth_player", {}));
            c.input = o.getProperty ("input", {}).toString();
            c.monitor = o.getProperty ("monitor", false);
            c.volume = (float) o.getProperty ("volume", 0.78);
            c.pan = (float) o.getProperty ("pan", 0.0);
            c.muted = o.getProperty ("muted", false);
            c.effects = effectsFromVar (o.getProperty ("effects", {}), maxId);
            c.output = o.getProperty ("output", 0);

            if (auto* sendList = o.getProperty ("sends", {}).getArray())
                for (auto& s : *sendList)
                    if (s.size() >= 3)
                        c.sends.push_back ({ (int) s[0], (float) s[1], (bool) s[2] });

            maxId = std::max (maxId, c.id);
            channels.push_back (c);
        }
    }

    masterEffects = effectsFromVar (v.getProperty ("masterEffects", {}), maxId);

    if (auto* list = v.getProperty ("buses", {}).getArray())
    {
        for (auto& o : *list)
        {
            Bus b;
            b.id = o.getProperty ("id", 0);
            b.name = o.getProperty ("name", "Bus").toString();
            b.colour = juce::Colour::fromString (o.getProperty ("colour", "ff888888").toString());
            b.volume = (float) o.getProperty ("volume", 0.78);
            b.pan = (float) o.getProperty ("pan", 0.0);
            b.muted = o.getProperty ("muted", false);
            b.effects = effectsFromVar (o.getProperty ("effects", {}), maxId);
            maxId = std::max (maxId, b.id);
            buses.push_back (b);
        }
    }

    // Outputs and sends only to buses that exist
    for (auto& c : channels)
    {
        if (c.output != 0 && findBus (c.output) == nullptr)
            c.output = 0;

        std::erase_if (c.sends, [this] (auto& s) { return findBus (s.busId) == nullptr; });
    }

    if (auto* list = v.getProperty ("patterns", {}).getArray())
    {
        for (auto& o : *list)
        {
            Pattern p;
            p.id = o.getProperty ("id", 0);
            p.name = o.getProperty ("name", "Pattern").toString();
            p.colour = juce::Colour::fromString (o.getProperty ("colour", "ff5b7cfa").toString());

            if (auto* m = o.getProperty ("meter", {}).getArray(); m != nullptr && m->size() == 2)
                for (auto [beats, unit] : meters())
                    if (beats == (int) (*m)[0] && unit == (int) (*m)[1])
                        std::tie (p.meterBeats, p.meterUnit) = std::pair (beats, unit);

            if (auto* noteMap = o.getProperty ("notes", {}).getDynamicObject())
            {
                for (auto& prop : noteMap->getProperties())
                {
                    auto& target = p.notes[prop.name.toString().getIntValue()];

                    if (auto* noteArray = prop.value.getArray())
                        for (auto& n : *noteArray)
                            if (n.size() >= 4)
                                target.push_back ({ (int) n[0], juce::jmax (1, (int) n[1]),
                                                    juce::jlimit (0, 127, (int) n[2]), juce::jlimit (1, 127, (int) n[3]) });
                }
            }

            maxId = std::max (maxId, p.id);
            patterns.push_back (p);
        }
    }

    if (auto* list = v.getProperty ("playlist", {}).getArray())
        for (auto& c : *list)
            if (c.size() >= 3)
                playlist.push_back ({ (int) c[0], (int) c[1], (int) c[2],
                                      c.size() >= 5 ? juce::jmax (0, (int) c[3]) : 0, c.size() >= 5 ? juce::jmax (0, (int) c[4]) : 0 });

    if (auto* list = v.getProperty ("audioClips", {}).getArray())
    {
        for (auto& o : *list)
        {
            AudioClip c;
            c.channelId = o.getProperty ("channel", 0);
            c.track = juce::jmax (0, (int) o.getProperty ("track", 0));
            c.start = juce::jmax (0, (int) o.getProperty ("start", 0));
            c.length = o.getProperty ("length", 0.0);
            c.expanded = o.getProperty ("expanded", false);
            c.fadeIn = juce::jmax (0.0, (double) o.getProperty ("fadeIn", 0.0));
            c.fadeOut = juce::jmax (0.0, (double) o.getProperty ("fadeOut", 0.0));
            c.sourceBpm = juce::jlimit (0.0, 999.0, (double) o.getProperty ("bpm", 0.0));
            c.followTempo = o.getProperty ("followTempo", false);
            c.speed = juce::jlimit (AudioClip::slowest, AudioClip::fastest, (double) o.getProperty ("speed", 1.0));
            c.pitch = juce::jlimit (-24.0f, 24.0f, (float) o.getProperty ("pitch", 0.0f));

            if (auto* takeList = o.getProperty ("takes", {}).getArray())
            {
                for (auto& t : *takeList)
                    c.takes.push_back ({ t.getProperty ("file", {}).toString(), t.getProperty ("offset", 0.0),
                                         t.getProperty ("from", 0.0), t.getProperty ("to", 0.0) });

                if (auto* compList = o.getProperty ("comp", {}).getArray())
                    for (auto& s : *compList)
                        if (s.size() >= 3 && juce::isPositiveAndBelow ((int) s[2], (int) c.takes.size()))
                            c.comp.push_back ({ (double) s[0], (double) s[1], (int) s[2] });
            }
            else if (auto file = o.getProperty ("file", {}).toString(); file.isNotEmpty())
            {
                // Before take folders a clip was one file
                c.takes.push_back ({ file, o.getProperty ("offset", 0.0), 0.0, c.length });
                c.comp.push_back ({ 0.0, c.length, 0 });
            }

            if (c.length > 0.0 && ! c.takes.empty() && findChannel (c.channelId) != nullptr)
                audioClips.push_back (c);
        }
    }

    if (auto* loop = v.getProperty ("loop", {}).getArray(); loop != nullptr && loop->size() >= 3)
    {
        loopOn = (bool) (*loop)[0];
        loopStart = juce::jmax (0, (int) (*loop)[1]);
        loopEnd = juce::jmax (loopStart, (int) (*loop)[2]);
    }

    // Automation lanes of channels and buses that are here (and the master's), points in time order
    if (auto* list = v.getProperty ("automation", {}).getArray())
        for (auto& o : *list)
        {
            AutomationLane lane;
            lane.owner = o.getProperty ("owner", 0);
            lane.param = o.getProperty ("param", {}).toString();
            lane.name = o.getProperty ("name", {}).toString();

            if (auto* points = o.getProperty ("points", {}).getArray())
                for (auto& p : *points)
                    if (p.size() >= 2)
                        lane.points.push_back ({ juce::jmax (0, (int) p[0]), (float) (double) p[1],
                                                 p.size() >= 3 ? juce::jlimit (-automationcurve::most, automationcurve::most, (float) (double) p[2]) : 0.0f });

            std::stable_sort (lane.points.begin(), lane.points.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });

            if (lane.param.isNotEmpty() && canOwnAutomation (lane.owner) && findLane (lane.owner, lane.param) == nullptr)
                automation.push_back (lane);
        }

    showAutomation = v.getProperty ("showAutomation", false);

    if (auto* list = v.getProperty ("midiLinks", {}).getArray())
        for (auto& l : *list)
            if (l.size() >= 5)
            {
                ControllerLink link { l[0].toString(), juce::jlimit (1, 16, (int) l[1]), juce::jlimit (0, 127, (int) l[2]), (int) l[3], l[4].toString() };

                if (link.param.isNotEmpty() && canOwnAutomation (link.owner))
                    controllerLinks.push_back (link);
            }

    if (auto* list = v.getProperty ("arrangeTracks", {}).getArray())
    {
        for (auto& o : *list)
        {
            Track t;
            t.name = o.getProperty ("name", {}).toString();
            t.icon = o.getProperty ("icon", {}).toString();
            t.channelId = o.getProperty ("channel", 0);
            t.muted = o.getProperty ("mute", false);
            t.soloed = o.getProperty ("solo", false);

            if (findChannel (t.channelId) == nullptr)
                t.channelId = 0;

            tracks.push_back (t);
        }

        coverClipsWithTracks();
    }
    else
    {
        // Before tracks were added one by one: a fixed number of rows (12 at first), the
        // named, muted or soloed ones listed. The rows nobody used are dropped
        tracks.resize ((size_t) juce::jlimit (0, 1000, (int) v.getProperty ("playlistTracks", 12)));
        coverClipsWithTracks();

        if (auto* named = v.getProperty ("tracks", {}).getArray())
        {
            for (auto& o : *named)
            {
                const int row = o.getProperty ("row", -1);

                if (! juce::isPositiveAndBelow (row, numTracks()))
                    continue;

                auto& t = tracks[(size_t) row];
                t.name = o.getProperty ("name", {}).toString();
                t.muted = o.getProperty ("mute", false);
                t.soloed = o.getProperty ("solo", false);
            }
        }

        for (int row = numTracks(); --row >= 0;)
        {
            auto& t = tracks[(size_t) row];

            if (t.name.isEmpty() && ! t.muted && ! t.soloed && isTrackEmpty (row))
                removeTrack (row);
        }
    }

    nextId = maxId + 1;
}

bool Song::save (const juce::File& f) const
{
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (juce::JSON::toString (toVar(), false));
}

bool Song::load (const juce::File& f)
{
    auto v = juce::JSON::parse (f.loadFileAsString());

    if (v.getProperty ("format", {}).toString() != "nightshift-project")
        return false;

    fromVar (v);
    undoStack.clear();
    redoStack.clear();
    notify (Change::everything);
    dirty = false;     // after notify, which marks every change as unsaved
    return true;
}

} // namespace nightshift
