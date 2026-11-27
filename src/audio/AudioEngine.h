// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>
#include "model/Song.h"
#include "Nova.h"
#include "Metronome.h"
#include "Mastering.h"
#include "MidiOut.h"

namespace te = tracktion;

namespace nightshift
{

class AutomationRecorder;

/** Mirrors the Song into a Tracktion Edit: one audio track per channel holding its
    instrument, and MIDI clips generated from the patterns (the current pattern in
    pattern mode, the playlist in song mode). Audio channels are tracks fed by an
    input; recording turns their takes into AudioClips in the Song.

    Each bus is a track too, starting with an aux return: a channel playing into a bus has
    its track's output sent to the bus's track, and each send is an aux send on the
    channel's track (before its fader, or after it and the meter).
*/
class AudioEngine : private Song::Listener,
                    private juce::AsyncUpdater,
                    private juce::ChangeListener,
                    private juce::Timer,
                    private te::TransportControl::Listener
{
public:
    AudioEngine (Song&, bool openAudioDevice);
    ~AudioEngine() override;

    /** Set for command-line runs: even with the real device open, the engine reads a private
        copy of the studio's settings and writes nothing back (no device setup, no plugin
        list), so a test can't overwrite what the studio saved. */
    static inline bool keepStudioSettings = false;

    te::Engine& getEngine()                     { return *engine; }
    te::Edit& getEdit()                         { return *edit; }
    juce::AudioDeviceManager& getDeviceManager()   { return engine->getDeviceManager().deviceManager; }

    //==============================================================================
    /** Play, or stop if playing, as FL's Play: with Record armed, Play records. Returns
        something to tell the user (what's recording, or why nothing can), or {}. */
    juce::String togglePlay();
    void stop();
    bool isPlaying() const;

    /** FL's Record button. Armed, Play records what's played on the selected instrument
        channel (into the current pattern as it loops in pattern mode; in song mode, into a
        pattern clip placed where playback started) and every armed audio track (into the
        Arrange, in song mode). Arming while playing punches in, disarming punches out; it
        stays armed after Stop. Returns something to tell the user, or {}. */
    juce::String setRecordArmed (bool);
    bool isRecordArmed() const                  { return recordArmed; }

    /** How recorded notes snap, in ticks (the piano roll's snap; 1 keeps them as played). */
    void setRecordSnap (int ticks)              { recordSnapTicks = juce::jmax (1, ticks); }

    /** What Play would record now, for the user: "Keys (notes) and Guitar (audio)", or "". */
    juce::String describeRecordTargets() const;

    /** Playhead from the start of the loop: the pattern in pattern mode, the song in song mode. */
    double getPlayheadTicks() const;
    void setPlayheadTicks (double);

    /** Plays a note on a channel immediately, whether or not the transport is running. */
    void previewNote (int channelId, int key, int velocity = 100, int lengthMs = 400);

    /** A note from a keyboard (the PC's, or one plugged in) on a channel's instrument, held
        until liveNoteOff: heard at once, and while notes are being recorded on that channel,
        written into the pattern. */
    void liveNoteOn (int channelId, int key, int velocity);
    void liveNoteOff (int channelId, int key);

    /** Notes are being recorded (or Play is waiting for the first one). */
    bool isRecordingNotes() const               { return noteTake.has_value(); }
    int getNoteRecordingChannel() const         { return noteTake.has_value() ? noteTake->channelId : 0; }

    /** Play was pressed with "start when you play" on: nothing moves until the first note. */
    bool isWaitingForInput() const              { return noteTake.has_value() && noteTake->waiting; }

    //==============================================================================
    /** The metronome (see MetronomePlugin): heard after the master, never exported. */
    MetronomePlugin* getMetronome() const       { return dynamic_cast<MetronomePlugin*> (metronomePlugin.get()); }

    /** The metronome clicking on its own to practise to; Play stops it, and it stops Play. */
    void setPractising (bool);
    bool isPractising() const;

    /** The time signature playing now: the song's at its position in Song mode, the pattern's in
        Pattern mode. Changing it changes the stretch the song position is in (the start's, or a
        change's), or in Pattern mode the pattern's own if it has one, else the song's at the start. */
    std::pair<int, int> getMeterNow() const;
    void setMeterNow (int beats, int unit);

    /** The tempo playing now: at the song position in Song mode (along a glide, in between), else
        the start's. Changing it changes the change in effect there, or the start's. */
    double getTempoNow() const;
    void setTempoNow (double bpm);

    /** How long a count-in of `bars` before `homeSeconds` lasts: the bars before it as the song
        plays them (its tempo and time signature there), a pattern's in Pattern mode. */
    double countInSeconds (double homeSeconds, int bars) const;

    /** A time signature as the metronome groups it (7/8 as 2+2+3, say), and the accent on each
        of its beats; setting either keeps it for that time signature. Without one, the one
        playing now. */
    metronome::Signature getClickSignature() const;
    metronome::Signature getClickSignature (int beats, int unit) const;
    std::vector<int> getClickAccents() const;
    std::vector<int> getClickAccents (int beats, int unit) const;
    void setClickGrouping (const metronome::Signature&);
    void setClickAccents (const std::vector<int>&);

    /** Master peak per side since the last call, as gain. */
    std::pair<float, float> getMasterPeak();

    /** What getMasterPeak last returned, for a second display of it (the inspector). */
    std::pair<float, float> getLastMasterPeak() const   { return lastMasterPeak; }

    /** Reads the meters of the strips on screen (channels and buses, by id; after their
        effects and faders), once per UI tick: peak per side since the last read, as gain.
        Strips left out stop being metered. */
    void readMeters (const std::vector<int>& ids);

    /** What readMeters last found for a channel or bus (silence if it wasn't asked for). */
    std::pair<float, float> getPeak (int id) const;

    /** How hard the master limiter is working, in dB. */
    float getLimiterReductionDb() const;

    /** How loud the master is (EBU R128: now, the last 3 seconds, since Play, the range, the
        true peak), as the Master tab shows it. Starts again whenever playback starts. */
    mastering::Readings getMasterLoudness() const;
    void resetMasterLoudness();

    /** How wide the master is (and how it holds up in mono), and how hard each of the Master tab's
        Punch bands (0 lows, 1 mids, 2 highs) is squeezing, in dB. */
    tracktion::StereoFieldAnalyser::Readings getMasterStereo() const;
    float getPunchReductionDb (int band) const;

    /** Renders the song (or pattern, as an export would) as it goes into the master limiter -
        after the master volume, before mastering's push and the limiting - to `file`, a 32-bit
        float WAV, for mastering::findPush to work out the push from. */
    bool captureMix (const juce::File&, bool raw = false);

    /** Before and after at the same loudness (see MasterLimiter::setCompare): 0 off, 1 before, 2 after. */
    void setMasterCompare (int mode, float afterGainDb);
    int getMasterCompare() const                        { return masterCompare; }

    //==============================================================================
    /** Audio only (Play with Record armed does this and the notes together): starts
        recording every armed audio channel from the song position (after the count-in), or
        stops and keeps the takes. With the playlist loop on, it records round the loop and
        every pass becomes a take in a folder. Returns why it couldn't start, or {}. */
    juce::String toggleRecord();

    /** Crossfade at each join between comp segments, and the fade at a folder's free edges. */
    static constexpr double compCrossfadeSeconds = 0.010, edgeFadeSeconds = 0.002;

    /** Audio is being recorded. */
    bool isRecording() const;

    /** True while the count-in is still running before a take (audio or notes) starts. */
    bool isCountingIn() const;

    /** Where the current take starts, in ticks. */
    double getRecordStartTicks() const          { return recordStartTicks; }

    /** The inputs an audio channel can record from, e.g. "Input 1". */
    juce::StringArray getInputNames() const;

    /** Peak of an input since the last call, as gain. */
    float getInputPeak (const juce::String& inputName);

    /** Where recordings go: next to the project once it is saved, else Documents. */
    void setProjectFile (const juce::File&);

    /** Once, if Windows refused the recordings folder: what happened and where takes went. */
    juce::String takeRecordingFolderNotice();

    //==============================================================================
    struct LatencyInfo
    {
        juce::String driver, device;
        double sampleRate = 0.0;
        int bufferSize = 0, inputSamples = 0, outputSamples = 0, xruns = -1;
        double cpu = 0.0;

        double toMs (int samples) const     { return sampleRate > 0.0 ? samples * 1000.0 / sampleRate : 0.0; }
        double roundTripMs() const          { return toMs (inputSamples + outputSamples); }
    };

    LatencyInfo getLatencyInfo() const;

    /** Options kept with the app rather than the song. */
    struct Options
    {
        bool lowLatencyMonitoring = true;   // bypass plugins that add delay while monitoring an armed track
        bool preferInterface = true;        // open the preferred interface with ASIO at startup
        int countInBars = 1;                // countdown before recording: 0, 1 or 2 bars
        bool computerKeyboard = true;       // the PC keyboard plays the selected channel

        // Recording, as FL's Record menu has it
        bool blendNotes = true;             // a take keeps the notes already there (off: it replaces them)
        bool startOnInput = false;          // armed, Play waits for the first note played

        // The metronome
        bool metronome = false;             // click while the song plays
        bool metronomeRecordingOnly = false;
        int clickSound = 0, clickRhythm = 0;
        float clickVolume = 0.7f, clickPan = 0.0f;
        std::map<juce::String, juce::String> clickGroupings;            // "7/8" -> "2+2+3"
        std::map<juce::String, std::vector<int>> clickAccents;          // a signature's name -> its accents

        // Starting up
        bool startScreen = true;            // open on "What do you want to do?" (templates, recent projects)
        juce::StringArray recentProjects;   // newest first, full paths
        bool donationAsked = false;         // the start screen's "NightShift is free" banner has been shown (it asks once)

        // Export: 24 (for more work on it), 16 (with dither: CDs and most uploads) or 32 (float)
        int exportBits = 24;

        // MIDI inputs switched off in Audio settings (the rest play the selected channel)
        juce::StringArray disabledMidiInputs;

        // Synths plugged in: each MIDI output's timing and whether it follows the tempo, by its name
        std::map<juce::String, MidiOutHub::OutputSettings> midiOuts;

        bool operator== (const Options&) const = default;
    };

    Options getOptions() const              { return options; }
    void setOptions (const Options&);

    /** The interface Nightshift is built around; matched against ASIO driver names. */
    static constexpr const char* preferredInterface = "MiniFuse";

    /** The system's low-latency driver, the one the interface is opened on: ASIO on Windows, the
        Mac's own Core Audio (as low as ASIO, built in), and how it's named to the user. */
   #if JUCE_MAC
    static constexpr const char* lowLatencyDriver = "CoreAudio";
    static constexpr const char* lowLatencyName = "Core Audio";
    static constexpr const char* systemAudioName = "Core Audio";
   #else
    static constexpr const char* lowLatencyDriver = "ASIO";
    static constexpr const char* lowLatencyName = "ASIO";
    static constexpr const char* systemAudioName = "Windows audio";
   #endif

    /** Opens the preferred interface through ASIO with a small buffer, if it is there.
        Returns true if it is now the open device. 32 samples measured 5.7 ms round trip
        on the MiniFuse 2 at 16% CPU playing the demo; 16 saves only 0.3 ms more. */
    bool openPreferredInterface (int bufferSize = 32);

    /** Plugins currently bypassed so monitoring adds no delay. */
    juce::StringArray getLatencyBypassedPlugins() const;

    /** Headless runs only: pushes one block through the engine as if from a device (with MIDI
        into its one MIDI input, for the second). */
    void processHostedBlock (juce::AudioBuffer<float>&);
    void processHostedBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&);

