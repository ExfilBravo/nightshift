// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Theme.h"
#include "BinaryData.h"

namespace nightshift::theme
{

namespace
{
    // Segoe UI's ascent + descent (what JUCE calls a font's height) for each unit of its letters'
    // size (em), from its own metrics: 2210 + 514 in 2048
    constexpr float segoeHeightPerEm = 2724.0f / 2048.0f;

    // Selawik's two faces, made (and so known to the system by name) the first time they're wanted
    struct SelawikFaces
    {
        juce::Typeface::Ptr regular, semibold;
    };

    const SelawikFaces& selawikFaces()
    {
        static const SelawikFaces faces { juce::Typeface::createSystemTypefaceFor (BinaryData::selawk_ttf, (size_t) BinaryData::selawk_ttfSize),
                                          juce::Typeface::createSystemTypefaceFor (BinaryData::selawksb_ttf, (size_t) BinaryData::selawksb_ttfSize) };
        return faces;
    }

    // Where Segoe UI isn't installed (a Mac), or NIGHTSHIFT_FONT=selawik says so (texttest on Windows).
    // Not Helvetica Neue any more: at the same height its letters were 14% bigger and wider, and
    // didn't fit where Segoe UI's do (the user chose Selawik, 2026-11-15)
    bool selawikStandsIn()
    {
        static const bool standsIn = juce::SystemStats::getEnvironmentVariable ("NIGHTSHIFT_FONT", {}).equalsIgnoreCase ("selawik")
                                       || ! juce::Font::findAllTypefaceNames().contains ("Segoe UI");
        return standsIn;
    }
}

juce::Font font (float height, bool bold)
{
    // Not Segoe UI Variable: JUCE draws a variable font at its default weight whatever
    // style is asked for, so its Semibold comes out Regular
    if (selawikStandsIn())
        return selawikFont (height, bold);

    return juce::Font (juce::FontOptions ("Segoe UI", bold ? "Semibold" : "Regular", height));
}

juce::Font selawikFont (float height, bool bold)
{
    // Selawik's letters are Segoe UI's widths, but its own ascent and descent are less (1.200 em
    // against 1.330): sized by its letters, as Segoe UI's are at this height, it takes the same room
    const auto& faces = selawikFaces();
    return juce::Font (juce::FontOptions (bold ? faces.semibold : faces.regular).withPointHeight (height / segoeHeightPerEm));
}

juce::Typeface::Ptr selawik (bool bold)
{
    if (! selawikStandsIn())
        return {};

    const auto& faces = selawikFaces();
    return bold ? faces.semibold : faces.regular;
}

float sizeOf (const juce::Font& f)
{
    // A font with Selawik in it where it stands in (font()'s): back from its letters to the
    // height asked for. Text in the default font was given a height as if it were Segoe UI
    if (selawikStandsIn() && f.getTypefaceName() != juce::Font::getDefaultSansSerifFontName())
        return f.getHeightInPoints() * segoeHeightPerEm;

    return f.getHeight();
}

juce::String shortcutText (const juce::String& words)
{
    // A Mac's words; NIGHTSHIFT_KEYS=mac says them on Windows too (with NIGHTSHIFT_FONT=selawik, texttest
    // then draws what a Mac does)
   #if JUCE_MAC
    constexpr bool mac = true;
   #else
    static const bool mac = juce::SystemStats::getEnvironmentVariable ("NIGHTSHIFT_KEYS", {}).equalsIgnoreCase ("mac");
   #endif

    if (! mac)
        return words;

    return words.replace ("Shift/Ctrl/Alt+", "Shift/Cmd/Option+").replace ("Ctrl+", "Cmd+").replace ("Ctrl K", "Cmd K").replace ("Alt+", "Option+");
}

juce::Font monoFont (float height, bool bold)
{
    return font (height, bold);
}

juce::Font labelFont()
{
    return font (14.0f, true).withExtraKerningFactor (0.06f);
}

juce::Image logo()
{
    return juce::ImageCache::getFromMemory (BinaryData::logo_64_png, BinaryData::logo_64_pngSize);
}

namespace icons
{
    // All icons are drawn in a 0..10 box; callers scale them to fit

