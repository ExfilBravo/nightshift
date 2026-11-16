// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "MasteringEngineer.h"
#include "Request.h"

namespace nightshift::engineer
{

namespace
{
    juce::String spaced (const juce::String& words)
    {
        auto t = words.toLowerCase().replace ("'", "");

        for (const char* p = ",.!?;:()\"/"; *p != 0; ++p)
            t = t.replaceCharacter ((juce::juce_wchar) *p, ' ');

        return " " + t + " ";
    }

    bool has (const juce::String& text, std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (text.contains (" " + juce::String (w) + " "))
                return true;

        return false;
    }

    juce::String db (float v, int places = 1)
    {
        const auto s = juce::String (std::abs (v), places).replace (".0", "");
        return (v > 0.0f ? "+" : v < 0.0f ? "-" : "") + s;
    }

    float roundHalf (float v)       { return std::round (v * 2.0f) / 2.0f; }

    const char* const targetOrder[] { "safe", "apple", "streaming", "loud" };

    int targetStep (const juce::String& id)
    {
        for (int i = 0; i < 4; ++i)
            if (id == targetOrder[i])
                return i;

        return 2;
    }

    juce::String targetWords (const juce::String& id)
    {
        if (const auto* t = mastering::findTarget (id))
            return t->lufs != 0.0f ? juce::String (t->name) + " (" + juce::String (juce::roundToInt (t->lufs)) + " LUFS)"
                                   : juce::String ("no loudness target, only kept from clipping");

        return id;
    }

    // Each band's word for too much and too little
    const char* const tooMuch[] { "heavy", "thick", "forward", "bright" };
    const char* const tooLittle[] { "light", "thin", "soft", "dull" };
    const char* const bandWords[] { "the low end", "the body", "the presence", "the air" };
}

float tolerance (int band)
{
    // How far a band may be from the style's balance before it's moved: masters differ, and the
    // very top most of all
    static const float window[] { 3.0f, 2.5f, 2.5f, 4.0f };
    return window[juce::jlimit (0, 3, band)];
}

const char* bandName (int band)
{
    static const char* const names[] { "Bass", "Body", "Presence", "Air" };
    return names[juce::jlimit (0, 3, band)];
}

//==============================================================================
Hearing hear (const juce::AudioBuffer<float>& mix, double rate)
{
    Hearing h;
    const int n = mix.getNumSamples();

    if (n < (int) (rate * 0.5) || mix.getNumChannels() == 0)
        return h;

    const float* l = mix.getReadPointer (0);
    const float* r = mix.getReadPointer (mix.getNumChannels() > 1 ? 1 : 0);

    // How loud, as the meter has it
    {
        constexpr int chunk = 8192;
        tracktion::LoudnessMeter meter;
        meter.prepare (rate, 2, chunk);

        for (int pos = 0; pos < n; pos += chunk)
        {
            const float* ch[2] = { l + pos, r + pos };
            meter.process (ch, 2, std::min (chunk, n - pos));
        }

        meter.flush();
        const auto rd = meter.getReadings();

        if (! rd.integratedValid)
            return h;

        h.lufs = rd.integratedLufs;
        h.truePeakDb = rd.truePeakDb;
        h.rangeLu = rd.loudnessRangeLu;
        h.peakToLoudness = rd.truePeakDb - rd.integratedLufs;
    }

    // Its tone and width: the long-term spectrum of the centre and the sides, over the parts that play
    constexpr int order = 12, size = 1 << order, hop = size / 2;
    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) size), a ((size_t) size * 2), b ((size_t) size * 2);
    std::vector<double> mid ((size_t) size / 2 + 1), side ((size_t) size / 2 + 1);

    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);

    for (int start = 0; start + size <= n; start += hop)
    {
        double energy = 0.0;

        for (int i = 0; i < size; ++i)
            energy += (double) l[start + i] * l[start + i] + (double) r[start + i] * r[start + i];

        if (energy / (2.0 * size) < 1.0e-6)      // quieter than -60 dB: a gap, not the song
            continue;

        std::fill (a.begin(), a.end(), 0.0f);
        std::fill (b.begin(), b.end(), 0.0f);

        for (int i = 0; i < size; ++i)
        {
            a[(size_t) i] = 0.5f * (l[start + i] + r[start + i]) * window[(size_t) i];
            b[(size_t) i] = 0.5f * (l[start + i] - r[start + i]) * window[(size_t) i];
        }

        fft.performFrequencyOnlyForwardTransform (a.data());
        fft.performFrequencyOnlyForwardTransform (b.data());

        for (size_t k = 0; k < mid.size(); ++k)
        {
            mid[k] += (double) a[k] * a[k];
            side[k] += (double) b[k] * b[k];
        }
    }

    double lr = 0.0, ll = 0.0, rr = 0.0;

    for (int i = 0; i < n; ++i)
    {
        lr += (double) l[i] * r[i];
        ll += (double) l[i] * l[i];
        rr += (double) r[i] * r[i];
    }

    h.correlation = ll > 0.0 && rr > 0.0 ? (float) (lr / std::sqrt (ll * rr)) : 1.0f;

    const double binHz = rate / size;

    auto sum = [&] (const std::vector<double>& p, double from, double to)
    {
        double s = 0.0;

        for (size_t k = 1; k < p.size(); ++k)
            if (const double f = (double) k * binHz; f >= from && f < to)
                s += p[k];

        return s;
    };

    // Per octave, both centre and sides: the tone of everything that plays
    auto perOctave = [&] (double from, double to)
    {
        return (sum (mid, from, to) + sum (side, from, to)) / std::log2 (to / from) + 1.0e-20;
    };

    const double mids = perOctave (500.0, 2000.0);
    const double edges[4][2] { { 40.0, 120.0 }, { 200.0, 500.0 }, { 2000.0, 5000.0 }, { 8000.0, std::min (16000.0, rate * 0.45) } };

    for (int i = 0; i < 4; ++i)
    {
        const double band = perOctave (edges[i][0], edges[i][1]);
        h.bands[(size_t) i] = (float) (10.0 * std::log10 (band / mids));
        h.levels[(size_t) i] = (float) (10.0 * std::log10 (band / size));
    }

    h.sidesDb = (float) (10.0 * std::log10 ((sum (side, 30.0, 16000.0) + 1.0e-20) / (sum (mid, 30.0, 16000.0) + 1.0e-20)));
    h.lowSidesDb = (float) (10.0 * std::log10 ((sum (side, 30.0, 150.0) + 1.0e-20) / (sum (mid, 30.0, 150.0) + 1.0e-20)));
    h.seconds = n / rate;
    h.valid = true;
    return h;
}

