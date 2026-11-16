// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "BassPanel.h"
#include "ai/OfflineBass.h"
#include "ai/OfflineDrummer.h"
#include "audio/Nova.h"

namespace nightshift
{

using namespace theme;

namespace
{
    const char* const ideas[] = { "Metalcore in drop C, locked to the kick", "Trap 808 with slides", "Funk on the one, slap the octaves",
                                  "Walking jazz bass", "Lofi, warm and lazy", "Pop punk, driving 8ths" };

    constexpr int sideWidth = 340;

    void drawWrapped (juce::Graphics& g, const juce::String& s, juce::Font f, juce::Colour c, juce::Rectangle<int> area)
    {
        juce::AttributedString words;
        words.append (s, f, c);
        words.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (words, (float) area.getWidth());
        layout.draw (g, area.toFloat());
    }

    // The drummer the bassist plays with, and the part that drummer is playing
    std::pair<int, std::optional<drummer::Groove>> drumsFor (const Song& song)
    {
        const int id = bass::drummerToFollow (song);

        for (auto& c : song.channels)
            if (c.id == id && juce::isPositiveAndBelow (c.drummer.shown, (int) c.drummer.turns.size()))
                return { id, drummer::Groove::fromJson (juce::JSON::parse (c.drummer.turns[(size_t) c.drummer.shown].groove)) };

        return { 0, std::nullopt };
    }
}

//==============================================================================
/** What was asked (right) and what the bassist played (left), each answer a version; click one
    to hear it again. */
class BassPanel::Conversation : public juce::Component
{
public:
    explicit Conversation (BassPanel& p) : panel (p)        {}

