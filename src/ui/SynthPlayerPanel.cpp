// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "SynthPlayerPanel.h"
#include "FreezeGuard.h"
#include "ai/OfflineBass.h"
#include "ai/OfflineDrummer.h"
#include "audio/Nova.h"

namespace nightshift
{

using namespace theme;

namespace
{
    const char* const ideas[] = { "Metalcore trance arpeggio", "Trap bell melody", "Djent ambient pad",
                                  "Neo soul Moog lead with slides", "Lofi lead, lazy", "Funk horn stabs" };

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

    // The drummer the band plays with, and the part that drummer is playing
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
/** What was asked (right) and what the synth player played (left), each answer a version; click one
    to hear it again. */
class SynthPlayerPanel::Conversation : public juce::Component
{
public:
    explicit Conversation (SynthPlayerPanel& p) : panel (p)        {}

    void update (int width)
    {
        entries.clear();
        int y = 12;

        if (auto c = panel.channel())
        {
            const int bubbleWidth = std::min (760, (int) (width * 0.8f));

            for (int i = 0; i < (int) c->synthPlayer.turns.size(); ++i)
            {
                const auto& turn = c->synthPlayer.turns[(size_t) i];

                juce::AttributedString asked;
                asked.append (turn.prompt, font (16.0f), text);
                asked.setWordWrap (juce::AttributedString::byWord);
                y = add (asked, true, -1, width, bubbleWidth, y);

                auto part = synth::Part::fromJson (juce::JSON::parse (turn.part));
                juce::AttributedString answer;
                answer.append ("Version " + juce::String (i + 1) + (part.has_value() ? "  " + part->title : juce::String())
                                 + (i == c->synthPlayer.shown ? "   PLAYING" : "") + "\n", font (15.0f, true), i == c->synthPlayer.shown ? accent : textDim);
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
            const bool shown = ! e.mine && c != nullptr && e.turn == c->synthPlayer.shown;
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

    SynthPlayerPanel& panel;
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
SynthPlayerPanel::SynthPlayerPanel (Song& s, AudioEngine& a)
    : song (s), audio (a)
{
    conversation = std::make_unique<Conversation> (*this);
    conversationView.setViewedComponent (conversation.get(), false);
    conversationView.setScrollBarsShown (true, false);
    conversationView.setScrollBarThickness (8);
    addAndMakeVisible (conversationView);

    soundChip.setTooltip ("The synth's sound: pick another");
    soundChip.onClick = [this] { showSoundMenu(); };
    addAndMakeVisible (soundChip);

    for (auto* b : { &previousButton, &nextButton, &arrangeButton })
    {
        b->setFramed (true);
        addAndMakeVisible (*b);
    }

    previousButton.setTooltip ("The version before");
    nextButton.setTooltip ("The version after");
    previousButton.onClick = [this] { if (auto c = channel()) showVersion (c->synthPlayer.shown - 1); };
    nextButton.onClick = [this] { if (auto c = channel()) showVersion (c->synthPlayer.shown + 1); };

    arrangeButton.setLabel ("Put in Arrange");
    arrangeButton.setTooltip ("Lay the sections out in Arrange, under the band's (the drums go in first if they aren't there yet)");
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

SynthPlayerPanel::~SynthPlayerPanel()
{
    song.removeListener (this);
}

Channel* SynthPlayerPanel::channel() const
{
    return channelId != 0 ? song.findChannel (channelId) : nullptr;
}

void SynthPlayerPanel::setChannel (int id)
{
    if (id == channelId)
        return;

    channelId = id;
    status = {};
    refresh();
    conversationView.setViewPosition (0, conversation->getHeight());
    changed();
}

bool SynthPlayerPanel::hasTurns() const
{
    auto c = channel();
    return c != nullptr && ! c->synthPlayer.turns.empty();
}

void SynthPlayerPanel::changed()
{
    if (onStateChanged != nullptr)
        onStateChanged();
}

void SynthPlayerPanel::songChanged (Song::Change c)
{
    if (c != Song::Change::selection)
        refresh();
}

void SynthPlayerPanel::refresh()
{
    auto c = channel();
    const bool talking = hasTurns();
    const auto part = c != nullptr ? synth::shownPart (*c) : std::nullopt;

    soundChip.setLabel (c != nullptr ? "Sound: " + (c->type == InstrumentType::synth ? c->synthPreset : c->name) : juce::String());
    soundChip.setSwatch (c != nullptr ? c->colour : accent);
    soundChip.setEnabled (c != nullptr && c->type == InstrumentType::synth);

    const int turns = c != nullptr ? (int) c->synthPlayer.turns.size() : 0;
    const int shown = c != nullptr ? c->synthPlayer.shown : -1;
    previousButton.setEnabled (shown > 0);
    nextButton.setEnabled (shown >= 0 && shown < turns - 1);
    arrangeButton.setEnabled (part.has_value());

    sectionChips.clear();

    if (part.has_value() && c != nullptr)
        for (auto& section : part->sections)
        {
            auto it = c->synthPlayer.patterns.find (section.name);
            auto pattern = it != c->synthPlayer.patterns.end() ? song.findPattern (it->second) : nullptr;

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
void SynthPlayerPanel::ask (const juce::String& words)
{
    if (words.trim().isEmpty())
        return;

    // A frozen channel's notes can't change: it asks to unfreeze it first
    if (auto frozen = channel(); frozen != nullptr && ! freezing::mayEdit (frozen->id))
        return;

    // It plays with the band: the drummer's part and the bassist's chords; it builds on its own
    // last request (or the bassist's, or the drummer's, for a first part)
    auto [drumsId, drums] = drumsFor (song);
    auto [bassId, bassLine] = bassIn (song);
    std::optional<agents::Request> previous;

    juce::String previousRole;

    if (auto c = channel(); c != nullptr && juce::isPositiveAndBelow (c->synthPlayer.shown, (int) c->synthPlayer.turns.size()))
    {
        previous = synth::offline::requestOf (c->synthPlayer.turns[(size_t) c->synthPlayer.shown]);

        if (auto shown = synth::shownPart (*c))
            previousRole = shown->role;
    }

    if (! previous.has_value() && bassId != 0)
        if (auto b = song.findChannel (bassId); b != nullptr && juce::isPositiveAndBelow (b->bass.shown, (int) b->bass.turns.size()))
            previous = bass::offline::requestOf (b->bass.turns[(size_t) b->bass.shown]);

    if (! previous.has_value() && drumsId != 0)
        if (auto d = song.findChannel (drumsId); d != nullptr && juce::isPositiveAndBelow (d->drummer.shown, (int) d->drummer.turns.size()))
            previous = drummer::offline::requestOf (d->drummer.turns[(size_t) d->drummer.shown]);

    // No channel yet: one on the sound the style wants for the job (or the sound asked for)
    if (channel() == nullptr && onNeedChannel != nullptr)
    {
        auto style = agents::Request::parse (words, previous.has_value() ? &*previous : nullptr).style;

        if (style.isEmpty())
            style = "rock";

        const auto wanted = synth::offline::askOf (words);
        const auto role = wanted.role.isNotEmpty() ? wanted.role : synth::offline::roleFor (style);
        channelId = onNeedChannel (wanted.sound.isNotEmpty() ? wanted.sound : synth::offline::soundFor (style, role), role);
    }

    auto c = channel();

    if (c == nullptr)
    {
        status = "There's no channel to write the synth part for.";
        refresh();
        changed();
        return;
    }

    const bool blank = std::none_of (song.channels.begin(), song.channels.end(), [this, &c] (const Channel& other)
    {
        if (other.id == c->id)
            return false;

        for (auto& p : song.patterns)
            if (auto notes = p.findNotes (other.id); notes != nullptr && ! notes->empty())
                return true;

        return false;
    }) && song.audioClips.empty();

    // The keys player's part, for a lead to answer rather than talk over
    std::optional<keys::Part> keysPart;

    for (auto& other : song.channels)
        if (other.id != c->id && ! keysPart.has_value())
            keysPart = keys::shownPart (other);

    const bool chooseSound = c->synthPlayer.pickSound && c->synthPlayer.turns.empty();
    const auto currentSound = c->type == InstrumentType::synth ? c->synthPreset : c->name;
    const auto result = synth::offline::write (words, song, previous.has_value() ? &*previous : nullptr, previousRole, currentSound,
                                               drums.has_value() ? &*drums : nullptr, bassLine.has_value() ? &*bassLine : nullptr,
                                               keysPart.has_value() ? &*keysPart : nullptr, chooseSound, blank,
                                               juce::Time::currentTimeMillis() + ++asks);

    song.checkpoint();
    c->synthPlayer.turns.push_back (synth::offline::turnFor (words, result));
    c->synthPlayer.shown = (int) c->synthPlayer.turns.size() - 1;
    c->synthPlayer.pickSound = false;

    // The sound it chose (or was asked for), before the part is played in: a legato lead's
    // notes are laid out for its glide
    if (const auto* patch = nova::findFactory (result.part.sound);
        patch != nullptr && c->type == InstrumentType::synth && c->synthPreset != result.part.sound)
    {
        audio.applySynthPatch (c->id, *patch);
        song.notify (Song::Change::channels);
    }

    // Its fader where the style's mix wants it, on a channel made for it
    if (chooseSound)
    {
        c->volume = juce::jlimit (0.0f, 1.0f, 0.78f * std::pow (10.0f, result.part.levelDb / 40.0f));
        song.notify (Song::Change::mixer);
    }

    synth::rerender (song, c->id);

    if (! result.part.sections.empty())
        if (auto it = c->synthPlayer.patterns.find (result.part.sections.front().name); it != c->synthPlayer.patterns.end())
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

void SynthPlayerPanel::showVersion (int index)
{
    auto c = channel();

    if (c == nullptr || ! juce::isPositiveAndBelow (index, (int) c->synthPlayer.turns.size()) || index == c->synthPlayer.shown)
        return;

    song.checkpoint();
    c->synthPlayer.shown = index;
    synth::rerender (song, channelId);
    refresh();
}

void SynthPlayerPanel::showSoundMenu()
{
    auto c = channel();

    if (c == nullptr)
        return;

    juce::PopupMenu menu;

    if (c->type != InstrumentType::synth)
        return;

    // Nova's sounds, a submenu for each kind
    for (auto* category : { "Lead", "Pad", "Arp", "Pluck", "Bell", "Brass", "Keys", "Texture" })
    {
        juce::PopupMenu sub;

        for (auto& name : nova::factoryNames())
            if (auto f = nova::findFactory (name); f != nullptr && f->category == category)
                sub.addItem (name, true, c->synthPreset == name, [this, name]
                {
                    if (auto ch = channel())
                        if (auto f2 = nova::findFactory (name))
                        {
                            song.checkpoint();
                            audio.applySynthPatch (ch->id, *f2);
                            synth::rerender (song, ch->id);
                            song.notify (Song::Change::channels);
                        }
                });

        const juce::String kind (category);
        const juce::String label = kind == "Lead" ? "Leads" : kind == "Pad" ? "Pads" : kind == "Arp" ? "Arpeggios" : kind == "Pluck" ? "Plucks"
                                 : kind == "Bell" ? "Bells" : kind == "Brass" ? "Brass and stabs" : kind == "Keys" ? "Keys" : "Textures";
        const auto* current = nova::findFactory (c->synthPreset);

        if (sub.getNumItems() > 0)
            menu.addSubMenu (label, sub, true, nullptr, current != nullptr && current->category == kind);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&soundChip));
}

void SynthPlayerPanel::putInArrange()
{
    auto c = channel();
    auto part = c != nullptr ? synth::shownPart (*c) : std::nullopt;

    if (! part.has_value())
        return;

    song.checkpoint();

    // The drums first, if the band follows a drummer whose part isn't in Arrange yet
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

    synth::placeInArrange (song, channelId, *part);
}

//==============================================================================
void SynthPlayerPanel::resized()
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

void SynthPlayerPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    auto c = channel();

    if (! hasTurns())
    {
        auto t = introArea;
        g.setColour (text);
        g.setFont (font (21.0f, true));
        g.drawText ("Describe a synth part in the bar above and press Enter", t.removeFromTop (30), juce::Justification::centredLeft, true);

        auto [drumsId, drums] = drumsFor (song);
        const auto drummerChannel = drumsId != 0 ? song.findChannel (drumsId) : nullptr;
        const auto [bassId, bassLine] = bassIn (song);
        const juce::String with = bassLine.has_value() ? "It plays the bassist's chords with " + (drummerChannel != nullptr ? drummerChannel->name : juce::String ("the drums")) + ". "
                                                       : juce::String ("No bass yet, so it plays the chords its style would. ");
        const juce::String who = c != nullptr ? "It writes for " + c->name + ". " : juce::String ("It adds a synth channel on the style's sound for the job: a pad, an arpeggio, a lead, stabs. ");
        drawWrapped (g, who + with + "Or start from one of these:", font (16.0f), textDim, t.removeFromTop (48).withTrimmedTop (4));
    }
    else
    {
        g.setColour (hairline);
        g.fillRect (sideArea.getX() - 6, sideArea.getY(), 1, sideArea.getHeight());

        const auto part = c != nullptr ? synth::shownPart (*c) : std::nullopt;
        const int turns = c != nullptr ? (int) c->synthPlayer.turns.size() : 0;
        g.setColour (part.has_value() ? text : textFaint);
        g.setFont (font (16.0f, true));
        g.drawText (part.has_value() ? "Version " + juce::String (c->synthPlayer.shown + 1) + " of " + juce::String (turns) : juce::String ("No part yet"),
                    versionArea.withTrimmedLeft (84), juce::Justification::centredLeft, true);

        if (part.has_value())
        {
            g.setColour (textDim);
            g.setFont (font (16.0f));
            const juce::String job = part->role == "pad" ? "a pad" : part->role == "arp" ? "an arpeggio" : part->role == "stabs" ? "stabs" : "a lead";
            g.drawText ("In " + part->key + ", playing " + job, versionArea.translated (0, 36).withHeight (24), juce::Justification::centredLeft, true);
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
