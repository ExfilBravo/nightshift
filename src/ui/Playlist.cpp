// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Playlist.h"
#include "Theme.h"
#include "TrackIcons.h"
#include "Widgets.h"
#include "audio/Samples.h"
#include "audio/Kits.h"
#include "audio/Instruments.h"

namespace nightshift
{

using namespace theme;

Playlist::Playlist (Song& s, AudioEngine& a)
    : song (s), audio (a)
{
    for (auto* bar : { &hScroll, &vScroll })
    {
        bar->addListener (this);
        bar->setAutoHide (false);
        addAndMakeVisible (*bar);
    }

    formats.registerBasicFormats();
    song.addListener (this);
    setWantsKeyboardFocus (true);
}

Playlist::~Playlist()
{
    for (auto& [file, thumbnail] : thumbnails)
        thumbnail->removeChangeListener (this);

    song.removeListener (this);
}

void Playlist::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A waveform finished (or progressed) loading
    repaint();
}

juce::AudioThumbnail* Playlist::thumbnailFor (const juce::String& file)
{
    auto& thumbnail = thumbnails[file];

    if (thumbnail == nullptr)
    {
        thumbnail = std::make_unique<juce::AudioThumbnail> (256, formats, thumbnailCache);
        thumbnail->addChangeListener (this);
        thumbnail->setSource (new juce::FileInputSource (juce::File (file)));
    }

    return thumbnail.get();
}

void Playlist::songChanged (Song::Change c)
{
    // A song loaded or undone, or channels or patterns taken away (their clips with them): the
    // selection's places in the lists may be other clips now
    if (! ownChange && (c == Song::Change::everything || c == Song::Change::channels || c == Song::Change::patterns))
        clearSelection();
    else
        pruneSelection();

    // An automation point chosen or held that isn't there any more
    for (auto* ref : { &chosenPoint, &dragPoint })
        if (ref->valid())
            if (auto lane = song.findLane (ref->owner, ref->param); c == Song::Change::everything || lane == nullptr
                                                                     || ! juce::isPositiveAndBelow (ref->index, (int) lane->points.size()))
                *ref = {};

    // The master's automation strip comes and goes, and grows with its lanes
    if (c == Song::Change::automation || c == Song::Change::everything)
        resized();
    else
        updateScrollBars();

    repaint();
}

void Playlist::notifyPlaylist()
{
    const juce::ScopedValueSetter<bool> mine (ownChange, true);
    song.notify (Song::Change::playlist);
}

void Playlist::pruneSelection()
{
    std::erase_if (chosenClips, [this] (int i) { return i >= (int) song.playlist.size(); });
    std::erase_if (chosenAudio, [this] (int i) { return i >= (int) song.audioClips.size(); });
}

void Playlist::clearSelection()
{
    chosenClips.clear();
    chosenAudio.clear();
}

void Playlist::selectOnly (bool isAudio, int index)
{
    clearSelection();
    (isAudio ? chosenAudio : chosenClips).insert (index);
}

bool Playlist::isChosen (bool isAudio, int index) const
{
    return (isAudio ? chosenAudio : chosenClips).count (index) > 0;
}

void Playlist::forgetClip (bool isAudio, int index)
{
    // A clip taken out of its list: those after it move up a place
    auto& set = isAudio ? chosenAudio : chosenClips;
    std::set<int> shifted;

    for (int i : set)
        if (i != index)
            shifted.insert (i > index ? i - 1 : i);

    set = shifted;
}

//==============================================================================
float Playlist::tickToX (double t) const       { return (float) (gridArea.getX() + (t - scrollTicks) * pxPerTick); }
double Playlist::xToTick (float x) const        { return (x - gridArea.getX()) / pxPerTick + scrollTicks; }

int Playlist::takeLanesOn (int row) const
{
    int lanes = 0;

    for (auto& c : song.audioClips)
        if (c.track == row && c.expanded && c.takes.size() > 1)
            lanes = std::max (lanes, (int) c.takes.size());

    return lanes;
}

int Playlist::rowHeightFor (int row) const
{
    // A row grows to show the take lanes of an open folder on it, then its channel's automation
    return rowHeight + takeLanesOn (row) * laneHeight + automationLanesUnder (row) * automationLaneHeight;
}

//==============================================================================
std::vector<juce::String> Playlist::laneParamsOf (int owner) const
{
    std::vector<juce::String> params;

    for (auto* lane : song.lanesOf (owner))
        params.push_back (lane->param);

    if (params.empty())
        params.push_back ("volume");

    return params;
}

int Playlist::automationRowFor (int owner) const
{
    // A channel's lanes go under its first track
    for (int row = 0; row < song.numTracks(); ++row)
        if (song.trackChannel (row) == owner)
            return row;

    return -1;
}

int Playlist::automationLanesUnder (int row) const
{
    if (! song.showAutomation || ! juce::isPositiveAndBelow (row, song.numTracks()))
        return 0;

    const int owner = song.trackChannel (row);
    return owner != 0 && automationRowFor (owner) == row ? (int) laneParamsOf (owner).size() : 0;
}

std::vector<Playlist::LaneView> Playlist::visibleLanes() const
{
    std::vector<LaneView> lanes;

    if (! song.showAutomation)
        return lanes;

    for (int row = 0; row < song.numTracks(); ++row)
    {
        const int count = automationLanesUnder (row);

        if (count == 0)
            continue;

        const int owner = song.trackChannel (row);
        const auto params = laneParamsOf (owner);
        float y = rowToY (row) + (float) (rowHeight + takeLanesOn (row) * laneHeight);

        for (int i = 0; i < count; ++i, y += (float) automationLaneHeight)
            lanes.push_back ({ owner, params[(size_t) i],
                               { (float) gridArea.getX(), y, (float) gridArea.getWidth(), (float) automationLaneHeight },
                               { 0.0f, y, (float) headerWidth, (float) automationLaneHeight } });
    }

    // The master's, along the bottom
    const auto params = laneParamsOf (0);

    for (int i = 0; i < (int) params.size(); ++i)
    {
        const float y = (float) masterArea.getY() + (float) (i * automationLaneHeight);
        lanes.push_back ({ 0, params[(size_t) i],
                           { (float) masterArea.getX(), y, (float) masterArea.getWidth(), (float) automationLaneHeight },
                           { 0.0f, y, (float) headerWidth, (float) automationLaneHeight } });
    }

    return lanes;
}

juce::Rectangle<float> Playlist::laneInner (const LaneView& lane) const
{
    return lane.area.reduced (0.0f, 6.0f);
}

float Playlist::laneValueToY (const LaneView& lane, float value) const
{
    // Up the lane as its control's knob goes round (a frequency's skew included)
    auto inner = laneInner (lane);
    float norm = juce::jlimit (0.0f, 1.0f, value);

    if (auto p = audio.findAutomationParameter (lane.owner, lane.param))
        norm = p->valueRange.convertTo0to1 (juce::jlimit (p->valueRange.start, p->valueRange.end, value));

    return inner.getBottom() - norm * inner.getHeight();
}

float Playlist::laneYToValue (const LaneView& lane, float y) const
{
    auto inner = laneInner (lane);
    const float norm = juce::jlimit (0.0f, 1.0f, (inner.getBottom() - y) / std::max (1.0f, inner.getHeight()));

    if (auto p = audio.findAutomationParameter (lane.owner, lane.param))
        return p->valueRange.convertFrom0to1 (norm);

    return norm;
}

float Playlist::laneResting (const LaneView& lane) const
{
    // Where its control is (a lane with no points leaves it there)
    if (auto p = audio.findAutomationParameter (lane.owner, lane.param))
        return p->getCurrentValue();

    return 0.5f;
}

juce::String Playlist::laneText (const LaneView& lane, float value) const
{
    if (auto p = audio.findAutomationParameter (lane.owner, lane.param))
    {
        auto words = p->valueToString (value);
        const auto label = p->getLabel();
        return label.isNotEmpty() && ! words.endsWith (label) ? words + " " + label : words;
    }

    return juce::String (value, 2);
}

juce::String Playlist::laneName (const LaneView& lane) const
{
    return (lane.owner == 0 ? juce::String ("Master: ") : juce::String()) + audio.automationName (lane.owner, lane.param);
}

int Playlist::pointAt (const LaneView& lane, juce::Point<float> p) const
{
    const auto* data = song.findLane (lane.owner, lane.param);
    int best = -1;
    float bestDistance = 7.0f;

    if (data != nullptr)
        for (int i = 0; i < (int) data->points.size(); ++i)
        {
            const auto& point = data->points[(size_t) i];
            const float d = juce::Point<float> (tickToX (point.tick), laneValueToY (lane, point.value)).getDistanceFrom (p);

            if (d <= bestDistance)
            {
                bestDistance = d;
                best = i;
            }
        }

    return best;
}

float Playlist::rowToY (int row) const
{
    float y = (float) gridArea.getY();

    for (int r = firstRow; r < row; ++r)
        y += (float) rowHeightFor (r);

    for (int r = row; r < firstRow; ++r)
        y -= (float) rowHeightFor (r);

    return y;
}

int Playlist::yToRow (float y) const
{
    // Below the last track is the Add track line (addRow)
    float top = (float) gridArea.getY();
    int row = firstRow;

    while (row < addRow() && y >= top + (float) rowHeightFor (row))
        top += (float) rowHeightFor (row++);

    return row;
}

int Playlist::snapTicks (const juce::ModifierKeys& mods) const
{
    // The snap setting; Alt goes a step finer
    if (! mods.isAltDown())
        return snap;

    return snap == Song::barGrid ? Song::beatGrid : snap == Song::beatGrid ? ticksPerStep : 1;
}

int Playlist::snapped (double tick, const juce::ModifierKeys& mods, int way) const
{
    return std::max (0, song.snapTick (tick, snapTicks (mods), way));
}

int Playlist::followDistance (int from, int to) const
{
    // A bar's worth: on by as many bars as it spans, from as far into its bar as it starts
    const int bar = song.barAt (from);

    if (std::max (1, to - from) * 2 > song.barLength (bar))
    {
        const int bars = std::max (1, (to - from + song.barLength (bar) - 1) / song.barLength (bar));
        return song.barStart (bar + bars) + (from - song.barStart (bar)) - from;
    }

    const int beat = song.meterAt (from).beatTicks();
    return std::max (1, (to - from + beat - 1) / beat) * beat;
}

juce::String Playlist::describeTick (int tick) const
{
    const int bar = song.barAt (tick), within = tick - song.barStart (bar);
    return "bar " + juce::String (bar + 1)
            + (within == 0 ? juce::String() : ", beat " + juce::String (within / song.meterAt (tick).beatTicks() + 1));
}

float Playlist::secondsToX (const AudioClip& clip, double seconds) const
{
    return tickToX (song.tickAt (song.secondsAt (clip.start) + seconds));
}

double Playlist::xToSeconds (const AudioClip& clip, float x) const
{
    return song.secondsBetween (clip.start, xToTick (x));
}

juce::Rectangle<float> Playlist::clipBounds (const PlaylistClip& clip) const
{
    const int length = song.clipLength (clip);
    return { tickToX (clip.start) + 1.0f, rowToY (clip.track) + 4.0f, std::max (3.0f, (float) (length * pxPerTick) - 2.0f), rowHeight - 8.0f };
}

juce::Rectangle<float> Playlist::clipBounds (const AudioClip& clip) const
{
    const float width = (float) ((song.tickAt (song.secondsAt (clip.start) + clip.length) - clip.start) * pxPerTick);
    return { tickToX (clip.start) + 1.0f, rowToY (clip.track) + 4.0f, std::max (3.0f, width - 2.0f), rowHeight - 8.0f };
}

juce::Rectangle<float> Playlist::laneBounds (const AudioClip& clip, int lane) const
{
    auto r = clipBounds (clip);
    return r.withY (rowToY (clip.track) + rowHeight + lane * laneHeight + 3.0f).withHeight (laneHeight - 6.0f);
}

juce::Rectangle<float> Playlist::toggleBounds (const AudioClip& clip) const
{
    // Just in from the corner, which is the fade-in's
    return clipBounds (clip).withHeight (19.0f).withWidth (20.0f).translated (12.0f, 0.0f);
}

juce::Point<float> Playlist::fadeDot (const AudioClip& clip, bool in) const
{
    // Where the fade's curve ends, on the top of the waveform under the title (a corner of it
    // when there's no fade), clear of the name and the take folder's arrow
    auto r = clipBounds (clip);
    const float x = in ? std::max (r.getX() + 6.0f, secondsToX (clip, clip.fadeIn))
                       : std::min (r.getRight() - 6.0f, secondsToX (clip, clip.length - clip.fadeOut));
    return { juce::jlimit (r.getX() + 6.0f, std::max (r.getX() + 6.0f, r.getRight() - 6.0f), x), r.getY() + 21.0f };
}

Playlist::Edge Playlist::edgeAt (juce::Point<float> p) const
{
    // Fade dots first (take folders wide enough to show them), then either end of any clip
    for (int i = (int) song.audioClips.size(); --i >= 0;)
    {
        const auto& clip = song.audioClips[(size_t) i];
        auto r = clipBounds (clip);

        if (r.getWidth() >= 40.0f)
            for (bool in : { true, false })
                if (fadeDot (clip, in).getDistanceFrom (p) <= 6.0f)
                    return { true, i, in, true };

        if (r.contains (p))
        {
            const float zone = std::min (6.0f, r.getWidth() / 4.0f);

            if (p.x < r.getX() + zone)       return { true, i, true, false };
            if (p.x > r.getRight() - zone)   return { true, i, false, false };
            return {};
        }
    }

    for (int i = (int) song.playlist.size(); --i >= 0;)
    {
        auto r = clipBounds (song.playlist[(size_t) i]);

        if (r.contains (p))
        {
            const float zone = std::min (6.0f, r.getWidth() / 4.0f);

            if (p.x < r.getX() + zone)       return { false, i, true, false };
            if (p.x > r.getRight() - zone)   return { false, i, false, false };
            return {};
        }
    }

    return {};
}

//==============================================================================
Playlist::TrackInfo Playlist::trackInfo (int row) const
{
    // Its own name, else its channel's, else its earliest clip's: audio first, as a track
    // holding takes is an audio track
    TrackInfo info;
    int earliest = std::numeric_limits<int>::max();

    for (auto& c : song.audioClips)
    {
        if (c.track != row || c.start >= earliest)
            continue;

        if (auto channel = song.findChannel (c.channelId))
        {
            earliest = c.start;
            info.name = channel->name;
            info.colour = channel->colour;
        }
    }

    if (info.name.isEmpty())
    {
        for (auto& c : song.playlist)
        {
            if (c.track != row || c.start >= earliest)
                continue;

            if (auto pattern = song.findPattern (c.patternId))
            {
                earliest = c.start;
                info.name = pattern->name;
                info.colour = pattern->colour;
            }
        }
    }

    info.hasClips = ! song.isTrackEmpty (row);
    info.channelId = song.trackChannel (row);
    const bool addedFor = juce::isPositiveAndBelow (row, song.numTracks()) && song.tracks[(size_t) row].channelId == info.channelId
                            && info.channelId != 0;

    if (auto channel = song.findChannel (info.channelId))
    {
        if (addedFor || ! info.hasClips)
            info.name = channel->name;

        info.colour = channel->colour;

        if (channel->type == InstrumentType::audio)
        {
            // Its icon and arm button say it's audio; the line says what it records from
            info.audioChannelId = channel->id;
            info.detail = channel->input.isNotEmpty() ? channel->input : juce::String ("No input");
        }
        else
        {
            info.detail = describeInstrument (*channel);
        }
    }
    else if (info.hasClips)
    {
        const auto ids = song.channelsOnTrack (row);
        info.detail = "Pattern" + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + juce::String ((int) ids.size())
                        + (ids.size() == 1 ? " sound" : " sounds");
    }
    else
    {
        info.detail = "Empty: paint or drop here";
    }

    if (juce::isPositiveAndBelow (row, song.numTracks()) && song.tracks[(size_t) row].name.isNotEmpty())
        info.name = song.tracks[(size_t) row].name;

    if (info.name.isEmpty())
        info.name = "Track " + juce::String (row + 1);

    if (info.colour.isTransparent())
        info.colour = textDim;

    info.icon = trackicons::forTrack (song, row);
    return info;
}

juce::String Playlist::describeInstrument (const Channel& c) const
{
    switch (c.type)
    {
        case InstrumentType::synth:      return "Synth" + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + c.synthPreset;
        case InstrumentType::kit:        return "Drum kit" + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + drumkits::displayName (c.kit);
        case InstrumentType::instrument: return instruments::displayName (c.instrument);
        case InstrumentType::audio:      return "Audio";

        case InstrumentType::plugin:
            if (auto desc = audio.getKnownPlugins().getTypeForIdentifierString (c.pluginId))
                return desc->name;

            return "Plugin";

        case InstrumentType::sampler:
            break;
    }

    return c.sample.startsWith ("kit:") ? juce::String ("Kit sound") : "Sample" + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "))
                                                                        + juce::File (c.sample).getFileNameWithoutExtension();
}

juce::Rectangle<float> Playlist::iconBounds (int row) const
{
    return { 12.0f, rowToY (row) + (rowHeight - 32.0f) * 0.5f, 32.0f, 32.0f };
}

