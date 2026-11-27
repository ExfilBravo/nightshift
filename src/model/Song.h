// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

namespace nightshift
{

// A tick is 1/96 of a quarter note whatever the time signature; a step is a 16th. How long a
// bar is depends on the time signature where it is (Song::barStart; a pattern's own, Song::meterOf)
constexpr int ticksPerBeat   = 96;
constexpr int ticksPerStep   = ticksPerBeat / 4;
constexpr int defaultRootKey = 60;   // FL Studio calls MIDI 60 "C5"

/** FL-style note name, where MIDI 60 is C5. */
juce::String noteName (int key);

/** Knob position to dB. Channels are unity at 0.78 and the master at 0.8, as in FL. */
inline float channelVolumeToDb (float v)   { return v <= 0.001f ? -100.0f : 40.0f * std::log10 (v / 0.78f); }
inline float masterVolumeToDb (float v)    { return v <= 0.001f ? -100.0f : 40.0f * std::log10 (v / 0.8f); }

//==============================================================================
struct Note
{
    int start = 0;                   // ticks from the pattern start
    int length = ticksPerStep;
    int key = defaultRootKey;
    int velocity = 100;              // 1..127

    int end() const                  { return start + length; }
    bool operator== (const Note&) const = default;
};

/** sampler/synth/plugin/kit channels play notes; an audio channel is a track you record into.
    A kit channel plays a multi-sampled drum kit, and has an AI drummer to write for it. A midiOut
    channel plays a synth plugged in: its notes go out of a MIDI output, in time with the song. */
enum class InstrumentType { sampler, synth, plugin, audio, kit, instrument, midiOut };

/** An effect plugin in a channel's chain or the master's. */
struct Effect
{
    int id = 0;                      // unique in the song, so editors and the engine can follow it
    juce::String pluginId;           // juce::PluginDescription identifier string
    juce::String name;
    juce::String state;              // the plugin's own state, restored on load
    bool bypassed = false;
};

/** Some of a channel's sound sent on to a bus as well as its own output, like Logic's sends
    (a reverb shared by several channels, say). */
struct Send
{
    int busId = 0;
    float level = 0.78f;             // like a fader: 0.78 is 0 dB
    bool preFader = false;           // taken before the channel's fader, so the fader doesn't change it
};

/** Logic's aux, an FL mixer track: channels play into it through their output or a send,
    then its effects and fader go on to the master. */
struct Bus
{
    int id = 0;                      // from the same counter as channels, so they never clash
    juce::String name;
    juce::Colour colour;
    float volume = 0.78f;
    float pan = 0.0f;
    bool muted = false;
    std::vector<Effect> effects;
};

/** One request to the AI drummer: what was asked, what it said, and the groove it wrote. */
struct DrummerTurn
{
    juce::String prompt, reply;
    juce::String groove;             // the groove as written (JSON: sections of bars of drum lanes)
    juce::String answer;             // what the offline drummer understood (JSON), for a follow-up to build on
};

/** A channel's AI drummer: the conversation, which version is in the patterns, and the feel
    controls that reshape it without asking again. */
struct Drummer
{
    std::vector<DrummerTurn> turns;
    int shown = -1;                  // the turn whose groove is in the patterns
    float swing = -1.0f;             // -1: as written; else 0..1 (straight to full triplet)
    float looseness = 1.0f;          // scales the written timing feel; 0 is on the grid
    float dynamics = 1.0f;           // scales how far velocities spread from the loudest
    std::map<juce::String, int> patterns;   // section name -> pattern id
    bool pickKit = false;            // made for the drummer, not for a kit you picked: its first answer chooses the kit (not saved)
};

/** One request to the AI bassist: what was asked, what it said, and the part it wrote. */
struct BassTurn
{
    juce::String prompt, reply;
    juce::String part;               // the part as written (JSON: sections of bars of notes)
    juce::String answer;             // what the offline bassist understood (JSON), for a follow-up to build on
};

/** A bass channel's AI bassist: its versions, which one is in the patterns, and those patterns. */
struct Bassist
{
    std::vector<BassTurn> turns;
    int shown = -1;                  // the turn whose part is in the patterns
    std::map<juce::String, int> patterns;   // section name -> pattern id
    bool pickSound = false;          // made for the bassist: its first answer chooses the sound (not saved)
};

/** One request to the AI guitarist: what was asked, what it said, and the part it wrote. */
struct GuitarTurn
{
    juce::String prompt, reply;
    juce::String part;               // the part as written (JSON: sections of bars of fingered notes)
    juce::String answer;             // what the offline guitarist understood (JSON), for a follow-up to build on
};

/** A guitar channel's AI guitarist: its versions, which one is in the patterns, and those patterns. */
struct Guitarist
{
    std::vector<GuitarTurn> turns;
    int shown = -1;
    std::map<juce::String, int> patterns;   // section name -> pattern id
    bool pickSound = false;          // made for the guitarist: its first answer chooses the sound (not saved)
};

/** One request to the AI keys player: what was asked, what it said, and the part it wrote. */
struct KeysTurn
{
    juce::String prompt, reply;
    juce::String part;               // the part as written (JSON: sections of bars of notes for each hand)
    juce::String answer;             // what the offline keys player understood (JSON), for a follow-up to build on
};

/** A keys channel's AI player: its versions, which one is in the patterns, and those patterns. */
struct KeysPlayer
{
    std::vector<KeysTurn> turns;
    int shown = -1;
    std::map<juce::String, int> patterns;   // section name -> pattern id
    bool pickSound = false;          // made for the keys player: its first answer chooses the sound (not saved)
};

/** One request to the AI synth player: what was asked, what it said, and the part it wrote. */
struct SynthTurn
{
    juce::String prompt, reply;
    juce::String part;               // the part as written (JSON: sections of bars of notes, its role and sound)
    juce::String answer;             // what the offline synth player understood (JSON), for a follow-up to build on
};

/** A synth channel's AI player: its versions, which one is in the patterns, and those patterns. */
struct SynthPlayer
{
    std::vector<SynthTurn> turns;
    int shown = -1;
    std::map<juce::String, int> patterns;   // section name -> pattern id
    bool pickSound = false;          // made for the synth player: its first answer chooses the sound (not saved)
};

struct Channel
{
    int id = 0;
    juce::String name;
    juce::Colour colour;
    InstrumentType type = InstrumentType::sampler;

