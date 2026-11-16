// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "AiPanel.h"
#include "Theme.h"

namespace nightshift
{

using namespace theme;

namespace
{
    constexpr int top = 8;
}

AiPanel::AiPanel()
{
    setSize (900, 360);
}

void AiPanel::setAgents (std::vector<Agent> list)
{
    for (auto& a : agents)
        if (a.panel != nullptr)
            removeChildComponent (a.panel);

    agents = std::move (list);
    shown = -1;
    repaint();
}

void AiPanel::setPrompt (juce::Component* newPrompt, int height)
{
    if (prompt != nullptr)
        removeChildComponent (prompt);

    prompt = newPrompt;
    promptHeight = height;

    if (prompt != nullptr)
        addAndMakeVisible (prompt);

    resized();
}

void AiPanel::show (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) agents.size()))
        return;

    if (index != shown)
    {
        if (juce::isPositiveAndBelow (shown, (int) agents.size()) && agents[(size_t) shown].panel != nullptr)
            removeChildComponent (agents[(size_t) shown].panel);

        shown = index;

        if (auto* p = agents[(size_t) shown].panel)
            addAndMakeVisible (p);
    }

    resized();
    repaint();
}

juce::Rectangle<int> AiPanel::getRowBounds (int index) const
{
    return { 8, top + index * rowHeight, listWidth - 16, rowHeight - 3 };
}

int AiPanel::rowAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) agents.size(); ++i)
        if (getRowBounds (i).contains (p))
            return i;

    return -1;
}

void AiPanel::resized()
{
    // Rows as tall as there's room for: a name and what it does, or just the name when it's tight
    rowHeight = juce::jlimit (28, 52, (getHeight() - 2 * top) / std::max (1, (int) agents.size()));

    // The prompt along the top beside the list, the agent's panel under it
    auto right = getLocalBounds().withTrimmedLeft (listWidth + 1);

    if (prompt != nullptr)
    {
        prompt->setBounds (right.removeFromTop (promptHeight + 4).reduced (12, 0).withTrimmedTop (4));
        right.removeFromTop (1);
    }

    if (juce::isPositiveAndBelow (shown, (int) agents.size()))
        if (auto* p = agents[(size_t) shown].panel)
            p->setBounds (right);
}

void AiPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    // The list, and the line between it and the agent; and one under the prompt
    g.setColour (hairline);
    g.fillRect (listWidth, 0, 1, getHeight());

    if (prompt != nullptr)
        g.fillRect (listWidth + 1, prompt->getBottom(), getWidth() - listWidth - 1, 1);

    for (int i = 0; i < (int) agents.size(); ++i)
    {
        const auto& a = agents[(size_t) i];
        auto r = getRowBounds (i);
        const bool on = i == shown;

        if (on || i == hovered)
        {
            g.setColour (on ? raised : panelLight);
            g.fillRoundedRectangle (r.toFloat(), 8.0f);
        }

        if (on)
        {
            g.setColour (accent);
            g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 9.0f, 3.0f, (float) r.getHeight() - 18.0f, 1.5f);
        }

        auto t = r.reduced (14, rowHeight >= 46 ? 6 : 0);
        const bool both = rowHeight >= 46;
        g.setColour (on ? text : textDim.brighter (0.15f));
        g.setFont (font (16.5f, true));
        g.drawText (a.name, both ? t.removeFromTop (21) : t, juce::Justification::centredLeft, true);

        if (both)
        {
            g.setColour (on ? textDim : textFaint);
            g.setFont (font (16.0f));
            g.drawText (a.what, t, juce::Justification::centredLeft, true);
        }
    }
}

void AiPanel::mouseMove (const juce::MouseEvent& e)
{
    const int over = rowAt (e.getPosition());
    setMouseCursor (over >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);

    if (over != hovered)
    {
        hovered = over;
        repaint (0, 0, listWidth, getHeight());
    }
}

void AiPanel::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint (0, 0, listWidth, getHeight());
}

void AiPanel::mouseDown (const juce::MouseEvent& e)
{
    if (const int row = rowAt (e.getPosition()); row >= 0 && onPick != nullptr)
        onPick (row);
}

juce::String AiPanel::getTooltip()
{
    const int row = rowAt (getMouseXYRelative());

    if (! juce::isPositiveAndBelow (row, (int) agents.size()))
        return {};

    const auto& a = agents[(size_t) row];
    return "The AI " + a.name.toLowerCase() + (a.key.isNotEmpty() ? " (" + a.key + ")" : juce::String()) + ": " + a.what;
}

} // namespace nightshift
