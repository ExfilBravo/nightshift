// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "Templates.h"

namespace nightshift
{

/** "What do you want to do?": what NightShift opens on (and New shows), over the studio. Part
    Logic's project chooser, part FL's templates: a card per template on the left; on the right,
    Open a project, the recent ones and the demo song. Close it (the cross, or Esc) for the blank
    project already behind it: one empty track. A switch at the bottom says whether it shows
    when NightShift opens. The first time it shows, a banner across its foot says NightShift is
    free and asks for a donation (Donate, No thanks); it never asks by itself again. */
class StartScreen : public juce::Component
{
public:
    StartScreen();

    std::function<void (const juce::String& templateId)> onTemplate;
    std::function<void()> onOpen, onDemo, onClose;
    std::function<void (const juce::File&)> onOpenRecent;
    std::function<void (bool)> onShowAtStartupChanged;

    /** The donation banner: Donate opens the page (and the banner says thank you); No thanks
        takes it away. */
    std::function<void()> onDonate, onNoThanks;
    void setDonationShown (bool);
    bool isDonationShown() const                { return donation != Donation::hidden; }
    bool isDonationThanked() const              { return donation == Donation::thanked; }
    juce::Rectangle<int> getDonateBounds() const;
    juce::Rectangle<int> getNoThanksBounds() const;

    /** The recent projects to list (only those still there are shown), newest first. */
    void setRecent (const juce::StringArray& paths);
    void setShowAtStartup (bool);
    bool getShowAtStartup() const               { return showAtStartup; }

    /** Where things are, for tests: a template's card, a recent project's row (by index). */
    juce::Rectangle<int> getTemplateBounds (const juce::String& id) const;
    juce::Rectangle<int> getRecentBounds (int index) const;
    juce::Rectangle<int> getCloseBounds() const  { return closeArea; }
    juce::Rectangle<int> getStartupSwitchBounds() const { return switchArea; }
    juce::Rectangle<int> getCard() const         { return card; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override         { return true; }     // no notes from the PC keyboard behind it

private:
    enum class Kind { none, templateCard, open, recent, demo, close, startupSwitch, donate, noThanks };
    enum class Donation { hidden, asking, thanked };

    struct Hit
    {
        Kind kind = Kind::none;
        int index = -1;
        juce::Rectangle<int> area;
    };

    juce::Array<juce::File> recent;
    bool showAtStartup = true;
    Donation donation = Donation::hidden;
    juce::Rectangle<int> card, closeArea, switchArea, templatesArea, sideArea, donationArea;
    std::vector<Hit> hits;
    Hit hovered;

    Hit hitAt (juce::Point<int>) const;
    void paintTemplate (juce::Graphics&, const templates::Template&, juce::Rectangle<int>, bool hover);
    void paintRow (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, const juce::String& detail,
                   const juce::Path& icon, bool hover);
    void paintDonation (juce::Graphics&);
    juce::Rectangle<int> boundsOf (Kind) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StartScreen)
};

} // namespace nightshift
