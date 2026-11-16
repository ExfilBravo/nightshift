// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "VibeListener.h"
#include "Drummer.h"
#include "OfflineDrummer.h"
#include "Request.h"
#include "StyleEars.h"
#include "Theory.h"
#include "model/Song.h"

namespace nightshift::vibes
{
namespace
{
    constexpr double longest = 8.0 * 60.0;      // seconds heard at most
    const char* const noteNames[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

    using Chroma = std::array<float, 12>;

    float cosine (const float* a, const float* b, int n)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;

        for (int i = 0; i < n; ++i)
        {
            ab += (double) a[i] * b[i];
            aa += (double) a[i] * a[i];
            bb += (double) b[i] * b[i];
        }

        return aa > 0.0 && bb > 0.0 ? (float) (ab / std::sqrt (aa * bb)) : 0.0f;
    }

    float cosine (const Chroma& a, const Chroma& b)     { return cosine (a.data(), b.data(), 12); }

    void normalise (Chroma& c)
    {
        double sum = 0.0;

        for (auto x : c)
            sum += (double) x * x;

        if (sum > 0.0)
            for (auto& x : c)
                x = (float) (x / std::sqrt (sum));
    }

    float median (std::vector<float> v)
    {
        if (v.empty())
            return 0.0f;

        std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
        return v[v.size() / 2];
    }

    float percentile (std::vector<float> v, float p)
    {
        if (v.empty())
            return 0.0f;

        const auto at = (size_t) juce::jlimit (0, (int) v.size() - 1, (int) std::round (p * (float) (v.size() - 1)));
        std::nth_element (v.begin(), v.begin() + (long) at, v.end());
        return v[at];
    }

    //==============================================================================
    // Short-time spectra: a Hann window every `hop` samples, its magnitudes (bins 0..size/2) handed to `each`
    void spectra (const float* x, int n, int order, int hop, const std::function<void (const float*)>& each,
                  const std::function<bool()>& stop, const std::function<void (float)>& progress, float from, float to)
    {
        const int size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> window ((size_t) size), buffer ((size_t) size * 2);

        for (int i = 0; i < size; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);

        const int frames = n >= size ? (n - size) / hop + 1 : 0;

        for (int f = 0; f < frames; ++f)
        {
            if ((f & 127) == 0)
            {
                if (stop != nullptr && stop())
                    return;

                if (progress != nullptr)
                    progress (from + (to - from) * (float) f / (float) frames);
            }

            const float* in = x + (size_t) f * (size_t) hop;

            for (int i = 0; i < size; ++i)
                buffer[(size_t) i] = in[i] * window[(size_t) i];

            std::fill (buffer.begin() + size, buffer.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buffer.data(), true);
            each (buffer.data());
        }
    }

    //==============================================================================
    // Onsets: how much the spectrum rises from frame to frame (in all of it, the kick's low band, the snare's middle,
    // the hats' top), and the song's sound on average: its brightness (its top's share), grit (how flat, noise-like, its
    // held sound's 1 to 6 kHz is: distortion), and its low end's share of the power
    struct Onsets
    {
        double frameSeconds = 0.0, offsetSeconds = 0.0;     // frame f is at offset + f * frameSeconds
        std::vector<float> all;                             // the whole spectrum's rise (for the beat)
        std::vector<float> low, mid, high;                  // the drums' alone: the percussive part's rise in each band
        std::vector<float> brightness;                      // each frame's spectral centroid, in octaves above 1 kHz
        double lowShare = 0.0;                              // the power under 120 Hz against all of it
        double highShare = 0.0;                             // the power over 2 kHz against all of it over 200 Hz (the bass left out)
        double harmonicFlatness = 0.0;                      // how flat the held sound's 1 to 6 kHz is (distortion; the drums left out)

        double timeOf (double frame) const      { return offsetSeconds + frame * frameSeconds; }
        double frameAt (double seconds) const   { return (seconds - offsetSeconds) / frameSeconds; }
    };

    Onsets onsetsOf (const float* x, int n, double rate, const std::function<bool()>& stop, const std::function<void (float)>& progress)
    {
        Onsets o;
        const int order = rate > 32000.0 ? 11 : 10;        // about 46 ms
        const int size = 1 << order, hop = size / 4;
        o.frameSeconds = hop / rate;
        o.offsetSeconds = size / 2.0 / rate;
        const int bins = size / 2 + 1;
        const double binHz = rate / size;
        auto bin = [&] (double hz) { return juce::jlimit (1, bins - 1, (int) std::round (hz / binHz)); };
        const int lowShareTop = bin (120.0);

        // The spectrum in 64 bands kept for every frame (what the drums' part is taken from): each bin its own band up to
        // 400 Hz (the kick's needs every one there is), then evenly spaced in pitch to 16 kHz
        constexpr int bands = 64;
        const double top = std::min (16000.0, rate * 0.45);
        const int lowBins = std::min (bands / 2, std::max (1, (int) (400.0 / binHz)));
        const double split = (lowBins + 0.5) * binHz;
        std::vector<int> bandOf ((size_t) bins, -1);
        std::vector<double> centre ((size_t) bands);

        for (int k = 1; k < bins; ++k)
        {
            const double hz = k * binHz;

            if (k <= lowBins)
                bandOf[(size_t) k] = k - 1;
            else if (hz < top)
                bandOf[(size_t) k] = juce::jlimit (lowBins, bands - 1, lowBins + (int) ((bands - lowBins) * std::log (hz / split) / std::log (top / split)));
        }

        for (int b = 0; b < bands; ++b)
            centre[(size_t) b] = b < lowBins ? (b + 1) * binHz : split * std::pow (top / split, (b - lowBins + 0.5) / (bands - lowBins));

        std::vector<float> spectrum;
        std::vector<float> previous ((size_t) bins, 0.0f);
        double lowSum = 0.0, midsSum = 0.0, highsSum = 0.0;
        int counted = 0;
        const int midsFrom = bin (200.0), highsFrom = bin (2000.0), highsTo = bin (16000.0);

        spectra (x, n, order, hop, [&] (const float* mag)
        {
            float a = 0.0f;
            double power = 0.0, weighted = 0.0, low = 0.0;
            std::array<double, bands> band {};

            for (int k = 1; k < bins; ++k)
            {
                const float level = std::log1p (10.0f * mag[k]);
                a += std::max (0.0f, level - previous[(size_t) k]);
                previous[(size_t) k] = level;

                const double p = (double) mag[k] * mag[k];
                power += p;
                weighted += p * k * binHz;

                if (k >= midsFrom && k < highsTo)
                {
                    midsSum += p;

                    if (k >= highsFrom)
                        highsSum += p;
                }

                if (bandOf[(size_t) k] >= 0)
                    band[(size_t) bandOf[(size_t) k]] += p;

                if (k <= lowShareTop)
                    low += p;
            }

            o.all.push_back (a);

            for (auto p : band)
                spectrum.push_back ((float) std::sqrt (p));

            o.brightness.push_back (power > 1.0e-3 ? (float) std::log2 (weighted / power / 1000.0) : 0.0f);

            if (power > 1.0e-3)
            {
                lowSum += low / power;
                ++counted;
            }
        }, stop, progress, 0.0f, 0.4f);

        if (counted > 0)
            o.lowShare = lowSum / counted;

        o.highShare = midsSum > 0.0 ? highsSum / midsSum : 0.0;

        // The drums alone: harmonic sound holds still in pitch over time (a median across time keeps it), a drum hit is
        // short and spread across pitch (a median across the bands keeps it). Each band's share that's drum, then the rise
        // of that share in the kick's band (under 130 Hz), the snare's (180 Hz to 2.5 kHz) and the hats' (over 5 kHz)
        const int frames = (int) o.all.size();
        o.low.assign ((size_t) frames, 0.0f);
        o.mid.assign ((size_t) frames, 0.0f);
        o.high.assign ((size_t) frames, 0.0f);
        std::array<float, bands> last {};
        std::vector<float> window;
        double heldFlatSum = 0.0, heldWeight = 0.0;

        for (int t = 0; t < frames; ++t)
        {
            const float* now = spectrum.data() + (size_t) t * bands;
            double heldLog = 0.0, heldLin = 0.0;
            int heldBands = 0;

            for (int b = 0; b < bands; ++b)
            {
                window.clear();

                for (int u = std::max (0, t - 8); u <= std::min (frames - 1, t + 8); ++u)
                    window.push_back (spectrum[(size_t) u * bands + (size_t) b]);

                std::nth_element (window.begin(), window.begin() + (long) window.size() / 2, window.end());
                const float harmonic = window[window.size() / 2];

                window.clear();

                for (int c = std::max (0, b - 4); c <= std::min (bands - 1, b + 4); ++c)
                    window.push_back (now[c]);

                std::nth_element (window.begin(), window.begin() + (long) window.size() / 2, window.end());
                const float percussive = window[window.size() / 2];
                const float share = percussive * percussive / (percussive * percussive + harmonic * harmonic + 1.0e-12f);
                const float level = std::log1p (10.0f * now[b] * share);
                const float rise = std::max (0.0f, level - last[(size_t) b]);
                last[(size_t) b] = level;
                const double hz = centre[(size_t) b];

                if (hz < 130.0)                         o.low[(size_t) t] += rise;
                else if (hz >= 180.0 && hz < 2500.0)    o.mid[(size_t) t] += rise;
                else if (hz >= 5000.0)                  o.high[(size_t) t] += rise;

                // The held (harmonic) part's 1 to 6 kHz, for its flatness
                if (hz >= 1000.0 && hz < 6000.0)
                {
                    const double held = (double) now[b] * (1.0 - share) + 1.0e-9;
                    heldLog += std::log (held);
                    heldLin += held;
                    ++heldBands;
                }
            }

            // (Each frame counted by how loud it is: the quiet ones' flatness is the noise floor's)
            if (heldBands > 0)
            {
                const double mean = heldLin / heldBands;
                heldFlatSum += std::exp (heldLog / heldBands);
                heldWeight += mean;
            }

            if ((t & 1023) == 0 && progress != nullptr)
                progress (0.4f + 0.1f * (float) t / (float) std::max (1, frames));
        }

        o.harmonicFlatness = heldWeight > 0.0 ? heldFlatSum / heldWeight : 0.0;
        return o;
    }

