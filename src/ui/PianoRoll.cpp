// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "PianoRoll.h"
#include "Theme.h"
#include "audio/Kits.h"

namespace nightshift
{

using namespace theme;

namespace
{
    bool isBlackKey (int key)
    {
        const int n = ((key % 12) + 12) % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    // The keyboard's own greys: white keys raised, black keys sunk
    const juce::Colour whiteKey { 0xff2b3141 }, blackKey { 0xff12151c };
}

PianoRoll::PianoRoll (Song& s, AudioEngine& a)
    : song (s), audio (a)
{
    for (auto* bar : { &hScroll, &vScroll })
    {
        bar->addListener (this);
        bar->setAutoHide (false);
        addAndMakeVisible (*bar);
    }

    setWantsKeyboardFocus (true);
    song.addListener (this);
    followSelection();
    updateDrums();
    centreOnNotes();
}

PianoRoll::~PianoRoll()
{
    song.removeListener (this);
}

juce::String PianoRoll::getTitle() const
{
    auto c = channel();
    return "Piano roll - " + (c != nullptr ? c->name : juce::String ("no channel")) + " - " + song.currentPattern().name;
}

void PianoRoll::songChanged (Song::Change c)
{
    if (c == Song::Change::selection || c == Song::Change::currentPattern || c == Song::Change::everything || c == Song::Change::channels)
    {
        const int before = editId;

        if (c == Song::Change::everything)
            editId = 0;   // a loaded song's ids mean nothing next to the last one's

        followSelection();
        updateDrums();

        // Selecting an audio track leaves the piano roll as it was
        if (c != Song::Change::selection || editId != before)
        {
            chosen.clear();

            if (c != Song::Change::channels)
                centreOnNotes();

            if (onTitleChanged != nullptr)
                onTitleChanged();
        }
    }

    // Notes changed from elsewhere (an AI player rewriting the pattern, say): the selection's
    // places in the list mean nothing now
    if (c == Song::Change::notes && ! ownChange)
        chosen.clear();

    updateScrollBars();
    repaint();
}

void PianoRoll::changed()
{
    const juce::ScopedValueSetter<bool> mine (ownChange, true);
    song.notify (Song::Change::notes);
}

void PianoRoll::sortNotes()
{
    // In time order, as the rest of NightShift keeps them, the selection following its notes
    auto& list = notes();
    std::vector<std::pair<Note, bool>> tagged;

    for (int i = 0; i < (int) list.size(); ++i)
        tagged.emplace_back (list[(size_t) i], chosen.count (i) > 0);

    std::stable_sort (tagged.begin(), tagged.end(), [] (auto& a, auto& b) { return a.first.start < b.first.start; });
    chosen.clear();

    for (int i = 0; i < (int) tagged.size(); ++i)
    {
        list[(size_t) i] = tagged[(size_t) i].first;

        if (tagged[(size_t) i].second)
            chosen.insert (i);
    }
}

void PianoRoll::eraseNote (int index)
{
    auto& list = notes();
    list.erase (list.begin() + index);
    std::set<int> shifted;

    for (int i : chosen)
        if (i != index)
            shifted.insert (i > index ? i - 1 : i);

    chosen = shifted;
}

void PianoRoll::removeChosen()
{
    auto& list = notes();

    for (auto it = chosen.rbegin(); it != chosen.rend(); ++it)
        if (*it < (int) list.size())
            list.erase (list.begin() + *it);

    chosen.clear();
}

void PianoRoll::copyChosen()
{
    auto& list = notes();
    int from = std::numeric_limits<int>::max(), to = 0;

    for (int i : chosen)
    {
        from = std::min (from, list[(size_t) i].start);
        to = std::max (to, list[(size_t) i].end());
    }

    auto& copied = clipboard::notes;
    copied.anchor = snapDown (from);
    copied.span = clipboard::followDistance (from, to, song.barTicksOf (song.currentPattern()), song.beatTicksOf (song.currentPattern()));
    copied.notes.clear();

    for (int i : chosen)
    {
        auto n = list[(size_t) i];
        n.start -= copied.anchor;
        copied.notes.push_back (n);
    }
}

juce::String PianoRoll::gridName (int ticks) const
{
    if (ticks == song.barTicksOf (song.currentPattern()))        return "the bar";
    if (ticks == song.beatTicksOf (song.currentPattern()))  return "the beat";
    if (ticks == ticksPerStep)              return "1/16";
    if (ticks == ticksPerStep / 2)          return "1/32";
    return juce::String (ticks) + " ticks";
}

bool PianoRoll::canEdit (EditCommand c) const
{
    auto& list = const_cast<PianoRoll*> (this)->notes();

    switch (c)
    {
        case EditCommand::selectAll:  return ! list.empty() && chosen.size() < list.size();
        case EditCommand::paste:      return channel() != nullptr && ! clipboard::notes.notes.empty();
        case EditCommand::quantize:   return ! list.empty();
        case EditCommand::split:      return false;
        case EditCommand::deselect:
        case EditCommand::copy:
        case EditCommand::cut:
        case EditCommand::duplicate:
        case EditCommand::remove:
        case EditCommand::up:
        case EditCommand::down:
        case EditCommand::octaveUp:
        case EditCommand::octaveDown:
        case EditCommand::left:
        case EditCommand::right:      return ! chosen.empty();
    }

    return false;
}

bool PianoRoll::edit (EditCommand c, std::optional<int> pasteAt)
{
    // (should the notes ever change unannounced, the selection keeps only places that exist)
    auto& list = notes();
    std::erase_if (chosen, [&list] (int i) { return i >= (int) list.size(); });

    if (! canEdit (c))
        return false;
    const int step = snap > 1 ? snap : ticksPerStep;

    // The selected notes' earliest start, lowest and highest keys, and latest end
    int earliest = std::numeric_limits<int>::max(), latest = 0, lowest = 127, highest = 0;

    for (int i : chosen)
    {
        auto& n = list[(size_t) i];
        earliest = std::min (earliest, n.start);
        latest = std::max (latest, n.end());
        lowest = std::min (lowest, n.key);
        highest = std::max (highest, n.key);
    }

    switch (c)
    {
        case EditCommand::selectAll:
            for (int i = 0; i < (int) list.size(); ++i)
                chosen.insert (i);

            repaint();
            return true;

        case EditCommand::deselect:
            chosen.clear();
            repaint();
            return true;

        case EditCommand::copy:
            copyChosen();
            return true;

        case EditCommand::cut:
            copyChosen();
            song.checkpoint();
            removeChosen();
            changed();
            return true;

        case EditCommand::remove:
            song.checkpoint();
            removeChosen();
            changed();
            return true;

        case EditCommand::paste:
        case EditCommand::duplicate:
        {
            // Paste: where it's asked for, else where the mouse points, else just after the
            // copied notes. Duplicate: a copy of the selection right after it
            std::vector<Note> added;

            if (c == EditCommand::paste)
            {
                const auto& copied = clipboard::notes;
                int at = copied.anchor + copied.span;

                if (pasteAt.has_value())
                    at = *pasteAt;
                else if (const auto mouse = getMouseXYRelative(); isShowing() && gridArea.contains (mouse))
                    at = snapDown (xToTick ((float) mouse.x));

                for (auto n : copied.notes)
                {
                    n.start = std::max (0, n.start + at);
                    added.push_back (n);
                }
            }
            else
            {
                const int distance = clipboard::followDistance (earliest, latest, song.barTicksOf (song.currentPattern()), song.beatTicksOf (song.currentPattern()));

                for (int i : chosen)
                {
                    auto n = list[(size_t) i];
                    n.start += distance;
                    added.push_back (n);
                }
            }

            song.checkpoint();
            chosen.clear();

            for (auto& n : added)
            {
                list.push_back (n);
                chosen.insert ((int) list.size() - 1);
            }

            sortNotes();
            changed();
            return true;
        }

        case EditCommand::quantize:
        {
            // Every selected note's start onto the snap (all of them when none are selected);
            // lengths stay as they are
            const int grid = step;
            int count = 0, moved = 0;
            song.checkpoint();

            for (int i = 0; i < (int) list.size(); ++i)
            {
                if (! chosen.empty() && chosen.count (i) == 0)
                    continue;

                auto& n = list[(size_t) i];
                const int start = (int) std::round (n.start / (double) grid) * grid;
                ++count;

                if (start != n.start)
                {
                    n.start = start;
                    ++moved;
                }
            }

            sortNotes();
            changed();

            if (onMessage != nullptr)
                onMessage ("Quantized " + juce::String (count) + (count == 1 ? " note" : " notes") + " to " + gridName (grid)
                           + (moved == 0 ? juce::String (": all were on it already") : ": " + juce::String (moved) + " moved"));
            return true;
        }

        case EditCommand::up:
        case EditCommand::down:
        case EditCommand::octaveUp:
        case EditCommand::octaveDown:
        {
            const int by = c == EditCommand::up ? 1 : c == EditCommand::down ? -1 : c == EditCommand::octaveUp ? 12 : -12;

            if (lowest + by < 0 || highest + by > 127)
                return false;

            song.checkpoint();

            for (int i : chosen)
                list[(size_t) i].key += by;

            lastPreviewKey = -1;
            preview (list[(size_t) *chosen.begin()].key, list[(size_t) *chosen.begin()].velocity);
            changed();
            return true;
        }

        case EditCommand::left:
        case EditCommand::right:
        {
            const int by = c == EditCommand::right ? step : -std::min (step, earliest);

            if (by == 0)
                return false;

            song.checkpoint();

            for (int i : chosen)
                list[(size_t) i].start += by;

            sortNotes();
            changed();
            return true;
        }

        case EditCommand::split:
            break;
    }

    return false;
}

juce::PopupMenu PianoRoll::createEditMenu()
{
    juce::PopupMenu menu;

    auto add = [this, &menu] (EditCommand c, const juce::String& name)
    {
        juce::PopupMenu::Item item (name);
        item.isEnabled = canEdit (c);
        item.shortcutKeyDescription = editCommandKey (c);
        item.action = [this, c] { edit (c); };
        menu.addItem (std::move (item));
    };

    const int picked = (int) chosen.size();
    menu.addSectionHeader (picked == 0 ? shortcutText ("No notes selected: drag a box with Select, or Ctrl+drag")
                                       : juce::String (picked) + (picked == 1 ? " note selected" : " notes selected"));
    add (EditCommand::selectAll, "Select all");
    add (EditCommand::deselect, "Select none");
    menu.addSeparator();
    add (EditCommand::cut, "Cut");
    add (EditCommand::copy, "Copy");
    add (EditCommand::paste, "Paste");
    add (EditCommand::duplicate, "Duplicate");
    add (EditCommand::remove, "Delete");
    menu.addSeparator();
    add (EditCommand::quantize, picked == 0 ? "Quantize every note" : "Quantize");
    menu.addSeparator();
    add (EditCommand::up, "Up a semitone");
    add (EditCommand::down, "Down a semitone");
    add (EditCommand::octaveUp, "Up an octave");
    add (EditCommand::octaveDown, "Down an octave");
    add (EditCommand::left, "Earlier");
    add (EditCommand::right, "Later");
    return menu;
}

void PianoRoll::followSelection()
{
    auto playsNotes = [this] (int id)
    {
        auto c = song.findChannel (id);
        return c != nullptr && c->type != InstrumentType::audio;
    };

    if (playsNotes (song.selectedChannelId))
    {
        editId = song.selectedChannelId;
        return;
    }

    if (playsNotes (editId))
        return;

    // Nothing to stay on: the first instrument with notes in this pattern, else the first instrument
    editId = 0;
    auto& pattern = song.currentPattern();

    for (auto& c : song.channels)
    {
        if (! playsNotes (c.id))
            continue;

        if (auto list = pattern.findNotes (c.id); list != nullptr && ! list->empty())
        {
            editId = c.id;
            return;
        }

        if (editId == 0)
            editId = c.id;
    }
}

juce::PopupMenu PianoRoll::createChannelMenu()
{
    auto& pattern = song.currentPattern();
    std::vector<juce::PopupMenu::Item> used, unused;

    for (auto& c : song.channels)
    {
        if (c.type == InstrumentType::audio)
            continue;

        auto list = pattern.findNotes (c.id);
        const int count = list != nullptr ? (int) list->size() : 0;

        juce::PopupMenu::Item item (c.name);
        item.colour = c.colour;
        item.isTicked = c.id == editId;
        item.shortcutKeyDescription = count > 0 ? juce::String (count) + (count == 1 ? " note" : " notes") : juce::String();
        item.action = [this, id = c.id]
        {
            song.selectedChannelId = id;
            song.notify (Song::Change::selection);
        };

        (count > 0 ? used : unused).push_back (std::move (item));
    }

    juce::PopupMenu menu;

    if (! used.empty())
        menu.addSectionHeader ("In " + pattern.name);

    for (auto& item : used)
        menu.addItem (std::move (item));

    if (! unused.empty())
        menu.addSectionHeader (used.empty() ? "Nothing in " + pattern.name + " yet" : juce::String ("Other channels"));

    for (auto& item : unused)
        menu.addItem (std::move (item));

    if (menu.getNumItems() == 0)
        menu.addItem ("No instruments yet - add one in the channel rack", false, false, nullptr);

    return menu;
}

std::vector<Note>& PianoRoll::notes()
{
    // With no instrument there's nothing to edit, and no reason to add a list to the pattern
    if (channel() == nullptr)
    {
        noNotes.clear();
        return noNotes;
    }

    return song.currentPattern().notesFor (editId);
}

const Channel* PianoRoll::channel() const
{
    return song.findChannel (editId);
}

void PianoRoll::centreOnNotes()
{
    auto& list = notes();
    int lo = 128, hi = -1;

    for (auto& n : list)
    {
        lo = std::min (lo, n.key);
        hi = std::max (hi, n.key);
    }

    const int rootKey = channel() != nullptr ? channel()->rootKey : defaultRootKey;
    const double visibleKeys = gridArea.getHeight() > 0 ? gridArea.getHeight() / (double) keyHeight : 24.0;

    if (hi < 0)
        topKey = rootKey + visibleKeys * 0.5;
    else if (hi - lo + 4 <= visibleKeys)
        topKey = (hi + lo) * 0.5 + visibleKeys * 0.5;   // the whole melody fits: centre it
    else
        topKey = hi + 3;

    topKey = juce::jlimit (visibleKeys - 1.0, 127.0, std::round (topKey));
    scrollTicks = 0.0;
    updateScrollBars();
    repaint();
}

//==============================================================================
float PianoRoll::tickToX (double tick) const        { return (float) (gridArea.getX() + (tick - scrollTicks) * pxPerTick); }
double PianoRoll::xToTick (float x) const           { return (x - gridArea.getX()) / pxPerTick + scrollTicks; }
float PianoRoll::keyToY (int key) const             { return (float) (gridArea.getY() + (topKey - key) * keyHeight); }
int PianoRoll::yToKey (float y) const               { return juce::jlimit (0, 127, (int) std::ceil (topKey - (y - gridArea.getY()) / keyHeight)); }

int PianoRoll::snapDown (double tick) const
{
    return std::max (0, (int) std::floor (tick / snap) * snap);
}

bool PianoRoll::isNearEnd (juce::Rectangle<float> note, juce::Point<float> p)
{
    // The last 6 px stretch a note, but at most its last third: a short note mostly moves
    return note.getRight() - p.x < std::min (6.0f, note.getWidth() / 3.0f);
}

juce::Rectangle<float> PianoRoll::noteBounds (const Note& n) const
{
    return { tickToX (n.start) + 0.5f, keyToY (n.key) + 1.0f, std::max (4.0f, (float) (n.length * pxPerTick) - 1.0f), (float) keyHeight - 2.0f };
}

int PianoRoll::noteAt (juce::Point<float> p) const
{
    auto& list = const_cast<PianoRoll*> (this)->notes();

    for (int i = (int) list.size(); --i >= 0;)
        if (noteBounds (list[(size_t) i]).contains (p))
            return i;

    return -1;
}

int PianoRoll::velocityNoteAt (juce::Point<float> p) const
{
    auto& list = const_cast<PianoRoll*> (this)->notes();
    int best = -1;
    float bestDistance = 8.0f;

    for (int i = 0; i < (int) list.size(); ++i)
    {
        const float d = std::abs (tickToX (list[(size_t) i].start) + 1.0f - p.x);

        if (d < bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }

    return best;
}

void PianoRoll::preview (int key, int velocity)
{
    if (key != lastPreviewKey)
    {
        audio.previewNote (editId, key, velocity, 260);
        lastPreviewKey = key;
    }
}

//==============================================================================
void PianoRoll::resized()
{
    // Drums get a wider column with each drum named, instead of a keyboard
    const int keysColumn = drumKit != nullptr || drumPlugin ? drumKeysWidth : keysWidth;
    auto r = getLocalBounds();
    hScroll.setBounds (r.removeFromBottom (10).withTrimmedLeft (keysColumn).withTrimmedRight (10));
    vScroll.setBounds (r.removeFromRight (10).withTrimmedTop (rulerHeight));
    velocityArea = r.removeFromBottom (velocityHeight).withTrimmedLeft (keysColumn);
    rulerArea = r.removeFromTop (rulerHeight).withTrimmedLeft (keysColumn);
    keysArea = r.removeFromLeft (keysColumn);
    gridArea = r;

    updateScrollBars();
}

void PianoRoll::updateScrollBars()
{
    const int length = song.patternLength (song.currentPattern());
    const double visibleTicks = gridArea.getWidth() / pxPerTick;
    const double total = std::max ((double) length + song.barTicksOf (song.currentPattern()) * 4, scrollTicks + visibleTicks);
    hScroll.setRangeLimits (0.0, total, juce::dontSendNotification);
    hScroll.setCurrentRange (scrollTicks, visibleTicks, juce::dontSendNotification);

    const double visibleKeys = gridArea.getHeight() / (double) keyHeight;
    vScroll.setRangeLimits (0.0, 128.0, juce::dontSendNotification);
    vScroll.setCurrentRange (127.0 - topKey, visibleKeys, juce::dontSendNotification);
}

void PianoRoll::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hScroll)
        scrollTicks = start;
    else
        topKey = 127.0 - start;

