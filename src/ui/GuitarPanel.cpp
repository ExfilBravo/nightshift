// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "GuitarPanel.h"
#include "FreezeGuard.h"
#include "ai/OfflineGuitar.h"
#include "ai/OfflineBass.h"
#include "ai/OfflineDrummer.h"
#include "audio/GuitarSound.h"

namespace nightshift
{

using namespace theme;

namespace
{
    const char* const ideas[] = { "Metalcore chugs in drop C, locked to the kick", "Pop punk power chords", "Funk chicken scratch in E",
                                  "Jazz comping, the Charleston", "Neo soul chords with hammer-ons", "Trap guitar loop, arpeggios" };

    constexpr int sideWidth = 320;

    void drawWrapped (juce::Graphics& g, const juce::String& s, juce::Font f, juce::Colour c, juce::Rectangle<int> area)
    {
        juce::AttributedString words;
        words.append (s, f, c);
        words.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (words, (float) area.getWidth());
        layout.draw (g, area.toFloat());
    }

    // Wrapped, as many whole lines as fit (no line cut through its middle); the rest is left off
    void drawWholeLines (juce::Graphics& g, const juce::String& s, juce::Font f, juce::Colour c, juce::Rectangle<int> area)
    {
        juce::AttributedString words;
        words.append (s, f, c);
        words.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (words, (float) area.getWidth());
        float shown = 0.0f;

        for (int i = 0; i < layout.getNumLines(); ++i)
            if (const float bottom = layout.getLine (i).getLineBounds().getBottom(); bottom <= (float) area.getHeight() + 0.5f)
                shown = bottom;

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (area.withHeight ((int) std::ceil (shown)));
        layout.draw (g, area.toFloat().withHeight (std::max ((float) area.getHeight(), layout.getHeight())));
    }

    std::optional<drummer::Groove> drumsIn (const Song& song)
    {
        const int id = bass::drummerToFollow (song);

        for (auto& c : song.channels)
            if (c.id == id && juce::isPositiveAndBelow (c.drummer.shown, (int) c.drummer.turns.size()))
                return drummer::Groove::fromJson (juce::JSON::parse (c.drummer.turns[(size_t) c.drummer.shown].groove));

        return {};
    }

    // The bassist's part it plays over: the selected channel's if it has one, else the first
    std::pair<int, std::optional<bass::Part>> bassIn (const Song& song)
    {
        const Channel* found = nullptr;

        for (auto& c : song.channels)
            if (! c.bass.turns.empty() && c.bass.shown >= 0 && (found == nullptr || c.id == song.selectedChannelId))
                found = &c;

        if (found == nullptr)
            return { 0, std::nullopt };

        return { found->id, bass::shownPart (*found) };
    }
}

//==============================================================================
/** A section of the part as tab: the strings (lowest at the bottom, named by their tuning), a
    fret where each note is played, bars wrapped across the width with the chord over each, P.M.
    over palm-muted runs, x for scratches, h before a hammer-on and / before a slide. */
class GuitarPanel::TabView : public juce::Component
{
public:
    void setPart (std::optional<guitar::Part> p, const juce::String& sectionName)
    {
        part = std::move (p);
        section = nullptr;

        if (part.has_value())
            for (auto& s : part->sections)
                if (s.name == sectionName)
                    section = &s;

        layout (getWidth());
    }

    void layout (int width)
    {
        const int strings = part.has_value() ? (int) part->tuning.size() : 6;
        rowHeight = top + strings * lineGap + 26;
        barsPerRow = 1;

        if (section != nullptr && ! section->bars.empty())
        {
            const int steps = section->bars.front().steps;
            barWidth = juce::jlimit (240, 520, steps * cellFor (steps) + 16);
            barsPerRow = std::max (1, (width - left - 8) / barWidth);
            barWidth = (width - left - 8) / barsPerRow;
        }

        const int bars = section != nullptr ? (int) section->bars.size() : 0;
        const int rows = std::max (1, (bars + barsPerRow - 1) / barsPerRow);
        setSize (std::max (width, 300), rows * rowHeight + 8);
        repaint();
    }