void Playlist::addTrackFor (int channelId, int at)
{
    if (song.findChannel (channelId) == nullptr)
        return;

    // One it has already (an audio channel comes with its own) moves to where it was asked for
    for (int row = 0; row < song.numTracks(); ++row)
    {
        if (song.tracks[(size_t) row].channelId != channelId)
            continue;

        if (juce::isPositiveAndBelow (at, song.numTracks() + 1) && row != at && row != at - 1 && song.isTrackEmpty (row))
        {
            const auto t = song.tracks[(size_t) row];
            song.removeTrack (row);
            song.insertTrack (t, row < at ? at - 1 : at);
            song.notify (Song::Change::playlist);
        }

        selectChannel (channelId);
        return;
    }

    Track t;
    t.channelId = channelId;
    song.insertTrack (t, at);
    song.notify (Song::Change::playlist);
    selectChannel (channelId);
}

void Playlist::selectChannel (int channelId)
{
    if (song.selectedChannelId != channelId && song.findChannel (channelId) != nullptr)
    {
        song.selectedChannelId = channelId;
        song.notify (Song::Change::selection);
    }
}

void Playlist::selectChannelOn (int row)
{
    // A row playing several channels (a drum pattern's) keeps the selected one if it's
    // among them, else takes the first; the others are in the right-click menu
    const auto ids = channelsOn (row);

    if (! ids.empty() && std::find (ids.begin(), ids.end(), song.selectedChannelId) == ids.end())
        selectChannel (ids.front());
}

int Playlist::firstPatternOn (int row) const
{
    const PlaylistClip* first = nullptr;

    for (auto& clip : song.playlist)
        if (clip.track == row && (first == nullptr || clip.start < first->start))
            first = &clip;

    return first != nullptr ? first->patternId : 0;
}

const AudioClip* Playlist::openFolderOn (int row) const
{
    for (auto& c : song.audioClips)
        if (c.track == row && c.expanded && c.takes.size() > 1)
            return &c;

    return nullptr;
}

juce::Rectangle<float> Playlist::trackButtonBounds (int row, TrackButton which) const
{
    const float y = rowToY (row) + rowHeight * 0.5f - 10.0f;
    const float slot = which == TrackButton::solo ? 1.0f : which == TrackButton::mute ? 2.0f : 3.0f;
    return { headerWidth - 10.0f - slot * 22.0f - (slot - 1.0f) * 3.0f, y, 22.0f, 20.0f };
}

std::optional<Playlist::TrackButton> Playlist::trackButtonAt (int row, juce::Point<float> p) const
{
    if (! juce::isPositiveAndBelow (row, song.numTracks()))
        return std::nullopt;

    for (auto which : { TrackButton::solo, TrackButton::mute, TrackButton::arm })
    {
        if (which == TrackButton::arm && trackInfo (row).audioChannelId == 0)
            continue;

        if (trackButtonBounds (row, which).expanded (1.5f).contains (p))
            return which;
    }

    return std::nullopt;
}

void Playlist::pressTrackButton (int row, TrackButton which)
{
    if (which == TrackButton::arm)
    {
        // Arming isn't saved, so it needs no undo step
        if (auto channel = song.findChannel (trackInfo (row).audioChannelId))
        {
            channel->armed = ! channel->armed;
            song.notify (Song::Change::mixer);
        }

        return;
    }

    if (! juce::isPositiveAndBelow (row, song.numTracks()))
        return;

    song.checkpoint();
    auto& t = song.tracks[(size_t) row];
    auto& flag = which == TrackButton::mute ? t.muted : t.soloed;
    flag = ! flag;
    song.notify (Song::Change::trackState);
}

