// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "Theme.h"

namespace nightshift
{

/** What a sound dragged out of the sidebar carries: this, then kit:, synth: or plugin:
    and the sound's name (or the plugin's identifier string). */
inline constexpr const char* soundDragPrefix = "nightshift-sound:";

/** What a channel dragged out of Steps carries: this, then its id. Arrange gives it a track. */
inline constexpr const char* channelDragPrefix = "nightshift-channel:";

/** What a pattern dragged out of the sidebar carries: this, then its id. Arrange puts a clip
    of it where it's dropped. */
inline constexpr const char* patternDragPrefix = "nightshift-pattern:";

/** A small rotary knob that never takes keyboard focus (so Space keeps meaning play). */
class Knob : public juce::Slider
{
public:
    Knob (double min, double max, double defaultValue, bool bipolar = false);

    std::function<juce::String (double)> formatValue;
    juce::String getTextFromValue (double v) override;
};

//==============================================================================
/** A flat button that draws a vector icon, optionally with a label beside it. It shows
    nothing behind the icon until hovered; when on, it takes a tint of its colour, or the
    full colour with a dark icon if it's solid (the play button). Framed buttons sit on a
    raised surface with an edge, for actions like Export. */
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, juce::Path icon, juce::Colour onColour = theme::accent);

    void setLabel (const juce::String& newLabel)    { label = newLabel; repaint(); }
    const juce::String& getLabel() const            { return label; }
    void setIconColour (juce::Colour c)             { iconColour = c; repaint(); }
    void setFilled (bool shouldFill)                { filled = shouldFill; repaint(); }
    void setSolidWhenOn (bool shouldBeSolid)        { solidWhenOn = shouldBeSolid; repaint(); }
    void setFramed (bool shouldBeFramed)            { framed = shouldBeFramed; repaint(); }
    void setIconSize (float size)                   { iconSize = size; repaint(); }

    /** Wide enough for its icon and label, with its padding (for a label: the icon's size taken at the
        given height). */
    int getIdealWidth (int height) const;

    /** A menu on right-click (it shows a small corner, as a split button does). */
    std::function<void()> onRightClick;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Path icon;
    juce::Colour onColour, iconColour { theme::textDim };
    juce::String label;
    float iconSize = 0.0f;
    bool filled = true, solidWhenOn = false, framed = false;
};

//==============================================================================
/** A row of choices: a pill-shaped segmented control (Pattern | Song), or tabs with an
    amber underline (Steps | Piano roll). Options can be switched off with a badge, such
    as "soon". */
class SegmentedControl : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    enum class Style { pill, tabs };

    SegmentedControl (const juce::StringArray& options, Style);

    void setSelected (int index, juce::NotificationType = juce::dontSendNotification);
    int getSelected() const                         { return selected; }
    void setUnavailable (int index, const juce::String& badge);
    int getIdealWidth() const;

    /** The narrowest it lays out without cutting a label: less room around them, and the short
        labels when it has them. */
    int getMinimumWidth() const;

    /** Shorter labels for when it's squeezed ("Tone" for "Tone, punch & width"). */
    void setShortLabels (const juce::StringArray& labels)  { shortLabels = labels; resized(); repaint(); }

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::StringArray options, badges, shortLabels, shown;
    std::vector<juce::Rectangle<int>> areas;
    Style style;
    int selected = 0, hovered = -1;

    int indexAt (juce::Point<int>) const;
    juce::Font optionFont() const;
};

//==============================================================================
/** A slim horizontal bar for a value, filled from the left, or from the centre when
    bipolar (pan). Drag sideways to change it (Shift for fine), scroll, double-click for
    the default. The tooltip shows the value. */