    //==============================================================================
    /** A parameter automation can drive (a Song::AutomationLane's key) and what it's called:
        its group ("" for the channel's own volume and pan, "Sends", or an effect's or the
        instrument's name) and its own name. */
    struct AutomationTarget
    {
        juce::String param, group, name;
    };

    /** Everything on a channel (or the master, for 0) that automation can drive. */
    std::vector<AutomationTarget> automationTargets (int owner) const;

    /** The engine's parameter behind a lane's key, if it's there (its plugin may be gone). */
    te::AutomatableParameter* findAutomationParameter (int owner, const juce::String& param) const;

    /** Which lane a parameter would be (a knob's right-click Automate): its owner and key. */
    std::optional<std::pair<int, juce::String>> automationKeyFor (const te::AutomatableParameter&) const;

    /** What a lane is called: "Volume", "Send to Reverb", "Delay: Feedback". */
    juce::String automationName (int owner, const juce::String& param) const;

    /** A lane's points as its parameter's curve takes them, ticks and values: a bent line as
        short straight ones (see syncAutomation). */
    static std::vector<std::pair<double, float>> curvePoints (const AutomationLane&);

    /** Recording automation (Logic's touch, FL's automation recording): switched on, a control
        moved while the song plays in Song mode writes its lane, from where it was grabbed to where
        it was let go (see AutomationRecorder). Off each time NightShift opens. */
    void setAutomationRecording (bool);
    bool isAutomationRecording() const;

