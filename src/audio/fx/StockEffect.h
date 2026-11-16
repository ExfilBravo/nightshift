// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "Dsp.h"

namespace nightshift
{

/** What Nightshift's stock effects share: stereo in and out, and parameters kept in the
    plugin's state (so an Effect's saved state carries them) that can be automated.
    Editors find parameters by id and set them through the AutomatableParameter.
*/
class StockEffect : public tracktion::Plugin
{
public:
    explicit StockEffect (tracktion::PluginCreationInfo);
    ~StockEffect() override;

    BusLayout getBusses() const override                        { return BusLayout::singleStereoInOut(); }
    int getNumOutputChannelsGivenInputs (int) override          { return 2; }
    juce::String getSelectableDescription() override            { return getName(); }
    bool needsConstantBufferSize() override                     { return false; }

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override                                {}
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    struct Param
    {
        juce::String id, name;
        float defaultValue = 0.0f;
        std::unique_ptr<tracktion::ParameterWithStateValue> value;

        float get() const                                       { return value->getCurrentValue(); }
        juce::NormalisableRange<float> range;
        juce::String text() const                               { return value->getParameter().getCurrentValueAsString(); }
        void set (float v)                                      { value->setParameter (v, juce::sendNotification); }
    };

    Param* findParam (const juce::String& id) const;
    const std::vector<std::unique_ptr<Param>>& getParams() const   { return params; }

    /** Puts every parameter back to its default. */
    void resetParams();

protected:
    double sampleRate = 48000.0;

    Param& addParam (const juce::String& id, const juce::String& name, float defaultValue,
                     juce::NormalisableRange<float>, std::function<juce::String (float)> toText);

    /** Called from initialise once the sample rate is known: size buffers, then clear them. */
    virtual void prepare() = 0;

    /** The stereo pair to work on in a render call (one channel is used for both if the
        track is mono). Returns false if there's nothing to do. */
    static bool getStereo (const tracktion::PluginRenderContext&, float*& left, float*& right);

    static juce::NormalisableRange<float> frequencyRange (float low, float high);
    static juce::NormalisableRange<float> toggleRange()       { return { 0.0f, 1.0f, 1.0f }; }
    static std::function<juce::String (float)> hz();
    static std::function<juce::String (float)> db();
    static std::function<juce::String (float)> ms();
    static std::function<juce::String (float)> percent();
    static std::function<juce::String (float)> onOff();

private:
    std::vector<std::unique_ptr<Param>> params;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StockEffect)
};

} // namespace nightshift
