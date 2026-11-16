// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "TrackIcons.h"
#include "audio/Instruments.h"
#include "audio/Nova.h"

namespace nightshift::trackicons
{

const std::vector<Icon>& all()
{
    static const std::vector<Icon> icons {
        { "drums", "Drums" },     { "guitar", "Guitar" },   { "bass", "Bass" },
        { "keys", "Keys" },       { "synth", "Synth" },     { "vocals", "Vocals" },
        { "strings", "Strings" }, { "brass", "Brass" },     { "woodwind", "Woodwind" },
        { "audio", "Audio" },     { "pattern", "Pattern" }
    };

    return icons;
}

bool isIcon (const juce::String& id)
{
    return std::any_of (all().begin(), all().end(), [&id] (auto& i) { return id == i.id; });
}

juce::String nameOf (const juce::String& id)
{
    for (auto& i : all())
        if (id == i.id)
            return i.name;

    return {};
}

//==============================================================================
namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;

    /** The outline of a body with two bouts (a guitar's, a violin's): two circles on one
        upright line, the upper one smaller, joined where they cross. */
    void addBody (juce::Path& p, juce::Point<float> lower, float rl, juce::Point<float> upper, float ru)
    {
        const float d = lower.y - upper.y;
        const float a = (rl * rl - ru * ru + d * d) / (2.0f * d);
        const float h = std::sqrt (std::max (0.0f, rl * rl - a * a));
        const float atLower = std::atan2 (h, a);
        const float atUpper = pi - std::atan2 (h, d - a);

        p.startNewSubPath (lower.x + h, lower.y - a);
        p.addCentredArc (lower.x, lower.y, rl, rl, 0.0f, atLower, 2.0f * pi - atLower, false);
        p.addCentredArc (upper.x, upper.y, ru, ru, 0.0f, -atUpper, atUpper, false);
        p.closeSubPath();
    }

    /** Scales a drawing to fill the 24-unit square (less a margin), keeping its shape. */
    juce::Path fitted (juce::Path p, float margin = 2.0f)
    {
        p.applyTransform (p.getTransformToScaleToFit (juce::Rectangle<float> (margin, margin, 24.0f - 2.0f * margin, 24.0f - 2.0f * margin), true));
        return p;
    }

    juce::Path drums()
    {
        // A snare: its head, shell and lugs, and two sticks resting on it
        juce::Path p;
        p.addEllipse (3.5f, 8.0f, 17.0f, 6.0f);
        p.startNewSubPath (3.5f, 11.0f);   p.lineTo (3.5f, 17.5f);
        p.startNewSubPath (20.5f, 11.0f);  p.lineTo (20.5f, 17.5f);
        p.addCentredArc (12.0f, 17.5f, 8.5f, 3.0f, 0.0f, 0.5f * pi, 1.5f * pi, true);

        for (float x : { 8.0f, 12.0f, 16.0f })
        {
            const float frontOfHead = 11.0f + 3.0f * std::sqrt (1.0f - std::pow ((x - 12.0f) / 8.5f, 2.0f));
            p.startNewSubPath (x, frontOfHead + 1.2f);
            p.lineTo (x, frontOfHead + 6.5f);
        }

        p.startNewSubPath (3.0f, 2.5f);   p.lineTo (10.0f, 9.5f);
        p.startNewSubPath (21.0f, 2.5f);  p.lineTo (14.0f, 9.5f);
        return fitted (p);
    }

    juce::Path guitar()
    {
        // Drawn upright, then leant over: body, sound hole, bridge, neck and head
        juce::Path p;
        addBody (p, { 12.0f, 17.5f }, 5.0f, { 12.0f, 11.6f }, 3.6f);
        p.addEllipse (10.75f, 13.4f, 2.5f, 2.5f);
        p.startNewSubPath (10.4f, 19.6f);  p.lineTo (13.6f, 19.6f);
        p.startNewSubPath (12.0f, 8.0f);   p.lineTo (12.0f, 2.9f);
        p.addRoundedRectangle (10.6f, 0.0f, 2.8f, 3.4f, 0.9f);
        p.applyTransform (juce::AffineTransform::rotation (pi * 0.25f, 12.0f, 12.0f));
        return fitted (p, 1.5f);
    }