juce::PopupMenu Playlist::createIconMenu (int row)
{
    // Each icon drawn beside its name; "Automatic" follows what the track plays
    juce::PopupMenu menu;
    const auto own = juce::isPositiveAndBelow (row, song.numTracks()) ? song.tracks[(size_t) row].icon : juce::String();
    auto setIcon = [this, row] (const juce::String& id)
    {
        if (! juce::isPositiveAndBelow (row, song.numTracks()) || song.tracks[(size_t) row].icon == id)
            return;

        song.checkpoint();
        song.tracks[(size_t) row].icon = id;
        song.notify (Song::Change::trackState);
    };

    auto picture = [] (const juce::String& id)
    {
        auto drawable = std::make_unique<juce::DrawablePath>();
        auto p = trackicons::path (id);
        drawable->setPath (p);
        drawable->setFill (juce::FillType (juce::Colours::transparentBlack));
        drawable->setStrokeFill (juce::FillType (text));
        drawable->setStrokeType (juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return drawable;
    };

    {
        juce::PopupMenu::Item automatic ("Automatic (" + trackicons::nameOf (trackicons::automaticFor (song, row)) + ")");
        automatic.isTicked = ! trackicons::isIcon (own);
        automatic.action = [setIcon] { setIcon ({}); };
        menu.addItem (std::move (automatic));
    }

    menu.addSeparator();

    for (auto& icon : trackicons::all())
    {
        juce::PopupMenu::Item item (icon.name);
        item.image = picture (icon.id);
        item.isTicked = own == icon.id;
        item.action = [setIcon, id = juce::String (icon.id)] { setIcon (id); };
        menu.addItem (std::move (item));
    }

    return menu;
}

void Playlist::deleteTrack (int row, bool andChannel)
{
    if (! juce::isPositiveAndBelow (row, song.numTracks()))
        return;

    const int channelId = song.trackChannel (row);
    song.checkpoint();
    clearSelection();       // its clips go with it, and the rest move up the lists
    song.removeTrack (row);

    if (andChannel && song.findChannel (channelId) != nullptr)
    {
        song.removeChannel (channelId);
        song.notify (Song::Change::channels);
        song.notify (Song::Change::selection);
    }

    song.notify (Song::Change::playlist);
}

void Playlist::showTrackMenu (int row)
{
    if (! juce::isPositiveAndBelow (row, song.numTracks()))
        return;

    const auto& track = song.tracks[(size_t) row];
    const bool muted = track.muted, soloed = track.soloed;
    const auto info = trackInfo (row);
    juce::PopupMenu menu;
    menu.addSectionHeader (info.name);

    // Which of its channels the Inspector shows
    if (const auto ids = channelsOn (row); ids.size() > 1)
    {
        juce::PopupMenu channels;

        for (auto id : ids)
            if (auto c = song.findChannel (id))
            {
                juce::PopupMenu::Item item (c->name);
                item.colour = c->colour;
                item.isTicked = id == song.selectedChannelId;
                item.action = [this, id] { selectChannel (id); };
                channels.addItem (item);
            }

        menu.addSubMenu ("Select channel", channels);
        menu.addSeparator();
    }

    menu.addItem ("Rename...", [this, row] { renameTrack (row); });

    if (track.name.isNotEmpty())
        menu.addItem (song.findChannel (info.channelId) != nullptr ? "Name it after its channel" : "Name it after what's on it", [this, row]
        {
            song.checkpoint();
            song.tracks[(size_t) row].name = {};
            song.notify (Song::Change::trackState);
        });

    menu.addSubMenu ("Icon", createIconMenu (row));
    menu.addSeparator();
    menu.addItem (muted ? "Unmute" : "Mute", [this, row] { pressTrackButton (row, TrackButton::mute); });
    menu.addItem (soloed ? "Unsolo" : "Solo", [this, row] { pressTrackButton (row, TrackButton::solo); });

    const bool anySoloed = std::any_of (song.tracks.begin(), song.tracks.end(), [] (auto& t) { return t.soloed; });
    menu.addItem ("Unsolo all", anySoloed, false, [this]
    {
        song.checkpoint();

        for (auto& t : song.tracks)
            t.soloed = false;

        song.notify (Song::Change::trackState);
    });

    menu.addSeparator();
    menu.addItem ("Add a track under this one...", [this, row]
    {
        if (onShowAddMenu != nullptr)
            onShowAddMenu (row + 1, {});
    });
    menu.addItem (info.hasClips ? "Delete track (and its clips)" : "Delete track", [this, row] { deleteTrack (row, false); });

    // The channel goes from Steps too, notes and all, so it's offered only for a track that's its own
    if (auto c = song.findChannel (info.channelId); c != nullptr && track.channelId == c->id)
        menu.addItem ("Delete track and the " + c->name + " channel", [this, row] { deleteTrack (row, true); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

//==============================================================================
juce::Range<double> Playlist::tempoLaneRange() const
{
    // Held still while a point is dragged, else the song's tempos with room around them
    if (drag == Drag::tempo)
        return dragTempoRange;

    double lo = song.bpm, hi = song.bpm;

    for (auto& t : song.tempoChanges)
    {
        lo = std::min (lo, t.bpm);
        hi = std::max (hi, t.bpm);
    }

    return { std::max (20.0, std::floor ((lo - 20.0) / 10.0) * 10.0), std::min (400.0, std::ceil ((hi + 20.0) / 10.0) * 10.0) };
}

float Playlist::tempoToY (double bpm, juce::Range<double> range) const
{
    const auto r = tempoArea.toFloat().reduced (0.0f, 8.0f);
    return r.getBottom() - (float) ((bpm - range.getStart()) / std::max (1.0, range.getLength())) * r.getHeight();
}

double Playlist::yToTempo (float y, juce::Range<double> range) const
{
    const auto r = tempoArea.toFloat().reduced (0.0f, 8.0f);
    return juce::jlimit (20.0, 400.0, range.getStart() + (double) ((r.getBottom() - y) / r.getHeight()) * range.getLength());
}

juce::Point<float> Playlist::tempoPoint (int index, juce::Range<double> range) const
{
    const bool start = index < 0;
    const auto& t = start ? TempoChange { 0, song.bpm, false } : song.tempoChanges[(size_t) index];
    return { tickToX (t.tick), tempoToY (t.bpm, range) };
}

int Playlist::tempoPointAt (juce::Point<float> p) const
{
    const auto range = tempoLaneRange();

    for (int i = (int) song.tempoChanges.size(); --i >= -1;)
        if (tempoPoint (i, range).getDistanceFrom (p) <= 8.0f)
            return i;

    return noTempoPoint;
}

void Playlist::paintTempoLane (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState state (g);
    const auto strip = tempoArea.getUnion (tempoHeaderArea);
    g.reduceClipRegion (strip);
    g.setColour (panel);
    g.fillRect (strip);
    g.setColour (juce::Colours::black.withAlpha (0.24f));
    g.fillRect (strip);

    // The header: a curve, lit when the tempo changes; its name and the start's tempo
    const auto h = tempoHeaderArea.toFloat();
    auto curve = icons::automation();
    curve.applyTransform (juce::AffineTransform::scale (1.4f).translated (16.0f, h.getCentreY() - 7.0f));
    g.setColour (song.tempoChanges.empty() ? textFaint : accent);
    g.strokePath (curve, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (text);
    g.setFont (font (16.0f, true));
    g.drawText ("Tempo", h.withTrimmedLeft (46.0f), juce::Justification::centredLeft, true);

    // The line: held to each change and jumping there, or gliding to it; level before the start
    // and after the last
    const auto range = tempoLaneRange();
    juce::Graphics::ScopedSaveState lane (g);
    g.reduceClipRegion (tempoArea);
    const float left = std::min ((float) tempoArea.getX(), tickToX (0)), right = (float) tempoArea.getRight();
    double held = song.bpm;
    juce::Path line;
    line.startNewSubPath (left, tempoToY (held, range));
    line.lineTo (tickToX (0), tempoToY (held, range));

    for (auto& t : song.tempoChanges)
    {
        const float x = tickToX (t.tick);

        if (! t.glide)
            line.lineTo (x, tempoToY (held, range));

        line.lineTo (x, tempoToY (t.bpm, range));
        held = t.bpm;
    }

    line.lineTo (std::max (right, tickToX (song.tempoChanges.empty() ? 0 : song.tempoChanges.back().tick)), tempoToY (held, range));

    juce::Path fill (line);
    fill.lineTo (right, (float) tempoArea.getBottom());
    fill.lineTo (left, (float) tempoArea.getBottom());
    fill.closeSubPath();
    g.setColour (accent.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (accent.interpolatedWith (juce::Colours::white, 0.25f));
    g.strokePath (line, juce::PathStrokeType (1.8f));

    // The points, each saying its tempo (a glide's: where it gets to)
    g.setFont (monoFont (15.0f, true));

    for (int i = -1; i < (int) song.tempoChanges.size(); ++i)
    {
        const auto c = tempoPoint (i, range);

        if (c.x < tempoArea.getX() - 60.0f || c.x > right + 8.0f)
            continue;

        const bool dragged = dragTempo == i && drag == Drag::tempo;
        const float r = dragged ? 5.5f : 4.5f;
        g.setColour (accent.interpolatedWith (juce::Colours::white, 0.4f));
        g.fillEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r);
        g.setColour (dragged ? juce::Colours::white : background);
        g.drawEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r, dragged ? 1.6f : 1.0f);

        const double bpm = i < 0 ? song.bpm : song.tempoChanges[(size_t) i].bpm;
        const auto words = juce::String (bpm, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        const bool below = c.y < tempoArea.getY() + 20.0f;
        g.setColour (dragged ? juce::Colours::white : textDim);
        g.drawText (words, juce::Rectangle<float> (c.x + 8.0f, below ? c.y + 2.0f : c.y - 19.0f, 70.0f, 17.0f), juce::Justification::centredLeft, true);
    }

    // No changes yet: what to do, on a backing of its own
    if (song.tempoChanges.empty())
    {
        g.setFont (font (16.0f));
        const auto hint = juce::String ("Click to change the tempo there; right-click a point to glide to it");
        const auto tag = juce::Rectangle<float> ((float) tempoArea.getX() + std::max (60.0f, tickToX (0) - tempoArea.getX() + 60.0f),
                                                 (float) tempoArea.getCentreY() - 10.0f,
                                                 juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), hint) + 12.0f, 20.0f);
        g.setColour (panel.withAlpha (0.92f));
        g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (textFaint);
        g.drawText (hint, tag, juce::Justification::centred, true);
    }

    if (playheadTicks >= 0.0)
    {
        g.setColour (playhead);
        g.fillRect (tickToX (playheadTicks), (float) tempoArea.getY(), 2.0f, (float) tempoArea.getHeight());
    }

    g.setColour (hairline);
    g.fillRect ((float) strip.getX(), (float) strip.getBottom() - 1.0f, (float) strip.getWidth(), 1.0f);
}

void Playlist::tempoMouseDown (const juce::MouseEvent& e)
{
    const int index = tempoPointAt (e.position);
    const int tick = snapped (xToTick (e.position.x), e.mods);

    if (e.mods.isPopupMenu())
    {
        showTempoMenu (index, index == noTempoPoint ? tick : index < 0 ? 0 : song.tempoChanges[(size_t) index].tick);
        return;
    }

    if (e.getNumberOfClicks() > 1 && index != noTempoPoint)
    {
        askTempo (index < 0 ? 0 : song.tempoChanges[(size_t) index].tick);
        return;
    }

    song.checkpoint();
    dragTempoRange = tempoLaneRange();

    if (index == noTempoPoint)
    {
        // A new change here, at the tempo where it's clicked (at the start: the start's tempo)
        const double bpm = std::round (yToTempo (e.position.y, dragTempoRange));
        song.setTempoAt (tick, bpm);
        dragTempo = tick <= 0 ? -1 : (int) std::distance (song.tempoChanges.begin(),
                                                           std::find_if (song.tempoChanges.begin(), song.tempoChanges.end(),
                                                                         [tick] (auto& t) { return t.tick == tick; }));
        song.notify (Song::Change::transport);
    }
    else
    {
        dragTempo = index;
    }

    drag = Drag::tempo;
    repaint();
}

void Playlist::showTempoMenu (int index, int tick)
{
    juce::PopupMenu menu;

    if (index == noTempoPoint)
    {
        menu.addSectionHeader ("At " + describeTick (tick));
        menu.addItem ("Change the tempo here...", [this, tick] { askTempo (tick); });
    }
    else
    {
        const bool start = index < 0;
        const auto change = start ? TempoChange { 0, song.bpm, false } : song.tempoChanges[(size_t) index];
        menu.addSectionHeader (start ? juce::String ("The start's tempo") : "Tempo change at " + describeTick (change.tick));
        menu.addItem ("Set the tempo...", [this, tick = change.tick] { askTempo (tick); });

        if (! start)
        {
            menu.addItem ("Glide to it from the tempo before", true, change.glide, [this, tick = change.tick]
            {
                for (auto& t : song.tempoChanges)
                {
                    if (t.tick == tick)
                    {
                        song.checkpoint();
                        t.glide = ! t.glide;
                        song.notify (Song::Change::transport);
                        return;
                    }
                }
            });

            menu.addSeparator();
            menu.addItem ("Delete this tempo change", [this, tick = change.tick]
            {
                song.checkpoint();
                std::erase_if (song.tempoChanges, [tick] (auto& t) { return t.tick == tick; });
                song.notify (Song::Change::transport);
            });
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void Playlist::showRulerMenu (int tick)
{
    // A tempo goes where it's clicked (on the snap); a time signature from the start of that bar
    const int at = snapped (tick, {});
    const int bar = song.barAt (std::max (0, tick));
    const int barTick = song.barStart (bar);
    juce::PopupMenu menu;
    menu.addSectionHeader ("At " + describeTick (at));
    menu.addItem ("Change the tempo here...", [this, at] { askTempo (at); });

    juce::PopupMenu meters;
    const auto there = song.meterAt (barTick);
    int lastUnit = 4;

    for (auto [beats, unit] : Song::meters())
    {
        if (unit != lastUnit)
            meters.addSeparator();

        lastUnit = unit;
        meters.addItem (juce::String (beats) + "/" + juce::String (unit), true, there.beats == beats && there.unit == unit,
                        [this, barTick, beats = beats, unit = unit]
                        {
                            song.checkpoint();
                            song.setMeterAt (barTick, beats, unit);
                            song.notify (Song::Change::transport);
                        });
    }

    menu.addSubMenu (bar == 0 ? juce::String ("Change the time signature (the start's)")
                              : "Change the time signature from bar " + juce::String (bar + 1), meters);

    // What changes in this bar, to take away
    const int barEnd = song.barStart (bar + 1);
    bool any = false;

    for (auto& t : song.tempoChanges)
    {
        if (t.tick >= barTick && t.tick < barEnd)
        {
            if (! std::exchange (any, true))
                menu.addSeparator();

            menu.addItem ("Delete the tempo change at " + describeTick (t.tick) + " ("
                              + juce::String (t.bpm, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " bpm)",
                          [this, changeTick = t.tick]
                          {
                              song.checkpoint();
                              std::erase_if (song.tempoChanges, [changeTick] (auto& c) { return c.tick == changeTick; });
                              song.notify (Song::Change::transport);
                          });
        }
    }

    for (int i = 0; i < (int) song.meterChanges.size(); ++i)
    {
        const auto& m = song.meterChanges[(size_t) i];

        if (m.tick >= barTick && m.tick < barEnd)
        {
            if (! std::exchange (any, true))
                menu.addSeparator();

            menu.addItem ("Delete the time signature change at bar " + juce::String (bar + 1) + " (" + juce::String (m.beats) + "/" + juce::String (m.unit) + ")",
                          [this, changeTick = m.tick]
                          {
                              song.checkpoint();

                              for (int k = 0; k < (int) song.meterChanges.size(); ++k)
                                  if (song.meterChanges[(size_t) k].tick == changeTick)
                                      song.removeMeterChange (k);

                              song.notify (Song::Change::transport);
                          });
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void Playlist::askTempo (int tick)
{
    tick = std::max (0, tick);
    const auto it = std::find_if (song.tempoChanges.begin(), song.tempoChanges.end(), [tick] (auto& t) { return t.tick == tick; });
    const bool existing = it != song.tempoChanges.end();
    const double now = tick == 0 ? song.bpm : existing ? it->bpm : song.bpmAt (tick);

    auto* window = new juce::AlertWindow (tick == 0 ? juce::String ("Tempo at the start") : "Tempo from " + describeTick (tick), {},
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("bpm", juce::String (now, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."), "Beats a minute (quarter notes)");

    if (tick > 0)
    {
        window->addComboBox ("how", { "Jump to it here", "Glide to it from the tempo before" }, "How it gets there");

        if (auto* how = window->getComboBoxComponent ("how"))
            how->setSelectedItemIndex (existing && it->glide ? 1 : 0, juce::dontSendNotification);
    }

    window->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, tick] (int result)
    {
        const double bpm = window->getTextEditorContents ("bpm").trim().getDoubleValue();
        const auto* how = window->getComboBoxComponent ("how");
        const bool glide = how != nullptr && how->getSelectedItemIndex() == 1;

        if (result != 1)
            return;

        if (bpm < 20.0 || bpm > 400.0)
        {
            if (onMessage != nullptr)
                onMessage ("A tempo goes from 20 to 400 bpm");

            return;
        }

        song.checkpoint();
        song.setTempoAt (tick, bpm, glide);
        song.notify (Song::Change::transport);
    }), true);
}

void Playlist::renameTrack (int row)
{
    auto* window = new juce::AlertWindow ("Rename track", {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", trackInfo (row).name);
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, row] (int result)
    {
        const auto name = window->getTextEditorContents ("name").trim();

        if (result != 1 || ! juce::isPositiveAndBelow (row, song.numTracks()))
            return;

        song.checkpoint();
        auto& track = song.tracks[(size_t) row];

        // A track added for a channel and the channel are one thing, as in Logic: renaming
        // one renames both
        if (auto c = song.findChannel (track.channelId); c != nullptr && name.isNotEmpty())
        {
            c->name = name;
            track.name = {};
            song.notify (Song::Change::channels);
        }
        else
        {
            track.name = name;
        }

        song.notify (Song::Change::trackState);
    }), true);
}

Playlist::Hit Playlist::clipAt (juce::Point<float> p) const
{
    // Audio paints on top, so it is hit first
    for (int i = (int) song.audioClips.size(); --i >= 0;)
    {
        const auto& clip = song.audioClips[(size_t) i];

        if (clip.takes.size() > 1 && toggleBounds (clip).contains (p))
            return { true, i, -1, true };

        if (clipBounds (clip).contains (p))
            return { true, i };

        if (clip.expanded && clip.takes.size() > 1)
            for (int lane = 0; lane < (int) clip.takes.size(); ++lane)
                if (laneBounds (clip, lane).contains (p))
                    return { true, i, lane };
    }

    for (int i = (int) song.playlist.size(); --i >= 0;)
        if (clipBounds (song.playlist[(size_t) i]).contains (p))
            return { false, i };

    return {};
}

bool Playlist::inLoopStrip (juce::Point<int> p) const
{
    return rulerArea.contains (p) && p.y < rulerArea.getY() + loopStripHeight;
}

//==============================================================================
void Playlist::resized()
{
    auto r = getLocalBounds();
    hScroll.setBounds (r.removeFromBottom (10).withTrimmedLeft (headerWidth).withTrimmedRight (10));
    vScroll.setBounds (r.removeFromRight (10).withTrimmedTop (rulerHeight));
    rulerArea = r.removeFromTop (rulerHeight).withTrimmedLeft (headerWidth);

    // The tempo along the top and the master's automation along the bottom while automation shows
    masterArea = masterHeaderArea = tempoArea = tempoHeaderArea = {};

    if (song.showAutomation)
    {
        auto tempoStrip = r.removeFromTop (automationLaneHeight);
        tempoHeaderArea = tempoStrip.removeFromLeft (headerWidth);
        tempoArea = tempoStrip;

        auto strip = r.removeFromBottom ((int) laneParamsOf (0).size() * automationLaneHeight);
        masterHeaderArea = strip.removeFromLeft (headerWidth);
        masterArea = strip;
    }

    headerArea = r.removeFromLeft (headerWidth);
    gridArea = r;
    updateScrollBars();
}

void Playlist::updateScrollBars()
{
    const double visible = gridArea.getWidth() / pxPerTick;
    const double total = std::max ((double) song.songLengthInTicks() + song.ticksPerBar() * 16, scrollTicks + visible);
    hScroll.setRangeLimits (0.0, total, juce::dontSendNotification);
    hScroll.setCurrentRange (scrollTicks, visible, juce::dontSendNotification);

    vScroll.setRangeLimits (0.0, song.numTracks() + 1, juce::dontSendNotification);
    vScroll.setCurrentRange (firstRow, gridArea.getHeight() / (double) rowHeight, juce::dontSendNotification);
}

void Playlist::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hScroll)
        scrollTicks = start;
    else
        firstRow = juce::jmax (0, (int) std::round (start));

    repaint();
}

//==============================================================================
void Playlist::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    paintGrid (g);
    paintRuler (g);
    paintHeaders (g);

    // Corner over the headers, and the edge between headers and grid
    g.setColour (panel);
    g.fillRect (0, 0, headerWidth, rulerHeight);
    g.setColour (hairline);
    g.fillRect (0, rulerHeight - 1, getWidth(), 1);

    if (! tempoArea.isEmpty())
        paintTempoLane (g);

    // The master's automation along the bottom, with the song position through it
    if (! masterArea.isEmpty())
    {
        const auto strip = masterArea.getUnion (masterHeaderArea);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (strip);
        g.setColour (panel);
        g.fillRect (strip);

        for (auto& lane : visibleLanes())
            if (lane.owner == 0)
            {
                paintAutomationLane (g, lane);
                paintAutomationHeader (g, lane);
            }

        if (playheadTicks >= 0.0)
        {
            g.setColour (playhead);
            g.fillRect (tickToX (playheadTicks), (float) masterArea.getY(), 2.0f, (float) masterArea.getHeight());
        }

        g.setColour (hairlineStrong);
        g.fillRect (strip.getX(), strip.getY(), strip.getWidth(), 1);
    }

    g.setColour (hairline);
    g.fillRect (headerWidth - 1, 0, 1, getHeight());

    paintDrop (g);
}

void Playlist::paintAutomationLane (juce::Graphics& g, const LaneView& lane)
{
    const auto area = lane.area;
    const auto* channel = song.findChannel (lane.owner);
    const auto colour = channel != nullptr ? channel->colour : accent;
    g.setColour (juce::Colours::black.withAlpha (0.24f));
    g.fillRect (area);
    g.setColour (hairline);
    g.fillRect (area.withTop (area.getBottom() - 1.0f));

    const auto* data = song.findLane (lane.owner, lane.param);
    const auto inner = laneInner (lane);

    if (data == nullptr || data->points.empty())
    {
        // Nothing drawn yet: where the control is, dashed, and what to do
        const float y = laneValueToY (lane, laneResting (lane));
        const float dashes[] { 5.0f, 4.0f };
        g.setColour (colour.withAlpha (0.55f));
        g.drawDashedLine (juce::Line<float> (area.getX(), y, area.getRight(), y), dashes, 2, 1.2f);

        // On a backing of its own, so the line doesn't run through it
        g.setFont (font (16.0f));
        const auto hint = juce::String ("Click to add a point");
        const auto tag = juce::Rectangle<float> (area.getX() + 10.0f, area.getCentreY() - 10.0f,
                                                 juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), hint) + 12.0f, 20.0f);
        g.setColour (panel.withAlpha (0.9f));
        g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (textFaint);
        g.drawText (hint, tag, juce::Justification::centred, true);
        return;
    }

    // Level before the first point and after the last, straight lines between
    const auto& points = data->points;
    auto at = [&] (const AutomationPoint& p) { return juce::Point<float> (tickToX (p.tick), laneValueToY (lane, p.value)); };
    const float left = std::min (area.getX(), at (points.front()).x), right = std::max (area.getRight(), at (points.back()).x);
    juce::Path line;
    line.startNewSubPath (left, at (points.front()).y);

    for (auto& p : points)
        line.lineTo (at (p));

    line.lineTo (right, at (points.back()).y);

    juce::Path fill (line);
    fill.lineTo (right, inner.getBottom());
    fill.lineTo (left, inner.getBottom());
    fill.closeSubPath();
    g.setColour (colour.withAlpha (0.16f));
    g.fillPath (fill);
    g.setColour (colour.interpolatedWith (juce::Colours::white, 0.3f));
    g.strokePath (line, juce::PathStrokeType (1.8f));

    for (int i = 0; i < (int) points.size(); ++i)
    {
        const auto c = at (points[(size_t) i]);

        if (c.x < area.getX() - 8.0f || c.x > area.getRight() + 8.0f)
            continue;

        const bool picked = chosenPoint.on (lane) && chosenPoint.index == i;
        const float r = picked ? 5.5f : 4.0f;
        g.setColour (colour.interpolatedWith (juce::Colours::white, 0.5f));
        g.fillEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r);
        g.setColour (picked ? juce::Colours::white : background);
        g.drawEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r, picked ? 1.6f : 1.0f);
    }

    // A point being dragged says its value beside it
    if (dragPoint.on (lane) && juce::isPositiveAndBelow (dragPoint.index, (int) points.size()))
    {
        const auto& p = points[(size_t) dragPoint.index];
        const auto c = at (p);
        const auto words = laneText (lane, p.value);
        g.setFont (font (16.0f, true));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), words) + 14.0f;
        auto box = juce::Rectangle<float> (c.x + 10.0f, c.y - 11.0f, w, 22.0f);

        if (box.getRight() > area.getRight())
            box.setX (c.x - 10.0f - w);

        box.setY (juce::jlimit (area.getY(), area.getBottom() - 22.0f, box.getY()));
        g.setColour (background.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (text);
        g.drawText (words, box, juce::Justification::centred, true);
    }
}

void Playlist::paintAutomationHeader (juce::Graphics& g, const LaneView& lane)
{
    const auto h = lane.header;
    g.setColour (juce::Colours::black.withAlpha (0.24f));
    g.fillRect (h);
    g.setColour (hairline);
    g.fillRect (h.withTop (h.getBottom() - 1.0f));

    // A curve, lit when the lane has points; its name; the arrow that says a menu opens
    const auto* data = song.findLane (lane.owner, lane.param);
    const bool there = audio.findAutomationParameter (lane.owner, lane.param) != nullptr;
    auto curve = icons::automation();
    curve.applyTransform (juce::AffineTransform::scale (1.4f).translated (16.0f, h.getCentreY() - 7.0f));
    g.setColour (data != nullptr && ! data->points.empty() ? accent : textFaint);
    g.strokePath (curve, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto chevron = icons::chevronDown();
    chevron.applyTransform (juce::AffineTransform::scale (1.3f).translated (h.getRight() - 28.0f, h.getCentreY() - 6.5f));
    g.setColour (textFaint);
    g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (there ? text : textFaint);
    g.setFont (font (16.0f, true));
    g.drawText (laneName (lane) + (there ? juce::String() : juce::String (" (not here)")),
                h.withTrimmedLeft (40.0f).withTrimmedRight (32.0f), juce::Justification::centredLeft, true);
}

void Playlist::paintGrid (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (gridArea);

    // No tracks yet: what to do about it
    if (song.numTracks() == 0)
    {
        auto hint = gridArea.withTrimmedTop (rowHeight).withHeight (rowHeight * 2);
        g.setColour (textFaint);
        g.setFont (font (17.0f, true));
        g.drawText ("No tracks yet", hint.removeFromTop (28), juce::Justification::centred, true);
        g.setFont (font (16.0f));
        g.drawText ("Add one with Add track, or drag a sound here from the sidebar (or a channel from Steps)",
                    hint.removeFromTop (24), juce::Justification::centred, true);
    }

    // Rows, with the take lanes of open folders a shade darker
    for (int row = firstRow; row < song.numTracks(); ++row)
    {
        const float y = rowToY (row);

        if (y > gridArea.getBottom())
            break;

        const float h = (float) rowHeightFor (row);

        if (h > rowHeight)
        {
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.fillRect ((float) gridArea.getX(), y + rowHeight, (float) gridArea.getWidth(), h - rowHeight);
        }

        if (! song.isTrackAudible (row))
        {
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.fillRect ((float) gridArea.getX(), y, (float) gridArea.getWidth(), h);
        }

        g.setColour (hairline);
        g.fillRect ((float) gridArea.getX(), y + h - 1.0f, (float) gridArea.getWidth(), 1.0f);
    }

    // The loop, lightly shaded when it's on
    if (song.loopEnd > song.loopStart && song.loopOn)
    {
        const float x0 = tickToX (song.loopStart), x1 = tickToX (song.loopEnd);
        g.setColour (accent.withAlpha (0.035f));
        g.fillRect (x0, (float) gridArea.getY(), x1 - x0, (float) gridArea.getHeight());
        g.setColour (accent.withAlpha (0.25f));
        g.fillRect (x0, (float) gridArea.getY(), 1.0f, (float) gridArea.getHeight());
        g.fillRect (x1 - 1.0f, (float) gridArea.getY(), 1.0f, (float) gridArea.getHeight());
    }

    // Bar lines, and beat lines once there's room for them: bars as long as the time signature
    // makes them where they are (a bar cut short by a change ends at it)
    const auto sections = song.meterSections();

    for (size_t i = 0; i < sections.size(); ++i)
    {
        const auto& s = sections[i];
        const double end = i + 1 < sections.size() ? sections[i + 1].start : std::numeric_limits<double>::max();
        const double visibleEnd = xToTick ((float) gridArea.getRight()) + 1.0;

        if (end < scrollTicks || s.start > visibleEnd)
            continue;

        const bool beatLines = pxPerTick * s.beatTicks() >= 10.0;
        const int first = std::max (0, (int) std::floor ((std::max (scrollTicks, (double) s.start) - s.start) / s.beatTicks()));

        for (int beat = first; ; ++beat)
        {
            const double tick = s.start + (double) beat * s.beatTicks();

            if (tick >= end || tick > visibleEnd)
                break;

            const bool isBar = beat % s.beats == 0;

            if (isBar || beatLines)
            {
                g.setColour (juce::Colours::white.withAlpha (isBar ? 0.055f : 0.022f));
                g.fillRect (tickToX (tick), (float) gridArea.getY(), 1.0f, (float) gridArea.getHeight());
            }
        }
    }

    for (int i = 0; i < (int) song.playlist.size(); ++i)
        paintClip (g, song.playlist[(size_t) i], song.playlist[(size_t) i].patternId == song.currentPatternId, chosenClips.count (i) > 0);

    for (int i = 0; i < (int) song.audioClips.size(); ++i)
        paintAudioClip (g, song.audioClips[(size_t) i], chosenAudio.count (i) > 0);

    // Channels' automation lanes, under their tracks
    for (auto& lane : visibleLanes())
        if (lane.owner != 0)
            paintAutomationLane (g, lane);

    // The box being dragged to select clips
    if (drag == Drag::box)
    {
        const auto box = juce::Rectangle<float> (boxStart, boxEnd);
        g.setColour (accentCool.withAlpha (0.12f));
        g.fillRect (box);
        g.setColour (accentCool.withAlpha (0.8f));
        g.drawRect (box, 1.0f);
    }

    // The take so far, while recording
    if (recordingShown && playheadTicks > audio.getRecordStartTicks())
    {
        const float x0 = tickToX (audio.getRecordStartTicks()), x1 = tickToX (playheadTicks);
        g.setColour (danger.withAlpha (0.10f));
        g.fillRect (x0, (float) gridArea.getY(), x1 - x0, (float) gridArea.getHeight());
        g.setColour (danger.withAlpha (0.7f));
        g.fillRect (x0, (float) gridArea.getY(), 1.5f, (float) gridArea.getHeight());
    }

    if (playheadTicks >= 0.0)
    {
        const float x = tickToX (playheadTicks);
        g.setColour (playhead.withAlpha (0.16f));
        g.fillRect (x - 2.5f, (float) gridArea.getY(), 7.0f, (float) gridArea.getHeight());
        g.setColour (playhead);
        g.fillRect (x, (float) gridArea.getY(), 2.0f, (float) gridArea.getHeight());
    }
}

void Playlist::paintRuler (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (rulerArea);
    const float top = (float) rulerArea.getY(), bottom = (float) rulerArea.getBottom();

    // Loop band along the top: amber when on, grey when off
    if (song.loopEnd > song.loopStart)
    {
        auto band = juce::Rectangle<float>::leftTopRightBottom (tickToX (song.loopStart) + 1.0f, top + 3.0f,
                                                               tickToX (song.loopEnd) - 1.0f, top + 9.0f);
        g.setColour (song.loopOn ? accent.withAlpha (0.6f) : textFaint.withAlpha (0.35f));
        g.fillRoundedRectangle (band, 3.0f);
    }

    // Bar numbers, bars as long as they are where they are (drawn once the tags are placed, so a
    // tag never sits on a number)
    const auto numberFont = monoFont (15.0f, true);
    const int barStep = pxPerTick * song.ticksPerBar() < 28.0 ? 4 : 1;
    std::map<int, float> numberEnds;            // bar start tick: where its number ends
    std::vector<std::pair<juce::String, juce::Rectangle<float>>> numbers;

    for (int bar = song.barAt (scrollTicks); ; ++bar)
    {
        const int tick = song.barStart (bar);
        const float x = tickToX (tick);

        if (x > rulerArea.getRight())
            break;

        const bool labelled = bar % barStep == 0;

        if (labelled)
        {
            const auto number = juce::String (bar + 1);
            const float w = juce::GlyphArrangement::getStringWidth (numberFont, number);
            numbers.push_back ({ number, { x + 6.0f, top + 10.0f, w + 2.0f, 18.0f } });
            numberEnds[tick] = x + 6.0f + w;
        }

        g.setColour (hairlineStrong);
        g.fillRect (x, bottom - (labelled ? 12.0f : 6.0f), 1.0f, labelled ? 12.0f : 6.0f);
    }

    // Where the time signature and the tempo change, as tags after the bar's number (the
    // start's too once there are changes): blue for a time signature, amber for a tempo
    struct Tag { int tick; juce::String words; juce::Colour colour; juce::Rectangle<float> box; };
    std::vector<Tag> tags;

    if (! song.meterChanges.empty() || ! song.tempoChanges.empty())
    {
        for (auto& s : song.meterSections())
            tags.push_back ({ s.start, s.name(), accentCool, {} });

        tags.push_back ({ 0, juce::String (song.bpm, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " bpm", accent, {} });

        for (auto& t : song.tempoChanges)
            tags.push_back ({ t.tick, juce::String (t.bpm, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " bpm", accent, {} });

        std::stable_sort (tags.begin(), tags.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });
        float lastEnd = -1.0e9f;

        for (auto& tag : tags)
        {
            const float x = tickToX (tag.tick);
            const float w = juce::GlyphArrangement::getStringWidth (font (15.0f, true), tag.words) + 12.0f;
            float left = std::max (lastEnd + 3.0f, (numberEnds.count (tag.tick) > 0 ? numberEnds[tag.tick] : x) + 4.0f);

            if (left > x + 80.0f || left + w < rulerArea.getX() || left > rulerArea.getRight())
                continue;           // too crowded here to say (zoom in to see it)

            tag.box = { left, top + 10.0f, w, 19.0f };
            lastEnd = tag.box.getRight();
        }
    }

    g.setFont (numberFont);
    g.setColour (textFaint);

    for (auto& [number, area] : numbers)
        if (std::none_of (tags.begin(), tags.end(), [&area] (const Tag& t) { return t.box.expanded (2.0f, 0.0f).intersects (area); }))
            g.drawText (number, area, juce::Justification::centredLeft, true);

    g.setFont (font (15.0f, true));

    for (auto& tag : tags)
    {
        if (tag.box.isEmpty())
            continue;

        g.setColour (panel);
        g.fillRoundedRectangle (tag.box, 4.0f);
        g.setColour (tag.colour.withAlpha (0.2f));
        g.fillRoundedRectangle (tag.box, 4.0f);
        g.setColour (tag.colour.interpolatedWith (juce::Colours::white, 0.35f));
        g.drawText (tag.words, tag.box, juce::Justification::centred, true);
        g.setColour (tag.colour);
        g.fillRect (tickToX (tag.tick), bottom - 12.0f, 1.5f, 12.0f);
    }

    // Song end
    if (const int end = song.songLengthInTicks(); end > 0)
    {
        g.setColour (accent.withAlpha (0.55f));
        g.fillRect (tickToX (end) - 1.0f, bottom - 14.0f, 2.0f, 14.0f);
    }

    // Song position: amber while playing, grey where Play will start from
    if (playheadTicks >= 0.0 || song.mode == PlayMode::song)
    {
        const float x = tickToX (playheadTicks >= 0.0 ? playheadTicks : audio.getPlayheadTicks()) + (playheadTicks >= 0.0 ? 1.0f : 0.5f);
        juce::Path diamond;
        diamond.addRoundedRectangle (-5.0f, -5.0f, 10.0f, 10.0f, 2.0f);
        diamond.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi * 0.25f).translated (x, bottom - 11.0f));
        g.setColour (playheadTicks >= 0.0 ? playhead : textDim);
        g.fillPath (diamond);
    }
}

void Playlist::paintHeaders (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (headerArea);

    // Under the last track: Add track
    if (const float y = rowToY (addRow()); y < headerArea.getBottom() && addRow() >= firstRow)
    {
        auto line = juce::Rectangle<float> (0.0f, y, (float) headerWidth - 1.0f, (float) rowHeight);
        const bool hovered = hoveredRow == addRow();

        if (hovered)
        {
            g.setColour (panelLight);
            g.fillRect (line);
        }

        auto plus = icons::plusLine();
        plus.applyTransform (juce::AffineTransform::scale (1.5f).translated (21.0f, line.getCentreY() - 7.5f));
        g.setColour (hovered ? text : textDim);
        g.strokePath (plus, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (font (17.0f, true));
        g.drawText ("Add track", line.withTrimmedLeft (52.0f), juce::Justification::centredLeft, true);
    }

    for (int row = firstRow; row < song.numTracks(); ++row)
    {
        const float y = rowToY (row);

        if (y > headerArea.getBottom())
            break;

        auto r = juce::Rectangle<float> (0.0f, y, (float) headerWidth, (float) rowHeightFor (row));
        auto top = r.withHeight ((float) rowHeight);
        const auto info = trackInfo (row);
        const auto& track = song.tracks[(size_t) row];
        const bool audible = song.isTrackAudible (row);

        if (row == hoveredRow)
        {
            g.setColour (panelLight);
            g.fillRect (top);
        }

        // The track playing the selected channel
        if (const auto ids = channelsOn (row); std::find (ids.begin(), ids.end(), song.selectedChannelId) != ids.end())
        {
            g.setColour (accentCool.withAlpha (0.12f));
            g.fillRect (top);
            g.setColour (accentCool.withAlpha (0.8f));
            g.fillRect (top.getRight() - 3.0f, top.getY() + 6.0f, 2.0f, top.getHeight() - 12.0f);
        }

        g.setColour (hairline);
        g.fillRect (0.0f, r.getBottom() - 1.0f, (float) headerWidth, 1.0f);

        g.setColour (info.colour.withAlpha (audible ? 1.0f : 0.4f));
        g.fillRoundedRectangle (0.0f, top.getY() + 9.0f, 3.0f, top.getHeight() - 18.0f, 1.5f);

        // Its icon on a tile of its colour
        const auto tile = iconBounds (row);
        g.setColour (info.colour.withAlpha (audible ? 0.18f : 0.08f));
        g.fillRoundedRectangle (tile, 8.0f);
        g.setColour (info.colour.withAlpha (audible ? 0.45f : 0.2f));
        g.drawRoundedRectangle (tile.reduced (0.5f), 8.0f, 1.0f);
        trackicons::draw (g, info.icon, tile.reduced (6.0f),
                          info.colour.interpolatedWith (juce::Colours::white, 0.45f).withAlpha (audible ? 1.0f : 0.45f));

        // Buttons on the right: arm (audio only), mute, solo
        float buttonsLeft = (float) headerWidth;

        for (auto which : { TrackButton::arm, TrackButton::mute, TrackButton::solo })
        {
            if (which == TrackButton::arm && info.audioChannelId == 0)
                continue;

            auto b = trackButtonBounds (row, which);
            buttonsLeft = std::min (buttonsLeft, b.getX());

            const auto channel = song.findChannel (info.audioChannelId);
            const bool on = which == TrackButton::mute ? track.muted
                          : which == TrackButton::solo ? track.soloed
                                                       : channel != nullptr && channel->armed;
            const auto tint = which == TrackButton::mute ? accent : which == TrackButton::solo ? accentCool : danger;

            g.setColour (on ? tint.withAlpha (which == TrackButton::arm ? 0.16f : 0.85f) : panelLight);
            g.fillRoundedRectangle (b, 5.0f);
            g.setColour (on && which == TrackButton::arm ? danger.withAlpha (0.45f) : hairline);
            g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);

            if (which == TrackButton::arm)
            {
                g.setColour (on ? danger : danger.withAlpha (0.55f));
                g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (b.getCentre()));
            }
            else
            {
                g.setColour (on ? background : textFaint);
                g.setFont (font (15.0f, true));
                g.drawText (which == TrackButton::mute ? "M" : "S", b, juce::Justification::centred, true);
            }
        }

        // Name, and what the track plays underneath
        auto words = top.withLeft (tile.getRight() + 10.0f).withRight (buttonsLeft - 6.0f);
        g.setColour (audible ? text : textFaint);
        g.setFont (font (17.5f, true));
        g.drawText (info.name, words.withY (top.getY() + 9.0f).withHeight (20.0f), juce::Justification::centredLeft, true);

        g.setColour (textFaint);
        g.setFont (font (15.0f));
        g.drawText (info.detail, words.withY (top.getY() + 29.0f).withHeight (15.0f), juce::Justification::centredLeft, true);

        // An open take folder's lanes, labelled here; a dot marks the takes the comp uses
        if (auto folder = openFolderOn (row))
        {
            for (int lane = 0; lane < (int) folder->takes.size(); ++lane)
            {
                auto l = juce::Rectangle<float> (0.0f, top.getBottom() + lane * laneHeight, (float) headerWidth - 1.0f, (float) laneHeight);
                const bool used = std::any_of (folder->comp.begin(), folder->comp.end(), [lane] (auto& s) { return s.take == lane; });

                g.setColour (juce::Colours::black.withAlpha (0.18f));
                g.fillRect (l);
                g.setColour (hairline);
                g.fillRect (l.withTop (l.getBottom() - 1.0f));

                if (used)
                {
                    g.setColour (accent);
                    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ 20.0f, l.getCentreY() }));
                }

                g.setColour (used ? text : textFaint);
                g.setFont (font (16.0f, true));
                g.drawText ("Take " + juce::String (lane + 1), l.withTrimmedLeft (30.0f), juce::Justification::centredLeft, true);
            }
        }
    }

    // Channels' automation lanes: each one's name, which opens its menu
    for (auto& lane : visibleLanes())
        if (lane.owner != 0)
            paintAutomationHeader (g, lane);
}

