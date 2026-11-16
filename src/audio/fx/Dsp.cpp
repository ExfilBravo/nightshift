// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Dsp.h"

namespace nightshift::dsp
{

namespace
{
    constexpr double pi = juce::MathConstants<double>::pi;

    double clampFreq (double freq, double sampleRate)
    {
        return juce::jlimit (5.0, sampleRate * 0.49, freq);
    }

    Coeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }
}

Coeffs Coeffs::lowPass (double freq, double q, double sampleRate)
{
    const double w = 2.0 * pi * clampFreq (freq, sampleRate) / sampleRate;
    const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
    return normalise ((1.0 - cw) / 2.0, 1.0 - cw, (1.0 - cw) / 2.0, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
}

Coeffs Coeffs::highPass (double freq, double q, double sampleRate)
{
    const double w = 2.0 * pi * clampFreq (freq, sampleRate) / sampleRate;
    const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
    return normalise ((1.0 + cw) / 2.0, -(1.0 + cw), (1.0 + cw) / 2.0, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
}

Coeffs Coeffs::bell (double freq, double q, double gainDb, double sampleRate)
{
    const double a = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * pi * clampFreq (freq, sampleRate) / sampleRate;
    const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
    return normalise (1.0 + alpha * a, -2.0 * cw, 1.0 - alpha * a, 1.0 + alpha / a, -2.0 * cw, 1.0 - alpha / a);
}

Coeffs Coeffs::lowShelf (double freq, double gainDb, double sampleRate)
{
    // Shelf slope 1: the steepest without a bump
    const double a = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * pi * clampFreq (freq, sampleRate) / sampleRate;
    const double cw = std::cos (w), alpha = std::sin (w) / 2.0 * std::sqrt (2.0);
    const double sa = 2.0 * std::sqrt (a) * alpha;

    return normalise (a * ((a + 1.0) - (a - 1.0) * cw + sa),
                      2.0 * a * ((a - 1.0) - (a + 1.0) * cw),
                      a * ((a + 1.0) - (a - 1.0) * cw - sa),
                      (a + 1.0) + (a - 1.0) * cw + sa,
                      -2.0 * ((a - 1.0) + (a + 1.0) * cw),
                      (a + 1.0) + (a - 1.0) * cw - sa);
}

Coeffs Coeffs::highShelf (double freq, double gainDb, double sampleRate)
{
    const double a = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * pi * clampFreq (freq, sampleRate) / sampleRate;
    const double cw = std::cos (w), alpha = std::sin (w) / 2.0 * std::sqrt (2.0);
    const double sa = 2.0 * std::sqrt (a) * alpha;

    return normalise (a * ((a + 1.0) + (a - 1.0) * cw + sa),
                      -2.0 * a * ((a - 1.0) + (a + 1.0) * cw),
                      a * ((a + 1.0) + (a - 1.0) * cw - sa),
                      (a + 1.0) - (a - 1.0) * cw + sa,
                      2.0 * ((a - 1.0) - (a + 1.0) * cw),
                      (a + 1.0) - (a - 1.0) * cw - sa);
}

double Coeffs::magnitudeDb (double freq, double sampleRate) const
{
    const double w = 2.0 * pi * freq / sampleRate;
    const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);

    const double numRe = b0 + b1 * c1 + b2 * c2, numIm = -(b1 * s1 + b2 * s2);
    const double denRe = 1.0 + a1 * c1 + a2 * c2, denIm = -(a1 * s1 + a2 * s2);
    const double power = (numRe * numRe + numIm * numIm) / std::max (1.0e-30, denRe * denRe + denIm * denIm);
    return 10.0 * std::log10 (std::max (1.0e-30, power));
}

//==============================================================================
void Svf::set (double freq, double q, double sampleRate)
{
    g = std::tan (pi * clampFreq (freq, sampleRate) / sampleRate);
    k = 1.0 / std::max (0.05, q);
    a1 = 1.0 / (1.0 + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

double Svf::magnitudeDb (int mode, double freq, double sampleRate) const
{
    // The analogue prototype with the same warped cutoff: s / wc at the warped frequency
    const double x = std::tan (pi * std::min (freq, sampleRate * 0.499) / sampleRate) / g;
    const double denRe = 1.0 - x * x, denIm = k * x;
    const double den = denRe * denRe + denIm * denIm;
    const double num = mode == 0 ? 1.0 : mode == 1 ? (k * x) * (k * x) : x * x * x * x;
    return 10.0 * std::log10 (std::max (1.0e-30, num / std::max (1.0e-30, den)));
}

} // namespace nightshift::dsp
