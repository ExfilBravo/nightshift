// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Keys.h"

namespace nightshift::keys
{

namespace
{
    float wobble (juce::Random& r)
    {
        return r.nextFloat() + r.nextFloat() + r.nextFloat() - 1.5f;
    }
}

juce::String noteName (int pitch)
{
    static const char* const names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return juce::String (names[((pitch % 12) + 12) % 12]) + juce::String (pitch / 12 - 1);
}

//==============================================================================
juce::var Part::toJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("title", title);
    o->setProperty ("message", message);
    o->setProperty ("key", key);
    o->setProperty ("sound", sound);
    o->setProperty ("sampled", sampled);
    o->setProperty ("level_db", levelDb);
    o->setProperty ("tempo", tempo);
    o->setProperty ("meter", meterBeats > 0 ? juce::String (meterBeats) + "/" + juce::String (meterUnit) : juce::String());

    auto* feel = new juce::DynamicObject();
    feel->setProperty ("swing", swing);
    feel->setProperty ("swing_note", swingSixteenths ? "16th" : "8th");
    feel->setProperty ("looseness_ms", looseMs);
    feel->setProperty ("push_ms", pushMs);
    feel->setProperty ("roll_ms", rollMs);
    o->setProperty ("feel", juce::var (feel));

    juce::Array<juce::var> list;

    for (auto& s : sections)
    {
        juce::Array<juce::var> bars;

        for (auto& b : s.bars)
        {
            juce::Array<juce::var> notes;

            for (auto& n : b.notes)
                notes.add (juce::Array<juce::var> { n.step, n.length, n.pitch, n.velocity, juce::String (n.left ? "l" : "r") + (n.grace ? "g" : "") });

            auto* bar = new juce::DynamicObject();
            bar->setProperty ("steps", b.steps);
            bar->setProperty ("chord", b.chord);
            bar->setProperty ("notes", notes);
            bars.add (juce::var (bar));
        }

        auto* section = new juce::DynamicObject();
        section->setProperty ("name", s.name);
        section->setProperty ("bars", bars);
        list.add (juce::var (section));
    }

    o->setProperty ("sections", list);

    juce::Array<juce::var> order;

    for (auto& a : arrangement)
        order.add (a);

    o->setProperty ("arrangement", order);
    return juce::var (o);
}

std::optional<Part> Part::fromJson (const juce::var& v)
{
    auto* list = v.getProperty ("sections", {}).getArray();

    if (list == nullptr)
        return {};

    Part p;
    p.title = v.getProperty ("title", {}).toString();
    p.message = v.getProperty ("message", {}).toString();
    p.key = v.getProperty ("key", {}).toString();
    p.sound = v.getProperty ("sound", {}).toString();
    p.sampled = v.getProperty ("sampled", {}).toString();
    p.levelDb = juce::jlimit (-24.0f, 6.0f, (float) v.getProperty ("level_db", 0.0));
    p.tempo = v.getProperty ("tempo", 0);

    if (const auto meter = v.getProperty ("meter", {}).toString(); meter.containsChar ('/'))
    {
        p.meterBeats = meter.upToFirstOccurrenceOf ("/", false, false).getIntValue();
        p.meterUnit = meter.fromFirstOccurrenceOf ("/", false, false).getIntValue();
    }

    const auto feel = v.getProperty ("feel", {});
    p.swing = juce::jlimit (0.0f, 1.0f, (float) feel.getProperty ("swing", 0.0));
    p.swingSixteenths = feel.getProperty ("swing_note", "8th").toString() == "16th";
    p.looseMs = juce::jlimit (0.0f, 40.0f, (float) feel.getProperty ("looseness_ms", 0.0));
    p.pushMs = juce::jlimit (-40.0f, 40.0f, (float) feel.getProperty ("push_ms", 0.0));
    p.rollMs = juce::jlimit (0.0f, 60.0f, (float) feel.getProperty ("roll_ms", 0.0));

    for (auto& sv : *list)
    {
        Section s;
        s.name = sv.getProperty ("name", {}).toString();

        if (auto* bars = sv.getProperty ("bars", {}).getArray())
            for (auto& bv : *bars)
            {
                Bar b;
                b.steps = juce::jlimit (1, 96, (int) bv.getProperty ("steps", 16));
                b.chord = bv.getProperty ("chord", {}).toString();

                if (auto* notes = bv.getProperty ("notes", {}).getArray())
                    for (auto& nv : *notes)
                        if (auto* f = nv.getArray(); f != nullptr && f->size() >= 4)
                        {
                            Note n;
                            n.step = juce::jlimit (0, b.steps - 1, (int) (*f)[0]);
                            n.length = juce::jlimit (1, b.steps * 2, (int) (*f)[1]);
                            n.pitch = juce::jlimit (0, 127, (int) (*f)[2]);
                            n.velocity = juce::jlimit (1, 127, (int) (*f)[3]);
                            const auto how = f->size() > 4 ? (*f)[4].toString() : juce::String();
                            n.left = how.containsChar ('l');
                            n.grace = how.containsChar ('g');
                            b.notes.push_back (n);
                        }

                s.bars.push_back (std::move (b));
            }

        if (s.name.isNotEmpty() && ! s.bars.empty())
            p.sections.push_back (std::move (s));
    }

    if (auto* order = v.getProperty ("arrangement", {}).getArray())
        for (auto& a : *order)
            p.arrangement.add (a.toString());

    if (p.sections.empty())
        return {};

    return p;
}