    juce::Path bass()
    {
        // A bass clef: the curl from its dot, and the two dots beside it
        juce::Path p;
        p.addEllipse (5.3f, 8.0f, 2.6f, 2.6f);
        p.startNewSubPath (5.4f, 9.3f);
        p.cubicTo (5.4f, 5.6f, 8.6f, 3.6f, 11.6f, 3.6f);
        p.cubicTo (15.3f, 3.6f, 17.4f, 6.4f, 17.4f, 9.8f);
        p.cubicTo (17.4f, 14.6f, 13.2f, 18.4f, 5.0f, 21.0f);
        p.addEllipse (20.0f, 6.4f, 1.6f, 1.6f);
        p.addEllipse (20.0f, 11.6f, 1.6f, 1.6f);
        return fitted (p);
    }

    juce::Path keys()
    {
        // Four white keys, two black
        juce::Path p;
        p.addRoundedRectangle (2.5f, 5.0f, 19.0f, 14.0f, 2.0f);
        p.addRectangle (6.0f, 5.0f, 2.5f, 7.6f);
        p.addRectangle (10.75f, 5.0f, 2.5f, 7.6f);

        for (float x : { 7.25f, 12.0f })
        {
            p.startNewSubPath (x, 12.6f);
            p.lineTo (x, 19.0f);
        }

        p.startNewSubPath (16.75f, 5.0f);
        p.lineTo (16.75f, 19.0f);
        return fitted (p);
    }

    juce::Path synth()
    {
        // An oscillator's sine, two cycles
        juce::Path p;
        p.startNewSubPath (2.0f, 12.0f);

        for (int half = 0; half < 4; ++half)
        {
            const float x0 = 2.0f + half * 5.0f;
            p.quadraticTo (x0 + 2.5f, half % 2 == 0 ? 0.5f : 23.5f, x0 + 5.0f, 12.0f);
        }

        return fitted (p);
    }

    juce::Path vocals()
    {
        // A microphone on its stand
        juce::Path p;
        p.addRoundedRectangle (9.0f, 2.0f, 6.0f, 11.5f, 3.0f);
        p.addCentredArc (12.0f, 10.5f, 6.5f, 6.5f, 0.0f, 1.5f * pi, 0.5f * pi, true);
        p.startNewSubPath (12.0f, 17.0f);  p.lineTo (12.0f, 21.5f);
        p.startNewSubPath (8.5f, 21.5f);   p.lineTo (15.5f, 21.5f);
        return fitted (p);
    }

    juce::Path strings()
    {
        // A violin, upright (the guitar leans): body, f-holes, tailpiece, neck and scroll
        juce::Path p;
        addBody (p, { 12.0f, 16.0f }, 4.6f, { 12.0f, 9.6f }, 3.5f);
        p.startNewSubPath (10.0f, 13.4f);  p.lineTo (10.0f, 17.4f);
        p.startNewSubPath (14.0f, 13.4f);  p.lineTo (14.0f, 17.4f);
        p.startNewSubPath (12.0f, 17.4f);  p.lineTo (12.0f, 20.6f);
        p.startNewSubPath (12.0f, 6.1f);   p.lineTo (12.0f, 2.6f);
        p.addEllipse (10.9f, 0.4f, 2.2f, 2.2f);
        return fitted (p, 1.5f);
    }