    juce::String sample;             // sampler: a file path, or "kit:<name>" for the built-in kit
    int rootKey = defaultRootKey;    // sampler: the key that plays the sample unpitched
    bool oneShot = true;             // sampler: ignore note length and play the whole sample

    juce::String synthPreset;        // synth: built-in preset name
    juce::String pluginId;           // plugin: juce::PluginDescription identifier string
    juce::String pluginState;        // synth/plugin: engine plugin state, restored on load

    juce::String kit;                // kit: the drum kit (a folder under drumkits::folder())
    juce::String instrument;         // instrument: a sampled instrument's SFZ (under SfzPlugin::folder())

    juce::String midiOutDevice;      // midiOut: the MIDI output it plays, by the name the system gives it
    int midiOutChannel = 1;          // midiOut: the MIDI channel its notes go out on, 1..16
    int midiProgram = -1;            // the sound a synth is asked for (a program change, 0..127) before it
                                     // plays, -1 for none: a MIDI file's comes in here and goes out again
    Drummer drummer;                 // kit (or a drum plugin): the AI drummer writing for it
    Bassist bass;                    // synth (or a bass plugin): the AI bassist writing for it
    Guitarist guitar;                // a guitar: the AI guitarist writing for it
    KeysPlayer keys;                 // a piano, electric piano or organ: the AI keys player writing for it
    SynthPlayer synthPlayer;         // a synth: the AI synth player writing for it (a lead, pad, arp...)

    juce::String input;              // audio: the input it records from, e.g. "Input 1"
    bool monitor = false;            // audio: hear the input through Nightshift while armed
    bool armed = false;              // audio: records on the next Record (not saved)

    float volume = 0.78f;            // 0..1, 0.78 is unity gain (as in FL)
    float pan = 0.0f;                // -1..1
    bool muted = false;

    std::vector<Effect> effects;     // in order, after the instrument (or the input), before volume and pan