    void update (int width)
    {
        entries.clear();
        int y = 12;

        if (auto c = panel.channel())
        {
            const int bubbleWidth = std::min (760, (int) (width * 0.8f));

            for (int i = 0; i < (int) c->bass.turns.size(); ++i)
            {
                const auto& turn = c->bass.turns[(size_t) i];

                juce::AttributedString asked;
                asked.append (turn.prompt, font (16.0f), text);
                asked.setWordWrap (juce::AttributedString::byWord);
                y = add (asked, true, -1, width, bubbleWidth, y);

                auto part = bass::Part::fromJson (juce::JSON::parse (turn.part));
                juce::AttributedString answer;
                answer.append ("Version " + juce::String (i + 1) + (part.has_value() ? "  " + part->title : juce::String())
                                 + (i == c->bass.shown ? "   PLAYING" : "") + "\n", font (15.0f, true), i == c->bass.shown ? accent : textDim);
                answer.append (turn.reply, font (16.0f), text);
                answer.setWordWrap (juce::AttributedString::byWord);
                y = add (answer, false, i, width, bubbleWidth, y);
            }
        }

        setSize (width, std::max (y + 8, panel.conversationView.getHeight()));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto c = panel.channel();

        for (auto& e : entries)
        {
            auto b = e.bounds.toFloat();
            const bool shown = ! e.mine && c != nullptr && e.turn == c->bass.shown;
            g.setColour (e.mine ? raised : (shown ? accent.withAlpha (0.08f) : panelLight));
            g.fillRoundedRectangle (b, 10.0f);

            if (! e.mine)
            {
                g.setColour (shown ? accent : (c != nullptr ? c->colour : accent).withAlpha (0.6f));
                g.fillRoundedRectangle (b.getX(), b.getY() + 8.0f, 3.0f, b.getHeight() - 16.0f, 1.5f);
            }

            e.layout.draw (g, b.reduced (14.0f, 10.0f));
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (versionAt (e.getPosition()) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (const int turn = versionAt (e.getPosition()); turn >= 0 && e.mouseWasClicked())
            panel.showVersion (turn);
    }

private:
    struct Entry
    {
        juce::Rectangle<int> bounds;
        juce::TextLayout layout;
        bool mine = false;
        int turn = -1;
    };

    BassPanel& panel;
    std::vector<Entry> entries;

    int add (const juce::AttributedString& words, bool mine, int turn, int width, int bubbleWidth, int y)
    {
        Entry e;
        e.layout.createLayout (words, (float) bubbleWidth - 28.0f);
        const int w = std::min (bubbleWidth, (int) std::ceil (e.layout.getWidth()) + 30);
        const int h = (int) std::ceil (e.layout.getHeight()) + 20;
        e.bounds = { mine ? width - 16 - w : 16, y, w, h };
        e.mine = mine;
        e.turn = turn;
        entries.push_back (std::move (e));
        return y + h + 10;
    }

    int versionAt (juce::Point<int> p) const
    {
        for (auto& e : entries)
            if (! e.mine && e.bounds.contains (p))
                return e.turn;

        return -1;
    }
};

//==============================================================================
BassPanel::BassPanel (Song& s, AudioEngine& a)
    : song (s), audio (a)
{
    conversation = std::make_unique<Conversation> (*this);
    conversationView.setViewedComponent (conversation.get(), false);
    conversationView.setScrollBarsShown (true, false);
    conversationView.setScrollBarThickness (8);
    addAndMakeVisible (conversationView);

    soundChip.setTooltip ("The sound the bass plays: pick another bass sound");
    soundChip.onClick = [this] { showSoundMenu(); };
    addAndMakeVisible (soundChip);

    for (auto* b : { &previousButton, &nextButton, &arrangeButton })
    {
        b->setFramed (true);
        addAndMakeVisible (*b);
    }

    previousButton.setTooltip ("The version before");
    nextButton.setTooltip ("The version after");
    previousButton.onClick = [this] { if (auto c = channel()) showVersion (c->bass.shown - 1); };
    nextButton.onClick = [this] { if (auto c = channel()) showVersion (c->bass.shown + 1); };

    arrangeButton.setLabel ("Put in Arrange");
    arrangeButton.setTooltip ("Lay the sections out in Arrange, under the drummer's (the drums go in first if they aren't there yet)");
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

BassPanel::~BassPanel()
{
    song.removeListener (this);
}

Channel* BassPanel::channel() const
{
    return channelId != 0 ? song.findChannel (channelId) : nullptr;
}

void BassPanel::setChannel (int id)
{
    if (id == channelId)
        return;

    channelId = id;
    status = {};
    refresh();
    conversationView.setViewPosition (0, conversation->getHeight());
    changed();
}

bool BassPanel::hasTurns() const
{
    auto c = channel();
    return c != nullptr && ! c->bass.turns.empty();
}

void BassPanel::changed()
{
    if (onStateChanged != nullptr)
        onStateChanged();
}

void BassPanel::songChanged (Song::Change c)
{
    if (c != Song::Change::selection)
        refresh();
}

void BassPanel::refresh()
{
    auto c = channel();
    const bool talking = hasTurns();
    const auto part = c != nullptr ? bass::shownPart (*c) : std::nullopt;

    soundChip.setLabel (c != nullptr ? "Sound: " + (c->type == InstrumentType::synth ? c->synthPreset : c->name) : juce::String());
    soundChip.setSwatch (c != nullptr ? c->colour : accent);
    soundChip.setEnabled (c != nullptr && c->type == InstrumentType::synth);

    const int turns = c != nullptr ? (int) c->bass.turns.size() : 0;
    const int shown = c != nullptr ? c->bass.shown : -1;
    previousButton.setEnabled (shown > 0);
    nextButton.setEnabled (shown >= 0 && shown < turns - 1);
    arrangeButton.setEnabled (part.has_value());

    sectionChips.clear();

    if (part.has_value() && c != nullptr)
        for (auto& section : part->sections)
        {
            auto it = c->bass.patterns.find (section.name);
            auto pattern = it != c->bass.patterns.end() ? song.findPattern (it->second) : nullptr;

            if (pattern == nullptr)
                continue;

            auto* chip = sectionChips.add (new Chip (section.name));
            chip->setOpensMenu (false);
            chip->setSwatch (pattern->colour);
            chip->setTooltip ("Open " + pattern->name + " in the piano roll");
            chip->onClick = [this, id = pattern->id] { if (onShowPattern != nullptr) onShowPattern (channelId, id); };
            addChildComponent (chip);
        }

    for (auto* comp : std::initializer_list<juce::Component*> { &soundChip, &previousButton, &nextButton, &arrangeButton, &conversationView })
        comp->setVisible (talking);

    for (auto* chip : sectionChips)
        chip->setVisible (talking);

    for (auto* chip : ideaChips)
        chip->setVisible (! talking);

    resized();
    conversation->update (std::max (200, conversationView.getWidth() - conversationView.getScrollBarThickness()));
    repaint();
}

//==============================================================================
void BassPanel::ask (const juce::String& words)
{
    if (words.trim().isEmpty())
        return;

    if (channel() == nullptr && onNeedChannel != nullptr)
        channelId = onNeedChannel();

    auto c = channel();

    if (c == nullptr)
    {
        status = "There's no channel to write the bass for.";
        refresh();
        changed();
        return;
    }

    // It plays with the drummer: their part (its sections and kick), and their style to start from
    auto [drumsId, drums] = drumsFor (song);
    std::optional<agents::Request> previous;

    if (juce::isPositiveAndBelow (c->bass.shown, (int) c->bass.turns.size()))
        previous = bass::offline::requestOf (c->bass.turns[(size_t) c->bass.shown]);

    if (! previous.has_value() && drumsId != 0)
        if (auto d = song.findChannel (drumsId); d != nullptr && juce::isPositiveAndBelow (d->drummer.shown, (int) d->drummer.turns.size()))
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

    const bool chooseSound = c->bass.pickSound && c->bass.turns.empty() && c->type == InstrumentType::synth;
    const auto result = bass::offline::write (words, song, previous.has_value() ? &*previous : nullptr, drums.has_value() ? &*drums : nullptr,
                                              chooseSound, blank, juce::Time::currentTimeMillis() + ++asks);

    song.checkpoint();
    c->bass.turns.push_back (bass::offline::turnFor (words, result));
    c->bass.shown = (int) c->bass.turns.size() - 1;
    c->bass.pickSound = false;

    // The sound it chose for the style, on a channel made for it
    if (chooseSound)
        if (auto patch = nova::Patch::fromVar (result.part.patch))
        {
            audio.applySynthPatch (c->id, *patch);
            song.notify (Song::Change::channels);
        }

    bass::rerender (song, c->id);

    // Its first section in the editors, ready to hear
    if (! result.part.sections.empty())
        if (auto it = c->bass.patterns.find (result.part.sections.front().name); it != c->bass.patterns.end())
        {
            song.currentPatternId = it->second;
            song.notify (Song::Change::currentPattern);
        }

    status = {};
    refresh();
    conversationView.setViewPosition (0, conversation->getHeight());

    if (onAnswered != nullptr)
        onAnswered();

    changed();
}

void BassPanel::showVersion (int index)
{
    auto c = channel();

    if (c == nullptr || ! juce::isPositiveAndBelow (index, (int) c->bass.turns.size()) || index == c->bass.shown)
        return;

    song.checkpoint();
    c->bass.shown = index;
    bass::rerender (song, channelId);
    refresh();
}

void BassPanel::showSoundMenu()
{
    auto c = channel();

    if (c == nullptr || c->type != InstrumentType::synth)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Bass sounds");

    // The bassist's own sound for each style, then Nova's basses
    juce::StringArray made;

    for (auto style : { "metalcore", "trap", "rock", "poppunk", "boombap", "lofi", "jazz", "neosoul", "funk" })
        if (auto v = bass::offline::soundFor (style); ! v.isVoid())
            if (const auto name = v.getProperty ("name", {}).toString(); ! made.contains (name))
            {
                made.add (name);
                menu.addItem (name, true, c->synthPreset == name, [this, v]
                {
                    if (auto ch = channel())
                        if (auto patch = nova::Patch::fromVar (v))
                        {
                            song.checkpoint();
                            audio.applySynthPatch (ch->id, *patch);
                            song.notify (Song::Change::channels);
                        }
                });
            }

    menu.addSeparator();

    for (auto& name : nova::factoryNames())
        if (auto f = nova::findFactory (name); f != nullptr && f->category == "Bass" && ! made.contains (name))
            menu.addItem (name, true, c->synthPreset == name, [this, name]
            {
                if (auto ch = channel())
                    if (auto f2 = nova::findFactory (name))
                    {
                        song.checkpoint();
                        audio.applySynthPatch (ch->id, *f2);
                        song.notify (Song::Change::channels);
                    }
            });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&soundChip));
}

void BassPanel::putInArrange()
{
    auto c = channel();
    auto part = c != nullptr ? bass::shownPart (*c) : std::nullopt;

    if (! part.has_value())
        return;

    song.checkpoint();

    // The drums first, if the bassist follows a drummer whose part isn't in Arrange yet
    auto [drumsId, drums] = drumsFor (song);

    if (drumsId != 0 && drums.has_value())
        if (auto d = song.findChannel (drumsId))
        {
            const bool placed = std::any_of (song.playlist.begin(), song.playlist.end(), [d] (const PlaylistClip& clip)
            {
                for (auto& [name, id] : d->drummer.patterns)
                    if (clip.patternId == id)
                        return true;

                return false;
            });

            if (! placed)
                drummer::placeInArrange (song, drumsId, *drums);
        }

    bass::placeInArrange (song, channelId, *part);
}

//==============================================================================
void BassPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 10);