    repaint();
}

//==============================================================================
void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    paintGrid (g);
    paintKeyboard (g);
    paintVelocity (g);

    // Ruler: bar numbers, and the playhead as a diamond
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (rulerArea);
        g.setFont (monoFont (15.0f, true));

        for (int bar = (int) (scrollTicks / song.barTicksOf (song.currentPattern())); ; ++bar)
        {
            const float x = tickToX (bar * song.barTicksOf (song.currentPattern()));

            if (x > rulerArea.getRight())
                break;

            g.setColour (textFaint);
            g.drawText (juce::String (bar + 1), (int) x + 6, rulerArea.getY(), 40, rulerArea.getHeight(), juce::Justification::centredLeft, true);
            g.setColour (hairlineStrong);
            g.fillRect (x, (float) rulerArea.getBottom() - 8.0f, 1.0f, 8.0f);
        }

        if (playheadTicks >= 0.0)
        {
            juce::Path diamond;
            diamond.addRoundedRectangle (-5.0f, -5.0f, 10.0f, 10.0f, 2.0f);
            diamond.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi * 0.25f)
                                        .translated (tickToX (playheadTicks) + 1.0f, (float) rulerArea.getBottom() - 9.0f));
            g.setColour (playhead);
            g.fillPath (diamond);
        }
    }

    // The corner over the keyboard, the velocity lane's label, and the edges
    g.setColour (panel);
    g.fillRect (0, 0, keysArea.getWidth(), rulerHeight);
    g.fillRect (0, velocityArea.getY(), keysArea.getWidth(), velocityArea.getHeight());
    g.setColour (hairline);
    g.fillRect (0, rulerHeight - 1, getWidth(), 1);
    g.fillRect (0, velocityArea.getY(), getWidth(), 1);
    g.fillRect (keysArea.getRight() - 1, 0, 1, getHeight());

    g.setColour (textFaint);
    g.setFont (labelFont());
    g.drawText ("VEL", 0, velocityArea.getY() + 8, keysArea.getWidth(), 14, juce::Justification::centred, true);
}

