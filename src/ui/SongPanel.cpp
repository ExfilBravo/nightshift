// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "SongPanel.h"
#include "audio/Kits.h"
#include "audio/Instruments.h"
#include "audio/GuitarSound.h"

namespace nightshift
{

using namespace theme;

namespace
{
    const char* const ideas[] = { "Metalcore song in drop C", "Dark trap beat with bells", "Lazy lofi beat",
                                  "Pop punk song", "Funk with horns", "Jazz trio with a flute" };

    constexpr int sideWidth = 300;

    juce::TextLayout layoutOf (const juce::String& s, juce::Font f, juce::Colour c, int width)
    {
        juce::AttributedString words;
        words.append (s, f, c);
        words.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (words, (float) width);
        return layout;
    }
}

SongPanel::SongPanel (Song& s)
    : song (s)
{
    playButton.setButtonText ("Play from the top");
    playButton.setColour (juce::TextButton::buttonColourId, accent);
    playButton.setColour (juce::TextButton::textColourOffId, background);
    playButton.setTooltip ("Plays the song from its first bar, in Arrange");
    playButton.onClick = [this] { if (onPlay != nullptr) onPlay(); };
    playButton.setWantsKeyboardFocus (false);
    addChildComponent (playButton);

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

SongPanel::~SongPanel()
{
    song.removeListener (this);
}

void SongPanel::ask (const juce::String& words)
{
    if (working || words.trim().isEmpty())
        return;

    if (onMake != nullptr)
        onMake (words.trim());
}

void SongPanel::started (const juce::String& words)
{
    working = true;
    asked = words;
    steps.clear();
    status = {};
    startTimerHz (8);
    refresh();
}

void SongPanel::step (const juce::String& what)
{
    steps.add (what);
    repaint();
}

void SongPanel::finished()
{
    working = false;
    stopTimer();
    status = {};
    refresh();
}

void SongPanel::failed (const juce::String& problem)
{
    working = false;
    stopTimer();
    status = problem;
    refresh();
}

void SongPanel::timerCallback()
{
    if (working && currentStatus != nullptr)
    {
        status = currentStatus();
        repaint();
    }
}

void SongPanel::songChanged (Song::Change c)
{
    if (! working && (c == Song::Change::everything || c == Song::Change::channels))
        refresh();
}

void SongPanel::refresh()
{
    const bool made = ! working && song.songReply.isNotEmpty();

    // A button for each player that played, to open its agent
    playerChips.clear();

    if (made)
    {
        auto add = [this] (const juce::String& label, const juce::String& agent, juce::Colour colour)
        {
            auto* chip = playerChips.add (new Chip (label));
            chip->setOpensMenu (false);
            chip->setSwatch (colour);
            chip->setTooltip ("Open the AI " + agent + " to change its part (or its master)");
            chip->onClick = [this, agent] { if (onOpenAgent != nullptr) onOpenAgent (agent); };
            addAndMakeVisible (chip);
        };

        for (auto& c : song.channels)
        {
            // Each with the sound it's playing
            if (! c.drummer.turns.empty())          add ("Drums: " + (c.type == InstrumentType::kit ? drumkits::displayName (c.kit) : c.name), "drums", c.colour);
            else if (! c.bass.turns.empty())        add ("Bass: " + (c.synthPreset.isNotEmpty() ? c.synthPreset : c.name), "bass", c.colour);
            else if (! c.guitar.turns.empty())      add ("Guitar: " + (isGuitar (c) ? guitarPresetOf (c) : c.name), "guitar", c.colour);
            else if (! c.keys.turns.empty())        add ("Keys: " + (c.type == InstrumentType::instrument ? instruments::displayName (c.instrument) : c.synthPreset), "keys", c.colour);
            else if (! c.synthPlayer.turns.empty()) add ("Synth: " + (c.synthPreset.isNotEmpty() ? c.synthPreset : c.name), "synth", c.colour);
        }

        if (! song.masterTurns.empty())
            add ("Master", "master", accent);
    }

    playButton.setVisible (made);

    for (auto* chip : ideaChips)
        chip->setVisible (! made && ! working);

    resized();
    repaint();
}

void SongPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 10);
    statusArea = r.removeFromBottom (22);

