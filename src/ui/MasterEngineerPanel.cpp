// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "MasterEngineerPanel.h"
#include "ai/Bass.h"
#include "ai/OfflineDrummer.h"
#include "ai/Request.h"

namespace nightshift
{

using namespace theme;

namespace
{
    const char* const ideas[] = { "Loud, for SoundCloud", "Warm, for Spotify", "Punchy and wide",
                                  "Dynamic, for Apple Music", "Bright and crisp", "Big low end, the bass in the middle" };

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

    juce::String signedDb (float v)
    {
        return (v > 0.05f ? "+" : "") + juce::String (v, 1);
    }
}

//==============================================================================
/** What was asked (right) and what the engineer did (left), each answer a version; click one to
    put its settings back. */
class MasterEngineerPanel::Conversation : public juce::Component
{
public:
    explicit Conversation (MasterEngineerPanel& p) : panel (p)  {}

    void update (int width)
    {
        entries.clear();
        int y = 12;
        const int bubbleWidth = std::min (760, (int) (width * 0.8f));
        const auto& turns = panel.song.masterTurns;

        for (int i = 0; i < (int) turns.size(); ++i)
        {
            const auto& turn = turns[(size_t) i];

            juce::AttributedString asked;
            asked.append (turn.prompt.isNotEmpty() ? turn.prompt : juce::String ("Master it"), font (16.0f), text);
            asked.setWordWrap (juce::AttributedString::byWord);
            y = add (asked, true, -1, width, bubbleWidth, y);

            const bool shown = i == panel.song.masterShown;
            juce::AttributedString answer;
            answer.append ("Version " + juce::String (i + 1) + (shown ? "   PLAYING" : "") + "\n", font (15.0f, true), shown ? accent : textDim);
            answer.append (turn.reply, font (16.0f), text);
            answer.setWordWrap (juce::AttributedString::byWord);
            y = add (answer, false, i, width, bubbleWidth, y);
        }

        setSize (width, std::max (y + 8, panel.conversationView.getHeight()));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        for (auto& e : entries)
        {
            auto b = e.bounds.toFloat();
            const bool shown = ! e.mine && e.turn == panel.song.masterShown;
            g.setColour (e.mine ? raised : (shown ? accent.withAlpha (0.08f) : panelLight));
            g.fillRoundedRectangle (b, 10.0f);

            if (! e.mine)
            {
                g.setColour (shown ? accent : accentCool.withAlpha (0.6f));
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

    MasterEngineerPanel& panel;
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
MasterEngineerPanel::MasterEngineerPanel (Song& s, AudioEngine& a)
    : song (s), audio (a)
{
    conversation = std::make_unique<Conversation> (*this);
    conversationView.setViewedComponent (conversation.get(), false);
    conversationView.setScrollBarsShown (true, false);
    conversationView.setScrollBarThickness (8);
    addAndMakeVisible (conversationView);

    masterButton.setColour (juce::TextButton::buttonColourId, accent);
    masterButton.setColour (juce::TextButton::textColourOffId, background);
    masterButton.setTooltip ("Listens to the whole song, then sets Tone, Punch, Width and the loudness the way the song's style is mastered, and says why");
    masterButton.onClick = [this] { ask ({}); };
    masterButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (masterButton);

    for (auto* b : { &previousButton, &nextButton })
    {
        b->setFramed (true);
        addAndMakeVisible (*b);
    }

    previousButton.setTooltip ("The version before");
    nextButton.setTooltip ("The version after");
    previousButton.onClick = [this] { showVersion (song.masterShown - 1); };
    nextButton.onClick = [this] { showVersion (song.masterShown + 1); };

    listenSwitch.setTooltip ("Mastered: as it will be exported. Before: your mix as it was. After: the master turned down to the same "
                             "loudness as Before, so you hear what it changed, not just that it's louder");
    listenSwitch.onChange = [this] (int mode) { setListen (mode); };
    addAndMakeVisible (listenSwitch);

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

MasterEngineerPanel::~MasterEngineerPanel()
{
    *stillWanted = false;
    song.removeListener (this);
    audio.setMasterCompare (0, 0.0f);
}

juce::String MasterEngineerPanel::songStyle() const
{
    // Its own last version's style
    const auto answer = shownAnswer();

    if (const auto style = answer.getProperty ("style", {}).toString(); style.isNotEmpty())
        return style;

    // Else the drummer's the band follows
    const int drumsId = bass::drummerToFollow (song);

    for (auto& c : song.channels)
        if (c.id == drumsId && juce::isPositiveAndBelow (c.drummer.shown, (int) c.drummer.turns.size()))
            if (auto request = drummer::offline::requestOf (c.drummer.turns[(size_t) c.drummer.shown]))
                return request->style;

    return {};
}

juce::var MasterEngineerPanel::shownAnswer() const
{
    if (! juce::isPositiveAndBelow (song.masterShown, (int) song.masterTurns.size()))
        return {};

    return juce::JSON::parse (song.masterTurns[(size_t) song.masterShown].answer);
}

//==============================================================================
void MasterEngineerPanel::ask (const juce::String& words)
{
    if (working)
        return;

    working = true;
    status = "Listening to your song...";
    progress = std::make_shared<std::atomic<float>> (0.0f);
    setListen (0);
    refresh();

    // The mix as it comes into the master, raw - no shaping, no push: a render, here, while the cursor waits
    audio.stop();
    const auto capture = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NightShift master capture.wav");
    juce::MouseCursor::showWaitCursor();
    const bool captured = audio.captureMix (capture, true);
    juce::MouseCursor::hideWaitCursor();

    if (! captured)
    {
        working = false;
        status = "NightShift couldn't play the song through to listen to it.";
        refresh();

        if (onDone != nullptr)
            onDone (false);

        return;
    }

    // A follow-up moves on from the settings as they are (knobs turned since included)
    std::optional<Song::Mastering> previous;

    if (hasTurns())
        previous = song.mastering;

    const auto style = songStyle();
    auto wanted = stillWanted;
    auto shared = progress;
    juce::Component::SafePointer<MasterEngineerPanel> safe (this);
    startTimerHz (10);

    juce::Thread::launch ([wanted, shared, safe, capture, words, style, previous]
    {
        auto result = engineer::master (words, capture, style, previous.has_value() ? &*previous : nullptr, [wanted, shared] (float p)
        {
            *shared = p;
            return wanted->load();
        });

        capture.deleteFile();

        juce::MessageManager::callAsync ([safe, words, result]
        {
            if (safe != nullptr)
                safe->finished (words, result);
        });
    });
}

void MasterEngineerPanel::finished (const juce::String& words, const engineer::Result& r)
{
    working = false;
    stopTimer();

    if (! r.ok)
    {
        status = r.problem;
        refresh();
        changed();

        if (onDone != nullptr)
            onDone (false);

        return;
    }

    song.checkpoint();
    song.mastering = r.settings;
    song.masterTurns.push_back (engineer::turnFor (words, r));
    song.masterShown = (int) song.masterTurns.size() - 1;
    status = {};
    song.notify (Song::Change::mixer);

    refresh();
    conversationView.setViewPosition (0, conversation->getHeight());

    if (onAnswered != nullptr)
        onAnswered();

    changed();

    if (onDone != nullptr)
        onDone (true);
}

void MasterEngineerPanel::showVersion (int index)
{
    if (working || ! juce::isPositiveAndBelow (index, (int) song.masterTurns.size()) || index == song.masterShown)
        return;

    song.checkpoint();
    song.mastering = Song::Mastering::fromVar (juce::JSON::parse (song.masterTurns[(size_t) index].settings));
    song.masterShown = index;
    song.notify (Song::Change::mixer);
    setListen (listen);
    refresh();
    changed();
}

void MasterEngineerPanel::setListen (int mode)
{
    listen = juce::jlimit (0, 2, mode);

    if (! hasTurns())
        listen = 0;

    listenSwitch.setSelected (listen);
    audio.setMasterCompare (listen, (float) shownAnswer().getProperty ("compare_db", 0.0));
    repaint();
}

void MasterEngineerPanel::changed()
{
    if (onStateChanged != nullptr)
        onStateChanged();
}

void MasterEngineerPanel::songChanged (Song::Change c)
{
    if (c == Song::Change::mixer || c == Song::Change::everything)
    {
        if (c == Song::Change::everything)
            setListen (0);

        refresh();
    }
}

void MasterEngineerPanel::timerCallback()
{
    if (working && progress != nullptr)
    {
        const int percent = juce::roundToInt (progress->load() * 100.0f);
        status = percent < 12 ? "Listening to your song..." : percent < 38 ? "Hearing it through its moves, and checking them..."
                                                                            : "Working out how loud to make it... " + juce::String (percent) + "%";
        repaint (statusArea);
    }
}

void MasterEngineerPanel::refresh()
{
    const bool talking = hasTurns();
    const int turns = (int) song.masterTurns.size();

    masterButton.setButtonText (working ? "Listening..." : talking ? "Master it again, from scratch" : "Master it");
    masterButton.setEnabled (! working);
    previousButton.setEnabled (! working && song.masterShown > 0);
    nextButton.setEnabled (! working && song.masterShown >= 0 && song.masterShown < turns - 1);
    listenSwitch.setEnabled (talking && ! working);

    for (auto* comp : std::initializer_list<juce::Component*> { &previousButton, &nextButton, &listenSwitch, &conversationView })
        comp->setVisible (talking);

    for (auto* chip : ideaChips)
        chip->setVisible (! talking);

    resized();
    conversation->update (std::max (200, conversationView.getWidth() - conversationView.getScrollBarThickness()));
    repaint();
}

//==============================================================================
void MasterEngineerPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 10);

    // Before the first master: what it does, Master it, and ideas to start from
    if (! hasTurns())
    {
        introArea = r.withSizeKeepingCentre (std::min (860, r.getWidth()), std::min (360, r.getHeight()));
        auto t = introArea.withTrimmedTop (92);
        masterButton.setBounds (t.removeFromTop (40).withWidth (220));
        t.removeFromTop (14);
        statusArea = t.removeFromBottom (24);
        const int columns = t.getWidth() >= 760 ? 3 : 2, rowHeight = 60;
        const int w = (t.getWidth() - (columns - 1) * 8) / columns;

        for (int i = 0; i < ideaChips.size(); ++i)
        {
            const int row = i / columns, col = i % columns;
            auto b = juce::Rectangle<int> (t.getX() + col * (w + 8), t.getY() + row * rowHeight, w, 52);
            ideaChips[i]->setVisible (b.getBottom() <= t.getBottom());
            ideaChips[i]->setBounds (b);
        }

        return;
    }

    // The version playing down the right: Master it, versions, before and after, the tone, Listen
    sideArea = r.removeFromRight (std::min (sideWidth, r.getWidth() * 2 / 5));
    r.removeFromRight (12);
    {
        auto s = sideArea.reduced (0, 4);
        masterButton.setBounds (s.removeFromTop (36));
        s.removeFromTop (10);
        versionArea = s.removeFromTop (32);
        {
            auto v = versionArea;
            previousButton.setBounds (v.removeFromLeft (34));
            v.removeFromLeft (4);
            nextButton.setBounds (v.removeFromLeft (34));
        }

        s.removeFromTop (6);
        statusArea = s.removeFromBottom (22);
        s.removeFromBottom (4);

        // Listen, and its switch beside it
        auto listenRow = s.removeFromBottom (30);
        listenLabelArea = listenRow.removeFromLeft (64);
        listenSwitch.setBounds (listenRow.withWidth (std::min (listenRow.getWidth(), listenSwitch.getIdealWidth())));
        s.removeFromBottom (6);
        factsArea = s.removeFromTop (66);
        toneArea = s;
    }

    conversationView.setBounds (r);
    conversation->update (std::max (200, r.getWidth() - conversationView.getScrollBarThickness()));
}

void MasterEngineerPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    if (! hasTurns())
    {
        auto t = introArea;
        g.setColour (text);
        g.setFont (font (21.0f, true));
        g.drawText ("Let the AI master your song", t.removeFromTop (30), juce::Justification::centredLeft, true);

        const auto style = songStyle();
        const auto* named = agents::findStyle (style);
        const juce::String forWhat = named != nullptr ? "the way a " + juce::String (named->name).toLowerCase() + " master sounds"
                                                      : juce::String ("for the song's style (or a balanced master, with no style yet)");
        drawWrapped (g, "It listens to the whole song, then sets Tone, Punch, Width and the loudness " + forWhat
                          + ", and tells you why. Describe what you want in the bar above (\"loud metalcore master\", \"warm, for Spotify\"), or:",
                     font (16.0f), textDim, t.removeFromTop (58).withTrimmedTop (4));
    }
    else
    {
        g.setColour (hairline);
        g.fillRect (sideArea.getX() - 6, sideArea.getY(), 1, sideArea.getHeight());
        paintSide (g);
    }

    if (status.isNotEmpty())
    {
        g.setColour (working ? textDim : danger);
        g.setFont (font (16.0f, ! working));
        g.drawFittedText (status, statusArea, juce::Justification::centredLeft, 1);
    }
    else if (hasTurns())
    {
        g.setColour (textFaint);
        g.setFont (font (16.0f));
        g.drawText ("Masters offline, on this computer", statusArea, juce::Justification::centredLeft, true);
    }
}

void MasterEngineerPanel::paintSide (juce::Graphics& g)
{
    const auto answer = shownAnswer();
    const int turns = (int) song.masterTurns.size();

    g.setColour (text);
    g.setFont (font (16.0f, true));
    g.drawText ("Version " + juce::String (song.masterShown + 1) + " of " + juce::String (turns), versionArea.withTrimmedLeft (84),
                juce::Justification::centredLeft, true);

    if (! answer.isObject())
        return;

    // What it's for, and before and after
    const auto* style = agents::findStyle (answer.getProperty ("style", {}).toString());
    const auto* target = mastering::findTarget (answer.getProperty ("target", {}).toString());
    const auto before = answer.getProperty ("before", {}), after = answer.getProperty ("after", {});
    auto f = factsArea;

    g.setColour (textDim);
    g.setFont (font (16.0f));
    g.drawText ((style != nullptr ? juce::String (style->name) : juce::String ("Balanced"))
                  + (target != nullptr ? juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + target->name
                                           + (target->lufs != 0.0f ? " (" + juce::String (juce::roundToInt (target->lufs)) + " LUFS)" : juce::String()) : juce::String()),
                f.removeFromTop (22), juce::Justification::centredLeft, true);

    auto row = [&g, &f] (const juce::String& label, const juce::String& from, const juce::String& to)
    {
        auto line = f.removeFromTop (21);
        g.setColour (textDim);
        g.setFont (font (16.0f));
        g.drawText (label, line.removeFromLeft (92), juce::Justification::centredLeft, true);
        g.setColour (text);
        g.drawText (from + juce::String (juce::CharPointer_UTF8 ("  \xe2\x86\x92  ")) + to, line, juce::Justification::centredLeft, true);
    };

    row ("Loudness", juce::String ((float) before.getProperty ("lufs", -100.0), 1), juce::String ((float) after.getProperty ("lufs", -100.0), 1) + " LUFS");
    row ("Peaks", signedDb ((float) before.getProperty ("peak", -100.0)), signedDb ((float) after.getProperty ("peak", -100.0)) + " dB");

    // Listen
    g.setColour (textDim);
    g.setFont (font (16.0f));
    g.drawText ("Listen", listenLabelArea, juce::Justification::centredLeft, true);

    // The tone against the style's: the band of what masters do shaded, before as a ring, after as a
    // dot (where there's room for it)
    auto t = toneArea.withTrimmedTop (4);

    if (t.getHeight() < 20 + 4 * 17)
        return;

    g.setColour (textDim);
    g.setFont (font (16.0f));
    g.drawText ("Tone against a " + (style != nullptr ? juce::String (style->name).toLowerCase() : juce::String ("balanced")) + " master",
                t.removeFromTop (20), juce::Justification::centredLeft, true);

    const auto* wanted = answer.getProperty ("wanted", {}).getArray();
    const auto* was = before.getProperty ("bands", {}).getArray();
    const auto* now = after.getProperty ("bands", {}).getArray();

    if (wanted == nullptr || was == nullptr || now == nullptr || wanted->size() < 4 || was->size() < 4 || now->size() < 4)
        return;

    const int rowHeight = juce::jlimit (17, 26, t.getHeight() / 4);
    constexpr float range = 9.0f;

    for (int b = 0; b < 4; ++b)
    {
        auto line = t.removeFromTop (rowHeight);
        g.setColour (textDim);
        g.drawText (engineer::bandName (b), line.removeFromLeft (76), juce::Justification::centredLeft, true);

        auto track = line.reduced (6, 0).toFloat();
        const float mid = track.getCentreX(), y = track.getCentreY();
        auto xOf = [&] (float off) { return mid + juce::jlimit (-1.0f, 1.0f, off / range) * track.getWidth() * 0.5f; };

        const float tol = engineer::tolerance (b);
        g.setColour (accentCool.withAlpha (0.12f));
        g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (xOf (-tol), y - 6.0f, xOf (tol), y + 6.0f), 4.0f);
        g.setColour (hairlineStrong);
        g.drawHorizontalLine ((int) y, track.getX(), track.getRight());
        g.setColour (textFaint);
        g.fillRect (mid - 0.5f, y - 7.0f, 1.0f, 14.0f);

        const float target = (float) (*wanted)[b];
        const float x0 = xOf ((float) (*was)[b] - target), x1 = xOf ((float) (*now)[b] - target);
        g.setColour (textDim);
        g.drawEllipse (x0 - 4.5f, y - 4.5f, 9.0f, 9.0f, 1.5f);
        g.setColour (accent);
        g.fillEllipse (x1 - 4.5f, y - 4.5f, 9.0f, 9.0f);
    }

}

} // namespace nightshift