    int barsShown() const                   { return section != nullptr ? (int) section->bars.size() : 0; }

    void paint (juce::Graphics& g) override
    {
        if (! part.has_value() || section == nullptr)
            return;

        const auto& tuning = part->tuning;
        const int strings = (int) tuning.size();

        for (int b = 0; b < (int) section->bars.size(); ++b)
        {
            const auto& bar = section->bars[(size_t) b];
            const int row = b / barsPerRow, col = b % barsPerRow;
            const int x0 = left + col * barWidth, y0 = row * rowHeight + top;
            const float cell = (float) (barWidth - 16) / (float) bar.steps;
            auto yOf = [&] (int string) { return (float) y0 + (float) (strings - 1 - string) * lineGap; };
            auto xOf = [&] (int step) { return (float) x0 + 10.0f + ((float) step + 0.5f) * cell; };

            // The strings' names at the start of each row
            if (col == 0)
            {
                g.setFont (font (16.0f, true));
                g.setColour (textDim);

                for (int s = 0; s < strings; ++s)
                    g.drawText (guitar::tuningNotes ({ tuning[(size_t) s] }), 0, (int) yOf (s) - 9, left - 8, 18, juce::Justification::centredRight, true);
            }

            // The strings, the bar lines, the chord over the bar
            g.setColour (hairline.brighter (0.4f));

            for (int s = 0; s < strings; ++s)
                g.fillRect ((float) x0, yOf (s), (float) barWidth, 1.0f);

            g.fillRect ((float) x0, yOf (strings - 1), 1.0f, yOf (0) - yOf (strings - 1) + 1.0f);
            g.fillRect ((float) (x0 + barWidth), yOf (strings - 1), 1.0f, yOf (0) - yOf (strings - 1) + 1.0f);

            g.setColour (accent);
            g.setFont (font (16.0f, true));
            g.drawText (bar.chord, x0 + 6, y0 - top + 2, barWidth - 10, 20, juce::Justification::centredLeft, true);

            // P.M. over each palm-muted run
            int runStart = -1, runEnd = -1;

            auto flushRun = [&]
            {
                if (runStart < 0)
                    return;

                const float ax = xOf (runStart) - cell * 0.4f, bx = xOf (runEnd) + cell * 0.4f;
                const float y = yOf (strings - 1) - 18.0f;
                g.setColour (textDim);
                g.setFont (font (16.0f));
                g.drawText ("P.M.", (int) ax, (int) y - 8, 40, 16, juce::Justification::centredLeft, true);

                for (float x = ax + 34.0f; x < bx; x += 7.0f)
                    g.fillRect (x, y, 4.0f, 1.0f);

                runStart = runEnd = -1;
            };

            for (int step = 0; step < bar.steps; ++step)
            {
                const bool muted = std::any_of (bar.notes.begin(), bar.notes.end(), [step] (const guitar::Note& n) { return n.step == step && n.palmMute; });
                const bool any = std::any_of (bar.notes.begin(), bar.notes.end(), [step] (const guitar::Note& n) { return n.step == step; });

                if (muted)
                {
                    if (runStart < 0)
                        runStart = step;

                    runEnd = step;
                }
                else if (any)
                {
                    flushRun();
                }
            }

            flushRun();

            // The frets
            for (auto& n : bar.notes)
            {
                if (n.string < 0 || n.string >= strings)
                    continue;

                const auto label = (n.hammer ? juce::String ("h") : n.slide ? juce::String ("/") : juce::String())
                                   + (n.muted ? juce::String ("x") : juce::String (n.fret));
                const float x = xOf (n.step), y = yOf (n.string);
                const float w = (float) label.length() * 9.0f + 6.0f;
                g.setColour (panel);
                g.fillRect (x - w / 2.0f, y - 9.0f, w, 18.0f);
                g.setColour (n.muted ? textDim : n.palmMute ? text.withAlpha (0.85f) : text);
                g.setFont (font (16.0f, ! n.muted));
                g.drawText (label, juce::Rectangle<float> (x - w / 2.0f, y - 9.0f, w, 18.0f), juce::Justification::centred, true);
            }
        }
    }

private:
    std::optional<guitar::Part> part;
    const guitar::Section* section = nullptr;
    static constexpr int left = 34, top = 30, lineGap = 18;
    int rowHeight = 160, barsPerRow = 1, barWidth = 400;