void PianoRoll::paintGrid (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (gridArea);

    const float gridX = (float) gridArea.getX(), gridW = (float) gridArea.getWidth();
    const int bottomKey = yToKey ((float) gridArea.getBottom());
    const int top = (int) std::ceil (topKey);

    // Rows: black keys a shade darker, a line under each C
    for (int key = bottomKey; key <= top; ++key)
    {
        const float y = keyToY (key);

        if (isBlackKey (key))
        {
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.fillRect (gridX, y, gridW, (float) keyHeight);
        }

        if (key % 12 == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (gridX, y + keyHeight - 1.0f, gridW, 1.0f);
        }
    }

    // Bars, beats, and steps once there's room for them
    for (int s = (int) (scrollTicks / ticksPerStep); ; ++s)
    {
        const int tick = s * ticksPerStep;
        const float x = tickToX (tick);

        if (x > gridArea.getRight())
            break;

        const bool isBar = tick % song.barTicksOf (song.currentPattern()) == 0, isBeat = tick % song.beatTicksOf (song.currentPattern()) == 0;

        if (! isBar && ! isBeat && pxPerTick * ticksPerStep < 6.0)
            continue;

        g.setColour (juce::Colours::white.withAlpha (isBar ? 0.09f : isBeat ? 0.04f : 0.015f));
        g.fillRect (x, (float) gridArea.getY(), 1.0f, (float) gridArea.getHeight());
    }

    // Past the end of the pattern
    const float endX = tickToX (song.patternLength (song.currentPattern()));

    if (endX < gridArea.getRight())
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> (endX, (float) gridArea.getY(), gridArea.getRight() - endX, (float) gridArea.getHeight()));
    }

    // Ghost notes from the pattern's other melodic channels
    auto& pattern = song.currentPattern();

    for (auto& [channelId, list] : pattern.notes)
    {
        auto other = song.findChannel (channelId);

        if (channelId == editId || other == nullptr || steps::isStepSequence (list, other->rootKey, song.barTicksOf (song.currentPattern())))
            continue;

        g.setColour (other->colour.withAlpha (0.45f));

        for (auto& n : list)
            g.drawRoundedRectangle (noteBounds (n).reduced (0.5f), 4.0f, 1.0f);
    }

    // Notes, in the channel's colour, brighter for a harder hit
    const auto colour = channel() != nullptr ? channel()->colour : accent;
    auto& list = notes();

    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto& n = list[(size_t) i];
        auto r = noteBounds (n);
        const float strength = juce::jmap ((float) n.velocity, 1.0f, 127.0f, 0.55f, 1.0f);

        g.setColour (panelLight.interpolatedWith (colour, strength));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRect (r.reduced (3.0f, 0.0f).withHeight (1.0f).translated (0.0f, 1.0f));

        if (chosen.count (i) > 0)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (r.expanded (0.25f), 4.0f, 1.5f);
        }
        else
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        }

        if (r.getWidth() > 34.0f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.setFont (font (15.0f, true));
            const auto drum = drumName (n.key);
            g.drawText (drum.isNotEmpty() ? drum : noteName (n.key), r.reduced (5.0f, 0.0f), juce::Justification::centredLeft, true);
        }
    }

    if (list.empty())
    {
        const auto message = channel() == nullptr
            ? juce::String ("No instruments yet - add one in the channel rack to write notes")
            : "No " + channel()->name + " notes in " + pattern.name + " yet. Click to add some, or pick another channel above.";
        g.setColour (textFaint);
        g.setFont (font (17.0f, true));
        g.drawFittedText (message, gridArea.reduced (24), juce::Justification::centred, 2);
    }

    if (playheadTicks >= 0.0)
    {
        const float x = tickToX (playheadTicks);
        g.setColour (playhead.withAlpha (0.16f));
        g.fillRect (x - 2.5f, (float) gridArea.getY(), 7.0f, (float) gridArea.getHeight());
        g.setColour (playhead);
        g.fillRect (x, (float) gridArea.getY(), 2.0f, (float) gridArea.getHeight());
    }

    // The box being dragged to select notes
    if (drag == Drag::box)
    {
        const auto box = juce::Rectangle<float> (boxStart, boxEnd);
        g.setColour (accentCool.withAlpha (0.12f));
        g.fillRect (box);
        g.setColour (accentCool.withAlpha (0.8f));
        g.drawRect (box, 1.0f);
    }
}