    int output = 0;                  // the bus it plays into, or 0 for the master
    std::vector<Send> sends;         // at most one per bus
};

/** One recorded pass. Several takes can share a file: loop recording records one file and
    slices it, a pass per take. Times are seconds from the start of the folder holding it. */
struct Take
{
    juce::String file;               // absolute path to the WAV
    double fileOffset = 0.0;         // position in the file at the folder's start (may be negative)
    double from = 0.0, to = 0.0;     // where in the folder this take has audio
};

/** Which take is heard over [from, to), in seconds from the folder start. */
struct CompSegment
{
    double from = 0.0, to = 0.0;
    int take = 0;
};

/** Recorded audio in the playlist: a take folder, like Logic's. A single recording is a
    folder with one take. The comp picks which take plays where; gaps are silent. Its
    length is real time, so it keeps its speed when the tempo changes; only its start is
    in ticks.

    Stretched (Logic's Flex Time, FL's stretched audio clips) it plays faster or slower without
    its pitch changing, or higher or lower without its length changing. Following the tempo, its
    beats stay on the song's beats whatever the tempo does. Its own times (length, takes, comp,
    fades) stay in its audio's seconds either way: Song::audioTickAt says where they land. */
struct AudioClip
{
    int channelId = 0;
    int track = 0;                   // playlist row
    int start = 0;                   // ticks
    double length = 0.0;             // seconds of its own audio
    std::vector<Take> takes;
    std::vector<CompSegment> comp;
    bool expanded = false;           // take lanes showing in the playlist
    double fadeIn = 0.0, fadeOut = 0.0;   // seconds it takes to come in from silence, and to go back out

    bool followTempo = false;        // its beats stay on the song's when the tempo changes
    double sourceBpm = 0.0;          // the tempo its audio was played at (a recording's, a loop's); 0: not known
    double speed = 1.0;              // how much faster than it was played (stretched to fit: 2 is twice as fast)
    float pitch = 0.0f;              // semitones up (down if negative), its length kept

    static constexpr double slowest = 0.25, fastest = 4.0;     // how far it stretches either way

    /** Makes `take` the one heard over [from, to), trimming whatever was there. */
    void setComp (double from, double to, int take);

    /** The take heard at `t` seconds into the folder, or -1 where it is silent. */
    int takeAt (double t) const;

    void removeTake (int index);

    /** Deletes the takes the comp doesn't use (their files stay on disk). */
    void keepOnlyComp();
};

struct Pattern
{
    int id = 0;
    juce::String name;
    juce::Colour colour;
    std::map<int, std::vector<Note>> notes;   // keyed by channel id

    // Its own time signature (FL's pattern time signature), for a part in another one (a 7/8
    // breakdown); 0: the song's at its start (Song::meterOf)
    int meterBeats = 0, meterUnit = 0;

    std::vector<Note>& notesFor (int channelId)           { return notes[channelId]; }
    const std::vector<Note>* findNotes (int channelId) const;

    /** Whole bars (of `barTicks`), at least one, long enough to hold every note. */
    int lengthInTicks (int barTicks) const;
    bool isEmpty() const;
};

/** A pattern placed in Arrange. It plays its whole pattern until it's split or trimmed; then it
    plays `length` ticks of it, from `offset` ticks in (a split clip's second half starts part
    way through its pattern). */
struct PlaylistClip
{
    int patternId = 0;
    int track = 0;
    int start = 0;                   // ticks
    int offset = 0;                  // ticks into its pattern where it starts playing
    int length = 0;                  // ticks it plays for; 0: on to the end of its pattern
};

/** A point on an automation lane: the parameter's value at a moment of the song, and how the
    line on to the next point bends (FL's curves): 0 straight; towards 1 it eases in (it starts
    slowly and gets there quickly), towards -1 it eases out. */
struct AutomationPoint
{
    int tick = 0;
    float value = 0.0f;              // in the parameter's own units (pan -1..1, a fader's 0..1...)
    float curve = 0.0f;              // the bend of the line from here to the next point, -1..1
    bool operator== (const AutomationPoint&) const = default;
};

/** The shape of a bent automation line. A bend is where the line's middle (in time) gets to: a
    bend of `c` puts it (1 - c) / 2 of the way from the first value to the second, so dragging the
    middle of a line up or down sets its bend. Easing in is a power curve, easing out its mirror. */
namespace automationcurve
{
    constexpr float most = 0.95f;    // as far as a line bends either way (its middle 2.5% of the way)

