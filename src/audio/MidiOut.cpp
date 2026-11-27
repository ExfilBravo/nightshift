// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "MidiOut.h"
#include <tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h>

namespace nightshift
{

namespace
{
    // Real-time messages: clock, start, continue, stop; and the song position
    constexpr juce::uint8 midiClock = 0xf8, midiStart = 0xfa, midiContinue = 0xfb, midiStop = 0xfc, songPosition = 0xf2;

    void mix (std::size_t& seed, std::uint64_t value)
    {
        seed = (std::size_t) tracktion::hash_mix (((std::uint64_t) seed + 1) * 0x9e3779b97f4a7c15ull + value);
    }

    std::uint64_t bitsOf (double d)
    {
        std::uint64_t u = 0;
        std::memcpy (&u, &d, sizeof (u));
        return u;
    }
}

//==============================================================================
/** After the master and the metronome: passes the sound on, and drives the hub each block with
    where the playhead is and will be. */
struct MidiOutHub::Node final : public te::graph::Node,
                                public te::TracktionEngineNode
{
    Node (std::unique_ptr<te::graph::Node> in, te::ProcessState& state, MidiOutHub& h)
        : te::TracktionEngineNode (state), input (std::move (in)), hub (h)
    {
    }

    te::graph::NodeProperties getNodeProperties() override
    {
        auto props = input->getNodeProperties();
        tracktion::hash_combine (props.nodeID, (std::size_t) 0x4e534d4944494f55ull);
        return props;
    }

    std::vector<te::graph::Node*> getDirectInputNodes() override     { return { input.get() }; }
    bool isReadyToProcess() override                                  { return input->hasProcessed(); }

    void prepareToPlay (const te::graph::PlaybackInitialisationInfo&) override
    {
        // What's heard of a block is the graph's latency behind it (the master limiter looks ahead)
        latency = input->getNodeProperties().latencyNumSamples;
    }

    void process (ProcessContext& pc) override
    {
        auto in = input->getProcessedOutput();
        choc::buffer::copy (pc.buffers.audio, in.audio);
        pc.buffers.midi.copyFrom (in.midi);
        hub.process (getPlayHeadState(), getProcessState(), pc.referenceSampleRange, latency);
    }

    std::unique_ptr<te::graph::Node> input;
    MidiOutHub& hub;
    int latency = 0;
};

//==============================================================================
MidiOutHub::MidiOutHub (bool isSimulated)
    : simulated (isSimulated)
{
    queue.reserve (8192);
    due.reserve (8192);
}

MidiOutHub::~MidiOutHub()
{
    stopTimer();

    // Nothing left sounding on a synth when NightShift goes
    const juce::ScopedLock sl (deviceLock);

    for (int p = 0; p < numPorts.load(); ++p)
    {
        auto& port = ports[(size_t) p];

        if (port.device == nullptr)
            continue;

        for (int ch = 0; ch < 16; ++ch)
            for (int key = 0; key < 128; ++key)
                if (port.held[(size_t) ch][(size_t) key])
                    port.device->sendMessageNow (juce::MidiMessage::noteOff (ch + 1, key));

        port.device.reset();
    }
}

juce::StringArray MidiOutHub::getOutputNames() const
{
    if (simulated)
        return testOutputs;

    juce::StringArray names;

    for (auto& d : juce::MidiOutput::getAvailableDevices())
        names.addIfNotAlreadyThere (d.name);

    return names;
}

int MidiOutHub::portFor (const juce::String& name)
{
    const int n = numPorts.load();

    for (int i = 0; i < n; ++i)
        if (ports[(size_t) i].name == name)
            return i;

    if (n >= maxPorts || name.isEmpty())
        return -1;

    ports[(size_t) n].name = name;
    numPorts.store (n + 1);
    return n;
}

void MidiOutHub::refreshPorts()
{
    if (simulated || numPorts.load() == 0)
        return;

    const auto available = juce::MidiOutput::getAvailableDevices();
    const double now = juce::Time::getMillisecondCounterHiRes();
    bool any = false;

    for (int p = 0; p < numPorts.load(); ++p)
    {
        auto& port = ports[(size_t) p];
        any = any || port.wanted;

        auto info = std::find_if (available.begin(), available.end(), [&port] (const juce::MidiDeviceInfo& d) { return d.name == port.name; });
        bool open = false;

        {
            const juce::ScopedLock sl (deviceLock);
            open = port.device != nullptr;
        }

        // Unplugged: closed (plugged in again, it opens again)
        if (info == available.end())
        {
            std::unique_ptr<juce::MidiOutput> gone;

            {
                const juce::ScopedLock sl (deviceLock);
                gone = std::move (port.device);
            }

            continue;
        }

        // Opened once used, and kept open (a synth switched away from still hears its notes end).
        // Tracktion may still have it open for a moment after it's switched off there: try again
        if (port.wanted && ! open && now >= port.retryAt)
        {
            if (auto device = juce::MidiOutput::openDevice (info->identifier))
            {
                const juce::ScopedLock sl (deviceLock);
                port.device = std::move (device);
            }
            else
            {
                port.retryAt = now + 2000.0;
            }
        }
    }

    // The sender runs once anything's been sent
    if (any && ! timerRunning)
    {
        timerRunning = true;
        startTimer (1);
    }
}

//==============================================================================
void MidiOutHub::enqueue (int port, const juce::uint8* bytes, int size, double sendMs, double heardMs, bool scheduled)
{
    if (! juce::isPositiveAndBelow (port, maxPorts) || size <= 0 || size > 3)
        return;

    Pending p;
    p.sendMs = sendMs;
    p.heardMs = heardMs;
    p.port = port;
    p.size = size;
    p.scheduled = scheduled;
    std::copy (bytes, bytes + size, p.bytes.begin());

    const juce::SpinLock::ScopedLockType sl (queueLock);

    if (queue.size() < queue.capacity())
    {
        p.order = nextOrder++;
        queue.push_back (p);
    }
}

void MidiOutHub::enqueueRelease (int port, double atMs)
{
    if (! juce::isPositiveAndBelow (port, maxPorts))
        return;

    Pending p;
    p.sendMs = p.heardMs = atMs;
    p.port = port;
    p.scheduled = p.release = true;

    const juce::SpinLock::ScopedLockType sl (queueLock);

    if (queue.size() < queue.capacity())
    {
        p.order = nextOrder++;
        queue.push_back (p);
    }
}

void MidiOutHub::cancelScheduled (int port)
{
    const juce::SpinLock::ScopedLockType sl (queueLock);
    std::erase_if (queue, [port] (const Pending& p) { return p.port == port && p.scheduled && ! p.release; });
}

void MidiOutHub::drain (double now)
{
    due.clear();

    {
        const juce::SpinLock::ScopedLockType sl (queueLock);

        if (queue.empty())
            return;

        for (auto& p : queue)
            if (p.sendMs <= now)
                due.push_back (p);

        if (due.empty())
            return;

        std::erase_if (queue, [now] (const Pending& p) { return p.sendMs <= now; });
    }

    std::sort (due.begin(), due.end(), [] (const Pending& a, const Pending& b)
    {
        return a.sendMs != b.sendMs ? a.sendMs < b.sendMs : a.order < b.order;
    });

    for (auto& p : due)
    {
        auto& port = ports[(size_t) p.port];

        if (! p.release)
        {
            // The plan's notes this output holds on, to let go at a stop, a jump or the loop's end;
            // one's end with no start sent (it started before an edit took it over) isn't sent
            if (p.scheduled && p.size == 3)
            {
                const int status = p.bytes[0] & 0xf0, channel = p.bytes[0] & 0x0f, key = p.bytes[1] & 0x7f;
                auto& held = port.held[(size_t) channel];

                if (status == 0x90 && p.bytes[2] > 0)
                {
                    held.set ((size_t) key);
                }
                else if (status == 0x80 || status == 0x90)
                {
                    if (! held[(size_t) key])
                        continue;

                    held.reset ((size_t) key);
                }
            }

            deliver (port, p.bytes.data(), p.size, ! p.scheduled, p.sendMs, p.heardMs);
            continue;
        }

        for (int channel = 0; channel < 16; ++channel)
        {
            auto& held = port.held[(size_t) channel];

            if (held.none())
                continue;

            for (int key = 0; key < 128; ++key)
                if (held[(size_t) key])
                {
                    const juce::uint8 off[3] = { (juce::uint8) (0x80 | channel), (juce::uint8) key, 0 };
                    deliver (port, off, 3, false, p.sendMs, p.heardMs);
                }

            held.reset();
        }
    }
}

void MidiOutHub::deliver (Port& port, const juce::uint8* bytes, int size, bool live, double sentMs, double heardMs)
{
    const juce::MidiMessage m (bytes, size, sentMs * 0.001);

    if (simulated)
    {
        const juce::SpinLock::ScopedLockType sl (sentLock);
        sent.push_back ({ port.name, m, sentMs, heardMs, live });
        return;
    }

    const juce::ScopedLock sl (deviceLock);

    if (port.device != nullptr)
        port.device->sendMessageNow (m);
}

//==============================================================================
void MidiOutHub::setPlan (Plan plan)
{
    if (plan.outputs.size() > (size_t) maxPorts)
        plan.outputs.resize ((size_t) maxPorts);

    plan.ports.clear();
    plan.hashes.clear();

    for (auto& out : plan.outputs)
    {
        const int port = portFor (out.name);
        plan.ports.push_back (port);

        if (port >= 0)
            ports[(size_t) port].wanted = true;

        // What it plays, so a change shows (and nothing else is disturbed)
        std::size_t h = (std::size_t) out.name.hashCode64();
        mix (h, (std::uint64_t) (out.settings.timingMs + 100000));
        mix (h, out.settings.clock ? 1u : 2u);

        for (auto& e : out.events)
        {
            mix (h, bitsOf (e.seconds));
            mix (h, ((std::uint64_t) e.bytes[0] << 16) | ((std::uint64_t) e.bytes[1] << 8) | e.bytes[2]);
        }

        for (auto& m : out.programs)
            mix (h, ((std::uint64_t) m.getRawData()[0] << 8) | (std::uint64_t) (m.getRawDataSize() > 1 ? m.getRawData()[1] : 0));

        plan.hashes.push_back (h);
    }

    // Where each channel's live notes go
    std::vector<LiveRoute> live;

    for (auto& r : plan.routes)
        if (juce::isPositiveAndBelow (r.output, (int) plan.ports.size()))
            live.push_back ({ r.channelId, plan.ports[(size_t) r.output], juce::jlimit (1, 16, r.midiChannel), r.muted });

    {
        const juce::SpinLock::ScopedLockType sl (routeLock);
        routes.swap (live);
    }

    refreshPorts();

    // The same as what's waiting or playing: nothing to do
    {
        const juce::SpinLock::ScopedLockType sl (planLock);

        if (pendingPlan != nullptr && pendingPlan->hashes == plan.hashes && pendingPlan->ports == plan.ports
             && pendingPlan->baseUnit == plan.baseUnit)
            return;

        if (pendingPlan == nullptr && plan.outputs.empty())
            return;
    }

    auto next = std::make_unique<Plan> (std::move (plan));
    Plan* raw = next.get();
    plans.push_back (std::move (next));
    Plan* active = nullptr;

    {
        const juce::SpinLock::ScopedLockType sl (planLock);
        pendingPlan = raw;
        active = activePlan;
    }

    // The ones the audio thread can't come back to
    std::erase_if (plans, [raw, active] (const std::unique_ptr<Plan>& p) { return p.get() != raw && p.get() != active; });
}

//==============================================================================
void MidiOutHub::sendLive (int channelId, const juce::MidiMessage& m)
{
    const int size = m.getRawDataSize();

    if (size < 1 || size > 3 || m.getChannel() == 0)
        return;

    LiveRoute route;

    {
        const juce::SpinLock::ScopedLockType sl (routeLock);

        for (auto& r : routes)
            if (r.channelId == channelId)
            {
                route = r;
                break;
            }
    }

    // A muted channel plays nothing new; what it's holding still lets go
    if (route.port < 0 || (route.muted && m.isNoteOn()))
        return;

    std::array<juce::uint8, 3> bytes {};
    std::copy (m.getRawData(), m.getRawData() + size, bytes.begin());
    bytes[0] = (juce::uint8) ((bytes[0] & 0xf0) | (route.midiChannel - 1));

    const double now = nowMs();
    enqueue (route.port, bytes.data(), size, now, now, false);
}

void MidiOutHub::sendNow (const juce::String& output, const juce::MidiMessage& m)
{
    const int port = portFor (output);

    if (port < 0 || m.getRawDataSize() > 3)
        return;

    ports[(size_t) port].wanted = true;
    refreshPorts();
    const double now = nowMs();
    enqueue (port, m.getRawData(), m.getRawDataSize(), now, now, false);
}

std::vector<MidiOutHub::Sent> MidiOutHub::takeSent()
{
    const juce::SpinLock::ScopedLockType sl (sentLock);
    return std::exchange (sent, {});
}

void MidiOutHub::flush()
{
    drain (std::numeric_limits<double>::max());
}

double MidiOutHub::nowMs() const
{
    return simulated ? lastBlockMs.load() : juce::Time::getMillisecondCounterHiRes();
}

std::unique_ptr<te::graph::Node> MidiOutHub::createNode (std::unique_ptr<te::graph::Node> input, te::ProcessState& state)
{
    return std::make_unique<Node> (std::move (input), state, *this);
}

//==============================================================================
void MidiOutHub::process (te::graph::PlayHeadState& playHeadState, te::ProcessState& state,
                          juce::Range<juce::int64> range, int graphLatency)
{
    const double sampleRate = state.sampleRate;

    if (sampleRate <= 0.0 || range.isEmpty())
        return;

    // When the block plays: simulated, counted in samples; live, the clock, followed gently (blocks
    // come at the interface's steady pace, however long the graph took to get here)
    if (simulated)
    {
        blockWall = (double) range.getStart() * 1000.0 / sampleRate;
    }
    else
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double expected = blockWall + lastBlockLengthMs;
        blockWall = range.getStart() == lastBlockEnd && std::abs (now - expected) < 40.0 ? expected + 0.05 * (now - expected) : now;
    }

