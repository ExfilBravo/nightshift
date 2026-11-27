// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "MidiFiles.h"
#include "Kits.h"
#include "Nova.h"
#include "GuitarSound.h"
#include "Instruments.h"
#include "Machines.h"
#include <set>

namespace nightshift::midifile
{

namespace
{
    // What a written file counts a quarter note in: ten to NightShift's tick, so swing's part-ticks
    // keep their place
    constexpr int fileTicksPerBeat = 960;

    /** Swing pushes every second 16th of a pattern later, as the engine plays it (fillClip). */
    double swungStart (const Song& song, const Note& n)
    {
        double start = n.start;

        if (n.start % (2 * ticksPerStep) == ticksPerStep)
            start += song.swing * 0.5 * ticksPerStep;

        return start;
    }

    /** A kit's keys to General MIDI's, by the drum each plays: a key General MIDI has for the same
        drum stays where it is. */
    std::map<int, int> generalMidiKeysFor (const juce::String& kitId)
    {
        std::map<int, int> map;
        auto kit = drumkits::load (kitId);

        if (kit == nullptr)
            return map;

        for (auto& voice : drumkits::voiceNames())
        {
            const int key = drumkits::keyForVoice (*kit, voice), gm = drumkits::generalMidiKey (voice);

            if (key >= 0 && gm >= 0 && key == gm)
                map[key] = gm;
        }

        for (auto& voice : drumkits::voiceNames())
        {
            const int key = drumkits::keyForVoice (*kit, voice), gm = drumkits::generalMidiKey (voice);

            if (key >= 0 && gm >= 0 && map.count (key) == 0)
                map[key] = gm;
        }

        return map;
    }

    /** General MIDI's drums onto a kit's keys, by the drum each is; ones the kit has nothing like
        keep their key. */
    int kitKeyFor (const drumkits::Kit* kit, int generalMidiKey)
    {
        if (kit == nullptr)
            return generalMidiKey;

        for (auto& voice : drumkits::voiceNames())
            if (drumkits::generalMidiKey (voice) == generalMidiKey)
                if (const int key = drumkits::keyForVoice (*kit, voice); key >= 0)
                    return key;

        return generalMidiKey;
    }

