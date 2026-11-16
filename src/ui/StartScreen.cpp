// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "StartScreen.h"
#include "Donate.h"   // donate::pitch
#include "TextAudit.h"
#include "Theme.h"
#include "TrackIcons.h"

namespace nightshift
{

using namespace theme;

namespace
{
    // A folder too long for its row keeps its end, the folder a project is in ("...\Music\Beats"), not its
    // start (which is the same C:\Users\... for all of them)
    juce::String endThatFits (const juce::String& path, const juce::Font& f, float width)
    {
        if (juce::GlyphArrangement::getStringWidth (f, path) <= width)
            return path;

        const auto dots = juce::String::charToString ((juce_wchar) 0x2026);

        for (int start = 1; start < path.length(); ++start)
            if (const auto shorter = dots + path.substring (start); juce::GlyphArrangement::getStringWidth (f, shorter) <= width)
                return shorter;

        return dots;
    }

    // A folder, for Open a project (on the icons' 0..10 box, stroked)
    juce::Path folderIcon()
    {
        juce::Path p;
        p.startNewSubPath (1.0f, 8.5f);
        p.lineTo (1.0f, 2.0f);
        p.lineTo (4.0f, 2.0f);
        p.lineTo (5.0f, 3.2f);
        p.lineTo (9.0f, 3.2f);
        p.lineTo (9.0f, 8.5f);
        p.closeSubPath();
        return p;
    }

    // A page with its corner turned, for a recent project
    juce::Path pageIcon()
    {
        juce::Path p;
        p.startNewSubPath (2.0f, 1.0f);
        p.lineTo (6.0f, 1.0f);
        p.lineTo (8.5f, 3.5f);
        p.lineTo (8.5f, 9.0f);
        p.lineTo (2.0f, 9.0f);
        p.closeSubPath();
        p.startNewSubPath (6.0f, 1.0f);
        p.lineTo (6.0f, 3.5f);
        p.lineTo (8.5f, 3.5f);
        return p;
    }