    if (song.songReply.isEmpty() || working)
    {
        introArea = r.withSizeKeepingCentre (std::min (860, r.getWidth()), std::min (320, r.getHeight()));
        auto ideasArea = introArea.withTrimmedTop (104);
        const int columns = ideasArea.getWidth() >= 760 ? 3 : 2, rowHeight = 60;
        const int w = (ideasArea.getWidth() - (columns - 1) * 8) / columns;

        for (int i = 0; i < ideaChips.size(); ++i)
        {
            const int row = i / columns, col = i % columns;
            auto b = juce::Rectangle<int> (ideasArea.getX() + col * (w + 8), ideasArea.getY() + row * rowHeight, w, 52);
            ideaChips[i]->setVisible (! working && song.songReply.isEmpty() && b.getBottom() <= ideasArea.getBottom());
            ideaChips[i]->setBounds (b);
        }

        return;
    }

    // What it made on the left; Play and the players down the right
    sideArea = r.removeFromRight (std::min (sideWidth, r.getWidth() * 2 / 5));
    r.removeFromRight (16);
    textArea = r;

    auto s = sideArea;
    playButton.setBounds (s.removeFromTop (36));
    s.removeFromTop (14);
    s.removeFromTop (22);   // "Who played"

    for (auto* chip : playerChips)
    {
        chip->setBounds (s.removeFromTop (30));
        chip->setVisible (chip->getBottom() <= sideArea.getBottom());
        s.removeFromTop (6);
    }
}

void SongPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    if (working)
    {
        auto t = introArea;
        g.setColour (text);
        g.setFont (font (21.0f, true));
        g.drawText ("Making your song...", t.removeFromTop (30), juce::Justification::centredLeft, true);
        g.setColour (textDim);
        g.setFont (font (16.0f));
        g.drawText ("\"" + asked + "\"", t.removeFromTop (26), juce::Justification::centredLeft, true);
        t.removeFromTop (6);

        for (int i = 0; i < steps.size(); ++i)
        {
            g.setColour (i == steps.size() - 1 ? accent : textDim);
            g.drawText ((i == steps.size() - 1 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb8 ")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93 "))) + steps[i],
                        t.removeFromTop (22), juce::Justification::centredLeft, true);
        }
    }
    else if (song.songReply.isEmpty())
    {
        auto t = introArea;
        g.setColour (text);
        g.setFont (font (21.0f, true));
        g.drawText ("Make a whole song from one prompt", t.removeFromTop (30), juce::Justification::centredLeft, true);
        const auto words = layoutOf ("Type a style and anything about it in the bar above (\"metalcore song in drop C at 150\", \"lofi beat with a flute\", "
                                     "\"funk with horns, no keys\") and press Enter. It starts a new project, the AI drummer, bassist, guitarist, keys "
                                     "and synth play it together, it's laid out two minutes or more in Arrange, and the AI mastering engineer masters it. "
                                     "Or start from one of these:", font (16.0f), textDim, t.getWidth());
        words.draw (g, t.removeFromTop (70).withTrimmedTop (4).toFloat());
    }
    else
    {
        g.setColour (hairline);
        g.fillRect (sideArea.getX() - 8, sideArea.getY(), 1, sideArea.getHeight());

        auto t = textArea;
        g.setColour (textDim);
        g.setFont (font (16.0f, true));
        g.drawText ("You asked: \"" + song.songPrompt + "\"", t.removeFromTop (22), juce::Justification::centredLeft, true);
        t.removeFromTop (6);
        const auto reply = layoutOf (song.songReply, font (16.0f), text, t.getWidth());
        reply.draw (g, t.withHeight (std::min (t.getHeight(), (int) std::ceil (reply.getHeight()) + 2)).toFloat());

        g.setColour (textDim);
        g.setFont (font (16.0f));
        g.drawText ("Who played (open one to change its part)", sideArea.withTrimmedTop (50).withHeight (22), juce::Justification::centredLeft, true);
    }

    if (status.isNotEmpty())
    {
        g.setColour (working ? textDim : danger);
        g.setFont (font (16.0f, ! working));
        g.drawFittedText (status, statusArea, juce::Justification::centredLeft, 1);
    }
    else if (! working && song.songReply.isNotEmpty())
    {
        g.setColour (textFaint);
        g.setFont (font (16.0f));
        g.drawText ("Made offline, on this computer. Describe another in the bar to start a new song", statusArea, juce::Justification::centredLeft, true);
    }
}

} // namespace nightshift