    static int cellFor (int steps)          { return steps <= 12 ? 24 : steps <= 16 ? 17 : steps <= 32 ? 9 : 6; }
};

//==============================================================================
GuitarPanel::GuitarPanel (Song& s, AudioEngine& a)
    : song (s), audio (a)
{
    tab = std::make_unique<TabView>();
    tabView.setViewedComponent (tab.get(), false);
    tabView.setScrollBarsShown (true, false);
    tabView.setScrollBarThickness (8);
    addAndMakeVisible (tabView);

    soundChip.setTooltip ("The guitar sound: pick another");
    soundChip.onClick = [this] { showSoundMenu(); };
    addAndMakeVisible (soundChip);

    for (auto* b : { &previousButton, &nextButton, &arrangeButton })
    {
        b->setFramed (true);
        addAndMakeVisible (*b);
    }

    previousButton.setTooltip ("The version before");
    nextButton.setTooltip ("The version after");
    previousButton.onClick = [this] { if (auto c = channel()) showVersion (c->guitar.shown - 1); };
    nextButton.onClick = [this] { if (auto c = channel()) showVersion (c->guitar.shown + 1); };

    arrangeButton.setLabel ("Put in Arrange");
    arrangeButton.setTooltip ("Lay the sections out in Arrange, under the drummer's and the bassist's");
    arrangeButton.onClick = [this] { putInArrange(); };

    for (auto idea : ideas)
    {
        auto* chip = ideaChips.add (new Chip (idea));
        chip->setWraps (true);
        chip->setOpensMenu (false);
        chip->setTooltip ("Put this in the prompt above, to send as it is or change first");
        chip->onClick = [this, idea = juce::String (idea)] { if (onUseIdea != nullptr) onUseIdea (idea); };
        addChildComponent (chip);
    }

    song.addListener (this);
    setSize (900, 360);
    refresh();
}

GuitarPanel::~GuitarPanel()
{
    song.removeListener (this);
}

Channel* GuitarPanel::channel() const
{
    return channelId != 0 ? song.findChannel (channelId) : nullptr;
}

juce::Component& GuitarPanel::getTab()
{
    return *tab;
}

int GuitarPanel::getTabBarsShown() const
{
    return tab->barsShown();
}

void GuitarPanel::setChannel (int id)
{
    if (id == channelId)
        return;

    channelId = id;
    status = {};
    shownSection = {};
    refresh();
    changed();
}

bool GuitarPanel::hasTurns() const
{
    auto c = channel();
    return c != nullptr && ! c->guitar.turns.empty();
}

void GuitarPanel::changed()
{
    if (onStateChanged != nullptr)
        onStateChanged();
}

void GuitarPanel::songChanged (Song::Change c)
{
    if (c != Song::Change::selection)
        refresh();
}

void GuitarPanel::showSection (const juce::String& name)
{
    shownSection = name;
    refresh();
}

void GuitarPanel::refresh()
{
    auto c = channel();
    const bool talking = hasTurns();
    const auto part = c != nullptr ? guitar::shownPart (*c) : std::nullopt;

    soundChip.setLabel (c != nullptr ? "Sound: " + (isGuitar (*c) ? guitarPresetOf (*c) : c->name) : juce::String());
    soundChip.setSwatch (c != nullptr ? c->colour : accent);
    soundChip.setEnabled (c != nullptr && isGuitar (*c));

    const int turns = c != nullptr ? (int) c->guitar.turns.size() : 0;
    const int shown = c != nullptr ? c->guitar.shown : -1;
    previousButton.setEnabled (shown > 0);
    nextButton.setEnabled (shown >= 0 && shown < turns - 1);
    arrangeButton.setEnabled (part.has_value());

    // The section in the tab: the one picked, else the first
    if (part.has_value() && ! part->sections.empty()
          && std::none_of (part->sections.begin(), part->sections.end(), [this] (const guitar::Section& s) { return s.name == shownSection; }))
        shownSection = part->sections.front().name;

    sectionChips.clear();

    if (part.has_value() && c != nullptr)
        for (auto& section : part->sections)
        {
            auto it = c->guitar.patterns.find (section.name);
            auto pattern = it != c->guitar.patterns.end() ? song.findPattern (it->second) : nullptr;

            if (pattern == nullptr)
                continue;

            auto* chip = sectionChips.add (new Chip (section.name));
            chip->setOpensMenu (false);
            chip->setSwatch (section.name == shownSection ? accent : pattern->colour);
            chip->setTooltip ("Its tab; right-click to open " + pattern->name + " in the piano roll");
            chip->onClick = [this, name = section.name, id = pattern->id]
            {
                if (juce::ModifierKeys::currentModifiers.isPopupMenu())
                {
                    if (onShowPattern != nullptr)
                        onShowPattern (channelId, id);

                    return;
                }

                showSection (name);
            };
            addChildComponent (chip);
        }

    for (auto* comp : std::initializer_list<juce::Component*> { &soundChip, &previousButton, &nextButton, &arrangeButton, &tabView })
        comp->setVisible (talking);

    for (auto* chip : sectionChips)
        chip->setVisible (talking);

    for (auto* chip : ideaChips)
        chip->setVisible (! talking);

    tab->setPart (part, shownSection);
    resized();
    repaint();
}

//==============================================================================
void GuitarPanel::ask (const juce::String& words)
{
    if (words.trim().isEmpty())
        return;

    // A frozen channel's notes can't change: it asks to unfreeze it first
    if (auto frozen = channel(); frozen != nullptr && ! freezing::mayEdit (frozen->id))
        return;

    if (channel() == nullptr && onNeedChannel != nullptr)
        channelId = onNeedChannel();

    auto c = channel();

    if (c == nullptr)
    {
        status = "There's no channel to write the guitar for.";
        refresh();
        changed();
        return;
    }

    // It plays with the band: the drummer's part and the bassist's chords, and builds on its own
    // last request (or the bassist's, or the drummer's, for a first part)
    const auto drums = drumsIn (song);
    auto [bassId, bassLine] = bassIn (song);
    std::optional<agents::Request> previous;

    if (juce::isPositiveAndBelow (c->guitar.shown, (int) c->guitar.turns.size()))
        previous = guitar::offline::requestOf (c->guitar.turns[(size_t) c->guitar.shown]);

    if (! previous.has_value() && bassId != 0)
        if (auto b = song.findChannel (bassId); b != nullptr && juce::isPositiveAndBelow (b->bass.shown, (int) b->bass.turns.size()))
            previous = bass::offline::requestOf (b->bass.turns[(size_t) b->bass.shown]);

    if (! previous.has_value())
        if (auto d = song.findChannel (bass::drummerToFollow (song)); d != nullptr && juce::isPositiveAndBelow (d->drummer.shown, (int) d->drummer.turns.size()))
            previous = drummer::offline::requestOf (d->drummer.turns[(size_t) d->drummer.shown]);

    const bool blank = std::none_of (song.channels.begin(), song.channels.end(), [this, &c] (const Channel& other)
    {
        if (other.id == c->id)
            return false;

        for (auto& p : song.patterns)
            if (auto notes = p.findNotes (other.id); notes != nullptr && ! notes->empty())
                return true;

        return false;
    }) && song.audioClips.empty();

    const bool chooseSound = c->guitar.pickSound && c->guitar.turns.empty() && isGuitar (*c);
    const auto result = guitar::offline::write (words, song, previous.has_value() ? &*previous : nullptr, drums.has_value() ? &*drums : nullptr,
                                                bassLine.has_value() ? &*bassLine : nullptr, chooseSound, blank, juce::Time::currentTimeMillis() + ++asks);

    song.checkpoint();
    c->guitar.turns.push_back (guitar::offline::turnFor (words, result));
    c->guitar.shown = (int) c->guitar.turns.size() - 1;
    c->guitar.pickSound = false;

    // The sound it chose for the style, and its fader where that style's mix wants it
    if (chooseSound && result.part.sound.isNotEmpty())
    {
        audio.setGuitarPreset (c->id, result.part.sound);
        c->volume = juce::jlimit (0.0f, 1.0f, 0.78f * std::pow (10.0f, result.part.levelDb / 40.0f));
        song.notify (Song::Change::mixer);
    }

    guitar::rerender (song, c->id);

    if (! result.part.sections.empty())
    {
        shownSection = result.part.sections.front().name;

        if (auto it = c->guitar.patterns.find (shownSection); it != c->guitar.patterns.end())
        {
            song.currentPatternId = it->second;
            song.notify (Song::Change::currentPattern);
        }
    }

    status = {};
    refresh();

    if (onAnswered != nullptr)
        onAnswered();

    changed();
}

void GuitarPanel::showVersion (int index)
{
    auto c = channel();

    if (c == nullptr || ! juce::isPositiveAndBelow (index, (int) c->guitar.turns.size()) || index == c->guitar.shown)
        return;

    song.checkpoint();
    c->guitar.shown = index;
    guitar::rerender (song, channelId);
    refresh();
}

void GuitarPanel::showSoundMenu()
{
    auto c = channel();

    if (c == nullptr || ! isGuitar (*c))
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Guitar sounds");

    for (auto& name : guitarsound::presetNames())
        menu.addItem (name, true, guitarPresetOf (*c) == name, [this, name]
        {
            if (auto ch = channel())
            {
                song.checkpoint();
                audio.setGuitarPreset (ch->id, name);
                refresh();
            }
        });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&soundChip));
}