    juce::Path play()
    {
        juce::Path p;
        p.addTriangle (2.0f, 1.0f, 2.0f, 9.0f, 9.0f, 5.0f);
        return p;
    }

    juce::Path stop()
    {
        juce::Path p;
        p.addRoundedRectangle (2.0f, 2.0f, 6.0f, 6.0f, 0.8f);
        return p;
    }

    juce::Path record()
    {
        juce::Path p;
        p.addEllipse (2.0f, 2.0f, 6.0f, 6.0f);
        return p;
    }

    juce::Path plus()
    {
        juce::Path p;
        p.addRectangle (4.2f, 1.0f, 1.6f, 8.0f);
        p.addRectangle (1.0f, 4.2f, 8.0f, 1.6f);
        return p;
    }

    juce::Path menu()
    {
        juce::Path p;

        for (float y : { 2.0f, 4.4f, 6.8f })
            p.addRoundedRectangle (1.0f, y, 8.0f, 1.2f, 0.5f);

        return p;
    }

    juce::Path close()
    {
        juce::Path p;
        p.startNewSubPath (2.5f, 2.5f);
        p.lineTo (7.5f, 7.5f);
        p.startNewSubPath (7.5f, 2.5f);
        p.lineTo (2.5f, 7.5f);
        return p;
    }

    juce::Path playlist()
    {
        juce::Path p;
        p.addRoundedRectangle (0.5f, 1.0f, 5.0f, 2.2f, 0.5f);
        p.addRoundedRectangle (3.5f, 4.0f, 6.0f, 2.2f, 0.5f);
        p.addRoundedRectangle (1.5f, 7.0f, 4.0f, 2.2f, 0.5f);
        return p;
    }

    juce::Path rack()
    {
        juce::Path p;

        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 4; ++col)
                p.addRoundedRectangle (0.6f + col * 2.4f, 1.0f + row * 3.0f, 1.8f, 2.2f, 0.4f);

        return p;
    }

    juce::Path piano()
    {
        juce::Path p;
        p.addRoundedRectangle (0.5f, 1.5f, 9.0f, 7.0f, 0.6f);
        return p;
    }

    juce::Path exportAudio()
    {
        juce::Path p;
        p.addRectangle (4.3f, 1.0f, 1.4f, 5.0f);
        p.addTriangle (2.0f, 5.0f, 8.0f, 5.0f, 5.0f, 8.0f);
        p.addRectangle (1.0f, 8.6f, 8.0f, 1.2f);
        return p;
    }

    juce::Path left()
    {
        juce::Path p;
        p.addTriangle (7.0f, 1.5f, 7.0f, 8.5f, 2.5f, 5.0f);
        return p;
    }

    juce::Path right()
    {
        juce::Path p;
        p.addTriangle (3.0f, 1.5f, 3.0f, 8.5f, 7.5f, 5.0f);
        return p;
    }

    juce::Path heart()
    {
        // Filled, for Donate (already on the 0..10 box)
        juce::Path p;
        p.startNewSubPath (5.0f, 9.2f);
        p.cubicTo (1.2f, 6.6f, 0.0f, 4.6f, 0.4f, 3.0f);
        p.cubicTo (0.9f, 0.8f, 3.9f, 0.3f, 5.0f, 2.5f);
        p.cubicTo (6.1f, 0.3f, 9.1f, 0.8f, 9.6f, 3.0f);
        p.cubicTo (10.0f, 4.6f, 8.8f, 6.6f, 5.0f, 9.2f);
        p.closeSubPath();
        return p;
    }

