// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Stretcher.h"
#include "fx/Dsp.h"

// Signalsmith Stretch (MIT, Geraint Luff / Signalsmith Audio), as the engine ships it in its 3rd_party folder, its
// warnings left to it
#if JUCE_MSVC
 #pragma warning (push)
 #pragma warning (disable: 4100 4127 4189 4244 4267 4305 4456 4458 4459)
#endif
#ifdef __GNUC__
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wfloat-conversion"
 #if __clang__
  #pragma GCC diagnostic ignored "-Wimplicit-float-conversion"
  #pragma GCC diagnostic ignored "-Wimplicit-int-float-conversion"
  #pragma GCC diagnostic ignored "-Wshorten-64-to-32"
  #pragma GCC diagnostic ignored "-Wshadow-field"
 #endif
#endif
#include <tracktion_engine/3rd_party/signalsmith-stretch/signalsmith-stretch.h>
#if JUCE_MSVC
 #pragma warning (pop)
#endif
#ifdef __GNUC__
 #pragma GCC diagnostic pop
#endif

namespace nightshift::stretch
{

namespace
{
    /** Where in the file (in its samples) sample `n` of the render reads: along the map, straight on past its ends (a
        render `outRate` samples a second of the map's time; the file `fileRate`). It's read in order, so it walks
        along rather than searching. */
    struct Along
    {
        const std::vector<std::pair<double, double>>& map;
        double outRate, fileRate;
        size_t at = 0;

        double operator() (double n)
        {
            const double t = n / outRate;
            const double rate = fileRate;

            if (map.size() < 2)
                return map.empty() ? 0.0 : map.front().second * rate;

            while (at + 2 < map.size() && map[at + 1].first <= t)
                ++at;

            while (at > 0 && map[at].first > t)
                --at;

            const auto& a = map[at];
            const auto& b = map[at + 1];
            const double slope = b.first > a.first ? (b.second - a.second) / (b.first - a.first) : 1.0;
            return (a.second + (t - a.first) * slope) * rate;
        }
    };

    /** Reads `z` at a fractional position: a windowed sinc (eight samples either side, Blackman-Harris). */
    float sincAt (const float* z, int length, double position)
    {
        constexpr int half = 8;
        const int base = (int) std::floor (position);
        const double frac = position - base;
        double sum = 0.0;

        for (int k = -half + 1; k <= half; ++k)
        {
            const int i = base + k;

            if (i < 0 || i >= length)
                continue;

            const double x = k - frac;
            const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
            const double w = (x + half) / (2.0 * half);         // 0..1 across the window
            const double window = 0.35875 - 0.48829 * std::cos (juce::MathConstants<double>::twoPi * w)
                                 + 0.14128 * std::cos (2.0 * juce::MathConstants<double>::twoPi * w)
                                 - 0.01168 * std::cos (3.0 * juce::MathConstants<double>::twoPi * w);
            sum += z[i] * sinc * window;
        }

        return (float) sum;
    }

    /** Reads the file into a buffer from `start` (samples, possibly before its start or past its end: silence there). */
    void readInput (juce::AudioFormatReader& reader, juce::AudioBuffer<float>& buffer, juce::int64 start, int count)
    {
        if (buffer.getNumSamples() < count)
            buffer.setSize (buffer.getNumChannels(), count, false, false, true);

        buffer.clear (0, count);

        if (count > 0)
            reader.read (&buffer, 0, count, start, true, buffer.getNumChannels() > 1);
    }

    bool stopped (const std::atomic<bool>* stop)  { return stop != nullptr && stop->load(); }
}

juce::String Job::key() const
{
    // Everything that makes it: the file (and its size and date, should it change), the pitch, and the map
    juce::String all;
    all << source.getFullPathName() << "|" << source.getSize() << "|" << source.getLastModificationTime().toMilliseconds()
        << "|" << semitones << "|render 1";

    for (auto& [t, f] : map)
        all << "|" << juce::String (t, 6) << ":" << juce::String (f, 6);

    return source.getFileNameWithoutExtension().retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_").substring (0, 40).trim()
             + " " + juce::String::toHexString (all.hashCode64());
}

juce::File cacheFolder()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NightShift stretched audio");
}

juce::File cachedFile (const Job& job)
{
    return cacheFolder().getChildFile (job.key() + ".wav");
}

void tidyCache()
{
    const auto weeksAgo = juce::Time::getCurrentTime() - juce::RelativeTime::days (14);

    for (const auto& entry : juce::RangedDirectoryIterator (cacheFolder(), false, "*.wav;*.part", juce::File::findFiles))
        if (entry.getModificationTime() < weeksAgo)
            entry.getFile().deleteFile();
}