    /** How far along a line its value has got (0..1) at `along` (0..1) of the way in time. */
    float shape (float along, float curve);

    /** The bend that puts the middle `fraction` (0..1) of the way, and back. */
    inline float fromMiddle (float fraction)        { return juce::jlimit (-most, most, 1.0f - 2.0f * fraction); }
    inline float middleOf (float curve)             { return (1.0f - curve) * 0.5f; }
}

/** One parameter changing over the song (Logic's automation), drawn as a lane under its
    channel's track in Arrange, or along the bottom for a bus's and the master's. Song mode plays
    it; in Pattern mode the parameter stays where its control is. Its parameter, by key:
    "volume", "pan", "send:<bus id>", "fx:<effect id>:<parameter id>", "instrument:<parameter id>";
    a bus has "volume", "pan" and "fx:...", the master (owner 0) "volume" and "fx:...". */
struct AutomationLane
{
    int owner = 0;                   // the channel or bus, or 0 for the master
    juce::String param;
    juce::String name;               // what it was called, for when its plugin isn't there
    std::vector<AutomationPoint> points;     // in time order

    /** Its value at `tick`: lines between points (bent as each point says), level before the
        first and after the last. */
    float valueAt (double tick) const;
};

/** A knob or fader on a MIDI controller linked to one of NightShift's (MIDI learn): moving it
    moves that control. The control is named as an automation lane's parameter is. */
struct ControllerLink
{
    juce::String device;             // the MIDI input it comes from
    int midiChannel = 1;             // 1..16
    int controller = 0;              // the CC number
    int owner = 0;                   // the channel or bus, or 0 for the master
    juce::String param;              // "volume", "pan", "send:<bus id>", "fx:<effect id>:<parameter id>", "instrument:<parameter id>"
};

/** A tempo change along the song (Logic's tempo track): from `tick` it plays at `bpm`, quarter
    notes a minute. It jumps there, or with `glide` gets there gradually, from the tempo before,
    over the stretch since the change before (an accelerando or a ritardando). */
struct TempoChange
{
    int tick = 0;
    double bpm = 120.0;
    bool glide = false;
    bool operator== (const TempoChange&) const = default;
};

/** A time signature change along the song: from `tick` (a bar's start when it was made) bars
    have `beats` of `unit`. The bar it lands in ends there, short if it has to. */
struct MeterChange
{
    int tick = 0;
    int beats = 4, unit = 4;
    bool operator== (const MeterChange&) const = default;
};

/** A stretch of the song in one time signature (Song::meterSections), from a bar's start. */
struct MeterSection
{
    int start = 0;                   // ticks
    int beats = 4, unit = 4;
    int firstBar = 0;                // its first bar's number, counting from 0 (bar 1 on screen)
    int barTicks() const             { return beats * ticksPerBeat * 4 / unit; }
    int beatTicks() const            { return ticksPerBeat * 4 / unit; }
    juce::String name() const        { return juce::String (beats) + "/" + juce::String (unit); }
};

/** An Arrange track. Most are added for a channel (an instrument, or an audio track to record
    into) and take its name and icon; recordings and new clips of that channel land on it. One
    added for nothing holds whatever patterns you paint on it, as in FL. Clips say which track
    they're on by its index in Song::tracks. */
struct Track
{
    juce::String name;               // its own name; empty: named after its channel or what's on it
    juce::String icon;               // one of trackicons::all(); empty: picked from what it plays
    int channelId = 0;               // the channel it was added for, or 0
    bool muted = false, soloed = false;
};

enum class PlayMode { pattern, song };

//==============================================================================
/** Step-sequencer helpers. A channel shows step buttons when all its notes sit
    on the 16-step grid at the channel's root key, exactly like FL's channel rack.
*/
namespace steps
{
    bool isStepSequence (const std::vector<Note>&, int rootKey, int barTicks);
    const Note* find (const std::vector<Note>&, int step, int rootKey);
    void set (std::vector<Note>&, int step, bool on, int rootKey, int velocity = 100);
}

//==============================================================================
class Song
{
public:
    enum class Change
    {
        notes,          // note data inside a pattern
        channels,       // channels or buses added or removed, or a channel's instrument changed
        mixer,          // volume, pan, mute, effects, outputs and sends
        patterns,       // patterns added, removed, renamed
        playlist,       // playlist clips
        trackState,     // a playlist row's mute, solo or name
        transport,      // tempo, swing, play mode
        currentPattern, // which pattern is being edited (and played, in pattern mode)
        selection,      // selected channel (view only)
        automation,     // automation lanes, their points, or whether Arrange shows them
        everything
    };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void songChanged (Change) = 0;
    };