    juce::Path metronome()
    {
        // Pyramid body with the pendulum swung right
        juce::Path p;
        p.startNewSubPath (3.6f, 1.0f);
        p.lineTo (6.4f, 1.0f);
        p.lineTo (8.6f, 9.2f);
        p.lineTo (1.4f, 9.2f);
        p.closeSubPath();
        p.setUsingNonZeroWinding (false);
        p.addTriangle (2.9f, 7.0f, 7.1f, 7.0f, 7.4f, 8.2f);
        p.addRectangle (2.6f, 7.0f, 4.8f, 1.2f);

        juce::Path arm;
        arm.addRectangle (-0.55f, -6.0f, 1.1f, 6.4f);
        arm.applyTransform (juce::AffineTransform::rotation (0.5f).translated (5.0f, 7.2f));
        p.addPath (arm);
        return p;
    }

    //==============================================================================
    // Line icons are drawn on a 24-unit grid, like most icon sets, then scaled into the box
    static juce::Path fromGrid (juce::Path p)
    {
        p.applyTransform (juce::AffineTransform::scale (10.0f / 24.0f));
        return p;
    }

    juce::Path loop()
    {
        juce::Path p;
        p.startNewSubPath (17.0f, 3.0f);  p.lineTo (20.0f, 6.0f);  p.lineTo (17.0f, 9.0f);
        p.startNewSubPath (4.0f, 12.0f);  p.lineTo (4.0f, 10.0f);  p.quadraticTo (4.0f, 6.0f, 8.0f, 6.0f);  p.lineTo (20.0f, 6.0f);
        p.startNewSubPath (7.0f, 21.0f);  p.lineTo (4.0f, 18.0f);  p.lineTo (7.0f, 15.0f);
        p.startNewSubPath (20.0f, 12.0f); p.lineTo (20.0f, 14.0f); p.quadraticTo (20.0f, 18.0f, 16.0f, 18.0f); p.lineTo (4.0f, 18.0f);
        return fromGrid (p);
    }

    juce::Path more()
    {
        juce::Path p;

        for (float x : { 5.0f, 12.0f, 19.0f })
            p.addEllipse (x - 1.1f, 10.9f, 2.2f, 2.2f);

        return fromGrid (p);
    }

    juce::Path exportLine()
    {
        juce::Path p;
        p.startNewSubPath (12.0f, 4.0f);  p.lineTo (12.0f, 15.0f);
        p.startNewSubPath (7.5f, 10.5f);  p.lineTo (12.0f, 15.0f); p.lineTo (16.5f, 10.5f);
        p.startNewSubPath (5.0f, 20.0f);  p.lineTo (19.0f, 20.0f);
        return fromGrid (p);
    }

    juce::Path plusLine()
    {
        juce::Path p;
        p.startNewSubPath (12.0f, 5.0f);  p.lineTo (12.0f, 19.0f);
        p.startNewSubPath (5.0f, 12.0f);  p.lineTo (19.0f, 12.0f);
        return fromGrid (p);
    }

    juce::Path chevronDown()
    {
        juce::Path p;
        p.startNewSubPath (7.0f, 10.0f);  p.lineTo (12.0f, 15.0f); p.lineTo (17.0f, 10.0f);
        return fromGrid (p);
    }

    juce::Path chevronRight()
    {
        juce::Path p;
        p.startNewSubPath (10.0f, 7.0f);  p.lineTo (15.0f, 12.0f); p.lineTo (10.0f, 17.0f);
        return fromGrid (p);
    }

    juce::Path search()
    {
        juce::Path p;
        p.addEllipse (4.5f, 4.5f, 13.0f, 13.0f);
        p.startNewSubPath (20.0f, 20.0f); p.lineTo (15.8f, 15.8f);
        return fromGrid (p);
    }

    juce::Path refresh()
    {
        // Most of a circle, clockwise from three o'clock to twelve, with its arrowhead at the top right
        juce::Path p;
        p.addCentredArc (12.0f, 12.0f, 8.5f, 8.5f, 0.0f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::twoPi, true);
        p.cubicTo (14.4f, 3.5f, 16.7f, 4.4f, 18.4f, 6.1f);
        p.lineTo (20.5f, 8.2f);
        p.startNewSubPath (20.5f, 3.5f); p.lineTo (20.5f, 8.2f); p.lineTo (15.8f, 8.2f);
        return fromGrid (p);
    }