    lastBlockLengthMs = (double) range.getLength() * 1000.0 / sampleRate;
    lastBlockEnd = range.getEnd();
    lastBlockMs.store (blockWall);

    Block block;
    block.range = range;
    block.sampleRate = sampleRate;
    block.wallMs = blockWall;
    block.latencyMs = (outputLatency.load() + graphLatency) * 1000.0 / sampleRate;
    latencyMs.store (block.latencyMs);

    takeUpPlan (block);

    auto& playHead = playHeadState.playHead;
    const bool playing = playHead.isPlaying() && ! playHead.isUserDragging();

    if (! playing)
    {
        if (wasPlaying)
            stopAll (blockWall);

        wasPlaying = false;
    }
    else
    {
        // Starting, or moved somewhere else: from here, the clock finding its place again and the
        // sounds the channels ask for first
        if (! wasPlaying || playHeadState.didPlayheadJump())
        {
            if (wasPlaying)
                stopAll (blockWall);

            for (auto& s : states)
            {
                s.cursor = range.getStart();
                s.lastTimelineEnd = -1;
                s.positioned = false;
            }

            if (activePlan != nullptr)
                for (size_t i = 0; i < activePlan->outputs.size(); ++i)
                    for (auto& m : activePlan->outputs[i].programs)
                        enqueue (activePlan->ports[i], m.getRawData(), m.getRawDataSize(), blockWall, blockWall, false);
        }

        wasPlaying = true;

        if (activePlan != nullptr)
            for (size_t i = 0; i < activePlan->outputs.size(); ++i)
                if (const int port = activePlan->ports[i]; juce::isPositiveAndBelow (port, maxPorts))
                    scheduleOutput (port, activePlan->outputs[i], states[(size_t) port], playHead, state.getTempoSequence(),
                                    activePlan->baseUnit, block);
    }

