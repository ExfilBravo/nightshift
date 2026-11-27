// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Widgets.h"

namespace nightshift
{

using namespace theme;

Knob::Knob (double min, double max, double defaultValue, bool bipolar)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)
{
    setRange (min, max);
    setValue (defaultValue, juce::dontSendNotification);
    setDoubleClickReturnValue (true, defaultValue);
    setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    setMouseDragSensitivity (160);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setPopupDisplayEnabled (true, true, nullptr, 1200);
    getProperties().set ("bipolar", bipolar);
}

juce::String Knob::getTextFromValue (double v)
{
    return formatValue != nullptr ? formatValue (v) : juce::Slider::getTextFromValue (v);
}

//==============================================================================
IconButton::IconButton (const juce::String& name, juce::Path p, juce::Colour on)
    : juce::Button (name), icon (std::move (p)), onColour (on)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

int IconButton::getIdealWidth (int height) const
{
    const float size = iconSize > 0.0f ? iconSize : std::round (height * (filled ? 0.42f : 0.5f));

    if (label.isEmpty())
        return height;

    // As paintButton lays it out: 12 in, the icon, 8, the label, and 14 after it
    return 12 + (int) size + 8 + (int) std::ceil (juce::GlyphArrangement::getStringWidth (font (17.5f, true), label)) + 14;
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState() && isEnabled();

    auto fillColour = framed ? titleBar : juce::Colours::transparentBlack;

    if (on)
        fillColour = solidWhenOn ? onColour : onColour.withAlpha (0.14f);
    else if (down)
        fillColour = raised.brighter (0.1f);
    else if (highlighted && isEnabled())
        fillColour = framed ? raised : raised.withAlpha (0.75f);

    g.setColour (fillColour);
    g.fillRoundedRectangle (r, controlRadius);

    if (framed)
    {
        g.setColour (hairlineStrong);
        g.drawRoundedRectangle (r, controlRadius, 1.0f);
    }

    // The label where most of it fits; squeezed narrower than that (a header on a small screen), the icon
    // stands alone and its tooltip says the rest, rather than a word cut to a stub
    const float size = iconSize > 0.0f ? iconSize : std::round (std::min (getWidth(), getHeight()) * (filled ? 0.42f : 0.5f));
    const bool labelShown = label.isNotEmpty()
                            && (float) getWidth() - 32.0f - size >= 0.75f * juce::GlyphArrangement::getStringWidth (font (17.5f, true), label);
    auto content = getLocalBounds().reduced (labelShown ? 12 : 0, 0);
    auto iconArea = ! labelShown ? content.toFloat() : content.removeFromLeft ((int) size).toFloat();
    auto iconBounds = juce::Rectangle<float> (size, size).withCentre (iconArea.getCentre());

    auto drawn = icon;
    drawn.applyTransform (juce::AffineTransform::scale (size / 10.0f).translated (iconBounds.getPosition()));

    const auto c = ! isEnabled() ? textFaint.withAlpha (0.6f)
                 : on ? (solidWhenOn ? background : onColour)
                      : (highlighted ? theme::text : iconColour);
    g.setColour (c);

    if (filled)
        g.fillPath (drawn);
    else
        g.strokePath (drawn, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (labelShown)
    {
        // On a solid (amber) button the words are dark like its icon: white on amber is only 1.5 to 1
        g.setColour (! isEnabled() ? textFaint : on && solidWhenOn ? background : theme::text);
        g.setFont (font (17.5f, true));
        g.drawText (label, content.withTrimmedLeft (8), juce::Justification::centredLeft, true);
    }

    // It has a menu (right-click): a small corner, as a split button has
    if (onRightClick != nullptr)
    {
        const float x = r.getRight() - 4.0f, y = r.getBottom() - 4.0f;
        juce::Path corner;
        corner.addTriangle (x, y - 5.0f, x, y, x - 5.0f, y);
        g.setColour ((on && solidWhenOn ? background : (highlighted ? theme::text : iconColour)).withAlpha (0.7f));
        g.fillPath (corner);
    }
}

void IconButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick != nullptr)
    {
        onRightClick();
        return;
    }

    juce::Button::mouseDown (e);
}