    juce::Path expand()
    {
        juce::Path p;
        p.startNewSubPath (4.0f, 9.0f);   p.lineTo (4.0f, 4.0f);   p.lineTo (9.0f, 4.0f);
        p.startNewSubPath (20.0f, 15.0f); p.lineTo (20.0f, 20.0f); p.lineTo (15.0f, 20.0f);
        p.startNewSubPath (15.0f, 4.0f);  p.lineTo (20.0f, 4.0f);  p.lineTo (20.0f, 9.0f);
        p.startNewSubPath (9.0f, 20.0f);  p.lineTo (4.0f, 20.0f);  p.lineTo (4.0f, 15.0f);
        return fromGrid (p);
    }

    juce::Path pointer()
    {
        juce::Path p;
        p.startNewSubPath (6.0f, 4.0f); p.lineTo (18.5f, 11.2f); p.lineTo (12.9f, 12.9f); p.lineTo (10.5f, 18.5f); p.closeSubPath();
        return fromGrid (p);
    }

    juce::Path pencil()
    {
        juce::Path p;
        p.startNewSubPath (4.5f, 19.5f); p.lineTo (8.3f, 18.6f); p.lineTo (19.0f, 7.9f); p.lineTo (16.1f, 5.0f); p.lineTo (5.4f, 15.7f); p.closeSubPath();
        p.startNewSubPath (14.0f, 7.0f); p.lineTo (17.0f, 10.0f);
        return fromGrid (p);
    }

    juce::Path eraser()
    {
        juce::Path p;
        p.startNewSubPath (4.6f, 14.6f); p.lineTo (13.8f, 5.4f); p.lineTo (16.6f, 5.4f); p.lineTo (18.6f, 7.4f);
        p.lineTo (18.6f, 10.2f); p.lineTo (11.0f, 18.8f); p.lineTo (8.8f, 18.8f); p.closeSubPath();
        p.startNewSubPath (8.0f, 20.5f); p.lineTo (19.0f, 20.5f);
        return fromGrid (p);
    }

    juce::Path scissors()
    {
        // Two handles at the bottom, the blades crossing above them
        juce::Path p;
        p.addEllipse (4.0f, 14.5f, 5.5f, 5.5f);
        p.addEllipse (14.5f, 14.5f, 5.5f, 5.5f);
        p.startNewSubPath (8.6f, 15.1f); p.lineTo (17.5f, 4.0f);
        p.startNewSubPath (15.4f, 15.1f); p.lineTo (6.5f, 4.0f);
        return fromGrid (p);
    }

    juce::Path automation()
    {
        // A line through three points, as on an automation lane
        juce::Path p;
        p.startNewSubPath (3.5f, 17.5f); p.lineTo (9.0f, 9.5f); p.lineTo (14.5f, 14.0f); p.lineTo (20.5f, 6.0f);
        p.addEllipse (7.4f, 7.9f, 3.2f, 3.2f);
        p.addEllipse (12.9f, 12.4f, 3.2f, 3.2f);
        return fromGrid (p);
    }

    juce::Path magnet()
    {
        juce::Path p;
        p.startNewSubPath (6.0f, 4.0f); p.lineTo (6.0f, 11.0f);
        p.addCentredArc (12.0f, 11.0f, 6.0f, 6.0f, 0.0f, juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::pi * 0.5f, false);
        p.lineTo (18.0f, 4.0f);
        p.startNewSubPath (6.0f, 8.0f);  p.lineTo (10.0f, 8.0f);
        p.startNewSubPath (14.0f, 8.0f); p.lineTo (18.0f, 8.0f);
        return fromGrid (p);
    }