Hearing hearShaped (const juce::AudioBuffer<float>& mix, double rate, const Song::Mastering& settings)
{
    juce::AudioBuffer<float> shaped (2, mix.getNumSamples());
    shaped.copyFrom (0, 0, mix, 0, 0, mix.getNumSamples());
    shaped.copyFrom (1, 0, mix, mix.getNumChannels() > 1 ? 1 : 0, 0, mix.getNumSamples());

    mastering::Shaper shaper;
    shaper.prepare (rate);
    shaper.set (settings);
    constexpr int chunk = 4096;

    for (int pos = 0; pos < shaped.getNumSamples(); pos += chunk)
        shaper.process (shaped.getWritePointer (0, pos), shaped.getWritePointer (1, pos), std::min (chunk, shaped.getNumSamples() - pos));

    return hear (shaped, rate);
}

//==============================================================================
const Profile& profileFor (const juce::String& style)
{
    // About how a modern master's long-term spectrum slopes: per octave against the mids, the low
    // end above, the top below (between the 4.5 dB an octave analysers are tilted by and the
    // steeper fall of the very top that surveys of released music find); each style's own lean
    // on top of that. Where it's unsure (the air most), tolerance() leaves room
    static const std::array<float, 4> neutral { 7.0f, 3.5f, -4.0f, -9.0f };

    struct Row { const char* style; const char* target; std::array<float, 4> lean; float punch; std::array<float, 3> shares; float width, mono; const char* sound; };
    static const Row rows[] {
        { "metalcore", "loud",      {  0.0f, -1.5f,  1.5f,  1.0f }, 0.45f, { 1.2f, 0.8f, 1.0f }, 1.15f, 120.0f, "dense and bright, the kick tight and the low mids clean so the guitars and the breakdown hit hard" },
        { "deathcore", "loud",      {  1.0f, -1.0f,  1.0f,  0.0f }, 0.50f, { 1.2f, 0.8f, 0.9f }, 1.10f, 120.0f, "heavy and dense, the low end huge but tight, the low mids clean" },
        { "djent",     "loud",      {  0.0f, -1.5f,  2.0f,  1.5f }, 0.40f, { 1.2f, 0.8f, 1.0f }, 1.15f, 120.0f, "tight and clear, the chugs' attack forward, wide and airy above it" },
        { "thrash",    "loud",      { -1.0f, -1.0f,  2.0f,  1.0f }, 0.45f, { 1.1f, 0.9f, 1.0f }, 1.05f, 120.0f, "fast and cutting, a tight low end and the guitars' bite forward" },
        { "blast",     "loud",      { -1.0f, -0.5f,  1.5f,  1.5f }, 0.45f, { 1.1f, 0.9f, 1.0f }, 1.10f, 120.0f, "a wall of sound, the lows tight so the blasts stay clear, bright above" },
        { "metal",     "loud",      {  0.0f, -1.0f,  1.0f,  0.5f }, 0.40f, { 1.1f, 0.9f, 1.0f }, 1.10f, 120.0f, "full and powerful, the low mids clean" },
        { "trap",      "loud",      {  4.0f, -1.5f, -0.5f,  1.5f }, 0.35f, { 0.6f, 1.0f, 1.2f }, 1.10f, 150.0f, "the 808 big and in the middle, the low mids cleared for it, the hats crisp" },
        { "drill",     "loud",      {  4.0f, -1.0f,  0.0f,  1.0f }, 0.35f, { 0.6f, 1.0f, 1.2f }, 1.10f, 150.0f, "the sliding 808 big and centred, dark but crisp above" },
        { "boombap",   "streaming", {  2.0f,  1.0f, -0.5f, -2.0f }, 0.40f, { 1.0f, 1.1f, 0.8f }, 0.95f, 120.0f, "dusty and warm, the drums knocking, the top rolled off a little" },
        { "hiphop",    "loud",      {  3.0f,  0.0f,  0.0f,  0.5f }, 0.35f, { 0.8f, 1.0f, 1.1f }, 1.05f, 150.0f, "the low end big and centred, the vocal range clear" },
        { "rock",      "streaming", {  0.5f,  0.0f,  0.5f,  0.0f }, 0.35f, { 1.0f, 1.0f, 1.0f }, 1.05f, 100.0f, "natural and full, the guitars and the drums clear" },
        { "hardrock",  "loud",      {  0.5f,  0.5f,  1.0f,  0.0f }, 0.40f, { 1.0f, 1.0f, 1.0f }, 1.05f, 100.0f, "big and warm, the guitars forward" },
        { "poppunk",   "loud",      {  0.0f, -1.0f,  1.5f,  1.0f }, 0.45f, { 1.1f, 0.9f, 1.0f }, 1.15f, 100.0f, "bright, wide and energetic, the guitars a wall" },
        { "punk",      "loud",      { -0.5f,  0.0f,  1.5f,  0.0f }, 0.45f, { 1.0f, 1.0f, 1.0f }, 1.00f, 100.0f, "raw and in your face, the mids forward" },
        { "lofi",      "streaming", {  2.0f,  1.5f, -2.0f, -4.0f }, 0.25f, { 0.8f, 1.2f, 0.6f }, 0.95f, 0.0f,   "warm and dark, soft at the top, glued rather than loud" },
        { "jazz",      "apple",     {  0.0f,  1.0f, -0.5f, -0.5f }, 0.10f, { 1.0f, 1.0f, 1.0f }, 1.00f, 0.0f,   "natural and open, left to breathe" },
        { "ballad",    "apple",     {  0.0f,  1.0f, -1.0f, -0.5f }, 0.05f, { 1.0f, 1.0f, 1.0f }, 1.00f, 0.0f,   "soft and natural, all of its quiet and loud kept" },
        { "neosoul",   "streaming", {  2.5f,  1.0f, -1.0f, -0.5f }, 0.25f, { 0.9f, 1.1f, 0.8f }, 1.05f, 100.0f, "warm and round, the bass deep, smooth at the top" },
        { "funk",      "streaming", {  1.5f, -0.5f,  1.0f,  0.5f }, 0.30f, { 1.2f, 1.0f, 0.9f }, 1.05f, 100.0f, "punchy and tight, the bass and the kick locked, the guitars bright" },
        { "house",     "loud",      {  3.0f, -1.0f,  0.5f,  1.0f }, 0.35f, { 0.8f, 1.0f, 1.1f }, 1.10f, 120.0f, "the kick and the bass big and locked, the low mids clean, open on top" },
        { "techno",    "loud",      {  3.0f, -1.5f,  0.5f,  0.5f }, 0.40f, { 0.8f, 1.0f, 1.1f }, 1.10f, 120.0f, "a driving kick over a deep, tight low end, the top crisp" },
        { "edm",       "loud",      {  2.5f, -1.5f,  1.0f,  1.5f }, 0.35f, { 0.8f, 1.0f, 1.2f }, 1.20f, 120.0f, "huge and bright, the drop wide, the kick and the bass punching through" },
        { "dnb",       "loud",      {  3.0f, -1.0f,  0.5f,  1.0f }, 0.45f, { 0.8f, 1.0f, 1.1f }, 1.10f, 120.0f, "a deep sub under fast, snappy breaks, crisp on top" },
        { "dubstep",   "loud",      {  4.0f, -1.0f,  0.5f,  0.5f }, 0.40f, { 0.7f, 1.0f, 1.1f }, 1.10f, 150.0f, "a massive, centred low end, the drop's growl forward, the snare cracking" },
        { "synthwave", "streaming", {  1.5f,  0.5f,  0.0f,  0.5f }, 0.30f, { 1.0f, 1.0f, 1.0f }, 1.15f, 100.0f, "warm and wide, an 80s shine on top, the snare big" },
        { "reggae",    "streaming", {  3.0f,  0.5f, -0.5f, -1.0f }, 0.20f, { 0.8f, 1.1f, 0.9f }, 1.00f, 120.0f, "the bass deep and warm, the skank and the drums clear, left to breathe" },
        { "reggaeton", "loud",      {  3.5f, -1.0f,  0.0f,  1.0f }, 0.35f, { 0.7f, 1.0f, 1.1f }, 1.05f, 150.0f, "the dembow punchy, the bass big and centred, crisp on top" },
        { "afrobeats", "streaming", {  2.5f,  0.0f,  0.0f,  0.5f }, 0.30f, { 0.9f, 1.0f, 1.0f }, 1.10f, 120.0f, "warm and bouncy, the low end round, the percussion crisp" },
        { "bossa",     "apple",     {  0.0f,  1.0f, -0.5f, -0.5f }, 0.10f, { 1.0f, 1.0f, 1.0f }, 1.00f, 0.0f,   "intimate and natural, soft and open" },
        { "ambient",   "apple",     {  0.5f,  0.5f, -1.0f,  0.0f }, 0.05f, { 1.0f, 1.0f, 1.0f }, 1.20f, 0.0f,   "spacious and soft, wide, all of its quiet kept" },
        { "cinematic", "streaming", {  2.0f,  0.0f,  0.0f,  0.5f }, 0.15f, { 1.0f, 1.0f, 1.0f }, 1.15f, 100.0f, "big and wide, the low end deep, its quiet and loud parts kept" },
        { "country",   "streaming", {  0.0f,  0.5f,  1.0f,  0.5f }, 0.30f, { 1.0f, 1.0f, 1.0f }, 1.05f, 100.0f, "warm and clear, the guitars and the piano bright" },
        { "",          "streaming", {  0.0f,  0.0f,  0.0f,  0.0f }, 0.25f, { 1.0f, 1.0f, 1.0f }, 1.00f, 100.0f, "balanced, nothing leaning" },
    };

    static const std::vector<Profile> profiles = []
    {
        std::vector<Profile> list;

        for (auto& row : rows)
        {
            Profile p { row.style, row.target, {}, row.punch, row.shares, row.width, row.mono, row.sound };

            for (size_t i = 0; i < 4; ++i)
                p.tone[i] = neutral[i] + row.lean[i];

            list.push_back (p);
        }

        return list;
    }();

    for (auto& p : profiles)
        if (style == p.style)
            return p;

    return profiles.back();
}