void IconButton::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick != nullptr)
        return;

    juce::Button::mouseUp (e);
}

//==============================================================================
SegmentedControl::SegmentedControl (const juce::StringArray& o, Style s)
    : options (o), style (s)
{
    for (int i = 0; i < options.size(); ++i)
        badges.add ({});

    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

juce::Font SegmentedControl::optionFont() const
{
    return font (style == Style::pill ? 17.5f : 17.5f, true);
}

void SegmentedControl::setSelected (int index, juce::NotificationType nt)
{
    if (! juce::isPositiveAndBelow (index, options.size()) || badges[index].isNotEmpty())
        return;

    const bool changed = index != selected;
    selected = index;
    repaint();

    if (changed && nt != juce::dontSendNotification && onChange != nullptr)
        onChange (selected);
}

void SegmentedControl::setUnavailable (int index, const juce::String& badge)
{
    badges.set (index, badge);
    resized();
    repaint();
}

int SegmentedControl::getIdealWidth() const
{
    const auto f = optionFont();
    int width = style == Style::pill ? 6 : 0;

    for (int i = 0; i < options.size(); ++i)
    {
        width += (int) juce::GlyphArrangement::getStringWidth (f, options[i]) + (style == Style::pill ? 26 : 22);

        if (badges[i].isNotEmpty())
            width += 46;
    }

    // Tabs sit 2 px apart (see resized)
    if (style != Style::pill && options.size() > 1)
        width += 2 * (options.size() - 1);

    return width;
}

int SegmentedControl::getMinimumWidth() const
{
    const int pad = style == Style::pill ? 26 : 22;
    int width = getIdealWidth() - options.size() * (pad - 10);

    if (shortLabels.size() == options.size())
    {
        const auto f = optionFont();

        for (int i = 0; i < options.size(); ++i)
            width -= (int) juce::GlyphArrangement::getStringWidth (f, options[i]) - (int) juce::GlyphArrangement::getStringWidth (f, shortLabels[i]);
    }

    return width;
}

void SegmentedControl::resized()
{
    const auto f = optionFont();
    areas.clear();
    int x = style == Style::pill ? 3 : 0;

    // Less width than it wants: less room around each label (never under 10 px), the labels whole;
    // squeezed further, the short labels
    const int pad = style == Style::pill ? 26 : 22;
    const int fullMinimum = getIdealWidth() - options.size() * (pad - 10);
    shown = shortLabels.size() == options.size() && getWidth() < fullMinimum ? shortLabels : options;
    int wanted = (style == Style::pill ? 6 : 2 * (options.size() - 1)) + options.size() * pad;

    for (int i = 0; i < options.size(); ++i)
        wanted += (int) juce::GlyphArrangement::getStringWidth (f, shown[i]) + (badges[i].isNotEmpty() ? 46 : 0);

    const int shortBy = std::max (0, wanted - getWidth());
    const int padding = std::max (10, pad - (shortBy + options.size() - 1) / std::max (1, options.size()));

    for (int i = 0; i < options.size(); ++i)
    {
        int w = (int) juce::GlyphArrangement::getStringWidth (f, shown[i]) + padding;

        if (badges[i].isNotEmpty())
            w += 46;

        areas.push_back (style == Style::pill ? juce::Rectangle<int> (x, 3, w, getHeight() - 6)
                                              : juce::Rectangle<int> (x, 0, w, getHeight()));
        x += w + (style == Style::pill ? 0 : 2);
    }
}

void SegmentedControl::paint (juce::Graphics& g)
{
    if (style == Style::pill)
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (panelLight);
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (hairline);
        g.drawRoundedRectangle (r, 9.0f, 1.0f);
    }

    g.setFont (optionFont());

    for (int i = 0; i < options.size(); ++i)
    {
        auto a = areas[(size_t) i];
        const bool on = i == selected, unavailable = badges[i].isNotEmpty();

        if (style == Style::pill)
        {
            if (on)
            {
                g.setColour (raised);
                g.fillRoundedRectangle (a.toFloat(), 6.0f);
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.drawHorizontalLine (a.getY(), (float) a.getX() + 4.0f, (float) a.getRight() - 4.0f);
            }

            g.setColour (on ? text : (i == hovered ? text.withAlpha (0.85f) : textDim));
            g.drawText (i < shown.size() ? shown[i] : options[i], a, juce::Justification::centred, true);
            continue;
        }

        // Tabs: an amber underline under the chosen one
        auto label = a;

        if (unavailable)
        {
            auto badge = label.removeFromRight (56).withSizeKeepingCentre (48, 22).toFloat();
            g.setColour (titleBar);
            g.fillRoundedRectangle (badge, 4.0f);
            g.setColour (textFaint);
            g.setFont (font (15.0f, true));
            g.drawText (badges[i], badge, juce::Justification::centred, true);
            g.setFont (optionFont());
        }

        g.setColour (on ? text : (i == hovered && ! unavailable ? textDim : textFaint));
        g.drawText (i < shown.size() ? shown[i] : options[i], label, juce::Justification::centred, true);

        if (on)
        {
            g.setColour (accent);
            g.fillRect (a.getX() + 4, getHeight() - 2, a.getWidth() - 8, 2);
        }
    }
}

