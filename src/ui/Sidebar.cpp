// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Sidebar.h"
#include "ComputerKeyboard.h"
#include "TextAudit.h"
#include "Widgets.h"
#include "audio/DrumKit.h"
#include "audio/Nova.h"
#include "audio/fx/StockEffects.h"
#include "audio/Kits.h"
#include "audio/Instruments.h"
#include "audio/GuitarSound.h"
#include "audio/Packs.h"

namespace nightshift
{

using namespace theme;

namespace
{
    const char* const groupNames[] = { "NightShift kit", "Synth presets", "Plugin instruments", "Effects", "Plugin effects", "Drum kits", "Instruments",
                                       "Samples" };

    // What developersOpen holds for a developer in a group: Plugin instruments' and Plugin effects'
    // are apart (a developer of both opens in one and stays shut in the other)
    juce::String openKey (int group, const juce::String& developer)
    {
        return juce::String (group) + "|" + developer;
    }

    // Samples' rows: pink, beside the kits' orange, the instruments' blue and the effects' teal
    const juce::Colour sampleColour { 0xffef8fc8 };

    // Searching, the most matches shown under each of Samples' folders (the rest are counted)
    constexpr int maxSampleMatches = 60;
}

bool Sidebar::isGroupName (const juce::String& name)
{
    return std::any_of (std::begin (groupNames), std::end (groupNames), [&name] (const char* g) { return name == g; });
}

Sidebar::Sidebar (Song& s, AudioEngine& a)
    : song (s), audio (a)
{
    search.setTextToShowWhenEmpty ("Search sounds", textFaint);
    search.setFont (font (17.0f));
    search.setIndents (32, 0);
    search.setJustification (juce::Justification::centredLeft);
    search.setEscapeAndReturnKeysConsumed (false);
    search.onTextChange = [this]
    {
        soundScroll = 0;

        // Searching looks through Samples' folders too (once a minute at most)
        if (search.getText().trim().isNotEmpty())
            lookThroughSampleFolders (60.0);

        rebuildSounds();
    };
    search.onEscapeKey = [this]
    {
        search.clear();
        rebuildSounds();

        if (auto top = getTopLevelComponent())
            top->grabKeyboardFocus();
    };
    search.onReturnKey = [this]
    {
        if (juce::isPositiveAndBelow (highlightedSound, (int) soundRows.size()))
            addSound (soundRows[(size_t) highlightedSound]);
    };
    search.addKeyListener (&searchKeys);
    addAndMakeVisible (search);

    scanButton.setFilled (false);
    scanButton.setFramed (true);
    scanButton.setIconSize (17.0f);
    scanButton.setLabel (scanButton.getName());
    scanButton.setTooltip ("Finds instruments and effects you've installed since NightShift last looked, and lists them "
                           "under Plugin instruments. Installed one in an unusual folder? Right-click to add that folder");
    scanButton.onClick = [this] { if (onScanInstruments != nullptr) onScanInstruments(); };
    scanButton.onRightClick = [this] { if (onScanPlugins != nullptr) onScanPlugins(); };
    addAndMakeVisible (scanButton);

    // What's in Samples' folders comes in on a thread of its own
    sampleLibrary.onChanged = [this] { rebuildSounds(); };

    song.addListener (this);
    audio.getKnownPlugins().addChangeListener (this);
    rebuildPatterns();
    rebuildSounds();
}

Sidebar::~Sidebar()
{
    stopTimer();
    sampleLibrary.onChanged = nullptr;
    search.removeKeyListener (&searchKeys);
    audio.getKnownPlugins().removeChangeListener (this);
    song.removeListener (this);
}

void Sidebar::songChanged (Song::Change c)
{
    if (c == Song::Change::patterns || c == Song::Change::currentPattern || c == Song::Change::notes || c == Song::Change::everything)
        rebuildPatterns();
}

void Sidebar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildSounds();
}

void Sidebar::rebuildPatterns()
{
    patternRows.clear();

    for (auto& p : song.patterns)
    {
        const int bars = song.patternLength (p) / song.barTicksOf (p);
        patternRows.push_back ({ Kind::pattern, p.name, juce::String (bars) + (bars == 1 ? " bar" : " bars"), p.colour, p.id, {} });
    }

    resized();
    repaint();
}