//==============================================================================
bool Ask::movesOnly() const
{
    return style.isEmpty() && ! again
             && (target.isNotEmpty() || louder != 0 || mono || punch != 0.0f || width != 0.0f
                  || std::any_of (tone.begin(), tone.end(), [] (float v) { return v != 0.0f; }));
}

Ask askOf (const juce::String& words)
{
    Ask a;
    const auto t = spaced (words);
    a.style = agents::Request::parse (words).style;

    if (a.style.isNotEmpty())
        if (const auto* s = agents::findStyle (a.style))
            a.understood.add (juce::String (s->name).toLowerCase());

    // Where it's going: the loudness it aims for
    if (has (t, { "keep it from clipping", "no loudness", "as mixed", "dont make it louder", "not louder", "no louder" }))
        a.target = "safe";
    else if (has (t, { "apple", "apple music", "itunes", "podcast", "podcasts" }))
        a.target = "apple";
    else if (has (t, { "spotify", "streaming", "stream", "youtube", "tidal", "amazon", "deezer" }))
        a.target = "streaming";
    else if (has (t, { "loud", "club", "clubs", "soundcloud", "beat store", "beatstars", "dj" }))
        a.target = "loud";

    if (a.target.isNotEmpty())
        a.understood.add ("for " + targetWords (a.target));

    if (has (t, { "louder", "more loud", "hotter", "crank it", "pump it up" }))
    {
        a.louder = 1;
        a.understood.add ("louder");
    }
    else if (has (t, { "quieter", "softer", "less loud", "not so loud", "more dynamic", "dynamic", "breathe", "more dynamics" }))
    {
        a.louder = -1;
        a.punch -= 0.1f;
        a.understood.add ("more dynamic");
    }

    auto move = [&a] (int band, float by) { a.tone[(size_t) band] += by; };

    if (has (t, { "warm", "warmer", "warmth" }))                                     { move (1, 1.0f); move (2, -1.0f); move (3, -1.0f); a.understood.add ("warmer"); }
    if (has (t, { "dark", "darker", "less bright", "duller" }))                      { move (2, -1.0f); move (3, -1.5f); a.understood.add ("darker"); }
    if (has (t, { "bright", "brighter", "more air", "airy", "airier", "crisp", "crisper", "sparkle", "shine", "clearer top" }))
                                                                                     { move (2, 1.0f); move (3, 1.5f); a.understood.add ("brighter"); }
    if (has (t, { "harsh", "less harsh", "too harsh", "sibilant", "piercing", "fizzy", "shrill" }))
                                                                                     { move (2, -1.5f); move (3, -0.5f); a.understood.add ("less harsh"); }
    if (has (t, { "muddy", "mud", "less mud", "clearer", "boxy", "cleaner", "clean it up" }))
                                                                                     { move (1, -1.5f); a.understood.add ("less mud"); }

    if (has (t, { "less bass", "less low end", "boomy", "too much bass", "thinner", "less 808", "less sub" }))
                                                                                     { move (0, -1.5f); a.understood.add ("less bass"); }
    else if (has (t, { "more bass", "more low end", "heavier", "bigger", "fatter", "more 808", "more sub", "thicker", "bassier" }))
                                                                                     { move (0, 1.5f); a.understood.add ("more bass"); }

    if (has (t, { "less punch", "less squashed", "squashed", "less compressed", "more open", "more natural", "natural" }))
                                                                                     { a.punch -= 0.15f; a.understood.add ("less squeezed"); }
    else if (has (t, { "punch", "punchy", "punchier", "more punch", "tighter", "tight", "glue", "more glue", "glued" }))
                                                                                     { a.punch += 0.15f; a.understood.add ("more punch"); }

    if (has (t, { "narrower", "less wide", "more mono", "mono", "more focused", "centred", "centered", "too wide" }))
    {
        a.width -= 0.15f;
        a.mono = true;
        a.understood.add ("narrower");
    }
    else if (has (t, { "wider", "more width", "wide", "more stereo", "bigger stereo" }))
    {
        a.width += 0.15f;
        a.understood.add ("wider");
    }

    a.again = has (t, { "again", "start over", "redo", "from scratch", "afresh", "fresh" });
    return a;
}

