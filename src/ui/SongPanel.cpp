// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "SongPanel.h"
#include "ComputerKeyboard.h"
#include "audio/Kits.h"
#include "audio/Instruments.h"
#include "audio/GuitarSound.h"
#include "ai/Request.h"

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

    void styleAsPrimary (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, accent);
        b.setColour (juce::TextButton::textColourOffId, background);
    }
}

SongPanel::SongPanel (Song& s)
    : song (s)
{
    playButton.setButtonText ("Play from the top");
    styleAsPrimary (playButton);
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

    // The vibe
    listenButton.setButtonText ("Listen to a song...");
    listenButton.setTooltip ("Pick a song (MP3, WAV, AIFF, FLAC or Ogg) for the band to hear its vibe and write a new song like it. "
                             "It listens on this computer: nothing's sent anywhere. You can drop a song file here, too");
    listenButton.onClick = [this] { chooseSongToListenTo(); };
    listenButton.setWantsKeyboardFocus (false);
    addChildComponent (listenButton);

    writeButton.setButtonText ("Write a song with this vibe");
    styleAsPrimary (writeButton);
    writeButton.setTooltip ("Starts a new project and has the band write and play a new song with this vibe: its own parts, never a copy");
    writeButton.onClick = [this] { if (heard != nullptr && plan.words.isNotEmpty() && onMake != nullptr) onMake (plan.words); };
    writeButton.setWantsKeyboardFocus (false);
    addChildComponent (writeButton);

    anotherButton.setButtonText ("Listen to another...");
    anotherButton.setTooltip ("Pick another song to hear");
    anotherButton.onClick = [this] { chooseSongToListenTo(); };
    anotherButton.setWantsKeyboardFocus (false);
    addChildComponent (anotherButton);

    stopButton.setButtonText ("Stop");
    stopButton.setTooltip ("Stops listening");
    stopButton.onClick = [this] { if (stopListening != nullptr) stopListening->store (true); };
    stopButton.setWantsKeyboardFocus (false);
    addChildComponent (stopButton);

    closeness.setTooltip ("How close the new song keeps to it. Close: its tempo, key, chords and form as heard. Some changes: the tempo "
                          "moves a little, a chord or two turns to a neighbour. Loosely: the key may change too, and the form");
    closeness.onChange = [this] (int) { updatePlan(); resized(); repaint(); };
    addChildComponent (closeness);

    song.addListener (this);
    setSize (900, 360);
    refresh();
}

SongPanel::~SongPanel()
{
    if (stopListening != nullptr)
        stopListening->store (true);

    song.removeListener (this);
}

SongPanel::Mode SongPanel::mode() const
{
    if (working)                        return Mode::working;
    if (listening)                      return Mode::listening;
    if (heard != nullptr)               return Mode::heard;
    if (song.songReply.isEmpty())       return Mode::intro;
    return Mode::made;
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
    if (stopListening != nullptr)
        stopListening->store (true);

    working = true;
    listening = false;
    asked = words;
    steps.clear();
    status = {};
    heard.reset();
    playAs.reset();
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

//==============================================================================
void SongPanel::chooseSongToListenTo()
{
    if (working || listening)
        return;

    chooser = std::make_unique<juce::FileChooser> ("Pick a song for the band to hear",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory), vibes::fileTypes());
    juce::Component::SafePointer<SongPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, dialog = ComputerKeyboard::openSystemDialog()] (const juce::FileChooser& fc)
    {
        if (safe != nullptr && fc.getResult().existsAsFile())
            safe->listenTo (fc.getResult());
    });
}

bool SongPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (working || listening || files.size() != 1)
        return false;

    return juce::StringArray::fromTokens (vibes::fileTypes(), ";", {}).contains ("*" + juce::File (files[0]).getFileExtension().toLowerCase());
}

void SongPanel::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty())
        listenTo (juce::File (files[0]));
}

