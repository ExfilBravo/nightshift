// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "AudioEngine.h"
#include "AutomationRecorder.h"
#include "model/Paths.h"
#include "model/SongKey.h"
#include "Stretcher.h"
#include "DrumKit.h"
#include "DrumSynth.h"
#include "MasterLimiter.h"
#include "fx/StockEffects.h"
#include "Kits.h"
#include "Machines.h"
#include "Sfz.h"
#include "Nova.h"
#include "GuitarSound.h"
#include "SamplePreview.h"
#include "MidiFiles.h"
#include "Freeze.h"
#include "Mp3Encoder.h"
#include <tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h>
#include <set>

namespace nightshift
{

namespace
{
    /** A sound of nothing. Tracktion's own click plays through every count-in whatever its
        metronome is set to; Nightshift's metronome does the counting in, so Tracktion's is
        given this to play. */
    juce::File silentClick()
    {
        static const juce::File file = []
        {
            auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Nightshift silent click.wav");

            if (! f.existsAsFile())
            {
                juce::AudioBuffer<float> silence (1, 32);
                silence.clear();
                drumsynth::writeWav (f, silence, 48000.0);
            }

            return f;
        }();

        return file;
    }

    /** Tracktion's settings, with its click silenced (see silentClick). */
    struct StudioStorage : public te::PropertyStorage
    {
        using te::PropertyStorage::PropertyStorage;

        // Tracktion's default is userApplicationDataDirectory/Nightshift, which on a Mac is a
        // loose ~/Library/Nightshift; keep Settings.xml with the rest in paths::appData()
        juce::File getAppPrefsFolder() override
        {
            auto f = paths::appData();
            f.createDirectory();
            return f;
        }

        juce::var getProperty (te::SettingID setting, const juce::var& defaultValue) override
        {
            if (setting == te::SettingID::clickTrackSampleBig || setting == te::SettingID::clickTrackSampleSmall)
                return silentClick().getFullPathName();

            return te::PropertyStorage::getProperty (setting, defaultValue);
        }
    };

    struct NightEngineBehaviour : public te::EngineBehaviour
    {
        explicit NightEngineBehaviour (bool open) : openDevice (open) {}

        bool autoInitialiseDeviceManager() override     { return openDevice; }
        bool shouldOpenAudioInputByDefault() override   { return false; }
        bool canScanPluginsOutOfProcess() override      { return true; }

        // A bypassed plugin left in the graph still delays everything by its lookahead;
        // low-latency monitoring only works if bypassing takes it out entirely
        bool shouldBypassedPluginsBeRemovedFromPlaybackGraph() override   { return true; }

        juce::File getDefaultFolderForAudioRecordings (te::Edit&) override
        {
            // Windows' Controlled Folder Access can refuse Documents to an app it doesn't
            // know; Tracktion would then write into whatever the working directory is.
            // Use a folder in the user's profile instead and say so once.
            // Each refused write pops a Defender notification, so a folder is tested once
            if (recordingFolder == writableFolder)
                return recordingFolder;

            if (blockedFolder != recordingFolder)
            {
                if (isWritable (recordingFolder))
                {
                    writableFolder = recordingFolder;
                    return recordingFolder;
                }

                blockedFolder = recordingFolder;
               #if JUCE_MAC
                notice = "macOS blocked NightShift from saving takes in\n" + recordingFolder.getFullPathName()
                       + "\n(Files and Folders access to Documents was refused).\n\n"
                         "Your takes are going to\n" + fallbackFolder.getFullPathName() + "\ninstead. To keep them "
                         "beside your projects, allow NightShift in System Settings > Privacy & Security > Files and Folders.";
               #else
                notice = "Windows blocked NightShift from saving takes in\n" + recordingFolder.getFullPathName()
                       + "\n(Controlled folder access, in Windows Security > Ransomware protection).\n\n"
                         "Your takes are going to\n" + fallbackFolder.getFullPathName() + "\ninstead. To keep them "
                         "beside your projects, allow NightShift through Controlled folder access.";
               #endif
            }

            fallbackFolder.createDirectory();
            return fallbackFolder;
        }

        static bool isWritable (const juce::File& folder)
        {
            if (! folder.createDirectory().wasOk())
                return false;

            // Write access checks don't see Controlled Folder Access; only a real write does
            auto probe = folder.getNonexistentChildFile (".nightshift-write-test", ".tmp");
            const bool ok = probe.create().wasOk();
            probe.deleteFile();
            return ok;
        }

        bool openDevice;
        juce::File recordingFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                        .getChildFile ("Nightshift").getChildFile ("Recordings"),
                   fallbackFolder = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                        .getChildFile ("Nightshift").getChildFile ("Recordings"),
                   writableFolder, blockedFolder;
        juce::String notice;
    };

    /** Runs engine jobs (renders, mostly) on a thread while keeping the message loop alive. */
    struct NightUIBehaviour : public te::UIBehaviour
    {
        void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& task) override
        {
            struct Runner : public juce::Thread
            {
                explicit Runner (te::ThreadPoolJobWithProgress& t) : juce::Thread (t.getJobName()), job (t)  { startThread(); }
                ~Runner() override    { job.signalJobShouldExit(); stopThread (10000); }

                void run() override
                {
                    while (! threadShouldExit())
                        if (job.runJob() == juce::ThreadPoolJob::jobHasFinished)
                            break;
                }

                te::ThreadPoolJobWithProgress& job;
            };

            Runner runner (task);

            // Keep messages flowing while the job runs; if the loop can't be pumped, just wait
            while (runner.isThreadRunning())
                if (! juce::MessageManager::getInstance()->runDispatchLoopUntil (10))
                    juce::Thread::sleep (10);
        }

        void showWarningMessage (const juce::String& message) override    { report ("Warning", message); }
        void showInfoMessage (const juce::String& message) override       { report ("Info", message); }
        void showWarningAlert (const juce::String& title, const juce::String& message) override  { report (title, message); }

        void report (const juce::String& title, const juce::String& message)
        {
            std::cerr << title << ": " << message << std::endl;
            juce::Logger::writeToLog (title + ": " + message);
        }
    };

    /** Settings for command-line runs: the studio's, read from a private copy that is thrown
        away afterwards. Writing the real file back from a copy loaded minutes earlier wiped a
        plugin scan the studio had saved in between; and a run's virtual device or made-up
        plugin list must never reach the studio either way. */
    struct HeadlessStorage : public StudioStorage
    {
        // Live runs (the real device open) save to the copy as the studio would; with the
        // virtual device nothing about devices or plugins is saved even there
        HeadlessStorage (const juce::String& name, bool live) : StudioStorage (name), liveDevice (live) {}
        const bool liveDevice;

        ~HeadlessStorage() override
        {
            copy = nullptr;
            copyFile.deleteFile();
        }

        juce::PropertiesFile& getPropertiesFile() override
        {
            if (copy == nullptr)
            {
                copyFile = juce::File::createTempFile (".xml");
                getAppPrefsFolder().getChildFile ("Settings.xml").copyFileTo (copyFile);

                juce::PropertiesFile::Options options;
                options.millisecondsBeforeSaving = -1;
                options.storageFormat = juce::PropertiesFile::storeAsXML;
                copy = std::make_unique<juce::PropertiesFile> (copyFile, options);
            }

            return *copy;
        }

        std::unique_ptr<juce::PropertiesFile> copy;
        juce::File copyFile;

        void setXmlProperty (te::SettingID setting, const juce::XmlElement& xml) override
        {
            if (liveDevice || (setting != te::SettingID::audio_device_setup && setting != te::SettingID::knownPluginList
                                 && setting != te::SettingID::knownPluginList64))
                te::PropertyStorage::setXmlProperty (setting, xml);
        }

        void setXmlPropertyItem (te::SettingID setting, juce::StringRef item, const juce::XmlElement& xml) override
        {
            if (liveDevice || setting != te::SettingID::audiosettings)
                te::PropertyStorage::setXmlPropertyItem (setting, item, xml);
        }
    };

    /** Lets a save or render see every plugin enabled, then puts the latency bypass back. */
    struct ScopedLatencyBypassLift
    {
        explicit ScopedLatencyBypassLift (te::Edit& e) : edit (e), bypassed (e.getLowLatencyDisabledPlugins())
        {
            if (! bypassed.isEmpty())
                edit.setLowLatencyDisabledPlugins ({});
        }

        ~ScopedLatencyBypassLift()
        {
            if (! bypassed.isEmpty())
                edit.setLowLatencyDisabledPlugins (bypassed);
        }

        te::Edit& edit;
        juce::Array<te::EditItemID> bypassed;
    };
}

//==============================================================================
AudioEngine::AudioEngine (Song& s, bool openAudioDevice)
    : song (s), liveAudio (openAudioDevice)
{
    kit::ensureWritten();
    drumkits::machines::ensure();       // the 808 and 909, written once

    // MIDI from the inputs' threads lands here, on the message thread
    midiQueue.reserve (256);
    midiDrain.drain = [this] { drainMidi(); };

    // Only the studio itself writes the settings; every other run works on a private copy
    const bool studio = openAudioDevice && ! keepStudioSettings;
    engine = std::make_unique<te::Engine> (studio ? std::make_unique<StudioStorage> ("Nightshift")
                                                  : std::make_unique<HeadlessStorage> ("Nightshift", openAudioDevice),
                                           std::make_unique<NightUIBehaviour>(),
                                           std::make_unique<NightEngineBehaviour> (openAudioDevice));

    engine->getPluginManager().createBuiltInType<MasterLimiter>();
    engine->getPluginManager().createBuiltInType<MetronomePlugin>();
    engine->getPluginManager().createBuiltInType<SamplePreview>();
    engine->getPluginManager().createBuiltInType<DrumKitPlugin>();
    engine->getPluginManager().createBuiltInType<SfzPlugin>();
    engine->getPluginManager().createBuiltInType<NovaPlugin>();
    engine->getPluginManager().createBuiltInType<GuitarPlugin>();
    engine->getPluginManager().createBuiltInType<MidiOutPlugin>();
    stockfx::registerAll (*engine);

    // Synths plugged in: a command-line run's counts time in samples and catches what it sends
    midiOut = std::make_unique<MidiOutHub> (! openAudioDevice);
    loadOptions();

    // Stretched takes' renders nobody's played in two weeks go (the studio only)
    if (liveAudio)
        stretch::tidyCache();

    if (liveAudio)
    {
        if (options.preferInterface)
            openPreferredInterface();

        // The saved device (the interface) may be unplugged: play through Windows' default
        // output rather than nothing. The interface is picked again when it's back. ASIO is
        // skipped here: its drivers stay listed when their hardware is gone.
        auto& dm = getDeviceManager();

        for (auto* type : dm.getAvailableDeviceTypes())
        {
            if (dm.getCurrentAudioDevice() != nullptr)
                break;

            type->scanForDevices();

            if (type->getTypeName() != "ASIO" && ! type->getDeviceNames().isEmpty())
                dm.setCurrentAudioDeviceType (type->getTypeName(), true);
        }

        // Any device change above rebuilds the engine's outputs later, and their arrival
        // stops a transport that's already playing; bring them in now
        dm.dispatchPendingMessages();
        engine->getDeviceManager().dispatchPendingUpdates();

        dm.addAudioCallback (&deviceStarts);
    }
    else
    {
        // Headless: a virtual device gives the tracks somewhere to render to, and two
        // inputs so recording can be tested without hardware
        te::HostedAudioDeviceInterface::Parameters params;
        params.sampleRate = 48000.0;
        params.blockSize = 512;
        params.inputChannels = 2;
        params.outputChannels = 2;
        params.useMidiDevices = headlessMidiDevices;

        auto& hosted = engine->getDeviceManager().getHostedAudioDeviceInterface();
        hosted.initialise (params);
        hosted.prepareToPlay (params.sampleRate, params.blockSize);
        engine->getDeviceManager().dispatchPendingUpdates();
    }

    // Tracktion's first look for MIDI inputs is put off by its four-second check for new
    // ones (and with no hardware to look at, never comes): look now, so a keyboard plays as
    // soon as the studio's open. A test playing MIDI into the headless engine wants its input
    if (liveAudio || routeHostedMidi)
        engine->getDeviceManager().rescanMidiDeviceList();

    edit = std::make_unique<te::Edit> (*engine, te::Edit::EditRole::forEditing);

    // A new Edit starts with a track of its own; every track here belongs to a channel
    for (auto t : te::getAudioTracks (*edit))
        edit->deleteTrack (t);

    if ((masterLimiter = edit->getPluginCache().createNewPlugin (MasterLimiter::xmlTypeName, {})))
        edit->getMasterPluginList().insertPlugin (masterLimiter, -1, nullptr);

    if ((masterMeter = edit->getPluginCache().createNewPlugin (te::LevelMeterPlugin::create())))
    {
        edit->getMasterPluginList().insertPlugin (masterMeter, -1, nullptr);

        if (auto meter = dynamic_cast<te::LevelMeterPlugin*> (masterMeter.get()))
            meter->measurer.addClient (meterClient);
    }

    // On no track: it joins the output after the master (playbackContextChanged), and a sample heard from the
    // sidebar after it
    metronomePlugin = edit->getPluginCache().createNewPlugin (MetronomePlugin::xmlTypeName, {});
    previewPlugin = edit->getPluginCache().createNewPlugin (SamplePreview::xmlTypeName, {});
    edit->clickTrackEnabled = false;

    setOptions (options);
    edit->getTransport().addListener (this);
    playbackContextChanged();
    startTimerHz (20);
    engine->getDeviceManager().addChangeListener (this);

    // Our own copy of the plugin list, on top of whatever the settings held
    loadPluginList (getKnownPlugins(), pluginListFile());
    getKnownPlugins().addChangeListener (this);
    song.addListener (this);
    automationRecorder = std::make_unique<AutomationRecorder> (song, *this);
    syncNow();
}

AudioEngine::~AudioEngine()
{
    *alive = false;
    stopStretching = true;
    stretchPool.removeAllJobs (true, 10000);
    stopTimer();
    automationRecorder.reset();     // off every parameter it listens to, while they're there
    pluginFinder.reset();

    if (liveAudio)
        getDeviceManager().removeAudioCallback (&deviceStarts);

    song.removeListener (this);
    engine->getDeviceManager().removeChangeListener (this);
    getKnownPlugins().removeChangeListener (this);
    edit->getTransport().removeListener (this);
    clearInputMeters();

    // Off every MIDI input before the devices go, and nothing left to hand over
    midiWatchers.clear();
    midiDrain.drain = nullptr;
    midiDrain.cancelPendingUpdate();

    if (auto meter = dynamic_cast<te::LevelMeterPlugin*> (masterMeter.get()))
        meter->measurer.removeClient (meterClient);

    readMeters ({});

    edit->getTransport().stop (false, true);
    rowClips.clear();       // they belong to the edit
    masterFx.clear();
    slots.clear();
    busSlots.clear();
    masterMeter = nullptr;
    masterLimiter = nullptr;
    metronomePlugin = nullptr;
    previewPlugin = nullptr;
    edit.reset();

    if (! liveAudio)
    {
        engine->getDeviceManager().deviceManager.closeAudioDevice();
        engine->getDeviceManager().removeHostedAudioDeviceInterface();
    }

    engine.reset();
}

//==============================================================================
void AudioEngine::songChanged (Song::Change c)
{
    switch (c)
    {
        case Song::Change::notes:       pending |= needNotes; break;
        case Song::Change::channels:    pending |= needChannels | needMixer | needClips | needInputs; break;
        case Song::Change::mixer:       pending |= needMixer | needInputs; break;
        case Song::Change::patterns:    pending |= needClips | needTempo; break;    // (a pattern's time signature: the metronome's)
        case Song::Change::playlist:    pending |= needClips; break;
        case Song::Change::trackState:  pending |= needMutes; break;
        case Song::Change::transport:   pending |= needTempo | needClips; break;

        case Song::Change::automation:
            pending |= needAutomation;

            // A frozen channel's instrument and effects automation is in its audio: a change there plays it live
            for (auto& channel : song.channels)
                if (channel.freeze.on)
                    pending |= needClips;
            break;

        case Song::Change::selection:   pending |= needMidi; break;     // MIDI keyboards play the selected channel

        case Song::Change::currentPattern:
            if (song.mode != PlayMode::pattern)
                return;

            pending |= needClips | needTempo;       // (and the metronome: the pattern's time signature)
            break;
        case Song::Change::everything:  pending |= needTempo | needChannels | needMixer | needClips | needInputs | needAutomation; break;
    }

    triggerAsyncUpdate();
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    // A scan changed the plugin list: write our own copy straight away. Tracktion keeps it in
    // the settings too, but a scan once never reached the disk that way, and nobody knew
    if (source == &getKnownPlugins())
    {
        if (liveAudio && ! keepStudioSettings && ! savePluginList (getKnownPlugins(), pluginListFile()))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Plugin list not saved",
                                                    "NightShift couldn't write " + pluginListFile().getFullPathName()
                                                        + ", so the plugins just scanned will be forgotten when it closes.");

        return;
    }

    // The device or its inputs changed: inputs are rebuilt, so targets and meters are too (and
    // the MIDI outputs: Tracktion's closed, the hub's opened again for one plugged back in)
    pending |= needInputs;
    triggerAsyncUpdate();

    if (liveAudio)
    {
        closeTracktionMidiOutputs();
        midiOut->refreshPorts();
    }
}

void AudioEngine::syncNow()
{
    cancelPendingUpdate();

    if (pending != 0)
        handleAsyncUpdate();
}

void AudioEngine::handleAsyncUpdate()
{
    const int flags = std::exchange (pending, 0);

    if (flags & needTempo)      syncTempo();
    if (flags & needChannels)   syncChannels();
    if (flags & needMixer)      syncMixer();

    if (flags & (needChannels | needMixer))
        syncEffects();

    if (flags & (needNotes | needClips))
        syncClips ((flags & needClips) == 0);
    else if (flags & needMutes)
        syncMutes();

    if (flags & needInputs)     syncInputs();

    // MIDI inputs onto the channel being played (a new device, a new selection, new tracks)
    if (flags & (needInputs | needChannels | needMidi))
        syncMidiInputs();

    // Last, onto whatever plugins are there now (a chain rebuilt, the mode or tempo changed)
    if (flags & (needAutomation | needTempo | needChannels | needMixer | needClips))
        syncAutomation();

    // Synths plugged in: their notes as the song now plays them (unchanged, nothing's disturbed)
    if (flags & (needTempo | needChannels | needMixer | needNotes | needClips | needMutes | needInputs))
        syncMidiOut();

    // Pitch correction pulls to the song's key, heard from its notes: new notes, or new correctors
    if (flags & (needNotes | needClips | needChannels | needMixer))
        syncSongKey();
}

void AudioEngine::syncSongKey()
{
    const auto key = songkey::find (song);

    auto tell = [&key] (const std::vector<std::pair<int, te::Plugin::Ptr>>& chain)
    {
        for (auto& [id, plugin] : chain)
            if (auto tune = dynamic_cast<NightTune*> (plugin.get()))
                tune->setSongKey (key.root, key.minor);
    };

    for (auto& [id, slot] : slots)
        tell (slot.effects);

    for (auto& [id, slot] : busSlots)
        tell (slot.effects);

    tell (masterFx);
}

void AudioEngine::syncMutes()
{
    // In place, never by rebuilding: a clip muted while it plays sends note-offs for what it
    // holds, where one taken away mid-chord left the chord ringing for ever
    for (auto& [clip, row] : rowClips)
        clip->setMuted (! song.isTrackAudible (row));
}

//==============================================================================
void AudioEngine::syncTempo()
{
    auto& sequence = edit->tempoSequence;

    // Tracktion's beat is the time signature's unit (an eighth in 7/8); the tempo stays in quarter
    // notes either way. It keeps the start's time signature: changes along the song are ours
    // (bars, the metronome), so a tick is the same many of its beats all through
    if (auto sig = sequence.getTimeSig (0); sig != nullptr
         && (sig->numerator.get() != song.meterBeats || sig->denominator.get() != song.meterUnit))
    {
        sig->numerator = song.meterBeats;
        sig->denominator = song.meterUnit;
    }

    // The tempo map: the start's tempo, then Song mode's changes (Pattern mode plays the start's).
    // Each holds until the next (curve 1), or glides to it along its curve (a bezier, 0 a line:
    // Song::tracktionCurve, as Song::secondsAt times it)
    struct Wanted { double beat, bpm; float curve; };
    std::vector<Wanted> wanted { { 0.0, song.bpm, 1.0f } };

    if (song.mode == PlayMode::song)
    {
        for (auto& change : song.tempoChanges)
        {
            wanted.back().curve = Song::tracktionCurve (change);
            wanted.push_back ({ beatsFor (change.tick), change.bpm, 1.0f });
        }
    }

    bool same = sequence.getNumTempos() == (int) wanted.size();

    for (int i = 0; same && i < (int) wanted.size(); ++i)
    {
        auto* t = sequence.getTempo (i);
        same = std::abs (t->getStartBeat().inBeats() - wanted[(size_t) i].beat) < 1.0e-9
                && t->getBpm() == wanted[(size_t) i].bpm && t->getCurve() == wanted[(size_t) i].curve;
    }

    if (! same)
    {
        // Tracktion always has one at the start; the rest go and come back. Nothing is remapped:
        // the clips and automation are put back where the new map says (syncClips, syncAutomation)
        for (int i = sequence.getNumTempos(); --i >= 1;)
            sequence.removeTempo (i, false);

        if (auto* first = sequence.getTempo (0))
            first->set (te::BeatPosition(), wanted[0].bpm, wanted[0].curve, false);

        for (size_t i = 1; i < wanted.size(); ++i)
            sequence.insertTempo (te::BeatPosition::fromBeats (wanted[i].beat), wanted[i].bpm, wanted[i].curve);

        // Now, not on the next message: the clips' times are worked out from it straight away
        sequence.updateTempoData();
        edit->getUndoManager().clearUndoHistory();
    }

    syncMetronome();
}

