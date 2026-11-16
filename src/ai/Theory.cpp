// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Theory.h"

namespace nightshift::theory
{

namespace
{
    int pitchClassOf (const juce::String& name, int& length)
    {
        static const juce::String letters ("C D EF G A B");
        length = 0;

        if (name.isEmpty())
            return -1;

        int pc = letters.indexOfChar (juce::CharacterFunctions::toUpperCase (name[0]));

        if (pc < 0)
            return -1;

        length = 1;

        if (name.length() > 1 && (name[1] == '#' || name[1] == 'b'))
        {
            pc += name[1] == '#' ? 1 : -1;
            length = 2;
        }

        return (pc + 12) % 12;
    }
}

Chord parseChord (const juce::String& name)
{
    Chord c;
    c.name = name;
    int length = 0;
    const int pc = pitchClassOf (name, length);

    if (pc < 0)
        return c;

    c.root = pc;
    const auto rest = name.substring (length);

    if (rest.startsWith ("dim"))                                c.tones = { 0, 3, 6 };
    else if (rest.startsWith ("m") && ! rest.startsWith ("maj")) c.tones = { 0, 3, 7 };
    else if (rest == "5")                                       c.tones = { 0, 7 };

    if (rest.contains ("maj7"))                                 c.tones.push_back (11);
    else if (rest.contains ("7") || rest.contains ("9"))        c.tones.push_back (10);

    if (rest.contains ("9"))                                    c.tones.push_back (14);

    return c;
}

std::vector<int> scaleOf (const juce::String& mode)
{
    if (mode == "major")        return { 0, 2, 4, 5, 7, 9, 11 };
    if (mode == "dorian")       return { 0, 2, 3, 5, 7, 9, 10 };
    if (mode == "phrygian")     return { 0, 1, 3, 5, 7, 8, 10 };
    if (mode == "mixolydian")   return { 0, 2, 4, 5, 7, 9, 10 };
    if (mode == "lydian")       return { 0, 2, 4, 6, 7, 9, 11 };
    return { 0, 2, 3, 5, 7, 8, 10 };
}

bool Key::inScale (int pitch) const
{
    const int d = (((pitch - root) % 12) + 12) % 12;
    return std::find (scale.begin(), scale.end(), d) != scale.end();
}

int Key::step (int pitch, int direction) const
{
    for (int p = pitch + direction; std::abs (p - pitch) <= 3; p += direction)
        if (inScale (p))
            return p;

    return pitch + direction * 2;
}

Key keyFrom (const juce::String& text)
{
    Key k;
    int length = 0;

    if (const int pc = pitchClassOf (text.trim(), length); pc >= 0)
        k.root = pc;

    const auto mode = text.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (",", false, false).trim().toLowerCase();

    if (mode.isNotEmpty())
        k.mode = mode;

    k.scale = scaleOf (k.mode);
    return k;
}

std::vector<int> beatStarts (int steps, int beats, int unit)
{
    std::vector<int> groups;

    if (unit == 4 || unit == 2)
        for (int b = 0; b < beats; ++b)
            groups.push_back (16 / unit);
    else if (unit == 8 && beats % 3 == 0)
        for (int b = 0; b < beats / 3; ++b)
            groups.push_back (6);
    else
        for (int left = beats; left > 0;)
        {
            const int g = left == 3 || left == 5 ? 3 : 2;
            groups.push_back (g * 16 / unit);
            left -= g;
        }

    int total = 0;

    for (auto g : groups)
        total += g;

    std::vector<int> starts;
    int at = 0;

    for (auto g : groups)
    {
        starts.push_back (at * steps / std::max (1, total));
        at += g;
    }

    return starts;
}

std::vector<int> hitsIn (const drummer::Bar& bar, std::initializer_list<const char*> voices, bool ghostsToo)
{
    std::vector<int> hits;

    for (auto& lane : bar.lanes)
        for (auto v : voices)
            if (lane.voice == v)
                for (int i = 0; i < lane.hits.length(); ++i)
                    if (lane.hits[i] != '.' && (ghostsToo || lane.hits[i] != 'g'))
                        hits.push_back (i);

    std::sort (hits.begin(), hits.end());
    hits.erase (std::unique (hits.begin(), hits.end()), hits.end());
    return hits;
}

std::vector<int> led (const std::vector<int>& intervals, int rootPc, const std::vector<int>& previous, int centre, int lo, int hi)
{
    std::vector<int> best;
    float bestCost = 1.0e9f;
    const int n = (int) intervals.size();

    for (int inversion = 0; inversion < n; ++inversion)
        for (int base = lo - 12; base <= hi; ++base)
        {
            if (((base - rootPc - intervals[(size_t) inversion]) % 12 + 12) % 12 != 0)
                continue;

            // Stacked upwards from this inversion's bottom note, closed
            std::vector<int> v { base };

            for (int k = 1; k < n; ++k)
            {
                const int pc = (rootPc + intervals[(size_t) ((inversion + k) % n)]) % 12;
                v.push_back (atOrAbove (pc, v.back() + 1));
            }

            if (v.front() < lo || v.back() > hi)
                continue;

            float cost = 0.0f;

            if (previous.size() == v.size())
            {
                auto p = previous;
                std::sort (p.begin(), p.end());

                for (size_t k = 0; k < v.size(); ++k)
                    cost += (float) std::abs (v[k] - p[k]);
            }
            else
            {
                float mean = 0.0f;

                for (auto x : v)
                    mean += (float) x;

                cost = std::abs (mean / (float) v.size() - (float) centre) * 1.5f;
            }

            if (cost < bestCost)
            {
                bestCost = cost;
                best = v;
            }
        }

    return best;
}

juce::String kindOf (const juce::String& name)
{
    const auto n = name.toLowerCase();

    if (n == "head")        return "verse";
    if (n == "solos")       return "chorus";
    if (n == "pre-chorus")  return "prechorus";

    return n;
}

} // namespace nightshift::theory