    void strokeIcon (juce::Graphics& g, juce::Path icon, juce::Rectangle<float> area, juce::Colour colour)
    {
        icon.applyTransform (icon.getTransformToScaleToFit (area, true));
        g.setColour (colour);
        g.strokePath (icon, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

StartScreen::StartScreen()
{
    setWantsKeyboardFocus (true);
    setOpaque (false);
}

void StartScreen::setRecent (const juce::StringArray& paths)
{
    recent.clear();

    for (auto& path : paths)
        if (juce::File f (path); juce::File::isAbsolutePath (path) && f.existsAsFile() && recent.size() < 6)
            recent.add (f);

    resized();
    repaint();
}

void StartScreen::setShowAtStartup (bool shouldShow)
{
    showAtStartup = shouldShow;
    repaint();
}

void StartScreen::setDonationShown (bool shouldShow)
{
    donation = shouldShow ? Donation::asking : Donation::hidden;
    resized();
    repaint();
}

juce::Rectangle<int> StartScreen::boundsOf (Kind kind) const
{
    for (auto& h : hits)
        if (h.kind == kind)
            return h.area;

    return {};
}

juce::Rectangle<int> StartScreen::getDonateBounds() const     { return boundsOf (Kind::donate); }
juce::Rectangle<int> StartScreen::getNoThanksBounds() const   { return boundsOf (Kind::noThanks); }

juce::Rectangle<int> StartScreen::getTemplateBounds (const juce::String& id) const
{
    const auto& list = templates::all();

    for (auto& h : hits)
        if (h.kind == Kind::templateCard && juce::isPositiveAndBelow (h.index, (int) list.size()) && list[(size_t) h.index].id == id)
            return h.area;

    return {};
}

juce::Rectangle<int> StartScreen::getRecentBounds (int index) const
{
    for (auto& h : hits)
        if (h.kind == Kind::recent && h.index == index)
            return h.area;

    return {};
}

//==============================================================================
void StartScreen::resized()
{
    hits.clear();

    // The card: as big as it needs, smaller in a small window
    card = getLocalBounds().withSizeKeepingCentre (std::min (1060, getWidth() - 48), std::min (640, getHeight() - 48));
    auto r = card.reduced (32, 28);

    auto header = r.removeFromTop (70);
    closeArea = juce::Rectangle<int> (header.getRight() - 34, header.getY() - 6, 40, 40);
    hits.push_back ({ Kind::close, -1, closeArea });

    auto footer = r.removeFromBottom (34);
    switchArea = footer.withWidth (300);
    hits.push_back ({ Kind::startupSwitch, -1, switchArea });
    r.removeFromBottom (14);

    // The donation banner, across the foot above the switch (the first time only); its buttons
    // at its right while it's asking
    donationArea = {};

    if (donation != Donation::hidden)
    {
        donationArea = r.removeFromBottom (62);
        r.removeFromBottom (16);

        if (donation == Donation::asking)
        {
            auto buttons = donationArea.reduced (14, 13);
            hits.push_back ({ Kind::donate, -1, buttons.removeFromRight (112) });
            buttons.removeFromRight (8);
            hits.push_back ({ Kind::noThanks, -1, buttons.removeFromRight (104) });
        }
    }

    // Templates on the left, a grid of three across; Open, recent and the demo on the right
    sideArea = r.removeFromRight (std::max (260, r.getWidth() * 34 / 100));
    r.removeFromRight (28);
    templatesArea = r;

    auto grid = templatesArea.withTrimmedTop (30);
    const auto& list = templates::all();
    const int columns = 3, gap = 14;
    const int rows = ((int) list.size() + columns - 1) / columns;
    const int cardW = (grid.getWidth() - gap * (columns - 1)) / columns;
    const int cardH = std::min (190, (grid.getHeight() - gap * (rows - 1)) / std::max (1, rows));

    for (int i = 0; i < (int) list.size(); ++i)
        hits.push_back ({ Kind::templateCard, i, { grid.getX() + (i % columns) * (cardW + gap), grid.getY() + (i / columns) * (cardH + gap), cardW, cardH } });

    auto side = sideArea.withTrimmedTop (30);
    hits.push_back ({ Kind::open, -1, side.removeFromTop (54) });
    side.removeFromTop (44);       // RECENT's label

    // As many recent projects as leave room for the demo under them
    const int roomForRecent = std::max (1, (side.getHeight() - 18 - 54) / 50);

    for (int i = 0; i < std::min (recent.size(), roomForRecent); ++i)
        hits.push_back ({ Kind::recent, i, side.removeFromTop (50) });

    if (recent.isEmpty())
        side.removeFromTop (50);

    side.removeFromTop (18);
    hits.push_back ({ Kind::demo, -1, side.removeFromTop (54) });
}

StartScreen::Hit StartScreen::hitAt (juce::Point<int> p) const
{
    for (auto& h : hits)
        if (h.area.contains (p))
            return h;

    return {};
}

//==============================================================================
void StartScreen::paint (juce::Graphics& g)
{
    // The studio dimmed behind, the card over it
    g.fillAll (background.withAlpha (0.82f));

    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 40, { 0, 12 }).drawForRectangle (g, card);
    g.setColour (panel);
    g.fillRoundedRectangle (card.toFloat(), 18.0f);
    g.setColour (hairlineStrong);
    g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 18.0f, 1.0f);

    auto r = card.reduced (32, 28);
    auto header = r.removeFromTop (70);

    // The moon, the question, and how to skip it
    const auto mark = header.removeFromLeft (52).withSizeKeepingCentre (48, 48).withY (header.getY() - 2);
    g.setOpacity (1.0f);        // images take the current colour's alpha, the faint outline's just now
    g.drawImage (logo(), mark.toFloat());
    header.removeFromLeft (16);
    g.setColour (text);
    g.setFont (font (27.0f, true));
    g.drawText ("What do you want to do?", header.removeFromTop (34).withTrimmedRight (60), juce::Justification::centredLeft, true);
    g.setColour (textDim);
    g.setFont (font (16.5f));
    g.drawText ("Start from a template, open something you've made, or close this for a blank project.",
                header.withTrimmedRight (60).withHeight (22), juce::Justification::centredLeft, true);

    const bool closeHover = hovered.kind == Kind::close;

    if (closeHover)
    {
        g.setColour (raised);
        g.fillRoundedRectangle (closeArea.toFloat(), 10.0f);
    }

    strokeIcon (g, icons::close(), closeArea.toFloat().withSizeKeepingCentre (14.0f, 14.0f), closeHover ? text : textDim);

    // Section labels
    g.setFont (labelFont());
    g.setColour (textFaint);
    g.drawText ("START FROM A TEMPLATE", templatesArea.withHeight (20), juce::Justification::centredLeft, true);
    g.drawText ("OPEN", sideArea.withHeight (20), juce::Justification::centredLeft, true);

    const auto& list = templates::all();

    for (auto& h : hits)
    {
        const bool hover = hovered.kind == h.kind && hovered.index == h.index;

        switch (h.kind)
        {
            case Kind::templateCard:
                paintTemplate (g, list[(size_t) h.index], h.area, hover);
                break;

            case Kind::open:
                paintRow (g, h.area, "Open a project...", "A song you've saved (.nsproj)", folderIcon(), hover);
                break;

            case Kind::recent:
            {
                const auto& f = recent[h.index];
                textaudit::TheirWords theirs;   // a project's name and folder, however long
                paintRow (g, h.area, f.getFileNameWithoutExtension(), f.getParentDirectory().getFullPathName(), pageIcon(), hover);
                break;
            }

            case Kind::demo:
                paintRow (g, h.area, "Demo song", "Hear what NightShift can do: keys, a beat and an 808", icons::sparkle(), hover);
                break;

            case Kind::startupSwitch:
            {
                // A switch, lit when on
                auto sw = h.area.withWidth (38).withSizeKeepingCentre (38, 22).toFloat();
                g.setColour (showAtStartup ? accentCool.withAlpha (hover ? 0.95f : 0.8f) : (hover ? raised : titleBar));
                g.fillRoundedRectangle (sw, 11.0f);
                g.setColour (showAtStartup ? background : textDim);
                g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ showAtStartup ? sw.getRight() - 11.0f : sw.getX() + 11.0f, sw.getCentreY() }));
                g.setColour (hover ? text : textDim);
                g.setFont (font (16.0f));
                g.drawText ("Show this when NightShift opens", h.area.withTrimmedLeft (50), juce::Justification::centredLeft, true);
                break;
            }

