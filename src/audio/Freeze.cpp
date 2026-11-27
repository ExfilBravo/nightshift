// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Freeze.h"

namespace nightshift::freeze
{

namespace
{
    juce::String hashOf (const juce::String& text)
    {
        return juce::String::toHexString (text.hashCode64());
    }

    /** The channel's instrument and effects, as they decide its sound. */
    void addSound (juce::String& s, const Channel& c)
    {
        s << "|type " << (int) c.type << " " << c.sample << " " << c.rootKey << " " << (int) c.oneShot << " " << c.synthPreset
          << " " << c.pluginId << " " << c.kit << " " << c.instrument;

        for (auto& e : c.effects)
            s << "|fx " << e.id << " " << e.pluginId << " " << (int) e.bypassed;
    }

    void addNotes (juce::String& s, const std::vector<Note>* notes)
    {
        if (notes == nullptr)
            return;

        for (auto& n : *notes)
            s << n.start << "," << n.length << "," << n.key << "," << n.velocity << ";";
    }
}

bool hasNotes (const Pattern& p, int channelId)
{
    auto notes = p.findNotes (channelId);
    return notes != nullptr && ! notes->empty();
}

std::set<int> rowsOf (const Song& song, int channelId)
{
    std::set<int> rows;

    for (auto& clip : song.playlist)
        if (auto p = song.findPattern (clip.patternId); p != nullptr && hasNotes (*p, channelId))
            rows.insert (clip.track);

    return rows;
}

juce::String songStamp (const Song& song, const Channel& c)
{
    juce::String s;
    s << "song " << song.bpm << " " << song.meterBeats << "/" << song.meterUnit << " swing " << song.swing;

    for (auto& t : song.tempoChanges)
        s << "|tempo " << t.tick << " " << t.bpm << " " << (int) t.glide;

    addSound (s, c);

    for (auto& clip : song.playlist)
    {
        auto p = song.findPattern (clip.patternId);

        if (p == nullptr || ! hasNotes (*p, c.id))
            continue;

        s << "|clip " << clip.track << " " << clip.start << " " << clip.offset << " " << song.clipLength (clip) << " " << p->id << ":";
        addNotes (s, p->findNotes (c.id));
    }

    // Its instrument's and effects' automation is in the audio
    for (auto& lane : song.automation)
        if (lane.owner == c.id && (lane.param.startsWith ("instrument:") || lane.param.startsWith ("fx:")))
        {
            s << "|lane " << lane.param << ":";

            for (auto& point : lane.points)
                s << point.tick << "," << point.value << ";";
        }

    return hashOf (s);
}

juce::String patternStamp (const Song& song, const Channel& c, const Pattern& p)
{
    const auto [beats, unit] = song.meterOf (p);
    juce::String s;
    s << "pattern " << p.id << " " << song.bpm << " " << song.meterBeats << "/" << song.meterUnit << " " << beats << "/" << unit
      << " " << song.patternLength (p) << " swing " << song.swing;
    addSound (s, c);
    s << "|notes ";
    addNotes (s, p.findNotes (c.id));
    return hashOf (s);
}

bool isCurrent (const Song& song, const Channel& c)
{
    if (! c.freeze.on)
        return false;

    auto exists = [] (const juce::String& path) { return path.isNotEmpty() && juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile(); };

    if (song.mode == PlayMode::song)
    {
        if (c.freeze.songStamp != songStamp (song, c))
            return false;

        for (auto& [row, file] : c.freeze.rows)
            if (! exists (file))
                return false;

        return true;
    }

    // Pattern mode: the pattern playing. One without its notes plays nothing of it either way
    auto p = song.findPattern (song.currentPatternId);

    if (p == nullptr || ! hasNotes (*p, c.id))
        return true;

    auto file = c.freeze.patterns.find (p->id);
    auto stamp = c.freeze.patternStamps.find (p->id);

    return file != c.freeze.patterns.end() && stamp != c.freeze.patternStamps.end()
            && stamp->second == patternStamp (song, c, *p) && exists (file->second);
}

juce::StringArray filesOf (const Freeze& f)
{
    juce::StringArray files;

    for (auto& [row, file] : f.rows)
        files.add (file);

    for (auto& [pattern, file] : f.patterns)
        files.add (file);

    return files;
}

} // namespace nightshift::freeze