    Song();

    //==============================================================================
    double bpm = 120.0;

    // The time signature: beats in a bar, and the note value of a beat (7/8 is 7 and 8)
    int meterBeats = 4, meterUnit = 4;

    // The start's bar (later ones follow meterChanges: meterSections, barStart, barAt)
    int ticksPerBar() const                         { return meterBeats * ticksPerBeat * 4 / meterUnit; }
    int stepsPerBar() const                         { return ticksPerBar() / ticksPerStep; }
    int ticksPerMeterBeat() const                   { return ticksPerBeat * 4 / meterUnit; }
    juce::String meterName() const                  { return juce::String (meterBeats) + "/" + juce::String (meterUnit); }

    /** A pattern's time signature, its own or the song's at the start, and its bar. Its length is
        whole bars of it. */
    std::pair<int, int> meterOf (const Pattern&) const;
    int barTicksOf (const Pattern& p) const         { auto [b, u] = meterOf (p); return b * ticksPerBeat * 4 / u; }
    int beatTicksOf (const Pattern& p) const        { return ticksPerBeat * 4 / meterOf (p).second; }
    int patternLength (const Pattern& p) const      { return p.lengthInTicks (barTicksOf (p)); }

    /** Gives a pattern being made for `tick` the time signature there: its own when that isn't
        the start's, else the song's. */
    void fitPatternMeter (Pattern&, double tick) const;

    //==============================================================================
    // Tempo and time signature changes along the song (Logic's tempo and signature tracks).
    // `bpm` and the time signature above are the start's. Song mode plays the changes; Pattern
    // mode plays at the start's tempo, each pattern in its own time signature
    std::vector<TempoChange> tempoChanges;          // in order, each after the start
    std::vector<MeterChange> meterChanges;          // in order, each after the start

    /** Where `tick` falls in real time, in seconds from the start, and back (before the start,
        at the start's tempo). A glide goes in steps, four to a beat, as Tracktion plays it. */
    double secondsAt (double tick) const;
    double tickAt (double seconds) const;

    /** The tempo at `tick`, part way along a glide in between. */
    double bpmAt (double tick) const;

    /** The tempo change in effect at `tick`; -1 for the start's tempo. */
    int tempoChangeAt (double tick) const;

    /** The song's stretches of one time signature, the start's first; each begins a bar. */
    std::vector<MeterSection> meterSections() const;
    MeterSection meterAt (double tick) const;
    int meterChangeAt (double tick) const;          // -1 for the start's
    int barStart (int bar) const;                   // bars count from 0; before the start, the start's bars
    int barAt (double tick) const;
    int barLength (int bar) const                   { return barStart (bar + 1) - barStart (bar); }

    /** Snap grids that follow the time signature where they're used: whole bars, or its beats. */
    static constexpr int barGrid = -1, beatGrid = -2;

    /** `tick` onto a grid: barGrid, beatGrid (the beats of the bar it's in), else steps of that
        many ticks from its bar's start (1 or 0: whole ticks). To the nearest line (`way` 0), the
        one at or before it (-1), or at or after it (1). */
    int snapTick (double tick, int grid, int way = 0) const;

    /** How long one step of a grid is at `tick` (a bar there, a beat there, or the grid itself). */
    int gridStep (int grid, double tick) const;