std::pair<int, int> AudioEngine::getMeterNow() const
{
    if (song.mode == PlayMode::pattern)
        if (auto* p = song.findPattern (song.currentPatternId))
            return song.meterOf (*p);

    const auto section = song.meterAt (std::max (0.0, getPlayheadTicks()));
    return { section.beats, section.unit };
}

void AudioEngine::setMeterNow (int beats, int unit)
{
    if (song.mode == PlayMode::pattern)
    {
        if (auto* p = song.findPattern (song.currentPatternId); p != nullptr && p->meterBeats > 0)
        {
            const bool start = beats == song.meterBeats && unit == song.meterUnit;
            p->meterBeats = start ? 0 : beats;
            p->meterUnit = start ? 0 : unit;
            song.notify (Song::Change::patterns);
            song.notify (Song::Change::transport);
            return;
        }

        song.setMeterAt (0, beats, unit);
    }
    else
    {
        const int index = song.meterChangeAt (std::max (0.0, getPlayheadTicks()));
        song.setMeterAt (index < 0 ? 0 : song.meterChanges[(size_t) index].tick, beats, unit);
    }

    song.notify (Song::Change::patterns);
    song.notify (Song::Change::transport);
}

double AudioEngine::getTempoNow() const
{
    return song.mode == PlayMode::song ? song.bpmAt (std::max (0.0, getPlayheadTicks())) : song.bpm;
}

void AudioEngine::setTempoNow (double bpm)
{
    bpm = juce::jlimit (20.0, 400.0, bpm);
    const int index = song.mode == PlayMode::song ? song.tempoChangeAt (std::max (0.0, getPlayheadTicks())) : -1;

    if (index < 0)
        song.bpm = bpm;
    else
        song.tempoChanges[(size_t) index].bpm = bpm;

    song.notify (Song::Change::transport);
}

//==============================================================================
metronome::Signature AudioEngine::getClickSignature() const
{
    const auto [beats, unit] = getMeterNow();
    return getClickSignature (beats, unit);
}

metronome::Signature AudioEngine::getClickSignature (int beats, int unit) const
{
    const auto meter = juce::String (beats) + "/" + juce::String (unit);
    const auto it = options.clickGroupings.find (meter);
    return metronome::find (beats, unit, it != options.clickGroupings.end() ? it->second : juce::String());
}

std::vector<int> AudioEngine::getClickAccents() const
{
    const auto [beats, unit] = getMeterNow();
    return getClickAccents (beats, unit);
}

std::vector<int> AudioEngine::getClickAccents (int beats, int unit) const
{
    const auto signature = getClickSignature (beats, unit);

    if (auto it = options.clickAccents.find (signature.name()); it != options.clickAccents.end() && (int) it->second.size() == signature.beats)
        return it->second;

    return metronome::defaultAccents (signature);
}

void AudioEngine::setClickGrouping (const metronome::Signature& signature)
{
    auto o = options;
    o.clickGroupings[signature.meter()] = signature.grouping();
    setOptions (o);
}

void AudioEngine::setClickAccents (const std::vector<int>& accents)
{
    auto o = options;
    const auto signature = getClickSignature();

    if (accents == metronome::defaultAccents (signature))
        o.clickAccents.erase (signature.name());
    else
        o.clickAccents[signature.name()] = accents;

    setOptions (o);
}

void AudioEngine::syncMetronome()
{
    auto* m = getMetronome();

    if (m == nullptr)
        return;

    MetronomePlugin::Settings s;
    s.on = options.metronome;
    s.recordingOnly = options.metronomeRecordingOnly;
    s.sound = options.clickSound;
    s.rhythm = options.clickRhythm;
    s.volume = options.clickVolume;
    s.pan = options.clickPan;

    auto accentsInto = [this] (std::array<int, metronome::maxBeats>& into, int beats, int unit)
    {
        const auto accents = getClickAccents (beats, unit);

        for (size_t i = 0; i < into.size(); ++i)
            into[i] = i < accents.size() ? accents[i] : (int) metronome::normal;
    };

    // Practising: the tempo and time signature where the song stands
    const auto [beats, unit] = getMeterNow();
    s.bpm = song.mode == PlayMode::song ? song.bpmAt (std::max (0.0, getPlayheadTicks())) : song.bpm;
    s.beats = juce::jlimit (1, metronome::maxBeats, beats);
    s.unit = unit;
    accentsInto (s.accents, beats, unit);

    // Playing: the song's time signatures where they change, or the pattern's
    s.baseUnit = song.meterUnit;

    if (song.mode == PlayMode::song)
    {
        for (auto& section : song.meterSections())
        {
            if (s.numSections == MetronomePlugin::Settings::maxSections)
                break;

            auto& into = s.sections[(size_t) s.numSections++];
            into.start = section.start;
            into.beats = juce::jlimit (1, metronome::maxBeats, section.beats);
            into.unit = section.unit;
            accentsInto (into.accents, section.beats, section.unit);
        }
    }
    else
    {
        auto& into = s.sections[0];
        s.numSections = 1;
        into.start = 0;
        into.beats = s.beats;
        into.unit = s.unit;
        into.accents = s.accents;
    }

    m->setSettings (s);
    m->setRecording (isRecording() || (noteTake.has_value() && ! noteTake->waiting));
}

void AudioEngine::setPractising (bool shouldPractise)
{
    if (auto* m = getMetronome())
    {
        // Practising is the metronome on its own: the song stops for it
        if (shouldPractise && isPlaying())
            stop();

        syncMetronome();
        edit->getTransport().ensureContextAllocated();
        m->setPractising (shouldPractise);
    }
}

bool AudioEngine::isPractising() const
{
    auto* m = getMetronome();
    return m != nullptr && m->isPractising();
}

bool AudioEngine::previewSample (const juce::File& file)
{
    auto* preview = dynamic_cast<SamplePreview*> (previewPlugin.get());

    if (preview == nullptr)
        return false;

    // Heard whether or not the song plays: the output's graph runs for it as it does for practising
    edit->getTransport().ensureContextAllocated();
    return preview->play (file);
}

void AudioEngine::stopPreview()
{
    if (auto* preview = dynamic_cast<SamplePreview*> (previewPlugin.get()))
        preview->stop();
}

juce::File AudioEngine::getPreviewing() const
{
    auto* preview = dynamic_cast<SamplePreview*> (previewPlugin.get());
    return preview != nullptr ? preview->getPlaying() : juce::File();
}

void AudioEngine::playbackContextChanged()
{
    // The metronome joins each output after the master chain: the master's effects, limiter
    // and meter never hear it, and renders (built without devices) never contain it. A new
    // context starts without it, so it's put back each time.
    auto context = edit != nullptr ? edit->getTransport().getCurrentPlaybackContext() : nullptr;

    if (context == nullptr || metronomePlugin == nullptr)
        return;

    // After it, the synths plugged in take their timing from what's heard (see MidiOutHub)
    context->setInsertOptionalLastStageNodeForDeviceCallback (
        [plugin = metronomePlugin, preview = previewPlugin, &dm = engine->getDeviceManager(), hub = midiOut.get()]
        (te::OutputDevice& device, const te::CreateNodeParams& params, std::unique_ptr<te::graph::Node> node)
        {
            if (device.isMidi() || dm.getDefaultWaveOutDeviceID() != device.getDeviceID())
                return node;

            // The metronome, then a sample heard from the sidebar (on the same output, after the master), and the
            // synths plugged in last, timed from all that's heard
            node = te::createGeneratorPluginNode (plugin, params, std::move (node));

            if (preview != nullptr)
                node = te::createGeneratorPluginNode (preview, params, std::move (node));

            return hub != nullptr ? hub->createNode (std::move (node), params.processState) : std::move (node);
        });

    context->reallocate();
}

double AudioEngine::beatsFor (double ticks) const
{
    return ticks / ticksPerBeat * song.meterUnit / 4.0;
}

juce::String AudioEngine::sourceKey (const Channel& c)
{
    switch (c.type)
    {
        case InstrumentType::synth:   return c.synthPreset;
        case InstrumentType::plugin:  return c.pluginId;
        case InstrumentType::audio:   return {};
        case InstrumentType::kit:     return c.kit;
        case InstrumentType::instrument: return c.instrument;
        case InstrumentType::midiOut: return "midiOut";     // where it goes is the hub's, not the plugin's
        case InstrumentType::sampler: break;
    }

    return c.sample;
}

juce::File AudioEngine::pluginListFile()
{
    return paths::appData().getChildFile ("Plugins.xml");
}

bool AudioEngine::savePluginList (const juce::KnownPluginList& list, const juce::File& file)
{
    auto xml = list.createXml();
    return xml != nullptr && file.getParentDirectory().createDirectory() && xml->writeTo (file);
}

juce::String AudioEngine::developerName (const juce::String& manufacturer)
{
    // Without a company's legal ending: "Valhalla DSP, LLC" is Valhalla DSP, "Apple Inc." Apple. Only
    // as a word of its own at the end (after a space or a comma), and never down to nothing
    static const juce::StringArray endings { "LLC", "L.L.C.", "Inc.", "Inc", "Ltd.", "Ltd", "Limited", "Pty", "GmbH", "AG", "KG", "UG",
                                             "AB", "Oy", "ApS", "AS", "S.L.", "S.A.", "SAS", "S.r.l.", "SRL", "B.V.", "BV", "LLP", "PLC",
                                             "Corp.", "Corp", "Corporation" };
    auto name = manufacturer.trim();

    for (bool shortened = true; shortened;)
    {
        shortened = false;

        for (auto& ending : endings)
        {
            if (! name.endsWithIgnoreCase (ending) || name.length() <= ending.length())
                continue;

            const auto before = name.dropLastCharacters (ending.length());
            const auto separated = before.endsWithChar (' ') || before.endsWithChar (',');
            const auto rest = before.trimEnd().trimCharactersAtEnd (",").trimEnd();

            if (separated && rest.isNotEmpty())
            {
                name = rest;
                shortened = true;
                break;
            }
        }
    }

    return name;
}

std::vector<AudioEngine::Developer> AudioEngine::pluginsByDeveloper (const juce::KnownPluginList& list, bool instruments)
{
    std::vector<Developer> developers;
    std::vector<juce::StringArray> spellings;       // each developer's name as each of its plugins gives it

    for (auto& desc : list.getTypes())
    {
        if (desc.isInstrument != instruments)
            continue;

        const auto name = developerName (desc.manufacturerName);
        auto developer = std::find_if (developers.begin(), developers.end(), [&name] (const Developer& d) { return d.name.equalsIgnoreCase (name); });

        if (developer == developers.end())
        {
            developers.push_back (Developer { name, {}, {} });
            spellings.emplace_back();
            developer = developers.end() - 1;
        }

        spellings[(size_t) (developer - developers.begin())].add (name);
        developer->given.addIfNotAlreadyThere (desc.manufacturerName.trim());

        // Its twin (the same plugin as an Audio Unit, on a Mac): one of them, the VST3
        auto& plugins = developer->plugins;
        const auto twin = std::find_if (plugins.begin(), plugins.end(), [&desc] (const juce::PluginDescription& p) { return p.name == desc.name; });

        if (twin == plugins.end())
            plugins.push_back (desc);
        else if (desc.pluginFormatName == "VST3" && twin->pluginFormatName != "VST3")
            *twin = desc;
    }

    // Spelled more than one way, a developer goes by the spelling most of its plugins give, then one
    // that isn't all capitals, then the first A to Z: the same whatever order the list is in
    for (size_t i = 0; i < developers.size(); ++i)
    {
        const auto& said = spellings[i];
        auto votes = [&said] (const juce::String& s) { return (int) std::count (said.begin(), said.end(), s); };
        auto shouted = [] (const juce::String& s) { return s == s.toUpperCase() && s != s.toLowerCase(); };
        auto& best = developers[i].name;

        for (auto& s : said)
            if (votes (s) != votes (best) ? votes (s) > votes (best)
                                          : shouted (s) != shouted (best) ? ! shouted (s) : s.compare (best) < 0)
                best = s;
    }

    // A to Z, case aside and numbers as numbers (Pro-Q 2 before Pro-Q 10)
    auto before = [] (const juce::String& a, const juce::String& b) { return a.compareNatural (b) < 0; };

    for (auto& d : developers)
        std::stable_sort (d.plugins.begin(), d.plugins.end(), [&before] (const juce::PluginDescription& a, const juce::PluginDescription& b)
        {
            return before (a.name, b.name);
        });

    std::stable_sort (developers.begin(), developers.end(), [&before] (const Developer& a, const Developer& b)
    {
        if (a.name.isEmpty() != b.name.isEmpty())
            return b.name.isEmpty();

        return before (a.name, b.name);
    });

    return developers;
}

bool AudioEngine::loadPluginList (juce::KnownPluginList& list, const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
        return false;

    juce::KnownPluginList saved;
    saved.recreateFromXml (*xml);

    for (auto& type : saved.getTypes())
        list.addType (type);

    return true;
}

void AudioEngine::findNewPlugins (std::function<void (const juce::StringArray&)> done)
{
    struct Finder : public juce::Thread
    {
        explicit Finder (AudioEngine& e)
            : juce::Thread ("New plugins"), engine (e), alive (e.alive)   { startThread(); }

        ~Finder() override      { stopThread (20000); }

        void report (const juce::StringArray& added, int scanned)
        {
            // Back on the message thread, unless the engine's gone by then
            juce::MessageManager::callAsync ([weak = std::weak_ptr<bool> (alive), e = &engine, added, scanned]
            {
                if (auto stillAlive = weak.lock(); stillAlive && *stillAlive)
                    e->pluginsFound (added, scanned);
            });
        }

        void run() override
        {
            // VST3s everywhere; on a Mac, Audio Units too (most Mac instruments come as both)
            auto& list = engine.getKnownPlugins();
            juce::StringArray added;
            int scanned = 0;

            // The plugins the list already had: found again (a new build, moved, listed by another
            // of its files), a plugin isn't new, and saying it was put the same note up every start
            std::set<juce::String> known;

            for (auto& type : list.getTypes())
                known.insert (pluginKey (type));

            for (auto* format : engine.getPluginFormats().getFormats())
            {
                const auto kind = format->getName();

                if (kind != "VST3" && kind != "AudioUnit")
                    continue;

                // The usual places, and for VST3 the per-user one installers without admin rights
                // use (Audio Units the system lists, wherever they're installed)
                auto places = format->getDefaultLocationsToSearch();

                if (kind == "VST3")
                    places.addIfNotAlreadyThere (paths::userVst3());

                if (pluginFolderOverride != juce::File())
                {
                    if (kind != "VST3")
                        continue;

                    places = juce::FileSearchPath (pluginFolderOverride.getFullPathName());
                }

                for (auto& file : format->searchPathsForPlugins (places, true, true))
                {
                    if (threadShouldExit())
                        return;

                    // Audio Units: only real effects and instruments. The system also lists
                    // Apple's mixers, panners, generators and MIDI processors, no use as inserts
                    if (kind == "AudioUnit"
                         && ! (file.startsWith ("AudioUnit:Effects/") || file.startsWith ("AudioUnit:Synths/")))
                        continue;

                    if (isListedAndCurrent (list, file, *format) || list.getBlacklistedFiles().contains (file))
                        continue;

                    juce::OwnedArray<juce::PluginDescription> found;
                    list.scanAndAddFile (file, true, found, *format);
                    ++scanned;

                    for (auto* d : found)
                        if (known.count (pluginKey (*d)) == 0)
                            added.addIfNotAlreadyThere (d->name);
                }
            }

            // A plugin scanned again (a new build of one listed) takes its old entry's place without
            // JUCE telling anyone, so the list file never had its new date and every start scanned it
            // again: tell the list's listeners (the studio writes Plugins.xml, Tracktion its copy)
            if (scanned > 0)
                list.sendChangeMessage();

            report (added, scanned);
        }

        AudioEngine& engine;
        std::shared_ptr<bool> alive;
    };

    // One scan at a time: asked during one, this waits for it (it finishes when its names
    // reach the message thread, not when its thread stops, so nobody misses them)
    if (done != nullptr)
        pluginFinderWaiting.push_back (std::move (done));

    if (findingPlugins)
        return;

    findingPlugins = true;
    pluginFinder = std::make_unique<Finder> (*this);
}

bool AudioEngine::isListedAndCurrent (const juce::KnownPluginList& list, const juce::String& file, juce::AudioPluginFormat& format)
{
    const auto inside = file + juce::File::getSeparatorString();
    bool listed = false;

    for (auto& type : list.getTypes())
    {
        if (! type.fileOrIdentifier.equalsIgnoreCase (file) && ! type.fileOrIdentifier.startsWithIgnoreCase (inside))
            continue;

        if (format.pluginNeedsRescanning (type))
            return false;

        listed = true;
    }

    return listed;
}

juce::String AudioEngine::pluginKey (const juce::PluginDescription& d)
{
    return d.pluginFormatName + "|" + d.name + "|" + juce::String::toHexString (d.uniqueId);
}

void AudioEngine::pluginsFound (const juce::StringArray& added, int scanned)
{
    findingPlugins = false;
    pluginsScannedLastTime = scanned;
    auto waiting = std::move (pluginFinderWaiting);
    pluginFinderWaiting.clear();

    for (auto& callback : waiting)
        callback (added);
}

juce::File AudioEngine::resolveSample (const juce::String& sample)
{
    if (sample.startsWith ("kit:"))
        return kit::fileFor (sample.fromFirstOccurrenceOf ("kit:", false, false));

    return juce::File::isAbsolutePath (sample) ? juce::File (sample) : juce::File();
}

void AudioEngine::syncChannels()
{
    for (auto it = slots.begin(); it != slots.end();)
    {
        if (song.findChannel (it->first) == nullptr)
        {
            // Off its meter first: the measurer goes with the track
            dropMeter (it->first);
            edit->deleteTrack (it->second.track.get());
            it = slots.erase (it);
        }
        else
        {
            ++it;
        }
    }

    for (auto& channel : song.channels)
    {
        auto& slot = slots[channel.id];

        if (slot.track == nullptr)
            slot.track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, te::getAudioTracks (*edit).getLast()), nullptr);

        if (slot.track == nullptr)
            continue;

        if (slot.track->getName() != channel.name)
            slot.track->setName (channel.name);

        const bool wantsInstrument = channel.type != InstrumentType::audio;

        if (slot.type != channel.type || slot.source != sourceKey (channel) || (wantsInstrument && slot.instrument == nullptr))
        {
            buildInstrument (slot, channel);
        }
        else if (auto sampler = dynamic_cast<te::SamplerPlugin*> (slot.instrument.get());
                 sampler != nullptr && sampler->getNumSounds() > 0
                  && (slot.rootKey != channel.rootKey || slot.oneShot != channel.oneShot))
        {
            sampler->setSoundParams (0, channel.rootKey, 0, 127);
            sampler->setSoundOpenEnded (0, channel.oneShot);
            slot.rootKey = channel.rootKey;
            slot.oneShot = channel.oneShot;
        }
    }

    syncBuses();

    // Every instrument is heard as soon as it's there, the song stopped: a plugin's own window
    // (Nightshift Drums' kit, a synth's keyboard) plays through Tracktion's playback, which
    // otherwise only Play, a key, a recording or an audio track's input would have started
    if (! song.channels.empty() && ! edit->getTransport().isRecording())
        edit->getTransport().ensureContextAllocated();
}