void Sidebar::rebuildSounds()
{
    soundRows.clear();
    const auto filter = search.getText().trim();
    const bool filtering = filter.isNotEmpty();

    auto addGroup = [&] (int index, std::vector<Row> items)
    {
        std::vector<Row> matches;

        for (auto& item : items)
            if (! filtering || item.name.containsIgnoreCase (filter) || item.kind == Kind::scan)
                matches.push_back (item);

        // Searching: a group with nothing but its action rows (Scan, Get the studio kits...) stays out
        if (filtering && std::none_of (matches.begin(), matches.end(), [] (const Row& m) { return m.kind != Kind::scan; }))
            return;

        int count = 0;

        for (auto& m : matches)
            count += m.kind == Kind::scan ? 0 : 1;

        soundRows.push_back ({ Kind::group, groupNames[index], juce::String (count), {}, index, {} });

        if (filtering || groupOpen[index])
            for (auto& m : matches)
                soundRows.push_back (m);
    };

    std::vector<Row> drumRows, kitRows, synthRows, stockRows, instrumentRows;
    const auto kitNames = kit::getSoundNames();

    // Multi-sampled kits, each a channel with an AI drummer
    for (auto& id : drumkits::ids())
        drumRows.push_back ({ Kind::drumkit, drumkits::displayName (id), "Kit", accent, 0, {}, id });

    // The studio kits until they're here, and a kit of your own
    if (! drumkits::haveDownloads())
        drumRows.push_back ({ Kind::scan, "Get the studio drum kits (10.5 GB)...", {}, accent, getKitsRow, {} });

    drumRows.push_back ({ Kind::scan, "Drum kit from a folder...", {}, accent, importKitRow, {} });

    // Nightshift Guitar's sounds, always there, then the sampled instruments: piano, strings,
    // brass... and the free ones until they're here
    for (auto& name : guitarsound::presetNames())
        instrumentRows.push_back ({ Kind::instrument, "Guitar, " + name, "Guitar", accentCool, 0, {}, guitarsound::pathFor (name) });

    for (auto& e : instruments::list())
        instrumentRows.push_back ({ Kind::instrument, e.name, e.category, accentCool, 0, {}, e.path });

    if (! instruments::haveLibraries())
        instrumentRows.push_back ({ Kind::scan, "Get the piano and orchestra (2.9 GB)...", {}, accentCool, getInstrumentsRow, {} });

    for (int i = 0; i < kitNames.size(); ++i)
        kitRows.push_back ({ Kind::kit, kitNames[i], {}, Song::channelColour (i), 0, {} });

    const auto categories = nova::categories();

    for (auto& p : nova::factory())
        synthRows.push_back ({ Kind::synth, p.name, p.category, Song::channelColour (categories.indexOf (p.category) + 3), 0, {} });

    // Nightshift's own effects, always there
    for (auto& desc : stockfx::descriptions())
        stockRows.push_back ({ Kind::effect, desc.name.fromFirstOccurrenceOf ("Nightshift ", false, false), "Stock", effectColour, 0, desc });

    // Plugin instruments and effects by developer, each developer shut until it's clicked (the user,
    // 2026-11-15: effects, then instruments too). Searching finds a plugin by its own name or its
    // developer's, shown under its developer. The rows needn't say VST3: nearly all are (and a Mac's
    // Audio Unit twins are left out). False when the group has nothing to show
    auto addByDeveloper = [&] (int index, bool instruments)
    {
        const auto kind = instruments ? Kind::plugin : Kind::effect;
        const auto colour = instruments ? accentCool : effectColour;
        const juce::String one (instruments ? " instrument" : " effect"), many (instruments ? " instruments" : " effects");
        std::vector<Row> rows;
        int count = 0;

        for (auto& developer : AudioEngine::pluginsByDeveloper (audio.getKnownPlugins(), instruments))
        {
            const auto name = developer.name.isNotEmpty() ? developer.name : juce::String ("Unknown developer");
            const bool all = filtering && name.containsIgnoreCase (filter);
            std::vector<Row> theirs;

            for (auto& desc : developer.plugins)
                if (! filtering || all || desc.name.containsIgnoreCase (filter))
                    theirs.push_back ({ kind, desc.name, desc.pluginFormatName == "AudioUnit" ? juce::String ("Audio Unit") : juce::String(),
                                        colour, 0, desc, {}, 1 });

            if (theirs.empty())
                continue;

            count += (int) theirs.size();
            const auto given = developer.given.joinIntoString (" / ");
            const auto tip = (given.isNotEmpty() ? given : name) + ": " + juce::String ((int) theirs.size())
                               + (theirs.size() == 1 ? one : many) + ". Click to show or hide them";
            rows.push_back ({ Kind::developer, name, juce::String ((int) theirs.size()), colour, index, {}, developer.name, 1, tip });

            if (filtering || developersOpen.contains (openKey (index, developer.name)))
                rows.insert (rows.end(), theirs.begin(), theirs.end());
        }

        if (count == 0)
            return false;

        soundRows.push_back ({ Kind::group, groupNames[index], juce::String (count), {}, index, {} });

        if (filtering || groupOpen[index])
            soundRows.insert (soundRows.end(), rows.begin(), rows.end());

        return true;
    };

    addGroup (5, drumRows);
    addGroup (6, instrumentRows);

    addGroup (0, kitRows);
    addGroup (1, synthRows);

    // None found yet: the row that looks for them (searching, that group stays out)
    if (! addByDeveloper (2, true))
        addGroup (2, { Row { Kind::scan, "Find instruments you've installed", {}, accentCool, 0, {} } });

    addGroup (3, stockRows);

    // Plugin effects, when there are any (searching: any that match)
    addByDeveloper (4, false);

    // Samples, last: the audio files in the user's folders (searching: those whose names match, under their folder).
    // Its count is every sound found, once its folders have been looked through
    {
        int count = 0;
        bool known = false;
        auto rows = sampleRows (filter, count);

        for (auto& folder : getSampleFolders())
            known = known || sampleLibrary.get (folder) != nullptr;

        const bool stillLooking = std::any_of (rows.begin(), rows.end(), [] (const Row& r) { return r.kind == Kind::note; });

        if (! filtering || count > 0 || stillLooking)
        {
            soundRows.push_back ({ Kind::group, groupNames[samplesGroup], known ? juce::String (count) : juce::String(), {}, samplesGroup, {} });

            if (filtering || groupOpen[samplesGroup])
                soundRows.insert (soundRows.end(), rows.begin(), rows.end());

            // The packs' card on show: it's been offered (kept with NightShift, so it's offered once)
            if (groupOpen[samplesGroup] && ! filtering && std::any_of (rows.begin(), rows.end(), [] (const Row& r) { return r.kind == Kind::card; }))
            {
                packsCardShown = true;

                if (! audio.getOptions().packsOffered)
                {
                    auto options = audio.getOptions();
                    options.packsOffered = true;
                    audio.setOptions (options);
                }
            }
        }
    }

    // While searching, Enter adds the first name that starts with what was typed, or
    // failing that the first match (so "h" finds Hat before the Blonde Bop HR kit)
    highlightedSound = -1;

    if (filtering)
    {
        for (int i = 0; i < (int) soundRows.size(); ++i)
            if (isSound (soundRows[(size_t) i]) && soundRows[(size_t) i].name.startsWithIgnoreCase (filter))
            {
                highlightedSound = i - 1;
                break;
            }

        moveHighlight (1);
    }

    soundScroll = juce::jlimit (0, maxSoundScroll(), soundScroll);
    repaint();
}

void Sidebar::moveHighlight (int delta)
{
    const int count = (int) soundRows.size();

    for (int i = highlightedSound + delta; juce::isPositiveAndBelow (i, count); i += delta)
    {
        if (! isSound (soundRows[(size_t) i]))
            continue;

        highlightedSound = i;

        // Keep it in view
        const int top = i * soundRowHeight, bottom = top + soundRowHeight;

        if (top < soundScroll)
            soundScroll = top;
        else if (bottom > soundScroll + soundsArea.getHeight())
            soundScroll = bottom - soundsArea.getHeight();

        soundScroll = juce::jlimit (0, maxSoundScroll(), soundScroll);
        repaint();
        return;
    }
}

bool Sidebar::SearchKeys::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)
    {
        sidebar.moveHighlight (key == juce::KeyPress::downKey ? 1 : -1);
        return true;
    }

    return false;
}

void Sidebar::focusSearch()
{
    search.grabKeyboardFocus();
    search.selectAll();
}