bool render (const Job& job, const juce::File& dest, Kind kind, const std::atomic<bool>* stop)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (job.source));

    if (reader == nullptr || reader->sampleRate <= 0.0 || job.map.size() < 2 || job.length() <= 0.0)
        return false;

    const double rate = reader->sampleRate;
    const int channels = juce::jlimit (1, 8, (int) reader->numChannels);
    const auto total = (juce::int64) std::ceil (job.length() * rate);
    constexpr int block = 1024;

    // Written beside where it goes, then moved there: a render half done is never taken for one
    dest.getParentDirectory().createDirectory();
    const auto part = dest.getSiblingFile (dest.getFileNameWithoutExtension() + ".part");
    part.deleteFile();
    std::unique_ptr<juce::AudioFormatWriter> writer;
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (part);
        writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                     .withSampleRate (rate).withNumChannels (channels).withBitsPerSample (32)
                                                                     .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    }

    if (writer == nullptr)
        return false;

    juce::AudioBuffer<float> in (channels, block * 8), out (channels, block);
    bool ok = true;

    if (kind == Kind::signalsmith)
    {
        // Pitched, it's stretched to p times its length at its own pitch (time-stretching keeps every frequency as it
        // is: Signalsmith's own transposing came out 4.5 cents sharp on a sine), then read back p times faster, so its
        // length comes back and every frequency is p times higher (going up, low-passed first so nothing folds over)
        const double p = std::pow (2.0, (double) job.semitones / 12.0);
        const bool pitched = std::abs (p - 1.0) > 1.0e-6;
        const auto made = pitched ? (juce::int64) std::ceil ((double) total * p) + 32 : total;
        Along along { job.map, rate * (pitched ? p : 1.0), rate };
        juce::AudioBuffer<float> stretched (channels, pitched ? (int) made : 1);
        stretched.clear();

        signalsmith::stretch::SignalsmithStretch<float> s;
        s.presetDefault (channels, (float) rate);

        // Lined up by Signalsmith's own seek: given the input from where the render starts (and as much more as it
        // looks ahead at the rate there), its next output is that start
        const double startAt = along (0.0);
        const double rateThere = (along ((double) block) - startAt) / block;
        const int seekLength = s.outputSeekLength ((float) rateThere);
        const auto start = (juce::int64) std::floor (startAt + 0.5);
        readInput (*reader, in, start, seekLength);
        s.outputSeek (in.getArrayOfWritePointers(), seekLength);
        juce::int64 fed = start + seekLength;

        // Then each block of output takes the input up to where the map is a latency ahead of its end: what the
        // stretcher hears first comes out that much later, so a tempo change is heard where it lands
        for (juce::int64 n = 0; n < made && ok; n += block)
        {
            const int count = (int) std::min<juce::int64> (block, made - n);
            const auto upTo = (juce::int64) std::floor (along ((double) (n + count + s.outputLatency())) + s.inputLatency() + 0.5);
            const int toFeed = (int) std::max<juce::int64> (0, upTo - fed);

            readInput (*reader, in, fed, toFeed);
            out.clear();
            s.process (in.getArrayOfWritePointers(), toFeed, out.getArrayOfWritePointers(), count);
            fed += toFeed;

            if (pitched)
                for (int c = 0; c < channels; ++c)
                    stretched.copyFrom (c, (int) n, out, c, 0, count);
            else
                ok = writer->writeFromAudioSampleBuffer (out, 0, count);

            ok = ok && ! stopped (stop);
        }

        if (pitched && ok)
        {
            // Going up: an eighth-order Butterworth low-pass just under where the top will fold, forwards and back
            // (no delay)
            if (p > 1.0)
            {
                const double cutoff = 0.45 * rate / p;

                for (int c = 0; c < channels; ++c)
                {
                    auto* z = stretched.getWritePointer (c);

                    for (int way = 0; way < 2; ++way)
                        for (double q : { 0.5098, 0.6013, 0.9000, 2.5629 })
                        {
                            dsp::Biquad lp;
                            lp.c = dsp::Coeffs::lowPass (cutoff, q, rate);

                            if (way == 0)
                                for (juce::int64 i = 0; i < made; ++i)
                                    z[i] = lp.process (z[i], 0);
                            else
                                for (juce::int64 i = made; --i >= 0;)
                                    z[i] = lp.process (z[i], 0);
                        }
                }
            }

            // Read back p times faster
            for (juce::int64 n = 0; n < total && ok; n += block)
            {
                const int count = (int) std::min<juce::int64> (block, total - n);

                for (int c = 0; c < channels; ++c)
                    for (int i = 0; i < count; ++i)
                        out.setSample (c, i, sincAt (stretched.getReadPointer (c), (int) made, (double) (n + i) * p));

                ok = writer->writeFromAudioSampleBuffer (out, 0, count) && ! stopped (stop);
            }
        }
    }
    else
    {
        Along along { job.map, rate, rate };
        // SoundTouch through the engine, at the rate of each block (its own latency taken off the front)
        tracktion::TimeStretcher soundTouch;
        soundTouch.initialise (rate, block, channels, tracktion::TimeStretcher::soundtouchBetter, {}, false);
        juce::int64 produced = 0, fed = (juce::int64) std::floor (along (0.0) + 0.5);
        int skip = soundTouch.getLatencySamples();
        std::vector<float> pending;
        juce::AudioBuffer<float> made (channels, block * 4);

        while (produced < total && ok)
        {
            const double rateNow = (along ((double) produced + block) - along ((double) produced)) / block;
            soundTouch.setSpeedAndPitch ((float) (1.0 / std::max (0.01, rateNow)), job.semitones);
            const int needed = soundTouch.getFramesNeeded();

            if (needed < 0)
                break;

            readInput (*reader, in, fed, needed);
            fed += needed;
            made.clear();
            const int got = soundTouch.processData (in.getArrayOfReadPointers(), needed, made.getArrayOfWritePointers());
            const int from = std::min (got, skip);
            skip -= from;
            const int keep = (int) std::min<juce::int64> (got - from, total - produced);

            if (keep > 0)
            {
                ok = writer->writeFromAudioSampleBuffer (made, from, keep) && ! stopped (stop);
                produced += keep;
            }
        }
    }

    writer = nullptr;

    if (! ok)
    {
        part.deleteFile();
        return false;
    }

    dest.deleteFile();
    return part.moveFileTo (dest);
}

} // namespace nightshift::stretch
