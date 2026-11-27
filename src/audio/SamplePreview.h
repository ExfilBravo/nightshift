// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

namespace nightshift
{

/** A sample heard from the sidebar's Samples: an audio file played through the studio's output, after the master as
    the metronome is (the master's effects, limiter and meter never hear it, and exports never contain it), whether
    or not the song plays. It reads the file ahead on a thread of its own and plays it at the output's rate. Stopped,
    it fades out over the output's next block (JUCE's transport does); another sample asked for while one plays starts
    once that one has faded, so neither clicks. On no track: AudioEngine adds it to the output (playbackContextChanged). */
class SamplePreview : public tracktion::Plugin,
                      private juce::Timer
{
public:
    explicit SamplePreview (tracktion::PluginCreationInfo);
    ~SamplePreview() override;

    static const char* xmlTypeName;

    juce::String getName() const override                   { return "Sample preview"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getSelectableDescription() override        { return getName(); }
    BusLayout getBusses() const override                    { return BusLayout::singleStereoInOut(); }
    bool producesAudioWhenNoAudioInput() override           { return true; }
    bool needsConstantBufferSize() override                 { return false; }

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override                            {}
    void reset() override                                   {}
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

    /** Plays a file from its start (any it was playing stops). False when it isn't audio NightShift can read.
        The message thread. */
    bool play (const juce::File&);
    void stop();

    /** The file playing (or about to, once the one before has faded): none once it's stopped or played to its end. */
    juce::File getPlaying() const;

    /** Lets go of a file a moment after it stopped or played to its end, so it can be moved or deleted. The message
        thread (AudioEngine's timer). */
    void letGoIfDone();

private:
    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAhead { "Sample preview" };
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> source;
    juce::File file;
    std::unique_ptr<juce::AudioFormatReader> next;          // waiting for the one playing to fade
    juce::File nextFile;
    juce::uint32 stoppedAt = 0;
    juce::AudioBuffer<float> scratch;
    double rate = 0.0;

    void start (std::unique_ptr<juce::AudioFormatReader>, const juce::File&);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplePreview)
};

} // namespace nightshift
