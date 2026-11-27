// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include <bitset>

namespace te = tracktion;

namespace nightshift
{

/** Synths plugged in (MIDI out): a channel's notes go out of a MIDI output in time with the song,
    and an output can follow NightShift's tempo (MIDI clock, start and stop).

    What plays is a Plan built on the message thread from the song: each output's notes in edit
    seconds. A node at the end of the playback graph (after the master and the metronome) reads the
    playhead each block and sends what's coming, stamped for when its sound is heard: the block's
    time, the graph's latency and the interface's own output latency, plus the output's timing.
    Timing earlier than those latencies cover (a synth that's slow to sound) looks further ahead
    along the playhead, round the loop as it will play. A sender thread sends each message at its
    time, a millisecond at a time; a command-line run counts time in samples and catches what's sent.

    The hub opens the MIDI outputs itself (Tracktion's own are switched off: a port opened twice
    fails on Windows), so a synth unplugged mid-song can't leave anything pointing at nothing. */
class MidiOutHub : private juce::HighResolutionTimer
{
public:
    /** `simulated`: a command-line run's engine: time is the samples played, nothing is opened,
        and every message is caught for takeSent. */
    explicit MidiOutHub (bool simulated);
    ~MidiOutHub() override;

    /** Outputs that tests and snapshots pretend are plugged in (simulated only). */
    static inline juce::StringArray testOutputs;

    /** The MIDI outputs there are (a synth, an interface's MIDI out, the test ones). Message thread. */
    juce::StringArray getOutputNames() const;

    /** Per output: when its messages go out against the song (negative: earlier, for a synth slow
        to sound; positive: later), and whether it follows the tempo (clock, start and stop). */
    struct OutputSettings
    {
        int timingMs = 0;
        bool clock = false;
        bool operator== (const OutputSettings&) const = default;
    };

    static constexpr int maxTimingMs = 250;

    //==============================================================================
    /** A message at a moment of the song, in edit seconds. */
    struct Event
    {
        double seconds = 0.0;
        std::array<juce::uint8, 3> bytes {};
        int size = 0;
    };

    struct Output
    {
        juce::String name;
        OutputSettings settings;
        std::vector<Event> events;                  // in time order (a note's end before another's start at one moment)
        std::vector<juce::MidiMessage> programs;    // the sounds asked for, sent as playing starts
    };

    /** Where a channel's live notes go: its output (an index into Plan::outputs) and MIDI channel. */
    struct Route
    {
        int channelId = 0, output = -1, midiChannel = 1;
        bool muted = false;
    };

    struct Plan
    {
        std::vector<Output> outputs;
        std::vector<Route> routes;
        int baseUnit = 4;                           // Tracktion's beat: the start's time signature unit (for the clock)

        // Filled in by setPlan: each output's port, and a fingerprint of what it plays
        std::vector<int> ports;
        std::vector<std::size_t> hashes;
    };

    /** Makes this what plays. An output whose messages or settings changed while playing goes over
        to the new ones from what was already sent ahead, its held notes let go there. Message thread. */
    void setPlan (Plan);

    /** The interface's own output latency, in samples. */
    void setOutputLatency (int samples)             { outputLatency.store (samples); }

    /** Opens the outputs the plan (or a test note) uses and closes ones that went away. Message thread. */
    void refreshPorts();

    //==============================================================================
    /** A message a keyboard (the computer's or one plugged in) plays on a channel that plays a
        synth plugged in: sent at once, on the channel's MIDI channel. Any thread. */
    void sendLive (int channelId, const juce::MidiMessage&);

    /** A message to an output now (a test note, a program change). Message thread. */
    void sendNow (const juce::String& output, const juce::MidiMessage&);

    //==============================================================================
    /** The playback graph's last node, passing the sound on and driving the hub. */
    std::unique_ptr<te::graph::Node> createNode (std::unique_ptr<te::graph::Node> input, te::ProcessState&);

    //==============================================================================
    /** Tests: what was sent, the time it was sent (ms on the hub's clock: simulated, from the
        first block) and, for a note of the plan, when it's heard. */
    struct Sent
    {
        juce::String output;
        juce::MidiMessage message;
        double sentMs = 0.0, heardMs = 0.0;
        bool live = false;
    };

    std::vector<Sent> takeSent();

    /** Tests: sends what's still waiting, however far ahead. */
    void flush();

    /** The hub's time now, in ms (simulated: the last block's). */
    double nowMs() const;

    /** Tests: the latency a note's time is heard after it leaves the graph (the graph's and the
        interface's), in ms, as the last block found it. */
    double getLatencyMs() const                     { return latencyMs.load(); }

private:
    struct Node;

    static constexpr int maxPorts = 32;

    /** An output, once used: its device (opened by the hub) and the notes of the plan it holds on. */
    struct Port
    {
        juce::String name;
        std::unique_ptr<juce::MidiOutput> device;
        std::array<std::bitset<128>, 16> held;      // sender only
        bool wanted = false;                        // message thread: the plan or a test note uses it
        double retryAt = 0.0;                       // message thread: when to try opening it again
    };