void Sidebar::startDraggingSound (const Row& row)
{
    auto container = juce::DragAndDropContainer::findParentDragContainerFor (this);

    if (container == nullptr)
        return;

    const auto kind = row.kind == Kind::kit ? "kit:" : row.kind == Kind::synth ? "synth:" : row.kind == Kind::effect ? "effect:"
                    : row.kind == Kind::drumkit ? "drumkit:" : row.kind == Kind::instrument ? "instrument:" : row.kind == Kind::sample ? "sample:" : "plugin:";
    const auto payload = row.kind == Kind::plugin || row.kind == Kind::effect ? row.plugin.createIdentifierString()
                       : row.kind == Kind::drumkit || row.kind == Kind::instrument || row.kind == Kind::sample ? row.key : row.name;

    // The dragged thing: a chip with the sound's colour and name
    const auto f = font (17.0f, true);
    const int w = (int) juce::GlyphArrangement::getStringWidth (f, row.name) + 40, h = 30;
    juce::Image image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (image);
        g.setColour (raised);
        g.fillRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 8.0f);
        g.setColour (row.colour);
        g.fillEllipse (12.0f, h * 0.5f - 4.0f, 8.0f, 8.0f);
        g.setColour (text);
        g.setFont (f);
        g.drawText (row.name, 28, 0, w - 32, h, juce::Justification::centredLeft, true);
    }

    dragging = true;

    if (onSoundDragStarted != nullptr)
        onSoundDragStarted();

    container->startDragging (juce::String (soundDragPrefix) + kind + payload, this, juce::ScaledImage (image));
}

void Sidebar::startDraggingPattern (const Row& row)
{
    auto container = juce::DragAndDropContainer::findParentDragContainerFor (this);

    if (container == nullptr)
        return;

    // The dragged thing: a little clip, in the pattern's colour, with its name and length
    const auto nameFont = font (17.0f, true), detailFont = font (15.0f);
    const int nameWidth = (int) juce::GlyphArrangement::getStringWidth (nameFont, row.name);
    const int detailWidth = (int) juce::GlyphArrangement::getStringWidth (detailFont, row.detail);
    const int w = nameWidth + detailWidth + 48, h = 34;
    juce::Image image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (image);
        const auto r = juce::Rectangle<float> (0.5f, 0.5f, w - 1.0f, h - 1.0f);
        g.setColour (raised.interpolatedWith (row.colour, 0.22f));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (row.colour);
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.5f);
        g.fillRoundedRectangle (8.0f, 8.0f, 4.0f, h - 16.0f, 2.0f);
        g.setColour (text);
        g.setFont (nameFont);
        g.drawText (row.name, 20, 0, nameWidth + 4, h, juce::Justification::centredLeft, true);
        g.setColour (textDim);
        g.setFont (detailFont);
        g.drawText (row.detail, w - detailWidth - 14, 0, detailWidth + 4, h, juce::Justification::centredLeft, true);
    }

    dragging = true;

    if (onPatternDragStarted != nullptr)
        onPatternDragStarted();

    container->startDragging (juce::String (patternDragPrefix) + juce::String (row.id), this, juce::ScaledImage (image));
}

//==============================================================================
void Sidebar::resized()
{
    auto r = getLocalBounds();
    patternsHeader = r.removeFromTop (sectionHeaderHeight);
    addButtonArea = patternsHeader.removeFromRight (40).withSizeKeepingCentre (26, 26);

    const int wanted = (int) patternRows.size() * patternRowHeight + 6;
    patternsArea = r.removeFromTop (std::min (wanted, std::max (patternRowHeight * 2, getHeight() * 2 / 5)));
    patternScroll = juce::jlimit (0, maxPatternScroll(), patternScroll);

    r.removeFromTop (10);
    soundsHeader = r.removeFromTop (sectionHeaderHeight);
    search.setBounds (r.removeFromTop (34).reduced (10, 0));
    r.removeFromTop (8);

    // Scan for VST3 instruments, under the list
    scanButton.setBounds (r.removeFromBottom (footerHeight).reduced (10, 9));
    soundsArea = r.withTrimmedBottom (4);
    soundScroll = juce::jlimit (0, maxSoundScroll(), soundScroll);
}

void Sidebar::setScanning (bool scanning)
{
    scanButton.setEnabled (! scanning);
    scanButton.setLabel (scanning ? juce::String ("Looking for new instruments...") : scanButton.getName());
}

juce::StringArray Sidebar::getShownSounds() const
{
    juce::StringArray names;

    for (auto& row : soundRows)
        names.add (row.name);

    return names;
}

juce::Rectangle<int> Sidebar::showSoundRow (const juce::String& name, const juce::String& inGroup)
{
    // Only from that group's own row to the next group's, when one's named (a developer can be in two)
    int from = 0, to = (int) soundRows.size();

    if (inGroup.isNotEmpty())
    {
        from = -1;

        for (int i = 0; i < (int) soundRows.size(); ++i)
        {
            if (soundRows[(size_t) i].kind != Kind::group)
                continue;

            if (from >= 0)
            {
                to = i;
                break;
            }

            if (soundRows[(size_t) i].name == inGroup)
                from = i;
        }

        if (from < 0)
            return {};
    }

    for (int i = from; i < to; ++i)
    {
        if (soundRows[(size_t) i].name != name)
            continue;

        const int top = i * soundRowHeight;

        if (top < soundScroll)
            soundScroll = top;
        else if (top + soundRowHeight > soundScroll + soundsArea.getHeight())
            soundScroll = top + soundRowHeight - soundsArea.getHeight();

        soundScroll = juce::jlimit (0, maxSoundScroll(), soundScroll);
        repaint();
        return { soundsArea.getX(), soundsArea.getY() + top - soundScroll, soundsArea.getWidth(), soundRowHeight };
    }

    return {};
}

void Sidebar::showPluginInstruments (const juce::StringArray& found)
{
    groupOpen[2] = true;

    // And the developers of the ones just found, or they'd be in there shut
    for (auto& developer : AudioEngine::pluginsByDeveloper (audio.getKnownPlugins(), true))
        for (auto& desc : developer.plugins)
            if (found.contains (desc.name))
                developersOpen.addIfNotAlreadyThere (openKey (2, developer.name));

    rebuildSounds();

    for (int i = 0; i < (int) soundRows.size(); ++i)
        if (soundRows[(size_t) i].kind == Kind::group && soundRows[(size_t) i].id == 2)
            soundScroll = juce::jlimit (0, maxSoundScroll(), i * soundRowHeight);

    repaint();
}