void Playlist::paintClip (juce::Graphics& g, const PlaylistClip& clip, bool isCurrent, bool chosen)
{
    auto pattern = song.findPattern (clip.patternId);

    if (pattern == nullptr)
        return;

    auto r = clipBounds (clip);

    if (r.getRight() < gridArea.getX() || r.getX() > gridArea.getRight())
        return;

    const auto colour = pattern->colour;
    g.setColour (panelLight.interpolatedWith (colour, chosen ? 0.32f : 0.17f));
    g.fillRoundedRectangle (r, 6.0f);

    g.setColour (colour.interpolatedWith (juce::Colours::white, 0.35f));
    g.setFont (font (15.0f, true));
    g.drawText (pattern->name, r.reduced (7.0f, 0.0f).withY (r.getY() + 2.0f).withHeight (16.0f), juce::Justification::centredLeft, true);

    // Miniature of the pattern's notes
    int lo = 127, hi = 0;

    for (auto& [channelId, list] : pattern->notes)
        for (auto& n : list)
        {
            lo = std::min (lo, n.key);
            hi = std::max (hi, n.key);
        }

    if (lo <= hi)
    {
        // The part of the pattern it plays (all of it, unless it's been split or trimmed)
        auto body = r.withTrimmedTop (19.0f).reduced (4.0f, 4.0f);
        const float from = (float) clip.offset, length = (float) song.clipLength (clip);
        const float keySpan = (float) std::max (6, hi - lo + 1);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (body.toNearestInt());
        g.setColour (colour.withAlpha (0.85f));

        for (auto& [channelId, list] : pattern->notes)
            for (auto& n : list)
            {
                if (n.start < clip.offset || n.start >= clip.offset + length)
                    continue;

                const float x = body.getX() + body.getWidth() * (n.start - from) / length;
                const float w = std::max (1.5f, body.getWidth() * n.length / length - 0.5f);
                const float y = body.getBottom() - body.getHeight() * (n.key - lo + 1) / keySpan;
                g.fillRoundedRectangle (x, y, w, std::max (1.5f, body.getHeight() / keySpan), 0.75f);
            }
    }

    if (chosen)
    {
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (r.reduced (0.25f), 6.0f, 2.0f);
    }
    else if (isCurrent)
    {
        g.setColour (accentCool);
        g.drawRoundedRectangle (r.reduced (0.25f), 6.0f, 1.5f);
    }
    else
    {
        g.setColour (colour.withAlpha (0.5f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }
}

float Playlist::zoomFor (const AudioClip& clip)
{
    // A quiet take would draw as a flat line; scale the folder's takes up to be seen (by
    // up to 30 dB), all by the same amount so they compare
    float peak = 0.0f;

    for (auto& take : clip.takes)
        if (auto thumbnail = thumbnailFor (take.file); thumbnail->getTotalLength() > 0.0)
            peak = std::max (peak, thumbnail->getApproximatePeak());

    return juce::jlimit (1.0f, 31.6f, 0.8f / std::max (peak, 1.0e-4f));
}

void Playlist::paintTake (juce::Graphics& g, const AudioClip& clip, const Take& take, double from, double to,
                          float y, float height, float zoom)
{
    from = std::max (from, take.from);
    to = std::min (to, take.to);

    auto thumbnail = thumbnailFor (take.file);

    if (to <= from || thumbnail->getTotalLength() <= 0.0)
        return;

    // Draw only the visible part: long takes stay cheap when zoomed in
    auto area = juce::Rectangle<float>::leftTopRightBottom (secondsToX (clip, from), y, secondsToX (clip, to), y + height);
    auto visible = area.getIntersection (gridArea.toFloat());

    if (visible.getWidth() < 1.0f)
        return;

    const double secondsPerPx = (to - from) / std::max (1.0f, area.getWidth());
    const double t0 = take.fileOffset + from + (visible.getX() - area.getX()) * secondsPerPx;
    thumbnail->drawChannel (g, visible.toNearestInt(), t0, t0 + visible.getWidth() * secondsPerPx, 0, zoom);
}

void Playlist::paintAudioClip (juce::Graphics& g, const AudioClip& clip, bool chosen)
{
    auto r = clipBounds (clip);

    if (r.getRight() < gridArea.getX() || r.getX() > gridArea.getRight())
        return;

    auto channel = song.findChannel (clip.channelId);
    const auto colour = channel != nullptr ? channel->colour : textDim;
    const bool folder = clip.takes.size() > 1;
    const float zoom = zoomFor (clip);

    g.setColour (panelLight.interpolatedWith (colour, chosen ? 0.32f : 0.17f));
    g.fillRoundedRectangle (r, 6.0f);

    // A take folder: a chevron to show the takes, and how many there are (the corner itself
    // is the fade-in's dot)
    juce::String title = channel != nullptr ? channel->name : juce::String();
    auto label = r.reduced (7.0f, 0.0f).withY (r.getY() + 2.0f).withHeight (16.0f).withTrimmedLeft (10.0f);

    if (folder)
    {
        auto chevron = clip.expanded ? icons::chevronDown() : icons::chevronRight();
        auto t = toggleBounds (clip);
        chevron.applyTransform (juce::AffineTransform::scale (1.4f).translated (t.getCentreX() - 7.0f, t.getCentreY() - 7.0f));
        g.setColour (colour.interpolatedWith (juce::Colours::white, 0.35f));
        g.strokePath (chevron, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        title << juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) << (int) clip.takes.size() << " takes";
        label = label.withTrimmedLeft (17.0f);
    }
    else if (! clip.takes.empty())
    {
        title << "  " << juce::File (clip.takes.front().file).getFileNameWithoutExtension();
    }

    g.setColour (colour.interpolatedWith (juce::Colours::white, 0.35f));
    g.setFont (font (15.0f, true));
    g.drawText (title, label, juce::Justification::centredLeft, true);

    // The comp: each segment drawn from the take it comes from
    auto body = r.withTrimmedTop (18.0f).reduced (2.0f, 3.0f);
    bool missing = false;
    float loudest = 0.0f;

    for (auto& segment : clip.comp)
    {
        if (! juce::isPositiveAndBelow (segment.take, (int) clip.takes.size()))
            continue;

        const auto& take = clip.takes[(size_t) segment.take];

        if (! juce::File (take.file).existsAsFile())
        {
            missing = true;
            continue;
        }

        g.setColour (colour.withAlpha (0.9f));
        paintTake (g, clip, take, segment.from, std::min (segment.to, clip.length), body.getY(), body.getHeight(), zoom);

        if (auto thumbnail = thumbnailFor (take.file); thumbnail->getTotalLength() > 0.0)
            loudest = std::max (loudest, thumbnail->getApproximatePeak());
    }

    // Where one take hands over to another
    if (folder)
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));

        for (size_t i = 1; i < clip.comp.size(); ++i)
            if (std::abs (clip.comp[i - 1].to - clip.comp[i].from) < 1.0e-6)
                g.fillRect (secondsToX (clip, clip.comp[i].from) - 0.5f, body.getY(), 1.0f, body.getHeight());
    }

    if (missing)
    {
        g.setColour (danger);
        g.setFont (font (15.0f, true));
        g.drawText ("file missing", body, juce::Justification::centred, true);
    }
    else if (loudest > 0.0f && loudest < 0.0316f && body.getWidth() > 90.0f)
    {
        const auto quiet = "quiet: peaks at " + juce::String (juce::roundToInt (juce::Decibels::gainToDecibels (loudest))) + " dB";
        g.setFont (font (15.0f, true));
        auto tag = body.reduced (4.0f, 3.0f).withWidth (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), quiet) + 12.0f)
                                            .withHeight (17.0f);
        g.setColour (background.withAlpha (0.85f));
        g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (accent);
        g.drawText (quiet, tag, juce::Justification::centred, true);
    }

    // Fades: the part they turn down shaded over the waveform, under a curve from silence
    auto fadeShape = [&] (bool in)
    {
        const float edge = in ? r.getX() : r.getRight();
        const float end = secondsToX (clip, in ? clip.fadeIn : clip.length - clip.fadeOut);
        const float top = body.getY(), bottom = body.getBottom();
        juce::Path shade, line;
        shade.startNewSubPath (edge, top);
        shade.lineTo (end, top);
        shade.quadraticTo (edge, top, edge, bottom);
        shade.closeSubPath();
        line.startNewSubPath (edge, bottom);
        line.quadraticTo (edge, top, end, top);
        g.setColour (background.withAlpha (0.55f));
        g.fillPath (shade);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.strokePath (line, juce::PathStrokeType (1.2f));
    };

    if (clip.fadeIn > 0.0)   fadeShape (true);
    if (clip.fadeOut > 0.0)  fadeShape (false);

    if (chosen)
    {
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (r.reduced (0.25f), 6.0f, 2.0f);
    }
    else
    {
        g.setColour (colour.withAlpha (0.5f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }

    // The fade dots on its top corners (on a clip wide enough to grab them)
    if (r.getWidth() >= 40.0f)
        for (bool in : { true, false })
        {
            const auto dot = fadeDot (clip, in);
            const bool faded = in ? clip.fadeIn > 0.0 : clip.fadeOut > 0.0;
            g.setColour (faded ? juce::Colours::white : colour.interpolatedWith (juce::Colours::white, 0.45f).withAlpha (0.8f));
            g.fillEllipse (dot.x - 4.0f, dot.y - 4.0f, 8.0f, 8.0f);
            g.setColour (background.withAlpha (0.8f));
            g.drawEllipse (dot.x - 4.0f, dot.y - 4.0f, 8.0f, 8.0f, 1.0f);
        }

    if (! folder || ! clip.expanded)
        return;

    // Take lanes, newest at the bottom: the parts the comp uses are lit, the rest dim. Their
    // names are in the row's header
    for (int lane = 0; lane < (int) clip.takes.size(); ++lane)
    {
        const auto& take = clip.takes[(size_t) lane];
        auto l = laneBounds (clip, lane);
        g.setColour (panelLight);
        g.fillRoundedRectangle (l, 5.0f);

        for (auto& segment : clip.comp)
        {
            if (segment.take != lane)
                continue;

            auto lit = juce::Rectangle<float>::leftTopRightBottom (secondsToX (clip, segment.from), l.getY(), secondsToX (clip, segment.to), l.getBottom());
            g.setColour (colour.withAlpha (0.22f));
            g.fillRect (lit.getIntersection (l));
        }

        g.setColour (textFaint.withAlpha (0.6f));
        paintTake (g, clip, take, 0.0, clip.length, l.getY() + 2.0f, l.getHeight() - 4.0f, zoom);

        for (auto& segment : clip.comp)
        {
            if (segment.take != lane)
                continue;

            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (juce::Rectangle<float>::leftTopRightBottom (secondsToX (clip, segment.from), l.getY(),
                                                                           secondsToX (clip, segment.to), l.getBottom()).toNearestInt());
            g.setColour (colour.brighter (0.25f));
            paintTake (g, clip, take, segment.from, segment.to, l.getY() + 2.0f, l.getHeight() - 4.0f, zoom);
        }

        g.setColour (hairline);
        g.drawRoundedRectangle (l.reduced (0.5f), 5.0f, 1.0f);
    }
}

//==============================================================================
void Playlist::scrubTo (float x)
{
    const int tick = std::max (0, song.snapTick (xToTick (x), Song::beatGrid));

    if (song.mode != PlayMode::song)
    {
        song.mode = PlayMode::song;
        song.notify (Song::Change::transport);
        audio.syncNow();
    }

    audio.setPlayheadTicks (tick);
    playheadTicks = audio.isPlaying() ? playheadTicks : -1.0;
    repaint();
}

juce::String Playlist::getTooltip()
{
    const auto p = getMouseXYRelative();

    if (inLoopStrip (p))
        return "Drag here to set a loop. Song mode plays it round, and recording in it makes a take each time round. "
               "Click it to switch the loop on or off.";

    if (headerArea.contains (p))
    {
        const int row = yToRow ((float) p.y);

        if (row == addRow())
            return "Add a track: a drum kit, the AI drummer, an audio track to record into, an instrument, a synth or a plugin. "
                   "You can also drag a sound here from the sidebar";

        if (auto which = trackButtonAt (row, p.toFloat()))
            return *which == TrackButton::mute ? "Mute this track"
                 : *which == TrackButton::solo ? "Solo this track (hear only the soloed ones)"
                                               : "Arm for recording (with Record armed, Space records the armed tracks)";

        if (iconBounds (row).contains (p.toFloat()))
            return "Click to pick this track's icon";

        return "Click to show this track's channel in the Inspector; double-click to open it; "
               "right-click to rename it, pick its icon, add a track under it or delete it. Drop a sound here to play its part with that";
    }

    for (auto& lane : visibleLanes())
    {
        if (lane.header.contains (p.toFloat()))
            return "Click for this lane's menu: a lane for another control (an effect's, the instrument's, a send), "
                   "clear its points, or remove it";

        if (lane.area.contains (p.toFloat()))
            return "Click to add a point, drag a point to move it, right-click one to delete it. Song mode plays it";
    }

    if (gridArea.contains (p) && tool != Tool::erase && tool != Tool::split)
        if (const auto edge = edgeAt (p.toFloat()); edge.found())
            return edge.fade ? (edge.left ? "Drag to fade in: from silence, up to full volume where you let go"
                                          : "Drag to fade out: from where you let go, down to silence")
                             : "Drag to trim this clip (drag back out to get it back)";

    if (const auto hit = clipAt (p.toFloat()); hit.audio)
    {
        if (hit.toggle)
            return "Show or hide the takes";

        if (hit.lane >= 0)
            return "Drag across this take to use that part of it; click to use all of it. Right-click for more.";
    }

    if (const auto hit = clipAt (p.toFloat()); hit.found())
        return tool == Tool::split ? juce::String ("Click to split this clip here")
                                   : shortcutText ("Drag to move it (the selected clips move together); Shift+click to select more. "
                                                   "Ctrl+C copies, Ctrl+B duplicates, Ctrl+T splits at the song position");

    return {};
}

void Playlist::mouseMove (const juce::MouseEvent& e)
{
    const int under = yToRow (e.position.y);
    const int row = headerArea.contains (e.getPosition()) && e.y < rowToY (under) + rowHeight ? under : -1;

    if (row != hoveredRow)
    {
        hoveredRow = row;
        repaint (headerArea);
    }

    if (row >= 0)
    {
        const bool clickable = row == addRow() || trackButtonAt (row, e.position).has_value() || iconBounds (row).contains (e.position);
        setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        return;
    }

    const auto hit = clipAt (e.position);

    if (tool == Tool::split)
    {
        setMouseCursor (hit.found() && hit.lane < 0 && ! hit.toggle ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
        return;
    }

    if (tool != Tool::erase && gridArea.contains (e.getPosition()))
        if (const auto edge = edgeAt (e.position); edge.found())
        {
            setMouseCursor (edge.fade ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::LeftRightResizeCursor);
            return;
        }

    setMouseCursor (hit.lane >= 0 ? juce::MouseCursor::IBeamCursor
                  : hit.toggle ? juce::MouseCursor::PointingHandCursor
                  : hit.found() ? juce::MouseCursor::DraggingHandCursor
                                : juce::MouseCursor::NormalCursor);
}

//==============================================================================
bool Playlist::keyPressed (const juce::KeyPress& key)
{
    // The automation point last clicked: Delete deletes it, Escape lets go of it
    if (chosenPoint.valid())
    {
        auto* lane = song.findLane (chosenPoint.owner, chosenPoint.param);

        if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
             && lane != nullptr && juce::isPositiveAndBelow (chosenPoint.index, (int) lane->points.size()))
        {
            song.checkpoint();
            lane->points.erase (lane->points.begin() + chosenPoint.index);
            chosenPoint = {};
            song.notify (Song::Change::automation);
            return true;
        }

        if (key == juce::KeyPress::escapeKey)
        {
            chosenPoint = {};
            repaint();
            return true;
        }
    }

    // The Edit menu's keys; anything else (and what has nothing to work on) goes on up
    if (auto command = editCommandFor (key))
        return edit (*command);

    return false;
}

void Playlist::copyChosen()
{
    auto& copied = clipboard::clips;
    copied = {};
    int from = std::numeric_limits<int>::max(), to = 0, top = std::numeric_limits<int>::max();

    for (int i : chosenClips)
    {
        const auto& c = song.playlist[(size_t) i];
        from = std::min (from, c.start);
        to = std::max (to, song.clipEnd (c));
        top = std::min (top, c.track);
    }

    for (int i : chosenAudio)
    {
        const auto& c = song.audioClips[(size_t) i];
        from = std::min (from, c.start);
        to = std::max (to, song.audioClipEnd (c));
        top = std::min (top, c.track);
    }

    copied.anchor = from;
    copied.topRow = top;
    copied.span = followDistance (from, to);

    for (int i : chosenClips)
    {
        auto c = song.playlist[(size_t) i];
        c.start -= from;
        c.track -= top;
        copied.patterns.push_back (c);
    }

    for (int i : chosenAudio)
    {
        auto c = song.audioClips[(size_t) i];
        c.start -= from;
        c.track -= top;
        copied.audio.push_back (c);
    }
}

void Playlist::removeChosen()
{
    // The clips only; recorded files stay on disk
    for (auto it = chosenClips.rbegin(); it != chosenClips.rend(); ++it)
        if (*it < (int) song.playlist.size())
            song.playlist.erase (song.playlist.begin() + *it);

    for (auto it = chosenAudio.rbegin(); it != chosenAudio.rend(); ++it)
        if (*it < (int) song.audioClips.size())
            song.audioClips.erase (song.audioClips.begin() + *it);

    clearSelection();
}

int Playlist::clipsToSplitAt (int tick) const
{
    // At the song position: the selected clips there, or with none selected every clip there
    if (song.mode != PlayMode::song)
        return 0;

    const bool onlyChosen = ! chosenClips.empty() || ! chosenAudio.empty();
    int count = 0;

    for (int i = 0; i < (int) song.playlist.size(); ++i)
        if ((! onlyChosen || chosenClips.count (i) > 0) && tick > song.playlist[(size_t) i].start && tick < song.clipEnd (song.playlist[(size_t) i]))
            ++count;

    for (int i = 0; i < (int) song.audioClips.size(); ++i)
        if ((! onlyChosen || chosenAudio.count (i) > 0) && tick > song.audioClips[(size_t) i].start && tick < song.audioClipEnd (song.audioClips[(size_t) i]))
            ++count;

    return count;
}

int Playlist::splitClipsAt (int tick)
{
    // Both halves of what was selected stay selected (second halves go on the end of their lists)
    const bool onlyChosen = ! chosenClips.empty() || ! chosenAudio.empty();
    const int clips = (int) song.playlist.size(), folders = (int) song.audioClips.size();
    int count = 0;

    for (int i = 0; i < clips; ++i)
        if ((! onlyChosen || chosenClips.count (i) > 0) && song.splitClip (i, tick))
        {
            ++count;

            if (onlyChosen)
                chosenClips.insert ((int) song.playlist.size() - 1);
        }

    for (int i = 0; i < folders; ++i)
        if ((! onlyChosen || chosenAudio.count (i) > 0) && song.splitAudioClip (i, tick))
        {
            ++count;

            if (onlyChosen)
                chosenAudio.insert ((int) song.audioClips.size() - 1);
        }

    return count;
}

bool Playlist::canEdit (EditCommand c) const
{
    const bool any = ! chosenClips.empty() || ! chosenAudio.empty();

    switch (c)
    {
        case EditCommand::selectAll:   return chosenClips.size() + chosenAudio.size() < song.playlist.size() + song.audioClips.size();
        case EditCommand::paste:       return ! clipboard::clips.isEmpty();
        case EditCommand::split:       return clipsToSplitAt ((int) std::round (audio.getPlayheadTicks())) > 0;
        case EditCommand::quantize:
        case EditCommand::up:
        case EditCommand::down:
        case EditCommand::octaveUp:
        case EditCommand::octaveDown:  return false;
        case EditCommand::deselect:
        case EditCommand::copy:
        case EditCommand::cut:
        case EditCommand::duplicate:
        case EditCommand::remove:
        case EditCommand::left:
        case EditCommand::right:       return any;
    }

    return false;
}

bool Playlist::edit (EditCommand c, std::optional<std::pair<int, int>> pasteAt)
{
    pruneSelection();

    if (! canEdit (c))
        return false;

    switch (c)
    {
        case EditCommand::selectAll:
            for (int i = 0; i < (int) song.playlist.size(); ++i)
                chosenClips.insert (i);

            for (int i = 0; i < (int) song.audioClips.size(); ++i)
                chosenAudio.insert (i);

            repaint();
            return true;

        case EditCommand::deselect:
            clearSelection();
            repaint();
            return true;

        case EditCommand::copy:
            copyChosen();
            return true;

        case EditCommand::cut:
            copyChosen();
            song.checkpoint();
            removeChosen();
            notifyPlaylist();
            return true;

        case EditCommand::remove:
            song.checkpoint();
            removeChosen();
            notifyPlaylist();
            return true;

        case EditCommand::paste:
        case EditCommand::duplicate:
        {
            // Paste: where it's asked for, else where the mouse points, else just after where
            // the clips were copied from. Duplicate: a copy of the selection right after it
            std::vector<PlaylistClip> patterns;
            std::vector<AudioClip> folders;

            if (c == EditCommand::paste)
            {
                const auto& copied = clipboard::clips;
                int at = copied.anchor + copied.span, row = copied.topRow;

                if (pasteAt.has_value())
                {
                    at = pasteAt->first;
                    row = pasteAt->second;
                }
                else if (const auto mouse = getMouseXYRelative(); isShowing() && gridArea.contains (mouse))
                {
                    at = snapped (xToTick ((float) mouse.x), {}, -1);
                    row = yToRow ((float) mouse.y);
                }

                for (auto clip : copied.patterns)
                    if (song.findPattern (clip.patternId) != nullptr)
                    {
                        clip.start += at;
                        clip.track += row;
                        patterns.push_back (clip);
                    }

                for (auto clip : copied.audio)
                    if (song.findChannel (clip.channelId) != nullptr)
                    {
                        clip.start += at;
                        clip.track += row;
                        folders.push_back (clip);
                    }
            }
            else
            {
                int from = std::numeric_limits<int>::max(), to = 0;

                for (int i : chosenClips)
                {
                    from = std::min (from, song.playlist[(size_t) i].start);
                    to = std::max (to, song.clipEnd (song.playlist[(size_t) i]));
                }

                for (int i : chosenAudio)
                {
                    from = std::min (from, song.audioClips[(size_t) i].start);
                    to = std::max (to, song.audioClipEnd (song.audioClips[(size_t) i]));
                }

                const int distance = followDistance (from, to);

                for (int i : chosenClips)
                {
                    auto clip = song.playlist[(size_t) i];
                    clip.start += distance;
                    patterns.push_back (clip);
                }

                for (int i : chosenAudio)
                {
                    auto clip = song.audioClips[(size_t) i];
                    clip.start += distance;
                    folders.push_back (clip);
                }
            }

            if (patterns.empty() && folders.empty())
                return false;

            song.checkpoint();
            clearSelection();

            for (auto& clip : patterns)
            {
                song.playlist.push_back (clip);
                chosenClips.insert ((int) song.playlist.size() - 1);
            }

            for (auto& clip : folders)
            {
                song.audioClips.push_back (clip);
                chosenAudio.insert ((int) song.audioClips.size() - 1);
            }

            song.coverClipsWithTracks();
            notifyPlaylist();
            return true;
        }

        case EditCommand::split:
        {
            const int tick = (int) std::round (audio.getPlayheadTicks());
            song.checkpoint();
            const int count = splitClipsAt (tick);
            notifyPlaylist();

            if (onMessage != nullptr)
            {
                onMessage ("Split " + juce::String (count) + (count == 1 ? " clip" : " clips") + " at " + describeTick (tick));
            }

            return count > 0;
        }

        case EditCommand::left:
        case EditCommand::right:
        {
            int earliest = std::numeric_limits<int>::max();

            for (int i : chosenClips)
                earliest = std::min (earliest, song.playlist[(size_t) i].start);

            for (int i : chosenAudio)
                earliest = std::min (earliest, song.audioClips[(size_t) i].start);

            // A step of the snap where the first of them is (a bar there, or the one before it)
            const int by = c == EditCommand::right ? song.gridStep (snapTicks ({}), earliest)
                                                   : -std::min (song.gridStep (snapTicks ({}), earliest - 1), earliest);

            if (by == 0)
                return false;

            song.checkpoint();

            for (int i : chosenClips)
                song.playlist[(size_t) i].start += by;

            for (int i : chosenAudio)
                song.audioClips[(size_t) i].start += by;

            notifyPlaylist();
            return true;
        }

        case EditCommand::quantize:
        case EditCommand::up:
        case EditCommand::down:
        case EditCommand::octaveUp:
        case EditCommand::octaveDown:
            break;
    }

    return false;
}

juce::PopupMenu Playlist::createEditMenu()
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

    const int picked = (int) (chosenClips.size() + chosenAudio.size());
    menu.addSectionHeader (picked == 0 ? shortcutText ("No clips selected: click one, or drag a box with Select (or Ctrl+drag)")
                                       : juce::String (picked) + (picked == 1 ? " clip selected" : " clips selected"));
    add (EditCommand::selectAll, "Select all");
    add (EditCommand::deselect, "Select none");
    menu.addSeparator();
    add (EditCommand::cut, "Cut");
    add (EditCommand::copy, "Copy");
    add (EditCommand::paste, "Paste");
    add (EditCommand::duplicate, "Duplicate");
    add (EditCommand::remove, "Delete");
    menu.addSeparator();
    add (EditCommand::split, song.mode == PlayMode::song ? "Split at the song position" : "Split at the song position (in Song mode)");
    add (EditCommand::left, "Earlier");
    add (EditCommand::right, "Later");
    menu.addSeparator();
    menu.addItem ("Drag a clip's edge to trim it; drag a recording's corner dots to fade it", false, false, nullptr);
    return menu;
}