    bool isGenericName (const juce::String& name)
    {
        // What some programs call every track: no better than nothing
        const auto n = name.trim().toLowerCase();
        return n.isEmpty() || n.startsWith ("untitled") || n == "track" || n.matchesWildcard ("track ?", true)
            || n.matchesWildcard ("track ??", true) || n == "midi" || n.startsWith ("midi track");
    }
}

//==============================================================================
std::vector<PlayedNote> notesAsPlayed (const Song& song, int patternId, bool heardOnly)
{
    std::vector<PlayedNote> out;

    auto plays = [&] (const Channel& c)
    {
        return c.type != InstrumentType::audio && ! (heardOnly && c.muted);
    };

    // The notes of a pattern from `offset` for `length` ticks (-1: all of it), placed at `at` in
    // the song and cut at `end`, as the engine's clip plays them
    auto add = [&] (const Pattern& p, double at, int offset, int length, double end)
    {
        for (auto& c : song.channels)
        {
            const auto* notes = plays (c) ? p.findNotes (c.id) : nullptr;

            if (notes == nullptr)
                continue;

            for (auto& n : *notes)
            {
                if (n.start < offset || (length >= 0 && n.start >= offset + length))
                    continue;

                const double start = at + swungStart (song, n) - offset;

                if (start < end)
                    out.push_back ({ c.id, start, std::min (start + n.length, end), n.key, n.velocity });
            }
        }
    };

    if (patternId != 0)
    {
        if (auto p = song.findPattern (patternId))
            add (*p, 0.0, 0, -1, song.patternLength (*p));
    }
    else
    {
        for (auto& placed : song.playlist)
        {
            auto p = song.findPattern (placed.patternId);

            if (p == nullptr || (heardOnly && ! song.isTrackAudible (placed.track)))
                continue;

            const int length = song.clipLength (placed);
            add (*p, placed.start, placed.offset, length, (double) placed.start + length);
        }
    }

    std::stable_sort (out.begin(), out.end(), [] (const PlayedNote& a, const PlayedNote& b)
    {
        return a.start != b.start ? a.start < b.start : a.key < b.key;
    });

    return out;
}

//==============================================================================
juce::String programName (int program)
{
    if (! juce::isPositiveAndBelow (program, 128))
        return {};

    return juce::String (juce::CharPointer_UTF8 (juce::MidiMessage::getGMInstrumentName (program)));
}

std::pair<int, int> nearestMeter (int beats, int unit)
{
    const auto& list = Song::meters();

    for (auto [b, u] : list)
        if (b == beats && u == unit)
            return { b, u };

    if (beats <= 0 || unit <= 0)
        return { 4, 4 };

    // Bars as long (2/2 as 4/4, 10/8 as 5/4), the beat closest
    const double quarters = beats * 4.0 / unit;
    std::pair<int, int> best { 0, 0 };
    double bestGap = 1.0e9;

    for (auto [b, u] : list)
        if (std::abs (b * 4.0 / u - quarters) < 1.0e-9)
            if (const double gap = std::abs (std::log2 ((double) u / unit)); gap < bestGap)
            {
                bestGap = gap;
                best = { b, u };
            }

    if (best.first > 0)
        return best;

    // The shortest bar of the same beat that holds a whole number of them (3/8 as 6/8)
    for (auto [b, u] : list)
        if (u == unit && b % beats == 0 && (best.first == 0 || b < best.first))
            best = { b, u };

    return best.first > 0 ? best : std::pair<int, int> { 4, 4 };
}

//==============================================================================
std::optional<Contents> read (const juce::File& file, juce::String& error)
{
    juce::FileInputStream in (file);

    if (! in.openedOk())
    {
        error = "NightShift couldn't open " + file.getFileName() + ".";
        return std::nullopt;
    }

    return read (in, error);
}

std::optional<Contents> read (juce::InputStream& in, juce::String& error)
{
    juce::MidiFile file;

    if (! file.readFrom (in, true))
    {
        error = "It isn't a MIDI file, or part of it is missing.";
        return std::nullopt;
    }

    // Ticks of a quarter note, or (a file timed in film frames) seconds at 120 to the minute
    const short format = file.getTimeFormat();

    if (format <= 0)
        file.convertTimestampTicksToSeconds();

    auto toTicks = [format] (double t)
    {
        return (int) std::llround (format > 0 ? t * ticksPerBeat / format : t * 2.0 * ticksPerBeat);
    };

    Contents out;

    // The tempo, from wherever in the file it's given; where it changes (from 20 to 400)
    juce::MidiMessageSequence tempos, meters;
    file.findAllTempoEvents (tempos);
    file.findAllTimeSigEvents (meters);
    tempos.sort();
    meters.sort();

    for (auto* e : tempos)
    {
        const double seconds = e->message.getTempoSecondsPerQuarterNote();

        if (seconds <= 0.0)
            continue;

        const double bpm = juce::jlimit (20.0, 400.0, std::round (60000.0 / seconds) / 1000.0);
        const int tick = toTicks (e->message.getTimeStamp());

        if (tick <= 0)
        {
            out.bpm = bpm;
            continue;
        }

        const double before = out.tempoChanges.empty() ? out.bpm : out.tempoChanges.back().bpm;

        if (! out.tempoChanges.empty() && out.tempoChanges.back().tick == tick)
            out.tempoChanges.back().bpm = bpm;
        else if (std::abs (bpm - before) > 1.0e-6)
            out.tempoChanges.push_back ({ tick, bpm, false });
    }

    std::erase_if (out.tempoChanges, [] (const TempoChange& t) { return t.tick <= 0; });

    // The time signature, as NightShift has it (one it doesn't, as the nearest that keeps the beat)
    for (auto* e : meters)
    {
        int beats = 4, unit = 4;
        e->message.getTimeSignatureInfo (beats, unit);
        const auto [b, u] = nearestMeter (beats, unit);

        if (b != beats || u != unit)
            out.warnings.addIfNotAlreadyThere (juce::String (beats) + "/" + juce::String (unit) + " plays as "
                                               + juce::String (b) + "/" + juce::String (u));

        const int tick = toTicks (e->message.getTimeStamp());

        if (tick <= 0)
        {
            out.meterBeats = b;
            out.meterUnit = u;
            continue;
        }

        const auto before = out.meterChanges.empty() ? std::pair (out.meterBeats, out.meterUnit)
                                                     : std::pair (out.meterChanges.back().beats, out.meterChanges.back().unit);

        if (! out.meterChanges.empty() && out.meterChanges.back().tick == tick)
            std::tie (out.meterChanges.back().beats, out.meterChanges.back().unit) = std::pair (b, u);
        else if (before != std::pair (b, u))
            out.meterChanges.push_back ({ tick, b, u });
    }

    // Each channel's first program anywhere in the file, for a track whose notes don't say
    std::map<int, int> programs;

    for (int t = 0; t < file.getNumTracks(); ++t)
        for (auto* e : *file.getTrack (t))
            if (e->message.isProgramChange() && programs.count (e->message.getChannel()) == 0)
                programs[e->message.getChannel()] = e->message.getProgramChangeNumber();

    // A part for each MIDI channel of each track with notes on it (a type 0 file is one track of
    // all of them)
    for (int t = 0; t < file.getNumTracks(); ++t)
    {
        auto track = *file.getTrack (t);
        track.updateMatchedPairs();
        juce::String trackName, instrumentName;
        std::map<int, Part> byChannel;
        const double trackEnd = track.getEndTime();

        for (auto* e : track)
        {
            const auto& m = e->message;

            if (m.isTrackNameEvent() && trackName.isEmpty())
            {
                trackName = m.getTextFromTextMetaEvent().trim();
            }
            else if (m.isMetaEvent() && m.getMetaEventType() == 4 && instrumentName.isEmpty())
            {
                instrumentName = m.getTextFromTextMetaEvent().trim();
            }
            else if (m.isProgramChange())
            {
                auto& part = byChannel[m.getChannel()];

                if (part.program < 0)
                    part.program = m.getProgramChangeNumber();
            }
            else if (m.isNoteOn())
            {
                auto& part = byChannel[m.getChannel()];
                const double off = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() : trackEnd;

                Note n;
                n.start = std::max (0, toTicks (m.getTimeStamp()));
                n.length = std::max (1, toTicks (off) - n.start);
                n.key = m.getNoteNumber();
                n.velocity = juce::jlimit (1, 127, (int) m.getVelocity());
                part.notes.push_back (n);
            }
        }

        const bool severalChannels = std::count_if (byChannel.begin(), byChannel.end(), [] (auto& p) { return ! p.second.notes.empty(); }) > 1;

        for (auto& [channel, part] : byChannel)
        {
            if (part.notes.empty())
                continue;

            part.midiChannel = channel;

            if (part.program < 0 && programs.count (channel) > 0)
                part.program = programs[channel];

            // Named after its track (a track of several channels: what each plays), else its
            // program, else its place
            const auto given = ! isGenericName (trackName) ? trackName : ! isGenericName (instrumentName) ? instrumentName : juce::String();
            const auto played = part.isDrums() ? juce::String ("Drums") : programName (part.program);

            if (given.isNotEmpty())
                part.name = severalChannels && played.isNotEmpty() ? given + " - " + played : given;
            else if (played.isNotEmpty())
                part.name = played;
            else
                part.name = "Track " + juce::String ((int) out.parts.size() + 1);

            std::stable_sort (part.notes.begin(), part.notes.end(), [] (const Note& a, const Note& b)
            {
                return a.start != b.start ? a.start < b.start : a.key < b.key;
            });

            for (auto& n : part.notes)
                out.lengthTicks = std::max (out.lengthTicks, n.end());

            out.parts.push_back (std::move (part));
        }
    }

    if (out.parts.empty())
        out.warnings.add ("It has no notes");

    return out;
}

//==============================================================================
std::map<int, int> midiChannelsFor (const Song& song, const std::vector<PlayedNote>& notes)
{
    std::set<int> withNotes;

    for (auto& n : notes)
        withNotes.insert (n.channelId);

    // Drums on 10 and a synth plugged in on its own channel; the rest take the others in turn
    std::map<int, int> map;
    std::set<int> taken { 10 };

    for (auto& c : song.channels)
        if (withNotes.count (c.id) > 0 && c.type == InstrumentType::midiOut)
            taken.insert (juce::jlimit (1, 16, c.midiOutChannel));

    std::vector<int> free;

    for (int ch = 1; ch <= 16; ++ch)
        if (taken.count (ch) == 0)
            free.push_back (ch);

    if (free.empty())
        for (int ch = 1; ch <= 16; ++ch)
            if (ch != 10)
                free.push_back (ch);

    size_t next = 0;

    for (auto& c : song.channels)
    {
        if (withNotes.count (c.id) == 0)
            continue;

        if (c.type == InstrumentType::midiOut)
            map[c.id] = juce::jlimit (1, 16, c.midiOutChannel);
        else if (c.type == InstrumentType::kit || ! c.drummer.turns.empty())
            map[c.id] = 10;
        else
            map[c.id] = free[next++ % free.size()];
    }

    return map;
}

juce::MidiFile write (const Song& song, int patternId)
{
    const auto notes = notesAsPlayed (song, patternId, true);
    const auto channels = midiChannelsFor (song, notes);
    auto at = [] (double ticks) { return (double) std::llround (ticks * fileTicksPerBeat / ticksPerBeat); };

    double last = 0.0;

    for (auto& n : notes)
        last = std::max (last, n.end);

    // The tempo track: time signatures, and the tempo where it changes
    juce::MidiMessageSequence tempo;
    double lastBpm = -1.0;

    auto addMeter = [&] (double tick, int beats, int unit)
    {
        tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (beats, unit), at (tick));
    };