int Sidebar::maxPatternScroll() const
{
    return std::max (0, (int) patternRows.size() * patternRowHeight + 6 - patternsArea.getHeight());
}

int Sidebar::maxSoundScroll() const
{
    return std::max (0, (int) soundRows.size() * soundRowHeight - soundsArea.getHeight());
}

int Sidebar::patternAt (juce::Point<int> p) const
{
    if (! patternsArea.contains (p))
        return -1;

    const int i = (p.y - patternsArea.getY() + patternScroll) / patternRowHeight;
    return juce::isPositiveAndBelow (i, (int) patternRows.size()) ? i : -1;
}

int Sidebar::soundAt (juce::Point<int> p) const
{
    if (! soundsArea.contains (p))
        return -1;

    const int i = (p.y - soundsArea.getY() + soundScroll) / soundRowHeight;
    return juce::isPositiveAndBelow (i, (int) soundRows.size()) ? i : -1;
}

//==============================================================================
void Sidebar::paint (juce::Graphics& g)
{
    auto sectionLabel = [&g] (const juce::String& label, juce::Rectangle<int> area)
    {
        g.setColour (textFaint);
        g.setFont (labelFont());
        g.drawText (label, area.withTrimmedLeft (16).withTrimmedTop (6), juce::Justification::centredLeft, true);
    };

    // Patterns
    sectionLabel ("PATTERNS", patternsHeader);

    {
        auto plus = icons::plusLine();
        plus.applyTransform (juce::AffineTransform::scale (1.6f).translated (addButtonArea.getCentreX() - 8.0f, addButtonArea.getCentreY() - 8.0f + 3.0f));

        if (addHovered)
        {
            g.setColour (raised);
            g.fillRoundedRectangle (addButtonArea.toFloat().translated (0.0f, 3.0f), 6.0f);
        }

        g.setColour (addHovered ? text : textFaint);
        g.strokePath (plus, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (patternsArea);

        for (int i = 0; i < (int) patternRows.size(); ++i)
        {
            const auto& row = patternRows[(size_t) i];
            auto area = juce::Rectangle<int> (patternsArea.getX() + 10, patternsArea.getY() + 3 + i * patternRowHeight - patternScroll,
                                              patternsArea.getWidth() - 20, patternRowHeight - 2);
            const bool current = row.id == song.currentPatternId;

            if (current)
            {
                g.setColour (accentCool.withAlpha (0.12f));
                g.fillRoundedRectangle (area.toFloat(), 8.0f);
                g.setColour (accentCool.withAlpha (0.3f));
                g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 8.0f, 1.0f);
            }
            else if (i == hoveredPattern)
            {
                g.setColour (panelLight);
                g.fillRoundedRectangle (area.toFloat(), 8.0f);
            }

            auto swatch = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ area.getX() + 15.0f, (float) area.getCentreY() });
            g.setColour (row.colour.withAlpha (0.2f));
            g.fillRoundedRectangle (swatch.expanded (3.0f), 5.0f);
            g.setColour (row.colour);
            g.fillRoundedRectangle (swatch, 3.0f);

            auto textArea = area.withTrimmedLeft (32).withTrimmedRight (10);
            g.setColour (textFaint);
            g.setFont (font (16.0f));
            g.drawText (row.detail, textArea, juce::Justification::centredRight, true);

            g.setColour (current ? text : textDim);
            g.setFont (font (17.5f, true));
            g.drawText (row.name, textArea.withTrimmedRight (56), juce::Justification::centredLeft, true);
        }
    }

    // Sounds
    g.setColour (hairline);
    g.fillRect (0, soundsHeader.getY() - 5, getWidth(), 1);
    sectionLabel ("SOUNDS", soundsHeader);

    // The edge between the list and the scan button under it
    g.setColour (hairline);
    g.fillRect (0, scanButton.getY() - 9, getWidth(), 1);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (soundsArea);

    // How far in a row starts: a developer's or a sample folder's chevron a step in for each level, the sounds in
    // them further (past the chevron). Samples' levels are 16 px apart, drawn at most 6 deep
    auto inset = [] (const Row& r)
    {
        const int level = std::min (r.depth, 6);

        switch (r.kind)
        {
            case Kind::group:           return 0;
            case Kind::developer:       return r.depth * 20;
            case Kind::sampleFolder:    return 20 + (level - 1) * 16;
            case Kind::sample:
            case Kind::note:            return 42 + (level - 2) * 16;
            default:                    return 22 + r.depth * 20;
        }
    };

    const bool filtering = search.getText().trim().isNotEmpty();

    for (int i = 0; i < (int) soundRows.size(); ++i)
    {
        const auto& row = soundRows[(size_t) i];
        auto area = juce::Rectangle<int> (soundsArea.getX() + 10, soundsArea.getY() + i * soundRowHeight - soundScroll,
                                          soundsArea.getWidth() - 20, soundRowHeight);

        if (area.getBottom() < soundsArea.getY() || area.getY() > soundsArea.getBottom())
            continue;

        // A line of words under a sample folder (looking through it, nothing in it): nothing to click
        // Samples' card: free sample packs on offer, in its colour
        if (row.kind == Kind::card)
        {
            auto card = area.withTrimmedLeft (20).toFloat().reduced (0.0f, 2.0f);
            g.setColour (sampleColour.withAlpha (i == hoveredSound ? 0.22f : 0.14f));
            g.fillRoundedRectangle (card, 7.0f);
            g.setColour (sampleColour.withAlpha (0.5f));
            g.drawRoundedRectangle (card.reduced (0.5f), 7.0f, 1.0f);

            auto content = card.reduced (10.0f, 0.0f);
            auto spark = icons::sparkle();
            spark.applyTransform (spark.getTransformToScaleToFit (content.removeFromLeft (15.0f).withSizeKeepingCentre (15.0f, 15.0f), true));
            g.setColour (sampleColour);
            g.fillPath (spark);
            content.removeFromLeft (8.0f);
            g.setColour (text);
            g.setFont (font (16.0f, true));
            g.drawText (row.name, content, juce::Justification::centredLeft, true);
            continue;
        }

        if (row.kind == Kind::note)
        {
            g.setColour (textFaint);
            g.setFont (font (15.0f));
            g.drawText (row.name, area.withTrimmedLeft (inset (row)).withTrimmedRight (8), juce::Justification::centredLeft, true);
            continue;
        }

        // A group, a developer in Plugin effects or a folder in Samples (a step further in, its name not bold): a
        // chevron, the name, and how many it holds
        if (row.kind == Kind::group || row.kind == Kind::developer || row.kind == Kind::sampleFolder)
        {
            const bool developer = row.kind != Kind::group;
            const bool open = filtering || (row.kind == Kind::sampleFolder ? sampleFoldersOpen.contains (row.key)
                                            : row.kind == Kind::developer ? developersOpen.contains (openKey (row.id, row.key)) : groupOpen[row.id]);
            const auto line = area.withTrimmedLeft (inset (row));
            auto chevron = open ? icons::chevronDown() : icons::chevronRight();
            chevron.applyTransform (juce::AffineTransform::scale (1.4f).translated ((float) line.getX(), (float) line.getCentreY() - 7.0f));
            g.setColour (textFaint);
            g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour (textFaint);
            g.setFont (font (16.0f));
            const int countWidth = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), row.detail) + 12;
            g.drawText (row.detail, area.withTrimmedRight (6), juce::Justification::centredRight, true);

            std::optional<textaudit::TheirWords> theirs;    // a developer's name, as its plugins give it; a folder's, as the user named it

            if (developer)
                theirs.emplace();

            g.setColour (i == hoveredSound ? text : textDim);
            g.setFont (developer ? font (17.0f) : font (16.5f, true));
            g.drawText (row.name, line.withTrimmedLeft (20).withTrimmedRight (countWidth), juce::Justification::centredLeft, true);
            continue;
        }

        // A sample playing now: lit, with a square to stop it where its dot was
        const bool playing = row.kind == Kind::sample && previewShown != juce::File() && row.key == previewShown.getFullPathName();
        const bool lit = i == hoveredSound || i == highlightedSound || playing;

        if (lit)
        {
            g.setColour (i == highlightedSound ? accentCool.withAlpha (0.14f) : playing ? sampleColour.withAlpha (0.12f) : panelLight);
            g.fillRoundedRectangle (area.toFloat().reduced (0.0f, 1.0f), 7.0f);
        }

        auto content = area.withTrimmedLeft (inset (row)).withTrimmedRight (8);
        g.setColour (row.colour);

        if (row.kind == Kind::scan)
        {
            auto plus = icons::plusLine();
            plus.applyTransform (juce::AffineTransform::scale (1.3f).translated ((float) content.getX() - 3.0f, (float) content.getCentreY() - 6.5f));
            g.strokePath (plus, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (playing)
        {
            g.fillRoundedRectangle ((float) content.getX() - 1.0f, (float) content.getCentreY() - 4.0f, 8.0f, 8.0f, 1.5f);
        }
        else
        {
            g.fillEllipse ((float) content.getX(), (float) content.getCentreY() - 3.0f, 6.0f, 6.0f);
        }

        content.removeFromLeft (16);
        const auto hint = i == highlightedSound ? juce::String ("Enter to add")
                        : playing ? juce::String ("click to stop")
                        : i == hoveredSound && row.kind == Kind::sample ? juce::String ("click to hear")
                        : i == hoveredSound && row.kind != Kind::scan ? juce::String ("double-click or drag")
                                                                      : row.detail;

        g.setColour (textFaint);
        g.setFont (font (15.0f));
        int hintWidth = hint.isEmpty() ? 0 : (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), hint) + 6;

        if (row.kind == Kind::sample && hint == row.detail && hintWidth > content.getWidth() / 2)
        {
            // The folder a sample found by a search is in takes half the row at most, so its name shows
            textaudit::TheirWords folderName;
            hintWidth = content.getWidth() / 2;
            g.drawText (hint, content.withTrimmedLeft (content.getWidth() - hintWidth + 6), juce::Justification::centredRight, true);
        }
        else
        {
            g.drawText (hint, content, juce::Justification::centredRight, true);
        }

        // A plugin's name is its maker's, however long; a sample's the user's
        std::optional<textaudit::TheirWords> theirs;

        if (row.kind == Kind::plugin || (row.kind == Kind::effect && row.depth > 0) || row.kind == Kind::sample)
            theirs.emplace();

        g.setColour (lit ? text : textDim);
        g.setFont (font (17.0f));
        g.drawText (row.name, content.withTrimmedRight (hintWidth), juce::Justification::centredLeft, true);
    }
}

