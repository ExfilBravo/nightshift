// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/fx/StockEffect.h"
#include "Editing.h"
#include "Theme.h"
#include "Widgets.h"

/** Controls bound to a stock effect's (or Nova's) parameters: knobs, fields, switches and
    choices, each carrying its parameter's id as its component ID and brought up to date by
    refresh() (automation, undo, a preset). */
namespace nightshift::controls
{

using namespace theme;

inline juce::NormalisableRange<double> toDouble (const juce::NormalisableRange<float>& r)
{
    return { r.start, r.end, r.interval, r.skew, r.symmetricSkew };
}

//==============================================================================
/** Something bound to a parameter that the panel brings up to date at the UI rate
    (automation, undo, a preset). */
struct ParamControl : public juce::Component,
                      public juce::SettableTooltipClient
{
    explicit ParamControl (StockEffect::Param& p) : param (p)   { setComponentID (p.id); }

    virtual void refresh()                          { repaint(); }
    // Greyed out but still readable: at 0.65 its words stay over 3 to 1 (at 0.4 they were under 2)
    void setDimmed (bool d)                         { dimmed = d; setAlpha (d ? 0.65f : 1.0f); }

    StockEffect::Param& param;
    bool dimmed = false;
};

/** A knob's slider that hands a right-click on (to offer Automate) rather than starting a drag. */
struct KnobSlider : public juce::Slider
{
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        rightClicked = e.mods.isPopupMenu() && onRightClick != nullptr;

        if (rightClicked)
            onRightClick();
        else
            juce::Slider::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override     { if (! rightClicked) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override       { if (! rightClicked) juce::Slider::mouseUp (e); rightClicked = false; }

private:
    bool rightClicked = false;
};

/** A knob with its value under it, then its name. Double-click resets it; right-click offers
    to automate it in Arrange. */
struct ParamKnob : public ParamControl
{
    ParamKnob (StockEffect::Param& p, juce::Colour colour, const juce::String& label)
        : ParamControl (p), name (label.isNotEmpty() ? label : p.name)
    {
        slider.onRightClick = [this]
        {
            if (automation::showMenuFor != nullptr)
                automation::showMenuFor (param.value->getParameter(), name);
        };

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setNormalisableRange (toDouble (p.range));
        slider.setDoubleClickReturnValue (true, p.defaultValue);
        slider.setMouseDragSensitivity (200);
        slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        slider.getProperties().set ("bipolar", p.range.start < 0.0f && p.range.end > 0.0f);
        slider.setValue (p.get(), juce::dontSendNotification);
        slider.setTooltip (p.name + " (drag; double-click to reset; right-click to automate it or link a MIDI knob)");
        slider.onValueChange = [this]
        {
            if (! updating)
            {
                param.set ((float) slider.getValue());
                repaint();
            }
        };
        addAndMakeVisible (slider);
    }

    void refresh() override
    {
        if (! slider.isMouseButtonDown())
        {
            const juce::ScopedValueSetter<bool> quiet (updating, true);
            slider.setValue (param.get(), juce::dontSendNotification);
        }

        repaint();
    }

    void resized() override
    {
        const int size = std::min (getWidth(), getHeight() - 36);
        slider.setBounds (getLocalBounds().removeFromTop (size).withSizeKeepingCentre (size, size));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().withTrimmedTop (slider.getBottom() + 1);
        g.setColour (slider.isMouseButtonDown() ? text : textDim);
        g.setFont (monoFont (15.0f, true));
        g.drawText (param.text(), r.removeFromTop (17), juce::Justification::centred, true);
        g.setColour (textFaint);
        g.setFont (labelFont());
        g.drawText (name.toUpperCase(), r.removeFromTop (16), juce::Justification::centred, true);
    }

    KnobSlider slider;
    juce::String name;
    bool updating = false;
};

/** A small field: a caption, and a value dragged up and down (Shift for fine), stepped
    with the wheel; double-click resets it. */
struct ParamValue : public ParamControl
{
    ParamValue (StockEffect::Param& p, const juce::String& captionText)
        : ParamControl (p), caption (captionText)
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setTooltip (p.name + " (drag up and down; double-click to reset)");
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (isMouseOverOrDragging() ? titleBar : panelLight);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (hairline);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        auto area = getLocalBounds().reduced (8, 0);
        g.setColour (textFaint);
        g.setFont (labelFont());
        g.drawText (caption, area, juce::Justification::centredLeft, true);
        g.setColour (text);
        g.setFont (monoFont (15.0f, true));
        g.drawText (param.text(), area, juce::Justification::centredRight, true);
    }

