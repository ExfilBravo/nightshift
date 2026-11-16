// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Templates.h"
#include "audio/Instruments.h"
#include "audio/Kits.h"
#include "audio/Nova.h"

namespace nightshift::templates
{

const std::vector<Template>& all()
{
    static const std::vector<Template> list {
        { "empty", "Empty project",
          "One empty track. Drag a sound onto it from the sidebar, or add tracks as you go.",
          { "pattern" }, 120.0 },
        { "beat", "Beat",
          "Drums on the 808 kit, an 808 bass and dusty Rhodes keys, swung.",
          { "drums", "bass", "keys" }, 90.0 },
        { "songwriter", "Songwriter",
          "A guitar and your voice to record, over piano and a studio drum kit.",
          { "guitar", "vocals", "keys", "drums" }, 100.0 },
        { "band", "Band",
          "Guitar, bass and vocals to record, and a studio drum kit for the AI drummer to play.",
          { "drums", "guitar", "bass", "vocals" }, 120.0 },
        { "electronic", "Electronic",
          "The 909, a sub bass, a pad and a lead, ready for patterns.",
          { "drums", "bass", "synth", "synth" }, 124.0 }
    };

    return list;
}

void build (const juce::String& id, Song& song, AudioEngine& audio)
{
    song.loadBlank();

    const Template* chosen = nullptr;

    for (auto& t : all())
        if (t.id == id)
            chosen = &t;

    if (chosen == nullptr || id == "empty")
        return;

    // Its tracks replace the blank one
    song.tracks.clear();
    song.bpm = chosen->bpm;

    auto track = [&song] (const juce::String& name, InstrumentType type) -> Channel&
    {
        auto& c = song.addChannel (name, type);
        Track t;
        t.channelId = c.id;
        song.insertTrack (t);
        return c;
    };

    // The first kit here whose name starts with one of `wanted`, else any
    auto kitFor = [] (std::initializer_list<const char*> wanted)
    {
        const auto ids = drumkits::ids();

        for (auto prefix : wanted)
            for (auto& kit : ids)
                if (kit.startsWithIgnoreCase (prefix))
                    return kit;

        return ids.isEmpty() ? juce::String() : ids[0];
    };

    auto drums = [&] (std::initializer_list<const char*> wanted)
    {
        if (const auto kit = kitFor (wanted); kit.isNotEmpty())
            track ("Drums", InstrumentType::kit).kit = kit;
    };

    auto synth = [&] (const juce::String& name, const juce::String& preset)
    {
        track (name, InstrumentType::synth).synthPreset = nova::findFactory (preset) != nullptr ? preset : nova::factory().front().name;
    };

    // Audio tracks on the first input; the first one armed, so Record and Space record it
    const auto inputs = audio.getInputNames();
    bool armedOne = false;

    auto audioTrack = [&] (const juce::String& name)
    {
        auto& c = track (name, InstrumentType::audio);
        c.input = inputs.isEmpty() ? juce::String() : inputs[0];
        c.armed = ! armedOne;
        armedOne = true;
    };

    // The sampled grand when it's been fetched, else Nova's piano-ish keys
    auto piano = [&]
    {
        for (auto& e : instruments::list())
            if (e.name.containsIgnoreCase ("piano") || e.name.containsIgnoreCase ("grand"))
            {
                track ("Piano", InstrumentType::instrument).instrument = e.path;
                return;
            }

        synth ("Piano", "Velvet Keys");
    };

    if (id == "beat")
    {
        song.swing = 0.25f;
        drums ({ "NS_808" });
        auto& bass = track ("808", InstrumentType::sampler);
        bass.sample = "kit:808";
        bass.rootKey = 36;
        bass.oneShot = false;
        synth ("Keys", "Dusty Rhodes");
    }
    else if (id == "songwriter")
    {
        audioTrack ("Guitar");
        audioTrack ("Vocals");
        piano();
        drums ({ "DRS_BRUSHES", "DRS_KIT", "MULDJORD", "BLACK_PEARL" });
    }
    else if (id == "band")
    {
        drums ({ "MULDJORD", "DRS_KIT", "CROCELL", "RED_ZEPPELIN" });
        audioTrack ("Guitar");
        audioTrack ("Bass");
        audioTrack ("Vocals");
    }
    else if (id == "electronic")
    {
        drums ({ "NS_909" });
        synth ("Bass", "Sub Bass");
        synth ("Pad", "Night Pad");
        synth ("Lead", "Supersaw Lead");
    }

    if (! song.channels.empty())
        song.selectedChannelId = song.channels.front().id;

    // A new project: nothing to undo, nothing unsaved yet
    song.notify (Song::Change::everything);
    song.markSaved();
}

} // namespace nightshift::templates