    juce::Path mic()
    {
        juce::Path p;
        p.addRoundedRectangle (9.0f, 3.5f, 6.0f, 11.0f, 3.0f);
        p.startNewSubPath (5.5f, 11.5f);
        p.addCentredArc (12.0f, 11.5f, 6.5f, 6.5f, 0.0f, juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::pi * 0.5f, false);
        p.startNewSubPath (12.0f, 18.0f); p.lineTo (12.0f, 21.0f);
        return fromGrid (p);
    }

    juce::Path grid()
    {
        juce::Path p;

        for (float x : { 4.0f, 14.0f })
            for (float y : { 4.0f, 14.0f })
                p.addRoundedRectangle (x, y, 6.0f, 6.0f, 1.5f);

        return fromGrid (p);
    }

    juce::Path panelRight()
    {
        juce::Path p;
        p.addRoundedRectangle (3.5f, 4.5f, 17.0f, 15.0f, 2.5f);
        p.startNewSubPath (14.0f, 4.5f);  p.lineTo (14.0f, 19.5f);
        p.startNewSubPath (16.5f, 8.5f);  p.lineTo (18.0f, 8.5f);
        p.startNewSubPath (16.5f, 11.5f); p.lineTo (18.0f, 11.5f);
        return fromGrid (p);
    }

    juce::Path sparkle()
    {
        // A four-point star with a small one beside it: the AI drummer
        juce::Path p;
        auto star = [&p] (float cx, float cy, float r)
        {
            const float k = r * 0.16f;
            p.startNewSubPath (cx, cy - r);
            p.quadraticTo (cx + k, cy - k, cx + r, cy);
            p.quadraticTo (cx + k, cy + k, cx, cy + r);
            p.quadraticTo (cx - k, cy + k, cx - r, cy);
            p.quadraticTo (cx - k, cy - k, cx, cy - r);
            p.closeSubPath();
        };
        star (10.5f, 13.0f, 8.0f);
        star (18.5f, 5.5f, 3.2f);
        return fromGrid (p);
    }

    juce::Path keyboard()
    {
        // A computer keyboard: its outline, two rows of keys and the space bar
        juce::Path p;
        p.addRoundedRectangle (2.5f, 6.0f, 19.0f, 12.0f, 2.0f);

        for (float x : { 6.0f, 9.5f, 13.0f, 16.5f })
        {
            p.startNewSubPath (x, 9.5f);
            p.lineTo (x + 1.0f, 9.5f);
        }

        for (float x : { 7.5f, 11.0f, 14.5f })
        {
            p.startNewSubPath (x, 12.2f);
            p.lineTo (x + 1.0f, 12.2f);
        }

        p.startNewSubPath (8.0f, 14.8f);
        p.lineTo (16.0f, 14.8f);
        return fromGrid (p);
    }

    juce::Path metronomeLine()
    {
        juce::Path p;
        p.startNewSubPath (9.0f, 3.5f);   p.lineTo (15.0f, 3.5f);  p.lineTo (18.8f, 20.0f); p.lineTo (5.2f, 20.0f); p.closeSubPath();
        p.startNewSubPath (12.0f, 16.0f); p.lineTo (17.5f, 7.0f);
        p.startNewSubPath (8.0f, 15.0f);  p.lineTo (16.0f, 15.0f);
        return fromGrid (p);
    }
}

} // namespace nightshift::theme