    /** Switched on, and the song playing in Song mode past any count-in: a control moved now is
        written. */
    bool isWritingAutomation() const;

    /** Whether a lane is being written now (its header shows it). */
    bool isWritingLane (int owner, const juce::String& param) const;

    /** One of the song's own controls grabbed (down) or let go, by its lane's key: a fader, a pan
        bar, a send. Not `held`: a change with no holding (the wheel, a double-click), written until
        it's left alone a moment. */
    void touchControl (int owner, const juce::String& param, bool down, bool held = true);

    /** A knob's parameter grabbed or let go (a stock effect's, Nova's); `held` as above. */
    void touchParameter (te::AutomatableParameter&, bool down, bool held = true);

    /** A lane has started being written: the studio makes sure it's on show. */
    std::function<void (int owner, const juce::String& param)> onAutomationWriting;

    /** Where in the song what's heard now is, in ticks: the playhead less the output's delay. */
    double getHeardTicks() const;

    /** Every parameter automation can drive now: the channels', buses' and the master's. */
    std::vector<te::AutomatableParameter*> allAutomationParameters() const;

    /** Puts a parameter's value (in its own units) into its control: the song's own (volume, pan,
        a send, the master volume) through the song, so their faders show it; anything else
        straight into the parameter. */
    void takeParameterValue (int owner, const juce::String& param, float value);