int SegmentedControl::indexAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) areas.size(); ++i)
        if (areas[(size_t) i].contains (p))
            return i;

    return -1;
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());

    if (i >= 0 && badges[i].isEmpty())
        setSelected (i, juce::sendNotification);
}

void SegmentedControl::mouseMove (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());

    if (i != hovered)
    {
        hovered = i;
        setMouseCursor (i >= 0 && badges[i].isEmpty() && i != selected ? juce::MouseCursor::PointingHandCursor
                                                                       : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void SegmentedControl::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

//==============================================================================
MiniBar::MiniBar (double mn, double mx, double def, bool bi)
    : min (mn), max (mx), defaultValue (def), value (def), bipolar (bi)
{
    setRepaintsOnMouseActivity (true);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

void MiniBar::setValue (double v, juce::NotificationType nt)
{
    v = juce::jlimit (min, max, v);

    if (v == value)
        return;

    value = v;

    if (formatValue != nullptr)
        setTooltip (formatValue (value));

    repaint();

    if (nt != juce::dontSendNotification && onValueChange != nullptr)
        onValueChange();
}

void MiniBar::change (double v)
{
    setValue (v, juce::sendNotification);
}

void MiniBar::paint (juce::Graphics& g)
{
    auto track = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), 4.0f);
    g.setColour (titleBar);
    g.fillRoundedRectangle (track, 2.0f);

    const float proportion = (float) ((value - min) / (max - min));
    const float x = track.getX() + track.getWidth() * proportion;
    const auto colour = isMouseOverOrDragging() ? fill.brighter (0.3f) : fill;
    g.setColour (colour);

    if (bipolar)
    {
        const float centre = track.getCentreX();
        g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (std::min (x, centre), track.getY(), std::max (x, centre) + 1.0f,
                                                                           track.getBottom()), 2.0f);
        g.setColour (textFaint);
        g.fillRect (centre - 0.5f, track.getY() - 2.0f, 1.0f, track.getHeight() + 4.0f);
    }
    else
    {
        g.fillRoundedRectangle (track.withRight (std::max (track.getX() + 2.0f, x)), 2.0f);
    }

    if (isMouseOverOrDragging())
    {
        g.setColour (theme::text);
        g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, 10.0f).withCentre ({ x, track.getCentreY() }), 1.5f);
    }
}