namespace nightshift
{

using namespace theme;

NightLookAndFeel::NightLookAndFeel()
{
    // Text in the default font: Segoe UI, or Selawik where it stands in (getTypefaceForFont)
    if (selawik (false) == nullptr)
        setDefaultSansSerifTypefaceName (font (14.0f).getTypefaceName());

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::textColourId, text);
    setColour (juce::TextButton::buttonColourId, titleBar);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::PopupMenu::backgroundColourId, panelLight);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentCool.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, textFaint);
    setColour (juce::ComboBox::backgroundColourId, panelLight);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, hairlineStrong);
    setColour (juce::ComboBox::arrowColourId, textFaint);
    setColour (juce::TextEditor::backgroundColourId, panelLight);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accentCool.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId, hairlineStrong);
    setColour (juce::TextEditor::focusedOutlineColourId, accentCool.withAlpha (0.7f));
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, panelLight);
    setColour (juce::Slider::textBoxOutlineColourId, hairlineStrong);
    setColour (juce::ScrollBar::thumbColourId, raised);
    setColour (juce::TooltipWindow::backgroundColourId, titleBar);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::AlertWindow::backgroundColourId, panelLight);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, hairlineStrong);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ListBox::textColourId, text);
    setColour (juce::TableHeaderComponent::backgroundColourId, panelLight);
    setColour (juce::TableHeaderComponent::textColourId, text);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::GroupComponent::textColourId, textDim);
    setColour (juce::GroupComponent::outlineColourId, gridBar);

    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::windowBackground, background);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::widgetBackground, panelLight);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::menuBackground, panelLight);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::outline, hairlineStrong);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::defaultText, text);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::defaultFill, accentCool);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::highlightedText, juce::Colours::white);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::highlightedFill, accentCool.withAlpha (0.35f));
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::menuText, text);
}

void NightLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                         float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (1.5f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float arcRadius = radius - 1.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = slider.getProperties()["bipolar"];
    const auto fill = slider.findColour (juce::Slider::rotarySliderFillColourId);

    // Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (titleBar);
    g.strokePath (track, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc, from the centre for bipolar controls like pan
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, std::min (from, angle), std::max (from, angle), true);
    g.setColour (slider.isEnabled() ? fill : textFaint);
    g.strokePath (value, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Pointer dot on a flat body
    const float bodyRadius = radius - 4.5f;
    g.setColour (raised);
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre));
    const auto tip = centre.getPointOnCircumference (bodyRadius - 2.5f, angle);
    g.setColour (text);
    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (tip));
}

void NightLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                         float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, pos, minPos, maxPos, style, slider);
        return;
    }

    // A thin track filled up to a round thumb
    const float cy = (float) y + height * 0.5f;
    auto track = juce::Rectangle<float> ((float) x, cy - 2.0f, (float) width, 4.0f);
    g.setColour (titleBar);
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (slider.isEnabled() ? textDim : textFaint);
    g.fillRoundedRectangle (track.withRight (pos), 2.0f);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (pos - 5.5f, cy - 4.5f, 11.0f, 11.0f);
    g.setColour (slider.isMouseOverOrDragging() ? juce::Colours::white : text);
    g.fillEllipse (pos - 5.0f, cy - 5.0f, 10.0f, 10.0f);
}

void NightLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& colour,
                                             bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : colour;

    if (down)              c = c.brighter (0.12f);
    else if (highlighted)  c = c.brighter (0.06f);

    g.setColour (c);
    g.fillRoundedRectangle (r, controlRadius);
    g.setColour (hairlineStrong);
    g.drawRoundedRectangle (r, controlRadius, 1.0f);
}

juce::Font NightLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    // 15 px however short the button, 16 once it's tall enough
    return font (juce::jlimit (15.0f, 16.0f, buttonHeight * 0.6f), true);
}

void NightLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto f = getTextButtonFont (button, button.getHeight());
    g.setFont (f);

    const auto colour = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    g.setColour (button.isEnabled() ? colour : textFaint);

    // JUCE's layout (LookAndFeel_V2): in from the rounded ends, as much as the words need
    const int yIndent = std::min (4, button.proportionOfHeight (0.3f));
    const int cornerSize = std::min (button.getHeight(), button.getWidth()) / 2;
    const int fontHeight = juce::roundToInt (f.getHeight() * 0.6f);
    const int leftIndent = std::min (fontHeight, 2 + cornerSize / (button.isConnectedOnLeft() ? 4 : 2));
    const int rightIndent = std::min (fontHeight, 2 + cornerSize / (button.isConnectedOnRight() ? 4 : 2));
    const int textWidth = button.getWidth() - leftIndent - rightIndent;

    if (textWidth > 0)
        g.drawFittedText (button.getButtonText(), leftIndent, yIndent, textWidth, button.getHeight() - yIndent * 2,
                          juce::Justification::centred, 2);
}

void NightLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (panelLight);
    g.setColour (hairlineStrong);
    g.drawRect (0, 0, width, height);
}

juce::Font NightLookAndFeel::getPopupMenuFont()
{
    return font (popupFontSize);
}

void NightLookAndFeel::getIdealPopupMenuItemSize (const juce::String& itemText, bool isSeparator, int standardMenuItemHeight,
                                                  int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = 13;
        return;
    }

    // The tick or icon column on the left, the text, room for a submenu's arrow on the right
    idealHeight = std::max (standardMenuItemHeight, popupItemHeight);
    idealWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), itemText)) + 32 + 24 + 36;
}

void NightLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                          bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& itemText,
                                          const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (hairlineStrong);
        g.fillRect (area.reduced (12, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (5, 1);
    const bool lit = isHighlighted && isActive;

    if (lit)
    {
        g.setColour (accentCool.withAlpha (0.24f));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
    }

    // Greyed-out items (notes such as "No instruments found yet") stay readable
    const auto colour = ! isActive ? textDim : lit ? juce::Colours::white : (textColour != nullptr ? *textColour : text);
    r.reduce (10, 0);

    // A tick, or the item's icon, in a column of their own
    auto mark = r.removeFromLeft (22).toFloat();
    r.removeFromLeft (10);

    if (icon != nullptr)
    {
        icon->drawWithin (g, mark.withSizeKeepingCentre (20.0f, 20.0f), juce::RectanglePlacement::centred, 1.0f);
    }
    else if (isTicked)
    {
        juce::Path tick;
        tick.startNewSubPath (0.0f, 5.0f);
        tick.lineTo (3.5f, 8.5f);
        tick.lineTo (10.0f, 1.5f);
        tick.applyTransform (tick.getTransformToScaleToFit (mark.withSizeKeepingCentre (14.0f, 12.0f), true));
        g.setColour (isActive ? accentCool.brighter (0.2f) : textDim);
        g.strokePath (tick, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (hasSubMenu)
    {
        auto chevron = icons::chevronRight();
        chevron.applyTransform (chevron.getTransformToScaleToFit (r.removeFromRight (12).toFloat().withSizeKeepingCentre (8.0f, 13.0f), true));
        g.setColour (lit ? juce::Colours::white : textDim);
        g.strokePath (chevron, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        r.removeFromRight (8);
    }

    g.setColour (colour);
    g.setFont (getPopupMenuFont());
    g.drawFittedText (itemText, r, juce::Justification::centredLeft, 1, 0.95f);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (lit ? juce::Colours::white.withAlpha (0.8f) : textDim);
        g.setFont (font (16.0f));
        g.drawText (shortcutKeyText, r, juce::Justification::centredRight, true);
    }
}

void NightLookAndFeel::getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& sectionName, int, int& idealWidth,
                                                                      int& idealHeight, const juce::PopupMenu::Options&)
{
    idealHeight = 34;
    idealWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font (16.0f, true), sectionName)) + 40;
}

void NightLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    // A heading over the items under it: bold, dimmer than they are, sitting low in its row
    g.setColour (textDim);
    g.setFont (font (16.0f, true));
    g.drawFittedText (sectionName, area.reduced (15, 0).withTrimmedBottom (4), juce::Justification::bottomLeft, 1);
}

void NightLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                      bool isVertical, int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    // No track, just a slim thumb that firms up under the mouse
    auto thumb = isVertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize)
                            : juce::Rectangle<int> (thumbStart, y, thumbSize, height);

    g.setColour (raised.brighter (isMouseDown ? 0.35f : (isMouseOver ? 0.2f : 0.0f)));
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.5f), 3.0f);
}

void NightLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& tip, int width, int height)
{
    g.setColour (titleBar);
    g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 6.0f);
    g.setColour (hairlineStrong);
    g.drawRoundedRectangle (0.5f, 0.5f, width - 1.0f, height - 1.0f, 6.0f, 1.0f);
    g.setColour (text);
    g.setFont (font (16.0f));
    g.drawFittedText (tip, 12, 4, width - 24, height - 8, juce::Justification::centredLeft, 3, 1.0f);
}

juce::Rectangle<int> NightLookAndFeel::getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> area)
{
    // One line up to 440 px wide; longer tips wrap onto up to three
    const int one = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font (16.0f), tip)) + 28;
    const int w = juce::jmin (440, one);
    const int lines = juce::jlimit (1, 3, (one + 439) / 440);
    const int h = 12 + 22 * lines;
    return juce::Rectangle<int> (pos.x + 12, pos.y + 18, w, h).constrainedWithin (area);
}

juce::Font NightLookAndFeel::getAlertWindowTitleFont()     { return font (19.0f, true); }
juce::Font NightLookAndFeel::getAlertWindowMessageFont()   { return font (16.0f); }
juce::Font NightLookAndFeel::getAlertWindowFont()          { return font (16.0f); }

void NightLookAndFeel::drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent& header, const juce::String& columnName,
                                              int columnId, int width, int height, bool isMouseOver, bool isMouseDown, int columnFlags)
{
    // JUCE's, with the name at 15 px (its own is half the header's height)
    juce::ignoreUnused (columnId, isMouseDown);
    auto highlight = header.findColour (juce::TableHeaderComponent::highlightColourId);

    if (isMouseOver)
        g.fillAll (highlight.withMultipliedAlpha (0.625f));

    juce::Rectangle<int> area (width, height);
    area.reduce (6, 0);

    if ((columnFlags & (juce::TableHeaderComponent::sortedForwards | juce::TableHeaderComponent::sortedBackwards)) != 0)
    {
        juce::Path arrow;
        arrow.addTriangle (0.0f, 0.0f, 0.5f, (columnFlags & juce::TableHeaderComponent::sortedForwards) != 0 ? -0.8f : 0.8f, 1.0f, 0.0f);
        g.setColour (textDim);
        g.fillPath (arrow, arrow.getTransformToScaleToFit (area.removeFromRight (height / 2).reduced (2).toFloat(), true));
    }

    g.setColour (header.findColour (juce::TableHeaderComponent::textColourId));
    g.setFont (font (15.0f, true));
    g.drawFittedText (columnName, area, juce::Justification::centredLeft, 1);
}

void NightLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
    g.setColour (panelLight);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (hairlineStrong);
    g.drawRoundedRectangle (r, 7.0f, 1.0f);

    auto chevron = icons::chevronDown();
    chevron.applyTransform (juce::AffineTransform::scale (1.5f).translated (width - 22.0f, height * 0.5f - 7.5f));
    g.setColour (textFaint);
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font NightLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return font (16.0f, true);
}

juce::Label* NightLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (font (15.0f));
    return l;
}

juce::Font NightLookAndFeel::getLabelFont (juce::Label& l)
{
    // Never under 15 px, whatever a label was given (JUCE's own windows' too)
    return font (std::max (15.0f, sizeOf (l.getFont())));
}

juce::Typeface::Ptr NightLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // Where Selawik stands in for Segoe UI, text in the default font gets it too, Semibold for bold
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        if (auto face = selawik (f.isBold() || f.getTypefaceStyle().containsIgnoreCase ("bold")))
            return face;

    return juce::LookAndFeel_V4::getTypefaceForFont (f);
}

void NightLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& e)
{
    g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), controlRadius);
}

void NightLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& e)
{
    if (! e.isEnabled())
        return;

    const bool focused = e.hasKeyboardFocus (true) && ! e.isReadOnly();
    g.setColour (e.findColour (focused ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (juce::Rectangle<int> (width, height).toFloat().reduced (0.5f), controlRadius, focused ? 1.5f : 1.0f);
}

} // namespace nightshift
