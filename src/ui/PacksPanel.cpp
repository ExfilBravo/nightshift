// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "PacksPanel.h"
#include "Theme.h"

namespace nightshift
{

using namespace theme;

namespace
{
    constexpr int pad = 18, itemPad = 14, tickSize = 22, gap = 10;

    juce::TextLayout layoutOf (const juce::String& words, const juce::Font& f, juce::Colour c, float width)
    {
        juce::AttributedString s;
        s.setWordWrap (juce::AttributedString::byWord);
        s.append (words, f, c);
        juce::TextLayout layout;
        layout.createLayout (s, width);
        return layout;
    }

    // A pack's colour: the samples' pink
    const juce::Colour packColour { 0xffef8fc8 };
}

//==============================================================================
/** One pack: its tick (or "Here" once it is), name and size, what's in it and its licence. Clicking it ticks it. */
class PacksPanel::Item : public juce::Component
{
public:
    Item (PacksPanel& p, int i) : panel (p), index (i)
    {
        const auto& pack = packs::available()[(size_t) i];
        ticked = pack.library.isEmpty() && ! packs::isInstalled (pack);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    const packs::Pack& pack() const     { return packs::available()[(size_t) index]; }

    int heightFor (int w) const
    {
        const float textWidth = (float) (w - 2 * itemPad - tickSize - 14);
        return itemPad + 26 + (int) std::ceil (layoutOf (pack().about, font (15.0f), textDim, textWidth).getHeight()) + 6
               + (int) std::ceil (layoutOf (pack().licence, font (15.0f), textDim, textWidth).getHeight()) + itemPad;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pack();
        const bool here = packs::isInstalled (p);
        auto r = getLocalBounds().toFloat();
        g.setColour (isMouseOver() && ! here ? titleBar : panelLight);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (ticked ? packColour.withAlpha (0.5f) : hairline);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        auto area = getLocalBounds().reduced (itemPad);
        auto tickArea = area.removeFromLeft (tickSize).removeFromTop (tickSize).withY (area.getY() + 2).toFloat();
        area.removeFromLeft (14);

        // The tick, or that it's here already
        if (here)
        {
            g.setColour (ledOn);
            g.fillEllipse (tickArea.reduced (5.0f));
        }
        else
        {
            g.setColour (ticked ? packColour : panel.isBusy() ? hairline : hairlineStrong);

            if (ticked)
            {
                g.fillRoundedRectangle (tickArea, 5.0f);
                juce::Path mark;
                mark.startNewSubPath (tickArea.getX() + 5.5f, tickArea.getCentreY());
                mark.lineTo (tickArea.getX() + 9.5f, tickArea.getBottom() - 6.0f);
                mark.lineTo (tickArea.getRight() - 5.0f, tickArea.getY() + 6.0f);
                g.setColour (background);
                g.strokePath (mark, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else
            {
                g.drawRoundedRectangle (tickArea.reduced (0.5f), 5.0f, 1.5f);
            }
        }

        // Its name, and its size or what it is now
        auto top = area.removeFromTop (26);
        const auto right = here ? juce::String ("Here") : started ? juce::String ("On its way") : packs::sizeText (p.bytes);
        g.setFont (font (15.0f, true));
        g.setColour (here ? ledOn : textDim);
        const int rightWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), right)) + 4;
        g.drawText (right, top.removeFromRight (rightWidth), juce::Justification::centredRight, true);
        g.setColour (text);
        g.setFont (font (17.0f, true));
        g.drawText (p.name, top, juce::Justification::centredLeft, true);

        const float textWidth = (float) area.getWidth();
        auto about = layoutOf (p.about, font (15.0f), textDim, textWidth);
        about.draw (g, area.removeFromTop ((int) std::ceil (about.getHeight())).toFloat());
        area.removeFromTop (6);
        auto licence = layoutOf (p.licence, font (15.0f), textFaint, textWidth);
        licence.draw (g, area.removeFromTop ((int) std::ceil (licence.getHeight())).toFloat());
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && ! panel.isBusy() && ! packs::isInstalled (pack()) && ! started)
        {
            ticked = ! ticked;
            panel.refresh();
        }
    }

    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }

    PacksPanel& panel;
    const int index;
    bool ticked = false, started = false;
};