            case Kind::close:
            case Kind::donate:          // in the banner (paintDonation)
            case Kind::noThanks:
            case Kind::none:
                break;
        }
    }

    paintDonation (g);

    // Recent's label, and what it says before there's anything in it
    for (auto& h : hits)
        if (h.kind == Kind::open)
        {
            auto label = h.area.translated (0, h.area.getHeight() + 18).withHeight (20);
            g.setFont (labelFont());
            g.setColour (textFaint);
            g.drawText ("RECENT", label, juce::Justification::centredLeft, true);

            if (recent.isEmpty())
            {
                g.setFont (font (16.0f));
                g.drawFittedText ("Projects you save or open show up here.", label.translated (0, 26).withHeight (40),
                                  juce::Justification::topLeft, 2);
            }
        }

    // How to get a blank project, bottom right
    auto footer = card.reduced (32, 28).removeFromBottom (34);
    g.setColour (textFaint);
    g.setFont (font (16.0f));
    g.drawText ("Esc or the cross: a blank project with one empty track", footer, juce::Justification::centredRight, true);
}

void StartScreen::paintDonation (juce::Graphics& g)
{
    if (donation == Donation::hidden || donationArea.isEmpty())
        return;

    // A warm strip: a heart, NightShift is free, why a donation helps; Donate and No thanks
    const auto strip = donationArea.toFloat();
    g.setColour (accent.withAlpha (0.07f));
    g.fillRoundedRectangle (strip, 12.0f);
    g.setColour (accent.withAlpha (0.32f));
    g.drawRoundedRectangle (strip.reduced (0.5f), 12.0f, 1.0f);

    auto inner = donationArea.reduced (18, 0);
    auto heart = icons::heart();
    heart.applyTransform (heart.getTransformToScaleToFit (inner.removeFromLeft (24).withSizeKeepingCentre (24, 22).toFloat(), true));
    g.setColour (accent);
    g.fillPath (heart);
    inner.removeFromLeft (16);

    const auto donateArea = getDonateBounds(), noArea = getNoThanksBounds();

    if (donation == Donation::asking)
        inner.setRight (noArea.getX() - 16);

    auto lines = inner.withSizeKeepingCentre (inner.getWidth(), 40);
    const bool thanked = donation == Donation::thanked;
    g.setColour (text);
    g.setFont (font (17.0f, true));
    g.drawText (thanked ? juce::String ("Thank you!") : juce::String ("NightShift is free"), lines.removeFromTop (21),
                juce::Justification::centredLeft, true);
    g.setColour (textDim);
    g.setFont (font (16.0f));
    g.drawText (thanked ? juce::String ("The donation page is open in your browser. NightShift stays free either way.") : donate::pitch,
                lines, juce::Justification::centredLeft, true);

    if (thanked)
        return;

    // Donate: the one lit button; No thanks: quiet until you're on it
    const bool donateHover = hovered.kind == Kind::donate, noHover = hovered.kind == Kind::noThanks;
    g.setColour (donateHover ? accent.brighter (0.12f) : accent);
    g.fillRoundedRectangle (donateArea.toFloat(), 10.0f);
    g.setColour (background);
    g.setFont (font (16.5f, true));
    g.drawText ("Donate", donateArea, juce::Justification::centred, true);

    if (noHover)
    {
        g.setColour (raised);
        g.fillRoundedRectangle (noArea.toFloat(), 10.0f);
    }

    g.setColour (noHover ? text : textDim);
    g.setFont (font (16.0f));
    g.drawText ("No thanks", noArea, juce::Justification::centred, true);
}