//==============================================================================
void render (Song& song, int channelId, const Part& part)
{
    if (part.meterBeats > 0 && (part.meterBeats != song.meterBeats || part.meterUnit != song.meterUnit))
    {
        song.meterBeats = part.meterBeats;
        song.meterUnit = part.meterUnit;
        song.notify (Song::Change::transport);
    }

    if (part.tempo >= 20 && part.tempo <= 400 && part.tempo != juce::roundToInt (song.bpm))
    {
        song.bpm = part.tempo;
        song.notify (Song::Change::transport);
    }

    auto c = song.findChannel (channelId);

    if (c == nullptr)
        return;

    auto& k = c->keys;
    const int barTicks = song.ticksPerBar();
    const double ticksPerMs = song.bpm / 60.0 * ticksPerBeat / 1000.0;
    std::set<int> used;

    auto swung = [&] (double t)
    {
        const double beats = t / ticksPerBeat;
        const double frac = beats - std::floor (beats);

        if (! part.swingSixteenths && std::abs (frac - 0.5) < 1.0e-6)
            return t + part.swing * ticksPerBeat / 6.0;

        if (part.swingSixteenths && (std::abs (frac - 0.25) < 1.0e-6 || std::abs (frac - 0.75) < 1.0e-6))
            return t + part.swing * ticksPerBeat / 12.0;

        return t;
    };

    for (size_t s = 0; s < part.sections.size(); ++s)
    {
        const auto& section = part.sections[s];
        Pattern* pattern = nullptr;

        if (auto it = k.patterns.find (section.name); it != k.patterns.end())
            pattern = song.findPattern (it->second);

        if (pattern == nullptr || used.count (pattern->id) > 0)
        {
            auto& made = song.addPattern (c->name + " " + section.name);
            made.colour = c->colour.withRotatedHue ((float) s * 0.07f);
            pattern = &made;
            k.patterns[section.name] = made.id;
        }

        used.insert (pattern->id);
        std::vector<nightshift::Note> placed;

        for (size_t bi = 0; bi < section.bars.size(); ++bi)
        {
            const auto& bar = section.bars[bi];
            const double stepTicks = (double) barTicks / bar.steps;

            for (size_t ni = 0; ni < bar.notes.size(); ++ni)
            {
                const auto& n = bar.notes[ni];

                // Rolled: a chord's notes in the same hand, low to high, a little apart
                int order = 0;

                for (auto& other : bar.notes)
                    order += other.step == n.step && other.left == n.left && other.pitch < n.pitch && ! other.grace ? 1 : 0;

                juce::Random random ((juce::int64) (s * 1000003 + bi * 10007 + ni * 101) ^ 0x6b6579);
                const double feel = (part.pushMs + wobble (random) * part.looseMs * 0.6f) * ticksPerMs;
                const double roll = n.grace ? 0.0 : order * part.rollMs * ticksPerMs;
                const double at = (double) bi * barTicks;
                const double start = swung (at + n.step * stepTicks) + feel + roll;
                const double end = swung (at + (n.step + n.length) * stepTicks) + feel;
                const int velocity = juce::jlimit (1, 127, n.velocity + juce::roundToInt (wobble (random) * 3.0f));

                placed.push_back ({ std::max (0, juce::roundToInt (start)), std::max (3, juce::roundToInt (end - start)), n.pitch, velocity });
            }
        }

        std::stable_sort (placed.begin(), placed.end(), [] (const nightshift::Note& a, const nightshift::Note& b) { return a.start < b.start; });

        // The same key played again lets go just before
        for (size_t i = 0; i < placed.size(); ++i)
            for (size_t j = i + 1; j < placed.size(); ++j)
                if (placed[j].key == placed[i].key)
                {
                    if (placed[i].end() > placed[j].start - 2)
                        placed[i].length = std::max (3, placed[j].start - 2 - placed[i].start);

                    break;
                }

        auto& notes = pattern->notesFor (channelId);
        notes = placed;
    }

    song.notify (Song::Change::patterns);
    song.notify (Song::Change::notes);
}