void Sidebar::paintOverChildren (juce::Graphics& g)
{
    // The magnifier, in the room the search box's indent leaves
    auto glass = icons::search();
    glass.applyTransform (juce::AffineTransform::scale (1.5f).translated ((float) search.getX() + 9.0f, (float) search.getBounds().getCentreY() - 7.5f));
    g.setColour (textFaint);
    g.strokePath (glass, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Its shortcut, until something's typed
    if (search.isEmpty())
    {
        g.setFont (font (15.0f, true));
        const auto shortcut = shortcutText ("Ctrl K");
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), shortcut) + 12.0f;
        auto chip = juce::Rectangle<float> (w, 20.0f).withCentre ({ search.getRight() - 8.0f - w * 0.5f, (float) search.getBounds().getCentreY() });
        g.setColour (hairlineStrong);
        g.drawRoundedRectangle (chip, 5.0f, 1.0f);
        g.setColour (textFaint);
        g.drawText (shortcut, chip, juce::Justification::centred, true);
    }
}

void Sidebar::mouseMove (const juce::MouseEvent& e)
{
    const int p = patternAt (e.getPosition()), s = soundAt (e.getPosition());
    const bool add = addButtonArea.translated (0, 3).contains (e.getPosition());

    if (p != hoveredPattern || s != hoveredSound || add != addHovered)
    {
        hoveredPattern = p;
        hoveredSound = s;
        addHovered = add;
        setMouseCursor (p >= 0 || s >= 0 || add ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void Sidebar::mouseExit (const juce::MouseEvent&)
{
    hoveredPattern = hoveredSound = -1;
    addHovered = false;
    repaint();
}

void Sidebar::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int delta = juce::roundToInt (-w.deltaY * 160.0f);

    if (patternsArea.contains (e.getPosition()))
        patternScroll = juce::jlimit (0, maxPatternScroll(), patternScroll + delta);
    else if (soundsArea.contains (e.getPosition()))
        soundScroll = juce::jlimit (0, maxSoundScroll(), soundScroll + delta);

    mouseMove (e);
    repaint();
}

void Sidebar::mouseDown (const juce::MouseEvent& e)
{
    pressedSound = pressedPattern = -1;
    dragging = false;

    if (addButtonArea.translated (0, 3).contains (e.getPosition()))
    {
        if (onNewPattern != nullptr)
            onNewPattern();

        return;
    }

    if (const int i = patternAt (e.getPosition()); i >= 0)
    {
        const int id = patternRows[(size_t) i].id;

        if (song.currentPatternId != id)
        {
            song.currentPatternId = id;
            song.notify (Song::Change::currentPattern);
        }

        if (e.mods.isPopupMenu())
            showPatternMenu (id);
        else
            pressedPattern = i;     // pulled away, it drags onto Arrange

        return;
    }

    if (const int i = soundAt (e.getPosition()); i >= 0)
    {
        const auto row = soundRows[(size_t) i];     // (a copy: rebuilding the rows below replaces them)
        pressedSound = i;

        if (row.kind == Kind::group && row.id == samplesGroup && e.mods.isPopupMenu())
        {
            // Samples' own menu: the free packs, a folder of one's own
            juce::PopupMenu menu;
            menu.addItem ("Free sample packs...", [this] { if (onGetPacks != nullptr) onGetPacks(); });
            menu.addItem ("Add a folder...", [this] { chooseSampleFolder(); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        }
        else if (row.kind == Kind::group)
        {
            groupOpen[row.id] = ! groupOpen[row.id];

            // Samples' folders are looked through once it's open (again if it's been a minute)
            if (row.id == samplesGroup && groupOpen[row.id])
                lookThroughSampleFolders (60.0);

            rebuildSounds();
        }
        else if (row.kind == Kind::card || (row.kind == Kind::scan && row.id == getPacksRow))
        {
            if (onGetPacks != nullptr)
                onGetPacks();
        }
        else if (row.kind == Kind::sampleFolder)
        {
            if (e.mods.isPopupMenu())
            {
                showSampleMenu (row);
                return;
            }

            const bool opening = ! sampleFoldersOpen.contains (row.key);

            if (opening)
                sampleFoldersOpen.add (row.key);
            else
                sampleFoldersOpen.removeString (row.key);

            // A top folder opened is looked through again if it's been a few seconds (a download since)
            if (opening && row.depth == 1)
                sampleLibrary.lookThrough ({ juce::File (row.key) }, 10.0);

            rebuildSounds();
        }
        else if (row.kind == Kind::sample)
        {
            // A click plays it, the next stops it; the second click of a double-click adds it instead
            if (e.mods.isPopupMenu())
                showSampleMenu (row);
            else if (e.getNumberOfClicks() == 1)
                toggleSamplePreview (juce::File (row.key));
        }
        else if (row.kind == Kind::scan && row.id == addSampleFolderRow)
        {
            chooseSampleFolder();
        }
        else if (row.kind == Kind::developer)
        {
            const auto key = openKey (row.id, row.key);

            if (developersOpen.contains (key))
                developersOpen.removeString (key);
            else
                developersOpen.add (key);

            rebuildSounds();
        }
        else if (row.kind == Kind::scan && row.id == getKitsRow)
        {
            if (onGetKits != nullptr)
                onGetKits();
        }
        else if (row.kind == Kind::scan && row.id == importKitRow)
        {
            if (onImportKit != nullptr)
                onImportKit();
        }
        else if (row.kind == Kind::scan && row.id == getInstrumentsRow)
        {
            if (onGetInstruments != nullptr)
                onGetInstruments();
        }
        else if (row.kind == Kind::scan && onScanInstruments != nullptr)
        {
            // Plugin instruments' row when it has none: the same as Find new instruments
            onScanInstruments();
        }
    }
}

void Sidebar::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() <= 5)
        return;

    // Pulling a sound away from its row starts dragging it, to drop into Steps or Arrange; a
    // pattern, to drop on a track in Arrange
    if (juce::isPositiveAndBelow (pressedSound, (int) soundRows.size()) && isSound (soundRows[(size_t) pressedSound]))
        startDraggingSound (soundRows[(size_t) pressedSound]);
    else if (juce::isPositiveAndBelow (pressedPattern, (int) patternRows.size()))
        startDraggingPattern (patternRows[(size_t) pressedPattern]);
}

void Sidebar::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int i = patternAt (e.getPosition()); i >= 0)
    {
        renamePattern (patternRows[(size_t) i].id);
        return;
    }

    if (const int i = soundAt (e.getPosition()); i >= 0)
        addSound (soundRows[(size_t) i]);
}