//==============================================================================
juce::var Result::answer() const
{
    auto bands = [] (const std::array<float, 4>& b)
    {
        return juce::Array<juce::var> { b[0], b[1], b[2], b[3] };
    };

    auto* o = new juce::DynamicObject();
    o->setProperty ("offline", true);
    o->setProperty ("style", style);
    o->setProperty ("target", settings.target);

    auto* b = new juce::DynamicObject();
    b->setProperty ("lufs", before.lufs);
    b->setProperty ("peak", before.truePeakDb);
    b->setProperty ("bands", bands (before.bands));
    b->setProperty ("sides", before.sidesDb);
    o->setProperty ("before", juce::var (b));

    auto* a = new juce::DynamicObject();
    a->setProperty ("lufs", afterLufs);
    a->setProperty ("peak", afterPeakDb);
    a->setProperty ("bands", bands (after.bands));
    a->setProperty ("squeeze", squeezeDb);
    o->setProperty ("after", juce::var (a));

    o->setProperty ("wanted", bands (wanted));
    o->setProperty ("compare_db", compareDb);
    return juce::var (o);
}

Song::MasterTurn turnFor (const juce::String& prompt, const Result& r)
{
    return { prompt.trim(), r.reply, juce::JSON::toString (r.settings.toVar(), true), juce::JSON::toString (r.answer(), true) };
}

