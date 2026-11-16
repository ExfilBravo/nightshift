// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

namespace nightshift
{

/** One panel of the single studio window: a rounded surface with a header strip (a title,
    or something in its place such as tabs, then controls on either side) over its content.
    Its rounded corners are painted over the content, so children needn't know about them.
*/
class DockPanel : public juce::Component
{
public:
    explicit DockPanel (const juce::String& title = {});

    void setTitle (const juce::String& title, const juce::String& subtitle = {});
    void setHeaderVisible (bool);
    void setContent (juce::Component*);

    /** Shows `c` where the title goes (tabs, say), `width` wide. */
    void setTitleComponent (juce::Component* c, int width);

    /** Puts a control in the header, after the title or in from the right edge; hidden
        controls give up their place. When the header runs out of room, tabs and dropdowns
        narrow first, then optional controls (ones also reached another way) step aside. */
    void addToHeader (juce::Component&, int width, bool onRight, int height = 30, bool optional = false);

    /** Resizes a header control, for narrow windows. */
    void setHeaderItemWidth (juce::Component&, int width);

    /** A strip across the panel between the header and the content. */
    void setSubHeader (juce::Component*, int height);

    static constexpr int headerHeight = 44;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    struct Item
    {
        juce::Component* component;
        int width, height;
        bool onRight;
        bool optional = false;
    };

    juce::String title, subtitle;
    juce::Component* content = nullptr;
    juce::Component* titleComponent = nullptr;
    juce::Component* subHeader = nullptr;
    int titleComponentWidth = 0, subHeaderHeight = 0;
    std::vector<Item> items;
    bool headerVisible = true;

    int titleWidth() const;
};

} // namespace nightshift