    // The onset strength: each frame's rise above its local average (about a third of a second around it), in
    // standard deviations
    std::vector<float> envelope (const std::vector<float>& flux, double frameSeconds)
    {
        const int n = (int) flux.size();
        const int half = std::max (1, (int) std::round (0.15 / frameSeconds));
        std::vector<double> sums ((size_t) n + 1, 0.0);

        for (int i = 0; i < n; ++i)
            sums[(size_t) i + 1] = sums[(size_t) i] + flux[(size_t) i];

        std::vector<float> e ((size_t) n);
        double square = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const int a = std::max (0, i - half), b = std::min (n, i + half + 1);
            const double local = (sums[(size_t) b] - sums[(size_t) a]) / (b - a);
            e[(size_t) i] = (float) std::max (0.0, flux[(size_t) i] - local);
            square += (double) e[(size_t) i] * e[(size_t) i];
        }

        const double sd = n > 0 ? std::sqrt (square / n) : 0.0;

        if (sd > 1.0e-9)
            for (auto& v : e)
                v = (float) (v / sd);

        return e;
    }

    //==============================================================================
    // The tempo: the onset strength's autocorrelation over the periods of 50 to 220 bpm (each with its double's too, a
    // beat's onsets repeating every two), weighted toward the 120 bpm most music is near, an octave either way less
    // likely. Refined between frames; confidence: how far the best stands above the rest
    double tempoOf (const std::vector<float>& e, double frameSeconds, float& confidence)
    {
        confidence = 0.0f;
        const int n = (int) e.size();
        const int minLag = std::max (2, (int) std::floor (60.0 / 220.0 / frameSeconds));
        const int maxLag = std::min (n / 3, (int) std::ceil (60.0 / 50.0 / frameSeconds));

        if (maxLag <= minLag + 2)
            return 0.0;

        std::vector<double> ac ((size_t) maxLag * 4 + 3, 0.0);

        for (int lag = 1; lag < (int) ac.size() && lag < n; ++lag)
        {
            double s = 0.0;

            for (int t = 0; t + lag < n; ++t)
                s += (double) e[(size_t) t] * e[(size_t) (t + lag)];

            ac[(size_t) lag] = s / (n - lag);
        }

        auto prior = [] (double bpm)
        {
            const double octaves = std::log2 (bpm / 120.0);
            return std::exp (-0.5 * octaves * octaves / (0.8 * 0.8));
        };

        // The autocorrelation near a multiple of a lag: a lag's off its true period by up to half a frame, so its m-th
        // multiple by up to m halves, and an onset's peak is only a frame or two wide
        auto near = [&ac] (int lag, int times)
        {
            double m = 0.0;

            for (int d = -(times / 2); d <= times / 2; ++d)
                if (const int at = lag * times + d; at > 0 && at < (int) ac.size())
                    m = std::max (m, ac[(size_t) at]);

            return m;
        };

        double best = -1.0;
        int bestLag = minLag;
        std::vector<float> scores;

        // (Its double and its four times too: a beat's onsets repeat every two beats and every bar, where a beat and a
        // half's, a drum and bass two-step's, don't)
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            const double s = (ac[(size_t) lag] + 0.5 * near (lag, 2) + 0.25 * near (lag, 4)) * prior (60.0 / (lag * frameSeconds));
            scores.push_back ((float) s);

            if (s > best)
            {
                best = s;
                bestLag = lag;
            }
        }

        // A beat and a half heard as the beat (drum and bass's two-step at 174 heard at 116, its multiples all landing on
        // its 8ths): a period two thirds as long correlating better on its own says so. Then that, or its double
        if (const int third = (int) std::round (bestLag * 2.0 / 3.0); third >= minLag && near (third, 1) > 1.05 * near (bestLag, 1))
        {
            int pick = third;

            if (const int twice = third * 2; twice <= maxLag && scores[(size_t) (twice - minLag)] > scores[(size_t) (third - minLag)])
                pick = twice;

            bestLag = pick;
            best = scores[(size_t) (pick - minLag)];
        }