void SongPanel::listenTo (const juce::File& file)
{
    if (working || listening)
        return;

    listening = true;
    listeningTo = file.getFileNameWithoutExtension();
    status = {};
    heard.reset();
    playAs.reset();
    listenProgress = std::make_shared<std::atomic<float>> (0.0f);
    stopListening = std::make_shared<std::atomic<bool>> (false);
    startTimerHz (8);
    refresh();

    // On a thread of its own (a few seconds for a whole song), back here with what it heard
    juce::Component::SafePointer<SongPanel> safe (this);
    juce::Thread::launch ([safe, file, progress = listenProgress, stop = stopListening]
    {
        auto v = std::make_shared<vibes::Vibe> (vibes::listen (file, [progress] (float p) { progress->store (p); }, [stop] { return stop->load(); }));

        juce::MessageManager::callAsync ([safe, v, stop]
        {
            if (safe == nullptr)
                return;

            // (A song started in the meantime stopped it: what it heard is dropped)
            if (stop->load())
            {
                if (! safe->working)
                {
                    safe->listening = false;
                    safe->stopTimer();
                    safe->status = {};
                    safe->refresh();
                }

                return;
            }

            safe->listening = false;
            safe->stopTimer();
            safe->heardIt (*v);
        });
    });
}

void SongPanel::heardIt (const vibes::Vibe& v)
{
    if (! v.ok())
    {
        status = v.problem;
        refresh();
        return;
    }

    showHeard (v);
}

void SongPanel::showHeard (const vibes::Vibe& v)
{
    listening = false;
    heard = std::make_unique<vibes::Vibe> (v);
    status = {};
    vibeSeed = juce::Random::getSystemRandom().nextInt64();

    // Its nearest three of the band's styles, the nearest picked
    playAsIds.clear();
    juce::StringArray names;

    for (auto& g : v.styles)
        if (playAsIds.size() < 3)
            if (const auto* style = agents::findStyle (g.style))
            {
                playAsIds.add (g.style);
                names.add (style->name);
            }

    playAs.reset();

    if (! names.isEmpty())
    {
        playAs = std::make_unique<SegmentedControl> (names, SegmentedControl::Style::pill);
        playAs->setTooltip ("The band's style to play it as: the nearest it heard first");
        playAs->onChange = [this] (int) { updatePlan(); resized(); repaint(); };
        addAndMakeVisible (*playAs);
    }

    closeness.setSelected (0);
    updatePlan();
    refresh();
}

void SongPanel::showListening (const juce::String& name, float done)
{
    listening = true;
    listeningTo = name;
    heard.reset();
    playAs.reset();
    listenProgress = std::make_shared<std::atomic<float>> (done);
    stopListening = std::make_shared<std::atomic<bool>> (false);
    refresh();
}

juce::String SongPanel::aboutPlan() const
{
    // What the new song keeps and what it changes
    juce::String about = "The new song keeps " + (plan.kept.isEmpty() ? juce::String ("its vibe") : plan.kept.joinIntoString (", "));

    if (! plan.changed.isEmpty())
        about << "; it changes " << plan.changed.joinIntoString (", ");

    return about + ". The band writes its own parts: it won't be a copy.";
}

void SongPanel::updatePlan()
{
    if (heard == nullptr)
    {
        plan = {};
        return;
    }

    static const float closenessOf[] { 1.0f, 0.6f, 0.25f };
    const auto style = playAs != nullptr ? playAsIds[playAs->getSelected()] : juce::String();
    plan = vibes::planFrom (*heard, closenessOf[juce::jlimit (0, 2, closeness.getSelected())], vibeSeed, style);
}

void SongPanel::timerCallback()
{
    if (working && currentStatus != nullptr)
    {
        status = currentStatus();
        repaint();
    }
    else if (listening)
        repaint();
}

void SongPanel::songChanged (Song::Change c)
{
    if (! working && (c == Song::Change::everything || c == Song::Change::channels))
        refresh();
}

