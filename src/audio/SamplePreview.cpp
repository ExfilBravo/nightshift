// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "SamplePreview.h"

namespace nightshift
{

const char* SamplePreview::xmlTypeName = "nightshiftSamplePreview";

namespace
{
    // The most it plays in one go; a bigger block is played in turns (nothing's allocated on the audio thread)
    constexpr int maxChunk = 4096;
}

SamplePreview::SamplePreview (tracktion::PluginCreationInfo info)
    : tracktion::Plugin (info)
{
    formats.registerBasicFormats();
    scratch.setSize (2, maxChunk);
    readAhead.startThread (juce::Thread::Priority::normal);
}

SamplePreview::~SamplePreview()
{
    stopTimer();
    notifyListenersOfDeletion();
    transport.setSource (nullptr);
    source.reset();
    readAhead.stopThread (1000);
}

void SamplePreview::initialise (const tracktion::PluginInitialisationInfo& info)
{
    // (the graph is built again now and then: only another rate needs the player made ready again)
    if (info.sampleRate != rate)
    {
        rate = info.sampleRate;
        transport.prepareToPlay (maxChunk, rate);
    }
}

bool SamplePreview::play (const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (f.existsAsFile() ? formats.createReaderFor (f) : nullptr);

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return false;

    // One playing fades out over the output's next block first (a few milliseconds), then this one starts
    if (transport.isPlaying())
    {
        transport.stop();
        next = std::move (reader);
        nextFile = f;
        startTimer (30);
        return true;
    }

    start (std::move (reader), f);
    return true;
}

void SamplePreview::timerCallback()
{
    stopTimer();

    if (next != nullptr)
        start (std::move (next), nextFile);
}

void SamplePreview::start (std::unique_ptr<juce::AudioFormatReader> reader, const juce::File& f)
{
    const double fileRate = reader->sampleRate;
    auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    next.reset();
    nextFile = juce::File();

    // Read ahead on a thread of its own, played at the output's rate
    transport.stop();
    transport.setSource (newSource.get(), 32768, &readAhead, fileRate, 2);
    source = std::move (newSource);     // (the one it played before goes now: the transport has let go of it)
    transport.setPosition (0.0);
    file = f;
    transport.start();
}

void SamplePreview::stop()
{
    // It fades over the output's next block; the file is let go of a moment later (letGoIfDone)
    stopTimer();
    next.reset();
    nextFile = juce::File();
    transport.stop();
    file = juce::File();
    stoppedAt = juce::Time::getMillisecondCounter();
}

juce::File SamplePreview::getPlaying() const
{
    return next != nullptr ? nextFile : transport.isPlaying() ? file : juce::File();
}

void SamplePreview::letGoIfDone()
{
    if (source != nullptr && next == nullptr && ! transport.isPlaying() && juce::Time::getMillisecondCounter() - stoppedAt > 250)
    {
        transport.setSource (nullptr);
        source.reset();
    }
}

void SamplePreview::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0)
        return;

    auto& buffer = *fc.destBuffer;
    const int start = fc.bufferStartSample, numSamples = fc.bufferNumSamples;

    // Its input is silence: it only adds the sample to what the master plays (as the metronome adds its clicks).
    // The transport is asked even when it's stopped: the block after a stop is its fade out, then it's silence
    buffer.clear (start, numSamples);

    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (numSamples - done, maxChunk);
        juce::AudioSourceChannelInfo info (&scratch, 0, n);
        transport.getNextAudioBlock (info);

        // A mono file plays on both sides (the reader copies it to the second channel)
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom (ch, start + done, scratch, std::min (ch, 1), 0, n);

        done += n;
    }
}

} // namespace nightshift