    if (simulated)
        drain (blockWall);
}

void MidiOutHub::takeUpPlan (const Block& block)
{
    Plan* next = nullptr;

    {
        const juce::SpinLock::ScopedTryLockType sl (planLock);

        if (! sl.isLocked() || pendingPlan == activePlan)
            return;

        next = activePlan = pendingPlan;
    }

    std::array<bool, maxPorts> kept {};

    for (size_t i = 0; next != nullptr && i < next->outputs.size(); ++i)
    {
        const int port = next->ports[i];

        if (! juce::isPositiveAndBelow (port, maxPorts))
            continue;

        kept[(size_t) port] = true;
        auto& st = states[(size_t) port];
        const auto& out = next->outputs[i];

        if (! st.inPlan)
        {
            // New while playing: from this block on, the sound it asks for first
            st = {};
            st.inPlan = true;
            st.cursor = block.range.getStart();
            st.timingMs = out.settings.timingMs;
            st.hash = next->hashes[i];

            if (wasPlaying)
                for (auto& m : out.programs)
                    enqueue (port, m.getRawData(), m.getRawDataSize(), block.wallMs, block.wallMs, false);

            continue;
        }

        if (st.hash == next->hashes[i])
            continue;

        // Changed while playing: the new messages carry on from what was already sent ahead, and
        // what it holds of the old ones lets go there; a clock switched off stops, one switched on
        // finds its place
        if (wasPlaying)
        {
            enqueueRelease (port, std::max (block.wallMs, block.heardAt ((double) st.cursor) + st.timingMs));

            if (st.clockRunning && ! out.settings.clock)
            {
                enqueue (port, &midiStop, 1, block.wallMs, block.wallMs, false);
                st.clockRunning = false;
            }

            if (out.settings.clock && ! st.clockRunning)
                st.positioned = false;
        }

        st.hash = next->hashes[i];
        st.timingMs = out.settings.timingMs;
    }

    // Outputs it no longer plays: what they hold lets go, their clock stops
    for (int p = 0; p < maxPorts; ++p)
    {
        auto& st = states[(size_t) p];

        if (! st.inPlan || kept[(size_t) p])
            continue;

        if (wasPlaying)
        {
            enqueueRelease (p, block.wallMs);

            if (st.clockRunning)
                enqueue (p, &midiStop, 1, block.wallMs, block.wallMs, false);
        }

        st = {};
    }
}

