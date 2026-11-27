// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** Time-stretching and pitch-shifting a take, rendered once and played back as it is. A stretched take folder's take
    (Song::isStretched) is rendered by Signalsmith Stretch over the stretch of the song it plays in: each moment of the
    song reading the file where the folder's time map says (its speed, or its beats on the song's beats through every
    tempo change), at its pitch. Signalsmith's own seek lines its output up with the input to the sample, which the
    engine's live stretcher didn't (it came out early or late by up to a fifth of a second, more the slower it played).
    Renders are kept in a cache folder named by everything that made them, so a tempo change renders again and going
    back finds the old one. */
namespace nightshift::stretch
{
    /** One render: a file read along a time map, at a pitch. */
    struct Job
    {
        juce::File source;

        /** Where in the file (seconds) each moment of the render (seconds from its start) reads; points in order,
            straight lines between them (the first at 0, the last at its length). */
        std::vector<std::pair<double, double>> map;

        float semitones = 0.0f;

        double length() const               { return map.empty() ? 0.0 : map.back().first; }

        /** Everything that makes it what it is, as a name for its file. */
        juce::String key() const;
    };

    /** The stretchers it can render with: NightShift's (Signalsmith Stretch), or SoundTouch through the engine (for
        comparing them). */
    enum class Kind { signalsmith, soundTouch };

    /** Renders a job into `dest` (a float WAV at the source's sample rate). False if the source can't be read or the
        render was told to stop. */
    bool render (const Job&, const juce::File& dest, Kind = Kind::signalsmith, const std::atomic<bool>* stop = nullptr);

    /** The folder renders are kept in (in the system's temporary folder), and a job's file there. */
    juce::File cacheFolder();
    juce::File cachedFile (const Job&);

    /** Lets go of renders nobody has used in two weeks (when the studio opens). */
    void tidyCache();
}