void AudioEngine::syncBuses()
{
    for (auto it = busSlots.begin(); it != busSlots.end();)
    {
        if (song.findBus (it->first) != nullptr)
        {
            ++it;
            continue;
        }

        // Whatever still plays into it goes back to the master first
        for (auto& [id, slot] : slots)
            if (slot.track != nullptr && slot.track->getOutput().outputsToDestTrack (*it->second.track))
                slot.track->getOutput().setOutputToDefaultDevice (false);

        dropMeter (it->first);
        it->second.effects.clear();
        it->second.auxReturn = nullptr;
        edit->deleteTrack (it->second.track.get());
        it = busSlots.erase (it);
    }

    for (auto& bus : song.buses)
    {
        auto& slot = busSlots[bus.id];

        if (slot.track == nullptr)
        {
            slot.track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, te::getAudioTracks (*edit).getLast()), nullptr);

            if (slot.track == nullptr)
                continue;

            // The lowest aux bus number no other bus has
            std::set<int> taken;

            for (auto& [id, other] : busSlots)
                taken.insert (other.auxBus);

            for (slot.auxBus = 0; taken.count (slot.auxBus) > 0;)
                ++slot.auxBus;

            if ((slot.auxReturn = edit->getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {})))
            {
                if (auto aux = dynamic_cast<te::AuxReturnPlugin*> (slot.auxReturn.get()))
                    aux->busNumber = slot.auxBus;

                slot.track->pluginList.insertPlugin (slot.auxReturn, 0, nullptr);
            }
        }

        if (slot.track->getName() != bus.name)
            slot.track->setName (bus.name);

        edit->setAuxBusName (slot.auxBus, bus.name);
    }
}

juce::String AudioEngine::sendKey (const Send& s, int auxBus)
{
    return juce::String (s.busId) + (s.preFader ? "pre" : "post") + juce::String (auxBus);
}

void AudioEngine::syncSends (Slot& slot, const Channel& channel)
{
    // The sends to buses that are there, as they should be built
    std::vector<std::pair<juce::String, const Send*>> wanted;

    for (auto& s : channel.sends)
        if (auto bus = busSlots.find (s.busId); bus != busSlots.end() && bus->second.auxBus >= 0)
            wanted.emplace_back (sendKey (s, bus->second.auxBus), &s);

    bool same = wanted.size() == slot.sends.size();

    for (size_t i = 0; same && i < wanted.size(); ++i)
        same = wanted[i].first == slot.sends[i].first;

    if (! same)
    {
        for (auto& [key, plugin] : slot.sends)
            if (plugin != nullptr)
                plugin->deleteFromParent();

        slot.sends.clear();

        for (auto& [key, send] : wanted)
        {
            auto plugin = edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::create());

            if (auto aux = dynamic_cast<te::AuxSendPlugin*> (plugin.get()))
            {
                aux->busNumber = busSlots[send->busId].auxBus;

                // Before the fader, or after it (and the meter) at the end
                const int at = send->preFader ? slot.track->pluginList.indexOf (slot.track->getVolumePlugin()) : -1;
                slot.track->pluginList.insertPlugin (plugin, at, nullptr);
            }

            slot.sends.emplace_back (key, plugin);
        }
    }

    // Levels in place
    for (size_t i = 0; i < wanted.size(); ++i)
        if (auto aux = dynamic_cast<te::AuxSendPlugin*> (slot.sends[i].second.get()))
            if (std::abs (aux->getGainDb() - channelVolumeToDb (wanted[i].second->level)) > 0.01f)
                aux->setGainDb (channelVolumeToDb (wanted[i].second->level));
}

void AudioEngine::buildInstrument (Slot& slot, const Channel& channel)
{
    if (slot.instrument != nullptr)
        slot.instrument->deleteFromParent();

    slot.instrument = nullptr;
    te::Plugin::Ptr plugin;
    auto& cache = edit->getPluginCache();

    switch (channel.type)
    {
        case InstrumentType::sampler:
        {
            plugin = cache.createNewPlugin (te::SamplerPlugin::xmlTypeName, {});

            if (auto sampler = dynamic_cast<te::SamplerPlugin*> (plugin.get()))
            {
                auto file = resolveSample (channel.sample);

                if (file.existsAsFile())
                {
                    sampler->addSound (file.getFullPathName(), channel.name, 0.0, 0.0, 0.0f);
                    sampler->setSoundParams (0, channel.rootKey, 0, 127);
                    sampler->setSoundOpenEnded (0, channel.oneShot);
                }
            }
            break;
        }

        case InstrumentType::synth:
        {
            if (channel.pluginState.isNotEmpty())
                if (auto state = juce::ValueTree::fromXml (channel.pluginState); state.isValid())
                    plugin = cache.createNewPlugin (state);

            // Nova playing a factory sound (or, for a name it doesn't know, the init sound); a
            // song saved with the old synth keeps its state and so its sound
            if (plugin == nullptr)
                if ((plugin = cache.createNewPlugin (NovaPlugin::xmlTypeName, {})))
                    if (auto nova = dynamic_cast<NovaPlugin*> (plugin.get()))
                        nova->applyPatch (nova::findFactory (channel.synthPreset) != nullptr ? *nova::findFactory (channel.synthPreset)
                                                                                              : nova::Patch());
            break;
        }

        case InstrumentType::plugin:
        {
            if (channel.pluginState.isNotEmpty())
                if (auto state = juce::ValueTree::fromXml (channel.pluginState); state.isValid())
                    plugin = cache.createNewPlugin (state);

            if (plugin == nullptr)
                if (auto desc = getKnownPlugins().getTypeForIdentifierString (channel.pluginId))
                    plugin = cache.createNewPlugin (te::ExternalPlugin::xmlTypeName, *desc);
            break;
        }

        case InstrumentType::kit:
        {
            if ((plugin = cache.createNewPlugin (DrumKitPlugin::xmlTypeName, {})))
                if (auto kitPlugin = dynamic_cast<DrumKitPlugin*> (plugin.get()))
                    kitPlugin->setKit (channel.kit);
            break;
        }

        case InstrumentType::instrument:
        {
            // Nightshift Guitar, by its sound's name; else a sampled instrument
            if (guitarsound::isGuitarPath (channel.instrument))
            {
                if ((plugin = cache.createNewPlugin (GuitarPlugin::xmlTypeName, {})))
                    if (auto guitar = dynamic_cast<GuitarPlugin*> (plugin.get()))
                        guitar->setPreset (guitarsound::presetOfPath (channel.instrument));

                break;
            }

            if ((plugin = cache.createNewPlugin (SfzPlugin::xmlTypeName, {})))
                if (auto sfzPlugin = dynamic_cast<SfzPlugin*> (plugin.get()))
                    sfzPlugin->setInstrument (channel.instrument);
            break;
        }

        case InstrumentType::midiOut:
        {
            // Silent: what a keyboard plays on it goes straight out of the MIDI output
            if ((plugin = cache.createNewPlugin (MidiOutPlugin::xmlTypeName, {})))
                if (auto out = dynamic_cast<MidiOutPlugin*> (plugin.get()))
                    out->setRoute (midiOut.get(), channel.id);
            break;
        }

        case InstrumentType::audio:
            break;      // fed by its input, not an instrument
    }

    if (plugin != nullptr)
    {
        slot.track->pluginList.insertPlugin (plugin, 0, nullptr);
        slot.instrument = plugin;
    }

    slot.type = channel.type;
    slot.source = sourceKey (channel);
    slot.rootKey = channel.rootKey;
    slot.oneShot = channel.oneShot;
}

//==============================================================================
void AudioEngine::syncEffects()
{
    // A channel's effects come after its instrument (first in its list) or, on an audio
    // track, straight after the input; the master's go before its limiter so it still
    // catches whatever they do
    for (auto& channel : song.channels)
        if (auto it = slots.find (channel.id); it != slots.end() && it->second.track != nullptr)
            syncChain (it->second.track->pluginList, it->second.effects, channel.effects, it->second.instrument != nullptr ? 1 : 0);

    // A bus's come after its aux return
    for (auto& bus : song.buses)
        if (auto it = busSlots.find (bus.id); it != busSlots.end() && it->second.track != nullptr)
            syncChain (it->second.track->pluginList, it->second.effects, bus.effects, it->second.auxReturn != nullptr ? 1 : 0);

    syncChain (edit->getMasterPluginList(), masterFx, song.masterEffects, 0);
    syncSidechains();

    // (the bypasses just set, with a frozen channel's switched off again)
    applyFreezeToPlugins();
}

void AudioEngine::applyFreezeToPlugins()
{
    // A channel playing its frozen audio has its instrument and effects switched off: NightShift's engine takes
    // switched-off plugins out of playback altogether, so they cost nothing (and add no delay)
    for (auto& channel : song.channels)
    {
        auto it = slots.find (channel.id);

        if (it == slots.end())
            continue;

        const bool frozen = builtFrozen.count (channel.id) > 0;

        if (auto& instrument = it->second.instrument; instrument != nullptr && instrument->isEnabled() == frozen)
            instrument->setEnabled (! frozen);

        for (size_t i = 0; i < it->second.effects.size() && i < channel.effects.size(); ++i)
            if (auto& plugin = it->second.effects[i].second; plugin != nullptr)
                if (const bool on = ! frozen && ! channel.effects[i].bypassed; plugin->isEnabled() != on)
                    plugin->setEnabled (on);
    }
}

void AudioEngine::syncChain (te::PluginList& list, std::vector<std::pair<int, te::Plugin::Ptr>>& built,
                             const std::vector<Effect>& wanted, int firstIndex)
{
    bool same = built.size() == wanted.size();

    for (size_t i = 0; same && i < wanted.size(); ++i)
        same = built[i].first == wanted[i].id;

    if (! same)
    {
        // Added, removed or moved: keep what each plugin has been set to, then lay the chain
        // out again in order
        for (auto& [id, plugin] : built)
        {
            if (plugin == nullptr)
                continue;

            keepEffectState (id, *plugin);
            plugin->deleteFromParent();
        }

        built.clear();
        int index = firstIndex;

        for (auto& e : wanted)
        {
            auto plugin = createEffect (e);

            if (plugin != nullptr)
                list.insertPlugin (plugin, index++, nullptr);

            built.emplace_back (e.id, plugin);
        }
    }

    // Bypass in place
    for (size_t i = 0; i < wanted.size(); ++i)
        if (auto& plugin = built[i].second; plugin != nullptr && plugin->isEnabled() == wanted[i].bypassed)
            plugin->setEnabled (! wanted[i].bypassed);
}

te::Plugin::Ptr AudioEngine::createEffect (const Effect& e)
{
    auto& cache = edit->getPluginCache();

    // From its saved state, under a new id: the old one may still be in the edit
    if (e.state.isNotEmpty())
    {
        if (auto state = juce::ValueTree::fromXml (e.state); state.isValid())
        {
            state.removeProperty (te::IDs::id, nullptr);

            if (auto plugin = cache.createNewPlugin (state))
                return plugin;
        }
    }

    if (auto type = stockfx::typeForIdentifier (e.pluginId); type.isNotEmpty())
        return cache.createNewPlugin (type, {});

    if (auto desc = getKnownPlugins().getTypeForIdentifierString (e.pluginId))
        return cache.createNewPlugin (te::ExternalPlugin::xmlTypeName, *desc);

    return {};
}

std::unique_ptr<juce::PluginDescription> AudioEngine::findEffectType (const juce::String& identifier)
{
    for (auto& d : stockfx::descriptions())
        if (d.createIdentifierString() == identifier)
            return std::make_unique<juce::PluginDescription> (d);

    return getKnownPlugins().getTypeForIdentifierString (identifier);
}

void AudioEngine::syncSidechains()
{
    // A compressor listening to another channel: that channel's track wired into its extra
    // inputs. Only channels can be sources (a bus could be fed by the track it's on)
    auto wire = [this] (te::Plugin* plugin, te::Track* owner)
    {
        auto comp = dynamic_cast<NightCompressor*> (plugin);

        if (comp == nullptr)
            return;

        auto source = song.findChannel (comp->sidechain.get()) != nullptr ? getTrack (comp->sidechain.get()) : nullptr;
        const auto wanted = source != nullptr && source != owner ? source->itemID : te::EditItemID();

        if (comp->getSidechainSourceID() != wanted)
        {
            comp->setSidechainSourceID (wanted);

            if (wanted.isValid() && comp->getNumWires() == 0)
                comp->guessSidechainRouting();

            edit->restartPlayback();
        }

        comp->sidechainConnected = wanted.isValid();
    };

    for (auto& [id, slot] : slots)
        for (auto& [effectId, plugin] : slot.effects)
            wire (plugin.get(), slot.track.get());

    for (auto& [id, slot] : busSlots)
        for (auto& [effectId, plugin] : slot.effects)
            wire (plugin.get(), slot.track.get());

    for (auto& [effectId, plugin] : masterFx)
        wire (plugin.get(), nullptr);
}

void AudioEngine::keepEffectState (int effectId, te::Plugin& plugin)
{
    if (auto effect = song.findEffect (effectId))
    {
        plugin.flushPluginStateToValueTree();

        // Switched on or off as its bypass says, never as a freeze left it (that's the freeze's to say)
        auto state = plugin.state.createCopy();
        state.setProperty (te::IDs::enabled, ! effect->bypassed, nullptr);
        effect->state = state.toXmlString();
    }
}

te::Plugin* AudioEngine::getEffect (int effectId) const
{
    for (auto& [channelId, slot] : slots)
        for (auto& [id, plugin] : slot.effects)
            if (id == effectId)
                return plugin.get();

    for (auto& [busId, slot] : busSlots)
        for (auto& [id, plugin] : slot.effects)
            if (id == effectId)
                return plugin.get();

    for (auto& [id, plugin] : masterFx)
        if (id == effectId)
            return plugin.get();

    return nullptr;
}

void AudioEngine::syncMixer()
{
    for (auto& channel : song.channels)
    {
        auto it = slots.find (channel.id);

        if (it == slots.end() || it->second.track == nullptr)
            continue;

        auto& track = *it->second.track;

        if (auto vp = track.getVolumePlugin())
        {
            vp->setVolumeDb (channelVolumeToDb (channel.volume));
            vp->setPan (channel.pan);
        }

        if (track.isMuted (false) != channel.muted)
            track.setMute (channel.muted);

        // Into a bus's track, or out to the master
        auto bus = busSlots.find (channel.output);
        auto destination = bus != busSlots.end() ? bus->second.track.get() : nullptr;

        if (track.getOutput().getDestinationTrack() != destination)
        {
            if (destination != nullptr)
                track.getOutput().setOutputToTrack (destination);
            else
                track.getOutput().setOutputToDefaultDevice (false);
        }

        syncSends (it->second, channel);
    }

    for (auto& bus : song.buses)
    {
        auto it = busSlots.find (bus.id);

        if (it == busSlots.end() || it->second.track == nullptr)
            continue;

        auto& track = *it->second.track;

        if (auto vp = track.getVolumePlugin())
        {
            vp->setVolumeDb (channelVolumeToDb (bus.volume));
            vp->setPan (bus.pan);
        }

        if (track.isMuted (false) != bus.muted)
            track.setMute (bus.muted);
    }

    // The master volume goes in front of the limiter (MasterLimiter), so turning it up drives
    // the limiter rather than going over its ceiling; Tracktion's own master fader, after every
    // master plugin, stays at 0 dB
    if (auto master = edit->getMasterVolumePlugin(); master != nullptr && master->getVolumeDb() != 0.0f)
        master->setVolumeDb (0.0f);

    if (auto limiter = dynamic_cast<MasterLimiter*> (masterLimiter.get()))
    {
        limiter->setLevels (masterVolumeToDb (song.masterVolume), song.mastering.pushDb, song.mastering.ceilingDb);
        limiter->setMasterVolume (song.masterVolume);
        limiter->setShape (song.mastering);
    }
}

//==============================================================================
te::AutomatableParameter* AudioEngine::findAutomationParameter (int owner, const juce::String& param) const
{
    auto inPlugin = [] (te::Plugin* plugin, const juce::String& id) -> te::AutomatableParameter*
    {
        return plugin != nullptr ? plugin->getAutomatableParameterByID (id).get() : nullptr;
    };

    // "fx:<effect id>:<parameter id>" in a chain of effects by id
    auto inChain = [&] (const std::vector<std::pair<int, te::Plugin::Ptr>>& chain) -> te::AutomatableParameter*
    {
        const auto rest = param.fromFirstOccurrenceOf ("fx:", false, false);
        const int effectId = rest.upToFirstOccurrenceOf (":", false, false).getIntValue();

        for (auto& [id, plugin] : chain)
            if (id == effectId)
                return inPlugin (plugin.get(), rest.fromFirstOccurrenceOf (":", false, false));

        return nullptr;
    };

    if (owner == 0)
    {
        if (param == "volume")
            if (auto limiter = dynamic_cast<MasterLimiter*> (masterLimiter.get()))
                return limiter->getVolumeParameter();

        return param.startsWith ("fx:") ? inChain (masterFx) : nullptr;
    }

    // A bus: its track's volume and pan, and its effects
    if (auto bus = busSlots.find (owner); bus != busSlots.end())
    {
        if (bus->second.track == nullptr)
            return nullptr;

        if (param == "volume" || param == "pan")
            if (auto vp = bus->second.track->getVolumePlugin())
                return param == "volume" ? vp->volParam.get() : vp->panParam.get();

        return param.startsWith ("fx:") ? inChain (bus->second.effects) : nullptr;
    }

    auto it = slots.find (owner);

    if (it == slots.end() || it->second.track == nullptr)
        return nullptr;

    const auto& slot = it->second;

    if (param == "volume" || param == "pan")
        if (auto vp = slot.track->getVolumePlugin())
            return param == "volume" ? vp->volParam.get() : vp->panParam.get();

    if (param.startsWith ("send:"))
    {
        // The send's plugin is keyed by its bus first (sendKey)
        const int busId = param.fromFirstOccurrenceOf ("send:", false, false).getIntValue();

        for (auto& [key, plugin] : slot.sends)
            if (key.getIntValue() == busId)
                if (auto aux = dynamic_cast<te::AuxSendPlugin*> (plugin.get()))
                    return aux->gain.get();

        return nullptr;
    }

    if (param.startsWith ("instrument:"))
        return inPlugin (slot.instrument.get(), param.fromFirstOccurrenceOf ("instrument:", false, false));

    return param.startsWith ("fx:") ? inChain (slot.effects) : nullptr;
}

std::vector<AudioEngine::AutomationTarget> AudioEngine::automationTargets (int owner) const
{
    std::vector<AutomationTarget> targets;

    auto addPlugin = [&targets] (te::Plugin* plugin, const juce::String& prefix, const juce::String& group)
    {
        if (plugin != nullptr)
            for (auto* p : plugin->getAutomatableParameters())
                targets.push_back ({ prefix + p->paramID, group, p->getParameterName() });
    };

    auto effectName = [this] (int id, te::Plugin* plugin)
    {
        if (auto e = const_cast<Song&> (song).findEffect (id); e != nullptr && e->name.isNotEmpty())
            return e->name;

        return plugin != nullptr ? plugin->getName() : juce::String ("Effect");
    };

    targets.push_back ({ "volume", {}, "Volume" });

    if (owner == 0)
    {
        for (auto& [id, plugin] : masterFx)
            addPlugin (plugin.get(), "fx:" + juce::String (id) + ":", effectName (id, plugin.get()));

        return targets;
    }

    // A bus: volume, pan and its effects (it sends nowhere but on, to the master)
    if (auto bus = busSlots.find (owner); bus != busSlots.end())
    {
        targets.push_back ({ "pan", {}, "Pan" });

        for (auto& [id, plugin] : bus->second.effects)
            addPlugin (plugin.get(), "fx:" + juce::String (id) + ":", effectName (id, plugin.get()));

        return targets;
    }

    auto it = slots.find (owner);

    if (it == slots.end() || it->second.track == nullptr)
        return targets;

    targets.push_back ({ "pan", {}, "Pan" });

    if (auto channel = song.findChannel (owner))
        for (auto& s : channel->sends)
            if (auto bus = song.findBus (s.busId))
                targets.push_back ({ "send:" + juce::String (s.busId), "Sends", "Send to " + bus->name });

    if (auto instrument = it->second.instrument.get())
        addPlugin (instrument, "instrument:", instrument->getName());

    for (auto& [id, plugin] : it->second.effects)
        addPlugin (plugin.get(), "fx:" + juce::String (id) + ":", effectName (id, plugin.get()));

    return targets;
}

std::optional<std::pair<int, juce::String>> AudioEngine::automationKeyFor (const te::AutomatableParameter& param) const
{
    auto* p = const_cast<te::AutomatableParameter*> (&param);
    auto holds = [p] (te::Plugin* plugin) { return plugin != nullptr && plugin->getAutomatableParameters().contains (p); };

    if (auto limiter = dynamic_cast<MasterLimiter*> (masterLimiter.get()); limiter != nullptr && limiter->getVolumeParameter() == p)
        return std::make_pair (0, juce::String ("volume"));

    for (auto& [id, plugin] : masterFx)
        if (holds (plugin.get()))
            return std::make_pair (0, "fx:" + juce::String (id) + ":" + param.paramID);

    for (auto& [owner, slot] : busSlots)
    {
        if (slot.track != nullptr)
            if (auto vp = slot.track->getVolumePlugin())
            {
                if (vp->volParam.get() == p)  return std::make_pair (owner, juce::String ("volume"));
                if (vp->panParam.get() == p)  return std::make_pair (owner, juce::String ("pan"));
            }

        for (auto& [id, plugin] : slot.effects)
            if (holds (plugin.get()))
                return std::make_pair (owner, "fx:" + juce::String (id) + ":" + param.paramID);
    }

    for (auto& [owner, slot] : slots)
    {
        if (slot.track != nullptr)
            if (auto vp = slot.track->getVolumePlugin())
            {
                if (vp->volParam.get() == p)  return std::make_pair (owner, juce::String ("volume"));
                if (vp->panParam.get() == p)  return std::make_pair (owner, juce::String ("pan"));
            }

        for (auto& [key, plugin] : slot.sends)
            if (auto aux = dynamic_cast<te::AuxSendPlugin*> (plugin.get()); aux != nullptr && aux->gain.get() == p)
                return std::make_pair (owner, "send:" + juce::String (key.getIntValue()));

        if (holds (slot.instrument.get()))
            return std::make_pair (owner, "instrument:" + param.paramID);

        for (auto& [id, plugin] : slot.effects)
            if (holds (plugin.get()))
                return std::make_pair (owner, "fx:" + juce::String (id) + ":" + param.paramID);
    }

    return std::nullopt;
}