juce::String PianoRoll::drumName (int key) const
{
    if (drumKit != nullptr)
        if (auto piece = drumKit->findPiece (key))
            return piece->name;

    return drumPlugin ? drumkits::generalMidiName (key) : juce::String();
}

void PianoRoll::updateDrums()
{
    // A kit channel names its kit's drums; a drum plugin the drummer writes for, General MIDI's
    auto c = channel();
    std::shared_ptr<const drumkits::Kit> kit;
    const bool plugin = c != nullptr && c->type == InstrumentType::plugin && ! c->drummer.turns.empty();

    if (c != nullptr && c->type == InstrumentType::kit)
        kit = drumkits::load (c->kit);

    if (kit != drumKit || plugin != drumPlugin)
    {
        drumKit = kit;
        drumPlugin = plugin;
        resized();
    }
}

void PianoRoll::paintDrumNames (juce::Graphics& g, int bottomKey, int top)
{
    const float left = (float) keysArea.getX(), width = (float) keysArea.getWidth() - 1.0f;

    for (int key = bottomKey - 1; key <= top + 1; ++key)
    {
        const float rowTop = keyToY (key);
        const auto name = drumName (key);
        g.setColour (key == keyboardKey ? accentCool : (key % 2 == 0 ? whiteKey : whiteKey.darker (0.15f)));
        g.fillRect (left, rowTop, width, (float) keyHeight);

        g.setColour (key == keyboardKey ? background : name.isNotEmpty() ? textDim : textFaint.withAlpha (0.5f));
        g.setFont (font (15.0f, name.isNotEmpty()));
        g.drawText (name.isNotEmpty() ? name : noteName (key), juce::Rectangle<float> (left + 8.0f, rowTop, width - 12.0f, (float) keyHeight),
                    juce::Justification::centredLeft, true);
    }
}

