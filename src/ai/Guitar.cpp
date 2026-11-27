// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Guitar.h"
#include "Drummer.h"

namespace nightshift::guitar
{

namespace
{
    const char* const names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    constexpr int maxFret = 22;

    float wobble (juce::Random& r)
    {
        return r.nextFloat() + r.nextFloat() + r.nextFloat() - 1.5f;
    }
}

//==============================================================================
juce::String tuningNotes (const std::vector<int>& tuning)
{
    juce::StringArray n;

    for (auto p : tuning)
        n.add (names[((p % 12) + 12) % 12]);

    return n.joinIntoString (" ");
}

juce::String tuningName (const std::vector<int>& t)
{
    if (t.empty())
        return {};

    std::vector<int> steps;

    for (auto p : t)
        steps.push_back (p - t.front());

    const auto low = juce::String (names[((t.front() % 12) + 12) % 12]);

    if (steps == std::vector<int> { 0, 5, 10, 15, 19, 24 })
        return t.front() == 40 ? juce::String ("Standard") : low + " standard";

    if (steps == std::vector<int> { 0, 7, 12, 17, 21, 26 })
        return "Drop " + low;

    if (steps == std::vector<int> { 0, 5, 10, 15, 20, 24, 29 })
        return "7-string " + low + " standard";

    if (steps == std::vector<int> { 0, 7, 12, 17, 22, 26, 31 })
        return "7-string drop " + low;

    return "Tuned " + tuningNotes (t);
}

//==============================================================================
juce::var Part::toJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("title", title);
    o->setProperty ("message", message);
    o->setProperty ("key", key);
    o->setProperty ("sound", sound);
    o->setProperty ("tempo", tempo);
    o->setProperty ("meter", meterBeats > 0 ? juce::String (meterBeats) + "/" + juce::String (meterUnit) : juce::String());

    juce::Array<juce::var> strings;

    for (auto p : tuning)
        strings.add (p);

    o->setProperty ("tuning", strings);

    auto* feel = new juce::DynamicObject();
    feel->setProperty ("swing", swing);
    feel->setProperty ("swing_note", swingSixteenths ? "16th" : "8th");
    feel->setProperty ("looseness_ms", looseMs);
    feel->setProperty ("push_ms", pushMs);
    feel->setProperty ("strum_ms", strumMs);
    o->setProperty ("level_db", levelDb);
    o->setProperty ("feel", juce::var (feel));

    juce::Array<juce::var> list;