void GuitarPanel::putInArrange()
{
    auto c = channel();
    auto part = c != nullptr ? guitar::shownPart (*c) : std::nullopt;

    if (! part.has_value())
        return;

    song.checkpoint();
    guitar::placeInArrange (song, channelId, *part);
}

//==============================================================================
void GuitarPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 10);

    if (! hasTurns())
    {
        introArea = r.withSizeKeepingCentre (std::min (860, r.getWidth()), std::min (290, r.getHeight()));
        auto ideasArea = introArea.withTrimmedTop (84);
        statusArea = introArea.withY (introArea.getBottom() + 2).withHeight (22);
        const int columns = ideasArea.getWidth() >= 760 ? 3 : 2, rowHeight = 60;
        const int w = (ideasArea.getWidth() - (columns - 1) * 8) / columns;

        for (int i = 0; i < ideaChips.size(); ++i)
        {
            const int row = i / columns, col = i % columns;
            auto b = juce::Rectangle<int> (ideasArea.getX() + col * (w + 8), ideasArea.getY() + row * rowHeight, w, 52);
            ideaChips[i]->setVisible (b.getBottom() <= ideasArea.getBottom());
            ideaChips[i]->setBounds (b);
        }

        return;
    }

    statusArea = r.removeFromBottom (status.isNotEmpty() ? 24 : 0);

    sideArea = r.removeFromRight (std::min (sideWidth, r.getWidth() * 2 / 5));
    r.removeFromRight (12);
    {
        auto s = sideArea.reduced (0, 4);
        soundChip.setBounds (s.removeFromTop (32));
        s.removeFromTop (10);
        versionArea = s.removeFromTop (32);
        {
            auto v = versionArea;
            previousButton.setBounds (v.removeFromLeft (34));
            v.removeFromLeft (4);
            nextButton.setBounds (v.removeFromLeft (34));
        }

        s.removeFromTop (54);       // the key and the tuning, under the version
        int x = s.getX(), y = s.getY();

        for (auto* chip : sectionChips)
        {
            const int w = std::min (s.getWidth(), chip->getIdealWidth());

            if (x + w > s.getRight())
            {
                x = s.getX();
                y += 38;
            }

            chip->setBounds (x, y, w, 30);
            chip->setVisible (y + 30 <= s.getBottom() - 40);
            x += w + 8;
        }

        const int below = sectionChips.isEmpty() ? s.getY() : y + 38;
        arrangeButton.setBounds (s.getX(), std::min (below + 4, s.getBottom() - 32), s.getWidth(), 32);
    }

    // What it said across the top, the tab under it
    replyArea = r.removeFromTop (std::min (78, r.getHeight() / 3));
    r.removeFromTop (6);
    tabView.setBounds (r);
    tab->layout (std::max (300, r.getWidth() - tabView.getScrollBarThickness()));
}

void GuitarPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    auto c = channel();

    if (! hasTurns())
    {
        auto t = introArea;
        g.setColour (text);
        g.setFont (font (21.0f, true));
        g.drawText ("Describe a guitar part in the bar above and press Enter", t.removeFromTop (30), juce::Justification::centredLeft, true);

        const auto [bassId, bassLine] = bassIn (song);
        const juce::String with = bassLine.has_value() ? "It plays the bassist's chords, locked to the drummer, and shows it as tab to learn. "
                                                       : juce::String ("No bass yet, so it plays the chords its style would. It shows the part as tab to learn. ");
        const juce::String who = c != nullptr ? "It writes for " + c->name + ". " : juce::String ("It adds a guitar channel, choosing the sound for the style. ");
        drawWrapped (g, who + with + "Or start from one of these:", font (16.0f), textDim, t.removeFromTop (48).withTrimmedTop (4));
    }
    else
    {
        g.setColour (hairline);
        g.fillRect (sideArea.getX() - 6, sideArea.getY(), 1, sideArea.getHeight());

        const auto part = c != nullptr ? guitar::shownPart (*c) : std::nullopt;
        const int turns = c != nullptr ? (int) c->guitar.turns.size() : 0;
        g.setColour (part.has_value() ? text : textFaint);
        g.setFont (font (16.0f, true));
        g.drawText (part.has_value() ? "Version " + juce::String (c->guitar.shown + 1) + " of " + juce::String (turns) : juce::String ("No part yet"),
                    versionArea.withTrimmedLeft (84), juce::Justification::centredLeft, true);

        if (part.has_value())
        {
            g.setColour (textDim);
            g.setFont (font (16.0f));
            g.drawText ("In " + part->key, versionArea.translated (0, 34).withHeight (22), juce::Justification::centredLeft, true);
            g.drawText (guitar::tuningName (part->tuning) + ":  " + guitar::tuningNotes (part->tuning), versionArea.translated (0, 56).withHeight (22),
                        juce::Justification::centredLeft, true);

            // What it played, in its words; the section in the tab named over it
            const auto& turn = c->guitar.turns[(size_t) c->guitar.shown];
            // At its full size, wrapped, in whole lines; what doesn't fit is left off (the conversation has it all)
            drawWholeLines (g, turn.reply, font (16.0f), text, replyArea.withTrimmedBottom (20));
            g.setColour (accent);
            g.setFont (font (16.0f, true));
            g.drawText (shownSection + " (" + juce::String (tab->barsShown()) + " bars)", replyArea.withTop (replyArea.getBottom() - 18),
                        juce::Justification::centredLeft, true);
        }

        g.setColour (textFaint);
        g.setFont (font (16.0f));
        g.drawText ("Writes offline, on this computer", sideArea.withTop (sideArea.getBottom() - 20), juce::Justification::centredLeft, true);
    }

    if (status.isNotEmpty())
    {
        g.setColour (danger);
        g.setFont (font (16.0f, true));
        g.drawFittedText (status, statusArea, juce::Justification::centredLeft, 1);
    }
}

} // namespace nightshift