    juce::Path brass()
    {
        // A trumpet: mouthpiece, pipe, three valves, the slide under them and the bell
        juce::Path p;
        p.startNewSubPath (2.0f, 9.5f);    p.lineTo (2.0f, 12.5f);
        p.startNewSubPath (2.0f, 11.0f);   p.lineTo (4.5f, 11.0f);
        p.startNewSubPath (4.5f, 10.0f);   p.lineTo (15.0f, 10.0f);
        p.startNewSubPath (4.5f, 12.0f);   p.lineTo (15.0f, 12.0f);

        for (float x : { 7.5f, 10.0f, 12.5f })
        {
            p.startNewSubPath (x, 10.0f);          p.lineTo (x, 6.5f);
            p.startNewSubPath (x - 0.9f, 6.0f);    p.lineTo (x + 0.9f, 6.0f);
        }

        p.startNewSubPath (6.5f, 12.0f);
        p.lineTo (6.5f, 14.0f);
        p.quadraticTo (6.5f, 15.5f, 8.0f, 15.5f);
        p.lineTo (12.0f, 15.5f);
        p.quadraticTo (13.5f, 15.5f, 13.5f, 14.0f);
        p.lineTo (13.5f, 12.0f);

        p.startNewSubPath (15.0f, 10.0f);  p.quadraticTo (19.5f, 10.0f, 22.0f, 6.0f);
        p.startNewSubPath (15.0f, 12.0f);  p.quadraticTo (19.5f, 12.0f, 22.0f, 16.0f);
        p.startNewSubPath (22.0f, 6.0f);   p.lineTo (22.0f, 16.0f);
        return fitted (p);
    }

    juce::Path woodwind()
    {
        // A clarinet, leant the other way from the guitar: mouthpiece, body, keys and bell
        juce::Path p;
        p.addRoundedRectangle (10.8f, 2.6f, 2.4f, 15.6f, 1.0f);
        p.startNewSubPath (11.4f, 2.6f);   p.lineTo (11.7f, 0.3f);  p.lineTo (12.3f, 0.3f);  p.lineTo (12.6f, 2.6f);
        p.startNewSubPath (10.8f, 18.2f);  p.lineTo (9.0f, 22.0f);  p.lineTo (15.0f, 22.0f);  p.lineTo (13.2f, 18.2f);

        for (float y : { 6.0f, 9.0f, 12.0f, 15.0f })
            p.addEllipse (11.45f, y - 0.55f, 1.1f, 1.1f);

        p.applyTransform (juce::AffineTransform::rotation (-pi * 0.25f, 12.0f, 12.0f));
        return fitted (p, 1.5f);
    }

    juce::Path audio()
    {
        // A recording's waveform
        juce::Path p;
        const float heights[] = { 4.0f, 10.0f, 17.0f, 8.0f, 14.0f, 6.0f, 3.0f };

        for (int i = 0; i < 7; ++i)
        {
            const float x = 3.0f + i * 3.0f;
            p.startNewSubPath (x, 12.0f - heights[i] * 0.5f);
            p.lineTo (x, 12.0f + heights[i] * 0.5f);
        }

        return fitted (p);
    }

    juce::Path pattern()
    {
        // FL's pattern blocks
        juce::Path p;

        for (float x : { 3.5f, 13.5f })
            for (float y : { 3.5f, 13.5f })
                p.addRoundedRectangle (x, y, 7.0f, 7.0f, 1.6f);

        return fitted (p, 3.0f);
    }
}

juce::Path path (const juce::String& id)
{
    if (id == "drums")      return drums();
    if (id == "guitar")     return guitar();
    if (id == "bass")       return bass();
    if (id == "keys")       return keys();
    if (id == "synth")      return synth();
    if (id == "vocals")     return vocals();
    if (id == "strings")    return strings();
    if (id == "brass")      return brass();
    if (id == "woodwind")   return woodwind();
    if (id == "audio")      return audio();
    return pattern();
}