void PianoRoll::paintKeyboard (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (keysArea);

    const int bottomKey = yToKey ((float) keysArea.getBottom());
    const int top = (int) std::ceil (topKey);

    if (drumKit != nullptr || drumPlugin)
    {
        paintDrumNames (g, bottomKey, top);
        return;
    }

    // Dark keys: white keys reach halfway under their black neighbours, like a real
    // keyboard, and black keys are drawn over them at 62% width. Each C is named
    const float left = (float) keysArea.getX(), width = (float) keysArea.getWidth() - 1.0f;

    for (int key = bottomKey - 1; key <= top + 1; ++key)
    {
        if (isBlackKey (key))
            continue;

        const float rowTop = keyToY (key);
        const float upper = isBlackKey (key + 1) ? rowTop - keyHeight * 0.5f : rowTop;
        const float lower = isBlackKey (key - 1) ? rowTop + keyHeight * 1.5f : rowTop + keyHeight;

        g.setColour (key == keyboardKey ? accentCool : whiteKey);
        g.fillRect (left, upper, width, lower - upper);
        g.setColour (background);
        g.fillRect (left, lower - 1.0f, width, 1.0f);

        if (key % 12 == 0)
        {
            // On the lighter white key, the dim grey reads (the faint one is only 4.2 to 1 there)
            g.setColour (key == keyboardKey ? background : textDim);
            g.setFont (font (15.0f, true));
            g.drawText (noteName (key), juce::Rectangle<float> (left, rowTop, width - 5.0f, (float) keyHeight),
                        juce::Justification::centredRight, true);
        }
    }

    for (int key = bottomKey - 1; key <= top + 1; ++key)
    {
        if (! isBlackKey (key))
            continue;

        auto r = juce::Rectangle<float> (left, keyToY (key) + 1.0f, width * 0.62f, keyHeight - 2.0f);
        g.setColour (key == keyboardKey ? accentCool.darker (0.3f) : blackKey);
        g.fillRoundedRectangle (r, 2.5f);
    }
}