    /** What one of the song's own controls (volume, pan, a send, the master volume) is at while its
        automation drives it, in the song's units: Song mode playing a lane with points; else
        nothing, and it's where the song has it. */
    std::optional<float> getAutomatedControl (int owner, const juce::String& param) const;

    //==============================================================================
    /** MIDI keyboards and controllers. Every MIDI input (bar those switched off in Audio
        settings) plays the channel getMidiTargetChannel names, straight through the engine
        (Tracktion's input monitoring: no lag beyond the audio's own); while notes record they're
        written as the PC keyboard's are, the sustain pedal holding them on; and controller knobs
        move what they're linked to (MIDI learn, Song::controllerLinks). */
    juce::StringArray getMidiInputNames() const;
    bool isMidiInputEnabled (const juce::String& name) const    { return ! options.disabledMidiInputs.contains (name); }
    void setMidiInputEnabled (const juce::String& name, bool);

    /** The instrument channel MIDI keyboards play: the selected channel, or with an audio track
        selected, the last instrument that was (0 for none). */
    int getMidiTargetChannel() const;

    /** When MIDI last came in (Time::getMillisecondCounter; 0 for never), for the top bar's light. */
    juce::uint32 getLastMidiActivity() const        { return lastMidiActivity.load(); }

    /** MIDI learn: the next knob or fader moved on a MIDI input is linked to a control (owner and
        key as for automation lanes); onLinked says what it was ("CC 21 on KeyLab"). */
    void learnMidiController (int owner, const juce::String& param, std::function<void (const juce::String&)> onLinked);
    void cancelMidiLearn()                          { midiLearn.reset(); }
    bool isLearningMidi() const                     { return midiLearn.has_value(); }

    /** One message from a MIDI input, as the engine takes it on the message thread: notes into a
        recording, the pedal, learning and links (the sound has gone through the engine already).
        `secondsAgo`: how long since it arrived. Tests call it as a device would. */
    void handleMidiInput (const juce::String& device, const juce::MidiMessage&, double secondsAgo = 0.0);

    /** A MIDI input that's appeared while running (plugged in), by name. */
    std::function<void (const juce::String&)> onMidiInputAdded;

    /** Tests: a headless engine sees the system's MIDI devices (the real path) instead of the
        one input that processHostedBlock feeds. Set before making the engine. */
    static inline bool headlessMidiDevices = false;