        const double y0 = ac[(size_t) bestLag - 1], y1 = ac[(size_t) bestLag], y2 = ac[(size_t) bestLag + 1];
        const double bend = y0 - 2.0 * y1 + y2;
        const double shift = std::abs (bend) > 1.0e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (y0 - y2) / bend) : 0.0;
        const float middle = median (scores);
        confidence = best > 0.0 ? juce::jlimit (0.0f, 1.0f, (float) ((best - middle) / best)) : 0.0f;
        return 60.0 / ((bestLag + shift) * frameSeconds);
    }

    // The beats: dynamic programming over the onset strength (Ellis, 2007): each frame's best is its own strength plus
    // the best of a beat before it, a beat being about the tempo's period (stretching it costs, by how far, in log
    // terms). Backtracked from the best near the end: a beat a step, following the song as it drifts
    std::vector<double> beatsOf (const std::vector<float>& e, double period)
    {
        const int n = (int) e.size();
        std::vector<double> score ((size_t) n, 0.0);
        std::vector<int> back ((size_t) n, -1);
        const double tightness = 300.0;
        const int lo = std::max (1, (int) std::round (period * 0.5)), hi = std::max (lo + 1, (int) std::round (period * 2.0));

        for (int t = 0; t < n; ++t)
        {
            double best = 0.0;
            int arg = -1;

            for (int d = lo; d <= hi && t - d >= 0; ++d)
            {
                const double stretch = std::log ((double) d / period);
                const double v = score[(size_t) (t - d)] - tightness * stretch * stretch;

                if (v > best)
                {
                    best = v;
                    arg = t - d;
                }
            }

            score[(size_t) t] = e[(size_t) t] + best;
            back[(size_t) t] = arg;
        }

        int t = -1;
        double best = -1.0;

        for (int i = std::max (0, n - (int) std::ceil (period)); i < n; ++i)
            if (score[(size_t) i] > best)
            {
                best = score[(size_t) i];
                t = i;
            }

        std::vector<double> beats;

        for (; t >= 0; t = back[(size_t) t])
            beats.push_back (t);

        std::reverse (beats.begin(), beats.end());
        return beats;
    }

    //==============================================================================
    // Pitch classes: a frame's spectrum folded onto the twelve notes (55 Hz to 2 kHz, each bin to its nearest note,
    // weighted by how near), and the bass's alone (its notes from C1 to B3, each heard with its harmonics), every hop
    struct Chromas
    {
        double frameSeconds = 0.0, offsetSeconds = 0.0;
        std::vector<Chroma> notes, bass;
    };

    Chromas chromaOf (const float* x, int n, double rate, const std::function<bool()>& stop, const std::function<void (float)>& progress)
    {
        Chromas c;
        const int order = rate > 32000.0 ? 14 : 13;        // 16384 at 44.1 kHz: 2.7 Hz a bin
        const int size = 1 << order, hop = size / 4;
        c.frameSeconds = hop / rate;
        c.offsetSeconds = size / 2.0 / rate;
        const int bins = size / 2 + 1;
        const double binHz = rate / size;

        struct Weight { int bin, pc; float weight; };
        std::vector<Weight> weights;

        for (int k = 1; k < bins; ++k)
        {
            const double hz = k * binHz;

            if (hz < 55.0 || hz > 2000.0)
                continue;

            const double pitch = 69.0 + 12.0 * std::log2 (hz / 440.0);
            const double nearest = std::round (pitch);
            const auto off = (float) (pitch - nearest);
            weights.push_back ({ k, ((int) nearest % 12 + 12) % 12, std::exp (-0.5f * off * off / (0.3f * 0.3f)) });
        }

        // The bass's notes, each from its overtones together (C1 to B3: a drop-tuned low C's third harmonic, a G, can be
        // louder than the C itself, and more so distorted): its fundamental and three harmonics over it, nearer counting more
        auto magNear = [binHz, bins] (const float* mag, double hz)
        {
            const int k = (int) std::round (hz / binHz);
            float m = 0.0f;

            for (int i = k - 1; i <= k + 1; ++i)
                if (i > 0 && i < bins)
                    m = std::max (m, mag[i]);

            return m;
        };

        spectra (x, n, order, hop, [&] (const float* mag)
        {
            Chroma notes {}, bass {};

            for (auto& w : weights)
                notes[(size_t) w.pc] += mag[w.bin] * w.weight;

            // (Lower notes counting a little more: the bass is the lowest thing playing; distortion's odd harmonics, a
            // fifth and a third over it, aren't)
            for (int note = 24; note < 60; ++note)
            {
                const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                const float low = 1.5f - 0.5f * (float) (note - 24) / 35.0f;
                bass[(size_t) (note % 12)] += low * (magNear (mag, f0) + 0.8f * magNear (mag, 2.0 * f0) + 0.6f * magNear (mag, 3.0 * f0) + 0.45f * magNear (mag, 4.0 * f0));
            }

            c.notes.push_back (notes);
            c.bass.push_back (bass);
        }, stop, progress, 0.4f, 0.75f);

        return c;
    }

    // Krumhansl and Kessler's key profiles: how strongly each scale step belongs in a major and a minor key
    constexpr float majorProfile[] { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    constexpr float minorProfile[] { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    float pearson (const float* a, const float* b, int n)
    {
        double ma = 0.0, mb = 0.0;

        for (int i = 0; i < n; ++i)
        {
            ma += a[i];
            mb += b[i];
        }

        ma /= n;
        mb /= n;
        double ab = 0.0, aa = 0.0, bb = 0.0;

        for (int i = 0; i < n; ++i)
        {
            ab += (a[i] - ma) * (b[i] - mb);
            aa += (a[i] - ma) * (a[i] - ma);
            bb += (b[i] - mb) * (b[i] - mb);
        }

        return aa > 0.0 && bb > 0.0 ? (float) (ab / std::sqrt (aa * bb)) : 0.0f;
    }

    // A bar's chord: the triad (or power chord, seventh, major seventh or ninth) whose notes its pitch classes match
    // best, leaning to one whose root the bass plays (as much as it plays it: a bass going root to fifth leans both
    // ways; in a distorted song only the bass's) and to the key's own chords
    struct ChordGuess { int root = 0; int quality = 0; float score = 0.0f; };     // quality: 0 major, 1 minor, 2 power, 3 seventh, 4 minor seventh, 5 major seventh, 6 ninth

    ChordGuess chordOf (const Chroma& notes, const Chroma& bass, int keyRoot, bool minorKey, bool bassLeads)
    {
        static const std::vector<std::pair<int, std::vector<std::pair<int, float>>>> shapes {
            { 0, { { 0, 1.0f }, { 4, 0.8f }, { 7, 0.8f } } },
            { 1, { { 0, 1.0f }, { 3, 0.8f }, { 7, 0.8f } } },
            { 2, { { 0, 1.0f }, { 7, 0.9f } } },
            { 3, { { 0, 1.0f }, { 4, 0.8f }, { 7, 0.7f }, { 10, 0.6f } } },
            { 4, { { 0, 1.0f }, { 3, 0.8f }, { 7, 0.7f }, { 10, 0.6f } } },
            { 5, { { 0, 1.0f }, { 4, 0.8f }, { 7, 0.7f }, { 11, 0.6f } } },
            { 6, { { 0, 1.0f }, { 4, 0.8f }, { 7, 0.6f }, { 10, 0.6f }, { 2, 0.5f } } } };

        // The key's chords, as root and quality: I ii iii IV V vi (major); i III iv v/V VI VII (minor)
        static const std::vector<std::pair<int, int>> majorChords { { 0, 0 }, { 2, 1 }, { 4, 1 }, { 5, 0 }, { 7, 0 }, { 9, 1 } };
        static const std::vector<std::pair<int, int>> minorChords { { 0, 1 }, { 3, 0 }, { 5, 1 }, { 7, 1 }, { 7, 0 }, { 8, 0 }, { 10, 0 } };
        float bassTop = 0.0f, bassMean = 0.0f;

        for (auto b : bass)
        {
            bassTop = std::max (bassTop, b);
            bassMean += b / 12.0f;
        }

        ChordGuess best;
        best.score = -1.0f;

        for (int root = 0; root < 12; ++root)
            for (auto& [quality, tones] : shapes)
            {
                // Distorted, the guitars' overtones (a fifth, a third, a flat seventh over every note) blur which note is
                // the root: the bass's says
                if (bassLeads && bass[(size_t) root] < 0.85f * bassTop)
                    continue;

                // A seventh or ninth only when it's played, not just a chord tone's overtone (a fifth over its third
                // is a major seventh): as loud as the triad's quietest note, near enough
                if (quality >= 3)
                {
                    const float third = notes[(size_t) ((root + (quality == 4 ? 3 : 4)) % 12)];
                    const float triad = std::min ({ notes[(size_t) root], third, notes[(size_t) ((root + 7) % 12)] });
                    const float seventh = notes[(size_t) ((root + (quality == 5 ? 11 : 10)) % 12)];

                    if (seventh < 0.6f * triad || (quality == 6 && notes[(size_t) ((root + 2) % 12)] < 0.6f * triad))
                        continue;
                }

                Chroma t {};

                for (auto [interval, weight] : tones)
                    t[(size_t) ((root + interval) % 12)] = weight;

                float s = cosine (notes, t);

                if (bassTop > bassMean)
                    s += 0.1f * juce::jlimit (0.0f, 1.0f, (bass[(size_t) root] - bassMean) / (bassTop - bassMean));

                const int degree = ((root - keyRoot) % 12 + 12) % 12;
                const int basic = quality == 1 || quality == 4 ? 1 : quality == 2 ? 2 : 0;

                for (auto [d, q] : minorKey ? minorChords : majorChords)
                    if (d == degree && (q == basic || quality == 2))
                        s += 0.05f;

                // A power chord only where the third is truly missing; a ninth only where it's truly there
                if (quality == 2)
                    s -= 0.04f;

                if (quality == 6)
                    s -= 0.02f;

                if (s > best.score)
                    best = { root, quality, s };
            }

        return best;
    }

    juce::String nameOf (const ChordGuess& c)
    {
        static const char* const suffix[] { "", "m", "5", "7", "m7", "maj7", "9" };
        return juce::String (noteNames[c.root]) + suffix[c.quality];
    }

    //==============================================================================
    // The band's own drummer, playing each style once: the strength of its kick, snare and hats on each 16th of the
    // verse's and chorus's first bars, to hold a song's drums up against
    juce::String bandOf (const juce::String& voice)
    {
        if (voice == "kick")
            return "low";

        if (voice == "snare" || voice == "snare_rim" || voice == "sidestick" || voice == "clap")
            return "mid";

        if (voice.startsWith ("hat") || voice.startsWith ("ride") || voice == "shaker" || voice == "tambourine" || voice == "cowbell")
            return "high";

        return {};
    }

    void normalise (Pattern& p)
    {
        for (auto& band : p)
        {
            float top = 0.0f;

            for (auto v : band)
                top = std::max (top, v);

            if (top > 0.0f)
                for (auto& v : band)
                    v /= top;
        }
    }

    // A heard pattern's hits standing out: each band less the level of its quietest quarter (the onsets heard everywhere:
    // the other instruments', the room's), then scaled to its loudest
    void sharpen (Pattern& p)
    {
        for (auto& band : p)
        {
            auto sorted = band;
            std::sort (sorted.begin(), sorted.end());
            const float floor = sorted[4];

            for (auto& v : band)
                v = std::max (0.0f, v - floor);
        }

        normalise (p);
    }

    const std::map<juce::String, Pattern>& prototypes()
    {
        static const std::map<juce::String, Pattern> all = []
        {
            std::map<juce::String, Pattern> found;
            Song song;

            for (auto& style : agents::styles())
            {
                const auto result = drummer::offline::write (style.words[0], song, nullptr, {}, false, {}, true, 11);
                Pattern p {};

                for (auto& section : result.groove.sections)
                {
                    const auto kind = theory::kindOf (section.name);

                    if (kind != "verse" && kind != "chorus" && kind != "hook" && kind != "drop")
                        continue;

                    for (size_t b = 0; b < std::min<size_t> (2, section.bars.size()); ++b)
                        for (auto& lane : section.bars[b].lanes)
                        {
                            const auto band = bandOf (lane.voice);
                            const int steps = section.bars[b].steps;

                            if (band.isEmpty() || steps <= 0)
                                continue;

                            for (int i = 0; i < lane.hits.length(); ++i)
                                if (const auto hit = lane.hits[i]; hit != '.' && (i * 16) % steps == 0)
                                    p[band == "low" ? 0 : band == "mid" ? 1 : 2][(size_t) (i * 16 / steps)] += (float) drummer::velocityFor (hit) / 127.0f;
                        }
                }

                normalise (p);
                found[style.id] = p;
            }

            return found;
        }();

        return all;
    }

    // Every drum pattern the band knows: its drummer's in each style, and what its ears heard in each (StyleEars.h)
    const std::vector<std::pair<juce::String, Pattern>>& references()
    {
        static const auto all = []
        {
            std::vector<std::pair<juce::String, Pattern>> found;

            for (auto& [id, p] : prototypes())
                found.push_back ({ id, p });

            for (auto& ear : ears::all())
            {
                Pattern p {};

                for (size_t band = 0; band < 3; ++band)
                    for (size_t s = 0; s < 16; ++s)
                        p[band][s] = ear.pattern[band * 16 + s];

                found.push_back ({ ear.style, p });
            }

            return found;
        }();

        return all;
    }

    float patternMatch (const Pattern& song, const Pattern& style)
    {
        static const float weights[] { 0.45f, 0.35f, 0.2f };
        float s = 0.0f;

        for (size_t band = 0; band < 3; ++band)
            s += weights[band] * cosine (song[band].data(), style[band].data(), 16);

        return s;
    }

    // How likely the tempo heard is for a style: most at its usual tempo, a little less across its range, less as it
    // falls outside it (by octaves), and less again if it's the range's half or double (a half-time or double-time
    // hearing of it)
    float tempoMatch (double bpm, const agents::Style& s)
    {
        // Inside its range, nearer its usual tempo a little likelier
        auto at = [&s] (double b)
        {
            const double outside = b >= s.slowest && b <= s.fastest ? 0.0 : b < s.slowest ? std::log2 (s.slowest / b) : std::log2 (b / s.fastest);
            const double fromUsual = std::log2 (b / s.usual);
            return std::exp (-0.5 * outside * outside / (0.12 * 0.12)) * (0.7 + 0.3 * std::exp (-0.5 * fromUsual * fromUsual / (0.12 * 0.12)));
        };

        return (float) std::max (at (bpm), 0.6 * std::max (at (bpm / 2.0), at (bpm * 2.0)));
    }

    // The song's sound against a style's: grit for metal and hard rock, a heavy low end for trap, drill, dubstep and
    // hip-hop, darkness and quiet for lofi, ambient and ballads, brightness for EDM and pop-punk
    float soundMatch (const Vibe& v, const agents::Style& s)
    {
        const juce::String family (s.family), id (s.id);
        float m = 0.5f;

        if (family == "metal")                                          m = 0.2f + 0.8f * v.grit;
        else if (id == "hardrock" || id == "punk" || id == "poppunk")   m = 0.3f + 0.6f * v.grit;
        else if (family == "trap" || id == "dubstep")                   m = 0.3f + 0.7f * v.lowEnd;
        else if (id == "lofi" || id == "ambient" || id == "ballad")     m = 0.3f + 0.6f * (1.0f - v.brightness);
        else if (id == "edm" || id == "synthwave")                      m = 0.3f + 0.6f * v.brightness;
        else                                                            m = 0.6f - 0.4f * v.grit;

        // A kick on every beat belongs to house, techno, EDM and disco; swing to funk, hip-hop, jazz, lofi
        if (v.fourOnFloor)
            m += id == "house" || id == "techno" || id == "edm" ? 0.3f : family == "metal" ? 0.0f : -0.1f;

        if (v.swing > 0.3f)
            m += id == "funk" || id == "boombap" || id == "hiphop" || id == "jazz" || id == "lofi" || id == "neosoul" || id == "reggae" ? 0.2f : -0.1f;

        return juce::jlimit (0.0f, 1.0f, m);
    }

    //==============================================================================
    // Sections: the bars in phrases of four, each phrase the same music as an earlier one when it goes the same way bar
    // by bar (a letter); a run of one letter's phrases a section; then their kinds: a short first or last section whose
    // music never comes back (or that's quiet) the intro and outro, the loudest music that comes back the chorus, what
    // comes before it the verse, a quiet one in the middle a breakdown, one heard once late on a bridge
    struct Bar
    {
        double start = 0.0, end = 0.0;      // seconds
        Chroma notes {}, bass {};
        float db = -100.0f;
        float bright = 0.0f;                // its spectral centroid, octaves above 1 kHz
        float drums = 0.0f;                 // how busy and hard its drums are (the drums' onsets, on average)
        juce::String chord;
    };

    std::vector<Section> sectionsOf (const std::vector<Bar>& bars)
    {
        std::vector<Section> sections;
        const int n = (int) bars.size();

        if (n == 0)
            return sections;

        // A stretch's notes, loudness, brightness and drums
        struct Feature { Chroma notes {}; float db = -100.0f, bright = 0.0f, drums = 0.0f; };

        auto featureOf = [&bars] (int from, int to)
        {
            Feature f;
            double power = 0.0;

            for (int i = from; i < to; ++i)
            {
                for (size_t pc = 0; pc < 12; ++pc)
                    f.notes[pc] += bars[(size_t) i].notes[pc];

                power += std::pow (10.0, bars[(size_t) i].db / 10.0);
                f.bright += bars[(size_t) i].bright;
                f.drums += bars[(size_t) i].drums;
            }

            const int count = std::max (1, to - from);
            normalise (f.notes);
            f.db = (float) (10.0 * std::log10 (power / count + 1.0e-12));
            f.bright /= (float) count;
            f.drums /= (float) count;
            return f;
        };

        // The bars in phrases of four. Two phrases are the same music when they go the same way bar by bar (the same
        // chords in the same order: a verse's and a chorus's can share every chord in another order), about as loud,
        // as bright and with the drums as busy
        struct Block { int from, to; Feature f; };
        std::vector<Block> blocks;

        for (int b = 0; b < n; b += 4)
        {
            const int to = std::min (n, b + 4);

            // A last phrase under two bars joins the one before
            if (to - b < 2 && ! blocks.empty())
            {
                blocks.back().to = to;
                blocks.back().f = featureOf (blocks.back().from, to);
            }
            else
                blocks.push_back ({ b, to, featureOf (b, to) });
        }

        // (Bar against bar, their notes' correlation: the drums' noise under every note left out, two chords sharing a
        // note or two come out apart; the same chord heard is the same)
        auto sameMusic = [&bars] (const Block& a, const Block& b)
        {
            const int length = std::min (a.to - a.from, b.to - b.from);
            float together = 0.0f;

            for (int i = 0; i < length; ++i)
            {
                const auto& x = bars[(size_t) (a.from + i)];
                const auto& y = bars[(size_t) (b.from + i)];
                together += x.chord == y.chord ? std::max (0.9f, pearson (x.notes.data(), y.notes.data(), 12))
                                               : pearson (x.notes.data(), y.notes.data(), 12);
            }

            const float drumsApart = std::abs (a.f.drums - b.f.drums) / std::max (0.1f, std::max (a.f.drums, b.f.drums));
            return together / (float) std::max (1, length) > 0.82f && std::abs (a.f.db - b.f.db) < 3.0f
                    && std::abs (a.f.bright - b.f.bright) < 0.3f && drumsApart < 0.35f;
        };

        // Each phrase's letter: an earlier phrase's when it's the same music
        std::vector<int> phraseLetter (blocks.size(), -1);
        int phraseLetters = 0;

        for (size_t i = 0; i < blocks.size(); ++i)
        {
            for (size_t j = 0; j < i && phraseLetter[i] < 0; ++j)
                if (sameMusic (blocks[i], blocks[j]))
                    phraseLetter[i] = phraseLetter[j];

            if (phraseLetter[i] < 0)
                phraseLetter[i] = phraseLetters++;
        }

        // A section: a run of phrases of one letter (a verse is its phrase twice, then the chorus's comes in)
        std::vector<int> sectionPhrase;

        for (size_t i = 0; i < blocks.size(); ++i)
        {
            if (! sections.empty() && phraseLetter[i] == phraseLetter[i - 1])
            {
                sections.back().bars = blocks[i].to - sections.back().startBar;
                continue;
            }

            Section s;
            s.startBar = blocks[i].from;
            s.bars = blocks[i].to - blocks[i].from;
            sections.push_back (s);
            sectionPhrase.push_back (phraseLetter[i]);
        }

        // Each section's chords and loudness, and its letter: its phrase's
        std::vector<Feature> features;
        float loudest = -100.0f;

        for (auto& s : sections)
        {
            for (int i = s.startBar; i < s.startBar + s.bars; ++i)
                s.chords.add (bars[(size_t) i].chord);

            features.push_back (featureOf (s.startBar, s.startBar + s.bars));
            loudest = std::max (loudest, features.back().db);
        }

        std::vector<int> letterOf (sections.size(), -1);
        std::map<int, int> letterForPhrase;

        for (size_t i = 0; i < sections.size(); ++i)
        {
            sections[i].energyDb = features[i].db - loudest;
            const int phrase = sectionPhrase[i];

            if (letterForPhrase.count (phrase) == 0)
            {
                const int next = (int) letterForPhrase.size();
                letterForPhrase[phrase] = next;
            }

            letterOf[i] = letterForPhrase[phrase];
            sections[i].label = juce::String::charToString ((juce::juce_wchar) ('A' + std::min (25, letterOf[i])));
        }

        // Kinds. The ends first: a short start whose music never comes back, or that's quiet, the intro; a short end
        // likewise, or shorter than its music is elsewhere (the verse's first phrase once more, to finish), the outro
        const int count = (int) sections.size();
        std::vector<float> middle;
        std::map<int, int> heard;

        for (size_t i = 0; i < sections.size(); ++i)
        {
            middle.push_back (sections[i].energyDb);
            ++heard[letterOf[i]];
        }

        const float typical = median (middle);

        if (count > 2 && sections.front().bars <= 8 && (heard[letterOf.front()] == 1 || sections.front().energyDb < typical - 2.0f))
            sections.front().kind = "intro";

        if (count > 2 && sections.back().bars <= 8)
        {
            int longestElsewhere = 0;

            for (int i = 0; i + 1 < count; ++i)
                if (letterOf[(size_t) i] == letterOf.back())
                    longestElsewhere = std::max (longestElsewhere, sections[(size_t) i].bars);

            if (heard[letterOf.back()] == 1 || sections.back().energyDb < typical - 2.0f || sections.back().bars < longestElsewhere)
                sections.back().kind = "outro";
        }

        // The chorus: the loudest letter that comes back (else the loudest)
        std::map<int, int> times;
        std::map<int, float> level;

        for (int i = 0; i < count; ++i)
            if (sections[(size_t) i].kind.isEmpty())
            {
                ++times[letterOf[(size_t) i]];
                level[letterOf[(size_t) i]] = std::max (level.count (letterOf[(size_t) i]) > 0 ? level[letterOf[(size_t) i]] : -100.0f, sections[(size_t) i].energyDb);
            }

        int chorus = -1;
        float chorusLevel = -1000.0f;

        for (auto& [letter, n2] : times)
            if ((n2 >= 2 || times.size() == 1) && level[letter] > chorusLevel)
            {
                chorus = letter;
                chorusLevel = level[letter];
            }

        if (chorus < 0)
            for (auto& [letter, l] : level)
                if (l > chorusLevel)
                {
                    chorus = letter;
                    chorusLevel = l;
                }

        // The verse: the letter most often just before a chorus (else the commonest other one)
        std::map<int, int> before;

        for (int i = 1; i < count; ++i)
            if (letterOf[(size_t) i] == chorus && letterOf[(size_t) i - 1] != chorus && sections[(size_t) i - 1].kind.isEmpty())
                ++before[letterOf[(size_t) i - 1]];

        int verse = -1;

        for (auto& [letter, n2] : before)
            if (verse < 0 || n2 > before[verse])
                verse = letter;

        if (verse < 0)
            for (auto& [letter, n2] : times)
                if (letter != chorus && (verse < 0 || n2 > times[verse]))
                    verse = letter;

        // A letter between a verse and a chorus each time: the pre-chorus
        for (int i = 0; i < count; ++i)
        {
            auto& s = sections[(size_t) i];

            if (s.kind.isNotEmpty())
                continue;

            const int letter = letterOf[(size_t) i];

            if (letter == chorus)       s.kind = "chorus";
            else if (letter == verse)   s.kind = "verse";
            else if (i > 0 && i + 1 < count && letterOf[(size_t) i - 1] == verse && letterOf[(size_t) i + 1] == chorus && times[letter] >= 2)
                                        s.kind = "prechorus";
            else if (s.energyDb < typical - 4.0f)
                                        s.kind = "breakdown";
            else                        s.kind = "bridge";
        }

        return sections;
    }

    //==============================================================================
    juce::String percent (float x)      { return juce::String (juce::roundToInt (x * 100.0f)) + "%"; }

    int lastIndexOf (const juce::StringArray& a, const juce::String& s)
    {
        for (int i = a.size(); --i >= 0;)
            if (a[i] == s)
                return i;

        return -1;
    }

    // A chord moved by semitones, its quality kept
    juce::String transposed (const juce::String& name, int semitones)
    {
        const auto c = theory::parseChord (name);

        if (c.name.isEmpty() || semitones % 12 == 0)
            return name;

        int length = name.length() > 1 && (name[1] == '#' || name[1] == 'b') ? 2 : 1;
        return juce::String (noteNames[((c.root + semitones) % 12 + 12) % 12]) + name.substring (length);
    }

    // A chord's neighbour in the key, for a looser take: the I's relative (vi) or iii, the IV's ii, the V's iii...
    juce::String substitute (const juce::String& name, int keyRoot, bool minorKey, juce::Random& r)
    {
        const auto c = theory::parseChord (name);
        const int degree = ((c.root - keyRoot) % 12 + 12) % 12;
        std::vector<std::pair<int, bool>> options;      // degree, minor

        if (! minorKey)
        {
            if (degree == 0)        options = { { 9, true }, { 4, true } };
            else if (degree == 5)   options = { { 2, true }, { 9, true } };
            else if (degree == 7)   options = { { 4, true }, { 5, false } };
            else if (degree == 9)   options = { { 0, false }, { 5, false } };
            else if (degree == 2)   options = { { 5, false } };
            else if (degree == 4)   options = { { 0, false }, { 7, false } };
        }
        else
        {
            if (degree == 0)        options = { { 3, false }, { 8, false } };
            else if (degree == 5)   options = { { 8, false }, { 3, false } };
            else if (degree == 7)   options = { { 10, false }, { 5, true } };
            else if (degree == 8)   options = { { 5, true }, { 0, true } };
            else if (degree == 3)   options = { { 0, true }, { 10, false } };
            else if (degree == 10)  options = { { 7, true }, { 3, false } };
        }

        if (options.empty())
            return name;

        const auto [d, minor] = options[(size_t) r.nextInt ((int) options.size())];
        return juce::String (noteNames[(keyRoot + d) % 12]) + (minor ? "m" : "");
    }
}

//==============================================================================
int Vibe::bars() const
{
    int n = 0;

    for (auto& s : sections)
        n += s.bars;

    return n;
}

juce::String Vibe::keyName() const
{
    return juce::String (noteNames[(keyRoot % 12 + 12) % 12]) + " " + mode;
}

juce::String Vibe::summary() const
{
    if (! ok())
        return problem;

    juce::String s;
    s << juce::roundToInt (tempo) << " bpm, " << keyName() << ", 4/4";

    if (swing >= 0.15f)
        s << ", " << (swingSixteenths ? "16ths" : "8ths") << " swung " << percent (swing);

    if (fourOnFloor)
        s << ", a kick on every beat";

    if (halfTime)
        s << ", half-time";

    s << ". ";

    if (! styles.empty())
    {
        const auto* style = agents::findStyle (styles.front().style);
        s << "Nearest the band's " << (style != nullptr ? juce::String (style->name).toLowerCase() : styles.front().style);

        if (styles.front().why.isNotEmpty())
            s << " (" << styles.front().why << ")";

        if (styles.size() > 2)
        {
            const auto* second = agents::findStyle (styles[1].style);
            const auto* third = agents::findStyle (styles[2].style);
            s << ", then " << (second != nullptr ? juce::String (second->name).toLowerCase() : styles[1].style) << " and "
              << (third != nullptr ? juce::String (third->name).toLowerCase() : styles[2].style);
        }

        s << ". ";
    }

    juce::StringArray form;

    for (auto& sec : sections)
        form.add (sec.kind + " " + juce::String (sec.bars) + (sec.energyDb <= -6.0f ? " (quiet)" : juce::String()));

    s << "Sections: " << form.joinIntoString (", ") << ". ";

    // Each kind's chords, as first heard (four bars of them)
    juce::StringArray kinds;

    for (auto& sec : sections)
        if (! kinds.contains (sec.kind) && ! sec.chords.isEmpty())
        {
            kinds.add (sec.kind);
            juce::StringArray four;

            for (int i = 0; i < std::min (4, sec.chords.size()); ++i)
                four.add (sec.chords[i]);

            s << (kinds.size() == 1 ? "Chords: " : ", ") << sec.kind << " " << four.joinIntoString (" ");
        }

    if (! kinds.isEmpty())
        s << ". ";

    s << "It sounds " << (brightness > 0.6f ? "bright" : brightness < 0.35f ? "dark" : "warm") << (grit > 0.5f ? " and gritty" : grit > 0.25f ? ", a little gritty" : " and clean")
      << ", " << (lowEnd > 0.6f ? "with a heavy low end" : lowEnd < 0.3f ? "with a light low end" : "with a solid low end") << ".";
    return s;
}

juce::String fileTypes()
{
    return "*.mp3;*.wav;*.aif;*.aiff;*.flac;*.ogg";
}

//==============================================================================
Vibe listen (const juce::File& file, std::function<void (float)> progress, std::function<bool()> shouldStop)
{
    Vibe v;
    v.name = file.getFileNameWithoutExtension();
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        v.problem = "Couldn't read " + file.getFileName() + ": it isn't a song file NightShift knows (MP3, WAV, AIFF, FLAC, Ogg).";
        return v;
    }

    // As one channel, read a few seconds at a time (a whole song's channels at once would be a few hundred MB)
    const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (longest * reader->sampleRate));
    const int channels = (int) std::max (1u, reader->numChannels), chunk = 1 << 18;
    juce::AudioBuffer<float> buffer (channels, chunk);
    std::vector<float> mono ((size_t) length, 0.0f);

    for (int at = 0; at < length; at += chunk)
    {
        const int count = std::min (chunk, length - at);
        reader->read (&buffer, 0, count, at, true, true);

        for (int ch = 0; ch < channels; ++ch)
            juce::FloatVectorOperations::addWithMultiply (mono.data() + at, buffer.getReadPointer (ch), 1.0f / (float) channels, count);
    }

    return listen (mono.data(), length, reader->sampleRate, v.name, std::move (progress), std::move (shouldStop));
}