void MiniBar::mouseDown (const juce::MouseEvent& e)
{
    rightClicked = e.mods.isPopupMenu() && onRightClick != nullptr;

    if (rightClicked)
    {
        onRightClick();
        return;
    }

    dragStartValue = value;
    held = true;

    if (onDragStart != nullptr)
        onDragStart();
}

void MiniBar::mouseDrag (const juce::MouseEvent& e)
{
    if (rightClicked)
        return;

    // The whole range across the bar's width; Shift drags a fifth as far
    const double perPixel = (max - min) / std::max (1, getWidth()) * (e.mods.isShiftDown() ? 0.2 : 1.0);
    change (dragStartValue + e.getDistanceFromDragStartX() * perPixel);
}

void MiniBar::mouseUp (const juce::MouseEvent&)
{
    if (std::exchange (held, false) && onDragEnd != nullptr)
        onDragEnd();
}

void MiniBar::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDragStart != nullptr)
        onDragStart();

    change (defaultValue);
}

void MiniBar::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (onDragStart != nullptr)
        onDragStart();

    change (value + (w.deltaY > 0 ? 1.0 : -1.0) * (max - min) * 0.02);
}

//==============================================================================
LabelledBar::LabelledBar (const juce::String& l, double mn, double mx, double def)
    : bar (mn, mx, def), label (l)
{
    bar.onValueChange = [this]
    {
        repaint();

        if (onValueChange != nullptr)
            onValueChange();
    };

    addAndMakeVisible (bar);
}

int LabelledBar::labelWidth() const
{
    return (int) juce::GlyphArrangement::getStringWidth (font (16.5f), label) + 2;
}

void LabelledBar::resized()
{
    bar.setBounds (getLocalBounds().withTrimmedLeft (labelWidth() + 10).withTrimmedRight (52).withSizeKeepingCentre (
        std::max (10, getWidth() - labelWidth() - 62), 12));
}

void LabelledBar::paint (juce::Graphics& g)
{
    g.setColour (textFaint);
    g.setFont (font (16.5f));
    g.drawText (label, getLocalBounds(), juce::Justification::centredLeft, true);

    g.setColour (textDim);
    g.setFont (monoFont (16.5f, true));
    g.drawText (formatValue != nullptr ? formatValue (bar.getValue()) : juce::String (bar.getValue(), 2),
                getLocalBounds().removeFromRight (46), juce::Justification::centredRight, true);
}

//==============================================================================
void ToolStrip::addTool (const juce::String& name, juce::Path lineIcon, const juce::String& tooltip)
{
    auto* b = buttons.add (new IconButton (name, std::move (lineIcon), theme::text));
    const int index = buttons.size() - 1;
    b->setFilled (false);
    b->setIconSize (15.0f);
    b->setTooltip (tooltip);
    b->setToggleState (index == selected, juce::dontSendNotification);
    b->onClick = [this, index] { setSelected (index, juce::sendNotification); };
    addAndMakeVisible (b);
    resized();
}

void ToolStrip::setSelected (int index, juce::NotificationType nt)
{
    if (! juce::isPositiveAndBelow (index, buttons.size()))
        return;

    const bool changed = index != selected;
    selected = index;

    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (i == selected, juce::dontSendNotification);

    if (changed && nt != juce::dontSendNotification && onChange != nullptr)
        onChange (selected);
}

void ToolStrip::resized()
{
    auto r = getLocalBounds().reduced (2);

    for (auto* b : buttons)
        b->setBounds (r.removeFromLeft (30).reduced (1, 0));
}

void ToolStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (panelLight);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (hairline);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
}

//==============================================================================
Chip::Chip (const juce::String& l)
    : juce::Button (l), label (l)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void Chip::setLabel (const juce::String& l)
{
    label = l;
    repaint();
}

int Chip::getIdealWidth() const
{
    const bool lead = swatch.getAlpha() > 0 || ! icon.isEmpty();
    return (int) juce::GlyphArrangement::getStringWidth (font (17.0f, true), label) + (lead ? 26 : 10) + (opensMenu ? 30 : 14);
}