    /** Sets the tempo at `tick` (the start's at 0 or before): changes the one there, or adds one
        that jumps there. Keeps them in order. */
    void setTempoAt (int tick, double newBpm, bool glide = false);

    /** Sets the time signature from the bar starting at `tick` (the start's at 0): changes the one
        there or adds one, and takes away a change that would repeat the one before it. */
    void setMeterAt (int tick, int beats, int unit);
    void removeMeterChange (int index);

    /** Puts tempo and time signature changes in order, after the start, one per tick. */
    void tidyTimeline();

    /** The time signatures on offer, as { beats, unit }. */
    static const std::vector<std::pair<int, int>>& meters();
    float swing = 0.0f;              // 0..1
    float masterVolume = 0.8f;       // 0..1, 0.8 is unity

    /** Mastering (the Master tab): a limiter at the very end of the mix, after the master
        volume, that nothing leaves above `ceilingDb` (true peak). `pushDb` drives the mix into
        it: what "Set loudness" works out to reach the target's loudness. The default target,
        "safe", only keeps the mix from clipping (see mastering::targets). */
    struct Mastering
    {
        juce::String target = "safe";
        float pushDb = 0.0f;
        float ceilingDb = -1.0f;

        // Shaping it, before the limiter (the Master tab's second page; mastering::Shaper):
        // Tone - four bands, bass (a shelf at 100 Hz), body (300 Hz), presence (3 kHz) and air (a
        // shelf at 10 kHz), +/-6 dB, each on everything (0), the centre (1) or the sides (2)
        bool toneOn = true;
        std::array<float, 4> toneDb {};
        std::array<int, 4> toneWhere {};

        // Punch - lows (under 120 Hz), mids and highs (over 2.5 kHz) each compressed on their own,
        // `punchAmount` 0..1 for all three, `punchShare` each band's part of it (0..1.5)
        bool punchOn = true;
        float punchAmount = 0.0f;
        std::array<float, 3> punchShare { 1.0f, 1.0f, 1.0f };

        // Width - the sides louder or quieter (1 as mixed, 0 mono, 2 twice as wide), and nothing
        // on the sides below monoBassHz (0: off), so the bass stays in the middle
        bool widthOn = true;
        float width = 1.0f;
        float monoBassHz = 0.0f;

        bool operator== (const Mastering&) const = default;

        /** As saved with the song (and in the AI mastering engineer's versions). */
        juce::var toVar() const;
        static Mastering fromVar (const juce::var&);
    };

    Mastering mastering;

    /** One request to the AI mastering engineer: what was asked, what it said, the settings it chose. */
    struct MasterTurn
    {
        juce::String prompt, reply;
        juce::String settings;          // the Master tab's settings it chose (Mastering::toVar, as JSON)
        juce::String answer;            // what it heard and understood (JSON): the style, the before and after
    };

    std::vector<MasterTurn> masterTurns;
    int masterShown = -1;               // the version the Master tab's settings came from

    // The AI song agent's prompt that made this project, and what it said it made
    juce::String songPrompt, songReply;
    std::vector<Channel> channels;
    std::vector<Pattern> patterns;
    std::vector<PlaylistClip> playlist;
    std::vector<AudioClip> audioClips;
    std::vector<Effect> masterEffects;  // on the whole mix, before the master limiter
    std::vector<Bus> buses;

    // The Arrange's tracks: only the ones added (no empty rows waiting), top to bottom
    std::vector<Track> tracks;
    int numTracks() const            { return (int) tracks.size(); }

    // Automation: the lanes (in the order they were added), and whether Arrange shows them
    std::vector<AutomationLane> automation;
    bool showAutomation = false;

    AutomationLane* findLane (int owner, const juce::String& param);
    const AutomationLane* findLane (int owner, const juce::String& param) const;

    /** The lane for a parameter, added (with no points) if there isn't one. */
    AutomationLane& laneFor (int owner, const juce::String& param, const juce::String& name);

    /** The lanes of a channel or bus (or of the master, for 0), in the order they were added. */
    std::vector<const AutomationLane*> lanesOf (int owner) const;