    void mouseDown (const juce::MouseEvent&) override   { start = param.range.convertTo0to1 (param.get()); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float perPixel = e.mods.isShiftDown() ? 0.001f : 0.005f;
        setNormalised (start - e.getDistanceFromDragStartY() * perPixel);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override   { param.set (param.defaultValue); repaint(); }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        // A stepped parameter moves one step a notch
        const float steps = param.range.interval > 0.0f ? param.range.interval / (param.range.end - param.range.start) : 0.02f;
        setNormalised (param.range.convertTo0to1 (param.get()) + (w.deltaY > 0 ? steps : -steps));
    }

    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }

    void setNormalised (float n)
    {
        param.set (param.range.snapToLegalValue (param.range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n))));
        repaint();
    }

    juce::String caption;
    float start = 0.0f;
};

/** A switch for an on/off parameter: a pill with a light and its name. */
struct ParamToggle : public ParamControl
{
    ParamToggle (StockEffect::Param& p, juce::Colour c, const juce::String& label)
        : ParamControl (p), colour (c), name (label)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (p.name);
    }

    bool isOn() const                               { return param.get() >= 0.5f; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (isOn() ? colour.withAlpha (0.16f) : (isMouseOver() ? titleBar : panelLight));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (isOn() ? colour.withAlpha (0.55f) : hairlineStrong);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

        auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + 15.0f, r.getCentreY() });
        g.setColour (isOn() ? colour : textFaint);

        if (isOn())
            g.fillEllipse (dot);
        else
            g.drawEllipse (dot, 1.3f);

        g.setColour (isOn() ? text : textDim);
        g.setFont (font (15.0f, true));
        g.drawText (name, getLocalBounds().withTrimmedLeft (26).withTrimmedRight (8), juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent&) override   { param.set (isOn() ? 0.0f : 1.0f); repaint(); }
    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }

    juce::Colour colour;
    juce::String name;
};

/** A stepped parameter as a row of choices. */
struct ParamChoice : public ParamControl
{
    ParamChoice (StockEffect::Param& p, const juce::StringArray& options)
        : ParamControl (p), choices (options, SegmentedControl::Style::pill)
    {
        choices.onChange = [this] (int index) { param.set ((float) index); };
        addAndMakeVisible (choices);
        refresh();
    }

    void refresh() override     { choices.setSelected (juce::roundToInt (param.get())); }
    void resized() override     { choices.setBounds (getLocalBounds()); }
    int getIdealWidth() const   { return choices.getIdealWidth(); }

    SegmentedControl choices;
};

/** A choice from a list too long to show at once: a caption and the choice, which opens a
    menu of them; the wheel steps through. */
struct ParamMenu : public ParamControl
{
    ParamMenu (StockEffect::Param& p, const juce::String& captionText, const juce::StringArray& names)
        : ParamControl (p), caption (captionText), choices (names)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (p.name);
    }

    int index() const                               { return juce::roundToInt (param.get()); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (isMouseOver() ? titleBar : panelLight);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (hairline);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        auto area = getLocalBounds().reduced (8, 0);
        auto arrow = area.removeFromRight (12).toFloat();
        auto chevron = icons::chevronDown();
        g.setColour (textFaint);
        g.fillPath (chevron, chevron.getTransformToScaleToFit (arrow.withSizeKeepingCentre (9.0f, 9.0f), true));
        area.removeFromRight (4);

        if (caption.isNotEmpty())
        {
            g.setFont (labelFont());
            g.drawText (caption, area, juce::Justification::centredLeft, true);
        }

        g.setColour (index() == 0 && choices[0] == "Off" ? textDim : text);
        g.setFont (font (15.0f, true));
        g.drawText (choices[index()], area, caption.isNotEmpty() ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu menu;

        for (int i = 0; i < choices.size(); ++i)
            menu.addItem (i + 1, choices[i], true, i == index());

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<ParamMenu> (this)] (int result)
                            {
                                if (safe != nullptr && result > 0)
                                {
                                    safe->param.set ((float) (result - 1));
                                    safe->repaint();
                                }
                            });
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        param.set ((float) juce::jlimit (0, choices.size() - 1, index() + (w.deltaY > 0 ? -1 : 1)));
        repaint();
    }

    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }

    juce::String caption;
    juce::StringArray choices;
};

} // namespace nightshift::controls