//==============================================================================
namespace
{
    struct Decision
    {
        Song::Mastering settings;
        std::array<float, 4> intended {};   // how far it means each tone band to move
        juce::StringArray tone, notes;      // what it did, in words
    };

    Decision decide (const Ask& ask, const Hearing& h, const Profile& p, const Song::Mastering* previous)
    {
        Decision d;
        auto& m = d.settings;
        const bool followUp = previous != nullptr && ask.movesOnly();

        if (followUp)
        {
            m = *previous;

            for (size_t i = 0; i < 4; ++i)
                if (ask.tone[i] != 0.0f)
                {
                    const float before = m.toneDb[i];
                    m.toneDb[i] = juce::jlimit (-6.0f, 6.0f, roundHalf (m.toneDb[i] + ask.tone[i]));
                    d.intended[i] = m.toneDb[i] - before;
                }
        }
        else
        {
            m = {};
            m.ceilingDb = previous != nullptr ? previous->ceilingDb : -1.0f;
            m.target = p.target;

            // Tone: where a band is further from the style's balance than masters differ, most of
            // the way back to the edge of that; no more than 3 dB
            for (size_t i = 0; i < 4; ++i)
            {
                const float off = p.tone[i] - h.bands[i];
                const float excess = std::abs (off) - tolerance ((int) i);
                const float move = excess > 0.0f ? juce::jlimit (-3.0f, 3.0f, roundHalf (std::copysign (excess * 0.7f + 0.5f, off))) : 0.0f;
                m.toneDb[i] = juce::jlimit (-6.0f, 6.0f, roundHalf (move + ask.tone[i]));
                d.intended[i] = m.toneDb[i];
            }

            // Punch: the style's, more when the peaks have lots of room, less when it's dense already
            float punch = p.punch;

            if (h.peakToLoudness > 18.0f)
            {
                punch += 0.1f;
                d.notes.add ("lots of room between its peaks and its body, so Punch works a little harder");
            }
            else if (h.peakToLoudness < 11.0f)
            {
                punch -= 0.2f;
                d.notes.add ("it's dense already, so Punch goes easy");
            }

            m.punchAmount = punch;
            m.punchShare = p.shares;

            // Width: the style's, minus what the mix has already; the bass in the middle where the
            // style wants it, or where it wanders
            float width = p.width;
            m.monoBassHz = p.monoBassHz;

            if (h.sidesDb > -6.0f)
            {
                width = std::min (width, 1.0f) - (h.sidesDb > -3.0f ? 0.1f : 0.0f);
                d.notes.add ("it's wide already, so Width leaves it be");
            }
            else if (h.sidesDb < -20.0f)
            {
                width += 0.1f;
                d.notes.add ("it's nearly mono, so Width opens it up a little");
            }

            if (h.correlation < 0.2f)
            {
                width = std::min (width, 0.9f);
                m.monoBassHz = std::max (m.monoBassHz, 120.0f);
                d.notes.add ("parts of it cancel when played in mono (on a phone), so it's narrowed a little");
            }

            if (h.lowSidesDb > -12.0f && m.monoBassHz < 100.0f)
            {
                m.monoBassHz = 120.0f;
                d.notes.add ("the bass wanders to the sides, so it's kept in the middle");
            }

            m.width = width;

            // Where a band is boosted: the bass in the centre (it stays mono), the air on the sides
            // for the wide styles
            m.toneWhere = { 0, 0, 0, 0 };

            if (m.toneDb[0] > 0.0f && m.monoBassHz > 0.0f)
                m.toneWhere[0] = 1;

            if (m.toneDb[3] > 0.0f && p.width > 1.05f)
                m.toneWhere[3] = 2;
        }

        // The words' own moves on top
        m.punchAmount = juce::jlimit (0.0f, 1.0f, std::round ((m.punchAmount + ask.punch) * 20.0f) / 20.0f);
        m.width = juce::jlimit (0.0f, 2.0f, std::round ((m.width + ask.width) * 20.0f) / 20.0f);

        if (ask.mono)
            m.monoBassHz = std::max (m.monoBassHz, 120.0f);

        if (ask.target.isNotEmpty())
            m.target = ask.target;

        if (ask.louder != 0)
        {
            const int step = targetStep (m.target), next = juce::jlimit (0, 3, step + ask.louder);

            if (next == step && ask.louder > 0)
            {
                m.punchAmount = juce::jlimit (0.0f, 1.0f, m.punchAmount + 0.1f);
                d.notes.add ("it's at Loud already, the loudest target, so it's made denser with Punch rather than squashed harder");
            }

            m.target = targetOrder[next];
        }

        m.toneOn = m.punchOn = m.widthOn = true;

        for (size_t i = 0; i < 4; ++i)
            if (m.toneDb[i] != 0.0f && (! followUp || d.intended[i] != 0.0f))
                d.tone.add (juce::String (bandName ((int) i)) + " " + db (followUp ? d.intended[i] : m.toneDb[i]) + " dB"
                              + (m.toneWhere[i] == 1 ? " in the centre" : m.toneWhere[i] == 2 ? " on the sides" : ""));

        return d;
    }