//==============================================================================
/** The ticked zips, one after another, on a thread of its own. */
struct PacksPanel::Worker : public juce::Thread
{
    Worker (PacksPanel& p, std::vector<packs::Pack> list) : juce::Thread ("Sample packs"), panel (&p), todo (std::move (list))
    {
        startThread();
    }

    ~Worker() override
    {
        stopThread (15000);
    }

    void run() override
    {
        juce::String problem;

        for (size_t i = 0; i < todo.size() && ! threadShouldExit() && problem.isEmpty(); ++i)
        {
            const auto& pack = todo[i];
            setStep ("Downloading " + pack.name);
            problem = packs::install (pack, [this, i] (double f)
            {
                progress = ((double) i + juce::jlimit (0.0, 1.0, f) * 0.95) / (double) todo.size();

                if (f >= 1.0)
                    setStep ("Checking and unpacking " + todo[i].name);
            }, [this] { return threadShouldExit(); });

            if (problem.isEmpty() && ! threadShouldExit())
                juce::MessageManager::callAsync ([panel = panel] { if (panel != nullptr && panel->onInstalled != nullptr) panel->onInstalled(); });
        }

        const bool stopped = threadShouldExit();
        juce::MessageManager::callAsync ([panel = panel, problem, stopped] { if (panel != nullptr) panel->finished (problem, stopped); });
    }

    void setStep (const juce::String& s)
    {
        const juce::ScopedLock sl (lock);
        step = s;
    }

    juce::String getStep() const
    {
        const juce::ScopedLock sl (lock);
        return step;
    }

    juce::Component::SafePointer<PacksPanel> panel;
    std::vector<packs::Pack> todo;
    std::atomic<double> progress { 0.0 };
    juce::CriticalSection lock;
    juce::String step;
};

//==============================================================================
PacksPanel::PacksPanel()
{
    for (int i = 0; i < (int) packs::available().size(); ++i)
        addAndMakeVisible (items.add (new Item (*this, i)));

    download.setFilled (true);
    download.setLabel ("Download");
    download.setTooltip ("Download the ticked packs; NightShift lists them in Samples");
    download.onClick = [this] { startDownload(); };
    addAndMakeVisible (download);
    stop.setFramed (true);
    stop.setFilled (false);
    stop.setLabel ("Cancel");
    stop.setTooltip ("Stop downloading (what's come so far is kept, to carry on from next time)");
    stop.onClick = [this] { cancel(); };
    addChildComponent (stop);

    // Its height from what it holds
    int height = pad + 34 + 44 + gap;

    for (auto* item : items)
        height += item->heightFor (width - 2 * pad) + gap;

    setSize (width, height + 76);
    refresh();
}

PacksPanel::~PacksPanel()
{
    stopTimer();
    worker.reset();
}

void PacksPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    auto top = getLocalBounds().reduced (pad).removeFromTop (34);
    g.setColour (text);
    g.setFont (font (21.0f, true));
    g.drawText ("Free sample packs", top, juce::Justification::centredLeft, true);

    auto intro = layoutOf ("Sounds to use in your songs, free. Tick the ones you want: NightShift downloads them and lists them in Samples.",
                           font (16.0f), textDim, (float) introArea.getWidth());
    intro.draw (g, introArea.toFloat());

    // What it's doing, or has to say, and how far it's got
    auto foot = footerArea;
    auto words = foot.withTrimmedRight (download.getWidth() + stop.getWidth() + 20);
    g.setColour (statusIsProblem ? danger : isBusy() ? text : textFaint);
    g.setFont (font (15.0f));
    g.drawFittedText (status, words.removeFromTop (isBusy() ? 22 : words.getHeight()), juce::Justification::centredLeft, 2, 1.0f);

    if (isBusy())
    {
        auto bar = words.removeFromTop (14).withTrimmedTop (4).toFloat();
        g.setColour (panelLight);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (packColour);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, worker->progress.load())), 3.0f);
    }
}