    // Before the first part: what it does, and ideas to start from
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

    // The version playing down the right: its sound, versions, sections, Put in Arrange
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

        s.removeFromTop (34);       // the key, written under the version
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

    conversationView.setBounds (r);
    conversation->update (std::max (200, r.getWidth() - conversationView.getScrollBarThickness()));
}

void BassPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    auto c = channel();

    if (! hasTurns())
    {
        auto t = introArea;
        g.setColour (text);
        g.setFont (font (21.0f, true));
        g.drawText ("Describe a bass line in the bar above and press Enter", t.removeFromTop (30), juce::Justification::centredLeft, true);

        auto [drumsId, drums] = drumsFor (song);
        const auto drummerChannel = drumsId != 0 ? song.findChannel (drumsId) : nullptr;
        const juce::String with = drummerChannel != nullptr ? "It plays with " + drummerChannel->name + "'s part: the same sections, locked to its kick. "
                                                            : juce::String ("No drums yet, so it plays to the drums its style would have. ");
        const juce::String who = c != nullptr ? "It writes for " + c->name + ". " : juce::String ("It adds a bass channel, choosing the sound for the style. ");
        drawWrapped (g, who + with + "Or start from one of these:", font (16.0f), textDim, t.removeFromTop (48).withTrimmedTop (4));
    }
    else
    {
        g.setColour (hairline);
        g.fillRect (sideArea.getX() - 6, sideArea.getY(), 1, sideArea.getHeight());

        const auto part = c != nullptr ? bass::shownPart (*c) : std::nullopt;
        const int turns = c != nullptr ? (int) c->bass.turns.size() : 0;
        g.setColour (part.has_value() ? text : textFaint);
        g.setFont (font (16.0f, true));
        g.drawText (part.has_value() ? "Version " + juce::String (c->bass.shown + 1) + " of " + juce::String (turns) : juce::String ("No part yet"),
                    versionArea.withTrimmedLeft (84), juce::Justification::centredLeft, true);

        if (part.has_value())
        {
            g.setColour (textDim);
            g.setFont (font (16.0f));
            g.drawText ("In " + part->key, versionArea.translated (0, 36).withHeight (24), juce::Justification::centredLeft, true);
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