    for (auto& s : sections)
    {
        juce::Array<juce::var> bars;

        for (auto& b : s.bars)
        {
            juce::Array<juce::var> notes;

            for (auto& n : b.notes)
            {
                juce::String how;
                how << (n.palmMute ? "p" : "") << (n.muted ? "x" : "") << (n.slide ? "s" : "") << (n.hammer ? "h" : "") << (n.up ? "u" : "");
                notes.add (juce::Array<juce::var> { n.step, n.length, n.pitch, n.velocity, n.string, n.fret, how });
            }

            auto* bar = new juce::DynamicObject();
            bar->setProperty ("steps", b.steps);
            bar->setProperty ("chord", b.chord);
            bar->setProperty ("notes", notes);
            bars.add (juce::var (bar));
        }

        auto* section = new juce::DynamicObject();
        section->setProperty ("name", s.name);

        if (s.meterBeats > 0 && s.meterUnit > 0)
            section->setProperty ("meter", drummer::meterText (s.meterBeats, s.meterUnit));

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
    p.tempo = v.getProperty ("tempo", 0);

    if (const auto meter = v.getProperty ("meter", {}).toString(); meter.containsChar ('/'))
    {
        p.meterBeats = meter.upToFirstOccurrenceOf ("/", false, false).getIntValue();
        p.meterUnit = meter.fromFirstOccurrenceOf ("/", false, false).getIntValue();
    }

    if (auto* strings = v.getProperty ("tuning", {}).getArray())
        for (auto& s : *strings)
            p.tuning.push_back (juce::jlimit (20, 70, (int) s));

    if (p.tuning.empty())
        p.tuning = { 40, 45, 50, 55, 59, 64 };

    const auto feel = v.getProperty ("feel", {});
    p.swing = juce::jlimit (0.0f, 1.0f, (float) feel.getProperty ("swing", 0.0));
    p.swingSixteenths = feel.getProperty ("swing_note", "8th").toString() == "16th";
    p.looseMs = juce::jlimit (0.0f, 40.0f, (float) feel.getProperty ("looseness_ms", 0.0));
    p.pushMs = juce::jlimit (-40.0f, 40.0f, (float) feel.getProperty ("push_ms", 0.0));
    p.strumMs = juce::jlimit (0.0f, 40.0f, (float) feel.getProperty ("strum_ms", 6.0));
    p.levelDb = juce::jlimit (-24.0f, 6.0f, (float) v.getProperty ("level_db", 0.0));

    for (auto& sv : *list)
    {
        Section s;
        s.name = sv.getProperty ("name", {}).toString();
        std::tie (s.meterBeats, s.meterUnit) = drummer::meterFromText (sv.getProperty ("meter", {}).toString());

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
                            n.string = f->size() > 4 ? (int) (*f)[4] : -1;
                            n.fret = f->size() > 5 ? (int) (*f)[5] : -1;
                            const auto how = f->size() > 6 ? (*f)[6].toString() : juce::String();
                            n.palmMute = how.containsChar ('p');
                            n.muted = how.containsChar ('x');
                            n.slide = how.containsChar ('s');
                            n.hammer = how.containsChar ('h');
                            n.up = how.containsChar ('u');
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
void finger (std::vector<Note>& notes, const std::vector<int>& tuning, int& hand)
{
    const int strings = (int) tuning.size();

    if (strings == 0)
        return;

    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.step != b.step ? a.step < b.step : a.pitch < b.pitch; });

    // Into the guitar's range, an octave at a time; one of each pitch a step
    for (auto& n : notes)
    {
        while (n.pitch < tuning.front())                 n.pitch += 12;
        while (n.pitch > tuning.back() + maxFret)        n.pitch -= 12;
    }

    notes.erase (std::unique (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.step == b.step && a.pitch == b.pitch; }), notes.end());

    int lastString = -1;

    for (size_t i = 0; i < notes.size();)
    {
        size_t j = i;

        while (j < notes.size() && notes[j].step == notes[i].step)
            ++j;

        const int count = (int) (j - i);

        // A chord too big for the strings loses its middle notes
        while (count > strings && j - i > (size_t) strings)
        {
            notes.erase (notes.begin() + (std::ptrdiff_t) (i + (j - i) / 2));
            --j;
        }

        const int n = (int) (j - i);
        std::vector<int> best ((size_t) n, -1), current ((size_t) n, -1);
        float bestCost = 1.0e9f;

        // Strings in order with the pitches (low notes on low strings), the hand where it was,
        // the frets of a chord within reach; a slide or hammer-on stays on the string before
        std::function<void (int, int, float)> search = [&] (int k, int from, float cost)
        {
            if (cost >= bestCost)
                return;

            if (k == n)
            {
                int lo = 99, hi = -1;

                for (int a = 0; a < n; ++a)
                    if (const int f = notes[i + (size_t) a].pitch - tuning[(size_t) current[(size_t) a]]; f > 0)
                    {
                        lo = std::min (lo, f);
                        hi = std::max (hi, f);
                    }

                float span = hi >= 0 ? (float) std::max (0, hi - lo - 3) * 6.0f : 0.0f;

                // Strings skipped inside a chord: a jazz shell does, a power chord never does
                bool powerChord = n > 1;

                for (int a = 0; a < n; ++a)
                {
                    const int above = notes[i + (size_t) a].pitch - notes[i].pitch;
                    powerChord = powerChord && (above == 0 || above == 7 || above == 12 || above == 19 || above == 24);
                }

                for (int a = 1; a < n; ++a)
                    span += (float) std::max (0, current[(size_t) a] - current[(size_t) a - 1] - 1) * (powerChord ? 8.0f : 1.5f);

                if (cost + span < bestCost)
                {
                    bestCost = cost + span;
                    best = current;
                }

                return;
            }

            const auto& note = notes[i + (size_t) k];

            for (int s = from; s < strings - (n - 1 - k); ++s)
            {
                const int fret = note.pitch - tuning[(size_t) s];

                if (fret < 0 || fret > maxFret)
                    continue;

                if (note.string >= 0 && note.string < strings && s != note.string && note.pitch - tuning[(size_t) note.string] >= 0)
                    continue;

                float c = fret == 0 ? 0.6f : std::abs ((float) fret - (float) hand) * 0.5f;
                c += (float) fret * 0.05f;                                         // lower on the neck, all else equal

                if (n == 1 && (note.slide || note.hammer) && lastString >= 0)
                    c += s == lastString ? -3.0f : 6.0f;

                current[(size_t) k] = s;
                search (k + 1, s + 1, cost + c);
            }
        };

        search (0, 0, 0.0f);

        int sum = 0, fretted = 0;

        for (int a = 0; a < n; ++a)
        {
            auto& note = notes[i + (size_t) a];

            if (best[(size_t) a] < 0)
            {
                // Nowhere to put it in this chord: the nearest string that plays it at all
                for (int s = strings - 1; s >= 0; --s)
                    if (const int f = note.pitch - tuning[(size_t) s]; f >= 0 && f <= maxFret)
                        best[(size_t) a] = s;
            }

            note.string = best[(size_t) a];
            note.fret = note.string >= 0 ? note.pitch - tuning[(size_t) note.string] : -1;

            if (note.fret > 0)
            {
                sum += note.fret;
                ++fretted;
            }
        }

        if (fretted > 0)
            hand = sum / fretted;

        if (n == 1)
            lastString = notes[i].string;

        i += (size_t) n;
    }
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