    /** Whether lanes can belong to it: the master (0), a channel or a bus. */
    bool canOwnAutomation (int owner) const          { return owner == 0 || findChannel (owner) != nullptr || findBus (owner) != nullptr; }

    // MIDI controllers' knobs linked to controls (MIDI learn), saved with the song
    std::vector<ControllerLink> controllerLinks;

    /** The link driving a control, if one does. */
    const ControllerLink* findControllerLink (int owner, const juce::String& param) const;

    // The playlist loop (Logic's cycle): song mode plays it round and round, and recording
    // in it makes a take each time round
    bool loopOn = false;
    int loopStart = 0, loopEnd = 0;  // ticks
    bool hasLoop() const             { return loopOn && loopEnd > loopStart; }

    /** Whether a track is heard: not muted, and soloed if any track is. A row with no track
        (a clip being dragged below the last) is heard. */
    bool isTrackAudible (int row) const;

    /** The channels a track plays: the one it was added for, an audio track's, and those
        with notes in its patterns (in the rack's order). */
    std::vector<int> channelsOnTrack (int row) const;

    /** The channel a track is for: the one it was added for, else the only one heard on it;
        0 for neither (an empty track, or a drum pattern's of several sounds). */
    int trackChannel (int row) const;

    /** Whether anything is on a track over [start, end) ticks. */
    bool isTrackFree (int row, int start, int end) const;
    bool isTrackEmpty (int row) const  { return isTrackFree (row, 0, std::numeric_limits<int>::max()); }

    /** Adds a track at `index` (the end for -1), moving the clips below it down a row.
        Returns where it went. */
    int insertTrack (Track, int index = -1);

    /** Takes a track away with its clips; the tracks below move up. */
    void removeTrack (int row);

    /** Where a channel's new clip over [start, end) goes: the first of its tracks that's free
        there, else a new track for it, after its last one (at the end if it has none). */
    int trackForChannel (int channelId, int start, int end);

    /** Adds plain tracks until every clip's row has one. */
    void coverClipsWithTracks();

    // View state, saved with the project like FL does
    PlayMode mode = PlayMode::pattern;
    int currentPatternId = 0;
    int selectedChannelId = 0;

    //==============================================================================
    Channel* findChannel (int id);
    const Channel* findChannel (int id) const;
    Pattern* findPattern (int id);
    const Pattern* findPattern (int id) const;
    int indexOfPattern (int id) const;
    int indexOfChannel (int id) const;

    Bus* findBus (int id);
    const Bus* findBus (int id) const;

    /** An effect anywhere in the song, and the chain it's in (a channel's, a bus's or the master's). */
    Effect* findEffect (int id, std::vector<Effect>** chain = nullptr);

    /** The effect chain of a channel or bus by id, or the master's for 0; null if there's no such thing. */
    std::vector<Effect>* chainFor (int ownerId);

    /** The channel's send to a bus, if it has one. */
    static Send* findSend (Channel&, int busId);

    /** Whether a channel is heard through a bus: playing into it, or sending to it. */
    bool feedsBus (const Channel&, int busId) const;

    /** The current pattern, created if there are none. */
    Pattern& currentPattern();

    /** End of the last playlist clip (pattern or audio), in ticks. */
    int songLengthInTicks() const;

    /** How long a pattern clip plays: its own length once split or trimmed, else the rest of
        its pattern from its offset. */
    int clipLength (const PlaylistClip&) const;
    int clipEnd (const PlaylistClip& c) const        { return c.start + clipLength (c); }

    /** Makes a pattern clip play over [newStart, newEnd) with its notes staying where they are
        in the song: trimming its start plays its pattern from further in. It can't start
        before its pattern does. */
    void setClipRange (PlaylistClip&, int newStart, int newEnd) const;

    /** Makes a take folder cover [startTick, startTick + length seconds) with its audio staying
        where it is in the song: trimming an end hides audio, and growing it back shows what its
        takes still have there. */
    void setAudioRange (AudioClip&, int startTick, double length) const;

    /** Splits a pattern clip, or a take folder, in two at `tick` (which must be inside it); the
        second half goes on the end of its list. Returns whether it split. */
    bool splitClip (int index, int tick);
    bool splitAudioClip (int index, int tick);