void Chip::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? raised : (highlighted ? titleBar : panelLight));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (hairlineStrong);
    g.drawRoundedRectangle (r, 7.0f, 1.0f);

    auto content = getLocalBounds().reduced (9, 0);

    if (swatch.getAlpha() > 0)
    {
        auto s = content.removeFromLeft (10).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
        g.setColour (swatch);
        g.fillRoundedRectangle (s, 3.0f);
        content.removeFromLeft (8);
    }
    else if (! icon.isEmpty())
    {
        auto i = icon;
        i.applyTransform (juce::AffineTransform::scale (1.5f).translated ((float) content.getX() - 1.0f, (float) content.getCentreY() - 7.5f));
        g.setColour (textFaint);
        g.strokePath (i, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        content.removeFromLeft (20);
    }

    if (opensMenu)
    {
        auto chevron = icons::chevronDown();
        chevron.applyTransform (juce::AffineTransform::scale (1.4f).translated ((float) content.getRight() - 15.0f, (float) content.getCentreY() - 7.0f));
        g.setColour (textFaint);
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (isEnabled() ? theme::text : textFaint);
    g.setFont (font (17.0f, true));

    if (wraps)
        g.drawFittedText (label, content.withTrimmedRight (opensMenu ? 18 : 0).reduced (0, 4), juce::Justification::centredLeft, 2, 1.0f);
    else
        g.drawText (label, content.withTrimmedRight (opensMenu ? 18 : 0), juce::Justification::centredLeft, true);
}

//==============================================================================
ValueBox::ValueBox (double mn, double mx, double st, int dec)
    : min (mn), max (mx), step (st), decimals (dec)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void ValueBox::setValue (double v, juce::NotificationType nt)
{
    v = juce::jlimit (min, max, std::round (v / step) * step);

    if (v == value)
        return;

    value = v;
    repaint();

    if (nt != juce::dontSendNotification && onChange != nullptr)
        onChange (value);
}

void ValueBox::paint (juce::Graphics& g)
{
    const auto number = juce::String (value, decimals);
    const auto numberFont = monoFont ((float) getHeight() * 0.92f, true);
    g.setColour (textColour);
    g.setFont (numberFont);
    g.drawText (number, getLocalBounds(), juce::Justification::centredLeft, true);

    if (caption.isNotEmpty())
    {
        const int x = (int) juce::GlyphArrangement::getStringWidth (numberFont, number) + 3;
        g.setColour (textFaint);
        g.setFont (font (std::max (15.0f, (float) getHeight() * 0.56f)));
        g.drawText (caption, getLocalBounds().withTrimmedLeft (x).withTrimmedBottom (1), juce::Justification::bottomLeft, true);
    }
}

void ValueBox::mouseDown (const juce::MouseEvent&)
{
    dragStartValue = value;

    if (onDragStart != nullptr)
        onDragStart();
}

void ValueBox::mouseDrag (const juce::MouseEvent& e)
{
    // A quarter unit per pixel; Shift drags in single steps
    const double perPixel = e.mods.isShiftDown() ? step : 0.25;
    setValue (dragStartValue - e.getDistanceFromDragStartY() * perPixel, juce::sendNotification);
}

void ValueBox::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (onDragStart != nullptr)
        onDragStart();

    setValue (value + (w.deltaY > 0 ? 1.0 : -1.0) * (e.mods.isShiftDown() ? step : 1.0), juce::sendNotification);
}

void ValueBox::mouseDoubleClick (const juce::MouseEvent&)
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (monoFont ((float) getHeight() * 0.5f, true));
    editor->setText (juce::String (value, decimals), false);
    editor->setJustification (juce::Justification::centredLeft);
    editor->selectAll();
    editor->onReturnKey = [this] { commit (editor->getText().getDoubleValue()); };
    editor->onEscapeKey = [this] { commit (value); };
    editor->onFocusLost = [this] { commit (editor->getText().getDoubleValue()); };
    addAndMakeVisible (*editor);
    resized();
    editor->grabKeyboardFocus();
}