void draw (juce::Graphics& g, const juce::String& id, juce::Rectangle<float> area, juce::Colour colour)
{
    auto p = path (id);
    const float scale = std::min (area.getWidth(), area.getHeight()) / 24.0f;
    p.applyTransform (juce::AffineTransform::scale (scale).translated (area.getCentreX() - 12.0f * scale, area.getCentreY() - 12.0f * scale));
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (std::max (1.2f, 1.65f * scale), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
namespace
{
    /** An icon from the words in a name, or "" if nothing in it says. */
    juce::String fromWords (const juce::String& text)
    {
        const auto t = " " + text.toLowerCase() + " ";
        auto has = [&t] (std::initializer_list<const char*> words)
        {
            return std::any_of (words.begin(), words.end(), [&t] (const char* w) { return t.contains (w); });
        };

        // Most particular first: a bass drum is drums, a double bass strings, a bassoon a woodwind
        if (has ({ "kick", "snare", "drum", " hat", "hihat", "hi-hat", "clap", "cymbal", "crash", " ride ", " tom", "perc",
                   "shaker", " rim", "conga", "bongo", "tamb", "cowbell", "909", "beat", "breaks", " kit" }))
            return "drums";

        if (has ({ "violin", "viola", "cello", "contrabass", "double bass", "string", " harp ", "fiddle", "orchestra", "ensemble" }))
            return "strings";

        if (has ({ "flute", "clarinet", "oboe", "bassoon", "piccolo", "sax", "recorder", "woodwind" }))
            return "woodwind";

        if (has ({ "trumpet", "trombone", "horn", "tuba", "brass", "cornet", "flugel" }))
            return "brass";

        if (has ({ "bass", "808" }))
            return "bass";

        if (has ({ "guitar", "gtr", "strat", "telecaster", "les paul", "riff" }))
            return "guitar";

        if (has ({ "vocal", "vox", "voice", "singer", "singing", "choir" }))
            return "vocals";

        if (has ({ "piano", "keys", "rhodes", "wurli", "organ", "clav", "harpsi", "celest", "marimba", "vibraphone", "glock",
                   "xylo", "dexed", " ep " }))
            return "keys";

        return {};
    }
}

juce::String forChannel (const Channel& c)
{
    switch (c.type)
    {
        case InstrumentType::audio:
        {
            const auto words = fromWords (c.name);
            return words.isNotEmpty() ? words : juce::String ("audio");
        }

        case InstrumentType::kit:
            return "drums";

        case InstrumentType::sampler:
        {
            if (c.sample.startsWith ("kit:"))
            {
                const auto words = fromWords (c.sample.fromFirstOccurrenceOf ("kit:", false, false) + " " + c.name);
                return words.isNotEmpty() ? words : juce::String ("drums");
            }

            const auto words = fromWords (c.name + " " + juce::File (c.sample).getFileNameWithoutExtension());
            return words.isNotEmpty() ? words : juce::String ("audio");
        }

        case InstrumentType::synth:
        {
            const auto factory = nova::findFactory (c.synthPreset);
            const auto words = fromWords ((factory != nullptr ? factory->category : juce::String()) + " " + c.synthPreset + " " + c.name);
            return words.isNotEmpty() ? words : juce::String ("synth");
        }

        case InstrumentType::instrument:
        {
            const auto words = fromWords (instruments::displayName (c.instrument) + " " + c.instrument + " " + c.name);
            return words.isNotEmpty() ? words : juce::String ("keys");
        }

        case InstrumentType::plugin:
        {
            const auto words = fromWords (c.name + " " + c.pluginId.upToLastOccurrenceOf ("-", false, false));
            return words.isNotEmpty() ? words : juce::String ("synth");
        }
    }

    return "synth";
}

juce::String forTrack (const Song& song, int row)
{
    if (juce::isPositiveAndBelow (row, song.numTracks()) && isIcon (song.tracks[(size_t) row].icon))
        return song.tracks[(size_t) row].icon;

    return automaticFor (song, row);
}

juce::String automaticFor (const Song& song, int row)
{
    if (auto c = song.findChannel (song.trackChannel (row)))
        return forChannel (*c);

    // Several channels: drums if they're all drums (a beat), else the first one's (Keys and a
    // pad: keys); FL's pattern blocks for an empty track
    const auto ids = song.channelsOnTrack (row);

    if (ids.empty())
        return "pattern";

    const bool allDrums = std::all_of (ids.begin(), ids.end(), [&song] (int id)
    {
        auto c = song.findChannel (id);
        return c != nullptr && forChannel (*c) == "drums";
    });

    auto first = song.findChannel (ids.front());
    return allDrums ? juce::String ("drums") : first != nullptr ? forChannel (*first) : juce::String ("pattern");
}

} // namespace nightshift::trackicons
