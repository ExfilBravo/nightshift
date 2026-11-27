// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "Editing.h"
#include "audio/AudioEngine.h"
#include "model/Song.h"

namespace nightshift
{

/** Arrange: the tracks you've added, holding pattern clips and recorded audio. Add track
    (in the header, and under the last track) adds anything a channel can be as a new track;
    so does dragging a sound in from the sidebar, or a channel from Steps. Dropped on an
    instrument's track, a sound replaces its instrument. A pattern dragged from the sidebar
    goes in as a clip where it's let go (FL's way), on a new track under the last one. With the draw tool, click empty space
    to paint (on an instrument's track, a pattern of that instrument; elsewhere the current
    pattern, as in FL); drag clips to move them, right-click to delete (or use the erase tool),
    click the ruler to move the song position.

    Each track's header has its icon (click it to pick another), its name (its own, else its
    channel's or what's on it), mute and solo, and an arm button for an audio track.

    Recorded audio sits here as take folders, as in Logic: the folder plays its comp, and
    opening it shows a lane per take. Drag across a lane to use that part of the take
    (Quick Swipe Comping); click a lane to use all of it. Drag along the top of the ruler
    to set a loop: song mode plays it round, and recording in it makes a take each pass.

    Editing: click a clip to select it, Shift+click to add one, or drag a box over clips with
    the select tool (Ctrl+drag with any); the selection moves, copies, pastes, duplicates and
    deletes together (createEditMenu, and its keys). Drag a clip's edge to trim it; the split
    tool cuts a clip where it's clicked (Ctrl+T at the song position); a take folder's dots on
    its top corners drag out its fades.
*/
class Playlist : public juce::Component,
                 public juce::TooltipClient,
                 public juce::DragAndDropTarget,
                 private Song::Listener,
                 private juce::ScrollBar::Listener,
                 private juce::ChangeListener
{
public:
    Playlist (Song&, AudioEngine&);
    ~Playlist() override;

    void refreshPlayhead();

    /** FL's double-clicks: a pattern clip opens the pattern; a track's name opens its channel
        (or its first pattern, when it plays several channels). */
    std::function<void (int patternId)> onOpenPattern;
    std::function<void (int channelId)> onOpenChannel;

    /** Making channels for dragged sounds (returning the new channel's id, 0 if it can't),
        swapping a channel's instrument for one, putting an effect on a channel, naming a
        dragged sound, and showing Add track's menu (the new track going in at `insertAt`, -1
        for the end). MainComponent hands these to Steps, which knows how. */
    std::function<int (const juce::String& sound)> onAddSound;
    std::function<bool (int channelId, const juce::String& sound)> onReplaceSound;
    std::function<void (int channelId, const juce::String& effect)> onAddEffect;
    std::function<juce::String (const juce::String& sound)> nameOfSound;
    std::function<void (int insertAt, juce::Rectangle<int> screenArea)> onShowAddMenu;

    /** A track for a channel just added (at `at`, -1 for the end), unless it has one already. */
    void addTrackFor (int channelId, int at = -1);

    /** The channels a row plays: its channel, an audio track's, or those with notes in its patterns. */
    std::vector<int> channelsOn (int row) const      { return song.channelsOnTrack (row); }

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    /** Where a drop at `position` would go and what it would do there, as itemDropped does it.
        A pattern (patternDragPrefix) lands as a clip at `tick` on the track under it (`clip`),
        or on a new track (`newTrack`) under the last one or under an audio track. */
    struct DropTarget
    {
        enum class Kind { none, newTrack, replace, assign, effect, clip };
        Kind kind = Kind::none;
        int row = -1;                // the track it lands on, or where a new one goes in
        int tick = 0;                // where a dropped pattern's clip starts
    };

    DropTarget dropTargetAt (juce::Point<int> position, const juce::String& description) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    juce::String getTooltip() override;

    /** Select moves things and drags a box to select clips; draw also paints the current
        pattern on empty space; erase deletes what you click or drag over; split cuts a clip
        in two where it's clicked. */
    enum class Tool { select, draw, erase, split };
    void setTool (Tool t)                   { tool = t; }

    /** An Edit menu command on the selected clips. Paste puts the copied clips at `pasteAt` (a
        tick, and the track the highest of them goes on) if given, else where the mouse points
        in the grid, else right after where they were copied from. Split cuts the selected
        clips (or, with none selected, every clip) at the song position. False when there was
        nothing to do it to. */
    bool edit (EditCommand, std::optional<std::pair<int, int>> pasteAt = std::nullopt);
    bool canEdit (EditCommand) const;

    /** The Edit menu: those commands with their keys. */
    juce::PopupMenu createEditMenu();

    /** The selected clips: pattern clips and take folders, by index into their lists. */
    const std::set<int>& getSelectedClips() const   { return chosenClips; }
    const std::set<int>& getSelectedAudio() const   { return chosenAudio; }

    /** A line for the toast (what a split did, say). */
    std::function<void (const juce::String&)> onMessage;

    /** Clicked: the editing keys go to whichever editor was clicked last. */
    std::function<void()> onActivated;

    /** Where clips and the loop land: Song::barGrid, Song::beatGrid (bars and beats as long as
        they are where they're used), else ticks (1 for none); Alt goes a step finer. */
    void setSnap (int grid)                 { snap = grid < 0 ? grid : std::max (1, grid); }

    /** The ruler's menu at a tick: change the tempo or the time signature there. */
    void showRulerMenu (int tick);

    /** Asks for the tempo from `tick` (the start's at 0) and sets it, changing the one there. */
    void askTempo (int tick);

    /** Where a glide's curve handle is drawn, halfway along it (for the tests and snapshots). */
    juce::Point<float> curveHandlePosition (int index) const    { return tempoCurveHandle (index, tempoLaneRange()); }

    /** Scrolls so `tick` is at the grid's left edge (for snapshots). */
    void scrollToTick (double tick);

    static constexpr int headerWidth = 216, rulerHeight = 30, rowHeight = 54, laneHeight = 30, loopStripHeight = 10;
    static constexpr int automationLaneHeight = 44;

    /** An automation lane on show (Song::showAutomation): under its channel's first track, or
        along the bottom for the master's. A channel with no lanes shows its volume's, which
        becomes the song's when a point is added. */
    struct LaneView
    {
        int owner = 0;
        juce::String param;
        juce::Rectangle<float> area, header;    // in the grid, and in the header column
    };

    std::vector<LaneView> visibleLanes() const;

    /** The parameters shown as lanes for a channel (or the master): its lanes, else its volume. */
    std::vector<juce::String> laneParamsOf (int owner) const;

    /** The icon's tile at the left of a track's header (click it to pick another). */
    juce::Rectangle<float> iconBounds (int row) const;

private:
    enum class Drag { none, move, moveAudio, erase, scrub, loop, comp, box, trim, fade, point, tempo, tempoCurve };

    /** A point on an automation lane, by its lane (owner and parameter) and place in it. */
    struct PointRef
    {
        int owner = -1;
        juce::String param;
        int index = -1;
        bool valid() const      { return owner >= 0 && index >= 0; }
        bool on (const LaneView& l) const    { return valid() && owner == l.owner && param == l.param; }
    };

    /** A clip's edge, or a take folder's fade dot, under the mouse. */
    struct Edge
    {
        bool audio = false;
        int index = -1;
        bool left = false;           // the start's edge (or the fade in), else the end's
        bool fade = false;
        bool found() const      { return index >= 0; }
    };

    /** What a row shows in its header. */
    struct TrackInfo
    {
        juce::String name, detail, icon;
        juce::Colour colour;
        int channelId = 0;           // the channel it's for (see Song::trackChannel)
        int audioChannelId = 0;      // that channel, when it's an audio track
        bool hasClips = false;
    };

    enum class TrackButton { solo, mute, arm };

    /** What's under the mouse: a pattern clip, or a take folder (audio) - its body, its
        show-takes arrow, or one of its take lanes. */
    struct Hit
    {
        bool audio = false;
        int index = -1, lane = -1;
        bool toggle = false;
        bool found() const      { return index >= 0; }
    };

    Song& song;
    AudioEngine& audio;
    juce::ScrollBar hScroll { false }, vScroll { true };
    double pxPerTick = 72.0 / (ticksPerBeat * 4), scrollTicks = 0.0;
    int firstRow = 0;
    Drag drag = Drag::none;
    int dragIndex = -1, dragLane = -1, grabOffset = 0, loopAnchor = 0;
    double compAnchor = 0.0;
    bool dragMoved = false;
    juce::Point<float> downPosition;
    double playheadTicks = -1.0;
    bool recordingShown = false;
    Tool tool = Tool::draw;
    int snap = Song::barGrid, hoveredRow = -1;
    int tracksBeforeDrag = 0;        // tracks a clip dragged below the last makes go again if it leaves them
    DropTarget drop;                 // what a drag hovering over Arrange would do
    juce::String dropLabel;
    int dropPatternId = 0;           // the pattern it is, when it's one (drawn where it would land)
    juce::Rectangle<int> rulerArea, headerArea, gridArea;

    juce::Rectangle<int> masterArea, masterHeaderArea;     // the master's automation, along the bottom
    PointRef chosenPoint, dragPoint;            // the automation point last clicked, and one being dragged

    // The tempo lane, along the top while automation shows (Logic's tempo track): the start's
    // tempo and each change as a point, held or gliding to the next (along a curve: a glide's
    // handle, halfway along it, bends it as it's dragged up or down, FL's tension handle)
    juce::Rectangle<int> tempoArea, tempoHeaderArea;
    static constexpr int noTempoPoint = -2;     // else -1 for the start's, or a change's index
    int dragTempo = noTempoPoint;               // the point dragged, or the glide whose curve is
    juce::Range<double> dragTempoRange;         // the lane's tempos while one is dragged (it holds still)
    bool tempoCurveCheckpointed = false;        // a curve dragged has its undo step (once it bends)

    juce::Range<double> tempoLaneRange() const;
    float tempoToY (double bpm, juce::Range<double>) const;
    double yToTempo (float y, juce::Range<double>) const;
    juce::Point<float> tempoPoint (int index, juce::Range<double>) const;
    int tempoPointAt (juce::Point<float>) const;

    /** A glide's curve handle, halfway along it; whether that glide has one to show (it changes
        the tempo, and has room); the glide whose handle is under a point (-1: none). */
    juce::Point<float> tempoCurveHandle (int index, juce::Range<double>) const;
    bool hasTempoCurveHandle (int index) const;
    int tempoCurveHandleAt (juce::Point<float>) const;

    /** Sets a glide's curve (0 straight), as one change undo takes back. */
    void setTempoCurve (int index, float curve);

    void tempoMouseDown (const juce::MouseEvent&);
    void showTempoMenu (int index, int tick);
    void paintTempoLane (juce::Graphics&);

    /** `tick` onto the snap (Alt finer), never before the start: the nearest line, or the one at
        or before it (way -1), or at or after it (1). */
    int snapped (double tick, const juce::ModifierKeys&, int way = 0) const;

    /** How far a copy of things over [from, to) goes to follow them: whole bars for more than
        half a bar's worth (as long as the bars are there), else whole beats. */
    int followDistance (int from, int to) const;

    /** "bar 9" or "bar 9, beat 3", where `tick` is. */
    juce::String describeTick (int tick) const;

    int takeLanesOn (int row) const;
    int automationRowFor (int owner) const;
    int automationLanesUnder (int row) const;
    juce::Rectangle<float> laneInner (const LaneView&) const;
    float laneValueToY (const LaneView&, float value) const;
    float laneYToValue (const LaneView&, float y) const;
    float laneResting (const LaneView&) const;
    juce::String laneText (const LaneView&, float value) const;
    juce::String laneName (const LaneView&) const;
    int pointAt (const LaneView&, juce::Point<float>) const;
    void automationMouseDown (const LaneView&, const juce::MouseEvent&);
    void showAutomationMenu (const LaneView&);
    void paintAutomationLane (juce::Graphics&, const LaneView&);
    void paintAutomationHeader (juce::Graphics&, const LaneView&);

    std::set<int> chosenClips, chosenAudio;     // the selected pattern clips and take folders
    std::set<int> boxClips, boxAudio;           // what was selected before a box dragged with Shift
    std::vector<PlaylistClip> originClips;      // the clips as they were when a drag began
    std::vector<AudioClip> originAudio;
    juce::Point<float> boxStart, boxEnd;
    bool trimAudio = false, trimLeft = false;   // what a trim or fade drag holds
    bool ownChange = false;                     // a change Arrange is making (keeps the selection)

    Edge edgeAt (juce::Point<float>) const;
    juce::Point<float> fadeDot (const AudioClip&, bool in) const;
    void notifyPlaylist();
    void pruneSelection();
    void clearSelection();
    void selectOnly (bool isAudio, int index);
    bool isChosen (bool isAudio, int index) const;
    void forgetClip (bool isAudio, int index);
    void copyChosen();
    void removeChosen();
    int splitClipsAt (int tick);
    int clipsToSplitAt (int tick) const;

    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbnailCache { 64 };
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;

    void songChanged (Song::Change) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateScrollBars();

    float tickToX (double) const;
    double xToTick (float) const;
    int yToRow (float) const;
    float rowToY (int) const;
    int rowHeightFor (int row) const;
    float secondsToX (const AudioClip&, double seconds) const;
    double xToSeconds (const AudioClip&, float x) const;
    juce::Rectangle<float> clipBounds (const PlaylistClip&) const;
    juce::Rectangle<float> clipBounds (const AudioClip&) const;
    juce::Rectangle<float> laneBounds (const AudioClip&, int lane) const;
    juce::Rectangle<float> toggleBounds (const AudioClip&) const;
    Hit clipAt (juce::Point<float>) const;
    int snapTicks (const juce::ModifierKeys&) const;
    void scrubTo (float x);
    bool inLoopStrip (juce::Point<int>) const;
    void showLaneMenu (int folder, int lane);

    TrackInfo trackInfo (int row) const;
    juce::String describeInstrument (const Channel&) const;
    const AudioClip* openFolderOn (int row) const;
    juce::Rectangle<float> trackButtonBounds (int row, TrackButton) const;
    std::optional<TrackButton> trackButtonAt (int row, juce::Point<float>) const;
    void pressTrackButton (int row, TrackButton);
    void showTrackMenu (int row);
    juce::PopupMenu createIconMenu (int row);
    void renameTrack (int row);
    void deleteTrack (int row, bool andChannel);
    void selectChannel (int channelId);
    void selectChannelOn (int row);
    int firstPatternOn (int row) const;

    /** Row of the "Add track" line under the last track. */
    int addRow() const                      { return song.numTracks(); }

    /** The pattern a click at `tick` on a track paints: on an instrument's track, the current
        pattern if it holds only that instrument's notes, else that track's nearest pattern to
        the left, else a new one for it; on any other track the current pattern (FL's way).
        0 where nothing is painted (an audio track). */
    int patternToPaint (int row, int tick);

    /** Takes away tracks a clip being dragged left empty below where they started. */
    void dropTracksLeftBehind();
    void setDrop (DropTarget, const juce::String& description);
    void paintDrop (juce::Graphics&);

    void paintGrid (juce::Graphics&);
    void paintRuler (juce::Graphics&);
    void paintHeaders (juce::Graphics&);
    void paintClip (juce::Graphics&, const PlaylistClip&, bool isCurrent, bool chosen);
    void paintAudioClip (juce::Graphics&, const AudioClip&, bool chosen);
    void paintTake (juce::Graphics&, const AudioClip&, const Take&, double from, double to, float y, float height, float zoom);
    float zoomFor (const AudioClip&);
    juce::AudioThumbnail* thumbnailFor (const juce::String& file);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Playlist)
};

} // namespace nightshift