void ValueBox::commit (double v)
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ValueBox> (this), v]
    {
        if (safe != nullptr && safe->editor != nullptr)
        {
            safe->editor = nullptr;

            if (safe->onDragStart != nullptr)
                safe->onDragStart();

            safe->setValue (v, juce::sendNotification);

            if (auto parent = safe->getTopLevelComponent())
                parent->grabKeyboardFocus();
        }
    });
}

void ValueBox::resized()
{
    if (editor != nullptr)
        editor->setBounds (getLocalBounds().reduced (2));
}

//==============================================================================
void LevelMeter::push (float left, float right)
{
    const float in[2] = { left, right };

    for (int i = 0; i < 2; ++i)
    {
        // Fast attack, smooth fall
        levels[i] = in[i] > levels[i] ? in[i] : levels[i] * 0.86f;

        if (in[i] >= holds[i])
        {
            holds[i] = in[i];
            holdFrames[i] = 40;
        }
        else if (--holdFrames[i] < 0)
        {
            holds[i] *= 0.92f;
        }

        if (in[i] >= 0.999f)
            clipped = true;
    }

    repaint();
}

float LevelMeter::proportionFor (float gain)
{
    return juce::jmap (juce::Decibels::gainToDecibels (gain, -60.0f), -60.0f, 0.0f, 0.0f, 1.0f);
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    if (vertical)
    {
        // Two slim columns rising from the bottom, green through amber
        const float barWidth = std::min (5.0f, (r.getWidth() - 2.0f) * 0.5f), gap = 2.0f;
        auto bars = r.withSizeKeepingCentre (barWidth * 2.0f + gap, r.getHeight());

        for (int i = 0; i < 2; ++i)
        {
            auto bar = juce::Rectangle<float> (bars.getX() + i * (barWidth + gap), bars.getY(), barWidth, bars.getHeight());
            g.setColour (titleBar);
            g.fillRoundedRectangle (bar, 2.0f);

            const float level = proportionFor (levels[i]) * bar.getHeight();
            juce::ColourGradient grad (juce::Colour (0xff4fd18a), 0.0f, bar.getBottom(), juce::Colour (0xffffcf5a), 0.0f, bar.getY(), false);
            grad.addColour (0.7, juce::Colour (0xffb8e35a));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bar.withTop (bar.getBottom() - std::max (0.0f, level)), 2.0f);

            g.setColour (text.withAlpha (0.75f));
            g.fillRect (bar.getX(), bar.getBottom() - proportionFor (holds[i]) * bar.getHeight(), bar.getWidth(), 1.5f);

            if (clipped)
            {
                g.setColour (danger);
                g.fillRoundedRectangle (bar.removeFromTop (5.0f), 2.0f);
            }
        }

        return;
    }

    const float barHeight = 4.0f, gap = 3.0f;
    auto bars = r.withSizeKeepingCentre (r.getWidth(), barHeight * 2.0f + gap);

    auto toX = [&] (float gain)
    {
        const float dbValue = juce::Decibels::gainToDecibels (gain, -60.0f);
        return juce::jmap (dbValue, -60.0f, 0.0f, 0.0f, bars.getWidth());
    };

    for (int i = 0; i < 2; ++i)
    {
        auto bar = juce::Rectangle<float> (bars.getX(), bars.getY() + i * (barHeight + gap), bars.getWidth(), barHeight);
        g.setColour (titleBar);
        g.fillRoundedRectangle (bar, 2.0f);

        juce::ColourGradient grad (juce::Colour (0xff4fd18a), bar.getX(), 0.0f, juce::Colour (0xffffcf5a), bar.getRight(), 0.0f, false);
        grad.addColour (0.7, juce::Colour (0xffb8e35a));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar.withWidth (std::max (0.0f, toX (levels[i]))), 2.0f);

        g.setColour (text.withAlpha (0.75f));
        g.fillRect (bar.getX() + toX (holds[i]) - 1.0f, bar.getY(), 1.5f, bar.getHeight());

        if (clipped)
        {
            g.setColour (danger);
            g.fillRoundedRectangle (bar.removeFromRight (5.0f), 2.0f);
        }
    }
}

