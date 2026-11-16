// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "DrummerPanel.h"
#include "audio/Kits.h"

namespace nightshift
{

using namespace theme;

namespace
{
    const char* const ideas[] = { "Lofi jazz on brushes, lazy swing and ghost notes",
                                  "Prog rock in 7/8: verse, chorus and a big tom fill",
                                  "Metalcore: blast beat verse, half-time breakdown with china",
                                  "Funk pocket with ghost notes and open hats",
                                  "Trap: half-time snare, hi-hat rolls in triplets",
                                  "Boom bap at 90 BPM, dusty and behind the beat" };

    constexpr int sideWidth = 340, feelHeight = 44;

    /** Text wrapped at word breaks into an area, at its own size (drawFittedText squeezes it). */
    void drawWrapped (juce::Graphics& g, const juce::String& s, juce::Font f, juce::Colour c, juce::Rectangle<int> area)
    {
        juce::AttributedString text;
        text.append (s, f, c);
        text.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (text, (float) area.getWidth());
        layout.draw (g, area.toFloat());
    }
}

//==============================================================================
/** Fetches the free kits off the message thread and reports back on it. */
class DrummerPanel::KitFetcher : public juce::Thread
{
public:
    explicit KitFetcher (DrummerPanel& p) : juce::Thread ("Drum kits"), panel (&p)    { startThread(); }
    ~KitFetcher() override                                                           { stopThread (15000); }

    void run() override
    {
        juce::String last;
        const auto error = drumkits::fetch ([this, &last] (float done, const juce::String& what)
        {
            // Each whole percent, or a new step
            const auto note = what + "|" + juce::String (juce::roundToInt (done * 100.0f));

            if (note != last)
            {
                last = note;
                juce::MessageManager::callAsync ([p = panel, done, what] { if (p != nullptr) p->kitProgress (done, what); });
            }
        }, [this] { return threadShouldExit(); });

        if (! threadShouldExit())
            juce::MessageManager::callAsync ([p = panel, error] { if (p != nullptr) p->kitsFetched (error); });
    }

private:
    juce::Component::SafePointer<DrummerPanel> panel;
};

//==============================================================================
/** The conversation so far: what was asked (right) and what the drummer answered (left),
    each answer a version; click one to hear it again. */
class DrummerPanel::Conversation : public juce::Component
{
public:
    explicit Conversation (DrummerPanel& p) : panel (p)     { setMouseCursor (juce::MouseCursor::NormalCursor); }