    /** Tests: that one input is routed as a MIDI keyboard is (to the channel being played,
        always heard), so MIDI handed to processHostedBlock plays through the engine. */
    static inline bool routeHostedMidi = false;

    //==============================================================================
    /** Synths plugged in (MIDI out). A channel whose instrument is one (InstrumentType::midiOut)
        sends its notes out of a MIDI output on a MIDI channel, in time with the song: its patterns
        and clips as they play, and what a keyboard plays on it, at once. An output can be timed
        earlier for a synth slow to sound (or later), and can follow the tempo: MIDI clock, start
        and stop. The outputs there are (a command-line run's: MidiOutHub::testOutputs). */
    juce::StringArray getMidiOutputNames() const;
    MidiOutHub::OutputSettings getMidiOutSettings (const juce::String& output) const;
    void setMidiOutSettings (const juce::String& output, MidiOutHub::OutputSettings);

    /** A short middle C (or the channel's root) on a channel's synth plugged in, to hear it's connected. */
    void sendTestNote (int channelId);

    /** Asks a channel's synth for its program (Channel::midiProgram) now. */
    void sendMidiProgram (int channelId);

    MidiOutHub& getMidiOut()                        { return *midiOut; }

    //==============================================================================
    /** Renders the current pattern (pattern mode) or the whole song, plus a tail. */
    bool renderToFile (const juce::File&, double tailSeconds = 2.0, int bitDepth = 24);

    /** Copies synth and plugin state into the Song so it can be saved. */
    void flushPluginStates();

    /** Applies pending Song changes now instead of on the next message loop turn. */
    void syncNow();

    te::Plugin* getInstrument (int channelId) const;

    /** A synth channel's Nova (null if it isn't one, or still plays the old synth). */
    NovaPlugin* getNova (int channelId) const;

    /** Puts a sound on a synth channel: Nova plays it from the next block, and the channel
        takes its name and keeps its state (switching from the old synth if need be). */
    void applySynthPatch (int channelId, const nova::Patch&);

    /** Switches a Nightshift Guitar channel's sound (one of guitarsound::presetNames()), kept in
        the channel so it's saved with the song; the caller checkpoints for undo. */
    void setGuitarPreset (int channelId, const juce::String& preset);

    /** The track playing a channel or bus, for tests. */
    te::AudioTrack* getTrack (int id) const;

    /** The plugin playing an effect in a channel's chain or the master's (see Effect::id). */
    te::Plugin* getEffect (int effectId) const;
    juce::KnownPluginList& getKnownPlugins()    { return engine->getPluginManager().knownPluginList; }

    /** One developer's plugins, as pluginsByDeveloper lists them. */
    struct Developer
    {
        juce::String name;                          // as the plugins say it, less ", LLC" and the like; empty when they don't
        std::vector<juce::PluginDescription> plugins;
        juce::StringArray given;                    // its name as its plugins give it, each way they do
    };

    /** The plugin effects (or instruments) in a list, by developer: the developers A to Z (one
        whose plugins don't say who made them last, its name empty), each one's plugins A to Z.
        Spelled two ways ("FabFilter", "Fabfilter", "Valhalla DSP, LLC") it's one developer. On a
        Mac most plugins come as VST3 and Audio Unit both: the VST3 is kept (what a song saved on a
        PC names). */
    static std::vector<Developer> pluginsByDeveloper (const juce::KnownPluginList&, bool instruments);

    /** A developer's name without its company's legal ending ("Valhalla DSP, LLC": Valhalla DSP). */
    static juce::String developerName (const juce::String& manufacturer);

    /** An effect's description from its identifier string: a stock effect, or a scanned plugin. */
    std::unique_ptr<juce::PluginDescription> findEffectType (const juce::String& identifier);
    juce::AudioPluginFormatManager& getPluginFormats()  { return engine->getPluginManager().pluginFormatManager; }

    /** Finds VST3 plugins installed since the list was made (in the usual folders, the per-user
        one too) and scans just those, each in a process of its own so a bad one can't take
        Nightshift down, off the message thread. `done` gets the new ones' names, on the message
        thread. Asked again while one is running, it waits for that one and hears its names. */
    void findNewPlugins (std::function<void (const juce::StringArray&)> done);
    bool isFindingPlugins() const               { return findingPlugins; }