void PacksPanel::resized()
{
    auto area = getLocalBounds().reduced (pad);
    area.removeFromTop (34);
    introArea = area.removeFromTop (44);
    area.removeFromTop (gap);

    for (auto* item : items)
    {
        item->setBounds (area.removeFromTop (item->heightFor (area.getWidth())));
        area.removeFromTop (gap);
    }

    footerArea = area.removeFromBottom (44);
    auto buttons = footerArea;
    download.setBounds (buttons.removeFromRight (download.getIdealWidth (40)).withSizeKeepingCentre (download.getIdealWidth (40), 40));
    buttons.removeFromRight (10);
    stop.setBounds (buttons.removeFromRight (stop.getIdealWidth (40)).withSizeKeepingCentre (stop.getIdealWidth (40), 40));
}

void PacksPanel::setTicked (int index, bool shouldTick)
{
    if (juce::isPositiveAndBelow (index, items.size()))
    {
        items[index]->ticked = shouldTick;
        refresh();
    }
}

bool PacksPanel::isTicked (int index) const
{
    return juce::isPositiveAndBelow (index, items.size()) && items[index]->ticked;
}

juce::String PacksPanel::getStatus() const
{
    return status;
}

void PacksPanel::startDownload()
{
    if (isBusy())
        return;

    std::vector<packs::Pack> zips;
    juce::StringArray libraries;

    for (auto* item : items)
    {
        if (! item->ticked || packs::isInstalled (item->pack()))
            continue;

        if (item->pack().library.isEmpty())
        {
            zips.push_back (item->pack());
        }
        else
        {
            // The studio's own download, with its own progress at the foot of the window
            item->started = true;
            item->ticked = false;
            libraries.add (item->pack().name);

            if (onGetLibrary != nullptr)
                onGetLibrary (item->pack().library);
        }
    }

    statusIsProblem = false;

    if (! zips.empty())
    {
        worker = std::make_unique<Worker> (*this, zips);
        startTimerHz (10);
    }
    else if (! libraries.isEmpty())
    {
        status = libraries.joinIntoString (" and ") + (libraries.size() == 1 ? " is" : " are") + " on its way: its progress shows at the foot of NightShift's window.";
    }

    refresh();
}

void PacksPanel::cancel()
{
    if (worker != nullptr)
        worker->signalThreadShouldExit();
}

void PacksPanel::finished (const juce::String& problem, bool stopped)
{
    stopTimer();
    worker.reset();
    statusIsProblem = problem.isNotEmpty() && ! stopped;
    status = stopped ? juce::String ("Stopped. What came so far is kept: Download carries on from there.")
           : problem.isNotEmpty() ? problem
                                  : juce::String ("Done: Samples lists them under Free packs.");
    refresh();
}

void PacksPanel::timerCallback()
{
    if (worker != nullptr)
    {
        status = worker->getStep() + "...  " + juce::String (juce::roundToInt (worker->progress.load() * 100.0)) + "%";
        repaint();
    }
}

void PacksPanel::refresh()
{
    bool anyTicked = false;

    for (auto* item : items)
        anyTicked = anyTicked || (item->ticked && ! packs::isInstalled (item->pack()));

    download.setEnabled (anyTicked && ! isBusy());
    download.setVisible (! isBusy());
    stop.setVisible (isBusy());

    if (status.isEmpty() && ! isBusy())
        status = "Nothing downloads until you click Download.";

    for (auto* item : items)
        item->repaint();

    resized();
    repaint();
}

} // namespace nightshift