void StartScreen::paintTemplate (juce::Graphics& g, const templates::Template& t, juce::Rectangle<int> area, bool hover)
{
    auto r = area.toFloat();
    g.setColour (hover ? raised : panelLight);
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (hover ? accentCool : hairlineStrong);
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, hover ? 1.5f : 1.0f);

    auto inner = area.reduced (16, 14);

    // Its tracks' icons, each on a tile of the colour its channel would have
    auto iconRow = inner.removeFromTop (34);

    for (int i = 0; i < t.icons.size(); ++i)
    {
        auto tile = iconRow.removeFromLeft (34).toFloat();
        iconRow.removeFromLeft (6);
        const auto colour = t.id == "empty" ? textDim : Song::channelColour (i * 3);
        g.setColour (colour.withAlpha (0.18f));
        g.fillRoundedRectangle (tile, 8.0f);
        trackicons::draw (g, t.icons[i], tile.reduced (7.0f), colour.interpolatedWith (juce::Colours::white, 0.45f));
    }

    inner.removeFromTop (12);
    g.setColour (text);
    g.setFont (font (18.0f, true));
    g.drawText (t.name, inner.removeFromTop (24), juce::Justification::centredLeft, true);

    const auto tracks = t.icons.size();
    auto foot = inner.removeFromBottom (18);
    g.setColour (textFaint);
    g.setFont (font (15.0f));
    g.drawText (juce::String (tracks) + (tracks == 1 ? " track" : " tracks") + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "))
                    + juce::String (juce::roundToInt (t.bpm)) + " bpm",
                foot, juce::Justification::centredLeft, true);

    inner.removeFromTop (4);
    g.setColour (textDim);
    g.setFont (font (15.0f));
    g.drawFittedText (t.description, inner, juce::Justification::topLeft, 4, 1.0f);
}

void StartScreen::paintRow (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, const juce::String& detail,
                            const juce::Path& icon, bool hover)
{
    if (hover)
    {
        g.setColour (raised);
        g.fillRoundedRectangle (area.toFloat(), 10.0f);
    }

    auto r = area.reduced (12, 0);
    auto tile = r.removeFromLeft (34).withSizeKeepingCentre (34, 34).toFloat();
    g.setColour (panelLight.brighter (0.15f));
    g.fillRoundedRectangle (tile, 8.0f);
    strokeIcon (g, icon, tile.reduced (9.0f), hover ? text : textDim);

    r.removeFromLeft (12);
    auto lines = r.withSizeKeepingCentre (r.getWidth(), 38);
    g.setColour (text);
    g.setFont (font (17.0f, true));
    g.drawText (title, lines.removeFromTop (20), juce::Justification::centredLeft, true);
    g.setColour (textFaint);
    g.setFont (font (15.0f));
    g.drawText (endThatFits (detail, font (15.0f), (float) lines.getWidth()), lines, juce::Justification::centredLeft, true);
}

//==============================================================================
void StartScreen::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitAt (e.getPosition());

    if (h.kind != hovered.kind || h.index != hovered.index)
    {
        hovered = h;
        repaint();
    }

    setMouseCursor (h.kind != Kind::none ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void StartScreen::mouseExit (const juce::MouseEvent&)
{
    hovered = {};
    repaint();
}

void StartScreen::mouseDown (const juce::MouseEvent& e)
{
    const auto h = hitAt (e.getPosition());
    const auto& list = templates::all();

    switch (h.kind)
    {
        case Kind::templateCard:
            if (onTemplate != nullptr && juce::isPositiveAndBelow (h.index, (int) list.size()))
                onTemplate (list[(size_t) h.index].id);
            break;

        case Kind::open:
            if (onOpen != nullptr)
                onOpen();
            break;

        case Kind::recent:
            if (onOpenRecent != nullptr && juce::isPositiveAndBelow (h.index, recent.size()))
                onOpenRecent (recent[h.index]);
            break;

        case Kind::demo:
            if (onDemo != nullptr)
                onDemo();
            break;

        case Kind::close:
            if (onClose != nullptr)
                onClose();
            break;

        case Kind::startupSwitch:
            showAtStartup = ! showAtStartup;
            repaint();

            if (onShowAtStartupChanged != nullptr)
                onShowAtStartupChanged (showAtStartup);
            break;

        case Kind::donate:
            // The page opens; the banner stays to say thank you, without its buttons
            donation = Donation::thanked;
            hovered = {};
            resized();
            repaint();

            if (onDonate != nullptr)
                onDonate();
            break;

        case Kind::noThanks:
            donation = Donation::hidden;
            hovered = {};
            resized();
            repaint();

            if (onNoThanks != nullptr)
                onNoThanks();
            break;

        case Kind::none:
            // A click off the card is a click on nothing, not a way out: closing is Esc or the cross
            break;
    }
}

bool StartScreen::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && onClose != nullptr)
        onClose();

    return true;    // nothing reaches the studio behind it
}

} // namespace nightshift
