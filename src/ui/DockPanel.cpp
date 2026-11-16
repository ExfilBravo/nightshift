// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "DockPanel.h"
#include "Widgets.h"
#include "Theme.h"

namespace nightshift
{

using namespace theme;

DockPanel::DockPanel (const juce::String& t)
    : title (t)
{
}

void DockPanel::setTitle (const juce::String& t, const juce::String& s)
{
    if (t == title && s == subtitle)
        return;

    title = t;
    subtitle = s;
    resized();
    repaint (0, 0, getWidth(), headerHeight);
}

void DockPanel::setHeaderVisible (bool shouldShow)
{
    headerVisible = shouldShow;
    resized();
    repaint();
}

void DockPanel::setContent (juce::Component* c)
{
    if (content != nullptr)
        removeChildComponent (content);

    content = c;

    if (content != nullptr)
        addAndMakeVisible (content, 0);

    resized();
}

void DockPanel::setTitleComponent (juce::Component* c, int width)
{
    titleComponent = c;
    titleComponentWidth = width;

    if (c != nullptr)
        addAndMakeVisible (c);

    resized();
}

void DockPanel::addToHeader (juce::Component& c, int width, bool onRight, int height, bool optional)
{
    items.push_back ({ &c, width, height, onRight, optional });
    addAndMakeVisible (c);
    resized();
}

void DockPanel::setSubHeader (juce::Component* c, int height)
{
    if (subHeader != nullptr)
        removeChildComponent (subHeader);

    subHeader = c;
    subHeaderHeight = height;

    if (subHeader != nullptr)
        addAndMakeVisible (subHeader);

    resized();
}

void DockPanel::setHeaderItemWidth (juce::Component& c, int width)
{
    for (auto& item : items)
        if (item.component == &c && item.width != width)
        {
            item.width = width;
            resized();
        }
}

int DockPanel::titleWidth() const
{
    if (titleComponent != nullptr)
        return titleComponentWidth;

    int w = (int) juce::GlyphArrangement::getStringWidth (font (17.5f, true), title);

    if (subtitle.isNotEmpty())
        w += 12 + (int) juce::GlyphArrangement::getStringWidth (font (17.0f), subtitle);

    return w;
}

void DockPanel::resized()
{
    auto r = getLocalBounds();

    if (headerVisible)
    {
        auto header = r.removeFromTop (headerHeight).reduced (12, 0).withTrimmedLeft (2);
        r.removeFromTop (1);

        // Segmented controls narrow to their short labels, dropdowns (chips) to 90 px with their
        // labels cut short; buttons and tool strips keep their width
        auto minimumOf = [] (juce::Component* c, int width)
        {
            if (auto* segmented = dynamic_cast<SegmentedControl*> (c))
                return std::min (width, segmented->getMinimumWidth());

            if (dynamic_cast<Chip*> (c) != nullptr)
                return std::min (width, 90);

            return width;
        };

        int titleW = titleComponent != nullptr ? titleComponentWidth : titleWidth();
        const int titleMinimum = titleComponent != nullptr ? minimumOf (titleComponent, titleComponentWidth) : titleW;
        const int room = header.getWidth() - 14;
        std::vector<bool> placed (items.size(), false);
        std::vector<int> widths (items.size());
        int itemsWidth = 0, itemsMinimum = 0;

        auto measure = [&]
        {
            itemsWidth = itemsMinimum = 0;

            for (size_t i = 0; i < items.size(); ++i)
                if (placed[i])
                {
                    itemsWidth += widths[i] + 8;
                    itemsMinimum += minimumOf (items[i].component, widths[i]) + 8;
                }
        };

        for (size_t i = 0; i < items.size(); ++i)
        {
            placed[i] = items[i].component->isVisible();
            widths[i] = items[i].width;
        }

        measure();

        // Still too wide at the narrowest: a button with words goes down to its icon alone (its tooltip says
        // the rest), then the optional controls step aside, the last added first each time
        for (size_t i = items.size(); i-- > 0 && titleMinimum + itemsMinimum > room;)
            if (auto* button = dynamic_cast<IconButton*> (items[i].component); placed[i] && button != nullptr && button->getLabel().isNotEmpty())
            {
                widths[i] = items[i].height;
                measure();
            }

        for (size_t i = items.size(); i-- > 0 && titleMinimum + itemsMinimum > room;)
            if (placed[i] && items[i].optional)
            {
                placed[i] = false;
                items[i].component->setBounds ({});
                measure();
            }

        float itemsScale = 1.0f;

        if (titleW + itemsWidth > room)
        {
            titleW = std::max (titleMinimum, room - itemsWidth);

            if (titleW + itemsWidth > room && itemsWidth > itemsMinimum)
                itemsScale = juce::jlimit (0.0f, 1.0f, (float) (room - titleW - itemsMinimum) / (float) (itemsWidth - itemsMinimum));
        }

        if (titleComponent != nullptr)
            titleComponent->setBounds (header.removeFromLeft (titleW));
        else
            header.removeFromLeft (titleW);

        header.removeFromLeft (14);

        for (size_t i = 0; i < items.size(); ++i)
        {
            auto& item = items[i];

            if (! placed[i])
                continue;

            const int minimum = minimumOf (item.component, widths[i]);
            const int width = minimum + juce::roundToInt ((float) (widths[i] - minimum) * itemsScale);
            auto slot = item.onRight ? header.removeFromRight (width) : header.removeFromLeft (width);
            item.component->setBounds (slot.withSizeKeepingCentre (slot.getWidth(), item.height));

            if (item.onRight)
                header.removeFromRight (8);
            else
                header.removeFromLeft (8);
        }
    }

    if (subHeader != nullptr)
        subHeader->setBounds (r.removeFromTop (subHeaderHeight));

    if (content != nullptr)
        content->setBounds (r);
}

void DockPanel::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), panelRadius);

    if (! headerVisible)
        return;

    g.setColour (hairline);
    g.fillRect (0, headerHeight, getWidth(), 1);

    if (titleComponent == nullptr && title.isNotEmpty())
    {
        auto header = juce::Rectangle<int> (14, 0, getWidth() - 28, headerHeight);
        g.setColour (text);
        g.setFont (font (17.5f, true));
        const int w = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title) + 1;
        g.drawText (title, header.removeFromLeft (w), juce::Justification::centredLeft, true);

        if (subtitle.isNotEmpty())
        {
            g.setColour (textFaint);
            g.setFont (font (17.0f));
            g.drawText (subtitle, header.withTrimmedLeft (12), juce::Justification::centredLeft, true);
        }
    }
}

void DockPanel::paintOverChildren (juce::Graphics& g)
{
    // Round the content's corners off by painting the window background over them
    const auto bounds = getLocalBounds().toFloat();
    juce::Path corners;
    corners.addRectangle (bounds);
    corners.addRoundedRectangle (bounds, panelRadius);
    corners.setUsingNonZeroWinding (false);
    g.setColour (background);
    g.fillPath (corners);

    g.setColour (hairline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), panelRadius, 1.0f);
}

} // namespace nightshift