juce::String AudioEngine::automationName (int owner, const juce::String& param) const
{
    for (auto& t : automationTargets (owner))
        if (t.param == param)
            return t.group.isEmpty() || t.group == "Sends" ? t.name : t.group + ": " + t.name;

    if (auto lane = song.findLane (owner, param); lane != nullptr && lane->name.isNotEmpty())
        return lane->name;

    return param;
}

std::vector<std::pair<double, float>> AudioEngine::curvePoints (const AutomationLane& lane)
{
    std::vector<std::pair<double, float>> out;
    out.reserve (lane.points.size());

    for (size_t i = 0; i < lane.points.size(); ++i)
    {
        const auto& a = lane.points[i];
        out.emplace_back ((double) a.tick, a.value);

        if (i + 1 == lane.points.size() || a.curve == 0.0f)
            continue;

        // A bent line: short straight ones along it, one every two ticks up to 48 (within a
        // thousandth of the way at the sharpest bend)
        const auto& b = lane.points[i + 1];
        const int span = b.tick - a.tick;

        if (span <= 0 || b.value == a.value)
            continue;

        const int steps = juce::jlimit (4, 48, span / 2);

        for (int k = 1; k < steps; ++k)
        {
            const float along = (float) k / (float) steps;
            out.emplace_back (a.tick + span * (double) along, a.value + (b.value - a.value) * automationcurve::shape (along, a.curve));
        }
    }

    return out;
}

void AudioEngine::syncAutomation()
{
    // Song mode plays the lanes. Pattern mode loops one pattern, where the song's automation
    // means nothing, so there every parameter stays where its control is
    std::set<te::AutomatableParameter*> driven;

    if (song.mode == PlayMode::song)
    {
        for (auto& lane : song.automation)
        {
            auto param = lane.points.empty() ? nullptr : findAutomationParameter (lane.owner, lane.param);

            if (param == nullptr)
                continue;

            driven.insert (param);

            // A lane being recorded is the hand's until it's let go: the parameter follows the hand
            // meanwhile, and its curve is written once, when it's let go
            if (automationRecorder != nullptr && automationRecorder->isWritingLane (lane.owner, lane.param))
                continue;

            // Written again only when something changed (or the plugin is new, its curve empty).
            // A bent line goes in as short straight ones: Tracktion's own bends play only a quarter
            // of the way to the corner before turning into a hold and a jump, and its message thread
            // works them out twice as bent as its player, so a stopped song would sit elsewhere
            auto& written = writtenAutomation[juce::String (lane.owner) + "/" + lane.param];
            auto& curve = param->getCurve();
            const auto wanted = curvePoints (lane);

            if (written.param == param && written.points == lane.points && written.bpm == song.bpm
                 && written.tempoChanges == song.tempoChanges && written.unit == song.meterUnit
                 && curve.getNumPoints() == (int) wanted.size())
                continue;

            const auto range = param->getValueRange();
            curve.clear (nullptr);

            for (auto& [tick, value] : wanted)
                curve.addPoint (te::TimePosition::fromSeconds (ticksToSeconds (tick)),
                                juce::jlimit (range.getStart(), range.getEnd(), value), 0.0f, nullptr);

            written = { param, lane.points, song.bpm, song.tempoChanges, song.meterUnit };
        }
    }

    // Curves no lane asks for any more (a lane emptied or taken away, or Pattern mode): off,
    // and the control's own value back
    bool restored = false;

    auto sweep = [&] (te::Plugin* plugin)
    {
        if (plugin == nullptr)
            return;

        for (auto* p : plugin->getAutomatableParameters())
            if (p->hasAutomationPoints() && driven.count (p) == 0)
            {
                p->getCurve().clear (nullptr);
                p->updateFromAttachedValue();
                restored = true;
            }
    };

    for (auto& [id, slot] : slots)
    {
        if (slot.track != nullptr)
            sweep (slot.track->getVolumePlugin());

        sweep (slot.instrument.get());

        for (auto& [effectId, plugin] : slot.effects)
            sweep (plugin.get());

        for (auto& [key, plugin] : slot.sends)
            sweep (plugin.get());
    }

    for (auto& [id, slot] : busSlots)
    {
        if (slot.track != nullptr)
            sweep (slot.track->getVolumePlugin());

        for (auto& [effectId, plugin] : slot.effects)
            sweep (plugin.get());
    }

    for (auto& [id, plugin] : masterFx)
        sweep (plugin.get());

    sweep (masterLimiter.get());
    std::erase_if (writtenAutomation, [&driven] (auto& entry) { return driven.count (entry.second.param) == 0; });

    if (restored)
        syncMixer();
}

//==============================================================================
namespace
{
    /** A Tracktion fader position (a volume's or a send's parameter) as the song's 0..1, where
        0.78 is 0 dB: channelVolumeToDb backwards. */
    float levelFromFaderPosition (float position)
    {
        const float db = te::volumeFaderPositionToDB (position);
        return db <= -99.0f ? 0.0f : juce::jlimit (0.0f, 1.0f, 0.78f * std::pow (10.0f, db / 40.0f));
    }
}

void AudioEngine::setAutomationRecording (bool on)
{
    if (automationRecorder != nullptr)
        automationRecorder->setArmed (on);
}

bool AudioEngine::isAutomationRecording() const
{
    return automationRecorder != nullptr && automationRecorder->isArmed();
}

bool AudioEngine::isWritingAutomation() const
{
    return automationRecorder != nullptr && automationRecorder->isWriting();
}

bool AudioEngine::isWritingLane (int owner, const juce::String& param) const
{
    return automationRecorder != nullptr && automationRecorder->isWritingLane (owner, param);
}

void AudioEngine::touchControl (int owner, const juce::String& param, bool down, bool held)
{
    if (automationRecorder == nullptr || ! automationRecorder->isArmed())
        return;

    // The song's own controls: their faders made the undo step as they were grabbed. Their moves
    // reach the parameter a moment later (syncMixer), so the last one goes in before letting go
    if (auto p = findAutomationParameter (owner, param))
    {
        if (! held)
            automationRecorder->nudge (*p, false);
        else if (down)
            automationRecorder->touch (*p, true, false);
        else
        {
            syncNow();
            automationRecorder->touch (*p, false, false);
        }
    }
}

void AudioEngine::touchParameter (te::AutomatableParameter& p, bool down, bool held)
{
    if (automationRecorder == nullptr || ! automationRecorder->isArmed())
        return;

    if (! held)
        automationRecorder->nudge (p);
    else
        automationRecorder->touch (p, down);
}

double AudioEngine::getHeardTicks() const
{
    // Where the audio thread is now, less the output's delay before it's heard
    auto& transport = edit->getTransport();
    double seconds = transport.getPosition().inSeconds();

    if (auto context = transport.getCurrentPlaybackContext())
        seconds = context->getPosition().inSeconds();

    if (const auto info = getLatencyInfo(); info.sampleRate > 0.0)
        seconds -= info.outputSamples / info.sampleRate;

    return secondsToTicks (seconds);
}

std::vector<te::AutomatableParameter*> AudioEngine::allAutomationParameters() const
{
    std::vector<te::AutomatableParameter*> all;

    auto add = [&all] (te::Plugin* plugin)
    {
        if (plugin != nullptr)
            for (auto* p : plugin->getAutomatableParameters())
                all.push_back (p);
    };

    for (auto& [id, slot] : slots)
    {
        if (slot.track != nullptr)
            add (slot.track->getVolumePlugin());

        add (slot.instrument.get());

        for (auto& [effectId, plugin] : slot.effects)
            add (plugin.get());

        for (auto& [key, plugin] : slot.sends)
            add (plugin.get());
    }

    for (auto& [id, slot] : busSlots)
    {
        if (slot.track != nullptr)
            add (slot.track->getVolumePlugin());

        for (auto& [effectId, plugin] : slot.effects)
            add (plugin.get());
    }

    for (auto& [id, plugin] : masterFx)
        add (plugin.get());

    add (masterLimiter.get());
    return all;
}

void AudioEngine::takeParameterValue (int owner, const juce::String& param, float value)
{
    auto p = findAutomationParameter (owner, param);

    if (p != nullptr)
        p->setParameter (value, juce::dontSendNotification);

    // The song's own controls keep their values in the song (syncMixer puts them back on)
    bool mixer = true;

    if (owner == 0 && param == "volume")
        song.masterVolume = juce::jlimit (0.0f, 1.0f, value);
    else if (auto c = song.findChannel (owner); c != nullptr && param == "volume")
        c->volume = levelFromFaderPosition (value);
    else if (c != nullptr && param == "pan")
        c->pan = juce::jlimit (-1.0f, 1.0f, value);
    else if (auto s = c != nullptr && param.startsWith ("send:") ? Song::findSend (*c, param.fromFirstOccurrenceOf ("send:", false, false).getIntValue()) : nullptr)
        s->level = levelFromFaderPosition (value);
    else if (auto b = song.findBus (owner); b != nullptr && param == "volume")
        b->volume = levelFromFaderPosition (value);
    else if (b != nullptr && param == "pan")
        b->pan = juce::jlimit (-1.0f, 1.0f, value);
    else
        mixer = false;

    if (mixer)
        song.notify (Song::Change::mixer);
}

std::optional<float> AudioEngine::getAutomatedControl (int owner, const juce::String& param) const
{
    if (song.mode != PlayMode::song || ! isPlaying())
        return std::nullopt;

    const auto* lane = song.findLane (owner, param);
    auto p = lane != nullptr && ! lane->points.empty() ? findAutomationParameter (owner, param) : nullptr;

    if (p == nullptr)
        return std::nullopt;

    // The master's volume and every pan are in the song's units already; volumes and sends are
    // Tracktion fader positions
    const float value = p->getCurrentValue();
    return owner == 0 || param == "pan" ? value : levelFromFaderPosition (value);
}

//==============================================================================
/** On one MIDI input, on its own thread: what NightShift uses (notes, controllers, the pedal,
    pitch bend) goes into the queue for the message thread, and the light's time is noted. The
    sound itself takes Tracktion's own path through the engine. */
struct AudioEngine::MidiWatcher : public te::PhysicalMidiInputDevice::Listener
{
    MidiWatcher (AudioEngine& e, std::shared_ptr<te::MidiInputDevice> d)
        : owner (e), device (std::move (d)), physical (dynamic_cast<te::PhysicalMidiInputDevice*> (device.get())),
          name (device->getName())
    {
        physical->addListener (this);
    }

    ~MidiWatcher() override
    {
        physical->removeListener (this);
    }

    void handleIncomingMidiMessage (const juce::MidiMessage& m) override
    {
        if (! (m.isNoteOnOrOff() || m.isController() || m.isPitchWheel()))
            return;

        owner.lastMidiActivity = juce::Time::getMillisecondCounter();

        {
            const juce::SpinLock::ScopedLockType sl (owner.midiLock);
            owner.midiQueue.push_back ({ m, juce::Time::getMillisecondCounterHiRes(), name });
        }

        owner.midiDrain.triggerAsyncUpdate();
    }

    AudioEngine& owner;
    std::shared_ptr<te::MidiInputDevice> device;
    te::PhysicalMidiInputDevice* physical;
    juce::String name;
};

juce::StringArray AudioEngine::getMidiInputNames() const
{
    juce::StringArray names;

    for (auto& d : engine->getDeviceManager().getMidiInDevices())
        if (dynamic_cast<te::PhysicalMidiInputDevice*> (d.get()) != nullptr)
            names.add (d->getName());

    return names;
}

void AudioEngine::setMidiInputEnabled (const juce::String& name, bool on)
{
    auto o = options;
    o.disabledMidiInputs.removeString (name);

    if (! on)
        o.disabledMidiInputs.add (name);

    setOptions (o);
    syncMidiInputs();
}

int AudioEngine::getMidiTargetChannel() const
{
    auto playsNotes = [this] (int id)
    {
        auto c = song.findChannel (id);
        return c != nullptr && c->type != InstrumentType::audio;
    };

    if (playsNotes (song.selectedChannelId))
        return song.selectedChannelId;

    return playsNotes (lastMidiChannel) ? lastMidiChannel : 0;
}

void AudioEngine::syncMidiInputs()
{
    std::vector<std::shared_ptr<te::MidiInputDevice>> devices;

    for (auto& d : engine->getDeviceManager().getMidiInDevices())
        if (dynamic_cast<te::PhysicalMidiInputDevice*> (d.get()) != nullptr)
            devices.push_back (d);

    // A watcher on each input that's there, none on one that's gone; each on unless it's been
    // switched off. One plugged in while running is announced
    std::erase_if (midiWatchers, [&devices] (auto& w)
    {
        return std::none_of (devices.begin(), devices.end(), [&w] (auto& d) { return d == w->device; });
    });

    juce::StringArray names;

    for (auto& d : devices)
    {
        const auto name = d->getName();
        names.add (name);

        if (d->isEnabled() != isMidiInputEnabled (name))
            d->setEnabled (isMidiInputEnabled (name));

        if (std::none_of (midiWatchers.begin(), midiWatchers.end(), [&d] (auto& w) { return w->device == d; }))
            midiWatchers.push_back (std::make_unique<MidiWatcher> (*this, d));

        if (midiInputsListed && ! knownMidiInputs.contains (name) && isMidiInputEnabled (name) && onMidiInputAdded != nullptr)
            onMidiInputAdded (name);
    }

    knownMidiInputs = names;
    midiInputsListed = true;

    // Their notes go to the channel being played, through its instrument (Tracktion's input
    // monitoring, always on); recording them is ours, into patterns, so its own is off. Not
    // while keys or the pedal still hold notes on the one before (their releases would miss)
    if (! midiNoteChannels.empty() || midiPedal || ! midiPedalHeld.empty())
    {
        midiRetargetPending = true;
        return;
    }

    midiRetargetPending = false;
    const int channel = getMidiTargetChannel();

    if (channel != 0)
        lastMidiChannel = channel;

    auto slot = slots.find (channel);
    te::AudioTrack* track = slot != slots.end() ? slot->second.track.get() : nullptr;

    const bool hostedToo = routeHostedMidi && ! liveAudio;

    if ((devices.empty() && ! hostedToo) || edit->getTransport().isRecording())
        return;

    edit->getTransport().ensureContextAllocated();

    for (auto* instance : edit->getAllInputDevices())
    {
        auto& device = instance->getInputDevice();
        auto* midiDevice = dynamic_cast<te::MidiInputDevice*> (&device);
        const bool hosted = hostedToo && midiDevice != nullptr && te::HostedAudioDeviceInterface::isHostedMidiInputDevice (*midiDevice);

        if (device.getDeviceType() != te::InputDevice::physicalMidiDevice && ! hosted)
            continue;

        for (auto id : instance->getTargets())
            if (track == nullptr || id != track->itemID)
                (void) instance->removeTarget (id, nullptr);

        if (track != nullptr)
        {
            if (! instance->getTargets().contains (track->itemID))
                (void) instance->setTarget (track->itemID, false, nullptr, 0);

            if (instance->isRecordingEnabled (track->itemID))
                instance->setRecordingEnabled (track->itemID, false);
        }

        if (device.getMonitorMode() != te::InputDevice::MonitorMode::on)
            device.setMonitorMode (te::InputDevice::MonitorMode::on);
    }

    midiRoutedChannel = track != nullptr ? channel : 0;

    if (midiRoutedChannel != 0)
        lastMidiChannel = midiRoutedChannel;
}

void AudioEngine::drainMidi()
{
    std::vector<IncomingMidi> batch;
    batch.reserve (64);

    {
        const juce::SpinLock::ScopedLockType sl (midiLock);
        batch.swap (midiQueue);
    }

    const double now = juce::Time::getMillisecondCounterHiRes();

    for (auto& in : batch)
        handleMidiInput (in.device, in.message, std::max (0.0, (now - in.arrivedMs) * 0.001));
}

void AudioEngine::handleMidiInput (const juce::String& device, const juce::MidiMessage& m, double secondsAgo)
{
    lastMidiActivity = juce::Time::getMillisecondCounter();
    const int key = m.isNoteOnOrOff() ? m.getNoteNumber() : -1;

    // Controllers (bar the pedal): learning, or moving what they're linked to
    if (m.isController() && ! m.isSustainPedalOn() && ! m.isSustainPedalOff())
    {
        if (midiLearn.has_value())
        {
            ControllerLink link { device, m.getChannel(), m.getControllerNumber(), midiLearn->owner, midiLearn->param };

            // One control per knob, one knob per control
            std::erase_if (song.controllerLinks, [&link] (auto& l)
            {
                return (l.device == link.device && l.midiChannel == link.midiChannel && l.controller == link.controller)
                    || (l.owner == link.owner && l.param == link.param);
            });

            song.controllerLinks.push_back (link);
            auto onLinked = std::move (midiLearn->onLinked);
            midiLearn.reset();

            // The link is the song's (saved with it), and the knob's value goes in at once
            if (! applyControllerLinks (device, m))
                song.notify (Song::Change::mixer);

            if (onLinked != nullptr)
                onLinked ("CC " + juce::String (link.controller) + " on " + device);

            return;
        }

        applyControllerLinks (device, m);
        return;
    }

    // Notes into a recording (the engine has played them already). A note goes on with the
    // channel MIDI plays now, and ends on the channel it went on
    if (m.isNoteOn())
    {
        const int channel = midiRoutedChannel != 0 ? midiRoutedChannel : getMidiTargetChannel();

        if (channel == 0)
            return;

        midiNoteChannels[key] = channel;
        midiPedalHeld.erase (key);      // struck again while the pedal held it: held by the key now
        recordNoteOn (channel, key, m.getVelocity(), secondsAgo);
    }
    else if (m.isNoteOff())
    {
        auto it = midiNoteChannels.find (key);

        if (it == midiNoteChannels.end())
            return;

        const int channel = it->second;
        midiNoteChannels.erase (it);

        // Let go with the pedal down: it sounds on, and records on, until the pedal comes up
        if (midiPedal)
            midiPedalHeld[key] = channel;
        else
            recordNoteOff (channel, key, secondsAgo);
    }
    else if (m.isSustainPedalOn())
    {
        midiPedal = true;
    }
    else if (m.isSustainPedalOff())
    {
        midiPedal = false;

        for (auto [heldKey, channel] : std::exchange (midiPedalHeld, {}))
            recordNoteOff (channel, heldKey, secondsAgo);
    }

    // The selection moved while notes were held: follow it now they're all let go
    if (midiRetargetPending && midiNoteChannels.empty() && ! midiPedal && midiPedalHeld.empty())
        syncMidiInputs();
}

void AudioEngine::learnMidiController (int owner, const juce::String& param, std::function<void (const juce::String&)> onLinked)
{
    midiLearn = MidiLearn { owner, param, std::move (onLinked) };
}

bool AudioEngine::applyControllerLinks (const juce::String& device, const juce::MidiMessage& m)
{
    bool any = false, mixer = false;
    const float norm = (float) m.getControllerValue() / 127.0f;

    for (auto& link : song.controllerLinks)
    {
        if (link.controller != m.getControllerNumber() || link.midiChannel != m.getChannel() || (link.device.isNotEmpty() && link.device != device))
            continue;

        any = true;

        // Recording automation: the knob writes its control's lane until it's left alone a moment
        if (automationRecorder != nullptr && automationRecorder->isWriting())
            if (auto p = findAutomationParameter (link.owner, link.param))
                automationRecorder->nudge (*p);

        // The song's own controls through the song (their faders follow, it's saved); anything
        // else straight to its parameter
        if (link.param == "volume" || link.param == "pan" || link.param.startsWith ("send:"))
        {
            if (link.owner == 0)
            {
                if (link.param == "volume")
                    song.masterVolume = norm;
            }
            else if (auto c = song.findChannel (link.owner))
            {
                if (link.param == "volume")
                    c->volume = norm;
                else if (link.param == "pan")
                    c->pan = norm * 2.0f - 1.0f;
                else if (auto s = Song::findSend (*c, link.param.fromFirstOccurrenceOf ("send:", false, false).getIntValue()))
                    s->level = norm;
            }
            else if (auto b = song.findBus (link.owner))
            {
                if (link.param == "volume")
                    b->volume = norm;
                else if (link.param == "pan")
                    b->pan = norm * 2.0f - 1.0f;
            }

            mixer = true;
        }
        else if (auto p = findAutomationParameter (link.owner, link.param))
        {
            p->setNormalisedParameter (norm, juce::sendNotification);
        }
    }

    if (mixer)
        song.notify (Song::Change::mixer);

    return any;
}