    void update (int width)
    {
        entries.clear();
        int y = 12;
        auto c = panel.channel();

        if (c != nullptr)
        {
            const int bubbleWidth = std::min (760, (int) (width * 0.8f));

            for (int i = 0; i < (int) c->drummer.turns.size(); ++i)
            {
                const auto& turn = c->drummer.turns[(size_t) i];

                // What was asked
                juce::AttributedString asked;
                asked.append (turn.prompt, font (16.0f), text);
                asked.setWordWrap (juce::AttributedString::byWord);
                y = add (asked, true, -1, width, bubbleWidth, y);

                // The answer: its version, title, and what the drummer said
                auto groove = drummer::Groove::fromJson (juce::JSON::parse (turn.groove));
                juce::AttributedString answer;
                answer.append ("Version " + juce::String (i + 1) + (groove.has_value() ? "  " + groove->title : juce::String())
                                 + (i == c->drummer.shown ? "   PLAYING" : "") + "\n", font (15.0f, true), i == c->drummer.shown ? accent : textDim);
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
        for (auto& e : entries)
        {
            auto b = e.bounds.toFloat();
            const bool shown = ! e.mine && panel.channel() != nullptr && e.turn == panel.channel()->drummer.shown;
            g.setColour (e.mine ? raised : (shown ? accent.withAlpha (0.08f) : panelLight));
            g.fillRoundedRectangle (b, 10.0f);

            if (! e.mine)
            {
                g.setColour (shown ? accent : (panel.channel() != nullptr ? panel.channel()->colour : accent).withAlpha (0.6f));
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

    DrummerPanel& panel;
    std::vector<Entry> entries;

    int add (const juce::AttributedString& text, bool mine, int turn, int width, int bubbleWidth, int y)
    {
        Entry e;
        e.layout.createLayout (text, (float) bubbleWidth - 28.0f);
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
DrummerPanel::DrummerPanel (Song& s, AudioEngine& a)
    : song (s), audio (a)
{
    conversation = std::make_unique<Conversation> (*this);
    conversationView.setViewedComponent (conversation.get(), false);
    conversationView.setScrollBarsShown (true, false);
    conversationView.setScrollBarThickness (8);
    addAndMakeVisible (conversationView);

    kitChip.setTooltip ("The kit the drummer plays");
    kitChip.onClick = [this] { showKitMenu(); };
    addAndMakeVisible (kitChip);

    for (auto* b : { &previousButton, &nextButton, &arrangeButton })
    {
        b->setFramed (true);
        addAndMakeVisible (*b);
    }

    previousButton.setTooltip ("The version before");
    nextButton.setTooltip ("The version after");
    previousButton.onClick = [this] { if (auto c = channel()) showVersion (c->drummer.shown - 1); };
    nextButton.onClick = [this] { if (auto c = channel()) showVersion (c->drummer.shown + 1); };

    arrangeButton.setLabel ("Put in Arrange");
    arrangeButton.setTooltip ("Lay the sections out in Arrange, in the order the drummer gave, on a free track");
    arrangeButton.onClick = [this]
    {
        if (auto g = shownGroove())
        {
            song.checkpoint();
            drummer::placeInArrange (song, channelId, *g);
        }
    };

    for (auto idea : ideas)
    {
        auto* chip = starterChips.add (new Chip (idea));
        chip->setWraps (true);
        chip->setOpensMenu (false);
        chip->setTooltip ("Put this in the prompt above, to send as it is or change first");
        chip->onClick = [this, idea = juce::String (idea)] { if (onUseIdea != nullptr) onUseIdea (idea); };
        addChildComponent (chip);
    }

    // Feel: reshape the version playing without asking again
    auto feelBar = [this] (LabelledBar& bar, std::function<void (Drummer&, float)> apply)
    {
        auto format = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        bar.formatValue = format;
        bar.bar.formatValue = format;
        bar.bar.onDragStart = [this] { song.checkpoint(); };
        bar.onValueChange = [this, &bar, apply]
        {
            if (auto c = channel())
            {
                apply (c->drummer, (float) bar.bar.getValue());
                drummer::rerender (song, channelId);
            }
        };
        addAndMakeVisible (bar);
    };

    feelBar (swing, [] (Drummer& d, float v) { d.swing = v; });
    feelBar (looseness, [] (Drummer& d, float v) { d.looseness = v; });
    feelBar (dynamics, [] (Drummer& d, float v) { d.dynamics = v; });

    writtenSwing.setTooltip ("Swing as the drummer wrote it");
    writtenSwing.onClick = [this]
    {
        if (auto c = channel())
        {
            song.checkpoint();
            c->drummer.swing = -1.0f;
            drummer::rerender (song, channelId);
            refresh();
        }
    };
    addAndMakeVisible (writtenSwing);

    kitsButton.setLabel ("Get the free drum kits");
    kitsButton.setFramed (true);
    kitsButton.setSolidWhenOn (true);
    kitsButton.setToggleState (true, juce::dontSendNotification);
    kitsButton.setTooltip ("Download the studio drum kits (10.5 GB) from drumgizmo.org and mix them for NightShift");
    kitsButton.onClick = [this] { getKits(); };
    addChildComponent (kitsButton);

    song.addListener (this);
    setSize (900, 360);
    refresh();
}

DrummerPanel::~DrummerPanel()
{
    kitFetcher.reset();
    song.removeListener (this);
}

Channel* DrummerPanel::channel() const
{
    return channelId != 0 ? song.findChannel (channelId) : nullptr;
}

void DrummerPanel::setChannel (int id)
{
    if (id == channelId)
        return;

    channelId = id;

    // An old note belongs to the channel it was for
    status = {};
    statusIsError = false;

    refresh();
    conversationView.setViewPosition (0, conversation->getHeight());
    changed();
}

bool DrummerPanel::hasTurns() const
{
    auto c = channel();
    return c != nullptr && ! c->drummer.turns.empty();
}

std::optional<drummer::Groove> DrummerPanel::shownGroove() const
{
    auto c = channel();

    if (c == nullptr || ! juce::isPositiveAndBelow (c->drummer.shown, (int) c->drummer.turns.size()))
        return {};

    return drummer::Groove::fromJson (juce::JSON::parse (c->drummer.turns[(size_t) c->drummer.shown].groove));
}

void DrummerPanel::songChanged (Song::Change c)
{
    if (c != Song::Change::selection)
        refresh();
}

void DrummerPanel::changed()
{
    if (onStateChanged != nullptr)
        onStateChanged();
}

void DrummerPanel::refresh()
{
    auto c = channel();
    kitsMissing = channel() == nullptr && drumkits::ids().isEmpty();
    offerStudio = ! kitsMissing && (kitFetcher != nullptr || ! drumkits::haveDownloads());
    const bool talking = hasTurns();

    if (c != nullptr)
    {
        kitChip.setLabel (c->type == InstrumentType::kit ? "Kit: " + drumkits::displayName (c->kit) : "General MIDI to " + c->name);
        kitChip.setSwatch (c->colour);
        kitChip.setEnabled (c->type == InstrumentType::kit);
    }

    const auto groove = shownGroove();
    const int turns = c != nullptr ? (int) c->drummer.turns.size() : 0;
    const int shown = c != nullptr ? c->drummer.shown : -1;
    previousButton.setEnabled (shown > 0);
    nextButton.setEnabled (shown >= 0 && shown < turns - 1);
    arrangeButton.setEnabled (groove.has_value());

    // A chip per section of the version playing
    sectionChips.clear();

    if (groove.has_value() && c != nullptr)
    {
        for (auto& section : groove->sections)
        {
            auto it = c->drummer.patterns.find (section.name);
            auto pattern = it != c->drummer.patterns.end() ? song.findPattern (it->second) : nullptr;

            if (pattern == nullptr)
                continue;

            auto* chip = sectionChips.add (new Chip (section.name));
            chip->setOpensMenu (false);
            chip->setSwatch (pattern->colour);
            chip->setTooltip ("Open " + pattern->name + " in the piano roll");
            chip->onClick = [this, id = pattern->id] { if (onShowPattern != nullptr) onShowPattern (channelId, id); };
            addChildComponent (chip);
        }
    }

    if (c != nullptr)
    {
        swing.bar.setValue (c->drummer.swing >= 0.0f ? c->drummer.swing : (groove.has_value() ? groove->swing : 0.0f));
        looseness.bar.setValue (c->drummer.looseness);
        dynamics.bar.setValue (c->drummer.dynamics);
    }

    // The conversation and its controls once there's a part; ideas before; the kits first of all
    for (auto* comp : std::initializer_list<juce::Component*> { &kitChip, &previousButton, &nextButton, &arrangeButton,
                                                                 &swing, &looseness, &dynamics, &writtenSwing })
        comp->setVisible (talking);

    for (auto* chip : sectionChips)
        chip->setVisible (talking);

    writtenSwing.setEnabled (groove.has_value() && c != nullptr && c->drummer.swing >= 0.0f);

    for (auto* chip : starterChips)
        chip->setVisible (! kitsMissing && ! talking);

    conversationView.setVisible (talking);
    kitsButton.setVisible (kitsMissing || offerStudio);
    kitsButton.setLabel (kitsMissing ? "Get the free drum kits" : "Get the studio kits");
    kitsButton.setEnabled (kitFetcher == nullptr);

    resized();
    repaint();
}

void DrummerPanel::showVersion (int index)
{
    auto c = channel();

    if (c == nullptr || ! juce::isPositiveAndBelow (index, (int) c->drummer.turns.size()) || index == c->drummer.shown)
        return;

    song.checkpoint();
    c->drummer.shown = index;
    drummer::rerender (song, channelId);
    refresh();
}

void DrummerPanel::showKitMenu()
{
    auto c = channel();

    if (c == nullptr || c->type != InstrumentType::kit)
        return;

    juce::PopupMenu menu;

    for (auto& id : drumkits::ids())
        menu.addItem (drumkits::displayName (id) + "  -  " + drumkits::character (id), true, c->kit == id, [this, id]
        {
            if (auto ch = channel(); ch != nullptr && ch->kit != id)
            {
                song.checkpoint();
                ch->kit = id;
                ch->drummer.pickKit = false;
                song.notify (Song::Change::channels);
                drummer::rerender (song, channelId);
            }
        });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&kitChip));
}

void DrummerPanel::ask (const juce::String& text)
{
    if (text.trim().isEmpty())
        return;

    // No kit to write for and none here: it waits for the kits
    if (channel() == nullptr && drumkits::ids().isEmpty())
    {
        pending = text.trim();
        status = {};
        statusIsError = false;
        refresh();
        changed();
        return;
    }

    pending = {};

    if (channel() == nullptr && onNeedChannel != nullptr)
        channelId = onNeedChannel();

    auto c = channel();

    if (c == nullptr)
    {
        status = "There's no drum kit to write for: put the AVL kits in " + drumkits::folder().getFullPathName() + ".";
        statusIsError = true;
        refresh();
        changed();
        return;
    }

    // The offline drummer writes it here and now, building on the last request if there was one
    const bool mayPickKit = c->type == InstrumentType::kit && c->drummer.pickKit && c->drummer.turns.empty();
    std::optional<agents::Request> previous;

    if (c->drummer.shown >= 0 && c->drummer.shown < (int) c->drummer.turns.size())
        previous = drummer::offline::requestOf (c->drummer.turns[(size_t) c->drummer.shown]);

    const bool blank = std::none_of (song.channels.begin(), song.channels.end(), [this, &c] (const Channel& other)
    {
        if (other.id == c->id)
            return false;

        for (auto& p : song.patterns)
            if (auto notes = p.findNotes (other.id); notes != nullptr && ! notes->empty())
                return true;

        return false;
    }) && song.audioClips.empty();

    const auto result = drummer::offline::write (text, song, previous.has_value() ? &*previous : nullptr,
                                                 drummer::kitNamesFor (*c), mayPickKit,
                                                 c->type == InstrumentType::kit ? drumkits::displayName (c->kit) : juce::String(),
                                                 blank, juce::Time::currentTimeMillis() + ++asks);

    played (*c, drummer::offline::turnFor (text, result));
}

void DrummerPanel::played (Channel& channelWritten, const DrummerTurn& turn)
{
    auto c = &channelWritten;
    const int writtenId = c->id;
    song.checkpoint();
    c->drummer.turns.push_back (turn);
    c->drummer.shown = (int) c->drummer.turns.size() - 1;

    // The kit it chose, if it chose another
    if (auto groove = drummer::Groove::fromJson (juce::JSON::parse (turn.groove)); groove.has_value() && c->type == InstrumentType::kit)
    {
        const auto kitId = drumkits::idForName (groove->kit);

        if (kitId.isNotEmpty() && kitId != c->kit)
        {
            c->kit = kitId;
            song.notify (Song::Change::channels);
        }
    }

    c->drummer.pickKit = false;
    drummer::rerender (song, writtenId);

    // Show the part it wrote, its first section in the editors ready to hear
    channelId = writtenId;

    if (auto groove = shownGroove(); groove.has_value() && ! groove->sections.empty())
        if (auto ch = channel())
            if (auto it = ch->drummer.patterns.find (groove->sections.front().name); it != ch->drummer.patterns.end())
            {
                song.currentPatternId = it->second;
                song.notify (Song::Change::currentPattern);
            }

    status = {};
    statusIsError = false;
    refresh();
    conversationView.setViewPosition (0, conversation->getHeight());

    if (onAnswered != nullptr)
        onAnswered();

    changed();
}

void DrummerPanel::getKits()
{
    if (kitFetcher != nullptr)
        return;

    kitNote = "Starting the download...";
    kitNoteIsError = false;
    kitFetcher = std::make_unique<KitFetcher> (*this);
    refresh();
}

void DrummerPanel::kitProgress (float done, const juce::String& what)
{
    kitNote = what + "...  " + juce::String (juce::roundToInt (done * 100.0f)) + "%";
    repaint (kitNoteArea);
}

void DrummerPanel::kitsFetched (const juce::String& error)
{
    kitFetcher.reset();
    kitNote = error;
    kitNoteIsError = error.isNotEmpty();
    refresh();

    if (error.isNotEmpty())
        return;

    if (onKitsArrived != nullptr)
        onKitsArrived();

    // What was waiting for them goes now
    if (auto waiting = pending; waiting.isNotEmpty())
        ask (waiting);
    else
        changed();
}

void DrummerPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 10);
    const bool talking = hasTurns();

    // No kits yet: a card to fetch them
    if (kitsMissing)
    {
        card = r.withSizeKeepingCentre (std::min (700, r.getWidth()), std::min (196, r.getHeight()));
        auto k = card.reduced (22, 18);
        k.removeFromTop (26 + 50 + 8);
        auto row = k.removeFromTop (38);
        kitsButton.setBounds (row.removeFromLeft (250));
        row.removeFromLeft (14);
        kitNoteArea = row;
        statusArea = card.withY (card.getBottom() + 4).withHeight (22);
        return;
    }

    // Until the studio kits are here, an offer across the top (and how far the download's got)
    offerArea = {};

    if (offerStudio)
    {
        offerArea = r.removeFromTop (48);
        auto o = offerArea.reduced (14, 8);
        kitsButton.setBounds (o.removeFromRight (196));
        o.removeFromRight (12);
        kitNoteArea = o.withTrimmedLeft (4);
        r.removeFromTop (8);
    }

    // Before the first part: what it does, and ideas to start from
    if (! talking)
    {
        introArea = r.withSizeKeepingCentre (std::min (860, r.getWidth()), std::min (290, r.getHeight()));
        auto ideasArea = introArea.withTrimmedTop (84);
        statusArea = introArea.withY (introArea.getBottom() + 2).withHeight (22);
        const int columns = ideasArea.getWidth() >= 760 ? 3 : 2, rowHeight = 60;
        const int w = (ideasArea.getWidth() - (columns - 1) * 8) / columns;

        for (int i = 0; i < starterChips.size(); ++i)
        {
            const int row = i / columns, col = i % columns;
            auto b = juce::Rectangle<int> (ideasArea.getX() + col * (w + 8), ideasArea.getY() + row * rowHeight, w, 52);
            starterChips[i]->setVisible (b.getBottom() <= ideasArea.getBottom());
            starterChips[i]->setBounds (b);
        }

        return;
    }

    // The feel across the bottom, with the status beside it
    feelArea = r.removeFromBottom (feelHeight);
    {
        auto f = feelArea.withTrimmedTop (10).withHeight (28);
        const int barWidth = std::min (230, (f.getWidth() - 96 - 40) / 3);
        dynamics.setBounds (f.removeFromRight (barWidth));
        f.removeFromRight (20);
        looseness.setBounds (f.removeFromRight (barWidth));
        f.removeFromRight (10);
        writtenSwing.setBounds (f.removeFromRight (96).reduced (0, 2));
        f.removeFromRight (10);
        swing.setBounds (f.removeFromRight (barWidth));
        f.removeFromRight (16);
        statusArea = f;
    }

    // The version playing down the right: kit, versions, sections, Put in Arrange
    sideArea = r.removeFromRight (std::min (sideWidth, r.getWidth() * 2 / 5));
    r.removeFromRight (12);
    {
        auto s = sideArea.reduced (0, 4);
        kitChip.setBounds (s.removeFromTop (32));
        s.removeFromTop (10);
        versionArea = s.removeFromTop (32);
        {
            auto v = versionArea;
            previousButton.setBounds (v.removeFromLeft (34));
            v.removeFromLeft (4);
            nextButton.setBounds (v.removeFromLeft (34));
        }

        s.removeFromTop (10);

        // Section chips, wrapped
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

        // Then Put in Arrange, under them
        const int below = sectionChips.isEmpty() ? s.getY() : y + 38;
        arrangeButton.setBounds (s.getX(), std::min (below + 4, s.getBottom() - 32), s.getWidth(), 32);
    }

    conversationView.setBounds (r);

    // Bubbles laid out for the width, leaving the scroll bar its room whether it shows or not
    conversation->update (std::max (200, r.getWidth() - conversationView.getScrollBarThickness()));
}

void DrummerPanel::paintOffer (juce::Graphics& g)
{
    if (offerArea.isEmpty())
        return;

    g.setColour (panelLight);
    g.fillRoundedRectangle (offerArea.toFloat(), 10.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (offerArea.toFloat().withWidth (4.0f).reduced (0.0f, 10.0f), 2.0f);

    if (kitNote.isNotEmpty())
        drawWrapped (g, kitNote, font (16.0f, true), kitNoteIsError ? danger : accent,
                     kitNoteArea.withSizeKeepingCentre (kitNoteArea.getWidth(), std::min (kitNoteArea.getHeight(), 36)));
    else
        drawWrapped (g, "Studio-recorded drum kits, free: Crocell for metal, Muldjord for rock, DRS and DRS Brushes for jazz to rock. "
                        "10.5 GB, downloaded and mixed once.",
                     font (16.0f), text, kitNoteArea.withSizeKeepingCentre (kitNoteArea.getWidth(), std::min (kitNoteArea.getHeight(), 36)));
}

void DrummerPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    auto c = channel();

    if (kitsMissing)
    {
        g.setColour (panelLight);
        g.fillRoundedRectangle (card.toFloat(), 12.0f);
        g.setColour (hairline);
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 12.0f, 1.0f);

        auto t = card.reduced (22, 18);
        g.setColour (text);
        g.setFont (font (18.0f, true));
        g.drawText (pending.isNotEmpty() ? "One step first: get the drum kits" : "The drummer needs a drum kit", t.removeFromTop (26),
                    juce::Justification::centredLeft, true);
        drawWrapped (g, "NightShift plays free studio-recorded kits from DrumGizmo.org (CC BY 4.0): Crocell for metal, Muldjord for rock, "
                        "DRS and DRS Brushes for jazz to rock. 10.5 GB, downloaded and mixed once."
                          + (pending.isNotEmpty() ? " Then \"" + pending + "\" goes straight to the drummer." : juce::String()),
                     font (16.0f), textDim, t.removeFromTop (50).withTrimmedTop (4));

        if (kitNote.isNotEmpty())
            drawWrapped (g, kitNote, font (15.0f, true), kitNoteIsError ? danger : accent,
                         kitNoteArea.withSizeKeepingCentre (kitNoteArea.getWidth(), std::min (kitNoteArea.getHeight(), 40)));
    }
    else if (! hasTurns())
    {
        paintOffer (g);
        auto t = introArea;
        g.setColour (text);
        g.setFont (font (21.0f, true));
        g.drawText ("Describe a beat in the bar above and press Enter", t.removeFromTop (30), juce::Justification::centredLeft, true);
        const juce::String who = c != nullptr ? "It writes for " + c->name + (c->type == InstrumentType::kit ? " on the " + drumkits::displayName (c->kit) + " kit. "
                                                                                                             : " (General MIDI). ")
                                              : juce::String ("It adds a drum kit channel, choosing the kit for the style. ");
        drawWrapped (g, who + "Any style, any time signature; then ask for changes until it sits right. Or start from one of these:",
                     font (16.0f), textDim, t.removeFromTop (48).withTrimmedTop (4));
    }
    else
    {
        paintOffer (g);
        g.setColour (hairline);
        g.fillRect (sideArea.getX() - 6, sideArea.getY(), 1, sideArea.getHeight());
        g.fillRect (getLocalBounds().getX(), feelArea.getY() + 2, getWidth(), 1);

        // The version playing
        const auto groove = shownGroove();
        const int turns = c != nullptr ? (int) c->drummer.turns.size() : 0;
        g.setColour (groove.has_value() ? text : textFaint);
        g.setFont (font (16.0f, true));
        g.drawText (groove.has_value() ? "Version " + juce::String (c->drummer.shown + 1) + " of " + juce::String (turns) + ":  " + groove->title
                                       : juce::String ("No part yet"),
                    versionArea.withTrimmedLeft (84), juce::Justification::centredLeft, true);

        // Where it writes: here, with nothing sent anywhere
        g.setColour (textFaint);
        g.setFont (font (15.0f));
        g.drawText ("Writes offline, on this computer", sideArea.withTop (sideArea.getBottom() - 20), juce::Justification::centredLeft, true);
    }

    // Status: progress, or what went wrong
    if (status.isNotEmpty())
    {
        g.setColour (statusIsError ? danger : accent);
        g.setFont (font (15.0f, true));
        g.drawFittedText (status, statusArea, juce::Justification::centredLeft, 1);
    }
}

//==============================================================================
DrummerBar::DrummerBar()
{
    nameButton.setLabel ("AI Drummer");
    nameButton.setIconColour (accent);
    nameButton.setIconSize (20.0f);
    nameButton.setFramed (true);
    nameButton.setTooltip ("The AI drummer's conversation, versions and feel (F8)");
    nameButton.onClick = [this]
    {
        if (juce::ModifierKeys::currentModifiers.isPopupMenu() && onSwitchMode != nullptr)
            onSwitchMode();
        else if (onOpen != nullptr)
            onOpen();
    };
    addAndMakeVisible (nameButton);

    prompt.setFont (font (17.0f));
    prompt.setIndents (12, 0);
    prompt.setJustification (juce::Justification::centredLeft);
    prompt.setTextToShowWhenEmpty ("Describe a beat, or what to change", textFaint);
    prompt.onReturnKey = [this] { send(); };
    prompt.onEscapeKey = [this] { if (onDone != nullptr) onDone(); };
    addAndMakeVisible (prompt);

    target.setTooltip ("Who the drummer writes for: a drum kit channel (or a new one)");
    target.onClick = [this] { if (onTargetClicked != nullptr) onTargetClicked(); };
    addAndMakeVisible (target);

    writeButton.setLabel ("Write it");
    writeButton.setFramed (true);
    writeButton.setSolidWhenOn (true);
    writeButton.setToggleState (true, juce::dontSendNotification);
    writeButton.setTooltip ("Send it to the drummer (Enter)");
    writeButton.onClick = [this] { send(); };
    addAndMakeVisible (writeButton);
}

void DrummerBar::setMode (Mode newMode)
{
    mode = newMode;
    const bool bass = mode == Mode::bass, guitar = mode == Mode::guitar, keys = mode == Mode::keys, synth = mode == Mode::synth, master = mode == Mode::master, whole = mode == Mode::song;
    nameButton.setLabel (bass ? "AI Bass" : guitar ? "AI Guitar" : keys ? "AI Keys" : synth ? "AI Synth" : master ? "AI Master" : whole ? "AI Song" : "AI Drummer");
    nameButton.setTooltip (bass ? "The AI bassist's versions and sections (Shift+F8). Right-click for the others"
                         : whole ? "The AI song agent: a whole song from one prompt. Right-click for the others"
                         : master ? "The AI mastering engineer's versions, before and after (Shift+F12). Right-click for the others"
                         : synth ? shortcutText ("The AI synth player's versions and sections (Ctrl+Shift+F8). Right-click for the others")
                         : guitar ? shortcutText ("The AI guitarist's part as tab, versions and sections (Ctrl+F8). Right-click for the others")
                         : keys ? shortcutText ("The AI keys player's versions and sections (Alt+F8). Right-click for the others")
                                : "The AI drummer's conversation, versions and feel (F8). Right-click for the others");
    // Short enough to read whole in a narrow tab: the ideas under it give the examples
    prompt.setTextToShowWhenEmpty (bass ? "Describe a bass line, or what to change"
                                 : guitar ? "Describe a guitar part, or what to change"
                                 : keys ? "Describe a keys part, or what to change"
                                 : synth ? "Describe a synth part, or what to change"
                                 : master ? "Describe the master, or what to change"
                                 : whole ? "Describe a song: its style, mood and players"
                                          : "Describe a beat, or what to change",
                                   textFaint);
    target.setTooltip (bass ? "Who the bassist writes for: a bass channel (or a new one)"
                     : guitar ? "Who the guitarist writes for: a guitar channel (or a new one)"
                     : keys ? "Who the keys player writes for: a keys channel (or a new one)"
                     : synth ? "Who the synth player writes for: a synth channel (or a new one)"
                     : master ? "What the master is for: the loudness it aims at (pick one to master for it)"
                     : whole ? "A song is made in a new project (you're asked to save this one first if it has changes)"
                              : "Who the drummer writes for: a drum kit channel (or a new one)");
    writeButton.setTooltip (bass ? "Send it to the bassist (Enter)" : guitar ? "Send it to the guitarist (Enter)" : keys ? "Send it to the keys player (Enter)"
                                 : synth ? "Send it to the synth player (Enter)"
                                 : master ? "Send it to the mastering engineer (Enter)"
                                 : whole ? "Make the song (Enter)"
                                 : "Send it to the drummer (Enter)");
    prompt.repaint();
    repaint();
}

void DrummerBar::send()
{
    if (prompt.getText().trim().isNotEmpty() && onSend != nullptr)
        onSend (prompt.getText().trim());
}

void DrummerBar::setTarget (const juce::String& label, juce::Colour swatch)
{
    target.setLabel (label);
    target.setSwatch (swatch);
}

void DrummerBar::setText (const juce::String& t)
{
    prompt.setText (t, false);
    prompt.moveCaretToEnd();
}

void DrummerBar::resized()
{
    // Its name only where nothing else says who it talks to (in the AI tab, the list does)
    auto r = getLocalBounds().reduced (0, 6);
    nameButton.setVisible (nameWidth > 0);

    if (nameWidth > 0)
    {
        nameButton.setBounds (r.removeFromLeft (nameWidth));
        r.removeFromLeft (8);
    }

    // Write it as wide as its words; then who it's for, giving way (down to 150) so the prompt keeps 280
    writeButton.setBounds (r.removeFromRight (writeButton.getIdealWidth (r.getHeight())));
    r.removeFromRight (8);
    const int targetWidth = juce::jlimit (150, std::max (150, r.getWidth() - 8 - 280), juce::jlimit (150, 360, target.getIdealWidth()));
    target.setBounds (r.removeFromRight (targetWidth));
    r.removeFromRight (8);
    prompt.setBounds (r);
}

void DrummerBar::paint (juce::Graphics& g)
{
    // A faint glow behind the prompt, so it reads as the place to start
    auto p = prompt.getBounds().toFloat();
    g.setColour (accent.withAlpha (0.16f));
    g.drawRoundedRectangle (p.expanded (2.0f), 9.0f, 2.0f);
}

} // namespace nightshift
