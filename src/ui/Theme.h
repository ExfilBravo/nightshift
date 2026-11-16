// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

namespace nightshift::theme
{
    // Night neutrals: blue-black, each surface a step lighter than the one behind it
    inline const juce::Colour background   { 0xff08090e };   // behind the panels
    inline const juce::Colour workspace    { 0xff08090e };
    inline const juce::Colour panel        { 0xff0f1219 };   // a panel's surface
    inline const juce::Colour panelLight   { 0xff151922 };   // controls sitting on a panel
    inline const juce::Colour titleBar     { 0xff1c212c };   // raised controls, empty steps
    inline const juce::Colour raised       { 0xff252b38 };   // the chosen segment, hover
    inline const juce::Colour outline      { 0xff05060a };
    inline const juce::Colour hairline     { 0x0effffff };   // panel edges and dividers
    inline const juce::Colour hairlineStrong { 0x18ffffff }; // control edges
    inline const juce::Colour gridLine     { 0xff141820 };
    inline const juce::Colour gridBeat     { 0xff1a1f29 };
    inline const juce::Colour gridBar      { 0xff292f3d };
    inline const juce::Colour text         { 0xffe9ecf4 };
    inline const juce::Colour textDim      { 0xffa3aabc };
    inline const juce::Colour textFaint    { 0xff8a92a9 };   // 4.5:1 or better on every surface (it was 0xff687087, 3.2-4)

    // One light of our own (the moon's amber) for play, the playhead and the loop; periwinkle
    // for selection and toggles; red only for recording and danger
    inline const juce::Colour accent       { 0xffffb054 };
    inline const juce::Colour accentCool   { 0xff8f9bff };
    inline const juce::Colour playhead     { 0xffffb054 };
    inline const juce::Colour stepOffA     { 0xff1c212c };
    inline const juce::Colour stepOffB     { 0xff1c212c };
    inline const juce::Colour danger       { 0xffff5a6a };
    inline const juce::Colour ledOn        { 0xff6fdc8c };
    inline const juce::Colour effectColour { 0xff5fd0c5 };   // teal: effect plugins

    constexpr float panelRadius = 12.0f, controlRadius = 8.0f;

    /** Segoe UI, where bold means semibold. Its figures are all one width, so numbers that
        change don't jitter. Where Segoe UI isn't installed (a Mac), Selawik: Microsoft's open
        stand-in for it (SIL Open Font License 1.1), bundled, the same widths, sized by its
        letters so it takes the room Segoe UI would. NIGHTSHIFT_FONT=selawik uses it on Windows
        too (texttest's way to check it there). */
    juce::Font font (float height, bool bold = false);

    /** Selawik as font() gives it where it stands in for Segoe UI (texttest compares the two). */
    juce::Font selawikFont (float height, bool bold = false);

    /** Selawik's face (Semibold for bold) when it stands in for Segoe UI; otherwise nothing. */
    juce::Typeface::Ptr selawik (bool bold);

    /** The height font() was asked for, from a font it made (Selawik's own JUCE height is less
        for the same letters); any other font's JUCE height. */
    float sizeOf (const juce::Font&);

    /** Shortcut words as this system says them: on a Mac, Ctrl is Cmd and Alt is Option. */
    juce::String shortcutText (const juce::String& windowsWords);
    juce::Font monoFont (float height, bool bold = false);

    /** The small capitals over readouts and sidebar sections. */
    juce::Font labelFont();

    /** The app's moon, for the top bar. */
    juce::Image logo();

    /** Small vector icons used on buttons, in a 0..10 box. The line icons are meant to be
        stroked (IconButton::setFilled (false)); the rest are filled. */
    namespace icons
    {
        juce::Path play();
        juce::Path stop();
        juce::Path record();
        juce::Path plus();
        juce::Path menu();
        juce::Path close();
        juce::Path playlist();
        juce::Path rack();
        juce::Path piano();
        juce::Path exportAudio();
        juce::Path left();
        juce::Path right();
        juce::Path metronome();
        juce::Path heart();

        // Line icons
        juce::Path loop();
        juce::Path more();
        juce::Path exportLine();
        juce::Path plusLine();
        juce::Path chevronDown();
        juce::Path chevronRight();
        juce::Path search();
        juce::Path refresh();
        juce::Path expand();
        juce::Path metronomeLine();
        juce::Path pointer();
        juce::Path pencil();
        juce::Path eraser();
        juce::Path scissors();
        juce::Path automation();
        juce::Path magnet();
        juce::Path mic();
        juce::Path grid();
        juce::Path panelRight();
        juce::Path sparkle();
        juce::Path keyboard();
    }
}

namespace nightshift
{

class NightLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NightLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    /** JUCE's, but a greyed-out button's words in the faint grey (still 4.5 to 1), not at a quarter strength. */
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    /** Menus (right-click and the rest), sized to read at a glance: 17.5 px text in 34 px rows,
        white on the panel, greyed-out items still readable, headers and shortcuts in a dimmer grey. */
    static constexpr float popupFontSize = 17.5f;
    static constexpr int popupItemHeight = 34;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override           { return 6; }
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& itemText,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& text, int standardMenuItemHeight, int& idealWidth,
                                                        int& idealHeight, const juce::PopupMenu::Options&) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override     { return 10; }

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    // Dialogs and JUCE's own tables at the studio's sizes (JUCE's: 12 px for the dialog's boxes, half the
    // header's height for a column's name)
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& columnName, int columnId,
                                int width, int height, bool isMouseOver, bool isMouseDown, int columnFlags) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
};

} // namespace nightshift
