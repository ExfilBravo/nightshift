// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Drummer.h"
#include "LearnedFeel.h"
#include "audio/Kits.h"

namespace nightshift::drummer
{

namespace
{
    /** Roughly normal, -1.5..1.5, from a seeded generator: the same groove always plays the same. */
    float wobble (juce::Random& r)
    {
        return r.nextFloat() + r.nextFloat() + r.nextFloat() - 1.5f;
    }
}

juce::StringArray kitNamesFor (const Channel& channel)
{
    juce::StringArray names;

    if (channel.type == InstrumentType::kit)
        for (auto& id : drumkits::ids())
            names.add (drumkits::displayName (id));

    return names;
}

//==============================================================================
std::optional<Groove> Groove::fromJson (const juce::var& v)
{
    if (! v.isObject())
        return {};

    Groove g;
    g.title = v.getProperty ("title", "Groove").toString().trim();
    g.message = v.getProperty ("message", {}).toString().trim();
    g.kit = v.getProperty ("kit", {}).toString().trim();
    g.tempo = (int) v.getProperty ("tempo", 0);

    const auto meter = v.getProperty ("meter", {}).toString();

    for (auto [beats, unit] : Song::meters())
        if (meter == juce::String (beats) + "/" + juce::String (unit))
            std::tie (g.meterBeats, g.meterUnit) = std::pair (beats, unit);

    const auto feel = v.getProperty ("feel", {});
    g.swing = juce::jlimit (0.0f, 1.0f, (float) feel.getProperty ("swing", 0.0));
    g.swingSixteenths = feel.getProperty ("swing_note", "8th").toString() == "16th";
    g.looseMs = juce::jlimit (0.0f, 30.0f, (float) feel.getProperty ("looseness_ms", 0.0));

    if (auto* push = feel.getProperty ("push", {}).getArray())
        for (auto& p : *push)
            if (drumkits::voiceNames().contains (p.getProperty ("drum", {}).toString()))
                g.pushMs[p.getProperty ("drum", {}).toString()] = juce::jlimit (-40.0f, 40.0f, (float) p.getProperty ("ms", 0.0));

    if (learnedFeel (feel.getProperty ("learned", {}).toString()) != nullptr)
        g.learned = feel.getProperty ("learned", {}).toString();

    std::set<juce::String> names;

    if (auto* sections = v.getProperty ("sections", {}).getArray())
    {
        for (auto& s : *sections)
        {
            Section section;
            section.name = s.getProperty ("name", {}).toString().trim();

            if (section.name.isEmpty())
                section.name = "Part " + juce::String (g.sections.size() + 1);

            // Names have to tell sections apart: they name the patterns
            const auto base = section.name;

            for (int n = 2; names.count (section.name) > 0; ++n)
                section.name = base + " " + juce::String (n);

            if (auto* bars = s.getProperty ("bars", {}).getArray())
            {
                for (auto& b : *bars)
                {
                    Bar bar;
                    bar.steps = juce::jlimit (1, 64, (int) b.getProperty ("steps", 16));

                    if (auto* lanes = b.getProperty ("lanes", {}).getArray())
                    {
                        for (auto& l : *lanes)
                        {
                            Lane lane;
                            lane.voice = l.getProperty ("drum", {}).toString();
                            lane.hits = l.getProperty ("hits", {}).toString().removeCharacters (" |");

                            if (! drumkits::voiceNames().contains (lane.voice))
                                continue;

                            // Exactly one character a step
                            if (lane.hits.length() > bar.steps)
                                lane.hits = lane.hits.substring (0, bar.steps);

                            while (lane.hits.length() < bar.steps)
                                lane.hits << ".";

                            bar.lanes.push_back (lane);
                        }
                    }

                    section.bars.push_back (bar);
                }
            }

            if (! section.bars.empty())
            {
                names.insert (section.name);
                g.sections.push_back (section);
            }
        }
    }

    if (g.sections.empty())
        return {};

    if (auto* arrangement = v.getProperty ("arrangement", {}).getArray())
        for (auto& name : *arrangement)
            if (names.count (name.toString().trim()) > 0)
                g.arrangement.add (name.toString().trim());

    return g;
}

int velocityFor (juce::juce_wchar c)
{
    if (c >= '1' && c <= '9')
        return 20 + (int) (c - '1') * 107 / 8;

    if (c == 'g' || c == 'G')
        return 28;

    return 96;      // flams, drags and anything unexpected: a solid hit
}

void render (Song& song, int channelId, const Groove& g, std::function<int (const juce::String&)> keyFor)
{
    // The time signature and tempo it asked for, before bars are measured
    if (g.meterBeats > 0 && (g.meterBeats != song.meterBeats || g.meterUnit != song.meterUnit))
    {
        song.meterBeats = g.meterBeats;
        song.meterUnit = g.meterUnit;
        song.notify (Song::Change::transport);
    }

    if (g.tempo >= 20 && g.tempo <= 400 && g.tempo != juce::roundToInt (song.bpm))
    {
        song.bpm = g.tempo;
        song.notify (Song::Change::transport);
    }

    auto c = song.findChannel (channelId);

    if (c == nullptr)
        return;

    auto& d = c->drummer;
    const int barTicks = song.ticksPerBar();
    const double ticksPerMs = song.bpm / 60.0 * ticksPerBeat / 1000.0;
    const float swing = d.swing >= 0.0f ? d.swing : g.swing;
    std::set<int> used;

    // The feel learned from real drummers, where it has one: each step's own timing and accent (its steps are a
    // 4/4 bar's, so in quarter-note meters only, repeating every four beats)
    const auto* learned = song.meterUnit == 4 ? learnedFeel (g.learned) : nullptr;

    for (size_t s = 0; s < g.sections.size(); ++s)
    {
        const auto& section = g.sections[s];

        // The section's pattern: the one it had, else a new one
        Pattern* pattern = nullptr;

        if (auto it = d.patterns.find (section.name); it != d.patterns.end())
            pattern = song.findPattern (it->second);

        if (pattern == nullptr || used.count (pattern->id) > 0)
        {
            auto& made = song.addPattern (c->name + " " + section.name);
            made.colour = c->colour.withRotatedHue ((float) s * 0.07f);
            pattern = &made;
            d.patterns[section.name] = made.id;
        }

        used.insert (pattern->id);
        auto& notes = pattern->notesFor (channelId);
        notes.clear();

        for (size_t b = 0; b < section.bars.size(); ++b)
        {
            const auto& bar = section.bars[b];
            const double stepTicks = (double) barTicks / bar.steps;

            for (const auto& lane : bar.lanes)
            {
                const int key = keyFor (lane.voice);

                if (key < 0)
                    continue;

                const float push = g.pushMs.count (lane.voice) > 0 ? g.pushMs.at (lane.voice) : 0.0f;

                // Each hit's learned step (where the drummers played it, and how hard), and the lane's average accent
                // over them, so the accents shape the part without moving its level: half as strong as learned, the
                // parts already leaning on the beat themselves
                std::vector<std::optional<LearnedStep>> steps ((size_t) lane.hits.length());
                float accents = 0.0f;
                int accented = 0;

                if (learned != nullptr)
                    for (int i = 0; i < lane.hits.length(); ++i)
                        if (const auto hit = lane.hits[i]; hit != '.' && hit != '-' && hit != '0')
                            if ((steps[(size_t) i] = learnedStep (*learned, lane.voice, std::fmod (i * stepTicks / ticksPerBeat, 4.0))))
                            {
                                accents += steps[(size_t) i]->accent;
                                ++accented;
                            }

                const float meanAccent = accented > 0 ? accents / (float) accented : 1.0f;

                for (int i = 0; i < lane.hits.length(); ++i)
                {
                    const auto hit = lane.hits[i];

                    if (hit == '.' || hit == '-' || hit == '0')
                        continue;

                    double pos = (double) b * barTicks + i * stepTicks;
                    const auto& step = steps[(size_t) i];

                    // Swing: the off-beat 8ths (or 16ths) late, up to a triplet
                    const double beats = pos / ticksPerBeat;
                    const double frac = beats - std::floor (beats);

                    if (! g.swingSixteenths && std::abs (frac - 0.5) < 1.0e-6)
                        pos += swing * ticksPerBeat / 6.0;
                    else if (g.swingSixteenths && (std::abs (frac - 0.25) < 1.0e-6 || std::abs (frac - 0.75) < 1.0e-6))
                        pos += swing * ticksPerBeat / 12.0;

                    // Feel, the same every time it's played: the drum's push, the step's learned timing, then a little wander
                    juce::Random random ((juce::int64) (s * 1000003 + b * 10007 + (size_t) i * 101) ^ lane.voice.hashCode64());
                    pos += (push + (step ? step->timingMs : 0.0f) + wobble (random) * g.looseMs * 0.6f) * d.looseness * ticksPerMs;

                    int velocity = velocityFor (hit);

                    if (step && hit != 'g' && hit != 'G')
                        velocity = juce::roundToInt ((float) velocity * (1.0f + 0.5f * (step->accent / meanAccent - 1.0f)));

                    velocity = 127 - juce::roundToInt ((127 - juce::jlimit (1, 127, velocity)) * d.dynamics);
                    velocity += juce::roundToInt (wobble (random) * 3.0f * d.looseness);
                    velocity = juce::jlimit (1, 127, velocity);

                    const int length = std::max (6, ticksPerStep / 2);

                    // A flam's grace note, or a drag's two, just before
                    if (hit == 'f' || hit == 'F')
                        notes.push_back ({ std::max (0, juce::roundToInt (pos - 22.0 * ticksPerMs)), length, key, std::max (1, velocity * 45 / 100) });

                    if (hit == 'r' || hit == 'R')
                        for (int grace : { 2, 1 })
                            notes.push_back ({ std::max (0, juce::roundToInt (pos - grace * 45.0 * ticksPerMs)), length, key, std::max (1, velocity * 40 / 100) });

                    notes.push_back ({ std::max (0, juce::roundToInt (pos)), length, key, velocity });
                }
            }
        }

        std::sort (notes.begin(), notes.end(), [] (auto& a, auto& z) { return a.start < z.start; });
    }

    song.notify (Song::Change::patterns);
    song.notify (Song::Change::notes);
}

void placeInArrange (Song& song, int channelId, const Groove& g)
{
    auto c = song.findChannel (channelId);

    if (c == nullptr)
        return;

    juce::StringArray order = g.arrangement;

    if (order.isEmpty())
        for (auto& s : g.sections)
            order.add (s.name);

    std::vector<std::pair<int, int>> clips;     // pattern, length
    int total = 0;

    for (auto& name : order)
        if (auto it = c->drummer.patterns.find (name); it != c->drummer.patterns.end())
            if (auto p = song.findPattern (it->second))
            {
                clips.emplace_back (p->id, song.patternLength (*p));
                total += song.patternLength (*p);
            }

    if (clips.empty())
        return;

    const int row = song.trackForChannel (c->id, 0, total);
    int start = 0;

    for (auto [patternId, length] : clips)
    {
        song.playlist.push_back ({ patternId, row, start });
        start += length;
    }

    song.notify (Song::Change::playlist);
}

std::function<int (const juce::String&)> keyMapFor (const Channel& channel)
{
    if (channel.type == InstrumentType::kit)
    {
        auto kit = drumkits::load (channel.kit);
        return [kit] (const juce::String& voice) { return kit != nullptr ? drumkits::keyForVoice (*kit, voice) : -1; };
    }

    return [] (const juce::String& voice) { return drumkits::generalMidiKey (voice); };
}

juce::StringArray voicesFor (const Channel& channel)
{
    if (channel.type == InstrumentType::kit)
        if (auto kit = drumkits::load (channel.kit))
            return drumkits::voicesIn (*kit);

    return drumkits::voiceNames();
}

bool rerender (Song& song, int channelId)
{
    auto c = song.findChannel (channelId);

    if (c == nullptr || ! juce::isPositiveAndBelow (c->drummer.shown, (int) c->drummer.turns.size()))
        return false;

    auto groove = Groove::fromJson (juce::JSON::parse (c->drummer.turns[(size_t) c->drummer.shown].groove));

    if (! groove.has_value())
        return false;

    render (song, channelId, *groove, keyMapFor (*c));
    return true;
}

} // namespace nightshift::drummer