void Sidebar::addSound (const Row& row)
{
    if (row.kind == Kind::kit && onAddKitSound != nullptr)
        onAddKitSound (row.name);
    else if (row.kind == Kind::synth && onAddSynth != nullptr)
        onAddSynth (row.name);
    else if (row.kind == Kind::plugin && onAddPlugin != nullptr)
        onAddPlugin (row.plugin);
    else if (row.kind == Kind::effect && onAddEffect != nullptr)
        onAddEffect (row.plugin);
    else if (row.kind == Kind::instrument && onAddInstrument != nullptr)
        onAddInstrument (row.key);
    else if (row.kind == Kind::drumkit && onAddDrumKit != nullptr)
        onAddDrumKit (row.key);
    else if (row.kind == Kind::sample && onAddSample != nullptr)
        onAddSample (row.key);
}

juce::String Sidebar::getTooltip()
{
    if (addHovered)
        return "New pattern (F4)";

    if (hoveredPattern >= 0)
        return "Click to edit this pattern, drag it onto a track in Arrange to put it in the song, double-click to rename it, right-click for more";

    if (juce::isPositiveAndBelow (hoveredSound, (int) soundRows.size()))
    {
        const auto& row = soundRows[(size_t) hoveredSound];

        if (row.kind == Kind::developer)
            return row.tip;

        if (row.kind == Kind::sample)
            return juce::File (row.key).getFileName() + ": click to hear it, click again to stop. Drag it onto Arrange for an audio track, "
                   "or into Steps for a sampler channel (or double-click it)";

        if (row.kind == Kind::sampleFolder)
        {
            // NightShift's own folders say whose sounds they are
            juce::String credit;

            for (auto& own : samples::ownFolders())
                if (row.depth == 1 && own.folder.getFullPathName() == row.key)
                    credit = ". " + own.credit;

            return row.tip + (row.detail.isNotEmpty() ? ": " + row.detail + (row.detail == "1" ? " sound" : " sounds") : juce::String()) + credit
                   + ". Click to show or hide what's in it" + (row.depth == 1 && audio.getOptions().sampleFolders.contains (row.key)
                                                                  ? ", right-click to take it out of Samples" : "");
        }

        if (row.kind == Kind::scan && row.id == addSampleFolderRow)
            return "Add a folder of your own sounds to Samples; NightShift keeps it there";

        if (row.kind == Kind::card || (row.kind == Kind::scan && row.id == getPacksRow))
            return "Free sounds to use in your songs: drums, synths and loops made by NightShift, real drum kits, a piano and an orchestra. "
                   "Pick the ones you want; nothing downloads until you say";

        if (row.kind == Kind::effect)
            return "Double-click to put " + row.name + " on the selected channel, or drag it onto a channel in Steps";

        if (row.kind == Kind::drumkit)
            return "Double-click to add the " + row.name + " kit as a channel, with an AI drummer to write for it";

        if (row.kind == Kind::kit || row.kind == Kind::synth || row.kind == Kind::plugin || row.kind == Kind::instrument)
            return "Double-click to add " + row.name + " as a channel";
    }

    return {};
}

