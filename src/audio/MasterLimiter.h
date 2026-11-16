// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "Mastering.h"

namespace nightshift
{

/** The very end of the mix: the master volume, the Master tab's shaping (mastering::Shaper: Tone,
    Punch, Width), then mastering's push into a true-peak limiter (mastering::Maximizer) that
    nothing leaves above the ceiling - like the limiter FL puts on its master, made louder on
    request; and a loudness meter (EBU R128) and a stereo meter on what comes out. The master
    volume is here rather than on Tracktion's own master fader (which comes after the master
    plugins) so turning it up drives the limiter instead of going over the ceiling.

    Settings come from the message thread (setLevels, setShape) and take effect on the next block.
*/
class MasterLimiter : public tracktion::Plugin
{
public:
    explicit MasterLimiter (tracktion::PluginCreationInfo);
    ~MasterLimiter() override;

    static const char* getPluginName()      { return "Master Limiter"; }
    static const char* xmlTypeName;

    juce::String getName() const override                   { return getPluginName(); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getSelectableDescription() override        { return getName(); }
    BusLayout getBusses() const override                    { return BusLayout::singlePassThrough(); }
    double getLatencySeconds() override                     { return latencySeconds; }

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override                            {}
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

    /** The master volume and mastering's push (dB) and the ceiling (dBTP). */
    void setLevels (float masterDb, float pushDb, float ceilingDb);

    /** The master volume as a parameter automation can drive (0..1 like Song::masterVolume,
        0.8 being 0 dB): what the limiter actually uses. setMasterVolume sets it from the fader. */
    tracktion::AutomatableParameter* getVolumeParameter() const    { return volume != nullptr ? volume->parameter.get() : nullptr; }
    void setMasterVolume (float knob);

    /** The shaping (Tone, Punch, Width) from the song's mastering settings. */
    void setShape (const Song::Mastering&);

    /** On: the mix goes out as it comes in, after the master volume and the shaping but with no
        push and no limiting (AudioEngine::captureMix, for working out the push). */
    void setCapturing (bool shouldCapture, bool raw = false) { capturingRaw = raw; capturing = shouldCapture; }

    /** Before and after, at the same loudness (the AI mastering engineer's compare): 0 as mastered;
        1 before - the mix as it comes in, no shaping and no push, only kept under the ceiling;
        2 after - mastered, then turned down by `afterGainDb` to the mix's own loudness. */
    void setCompare (int mode, float afterGainDb)           { compareGainDb = afterGainDb; compare = mode; }

    /** Gain reduction in dB (positive) over the last block, for metering. */
    float getGainReductionDb() const                        { return maximizer.getGainReductionDb(); }

    /** How far each of Punch's bands (lows, mids, highs) is turning down, in dB. */
    float getPunchReductionDb (int band) const              { return shaper.getPunchReductionDb (band); }

    /** How loud what comes out is, and how wide; restarted by resetLoudness (and by initialise). */
    mastering::Readings getLoudness() const                 { return meter.getReadings(); }
    tracktion::StereoFieldAnalyser::Readings getStereo() const { return stereo.getReadings(); }
    void resetLoudness()                                    { meter.requestReset(); stereo.requestReset(); }

private:
    std::unique_ptr<tracktion::ParameterWithStateValue> volume;
    mastering::Shaper shaper;
    mastering::Maximizer maximizer;
    tracktion::LoudnessMeter meter;
    tracktion::StereoFieldAnalyser stereo;
    std::atomic<float> masterDb { 0.0f }, pushDb { 0.0f }, ceilingDb { -1.0f };
    std::atomic<bool> capturing { false }, capturingRaw { false };
    std::atomic<int> compare { 0 };
    std::atomic<float> compareGainDb { 0.0f };
    juce::SmoothedValue<float> compareGain;
    juce::SmoothedValue<float> masterGain;
    float pushNow = 0.0f, ceilingNow = -1.0f;
    bool capturingNow = false;
    double latencySeconds = 0.0;

    // The shaping's settings, handed over without the audio thread ever waiting for them
    juce::SpinLock shapeLock;
    Song::Mastering pendingShape;
    bool shapeChanged = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterLimiter)
};

} // namespace nightshift