    /** How many plugin files the last look for new plugins scanned (a list that's up to date: none). */
    int getPluginsScannedLastTime() const       { return pluginsScannedLastTime; }

    /** Tests: the one folder the finder looks in (VST3s only), instead of the usual places. */
    static inline juce::File pluginFolderOverride;

    /** True when the list has this plugin file and nothing of it needs scanning again: listed by
        the file itself or by a file inside it. A VST3 bundle without a moduleinfo.json (Surge XT,
        CHOWTapeModel, TAL's) is listed by the module inside it (Contents\x86_64-win\<name>.vst3),
        never by the bundle the search finds, so JUCE's own check always said "not listed". */
    static bool isListedAndCurrent (const juce::KnownPluginList&, const juce::String& file, juce::AudioPluginFormat&);

    /** Which plugin a description is, wherever its file is and however new: a plugin found again
        (a new build of it, moved, or listed by another of its files) isn't a new plugin. */
    static juce::String pluginKey (const juce::PluginDescription&);

    /** Whether this is the studio itself (the real audio device), not a command-line run. */
    bool isStudio() const                       { return liveAudio && ! keepStudioSettings; }

    static juce::File resolveSample (const juce::String&);

    /** Nightshift's own copy of the scanned plugin list (Plugins.xml beside the settings):
        written the moment a scan changes the list, read back at startup. Returns false if
        it couldn't be written / read. */
    static juce::File pluginListFile();
    static bool savePluginList (const juce::KnownPluginList&, const juce::File&);
    static bool loadPluginList (juce::KnownPluginList&, const juce::File&);

private:
    struct Slot
    {
        te::AudioTrack::Ptr track;
        te::Plugin::Ptr instrument;
        juce::ReferenceCountedObjectPtr<te::MidiClip> patternClip;
        InstrumentType type = InstrumentType::sampler;
        juce::String source;
        int rootKey = -1;
        bool oneShot = true;
        std::vector<std::pair<int, te::Plugin::Ptr>> effects;     // by Effect::id, in chain order
        std::vector<std::pair<juce::String, te::Plugin::Ptr>> sends;  // aux sends, keyed by sendKey
    };

    /** A bus's track: an aux return first (where its sends arrive), then its effects. */
    struct BusSlot
    {
        te::AudioTrack::Ptr track;
        te::Plugin::Ptr auxReturn;
        int auxBus = -1;
        std::vector<std::pair<int, te::Plugin::Ptr>> effects;
    };

    /** Notes when the audio device starts: opened, switched, or reset by its driver (the
        MiniFuse's does just after opening). Outputs nothing. */
    struct DeviceStartWatch : public juce::AudioIODeviceCallback
    {
        std::atomic<juce::uint32> lastStart { 0 };