    auto addTempo = [&] (double tick, double bpm)
    {
        if (std::abs (bpm - lastBpm) < 1.0e-9)
            return;

        lastBpm = bpm;
        tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::llround (60.0e6 / bpm)), at (tick));
    };

    if (patternId != 0)
    {
        // As Pattern mode plays it: the start's tempo, its own time signature
        const auto* p = song.findPattern (patternId);
        const auto [beats, unit] = p != nullptr ? song.meterOf (*p) : std::pair (song.meterBeats, song.meterUnit);
        addMeter (0.0, beats, unit);
        addTempo (0.0, song.bpm);
    }
    else
    {
        addMeter (0.0, song.meterBeats, song.meterUnit);

        for (auto& m : song.meterChanges)
        {
            addMeter (m.tick, m.beats, m.unit);
            last = std::max (last, (double) m.tick);
        }

        // A jump where it is; a glide a 16th at a time, each at the tempo that takes as long as the
        // song takes over it
        addTempo (0.0, song.bpm);
        double from = 0.0;

        for (auto& change : song.tempoChanges)
        {
            if (change.glide)
                for (double t = from; t < change.tick - 0.5; t += ticksPerStep)
                {
                    const double to = std::min ((double) change.tick, t + ticksPerStep);

                    if (const double seconds = song.secondsBetween (t, to); seconds > 0.0)
                        addTempo (t, (to - t) * 60.0 / (ticksPerBeat * seconds));
                }

            addTempo (change.tick, change.bpm);
            from = change.tick;
            last = std::max (last, (double) change.tick);
        }
    }

    tempo.addEvent (juce::MidiMessage::endOfTrack(), at (last));

    juce::MidiFile file;
    file.setTicksPerQuarterNote (fileTicksPerBeat);
    file.addTrack (tempo);

    // A track per channel: its name, its program, its notes (offs before ons at the same moment)
    for (auto& c : song.channels)
    {
        const auto found = channels.find (c.id);

        if (found == channels.end())
            continue;

        const int midiChannel = found->second;
        const auto keys = c.type == InstrumentType::kit ? generalMidiKeysFor (c.kit) : std::map<int, int>();
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::textMetaEvent (3, c.name), 0.0);

        if (c.midiProgram >= 0)
            track.addEvent (juce::MidiMessage::programChange (midiChannel, juce::jlimit (0, 127, c.midiProgram)), 0.0);

        struct Event
        {
            double time;
            bool on;
            juce::MidiMessage message;
        };

        std::vector<Event> events;

        for (auto& n : notes)
        {
            if (n.channelId != c.id)
                continue;

            const auto key = keys.find (n.key);
            const int written = juce::jlimit (0, 127, key != keys.end() ? key->second : n.key);
            events.push_back ({ at (n.start), true, juce::MidiMessage::noteOn (midiChannel, written, (juce::uint8) juce::jlimit (1, 127, n.velocity)) });
            events.push_back ({ at (n.end), false, juce::MidiMessage::noteOff (midiChannel, written) });
        }

        std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b)
        {
            return a.time != b.time ? a.time < b.time : (! a.on && b.on);
        });

        for (auto& e : events)
            track.addEvent (e.message, e.time);

        track.addEvent (juce::MidiMessage::endOfTrack(), at (last));
        file.addTrack (track);
    }

    return file;
}