void placeInArrange (Song& song, int channelId, const Part& part)
{
    auto c = song.findChannel (channelId);

    if (c == nullptr)
        return;

    auto has = [&part] (const juce::String& name)
    {
        return std::any_of (part.sections.begin(), part.sections.end(), [&] (const Section& s) { return s.name == name; });
    };

    std::vector<std::pair<int, juce::String>> starts;

    for (auto& ch : song.channels)
        if (ch.id != channelId)
            for (const auto* patterns : { &ch.drummer.patterns, &ch.bass.patterns, &ch.guitar.patterns })
                for (auto& [name, patternId] : *patterns)
                    if (has (name))
                        for (auto& clip : song.playlist)
                            if (clip.patternId == patternId)
                                starts.emplace_back (clip.start, name);

    std::sort (starts.begin(), starts.end());
    starts.erase (std::unique (starts.begin(), starts.end(), [] (auto& x, auto& y) { return x.first == y.first; }), starts.end());

    if (starts.empty())
    {
        juce::StringArray order = part.arrangement;

        if (order.isEmpty())
            for (auto& s : part.sections)
                order.add (s.name);

        int at = 0;

        for (auto& name : order)
            if (auto it = c->keys.patterns.find (name); it != c->keys.patterns.end())
                if (auto p = song.findPattern (it->second))
                {
                    starts.emplace_back (at, name);
                    at += song.patternLength (*p);
                }
    }

    if (starts.empty())
        return;

    int end = 0;

    for (auto& [at, name] : starts)
        if (auto it = c->keys.patterns.find (name); it != c->keys.patterns.end())
            if (auto p = song.findPattern (it->second))
                end = std::max (end, at + song.patternLength (*p));

    const int row = song.trackForChannel (c->id, starts.front().first, end);

    for (auto& [at, name] : starts)
        if (auto it = c->keys.patterns.find (name); it != c->keys.patterns.end())
            if (song.findPattern (it->second) != nullptr)
                song.playlist.push_back ({ it->second, row, at });

    song.notify (Song::Change::playlist);
}

std::optional<Part> shownPart (const Channel& c)
{
    if (! juce::isPositiveAndBelow (c.keys.shown, (int) c.keys.turns.size()))
        return {};

    return Part::fromJson (juce::JSON::parse (c.keys.turns[(size_t) c.keys.shown].part));
}

bool rerender (Song& song, int channelId)
{
    auto c = song.findChannel (channelId);

    if (c == nullptr)
        return false;

    auto part = shownPart (*c);

    if (! part.has_value())
        return false;

    render (song, channelId, *part);
    return true;
}

} // namespace nightshift::keys