void MidiOutHub::stopAll (double now)
{
    // What was sent ahead won't play; what's held lets go, and the clock stops
    for (int p = 0; p < numPorts.load(); ++p)
    {
        auto& st = states[(size_t) p];
        cancelScheduled (p);
        enqueueRelease (p, now);

        if (st.clockRunning)
            enqueue (p, &midiStop, 1, now, now, false);

        st.clockRunning = st.positioned = false;
    }
}

void MidiOutHub::scheduleOutput (int port, const Output& out, OutputState& st, te::graph::PlayHead& playHead,
                                 const te::tempo::Sequence* tempo, int baseUnit, const Block& block)
{
    const double sampleRate = block.sampleRate;
    const double timing = juce::jlimit ((double) -maxTimingMs, (double) maxTimingMs, (double) out.settings.timingMs);

    // Earlier than the latencies already cover: looks that much further ahead along the playhead
    const double aheadMs = std::max (0.0, -timing - block.latencyMs);
    const auto windowEnd = block.range.getEnd() + (juce::int64) std::ceil (aheadMs * sampleRate / 1000.0);

    st.cursor = std::max (st.cursor, block.range.getStart());

    if (st.cursor >= windowEnd)
        return;

    auto sendAt = [&] (double ref, double& heard)
    {
        heard = block.heardAt (ref);
        return std::max (block.wallMs, heard + timing);
    };

    const auto split = te::graph::referenceSampleRangeToSplitTimelineRange (playHead, { st.cursor, windowEnd });
    double pieceRef = (double) st.cursor;

    for (int piece = 0; piece < (split.isSplit ? 2 : 1); ++piece)
    {
        const auto range = piece == 0 ? split.timelineRange1 : split.timelineRange2;
        const double from = (double) range.getStart() / sampleRate, to = (double) range.getEnd() / sampleRate;
        auto refOf = [&] (double seconds) { return pieceRef + (seconds - from) * sampleRate; };

        // Round the loop (in this window, or between the last and this one when it ended right on
        // the loop's end): the plan's notes still held end here, and the clock goes back with it
        if (st.lastTimelineEnd >= 0 && range.getStart() < st.lastTimelineEnd)
        {
            double heard = 0.0;
            enqueueRelease (port, sendAt (pieceRef, heard));
            st.positioned = false;
        }

        st.lastTimelineEnd = range.getEnd();

        auto it = std::lower_bound (out.events.begin(), out.events.end(), from,
                                    [] (const Event& e, double t) { return e.seconds < t; });

        for (; it != out.events.end() && it->seconds < to; ++it)
        {
            double heard = 0.0;
            const double send = sendAt (refOf (it->seconds), heard);
            enqueue (port, it->bytes.data(), it->size, send, heard, true);
        }

        // The clock: 24 to the quarter note, from a song position and a start (from the top) or a
        // continue, on a 16th
        if (out.settings.clock && tempo != nullptr && to > from && baseUnit > 0)
        {
            auto quartersAt = [&] (double seconds) { return tempo->toBeats (te::TimePosition::fromSeconds (seconds)).inBeats() * 4.0 / baseUnit; };
            auto secondsAt = [&] (double quarters) { return tempo->toTime (te::BeatPosition::fromBeats (quarters * baseUnit / 4.0)).inSeconds(); };

            if (! st.positioned)
            {
                const double sixteenth = std::max (0.0, std::ceil (quartersAt (from) * 4.0 - 1.0e-6));
                const double at = secondsAt (sixteenth / 4.0);

                if (at < to)
                {
                    double heard = 0.0;
                    const double send = sendAt (refOf (at), heard);
                    const int beats = (int) sixteenth;
                    const juce::uint8 position[3] = { songPosition, (juce::uint8) (beats & 0x7f), (juce::uint8) ((beats >> 7) & 0x7f) };
                    enqueue (port, position, 3, send, heard, true);
                    enqueue (port, beats == 0 ? &midiStart : &midiContinue, 1, send, heard, true);
                    st.positioned = st.clockRunning = true;
                    st.clockFrom = sixteenth * 6.0;
                }
            }

            if (st.positioned)
            {
                double k = std::max (st.clockFrom, std::ceil (quartersAt (from) * 24.0 - 1.0e-6));

                for (;; k += 1.0)
                {
                    const double at = secondsAt (k / 24.0);

                    if (at >= to)
                        break;

                    double heard = 0.0;
                    const double send = sendAt (refOf (std::max (at, from)), heard);
                    enqueue (port, &midiClock, 1, send, heard, true);
                }

                st.clockFrom = k;
            }
        }

        pieceRef += (double) range.getLength();
    }

    st.cursor = windowEnd;
}

//==============================================================================
const char* MidiOutPlugin::xmlTypeName = "nightshiftMidiOut";

MidiOutPlugin::MidiOutPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

MidiOutPlugin::~MidiOutPlugin()
{
    notifyListenersOfDeletion();
}

void MidiOutPlugin::setRoute (MidiOutHub* target, int channelId)
{
    channel.store (channelId);
    hub.store (target);
}

void MidiOutPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer != nullptr && fc.bufferNumSamples > 0)
        fc.destBuffer->clear (fc.bufferStartSample, fc.bufferNumSamples);

    if (fc.bufferForMidiMessages == nullptr)
        return;

    // What a keyboard plays goes straight out (never from an export's render)
    if (auto* target = hub.load(); target != nullptr && ! fc.isRendering)
        for (auto& m : *fc.bufferForMidiMessages)
            target->sendLive (channel.load(), m);

    fc.bufferForMidiMessages->clear();
}

} // namespace nightshift