    std::array<Port, maxPorts> ports;
    std::atomic<int> numPorts { 0 };
    juce::CriticalSection deviceLock;               // a port's device, between the sender and the message thread

    int portFor (const juce::String& name);         // message thread: made when first used

    //==============================================================================
    /** A message waiting for its time. */
    struct Pending
    {
        double sendMs = 0.0, heardMs = 0.0;
        juce::uint64 order = 0;
        int port = -1;
        std::array<juce::uint8, 3> bytes {};
        int size = 0;
        bool scheduled = false;                     // from the plan (vs played live)
        bool release = false;                       // let go of the plan's notes this port holds
    };

    juce::SpinLock queueLock;
    std::vector<Pending> queue;                     // under queueLock
    juce::uint64 nextOrder = 0;                     // under queueLock
    std::vector<Pending> due;                       // the sender's

    void enqueue (int port, const juce::uint8* bytes, int size, double sendMs, double heardMs, bool scheduled);
    void enqueueRelease (int port, double atMs);

    /** Sends what's due by `now` (all of it with infinity). */
    void drain (double now);
    void deliver (Port&, const juce::uint8* bytes, int size, bool live, double sentMs, double heardMs);

    void hiResTimerCallback() override              { drain (juce::Time::getMillisecondCounterHiRes() + 0.5); }

    //==============================================================================
    // The plan: published under planLock; the audio thread takes it up at a block's start
    juce::SpinLock planLock;
    std::vector<std::unique_ptr<Plan>> plans;       // message thread: every one not yet let go
    Plan* pendingPlan = nullptr;                    // under planLock
    Plan* activePlan = nullptr;                     // audio thread (written under planLock)

    // Live routes: channel, port, MIDI channel, muted
    struct LiveRoute
    {
        int channelId = 0, port = -1, midiChannel = 1;
        bool muted = false;
    };

    juce::SpinLock routeLock;
    std::vector<LiveRoute> routes;                  // under routeLock

    //==============================================================================
    // The audio thread's state, per port
    struct OutputState
    {
        juce::int64 cursor = 0;                     // the next reference sample not yet looked at
        juce::int64 lastTimelineEnd = -1;           // where on the timeline the last window ended (-1: just started)
        bool inPlan = false;                        // the active plan has it
        bool clockRunning = false, positioned = false;
        double clockFrom = 0.0;                     // the next clock to send (24 to a quarter note, from the top)
        int timingMs = 0;
        std::size_t hash = 0;
    };

    std::array<OutputState, maxPorts> states;
    bool wasPlaying = false, timerRunning = false;
    double blockWall = 0.0, lastBlockLengthMs = 0.0;
    juce::int64 lastBlockEnd = -1;
    std::atomic<double> lastBlockMs { 0.0 }, latencyMs { 0.0 };
    std::atomic<int> outputLatency { 0 };
    const bool simulated;

    /** What the node calls each block. */
    struct Block
    {
        juce::Range<juce::int64> range;             // reference samples
        double sampleRate = 48000.0, wallMs = 0.0, latencyMs = 0.0;

        /** When a reference sample is heard, and (with an output's timing) when it's sent. */
        double heardAt (double ref) const           { return wallMs + (ref - (double) range.getStart()) * 1000.0 / sampleRate + latencyMs; }
    };

    void process (te::graph::PlayHeadState&, te::ProcessState&, juce::Range<juce::int64> block, int graphLatency);
    void takeUpPlan (const Block&);
    void stopAll (double now);
    void cancelScheduled (int port);
    void scheduleOutput (int port, const Output&, OutputState&, te::graph::PlayHead&, const te::tempo::Sequence*,
                         int baseUnit, const Block&);

    // Tests
    juce::SpinLock sentLock;
    std::vector<Sent> sent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiOutHub)
};

//==============================================================================
/** The instrument of a channel that plays a synth plugged in: silent, it hands whatever a
    keyboard plays into it (a MIDI keyboard through Tracktion's monitoring, the computer keyboard,
    a click on the piano roll's keys) to the hub at once. Its notes in patterns go by the hub's plan. */
class MidiOutPlugin : public te::Plugin
{
public:
    explicit MidiOutPlugin (te::PluginCreationInfo);
    ~MidiOutPlugin() override;

    static const char* xmlTypeName;

    juce::String getName() const override                   { return "Synth plugged in"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getSelectableDescription() override        { return getName(); }
    bool isSynth() override                                 { return true; }
    bool takesMidiInput() override                          { return true; }
    bool takesAudioInput() override                         { return false; }
    bool producesAudioWhenNoAudioInput() override           { return false; }
    int getNumOutputChannelsGivenInputs (int) override      { return 2; }
    BusLayout getBusses() const override                    { return BusLayout::singleStereoInOut(); }

    void initialise (const te::PluginInitialisationInfo&) override  {}
    void deinitialise() override                            {}
    void applyToBuffer (const te::PluginRenderContext&) override;

    /** Where what it's given goes: the hub, for this channel. */
    void setRoute (MidiOutHub*, int channelId);

private:
    std::atomic<MidiOutHub*> hub { nullptr };
    std::atomic<int> channel { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiOutPlugin)
};

} // namespace nightshift