bool save (const Song& song, const juce::File& target, int patternId)
{
    const auto file = write (song, patternId);
    juce::MemoryOutputStream bytes;

    if (! file.writeTo (bytes, 1))
        return false;

    return target.replaceWithData (bytes.getData(), bytes.getDataSize());
}

//==============================================================================
Sound soundFor (const Part& part)
{
    Sound sound;

    if (part.isDrums())
    {
        // A drum kit: the 808 for General MIDI's 808 kit, brushes for its brush kit, else the
        // first studio kit there is (the machines when there's nothing else)
        sound.type = InstrumentType::kit;
        const auto ids = drumkits::ids();
        const auto machines = drumkits::machines::ids();

        auto find = [&ids] (const juce::String& word) -> juce::String
        {
            for (auto& id : ids)
                if (id.containsIgnoreCase (word) || drumkits::displayName (id).containsIgnoreCase (word))
                    return id;

            return {};
        };

        if (part.program == 25)
            sound.kit = find ("808");
        else if (part.program == 40)
            sound.kit = find ("brush");

        if (sound.kit.isEmpty())
            for (auto& id : ids)
                if (! machines.contains (id))
                {
                    sound.kit = id;
                    break;
                }

        if (sound.kit.isEmpty() && ! ids.isEmpty())
            sound.kit = ids[0];

        return sound;
    }

    const int p = part.program;
    const int family = p < 0 ? 0 : p / 8;

    auto nova = [&sound] (const char* preset)
    {
        sound.type = InstrumentType::synth;
        sound.synthPreset = nova::findFactory (preset) != nullptr ? juce::String (preset) : nova::defaultPreset();
    };

    auto guitar = [&sound] (const char* preset)
    {
        sound.type = InstrumentType::instrument;
        sound.instrument = guitarsound::pathFor (preset);
    };

    switch (family)
    {
        case 0:     // pianos: the sampled grand when it's here
            if (p == 4 || p == 5)        { nova ("Dusty Rhodes"); break; }
            if (p == 6 || p == 7)        { nova ("Clav Funk"); break; }

            for (auto& e : instruments::list())
                if (e.category == "Keys" && e.name.containsIgnoreCase ("piano"))
                {
                    sound.type = InstrumentType::instrument;
                    sound.instrument = e.path;
                    return sound;
                }

            nova ("Velvet Keys");
            break;

        case 1:     nova (p == 10 ? "Music Box" : p == 14 ? "Church Bell" : p == 15 ? "Harp Pluck" : (p >= 11 && p <= 13) ? "Kalimba" : "Crystal Bell"); break;
        case 2:     nova ("Drawbar Organ"); break;
        case 3:     guitar (p == 26 ? "Jazz Box" : p == 28 ? "Funk Clean" : p == 29 ? "Crunch" : p == 30 ? "High Gain" : "Clean"); break;
        case 4:     nova (p >= 38 ? "Moog Bass" : "Finger Bass"); break;
        case 5:     nova (p == 45 ? "Pizz Pluck" : p == 46 ? "Harp Pluck" : "String Machine"); break;
        case 6:     nova (p >= 52 && p <= 54 ? "Glass Choir" : p == 55 ? "90s Stab" : "String Machine"); break;
        case 7:     nova ("Synth Brass"); break;
        case 8:     nova ("Mono Lead"); break;
        case 9:     nova ("Soft Flute"); break;
        case 10:    nova (p == 80 ? "Chip Lead" : p == 81 ? "Supersaw Lead" : "Mono Lead"); break;
        case 11:    nova (p == 89 ? "Warm Analog Pad" : p == 91 ? "Glass Choir" : "Night Pad"); break;
        case 12:    nova ("Space Drone"); break;
        case 13:    nova (p == 108 ? "Kalimba" : "Harp Pluck"); break;
        case 14:    nova (p == 112 ? "Crystal Bell" : "Kalimba"); break;
        default:    nova ("Riser"); break;
    }

    return sound;
}

