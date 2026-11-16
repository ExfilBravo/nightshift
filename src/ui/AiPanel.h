// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

namespace nightshift
{

/** The editor's AI tab: every AI agent in one place. Down the left, the agents - Song (a whole
    song from one prompt), Drums, Bass, Guitar, Keys, Synth, Master - each with what it does and
    its key; beside them, the prompt to type into, and under it the one picked (its own panel, as it
    always was). Picking one also points the prompt at it (the studio does that, through onPick). */
class AiPanel : public juce::Component,
                public juce::TooltipClient
{
public:
    struct Agent
    {
        juce::String name, what, key;
        juce::Component* panel = nullptr;
    };

    AiPanel();

    void setAgents (std::vector<Agent>);
    int getNumAgents() const                        { return (int) agents.size(); }

    /** The prompt (the studio's DrummerBar, which talks to whichever agent is picked), along the
        top beside the list, over the agent's panel. */
    void setPrompt (juce::Component* prompt, int height);

    /** Shows an agent's panel beside the list. */
    void show (int index);
    int getShown() const                            { return shown; }

    /** An agent clicked in the list. */
    std::function<void (int)> onPick;

    /** For tests: where an agent's row is. */
    juce::Rectangle<int> getRowBounds (int index) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    static constexpr int listWidth = 196;

private:
    std::vector<Agent> agents;
    int shown = -1, hovered = -1, rowHeight = 52;
    juce::Component* prompt = nullptr;
    int promptHeight = 0;

    int rowAt (juce::Point<int>) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiPanel)
};

} // namespace nightshift