    /** Where a take folder ends: its length is its audio's, through its speed and the tempo where it plays. */
    int audioClipEnd (const AudioClip& c) const      { return (int) std::ceil (audioTickAt (c, c.length) - 1.0e-6); }

    /** Where `seconds` into a take folder's own audio lands in the song, in ticks, and back. As
        recorded it's real time from its start (`speed` times faster); following the tempo, its
        beats (at `sourceBpm`, `speed` times faster) are the song's beats. */
    double audioTickAt (const AudioClip&, double seconds) const;
    double audioSecondsAt (const AudioClip&, double tick) const;

    /** Following the tempo: switched on, and its own tempo known. */
    static bool followsTempo (const AudioClip& c)   { return c.followTempo && c.sourceBpm > 0.0; }

    /** Whether it plays through the time-stretcher: faster or slower than it was played, pitched,
        or following a tempo somewhere along it that isn't its own. */
    bool isStretched (const AudioClip&) const;

    /** How much faster than it was played it is at `tick` (1: as played). */
    double stretchRatioAt (const AudioClip&, double tick) const;

    /** Switches following the tempo on or off. A folder whose tempo isn't known takes the song's
        where it starts, so it doesn't jump now; one that is, comes back into time with the song. */
    void setFollowTempo (AudioClip&, bool on) const;

    /** Stretches a take folder to play its audio over [startTick, endTick): faster to fit a shorter
        stretch, slower a longer one (within AudioClip's slowest and fastest). Returns whether it fit. */
    bool stretchAudioTo (AudioClip&, int startTick, double endTick) const;

    /** A take folder of a whole audio file at `tick` on a track: a loop, a sample, a recording from
        elsewhere. A loop whose tempo is known (`bpm` > 0) follows the song's tempo. */
    AudioClip& addAudioFile (const juce::File&, int channelId, int track, int tick, double seconds, double bpm);

    /** Seconds from one tick to another, through the tempo between. */
    double secondsBetween (double fromTick, double toTick) const     { return secondsAt (toTick) - secondsAt (fromTick); }

    /** Files new takes for a channel: into the folder they overlap (growing it to fit), or a
        new folder. `startSeconds` is where the takes' t = 0 falls in the song; the folder
        start snaps to a whole tick and the takes' offsets absorb the difference. The comp
        switches to `compTake` (an index into `newTakes`) where it has audio. */
    AudioClip& addTakes (int channelId, double startSeconds, double length, std::vector<Take> newTakes, int compTake);

    int createId()                   { return nextId++; }
    Channel& addChannel (const juce::String& name, InstrumentType);
    Pattern& addPattern (const juce::String& name = {});

    /** A new pattern for one channel's notes, named and coloured after it ("Keys 2" if
        there's a Keys already). */
    Pattern& addPatternFor (int channelId);
    Bus& addBus (const juce::String& name = {});
    void removeChannel (int id);

    /** Takes a bus away, and every output and send to it (those channels go to the master), with
        its automation lanes and MIDI links and those of the sends to it. */
    void removeBus (int id);
    void removePattern (int id);

    //==============================================================================
    void notify (Change);
    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    /** Records the current state for undo. Call before an edit, once per gesture. */
    void checkpoint();
    bool undo();
    bool redo();
    bool hasUnsavedChanges() const    { return dirty; }
    void markSaved()                  { dirty = false; }

    //==============================================================================
    /** Four of the built-in kit's sounds and no tracks: the old new project, kept for tests. */
    void loadEmpty();

    /** A blank canvas: one empty track, one empty pattern, no channels. What a new project
        is when you close the start screen, and what the templates build on. */
    void loadBlank();

    void loadDemo();

    juce::var toVar() const;
    void fromVar (const juce::var&);
    bool save (const juce::File&) const;
    bool load (const juce::File&);

    static juce::Colour channelColour (int index);
    static juce::Colour patternColour (int index);

private:
    juce::ListenerList<Listener> listeners;
    std::vector<juce::String> undoStack, redoStack;
    int nextId = 1;
    bool dirty = false;

    void restore (const juce::String& json);
    void resetToDefaults();
};

} // namespace nightshift