//==============================================================================
juce::StringArray AudioEngine::getMidiOutputNames() const
{
    return midiOut->getOutputNames();
}

MidiOutHub::OutputSettings AudioEngine::getMidiOutSettings (const juce::String& output) const
{
    auto found = options.midiOuts.find (output);
    return found != options.midiOuts.end() ? found->second : MidiOutHub::OutputSettings();
}

void AudioEngine::setMidiOutSettings (const juce::String& output, MidiOutHub::OutputSettings s)
{
    s.timingMs = juce::jlimit (-MidiOutHub::maxTimingMs, MidiOutHub::maxTimingMs, s.timingMs);
    auto o = options;

    // Kept only while it differs from on time, without the clock
    if (s == MidiOutHub::OutputSettings())
        o.midiOuts.erase (output);
    else
        o.midiOuts[output] = s;

    setOptions (o);
}

void AudioEngine::sendTestNote (int channelId)
{
    auto c = song.findChannel (channelId);

    if (c == nullptr || c->type != InstrumentType::midiOut || c->midiOutDevice.isEmpty())
        return;

    syncNow();
    const int ch = juce::jlimit (1, 16, c->midiOutChannel);
    const int key = juce::jlimit (0, 127, c->rootKey);

    if (c->midiProgram >= 0)
        midiOut->sendNow (c->midiOutDevice, juce::MidiMessage::programChange (ch, c->midiProgram));

    midiOut->sendNow (c->midiOutDevice, juce::MidiMessage::noteOn (ch, key, (juce::uint8) 100));

    juce::Timer::callAfterDelay (400, [weakAlive = std::weak_ptr<bool> (alive), hub = midiOut.get(), device = c->midiOutDevice, ch, key]
    {
        if (auto stillAlive = weakAlive.lock(); stillAlive && *stillAlive)
            hub->sendNow (device, juce::MidiMessage::noteOff (ch, key));
    });
}

void AudioEngine::sendMidiProgram (int channelId)
{
    if (auto c = song.findChannel (channelId); c != nullptr && c->type == InstrumentType::midiOut
                                                && c->midiOutDevice.isNotEmpty() && c->midiProgram >= 0)
        midiOut->sendNow (c->midiOutDevice, juce::MidiMessage::programChange (juce::jlimit (1, 16, c->midiOutChannel), c->midiProgram));
}

void AudioEngine::closeTracktionMidiOutputs()
{
    // NightShift never plays a track out of Tracktion's MIDI outputs; the hub opens what it needs
    auto& dm = engine->getDeviceManager();

    for (int i = 0; i < dm.getNumMidiOutDevices(); ++i)
        if (auto* d = dm.getMidiOutDevice (i); d != nullptr && d->isEnabled())
            d->setEnabled (false);
}

void AudioEngine::syncMidiOut()
{
    MidiOutHub::Plan plan;
    plan.baseUnit = song.meterUnit;
    std::map<juce::String, int> outputIndex;

    auto outputFor = [&] (const juce::String& name)
    {
        if (auto found = outputIndex.find (name); found != outputIndex.end())
            return found->second;

        MidiOutHub::Output out;
        out.name = name;
        out.settings = getMidiOutSettings (name);
        plan.outputs.push_back (std::move (out));
        return outputIndex[name] = (int) plan.outputs.size() - 1;
    };

    // Each channel playing a synth plugged in: where it goes, and the sound it asks for
    std::map<int, std::pair<int, int>> heard;     // channel -> output, MIDI channel

    for (auto& c : song.channels)
    {
        if (c.type != InstrumentType::midiOut || c.midiOutDevice.isEmpty())
            continue;

        const int out = outputFor (c.midiOutDevice);
        const int ch = juce::jlimit (1, 16, c.midiOutChannel);
        plan.routes.push_back ({ c.id, out, ch, c.muted });

        if (! c.muted)
            heard[c.id] = { out, ch };

        if (c.midiProgram >= 0)
            plan.outputs[(size_t) out].programs.push_back (juce::MidiMessage::programChange (ch, juce::jlimit (0, 127, c.midiProgram)));
    }

    // Outputs following the tempo, whether or not a channel plays them
    for (auto& [name, s] : options.midiOuts)
        if (s.clock)
            outputFor (name);

    // Their notes as the song plays them (the current pattern round and round in Pattern mode),
    // in the engine's seconds: where Tracktion's tempo map puts them
    if (! heard.empty())
    {
        const int patternId = song.mode == PlayMode::pattern ? song.currentPattern().id : 0;

        for (auto& n : midifile::notesAsPlayed (song, patternId, true))
        {
            const auto route = heard.find (n.channelId);

            if (route == heard.end())
                continue;

            auto& events = plan.outputs[(size_t) route->second.first].events;
            const auto ch = (juce::uint8) (route->second.second - 1);
            const auto key = (juce::uint8) juce::jlimit (0, 127, n.key);
            events.push_back ({ ticksToSeconds (n.start), { (juce::uint8) (0x90 | ch), key, (juce::uint8) juce::jlimit (1, 127, n.velocity) }, 3 });
            events.push_back ({ ticksToSeconds (n.end), { (juce::uint8) (0x80 | ch), key, 0 }, 3 });
        }

        // In time order; at one moment a note ends before another starts
        for (auto& out : plan.outputs)
            std::stable_sort (out.events.begin(), out.events.end(), [] (const MidiOutHub::Event& a, const MidiOutHub::Event& b)
            {
                if (a.seconds != b.seconds)
                    return a.seconds < b.seconds;

                return (a.bytes[0] & 0xf0) == 0x80 && (b.bytes[0] & 0xf0) == 0x90;
            });
    }

    if (liveAudio)
        midiOut->setOutputLatency (getLatencyInfo().outputSamples);

    midiOut->setPlan (std::move (plan));
}

//==============================================================================
void AudioEngine::removeAllClips()
{
    rowClips.clear();

    for (auto& [id, slot] : slots)
    {
        slot.patternClip = nullptr;

        if (slot.track != nullptr)
        {
            auto clips = slot.track->getClips();

            for (int i = clips.size(); --i >= 0;)
                clips.getUnchecked (i)->removeFromParent();
        }
    }
}

void AudioEngine::fillClip (te::MidiClip& clip, const std::vector<Note>* notes, int offset, int length)
{
    auto& sequence = clip.getSequence();
    sequence.clear (nullptr);

    if (notes == nullptr)
        return;

    for (auto& n : *notes)
    {
        // A split or trimmed clip plays the notes that start in its part of the pattern (FL's way)
        if (n.start < offset || (length >= 0 && n.start >= offset + length))
            continue;

        // Swing pushes every second 16th later, like FL's channel rack swing
        double start = n.start;

        if (n.start % (2 * ticksPerStep) == ticksPerStep)
            start += song.swing * 0.5 * ticksPerStep;

        sequence.addNote (n.key,
                          te::BeatPosition::fromBeats (beatsFor (start - offset)),
                          te::BeatDuration::fromBeats (beatsFor (n.length)),
                          n.velocity, 0, nullptr);
    }
}

std::map<int, juce::String> AudioEngine::clipsUnderPlayhead() const
{
    std::map<int, juce::String> found;
    const auto now = edit->getTransport().getPosition();

    for (auto& [id, slot] : slots)
        if (slot.track != nullptr && slot.type != InstrumentType::audio)
            for (auto clip : slot.track->getClips())
                if (clip->getEditTimeRange().contains (now))
                    found[id] << clip->getName() << "@" << juce::String (clip->getPosition().getStart().inSeconds(), 6) << ";";

    return found;
}

void AudioEngine::releaseNotesLeftBy (const std::map<int, juce::String>& before)
{
    // A MIDI clip taken away mid-note never sends that note's note-off, so the instrument
    // would hold it for ever. Instruments whose clip under the playhead is still the same
    // get their note-offs from it as usual
    const auto after = clipsUnderPlayhead();

    for (auto& [id, clips] : before)
    {
        if (auto same = after.find (id); same != after.end() && same->second == clips)
            continue;

        if (auto slot = slots.find (id); slot != slots.end() && slot->second.track != nullptr)
        {
            auto& track = *slot->second.track;
            track.injectLiveMidiMessage (juce::MidiMessage::allNotesOff (1), 0);

            for (int key = 0; key < 128; ++key)
                track.injectLiveMidiMessage (juce::MidiMessage::noteOff (1, key), 0);
        }
    }
}

void AudioEngine::syncClips (bool notesOnly)
{
    auto& transport = edit->getTransport();
    const auto heldBefore = transport.isPlaying() ? clipsUnderPlayhead() : std::map<int, juce::String>();
    const auto clipRange = [this] (int startTicks, int lengthTicks)
    {
        return te::TimeRange (te::TimePosition::fromSeconds (ticksToSeconds (startTicks)),
                              te::TimePosition::fromSeconds (ticksToSeconds (startTicks + lengthTicks)));
    };

    // Frozen channels whose audio still matches the song play it, their plugins switched off; the rest play live
    std::set<int> frozenNow;

    for (auto& channel : song.channels)
        if (channel.freeze.on && channel.type != InstrumentType::audio && channel.type != InstrumentType::midiOut
             && freeze::isCurrent (song, channel))
            frozenNow.insert (channel.id);

    // A frozen file as a clip on its channel's track, from `start` (seconds) for `length`
    auto frozenClip = [this] (Slot& slot, const juce::String& path, double start, double length) -> te::Clip::Ptr
    {
        const juce::File file (path);
        te::ClipPosition position { { te::TimePosition::fromSeconds (start), te::TimeDuration::fromSeconds (length) }, {} };
        auto clip = slot.track->insertWaveClip (file.getFileNameWithoutExtension(), file, position, false);

        if (clip != nullptr)
        {
            clip->setAutoTempo (false);
            clip->setAutoPitch (false);
        }

        return clip;
    };

    if (song.mode == PlayMode::pattern)
    {
        auto& pattern = song.currentPattern();
        const int length = song.patternLength (pattern);

        // Editing notes while a pattern loops: refill the clips in place, no rebuild
        if (notesOnly && builtMode == PlayMode::pattern && builtPatternId == pattern.id && builtLength == length && frozenNow == builtFrozen)
        {
            for (auto& channel : song.channels)
                if (auto it = slots.find (channel.id); it != slots.end() && it->second.patternClip != nullptr)
                    fillClip (*it->second.patternClip, pattern.findNotes (channel.id));

            return;
        }

        removeAllClips();

        // (a synth plugged in plays its notes from the MIDI out's plan, not from a clip)
        for (auto& channel : song.channels)
        {
            auto& slot = slots[channel.id];

            if (slot.track == nullptr || channel.type == InstrumentType::audio || channel.type == InstrumentType::midiOut)
                continue;

            // Frozen: the pattern's loop, rendered with what rings over from the end of it folded onto its start
            if (frozenNow.count (channel.id) > 0)
            {
                if (auto file = channel.freeze.patterns.find (pattern.id); file != channel.freeze.patterns.end() && freeze::hasNotes (pattern, channel.id))
                    frozenClip (slot, file->second, 0.0, ticksToSeconds (length));

                continue;
            }

            if ((slot.patternClip = slot.track->insertMIDIClip (pattern.name, clipRange (0, length), nullptr)))
                fillClip (*slot.patternClip, pattern.findNotes (channel.id));
        }

        builtPatternId = pattern.id;
        builtLength = length;
        loopEndTicks = length;
    }
    else
    {
        removeAllClips();
        stretchDeferred = false;

        for (auto& placed : song.playlist)
        {
            auto pattern = song.findPattern (placed.patternId);

            if (pattern == nullptr)
                continue;

            const int length = song.clipLength (placed);

            for (auto& channel : song.channels)
            {
                auto notes = pattern->findNotes (channel.id);
                auto& slot = slots[channel.id];

                if (notes != nullptr && ! notes->empty() && slot.track != nullptr && channel.type != InstrumentType::midiOut
                     && frozenNow.count (channel.id) == 0)
                {
                    if (auto clip = slot.track->insertMIDIClip (pattern->name, clipRange (placed.start, length), nullptr))
                    {
                        fillClip (*clip, notes, placed.offset, length);
                        clip->setMuted (! song.isTrackAudible (placed.track));
                        rowClips.emplace_back (clip, placed.track);
                    }
                }
            }
        }

        // Frozen channels: the song as rendered, a file for each Arrange row their notes are on, from the start,
        // muted and soloed with the row as its pattern clips would be
        for (auto& channel : song.channels)
        {
            auto it = slots.find (channel.id);

            if (frozenNow.count (channel.id) == 0 || it == slots.end() || it->second.track == nullptr)
                continue;

            for (auto& [row, path] : channel.freeze.rows)
            {
                const double length = te::AudioFile (*engine, juce::File (path)).getLength();

                if (length <= 0.0)
                    continue;

                if (auto clip = frozenClip (it->second, path, 0.0, length))
                {
                    clip->setMuted (! song.isTrackAudible (row));
                    rowClips.emplace_back (clip, row);
                }
            }
        }

        for (auto& folder : song.audioClips)
        {
            auto it = slots.find (folder.channelId);

            if (it == slots.end() || it->second.track == nullptr)
                continue;

            // One clip per comp segment. Where two segments meet, each runs on by half a
            // crossfade and they fade across each other (equal power: takes aren't
            // correlated); free edges get a short fade so nothing starts with a click.
            // The folder's times are its audio's: stretched, they land in the song through its
            // speed, or (following the tempo) its beats on the song's, and each take plays as
            // rendered so (over what the comp uses of it)
            const bool stretched = song.isStretched (folder);
            auto songSeconds = [this, &folder] (double t) { return ticksToSeconds (song.audioTickAt (folder, t)); };
            std::map<int, std::pair<juce::File, double>> renders;      // a take's render, and the song second it starts at

            if (stretched)
            {
                std::map<int, std::pair<double, double>> used;      // each take's stretch of the folder's time

                for (auto& s : folder.comp)
                    if (juce::isPositiveAndBelow (s.take, (int) folder.takes.size()))
                    {
                        const double from = std::max (0.0, s.from - compCrossfadeSeconds), to = std::min (folder.length, s.to + compCrossfadeSeconds);
                        auto [at, isNew] = used.try_emplace (s.take, from, to);

                        if (! isNew)
                            at->second = { std::min (at->second.first, from), std::max (at->second.second, to) };
                    }

                for (auto& [take, range] : used)
                    renders[take] = stretchedTake (folder, folder.takes[(size_t) take], range.first, range.second);
            }

            for (size_t i = 0; i < folder.comp.size(); ++i)
            {
                const auto& segment = folder.comp[i];

                if (! juce::isPositiveAndBelow (segment.take, (int) folder.takes.size()))
                    continue;

                const auto& take = folder.takes[(size_t) segment.take];
                juce::File file (take.file);

                if (! file.existsAsFile())
                    continue;

                // Stretched, its render (silent while it's still being made)
                if (stretched && ! renders[segment.take].first.existsAsFile())
                    continue;

                const bool joinsBefore = i > 0 && std::abs (folder.comp[i - 1].to - segment.from) < 1.0e-6;
                const bool joinsAfter = i + 1 < folder.comp.size() && std::abs (folder.comp[i + 1].from - segment.to) < 1.0e-6;
                const double from = std::max ({ segment.from - (joinsBefore ? compCrossfadeSeconds * 0.5 : 0.0), take.from, 0.0 });
                const double to = std::min ({ segment.to + (joinsAfter ? compCrossfadeSeconds * 0.5 : 0.0), take.to, folder.length });

                const double start = songSeconds (from), end = songSeconds (to);

                if (to - from < 0.005 || end - start < 0.005)
                    continue;

                te::ClipPosition position { { te::TimePosition::fromSeconds (start), te::TimeDuration::fromSeconds (end - start) },
                                            te::TimeDuration::fromSeconds (take.fileOffset + from) };

                if (stretched)
                {
                    const auto& [rendered, renderStart] = renders[segment.take];
                    file = rendered;
                    position.offset = te::TimeDuration::fromSeconds (std::max (0.0, start - renderStart));
                }

                if (auto clip = it->second.track->insertWaveClip (file.getFileNameWithoutExtension(), file, position, false))
                {
                    clip->setMuted (! song.isTrackAudible (folder.track));
                    rowClips.emplace_back (clip, folder.track);
                    clip->setFadeInType (te::AudioFadeCurve::convex);
                    clip->setFadeOutType (te::AudioFadeCurve::convex);

                    // The folder's own fades (its handles in Arrange) on its first and last parts, in
                    // the song's seconds (the joins' and edges' short fades are the song's already)
                    double fadeIn = joinsBefore ? compCrossfadeSeconds : edgeFadeSeconds;
                    double fadeOut = joinsAfter ? compCrossfadeSeconds : edgeFadeSeconds;

                    if (i == 0 && folder.fadeIn > 0.0)
                        fadeIn = std::max (fadeIn, std::min (songSeconds (folder.fadeIn) - start, end - start));

                    if (i + 1 == folder.comp.size() && folder.fadeOut > 0.0)
                        fadeOut = std::max (fadeOut, std::min (end - songSeconds (folder.length - folder.fadeOut), end - start - fadeIn));

                    clip->setFadeIn (te::TimeDuration::fromSeconds (std::max (edgeFadeSeconds, fadeIn)));
                    clip->setFadeOut (te::TimeDuration::fromSeconds (std::max (edgeFadeSeconds, fadeOut)));
                }
            }
        }

        builtPatternId = -1;
        loopEndTicks = std::max (song.ticksPerBar(), song.songLengthInTicks());
    }

    if (! heldBefore.empty())
        releaseNotesLeftBy (heldBefore);

    if (builtMode != song.mode)
        startTicks = 0.0;

    builtMode = song.mode;

    if (frozenNow != builtFrozen)
    {
        builtFrozen = frozenNow;
        applyFreezeToPlugins();
    }

    syncLooping();
}

std::pair<juce::File, double> AudioEngine::stretchedTake (const AudioClip& folder, const Take& take, double fromSeconds, double toSeconds)
{
    // Where each moment of the song over that stretch reads the take's file, through the folder's time map: as many
    // points as it takes (following the tempo: every 20 ms and at each tempo change; at a speed: its ends)
    const double s0 = ticksToSeconds (song.audioTickAt (folder, fromSeconds)), s1 = ticksToSeconds (song.audioTickAt (folder, toSeconds));
    std::vector<double> at { s0, s1 };

    if (Song::followsTempo (folder))
    {
        for (double s = s0 + 0.02; s < s1; s += 0.02)
            at.push_back (s);

        for (auto& change : song.tempoChanges)
            if (const double s = ticksToSeconds (change.tick); s > s0 && s < s1)
                at.push_back (s);

        std::sort (at.begin(), at.end());
        at.erase (std::unique (at.begin(), at.end(), [] (double a, double b) { return std::abs (a - b) < 1.0e-9; }), at.end());
    }

    stretch::Job job;
    job.source = juce::File (take.file);
    job.semitones = juce::jlimit (-24.0f, 24.0f, folder.pitch);

    for (double s : at)
        job.map.emplace_back (s - s0, take.fileOffset + song.audioSecondsAt (folder, secondsToTicks (s)));

    const auto file = stretch::cachedFile (job);

    if (file.existsAsFile())
    {
        file.setLastModificationTime (juce::Time::getCurrentTime());     // used: kept a while longer
        return { file, s0 };
    }

    // A short one (and every one when it's for a render or an export) at once; a long one in the background
    if (stretchAllNow || job.length() <= stretchNowSeconds)
    {
        stretch::render (job, file);
        return { file, s0 };
    }

    stretchDeferred = true;

    {
        const juce::ScopedLock sl (stretchingLock);

        if (! stretching.insert (file.getFileName()).second)
            return { {}, s0 };
    }

    stretchPool.addJob ([this, job, file, weakAlive = std::weak_ptr<bool> (alive)]
    {
        stretch::render (job, file, stretch::Kind::signalsmith, &stopStretching);

        {
            const juce::ScopedLock sl (stretchingLock);
            stretching.erase (file.getFileName());
        }

        // Done: the folder plays it from the next time the clips are built (now)
        juce::MessageManager::callAsync ([this, weakAlive]
        {
            if (auto stillAlive = weakAlive.lock(); stillAlive && *stillAlive)
            {
                pending |= needClips;
                triggerAsyncUpdate();
            }
        });
    });

    return { {}, s0 };
}

bool AudioEngine::isStretching() const
{
    const juce::ScopedLock sl (stretchingLock);
    return ! stretching.empty();
}