    auto& g = c->guitar;
    const double ticksPerMs = song.bpm / 60.0 * ticksPerBeat / 1000.0;
    std::set<int> used;

    for (size_t s = 0; s < part.sections.size(); ++s)
    {
        const auto& section = part.sections[s];
        Pattern* pattern = nullptr;

        if (auto it = g.patterns.find (section.name); it != g.patterns.end())
            pattern = song.findPattern (it->second);

        if (pattern == nullptr || used.count (pattern->id) > 0)
        {
            auto& made = song.addPattern (c->name + " " + section.name);
            made.colour = c->colour.withRotatedHue ((float) s * 0.07f);
            pattern = &made;
            g.patterns[section.name] = made.id;
        }

        used.insert (pattern->id);

        // In its section's time signature (the drummer's): its pattern's own where that isn't the song's at the start
        const auto [beats, unit] = section.meterBeats > 0 ? std::pair (section.meterBeats, section.meterUnit)
                                 : part.meterBeats > 0 ? std::pair (part.meterBeats, part.meterUnit) : std::pair (song.meterBeats, song.meterUnit);
        const int barTicks = beats * ticksPerBeat * 4 / unit;
        const bool startsMeter = beats == song.meterBeats && unit == song.meterUnit;
        pattern->meterBeats = startsMeter ? 0 : beats;
        pattern->meterUnit = startsMeter ? 0 : unit;

        struct Placed { int start, end, pitch, velocity, string; bool palmMute, muted, legato; };
        std::vector<Placed> placed;

        // Swing: the bar's off-beat 8ths (or 16ths) late, as the drums swing them
        auto swung = [&] (double t, double barStart) { return drummer::swung (t, barStart, part.swing, part.swingSixteenths); };

        for (size_t bi = 0; bi < section.bars.size(); ++bi)
        {
            const auto& bar = section.bars[bi];
            const double stepTicks = (double) barTicks / bar.steps;

            for (size_t ni = 0; ni < bar.notes.size(); ++ni)
            {
                const auto& n = bar.notes[ni];

                // Strummed: each string a little after the one before, from the low string down
                // (or the high one up)
                int order = 0, inChord = 0;

                for (auto& other : bar.notes)
                    if (other.step == n.step)
                    {
                        ++inChord;
                        order += (n.up ? other.string > n.string : other.string < n.string) ? 1 : 0;
                    }

                const double strum = inChord > 1 ? order * part.strumMs * ticksPerMs : 0.0;
                juce::Random random ((juce::int64) (s * 1000003 + bi * 10007 + ni * 101) ^ 0x6017a5);
                const double feel = (part.pushMs + wobble (random) * part.looseMs * 0.6f) * ticksPerMs;
                const double at = (double) bi * barTicks;
                const double start = swung (at + n.step * stepTicks, at) + feel + strum;
                const double end = swung (at + (n.step + n.length) * stepTicks, at) + feel;
                const int velocity = juce::jlimit (1, 127, n.velocity + juce::roundToInt (wobble (random) * 3.0f) - (inChord > 1 ? order * 2 : 0));

                placed.push_back ({ std::max (0, juce::roundToInt (start)), std::max (1, juce::roundToInt (end)), n.pitch, velocity, n.string,
                                    n.palmMute, n.muted, n.slide || n.hammer });
            }
        }

        std::stable_sort (placed.begin(), placed.end(), [] (auto& a, auto& b) { return a.start < b.start; });

        // A string plays one note at a time: each ends before the next on its string, or runs on
        // under it when the next is a slide or a hammer-on
        for (size_t i = 0; i < placed.size(); ++i)
            for (size_t k = i + 1; k < placed.size(); ++k)
                if (placed[k].string == placed[i].string && placed[k].string >= 0)
                {
                    if (placed[k].legato)
                        placed[i].end = std::max (placed[i].end, placed[k].start + std::max (6, ticksPerStep / 3));
                    else
                        placed[i].end = std::min (placed[i].end, placed[k].start - 1);

                    placed[i].end = std::max (placed[i].end, placed[i].start + 3);
                    break;
                }

        auto& notes = pattern->notesFor (channelId);
        notes.clear();

        // The keyswitches, held from just before the notes they change until they've started
        auto spans = [&] (auto which, int key)
        {
            int from = -1, to = -1;

            for (auto& n : placed)
            {
                if (! which (n))
                    continue;

                // One span over a run: the next of them within a beat, nothing else starting between
                const bool between = std::any_of (placed.begin(), placed.end(), [&] (const Placed& o)
                {
                    return ! which (o) && o.start > to && o.start < n.start - 4;
                });

                if (from >= 0 && n.start - 4 <= to + ticksPerBeat && ! between)
                {
                    to = std::max (to, n.start + 4);
                    continue;
                }

                if (from >= 0)
                    notes.push_back ({ from, to - from, key, 100 });

                from = std::max (0, n.start - 4);
                to = n.start + 4;
            }

            if (from >= 0)
                notes.push_back ({ from, to - from, key, 100 });
        };

        spans ([] (const Placed& n) { return n.palmMute && ! n.muted; }, palmMuteKey);
        spans ([] (const Placed& n) { return n.muted; }, scratchKey);

        for (auto& n : placed)
            notes.push_back ({ n.start, std::max (3, n.end - n.start), n.pitch, n.velocity });

        std::stable_sort (notes.begin(), notes.end(), [] (const nightshift::Note& a, const nightshift::Note& b) { return a.start < b.start; });
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

    // Where the drummer's (or the bassist's) sections of the same names are in Arrange
    std::vector<std::pair<int, juce::String>> starts;

    for (auto& ch : song.channels)
        if (ch.id != channelId)
            for (const auto* patterns : { &ch.drummer.patterns, &ch.bass.patterns })
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
            if (auto it = c->guitar.patterns.find (name); it != c->guitar.patterns.end())
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
        if (auto it = c->guitar.patterns.find (name); it != c->guitar.patterns.end())
            if (auto p = song.findPattern (it->second))
                end = std::max (end, at + song.patternLength (*p));

    const int row = song.trackForChannel (c->id, starts.front().first, end);
    const auto meters = song.meterChanges;
    const auto startMeter = std::pair (song.meterBeats, song.meterUnit);

    for (auto& [at, name] : starts)
        if (auto it = c->guitar.patterns.find (name); it != c->guitar.patterns.end())
            if (song.findPattern (it->second) != nullptr)
            {
                song.playlist.push_back ({ it->second, row, at });
                song.fitMeterToClip (it->second, at);       // a section in a time signature of its own: the bars there in it
            }

    if (song.meterChanges != meters || std::pair (song.meterBeats, song.meterUnit) != startMeter)
        song.notify (Song::Change::transport);

    song.notify (Song::Change::playlist);
}

std::optional<Part> shownPart (const Channel& c)
{
    if (! juce::isPositiveAndBelow (c.guitar.shown, (int) c.guitar.turns.size()))
        return {};

    return Part::fromJson (juce::JSON::parse (c.guitar.turns[(size_t) c.guitar.shown].part));
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

} // namespace nightshift::guitar