//==============================================================================
juce::Array<juce::File> Sidebar::getSampleFolders() const
{
    // Downloads and Music, NightShift's own (the free packs, the drum kits, the piano and orchestra), then the ones added
    // (those that are there: a drive unplugged comes back with it)
    auto folders = samples::standardFolders();

    for (auto& own : samples::ownFolders())
        if (! folders.contains (own.folder))
            folders.add (own.folder);

    for (auto& path : audio.getOptions().sampleFolders)
        if (juce::File::isAbsolutePath (path) && juce::File (path).isDirectory() && ! folders.contains (juce::File (path)))
            folders.add (juce::File (path));

    return folders;
}

void Sidebar::lookThroughSampleFolders (double maxAgeSeconds)
{
    sampleLibrary.lookThrough (getSampleFolders(), maxAgeSeconds);
}

bool Sidebar::lookThroughSamples (int ms)
{
    lookThroughSampleFolders (0.0);
    const bool done = sampleLibrary.waitUntilDone (ms);
    rebuildSounds();
    return done;
}

std::vector<Sidebar::Row> Sidebar::sampleRows (const juce::String& filter, int& count)
{
    std::vector<Row> rows;
    count = 0;

    // The first time it opens with no packs yet, a card at its top offers the free ones (and for the rest of that run)
    if (filter.isEmpty() && ! packs::anyInstalled() && (! audio.getOptions().packsOffered || packsCardShown))
        rows.push_back ({ Kind::card, "Get free sample packs", "free", sampleColour, getPacksRow, {} });

    for (auto& folder : getSampleFolders())
    {
        const auto path = folder.getFullPathName();
        const auto tree = sampleLibrary.get (folder);
        const bool looking = sampleLibrary.isLooking (folder);

        // Searching: the files whose names match, under the folder they're in, each saying where in it
        if (filter.isNotEmpty())
        {
            int total = 0;
            const auto found = sampleLibrary.search (folder, filter, maxSampleMatches, total);

            if (total == 0)
            {
                if (tree == nullptr && looking)
                    rows.push_back ({ Kind::note, "Looking through " + samples::displayName (folder) + "...", {}, sampleColour, samplesGroup, {}, {}, 2 });

                continue;
            }

            count += total;
            rows.push_back ({ Kind::sampleFolder, samples::displayName (folder), juce::String (total), sampleColour, samplesGroup, {}, path, 1, path });

            // (each says the folder it's in, its whole path in its tooltip)
            for (auto& match : found)
            {
                const auto file = folder.getChildFile (match.relative);
                const auto in = file.getParentDirectory() == folder ? file.getFileExtension().substring (1).toLowerCase()
                                                                    : file.getParentDirectory().getFileName();
                rows.push_back ({ Kind::sample, file.getFileNameWithoutExtension(), in, sampleColour, samplesGroup, {}, file.getFullPathName(), 2 });
            }

            if (total > (int) found.size())
                rows.push_back ({ Kind::note, juce::String (total - (int) found.size()) + " more: type more of the name", {}, sampleColour, samplesGroup,
                                  {}, {}, 2 });

            continue;
        }

        // Browsing: the folder, and what's in it when it's open
        count += tree != nullptr ? tree->count : 0;
        rows.push_back ({ Kind::sampleFolder, samples::displayName (folder), tree != nullptr ? juce::String (tree->count) : juce::String(),
                          sampleColour, samplesGroup, {}, path, 1, path });

        if (! sampleFoldersOpen.contains (path))
            continue;

        if (tree != nullptr)
            addFolderRows (rows, folder, tree.get(), 2);
        else
            rows.push_back ({ Kind::note, looking ? juce::String ("Looking through it...") : juce::String ("NightShift can't read this folder"), {},
                              sampleColour, samplesGroup, {}, {}, 2 });
    }

    if (filter.isEmpty())
    {
        rows.push_back ({ Kind::scan, "Add a folder...", {}, sampleColour, addSampleFolderRow, {} });
        rows.push_back ({ Kind::scan, "Free sample packs...", {}, sampleColour, getPacksRow, {} });
    }

    return rows;
}

void Sidebar::packsChanged()
{
    lookThroughSampleFolders (0.0);
    rebuildSounds();
}

void Sidebar::addFolderRows (std::vector<Row>& rows, const juce::File& dir, const samples::Folder* folder, int depth)
{
    if (folder->count == 0)
    {
        rows.push_back ({ Kind::note, "No sounds in it", {}, sampleColour, samplesGroup, {}, {}, depth });
        return;
    }

    // Its folders (each opening in place), then its sounds, each saying what kind of file it is
    for (auto& sub : folder->folders)
    {
        const auto subDir = dir.getChildFile (sub->name);
        const auto path = subDir.getFullPathName();
        rows.push_back ({ Kind::sampleFolder, sub->name, juce::String (sub->count), sampleColour, samplesGroup, {}, path, depth, path });

        if (sampleFoldersOpen.contains (path))
            addFolderRows (rows, subDir, sub.get(), depth + 1);
    }

    for (auto& name : folder->files)
    {
        const auto file = dir.getChildFile (name);
        rows.push_back ({ Kind::sample, file.getFileNameWithoutExtension(), file.getFileExtension().substring (1).toLowerCase(), sampleColour,
                          samplesGroup, {}, file.getFullPathName(), depth });
    }
}