void AudioEngine::syncLooping()
{
    auto& transport = edit->getTransport();

    // Recording sets its own looping: round the playlist loop, or straight on (notes in song
    // mode without a loop as well)
    if (transport.isRecording() || (noteTake.has_value() && noteTake->songMode && noteTake->wrap <= 0.0))
        return;

    auto clipRange = [this] (int startTick, int lengthTicks)
    {
        return te::TimeRange (te::TimePosition::fromSeconds (ticksToSeconds (startTick)),
                              te::TimePosition::fromSeconds (ticksToSeconds (startTick + lengthTicks)));
    };

    // Song mode goes round the playlist loop when there is one, else the whole song
    const bool cycle = song.mode == PlayMode::song && song.hasLoop();
    const int cycleFrom = cycle ? song.loopStart : 0, loopTo = cycle ? song.loopEnd : loopEndTicks;
    transport.setLoopRange (clipRange (cycleFrom, loopTo - cycleFrom));
    transport.looping = true;

    // (a count-in plays in from before the loop)
    if (transport.isPlaying() && ! isCountingIn())
    {
        const double now = getPlayheadTicks();

        if (now >= loopTo || now < cycleFrom - 0.5)
            transport.setPosition (te::TimePosition::fromSeconds (ticksToSeconds (cycleFrom)));
    }
}

//==============================================================================
void AudioEngine::syncInputs()
{
    auto& dm = engine->getDeviceManager();

    // One input per jack, so "Input 1" is the guitar or bass. If this splits anything the
    // inputs are rebuilt and the change comes back here.
    dm.setAllWaveInputsToNumChannels (1);

    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto wip = dm.getWaveInDevice (i); wip != nullptr && ! wip->isEnabled())
            wip->setEnabled (true);

    clearInputMeters();

    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
    {
        if (auto wip = dm.getWaveInDevice (i))
        {
            auto meter = std::make_unique<InputMeter>();
            meter->measurer = &wip->levelMeasurer;
            meter->measurer->addClient (meter->client);
            inputMeters[wip->getName()] = std::move (meter);
        }
    }

    bool anyAudioChannel = false;

    for (auto& c : song.channels)
        anyAudioChannel = anyAudioChannel || c.type == InstrumentType::audio;

    // Inputs only exist inside a playback context; targets can't move mid-take
    if (! anyAudioChannel || edit->getTransport().isRecording())
        return;

    edit->getTransport().ensureContextAllocated();

    for (auto* instance : edit->getAllInputDevices())
    {
        auto& device = instance->getInputDevice();

        if (device.getDeviceType() != te::InputDevice::waveDevice)
            continue;

        const auto name = device.getName();
        bool monitored = false;

        for (auto& channel : song.channels)
        {
            if (channel.type != InstrumentType::audio)
                continue;

            auto it = slots.find (channel.id);

            if (it == slots.end() || it->second.track == nullptr)
                continue;

            const auto trackId = it->second.track->itemID;
            const bool wanted = channel.input == name;
            const bool has = instance->getTargets().contains (trackId);

            if (wanted && ! has)
                (void) instance->setTarget (trackId, false, nullptr, 0);
            else if (! wanted && has)
                (void) instance->removeTarget (trackId, nullptr);

            if (wanted)
            {
                if (instance->isRecordingEnabled (trackId) != channel.armed)
                    instance->setRecordingEnabled (trackId, channel.armed);

                monitored = monitored || channel.monitor;
            }
        }

        // Automatic: you hear the input while its track is armed
        const auto mode = monitored ? te::InputDevice::MonitorMode::automatic : te::InputDevice::MonitorMode::off;

        if (device.getMonitorMode() != mode)
            device.setMonitorMode (mode);
    }

    // Monitoring through the computer must not wait for plugins that look ahead (the
    // master limiter's 1.5 ms, a plugin's oversampling): bypass them while an armed
    // track is monitoring, so the only delay left is the interface's own
    juce::Array<te::EditItemID> bypass;
    bool monitoring = false;

    for (auto& c : song.channels)
        monitoring = monitoring || (c.type == InstrumentType::audio && c.armed && c.monitor);

    // ...except pitch correction on a track being monitored: singing or playing through it live is what it's for
    // (its 9.4 ms comes with it); on any other track it steps aside like the rest
    if (monitoring && options.lowLatencyMonitoring)
    {
        juce::Array<te::EditItemID> monitoredTracks;

        for (auto& c : song.channels)
            if (c.type == InstrumentType::audio && c.armed && c.monitor)
                if (auto it = slots.find (c.id); it != slots.end() && it->second.track != nullptr)
                    monitoredTracks.add (it->second.track->itemID);

        for (auto p : te::getAllPlugins (*edit, false))
            if (p->getLatencySeconds() > 0.0)
                if (dynamic_cast<NightTune*> (p) == nullptr || p->getOwnerTrack() == nullptr
                     || ! monitoredTracks.contains (p->getOwnerTrack()->itemID))
                    bypass.add (p->itemID);
    }

    if (bypass != edit->getLowLatencyDisabledPlugins())
        edit->setLowLatencyDisabledPlugins (bypass);
}

void AudioEngine::clearInputMeters()
{
    // A rebuilt device list deletes the old inputs (and their measurers) first, so only
    // detach from measurers that still belong to a live input
    auto& dm = engine->getDeviceManager();

    for (auto& [name, meter] : inputMeters)
        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
            if (auto wip = dm.getWaveInDevice (i); wip != nullptr && &wip->levelMeasurer == meter->measurer)
                meter->measurer->removeClient (meter->client);

    inputMeters.clear();
}

juce::StringArray AudioEngine::getInputNames() const
{
    juce::StringArray names;
    auto& dm = engine->getDeviceManager();

    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto wip = dm.getWaveInDevice (i))
            names.add (wip->getName());

    return names;
}

float AudioEngine::getInputPeak (const juce::String& inputName)
{
    auto it = inputMeters.find (inputName);

    if (it == inputMeters.end())
        return 0.0f;

    return juce::Decibels::decibelsToGain (it->second->client.getAndClearAudioLevel (0).dB);
}

te::InputDeviceInstance* AudioEngine::findInput (const juce::String& name) const
{
    if (name.isEmpty())
        return nullptr;

    for (auto* instance : edit->getAllInputDevices())
        if (instance->getInputDevice().getName() == name)
            return instance;

    return nullptr;
}

bool AudioEngine::anyArmedInput() const
{
    for (auto& c : song.channels)
        if (c.type == InstrumentType::audio && c.armed && findInput (c.input) != nullptr)
            return true;

    return false;
}

int AudioEngine::channelForTrack (te::EditItemID trackId) const
{
    for (auto& [id, slot] : slots)
        if (slot.track != nullptr && slot.track->itemID == trackId)
            return id;

    return 0;
}

juce::String AudioEngine::takeRecordingFolderNotice()
{
    return std::exchange (static_cast<NightEngineBehaviour&> (engine->getEngineBehaviour()).notice, {});
}

void AudioEngine::setProjectFile (const juce::File& projectFile)
{
    auto& behaviour = static_cast<NightEngineBehaviour&> (engine->getEngineBehaviour());

    behaviour.recordingFolder = projectFile != juce::File()
        ? projectFile.getParentDirectory().getChildFile (projectFile.getFileNameWithoutExtension() + " Audio")
        : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Nightshift").getChildFile ("Recordings");
}

//==============================================================================
juce::String AudioEngine::toggleRecord()
{
    auto& transport = edit->getTransport();

    if (transport.isRecording())
    {
        stop();
        return {};
    }

    syncNow();

    bool armed = false;

    for (auto& c : song.channels)
        armed = armed || (c.type == InstrumentType::audio && c.armed);

    if (! armed)
        return "Arm an audio track first: click the round button on its channel in the rack.";

    if (! anyArmedInput())
        return "The armed track has no working input. Open Audio settings (F10) and check your interface's inputs.";

    // Takes land in the playlist, so recording always runs the song
    if (song.mode != PlayMode::song)
    {
        song.mode = PlayMode::song;
        song.notify (Song::Change::transport);
        syncNow();
    }

    if (liveAudio)
        engine->getDeviceManager().dispatchPendingUpdates();

    takeCheckpointed = false;
    loopTake.reset();
    recordStartTicks = startTicks;

    if (song.hasLoop())
    {
        // Round the loop from its start, a take each time round
        if (ticksToSeconds (song.loopEnd) - ticksToSeconds (song.loopStart) < 2.0)
            return "The loop is too short to record takes in: make it longer than 2 seconds.";

        loopTake = std::make_pair (song.loopStart, song.loopEnd);
        recordStartTicks = song.loopStart;
        transport.setLoopRange ({ te::TimePosition::fromSeconds (ticksToSeconds (song.loopStart)),
                                  te::TimePosition::fromSeconds (ticksToSeconds (song.loopEnd)) });
        transport.looping = true;
    }
    else
    {
        transport.looping = false;
    }

    transport.setPosition (te::TimePosition::fromSeconds (ticksToSeconds (recordStartTicks)));
    transport.record (false);

    if (! transport.isRecording())
        return "Recording couldn't start; check Audio settings (F10).";

    // Tracktion plays in from before the punch-in; the metronome counts it
    const double punchIn = ticksToSeconds (recordStartTicks);

    if (const double lead = countInSeconds (punchIn, options.countInBars); lead > 0.0)
    {
        if (auto* m = getMetronome())
            m->setCountIn (punchIn - lead, punchIn);

        countInEnd = punchIn;
    }

    syncMetronome();
    return {};
}

bool AudioEngine::isRecording() const
{
    return edit->getTransport().isRecording();
}

bool AudioEngine::isCountingIn() const
{
    if (! isPlaying())
        return false;

    auto& transport = edit->getTransport();
    double now = transport.getPosition().inSeconds();

    if (auto context = transport.getCurrentPlaybackContext())
        now = context->getPosition().inSeconds();

    return now < countInEnd - 1.0e-3;
}

double AudioEngine::countInSeconds (double homeSeconds, int bars) const
{
    bars = juce::jlimit (0, 2, bars);

    if (bars == 0)
        return 0.0;

    // Pattern mode: the pattern's bars at the start's tempo (the only one it plays)
    if (song.mode == PlayMode::pattern)
        return bars * song.barTicksOf (song.currentPattern()) * 60.0 / (ticksPerBeat * song.bpm);

    // The bars before home as the song plays them, home as far into its bar as it is (before
    // the start, the start's bars and tempo)
    const double home = secondsToTicks (homeSeconds);
    const int bar = song.barAt (home);
    const double from = home - (song.barStart (bar) - song.barStart (bar - bars));
    return homeSeconds - ticksToSeconds (from);
}

void AudioEngine::endCountIn()
{
    countInEnd = -1.0e9;
    loopFrom.reset();

    if (auto* m = getMetronome())
        m->setCountIn (0.0, 0.0);
}

juce::String AudioEngine::describeRecordTargets() const
{
    juce::StringArray parts;

    if (auto* c = song.findChannel (song.selectedChannelId); c != nullptr && c->type != InstrumentType::audio)
        parts.add (c->name + " (notes)");

    for (auto& c : song.channels)
        if (c.type == InstrumentType::audio && c.armed)
            parts.add (c.name + " (audio)");

    if (parts.size() > 1)
        return parts.joinIntoString (", ", 0, parts.size() - 1) + " and " + parts[parts.size() - 1];

    return parts.joinIntoString ({});
}

//==============================================================================
juce::String AudioEngine::setRecordArmed (bool armed)
{
    if (armed == recordArmed)
        return {};

    recordArmed = armed;

    // Armed while playing: it punches in now
    if (armed)
        return isPlaying() ? startRecordingPass (true) : juce::String();

    // Disarmed: whatever is recording stops, and playback carries on
    if (noteTake.has_value())
    {
        if (noteTake->waiting)
            noteTake.reset();
        else
            stopNoteTake();
    }

    if (isRecording())
    {
        auto& transport = edit->getTransport();

        // A loop take is sliced into passes by Nightshift; Tracktion mustn't split it too
        if (loopTake.has_value())
            transport.looping = false;

        transport.stopRecording (false);
    }

    endCountIn();
    syncMetronome();
    return {};
}

juce::String AudioEngine::startRecordingPass (bool punchIn)
{
    syncNow();

    const auto* selected = song.findChannel (song.selectedChannelId);
    int noteChannel = selected != nullptr && selected->type != InstrumentType::audio ? selected->id : 0;
    bool armedAudio = false;
    juce::String notice;

    // A frozen channel's notes can't change, so what's played isn't recorded into it
    if (noteChannel != 0 && selected->freeze.on)
    {
        notice = selected->name + " is frozen, so what you play isn't recorded into it: unfreeze it (its menu in Steps) first.";
        noteChannel = 0;
    }

    for (auto& c : song.channels)
        armedAudio = armedAudio || (c.type == InstrumentType::audio && c.armed);

    if (! armedAudio && noteChannel == 0)
    {
        if (! punchIn)
            startPlaying();

        return notice.isNotEmpty() ? notice
                                   : "Record is armed, but there's nothing to record into: select an instrument channel to record what you play, "
                                     "or arm an audio track (the round button on it).";
    }

    if (armedAudio)
    {
        if (! punchIn)
        {
            // From the song position (or round the loop), after the count-in
            if (auto problem = toggleRecord(); problem.isNotEmpty())
                return problem;
        }
        else if (song.mode == PlayMode::song && ! song.hasLoop() && anyArmedInput())
        {
            // In, here: Tracktion records the armed inputs from where the playhead is
            auto& transport = edit->getTransport();
            takeCheckpointed = false;
            loopTake.reset();
            recordStartTicks = getPlayheadTicks();

            if (auto context = transport.getCurrentPlaybackContext())
                recordStartTicks = secondsToTicks (context->getPosition().inSeconds());

            transport.looping = false;
            transport.record (false);
        }
        else
        {
            notice = "Audio punches in only in Song mode with the loop off; the armed tracks record from the next Play.";
        }
    }

    if (noteChannel != 0)
    {
        // Alongside audio the transport is already running: the notes ride along
        const bool running = punchIn || isRecording();
        startNoteTake (noteChannel, running);

        if (! running)
        {
            if (options.startOnInput)
            {
                noteTake->waiting = true;
                syncMetronome();
                return "Waiting for you: recording " + selected->name + " starts with the first note you play. Space cancels.";
            }

            playWithCountIn (ticksToSeconds (noteTake->songMode && noteTake->wrap <= 0.0 ? homeTicks() : noteTake->origin),
                             options.countInBars);
        }
    }

    syncMetronome();

    const auto what = describeRecordTargets();
    const auto where = noteChannel == 0 ? juce::String()
                     : song.mode == PlayMode::pattern ? " into " + song.currentPattern().name + " as it loops"
                                                      : juce::String (" into a new pattern on the Arrange");
    const auto countIn = punchIn || options.countInBars == 0 ? juce::String()
                       : " after a " + juce::String (options.countInBars) + "-bar count-in";

    return (notice.isNotEmpty() ? notice + "\n" : juce::String()) + "Recording " + what + where + countIn + ". Space stops.";
}

juce::String AudioEngine::startNoteTake (int channelId, bool running)
{
    NoteTake take;
    take.channelId = channelId;
    take.snapTicks = recordSnapTicks;
    take.blend = options.blendNotes;
    take.songMode = song.mode == PlayMode::song;

    if (const auto info = getLatencyInfo(); info.sampleRate > 0.0)
        take.latencySeconds = info.outputSamples / info.sampleRate;

    if (! take.songMode)
    {
        // Into the pattern, round and round
        take.patternId = song.currentPattern().id;
        take.wrap = song.patternLength (song.currentPattern());
    }
    else if (song.hasLoop())
    {
        // Round the playlist loop, into a clip at its start
        take.origin = song.loopStart;
        take.wrap = song.loopEnd - song.loopStart;
    }
    else
    {
        // Straight on, into a clip from the bar playback started in (or is in now)
        double from = homeTicks();

        if (running)
            from = isRecording() ? recordStartTicks : getPlayheadTicks();

        if (auto context = edit->getTransport().getCurrentPlaybackContext(); running && ! isRecording() && context != nullptr)
            from = secondsToTicks (context->getPosition().inSeconds());

        take.origin = song.barStart (song.barAt (std::max (0.0, from)));

        // The song plays round its whole length otherwise; a take runs on past the end (an
        // audio take has seen to it already: changing looping mid-take would stop it)
        if (! isRecording())
            edit->getTransport().looping = false;
    }

    noteTake = std::move (take);
    return {};
}

void AudioEngine::playWithCountIn (double home, int bars)
{
    if (liveAudio)
        engine->getDeviceManager().dispatchPendingUpdates();

    auto& transport = edit->getTransport();
    const bool loops = transport.looping.get();
    const double lead = countInSeconds (home, bars);

    if (lead <= 0.0)
    {
        transport.setPosition (te::TimePosition::fromSeconds (home));
        transport.play (false);
        wantsToPlay = true;
        return;
    }

    // From just before the count-in's first click (playback can start a block late, which
    // would swallow it), unlooped (Play would jump into the loop); then the loop is put back
    // with the playhead rolling into it from before, as Tracktion's own count-in does
    const double from = home - lead - 0.1;
    transport.looping = false;
    transport.setPosition (te::TimePosition::fromSeconds (from));
    transport.play (false);
    wantsToPlay = true;

    if (loops)
    {
        if (auto context = transport.getCurrentPlaybackContext())
        {
            if (auto playHead = context->getNodePlayHead())
            {
                const auto at = playHead->getUnloopedPosition();
                playHead->setLoopRange (true, te::toSamples (transport.getLoopRange(), context->getSampleRate()), false);
                playHead->setRollInToLoop (at);
            }
        }

        transport.looping = true;
    }

    if (auto* m = getMetronome())
        m->setCountIn (home - lead, home);

    countInEnd = home;
}

void AudioEngine::recordingFinished (te::InputDeviceInstance&, te::EditItemID target, const juce::ReferenceCountedArray<te::Clip>& clips)
{
    const int channelId = channelForTrack (target);

    if (channelId == 0)
        return;

    for (auto* clip : clips)
    {
        auto wave = dynamic_cast<te::WaveAudioClip*> (clip);

        if (wave == nullptr)
            continue;

        // Tracktion places the recording with latency compensation done; from that, the
        // file position for any song time t (unlooped: it keeps counting round a loop)
        const auto position = wave->getPosition();
        const juce::String file = wave->getOriginalFile().getFullPathName();
        const double clipStart = position.getStart().inSeconds();
        const double clipOffset = position.offset.inSeconds();
        const double fileEnd = clipOffset + position.getLength().inSeconds();
        auto fileAt = [=] (double songTime) { return clipOffset + (songTime - clipStart); };

        // The file holds the count-in as well; the takes start at the punch-in
        const double punchIn = ticksToSeconds (recordStartTicks);
        std::vector<Take> takes;
        double takesStart = punchIn, length = 0.0;
        int compTake = 0;

        if (loopTake.has_value())
        {
            // Round the loop: a take per pass, all slices of the one file. A pass cut short
            // by Stop is kept if it has a second or more in it.
            length = ticksToSeconds (loopTake->second) - ticksToSeconds (loopTake->first);

            for (int pass = 0;; ++pass)
            {
                const double passStart = fileAt (punchIn + pass * length);
                const double available = std::min (length, fileEnd - passStart);

                if (available < std::min (1.0, length))
                    break;

                takes.push_back ({ file, passStart, std::max (0.0, -passStart), available });

                if (available >= length - 1.0e-3)
                    compTake = pass;        // the latest complete pass is heard
            }
        }
        else
        {
            takesStart = std::max (punchIn, clipStart);
            length = clipStart + position.getLength().inSeconds() - takesStart;

            if (length >= 0.05)
                takes.push_back ({ file, fileAt (takesStart), 0.0, length });
        }

        if (takes.empty())
            continue;

        if (! std::exchange (takeCheckpointed, true))
            song.checkpoint();

        song.addTakes (channelId, takesStart, length, std::move (takes), compTake);
    }

    song.notify (Song::Change::playlist);
}

void AudioEngine::recordingStopped (te::SyncPoint, bool)
{
    // Back to the looping song, and to where the take began
    juce::MessageManager::callAsync ([this, weakAlive = std::weak_ptr<bool> (alive)]
    {
        if (auto stillAlive = weakAlive.lock(); stillAlive && *stillAlive)
        {
            edit->setClickTrackRange ({});
            loopTake.reset();
            endCountIn();
            pending |= needClips | needInputs;
            syncNow();
            syncMetronome();
        }
    });
}

//==============================================================================
AudioEngine::LatencyInfo AudioEngine::getLatencyInfo() const
{
    LatencyInfo info;
    auto& dm = engine->getDeviceManager().deviceManager;

    if (auto device = dm.getCurrentAudioDevice())
    {
        info.driver = device->getTypeName();
        info.device = device->getName();
        info.sampleRate = device->getCurrentSampleRate();
        info.bufferSize = device->getCurrentBufferSizeSamples();
        info.inputSamples = device->getInputLatencyInSamples();
        info.outputSamples = device->getOutputLatencyInSamples();
        info.xruns = device->getXRunCount();
        info.cpu = dm.getCpuUsage();
    }

    return info;
}

