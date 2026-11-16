// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** Building blocks for the stock effects: biquads, a state-variable filter and a delay line
    read between samples. */
namespace nightshift::dsp
{

/** Normalised biquad coefficients (a0 = 1), from Robert Bristow-Johnson's cookbook. */
struct Coeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static Coeffs lowPass (double freq, double q, double sampleRate);
    static Coeffs highPass (double freq, double q, double sampleRate);
    static Coeffs bell (double freq, double q, double gainDb, double sampleRate);
    static Coeffs lowShelf (double freq, double gainDb, double sampleRate);
    static Coeffs highShelf (double freq, double gainDb, double sampleRate);

    /** The filter's gain at a frequency, in dB. */
    double magnitudeDb (double freq, double sampleRate) const;
};

/** A biquad for each of two channels, transposed direct form II. */
struct Biquad
{
    Coeffs c;
    double s1[2] {}, s2[2] {};

    void reset()                        { s1[0] = s1[1] = s2[0] = s2[1] = 0.0; }

    float process (float x, int ch) noexcept
    {
        const double y = c.b0 * x + s1[ch];
        s1[ch] = c.b1 * x - c.a1 * y + s2[ch];
        s2[ch] = c.b2 * x - c.a2 * y;
        return (float) y;
    }
};

/** Zavalishin's topology-preserving state-variable filter: low, band and high pass at once,
    and stable however fast its cutoff moves. Two channels. */
struct Svf
{
    double g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double ic1[2] {}, ic2[2] {};

    void set (double freq, double q, double sampleRate);
    void reset()                        { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0; }

    struct Out { double low, band, high; };

    Out process (double x, int ch) noexcept
    {
        const double v3 = x - ic2[ch];
        const double v1 = a1 * ic1[ch] + a2 * v3;
        const double v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2.0 * v1 - ic1[ch];
        ic2[ch] = 2.0 * v2 - ic2[ch];
        return { v2, v1, x - k * v1 - v2 };
    }

    /** The response of the low (0), band (1) or high (2) pass output at a frequency, in dB. */
    double magnitudeDb (int mode, double freq, double sampleRate) const;
};

/** A one-pole low pass, for smoothing and gentle damping. */
struct OnePole
{
    float a = 0.0f, z = 0.0f;

    void setCutoff (double freq, double sampleRate)
    {
        a = (float) std::exp (-juce::MathConstants<double>::twoPi * freq / sampleRate);
    }

    float process (float x) noexcept    { return z = x + a * (z - x); }
};

/** A delay line read at fractional delays with cubic (Hermite) interpolation. A delay of 1
    is the sample pushed last; delays are kept to at least 2 so the curve never reads ahead. */
struct DelayLine
{
    std::vector<float> buffer;
    int mask = 0, write = 0;

    void prepare (int maxDelaySamples)
    {
        const int size = juce::nextPowerOfTwo (maxDelaySamples + 4);
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        write = 0;
    }

    void reset()                        { std::fill (buffer.begin(), buffer.end(), 0.0f); }
    void push (float x) noexcept        { buffer[(size_t) write] = x; write = (write + 1) & mask; }

    float read (double delay) const noexcept
    {
        delay = juce::jlimit (2.0, (double) mask - 2.0, delay);
        const double pos = write - delay;
        const double floorPos = std::floor (pos);
        const int i = (int) floorPos;
        const float t = (float) (pos - floorPos);

        const float xm1 = buffer[(size_t) ((i - 1) & mask)], x0 = buffer[(size_t) (i & mask)];
        const float x1 = buffer[(size_t) ((i + 1) & mask)], x2 = buffer[(size_t) ((i + 2) & mask)];

        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    /** Whole-sample read, for fixed delays. */
    float tap (int delay) const noexcept    { return buffer[(size_t) ((write - delay) & mask)]; }
};

/** Dry and wet gains for a mix knob: the dry stays whole up to half way, then fades, while
    the wet comes up to full by half way, so 50% is both at full. */
inline void mixGains (float mix, float& dry, float& wet)
{
    dry = std::min (1.0f, 2.0f * (1.0f - mix));
    wet = std::min (1.0f, 2.0f * mix);
}

} // namespace nightshift::dsp