        void audioDeviceAboutToStart (juce::AudioIODevice*) override    { lastStart = juce::Time::getMillisecondCounter(); }
        void audioDeviceStopped() override                              {}

        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&) override
        {
            for (int ch = 0; ch < numOutputs; ++ch)
                if (outputs[ch] != nullptr)
                    juce::FloatVectorOperations::clear (outputs[ch], numSamples);
        }
    };

    struct MeterTap
    {
        te::LevelMeasurer* measurer = nullptr;
        te::LevelMeasurer::Client client;
    };

    enum Pending
    {
        needTempo     = 1 << 0,
        needChannels  = 1 << 1,
        needMixer     = 1 << 2,
        needNotes     = 1 << 3,
        needClips     = 1 << 4,
        needInputs    = 1 << 5,
        needMutes     = 1 << 6,
        needAutomation = 1 << 7,
        needMidi      = 1 << 8
    };

    struct InputMeter
    {
        te::LevelMeasurer* measurer = nullptr;
        te::LevelMeasurer::Client client;
    };

    Song& song;
    const bool liveAudio;
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<juce::Thread> pluginFinder;
    std::vector<std::function<void (const juce::StringArray&)>> pluginFinderWaiting;   // message thread only
    bool findingPlugins = false;
    int pluginsScannedLastTime = 0;
    void pluginsFound (const juce::StringArray&, int scanned);
    std::unique_ptr<te::Edit> edit;
    std::map<int, Slot> slots;
    std::map<int, BusSlot> busSlots;
    te::LevelMeasurer::Client meterClient;
    std::map<int, std::unique_ptr<MeterTap>> meterTaps;
    std::map<int, std::pair<float, float>> peaks;
    std::pair<float, float> lastMasterPeak;
    te::Plugin::Ptr masterMeter, masterLimiter;
    int masterCompare = 0;
    float masterCompareDb = 0.0f;
    std::map<juce::String, std::unique_ptr<InputMeter>> inputMeters;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    // Play was pressed and not stopped: a device starting suspends playback while it's
    // prepared, and the transport stops itself; the timer carries on playing
    DeviceStartWatch deviceStarts;
    bool wantsToPlay = false;
    Options options;

    int pending = needTempo | needChannels | needMixer | needClips | needInputs;
    PlayMode builtMode = PlayMode::pattern;
    int builtPatternId = -1, builtLength = 0, loopEndTicks = ticksPerBeat * 4;
    double startTicks = 0.0, recordStartTicks = 0.0;
    bool takeCheckpointed = false;

    // Set while recording round the playlist loop: its range, in ticks
    std::optional<std::pair<int, int>> loopTake;

    // Set while notes are being recorded: the channel and pattern they go to, where the take
    // starts in the song (`origin`, ticks) and how long it goes round (`wrap`: the pattern, or
    // the playlist loop; 0 runs on), the keys held (where in the take each began, and how
    // hard), and notes finished but not yet written (they go in once every key is up, so a
    // rebuild can't cut a note still held). In song mode the pattern is picked, and its clip
    // placed, when the first notes go in.
    struct NoteTake
    {
        int channelId = 0, patternId = 0, snapTicks = 1;
        bool songMode = false, blend = true, waiting = false, wrote = false;
        double origin = 0.0, wrap = 0.0, latencySeconds = 0.0;
        std::map<int, std::pair<double, int>> held;
        std::vector<Note> finished;
    };

    std::optional<NoteTake> noteTake;
    bool recordArmed = false;
    int recordSnapTicks = ticksPerStep;

    // MIDI inputs: a watcher on each (on its own thread: into midiQueue, then midiDrain on the
    // message thread), the channel they play, the keys down (by the channel they went to), the
    // pedal and the keys it holds, and a learn under way
    struct MidiWatcher;
    std::vector<std::unique_ptr<MidiWatcher>> midiWatchers;

    struct IncomingMidi
    {
        juce::MidiMessage message;
        double arrivedMs = 0.0;
        juce::String device;
    };

    juce::SpinLock midiLock;
    std::vector<IncomingMidi> midiQueue;

    struct MidiDrain : public juce::AsyncUpdater
    {
        std::function<void()> drain;
        void handleAsyncUpdate() override       { if (drain != nullptr) drain(); }
    };

    MidiDrain midiDrain;
    std::atomic<juce::uint32> lastMidiActivity { 0 };
    juce::StringArray knownMidiInputs;
    bool midiInputsListed = false, midiRetargetPending = false;
    int lastMidiChannel = 0, midiRoutedChannel = 0;
    std::map<int, int> midiNoteChannels, midiPedalHeld;   // key -> the channel it's on
    bool midiPedal = false;

    struct MidiLearn
    {
        int owner = 0;
        juce::String param;
        std::function<void (const juce::String&)> onLinked;
    };

    std::optional<MidiLearn> midiLearn;

    void syncMidiInputs();

    /** The song's notes on synths plugged in, as the MIDI out plays them (see MidiOutHub). */
    void syncMidiOut();

    /** Tracktion's own MIDI outputs closed: the hub opens the ones it needs (a port open twice fails on Windows). */
    void closeTracktionMidiOutputs();
    std::unique_ptr<MidiOutHub> midiOut;
    int midiOutRefresh = 0;
    void drainMidi();
    void recordNoteOn (int channelId, int key, int velocity, double secondsAgo);
    void recordNoteOff (int channelId, int key, double secondsAgo);
    bool applyControllerLinks (const juce::String& device, const juce::MidiMessage&);

    // The transport's own looping is held off until the playhead reaches here (seconds): a
    // count-in before a looping pattern or loop plays in from before it
    std::optional<double> loopFrom;

    // A take's count-in ends here (seconds), for isCountingIn
    double countInEnd = -1.0e9;

    te::Plugin::Ptr metronomePlugin;

    /** Where in the take the sound being heard now is, in ticks (negative in the count-in). */
    double heardTakeTicks (double secondsAgo = 0.0) const;
    void finishNote (int key, double secondsAgo = 0.0);
    void writeTakeNotes();

    /** Play, recording nothing. */
    void startPlaying();

    /** Puts the transport's looping back as the song wants it (round the pattern, the
        playlist loop or the song), unless a recording holds it. */
    void syncLooping();

    /** Starts what Play records with Record armed; `punchIn`: already playing. */
    juce::String startRecordingPass (bool punchIn);
    juce::String startNoteTake (int channelId, bool punchIn);
    void stopNoteTake();

    /** Plays from `homeSeconds` after `countInBars` of count-in, holding the transport's
        looping off until the playhead reaches home. */
    void playWithCountIn (double homeSeconds, int countInBars);
    void syncMetronome();
    void endCountIn();

    // Song mode's clips and the playlist row each came from, so mute and solo can switch
    // them in place
    std::vector<std::pair<te::Clip::Ptr, int>> rowClips;

    // The master's effects, before its limiter
    std::vector<std::pair<int, te::Plugin::Ptr>> masterFx;

    // What syncAutomation last wrote into each lane's parameter ("owner/param"), so it writes
    // a curve again only when the lane, the tempo or the parameter changed
    struct WrittenAutomation
    {
        te::AutomatableParameter* param = nullptr;
        std::vector<AutomationPoint> points;
        double bpm = 0.0;
        std::vector<TempoChange> tempoChanges;
        int unit = 4;
    };

    std::map<juce::String, WrittenAutomation> writtenAutomation;

    // Recording automation: made with the edit, gone before it
    std::unique_ptr<AutomationRecorder> automationRecorder;

    void songChanged (Song::Change) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void recordingFinished (te::InputDeviceInstance&, te::EditItemID, const juce::ReferenceCountedArray<te::Clip>&) override;
    void recordingStopped (te::SyncPoint, bool discardRecordings) override;
    void playbackContextChanged() override;
    void startVideo() override                  { resetMasterLoudness(); }  // playback started, however it was

    void syncTempo();
    void syncChannels();
    void syncBuses();
    void syncMixer();
    void syncSends (Slot&, const Channel&);
    static juce::String sendKey (const Send&, int auxBus);
    void dropMeter (int id);
    void syncClips (bool notesOnly);
    void syncInputs();
    void syncMutes();
    void syncEffects();

    /** The song's automation lanes into the parameters' curves (Song mode only); curves no lane
        asks for come off, and the controls' own values go back on. */
    void syncAutomation();
    void syncSidechains();
    void syncChain (te::PluginList&, std::vector<std::pair<int, te::Plugin::Ptr>>& built, const std::vector<Effect>& wanted, int firstIndex);
    te::Plugin::Ptr createEffect (const Effect&);
    void keepEffectState (int effectId, te::Plugin&);
    void buildInstrument (Slot&, const Channel&);
    /** The notes into an engine clip; a split clip passes its part of the pattern (offset and
        length in ticks; -1 for all of it). */
    void fillClip (te::MidiClip&, const std::vector<Note>*, int offset = 0, int length = -1);
    void removeAllClips();

    /** Each instrument's clips under the playhead, as text that changes if they do. */
    std::map<int, juce::String> clipsUnderPlayhead() const;

    /** After clips were rebuilt while playing: note-offs for instruments whose clip under
        the playhead went away or changed (muting a row mid-chord used to leave it held). */
    void releaseNotesLeftBy (const std::map<int, juce::String>& before);
    void clearInputMeters();
    void loadOptions();

    int channelForTrack (te::EditItemID) const;
    te::InputDeviceInstance* findInput (const juce::String& name) const;
    bool anyArmedInput() const;

    /** Ticks (quarter-note based) to Tracktion beats (the time signature's unit). */
    double beatsFor (double ticks) const;
    double ticksToSeconds (double ticks) const;
    double secondsToTicks (double seconds) const;

    /** Where Play starts and Stop returns to: the loop's start while the loop is on, else
        the song position. */
    double homeTicks() const;
    static juce::String sourceKey (const Channel&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace nightshift