std::vector<int> addToSong (Song& song, const Contents& contents, bool takeTimeline, int atTick)
{
    std::vector<int> added;

    if (takeTimeline)
    {
        atTick = 0;
        song.bpm = contents.bpm;
        song.meterBeats = contents.meterBeats;
        song.meterUnit = contents.meterUnit;
        song.tempoChanges = contents.tempoChanges;
        song.meterChanges = contents.meterChanges;
        song.tidyTimeline();
    }

    atTick = std::max (0, atTick);

    for (auto& part : contents.parts)
    {
        if (part.notes.empty())
            continue;

        const auto sound = soundFor (part);
        auto& c = song.addChannel (part.name, sound.type);
        c.synthPreset = sound.synthPreset;
        c.kit = sound.kit;
        c.instrument = sound.instrument;
        c.midiProgram = part.program;
        const int channelId = c.id;
        const auto colour = c.colour;
        added.push_back (channelId);

        // Drums onto the kit's own keys
        const auto kit = sound.type == InstrumentType::kit && sound.kit.isNotEmpty() ? drumkits::load (sound.kit) : nullptr;

        // From the bar its first note is in, so its clip starts where it does
        const int first = atTick + part.notes.front().start;
        const int clipStart = std::max (0, std::min (first, song.barStart (song.barAt (first))));

        auto& pattern = song.addPattern (part.name);
        pattern.colour = colour;
        auto& list = pattern.notesFor (channelId);

        for (auto n : part.notes)
        {
            n.start = atTick + n.start - clipStart;

            if (part.isDrums())
                n.key = kitKeyFor (kit.get(), n.key);

            list.push_back (n);
        }

        song.fitPatternMeter (pattern, clipStart);
        const int patternId = pattern.id;
        const int length = song.patternLength (pattern);
        const int row = song.trackForChannel (channelId, clipStart, clipStart + length);

        PlaylistClip clip;
        clip.patternId = patternId;
        clip.track = row;
        clip.start = clipStart;
        song.playlist.push_back (clip);
    }

    if (! added.empty())
    {
        song.mode = PlayMode::song;
        song.selectedChannelId = added.front();

        for (auto& p : song.patterns)
            if (const auto* notes = p.findNotes (added.front()); notes != nullptr && ! notes->empty())
            {
                song.currentPatternId = p.id;
                break;
            }
    }

    return added;
}

} // namespace nightshift::midifile
