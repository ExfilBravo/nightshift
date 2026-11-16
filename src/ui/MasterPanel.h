// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "model/Song.h"
#include "Widgets.h"

namespace nightshift
{

/** The Master tab (F12): getting a song ready to put out, in plain words.

    On the left, what it's for - just keep it from clipping, Streaming (-14 LUFS), Apple Music
    (-16), Loud (-9) - and Set loudness, which listens to the whole song (a capture of the mix as
    it goes into the limiter) and works out how hard to push it to land there, without anything
    going over the ceiling. In the middle, how loud it is as it plays: now (the last 3 seconds),
    the whole song since Play, the peak (between the samples too) and the range from quiet to
    loud. On the right, the last 30 seconds of it against the target, and the knobs: Louder (the
    push), Never above (the ceiling), and how hard the limiter is squeezing.

    The second page (the switch in the editor's header) shapes the sound before the limiter, a
    card each with a switch to hear it without: Tone (Bass, Body, Presence, Air, each on
    everything, the centre or the sides, drawn as a curve), Punch (Amount, and each band's share
    of it, with how hard lows, mids and highs are squeezing) and Width (Width, Mono bass, and how
    the song holds up in mono). */
class MasterPanel : public juce::Component,
                    public juce::TooltipClient,
                    private Song::Listener,
                    private juce::Timer
{
public:
    MasterPanel (Song&, AudioEngine&);
    ~MasterPanel() override;

    /** Set loudness: captures the mix (on this thread, a few seconds), then works out the push
        on a thread of its own and sets it. Nothing for "safe". */
    void setLoudness();
    bool isWorking() const                          { return working; }

    /** What it last said about Set loudness, and where things are, for tests. */
    juce::String getStatus() const                  { return status; }
    juce::Rectangle<int> getTargetBounds (const juce::String& id) const;
    juce::Button& getSetButton()                    { return setButton; }
    juce::Slider& getLouderKnob()                   { return louder; }

    /** Picks a target as a click on it does. */
    void chooseTarget (const juce::String& id);

    /** 0: loudness; 1: tone, punch and width. */
    void setPage (int);

    /** "Let the AI master it": the AI tab's mastering engineer. */
    std::function<void()> onOpenAiMaster;
    juce::Button& getAiMasterButton()               { return aiButton; }
    int getPage() const                             { return page; }

    /** The second page's controls and where things are, for tests. */
    juce::Slider& getToneKnob (int band)            { return toneKnob[band]; }
    juce::Slider& getPunchKnob()                    { return punchKnob; }
    juce::Slider& getWidthKnob()                    { return widthKnob; }
    juce::Slider& getMonoBassKnob()                 { return monoBassKnob; }
    juce::Rectangle<int> getToneWhereBounds (int band) const    { return whereChips[band]; }
    juce::Rectangle<int> getSwitchBounds (int section) const    { return switches[section]; }   // 0 tone, 1 punch, 2 width

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    Song& song;
    AudioEngine& audio;

    juce::TextButton setButton;
    Knob louder { -12.0, 24.0, 0.0, true }, ceiling { -3.0, 0.0, -1.0 };

    juce::TextButton aiButton;

    // The second page
    int page = 0;
    Knob toneKnob[4] { { -6.0, 6.0, 0.0, true }, { -6.0, 6.0, 0.0, true }, { -6.0, 6.0, 0.0, true }, { -6.0, 6.0, 0.0, true } };
    Knob punchKnob { 0.0, 1.0, 0.0 };
    Knob shareKnob[3] { { 0.0, 1.5, 1.0 }, { 0.0, 1.5, 1.0 }, { 0.0, 1.5, 1.0 } };
    Knob widthKnob { 0.0, 2.0, 1.0, true }, monoBassKnob { 0.0, 300.0, 0.0 };
    juce::Rectangle<int> cards[3], switches[3], curveArea, whereChips[4], punchBars, stereoArea;
    float punchShown[3] {};
    tracktion::StereoFieldAnalyser::Readings stereo;
    int hoveredChip = -1, hoveredSwitch = -1;

    mastering::Readings readings;
    float squeezeDb = 0.0f;
    std::vector<float> history;                     // the last 30 s of "loudness now", 10 a second
    int ticks = 0;
    bool wasPlaying = false;

    juce::String status;
    bool working = false;
    std::atomic<float> progress { 0.0f };
    std::shared_ptr<std::atomic<float>> progressSource;   // the working-out thread's, while it runs
    std::shared_ptr<std::atomic<bool>> stillWanted = std::make_shared<std::atomic<bool>> (true);

    juce::Rectangle<int> targetsArea, readoutsArea, graphArea, knobsArea, statusArea;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> targetRows;
    juce::String hoveredTarget;

    void songChanged (Song::Change) override;
    void timerCallback() override;
    void refreshControls();
    void setMastering (std::function<void (Song::Mastering&)>);
    void finished (const mastering::Push&, float targetLufs, const juce::String& targetName);

    void paintReadout (juce::Graphics&, juce::Rectangle<int>, const juce::String& label, float value, bool valid,
                       const juce::String& unit, const juce::String& note, juce::Colour);
    void paintGraph (juce::Graphics&);
    void paintKnobsAndSqueeze (juce::Graphics&);

    void layOutShapePage();
    void paintShapePage (juce::Graphics&);
    void paintToneCurve (juce::Graphics&);
    void paintOverChildren (juce::Graphics&) override;
    bool sectionOn (int section) const;
    void toggleSection (int section);
    void cycleWhere (int band);
    void showPageControls();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterPanel)
};

} // namespace nightshift