void AudioEngine::loadOptions()
{
    // Kept beside Tracktion's own settings, in %APPDATA%\Nightshift\Settings.xml. Headless
    // runs (renders, tests) use the defaults and never write, so they can't change the app.
    if (! liveAudio)
        return;

    auto& file = engine->getPropertyStorage().getPropertiesFile();
    options.lowLatencyMonitoring = file.getBoolValue ("nsLowLatencyMonitoring", true);
    options.preferInterface = file.getBoolValue ("nsPreferInterface", true);
    options.interfaceInputDbu = juce::jlimit (-20.0, 40.0, file.getDoubleValue ("nsInterfaceInputDbu", NightAmp::defaultInterfaceLevel));
    options.countInBars = juce::jlimit (0, 2, file.getIntValue ("nsCountInBars", 1));
    options.computerKeyboard = file.getBoolValue ("nsComputerKeyboard", true);
    options.blendNotes = file.getBoolValue ("nsBlendNotes", true);
    options.startOnInput = file.getBoolValue ("nsStartOnInput", false);
    options.startScreen = file.getBoolValue ("nsStartScreen", true);
    options.recentProjects = juce::StringArray::fromTokens (file.getValue ("nsRecentProjects"), "|", {});
    options.recentProjects.removeEmptyStrings();
    options.sampleFolders = juce::StringArray::fromTokens (file.getValue ("nsSampleFolders"), "|", {});
    options.sampleFolders.removeEmptyStrings();
    options.donationAsked = file.getBoolValue ("nsDonationAsked", false);
    options.exportBits = file.getIntValue ("nsExportBits", 24);

    if (options.exportBits != 16 && options.exportBits != 32)
        options.exportBits = 24;

    options.exportKind = juce::jlimit (0, 3, file.getIntValue ("nsExportKind", 0));
    options.mp3Kbps = file.getIntValue ("nsMp3Kbps", 320);

    if (std::find (mp3::bitRates().begin(), mp3::bitRates().end(), options.mp3Kbps) == mp3::bitRates().end())
        options.mp3Kbps = 320;

    options.stemsThroughMaster = file.getBoolValue ("nsStemsThroughMaster", false);

    options.disabledMidiInputs = juce::StringArray::fromTokens (file.getValue ("nsDisabledMidiInputs"), "|", {});
    options.disabledMidiInputs.removeEmptyStrings();

    // Synths plugged in: [[name, timing in ms, clock], ...]
    if (auto* outs = juce::JSON::parse (file.getValue ("nsMidiOuts", "[]")).getArray())
        for (auto& o : *outs)
            if (o.size() >= 3 && o[0].toString().isNotEmpty())
                options.midiOuts[o[0].toString()] = { juce::jlimit (-MidiOutHub::maxTimingMs, MidiOutHub::maxTimingMs, (int) o[1]), (bool) o[2] };

    options.metronome = file.getBoolValue ("nsMetronome", false);
    options.metronomeRecordingOnly = file.getBoolValue ("nsClickRecordingOnly", false);
    options.clickSound = juce::jlimit (0, metronome::soundNames().size() - 1, file.getIntValue ("nsClickSound", 0));
    options.clickRhythm = juce::jlimit (0, (int) metronome::rhythms().size() - 1, file.getIntValue ("nsClickRhythm", 0));
    options.clickVolume = juce::jlimit (0.0f, 1.0f, (float) file.getDoubleValue ("nsClickVolume", 0.7));
    options.clickPan = juce::jlimit (-1.0f, 1.0f, (float) file.getDoubleValue ("nsClickPan", 0.0));

    // "7/8=2+2+3;5/4=3+2" and "7/8  2+2+3=3121211;4/4=3222"
    for (auto& item : juce::StringArray::fromTokens (file.getValue ("nsClickGroupings"), ";", {}))
        if (item.contains ("="))
            options.clickGroupings[item.upToFirstOccurrenceOf ("=", false, false)] = item.fromFirstOccurrenceOf ("=", false, false);

    for (auto& item : juce::StringArray::fromTokens (file.getValue ("nsClickAccents"), ";", {}))
    {
        const auto name = item.upToFirstOccurrenceOf ("=", false, false);
        const auto digits = item.fromFirstOccurrenceOf ("=", false, false);

        if (name.isNotEmpty() && digits.isNotEmpty())
            options.clickAccents[name] = metronome::accentsFromText (digits, digits.length());
    }
}

void AudioEngine::setOptions (const Options& o)
{
    const bool monitoringChanged = o.lowLatencyMonitoring != options.lowLatencyMonitoring;
    const bool midiOutsChanged = o.midiOuts != options.midiOuts;
    options = o;

    // Every amp hears the guitar at the interface's level (input calibration)
    NightAmp::setInterfaceLevel (options.interfaceInputDbu);

    if (midiOutsChanged && midiOut != nullptr)
        syncMidiOut();

    if (liveAudio)
    {
        auto& file = engine->getPropertyStorage().getPropertiesFile();
        file.setValue ("nsLowLatencyMonitoring", options.lowLatencyMonitoring);
        file.setValue ("nsPreferInterface", options.preferInterface);
        file.setValue ("nsInterfaceInputDbu", options.interfaceInputDbu);
        file.setValue ("nsCountInBars", options.countInBars);
        file.setValue ("nsComputerKeyboard", options.computerKeyboard);
        file.setValue ("nsBlendNotes", options.blendNotes);
        file.setValue ("nsStartOnInput", options.startOnInput);
        file.setValue ("nsStartScreen", options.startScreen);
        file.setValue ("nsRecentProjects", options.recentProjects.joinIntoString ("|"));
        file.setValue ("nsSampleFolders", options.sampleFolders.joinIntoString ("|"));
        file.setValue ("nsDonationAsked", options.donationAsked);
        file.setValue ("nsExportBits", options.exportBits);
        file.setValue ("nsExportKind", options.exportKind);
        file.setValue ("nsMp3Kbps", options.mp3Kbps);
        file.setValue ("nsStemsThroughMaster", options.stemsThroughMaster);
        file.setValue ("nsDisabledMidiInputs", options.disabledMidiInputs.joinIntoString ("|"));

        juce::Array<juce::var> outs;

        for (auto& [name, s] : options.midiOuts)
            outs.add (juce::Array<juce::var> { name, s.timingMs, s.clock });

        file.setValue ("nsMidiOuts", juce::JSON::toString (juce::var (outs), true));

        file.setValue ("nsMetronome", options.metronome);
        file.setValue ("nsClickRecordingOnly", options.metronomeRecordingOnly);
        file.setValue ("nsClickSound", options.clickSound);
        file.setValue ("nsClickRhythm", options.clickRhythm);
        file.setValue ("nsClickVolume", options.clickVolume);
        file.setValue ("nsClickPan", options.clickPan);

        juce::StringArray groupings, accents;

        for (auto& [meter, grouping] : options.clickGroupings)
            groupings.add (meter + "=" + grouping);

        for (auto& [name, levels] : options.clickAccents)
            accents.add (name + "=" + metronome::accentsToText (levels));

        file.setValue ("nsClickGroupings", groupings.joinIntoString (";"));
        file.setValue ("nsClickAccents", accents.joinIntoString (";"));
    }

    // Tracktion's count-in (for audio takes) sounds silent: the metronome counts in
    edit->setCountInMode (options.countInBars == 2 ? te::Edit::CountIn::twoBar
                        : options.countInBars == 1 ? te::Edit::CountIn::oneBar
                                                   : te::Edit::CountIn::none);
    edit->clickTrackEnabled = false;
    syncMetronome();

    if (monitoringChanged)
    {
        pending |= needInputs;
        triggerAsyncUpdate();
    }
}

bool AudioEngine::openPreferredInterface (int bufferSize)
{
    if (! liveAudio)
        return false;

    auto& dm = getDeviceManager();

    for (auto* type : dm.getAvailableDeviceTypes())
    {
        if (type->getTypeName() != lowLatencyDriver)
            continue;

        type->scanForDevices();

        for (auto& name : type->getDeviceNames())
        {
            if (! name.containsIgnoreCase (preferredInterface))
                continue;

            // Already open: keep whatever buffer the user settled on
            auto current = dm.getCurrentAudioDevice();

            if (current != nullptr && current->getTypeName() == lowLatencyDriver && current->getName() == name)
                return true;

            // The driver stays listed while the interface is off or unplugged. Try it on its own
            // before letting go of a device that works: closing and reopening one at startup
            // makes Windows report a device change a second later, and the rebuild that follows
            // stopped playback started in that second. (Only one ASIO driver can be loaded, so
            // there's no trying it beside another ASIO device.)
            if (current != nullptr && current->getTypeName() != lowLatencyDriver)
            {
                std::unique_ptr<juce::AudioIODevice> probe (type->createDevice (name, name));

                if (probe == nullptr || probe->getLastError().isNotEmpty() || probe->getAvailableSampleRates().isEmpty())
                    return false;
            }

            dm.setCurrentAudioDeviceType (lowLatencyDriver, true);

            juce::AudioDeviceManager::AudioDeviceSetup setup;
            dm.getAudioDeviceSetup (setup);
            setup.outputDeviceName = name;
            setup.inputDeviceName = name;
            setup.sampleRate = 48000.0;
            setup.bufferSize = bufferSize;
            setup.useDefaultInputChannels = false;
            setup.useDefaultOutputChannels = false;
            setup.inputChannels.clear();
            setup.inputChannels.setRange (0, 2, true);      // the two front jacks
            setup.outputChannels.clear();
            setup.outputChannels.setRange (0, 2, true);     // main outs and headphones

            if (auto error = dm.setAudioDeviceSetup (setup, true); error.isNotEmpty())
            {
                juce::Logger::writeToLog ("Couldn't open " + name + ": " + error);
                return false;
            }

            return dm.getCurrentAudioDevice() != nullptr;
        }
    }

    return false;
}

juce::StringArray AudioEngine::getLatencyBypassedPlugins() const
{
    juce::StringArray names;
    const auto ids = edit->getLowLatencyDisabledPlugins();

    for (auto p : te::getAllPlugins (*edit, false))
        if (ids.contains (p->itemID))
            names.add (p->getName());

    return names;
}

void AudioEngine::processHostedBlock (juce::AudioBuffer<float>& buffer)
{
    juce::MidiBuffer midi;
    processHostedBlock (buffer, midi);
}

void AudioEngine::processHostedBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    jassert (! liveAudio);
    engine->getDeviceManager().getHostedAudioDeviceInterface().processBlock (buffer, midi);
}

//==============================================================================
double AudioEngine::ticksToSeconds (double ticks) const
{
    return edit->tempoSequence.toTime (te::BeatPosition::fromBeats (beatsFor (ticks))).inSeconds();
}

double AudioEngine::secondsToTicks (double seconds) const
{
    return edit->tempoSequence.toBeats (te::TimePosition::fromSeconds (seconds)).inBeats() * ticksPerBeat * 4.0 / song.meterUnit;
}

juce::String AudioEngine::togglePlay()
{
    if (isPlaying() || isWaitingForInput())
    {
        stop();
        return {};
    }

    // The song takes over from practising
    if (auto* m = getMetronome())
        m->setPractising (false);

    if (recordArmed)
        return startRecordingPass (false);

    startPlaying();
    return {};
}

void AudioEngine::startPlaying()
{
    syncNow();

    // The output device list is built asynchronously after the device opens, and its
    // arrival stops the transport; bring it in now if Play comes that early
    if (liveAudio)
        engine->getDeviceManager().dispatchPendingUpdates();

    auto& transport = edit->getTransport();
    transport.setPosition (te::TimePosition::fromSeconds (ticksToSeconds (homeTicks())));
    transport.play (false);
    wantsToPlay = true;
}

void AudioEngine::timerCallback()
{
    // A sample that's played to its end lets go of its file
    if (auto* preview = dynamic_cast<SamplePreview*> (previewPlugin.get()))
        preview->letGoIfDone();

    if (! liveAudio)
        return;

    // A synth plugged in (or back in) while running: its output opened, every two seconds
    if (++midiOutRefresh >= 40)
    {
        midiOutRefresh = 0;
        closeTracktionMidiOutputs();
        midiOut->refreshPorts();
    }

    // A device starting (opened, switched, or reset by its driver) suspends playback until
    // it's prepared; if the transport's own check runs meanwhile it stops itself. Carry on from
    // where it got to, unless the stop came long after any device start
    auto& transport = edit->getTransport();

    if (! wantsToPlay || transport.isPlaying() || transport.isRecording())
        return;

    if (juce::Time::getMillisecondCounter() - deviceStarts.lastStart.load() < 3000)
        transport.play (false);
    else
        wantsToPlay = false;
}

double AudioEngine::homeTicks() const
{
    // With the playlist loop on, play always starts at its start, as an FL selection does;
    // starting from a song position inside it played a partial pass first
    return song.mode == PlayMode::song && song.hasLoop() ? (double) song.loopStart : startTicks;
}

void AudioEngine::stop()
{
    auto& transport = edit->getTransport();
    wantsToPlay = false;

    if (noteTake.has_value())
    {
        if (noteTake->waiting)
            noteTake.reset();
        else
            stopNoteTake();
    }

    // A loop take is cut into passes here, from one continuous file; with looping still on,
    // Tracktion would chop it up itself, by counting from the start of the file
    const bool recordingAudio = transport.isRecording();

    if (recordingAudio && loopTake.has_value())
        transport.looping = false;

    transport.stop (false, false);
    endCountIn();

    // (after audio, once the takes are in: recordingStopped)
    if (! recordingAudio)
        syncLooping();

    transport.setPosition (te::TimePosition::fromSeconds (ticksToSeconds (homeTicks())));
    syncMetronome();
}

bool AudioEngine::isPlaying() const
{
    return edit->getTransport().isPlaying();
}

double AudioEngine::getPlayheadTicks() const
{
    return secondsToTicks (edit->getTransport().getPosition().inSeconds());
}

void AudioEngine::setPlayheadTicks (double ticks)
{
    startTicks = juce::jmax (0.0, ticks);
    edit->getTransport().setPosition (te::TimePosition::fromSeconds (ticksToSeconds (startTicks)));
}

void AudioEngine::previewNote (int channelId, int key, int velocity, int lengthMs)
{
    syncNow();
    auto it = slots.find (channelId);

    if (it == slots.end() || it->second.track == nullptr || it->second.instrument == nullptr)
        return;

    edit->getTransport().ensureContextAllocated();
    it->second.track->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, key, (juce::uint8) juce::jlimit (1, 127, velocity)), 0);

    juce::Timer::callAfterDelay (lengthMs, [this, weakAlive = std::weak_ptr<bool> (alive), channelId, key]
    {
        if (auto stillAlive = weakAlive.lock(); stillAlive && *stillAlive)
            if (auto found = slots.find (channelId); found != slots.end() && found->second.track != nullptr)
                found->second.track->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, key), 0);
    });
}

//==============================================================================
void AudioEngine::liveNoteOn (int channelId, int key, int velocity)
{
    syncNow();
    auto it = slots.find (channelId);

    if (it == slots.end() || it->second.track == nullptr || it->second.instrument == nullptr)
        return;

    edit->getTransport().ensureContextAllocated();
    it->second.track->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, key, (juce::uint8) juce::jlimit (1, 127, velocity)), 0);
    recordNoteOn (channelId, key, velocity, 0.0);
}

void AudioEngine::recordNoteOn (int channelId, int key, int velocity, double secondsAgo)
{
    if (! noteTake.has_value() || noteTake->channelId != channelId)
        return;

    // Waiting for the first note: it starts the take, on its first beat
    if (noteTake->waiting)
    {
        noteTake->waiting = false;
        playWithCountIn (ticksToSeconds (noteTake->origin), 0);
        noteTake->held[key] = { 0.0, juce::jlimit (1, 127, velocity) };
        syncMetronome();
        return;
    }

    if (isPlaying())
    {
        // Pressed again before its release reached us: the first one ends here
        if (noteTake->held.count (key) > 0)
            finishNote (key, secondsAgo);

        noteTake->held[key] = { heardTakeTicks (secondsAgo), juce::jlimit (1, 127, velocity) };
    }
}

void AudioEngine::liveNoteOff (int channelId, int key)
{
    if (auto it = slots.find (channelId); it != slots.end() && it->second.track != nullptr)
        it->second.track->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, key), 0);

    recordNoteOff (channelId, key, 0.0);
}

void AudioEngine::recordNoteOff (int channelId, int key, double secondsAgo)
{
    if (noteTake.has_value() && noteTake->channelId == channelId && noteTake->held.count (key) > 0)
    {
        finishNote (key, secondsAgo);

        if (noteTake->held.empty())
            writeTakeNotes();
    }
}

double AudioEngine::heardTakeTicks (double secondsAgo) const
{
    auto& transport = edit->getTransport();
    double seconds = transport.getPosition().inSeconds();

    // The playback context is where the audio thread is now (the transport's own position is
    // a copy brought up to date now and then); what's heard left it the output latency ago,
    // and a note from a MIDI keyboard was played the time it took to reach us before that
    if (auto context = transport.getCurrentPlaybackContext())
        seconds = context->getPosition().inSeconds();

    seconds -= (noteTake.has_value() ? noteTake->latencySeconds : 0.0) + secondsAgo;

    // From the take's start; round its loop, but not back from the count-in before it
    const double origin = noteTake.has_value() ? noteTake->origin : 0.0;
    const double wrap = noteTake.has_value() ? noteTake->wrap : 0.0;
    double ticks = secondsToTicks (seconds) - origin;

    if (wrap > 0.0 && ticks >= 0.0)
        ticks = std::fmod (ticks, wrap);

    return ticks;
}

void AudioEngine::finishNote (int key, double secondsAgo)
{
    auto held = noteTake->held.find (key);

    if (held == noteTake->held.end())
        return;

    const double wrap = noteTake->wrap;
    const int snap = juce::jmax (1, noteTake->snapTicks);
    double start = held->second.first, duration = heardTakeTicks (secondsAgo) - start;
    const int velocity = held->second.second;
    noteTake->held.erase (held);

    // Played in the count-in: up to half a step early still means the first beat; earlier
    // than that it's getting ready, not playing
    if (start < -ticksPerStep * 0.5)
        return;

    // Held across the loop's end
    if (duration <= 0.0 && wrap > 0.0)
        duration += wrap;

    // Onto the grid: starts to the nearest step (one at the very end goes round to the start),
    // lengths to whole steps, at least one
    start = std::max (0.0, std::round (start / snap) * snap);

    if (wrap > 0.0 && start >= wrap)
        start -= wrap;

    duration = juce::jlimit ((double) snap, wrap > 0.0 ? wrap : 1.0e9, std::round (duration / snap) * snap);

    // A pattern can't hold a note round its end: held past it, it ends there (or the pattern
    // would grow a bar and stop matching the loop being recorded round)
    if (! noteTake->songMode && wrap > 0.0)
        duration = std::max (1.0, std::min (duration, wrap - start));

    noteTake->finished.push_back ({ (int) start, juce::jmax (1, (int) duration), key, velocity });
}

void AudioEngine::writeTakeNotes()
{
    auto& take = *noteTake;

    if (take.finished.empty())
        return;

    if (! std::exchange (take.wrote, true))
    {
        // The take is one undo step, back to before its first note
        song.checkpoint();

        if (take.songMode)
        {
            // FL's way: into a pattern placed where playback started. The current pattern if
            // it's empty and not on the Arrange yet; else a new one, so nothing else gets copied
            auto& current = song.currentPattern();
            bool placed = false;

            for (auto& clip : song.playlist)
                placed = placed || clip.patternId == current.id;

            const int patternId = current.isEmpty() && ! placed ? current.id : song.addPatternFor (take.channelId).id;
            const int start = (int) take.origin;

            // In the time signature where it's played (its bars are that long)
            if (auto* p = song.findPattern (patternId))
                song.fitPatternMeter (*p, start);

            // On the channel's track if it's free there, else a new one under it
            const int length = take.wrap > 0.0 ? (int) take.wrap : song.barLength (song.barAt (start));
            const int row = song.trackForChannel (take.channelId, start, start + length);
            song.playlist.push_back ({ patternId, row, start });

            take.patternId = patternId;
            song.currentPatternId = patternId;
            song.notify (Song::Change::patterns);
            song.notify (Song::Change::playlist);
            song.notify (Song::Change::currentPattern);
        }
        else if (! take.blend)
        {
            // Not blending: the take replaces what the channel had in the pattern
            if (auto pattern = song.findPattern (take.patternId))
                pattern->notesFor (take.channelId).clear();
        }
    }

    auto pattern = song.findPattern (take.patternId);

    if (pattern == nullptr)
    {
        take.finished.clear();
        return;
    }

    // Over what's there: the same key at the same step is replaced, not doubled, so going
    // round the loop again doesn't stack copies
    auto& notes = pattern->notesFor (take.channelId);

    for (auto& n : take.finished)
    {
        std::erase_if (notes, [&n] (const Note& old) { return old.key == n.key && old.start == n.start; });
        notes.push_back (n);
    }

    take.finished.clear();
    song.notify (Song::Change::notes);
}