    Result run (const juce::String& words, const juce::AudioBuffer<float>& mix, double rate, const juce::File& capture,
                const juce::String& songStyle, const Song::Mastering* previous, const std::function<bool (float)>& progress)
    {
        Result r;
        auto going = [&progress] (float p) { return progress == nullptr || progress (p); };
        const auto ask = askOf (words);

        r.before = hear (mix, rate);

        if (! r.before.valid)
        {
            r.problem = "The song is silent, so there's nothing to master.";
            return r;
        }

        if (! going (0.12f))
        {
            r.problem = "Stopped.";
            return r;
        }

        r.style = ask.style.isNotEmpty() ? ask.style : songStyle;
        const auto& p = profileFor (r.style);
        r.wanted = p.tone;
        auto d = decide (ask, r.before, p, previous);

        // It checks its tone by hearing the mix through it - the tone alone, Punch and Width left out
        // so only the moves are heard - and corrects a band that moved more or less than it meant
        auto toneOnly = [] (Song::Mastering m)
        {
            m.punchAmount = 0.0f;
            m.width = 1.0f;
            m.monoBassHz = 0.0f;
            return m;
        };

        const auto toned = hearShaped (mix, rate, toneOnly (d.settings));
        const auto from = previous != nullptr && ask.movesOnly() ? hearShaped (mix, rate, toneOnly (*previous)) : r.before;

        if (! going (0.25f))
        {
            r.problem = "Stopped.";
            return r;
        }

        bool corrected = false;

        for (size_t i = 0; i < 4; ++i)
            if (d.intended[i] != 0.0f)
            {
                const float moved = toned.levels[i] - from.levels[i];
                const float missed = d.intended[i] - moved;

                if (std::abs (missed) > 0.5f && std::abs (moved) < std::abs (d.intended[i]) * 3.0f)
                {
                    d.settings.toneDb[i] = juce::jlimit (-6.0f, 6.0f, roundHalf (d.settings.toneDb[i] + missed * 0.8f));
                    corrected = true;
                }
            }

        // Boosts at the top stay gentle (more turns harsh), whatever the check says
        for (size_t i : { (size_t) 2, (size_t) 3 })
            if (d.settings.toneDb[i] > 3.0f)
            {
                d.settings.toneDb[i] = 3.0f;
                corrected = true;
            }

        r.after = hearShaped (mix, rate, d.settings);

        if (! going (0.38f))
        {
            r.problem = "Stopped.";
            return r;
        }

        // The loudness: pushed into the limiter until it lands on the target
        r.settings = d.settings;
        const auto* target = mastering::findTarget (r.settings.target);

        juce::String short_;

        if (target != nullptr && target->lufs != 0.0f)
        {
            // A song with quiet parts and loud ones (a wide loudness range: Sleep Token's piano verses
            // and half-time choruses) keeps them: the limiter squeezes its loud parts 10 dB at most.
            // Past that, a loudness target is only met by lifting the quiet parts up to the loud ones
            // (measured: a verse 14 dB under its chorus came out louder than it)
            const bool dynamic = r.before.rangeLu > 8.0f;
            constexpr float dynamicSqueezeDb = 10.0f;

            auto pushFor = [&] (const Song::Mastering& m, float from, float span)
            {
                return mastering::findPush (capture, target->lufs, m.ceilingDb, [&progress, from, span] (float f)
                {
                    return progress == nullptr || progress (from + span * f);
                }, &m, dynamic ? dynamicSqueezeDb : 100.0f);
            };

            auto push = pushFor (r.settings, 0.38f, 0.3f);

            if (! push.ok)
            {
                r.problem = push.problem;
                return r;
            }

            // Short of it: the limiter is holding the peaks down so hard that pushing on makes it
            // quieter. As an engineer would, it tries again denser (more Punch, so the peaks sit
            // nearer the body) and with the low end boosted less (bass eats the room). Not when it's
            // holding back for the song's quiet parts: denser would flatten them too
            if (push.afterLufs < target->lufs - 0.5f && ! push.held && ! (previous != nullptr && ask.movesOnly()))
            {
                auto denser = r.settings;
                denser.punchAmount = juce::jlimit (0.0f, 0.8f, denser.punchAmount + 0.2f);

                for (size_t i : { (size_t) 0, (size_t) 1 })
                    if (denser.toneDb[i] > 0.0f)
                        denser.toneDb[i] = roundHalf (denser.toneDb[i] * 0.5f);

                const auto second = pushFor (denser, 0.68f, 0.3f);

                if (second.ok && second.afterLufs > push.afterLufs + 0.2f)
                {
                    d.notes.add ("to get it loud it's made denser (Punch up) and the low end boosted less");

                    r.settings = denser;
                    push = second;
                    r.after = hearShaped (mix, rate, r.settings);
                }
            }

            if (push.held && push.afterLufs < target->lufs - 0.5f)
                short_ = " It stops at " + juce::String (push.afterLufs, 1) + " LUFS rather than " + juce::String (juce::roundToInt (target->lufs))
                         + " so the quiet parts stay quiet: any louder and the limiter would squeeze the loud parts more than "
                         + juce::String (juce::roundToInt (dynamicSqueezeDb)) + " dB and lift the quiet ones up to them.";
            else if (push.afterLufs < target->lufs - 0.5f)
                short_ = " It stops at " + juce::String (push.afterLufs, 1) + " LUFS rather than " + juce::String (juce::roundToInt (target->lufs))
                         + ": past that the limiter has to hold the peaks down so hard it gets quieter, not louder. The mix's peaks stand "
                         + juce::String (juce::roundToInt (r.before.peakToLoudness)) + " dB over its body; more compression on the loudest parts"
                         + " (the drums, usually) would let it go further.";

            r.settings.pushDb = push.pushDb;
            r.afterLufs = push.afterLufs;
            r.afterPeakDb = push.truePeakDb;
            r.squeezeDb = push.squeezeDb;
        }
        else
        {
            r.settings.pushDb = 0.0f;
            r.afterLufs = r.after.lufs;
            r.afterPeakDb = std::min (r.after.truePeakDb, r.settings.ceilingDb);
        }

        r.compareDb = std::min (0.0f, r.before.lufs - r.afterLufs);

        // What it did, in words (the tone as it ended up, after checking and any second try)
        if (! (previous != nullptr && ask.movesOnly()))
        {
            d.tone.clear();

            for (size_t i = 0; i < 4; ++i)
                if (r.settings.toneDb[i] != 0.0f)
                    d.tone.add (juce::String (bandName ((int) i)) + " " + db (r.settings.toneDb[i]) + " dB"
                                  + (r.settings.toneWhere[i] == 1 ? " in the centre" : r.settings.toneWhere[i] == 2 ? " on the sides" : ""));
        }

        const auto* style = agents::findStyle (r.style);
        const juce::String styleName = style != nullptr ? juce::String (style->name) : juce::String();
        const bool followUp = previous != nullptr && ask.movesOnly();
        juce::String reply;

        if (followUp)
        {
            reply << juce::String (ask.understood.joinIntoString (", ")).substring (0, 1).toUpperCase()
                  << ask.understood.joinIntoString (", ").substring (1) << ": "
                  << (d.tone.isEmpty() ? juce::String ("the tone as it was") : "Tone " + d.tone.joinIntoString (", "))
                  << ". Punch " << juce::roundToInt (r.settings.punchAmount * 100.0f) << "%, Width " << juce::roundToInt (r.settings.width * 100.0f) << "%"
                  << (r.settings.monoBassHz > 0.0f ? ", the bass under " + juce::String (juce::roundToInt (r.settings.monoBassHz)) + " Hz in the middle" : juce::String()) << ". ";
        }
        else
        {
            if (styleName.isNotEmpty())
                reply << "Mastered as " << styleName.toLowerCase() << ", for " << targetWords (r.settings.target) << ": " << p.sound << ". ";
            else
                reply << "Mastered for " << targetWords (r.settings.target) << ", balanced: no style named (and no AI drummer to go by), so nothing leaning. ";

            // What it heard
            juce::StringArray heard;

            for (int i = 0; i < 4; ++i)
            {
                const float off = r.before.bands[(size_t) i] - p.tone[(size_t) i];

                if (std::abs (off) > tolerance (i))
                    heard.add (juce::String (bandWords[i]) + " " + juce::String (juce::roundToInt (std::abs (off))) + " dB " + (off > 0.0f ? tooMuch[i] : tooLittle[i]));
            }

            reply << "It heard " << juce::String (r.before.lufs, 1) << " LUFS, peaks at " << juce::String (r.before.truePeakDb, 1) << " dB; "
                  << (heard.isEmpty() ? juce::String ("its tone close to a ") + (styleName.isNotEmpty() ? styleName.toLowerCase() : juce::String ("balanced")) + " master's"
                                      : "against a " + (styleName.isNotEmpty() ? styleName.toLowerCase() : juce::String ("balanced")) + " master, " + heard.joinIntoString (", "))
                  << (d.notes.isEmpty() ? juce::String() : "; " + d.notes.joinIntoString ("; ")) << ". ";

            reply << (d.tone.isEmpty() ? juce::String ("Tone left as it is") : "Tone: " + d.tone.joinIntoString (", ")) << ". "
                  << "Punch " << juce::roundToInt (r.settings.punchAmount * 100.0f) << "%"
                  << (r.settings.punchShare[0] > 1.05f ? ", most on the lows" : r.settings.punchShare[2] > 1.05f ? ", most on the highs" : r.settings.punchShare[1] > 1.05f ? ", most on the mids" : "")
                  << ". Width " << juce::roundToInt (r.settings.width * 100.0f) << "%"
                  << (r.settings.monoBassHz > 0.0f ? ", the bass under " + juce::String (juce::roundToInt (r.settings.monoBassHz)) + " Hz kept in the middle" : juce::String()) << ". ";
        }

        if (target != nullptr && target->lufs != 0.0f)
            reply << (std::abs (r.settings.pushDb) < 0.05f ? juce::String ("No push needed into the limiter")
                      : r.settings.pushDb > 0.0f ? "Pushed " + db (r.settings.pushDb) + " dB into the limiter"
                                                 : "Turned down " + juce::String (-r.settings.pushDb, 1).replace (".0", "") + " dB into the limiter, the mix being louder than the target")
                  << ", squeezing at most " << juce::String (r.squeezeDb, 1) << " dB: it comes out at "
                  << juce::String (r.afterLufs, 1) << " LUFS, peaks " << juce::String (r.afterPeakDb, 1) << " dB.";
        else
            reply << "No push: only kept from clipping, at " << juce::String (r.afterLufs, 1) << " LUFS.";

        reply << short_;

        if (! followUp)
            reply << " Before and After play it at the same loudness, so you hear what changed.";

        r.reply = reply;
        r.ok = true;

        if (progress != nullptr)
            progress (1.0f);

        return r;
    }
}

Result master (const juce::String& words, const juce::File& rawCapture, const juce::String& songStyle,
               const Song::Mastering* previous, const std::function<bool (float)>& progress)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (rawCapture));

    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        Result r;
        r.problem = "NightShift couldn't read the song back to listen to it.";
        return r;
    }

    juce::AudioBuffer<float> mix (2, (int) reader->lengthInSamples);
    reader->read (&mix, 0, (int) reader->lengthInSamples, 0, true, true);

    if (reader->numChannels < 2)
        mix.copyFrom (1, 0, mix, 0, 0, mix.getNumSamples());

    return run (words, mix, reader->sampleRate, rawCapture, songStyle, previous, progress);
}

Result master (const juce::String& words, const juce::AudioBuffer<float>& mix, double sampleRate, const juce::String& songStyle,
               const Song::Mastering* previous, const std::function<bool (float)>& progress)
{
    // The push is worked out from a file: this mix, written as one
    const auto file = juce::File::createTempFile (".wav");

    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                          .withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (32)
                                                                          .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));

        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (mix, 0, mix.getNumSamples());
    }

    auto r = run (words, mix, sampleRate, file, songStyle, previous, progress);
    file.deleteFile();
    return r;
}

} // namespace nightshift::engineer