void Sidebar::toggleSamplePreview (const juce::File& file)
{
    // A click plays it through the studio's output; the next stops it (another sample plays instead)
    if (audio.getPreviewing() == file)
        audio.stopPreview();
    else
        audio.previewSample (file);

    previewShown = audio.getPreviewing();

    if (previewShown != juce::File())
        startTimerHz (10);      // to see it end

    repaint();
}

void Sidebar::timerCallback()
{
    const auto now = audio.getPreviewing();

    if (now != previewShown)
    {
        previewShown = now;
        repaint();
    }

    if (now == juce::File())
        stopTimer();
}

void Sidebar::addSampleFolder (const juce::File& dir)
{
    if (! dir.isDirectory())
        return;

    if (! getSampleFolders().contains (dir))
    {
        auto options = audio.getOptions();
        options.sampleFolders.add (dir.getFullPathName());
        audio.setOptions (options);
    }

    // Shown open, as it's looked through
    groupOpen[samplesGroup] = true;
    sampleFoldersOpen.addIfNotAlreadyThere (dir.getFullPathName());
    sampleLibrary.lookThrough ({ dir });
    rebuildSounds();
    showSoundRow (samples::displayName (dir), groupNames[samplesGroup]);
}

void Sidebar::removeSampleFolder (const juce::File& dir)
{
    auto options = audio.getOptions();
    options.sampleFolders.removeString (dir.getFullPathName());
    audio.setOptions (options);
    sampleFoldersOpen.removeString (dir.getFullPathName());
    rebuildSounds();
}

void Sidebar::chooseSampleFolder()
{
    folderChooser = std::make_unique<juce::FileChooser> ("Pick a folder of sounds", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    folderChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                [this, dialog = ComputerKeyboard::openSystemDialog()] (const juce::FileChooser& c)
                                {
                                    if (const auto dir = c.getResult(); dir.isDirectory())
                                        addSampleFolder (dir);
                                });
}

void Sidebar::showSampleMenu (const Row& row)
{
    const juce::File file (row.key);
    juce::PopupMenu menu;

    if (row.kind == Kind::sample)
    {
        menu.addItem (audio.getPreviewing() == file ? "Stop" : "Play", [this, file] { toggleSamplePreview (file); });
        menu.addItem ("Add to Steps as a sampler channel", [this, path = row.key] { if (onAddSample != nullptr) onAddSample (path); });
        menu.addSeparator();
    }

   #if JUCE_MAC
    menu.addItem ("Show in Finder", [file] { file.revealToUser(); });
   #else
    menu.addItem ("Show in Explorer", [file] { file.revealToUser(); });
   #endif

    // A folder added to Samples can go again (it stays where it is)
    if (row.kind == Kind::sampleFolder && row.depth == 1 && audio.getOptions().sampleFolders.contains (row.key))
        menu.addItem ("Remove from Samples", [this, file] { removeSampleFolder (file); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

//==============================================================================
void Sidebar::showPatternMenu (int patternId)
{
    juce::PopupMenu menu;
    menu.addItem ("Rename...", [this, patternId] { renamePattern (patternId); });

    menu.addItem ("Clone", [this, patternId]
    {
        auto source = song.findPattern (patternId);

        if (source == nullptr)
            return;

        song.checkpoint();
        const auto copy = *source;
        auto& added = song.addPattern (copy.name + " 2");
        added.notes = copy.notes;
        added.meterBeats = copy.meterBeats;
        added.meterUnit = copy.meterUnit;
        song.currentPatternId = added.id;
        song.notify (Song::Change::patterns);
        song.notify (Song::Change::currentPattern);
    });

    // Its own time signature (a 7/8 part in a 4/4 song), or the song's
    if (const auto* p = song.findPattern (patternId))
    {
        auto setMeter = [this, patternId] (int beats, int unit)
        {
            if (auto* pattern = song.findPattern (patternId))
            {
                song.checkpoint();
                const bool songs = beats == 0 || (beats == song.meterBeats && unit == song.meterUnit);
                pattern->meterBeats = songs ? 0 : beats;
                pattern->meterUnit = songs ? 0 : unit;
                song.notify (Song::Change::patterns);
                song.notify (Song::Change::transport);
            }
        };

        juce::PopupMenu meters;
        meters.addItem ("The song's (" + song.meterName() + ")", true, p->meterBeats == 0, [setMeter] { setMeter (0, 0); });
        meters.addSeparator();
        int lastUnit = 4;

        for (auto [beats, unit] : Song::meters())
        {
            if (unit != lastUnit)
                meters.addSeparator();

            lastUnit = unit;
            meters.addItem (juce::String (beats) + "/" + juce::String (unit), true, p->meterBeats == beats && p->meterUnit == unit,
                            [setMeter, beats = beats, unit = unit] { setMeter (beats, unit); });
        }

        const auto [beats, unit] = song.meterOf (*p);
        menu.addSubMenu ("Time signature  (" + juce::String (beats) + "/" + juce::String (unit) + ")", meters);
    }

    menu.addItem ("Delete", song.patterns.size() > 1, false, [this, patternId]
    {
        song.checkpoint();
        song.removePattern (patternId);
        song.notify (Song::Change::patterns);
        song.notify (Song::Change::currentPattern);
    });

    menu.addSeparator();
    menu.addItem ("New pattern  (F4)", [this] { if (onNewPattern != nullptr) onNewPattern(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void Sidebar::renamePattern (int patternId)
{
    auto pattern = song.findPattern (patternId);

    if (pattern == nullptr)
        return;

    auto* window = new juce::AlertWindow ("Rename pattern", {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", pattern->name);
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, patternId] (int result)
    {
        const auto name = window->getTextEditorContents ("name").trim();

        if (auto p = song.findPattern (patternId); p != nullptr && result == 1 && name.isNotEmpty())
        {
            song.checkpoint();
            p->name = name;
            song.notify (Song::Change::patterns);
        }
    }), true);
}

} // namespace nightshift