class MiniBar : public juce::Component,
                public juce::SettableTooltipClient
{
public:
    MiniBar (double min, double max, double defaultValue, bool bipolar = false);

    void setValue (double, juce::NotificationType = juce::dontSendNotification);
    double getValue() const                         { return value; }
    void setFillColour (juce::Colour c)             { fill = c; repaint(); }

    std::function<void()> onDragStart, onValueChange;
    std::function<juce::String (double)> formatValue;

    /** Right-click, instead of a drag (Automate, say). */
    std::function<void()> onRightClick;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override       { repaint(); }
    void mouseExit (const juce::MouseEvent&) override        { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    double min, max, defaultValue, value, dragStartValue = 0.0;
    bool bipolar, rightClicked = false;
    juce::Colour fill { theme::textDim };

    void change (double);
};

//==============================================================================
/** A MiniBar with a label before it and its value after:  Swing  ━━━━○──  32% */
class LabelledBar : public juce::Component
{
public:
    LabelledBar (const juce::String& label, double min, double max, double defaultValue);

    MiniBar bar;
    std::function<juce::String (double)> formatValue;
    std::function<void()> onValueChange;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String label;
    int labelWidth() const;
};

//==============================================================================
/** Icon buttons on a raised strip, one of them chosen (the Arrange tools). */
class ToolStrip : public juce::Component
{
public:
    void addTool (const juce::String& name, juce::Path lineIcon, const juce::String& tooltip);
    void setSelected (int index, juce::NotificationType = juce::dontSendNotification);
    int getSelected() const                         { return selected; }
    int getIdealWidth() const                       { return 4 + buttons.size() * 30; }

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::OwnedArray<IconButton> buttons;
    int selected = 0;
};

//==============================================================================
/** A small dropdown: an optional colour swatch or icon, a label and a chevron (the snap
    setting, say). Its owner shows the menu from onClick. */
class Chip : public juce::Button
{
public:
    explicit Chip (const juce::String& label = {});

    void setLabel (const juce::String&);
    void setSwatch (juce::Colour c)                 { swatch = c; repaint(); }
    void setIcon (juce::Path lineIcon)              { icon = std::move (lineIcon); repaint(); }

    /** Whether it shows the arrow that says a menu opens (the default) or just acts. */
    void setOpensMenu (bool shouldShowArrow)        { opensMenu = shouldShowArrow; repaint(); }

    /** Two lines when the label needs them (the AI tabs' ideas), instead of cutting it short. */
    void setWraps (bool shouldWrap)                 { wraps = shouldWrap; repaint(); }
    int getIdealWidth() const;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String label;
    juce::Colour swatch { juce::Colours::transparentBlack };
    juce::Path icon;
    bool opensMenu = true, wraps = false;
};

//==============================================================================
/** A numeric readout you drag up and down (Shift for fine) or double-click to type into.
    It draws only the number and a small unit after it; whatever holds it draws the frame. */
class ValueBox : public juce::Component
{
public:
    ValueBox (double min, double max, double step, int decimals);

    void setValue (double, juce::NotificationType = juce::dontSendNotification);
    double getValue() const                         { return value; }

    /** Being dragged or typed into: what's shown is the user's. */
    bool isEditing() const                          { return editor != nullptr || isMouseButtonDown(); }
    void setCaption (const juce::String& c)         { caption = c; repaint(); }
    void setTextColour (juce::Colour c)             { textColour = c; repaint(); }

    std::function<void (double)> onChange;
    std::function<void()> onDragStart;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void resized() override;

private:
    double min, max, step, value = 0.0, dragStartValue = 0.0;
    int decimals;
    juce::String caption;
    juce::Colour textColour { theme::text };
    std::unique_ptr<juce::TextEditor> editor;

    void commit (double);
};

//==============================================================================
/** Stereo peak meter: two slim bars with a falling peak; the far end lights red once
    either side clips, until clicked. Horizontal, or vertical beside a fader. */
class LevelMeter : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    void push (float left, float right);
    void setVertical (bool v)                       { vertical = v; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Where a level sits along the meter, 0..1 (-60 dB to 0 dB). */
    static float proportionFor (float gain);

private:
    float levels[2] {}, holds[2] {};
    int holdFrames[2] {};
    bool clipped = false, vertical = false;
};

//==============================================================================
/** A channel strip's volume fader: a vertical track with a cap you drag (Shift for fine),
    scroll, or double-click back to its default. Its position is the 0..1 volume value. */
class Fader : public juce::Component,
              public juce::SettableTooltipClient
{
public:
    explicit Fader (double defaultValue);

    void setValue (double, juce::NotificationType = juce::dontSendNotification);
    double getValue() const                         { return value; }

    std::function<void()> onDragStart, onValueChange;
    std::function<juce::String (double)> formatValue;

    /** Right-click, instead of a drag (Automate, say). */
    std::function<void()> onRightClick;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override       { repaint(); }
    void mouseExit (const juce::MouseEvent&) override        { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int capHeight = 22;

private:
    double defaultValue, value, dragStartValue = 0.0;
    bool rightClicked = false;

    void change (double);
    float travel() const                            { return (float) std::max (1, getHeight() - capHeight); }
};

} // namespace nightshift
