// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "audio/Packs.h"
#include "Widgets.h"

namespace nightshift
{

/** The free sample packs (Samples' card, its Free sample packs... row, or its right-click menu): each pack with
    what's in it, its size and its licence or credit, ticked to get it (NightShift Sounds 1 ticked to begin with, the
    big libraries not). Download fetches the ticked zips in the background (progress, Cancel; checked and unpacked
    into NightShift's own folder, where Samples lists them as Free packs) and starts the libraries NightShift fetches
    its own way (the studio drum kits, the piano and orchestra), whose progress shows at the foot of the studio.
    Nothing downloads until Download is clicked. */
class PacksPanel : public juce::Component,
                   private juce::Timer
{
public:
    PacksPanel();
    ~PacksPanel() override;

    /** A library to start getting its own way ("kits", "instruments"). */
    std::function<void (const juce::String& library)> onGetLibrary;

    /** A pack has arrived: Samples lists it. */
    std::function<void()> onInstalled;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** For tests: tick a pack, as clicking it; Download and Cancel as clicked; what it's doing. */
    void setTicked (int index, bool);
    bool isTicked (int index) const;
    void startDownload();
    void cancel();
    bool isBusy() const                     { return worker != nullptr; }
    juce::String getStatus() const;

    static constexpr int width = 660;

private:
    class Item;
    struct Worker;

    juce::OwnedArray<Item> items;
    IconButton download { "Download", theme::icons::exportLine() }, stop { "Cancel", theme::icons::close() };
    std::unique_ptr<Worker> worker;
    juce::String status;
    bool statusIsProblem = false;
    double shownProgress = -1.0;
    juce::Rectangle<int> introArea, footerArea;

    void timerCallback() override;
    void refresh();
    void finished (const juce::String& problem, bool stopped);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PacksPanel)
};

} // namespace nightshift