void AudioEngine::stopNoteTake()
{
    if (! noteTake.has_value())
        return;

    // Keys still down end where the take does
    while (! noteTake->held.empty())
        finishNote (noteTake->held.begin()->first);

    writeTakeNotes();
    noteTake.reset();
    syncLooping();
    syncMetronome();
}

std::pair<float, float> AudioEngine::getMasterPeak()
{
    const auto left = meterClient.getAndClearAudioLevel (0);
    const auto right = meterClient.getAndClearAudioLevel (1);
    lastMasterPeak = { juce::Decibels::decibelsToGain (left.dB), juce::Decibels::decibelsToGain (right.dB) };
    return lastMasterPeak;
}

te::AudioTrack* AudioEngine::getTrack (int id) const
{
    if (auto it = slots.find (id); it != slots.end())
        return it->second.track.get();

    if (auto it = busSlots.find (id); it != busSlots.end())
        return it->second.track.get();

    return nullptr;
}

void AudioEngine::dropMeter (int id)
{
    if (auto it = meterTaps.find (id); it != meterTaps.end())
    {
        it->second->measurer->removeClient (it->second->client);
        meterTaps.erase (it);
    }

    peaks.erase (id);
}

void AudioEngine::readMeters (const std::vector<int>& ids)
{
    // Off the strips no longer shown
    for (auto it = meterTaps.begin(); it != meterTaps.end();)
    {
        if (std::find (ids.begin(), ids.end(), it->first) == ids.end())
        {
            it->second->measurer->removeClient (it->second->client);
            it = meterTaps.erase (it);
        }
        else
        {
            ++it;
        }
    }

    peaks.clear();

    for (auto id : ids)
    {
        auto track = getTrack (id);
        auto meter = track != nullptr ? track->getLevelMeterPlugin() : nullptr;

        if (meter == nullptr)
            continue;

        auto& tap = meterTaps[id];

        if (tap != nullptr && tap->measurer != &meter->measurer)
        {
            tap->measurer->removeClient (tap->client);
            tap = nullptr;
        }

        if (tap == nullptr)
        {
            tap = std::make_unique<MeterTap>();
            tap->measurer = &meter->measurer;
            tap->measurer->addClient (tap->client);
        }

        const auto left = tap->client.getAndClearAudioLevel (0);
        const auto right = tap->client.getAndClearAudioLevel (1);
        peaks[id] = { juce::Decibels::decibelsToGain (left.dB), juce::Decibels::decibelsToGain (right.dB) };
    }
}

std::pair<float, float> AudioEngine::getPeak (int id) const
{
    auto it = peaks.find (id);
    return it != peaks.end() ? it->second : std::pair<float, float>();
}

float AudioEngine::getLimiterReductionDb() const
{
    if (auto limiter = dynamic_cast<MasterLimiter*> (masterLimiter.get()))
        return limiter->getGainReductionDb();

    return 0.0f;
}

mastering::Readings AudioEngine::getMasterLoudness() const
{
    if (auto limiter = dynamic_cast<MasterLimiter*> (masterLimiter.get()))
        return limiter->getLoudness();

    return {};
}

void AudioEngine::resetMasterLoudness()
{
    if (auto limiter = dynamic_cast<MasterLimiter*> (masterLimiter.get()))
        limiter->resetLoudness();
}

tracktion::StereoFieldAnalyser::Readings AudioEngine::getMasterStereo() const
{
    if (auto limiter = dynamic_cast<MasterLimiter*> (masterLimiter.get()))
        return limiter->getStereo();

    return {};
}

float AudioEngine::getPunchReductionDb (int band) const
{
    if (auto limiter = dynamic_cast<MasterLimiter*> (masterLimiter.get()))
        return limiter->getPunchReductionDb (band);

    return 0.0f;
}

bool AudioEngine::captureMix (const juce::File& file, bool raw)
{
    auto limiter = dynamic_cast<MasterLimiter*> (masterLimiter.get());

    if (limiter == nullptr)
        return false;

    // Float, so a mix that goes over 0 dB on its way in is kept as it is, not clipped; raw, without
    // the Master tab's shaping too (what the AI mastering engineer listens to)
    limiter->setCapturing (true, raw);
    const bool ok = renderToFile (file, 2.0, 32);
    limiter->setCapturing (false);
    return ok;
}

void AudioEngine::setMasterCompare (int mode, float afterGainDb)
{
    masterCompare = juce::jlimit (0, 2, mode);
    masterCompareDb = juce::jlimit (-40.0f, 0.0f, afterGainDb);

    if (auto limiter = dynamic_cast<MasterLimiter*> (masterLimiter.get()))
        limiter->setCompare (masterCompare, juce::jlimit (-40.0f, 0.0f, afterGainDb));
}

NovaPlugin* AudioEngine::getNova (int channelId) const
{
    return dynamic_cast<NovaPlugin*> (getInstrument (channelId));
}

void AudioEngine::applySynthPatch (int channelId, const nova::Patch& patch)
{
    auto channel = song.findChannel (channelId);

    if (channel == nullptr || channel->type != InstrumentType::synth)
        return;

    // A channel still on the old synth (or not built yet) gets Nova first
    if (getNova (channelId) == nullptr)
    {
        channel->pluginState = {};
        channel->synthPreset = patch.name;
        song.notify (Song::Change::channels);
        syncNow();
    }

    if (auto nova = getNova (channelId))
    {
        nova->applyPatch (patch);
        nova->flushPluginStateToValueTree();
        channel->synthPreset = patch.name;
        channel->pluginState = nova->state.toXmlString();

        // Already playing it: nothing to rebuild
        if (auto it = slots.find (channelId); it != slots.end())
            it->second.source = sourceKey (*channel);
    }
}

void AudioEngine::setGuitarPreset (int channelId, const juce::String& preset)
{
    auto channel = song.findChannel (channelId);

    if (channel == nullptr || ! isGuitar (*channel) || ! guitarsound::presetNames().contains (preset))
        return;

    channel->instrument = guitarsound::pathFor (preset);

    // Playing already: the new sound from its next block, without building it again
    if (auto it = slots.find (channelId); it != slots.end())
        if (auto guitar = dynamic_cast<GuitarPlugin*> (it->second.instrument.get()))
        {
            guitar->setPreset (preset);
            it->second.source = sourceKey (*channel);
        }

    song.notify (Song::Change::channels);
}

te::Plugin* AudioEngine::getInstrument (int channelId) const
{
    auto it = slots.find (channelId);
    return it != slots.end() ? it->second.instrument.get() : nullptr;
}

//==============================================================================
bool AudioEngine::renderToFile (const juce::File& file, double tailSeconds, int bitDepth)
{
    // Never the AI master's before and after: a render is the song as mastered
    struct CompareOff
    {
        AudioEngine& engine;
        int mode;
        float gainDb;
        ~CompareOff()   { engine.setMasterCompare (mode, gainDb); }
    } compareOff { *this, masterCompare, masterCompareDb };

    setMasterCompare (0, masterCompareDb);
    syncNow();

    // A stretched take still being rendered in the background is rendered now: a render never has a silent folder
    if (stretchDeferred)
    {
        const juce::ScopedValueSetter<bool> allNow (stretchAllNow, true);
        pending |= needClips;
        syncNow();
    }

    ScopedLatencyBypassLift everythingOn (*edit);

    te::Renderer::Parameters params (*edit);
    params.destFile = file;
    params.audioFormat = engine->getAudioFileFormatManager().getWavFormat();
    params.bitDepth = bitDepth == 16 || bitDepth == 32 ? bitDepth : 24;
    params.ditheringEnabled = params.bitDepth == 16;    // the quietest parts fade out smoothly, not in steps
    params.sampleRateForAudio = 48000.0;
    params.blockSizeForAudio = 512;
    params.time = te::TimeRange (te::TimePosition(), te::TimePosition::fromSeconds (ticksToSeconds (loopEndTicks) + tailSeconds));
    params.tracksToDo = te::toBitSet (te::getAllTracks (*edit));
    params.usePlugins = true;
    params.useMasterPlugins = true;
    params.canRenderInMono = false;

    file.getParentDirectory().createDirectory();
    file.deleteFile();
    const bool ok = te::Renderer::renderToFile ("Export", params).existsAsFile();

    // Rendering frees the live playback graph; bring it back for previews
    if (liveAudio)
        edit->getTransport().ensureContextAllocated();

    return ok;
}

bool AudioEngine::renderTracks (const juce::Array<te::Track*>& tracks, const juce::Array<te::EditItemID>& sources, const juce::File& file,
                                double seconds, double tail, bool loop, bool throughMaster, int bitDepth, double sampleRate)
{
    // Every track the render plays, and those of them routed into a track it doesn't play (a channel into a
    // bus): those play straight out for it, else the render would leave them out (it starts from tracks that
    // play out, and a bus brings in the channels routed into it)
    auto all = tracks;

    for (auto id : sources)
        if (auto t = te::findTrackForID (*edit, id))
            all.addIfNotAlreadyThere (t);

    std::vector<std::pair<te::AudioTrack*, te::AudioTrack*>> rerouted;

    for (auto t : all)
        if (auto at = dynamic_cast<te::AudioTrack*> (t))
            if (auto destination = at->getOutput().getDestinationTrack(); destination != nullptr && ! all.contains (destination))
            {
                rerouted.emplace_back (at, destination);
                at->getOutput().setOutputToDefaultDevice (false);
            }

    // The sources play silently: there for what they send or key, not heard themselves
    te::ScopedTrackMuter muter (*edit, sources);
    ScopedLatencyBypassLift everythingOn (*edit);

    te::Renderer::Parameters params (*edit);
    params.destFile = file;
    params.audioFormat = engine->getAudioFileFormatManager().getWavFormat();
    params.bitDepth = bitDepth == 16 || bitDepth == 32 ? bitDepth : 24;
    params.ditheringEnabled = params.bitDepth == 16;
    params.sampleRateForAudio = sampleRate;
    params.blockSizeForAudio = 512;
    params.time = te::TimeRange (te::TimePosition(), te::TimePosition::fromSeconds (seconds));
    params.endAllowance = te::TimeDuration::fromSeconds (tail);
    params.wrapRemainder = loop;
    params.tracksToDo = te::toBitSet (all);
    params.tracksToProcessWhileMuted = sources;
    params.usePlugins = true;
    params.useMasterPlugins = throughMaster;
    params.canRenderInMono = false;

    file.getParentDirectory().createDirectory();
    file.deleteFile();
    const bool ok = te::Renderer::renderToFile ("Render", params).existsAsFile();

    for (auto& [track, destination] : rerouted)
        track->getOutput().setOutputToTrack (destination);

    if (liveAudio)
        edit->getTransport().ensureContextAllocated();

    return ok;
}

std::vector<AudioEngine::Stem> AudioEngine::renderStems (const juce::File& folder, bool throughMaster, int bitDepth,
                                                         std::function<void (int, int, const juce::String&)> progress,
                                                         juce::StringArray* silent)
{
    struct CompareOff
    {
        AudioEngine& engine;
        int mode;
        float gainDb;
        ~CompareOff()   { engine.setMasterCompare (mode, gainDb); }
    } compareOff { *this, masterCompare, masterCompareDb };

    stop();
    setMasterCompare (0, masterCompareDb);
    syncNow();

    // As an export: a stretched take still being rendered in the background is rendered now, never left silent
    if (stretchDeferred)
    {
        const juce::ScopedValueSetter<bool> allNow (stretchAllNow, true);
        pending |= needClips;
        syncNow();
    }

    // What the export covers: the song (or pattern) and the same two seconds of tail, so a stem lines up
    // with the mix as well as with the others
    const double seconds = ticksToSeconds (loopEndTicks) + 2.0;

    struct Wanted { juce::String name; te::Track* track; bool bus; };
    std::vector<Wanted> wanted;

    for (auto& channel : song.channels)
    {
        // A synth plugged into the computer plays outside NightShift: nothing of it to render
        if (channel.type == InstrumentType::midiOut)
        {
            if (silent != nullptr)
                silent->add (channel.name + " (a synth plugged in, which NightShift doesn't hear)");

            continue;
        }

        if (auto it = slots.find (channel.id); it != slots.end() && it->second.track != nullptr)
            wanted.push_back ({ channel.name, it->second.track.get(), false });
    }

    for (auto& bus : song.buses)
        if (auto it = busSlots.find (bus.id); it != busSlots.end() && it->second.track != nullptr)
            wanted.push_back ({ bus.name, it->second.track.get(), true });

    folder.createDirectory();
    std::vector<Stem> written;
    juce::StringArray names;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    for (size_t i = 0; i < wanted.size(); ++i)
    {
        const auto& w = wanted[i];

        if (progress != nullptr)
            progress ((int) i, (int) wanted.size(), w.name);

        // Named after it, a bus said to be one, never two the same
        auto name = juce::File::createLegalFileName (w.name.trim());

        if (name.isEmpty())
            name = w.bus ? "Bus" : "Channel";

        if (w.bus)
            name << " (bus)";

        const auto first = name;

        for (int n = 2; names.contains (name, true); ++n)
            name = first + " " + juce::String (n);

        names.add (name);
        const auto file = folder.getChildFile (name + ".wav");

        // What it sends from or is keyed by plays along unheard; what's routed into a bus comes with it
        const auto sources = te::findStemSourceTracks (*edit, { w.track->itemID });
        bool sounds = renderTracks ({ w.track }, sources, file, seconds, 0.0, false, throughMaster, bitDepth, 48000.0);

        // Left out if it made no sound (muted, or nothing for it to play)
        if (sounds)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            juce::Range<float> levels[2];
            sounds = reader != nullptr;

            if (reader != nullptr)
            {
                reader->readMaxLevels (0, reader->lengthInSamples, levels, (int) std::min (2u, reader->numChannels));
                sounds = levels[0].getLength() > 1.0e-6f || levels[1].getLength() > 1.0e-6f;
            }
        }

        if (! sounds)
        {
            file.deleteFile();

            if (silent != nullptr)
                silent->add (w.name);

            continue;
        }

        written.push_back ({ w.name, file, w.bus });
    }

    if (progress != nullptr)
        progress ((int) wanted.size(), (int) wanted.size(), {});

    return written;
}

juce::String AudioEngine::renderMp3 (const juce::File& file, int kbps, const juce::String& title, mastering::Readings* loudness)
{
    // Rendered as a WAV export is (in float, nothing clipped before the encoder), then encoded
    stop();
    const auto wav = juce::File::createTempFile (".wav");
    struct Gone { juce::File f; ~Gone() { f.deleteFile(); } } gone { wav };

    if (! renderToFile (wav, 2.0, 32))
        return "NightShift couldn't render the song";

    if (loudness != nullptr)
        *loudness = mastering::measureFile (wav);

    return mp3::encode (wav, file, kbps, title);
}

juce::File AudioEngine::frozenFolder()
{
    // Beside the project's recordings (or where they go before it's saved, or when Windows refuses that)
    return engine->getEngineBehaviour().getDefaultFolderForAudioRecordings (*edit).getChildFile ("Frozen");
}

std::optional<Freeze> AudioEngine::renderFreeze (int channelId, juce::String& error)
{
    auto* channel = song.findChannel (channelId);
    auto it = slots.find (channelId);

    if (channel == nullptr || it == slots.end() || it->second.track == nullptr)
    {
        error = "there's no such channel";
        return {};
    }

    if (channel->type == InstrumentType::audio)
    {
        error = channel->name + " is an audio track: it plays audio already";
        return {};
    }

    if (channel->type == InstrumentType::midiOut)
    {
        error = channel->name + " plays a synth plugged into the computer, which NightShift doesn't hear, so there's nothing it can record of it";
        return {};
    }

    stop();

    // Every stretched take rendered as it's laid out, not left for later (a sidechain's key may be one)
    const juce::ScopedValueSetter<bool> allNow (stretchAllNow, true);
    auto& track = *it->second.track;
    const auto folder = frozenFolder();

    if (! folder.createDirectory())
    {
        error = "NightShift couldn't make the folder " + folder.getFullPathName();
        return {};
    }

    // Rendered from its instrument and effects (on, as a refreeze finds them off), as Song and Pattern mode
    // play them, then put back as they were
    const auto mode = song.mode;
    const int pattern = song.currentPatternId;
    const bool muted = track.isMuted (false);
    const auto wasOn = channel->freeze.on;

    channel->freeze.on = false;
    syncNow();
    syncClips (false);

    // Only what comes before its fader: the fader, pan and sends stay live on the audio, so they're out of
    // the render (and so is a mute)
    juce::Array<te::Plugin*> afterIt;

    for (auto p : track.pluginList)
        if (p->isEnabled() && (p == track.getVolumePlugin() || dynamic_cast<te::AuxSendPlugin*> (p) != nullptr))
            afterIt.add (p);

    for (auto p : afterIt)
        p->setEnabled (false);

    track.setMute (false);

    auto restore = [&]
    {
        song.mode = mode;
        song.currentPatternId = pattern;
        channel->freeze.on = wasOn;

        for (auto p : afterIt)
            p->setEnabled (true);

        track.setMute (muted);
        syncTempo();
        syncClips (false);
        syncAutomation();
    };

    const double rate = engine->getDeviceManager().getSampleRate() > 0.0 ? engine->getDeviceManager().getSampleRate() : 48000.0;
    const auto name = juce::File::createLegalFileName (channel->name.trim()).trim();
    const auto sources = te::findStemSourceTracks (*edit, { track.itemID });    // a sidechain's key, playing unheard
    Freeze made;
    made.on = true;

    auto fail = [&] (const juce::String& what)
    {
        for (auto& f : freeze::filesOf (made))
            juce::File (f).deleteFile();

        restore();
        error = what;
        return std::optional<Freeze>();
    };

    // The song: a file for each Arrange row its notes are on (each row's clips alone, every row heard), to
    // the song's end and on while anything rings, up to ten seconds
    song.mode = PlayMode::song;
    syncTempo();
    syncClips (false);
    syncAutomation();
    const double songSeconds = ticksToSeconds (std::max (song.ticksPerBar(), song.songLengthInTicks()));

    for (int row : freeze::rowsOf (song, channelId))
    {
        for (auto& [clip, clipRow] : rowClips)
            if (clip->getTrack() == &track)
                clip->setMuted (clipRow != row);

        const auto file = folder.getNonexistentChildFile ((name.isEmpty() ? juce::String ("Channel") : name) + " frozen", ".wav", false);

        if (! renderTracks ({ &track }, sources, file, songSeconds, 10.0, false, false, 32, rate))
            return fail ("NightShift couldn't render " + channel->name + " in the song");

        made.rows[row] = file.getFullPathName();
    }

    made.songStamp = freeze::songStamp (song, *channel);

    // Each pattern with its notes, as Pattern mode loops it: what rings over from its end folded onto its start
    song.mode = PlayMode::pattern;
    syncTempo();

    for (auto& p : song.patterns)
    {
        if (! freeze::hasNotes (p, channelId))
            continue;

        song.currentPatternId = p.id;
        syncClips (false);
        const auto file = folder.getNonexistentChildFile ((name.isEmpty() ? juce::String ("Channel") : name) + " frozen " + p.name.trim(), ".wav", false);

        if (! renderTracks ({ &track }, sources, file, ticksToSeconds (song.patternLength (p)), 10.0, true, false, 32, rate))
            return fail ("NightShift couldn't render " + channel->name + " in " + p.name);

        made.patterns[p.id] = file.getFullPathName();
        made.patternStamps[p.id] = freeze::patternStamp (song, *channel, p);
    }

    restore();
    return made;
}

void AudioEngine::flushPluginStates()
{
    // A plugin bypassed for low-latency monitoring must not be saved switched off
    ScopedLatencyBypassLift everythingOn (*edit);

    for (auto& channel : song.channels)
    {
        if (channel.type == InstrumentType::sampler || channel.type == InstrumentType::audio || channel.type == InstrumentType::kit
             || channel.type == InstrumentType::instrument || channel.type == InstrumentType::midiOut)
            continue;

        if (auto plugin = getInstrument (channel.id))
        {
            plugin->flushPluginStateToValueTree();

            // Saved switched on: a freeze switching it off is the freeze's to say
            auto state = plugin->state.createCopy();
            state.setProperty (te::IDs::enabled, true, nullptr);
            channel.pluginState = state.toXmlString();
        }
    }

    for (auto& [channelId, slot] : slots)
        for (auto& [id, plugin] : slot.effects)
            if (plugin != nullptr)
                keepEffectState (id, *plugin);

    for (auto& [busId, slot] : busSlots)
        for (auto& [id, plugin] : slot.effects)
            if (plugin != nullptr)
                keepEffectState (id, *plugin);

    for (auto& [id, plugin] : masterFx)
        if (plugin != nullptr)
            keepEffectState (id, *plugin);
}

} // namespace nightshift