void SongPanel::refresh()
{
    const auto m = mode();
    const bool made = m == Mode::made;

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
    listenButton.setVisible (m == Mode::intro || m == Mode::made);
    writeButton.setVisible (m == Mode::heard);
    anotherButton.setVisible (m == Mode::heard);
    closeness.setVisible (m == Mode::heard);
    stopButton.setVisible (m == Mode::listening);

    if (playAs != nullptr)
        playAs->setVisible (m == Mode::heard);

    for (auto* chip : ideaChips)
        chip->setVisible (m == Mode::intro);

    resized();
    repaint();
}

void SongPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 10);
    statusArea = r.removeFromBottom (22);
    const auto m = mode();

    if (m == Mode::intro || m == Mode::working || m == Mode::listening)
    {
        introArea = r.withSizeKeepingCentre (std::min (860, r.getWidth()), std::min (360, r.getHeight()));
        auto ideasArea = introArea.withTrimmedTop (104).withTrimmedBottom (48);
        const int columns = ideasArea.getWidth() >= 760 ? 3 : 2, rowHeight = 60;
        const int w = (ideasArea.getWidth() - (columns - 1) * 8) / columns;

        for (int i = 0; i < ideaChips.size(); ++i)
        {
            const int row = i / columns, col = i % columns;
            auto b = juce::Rectangle<int> (ideasArea.getX() + col * (w + 8), ideasArea.getY() + row * rowHeight, w, 52);
            ideaChips[i]->setVisible (m == Mode::intro && b.getBottom() <= ideasArea.getBottom());
            ideaChips[i]->setBounds (b);
        }

        // Under the ideas: or a song to go by
        listenButton.setBounds (introArea.getX(), introArea.getBottom() - 36, 220, 36);
        stopButton.setBounds (introArea.getX(), introArea.getY() + 104, 120, 36);
        return;
    }

    if (m == Mode::heard)
    {
        // What it heard; play it as; how close; what the new song keeps and changes (when there's room: else it's
        // Write's tooltip); then write it (beside the choices when even that's short of room)
        introArea = r.withSizeKeepingCentre (std::min (860, r.getWidth()), std::min (380, r.getHeight()));
        auto t = introArea;
        t.removeFromTop (32);

        const int summaryHeight = std::min (110, (int) std::ceil (layoutOf (heard->summary(), font (16.0f), text, t.getWidth()).getHeight()) + 4);
        const int planHeight = (int) std::ceil (layoutOf (aboutPlan(), font (16.0f), textDim, t.getWidth()).getHeight()) + 4;
        const int choices = 32 + 8 + 32;
        const bool roomForPlan = t.getHeight() >= summaryHeight + 10 + choices + 8 + planHeight + 10 + 36;
        const bool buttonsBeside = t.getHeight() < summaryHeight + 10 + choices + 10 + 36;

        summaryArea = t.removeFromTop (summaryHeight);
        t.removeFromTop (10);

        const int buttonsWidth = buttonsBeside ? std::min (250, t.getWidth() / 3) : 0;
        auto row = t.removeFromTop (32);
        auto besideFirst = row.removeFromRight (buttonsWidth);
        playAsLabel = row.removeFromLeft (112);

        if (playAs != nullptr)
            playAs->setBounds (row.withWidth (std::min (row.getWidth(), playAs->getIdealWidth())));

        t.removeFromTop (8);
        row = t.removeFromTop (32);
        auto besideSecond = row.removeFromRight (buttonsWidth);
        closenessLabel = row.removeFromLeft (112);
        closeness.setBounds (row.withWidth (std::min (row.getWidth(), closeness.getIdealWidth())));

        planArea = {};

        if (roomForPlan)
        {
            t.removeFromTop (8);
            planArea = t.removeFromTop (planHeight);
        }

        writeButton.setTooltip ("Starts a new project and has the band write and play a new song with this vibe. " + aboutPlan());

        if (buttonsBeside)
        {
            writeButton.setBounds (besideFirst);
            anotherButton.setBounds (besideSecond);
            return;
        }

        t.removeFromTop (10);
        auto buttons = t.removeFromTop (36);
        writeButton.setBounds (buttons.removeFromLeft (std::min (260, buttons.getWidth() / 2)));
        buttons.removeFromLeft (10);
        anotherButton.setBounds (buttons.removeFromLeft (std::min (200, buttons.getWidth())));
        return;
    }

    // What it made on the left; Play and the players down the right, Listen to a song at the bottom
    sideArea = r.removeFromRight (std::min (sideWidth, r.getWidth() * 2 / 5));
    r.removeFromRight (16);
    textArea = r;

    auto s = sideArea;
    listenButton.setBounds (s.removeFromBottom (32));
    s.removeFromBottom (8);
    playButton.setBounds (s.removeFromTop (36));
    s.removeFromTop (14);
    s.removeFromTop (22);   // "Who played"
    const int lowest = s.getBottom();

    for (auto* chip : playerChips)
    {
        chip->setBounds (s.removeFromTop (30));
        chip->setVisible (chip->getBottom() <= lowest);
        s.removeFromTop (6);
    }
}

void SongPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    const auto m = mode();

    if (m == Mode::working)
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
    else if (m == Mode::listening)
    {
        auto t = introArea;
        g.setColour (text);
        g.setFont (font (21.0f, true));
        g.drawText ("Listening to \"" + listeningTo + "\"...", t.removeFromTop (30), juce::Justification::centredLeft, true);
        g.setColour (textDim);
        g.setFont (font (16.0f));
        g.drawText ("Its tempo and beat, key and chords, where it changes, its drums and its sound", t.removeFromTop (26), juce::Justification::centredLeft, true);
        t.removeFromTop (14);

        // How far
        const float done = listenProgress != nullptr ? juce::jlimit (0.0f, 1.0f, listenProgress->load()) : 0.0f;
        auto bar = t.removeFromTop (8).withWidth (std::min (420, t.getWidth()));
        g.setColour (hairline);
        g.fillRoundedRectangle (bar.toFloat(), 4.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (bar.toFloat().withWidth (bar.getWidth() * done), 4.0f);
        g.setColour (textDim);
        g.drawText (juce::String (juce::roundToInt (done * 100.0f)) + "%", bar.withX (bar.getRight() + 12).withWidth (60).withSizeKeepingCentre (60, 22),
                    juce::Justification::centredLeft, false);
    }
    else if (m == Mode::heard)
    {
        auto t = introArea;
        g.setColour (text);
        g.setFont (font (21.0f, true));
        g.drawText ("Heard \"" + heard->name + "\"", t.removeFromTop (30), juce::Justification::centredLeft, true);

        layoutOf (heard->summary(), font (16.0f), text, summaryArea.getWidth()).draw (g, summaryArea.toFloat());

        g.setColour (textDim);
        g.setFont (font (16.0f));
        g.drawText ("Play it as", playAsLabel, juce::Justification::centredLeft, false);
        g.drawText ("How close", closenessLabel, juce::Justification::centredLeft, false);

        if (! planArea.isEmpty())
            layoutOf (aboutPlan(), font (16.0f), textDim, planArea.getWidth()).draw (g, planArea.toFloat());
    }
    else if (m == Mode::intro)
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

        g.setColour (textDim);
        g.setFont (font (16.0f));
        g.drawText ("Or give it a song to go by: the band hears its vibe and writes a new song like it",
                    juce::Rectangle<int> (listenButton.getRight() + 12, listenButton.getY(), introArea.getRight() - listenButton.getRight() - 12, listenButton.getHeight()),
                    juce::Justification::centredLeft, true);
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
    else if (m == Mode::listening || m == Mode::heard)
    {
        g.setColour (textFaint);
        g.setFont (font (16.0f));
        g.drawText ("Heard offline, on this computer: the song isn't sent anywhere", statusArea, juce::Justification::centredLeft, true);
    }
    else if (m == Mode::made)
    {
        g.setColour (textFaint);
        g.setFont (font (16.0f));
        g.drawText ("Made offline, on this computer. Describe another in the bar to start a new song", statusArea, juce::Justification::centredLeft, true);
    }
}

} // namespace nightshift