void Playlist::mouseExit (const juce::MouseEvent&)
{
    if (hoveredRow >= 0)
    {
        hoveredRow = -1;
        repaint (headerArea);
    }
}

int Playlist::patternToPaint (int row, int tick)
{
    const int channelId = row < song.numTracks() ? song.trackChannel (row) : 0;
    auto channel = song.findChannel (channelId);

    if (channel != nullptr && channel->type == InstrumentType::audio)
        return 0;

    const int currentId = song.currentPattern().id;

    if (channel == nullptr)
        return currentId;

    // The current pattern, if it's this instrument's (or empty)
    if (auto current = song.findPattern (currentId))
    {
        bool others = false;

        for (auto& [id, notes] : current->notes)
            others = others || (id != channelId && ! notes.empty());

        if (! others)
            return currentId;
    }

    // This track's nearest pattern to the left of the click, else its first
    const PlaylistClip* before = nullptr;
    const PlaylistClip* after = nullptr;

    for (auto& clip : song.playlist)
    {
        if (clip.track != row || song.findPattern (clip.patternId) == nullptr)
            continue;

        if (clip.start <= tick && (before == nullptr || clip.start > before->start))
            before = &clip;
        else if (clip.start > tick && (after == nullptr || clip.start < after->start))
            after = &clip;
    }

    if (auto nearest = before != nullptr ? before : after)
        return nearest->patternId;

    return song.addPatternFor (channelId).id;
}