Vibe listen (const float* samples, int numSamples, double rate, const juce::String& name,
             std::function<void (float)> progress, std::function<bool()> shouldStop)
{
    Vibe v;
    v.name = name;
    v.seconds = numSamples / std::max (1.0, rate);

    if (v.seconds < 10.0)
    {
        v.problem = "That's too short to hear a vibe in: give it at least ten seconds of a song.";
        return v;
    }

    // Level it: the loudest sample at full scale, so a quiet file is heard as a loud one
    std::vector<float> x (samples, samples + numSamples);
    const auto peak = juce::FloatVectorOperations::findMaximum (x.data(), numSamples);
    const auto trough = juce::FloatVectorOperations::findMinimum (x.data(), numSamples);
    const float top = std::max (std::abs (peak), std::abs (trough));

    if (top < 1.0e-4f)
    {
        v.problem = "That song is silent: there's nothing to hear in it.";
        return v;
    }

    juce::FloatVectorOperations::multiply (x.data(), 1.0f / top, numSamples);
    auto stopped = [&shouldStop] { return shouldStop != nullptr && shouldStop(); };

    // The onsets, the tempo and the beats
    const auto o = onsetsOf (x.data(), numSamples, rate, shouldStop, progress);

    if (stopped())
    {
        v.problem = "Stopped.";
        return v;
    }

    const auto e = envelope (o.all, o.frameSeconds);
    double bpm = tempoOf (e, o.frameSeconds, v.tempoConfidence);

    if (bpm <= 0.0)
    {
        v.problem = "Couldn't find a beat in it.";
        return v;
    }

    // Each band's onset strength, and the strongest of it near a moment
    auto strongestNear = [&o] (const std::vector<float>& band, double seconds)
    {
        const double f = o.frameAt (seconds);
        float m = 0.0f;

        for (int i = (int) std::floor (f) - 2; i <= (int) std::ceil (f) + 2; ++i)
            if (i >= 0 && i < (int) band.size())
                m = std::max (m, band[(size_t) i]);

        return m;
    };

    const auto lowE = envelope (o.low, o.frameSeconds), midE = envelope (o.mid, o.frameSeconds), highE = envelope (o.high, o.frameSeconds);

    // A run of beats' 8ths (the beats and halfway between), and the drums on the 16ths of bars of four beats starting
    // on the first 8th (a bar starting on any other 8th has the same pattern, turned)
    auto eighthsOf = [] (const std::vector<double>& at)
    {
        std::vector<double> eighths;

        for (size_t i = 0; i < at.size(); ++i)
        {
            eighths.push_back (at[i]);

            if (i + 1 < at.size())
                eighths.push_back ((at[i] + at[i + 1]) / 2.0);
        }

        return eighths;
    };

    auto drumsOver = [&] (const std::vector<double>& eighths)
    {
        Pattern base {};

        for (int j = 0; j + 8 < (int) eighths.size(); j += 8)
            for (int step = 0; step < 16; ++step)
            {
                const auto at = (size_t) (j + step / 2);
                const double t = step % 2 == 0 ? eighths[at] : (eighths[at] + eighths[at + 1]) / 2.0;
                base[0][(size_t) step] += strongestNear (lowE, t);
                base[1][(size_t) step] += strongestNear (midE, t);
                base[2][(size_t) step] += strongestNear (highE, t);
            }

        return base;
    };

    auto turned = [] (const Pattern& p, size_t eighths)
    {
        Pattern t {};

        for (size_t band = 0; band < 3; ++band)
            for (size_t s = 0; s < 16; ++s)
                t[band][s] = p[band][(s + 2 * eighths) % 16];

        sharpen (t);
        return t;
    };

    auto beatsAt = [&e, &o] (double tempo)
    {
        std::vector<double> at;

        for (auto f : beatsOf (e, 60.0 / tempo / o.frameSeconds))
            at.push_back (o.timeOf (f));

        return at;
    };

    auto beats = beatsAt (bpm);

    // Kept on one grid. The tracker can slip half a beat onto the offbeats and stay there a while (where reggae's skank
    // or funk's stabs outweigh the beat): a gap half again as long, or half as long, as the rest. What follows a slip
    // goes back in step with what came before it, so the whole song is one grid (which of its 8ths are the beats, the
    // bars decide below); a beat missed or doubled is put back or taken out
    if (beats.size() >= 16)
    {
        std::vector<float> gaps;

        for (size_t i = 1; i < beats.size(); ++i)
            gaps.push_back ((float) (beats[i] - beats[i - 1]));

        const double typical = median (gaps);
        std::vector<double> kept { beats.front() };
        double shift = 0.0;

        for (size_t i = 1; i < beats.size(); ++i)
        {
            const double ratio = (beats[i] - beats[i - 1]) / typical;

            if (ratio > 1.3 && ratio < 1.7)         shift -= 0.5 * typical;
            else if (ratio > 0.3 && ratio < 0.7)    shift += 0.5 * typical;

            // (Two half-beat slips the same way are a whole beat: in step again)
            if (std::abs (shift) >= 0.99 * typical)
                shift += shift > 0.0 ? -typical : typical;

            const double t = beats[i] + shift, apart = (t - kept.back()) / typical;

            if (apart < 0.5)
                continue;

            for (int missing = (int) std::round (apart) - 1; missing > 0 && missing <= 2; --missing)
                kept.push_back (kept.back() + (t - kept.back()) / (missing + 1));

            kept.push_back (t);
        }

        beats = kept;
    }

    if (beats.size() < 16)
    {
        v.problem = "Couldn't follow its beat.";
        return v;
    }

    // The tempo the beats keep (their median spacing), which the tracker may have refined; and the beats carried on at
    // that spacing out to where the sound starts and ends (the tracker can start a bar in, on a quiet intro)
    double gap = 0.0, soundFrom = 0.0, soundTo = v.seconds;
    {
        std::vector<float> gaps;

        for (size_t i = 1; i < beats.size(); ++i)
            gaps.push_back ((float) (beats[i] - beats[i - 1]));

        gap = median (gaps);
        v.tempo = gap > 0.0 ? 60.0 / gap : bpm;

        int first = 0, last = numSamples - 1;

        while (first < numSamples && std::abs (x[(size_t) first]) < 0.01f)
            ++first;

        while (last > first && std::abs (x[(size_t) last]) < 0.01f)
            --last;

        soundFrom = first / rate - gap * 0.25;
        soundTo = last / rate;
    }

    auto extend = [&beats, &gap, &soundFrom, &soundTo]
    {
        while (gap > 0.0 && beats.front() - gap >= soundFrom)
            beats.insert (beats.begin(), beats.front() - gap);

        while (gap > 0.0 && beats.back() + gap <= soundTo)
            beats.push_back (beats.back() + gap);
    };

    extend();

    // The notes (the chords' and the bass's), every hop
    const auto chroma = chromaOf (x.data(), numSamples, rate, shouldStop, progress);

    if (stopped())
    {
        v.problem = "Stopped.";
        return v;
    }

    auto notesBetween = [&chroma] (double from, double to, Chroma& notes, Chroma& bass)
    {
        const double a0 = (from - chroma.offsetSeconds) / chroma.frameSeconds, b0 = (to - chroma.offsetSeconds) / chroma.frameSeconds;
        const int last = (int) chroma.notes.size() - 1;
        int a = std::max (0, (int) std::ceil (a0)), b = std::min (last, (int) std::floor (b0));

        if (b < a)
            a = b = juce::jlimit (0, std::max (0, last), (int) std::round ((a0 + b0) / 2.0));

        for (int f = a; f <= b && f <= last; ++f)
            for (size_t pc = 0; pc < 12; ++pc)
            {
                notes[pc] += chroma.notes[(size_t) f][pc];
                bass[pc] += chroma.bass[(size_t) f][pc];
            }
    };

    // How loud each stretch is: from a running sum of squares, a block of 256 samples a step (5 ms at most)
    constexpr int block = 256;
    const int blocks = numSamples / block;
    std::vector<double> energy ((size_t) blocks + 1, 0.0);

    for (int i = 0; i < blocks; ++i)
    {
        double sum = 0.0;

        for (int k = i * block; k < (i + 1) * block; ++k)
            sum += (double) x[(size_t) k] * x[(size_t) k];

        energy[(size_t) i + 1] = energy[(size_t) i] + sum;
    }

    auto dbBetween = [&] (double from, double to)
    {
        const int a = juce::jlimit (0, blocks, (int) (from * rate / block)), b = juce::jlimit (0, blocks, (int) (to * rate / block));
        return b > a ? (float) (10.0 * std::log10 ((energy[(size_t) b] - energy[(size_t) a]) / ((b - a) * block) + 1.0e-12)) : -100.0f;
    };

    // Where the bars start, and whether the beats heard are the beats or the offbeats between them (funk's horn stabs,
    // house's open hats and reggaeton's claps can be as strong off the beat as on it). A bar could start on any 8th of
    // the beats heard: eight places. Each one's drums against the band's drummer's in every style (the kick on 1, the
    // backbeat on 2 and 4 or a half time's on 3, a one drop's on 3, house's kick on every beat with the open hat
    // between), the chords changing on its bar lines (far likelier there than mid-bar), the loudness stepping there (a
    // section starts on a bar line), and the song's first sound landing on one
    int downbeat = 0;
    {
        // A beat before the first heard, so the song's first sound has an 8th to land on even when the beats heard start
        // half a beat after it
        beats.insert (beats.begin(), beats.front() - gap);
        const auto eighths = eighthsOf (beats);
        const int ne = (int) eighths.size();
        const auto base = drumsOver (eighths);

        // Each 8th's notes, and how much they change from the two beats before an 8th to the two after it
        std::vector<Chroma> notes8 ((size_t) ne), bass8 ((size_t) ne);

        for (int j = 0; j + 1 < ne; ++j)
            notesBetween (eighths[(size_t) j], eighths[(size_t) j + 1], notes8[(size_t) j], bass8[(size_t) j]);

        auto changeAt = [] (const std::vector<Chroma>& per, int j)
        {
            Chroma before {}, after {};

            for (int k = 1; k <= 4; ++k)
                for (size_t pc = 0; pc < 12; ++pc)
                {
                    before[pc] += per[(size_t) (j - k)][pc];
                    after[pc] += per[(size_t) (j + k - 1)][pc];
                }

            return 1.0f - cosine (before, after);
        };

        std::array<double, 8> drums {}, harmony {}, steps {}, score {};
        std::array<int, 8> counted {};

        for (size_t k = 0; k < 8; ++k)
        {
            const auto t = turned (base, k);

            for (auto& reference : references())
                drums[k] = std::max (drums[k], (double) patternMatch (t, reference.second));
        }

        // (The loudness a bar either side: inside a bar it rises and falls with the beat, so shorter would hear that)
        for (int j = 8; j + 8 < ne; ++j)
        {
            const auto k = (size_t) (j % 8);
            harmony[k] += changeAt (notes8, j) + changeAt (bass8, j);
            steps[k] += std::abs (dbBetween (eighths[(size_t) j - 8], eighths[(size_t) j]) - dbBetween (eighths[(size_t) j], eighths[(size_t) j + 8]));
            ++counted[k];
        }

        // The song's first clear onset, and the 8th it lands on
        int firstK = -1;

        for (int f = 0; f < (int) e.size() && firstK < 0; ++f)
            if (e[(size_t) f] >= 2.0f)
            {
                const double t = o.timeOf (f);

                for (int j = 0; j < ne && firstK < 0; ++j)
                    if (std::abs (eighths[(size_t) j] - t) < gap * 0.15)
                        firstK = j % 8;

                break;
            }

        size_t best = 0;

        for (size_t k = 0; k < 8; ++k)
        {
            if (counted[k] > 0)
            {
                harmony[k] /= counted[k];
                steps[k] /= counted[k];
            }

            score[k] = drums[k] + 1.5 * harmony[k] + 0.02 * steps[k] + ((int) k == firstK ? 0.15 : 0.0);

            if (score[k] > score[best])
                best = k;
        }

        // The beats half a beat over when the bar starts between two heard; none before the song's first sound; the
        // downbeat the beat on the first bar line
        double barLine = eighths[best];

        if (best % 2 == 1)
        {
            for (size_t i = 0; i + 1 < beats.size(); ++i)
                beats[i] = (beats[i] + beats[i + 1]) / 2.0;

            beats.pop_back();
            extend();

            // (Halfway between offbeats is the beat only when they're straight: swung, they're late, and so is halfway.
            // The beats moved together to where the onsets are strongest, a sixth of a beat either way at most)
            auto at = [&e, &o] (double t)
            {
                const double f = o.frameAt (t);
                const int a = (int) std::floor (f);
                return a >= 0 && a + 1 < (int) e.size() ? std::max (e[(size_t) a], e[(size_t) a + 1]) : 0.0f;
            };

            const int reach = std::max (1, (int) (gap / 6.0 / o.frameSeconds));
            double bestOffset = 0.0, bestSum = -1.0;

            for (int d = -reach; d <= reach; ++d)
            {
                double sum = 0.0;

                for (auto b : beats)
                    sum += at (b + d * o.frameSeconds);

                if (sum > bestSum + 1.0e-9 || (std::abs (sum - bestSum) <= 1.0e-9 && std::abs (d) < std::abs (bestOffset / o.frameSeconds)))
                {
                    bestSum = sum;
                    bestOffset = d * o.frameSeconds;
                }
            }

            for (auto& b : beats)
                b += bestOffset;

            barLine += bestOffset;
        }

        while (beats.size() > 16 && beats[1] <= soundFrom + gap * 0.35)
            beats.erase (beats.begin());

        while (barLine < beats.front() - gap * 0.5)
            barLine += 4.0 * gap;

        size_t at = 0;

        for (size_t i = 1; i < beats.size(); ++i)
            if (std::abs (beats[i] - barLine) < std::abs (beats[at] - barLine))
                at = i;

        downbeat = (int) (at % 4);
    }

    // Each beat's notes
    const int nb = (int) beats.size();
    std::vector<Chroma> beatNotes ((size_t) nb), beatBass ((size_t) nb);

    for (int i = 0; i + 1 < nb; ++i)
        notesBetween (beats[(size_t) i], beats[(size_t) i + 1], beatNotes[(size_t) i], beatBass[(size_t) i]);

    // Bars: four beats each from the downbeat
    std::vector<Bar> bars;

    for (int i = downbeat; i + 4 < nb; i += 4)
    {
        Bar b;
        b.start = beats[(size_t) i];
        b.end = beats[(size_t) i + 4];

        for (int k = i; k < i + 4; ++k)
            for (size_t pc = 0; pc < 12; ++pc)
            {
                b.notes[pc] += beatNotes[(size_t) k][pc];
                b.bass[pc] += beatBass[(size_t) k][pc];
            }

        b.db = dbBetween (b.start, b.end);

        // Its brightness and its drums, over its frames
        const int from = std::max (0, (int) std::ceil (o.frameAt (b.start))), to = std::min ((int) o.all.size(), (int) std::floor (o.frameAt (b.end)));

        for (int f = from; f < to; ++f)
        {
            b.bright += o.brightness[(size_t) f];
            b.drums += lowE[(size_t) f] + midE[(size_t) f] + highE[(size_t) f];
        }

        if (to > from)
        {
            b.bright /= (float) (to - from);
            b.drums /= (float) (to - from);
        }
        bars.push_back (b);
    }

    if (bars.size() < 4)
    {
        v.problem = "Couldn't find enough bars in it.";
        return v;
    }

    v.firstBar = bars.front().start;

    // The key: every bar's notes together (each bar counted alike, so a loud chorus doesn't outvote the rest)
    {
        std::array<float, 12> all {};

        for (auto b : bars)
        {
            normalise (b.notes);

            for (size_t pc = 0; pc < 12; ++pc)
                all[pc] += b.notes[pc];
        }

        float best = -2.0f, second = -2.0f;

        for (int root = 0; root < 12; ++root)
            for (int minor = 0; minor < 2; ++minor)
            {
                std::array<float, 12> profile {};

                for (int i = 0; i < 12; ++i)
                    profile[(size_t) ((root + i) % 12)] = (minor != 0 ? minorProfile : majorProfile)[i];

                const float r = pearson (all.data(), profile.data(), 12);

                if (r > best)
                {
                    second = best;
                    best = r;
                    v.keyRoot = root;
                    v.mode = minor != 0 ? "minor" : "major";
                }
                else if (r > second)
                    second = r;
            }

        v.keyConfidence = juce::jlimit (0.0f, 1.0f, (best - second) * 5.0f + 0.3f * best);
    }

    // The sound, in words against what the band's ears heard in its own styles: where the song sits between the
    // darkest and the brightest of them, the cleanest and the grittiest, the lightest and the heaviest low end
    v.highShare = (float) o.highShare;
    v.heldFlatness = (float) o.harmonicFlatness;
    v.lowShare = (float) o.lowShare;
    {
        auto placeOf = [] (float value, float ears::Ear::* member, float low, float high, bool logScale)
        {
            if (ears::all().size() >= 4)
            {
                low = 1.0e9f;
                high = -1.0e9f;

                for (auto& ear : ears::all())
                {
                    low = std::min (low, ear.*member);
                    high = std::max (high, ear.*member);
                }
            }

            auto scaled = [logScale] (float x) { return logScale ? std::log (std::max (1.0e-4f, x)) : x; };
            return high > low ? juce::jlimit (0.0f, 1.0f, (scaled (value) - scaled (low)) / (scaled (high) - scaled (low))) : 0.5f;
        };

        v.brightness = placeOf (v.highShare, &ears::Ear::highShare, 0.03f, 0.45f, true);
        v.grit = placeOf (v.heldFlatness, &ears::Ear::heldFlatness, 0.3f, 0.8f, false);
        v.lowEnd = placeOf (v.lowShare, &ears::Ear::lowShare, 0.25f, 0.7f, false);
    }

    // A chord a bar, and what kinds they are
    {
        int minor = 0, seventh = 0, power = 0;

        for (auto& b : bars)
        {
            auto notes = b.notes, bass = b.bass;
            normalise (notes);
            normalise (bass);
            b.notes = notes;
            const auto chord = chordOf (notes, bass, v.keyRoot, v.mode == "minor", v.grit > 0.8f);
            b.chord = nameOf (chord);

            minor += chord.quality == 1 || chord.quality == 4 ? 1 : 0;
            seventh += chord.quality >= 3 ? 1 : 0;
            power += chord.quality == 2 ? 1 : 0;
        }

        const auto count = (float) bars.size();
        v.minorShare = (float) minor / count;
        v.seventhShare = (float) seventh / count;
        v.powerShare = (float) power / count;
    }

    v.sections = sectionsOf (bars);

    if (progress != nullptr)
        progress (0.8f);

    // Swing: where the offbeat onsets land within their beat, the hats' (they carry it: the kick and snare sit on the
    // beat, and a chord can change anywhere), or everything's when there aren't hats enough to go by. The ands
    // (between 0.42 and 0.72 of the beat: straight at a half, a full swing at two thirds), and the e's (0.15 to 0.38:
    // straight at a quarter, a full swing at a third)
    {
        std::vector<float> ands, es;

        auto offbeats = [&] (const std::vector<float>& from)
        {
            ands.clear();
            es.clear();
            const float strong = std::max (0.5f, percentile (from, 0.8f));
            size_t beat = 0;

            for (int f = 1; f + 1 < (int) from.size(); ++f)
            {
                if (from[(size_t) f] < strong || from[(size_t) f] < from[(size_t) f - 1] || from[(size_t) f] < from[(size_t) f + 1])
                    continue;

                const double t = o.timeOf (f);

                while (beat + 1 < beats.size() && beats[beat + 1] <= t)
                    ++beat;

                if (beat + 1 >= beats.size() || t < beats[beat])
                    continue;

                const auto at = (float) ((t - beats[beat]) / (beats[beat + 1] - beats[beat]));

                if (at > 0.42f && at < 0.72f)       ands.push_back (at);
                else if (at > 0.15f && at < 0.38f)  es.push_back (at);
            }
        };

        offbeats (highE);

        if (ands.size() + es.size() < 24)
            offbeats (e);

        v.swingSixteenths = es.size() >= ands.size() / 3 && es.size() >= 16;
        const float s = v.swingSixteenths ? (median (es) - 0.25f) / (1.0f / 12.0f) : (median (ands) - 0.5f) / (1.0f / 6.0f);
        v.swing = s >= 0.12f ? juce::jlimit (0.0f, 1.0f, s) : 0.0f;
    }

    // The drums' pattern: each band's onsets on each 16th of a bar, over the bars (the loudest section's music most)
    {
        Pattern p {};

        for (auto& b : bars)
            for (int step = 0; step < 16; ++step)
            {
                const double t = b.start + (b.end - b.start) * step / 16.0;
                p[0][(size_t) step] += strongestNear (lowE, t);
                p[1][(size_t) step] += strongestNear (midE, t);
                p[2][(size_t) step] += strongestNear (highE, t);
            }

        sharpen (p);
        v.pattern = p;

        // A kick on every beat: as strong on 2 and 4 as on 1 and 3, and well over what's between
        const auto& kick = p[0];
        const float beatsKick = std::min (std::min (kick[0], kick[4]), std::min (kick[8], kick[12]));
        const float between = (kick[2] + kick[6] + kick[10] + kick[14]) / 4.0f;
        v.fourOnFloor = beatsKick > 0.6f && beatsKick > between * 1.8f;

        // Half-time: the backbeat on 3 (the snare's strongest on 8, with 4 and 12 well under it)
        const auto& snare = p[1];
        v.halfTime = snare[8] > 0.8f && snare[4] < 0.55f && snare[12] < 0.55f;
    }

    // The band's style it's nearest. Against what the band's ears heard when it played each one (StyleEars.h): its
    // drums, its sound (how bright, how gritty, how much low end), its chords (minor, sevenths, power chords) and its
    // swing, each difference weighed against how much the styles differ in it; and how near its tempo is to where the
    // style lives. (Without the ears, the drummer's own patterns and the sound by rule)
    {
        auto measures = [] (float high, float held, float low, float minor, float seventh, float power)
        {
            return std::array<float, 6> { std::log (high + 1.0e-3f), held, low, minor, seventh, power };
        };

        const auto song = measures (v.highShare, v.heldFlatness, v.lowShare, v.minorShare, v.seventhShare, v.powerShare);
        std::array<float, 6> spread { 0.5f, 0.1f, 0.1f, 0.2f, 0.2f, 0.2f };

        if (ears::all().size() >= 4)
            for (size_t i = 0; i < 6; ++i)
            {
                double sum = 0.0, squares = 0.0;

                for (auto& ear : ears::all())
                {
                    const auto m = measures (ear.highShare, ear.heldFlatness, ear.lowShare, ear.minorShare, ear.seventhShare, ear.powerShare)[i];
                    sum += m;
                    squares += m * m;
                }

                const double n = (double) ears::all().size(), mean = sum / n;
                spread[i] = std::max (i == 0 ? 0.15f : 0.03f, (float) std::sqrt (std::max (0.0, squares / n - mean * mean)));
            }

        for (auto& style : agents::styles())
        {
            const auto* ear = ears::find (style.id);
            float drums = 0.0f, sound = 0.0f, chords = 0.5f, swing = 0.5f;

            if (ear != nullptr)
            {
                // (Its bar heard from 1 or from 3 alike: a one drop's kick and snare on 3 can be heard either way)
                Pattern heard {}, halfOver {};

                for (size_t band = 0; band < 3; ++band)
                    for (size_t s = 0; s < 16; ++s)
                    {
                        heard[band][s] = ear->pattern[band * 16 + s];
                        halfOver[band][s] = ear->pattern[band * 16 + (s + 8) % 16];
                    }

                drums = std::max (patternMatch (v.pattern, heard), patternMatch (v.pattern, halfOver));
                const auto m = measures (ear->highShare, ear->heldFlatness, ear->lowShare, ear->minorShare, ear->seventhShare, ear->powerShare);
                double apartSound = 0.0, apartChords = 0.0;

                for (size_t i = 0; i < 3; ++i)
                    apartSound += std::pow ((song[i] - m[i]) / spread[i], 2.0f);

                for (size_t i = 3; i < 6; ++i)
                    apartChords += std::pow ((song[i] - m[i]) / spread[i], 2.0f);

                sound = (float) std::exp (-apartSound / 6.0);
                chords = (float) std::exp (-apartChords / 6.0);
                swing = std::exp (-0.5f * std::pow ((v.swing - ear->swing) / 0.15f, 2.0f));
            }
            else
            {
                const auto& protos = prototypes();

                if (const auto it = protos.find (style.id); it != protos.end())
                    drums = patternMatch (v.pattern, it->second);

                sound = soundMatch (v, style);
            }

            const float time = tempoMatch (v.tempo, style);
            StyleGuess g;
            g.style = style.id;
            g.score = 0.4f * drums + 0.25f * time + 0.2f * sound + 0.1f * chords + 0.05f * swing;

            juce::StringArray why;

            if (drums > 0.8f)           why.add ("its drums");
            if (time > 0.9f)            why.add ("its tempo");
            if (sound > 0.6f)           why.add ("its sound");
            if (chords > 0.7f)          why.add ("its chords");

            g.why = why.joinIntoString (", ");
            v.styles.push_back (g);
        }
    }

    std::sort (v.styles.begin(), v.styles.end(), [] (const StyleGuess& a, const StyleGuess& b) { return a.score > b.score; });

    if (progress != nullptr)
        progress (1.0f);

    return v;
}