void PianoRoll::paintVelocity (juce::Graphics& g)
{
    g.setColour (juce::Colours::black.withAlpha (0.15f));
    g.fillRect (velocityArea);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (velocityArea);

    // A stem with a head per note: the taller, the harder
    const auto colour = channel() != nullptr ? channel()->colour : accent;
    auto& list = notes();
    auto lane = velocityArea.toFloat().reduced (0.0f, 8.0f).withTrimmedTop (4.0f);

    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto& n = list[(size_t) i];
        const float x = tickToX (n.start) + 2.0f;
        const float y = lane.getBottom() - lane.getHeight() * n.velocity / 127.0f;

        g.setColour (chosen.count (i) > 0 ? colour.brighter (0.4f) : colour.withAlpha (0.85f));
        g.fillRoundedRectangle (x - 1.0f, y, 2.0f, lane.getBottom() - y, 1.0f);
        g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
    }
}

//==============================================================================
void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;

    if (gridArea.contains (e.getPosition()))
    {
        const int index = noteAt (p);

        if (index >= 0 && isNearEnd (noteBounds (notes()[(size_t) index]), p))
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else if (index >= 0)
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        else
            setMouseCursor (juce::MouseCursor::NormalCursor);
    }
    else
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    lastPreviewKey = -1;
    const auto p = e.position;

    if (onActivated != nullptr)
        onActivated();

    // (should the notes ever change unannounced, the selection keeps only places that exist)
    std::erase_if (chosen, [size = (int) notes().size()] (int i) { return i >= size; });

    if (keysArea.contains (e.getPosition()))
    {
        drag = Drag::keyboard;
        keyboardKey = yToKey (p.y);
        preview (keyboardKey);
        repaint (keysArea);
        return;
    }

    if (rulerArea.contains (e.getPosition()))
    {
        drag = Drag::scrub;
        return;
    }

    if (channel() == nullptr)
        return;

    if (velocityArea.contains (e.getPosition()))
    {
        dragIndex = velocityNoteAt (p);

        if (dragIndex >= 0)
        {
            // A selected note's stem moves every selected note's velocity by as much
            song.checkpoint();
            drag = Drag::velocity;

            if (chosen.count (dragIndex) == 0)
                chosen = { dragIndex };

            dragOrigin = notes();
            mouseDrag (e);
        }

        return;
    }

    if (! gridArea.contains (e.getPosition()))
        return;

    const int index = noteAt (p);
    auto& list = notes();

    if (e.mods.isPopupMenu() || tool == Tool::erase)
    {
        song.checkpoint();
        drag = Drag::erase;
        mouseDrag (e);
        return;
    }

    // A box over notes selects them: dragged on empty space with the select tool, or with
    // Ctrl (Cmd on a Mac) held with any; Shift adds to what's selected
    if (index < 0 && (tool == Tool::select || e.mods.isCommandDown()))
    {
        drag = Drag::box;
        boxStart = boxEnd = p;
        boxBase = e.mods.isShiftDown() ? chosen : std::set<int>();
        chosen = boxBase;
        repaint();
        return;
    }

    if (index >= 0)
    {
        // Shift+click selects a note, or unselects it, without moving anything
        if (e.mods.isShiftDown())
        {
            if (chosen.erase (index) == 0)
                chosen.insert (index);

            repaint();
            return;
        }

        // A note that isn't selected becomes the selection; a selected one takes the others along
        if (chosen.count (index) == 0)
            chosen = { index };

        song.checkpoint();
        auto& n = list[(size_t) index];
        dragIndex = index;
        drag = isNearEnd (noteBounds (n), p) ? Drag::resize : Drag::move;
        grabOffset = (int) std::round (xToTick (p.x)) - n.start;
        dragOrigin = list;
        preview (n.key, n.velocity);
    }
    else
    {
        song.checkpoint();
        Note n;
        n.start = snapDown (xToTick (p.x));
        n.key = yToKey (p.y);
        n.length = newLength;
        n.velocity = 100;
        list.push_back (n);
        dragIndex = (int) list.size() - 1;
        chosen = { dragIndex };
        drag = Drag::move;
        grabOffset = 0;
        dragOrigin = list;
        preview (n.key);
        changed();
    }

    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    auto& list = notes();
    const auto p = e.position;

    switch (drag)
    {
        case Drag::keyboard:
        {
            const int key = yToKey (p.y);

            if (key != keyboardKey)
            {
                keyboardKey = key;
                preview (key);
                repaint (keysArea);
            }
            break;
        }

        case Drag::move:
        {
            if (! juce::isPositiveAndBelow (dragIndex, (int) list.size()) || dragOrigin.size() != list.size())
                break;

            // The grabbed note lands on the snap and the rest of the selection keeps its
            // distance from it, none before the pattern's start or off the keyboard
            const auto& grabbed = dragOrigin[(size_t) dragIndex];
            int byTicks = snapDown (xToTick (p.x) - grabOffset + snap * 0.5) - grabbed.start;
            int byKeys = yToKey (p.y) - grabbed.key;
            int earliest = std::numeric_limits<int>::max(), lowest = 127, highest = 0;

            for (int i : chosen)
            {
                const auto& o = dragOrigin[(size_t) i];
                earliest = std::min (earliest, o.start);
                lowest = std::min (lowest, o.key);
                highest = std::max (highest, o.key);
            }

            byTicks = std::max (byTicks, -earliest);
            byKeys = juce::jlimit (-lowest, 127 - highest, byKeys);
            bool moved = false;

            for (int i : chosen)
            {
                const auto& o = dragOrigin[(size_t) i];
                auto& n = list[(size_t) i];

                if (n.start != o.start + byTicks || n.key != o.key + byKeys)
                {
                    n.start = o.start + byTicks;
                    n.key = o.key + byKeys;
                    moved = true;
                }
            }

            if (moved)
            {
                preview (list[(size_t) dragIndex].key, list[(size_t) dragIndex].velocity);
                changed();
            }
            break;
        }

        case Drag::resize:
        {
            if (! juce::isPositiveAndBelow (dragIndex, (int) list.size()) || dragOrigin.size() != list.size())
                break;

            // Every selected note stretches by as much as the one grabbed
            const auto& grabbed = dragOrigin[(size_t) dragIndex];
            const int end = std::max (grabbed.start + std::max (1, snap), snapDown (xToTick (p.x) + snap * 0.5));
            const int by = end - grabbed.end();
            bool stretched = false;

            for (int i : chosen)
            {
                const auto& o = dragOrigin[(size_t) i];
                const int length = std::max (std::min (o.length, std::max (1, snap)), o.length + by);

                if (list[(size_t) i].length != length)
                {
                    list[(size_t) i].length = length;
                    stretched = true;
                }
            }

            if (stretched)
                changed();
            break;
        }

        case Drag::erase:
        {
            const int index = noteAt (p);

            if (index >= 0)
            {
                eraseNote (index);
                changed();
            }
            break;
        }

        case Drag::velocity:
        {
            if (! juce::isPositiveAndBelow (dragIndex, (int) list.size()) || dragOrigin.size() != list.size())
                break;

            auto lane = velocityArea.toFloat().reduced (0.0f, 8.0f).withTrimmedTop (4.0f);
            const int velocity = juce::jlimit (1, 127, juce::roundToInt ((lane.getBottom() - p.y) / lane.getHeight() * 127.0f));
            const int by = velocity - dragOrigin[(size_t) dragIndex].velocity;
            bool moved = false;

            for (int i : chosen)
            {
                const int v = juce::jlimit (1, 127, dragOrigin[(size_t) i].velocity + by);

                if (list[(size_t) i].velocity != v)
                {
                    list[(size_t) i].velocity = v;
                    moved = true;
                }
            }

            if (moved)
                changed();
            break;
        }

        case Drag::box:
        {
            boxEnd = p;
            const auto box = juce::Rectangle<float> (boxStart, boxEnd);
            auto picked = boxBase;

            for (int i = 0; i < (int) list.size(); ++i)
                if (noteBounds (list[(size_t) i]).intersects (box))
                    picked.insert (i);

            chosen = picked;
            break;
        }

        case Drag::scrub:
        case Drag::none:
            break;
    }

    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    auto& list = notes();

    if ((drag == Drag::move || drag == Drag::resize) && juce::isPositiveAndBelow (dragIndex, (int) list.size()))
    {
        // New notes take the length of the last one placed or stretched, as in FL
        newLength = list[(size_t) dragIndex].length;
        sortNotes();
    }

    drag = Drag::none;
    dragIndex = -1;
    keyboardKey = -1;
    dragOrigin.clear();
    repaint();
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())     // Cmd on a Mac
    {
        const double anchor = xToTick (e.position.x);
        pxPerTick = juce::jlimit (0.08, 4.0, pxPerTick * (w.deltaY > 0 ? 1.2 : 1.0 / 1.2));
        scrollTicks = std::max (0.0, anchor - (e.position.x - gridArea.getX()) / pxPerTick);
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float delta = std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY;
        scrollTicks = std::max (0.0, scrollTicks - delta * 600.0 / pxPerTick * 0.25);
    }
    else
    {
        const double visibleKeys = gridArea.getHeight() / (double) keyHeight;
        topKey = juce::jlimit (visibleKeys - 1.0, 127.0, topKey + w.deltaY * 12.0);
    }

    updateScrollBars();
    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    // The Edit menu's keys; anything else (and what has nothing to work on) goes on up
    if (auto command = editCommandFor (key))
        return edit (*command);

    return false;
}

void PianoRoll::refreshPlayhead()
{
    double now = -1.0;

    if (audio.isPlaying())
    {
        const double ticks = audio.getPlayheadTicks();

        if (song.mode == PlayMode::pattern)
        {
            now = ticks;
        }
        else
        {
            auto& pattern = song.currentPattern();

            for (auto& clip : song.playlist)
                if (clip.patternId == pattern.id && ticks >= clip.start && ticks < song.clipEnd (clip))
                    now = ticks - clip.start + clip.offset;
        }
    }

    if (now != playheadTicks)
    {
        playheadTicks = now;
        repaint();
    }
}

} // namespace nightshift