void Playlist::dropTracksLeftBehind()
{
    // Only plain tracks this drag made below the last one, now empty
    bool removed = false;

    for (int row = song.numTracks(); --row >= tracksBeforeDrag;)
    {
        const auto& t = song.tracks[(size_t) row];

        if (song.isTrackEmpty (row) && t.channelId == 0 && t.name.isEmpty() && t.icon.isEmpty())
        {
            song.removeTrack (row);
            removed = true;
        }
    }

    if (removed)
        song.notify (Song::Change::playlist);
}

void Playlist::mouseDown (const juce::MouseEvent& e)
{
    downPosition = e.position;
    dragMoved = false;
    tracksBeforeDrag = song.numTracks();
    grabKeyboardFocus();

    if (onActivated != nullptr)
        onActivated();

    // The tempo lane: points to add and drag, a menu on the right button
    if (tempoArea.contains (e.getPosition()))
    {
        tempoMouseDown (e);
        return;
    }

    if (tempoHeaderArea.contains (e.getPosition()))
    {
        showTempoMenu (-1, 0);
        return;
    }

    // The ruler's menu: the tempo or the time signature from there
    if (rulerArea.contains (e.getPosition()) && e.mods.isPopupMenu())
    {
        showRulerMenu ((int) std::round (xToTick (e.position.x)));
        return;
    }

    // Automation lanes first: a name opens its lane's menu, the lane takes points
    for (auto& lane : visibleLanes())
    {
        const bool onMaster = lane.owner == 0;

        if (lane.header.contains (e.position) && (onMaster ? masterHeaderArea : headerArea).contains (e.getPosition()))
        {
            showAutomationMenu (lane);
            return;
        }

        if (lane.area.contains (e.position) && (onMaster ? masterArea : gridArea).contains (e.getPosition()))
        {
            automationMouseDown (lane, e);
            return;
        }
    }

    chosenPoint = {};

    if (headerArea.contains (e.getPosition()))
    {
        const int row = yToRow (e.position.y);

        if (row == addRow())
        {
            if (e.y < rowToY (row) + rowHeight && onShowAddMenu != nullptr)
                onShowAddMenu (-1, juce::Rectangle<int> (e.getScreenX(), e.getScreenY(), 1, 1));

            return;
        }

        if (e.mods.isPopupMenu())
            showTrackMenu (row);
        else if (auto which = trackButtonAt (row, e.position))
            pressTrackButton (row, *which);
        else if (iconBounds (row).contains (e.position))
            createIconMenu (row).showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (iconBounds (row).toNearestInt() + getScreenPosition()));
        else if (e.getNumberOfClicks() == 1)
            selectChannelOn (row);

        return;
    }

    if (inLoopStrip (e.getPosition()))
    {
        drag = Drag::loop;
        loopAnchor = snapped (xToTick (e.position.x), e.mods);
        return;
    }

    if (rulerArea.contains (e.getPosition()))
    {
        drag = Drag::scrub;
        scrubTo (e.position.x);
        return;
    }

    if (! gridArea.contains (e.getPosition()))
        return;

    const auto hit = clipAt (e.position);

    if (hit.audio && hit.lane >= 0)
    {
        if (e.mods.isPopupMenu())
        {
            showLaneMenu (hit.index, hit.lane);
            return;
        }

        // Quick swipe comping starts here
        song.checkpoint();
        auto& clip = song.audioClips[(size_t) hit.index];
        const auto& take = clip.takes[(size_t) hit.lane];
        drag = Drag::comp;
        dragIndex = hit.index;
        dragLane = hit.lane;
        compAnchor = juce::jlimit (take.from, take.to, xToSeconds (clip, e.position.x));
        return;
    }

    if (hit.toggle)
    {
        auto& clip = song.audioClips[(size_t) hit.index];
        clip.expanded = ! clip.expanded;
        song.notify (Song::Change::selection);
        return;
    }

    if (e.mods.isPopupMenu() || tool == Tool::erase)
    {
        song.checkpoint();
        drag = Drag::erase;
        mouseDrag (e);
        return;
    }

    // The split tool cuts the clip it's clicked on, there (on the snap)
    if (tool == Tool::split)
    {
        if (hit.found())
        {
            const int at = snapped (xToTick (e.position.x), e.mods);
            const int start = hit.audio ? song.audioClips[(size_t) hit.index].start : song.playlist[(size_t) hit.index].start;
            const int end = hit.audio ? song.audioClipEnd (song.audioClips[(size_t) hit.index]) : song.clipEnd (song.playlist[(size_t) hit.index]);

            if (at > start && at < end)
            {
                song.checkpoint();

                if (hit.audio ? song.splitAudioClip (hit.index, at) : song.splitClip (hit.index, at))
                {
                    clearSelection();
                    notifyPlaylist();
                }
            }
        }

        return;
    }

    // A clip's ends trim it; a take folder's corner dots drag out its fades
    if (auto edge = edgeAt (e.position); edge.found())
    {
        song.checkpoint();
        drag = edge.fade ? Drag::fade : Drag::trim;
        trimAudio = edge.audio;
        trimLeft = edge.left;
        dragIndex = edge.index;
        originClips = song.playlist;
        originAudio = song.audioClips;

        if (! isChosen (edge.audio, edge.index))
            selectOnly (edge.audio, edge.index);

        repaint();
        return;
    }

    // A box over clips selects them: dragged on empty space with the select tool, or with Ctrl
    // (Cmd on a Mac) held with any; Shift adds to what's selected
    if (! hit.found() && (tool == Tool::select || e.mods.isCommandDown()))
    {
        drag = Drag::box;
        boxStart = boxEnd = e.position;
        boxClips = e.mods.isShiftDown() ? chosenClips : std::set<int>();
        boxAudio = e.mods.isShiftDown() ? chosenAudio : std::set<int>();
        chosenClips = boxClips;
        chosenAudio = boxAudio;
        repaint();
        return;
    }

    // Shift+click selects a clip, or unselects it, without moving anything
    if (hit.found() && e.mods.isShiftDown())
    {
        auto& set = hit.audio ? chosenAudio : chosenClips;

        if (set.erase (hit.index) == 0)
            set.insert (hit.index);

        repaint();
        return;
    }

    if (hit.audio)
    {
        // A take folder that isn't selected becomes the selection; a selected one takes the others along
        song.checkpoint();
        selectChannel (song.audioClips[(size_t) hit.index].channelId);

        if (! isChosen (true, hit.index))
            selectOnly (true, hit.index);

        drag = Drag::moveAudio;
        dragIndex = hit.index;
        grabOffset = (int) std::round (xToTick (e.position.x)) - song.audioClips[(size_t) hit.index].start;
        originClips = song.playlist;
        originAudio = song.audioClips;
        repaint();
        return;
    }

    int index = hit.index;

    // A pattern that plays one channel selects it (one that plays several leaves it alone)
    if (index >= 0)
        if (auto p = song.findPattern (song.playlist[(size_t) index].patternId))
        {
            std::vector<int> ids;

            for (auto& [id, notes] : p->notes)
                if (! notes.empty() && song.findChannel (id) != nullptr)
                    ids.push_back (id);

            if (ids.size() == 1)
                selectChannel (ids.front());
        }

    if (index < 0)
    {
        // Paint where you clicked: on an instrument's track its pattern, else the current one;
        // on the Add track line, onto a new track
        int row = yToRow (e.position.y);
        const int start = snapped (xToTick (e.position.x), e.mods, -1);

        // Patterns don't go on an audio track
        if (auto c = song.findChannel (row < song.numTracks() ? song.trackChannel (row) : 0); c != nullptr && c->type == InstrumentType::audio)
            return;

        song.checkpoint();
        const int patternId = patternToPaint (row, start);

        if (patternId == 0)
            return;

        if (row >= song.numTracks())
            row = song.insertTrack ({});

        song.playlist.push_back ({ patternId, row, start });
        index = (int) song.playlist.size() - 1;

        if (const int channelId = song.trackChannel (row); channelId != 0)
            selectChannel (channelId);

        if (song.currentPatternId != patternId)
        {
            song.currentPatternId = patternId;
            song.notify (Song::Change::patterns);
            song.notify (Song::Change::currentPattern);
        }

        song.notify (Song::Change::playlist);
        selectOnly (false, index);
    }
    else
    {
        song.checkpoint();

        if (song.currentPatternId != song.playlist[(size_t) index].patternId)
        {
            song.currentPatternId = song.playlist[(size_t) index].patternId;
            song.notify (Song::Change::currentPattern);
        }

        // A clip that isn't selected becomes the selection; a selected one takes the others along
        if (! isChosen (false, index))
            selectOnly (false, index);
    }

    drag = Drag::move;
    dragIndex = index;
    grabOffset = (int) std::round (xToTick (e.position.x)) - song.playlist[(size_t) index].start;
    originClips = song.playlist;
    originAudio = song.audioClips;
    repaint();
}