void LevelMeter::mouseDown (const juce::MouseEvent&)
{
    clipped = false;
    repaint();
}

//==============================================================================
Fader::Fader (double def)
    : defaultValue (def), value (def)
{
    setRepaintsOnMouseActivity (true);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void Fader::setValue (double v, juce::NotificationType nt)
{
    v = juce::jlimit (0.0, 1.0, v);

    if (v == value)
        return;

    value = v;

    if (formatValue != nullptr)
        setTooltip (formatValue (value));

    repaint();

    if (nt != juce::dontSendNotification && onValueChange != nullptr)
        onValueChange();
}

void Fader::change (double v)
{
    setValue (v, juce::sendNotification);
}

void Fader::paint (juce::Graphics& g)
{
    // A slot down the middle, lit below the cap, with a tick where the default sits
    const float cx = getWidth() * 0.5f;
    const float top = capHeight * 0.5f, bottom = getHeight() - capHeight * 0.5f;
    const float capY = bottom - (float) value * travel();
    auto slot = juce::Rectangle<float>::leftTopRightBottom (cx - 2.0f, top, cx + 2.0f, bottom);

    g.setColour (background);
    g.fillRoundedRectangle (slot, 2.0f);
    g.setColour (textDim.withAlpha (isMouseOverOrDragging() ? 0.9f : 0.6f));
    g.fillRoundedRectangle (slot.withTop (capY), 2.0f);

    const float unityY = bottom - (float) defaultValue * travel();
    g.setColour (textDim.withAlpha (0.7f));
    g.fillRect (cx - 12.0f, unityY - 0.5f, 7.0f, 1.0f);
    g.fillRect (cx + 5.0f, unityY - 0.5f, 7.0f, 1.0f);

    // The cap: a raised bar with a line across its middle
    auto cap = juce::Rectangle<float> (std::min (34.0f, (float) getWidth() - 4.0f), (float) capHeight).withCentre ({ cx, capY });
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), 5.0f);
    g.setColour (isMouseOverOrDragging() ? raised.brighter (0.25f) : raised.brighter (0.12f));
    g.fillRoundedRectangle (cap, 5.0f);
    g.setColour (hairlineStrong);
    g.drawRoundedRectangle (cap.reduced (0.5f), 5.0f, 1.0f);
    g.setColour (theme::text);
    g.fillRect (cap.getX() + 6.0f, cap.getCentreY() - 0.75f, cap.getWidth() - 12.0f, 1.5f);
}

void Fader::mouseDown (const juce::MouseEvent& e)
{
    rightClicked = e.mods.isPopupMenu() && onRightClick != nullptr;

    if (rightClicked)
    {
        onRightClick();
        return;
    }

    dragStartValue = value;
    held = true;

    if (onDragStart != nullptr)
        onDragStart();
}

void Fader::mouseDrag (const juce::MouseEvent& e)
{
    if (rightClicked)
        return;

    const double perPixel = 1.0 / travel() * (e.mods.isShiftDown() ? 0.2 : 1.0);
    change (dragStartValue - e.getDistanceFromDragStartY() * perPixel);
}

void Fader::mouseUp (const juce::MouseEvent&)
{
    if (std::exchange (held, false) && onDragEnd != nullptr)
        onDragEnd();
}

void Fader::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDragStart != nullptr)
        onDragStart();

    change (defaultValue);
}

void Fader::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (onDragStart != nullptr)
        onDragStart();

    change (value + (w.deltaY > 0 ? 0.02 : -0.02));
}

} // namespace nightshift