//==============================================================================
Plan planFrom (const Vibe& v, float closeness, juce::int64 seed, const juce::String& styleAsked)
{
    Plan plan;

    if (! v.ok())
        return plan;

    juce::Random r (seed);
    const float loose = 1.0f - juce::jlimit (0.0f, 1.0f, closeness);
    const auto styleId = styleAsked.isNotEmpty() ? styleAsked : (v.styles.empty() ? juce::String ("rock") : v.styles.front().style);
    const auto* style = agents::findStyle (styleId);

    if (style == nullptr)
        style = agents::findStyle ("rock");

    // The tempo: heard (at the octave the style lives at, or half again when that's where it lives: punk's 8ths at 190
    // can be heard as triplets at 127), moved up to 8% when it's looser
    double tempo = v.tempo;

    if (tempo * 2.0 <= style->fastest && tempo < style->slowest)
        tempo *= 2.0;
    else if (tempo / 2.0 >= style->slowest && tempo > style->fastest)
        tempo /= 2.0;
    else if (tempo < style->slowest && tempo * 1.5 >= style->slowest && tempo * 1.5 <= style->fastest)
        tempo *= 1.5;
    else if (tempo > style->fastest && tempo / 1.5 >= style->slowest && tempo / 1.5 <= style->fastest)
        tempo /= 1.5;

    int bpm = juce::roundToInt (tempo);

    if (loose > 0.0f)
        bpm = juce::roundToInt (tempo * (1.0 + (r.nextFloat() * 2.0f - 1.0f) * 0.08f * loose));

    (bpm == juce::roundToInt (tempo) ? plan.kept : plan.changed).add (juce::String (bpm) + " bpm" + (bpm == juce::roundToInt (tempo) ? juce::String() : " (heard " + juce::String (juce::roundToInt (tempo)) + ")"));

    // The key: heard, or (loosely) its relative, or a few steps away
    int shift = 0;
    auto mode = v.mode;

    if (loose > 0.5f && r.nextFloat() < loose)
    {
        if (r.nextBool())
        {
            shift = v.mode == "minor" ? 3 : -3;     // the relative: same notes, the other mode
            mode = v.mode == "minor" ? "major" : "minor";
        }
        else
            shift = (r.nextBool() ? 1 : -1) * (2 + r.nextInt (4));
    }

    const int keyRoot = ((v.keyRoot + shift) % 12 + 12) % 12;
    const auto key = juce::String (noteNames[keyRoot]) + " " + mode;
    (shift == 0 ? plan.kept : plan.changed).add (shift == 0 ? key : key + " (heard " + v.keyName() + ")");

    // The form: the kinds as heard (a looser take may drop a verse, or add a bridge before the last chorus)
    juce::StringArray form;
    bool reformed = false;

    for (auto& s : v.sections)
        form.add (s.kind);

    if (loose > 0.5f && r.nextFloat() < loose)
    {
        const int lastChorus = lastIndexOf (form, "chorus");

        if (r.nextBool() && ! form.contains ("bridge") && lastChorus > 0)
        {
            form.insert (lastChorus, "bridge");
            plan.changed.add ("a bridge before the last chorus");
            reformed = true;
        }
        else if (form.indexOf ("verse") != lastIndexOf (form, "verse"))
        {
            form.remove (lastIndexOf (form, "verse"));
            plan.changed.add ("a verse fewer");
            reformed = true;
        }
    }

    if (! reformed)
        plan.kept.add ("the form");

    // Each kind's chords: the first time it's heard, its first four (or eight, if they don't repeat) bars, moved to
    // the key and, looser, some swapped for their neighbours
    juce::String chordWords;
    int swapped = 0;
    juce::StringArray done;

    for (auto& s : v.sections)
    {
        if (done.contains (s.kind) || s.chords.isEmpty())
            continue;

        done.add (s.kind);
        juce::StringArray chords;
        const int length = s.chords.size() >= 8 && s.chords[4] != s.chords[0] ? 8 : std::min (4, s.chords.size());

        for (int i = 0; i < length; ++i)
        {
            auto name = transposed (s.chords[i], shift);

            if (loose > 0.0f && r.nextFloat() < 0.5f * loose)
            {
                const auto other = substitute (name, keyRoot, mode == "minor", r);
                swapped += other != name ? 1 : 0;
                name = other;
            }

            chords.add (name);
        }

        chordWords << ", " << s.kind << " chords " << chords.joinIntoString (" ");
    }

    (swapped > 0 ? plan.changed : plan.kept).add (swapped > 0 ? juce::String (swapped) + " chord" + (swapped == 1 ? "" : "s") + " swapped for a neighbour"
                                                              : juce::String ("the chords"));

    // Where it's quiet and where it's loud: a kind 6 dB or more under the loudest soft, the loudest heavy when the song
    // has that much in it
    juce::StringArray soft, heavy;
    float quietest = 0.0f;

    for (auto& s : v.sections)
        quietest = std::min (quietest, s.energyDb);

    for (auto& s : v.sections)
    {
        if (s.energyDb <= -6.0f && s.kind != "intro" && s.kind != "outro")
            soft.addIfNotAlreadyThere (s.kind);
        else if (s.energyDb > -1.5f && quietest <= -6.0f && (s.kind == "chorus" || s.kind == "drop" || s.kind == "hook"))
            heavy.addIfNotAlreadyThere (s.kind);
    }

    juce::String feel;

    for (auto& k : soft)
        feel << ", soft " << k;

    for (auto& k : heavy)
        feel << ", heavy " << k;

    if (soft.size() + heavy.size() > 0)
        plan.kept.add ("where it's quiet and where it's loud");

    // Its half time (the backbeat on 3) and its kick on every beat, when the style doesn't play that way already (as
    // the band's ears heard it play)
    {
        const auto* ear = ears::find (style->id);
        const bool styleHalfTime = ear != nullptr && ear->pattern[16 + 8] > 0.8f && ear->pattern[16 + 4] < 0.55f && ear->pattern[16 + 12] < 0.55f;
        const bool styleOnTheFloor = ear != nullptr && std::min ({ ear->pattern[0], ear->pattern[4], ear->pattern[8], ear->pattern[12] }) > 0.6f;

        if (v.halfTime && ! styleHalfTime)
        {
            feel << ", half-time";
            plan.kept.add ("its half-time feel");
        }

        if (v.fourOnFloor && ! styleOnTheFloor)
        {
            feel << ", four on the floor";
            plan.kept.add ("its kick on every beat");
        }
    }

    // Its swing, said in numbers
    juce::String swing;

    if (v.swing >= 0.15f)
    {
        swing = ", " + juce::String (juce::roundToInt (v.swing * 100.0f)) + "% swing";
        plan.kept.add ("its swing");
    }
    else if (juce::StringArray { "funk", "boombap", "hiphop", "lofi", "neosoul", "reggae", "afrobeats" }.contains (style->id))
        swing = ", straight";

    // Section length: 16 bars where its sections mostly ran that long
    int sixteen = 0, eight = 0;

    for (auto& s : v.sections)
        (s.bars >= 12 ? sixteen : eight) += 1;

    plan.words << juce::String (style->words[0]) << " at " << bpm << " bpm in " << key << swing << feel << chordWords
               << ", form " << form.joinIntoString (" ") << (sixteen > eight ? ", 16 bars" : "");
    plan.kept.insert (0, juce::String (style->name).toLowerCase());
    return plan;
}

} // namespace nightshift::vibes