void Playlist::mouseDrag (const juce::MouseEvent& e)
{
    dragMoved = dragMoved || e.position.getDistanceFrom (downPosition) > 3.0f;

    switch (drag)
    {
        case Drag::scrub:
            scrubTo (e.position.x);
            break;

        case Drag::loop:
        {
            if (! dragMoved)
                break;

            const int at = snapped (xToTick (e.position.x), e.mods);
            const int from = std::min (loopAnchor, at), to = std::max (loopAnchor, at);

            if (from != song.loopStart || to != song.loopEnd || ! song.loopOn)
            {
                song.loopStart = from;
                song.loopEnd = to;
                song.loopOn = to > from;
                song.notify (Song::Change::transport);
            }
            break;
        }

        case Drag::comp:
        {
            if (! dragMoved || ! juce::isPositiveAndBelow (dragIndex, (int) song.audioClips.size()))
                break;

            // The swiped range of this take goes into the comp, as it's dragged
            auto& clip = song.audioClips[(size_t) dragIndex];
            const auto& take = clip.takes[(size_t) dragLane];
            const double at = juce::jlimit (take.from, take.to, xToSeconds (clip, e.position.x));
            clip.setComp (std::min (compAnchor, at), std::max (compAnchor, at), dragLane);
            song.notify (Song::Change::playlist);
            break;
        }

        case Drag::erase:
            // Removes the clip only; recorded files stay on disk
            if (const auto hit = clipAt (e.position); hit.audio && hit.lane < 0 && ! hit.toggle)
            {
                song.audioClips.erase (song.audioClips.begin() + hit.index);
                forgetClip (true, hit.index);
                notifyPlaylist();
            }
            else if (hit.found() && ! hit.audio)
            {
                song.playlist.erase (song.playlist.begin() + hit.index);
                forgetClip (false, hit.index);
                notifyPlaylist();
            }
            break;

        case Drag::moveAudio:
        case Drag::move:
        {
            const bool isAudio = drag == Drag::moveAudio;

            if (! juce::isPositiveAndBelow (dragIndex, isAudio ? (int) originAudio.size() : (int) originClips.size())
                 || originAudio.size() != song.audioClips.size() || originClips.size() != song.playlist.size())
                break;

            // The grabbed clip snaps (audio by beats, bars with Shift: a take rarely starts on the
            // bar) and the rest of the selection keeps its distance from it
            const int grabbedStart = isAudio ? originAudio[(size_t) dragIndex].start : originClips[(size_t) dragIndex].start;
            const int grabbedTrack = isAudio ? originAudio[(size_t) dragIndex].track : originClips[(size_t) dragIndex].track;
            // (audio moves by grid steps from where it was, so a take off the grid stays as far off it)
            const int s = ! isAudio ? snapTicks (e.mods)
                                    : e.mods.isShiftDown() ? Song::barGrid : (e.mods.isAltDown() ? ticksPerStep : Song::beatGrid);
            const double to = xToTick (e.position.x) - grabOffset;
            const int start = isAudio ? std::max (0, grabbedStart + song.snapTick (to, s) - song.snapTick (grabbedStart, s))
                                      : std::max (0, song.snapTick (to, s));
            int byTicks = start - grabbedStart;
            int byRows = yToRow (e.position.y) - grabbedTrack;
            int earliest = std::numeric_limits<int>::max(), top = std::numeric_limits<int>::max();

            for (int i : chosenClips)
            {
                earliest = std::min (earliest, originClips[(size_t) i].start);
                top = std::min (top, originClips[(size_t) i].track);
            }

            for (int i : chosenAudio)
            {
                earliest = std::min (earliest, originAudio[(size_t) i].start);
                top = std::min (top, originAudio[(size_t) i].track);
            }

            byTicks = std::max (byTicks, -earliest);
            byRows = std::max (byRows, -top);
            bool moved = false;

            for (int i : chosenClips)
            {
                auto& c = song.playlist[(size_t) i];
                const auto& o = originClips[(size_t) i];
                moved = moved || c.start != o.start + byTicks || c.track != o.track + byRows;
                c.start = o.start + byTicks;
                c.track = o.track + byRows;
            }

            for (int i : chosenAudio)
            {
                auto& c = song.audioClips[(size_t) i];
                const auto& o = originAudio[(size_t) i];
                moved = moved || c.start != o.start + byTicks || c.track != o.track + byRows;
                c.start = o.start + byTicks;
                c.track = o.track + byRows;
            }

            if (! moved)
                break;

            // Below the last track: onto new ones, which go again if the clips move back up
            song.coverClipsWithTracks();
            notifyPlaylist();
            dropTracksLeftBehind();
            break;
        }

        case Drag::trim:
        {
            if (! dragMoved || (trimAudio ? originAudio.size() != song.audioClips.size() || ! juce::isPositiveAndBelow (dragIndex, (int) originAudio.size())
                                          : originClips.size() != song.playlist.size() || ! juce::isPositiveAndBelow (dragIndex, (int) originClips.size())))
                break;

            // An end lands on the snap; a clip keeps at least a snap step (a 16th with snap off)
            const int s = snapTicks (e.mods);
            const int at = snapped (xToTick (e.position.x), e.mods);
            const int least = s == 1 ? ticksPerStep : song.gridStep (s, at);

            if (trimAudio)
            {
                // A take folder grows back only as far as its takes have audio
                auto clip = originAudio[(size_t) dragIndex];
                double earliestAudio = 0.0, latestAudio = clip.length;

                for (auto& t : clip.takes)
                {
                    earliestAudio = std::min (earliestAudio, t.from);
                    latestAudio = std::max (latestAudio, t.to);
                }

                if (trimLeft)
                {
                    const int end = song.audioClipEnd (clip);
                    const int earliest = std::max (0, (int) std::ceil (song.tickAt (song.secondsAt (clip.start) + earliestAudio) - 1.0e-6));
                    const int newStart = juce::jlimit (earliest, std::max (earliest, end - least), at);
                    song.setAudioRange (clip, newStart, clip.length - song.secondsBetween (clip.start, newStart));
                }
                else
                {
                    const double shortest = song.secondsBetween (clip.start, clip.start + least);
                    song.setAudioRange (clip, clip.start, juce::jlimit (shortest, std::max (shortest, latestAudio), song.secondsBetween (clip.start, at)));
                }

                song.audioClips[(size_t) dragIndex] = clip;
            }
            else
            {
                auto clip = originClips[(size_t) dragIndex];
                const int end = song.clipEnd (clip);

                if (trimLeft)
                    song.setClipRange (clip, std::min (at, end - least), end);
                else
                    song.setClipRange (clip, clip.start, std::max (at, clip.start + least));

                song.playlist[(size_t) dragIndex] = clip;
            }

            notifyPlaylist();
            break;
        }

        case Drag::fade:
        {
            if (! dragMoved || ! juce::isPositiveAndBelow (dragIndex, (int) song.audioClips.size()))
                break;

            // As long as it's dragged: from the corner to where the dot is let go
            auto& clip = song.audioClips[(size_t) dragIndex];
            const double t = juce::jlimit (0.0, clip.length, xToSeconds (clip, e.position.x));

            if (trimLeft)
                clip.fadeIn = juce::jlimit (0.0, clip.length - clip.fadeOut, t);
            else
                clip.fadeOut = juce::jlimit (0.0, clip.length - clip.fadeIn, clip.length - t);

            notifyPlaylist();
            break;
        }

        case Drag::point:
        {
            // On the snap, between its neighbours; up and down its control's range
            auto* lane = song.findLane (dragPoint.owner, dragPoint.param);

            if (! dragMoved || lane == nullptr || ! juce::isPositiveAndBelow (dragPoint.index, (int) lane->points.size()))
                break;

            const auto views = visibleLanes();
            const auto view = std::find_if (views.begin(), views.end(), [this] (auto& v) { return dragPoint.on (v); });

            if (view == views.end())
                break;

            const int i = dragPoint.index;
            int tick = snapped (xToTick (e.position.x), e.mods);

            if (i > 0)
                tick = std::max (tick, lane->points[(size_t) i - 1].tick);

            if (i + 1 < (int) lane->points.size())
                tick = std::min (tick, lane->points[(size_t) i + 1].tick);

            const float value = laneYToValue (*view, e.position.y);
            auto& point = lane->points[(size_t) i];

            if (point.tick != tick || std::abs (point.value - value) > 1.0e-6f)
            {
                point.tick = tick;
                point.value = value;
                song.notify (Song::Change::automation);
            }

            break;
        }

        case Drag::tempo:
        {
            // Up and down its tempo (whole bpm, Alt tenths); a change also along, on the snap and
            // between its neighbours (the start's stays at the start)
            if (dragTempo == noTempoPoint || dragTempo >= (int) song.tempoChanges.size())
                break;

            const double fine = e.mods.isAltDown() ? 10.0 : 1.0;
            const double bpm = std::round (yToTempo (e.position.y, dragTempoRange) * fine) / fine;

            if (dragTempo < 0)
            {
                song.bpm = bpm;
            }
            else
            {
                auto& t = song.tempoChanges[(size_t) dragTempo];
                const int lo = dragTempo > 0 ? song.tempoChanges[(size_t) dragTempo - 1].tick + 1 : 1;
                const int hi = dragTempo + 1 < (int) song.tempoChanges.size() ? song.tempoChanges[(size_t) dragTempo + 1].tick - 1
                                                                                : std::numeric_limits<int>::max();
                t.tick = juce::jlimit (lo, std::max (lo, hi), snapped (xToTick (e.position.x), e.mods));
                t.bpm = bpm;
            }

            song.notify (Song::Change::transport);
            break;
        }

        case Drag::box:
        {
            boxEnd = e.position;
            const auto box = juce::Rectangle<float> (boxStart, boxEnd);
            chosenClips = boxClips;
            chosenAudio = boxAudio;

            for (int i = 0; i < (int) song.playlist.size(); ++i)
                if (clipBounds (song.playlist[(size_t) i]).intersects (box))
                    chosenClips.insert (i);

            for (int i = 0; i < (int) song.audioClips.size(); ++i)
                if (clipBounds (song.audioClips[(size_t) i]).intersects (box))
                    chosenAudio.insert (i);

            repaint();
            break;
        }

        case Drag::none:
            break;
    }
}

void Playlist::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::loop && ! dragMoved && song.loopEnd > song.loopStart)
    {
        // A click on the strip switches the loop on or off
        song.loopOn = ! song.loopOn;
        song.notify (Song::Change::transport);
    }
    else if (drag == Drag::comp && ! dragMoved && juce::isPositiveAndBelow (dragIndex, (int) song.audioClips.size()))
    {
        // A click on a take uses all of it
        auto& clip = song.audioClips[(size_t) dragIndex];
        const auto& take = clip.takes[(size_t) dragLane];
        clip.setComp (take.from, take.to, dragLane);
        song.notify (Song::Change::playlist);
    }

    if (drag == Drag::move || drag == Drag::moveAudio)
        dropTracksLeftBehind();

    const bool boxed = drag == Drag::box || drag == Drag::point || drag == Drag::tempo;
    drag = Drag::none;
    dragIndex = dragLane = -1;
    dragPoint = {};
    dragTempo = noTempoPoint;
    originClips.clear();
    originAudio.clear();

    if (boxed)
        repaint();
}

void Playlist::automationMouseDown (const LaneView& lane, const juce::MouseEvent& e)
{
    // Points belong to the lane, not the clips: clicking here lets go of selected clips
    clearSelection();
    const int hit = pointAt (lane, e.position);

    if (e.mods.isPopupMenu())
    {
        // Right-click: a point goes; anywhere else, the lane's menu
        if (auto* data = song.findLane (lane.owner, lane.param); data != nullptr && hit >= 0)
        {
            song.checkpoint();
            data->points.erase (data->points.begin() + hit);
            chosenPoint = {};
            song.notify (Song::Change::automation);
        }
        else
        {
            showAutomationMenu (lane);
        }

        return;
    }

    song.checkpoint();
    int index = hit;

    if (index < 0)
    {
        // A new point here, on the snap (a lane only on show becomes the song's)
        const int tick = snapped (xToTick (e.position.x), e.mods);
        const float value = laneYToValue (lane, e.position.y);
        auto& data = song.laneFor (lane.owner, lane.param, audio.automationName (lane.owner, lane.param));
        auto at = std::upper_bound (data.points.begin(), data.points.end(), tick, [] (int t, auto& p) { return t < p.tick; });
        index = (int) std::distance (data.points.begin(), data.points.insert (at, { tick, value }));
        song.notify (Song::Change::automation);
    }

    chosenPoint = dragPoint = { lane.owner, lane.param, index };
    drag = Drag::point;
    repaint();
}

void Playlist::showAutomationMenu (const LaneView& lane)
{
    const int owner = lane.owner;
    const juce::String param = lane.param;
    const auto shown = laneParamsOf (owner);
    juce::PopupMenu menu, add;
    std::vector<std::pair<juce::String, juce::PopupMenu>> groups;

    // A lane for another of its controls (a channel showing only its volume keeps that lane too)
    for (auto& target : audio.automationTargets (owner))
    {
        const bool there = std::find (shown.begin(), shown.end(), target.param) != shown.end();
        juce::PopupMenu::Item item (target.name);
        item.isTicked = there;
        item.isEnabled = ! there;
        item.action = [this, owner, target]
        {
            song.checkpoint();

            if (song.lanesOf (owner).empty())
                song.laneFor (owner, "volume", audio.automationName (owner, "volume"));

            song.laneFor (owner, target.param, audio.automationName (owner, target.param));
            song.notify (Song::Change::automation);
        };

        if (target.group.isEmpty())
        {
            add.addItem (std::move (item));
            continue;
        }

        auto group = std::find_if (groups.begin(), groups.end(), [&target] (auto& g) { return g.first == target.group; });

        if (group == groups.end())
            group = groups.emplace (groups.end(), target.group, juce::PopupMenu());

        group->second.addItem (std::move (item));
    }

    for (auto& [name, items] : groups)
        add.addSubMenu (name, items);

    const auto* data = song.findLane (owner, param);
    menu.addSectionHeader (laneName (lane));
    menu.addSubMenu ("Add a lane for", add);
    menu.addItem ("Clear this lane's points", data != nullptr && ! data->points.empty(), false, [this, owner, param]
    {
        if (auto* l = song.findLane (owner, param))
        {
            song.checkpoint();
            l->points.clear();
            song.notify (Song::Change::automation);
        }
    });
    menu.addItem ("Remove this lane", data != nullptr, false, [this, owner, param]
    {
        song.checkpoint();
        std::erase_if (song.automation, [owner, param] (auto& l) { return l.owner == owner && l.param == param; });
        song.notify (Song::Change::automation);
    });
    menu.addSeparator();
    menu.addItem ("Song mode plays automation; Pattern mode leaves every control where it is", false, false, nullptr);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (lane.header.toNearestInt().withWidth (headerWidth) + getScreenPosition()));
}

void Playlist::mouseDoubleClick (const juce::MouseEvent& e)
{
    // FL: a track's name opens its channel, or its pattern when it plays several (renaming
    // is in the right-click menu)
    if (headerArea.contains (e.getPosition()))
    {
        const int row = yToRow (e.position.y);

        if (row >= song.numTracks() || trackButtonAt (row, e.position).has_value() || iconBounds (row).contains (e.position))
            return;

        const auto ids = channelsOn (row);

        if (ids.size() == 1 && onOpenChannel != nullptr)
            onOpenChannel (ids.front());
        else if (ids.size() > 1 && onOpenPattern != nullptr)
            onOpenPattern (firstPatternOn (row));

        return;
    }

    if (! gridArea.contains (e.getPosition()))
        return;

    auto hit = clipAt (e.position);

    // Empty space on an instrument's track (with the select tool, which doesn't paint on a
    // click): a clip of its pattern there, opened
    if (! hit.found() && tool == Tool::select)
    {
        const int row = yToRow (e.position.y);

        if (row < song.numTracks() && song.trackChannel (row) != 0)
        {
            const int start = snapped (xToTick (e.position.x), e.mods, -1);
            song.checkpoint();

            if (const int patternId = patternToPaint (row, start); patternId != 0)
            {
                song.playlist.push_back ({ patternId, row, start });
                song.currentPatternId = patternId;
                song.notify (Song::Change::patterns);
                song.notify (Song::Change::playlist);
                song.notify (Song::Change::currentPattern);
                hit = clipAt (e.position);
            }
        }
    }

    if (hit.audio && hit.lane < 0 && ! hit.toggle && song.audioClips[(size_t) hit.index].takes.size() > 1)
    {
        auto& clip = song.audioClips[(size_t) hit.index];
        clip.expanded = ! clip.expanded;
        song.notify (Song::Change::selection);
    }
    else if (hit.found() && ! hit.audio && onOpenPattern != nullptr)
    {
        onOpenPattern (song.playlist[(size_t) hit.index].patternId);
    }
}

void Playlist::showLaneMenu (int folder, int lane)
{
    auto& clip = song.audioClips[(size_t) folder];
    const auto name = "Take " + juce::String (lane + 1);
    juce::PopupMenu menu;
    menu.addSectionHeader (name);

    menu.addItem ("Use " + name + " all the way through", [this, folder, lane]
    {
        if (juce::isPositiveAndBelow (folder, (int) song.audioClips.size()))
        {
            song.checkpoint();
            auto& c = song.audioClips[(size_t) folder];
            c.comp.clear();
            c.setComp (c.takes[(size_t) lane].from, c.takes[(size_t) lane].to, lane);
            song.notify (Song::Change::playlist);
        }
    });

    menu.addItem ("Delete " + name, clip.takes.size() > 1, false, [this, folder, lane]
    {
        if (juce::isPositiveAndBelow (folder, (int) song.audioClips.size()))
        {
            song.checkpoint();
            song.audioClips[(size_t) folder].removeTake (lane);
            song.notify (Song::Change::playlist);
        }
    });

    menu.addItem ("Keep only the comp (delete the unused takes)", [this, folder]
    {
        if (juce::isPositiveAndBelow (folder, (int) song.audioClips.size()))
        {
            song.checkpoint();
            song.audioClips[(size_t) folder].keepOnlyComp();
            song.notify (Song::Change::playlist);
        }
    });

    menu.addSeparator();
    menu.addItem ("Hide the takes", [this, folder]
    {
        if (juce::isPositiveAndBelow (folder, (int) song.audioClips.size()))
        {
            song.audioClips[(size_t) folder].expanded = false;
            song.notify (Song::Change::selection);
        }
    });

    menu.showMenuAsync ({});
}

void Playlist::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())     // Cmd on a Mac
    {
        const double anchor = xToTick (e.position.x);
        pxPerTick = juce::jlimit (12.0 / (ticksPerBeat * 4), 400.0 / (ticksPerBeat * 4), pxPerTick * (w.deltaY > 0 ? 1.2 : 1.0 / 1.2));
        scrollTicks = std::max (0.0, anchor - (e.position.x - gridArea.getX()) / pxPerTick);
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float delta = std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY;
        scrollTicks = std::max (0.0, scrollTicks - delta * 300.0 / pxPerTick * 0.25);
    }
    else
    {
        const int visibleRows = gridArea.getHeight() / rowHeight;
        firstRow = juce::jlimit (0, std::max (0, song.numTracks() + 1 - visibleRows), firstRow - (w.deltaY > 0 ? 1 : -1));
    }

    updateScrollBars();
    repaint();
}

//==============================================================================
bool Playlist::isInterestedInDragSource (const SourceDetails& details)
{
    const auto d = details.description.toString();
    return d.startsWith (soundDragPrefix) || d.startsWith (channelDragPrefix) || d.startsWith (patternDragPrefix);
}

Playlist::DropTarget Playlist::dropTargetAt (juce::Point<int> p, const juce::String& description) const
{
    DropTarget t;

    if (p.y < gridArea.getY() || p.y >= gridArea.getBottom())
        return t;

    const int row = yToRow ((float) p.y);

    // A pattern: a clip of it starting where it's let go (snapped, Alt finer, as painting
    // does; over the headers, the first snap in view), on the track under it. Under the last
    // track it gets a new one; patterns don't go on audio tracks, so there it gets a new one
    // just under
    if (description.startsWith (patternDragPrefix))
    {
        if (song.findPattern (description.fromFirstOccurrenceOf (patternDragPrefix, false, false).getIntValue()) == nullptr)
            return t;

        const auto mods = juce::ModifierKeys::getCurrentModifiers();
        t.tick = p.x >= gridArea.getX() ? snapped (xToTick ((float) p.x), mods, -1) : snapped (scrollTicks, mods, 1);

        if (row >= song.numTracks())
        {
            t.kind = DropTarget::Kind::newTrack;
            t.row = song.numTracks();
        }
        else if (auto c = song.findChannel (song.trackChannel (row)); c != nullptr && c->type == InstrumentType::audio)
        {
            t.kind = DropTarget::Kind::newTrack;
            t.row = row + 1;
        }
        else
        {
            t.kind = DropTarget::Kind::clip;
            t.row = row;
        }

        return t;
    }

    // A sample from the sidebar's Samples: an audio clip of it starting where it's let go (snapped, as a pattern),
    // on the audio track under it, on an empty track, or on a new audio track (under the last track, or just under
    // any other)
    if (description.startsWith (juce::String (soundDragPrefix) + "sample:"))
    {
        if (! juce::File (description.fromFirstOccurrenceOf ("sample:", false, false)).existsAsFile())
            return t;

        const auto mods = juce::ModifierKeys::getCurrentModifiers();
        t.tick = p.x >= gridArea.getX() ? snapped (xToTick ((float) p.x), mods, -1) : snapped (scrollTicks, mods, 1);
        t.kind = DropTarget::Kind::newTrack;
        t.row = std::min (row + 1, song.numTracks());

        if (row < song.numTracks())
        {
            if (auto c = song.findChannel (song.trackChannel (row)); c != nullptr && c->type == InstrumentType::audio)
                t = { DropTarget::Kind::clip, row, t.tick };
            else if (c == nullptr && song.isTrackEmpty (row))
                t = { DropTarget::Kind::assign, row, t.tick };
        }

        return t;
    }

    const bool isEffect = description.startsWith (juce::String (soundDragPrefix) + "effect:");
    const bool isChannel = description.startsWith (channelDragPrefix);

    if (isChannel && song.findChannel (description.fromFirstOccurrenceOf (channelDragPrefix, false, false).getIntValue()) == nullptr)
        return t;

    // An effect goes on a track's channel
    if (isEffect)
    {
        if (row < song.numTracks() && ! song.channelsOnTrack (row).empty())
            t = { DropTarget::Kind::effect, row };

        return t;
    }

    // Under the last track, or on the line between two: a new track there
    if (row >= song.numTracks())
        return { DropTarget::Kind::newTrack, song.numTracks() };

    const float top = rowToY (row), bottom = top + (float) rowHeightFor (row);

    if ((float) p.y < top + 8.0f)
        return { DropTarget::Kind::newTrack, row };

    if ((float) p.y > bottom - 8.0f)
        return { DropTarget::Kind::newTrack, row + 1 };

    // On an instrument's track, a sound takes over from its instrument; an empty track takes
    // whatever lands on it; anything else gets a track of its own under this one
    auto c = song.findChannel (song.trackChannel (row));

    if (c == nullptr && song.isTrackEmpty (row))
        return { DropTarget::Kind::assign, row };

    if (! isChannel && c != nullptr && c->type != InstrumentType::audio)
        return { DropTarget::Kind::replace, row };

    return { DropTarget::Kind::newTrack, row + 1 };
}

void Playlist::setDrop (DropTarget target, const juce::String& description)
{
    juce::String what;
    int patternId = 0;

    if (description.startsWith (channelDragPrefix))
    {
        if (auto c = song.findChannel (description.fromFirstOccurrenceOf (channelDragPrefix, false, false).getIntValue()))
            what = c->name;
    }
    else if (description.startsWith (patternDragPrefix))
    {
        if (auto p = song.findPattern (description.fromFirstOccurrenceOf (patternDragPrefix, false, false).getIntValue()))
        {
            what = p->name;
            patternId = p->id;
        }
    }
    else if (description.isNotEmpty() && nameOfSound != nullptr)
    {
        what = nameOfSound (description);
    }

    // A sample: drawn where it would land, as long as it plays (its length read once, as it comes over)
    const bool sample = description.startsWith (juce::String (soundDragPrefix) + "sample:");
    const auto samplePath = sample && target.kind != DropTarget::Kind::none ? description.fromFirstOccurrenceOf ("sample:", false, false) : juce::String();
    const bool sampleChanged = samplePath != dropSample;

    if (sampleChanged)
    {
        dropSample = samplePath;
        dropSampleSeconds = samplePath.isNotEmpty() ? samples::lengthInSeconds (juce::File (samplePath)) : 0.0;
    }

    // Where a pattern or a sample would start: bar 5, or bar 5 beat 3 off the bar
    juce::String when;

    if (patternId != 0 || sample)
    {
        when = " at " + describeTick (target.tick).replace (", beat", " beat");
    }

    juce::String label;

    switch (target.kind)
    {
        case DropTarget::Kind::newTrack:
            label = patternId != 0 ? what + " on a new track" + when
                  : sample ? what + " on a new audio track" + when
                           : "New track" + (what.isNotEmpty() ? ": " + what : juce::String());
            break;

        case DropTarget::Kind::clip:     label = what + " on " + trackInfo (target.row).name + when; break;
        case DropTarget::Kind::replace:  label = "Play " + trackInfo (target.row).name + " with " + what + " instead"; break;
        case DropTarget::Kind::assign:   label = "Put " + what + " on this track" + (sample ? when : juce::String()); break;

        case DropTarget::Kind::effect:
        {
            const int ch = song.trackChannel (target.row);
            const auto ids = song.channelsOnTrack (target.row);
            auto c = song.findChannel (ch != 0 ? ch : ids.front());
            label = "Add " + what + " to " + (c != nullptr ? c->name : juce::String ("this track"));
            break;
        }

        case DropTarget::Kind::none:
            break;
    }

    if (target.kind != drop.kind || target.row != drop.row || target.tick != drop.tick || label != dropLabel || patternId != dropPatternId
         || sampleChanged)
    {
        drop = target;
        dropLabel = label;
        dropPatternId = target.kind != DropTarget::Kind::none ? patternId : 0;
        repaint();
    }
}

void Playlist::itemDragEnter (const SourceDetails& details)
{
    itemDragMove (details);
}

void Playlist::itemDragMove (const SourceDetails& details)
{
    const auto d = details.description.toString();
    setDrop (dropTargetAt (details.localPosition, d), d);
}

void Playlist::itemDragExit (const SourceDetails&)
{
    setDrop ({}, {});
}

void Playlist::itemDropped (const SourceDetails& details)
{
    const auto d = details.description.toString();
    const auto target = dropTargetAt (details.localPosition, d);
    setDrop ({}, {});

    if (target.kind == DropTarget::Kind::none)
        return;

    // A pattern from the sidebar: a clip of it there (on a new track if it asks for one),
    // as painting makes one; one undo takes both away
    if (d.startsWith (patternDragPrefix))
    {
        const int id = d.fromFirstOccurrenceOf (patternDragPrefix, false, false).getIntValue();

        if (song.findPattern (id) == nullptr)
            return;

        song.checkpoint();
        int row = target.row;

        if (target.kind == DropTarget::Kind::newTrack)
            row = song.insertTrack ({}, target.row);

        song.playlist.push_back ({ id, row, target.tick });

        if (song.currentPatternId != id)
        {
            song.currentPatternId = id;
            song.notify (Song::Change::currentPattern);
        }

        song.notify (Song::Change::playlist);

        if (const int channelId = song.trackChannel (row); channelId != 0)
            selectChannel (channelId);

        return;
    }

    // A sample from the sidebar: an audio clip of the whole file where it was let go, on the audio track there or a
    // new one named after it (not armed: it plays the sample); one undo takes it all away
    if (d.startsWith (juce::String (soundDragPrefix) + "sample:"))
    {
        const juce::File file (d.fromFirstOccurrenceOf ("sample:", false, false));
        const double seconds = samples::lengthInSeconds (file);

        if (seconds <= 0.0)
            return;

        song.checkpoint();
        int row = target.row, channelId = 0;

        if (target.kind == DropTarget::Kind::clip)
        {
            channelId = song.trackChannel (row);
        }
        else
        {
            // A second "Kick 01" is "Kick 01 2"
            const auto base = file.getFileNameWithoutExtension();
            auto name = base;

            for (int n = 2; std::any_of (song.channels.begin(), song.channels.end(), [&name] (const Channel& c) { return c.name == name; }); ++n)
                name = base + " " + juce::String (n);

            auto& c = song.addChannel (name, InstrumentType::audio);
            c.input = audio.getInputNames()[0];
            channelId = c.id;

            if (target.kind == DropTarget::Kind::assign && juce::isPositiveAndBelow (row, song.numTracks()))
            {
                song.tracks[(size_t) row].channelId = channelId;
            }
            else
            {
                Track t;
                t.channelId = channelId;
                row = song.insertTrack (t, target.row);
            }
        }

        AudioClip clip;
        clip.channelId = channelId;
        clip.track = row;
        clip.start = target.tick;
        clip.length = seconds;
        clip.takes.push_back ({ file.getFullPathName(), 0.0, 0.0, seconds });
        clip.comp.push_back ({ 0.0, seconds, 0 });
        song.audioClips.push_back (clip);
        song.selectedChannelId = channelId;
        song.notify (Song::Change::channels);
        song.notify (Song::Change::playlist);
        selectChannel (channelId);
        return;
    }

    // A channel from Steps: a track for it
    if (d.startsWith (channelDragPrefix))
    {
        const int id = d.fromFirstOccurrenceOf (channelDragPrefix, false, false).getIntValue();

        if (song.findChannel (id) == nullptr)
            return;

        song.checkpoint();

        if (target.kind == DropTarget::Kind::assign)
        {
            song.tracks[(size_t) target.row].channelId = id;
        }
        else
        {
            Track t;
            t.channelId = id;
            song.insertTrack (t, target.row);
        }

        song.notify (Song::Change::playlist);
        selectChannel (id);
        return;
    }

    switch (target.kind)
    {
        case DropTarget::Kind::newTrack:
        case DropTarget::Kind::assign:
        {
            // The new channel's undo step covers its track too
            const int id = onAddSound != nullptr ? onAddSound (d) : 0;

            if (id == 0)
                return;

            if (target.kind == DropTarget::Kind::assign && juce::isPositiveAndBelow (target.row, song.numTracks()))
            {
                song.tracks[(size_t) target.row].channelId = id;
            }
            else
            {
                Track t;
                t.channelId = id;
                song.insertTrack (t, target.row);
            }

            song.notify (Song::Change::playlist);
            selectChannel (id);
            break;
        }

        case DropTarget::Kind::replace:
        {
            const int ch = song.trackChannel (target.row);

            if (onReplaceSound != nullptr && onReplaceSound (ch, d))
                selectChannel (ch);

            break;
        }

        case DropTarget::Kind::effect:
        {
            // The track's channel; on a pattern of several, the selected one if it's there
            const auto ids = song.channelsOnTrack (target.row);
            int ch = song.trackChannel (target.row);

            if (ch == 0)
                ch = std::find (ids.begin(), ids.end(), song.selectedChannelId) != ids.end() ? song.selectedChannelId : ids.front();

            selectChannel (ch);

            if (onAddEffect != nullptr)
                onAddEffect (ch, d);

            break;
        }

        case DropTarget::Kind::clip:     // patterns, handled above
        case DropTarget::Kind::none:
            break;
    }
}

void Playlist::paintDrop (juce::Graphics& g)
{
    if (drop.kind == DropTarget::Kind::none)
        return;

    float labelY;

    if (dropPatternId != 0 || dropSample.isNotEmpty())
    {
        // A pattern or a sample: the track it lands on lit faintly (a new one: its line), and the
        // clip itself, see-through, where it would go
        const float top = rowToY (drop.row);

        if (drop.kind == DropTarget::Kind::newTrack)
        {
            const float y = juce::jlimit ((float) gridArea.getY(), (float) gridArea.getBottom() - 2.0f, top - 1.0f);
            g.setColour (accentCool);
            g.fillRoundedRectangle (4.0f, y - 1.5f, (float) getWidth() - 18.0f, 3.0f, 1.5f);
            g.fillEllipse (2.0f, y - 4.0f, 8.0f, 8.0f);
        }
        else
        {
            g.setColour (accentCool.withAlpha (0.07f));
            g.fillRect (juce::Rectangle<float> (0.0f, top, (float) getWidth() - 10.0f, (float) rowHeight));
        }

        const PlaylistClip ghost { dropPatternId, drop.row, drop.tick };
        juce::Rectangle<float> r;

        if (dropPatternId != 0)
        {
            r = clipBounds (ghost);
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (gridArea);
            g.beginTransparencyLayer (0.75f);
            paintClip (g, ghost, false, false);
            g.endTransparencyLayer();
            g.setColour (accentCool);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 2.0f);
        }
        else
        {
            // A sample: a see-through clip as long as it plays
            const float from = tickToX (drop.tick), to = tickToX (song.tickAt (song.secondsAt (drop.tick) + dropSampleSeconds));
            r = { from, top + 3.0f, std::max (12.0f, to - from), (float) rowHeight - 6.0f };
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (gridArea);
            g.setColour (accentCool.withAlpha (0.2f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (accentCool);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 2.0f);
        }

        if (dropLabel.isEmpty())
            return;

        // The label beside the clip: after it if there's room, else before it
        const auto f = font (16.0f, true);
        const float w = juce::GlyphArrangement::getStringWidth (f, dropLabel) + 24.0f;
        float x = r.getRight() + 10.0f;

        if (x + w > (float) gridArea.getRight() - 4.0f)
            x = std::max ((float) gridArea.getX() + 4.0f, r.getX() - 10.0f - w);

        auto chip = juce::Rectangle<float> (x, r.getCentreY() - 12.0f, w, 24.0f);
        chip = chip.withY (juce::jlimit ((float) gridArea.getY() + 2.0f, (float) gridArea.getBottom() - 26.0f, chip.getY()));
        g.setColour (raised);
        g.fillRoundedRectangle (chip, 7.0f);
        g.setColour (accentCool);
        g.drawRoundedRectangle (chip.reduced (0.5f), 7.0f, 1.0f);
        g.setColour (text);
        g.setFont (f);
        g.drawText (dropLabel, chip, juce::Justification::centred, true);
        return;
    }

    if (drop.kind == DropTarget::Kind::newTrack)
    {
        // A line where the new track goes in
        const float y = juce::jlimit ((float) gridArea.getY(), (float) gridArea.getBottom() - 2.0f, rowToY (drop.row) - 1.0f);
        g.setColour (accentCool);
        g.fillRoundedRectangle (4.0f, y - 1.5f, (float) getWidth() - 18.0f, 3.0f, 1.5f);
        g.fillEllipse (2.0f, y - 4.0f, 8.0f, 8.0f);
        labelY = y + 6.0f;
    }
    else
    {
        // The track it lands on, lit
        const auto r = juce::Rectangle<float> (0.0f, rowToY (drop.row), (float) getWidth() - 10.0f, (float) rowHeight);
        g.setColour (accentCool.withAlpha (0.13f));
        g.fillRect (r);
        g.setColour (accentCool);
        g.drawRect (r, 2.0f);
        labelY = r.getCentreY() - 12.0f;
    }

    if (dropLabel.isEmpty())
        return;

    const auto f = font (16.0f, true);
    const float w = juce::GlyphArrangement::getStringWidth (f, dropLabel) + 24.0f;
    auto chip = juce::Rectangle<float> ((float) headerWidth + 14.0f, labelY, w, 24.0f);
    chip = chip.withY (juce::jlimit ((float) gridArea.getY() + 2.0f, (float) gridArea.getBottom() - 26.0f, chip.getY()));
    g.setColour (raised);
    g.fillRoundedRectangle (chip, 7.0f);
    g.setColour (accentCool);
    g.drawRoundedRectangle (chip.reduced (0.5f), 7.0f, 1.0f);
    g.setColour (text);
    g.setFont (f);
    g.drawText (dropLabel, chip, juce::Justification::centred, true);
}

void Playlist::refreshPlayhead()
{
    const double now = audio.isPlaying() && song.mode == PlayMode::song ? audio.getPlayheadTicks() : -1.0;
    const bool recording = audio.isRecording();

    if (now != playheadTicks || recording != recordingShown)
    {
        playheadTicks = now;
        recordingShown = recording;
        repaint();
    }
}

} // namespace nightshift
