// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include <JuceHeader.h>

#if JUCE_WINDOWS
 #define NOMINMAX
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>     // for the window scan in anotherCopyHasWindow()
#endif

#include "audio/AudioEngine.h"
#include "model/Paths.h"
#include "audio/DrumKit.h"
#include "audio/Nova.h"
#include "audio/GuitarSound.h"
#include "audio/Mastering.h"
#include "audio/fx/StockEffects.h"
#include "audio/Kits.h"
#include "audio/Machines.h"
#include "audio/DrumSynth.h"
#include "audio/Instruments.h"
#include "audio/Sfz.h"
#include "audio/MidiFiles.h"
#include "ui/Donate.h"
#include "ai/Drummer.h"
#include "ai/Genres.h"
#include "ai/OfflineDrummer.h"
#include "ai/LearnedFeel.h"
#include "ai/VibeListener.h"
#include "ai/StyleEars.h"
#include "ai/OfflineBass.h"
#include "ai/OfflineGuitar.h"
#include "ai/OfflineKeys.h"
#include "ai/OfflineSynth.h"
#include "ai/MasteringEngineer.h"
#include "ai/SongWriter.h"
#include "ai/Theory.h"
#include "ui/AudioSettings.h"
#include "ui/MainComponent.h"
#include "ui/MetronomePanel.h"
#include "ui/ParamControls.h"
#include "ui/TextAudit.h"
#include "ui/Theme.h"
#include "ui/TrackIcons.h"

using namespace nightshift;

namespace
{
    // The tests work in 4/4, the demo song's time signature
    constexpr int ticksPerBar = ticksPerBeat * 4;

    // Lets the message loop run for at least ms, and on until everything posted before the call
    // has been delivered (a text box's Enter, a button's click, a change from elsewhere). A wait
    // of a fixed time isn't enough on a Mac: JUCE hands over a few messages per pass of the run
    // loop there, and a fresh studio has a long queue (the engine's updates), so what a test
    // posted could still be waiting when the time ran out.
    void settleMessages (int ms)
    {
        auto* mm = juce::MessageManager::getInstance();
        const auto start = juce::Time::getMillisecondCounter();
        auto delivered = std::make_shared<bool> (false);
        juce::MessageManager::callAsync ([delivered] { *delivered = true; });

        // Ten seconds at most, should something keep the queue full
        while (juce::Time::getMillisecondCounter() - start < (juce::uint32) ms
               || (! *delivered && juce::Time::getMillisecondCounter() - start < 10000))
            mm->runDispatchLoopUntil (5);
    }

    struct Args
    {
        explicit Args (const juce::String& commandLine) : tokens (juce::StringArray::fromTokens (commandLine, true)) {}

        bool has (const juce::String& flag) const       { return tokens.contains (flag); }

        juce::String valueAfter (const juce::String& flag) const
        {
            const int i = tokens.indexOf (flag);
            return i >= 0 && i + 1 < tokens.size() ? tokens[i + 1].unquoted() : juce::String();
        }

        juce::StringArray tokens;
    };

    void applySongOptions (Song& song, const Args& args)
    {
        if (auto project = args.valueAfter ("--project"); project.isNotEmpty())
            song.load (juce::File (project));
        else if (args.has ("--emptysong"))
            song.loadEmpty();
        else if (args.has ("--blanksong"))
            song.loadBlank();
        else
            song.loadDemo();

        // A track of each kind under the demo's, for their icons: audio (guitar, bass, vocals),
        // a synth, a plugin, sampled strings, brass and woodwind, a drum kit
        if (args.has ("--moretracks"))
        {
            auto track = [&song] (const juce::String& name, InstrumentType type, std::function<void (Channel&)> setUp)
            {
                auto& c = song.addChannel (name, type);
                setUp (c);
                Track t;
                t.channelId = c.id;
                song.insertTrack (t);
                return c.id;
            };

            for (auto name : { "Guitar", "Bass", "Vocals" })
                track (name, InstrumentType::audio, [] (Channel& c) { c.input = "Input 1"; });

            const int surge = track ("Surge XT", InstrumentType::plugin, [] (Channel& c) { c.pluginId = "VST3-Surge XT-1b3a5a92-abcd1234"; });
            track ("Warm Analog Pad", InstrumentType::synth, [] (Channel& c) { c.synthPreset = "Warm Analog Pad"; });
            track ("Violin Section", InstrumentType::instrument, [] (Channel& c) { c.instrument = "VSCO/Strings/ViolinEnsSusVib.sfz"; });
            track ("Trumpet", InstrumentType::instrument, [] (Channel& c) { c.instrument = "VSCO/Brass/TrumpetSus.sfz"; });
            track ("Flute", InstrumentType::instrument, [] (Channel& c) { c.instrument = "VSCO/Woodwinds/FluteSus.sfz"; });
            track ("Drums", InstrumentType::kit, [] (Channel& c) { c.kit = "Red Zeppelin"; });

            // A clip on Surge's track, in its colour
            auto& p = song.addPatternFor (surge);
            p.notesFor (surge).push_back ({ 0, ticksPerBeat * 2, 60, 100 });
            p.notesFor (surge).push_back ({ ticksPerBeat * 2, ticksPerBeat * 2, 67, 100 });

            for (int row = 0; row < song.numTracks(); ++row)
                if (song.tracks[(size_t) row].channelId == surge)
                    song.playlist.push_back ({ p.id, row, 4 * song.ticksPerBar() });
        }

        if (auto patternName = args.valueAfter ("--pattern"); patternName.isNotEmpty())
        {
            for (auto& p : song.patterns)
                if (p.name == patternName)
                    song.currentPatternId = p.id;

            song.mode = PlayMode::pattern;
        }

        if (auto channelName = args.valueAfter ("--channel"); channelName.isNotEmpty())
            for (auto& c : song.channels)
                if (c.name == channelName)
                    song.selectedChannelId = c.id;

        if (auto solo = args.valueAfter ("--solo"); solo.isNotEmpty())
            for (auto& c : song.channels)
                c.muted = c.name != solo;

        // --meter 7/8 and the like
        if (auto meter = args.valueAfter ("--meter"); meter.containsChar ('/'))
        {
            song.meterBeats = juce::jlimit (1, 32, meter.upToFirstOccurrenceOf ("/", false, false).getIntValue());
            song.meterUnit = juce::jlimit (1, 32, meter.fromFirstOccurrenceOf ("/", false, false).getIntValue());
        }

        song.notify (Song::Change::everything);
    }

    /** Tests and snapshots: the four Nightshift Lofi effects, straight from where they're
        installed, into this engine's plugin list (which a command-line run never saves). */
    void addNightshiftLofi (AudioEngine& engine)
    {
        const auto vst3Folder = paths::userVst3();
        auto& formats = engine.getPluginFormats();

        for (auto name : { "Cassette", "Crush", "Haze", "Vinyl" })
        {
            const auto bundle = vst3Folder.getChildFile (juce::String ("Nightshift ") + name + ".vst3");

            for (int i = 0; i < formats.getNumFormats(); ++i)
                if (formats.getFormat (i)->getName() == "VST3" && bundle.exists())
                {
                    juce::OwnedArray<juce::PluginDescription> found;
                    formats.getFormat (i)->findAllTypesForFile (found, bundle.getFullPathName());

                    for (auto* d : found)
                        engine.getKnownPlugins().addType (*d);
                }
        }
    }

    //==============================================================================
    /** A song for the vibe listener to hear (--vibetest), made of sines and noise so what's in it is known: each bar's
        chord held (its notes from the C an octave under middle C, a little of their octaves and fifths over them), the
        bass on its root two octaves down, and the drums (a kick's falling sine, a snare's burst of noise, a hat's short
        hiss): the kick on 1 and 3 (every beat for four on the floor), the snare on 2 and 4, the hats on the 8ths, the
        offbeat ones late by `swing` (0 straight, 1 a triplet). Each bar as loud as its gain. */
    struct TestBar { juce::String chord; float gain = 1.0f; };

    std::vector<float> testSong (double rate, double bpm, const std::vector<TestBar>& bars, float swing, bool fourOnFloor)
    {
        const double beat = 60.0 / bpm;
        const auto n = (size_t) ((double) bars.size() * 4.0 * beat * rate + rate);
        std::vector<float> x (n, 0.0f);
        juce::Random random (42);

        auto add = [&] (double from, double length, float gain, const std::function<float (double)>& wave, double decay)
        {
            const auto a = (size_t) (from * rate), count = (size_t) (length * rate);

            for (size_t i = 0; i < count && a + i < n; ++i)
            {
                const double t = (double) i / rate;
                const double shape = std::min (1.0, t / 0.004) * std::exp (-t / decay);
                x[a + i] += gain * (float) shape * wave (t);
            }
        };

        auto tone = [] (double hz) { return [hz] (double t) { return (float) (std::sin (juce::MathConstants<double>::twoPi * hz * t)
                                                                                 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 2.0 * hz * t)
                                                                                 + 0.25 * std::sin (juce::MathConstants<double>::twoPi * 3.0 * hz * t)); }; };

        for (size_t b = 0; b < bars.size(); ++b)
        {
            const double start = (double) b * 4.0 * beat;
            const auto chord = theory::parseChord (bars[b].chord);
            const float g = bars[b].gain;

            for (auto t : chord.tones)
                add (start, 4.0 * beat, 0.05f * g, tone (130.81 * std::pow (2.0, (chord.root + t) / 12.0)), 3.0);

            for (int i = 0; i < 4; ++i)
                add (start + i * beat, beat, 0.12f * g, tone (32.70 * std::pow (2.0, (chord.root + 12) / 12.0)), 0.5);

            for (int step = 0; step < 16; step += 2)
            {
                const double at = start + step * beat / 4.0 + (step % 4 == 2 ? swing * beat / 6.0 : 0.0);

                if (step % 8 == 0 || (fourOnFloor && step % 4 == 0))
                    add (at, 0.3, 0.5f * g, [] (double t) { return (float) std::sin (juce::MathConstants<double>::twoPi * (45.0 * t + 75.0 * 0.03 * (1.0 - std::exp (-t / 0.03)))); }, 0.12);

                if (step == 4 || step == 12)
                    add (at, 0.2, 0.25f * g, [&random] (double) { return random.nextFloat() * 2.0f - 1.0f; }, 0.06);

                float last = 0.0f;
                add (at, 0.06, 0.08f * g, [&random, &last] (double) { const float w = random.nextFloat() * 2.0f - 1.0f, d = w - last; last = w; return d; }, 0.015);
            }
        }

        return x;
    }

    //==============================================================================
    /** Command-line modes for checking the app without opening a window:
          --writekit <folder>                   write the built-in kit WAVs
          --render <file.wav>                   render the demo song (or --project <file>)
          --snapshot <file.png> [--show playlist,rack,piano] [--raise playlist] [--toggle rack] [--size 1600x940]
                     [--arm <channel names>] [--search <text>] [--opensounds "Plugin effects;Nightshift"] [--lofi] [--settings] [--live]
                     [--midilight <keyboard>]
          --rectest <folder> [--countin] [--monitor]   record from a virtual input and check the take's timing
          --comptest <folder> [--countin] [--monitor]  loop-record takes, comp them, check takes and joins
          --pianotest                           check which channel the piano roll edits, and its picker
          --arrangetest / --stepstest / --sidebartest  click and type through those parts of the window
          --genretest                           the genres and artists the band knows, Sleep Token played by all five, each new style by the band
          --feeltest                            the drum feel learned from real drummers (Groove MIDI Dataset): the table, who plays it, render
          --vibetest                            the vibe listener: made-up songs, the band's own (rendered), the words it writes, read back
          --listen <song> [--closeness 0..1] [--seed n] [--as style] [--features]   what it hears in a song, and the words for one like it
          --looptest / --mutetest               where loop playback starts; muting mid-chord lets go
          --fxtest                              effect plugins (the Nightshift Lofi VST3s) in channel and master chains
          --stockfxtest / --bustest / --mixertest  the stock effects measured; buses, sends, sidechain; the mixer
          --devices [--probe]                   open the interface, report latency (and per buffer size)
        --render and --snapshot also take --pattern <name>, --channel <name>, --solo <name>.
        Returns -1 when there is nothing to do on the command line.
    */
    int runCommandLine (const Args& args)
    {
        // Whatever it opens, a command-line run reads the studio's settings and never writes them
        AudioEngine::keepStudioSettings = true;

        if (args.has ("--writekit"))
            return kit::writeTo (juce::File (args.valueAfter ("--writekit"))) ? 0 : 1;

        if (args.has ("--save"))
        {
            Song song;
            applySongOptions (song, args);
            return song.save (juce::File (args.valueAfter ("--save"))) ? 0 : 1;
        }

        if (args.has ("--selftest"))
        {
            // Opens the real audio device and runs the transport with the master fully
            // down, so nothing is heard, to prove live playback starts and stops cleanly
            Song song;
            applySongOptions (song, args);
            song.masterVolume = 0.0f;
            song.notify (Song::Change::mixer);

            AudioEngine audio (song, true);
            MainComponent main (song, audio);
            main.setSize (1400, 860);

            auto& dm = audio.getEngine().getDeviceManager().deviceManager;
            auto* device = dm.getCurrentAudioDevice();
            std::cout << "device: " << (device != nullptr ? device->getTypeName() + " / " + device->getName() + " @ "
                                                            + juce::String (device->getCurrentSampleRate()) + " Hz, "
                                                            + juce::String (device->getCurrentBufferSizeSamples()) + " samples"
                                                          : juce::String ("none")) << std::endl;

            // Press play straight away, as an impatient user would (or, with --settle, after 1.5 s)
            if (args.has ("--settle"))
            {
                const auto settleStart = juce::Time::getMillisecondCounter();

                while (juce::Time::getMillisecondCounter() - settleStart < 1500)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            }

            audio.togglePlay();
            auto& transport = audio.getEdit().getTransport();
            std::cout << "after play: isPlaying=" << transport.isPlaying()
                      << " context=" << (audio.getEdit().getCurrentPlaybackContext() != nullptr)
                      << " loop=" << transport.getLoopRange().getStart().inSeconds() << ".." << transport.getLoopRange().getEnd().inSeconds()
                      << " looping=" << (bool) transport.looping.get()
                      << " waveOuts=" << audio.getEngine().getDeviceManager().getNumWaveOutDevices() << std::endl;

            const auto start = juce::Time::getMillisecondCounter();

            // Says when the transport stops and when a device change arrives: a change Windows
            // reports a second after devices were closed and reopened once stopped playback here
            struct Witness : public te::TransportControl::Listener,
                             public juce::ChangeListener
            {
                explicit Witness (juce::uint32 t) : t0 (t) {}
                void report (const char* what)  { std::cout << "  at " << (juce::Time::getMillisecondCounter() - t0) << " ms: " << what << std::endl; }
                void stopVideo() override       { report ("the transport stopped"); }
                void playbackContextChanged() override  { report ("the playback graph was rebuilt"); }
                void changeListenerCallback (juce::ChangeBroadcaster* b) override
                {
                    if (b == engineDevices && engine != nullptr)
                    {
                        juce::String ins;

                        for (int i = 0; i < engine->getNumWaveInDevices(); ++i)
                            if (auto w = engine->getWaveInDevice (i))
                                ins << w->getName() << (w->isEnabled() ? "" : " (off)") << ", ";

                        report (("the engine rebuilt its devices; inputs: " + ins).toRawUTF8());
                    }
                    else
                    {
                        report ("a device change");
                    }
                }
                juce::uint32 t0;
                juce::ChangeBroadcaster* engineDevices = nullptr;
                te::DeviceManager* engine = nullptr;
            } witness (start);

            witness.engineDevices = witness.engine = &audio.getEngine().getDeviceManager();
            transport.addListener (&witness);
            dm.addChangeListener (&witness);
            audio.getEngine().getDeviceManager().addChangeListener (&witness);

            // --seconds N watches for longer (the check stays at 2 s worth of beats)
            const auto watchMs = (juce::uint32) juce::jlimit (2000, 60000, args.valueAfter ("--seconds").getIntValue() * 1000);

            while (juce::Time::getMillisecondCounter() - start < watchMs)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

            transport.removeListener (&witness);
            dm.removeChangeListener (&witness);
            audio.getEngine().getDeviceManager().removeChangeListener (&witness);
            std::cout << "  xruns: " << dm.getXRunCount() << ", CPU " << juce::roundToInt (dm.getCpuUsage() * 100.0) << "%" << std::endl;

            const double ticks = audio.getPlayheadTicks();
            const bool playing = audio.isPlaying();
            audio.stop();

            std::cout << "playing=" << playing << " playhead=" << ticks / ticksPerBeat << " beats after 2 s (expect ~"
                      << song.bpm / 30.0 << ")" << std::endl;
            return playing && ticks > 0.0 ? 0 : 1;
        }

        if (args.has ("--rectest"))
        {
            // Records from the virtual device's first input while clicks are fed in at known
            // song times, then checks the take puts each click back exactly where it was played
            Song song;
            song.loadEmpty();
            AudioEngine audio (song, false);
            audio.syncNow();

            const auto folder = juce::File (args.valueAfter ("--rectest"));
            folder.createDirectory();
            audio.setProjectFile (folder.getChildFile ("rectest.nsproj"));

            const auto inputs = audio.getInputNames();
            std::cout << "inputs: " << inputs.joinIntoString (", ") << std::endl;

            if (inputs.isEmpty())
                return 1;

            auto& guitar = song.addChannel ("Guitar", InstrumentType::audio);
            guitar.input = inputs[0];
            guitar.armed = true;
            guitar.monitor = args.has ("--monitor");    // bypasses the limiter's lookahead
            song.notify (Song::Change::channels);

            auto options = audio.getOptions();
            options.countInBars = args.has ("--countin") ? 1 : 0;
            audio.setOptions (options);
            song.mode = PlayMode::song;
            song.notify (Song::Change::transport);
            audio.syncNow();
            audio.setPlayheadTicks (ticksPerBar);     // start the take at bar 2

            if (auto problem = audio.toggleRecord(); problem.isNotEmpty())
            {
                std::cout << "record failed: " << problem << std::endl;
                return 1;
            }

            const double sr = 48000.0;
            const int block = 512;
            juce::AudioBuffer<float> buffer (2, block);
            std::vector<double> playedAt;               // song time of each click, in seconds
            int64_t fed = 0;
            int graphLatency = 0;

            for (int b = 0; b < (int) (sr * 4.5) / block; ++b)
            {
                buffer.clear();

                // The playhead itself, not the transport's cached copy, which lags behind
                auto context = audio.getEdit().getCurrentPlaybackContext();
                const double blockTime = context != nullptr ? context->getPosition().inSeconds() : 0.0;
                graphLatency = context != nullptr ? context->getLatencySamples() : 0;

                // A click every 0.37 s of fed audio, once the take is under way
                for (int i = 0; i < block; ++i, ++fed)
                {
                    if (fed % 17760 == 8000 && blockTime > 0.0)
                    {
                        buffer.setSample (0, i, 0.8f);
                        playedAt.push_back (blockTime + i / sr);
                    }
                }

                audio.processHostedBlock (buffer);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
            }

            const double recordStart = song.bpm > 0.0 ? ticksPerBar / (double) ticksPerBeat * 60.0 / song.bpm : 0.0;
            audio.toggleRecord();

            for (int i = 0; i < 30; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            std::cout << "takes: " << song.audioClips.size() << std::endl;

            if (auto notice = audio.takeRecordingFolderNotice(); notice.isNotEmpty())
                std::cout << "notice: " << notice.replace ("\n", " ") << std::endl;

            if (song.audioClips.size() != 1)
                return 1;

            const auto& takeFolder = song.audioClips.front();
            const auto& take = takeFolder.takes.front();
            const double clipStart = song.secondsAt (takeFolder.start);
            std::cout << "take: " << take.file << "  starts " << clipStart << " s (record start " << recordStart
                      << " s), offset " << take.fileOffset << " s, length " << takeFolder.length << " s, row " << takeFolder.track << std::endl;

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (take.file)));

            if (reader == nullptr)
                return 1;

            juce::AudioBuffer<float> recorded ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&recorded, 0, (int) reader->lengthInSamples, 0, true, true);

            // Each click heard in the file, placed on the song timeline the way playback places it
            std::vector<double> heardAt;

            for (int i = 0; i < recorded.getNumSamples(); ++i)
                if (std::abs (recorded.getSample (0, i)) > 0.4f)
                    heardAt.push_back (clipStart + i / reader->sampleRate - take.fileOffset);

            // Plugins that look ahead (the master limiter) make the backing reach your ears
            // late; playing in time with what you hear puts you that late against the
            // playhead, and the take is moved back by exactly that much
            std::cout << "backing heard " << graphLatency << " samples late (plugin lookahead); takes shift back by that" << std::endl;
            std::cout << "bypassed while monitoring: " << audio.getLatencyBypassedPlugins().joinIntoString (", ") << std::endl;

            double worst = 0.0;
            const size_t n = std::min (playedAt.size(), heardAt.size());

            for (size_t i = 0; i < n; ++i)
            {
                // Clicks before the clip starts were trimmed off; match from the end
                const double error = heardAt[heardAt.size() - n + i] - playedAt[playedAt.size() - n + i] + graphLatency / sr;
                worst = std::max (worst, std::abs (error));
                std::cout << "click " << i << ": played " << juce::String (playedAt[playedAt.size() - n + i], 5)
                          << " s, lands " << juce::String (heardAt[heardAt.size() - n + i], 5)
                          << " s, error " << juce::String (error * sr, 1) << " samples" << std::endl;
            }

            const bool ok = n >= 3 && worst * sr <= 1.0;
            std::cout << (ok ? "PASS" : "FAIL") << "  " << n << " clicks, worst error " << juce::String (worst * sr, 1) << " samples" << std::endl;
            return ok ? 0 : 1;
        }

        if (args.has ("--comptest"))
        {
            // Loop-records three and a half passes of a known signal (a different pitch each
            // pass, restarting in phase, with a click half a second in), then checks that each
            // pass became a take lined up with the loop to the sample, that the latest
            // complete pass is the comp, that a comp of two takes plays each where it should
            // without a click at the join, and that recording over the folder adds to it
            Song song;
            song.loadEmpty();
            AudioEngine audio (song, false);
            audio.syncNow();

            const auto folderOut = juce::File (args.valueAfter ("--comptest"));
            folderOut.createDirectory();
            audio.setProjectFile (folderOut.getChildFile ("comptest.nsproj"));

            const auto inputs = audio.getInputNames();

            if (inputs.isEmpty())
                return 1;

            auto& guitar = song.addChannel ("Guitar", InstrumentType::audio);
            guitar.input = inputs[0];
            guitar.armed = true;
            guitar.monitor = args.has ("--monitor");
            const int guitarId = guitar.id;

            auto options = audio.getOptions();
            options.countInBars = args.has ("--countin") ? 1 : 0;
            options.metronome = false;
            audio.setOptions (options);

            song.mode = PlayMode::song;
            song.loopStart = ticksPerBar;
            song.loopEnd = 3 * ticksPerBar;
            song.loopOn = true;
            song.notify (Song::Change::channels);
            song.notify (Song::Change::transport);
            audio.syncNow();

            const double sr = 48000.0, loopStart = song.secondsAt (song.loopStart);
            const double loopLength = song.secondsAt (song.loopEnd) - loopStart;
            const double pitches[] = { 300.0, 500.0, 700.0, 900.0, 1100.0 };
            int failures = 0;

            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            // Plays `seconds` of the take into the virtual input, in time with what is heard:
            // the backing reaches the player late by the graph's latency, so they are too
            auto perform = [&] (double untilSongTime, std::function<float (double musicalTime)> signal)
            {
                juce::AudioBuffer<float> buffer (2, 512);

                for (int guard = 0; guard < 4000; ++guard)
                {
                    auto context = audio.getEdit().getCurrentPlaybackContext();
                    const double now = context != nullptr ? context->getUnloopedPosition().inSeconds() : 0.0;

                    if (now >= untilSongTime)
                        break;

                    const double lag = context != nullptr ? context->getLatencySamples() / sr : 0.0;
                    buffer.clear();

                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (0, i, signal (now + i / sr - lag));

                    audio.processHostedBlock (buffer);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
                }
            };

            auto passSignal = [&] (double musical)
            {
                if (musical < loopStart)
                    return 0.0f;

                const int pass = (int) std::floor ((musical - loopStart) / loopLength);
                const double within = musical - loopStart - pass * loopLength;
                float v = 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * pitches[std::min (pass, 3)] * within);

                if (within >= 0.5 && within < 0.5 + 1.0 / sr)
                    v += 0.8f;

                return v;
            };

            if (auto problem = audio.toggleRecord(); problem.isNotEmpty())
            {
                std::cout << "record failed: " << problem << std::endl;
                return 1;
            }

            perform (loopStart + 3.5 * loopLength, passSignal);
            audio.toggleRecord();

            for (int i = 0; i < 30; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            check (song.audioClips.size() == 1, "one take folder (" + juce::String ((int) song.audioClips.size()) + ")");

            if (song.audioClips.size() != 1)
                return 1;

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            auto readFile = [&formats] (const juce::String& path)
            {
                juce::AudioBuffer<float> b;

                if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (juce::File (path)) })
                {
                    b.setSize (1, (int) reader->lengthInSamples);
                    reader->read (&b, 0, (int) reader->lengthInSamples, 0, true, false);
                }

                return b;
            };

            auto pitchOf = [] (const juce::AudioBuffer<float>& b, int from, int to)
            {
                int crossings = 0;

                for (int i = std::max (1, from); i < std::min (to, b.getNumSamples()); ++i)
                    if ((b.getSample (0, i - 1) < 0.0f) != (b.getSample (0, i) < 0.0f))
                        ++crossings;

                return crossings / 2.0 / ((to - from) / 48000.0);
            };

            {
                const auto& folder = song.audioClips.front();
                check (folder.start == song.loopStart && std::abs (folder.length - loopLength) < 1.0e-6,
                       "folder covers the loop (starts at tick " + juce::String (folder.start) + ", " + juce::String (folder.length, 4) + " s)");
                check (folder.takes.size() == 4, "3 full passes + the half pass = 4 takes (" + juce::String ((int) folder.takes.size()) + ")");
                check (folder.comp.size() == 1 && folder.comp[0].take == 2 && folder.comp[0].from == 0.0
                         && std::abs (folder.comp[0].to - loopLength) < 1.0e-6,
                       "the comp is the latest full pass, take 3");

                const auto file = readFile (folder.takes.front().file);

                for (size_t k = 0; k < folder.takes.size(); ++k)
                {
                    const auto& take = folder.takes[k];
                    const int expected = juce::roundToInt ((take.fileOffset + 0.5) * sr);
                    int found = -1;

                    for (int i = std::max (0, expected - 2000); i < std::min (file.getNumSamples(), expected + 2000); ++i)
                        if (file.getSample (0, i) > 0.6f)
                            found = i;

                    const double hz = pitchOf (file, juce::roundToInt ((take.fileOffset + 0.6) * sr), juce::roundToInt ((take.fileOffset + std::min (take.to, 1.8)) * sr));
                    check (found >= 0 && std::abs (found - expected) <= 1 && std::abs (hz - pitches[k]) < 3.0,
                           "take " + juce::String ((int) k + 1) + ": click " + (found < 0 ? juce::String ("missing") : juce::String (found - expected) + " samples off")
                             + ", pitch " + juce::String (hz, 1) + " Hz (played " + juce::String (pitches[k], 0) + "), audio "
                             + juce::String (take.from, 3) + ".." + juce::String (take.to, 3) + " s");
                }
            }

            // A comp: take 1 up to 1.6 s, take 2 after it
            {
                song.checkpoint();
                auto& folder = song.audioClips.front();
                folder.setComp (0.0, 1.6, 0);
                folder.setComp (1.6, loopLength, 1);
                song.notify (Song::Change::playlist);
                audio.syncNow();

                const auto rendered = folderOut.getChildFile ("comp.wav");
                audio.renderToFile (rendered, 0.5);
                const auto mix = readFile (rendered.getFullPathName());
                const int join = juce::roundToInt ((loopStart + 1.6) * sr);

                const double before = pitchOf (mix, juce::roundToInt ((loopStart + 0.7) * sr), juce::roundToInt ((loopStart + 1.5) * sr));
                const double after = pitchOf (mix, juce::roundToInt ((loopStart + 1.75) * sr), juce::roundToInt ((loopStart + 3.5) * sr));
                check (std::abs (before - 300.0) < 3.0 && std::abs (after - 500.0) < 3.0,
                       "comp plays take 1 (" + juce::String (before, 1) + " Hz) then take 2 (" + juce::String (after, 1) + " Hz)");

                auto maxStep = [&mix] (int from, int to)
                {
                    float m = 0.0f;

                    for (int i = std::max (1, from); i < std::min (to, mix.getNumSamples()); ++i)
                        m = std::max (m, std::abs (mix.getSample (0, i) - mix.getSample (0, i - 1)));

                    return m;
                };

                const float steady = maxStep (juce::roundToInt ((loopStart + 2.0) * sr), juce::roundToInt ((loopStart + 3.0) * sr));
                const float atJoin = maxStep (join - 720, join + 720);
                check (atJoin <= steady * 1.15f, "no click at the join: largest step " + juce::String (atJoin, 4)
                                                   + " vs " + juce::String (steady, 4) + " in steady 500 Hz");

                // Equal-power crossfade: the level holds through the join
                float lo = 1.0f, hi = 0.0f;

                for (int w = join - 960; w < join + 960; w += 240)
                {
                    double sum = 0.0;

                    for (int i = w; i < w + 480; ++i)
                        sum += mix.getSample (0, i) * mix.getSample (0, i);

                    const float rms = (float) std::sqrt (sum / 480.0);
                    lo = std::min (lo, rms);
                    hi = std::max (hi, rms);
                }

                check (juce::Decibels::gainToDecibels (hi / std::max (lo, 1.0e-6f)) < 3.0f,
                       "level through the join within 3 dB (" + juce::String (juce::Decibels::gainToDecibels (hi / std::max (lo, 1.0e-6f)), 2) + " dB)");
            }

            // Recording over part of it again, without the loop, adds a take to the folder
            {
                song.loopOn = false;
                song.notify (Song::Change::transport);
                audio.syncNow();
                audio.setPlayheadTicks (song.loopStart + ticksPerBeat);

                const double punch = song.secondsAt (song.loopStart + ticksPerBeat);

                if (auto problem = audio.toggleRecord(); problem.isNotEmpty())
                {
                    std::cout << "record failed: " << problem << std::endl;
                    return 1;
                }

                perform (punch + 1.5, [&] (double musical)
                {
                    return musical < punch ? 0.0f : 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * pitches[4] * (musical - punch));
                });

                audio.toggleRecord();

                for (int i = 0; i < 30; ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

                const auto& folder = song.audioClips.front();
                const double into = punch - song.secondsAt (folder.start);
                const int lastTake = (int) folder.takes.size() - 1;
                const bool compHasIt = std::any_of (folder.comp.begin(), folder.comp.end(), [&] (auto& s)
                                                    { return s.take == lastTake && std::abs (s.from - into) < 0.002; });

                check (song.audioClips.size() == 1 && folder.takes.size() == 5 && compHasIt && folder.takeAt (into + 0.5) == lastTake
                         && folder.takeAt (into - 0.1) == 0,
                       "punching in again adds take 5 to the folder and the comp uses it from " + juce::String (into, 3) + " s ("
                         + juce::String ((int) song.audioClips.size()) + " folder(s), " + juce::String ((int) folder.takes.size()) + " takes)");
                check (song.findChannel (guitarId) != nullptr, "channel intact");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--swipetest"))
        {
            // Mouse gestures on the playlist, sent as real mouse events: swipe across a take
            // lane, click a lane, drag a loop in the ruler, click the loop on and off, and
            // open and close a folder. Needs a project whose first audio clip is a folder.
            Song song;
            applySongOptions (song, args);
            AudioEngine audio (song, false);
            Playlist playlist (song, audio);
            playlist.setSize (1600, 800);

            if (song.audioClips.empty() || song.audioClips.front().takes.size() < 3)
                return 1;

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto source = juce::Desktop::getInstance().getMainMouseSource();
            auto gesture = [&] (juce::Point<float> from, juce::Point<float> to)
            {
                const auto now = juce::Time::getCurrentTime();
                const auto mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
                auto event = [&] (juce::Point<float> p, bool dragged)
                {
                    return juce::MouseEvent (source, p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &playlist, &playlist, now, from, now, 1, dragged);
                };

                playlist.mouseDown (event (from, false));

                if (from != to)
                    for (int i = 1; i <= 8; ++i)
                        playlist.mouseDrag (event (from + (to - from) * (i / 8.0f), true));

                playlist.mouseUp (event (to, from != to));
            };

            // The default layout: no scroll, 72 px a bar
            const float pxPerTick = 72.0f / ticksPerBar;
            auto& folder = song.audioClips.front();
            auto xAt = [&] (double seconds) { return Playlist::headerWidth + (float) (song.tickAt (song.secondsAt (folder.start) + seconds) * pxPerTick); };
            auto laneY = [&] (int lane) { return (float) (Playlist::rulerHeight + folder.track * Playlist::rowHeight + Playlist::rowHeight
                                                          + lane * Playlist::laneHeight + Playlist::laneHeight / 2); };

            folder.expanded = true;
            folder.comp = { { 0.0, folder.length, 0 } };
            song.notify (Song::Change::playlist);

            gesture ({ xAt (0.5), laneY (2) }, { xAt (2.0), laneY (2) });
            check (folder.takeAt (1.0) == 2 && folder.takeAt (0.3) == 0 && folder.takeAt (2.5) == 0,
                   "swiping take 3 from 0.5 s to 2 s puts it in the comp there and nowhere else");

            gesture ({ xAt (1.0), laneY (1) }, { xAt (1.0), laneY (1) });
            check (folder.comp.size() == 1 && folder.comp[0].take == 1, "clicking take 2 uses all of it");

            const auto rulerY = (float) Playlist::loopStripHeight / 2;
            gesture ({ Playlist::headerWidth + ticksPerBar * pxPerTick, rulerY }, { Playlist::headerWidth + 3 * ticksPerBar * pxPerTick, rulerY });
            check (song.loopOn && song.loopStart == ticksPerBar && song.loopEnd == 3 * ticksPerBar, "dragging the ruler's top strip sets a loop over bars 2-3");

            gesture ({ Playlist::headerWidth + 2 * ticksPerBar * pxPerTick, rulerY }, { Playlist::headerWidth + 2 * ticksPerBar * pxPerTick, rulerY });
            check (! song.loopOn && song.loopEnd > song.loopStart, "clicking the loop switches it off and keeps it");

            // The arrow sits just in from the corner (the corner is the fade-in's dot)
            const float headerY = (float) (Playlist::rulerHeight + folder.track * Playlist::rowHeight + 8);
            gesture ({ xAt (0.0) + 22.0f, headerY }, { xAt (0.0) + 22.0f, headerY });
            check (! folder.expanded, "the arrow closes the folder");

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--fxstoptest"))
        {
            // Noise the effects make up themselves (Cassette's hiss on a channel, Vinyl's surface
            // on the master, both turned right up) must start and stop with the song: a silent
            // song, stopped, then playing, then stopped again
            Song song;
            song.loadEmpty();
            AudioEngine audio (song, false);
            addNightshiftLofi (audio);
            auto& pad = song.addChannel ("Pad", InstrumentType::synth);
            pad.synthPreset = nova::defaultPreset();
            const int padId = pad.id;
            song.notify (Song::Change::channels);
            audio.syncNow();

            auto add = [&] (std::vector<Effect>& chain, const juce::String& name)
            {
                Effect e;
                e.id = song.createId();
                e.name = name;

                for (auto& d : audio.getKnownPlugins().getTypes())
                    if (d.name == name)
                        e.pluginId = d.createIdentifierString();

                chain.push_back (e);
                return e.id;
            };

            const int cassette = add (song.findChannel (padId)->effects, "Nightshift Cassette");
            const int vinyl = add (song.masterEffects, "Nightshift Vinyl");
            song.notify (Song::Change::mixer);
            audio.syncNow();

            auto turnUp = [&audio] (int effectId, const juce::String& paramName)
            {
                if (auto p = audio.getEffect (effectId))
                    for (auto param : p->getAutomatableParameters())
                        if (param->getParameterName() == paramName)
                        {
                            param->setParameter (1.0f, juce::sendNotificationSync);
                            return true;
                        }

                return false;
            };

            const bool ready = turnUp (cassette, "Hiss") && turnUp (vinyl, "Noise");
            std::cout << "      Cassette hiss and Vinyl noise at their loudest: " << (ready ? "yes" : "no") << std::endl;

            const double sr = 48000.0;
            const int block = 512;
            juce::AudioBuffer<float> buffer (2, block);

            // The loudest the output gets over the last `measure` seconds of `seconds`
            auto listen = [&] (double seconds, double measure)
            {
                float peak = 0.0f;
                const int blocks = (int) (sr * seconds) / block, from = blocks - (int) (sr * measure) / block;

                for (int b = 0; b < blocks; ++b)
                {
                    buffer.clear();
                    audio.processHostedBlock (buffer);

                    if (b % 64 == 0)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

                    if (b >= from)
                        peak = std::max (peak, buffer.getMagnitude (0, block));
                }

                return juce::Decibels::gainToDecibels (peak, -150.0f);
            };

            const float stoppedDb = listen (1.5, 1.0);
            audio.togglePlay();
            const float playingDb = listen (1.5, 1.0);
            audio.stop();
            const float afterDb = listen (1.0, 0.6);

            std::cout << "      stopped " << juce::String (stoppedDb, 1) << " dB, playing " << juce::String (playingDb, 1)
                      << " dB, stopped again " << juce::String (afterDb, 1) << " dB" << std::endl;

            int failures = ready ? 0 : 1;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            check (stoppedDb < -100.0f, "stopped, the effects make no noise");
            check (playingDb > -70.0f, "playing, the hiss and surface noise are there");
            check (afterDb < -100.0f, "stopped again, they fall silent");
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--savetest"))
        {
            // The studio's own settings path, under a throwaway name and with no audio device:
            // a plugin added to the list must reach the settings file on disk
            struct NoDevices : public te::EngineBehaviour
            {
                bool autoInitialiseDeviceManager() override    { return false; }
            };

            const auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("NightshiftSaveProbe");
            folder.deleteRecursively();
            bool listed = false, onDisk = false;

            {
                te::Engine probe (std::make_unique<te::PropertyStorage> ("NightshiftSaveProbe"), std::make_unique<te::UIBehaviour>(),
                                  std::make_unique<NoDevices>());
                juce::PluginDescription desc;
                desc.name = "Save Probe";
                desc.pluginFormatName = "VST3";
                desc.fileOrIdentifier = "C:\\nowhere\\Save Probe.vst3";
                desc.uniqueId = 1234;
                probe.getPluginManager().knownPluginList.addType (desc);

                for (int i = 0; i < 30; ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

                listed = probe.getPropertyStorage().getXmlProperty (te::SettingID::knownPluginList64) != nullptr;
                onDisk = folder.getChildFile ("Settings.xml").loadFileAsString().contains ("Save Probe");
            }

            std::cout << "in the settings: " << (listed ? "yes" : "no") << ", on disk after 3 s: " << (onDisk ? "yes" : "no") << std::endl;
            folder.deleteRecursively();
            std::cout << (listed && onDisk ? "PASS" : "FAIL") << "  a plugin added to the list reaches the settings file" << std::endl;

            // Nightshift's own copy: written and read back the way the studio does it
            juce::KnownPluginList list, back;
            juce::PluginDescription desc;
            desc.name = "Save Probe";
            desc.pluginFormatName = "VST3";
            desc.fileOrIdentifier = "C:\\nowhere\\Save Probe.vst3";
            desc.uniqueId = 1234;
            list.addType (desc);
            const auto file = juce::File::createTempFile (".xml");
            const bool written = AudioEngine::savePluginList (list, file);
            const bool read = AudioEngine::loadPluginList (back, file);
            file.deleteFile();
            const bool same = back.getNumTypes() == 1 && back.getTypes()[0].name == "Save Probe";
            std::cout << (written && read && same ? "PASS" : "FAIL") << "  Nightshift's own plugin list file is written and read back" << std::endl;

            return listed && onDisk && written && read && same ? 0 : 1;
        }

        if (args.has ("--fxtest"))
        {
            // Effect plugins with the real Nightshift Lofi VST3s: where they sit in each chain,
            // that they change the sound, bypass, order, removal, and that a setting survives
            // saving and loading into a fresh engine
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            Song song;
            song.loadDemo();
            auto audio = std::make_unique<AudioEngine> (song, false);
            audio->syncNow();
            ChannelRack rack (song, *audio);

            // The scanned list may not be saved yet (the studio writes it when a scan ends), so
            // load the four straight from where they're installed; a test engine never saves it
            addNightshiftLofi (*audio);

            auto findPlugin = [&audio] (const juce::String& name) -> std::unique_ptr<juce::PluginDescription>
            {
                for (auto& d : audio->getKnownPlugins().getTypes())
                    if (d.name == name)
                        return std::make_unique<juce::PluginDescription> (d);

                return nullptr;
            };

            auto crush = findPlugin ("Nightshift Crush"), haze = findPlugin ("Nightshift Haze"), vinyl = findPlugin ("Nightshift Vinyl");
            check (crush != nullptr && haze != nullptr && vinyl != nullptr, "the Nightshift effects are in the scanned plugin list");

            if (crush == nullptr || haze == nullptr || vinyl == nullptr)
                return 1;

            auto channelId = [&song] (const juce::String& name) { for (auto& c : song.channels) if (c.name == name) return c.id; return 0; };
            auto pluginsOn = [&audio] (const juce::String& trackName)
            {
                juce::StringArray names;

                for (auto t : te::getAudioTracks (audio->getEdit()))
                    if (t->getName() == trackName)
                        for (auto p : t->pluginList.getPlugins())
                            names.add (p->getName());

                return names;
            };

            // On an instrument: straight after it
            const int kick = channelId ("Kick");
            const int crushId = rack.addEffect (kick, *crush);
            audio->syncNow();
            auto chain = pluginsOn ("Kick");
            std::cout << "      Kick: " << chain.joinIntoString (" > ") << std::endl;
            check (chain.size() > 2 && chain[1] == "Nightshift Crush" && audio->getEffect (crushId) != nullptr,
                   "Crush goes on Kick straight after its sampler");

            // It changes the sound: Kick alone through the pattern, Crush on and bypassed
            for (auto& c : song.channels)
                c.muted = c.id != kick;

            song.notify (Song::Change::mixer);
            auto plugin = audio->getEffect (crushId);

            if (plugin != nullptr)
                for (auto param : plugin->getAutomatableParameters())
                    if (param->getParameterName().containsIgnoreCase ("bits"))
                        param->setParameter (0.15f, juce::sendNotificationSync);     // few bits: plainly crushed

            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-fxtest");
            folder.createDirectory();
            const auto wet = folder.getChildFile ("wet.wav"), dry = folder.getChildFile ("dry.wav");
            audio->renderToFile (wet, 0.5);
            song.findEffect (crushId)->bypassed = true;
            song.notify (Song::Change::mixer);
            audio->syncNow();
            check (plugin != nullptr && ! plugin->isEnabled(), "bypassing it switches the plugin off");
            audio->renderToFile (dry, 0.5);

            {
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> a (formats.createReaderFor (wet)), b (formats.createReaderFor (dry));
                double diff = 0.0, level = 0.0;

                if (a != nullptr && b != nullptr)
                {
                    const int n = (int) std::min (a->lengthInSamples, b->lengthInSamples);
                    juce::AudioBuffer<float> x (2, n), y (2, n);
                    a->read (&x, 0, n, 0, true, true);
                    b->read (&y, 0, n, 0, true, true);

                    for (int i = 0; i < n; ++i)
                    {
                        diff += std::pow (x.getSample (0, i) - y.getSample (0, i), 2.0);
                        level += std::pow (y.getSample (0, i), 2.0);
                    }
                }

                const double ratioDb = 10.0 * std::log10 ((diff + 1.0e-20) / (level + 1.0e-20));
                std::cout << "      crushed minus clean: " << juce::String (ratioDb, 1) << " dB relative to the clean kick" << std::endl;
                check (level > 0.0 && ratioDb > -30.0, "with Crush on, the kick sounds different from bypassed");
            }

            folder.deleteRecursively();
            song.findEffect (crushId)->bypassed = false;

            // Order and removal
            const int hazeId = rack.addEffect (kick, *haze);
            audio->syncNow();
            check (pluginsOn ("Kick")[2] == "Nightshift Haze", "Haze goes after Crush");
            auto& kickFx = song.findChannel (kick)->effects;
            std::swap (kickFx[0], kickFx[1]);
            song.notify (Song::Change::mixer);
            audio->syncNow();
            chain = pluginsOn ("Kick");
            check (chain[1] == "Nightshift Haze" && chain[2] == "Nightshift Crush", "moving Haze up puts it before Crush");
            kickFx.erase (kickFx.begin());
            song.notify (Song::Change::mixer);
            audio->syncNow();
            check (! pluginsOn ("Kick").contains ("Nightshift Haze") && audio->getEffect (hazeId) == nullptr, "removing Haze takes it off the track");

            // An audio track has no instrument: its effects come first, straight off the input
            auto& guitar = song.addChannel ("Guitar", InstrumentType::audio);
            const int guitarId = guitar.id;
            song.notify (Song::Change::channels);
            rack.addEffect (guitarId, *haze);
            audio->syncNow();
            check (pluginsOn ("Guitar")[0] == "Nightshift Haze", "on an audio track, Haze comes first");

            // The master: before the limiter
            const int vinylId = rack.addEffect (0, *vinyl);
            audio->syncNow();
            juce::StringArray master;

            for (auto p : audio->getEdit().getMasterPluginList().getPlugins())
                master.add (p->getName());

            std::cout << "      master: " << master.joinIntoString (" > ") << std::endl;
            check (song.masterEffects.size() == 1 && master[0] == "Nightshift Vinyl" && audio->getEffect (vinylId) != nullptr,
                   "Vinyl on the master goes before its limiter");

            // The Inspector: Kick's strip on the left with Crush in it, the master's on the right
            // with Vinyl; a click on a slot opens that effect, and its light switches it off
            {
                song.selectedChannelId = kick;
                song.notify (Song::Change::selection);
                Inspector inspector (song, *audio, rack);
                int opened = 0;
                inspector.getChannelStrip().onOpenEffect = [&opened] (int id) { opened = id; };
                inspector.getMasterStrip().onOpenEffect = [&opened] (int id) { opened = id; };
                inspector.setSize (Inspector::width, 900);
                inspector.setVisible (true);

                auto clickOn = [] (ChannelStrip& strip, juce::Rectangle<int> area)
                {
                    const auto p = area.getCentre().toFloat();
                    const auto now = juce::Time::getCurrentTime();
                    const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), p,
                                              juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                              &strip, &strip, now, p, now, 1, false);
                    strip.mouseDown (e);
                };

                auto& kickStrip = inspector.getChannelStrip();
                auto& masterStrip = inspector.getMasterStrip();
                const auto crushSlot = kickStrip.getSlotArea (crushId), vinylSlot = masterStrip.getSlotArea (vinylId);
                check (kickStrip.getChannel() == kick && ! crushSlot.isEmpty() && ! vinylSlot.isEmpty()
                         && kickStrip.getSlotArea (vinylId).isEmpty() && masterStrip.getSlotArea (crushId).isEmpty(),
                       "the Inspector shows Crush on Kick's strip and Vinyl on the master's");

                clickOn (kickStrip, crushSlot);
                const bool crushOpened = opened == crushId;
                clickOn (masterStrip, vinylSlot);
                check (crushOpened && opened == vinylId, "clicking a slot opens that effect");

                clickOn (kickStrip, kickStrip.getLightArea (crushId));
                audio->syncNow();
                const bool off = song.findEffect (crushId)->bypassed && ! audio->getEffect (crushId)->isEnabled();
                check (off && ! kickStrip.getSlotArea (crushId).isEmpty(), "its light switches Crush off, and it keeps its slot");
                clickOn (kickStrip, kickStrip.getLightArea (crushId));
                audio->syncNow();
                check (! song.findEffect (crushId)->bypassed && audio->getEffect (crushId)->isEnabled(), "and on again");
            }

            // A setting survives saving and loading
            float setTo = -1.0f;

            if (auto p = audio->getEffect (crushId))
                if (auto param = p->getAutomatableParameters()[0])
                {
                    param->setParameter (0.37f, juce::sendNotificationSync);
                    setTo = param->getCurrentValue();
                }

            audio->flushPluginStates();
            const auto saved = song.toVar();
            audio.reset();

            // A fresh engine knows the plugins before the song arrives, as the studio does
            // from its saved list
            Song reloaded;
            AudioEngine fresh (reloaded, false);
            addNightshiftLofi (fresh);
            reloaded.fromVar (saved);
            reloaded.notify (Song::Change::everything);
            fresh.syncNow();
            float readBack = -2.0f;

            if (auto p = fresh.getEffect (crushId))
                if (auto param = p->getAutomatableParameters()[0])
                    readBack = param->getCurrentValue();

            check (reloaded.findEffect (crushId) != nullptr && reloaded.masterEffects.size() == 1 && std::abs (readBack - setTo) < 0.001f,
                   "after saving and loading, Crush comes back with its setting (" + juce::String (setTo, 3) + " -> " + juce::String (readBack, 3) + ")");

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--genretest"))
        {
            // What the band knows beyond its styles (ai/Genres.h): genres and artists read from a request,
            // each played as the style nearest it with its feel on top; how sections are played ("soft
            // verses, a heavy half-time chorus"), heard in every musician's part; the blues' twelve bars
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            using agents::Request;

            {
                juce::StringArray bad;

                for (auto& g : agents::genres())
                    if (agents::findStyle (g.plays) == nullptr)
                        bad.add (g.id);

                for (auto& a : agents::artists())
                    if (agents::findGenre (a.genre) == nullptr && agents::findStyle (a.genre) == nullptr)
                        bad.add (a.name);

                check (bad.isEmpty(), juce::String ((int) agents::genres().size()) + " genres and " + juce::String ((int) agents::artists().size())
                                        + " artists, each played as a style the band has" + (bad.isEmpty() ? juce::String() : ": not " + bad.joinIntoString (", ")));
            }

            {
                juce::StringArray wrong;
                int words = 0;

                for (auto& g : agents::genres())
                    for (auto& w : g.words)
                    {
                        ++words;
                        const auto r = Request::parse ("write a " + w + " song");

                        if (r.genre != g.id || r.style != g.plays)
                            wrong.add (w + " -> " + r.genre + "/" + r.style);
                    }

                check (wrong.isEmpty(), "each of the genres' " + juce::String (words) + " names finds it, played as its style"
                                          + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")));
            }

            {
                juce::StringArray wrong;
                int names = 0;

                for (auto& a : agents::artists())
                {
                    juce::StringArray all { a.name };
                    all.addArray (a.words);

                    for (auto& n : all)
                    {
                        ++names;
                        const auto r = Request::parse ("make a " + n + " style song");

                        if (r.artist != juce::String (a.name))
                            wrong.add (n + " -> " + (r.artist.isNotEmpty() ? r.artist : "nobody"));
                    }
                }

                check (wrong.isEmpty(), "each of the artists' " + juce::String (names) + " names finds it (\"<name> style\")"
                                          + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")));
            }

            {
                // One-word names that are words too only count beside one that says so
                const auto death = Request::parse ("a sad song about death");
                const auto ride = Request::parse ("busier, more ride");
                const auto ghost = Request::parse ("metalcore with ghost notes on the snare");
                const auto ghostly = Request::parse ("like ghost");
                const auto future = Request::parse ("a future type beat");
                check (death.artist.isEmpty() && ride.artist.isEmpty() && ghost.artist.isEmpty() && ghost.style == "metalcore" && ghost.ghostNotes
                         && ghostly.artist == "Ghost" && future.artist == "Future",
                       "\"about death\", \"more ride\" and \"ghost notes\" name nobody; \"like Ghost\" and \"a Future type beat\" do");

                const auto accented = Request::parse (juce::String (juce::CharPointer_UTF8 ("something like Beyonc\xc3\xa9")));
                check (accented.artist == "Beyonce" && accented.genre == "rnb", "an accent typed or not: Beyonce with an e acute is Beyonce (" + accented.artist + ")");
            }

            // Sleep Token: alternative metal, played as djent, its piano and soul chords, quiet verses
            const juce::String sleepToken ("make a sleep token style song");
            const auto st = Request::parse (sleepToken);
            check (st.style == "djent" && st.genre == "altmetal" && st.artist == "Sleep Token" && st.understood.contains ("Sleep Token (alternative metal)"),
                   "\"" + sleepToken + "\": " + st.understood.joinIntoString (", "));
            check (st.softIn.contains ("verse") && st.softIn.contains ("intro") && st.heavyIn.contains ("chorus") && st.halfTimeIn.contains ("chorus")
                     && st.lowest == 24 && st.piano && st.richChords,
                   "its feel: quiet verses and intro, heavy half-time choruses, drop C, piano, soul chords");
            check (! st.halfTime && std::abs (st.energy - 0.5f) < 0.01f && st.sections.isEmpty(),
                   "which isn't the whole song half-time or softer, nor a song of verses and choruses alone");

            const auto own = Request::parse ("metalcore with soft verses and a heavy half-time chorus");
            check (own.style == "metalcore" && own.genre.isEmpty() && own.softIn == juce::StringArray { "verse" } && own.heavyIn == juce::StringArray { "chorus" }
                     && own.halfTimeIn == juce::StringArray { "chorus" } && own.sections.isEmpty() && ! own.halfTime,
                   "typed for any style: \"metalcore with soft verses and a heavy half-time chorus\"");

            Song song;
            song.loadBlank();
            const auto drums = drummer::offline::write (sleepToken, song, nullptr, {}, false, {}, true, 1);
            const auto bassLine = bass::offline::write (sleepToken, song, nullptr, &drums.groove, true, true, 1);
            const auto guitarPart = guitar::offline::write (sleepToken, song, nullptr, &drums.groove, &bassLine.part, true, true, 1);
            const auto keysPart = keys::offline::write (sleepToken, song, nullptr, &drums.groove, &bassLine.part, true, true, 1);
            const auto synthPart = synth::offline::write (sleepToken, song, nullptr, {}, {}, &drums.groove, &bassLine.part, &keysPart.part, true, true, 1);

            auto sectionOf = [] (const auto& sections, const juce::String& kind) -> decltype (&sections.front())
            {
                for (auto& s : sections)
                    if (theory::kindOf (s.name) == kind)
                        return &s;

                return nullptr;
            };

            auto drumWeight = [] (const drummer::Section* s)
            {
                float sum = 0.0f;
                int n = 0;

                if (s != nullptr)
                    for (auto& b : s->bars)
                        for (auto& l : b.lanes)
                            for (auto ch : l.hits)
                                if (ch >= '1' && ch <= '9')
                                {
                                    sum += (float) (ch - '0');
                                    ++n;
                                }

                return n > 0 ? sum / (float) n : 0.0f;
            };

            auto snareSteps = [] (const drummer::Bar& b)
            {
                juce::Array<int> at;

                for (auto& l : b.lanes)
                    if (l.voice == "snare")
                        for (int i = 0; i < l.hits.length(); ++i)
                            if (l.hits[i] != '.' && l.hits[i] != 'g')
                                at.add (i);

                return at;
            };

            const auto* verse = sectionOf (drums.groove.sections, "verse");
            const auto* chorus = sectionOf (drums.groove.sections, "chorus");
            bool sidestick = false;

            if (verse != nullptr)
                for (auto& l : verse->bars.front().lanes)
                    sidestick = sidestick || (l.voice == "sidestick" && l.hits.containsAnyOf ("123456789"));

            const auto chorusSnares = chorus != nullptr ? snareSteps (chorus->bars.front()) : juce::Array<int>();
            check (verse != nullptr && chorus != nullptr && sidestick && drumWeight (verse) + 1.5f < drumWeight (chorus)
                     && chorusSnares.contains (8) && ! chorusSnares.contains (4) && ! chorusSnares.contains (12) && drums.groove.tempo == 140,
                   "the drummer: the verse quiet on a side-stick (" + juce::String (drumWeight (verse), 1) + " against the chorus's "
                     + juce::String (drumWeight (chorus), 1) + "), the chorus half-time (its snare on " + juce::String (chorusSnares.size() > 0 ? chorusSnares[0] : -1)
                     + "), at 140 bpm (" + juce::String (drums.groove.tempo) + "): " + drums.reply.upToFirstOccurrenceOf (".", true, false));

            const auto* bassVerse = sectionOf (bassLine.part.sections, "verse");
            bool held = bassVerse != nullptr;

            if (bassVerse != nullptr)
                for (auto& b : bassVerse->bars)
                    held = held && b.notes.empty();

            check (held, "the bassist: out of the quiet verse, with the guitars (the piano's left hand has the root)");

            const auto* guitarVerse = sectionOf (guitarPart.part.sections, "verse");
            const auto* guitarChorus = sectionOf (guitarPart.part.sections, "chorus");
            bool silent = guitarVerse != nullptr, playing = false;

            if (guitarVerse != nullptr)
                for (auto& b : guitarVerse->bars)
                    silent = silent && b.notes.empty();

            if (guitarChorus != nullptr)
                for (auto& b : guitarChorus->bars)
                    playing = playing || ! b.notes.empty();

            check (silent && playing, "the guitarist: out for the quiet verse, in for the chorus");

            const auto* keysVerse = sectionOf (keysPart.part.sections, "verse");
            const auto* keysChorus = sectionOf (keysPart.part.sections, "chorus");
            bool arpeggio = keysVerse != nullptr, colour = false;

            if (keysVerse != nullptr)
                for (auto& b : keysVerse->bars)
                    arpeggio = arpeggio && b.notes.size() >= 6;

            if (keysChorus != nullptr)
                for (auto& b : keysChorus->bars)
                {
                    const auto root = theory::parseChord (b.chord).root;

                    for (auto& n : b.notes)
                    {
                        const int interval = ((n.pitch - root) % 12 + 12) % 12;
                        colour = colour || interval == 10 || interval == 11 || interval == 2;
                    }
                }

            check (arpeggio && colour && (keysPart.part.sound == "Grand Piano" || keysPart.part.sound == "Velvet Keys"),
                   "the keys: on the piano (" + keysPart.part.sound + "), an arpeggio through the quiet verse, sevenths and ninths in the chorus's chords");

            auto meanVelocity = [] (const auto* s)
            {
                float sum = 0.0f;
                int n = 0;

                if (s != nullptr)
                    for (auto& b : s->bars)
                        for (auto& note : b.notes)
                        {
                            sum += (float) note.velocity;
                            ++n;
                        }

                return n > 0 ? sum / (float) n : 0.0f;
            };

            const auto synthVerse = meanVelocity (sectionOf (synthPart.part.sections, "verse"));
            const auto synthChorus = meanVelocity (sectionOf (synthPart.part.sections, "chorus"));
            check (synthChorus > synthVerse + 10.0f, "the synth: out of the quiet verse or quieter in it (" + juce::String (synthVerse, 0) + ", the chorus "
                                                       + juce::String (synthChorus, 0) + ")");

            // The blues: a shuffle on twelve-bar changes, twelve bars a section
            {
                const auto r = Request::parse ("write a slow blues");
                const auto bluesDrums = drummer::offline::write ("write a slow blues", song, nullptr, {}, false, {}, true, 2);
                const auto bluesBass = bass::offline::write ("write a slow blues", song, nullptr, &bluesDrums.groove, true, true, 2);
                const auto* v = sectionOf (bluesBass.part.sections, "verse");
                juce::StringArray chords;

                if (v != nullptr)
                    for (auto& b : v->bars)
                        chords.add (b.chord);

                const bool twelve = chords.size() == 12 && chords[0] == chords[2] && chords[2] == chords[3] && chords[4] == chords[5]
                                    && chords[4] != chords[0] && chords[8] != chords[0] && chords[8] != chords[4] && chords[8] == chords[11]
                                    && chords[0].endsWith ("7") && chords[4].endsWith ("7") && chords[8].endsWith ("7");
                check (r.genre == "blues" && r.changes == "twelvebar" && r.bars == 12 && r.swing > 0.4f && twelve,
                       "\"write a slow blues\": a shuffle on twelve-bar changes (" + chords.joinIntoString (" ") + ")");
            }

            // The styles the band learned in round two, each played by the whole band: the drums' pattern
            // in a section's first bar, and what the bass, the guitar and the keys do over it
            {
                juce::StringArray notOwn;

                for (auto* id : { "house", "techno", "edm", "dnb", "dubstep", "synthwave", "reggae", "reggaeton", "afrobeats", "bossa", "ambient", "cinematic", "country" })
                {
                    const auto* g = agents::findGenre (id);

                    if (g == nullptr || ! g->own || juce::String (g->plays) != id || agents::findStyle (id) == nullptr)
                        notOwn.add (id);
                }

                check (notOwn.isEmpty(), "the 13 styles learned in round two are the band's own, each its own style"
                                           + (notOwn.isEmpty() ? juce::String() : ": not " + notOwn.joinIntoString (", ")));
            }

            {
                struct Parts
                {
                    drummer::offline::Result drums;
                    bass::offline::Result bass;
                    guitar::offline::Result guitar;
                    keys::offline::Result keys;
                };

                auto band = [&song] (const juce::String& words, juce::int64 seed)
                {
                    Parts p;
                    p.drums = drummer::offline::write (words, song, nullptr, {}, false, {}, true, seed);
                    p.bass = bass::offline::write (words, song, nullptr, &p.drums.groove, true, true, seed);
                    p.guitar = guitar::offline::write (words, song, nullptr, &p.drums.groove, &p.bass.part, true, true, seed);
                    p.keys = keys::offline::write (words, song, nullptr, &p.drums.groove, &p.bass.part, true, true, seed);
                    return p;
                };

                auto firstBar = [] (const auto& sections, const juce::String& kind) -> decltype (&sections.front().bars.front())
                {
                    for (auto& s : sections)
                        if (theory::kindOf (s.name) == kind && ! s.bars.empty())
                            return &s.bars.front();

                    return nullptr;
                };

                auto hits = [] (const drummer::Bar* b, const juce::String& voice)
                {
                    juce::Array<int> at;

                    if (b != nullptr)
                        for (auto& l : b->lanes)
                            if (l.voice == voice)
                                for (int i = 0; i < l.hits.length(); ++i)
                                    if (l.hits[i] != '.' && l.hits[i] != 'g')
                                        at.add (i);

                    return at;
                };

                auto stepsOf = [] (const auto* bar, bool leftHand)
                {
                    juce::Array<int> at;

                    if (bar != nullptr)
                        for (auto& n : bar->notes)
                            if (leftHand || ! n.left)
                                at.addIfNotAlreadyThere (n.step);

                    at.sort();
                    return at;
                };

                auto guitarSteps = [] (const auto* bar)
                {
                    juce::Array<int> at;

                    if (bar != nullptr)
                        for (auto& n : bar->notes)
                            at.addIfNotAlreadyThere (n.step);

                    at.sort();
                    return at;
                };

                auto keysOut = [] (const keys::offline::Result& r)
                {
                    bool silent = ! r.part.sections.empty();

                    for (auto& s : r.part.sections)
                        for (auto& b : s.bars)
                            silent = silent && b.notes.empty();

                    return silent;
                };

                auto is = [] (const juce::Array<int>& a, std::initializer_list<int> b) { return a == juce::Array<int> (b); };
                auto list = [] (const juce::Array<int>& a)
                {
                    juce::StringArray s;

                    for (auto x : a)
                        s.add (juce::String (x));

                    return "[" + s.joinIntoString (" ") + "]";
                };

                {
                    const auto r = Request::parse ("make a house track");
                    const auto p = band ("make a house track", 3);
                    const auto* chorus = firstBar (p.drums.groove.sections, "chorus");
                    const auto* intro = firstBar (p.keys.part.sections, "intro");
                    const auto* stabs = firstBar (p.keys.part.sections, "chorus");
                    bool short_ = stabs != nullptr && ! stabs->notes.empty();

                    if (stabs != nullptr)
                        for (auto& n : stabs->notes)
                            short_ = short_ && n.length <= 2;

                    check (r.genre == "house" && r.style == "house" && ! r.understood.joinIntoString (" ").contains ("for now")
                             && is (hits (chorus, "kick"), { 0, 4, 8, 12 }) && is (hits (chorus, "clap"), { 4, 12 })
                             && ! p.drums.reply.contains ("isn't something the band plays yet") && intro != nullptr && intro->notes.empty() && short_,
                           "\"make a house track\": house, the band's own: four on the floor " + list (hits (chorus, "kick")) + ", the clap on 2 and 4, short piano stabs "
                             + list (stepsOf (stabs, false)) + " on " + p.keys.part.sound + ", out of the intro");
                }

                {
                    const auto p = band ("a techno track", 4);
                    const auto lineup = songwriter::lineupFor ("techno", "a techno track");
                    check (keysOut (p.keys) && ! lineup.contains ("keys") && ! lineup.contains ("guitar") && is (hits (firstBar (p.drums.groove.sections, "chorus"), "kick"), { 0, 4, 8, 12 }),
                           "techno: four on the floor, no keys (the band: " + lineup.joinIntoString (", ") + ")");
                }

                {
                    const auto p = band ("an edm track", 5);
                    const auto* build = firstBar (p.drums.groove.sections, "prechorus");
                    const auto* drop = firstBar (p.drums.groove.sections, "drop");
                    const auto* chords = firstBar (p.keys.part.sections, "drop");
                    check (hits (build, "snare").size() == 16 && is (hits (drop, "kick"), { 0, 4, 8, 12 }) && is (stepsOf (chords, false), { 2, 6, 10, 14 }),
                           "EDM: the build's snare rolling in sixteenths, the drop four on the floor, the keys' chords on its offbeats " + list (stepsOf (chords, false)));
                }

                {
                    const auto p = band ("drum and bass", 6);
                    const auto* drop = firstBar (p.drums.groove.sections, "drop");
                    const auto* rhodes = firstBar (p.keys.part.sections, "drop");
                    int held = 0;

                    if (rhodes != nullptr)
                        for (auto& n : rhodes->notes)
                            held += ! n.left && n.step == 0 ? 1 : 0;

                    check (p.drums.groove.tempo == 174 && hits (drop, "kick").contains (0) && hits (drop, "kick").contains (10) && is (hits (drop, "snare"), { 4, 12 })
                             && p.keys.part.sound == "Dusty Rhodes" && held == 4,
                           "drum and bass at " + juce::String (p.drums.groove.tempo) + ": the two-step (the kick " + list (hits (drop, "kick")) + ", the snare on 2 and 4), "
                             + p.keys.part.sound + " holding four-note chords (" + juce::String (held) + ")");
                }

                {
                    const auto p = band ("dubstep", 7);
                    const auto* drop = firstBar (p.drums.groove.sections, "drop");
                    check (is (hits (drop, "snare"), { 8 }) && hits (drop, "kick").contains (0) && keysOut (p.keys) && ! songwriter::lineupFor ("dubstep", "dubstep").contains ("keys"),
                           "dubstep: half-time, the snare on 3 " + list (hits (drop, "snare")) + ", no keys, the bass on " + p.bass.part.sound);
                }

                {
                    const auto p = band ("a synthwave song", 8);
                    const auto* verse = firstBar (p.drums.groove.sections, "verse");
                    const auto* held = firstBar (p.keys.part.sections, "verse");
                    check (hits (verse, "kick").contains (0) && hits (verse, "kick").contains (8) && is (hits (verse, "snare"), { 4, 12 }) && is (stepsOf (held, false), { 0 }),
                           "synthwave: the kick on 1 and 3, a big snare on 2 and 4, the keys holding each chord a bar on " + p.keys.part.sound);
                }

                {
                    const auto p = band ("a reggae song", 9);
                    const auto* verse = firstBar (p.drums.groove.sections, "verse");
                    const auto* skank = firstBar (p.guitar.part.sections, "verse");
                    const auto* organ = firstBar (p.keys.part.sections, "verse");
                    bool offbeats = organ != nullptr && ! organ->notes.empty();

                    if (organ != nullptr)
                        for (auto& n : organ->notes)
                            offbeats = offbeats && n.step % 4 >= 2;

                    check (is (hits (verse, "kick"), { 8 }) && hits (verse, "sidestick").contains (8) && is (guitarSteps (skank), { 4, 12 })
                             && p.keys.part.sound == "Drawbar Organ" && offbeats,
                           "reggae: the one drop (the kick " + list (hits (verse, "kick")) + " with the side-stick on 3), the guitar's skank on 2 and 4 "
                             + list (guitarSteps (skank)) + ", the organ's bubble on the offbeats " + list (stepsOf (organ, false)));
                }

                {
                    const auto p = band ("reggaeton", 10);
                    const auto* hook = firstBar (p.drums.groove.sections, "hook");
                    const auto* chords = firstBar (p.keys.part.sections, "hook");
                    check (is (hits (hook, "kick"), { 0, 4, 8, 12 }) && is (hits (hook, "snare"), { 3, 6, 11, 14 }) && is (stepsOf (chords, false), { 0, 3, 6, 8, 11, 14 }),
                           "reggaeton: the dembow (the kick every beat, the snare " + list (hits (hook, "snare")) + "), the keys' chords on its 3+3+2 " + list (stepsOf (chords, false)));
                }

                {
                    const auto p = band ("afrobeats", 11);
                    const auto* verse = firstBar (p.drums.groove.sections, "verse");
                    const auto* highlife = firstBar (p.guitar.part.sections, "verse");
                    check (is (hits (verse, "clap"), { 4, 12 }) && hits (verse, "hat_closed").size() == 16 && hits (verse, "kick").contains (0)
                             && hits (verse, "kick").size() >= 3 && highlife != nullptr && highlife->notes.size() >= 8,
                           "afrobeats: the clap on 2 and 4, a shaker in sixteenths, the kick " + list (hits (verse, "kick")) + ", the guitar's highlife in sixteenths");
                }

                {
                    const auto p = band ("a bossa nova", 12);
                    const auto* verse = firstBar (p.drums.groove.sections, "verse");
                    const auto* shells = firstBar (p.keys.part.sections, "verse");
                    bool rootless = shells != nullptr && ! shells->notes.empty();

                    if (shells != nullptr)
                        for (auto& n : shells->notes)
                            rootless = rootless && n.pitch % 12 != theory::parseChord (shells->chord).root;

                    check (is (hits (verse, "sidestick"), { 0, 3, 6, 10, 13 }) && is (hits (verse, "kick"), { 0, 6, 8, 14 }) && rootless,
                           "bossa nova: the side-stick on the clave, the kick a soft surdo, " + p.keys.part.sound + " shells without the root");
                }

                {
                    const auto p = band ("an ambient track", 13);
                    const auto* verse = firstBar (p.drums.groove.sections, "verse");
                    bool empty = verse != nullptr;
                    int most = 0;

                    if (verse != nullptr)
                        for (auto& l : verse->lanes)
                            empty = empty && ! l.hits.containsAnyOf ("123456789g");

                    for (auto& s : p.keys.part.sections)
                        for (auto& b : s.bars)
                            most = std::max (most, stepsOf (&b, false).size());

                    check (empty && most >= 1 && most <= 2,
                           "ambient: no drums in the verse, the piano (" + p.keys.part.sound + ") a note or two a bar (" + juce::String (most) + " at most)");
                }

                {
                    const auto p = band ("cinematic", 14);
                    const auto* chorus = firstBar (p.drums.groove.sections, "chorus");
                    const auto* keysChorus = firstBar (p.keys.part.sections, "chorus");
                    int ostinato = 0;

                    if (keysChorus != nullptr)
                        for (auto& n : keysChorus->notes)
                            ostinato += n.left ? 1 : 0;

                    check (is (hits (chorus, "tom_floor"), { 0, 3, 6, 8, 11, 14 }) && is (hits (chorus, "snare"), { 4, 12 }) && ostinato == 8,
                           "cinematic: the chorus's toms in 3+3+2 under a big snare on 2 and 4, the piano's ostinato in eighths (" + juce::String (ostinato) + " notes a bar)");
                }

                {
                    const auto p = band ("a country song", 15);
                    const auto* verse = firstBar (p.drums.groove.sections, "verse");
                    const auto* piano = firstBar (p.keys.part.sections, "verse");
                    const auto* boomChick = firstBar (p.guitar.part.sections, "verse");
                    bool slide = false;

                    if (piano != nullptr)
                        for (auto& n : piano->notes)
                            slide = slide || n.grace;

                    check (hits (verse, "snare").size() == 8 && is (hits (verse, "kick"), { 0, 8 }) && slide && is (guitarSteps (boomChick), { 0, 4, 8, 12 }),
                           "country: the train beat (the snare in eighths, ghosts between), the piano (" + p.keys.part.sound
                             + ") sliding into the third, the guitar's boom-chick");
                }
            }

            // Ambient and cinematic describe as often as they name; the song agent's drummer keeps how each section
            // is played (its words rebuilt with the form first, the feel said again after)
            {
                const auto djent = Request::parse ("djent ambient pad");
                const auto drone = Request::parse ("drone metal");
                const auto arp = Request::parse ("an ambient arp");
                const auto down = Request::parse ("a downtempo song");
                const auto metalcore = Request::parse (songwriter::drummerWords ("metalcore with soft verses and heavy choruses", "metalcore"));
                const auto ambient = Request::parse (songwriter::drummerWords ("an ambient song", "ambient"));
                check (djent.style == "djent" && drone.style == "metal" && arp.style == "ambient" && down.genre == "ambient"
                         && metalcore.softIn == juce::StringArray { "verse" } && metalcore.heavyIn == juce::StringArray { "chorus" }
                         && metalcore.sections.size() > 0 && metalcore.sections[0] == "intro" && ambient.sections.size() > 0 && ambient.sections[0] == "intro",
                       "\"djent ambient pad\" is djent, \"drone metal\" metal, \"an ambient arp\" ambient, downtempo the genre; the song agent's drummer keeps "
                       "soft verses and heavy choruses (" + metalcore.understood.joinIntoString (", ") + ") and an ambient song's intro");
            }

            // A name the band doesn't know is said back, not quietly played as rock
            {
                const auto r = Request::parse ("make a zyxwv style song");
                const auto plain = Request::parse ("give me a heavy style beat");
                const bool told = r.understood.joinIntoString (" ").contains ("zyxwv") && r.style.isEmpty();
                const bool quiet = ! plain.understood.joinIntoString (" ").contains ("doesn't know");
                check (told && quiet, "\"zyxwv style\": " + r.understood.joinIntoString (", ") + "; \"heavy style\" isn't a name");
            }

            // Kept with the song
            {
                const auto back = Request::fromVar (st.toVar());
                check (back.genre == st.genre && back.artist == st.artist && back.softIn == st.softIn && back.heavyIn == st.heavyIn
                         && back.halfTimeIn == st.halfTimeIn && back.richChords && back.piano && back.lowest == st.lowest,
                       "a request saved and read back keeps its genre, artist and how its sections are played");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--vibetest"))
        {
            // The vibe listener (ai/VibeListener.h): songs made of sines and noise, where what's in them is known; the
            // band's own songs, rendered (another of this app, --bandrender), against what each player played; the
            // song agent's words for a vibe, read back; and the words it writes (chords, a form) played as asked
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto rootOf = [] (const juce::String& chord) { return theory::parseChord (chord).root; };

            // The song files it opens: MP3 among them (JUCE's own decoder: no download, no licence)
            {
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                bool all = true;

                for (auto type : juce::StringArray::fromTokens (vibes::fileTypes(), ";", {}))
                    all = all && formats.findFormatForFileExtension (type.fromFirstOccurrenceOf (".", false, false)) != nullptr;

                check (all && vibes::fileTypes().contains ("*.mp3"), "it opens " + vibes::fileTypes() + ", MP3 among them");
            }

            // A made-up song: a quiet intro, verse, chorus (louder, other chords), verse, chorus, a quiet outro, at 120
            {
                const double rate = 44100.0;
                std::vector<TestBar> bars;
                juce::StringArray want, kinds;

                auto section = [&] (const juce::String& kind, const juce::StringArray& chords, int times, float gain)
                {
                    for (int t = 0; t < times; ++t)
                        for (auto& c : chords)
                        {
                            bars.push_back ({ c, gain });
                            want.add (c);
                        }

                    kinds.add (kind);
                };

                section ("intro", { "Am", "Am", "Am", "Am" }, 1, 0.45f);
                section ("verse", { "Am", "F", "C", "G" }, 2, 0.7f);
                section ("chorus", { "F", "G", "Am", "E" }, 2, 1.0f);
                section ("verse", { "Am", "F", "C", "G" }, 2, 0.7f);
                section ("chorus", { "F", "G", "Am", "E" }, 2, 1.0f);
                section ("outro", { "Am", "Am", "Am", "Am" }, 1, 0.4f);

                const auto x = testSong (rate, 120.0, bars, 0.0f, false);
                const auto v = vibes::listen (x.data(), (int) x.size(), rate, "made up");
                int rightRoots = 0, heardBars = 0;
                juce::StringArray heardKinds, heardChords;

                for (auto& s : v.sections)
                {
                    heardKinds.add (s.kind);

                    for (int i = 0; i < s.chords.size(); ++i)
                    {
                        heardChords.add (s.chords[i]);
                        rightRoots += s.startBar + i < want.size() && rootOf (s.chords[i]) == rootOf (want[s.startBar + i]) ? 1 : 0;
                        ++heardBars;
                    }
                }

                check (v.ok() && std::abs (v.tempo - 120.0) < 1.5 && std::abs (v.firstBar) < 0.06,
                       "a made-up song at 120: heard at " + juce::String (v.tempo, 1) + " bpm, its first bar at " + juce::String (v.firstBar, 3) + " s");
                check (v.keyName() == "A minor", "its key, A minor: heard " + v.keyName());
                check (heardBars == want.size() && rightRoots >= want.size() * 9 / 10,
                       "a chord a bar, " + juce::String (rightRoots) + " of " + juce::String (want.size()) + " on the right root: " + heardChords.joinIntoString (" "));
                check (heardKinds == kinds, "its sections: " + heardKinds.joinIntoString (", ") + " (want " + kinds.joinIntoString (", ") + ")");
                check (v.sections.size() == 6 && v.sections[1].label == v.sections[3].label && v.sections[2].label == v.sections[4].label
                         && v.sections[1].label != v.sections[2].label && v.sections[2].energyDb > v.sections[1].energyDb + 2.0f,
                       "the verses one music and the choruses another, the choruses the louder");
                check (! v.fourOnFloor && v.swing < 0.15f, "straight, the kick on 1 and 3: not four on the floor, not swung");

                // The song agent's words for it: as heard when close, read back as the request it'll be
                const auto close = vibes::planFrom (v, 1.0f, 7, "rock");
                const auto r = agents::Request::parse (close.words);
                check (r.style == "rock" && std::abs (r.tempo - 120.0) < 0.5 && r.keyRoot == 9 && r.mode == "minor"
                         && r.form == juce::StringArray ({ "intro", "verse", "chorus", "verse", "chorus", "outro" })
                         && r.chordsIn.count ("verse") > 0 && r.chordsIn.at ("verse") == juce::StringArray ({ "Am", "F", "C", "G" })
                         && r.chordsIn.count ("chorus") > 0 && r.chordsIn.at ("chorus") == juce::StringArray ({ "F", "G", "Am", "E" })
                         && close.changed.isEmpty(),
                       "close, its words keep it all and read back so: \"" + close.words + "\"");

                // Looser, each take changes something, and not always the same thing
                int tempoMoved = 0, keyMoved = 0;

                for (juce::int64 seed = 1; seed <= 12; ++seed)
                {
                    const auto loose = agents::Request::parse (vibes::planFrom (v, 0.25f, seed, "rock").words);
                    tempoMoved += std::abs (loose.tempo - 120.0) >= 1.0 ? 1 : 0;
                    keyMoved += loose.keyRoot != 9 || loose.mode != "minor" ? 1 : 0;
                }

                check (tempoMoved >= 6 && keyMoved >= 3 && keyMoved <= 11,
                       "loosely, twelve takes: the tempo moved in " + juce::String (tempoMoved) + ", the key in " + juce::String (keyMoved));
            }

            // Swung, and four on the floor
            {
                std::vector<TestBar> bars;

                for (int i = 0; i < 6; ++i)
                    for (auto c : { "Dm7", "G7", "Cmaj7", "Cmaj7" })
                        bars.push_back ({ c, 0.8f });

                const auto swung = testSong (44100.0, 90.0, bars, 1.0f, false);
                const auto s = vibes::listen (swung.data(), (int) swung.size(), 44100.0, "swung");
                const auto floor = testSong (44100.0, 124.0, bars, 0.0f, true);
                const auto f = vibes::listen (floor.data(), (int) floor.size(), 44100.0, "four on the floor");
                check (s.ok() && std::abs (s.tempo - 90.0) < 1.5 && s.swing >= 0.6f && ! s.swingSixteenths,
                       "the hats' offbeats a triplet late: heard at " + juce::String (s.tempo, 1) + " bpm, 8ths swung " + juce::String (s.swing, 2));
                check (f.ok() && std::abs (f.tempo - 124.0) < 1.5 && f.fourOnFloor, "a kick on every beat at 124: heard at " + juce::String (f.tempo, 1)
                         + (f.fourOnFloor ? ", four on the floor" : ", not four on the floor"));
            }

            // The band's own songs, rendered by another of this app: what each player played is known
            {
                const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
                const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NightShift vibetest");
                folder.createDirectory();
                struct Take { juce::String words, style; juce::File wav; std::unique_ptr<juce::ChildProcess> process; juce::String out; };
                std::vector<Take> takes;
                takes.push_back ({ "a funk groove", "funk", folder.getChildFile ("funk.wav"), {}, {} });
                takes.push_back ({ "metalcore", "metalcore", folder.getChildFile ("metalcore.wav"), {}, {} });
                takes.push_back ({ "a house track", "house", folder.getChildFile ("house.wav"), {}, {} });

                for (auto& t : takes)
                {
                    t.process = std::make_unique<juce::ChildProcess>();
                    t.process->start (juce::StringArray { exe.getFullPathName(), "--bandrender", t.wav.getFullPathName(), t.words, "--song", "--seed", "3" });
                }

                for (auto& t : takes)
                {
                    t.out = t.process->readAllProcessOutput();
                    t.process->waitForProcessToFinish (600000);
                }

                for (auto& t : takes)
                {
                    const auto v = vibes::listen (t.wav);
                    const double bpm = t.out.fromFirstOccurrenceOf ("sections (RMS dBFS) at ", false, false).getDoubleValue();

                    // What the bassist played, bar by bar, in the song's order; and each section's kind
                    juce::StringArray played, kinds;

                    for (auto line : juce::StringArray::fromLines (t.out))
                        if (line.startsWith ("chords "))
                        {
                            kinds.add (theory::kindOf (line.fromFirstOccurrenceOf ("chords ", false, false).upToFirstOccurrenceOf (":", false, false)));
                            played.addTokens (line.fromFirstOccurrenceOf (": ", false, false), " ", {});
                        }

                    int right = 0, heardBars = 0;
                    juce::StringArray heardKinds, top;

                    for (auto& s : v.sections)
                    {
                        heardKinds.add (s.kind);

                        for (int i = 0; i < s.chords.size(); ++i, ++heardBars)
                            right += s.startBar + i < played.size() && rootOf (s.chords[i]) == rootOf (played[s.startBar + i]) ? 1 : 0;
                    }

                    for (size_t i = 0; i < std::min<size_t> (3, v.styles.size()); ++i)
                        top.add (v.styles[i].style);

                    const bool tempoRight = std::abs (v.tempo - bpm) < bpm * 0.02 || std::abs (v.tempo * 2.0 - bpm) < bpm * 0.02 || std::abs (v.tempo - bpm * 2.0) < bpm * 0.04;
                    check (v.ok() && tempoRight && std::abs (v.firstBar) < 0.06 && played.size() > 0 && right >= played.size() * 4 / 5,
                           "the band's " + t.style + " at " + juce::String (bpm, 0) + " bpm: heard at " + juce::String (v.tempo, 1) + ", its first bar at "
                             + juce::String (v.firstBar, 3) + " s, " + juce::String (right) + " of " + juce::String (played.size()) + " bars' chords on the bassist's root");
                    check (heardKinds.size() >= kinds.size() - 2 && heardKinds.size() <= kinds.size() + 2 && heardKinds.contains ("chorus") && heardKinds.contains ("verse"),
                           "  its sections: " + heardKinds.joinIntoString (", ") + " (played " + kinds.joinIntoString (", ") + ")");
                    check (top.contains (t.style) || (t.style == "house" && (top.contains ("edm") || top.contains ("techno"))),
                           "  its style among the nearest three: " + top.joinIntoString (", "));
                    t.wav.deleteFile();
                }

                folder.deleteRecursively();
            }

            // Its words read back: chords for a kind of section and for every section, a form; a chord isn't a key
            {
                const auto r = agents::Request::parse ("funk at 100 bpm in E minor, verse chords E7 A7 E7 B7, chorus chords A7 A7 E7 E7, form intro verse chorus verse chorus outro");
                const auto all = agents::Request::parse ("lofi, chords Dm7 G7 Cmaj7 Am7");
                check (r.style == "funk" && r.keyRoot == 4 && r.mode == "minor" && r.chordsIn.size() == 2 && r.chordsIn.at ("verse").size() == 4
                         && r.chordsIn.at ("chorus")[0] == "A7" && r.form.size() == 6 && all.chordsIn.count ({}) > 0 && all.chordsIn.at ({}).size() == 4
                         && agents::Request::fromVar (r.toVar()).chordsIn == r.chordsIn && agents::Request::fromVar (r.toVar()).form == r.form,
                       "\"verse chords ...\", \"chords ...\" and \"form ...\" read as asked, and saved and read back");

                // The bassist plays them, and the song agent lays the form out
                Song song;
                song.loadBlank();
                const auto words = juce::String ("funk at 100 bpm in E minor, verse chords E7 A7 E7 B7, chorus chords A7 A7 E7 E7, form intro verse chorus verse chorus bridge chorus outro");
                auto groove = drummer::offline::write (songwriter::drummerWords (words, "funk"), song, nullptr, {}, false, {}, true, 5).groove;
                const auto b = bass::offline::write (words, song, nullptr, &groove, true, true, 5);
                bool asked = true;
                int seen = 0;

                for (auto& s : b.part.sections)
                {
                    const auto kind = theory::kindOf (s.name);
                    const juce::StringArray want = kind == "verse" ? juce::StringArray { "E7", "A7", "E7", "B7" } : kind == "chorus" ? juce::StringArray { "A7", "A7", "E7", "E7" } : juce::StringArray();

                    if (want.isEmpty())
                        continue;

                    ++seen;

                    for (size_t i = 0; i < s.bars.size(); ++i)
                        asked = asked && rootOf (s.bars[i].chord) == rootOf (want[(int) i % 4]) && s.bars[i].chord.contains ("7");
                }

                const auto arrangement = songwriter::arrange (groove, "funk", words, 4.0 * 60.0 / 100.0);
                juce::StringArray arranged;

                for (auto& name : arrangement)
                    arranged.add (theory::kindOf (name));

                check (seen >= 2 && asked, "the bassist plays the verse's and the chorus's chords as asked");
                check (arranged == juce::StringArray ({ "intro", "verse", "chorus", "verse", "chorus", "bridge", "chorus", "outro" }), "the song agent lays out the form asked: " + arranged.joinIntoString (", "));
            }

            // The band's ears: every style it plays heard, from two renders or more, its drums between 0 and 1
            {
                juce::StringArray missing;

                for (auto& s : agents::styles())
                    if (const auto* ear = vibes::ears::find (s.id); ear == nullptr || ear->heard < 2
                          || std::any_of (std::begin (ear->pattern), std::end (ear->pattern), [] (float p) { return p < 0.0f || p > 1.0f; }))
                        missing.add (s.id);

                check (missing.isEmpty() && vibes::ears::all().size() == agents::styles().size(),
                       juce::String ((int) vibes::ears::all().size()) + " styles' ears, one for each style the band plays" + (missing.isEmpty() ? juce::String() : ": not " + missing.joinIntoString (", ")));
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--feeltest"))
        {
            // The drummer's feel learned from real drummers (ai/LearnedFeel.h, the Groove MIDI Dataset): the table, which
            // styles and genres play it (and which keep their own), and where render puts each hit and how hard
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            using drummer::learnedFeels;
            using drummer::learnedFeel;
            namespace offline = drummer::offline;
            {
                juce::StringArray bad;
                int minutes = 0;

                for (auto& f : learnedFeels())
                {
                    bool ok = (f.steps == 16 || f.steps == 12) && f.minutes >= 2 && f.grooves >= 2 && f.swing <= 1.0f && f.looseMs >= 0.0f && f.looseMs <= 30.0f
                              && (agents::findStyle (f.id) != nullptr || agents::findGenre (f.id) != nullptr) && ! f.timingMs.empty();

                    for (auto& [drum, ms] : f.pushMs)
                        ok = ok && std::abs (ms) <= 25.0f && f.timingMs.count (drum) > 0;

                    for (auto& [drum, v] : f.timingMs)
                    {
                        ok = ok && (int) v.size() == f.steps && f.accent.count (drum) > 0 && (int) f.accent.at (drum).size() == f.steps;

                        for (size_t i = 0; ok && i < v.size(); ++i)
                            ok = std::abs (v[i]) <= 40.0f && f.accent.at (drum)[i] >= 0.3f && f.accent.at (drum)[i] <= 2.0f;
                    }

                    if (! ok)
                        bad.add (f.id);

                    minutes += f.minutes;
                }

                check (learnedFeels().size() >= 15 && bad.isEmpty(),
                       juce::String ((int) learnedFeels().size()) + " feels learned (" + juce::String (minutes) + " minutes of playing), each a style or genre of the band's, "
                         "from two minutes or more, every step within 40 ms and every accent between 0.3 and 2" + (bad.isEmpty() ? juce::String() : ": not " + bad.joinIntoString (", ")));
            }

            Song song;
            song.loadBlank();
            auto write = [&song] (const juce::String& words) { return offline::write (words, song, nullptr, {}, false, {}, true, 7); };

            const auto funk = write ("a funk groove");
            const auto* f = learnedFeel ("funk");
            check (f != nullptr && funk.groove.learned == "funk" && std::abs (funk.groove.swing - f->swing) < 0.001f && funk.groove.swingSixteenths
                     && std::abs (funk.groove.looseMs - f->looseMs) < 0.001f && funk.groove.pushMs.count ("snare") > 0
                     && std::abs (funk.groove.pushMs.at ("snare") - f->pushMs.at ("snare")) < 0.001f
                     && std::abs (funk.groove.pushMs.at ("clap") - f->pushMs.at ("snare")) < 0.001f
                     && funk.reply.contains ("learned from real funk drummers") && funk.reply.contains ("Groove MIDI Dataset"),
                   "funk plays the funk drummers' feel: its 16ths swung " + juce::String (funk.groove.swing, 2) + ", the snare (and a clap) "
                     + juce::String (funk.groove.pushMs.count ("snare") > 0 ? funk.groove.pushMs.at ("snare") : 0.0f, 1) + " ms, looseness "
                     + juce::String (funk.groove.looseMs, 1) + " ms, and says so");

            // A genre's own learned feel first; the programmed styles and the lazy pockets keep theirs
            {
                const auto disco = write ("a disco song"), soul = write ("a soul song"), neo = write ("a neo soul groove");
                const auto trap = write ("a trap beat"), house = write ("a house track"), boombap = write ("a boom bap beat");
                const auto noSwing = write ("a funk groove, no swing");
                check (disco.groove.learned == "disco" && soul.groove.learned == "soul" && neo.groove.learned.isEmpty() && std::abs (neo.groove.swing - 0.55f) < 0.001f
                         && trap.groove.learned.isEmpty() && house.groove.learned.isEmpty()
                         && boombap.groove.learned == "boombap" && std::abs (boombap.groove.swing - 0.58f) < 0.001f
                         && boombap.groove.pushMs.count ("snare") > 0 && std::abs (boombap.groove.pushMs.at ("snare") - 12.0f) < 0.001f
                         && noSwing.groove.learned == "funk" && noSwing.groove.swing == 0.0f,
                       "disco and soul play their genres' feels; neo-soul keeps its lazy pocket, trap and house stay tight; boom bap "
                       "learns the steps but keeps its MPC swing and late snare; \"no swing\" still means none");
            }

            // Render: each hit at its grid place, swung, plus its drum's push and its step's learned timing (no wander
            // here), and its learned accent at half strength against the lane's own average
            {
                drummer::Groove g;
                g.title = "test";
                g.learned = "funk";
                g.swing = f->swing;
                g.swingSixteenths = true;
                g.looseMs = 0.0f;
                g.pushMs = funk.groove.pushMs;
                drummer::Bar bar;
                bar.steps = 16;
                bar.lanes = { { "snare", "....5.......5..." }, { "hat_closed", "5555555555555555" } };
                g.sections = { { "Verse", { bar } } };
                g.arrangement = { "Verse" };
                song.bpm = 100.0;
                const int drums = song.addChannel ("Drums", InstrumentType::kit).id;
                const auto keyFor = [] (const juce::String& v) { return drumkits::generalMidiKey (v); };
                const double ticksPerMs = 100.0 / 60.0 * ticksPerBeat / 1000.0;
                const int snareKey = drumkits::generalMidiKey ("snare"), hatKey = drumkits::generalMidiKey ("hat_closed");

                auto played = [&] () -> std::vector<Note>
                {
                    drummer::render (song, drums, g, keyFor);
                    const auto* c = song.findChannel (drums);

                    if (c != nullptr && c->drummer.patterns.count ("Verse") > 0)
                        if (const auto* p = song.findPattern (c->drummer.patterns.at ("Verse")))
                            if (auto it = p->notes.find (drums); it != p->notes.end())
                                return it->second;

                    return {};
                };

                auto noteAt = [] (const std::vector<Note>& notes, int key, int from, int to) -> std::optional<Note>
                {
                    for (auto& n : notes)
                        if (n.key == key && n.start >= from && n.start < to)
                            return n;

                    return {};
                };

                // Where: no wander (looseness_ms 0), so exactly the grid, the swing, the push and the step's timing
                const auto timed = played();
                const auto snare = noteAt (timed, snareKey, ticksPerBeat / 2, ticksPerBeat * 3 / 2);
                const int snareWant = juce::roundToInt (ticksPerBeat + (g.pushMs.at ("snare") + f->timingMs.at ("snare")[4]) * ticksPerMs);
                const auto hat = noteAt (timed, hatKey, ticksPerStep / 2, ticksPerStep * 3 / 2);
                const int hatWant = juce::roundToInt (ticksPerStep + f->swing * ticksPerBeat / 12.0
                                                      + (g.pushMs.at ("hat_closed") + f->timingMs.at ("hat_closed")[1]) * ticksPerMs);

                check (snare && snare->start == snareWant && hat && hat->start == hatWant,
                       "render: the snare on 2 at tick " + juce::String (snare ? snare->start : -1) + " (want " + juce::String (snareWant) + ": "
                         + juce::String (ticksPerBeat) + " plus its push and its step's timing), the hat on the e of 1 at " + juce::String (hat ? hat->start : -1)
                         + " (want " + juce::String (hatWant) + ", swung too)");

                // How hard: the hats, all written '5', accented as the funk drummers play their 16ths, half as strongly,
                // their average kept (the Looseness control at 0, which takes out the velocity's wander too)
                song.findChannel (drums)->drummer.looseness = 0.0f;
                const auto hard = played();
                song.findChannel (drums)->drummer.looseness = 1.0f;
                const auto& accent = f->accent.at ("hat_closed");
                float meanAccent = 0.0f, sum = 0.0f;
                bool shaped = true;
                int count = 0;

                for (auto a : accent)
                    meanAccent += a / 16.0f;

                for (int step = 0; step < 16; ++step)
                    if (const auto n = noteAt (hard, hatKey, step * ticksPerStep - ticksPerStep / 2, step * ticksPerStep + ticksPerStep / 2))
                    {
                        const int want = juce::roundToInt ((float) drummer::velocityFor ('5') * (1.0f + 0.5f * (accent[(size_t) step] / meanAccent - 1.0f)));
                        shaped = shaped && std::abs (n->velocity - want) <= 1;
                        sum += (float) n->velocity;
                        ++count;
                    }

                check (count == 16 && shaped && std::abs (sum / (float) count - (float) drummer::velocityFor ('5')) <= 2.0f,
                       "and the hats, all written '5', accented as the funk drummers play their 16ths, half as strongly, their average "
                         + juce::String (count > 0 ? sum / (float) count : 0.0f, 1) + " against " + juce::String (drummer::velocityFor ('5')));

                // Saved and read back with the song; a feel it doesn't know is dropped; none in 6/8 (no swing there either:
                // it counts quarter notes)
                const auto back = drummer::Groove::fromJson (offline::toJson (g));
                auto unknown = offline::toJson (g);

                if (auto* o = unknown.getProperty ("feel", {}).getDynamicObject())
                    o->setProperty ("learned", "zyx");

                const auto lost = drummer::Groove::fromJson (unknown);
                song.meterBeats = 6;
                song.meterUnit = 8;
                g.swing = 0.0f;
                const auto compound = played();
                const auto inSixEight = noteAt (compound, snareKey, 0, song.ticksPerBar() / 2);
                const int sixEightWant = juce::roundToInt (song.ticksPerBar() * 4 / 16.0 + g.pushMs.at ("snare") * ticksPerMs);

                check (back.has_value() && back->learned == "funk" && lost.has_value() && lost->learned.isEmpty() && inSixEight && inSixEight->start == sixEightWant,
                       "the feel is saved with the groove and read back, an unknown one dropped, and in 6/8 no learned steps (theirs is a 4/4 bar): "
                         + juce::String (inSixEight ? inSixEight->start : -1) + " against " + juce::String (sixEightWant));
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--sidebartest"))
        {
            // The sound search typed into for real (Enter adds the first match, Escape clears),
            // and sounds dropped on Steps the way a drag from the sidebar delivers them.
            // The sampled instruments as before they're fetched: with the orchestra here, "h"
            // would find its Harp before Hat
            SfzPlugin::folderOverride = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Nightshift no instruments");
            SfzPlugin::folderOverride.deleteRecursively();
            Song song;
            song.loadDemo();
            AudioEngine audio (song, false);
            Sidebar sidebar (song, audio);
            sidebar.setSize (252, 800);
            sidebar.setVisible (true);
            ChannelRack rack (song, audio);
            rack.setSize (1300, 600);

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            // Every group shut at the start: only their names showing
            const juce::StringArray groups { "NightShift kit", "Synth presets", "Plugin instruments", "Effects", "Plugin effects", "Drum kits", "Instruments" };
            auto onlyGroups = [&sidebar, &groups]
            {
                const auto shown = sidebar.getShownSounds();
                return shown.size() >= 6 && std::all_of (shown.begin(), shown.end(), [&groups] (const juce::String& s) { return groups.contains (s); });
            };

            check (onlyGroups(), "the sounds open with every group shut (" + sidebar.getShownSounds().joinIntoString (", ") + ")");

            juce::String added;
            sidebar.onAddKitSound = [&added] (const juce::String& name) { added = "kit:" + name; };
            sidebar.onAddSynth = [&added] (const juce::String& name) { added = "synth:" + name; };
            sidebar.onAddDrumKit = [&added] (const juce::String& id) { added = "drumkit:" + id; };

            juce::TextEditor* search = nullptr;

            for (auto* child : sidebar.getChildren())
                if (auto* editor = dynamic_cast<juce::TextEditor*> (child))
                    search = editor;

            // The box tells its listeners on the message loop, so let it run after each key
            auto settle = [] { settleMessages (30); };
            auto press = [search, &settle] (int keyCode)
            {
                search->keyPressed (juce::KeyPress (keyCode));
                settle();
            };

            check (search != nullptr, "the sidebar has a search box");

            if (search != nullptr)
            {
                search->setText ("sn", true);
                settle();
                press (juce::KeyPress::returnKey);
                check (added == "kit:Snare", "typing \"sn\" and pressing Enter adds Snare (" + added + ")");

                search->setText ("h", true);
                settle();
                press (juce::KeyPress::returnKey);
                check (added == "kit:Hat", "\"h\" matches Hat, Open Hat and Shaker; Enter adds the first, Hat (" + added + ")");

                if (drumkits::idForName ("Black Pearl").isNotEmpty())
                {
                    search->setText ("pearl", true);
                    settle();
                    press (juce::KeyPress::returnKey);
                    check (added == "drumkit:" + drumkits::idForName ("Black Pearl"), "\"pearl\" finds the Black Pearl drum kit (" + added + ")");
                }

                const auto preset = nova::defaultPreset();
                search->setText (preset, true);
                settle();
                press (juce::KeyPress::returnKey);
                check (added == "synth:" + preset, "searching a synth preset's name and pressing Enter adds it (" + added + ")");

                press (juce::KeyPress::escapeKey);
                check (search->isEmpty() && onlyGroups(), "Escape clears the search, and the groups are shut again");
            }

            // A scan you asked for opens Plugin instruments (and only that)
            {
                const auto before = sidebar.getShownSounds().size();
                sidebar.showPluginInstruments();
                const auto shown = sidebar.getShownSounds();
                const int at = shown.indexOf ("Plugin instruments");
                check (shown.size() > before && at >= 0 && at + 1 < shown.size() && ! groups.contains (shown[at + 1]),
                       "Plugin instruments opens after a scan finds some (" + juce::String (shown.size() - before) + " rows under it)");
            }

            // Plugin effects by developer, every developer shut to begin with (the user, 2026-11-15).
            // Made-up plugins beside whatever this computer has: one developer spelled two ways, a
            // Mac's Audio Unit twin, numbers in names, an effect that doesn't say who made it, and
            // an instrument by the same developer (it stays in Plugin instruments)
            {
                auto fake = [&audio] (const juce::String& name, const juce::String& developer, const juce::String& format = "VST3", bool instrument = false)
                {
                    juce::PluginDescription d;
                    d.name = name;
                    d.manufacturerName = developer;
                    d.pluginFormatName = format;
                    d.fileOrIdentifier = "C:\\nowhere\\" + developer + " " + name + (format == "VST3" ? ".vst3" : ".component");
                    d.uniqueId = (developer + name + format).hashCode();
                    d.isInstrument = instrument;
                    audio.getKnownPlugins().addType (d);
                };

                fake ("Tape Delay 10", "Lumen Audio");
                fake ("Glass Verb", "Lumen Audio", "AudioUnit");
                fake ("Glass Verb", "Lumen Audio");
                fake ("Tape Delay 2", "Lumen Audio");
                fake ("Wide Chorus", "LUMEN AUDIO");
                fake ("Big Comp", "Brightside DSP");
                fake ("Room Verb", "Brightside DSP, LLC");
                fake ("Mystery FX", {});
                fake ("Lumen Keys", "Lumen Audio", "VST3", true);
                settle();       // the list tells the sidebar on the message loop

                juce::String effectAdded;
                sidebar.onAddEffect = [&effectAdded] (const juce::PluginDescription& d) { effectAdded = d.name + " (" + d.pluginFormatName + ")"; };

                const juce::StringArray ours { "Glass Verb", "Tape Delay 2", "Tape Delay 10", "Wide Chorus", "Big Comp", "Room Verb", "Mystery FX" };

                // A company's legal ending isn't part of its name here: "Valhalla DSP, LLC" is Valhalla DSP
                {
                    juce::StringArray wrong;

                    for (auto [given, expected] : { std::pair<const char*, const char*> { "Valhalla DSP, LLC", "Valhalla DSP" }, { "Apple Inc.", "Apple" },
                                                    { "Plugin Boutique Pty Ltd", "Plugin Boutique" }, { "Toontrack Music AB", "Toontrack Music" },
                                                    { "Microsoft Corporation", "Microsoft" }, { "Steinberg Media Technologies GmbH", "Steinberg Media Technologies" },
                                                    { "Waves", "Waves" }, { "Atlas", "Atlas" }, { "Inc", "Inc" }, { "  Neural DSP ", "Neural DSP" } })
                        if (AudioEngine::developerName (given) != expected)
                            wrong.add (juce::String (given) + " -> " + AudioEngine::developerName (given));

                    check (wrong.isEmpty(), "developers' names lose ', LLC', 'Inc.', 'GmbH' and the like, and nothing else ("
                                              + (wrong.isEmpty() ? juce::String ("10 names") : wrong.joinIntoString ("; ")) + ")");
                }
                // A row in one group (a developer can be in both plugin groups: Lumen Audio is)
                auto click = [] (Sidebar& on, const juce::String& name, int clicks = 1, const juce::String& group = "Plugin effects")
                {
                    const auto area = on.showSoundRow (name, group);

                    if (area.isEmpty())
                        return false;

                    const auto at = area.getCentre().toFloat();
                    const auto now = juce::Time::getCurrentTime();
                    const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                              1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &on, &on, now, at, now, clicks, false);
                    on.mouseDown (e);

                    if (clicks == 2)
                        on.mouseDoubleClick (e);

                    return true;
                };

                // What's under Plugin effects, to the end of the list (it's the last group)
                auto underEffects = [&groups] (Sidebar& on)
                {
                    const auto shown = on.getShownSounds();
                    juce::StringArray rows;

                    for (int i = shown.indexOf ("Plugin effects") + 1; i > 0 && i < shown.size() && ! groups.contains (shown[i]); ++i)
                        rows.add (shown[i]);

                    return rows;
                };

                auto anyOfOurs = [&ours] (const juce::StringArray& rows)
                {
                    return std::any_of (ours.begin(), ours.end(), [&rows] (const juce::String& s) { return rows.contains (s); });
                };

                check (underEffects (sidebar).isEmpty() && click (sidebar, "Plugin effects"), "Plugin effects starts shut, and opens when clicked");

                auto developers = underEffects (sidebar);
                auto sorted = developers;
                sorted.removeString ("Unknown developer");
                sorted.sortNatural();
                sorted.add ("Unknown developer");
                check (developers.contains ("Lumen Audio") && developers.contains ("Brightside DSP") && ! anyOfOurs (developers)
                         && ! developers.contains ("LUMEN AUDIO") && ! developers.contains ("Lumen Keys") && developers == sorted,
                       "in it, one row per developer, A to Z and every one shut: " + developers.joinIntoString (", "));

                check (click (sidebar, "Lumen Audio"), "clicking Lumen Audio opens it");
                auto shown = sidebar.getShownSounds();
                auto at = shown.indexOf ("Lumen Audio", false, shown.indexOf ("Plugin effects"));
                juce::StringArray lumen;

                for (int i = at + 1; at >= 0 && i < shown.size() && ours.contains (shown[i]); ++i)
                    lumen.add (shown[i]);

                check (lumen == juce::StringArray { "Glass Verb", "Tape Delay 2", "Tape Delay 10", "Wide Chorus" } && ! shown.contains ("Big Comp")
                         && ! shown.contains ("Mystery FX"),
                       "under it its effects, A to Z (2 before 10), its other spelling's too, the Audio Unit twin once; the other developers stay shut ("
                         + lumen.joinIntoString (", ") + ")");

                check (click (sidebar, "Lumen Audio") && ! anyOfOurs (sidebar.getShownSounds()), "clicked again, it shuts");

                const bool doubleClicked = click (sidebar, "Brightside DSP") && click (sidebar, "Big Comp", 2);
                check (doubleClicked && effectAdded == "Big Comp (VST3)",
                       "an effect in an open developer goes on the channel when double-clicked (" + effectAdded + ")");

                {
                    shown = sidebar.getShownSounds();
                    at = shown.indexOf ("Brightside DSP", false, shown.indexOf ("Plugin effects"));
                    const auto area = sidebar.showSoundRow ("Brightside DSP", "Plugin effects");
                    const auto now = juce::Time::getCurrentTime();
                    const auto over = area.getCentre().toFloat();
                    sidebar.mouseMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), over, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                         &sidebar, &sidebar, now, over, now, 0, false));
                    const auto tip = sidebar.getTooltip();
                    check (at >= 0 && shown[at + 1] == "Big Comp" && shown[at + 2] == "Room Verb" && ! shown.contains ("Brightside DSP, LLC")
                             && tip.contains ("Brightside DSP, LLC") && tip.contains ("2 effects"),
                           "\"Brightside DSP, LLC\" is Brightside DSP: one row, both effects, its whole name in the tooltip (\"" + tip + "\")");
                }

                check (click (sidebar, "Unknown developer") && underEffects (sidebar).contains ("Mystery FX"),
                       "an effect that doesn't say who made it is under Unknown developer, last");

                sidebar.setSearchText ("lumen");
                settle();
                developers = underEffects (sidebar);
                check (developers == juce::StringArray { "Lumen Audio", "Glass Verb", "Tape Delay 2", "Tape Delay 10", "Wide Chorus" }
                         && sidebar.getShownSounds().contains ("Lumen Keys"),
                       "searching a developer's name finds all its effects, open under it (" + developers.joinIntoString (", ") + ")");

                sidebar.setSearchText ("delay");
                settle();
                developers = underEffects (sidebar);
                check (developers.contains ("Lumen Audio") && developers.contains ("Tape Delay 2") && developers.contains ("Tape Delay 10")
                         && ! developers.contains ("Glass Verb") && ! developers.contains ("Brightside DSP"),
                       "searching an effect's name shows it under its developer, and no developer without a match (" + developers.joinIntoString (", ") + ")");

                sidebar.setSearchText ({});
                settle();

                // NightShift opened again: every developer shut once more
                Sidebar fresh (song, audio);
                fresh.setSize (252, 800);
                check (click (fresh, "Plugin effects") && ! underEffects (fresh).isEmpty() && ! anyOfOurs (fresh.getShownSounds()),
                       "opened again, NightShift starts with every developer shut");

                // Steps' Add effect menu has them by developer too, a submenu each
                juce::StringArray menuDevelopers, lumenItems;
                const auto menu = rack.createAddEffectMenu (song.channels.front().id);

                for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                {
                    const auto& item = it.getItem();

                    if (item.subMenu == nullptr)
                        continue;

                    menuDevelopers.add (item.text);

                    if (item.text == "Lumen Audio")
                        for (juce::PopupMenu::MenuItemIterator sub (*item.subMenu); sub.next();)
                            lumenItems.add (sub.getItem().text);
                }

                check (menuDevelopers.contains ("Brightside DSP") && ! menuDevelopers.contains ("Brightside DSP, LLC") && menuDevelopers.contains ("Lumen Audio")
                         && menuDevelopers[menuDevelopers.size() - 1] == "Unknown developer"
                         && lumenItems == juce::StringArray { "Glass Verb", "Tape Delay 2", "Tape Delay 10", "Wide Chorus" },
                       "Steps' Add effect menu has a submenu per developer (" + menuDevelopers.joinIntoString (", ") + ")");

                // Plugin instruments by developer too (the user, 2026-11-15: "group plugin instruments by developer
                // too"), each group's developers opened apart: Lumen Audio makes an instrument (Lumen Keys) and effects
                {
                    auto underInstruments = [&groups] (Sidebar& on)
                    {
                        const auto rows = on.getShownSounds();
                        juce::StringArray under;

                        for (int i = rows.indexOf ("Plugin instruments") + 1; i > 0 && i < rows.size() && ! groups.contains (rows[i]); ++i)
                            under.add (rows[i]);

                        return under;
                    };

                    // NightShift opened again: Plugin instruments opens to its developers, every one shut
                    Sidebar again (song, audio);
                    again.setSize (252, 800);
                    const bool opened = click (again, "Plugin instruments", 1, "Plugin instruments");
                    auto rows = underInstruments (again);
                    check (opened && rows.contains ("Lumen Audio") && ! rows.contains ("Lumen Keys"),
                           "Plugin instruments opens to one row per developer, every one shut (" + rows.joinIntoString (", ") + ")");

                    const bool lumenOpened = click (again, "Lumen Audio", 1, "Plugin instruments");
                    rows = underInstruments (again);
                    const bool effectsShut = click (again, "Plugin effects") && ! underEffects (again).contains ("Glass Verb");
                    check (lumenOpened && rows.indexOf ("Lumen Keys") == rows.indexOf ("Lumen Audio") + 1 && effectsShut,
                           "clicking its Lumen Audio shows Lumen Keys, and Plugin effects' Lumen Audio stays shut (" + rows.joinIntoString (", ") + ")");

                    // A scan that found one opens Plugin instruments with that one's developer open
                    Sidebar scanned (song, audio);
                    scanned.setSize (252, 800);
                    scanned.showPluginInstruments ({ "Lumen Keys" });
                    rows = underInstruments (scanned);
                    check (rows.contains ("Lumen Keys") && rows.indexOf ("Lumen Keys") == rows.indexOf ("Lumen Audio") + 1,
                           "after a scan finds an instrument, its developer is open too (" + rows.joinIntoString (", ") + ")");

                    // Steps' and Arrange's add menu: Plugin instrument, a submenu per developer
                    juce::StringArray instrumentDevelopers, lumenInstruments;
                    const auto addMenu = rack.createAddMenu (nullptr);

                    for (juce::PopupMenu::MenuItemIterator it (addMenu); it.next();)
                        if (it.getItem().text == "Plugin instrument" && it.getItem().subMenu != nullptr)
                            for (juce::PopupMenu::MenuItemIterator developer (*it.getItem().subMenu); developer.next();)
                                if (developer.getItem().subMenu != nullptr)
                                {
                                    instrumentDevelopers.add (developer.getItem().text);

                                    if (developer.getItem().text == "Lumen Audio")
                                        for (juce::PopupMenu::MenuItemIterator plugin (*developer.getItem().subMenu); plugin.next();)
                                            lumenInstruments.add (plugin.getItem().text);
                                }

                    check (instrumentDevelopers.contains ("Lumen Audio") && lumenInstruments == juce::StringArray { "Lumen Keys" }
                             && ! instrumentDevelopers.contains ("Brightside DSP"),
                           "the add menu's Plugin instrument has a submenu per developer (" + instrumentDevelopers.joinIntoString (", ") + ")");
                }
            }

            const auto count = song.channels.size();
            check (rack.isInterestedInDragSource ({ juce::String (soundDragPrefix) + "kit:Clap", nullptr, {} })
                       && ! rack.isInterestedInDragSource ({ juce::String ("something else"), nullptr, {} }),
                   "Steps takes sounds dragged from the sidebar and nothing else");

            rack.itemDropped ({ juce::String (soundDragPrefix) + "kit:Clap", nullptr, { 400, 200 } });
            check (song.channels.size() == count + 1 && song.channels.back().sample == "kit:Clap"
                       && song.selectedChannelId == song.channels.back().id,
                   "dropping the kit's Clap adds a Clap channel and selects it");

            rack.itemDropped ({ juce::String (soundDragPrefix) + "synth:" + nova::defaultPreset(), nullptr, { 400, 200 } });
            check (song.channels.size() == count + 2 && song.channels.back().synthPreset == nova::defaultPreset(),
                   "dropping a synth preset adds a synth channel playing it");

            check (! rack.addDraggedSound (juce::String (soundDragPrefix) + "kit:No such sound") && song.channels.size() == count + 2,
                   "a sound that doesn't exist adds nothing");

            // Find new instruments, across the bottom of the sounds
            {
                auto& scan = sidebar.getScanButton();
                int scans = 0, lists = 0;
                sidebar.onScanInstruments = [&scans] { ++scans; };
                sidebar.onScanPlugins = [&lists] { ++lists; };
                sidebar.resized();
                check (scan.isVisible() && scan.getBottom() <= sidebar.getHeight() && scan.getY() >= sidebar.getHeight() - Sidebar::footerHeight
                         && scan.getWidth() > 200 && scan.getName() == "Find new instruments" && ! scan.getTooltip().contains ("VST"),
                       "Find new instruments sits across the bottom of the sounds, in plain words (" + scan.getBounds().toString() + ")");

                scan.triggerClick();
                settle();
                check (scans == 1 && lists == 0, "clicking it scans");

                const auto now = juce::Time::getCurrentTime();
                const auto at = scan.getLocalBounds().getCentre().toFloat();
                const juce::MouseEvent right (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier),
                                              1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &scan, &scan, now, at, now, 1, false);
                scan.mouseDown (right);
                scan.mouseUp (right);
                settle();
                check (lists == 1 && scans == 1, "right-clicking it opens the plugin list (for other folders) instead");

                sidebar.setScanning (true);
                const bool busy = sidebar.isScanning() && ! scan.isEnabled();
                scan.triggerClick();
                settle();
                sidebar.setScanning (false);
                check (busy && scans == 1 && scan.isEnabled(), "while a scan runs it can't be pressed again; after, it can");

                // What it says afterwards
                juce::KnownPluginList list;
                auto add = [&list] (const juce::String& name, bool instrument)
                {
                    juce::PluginDescription d;
                    d.name = name;
                    d.pluginFormatName = "VST3";
                    d.fileOrIdentifier = "C:\\nowhere\\" + name + ".vst3";
                    d.uniqueId = name.hashCode();
                    d.isInstrument = instrument;
                    list.addType (d);
                };

                add ("Surge XT", true);
                add ("Vital", true);
                add ("OTT", false);
                const auto both = MainComponent::describeNewPlugins ({ "Vital", "OTT" }, list, true);
                const auto effectOnly = MainComponent::describeNewPlugins ({ "OTT" }, list, true);
                const auto nothing = MainComponent::describeNewPlugins ({}, list, true);
                const auto quiet = MainComponent::describeNewPlugins ({}, list, false);
                check (both == "Found 1 new instrument: Vital, and 1 new effect: OTT" && effectOnly == "Found 1 new effect: OTT (no new instruments)"
                         && nothing.startsWith ("Nothing new found (you have 2 instruments)") && ! nothing.contains ("VST") && quiet.isEmpty(),
                       "afterwards it says what's new (\"" + both + "\"), or that nothing is when you asked (\"" + nothing + "\"), and nothing at startup");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--stepstest"))
        {
            // Steps on the demo's Beat pattern, with real mouse events sent to whatever is under
            // the point: pads on and off, the mute swatch (right-click solos), dragging and
            // resetting the level and pan bars, and a melody's preview opening the piano roll
            Song song;
            song.loadDemo();
            AudioEngine audio (song, false);
            ChannelRack rack (song, audio);
            rack.setSize (1300, 600);
            rack.setVisible (true);     // or nothing is found under the mouse
            int openedPianoRoll = -1;
            rack.onOpenPianoRoll = [&openedPianoRoll] (int id) { openedPianoRoll = id; };

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto send = [&rack] (juce::Point<float> from, juce::Point<float> to, bool right, int clicks)
            {
                auto* target = rack.getComponentAt (from.toInt());

                if (target == nullptr)
                    return;

                const auto now = juce::Time::getCurrentTime();
                const auto mods = juce::ModifierKeys (right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier);
                const auto start = target->getLocalPoint (&rack, from);
                auto event = [&] (juce::Point<float> p, bool dragged)
                {
                    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), target->getLocalPoint (&rack, p), mods,
                                             1.0f, 0.0f, 0.0f, 0.0f, 0.0f, target, target, now, start, now, clicks, dragged);
                };

                if (clicks == 2)
                {
                    target->mouseDoubleClick (event (from, false));
                    return;
                }

                target->mouseDown (event (from, false));

                if (from != to)
                    target->mouseDrag (event (to, true));

                target->mouseUp (event (to, from != to));
            };

            auto click = [&send] (juce::Point<float> p, bool right = false) { send (p, p, right, 1); };
            auto channel = [&song] (const juce::String& name) -> Channel& { for (auto& c : song.channels) if (c.name == name) return c; return song.channels.front(); };
            auto rowY = [&song] (const juce::String& name)
            {
                for (int i = 0; i < (int) song.channels.size(); ++i)
                    if (song.channels[(size_t) i].name == name)
                        return (float) (ChannelRack::headerHeight + i * ChannelRack::rowHeight + ChannelRack::rowHeight / 2);

                return 0.0f;
            };

            // At this width the pads are 40 px wide, 4 apart, with 8 more between beats
            auto padCentre = [] (int step) { return 268.0f + step * 44.0f + (step / 4) * 8.0f + 20.0f; };
            auto& beat = song.currentPattern();
            auto& kick = channel ("Kick");

            click ({ padCentre (1), rowY ("Kick") });
            check (steps::find (beat.notesFor (kick.id), 1, kick.rootKey) != nullptr, "clicking Kick's second pad turns it on");
            click ({ padCentre (1), rowY ("Kick") });
            check (steps::find (beat.notesFor (kick.id), 1, kick.rootKey) == nullptr, "clicking it again turns it off");

            click ({ 15.0f, rowY ("Snare") });
            check (channel ("Snare").muted, "clicking Snare's colour swatch mutes it");
            click ({ 15.0f, rowY ("Snare") });
            check (! channel ("Snare").muted, "clicking it again unmutes it");

            click ({ 15.0f, rowY ("Hat") }, true);
            const bool soloed = ! channel ("Hat").muted && channel ("Kick").muted && channel ("Snare").muted;
            click ({ 15.0f, rowY ("Hat") }, true);
            check (soloed && ! channel ("Kick").muted && ! channel ("Hat").muted, "right-clicking Hat's swatch solos it, and again un-solos");

            // Level bar: the full range across its 64 px; pan: -1..1 across the same
            const float levelY = rowY ("Kick") - 5.0f, panY = rowY ("Kick") + 5.0f;
            const float before = kick.volume;
            send ({ 200.0f, levelY }, { 216.0f, levelY }, false, 1);
            check (std::abs (kick.volume - (before + 0.25f)) < 0.02f,
                   "dragging Kick's level bar 16 px right raises it a quarter (" + juce::String (before, 2) + " to " + juce::String (kick.volume, 2) + ")");
            send ({ 200.0f, levelY }, { 200.0f, levelY }, false, 2);
            check (std::abs (kick.volume - 0.78f) < 0.001f, "double-clicking it resets to unity (0.78)");
            send ({ 220.0f, panY }, { 204.0f, panY }, false, 1);
            check (std::abs (kick.pan - (-0.5f)) < 0.03f, "dragging the pan bar 16 px left pans it half left (" + juce::String (kick.pan, 2) + ")");

            // A melody shows a preview; clicking it opens the piano roll on that channel
            for (auto& p : song.patterns)
                if (p.name == "Keys")
                    song.currentPatternId = p.id;

            song.notify (Song::Change::currentPattern);
            click ({ padCentre (6), rowY ("Keys") });
            check (openedPianoRoll == channel ("Keys").id, "clicking the Keys melody opens the piano roll on Keys");

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--drumtest"))
        {
            // The AVL kits: found, read, their drums named and mapped for the drummer; a kit
            // channel playing with velocity layers and the hi-hat choke; and grooves (written
            // here as the drummer would answer) played into patterns with swing, push, flams,
            // a time signature change, and laid out in Arrange. No AI is asked
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            const auto ids = drumkits::ids();
            const auto zeppelin = drumkits::idForName ("Red Zeppelin");
            check (ids.size() >= 5 && zeppelin.isNotEmpty(), "the kits are found: " + [&] { juce::StringArray n; for (auto& i : ids) n.add (drumkits::displayName (i)); return n.joinIntoString (", "); }());

            auto kit = drumkits::load (zeppelin);
            const bool loaded = kit != nullptr && kit->pieces.size() > 20 && ! kit->samples.empty();
            check (loaded, "Red Zeppelin loads: " + juce::String (kit != nullptr ? (int) kit->pieces.size() : 0) + " pieces, "
                             + juce::String (kit != nullptr ? (int) kit->samples.size() : 0) + " samples");

            if (! loaded)
                return 1;

            juce::String map;

            for (auto v : { "kick", "snare", "hat_closed", "hat_pedal", "hat_open", "tom_high", "tom_floor", "ride", "ride_bell", "crash", "crash_2", "china" })
                map << v << "=" << drumkits::keyForVoice (*kit, v) << " ";

            std::cout << "      " << map << std::endl;
            check (drumkits::keyForVoice (*kit, "kick") == 36 && drumkits::keyForVoice (*kit, "snare") == 38 && drumkits::keyForVoice (*kit, "hat_closed") == 42
                     && drumkits::keyForVoice (*kit, "hat_open") == 46 && drumkits::keyForVoice (*kit, "tom_floor") == 41 && drumkits::keyForVoice (*kit, "ride") == 51
                     && drumkits::keyForVoice (*kit, "crash") == 49 && drumkits::keyForVoice (*kit, "crash_2") == 57 && drumkits::keyForVoice (*kit, "china") == 62,
                   "the drummer's drums map to the kit's keys");
            check (kit->findPiece (42) != nullptr && kit->findPiece (42)->name == "Hat Closed" && kit->findPiece (36)->regions.size() == 5,
                   "pieces are named from the kit (42: " + (kit->findPiece (42) != nullptr ? kit->findPiece (42)->name : juce::String()) + ") with 5 velocity layers");

            // A kit channel playing
            Song song;
            song.loadEmpty();
            song.bpm = 120.0;
            auto& drums = song.addChannel ("Drums", InstrumentType::kit);
            drums.kit = zeppelin;
            const int drumsId = drums.id;

            for (auto& c : song.channels)
                c.muted = c.id != drumsId;

            auto& pattern = song.currentPattern();
            pattern.notesFor (drumsId) = { { 0, 24, 36, 127 }, { ticksPerBeat, 24, 38, 40 }, { 2 * ticksPerBeat, 24, 38, 120 }, { 3 * ticksPerBeat, 24, 46, 110 } };
            song.notify (Song::Change::everything);
            AudioEngine audio (song, false);
            audio.syncNow();

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-drumtest.wav");

            auto renderNow = [&]
            {
                audio.renderToFile (file, 1.0);
                juce::AudioBuffer<float> b;

                if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr)
                {
                    b.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
                    reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                }

                return b;
            };

            auto levelDb = [] (const juce::AudioBuffer<float>& b, double from, double to)
            {
                const int start = (int) (from * 48000.0), n = (int) ((to - from) * 48000.0);
                return start + n <= b.getNumSamples() ? juce::Decibels::gainToDecibels (b.getRMSLevel (0, start, n), -200.0f) : -200.0f;
            };

            auto b = renderNow();
            const float kick = levelDb (b, 0.0, 0.1), soft = levelDb (b, 0.5, 0.6), hard = levelDb (b, 1.0, 1.1);
            check (kick > -30.0f && hard - soft > 8.0f, "the kit plays: kick " + juce::String (kick, 1) + " dB; a snare at velocity 120 is "
                                                          + juce::String (hard - soft, 1) + " dB over one at 40 (another layer)");

            // An open hat on its own rings on; the softest touch of the pedal 100 ms later closes it
            pattern.notesFor (drumsId) = { { 0, 24, 46, 127 } };
            song.notify (Song::Change::notes);
            audio.syncNow();
            b = renderNow();
            const float ringing = levelDb (b, 0.15, 0.35);
            pattern.notesFor (drumsId).push_back ({ 19, 24, 44, 1 });
            song.notify (Song::Change::notes);
            audio.syncNow();
            b = renderNow();
            const float choked = levelDb (b, 0.15, 0.35);
            check (choked < ringing - 10.0f, "the hat pedal chokes the open hat (" + juce::String (ringing, 1) + " -> " + juce::String (choked, 1) + " dB)");
            file.deleteFile();

            // A groove, as the drummer would write it: swung lofi in 4/4
            const juce::String lofi = R"({ "title": "Dusty pocket", "message": "Lazy swung 8ths.", "tempo": 0, "meter": "4/4",
                "feel": { "swing": 1.0, "swing_note": "8th", "looseness_ms": 0, "push": [ { "drum": "snare", "ms": 20 } ] },
                "sections": [ { "name": "Verse", "bars": [ { "steps": 16, "lanes": [
                    { "drum": "kick", "hits": "9.......5.9....." },
                    { "drum": "snare", "hits": "....9..g....9..." },
                    { "drum": "hat_closed", "hits": "6.4.6.4.6.4.6.4.EXTRA" } ] } ] },
                  { "name": "Fill", "bars": [ { "steps": 12, "lanes": [ { "drum": "tom_floor", "hits": "f..r..9..9.." } ] } ] } ],
                "arrangement": [ "Verse", "Verse", "Fill" ] })";

            auto groove = drummer::Groove::fromJson (juce::JSON::parse (lofi));
            check (groove.has_value() && groove->sections.size() == 2 && groove->sections[0].bars[0].lanes[2].hits.length() == 16,
                   "the drummer's answer reads as a groove, its long lane cut to the bar's 16 steps");

            if (groove.has_value())
            {
                auto keyFor = [&kit] (const juce::String& voice) { return drumkits::keyForVoice (*kit, voice); };
                drummer::render (song, drumsId, *groove, keyFor);
                auto& d = song.findChannel (drumsId)->drummer;
                auto verse = song.findPattern (d.patterns["Verse"]);
                auto fill = song.findPattern (d.patterns["Fill"]);
                check (verse != nullptr && fill != nullptr && verse->name == "Drums Verse", "each section becomes a pattern (\"Drums Verse\", \"Drums Fill\")");

                if (verse != nullptr && fill != nullptr)
                {
                    const auto& notes = *verse->findNotes (drumsId);
                    int hats = 0, ghost = -1, swungHat = -1, snare = -1;

                    for (auto& n : notes)
                    {
                        hats += n.key == 42 ? 1 : 0;

                        if (n.key == 38 && n.velocity < 40)
                            ghost = n.velocity;

                        if (n.key == 42 && n.start > ticksPerBeat / 2 - 4 && n.start < ticksPerBeat)
                            swungHat = n.start;

                        if (n.key == 38 && n.velocity > 100 && snare < 0)
                            snare = n.start;
                    }

                    // Full swing moves the off-beat 8th from 48 to 64 ticks; the snare's +20 ms is 3.84 ticks at 120 BPM
                    check (hats == 8 && swungHat == 64, "8 hats, the off-beat ones swung to a triplet (tick " + juce::String (swungHat) + ", straight would be 48)");
                    check (snare == ticksPerBeat + 4 && ghost >= 24 && ghost <= 34, "the snare sits 20 ms late (tick " + juce::String (snare) + ") and the ghost is soft (velocity " + juce::String (ghost) + ", 28 give or take the looseness)");

                    const auto& fillNotes = *fill->findNotes (drumsId);
                    check (fillNotes.size() == 2 + 3 + 2, "a flam adds a grace note and a drag two (" + juce::String ((int) fillNotes.size()) + " notes)");
                }

                // Played again with the feel turned down: straight, no push
                d.swing = 0.0f;
                d.looseness = 0.0f;
                drummer::render (song, drumsId, *groove, keyFor);
                bool straight = true;

                for (auto& n : *song.findPattern (d.patterns["Verse"])->findNotes (drumsId))
                    straight = straight && n.start % ticksPerStep == 0;

                check (straight && song.findPattern (d.patterns["Verse"]) == verse, "with swing and looseness at 0 every hit is on the grid, in the same pattern");

                drummer::placeInArrange (song, drumsId, *groove);
                int placed = 0, end = 0;

                for (auto& clip : song.playlist)
                    if (clip.patternId == d.patterns["Verse"] || clip.patternId == d.patterns["Fill"])
                    {
                        ++placed;
                        end = std::max (end, clip.start + song.patternLength (*song.findPattern (clip.patternId)));
                    }

                check (placed == 3 && end == 3 * song.ticksPerBar(), "Put in Arrange lays out Verse, Verse, Fill one after another");
            }

            // Prog in 7/8: the song follows
            const juce::String prog = R"({ "title": "Seven", "message": "7/8.", "tempo": 140, "meter": "7/8",
                "feel": { "swing": 0, "swing_note": "8th", "looseness_ms": 2, "push": [] },
                "sections": [ { "name": "Riff", "bars": [ { "steps": 14, "lanes": [ { "drum": "kick", "hits": "9...9...9.9..." } ] } ] } ],
                "arrangement": [] })";

            if (auto progGroove = drummer::Groove::fromJson (juce::JSON::parse (prog)))
            {
                drummer::render (song, drumsId, *progGroove, [&kit] (const juce::String& v) { return drumkits::keyForVoice (*kit, v); });
                check (song.meterBeats == 7 && song.meterUnit == 8 && juce::roundToInt (song.bpm) == 140,
                       "a groove in 7/8 at 140 BPM switches the song to them");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--metertest"))
        {
            // 7/8: a bar is 14 steps; Tracktion counts in eighths; a kick on the first step of
            // each bar lands every 3.5 beats in a render; Steps shows 14 pads and a click on
            // the last sets step 14; the time signature is saved
            Song song;
            song.loadEmpty();
            song.bpm = 120.0;
            song.meterBeats = 7;
            song.meterUnit = 8;
            song.mode = PlayMode::song;
            const int kick = song.channels.front().id;     // an empty song starts with the kit's Kick
            auto& pattern = song.currentPattern();
            steps::set (pattern.notesFor (kick), 0, true, defaultRootKey, 127);

            for (int bar = 0; bar < 3; ++bar)
                song.playlist.push_back ({ pattern.id, 0, bar * song.ticksPerBar() });

            song.notify (Song::Change::everything);
            AudioEngine audio (song, false);
            audio.syncNow();

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            check (song.ticksPerBar() == 336 && song.stepsPerBar() == 14 && song.patternLength (pattern) == 336,
                   "a 7/8 bar is 336 ticks, 14 steps, and a one-bar pattern is that long");

            auto sig = audio.getEdit().tempoSequence.getTimeSig (0);
            check (sig != nullptr && sig->numerator.get() == 7 && sig->denominator.get() == 8, "Tracktion counts in 7/8 (for its metronome and count-in)");

            // Where the kicks land: the first loud sample after each silence
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-metertest.wav");
            audio.renderToFile (file, 0.5);
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            juce::Array<double> onsets;

            if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr)
            {
                juce::AudioBuffer<float> b ((int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                int quietFor = 48000;

                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    if (std::abs (b.getSample (0, i)) > 0.05f)
                    {
                        if (quietFor > 24000)
                            onsets.add (i / 48000.0);

                        quietFor = 0;
                    }
                    else
                    {
                        ++quietFor;
                    }
                }
            }

            file.deleteFile();
            juce::String where;

            for (auto t : onsets)
                where << juce::String (t, 3) << " s  ";

            std::cout << "      kicks at " << where << std::endl;
            check (onsets.size() == 3 && std::abs (onsets[0]) < 0.005 && std::abs (onsets[1] - 1.75) < 0.005 && std::abs (onsets[2] - 3.5) < 0.005,
                   "rendered, the kicks land a 7/8 bar apart: 0, 1.75 and 3.5 s at 120 BPM");

            // Steps: 14 pads; the last one, really clicked
            ChannelRack rack (song, audio);
            rack.setSize (1300, 400);
            rack.setVisible (true);
            const float padWidth = 40.0f;
            const auto lastPad = juce::Point<float> (268.0f + 13 * (padWidth + 4.0f) + 6 * 8.0f + padWidth / 2,
                                                     (float) (ChannelRack::headerHeight + ChannelRack::rowHeight / 2));

            if (auto* target = rack.getComponentAt (lastPad.toInt()))
            {
                const auto now = juce::Time::getCurrentTime();
                const auto p = target->getLocalPoint (&rack, lastPad);
                const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), p,
                                          juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          target, target, now, p, now, 1, false);
                target->mouseDown (e);
                target->mouseUp (e);
            }

            const auto* last = steps::find (*pattern.findNotes (kick), 13, defaultRootKey);
            check (last != nullptr && last->start == 13 * ticksPerStep, "clicking the 14th pad in Steps sets the last step of the 7/8 bar");

            Song copy;
            copy.fromVar (song.toVar());
            check (copy.meterBeats == 7 && copy.meterUnit == 8, "the time signature survives saving and loading");

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--mixertest"))
        {
            // The mixer in the whole studio, with real mouse events sent to whatever is under
            // the point: F9 opens it with the height to itself (and the Inspector out of the
            // way), + Bus adds a bus, a reverb bus comes with its reverb all wet, a send drags
            // sideways and resets, a bus's fader, mute and solo work, a stock effect's panel
            // sets its parameters, and leaving gives Arrange and the Inspector back
            Song song;
            song.loadDemo();
            AudioEngine audio (song, false);
            audio.syncNow();
            MainComponent main (song, audio);
            main.setSize (1600, 940);
            main.setVisible (true);
            using Panel = MainComponent::Panel;

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto send = [&main] (juce::Component& on, juce::Point<float> local, int clicks, juce::Point<float> dragBy = {})
            {
                const auto at = main.getLocalPoint (&on, local);
                auto* target = main.getComponentAt (at.toInt());

                if (target == nullptr)
                    return;

                const auto now = juce::Time::getCurrentTime();
                const auto start = target->getLocalPoint (&main, at);
                auto event = [&] (juce::Point<float> p, bool dragged)
                {
                    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p,
                                             juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                             target, target, now, start, now, clicks, dragged);
                };

                target->mouseDown (event (start, false));

                if (! dragBy.isOrigin())
                    target->mouseDrag (event (start + dragBy, true));

                target->mouseUp (event (start + dragBy, ! dragBy.isOrigin()));

                if (clicks == 2)
                    target->mouseDoubleClick (event (start, false));
            };

            auto click = [&send] (juce::Component& on, juce::Point<float> p) { send (on, p, 1); };
            auto idOf = [&song] (const juce::String& name) { for (auto& c : song.channels) if (c.name == name) return c.id; return 0; };
            auto& mixer = main.getMixer();

            main.togglePanel (Panel::mixer);
            check (main.isPanelVisible (Panel::mixer) && ! main.isPanelVisible (Panel::playlist) && ! main.isInspectorVisible()
                     && [&] { auto* under = main.getComponentAt (main.getLocalPoint (&mixer, mixer.getLocalBounds().getCentre())); return under == &mixer || mixer.isParentOf (under); }()
                     && mixer.findStrip (idOf ("Kick")) != nullptr && mixer.findStrip (idOf ("Pad")) != nullptr,
                   "F9 opens the mixer with a strip per channel, the height to itself and the Inspector out of the way");

            // + Bus, really clicked (scrolled into view first)
            auto& addBus = mixer.getAddBusButton();
            if (auto* viewport = addBus.findParentComponentOfClass<juce::Viewport>())
                viewport->setViewPosition (addBus.getParentComponent()->getWidth(), 0);

            click (addBus, addBus.getLocalBounds().getCentre().toFloat());
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            const int busId = song.buses.empty() ? 0 : song.buses.back().id;
            check (song.buses.size() == 1 && mixer.findStrip (busId) != nullptr, "clicking + Bus adds a bus with its own strip");

            // A reverb bus, as + Send's menu makes it, and a send from the keys
            const int verb = main.getRack().addBus ("Reverb", "nightshiftReverb");
            auto verbBus = song.findBus (verb);
            auto verbFx = verbBus != nullptr && ! verbBus->effects.empty() ? dynamic_cast<StockEffect*> (audio.getEffect (verbBus->effects[0].id)) : nullptr;
            check (verbFx != nullptr && dynamic_cast<NightReverb*> (verbFx) != nullptr && std::abs (verbFx->findParam ("mix")->get() - 1.0f) < 1.0e-4f,
                   "a new reverb bus comes with a reverb on it, all wet");

            const int keys = idOf ("Keys");
            song.findChannel (keys)->sends.push_back ({ verb, 0.78f, false });
            song.notify (Song::Change::mixer);
            auto* keysStrip = mixer.findStrip (keys);

            if (auto* viewport = keysStrip != nullptr ? keysStrip->findParentComponentOfClass<juce::Viewport>() : nullptr)
                viewport->setViewPosition (keysStrip->getX() - 20, 0);

            const auto sendArea = keysStrip != nullptr ? keysStrip->getSendArea (verb) : juce::Rectangle<int>();
            check (! sendArea.isEmpty(), "the keys' strip shows its send to the reverb bus");

            if (! sendArea.isEmpty())
            {
                send (*keysStrip, sendArea.getCentre().toFloat(), 1, { -80.0f, 0.0f });
                const float dragged = song.findChannel (keys)->sends[0].level;
                send (*keysStrip, sendArea.getCentre().toFloat(), 2);
                check (std::abs (dragged - 0.28f) < 0.01f && std::abs (song.findChannel (keys)->sends[0].level - 0.78f) < 1.0e-4f,
                       "dragging the send 80 px left takes it down (" + juce::String (dragged, 2) + "), double-click puts it back to 0 dB");
            }

            // The bus's fader, mute and solo
            if (auto* busStrip = mixer.findStrip (verb))
            {
                if (auto* viewport = busStrip->findParentComponentOfClass<juce::Viewport>())
                    viewport->setViewPosition (busStrip->getX() - 20, 0);

                auto& fader = busStrip->getFader();
                send (fader, fader.getLocalBounds().getCentre().toFloat(), 1, { 0.0f, 50.0f });
                check (song.findBus (verb)->volume < 0.7f, "dragging the reverb bus's fader down turns it down");

                click (*busStrip, busStrip->getMuteArea().getCentre().toFloat());
                const bool muted = song.findBus (verb)->muted;
                click (*busStrip, busStrip->getMuteArea().getCentre().toFloat());
                check (muted && ! song.findBus (verb)->muted, "its Mute mutes and unmutes the bus");

                click (*busStrip, busStrip->getSoloArea().getCentre().toFloat());
                bool onlyKeys = true;

                for (auto& c : song.channels)
                    onlyKeys = onlyKeys && (c.muted == (c.id != keys));

                click (*busStrip, busStrip->getSoloArea().getCentre().toFloat());
                bool allBack = true;

                for (auto& c : song.channels)
                    allBack = allBack && ! c.muted;

                check (onlyKeys && allBack, "its Solo leaves only what sends to it (the keys), and again brings everything back");
            }

            // A stock effect's panel: a knob turned sets the parameter
            if (verbFx != nullptr)
            {
                auto panel = createStockPanel (*verbFx, song, audio);
                auto* knob = panel != nullptr ? panel->findChildWithID ("decay") : nullptr;
                auto* slider = knob != nullptr ? dynamic_cast<juce::Slider*> (knob->getChildComponent (0)) : nullptr;

                if (slider != nullptr)
                    slider->setValue (4.5, juce::sendNotificationSync);

                check (slider != nullptr && std::abs (verbFx->findParam ("decay")->get() - 4.5f) < 0.01f, "turning the reverb panel's Decay knob sets its decay");

                verbFx->findParam ("size")->set (0.2f);
                settleMessages (80);
                auto* sizeKnob = dynamic_cast<juce::Slider*> (panel->findChildWithID ("size")->getChildComponent (0));
                check (sizeKnob != nullptr && std::abs (sizeKnob->getValue() - 0.2) < 0.01, "and a change from elsewhere moves its knob");
            }

            // Leaving gives Arrange and the Inspector back
            main.setPanelVisible (Panel::rack, true);
            check (main.isPanelVisible (Panel::rack) && main.isPanelVisible (Panel::playlist) && main.isInspectorVisible(),
                   "going to Steps gives Arrange its share and the Inspector back");

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--bustest"))
        {
            // Buses through the real engine, rendered: a channel playing into a bus is heard
            // through the bus's fader and mute, a bus's effects work on what arrives, a send
            // reaches its bus before or after the channel's fader and at its level, deleting a
            // bus sends its channels back to the master, buses survive saving, the bus meters,
            // and a compressor ducks the keys to a kick it can't hear (sidechain)
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-bustest");
            folder.createDirectory();

            // A steady 440 Hz tone as a sample (a synth varies a little from render to render)
            const auto toneFile = folder.getChildFile ("tone.wav");
            {
                juce::AudioBuffer<float> tone (1, 48000 * 3);

                for (int i = 0; i < tone.getNumSamples(); ++i)
                    tone.setSample (0, i, 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / 48000.0));

                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (toneFile);
                auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                                  .withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));

                if (writer != nullptr)
                    writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
            }

            Song song;
            song.loadEmpty();
            song.bpm = 120.0;
            song.mode = PlayMode::pattern;
            auto& keysChannel = song.addChannel ("Tone", InstrumentType::sampler);
            keysChannel.sample = toneFile.getFullPathName();
            const int keys = keysChannel.id;
            song.currentPattern().notesFor (keys) = { { 0, ticksPerBar, defaultRootKey, 127 } };
            song.notify (Song::Change::everything);
            AudioEngine audio (song, false);
            audio.syncNow();

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            int renders = 0;

            auto render = [&]
            {
                song.notify (Song::Change::mixer);
                audio.syncNow();
                const auto file = folder.getChildFile ("render" + juce::String (renders++) + ".wav");
                audio.renderToFile (file, 0.2);
                juce::AudioBuffer<float> b;

                if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr)
                {
                    b.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
                    reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                }

                return b;
            };

            // The middle of the bar (it's 2 s at 120 BPM)
            auto levelDb = [&]
            {
                auto b = render();
                return b.getNumSamples() > 96000 ? juce::Decibels::gainToDecibels (b.getRMSLevel (0, 9600, 76800), -200.0f) : -200.0f;
            };

            auto channel = [&song] (int id) -> Channel& { return *song.findChannel (id); };
            auto bus = [&song] (int id) -> Bus& { return *song.findBus (id); };

            const float direct = levelDb();
            check (direct > -40.0f, "the tone straight to the master: " + juce::String (direct, 1) + " dB");

            // Into a bus
            const int busA = song.addBus ("Bus A").id;
            song.notify (Song::Change::channels);
            channel (keys).output = busA;
            const float viaBus = levelDb();
            check (std::abs (viaBus - direct) < 0.5f, "played into a bus at 0 dB it's as loud (" + juce::String (viaBus, 1) + " dB)");

            bus (busA).volume = 0.0f;
            const float busDown = levelDb();
            bus (busA).volume = 0.78f;
            bus (busA).muted = true;
            const float busMuted = levelDb();
            bus (busA).muted = false;
            check (busDown < direct - 60.0f && busMuted < direct - 60.0f, "the bus's fader down, or its mute, silences it");

            // An effect on the bus: a low pass at 200 Hz takes most of the 440 Hz tone away
            Effect lowPass;
            lowPass.id = song.createId();
            lowPass.pluginId = stockfx::descriptions()[5].createIdentifierString();
            lowPass.name = "Nightshift Filter";
            bus (busA).effects.push_back (lowPass);
            song.notify (Song::Change::mixer);
            audio.syncNow();

            if (auto f = dynamic_cast<StockEffect*> (audio.getEffect (lowPass.id)))
                f->findParam ("cutoff")->set (200.0f);

            const float filtered = levelDb();
            check (audio.getEffect (lowPass.id) != nullptr && filtered < direct - 10.0f,
                   "a 200 Hz low pass on the bus takes the tone down to " + juce::String (filtered, 1) + " dB");
            bus (busA).effects.clear();
            channel (keys).output = 0;

            // Sends. With the tone's fader down only a send before the fader is heard
            const int busS = song.addBus ("Sends").id;
            song.notify (Song::Change::channels);
            channel (keys).sends = { { busS, 0.78f, true } };
            channel (keys).volume = 0.0f;
            const float preFaderDown = levelDb();
            channel (keys).sends[0].preFader = false;
            const float postFaderDown = levelDb();
            check (std::abs (preFaderDown - direct) < 0.3f && postFaderDown < direct - 60.0f,
                   "with the tone's fader down, a 0 dB send before the fader is as loud as the tone was ("
                     + juce::String (preFaderDown, 1) + " dB), and one after it is silent");

            // Both together: the same sound twice, 6 dB up; at half the send knob (-12 dB), 1.9 dB up
            channel (keys).volume = 0.78f;
            const float both = levelDb();
            channel (keys).sends[0].level = 0.39f;
            const float halfSend = levelDb();
            check (std::abs (both - direct - 6.02f) < 0.3f && std::abs (halfSend - direct - 1.94f) < 0.3f,
                   "with its own output too it's +" + juce::String (both - direct, 2) + " dB (expect +6.02), and +"
                     + juce::String (halfSend - direct, 2) + " dB with the send at -12 dB (expect +1.94)");

            // Muting a bus mutes what plays into it, sends and all (as FL does)
            const int hidden = song.addBus ("Hidden").id;
            song.notify (Song::Change::channels);
            bus (hidden).muted = true;
            channel (keys).output = hidden;
            const float intoMuted = levelDb();
            check (intoMuted < direct - 60.0f, "into a muted bus, the tone and its send are silent");

            // Saved and loaded
            {
                Song copy;
                copy.fromVar (song.toVar());
                auto c = copy.findChannel (keys);
                check (copy.buses.size() == song.buses.size() && c != nullptr && c->output == hidden && c->sends.size() == 1
                         && c->sends[0].busId == busS && std::abs (c->sends[0].level - 0.39f) < 1.0e-4f && copy.findBus (hidden)->muted,
                       "buses, outputs and sends survive saving and loading");
            }

            // Deleting the bus the tone plays into puts it back on the master
            song.removeBus (hidden);
            song.notify (Song::Change::channels);
            const float back = levelDb();
            check (channel (keys).output == 0 && song.findBus (hidden) == nullptr && std::abs (back - halfSend) < 0.3f,
                   "deleting its bus puts the tone back on the master (" + juce::String (back - direct, 2) + " dB)");

            // The bus's meter while playing
            {
                audio.togglePlay();
                juce::AudioBuffer<float> buffer (2, 512);
                float peak = 0.0f;

                for (int b = 0; b < 192; ++b)
                {
                    buffer.clear();
                    audio.processHostedBlock (buffer);

                    if (b % 64 == 63)
                    {
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
                        audio.readMeters ({ busS });
                        peak = std::max (peak, audio.getPeak (busS).first);
                    }
                }

                audio.stop();
                check (peak > 0.01f, "the send bus's meter moves while it plays (peak " + juce::String (peak, 3) + ")");
                audio.readMeters ({});
            }

            // Sidechain: a kick nobody hears (into a bus at -inf) ducks the keys
            {
                channel (keys).sends.clear();
                auto& kickChannel = song.addChannel ("Kick", InstrumentType::sampler);
                kickChannel.sample = "kit:Kick";
                const int kick = kickChannel.id;
                const int silent = song.addBus ("Silent").id;
                song.findBus (silent)->volume = 0.0f;
                channel (kick).output = silent;

                for (int step : { 0, 4, 8, 12 })
                    steps::set (song.currentPattern().notesFor (kick), step, true, defaultRootKey, 120);

                Effect compressor;
                compressor.id = song.createId();
                compressor.pluginId = stockfx::descriptions()[1].createIdentifierString();
                compressor.name = "Nightshift Compressor";
                channel (keys).effects.push_back (compressor);
                song.notify (Song::Change::channels);
                song.notify (Song::Change::notes);
                audio.syncNow();

                auto comp = dynamic_cast<NightCompressor*> (audio.getEffect (compressor.id));

                if (comp != nullptr)
                {
                    comp->findParam ("threshold")->set (-24.0f);
                    comp->findParam ("ratio")->set (20.0f);
                    comp->findParam ("attack")->set (0.5f);
                    comp->findParam ("release")->set (60.0f);
                }

                // How far the tone drops 50 ms after each beat, against just before the next (the
                // kit's kick rings for most of a beat)
                auto dip = [&]
                {
                    auto b = render();
                    double total = 0.0;

                    for (int beat = 0; beat < 4; ++beat)
                    {
                        const int hit = beat * 24000;
                        const float after = juce::Decibels::gainToDecibels (b.getRMSLevel (0, hit + 2400, 1200), -200.0f);
                        const float before = juce::Decibels::gainToDecibels (b.getRMSLevel (0, hit + 22800, 1200), -200.0f);
                        total += before - after;
                    }

                    return (float) (total / 4.0);
                };

                const float without = dip();

                if (comp != nullptr)
                    comp->sidechain = kick;

                const float with = dip();
                check (comp != nullptr && comp->sidechainConnected.load() && std::abs (without) < 2.0f && with > 6.0f,
                       "a compressor on the keys listening to the kick ducks them on each beat by " + juce::String (with, 1)
                         + " dB (without the sidechain: " + juce::String (without, 1) + " dB)");
            }

            folder.deleteRecursively();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--stockfxtest"))
        {
            // Each stock effect on known signals, straight through applyToBuffer at 48 kHz: the
            // EQ and filter against the curves their editors draw, the compressor against its
            // static curve and timing, the reverb's decay time and stability, the delay's echo
            // times and levels in time with the song, ping-pong, and the chorus
            Song song;
            song.loadEmpty();
            song.bpm = 120.0;
            song.notify (Song::Change::transport);
            AudioEngine audio (song, false);
            audio.syncNow();

            static constexpr double sr = 48000.0;
            static constexpr int block = 512;
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            std::vector<te::Plugin::Ptr> keep;    // the plugin cache lets go of plugins nobody holds
            auto make = [&audio, &keep] (const char* type) -> StockEffect*
            {
                auto plugin = audio.getEdit().getPluginCache().createNewPlugin (type, {});
                keep.push_back (plugin);
                auto fx = dynamic_cast<StockEffect*> (plugin.get());

                if (fx != nullptr)
                    fx->initialise ({ te::TimePosition(), sr, block });

                return fx;
            };

            auto set = [] (StockEffect& fx, const char* id, float v)
            {
                if (auto p = fx.findParam (id))
                    p->set (v);
                else
                    std::cout << "no parameter " << id << std::endl;
            };

            // Through in blocks, as the engine would play it
            auto run = [] (te::Plugin& p, juce::AudioBuffer<float>& b)
            {
                for (int start = 0; start < b.getNumSamples(); start += block)
                {
                    const int n = std::min (block, b.getNumSamples() - start);
                    const te::TimeRange time (te::TimePosition::fromSeconds (start / sr), te::TimePosition::fromSeconds ((start + n) / sr));
                    te::PluginRenderContext rc (&b, start, n, nullptr, 0.0, time, true, false, true, false);
                    p.applyToBuffer (rc);
                }
            };

            auto sine = [] (double freq, float amplitude, int samples)
            {
                juce::AudioBuffer<float> b (2, samples);

                for (int i = 0; i < samples; ++i)
                    b.setSample (0, i, amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sr));

                b.copyFrom (1, 0, b, 0, 0, samples);
                return b;
            };

            auto noise = [] (int samples, float amplitude)
            {
                juce::Random random (42);
                juce::AudioBuffer<float> b (2, samples);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < samples; ++i)
                        b.setSample (ch, i, amplitude * (random.nextFloat() * 2.0f - 1.0f));

                return b;
            };

            auto impulse = [] (int samples)
            {
                juce::AudioBuffer<float> b (2, samples);
                b.clear();
                b.setSample (0, 0, 1.0f);
                b.setSample (1, 0, 1.0f);
                return b;
            };

            auto rmsDb = [] (const juce::AudioBuffer<float>& b, int ch, int from, int to)
            {
                return juce::Decibels::gainToDecibels (b.getRMSLevel (ch, from, to - from), -200.0f);
            };

            // Gain on a steady sine (its second half), in dB
            auto sineGainDb = [&] (te::Plugin& p, double freq)
            {
                p.reset();
                auto b = sine (freq, 0.25f, (int) sr);
                run (p, b);
                return rmsDb (b, 0, (int) sr / 2, (int) sr) - juce::Decibels::gainToDecibels (0.25f / std::sqrt (2.0f));
            };

            auto energyAround = [] (const juce::AudioBuffer<float>& b, int ch, int at, int halfWidth)
            {
                double e = 0.0;

                for (int i = std::max (0, at - halfWidth); i < std::min (b.getNumSamples(), at + halfWidth); ++i)
                    e += b.getSample (ch, i) * b.getSample (ch, i);

                return e;
            };

            auto allFinite = [] (const juce::AudioBuffer<float>& b, float limit)
            {
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        if (! std::isfinite (b.getSample (ch, i)) || std::abs (b.getSample (ch, i)) > limit)
                            return false;

                return true;
            };

            //==============================================================================
            // EQ
            if (auto eq = dynamic_cast<NightEQ*> (make (NightEQ::xmlTypeName)))
            {
                auto in = noise ((int) sr / 2, 0.3f), out = in;
                run (*eq, out);
                float worst = 0.0f;

                for (int i = 0; i < in.getNumSamples(); ++i)
                    worst = std::max (worst, std::abs (in.getSample (0, i) - out.getSample (0, i)));

                check (worst < 1.0e-4f, "EQ at its defaults leaves the sound alone (largest difference " + juce::String (worst, 7) + ")");

                set (*eq, "hpOn", 1.0f);    set (*eq, "hpFreq", 80.0f);     set (*eq, "hpSteep", 1.0f);
                set (*eq, "lsFreq", 120.0f); set (*eq, "lsGain", 4.0f);
                set (*eq, "b2Freq", 1000.0f); set (*eq, "b2Gain", 9.0f);     set (*eq, "b2Q", 2.0f);
                set (*eq, "b3Freq", 5000.0f); set (*eq, "b3Gain", -6.0f);    set (*eq, "b3Q", 0.7f);
                set (*eq, "hsFreq", 10000.0f); set (*eq, "hsGain", 3.0f);
                set (*eq, "lpOn", 1.0f);    set (*eq, "lpFreq", 16000.0f);

                float largestMiss = 0.0f;
                juce::String table;

                for (double f : { 40.0, 100.0, 300.0, 1000.0, 2500.0, 5000.0, 12000.0, 18000.0 })
                {
                    const double measured = sineGainDb (*eq, f), drawn = eq->responseDb (f);
                    largestMiss = std::max (largestMiss, (float) std::abs (measured - drawn));
                    table << juce::String (f, 0) << " Hz " << juce::String (measured, 2) << "/" << juce::String (drawn, 2) << "  ";
                }

                std::cout << "      measured/drawn: " << table << std::endl;
                check (largestMiss < 0.3f, "EQ sounds the way its curve is drawn, within 0.3 dB (worst " + juce::String (largestMiss, 2) + " dB)");
                check (std::abs (sineGainDb (*eq, 1000.0) - 9.0) < 0.6, "a +9 dB bell at 1 kHz lifts 1 kHz by 9 dB");
            }

            //==============================================================================
            // Compressor
            if (auto comp = dynamic_cast<NightCompressor*> (make (NightCompressor::xmlTypeName)))
            {
                auto outDb = [&] (float inDb)
                {
                    comp->reset();
                    auto b = sine (1000.0, juce::Decibels::decibelsToGain (inDb), (int) sr);
                    run (*comp, b);
                    return juce::Decibels::gainToDecibels (b.getMagnitude (0, (int) (sr * 0.75), (int) (sr * 0.25)));
                };

                const float quiet = outDb (-30.0f), loud = outDb (-6.0f);
                check (std::abs (quiet + 30.0f) < 0.3f, "a sine below the threshold comes out as it went in (" + juce::String (quiet, 2) + " dB)");
                check (std::abs (loud - NightCompressor::curveDb (-6.0f, -18.0f, 4.0f, 6.0f)) < 0.6f,
                       "a -6 dB sine at 4:1 from -18 comes out at " + juce::String (loud, 2) + " dB (curve: -15)");

                set (*comp, "ratio", 2.0f);
                check (std::abs (outDb (-6.0f) + 12.0f) < 0.6f, "at 2:1 it comes out at -12 dB (" + juce::String (outDb (-6.0f), 2) + ")");
                set (*comp, "ratio", 4.0f);

                set (*comp, "makeup", 6.0f);
                check (std::abs (outDb (-6.0f) + 9.0f) < 0.6f, "6 dB of makeup lifts that to -9 dB (" + juce::String (outDb (-6.0f), 2) + ")");
                set (*comp, "makeup", 0.0f);

                set (*comp, "autoMakeup", 1.0f);
                check (std::abs (outDb (-6.0f) - (-15.0f + 6.75f)) < 0.6f, "auto makeup gives back half the reduction at full scale (" + juce::String (outDb (-6.0f), 2) + ")");
                set (*comp, "autoMakeup", 0.0f);

                set (*comp, "mix", 0.0f);
                check (std::abs (outDb (-6.0f) + 6.0f) < 0.1f, "at 0% mix it's the dry sound");
                set (*comp, "mix", 1.0f);

                // Timing: quiet, then loud at 0.5 s, then quiet again at 1.5 s
                comp->reset();
                auto b = sine (1000.0, juce::Decibels::decibelsToGain (-40.0f), (int) (sr * 2.5));
                b.applyGain ((int) (sr * 0.5), (int) sr, juce::Decibels::decibelsToGain (34.0f));
                const auto dry = b;
                run (*comp, b);

                auto reductionAt = [&] (double seconds)
                {
                    const int at = (int) (seconds * sr);
                    return rmsDb (dry, 0, at - 96, at + 96) - rmsDb (b, 0, at - 96, at + 96);
                };

                const float attacked = reductionAt (0.5 + 0.06), settledReduction = reductionAt (1.4), released = reductionAt (1.5 + 0.5);
                check (attacked > 0.8f * settledReduction && settledReduction > 8.0f,
                       "with a 10 ms attack it's taken " + juce::String (attacked, 1) + " of " + juce::String (settledReduction, 1) + " dB off 60 ms in");
                check (released < 1.0f, "and 0.5 s after it goes quiet (120 ms release) it's let go (" + juce::String (released, 2) + " dB)");
            }

            //==============================================================================
            // Reverb
            if (auto verb = dynamic_cast<NightReverb*> (make (NightReverb::xmlTypeName)))
            {
                set (*verb, "mix", 1.0f);
                set (*verb, "damping", 18000.0f);
                set (*verb, "lowCut", 20.0f);
                set (*verb, "preDelay", 0.0f);

                // Decay time from the impulse response: Schroeder integration, T30 doubled
                auto measureRt60 = [&] (float decaySeconds)
                {
                    set (*verb, "decay", decaySeconds);
                    verb->reset();
                    auto b = impulse ((int) (sr * (decaySeconds * 2.5 + 0.5)));
                    run (*verb, b);

                    std::vector<double> energy ((size_t) b.getNumSamples());
                    double total = 0.0;

                    for (int i = b.getNumSamples(); --i >= 0;)
                    {
                        total += b.getSample (0, i) * b.getSample (0, i) + b.getSample (1, i) * b.getSample (1, i);
                        energy[(size_t) i] = total;
                    }

                    int t5 = -1, t35 = -1;

                    for (int i = 0; i < (int) energy.size(); ++i)
                    {
                        const double dbDown = 10.0 * std::log10 (energy[(size_t) i] / total);

                        if (t5 < 0 && dbDown <= -5.0)
                            t5 = i;

                        if (t35 < 0 && dbDown <= -35.0)
                            t35 = i;
                    }

                    return t5 >= 0 && t35 > t5 ? 2.0 * (t35 - t5) / sr : -1.0;
                };

                for (float d : { 1.0f, 3.0f })
                {
                    const double rt = measureRt60 (d);
                    check (rt > d * 0.75 && rt < d * 1.25, "Reverb set to " + juce::String (d, 1) + " s decays in " + juce::String (rt, 2) + " s (RT60)");
                }

                // Stereo: the two sides of the tail are different
                verb->reset();
                set (*verb, "decay", 2.0f);
                auto tail = impulse ((int) sr);
                run (*verb, tail);
                double lr = 0.0, ll = 0.0, rr = 0.0;

                for (int i = (int) (0.05 * sr); i < (int) sr; ++i)
                {
                    const double l = tail.getSample (0, i), r = tail.getSample (1, i);
                    lr += l * r; ll += l * l; rr += r * r;
                }

                const double correlation = lr / std::sqrt (std::max (1.0e-30, ll * rr));
                check (std::abs (correlation) < 0.5, "its left and right are different (correlation " + juce::String (correlation, 2) + ")");

                // Level: half wet on noise lands near the dry level
                set (*verb, "mix", 0.5f);
                set (*verb, "damping", 6500.0f);
                set (*verb, "lowCut", 150.0f);
                verb->reset();
                auto in = noise ((int) (sr * 2), 0.3f), out = in;
                run (*verb, out);
                const float change = rmsDb (out, 0, (int) sr, (int) (sr * 2)) - rmsDb (in, 0, (int) sr, (int) (sr * 2));
                check (std::abs (change) < 6.0f, "at 50% mix it's within 6 dB of the dry level (" + juce::String (change, 1) + " dB)");

                // The biggest, longest, brightest setting stays put
                set (*verb, "mix", 1.0f);
                set (*verb, "size", 1.0f);
                set (*verb, "decay", 12.0f);
                set (*verb, "damping", 18000.0f);
                verb->reset();
                auto loud = noise ((int) (sr * 10), 0.5f);
                run (*verb, loud);
                check (allFinite (loud, 8.0f), "at full size and 12 s decay, 10 s of loud noise stays finite and bounded (peak "
                                                 + juce::String (loud.getMagnitude (0, loud.getNumSamples()), 2) + ")");
            }

            //==============================================================================
            // Delay
            if (auto delay = dynamic_cast<NightDelay*> (make (NightDelay::xmlTypeName)))
            {
                set (*delay, "mix", 1.0f);
                set (*delay, "mod", 0.0f);
                set (*delay, "lowCut", 20.0f);
                set (*delay, "highCut", 20000.0f);
                set (*delay, "feedback", 0.5f);
                set (*delay, "sync", 1.0f);
                set (*delay, "division", 5.0f);     // 1/8

                auto echoes = [&]
                {
                    delay->reset();
                    auto b = impulse ((int) sr);
                    run (*delay, b);
                    return b;
                };

                auto b = echoes();
                const int eighth = (int) (0.25 * sr);
                const double first = energyAround (b, 0, eighth, 24), second = energyAround (b, 0, 2 * eighth, 24);
                const double early = energyAround (b, 0, eighth - 200, 150);
                check (10.0 * std::log10 (first) > -1.5 && early < 1.0e-6,
                       "an 1/8 at 120 BPM echoes 250 ms later (" + juce::String (10.0 * std::log10 (first), 2) + " dB), and not before");
                check (std::abs (10.0 * std::log10 (second / first) + 6.02) < 1.0,
                       "at 50% feedback the next echo is 6 dB down (" + juce::String (10.0 * std::log10 (second / first), 2) + " dB)");

                set (*delay, "pingPong", 1.0f);
                b = echoes();
                const double firstL = energyAround (b, 0, eighth, 24), firstR = energyAround (b, 1, eighth, 24);
                const double secondL = energyAround (b, 0, 2 * eighth, 24), secondR = energyAround (b, 1, 2 * eighth, 24);
                check (firstL > 100.0 * firstR && secondR > 100.0 * secondL, "ping-pong: the first echo on the left, the next on the right");
                set (*delay, "pingPong", 0.0f);

                set (*delay, "sync", 0.0f);
                set (*delay, "time", 100.0f);
                b = echoes();
                check (energyAround (b, 0, (int) (0.1 * sr), 24) > 0.7, "unsynced at 100 ms it echoes 100 ms later");

                set (*delay, "sync", 1.0f);
                song.bpm = 90.0;
                song.notify (Song::Change::transport);
                audio.syncNow();
                b = echoes();
                check (energyAround (b, 0, (int) (sr / 3.0), 24) > 0.7, "at 90 BPM the 1/8 moves to 333 ms");
                song.bpm = 120.0;
                song.notify (Song::Change::transport);
                audio.syncNow();
            }

            //==============================================================================
            // Chorus
            if (auto chorus = dynamic_cast<NightChorus*> (make (NightChorus::xmlTypeName)))
            {
                set (*chorus, "mix", 0.0f);
                auto in = noise ((int) sr / 2, 0.3f), out = in;
                run (*chorus, out);
                float worst = 0.0f;

                for (int i = 0; i < in.getNumSamples(); ++i)
                    worst = std::max (worst, std::abs (in.getSample (1, i) - out.getSample (1, i)));

                check (worst < 1.0e-6f, "Chorus at 0% mix is the dry sound");

                set (*chorus, "mix", 1.0f);
                set (*chorus, "depth", 0.0f);
                chorus->reset();
                auto b = impulse ((int) sr / 10);
                run (*chorus, b);
                const int at = (int) (0.009 * sr);
                check (energyAround (b, 0, at, 2) > 0.95 && energyAround (b, 1, at, 2) > 0.95, "with no depth it's the sound 9 ms later, both sides");

                set (*chorus, "depth", 0.7f);
                set (*chorus, "feedback", 0.5f);
                chorus->reset();
                auto moving = noise ((int) sr * 2, 0.3f);
                moving.copyFrom (1, 0, moving, 0, 0, moving.getNumSamples());    // the same on both sides going in
                run (*chorus, moving);
                double lr = 0.0, ll = 0.0, rr = 0.0;

                for (int i = 0; i < moving.getNumSamples(); ++i)
                {
                    const double l = moving.getSample (0, i), r = moving.getSample (1, i);
                    lr += l * r; ll += l * l; rr += r * r;
                }

                check (allFinite (moving, 4.0f) && lr / std::sqrt (ll * rr) < 0.9, "with depth, its sides move apart (correlation "
                                                                                 + juce::String (lr / std::sqrt (ll * rr), 2) + ")");
            }

            //==============================================================================
            // Filter
            if (auto filter = dynamic_cast<NightFilter*> (make (NightFilter::xmlTypeName)))
            {
                auto compare = [&] (const juce::String& what, std::initializer_list<double> freqs, float tolerance)
                {
                    float largestMiss = 0.0f;
                    juce::String table;

                    for (double f : freqs)
                    {
                        const double measured = sineGainDb (*filter, f), drawn = filter->responseDb (f);
                        largestMiss = std::max (largestMiss, (float) std::abs (measured - drawn));
                        table << juce::String (f, 0) << " Hz " << juce::String (measured, 2) << "/" << juce::String (drawn, 2) << "  ";
                    }

                    std::cout << "      " << table << std::endl;
                    check (largestMiss < tolerance, what + " sounds as drawn, within " + juce::String (tolerance, 1) + " dB (worst "
                                                      + juce::String (largestMiss, 2) + ")");
                };

                set (*filter, "cutoff", 1000.0f);
                compare ("Filter low pass at 1 kHz", { 200.0, 1000.0, 4000.0 }, 0.5f);
                set (*filter, "steep", 1.0f);
                compare ("24 dB/oct", { 200.0, 1000.0, 4000.0 }, 0.5f);
                check (sineGainDb (*filter, 4000.0) < -20.0, "and two octaves up is more than 20 dB down");
                set (*filter, "steep", 0.0f);
                set (*filter, "mode", 2.0f);
                compare ("High pass", { 250.0, 1000.0, 5000.0 }, 0.5f);
                set (*filter, "mode", 1.0f);
                set (*filter, "resonance", 0.6f);
                compare ("Band pass", { 250.0, 1000.0, 4000.0 }, 0.5f);
                check (std::abs (sineGainDb (*filter, 1000.0)) < 0.5, "and its peak sits at 0 dB");

                set (*filter, "mode", 0.0f);
                set (*filter, "resonance", 1.0f);
                set (*filter, "drive", 24.0f);
                filter->reset();
                auto hot = noise ((int) sr * 2, 0.9f);
                run (*filter, hot);
                check (allFinite (hot, 4.0f), "full resonance and drive on loud noise stays finite and bounded");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--kitfetchtest"))
        {
            // The free kits fetched by the studio itself, into an empty Kits folder of its own, from
            // local copies of the zips (--kitfetchtest <folder of the five zips>): a request with no
            // kits waits, the drummer's tab offers them, the button fetches and unpacks all five and
            // the request is written. Then unpacking only ever writes a kit's own folder, and a broken zip
            // says so.
            const juce::File zips (args.valueAfter ("--kitfetchtest"));
            const auto kits = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Nightshift kit fetch test");
            kits.deleteRecursively();
            kits.createDirectory();
            drumkits::folderOverride = kits;

            // The five AVL SFZ zips stand in for the studio kits' downloads (the same fetching and
            // unpacking, without 11 GB)
            auto& list = drumkits::downloads();
            const auto studioKits = list;
            list.clear();

            for (auto id : { "RED_ZEPPELIN_2023_repack", "BLACK_PEARL_2023_repack", "BLONDE_BOP_SFZ", "BLONDE_BOP_HR_SFZ", "BUSKMANS_HOLIDAY_SFZ" })
                list.push_back ({ id, juce::URL (zips.getChildFile (juce::String (id) + ".zip")).toString (false), 18, { id }, {} });

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto waitFor = [] (std::function<bool()> done, int seconds)
            {
                for (int i = 0; i < seconds * 50 && ! done(); ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

                return done();
            };

            {
                Song song;
                song.loadDemo();
                AudioEngine audio (song, false);
                audio.syncNow();
                MainComponent main (song, audio);
                main.setSize (1600, 940);
                main.setVisible (true);

                auto& panel = main.getDrummer();
                auto& bar = main.getDrummerBar();
                main.setPanelVisible (MainComponent::Panel::drummer, true);
                auto& button = panel.getKitsButton();
                auto reachable = [&main] (juce::Component& c)
                {
                    auto* hit = main.getComponentAt (main.getLocalPoint (&c, c.getLocalBounds().getCentre()));
                    return c.isVisible() && hit != nullptr && (hit == &c || c.isParentOf (hit));
                };

                // From the start: the machines, and the studio kits on offer across the drummer's tab
                check (drumkits::ids().contains ("NS_808") && drumkits::ids().contains ("NS_909"), "the 808 and 909 are there from the start");
                check (reachable (button), "the drummer's tab offers the studio kits from the start");

                // With no kit at all (the machines gone), a request waits for them
                for (auto id : drumkits::machines::ids())
                    drumkits::folder().getChildFile (id).deleteRecursively();

                song.notify (Song::Change::channels);
                check (drumkits::ids().isEmpty(), "with the machines gone there are no kits");

                bar.setText ("Rock beat");
                bar.getEditor().keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                waitFor ([&] { return panel.getPending().isNotEmpty(); }, 2);
                const bool nothingWritten = std::none_of (song.channels.begin(), song.channels.end(), [] (const Channel& c) { return ! c.drummer.turns.empty(); });
                check (panel.getPending() == "Rock beat" && nothingWritten && main.isPanelVisible (MainComponent::Panel::drummer),
                       "with no kits the request waits in the drummer's tab");

                check (reachable (button), "and the tab offers Get the free drum kits");

                const auto started = juce::Time::getMillisecondCounter();
                panel.getKits();
                check (panel.isFetchingKits(), "clicking it starts the download");
                waitFor ([&] { return ! panel.isFetchingKits(); }, 180);

                check (drumkits::ids().size() == 5, "all five kits are fetched and unpacked (" + juce::String (drumkits::ids().size()) + " " + panel.getKitNote() + ", in "
                                                       + juce::String ((juce::Time::getMillisecondCounter() - started) / 1000.0, 1) + " s)");
                check (drumkits::downloadsFolder().getNumberOfChildFiles (juce::File::findFiles) == 0 && ! kits.getChildFile (".unpacking").exists(), "and nothing is left over from fetching");
                check (drumkits::haveDownloads() && ! button.isVisible(), "once they're all here the offer goes");

                if (auto kit = drumkits::load ("BUSKMANS_HOLIDAY_SFZ"))
                    check (kit->pieces.size() > 10 && ! kit->samples.empty(), "a fetched kit loads (" + juce::String ((int) kit->pieces.size()) + " pieces)");
                else
                    check (false, "a fetched kit loads");

                waitFor ([&] { for (auto& c : song.channels) if (c.type == InstrumentType::kit && ! c.drummer.turns.empty()) return true; return false; }, 5);
                bool written = false;

                for (auto& c : song.channels)
                    written = written || (c.type == InstrumentType::kit && c.drummer.turns.size() == 1 && c.kit == "RED_ZEPPELIN_2023_repack");

                check (written, "then the waiting request is written, on the kit the drummer picks for rock of those (Red Zeppelin)");
            }

            // Unpacking writes only the kit's own folder of samples and SFZ
            {
                const auto scratch = kits.getChildFile ("zips");
                scratch.createDirectory();
                const auto sfz = scratch.getChildFile ("a.sfz");
                sfz.replaceWithText ("<region> sample=a.wav");
                const auto zipFile = scratch.getChildFile ("tricky.zip");

                juce::ZipFile::Builder builder;
                builder.addFile (sfz, 0, "TRICKY/ok.sfz");
                builder.addFile (sfz, 0, "TRICKY/../escaped.sfz");
                builder.addFile (sfz, 0, "TRICKY/deeper/nested.sfz");
                builder.addFile (sfz, 0, "OTHER/other.sfz");
                builder.addFile (sfz, 0, "TRICKY/run.exe");

                {
                    juce::FileOutputStream out (zipFile);
                    builder.writeToStream (out, nullptr);
                }

                check (drumkits::unpack (zipFile, "TRICKY").isEmpty(), "a zip with tricks in it still unpacks its kit");
                const auto kit = kits.getChildFile ("TRICKY");
                check (kit.getChildFile ("ok.sfz").existsAsFile() && kit.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 1
                         && ! kits.getChildFile ("escaped.sfz").exists() && ! kits.getChildFile ("OTHER").exists() && ! kits.getParentDirectory().getChildFile ("escaped.sfz").exists(),
                       "but only its own SFZ: nothing outside its folder, no subfolders, no programs");

                const auto broken = scratch.getChildFile ("broken.zip");
                broken.replaceWithText ("not a zip");
                const auto error = drumkits::unpack (broken, "BROKEN");
                check (error.isNotEmpty() && ! kits.getChildFile ("BROKEN").exists(), "a broken zip says so (" + error + ")");
            }

            drumkits::folderOverride = juce::File();
            list = studioKits;
            kits.deleteRecursively();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--plugininstrumenttest"))
        {
            // A plugin instrument added to a song that has never played (Nightshift Drums, as
            // installed) is heard at once, the song stopped: a hit from its own window or a note
            // needs Tracktion's playback running, and pressing Play mustn't be what starts it
            Song song;
            song.loadEmpty();
            AudioEngine audio (song, false);

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            const auto bundle = paths::userVst3().getChildFile ("Nightshift Drums.vst3");
            std::unique_ptr<juce::PluginDescription> drums;
            auto& formats = audio.getPluginFormats();

            for (int i = 0; i < formats.getNumFormats() && drums == nullptr; ++i)
                if (formats.getFormat (i)->getName() == "VST3" && bundle.exists())
                {
                    juce::OwnedArray<juce::PluginDescription> found;
                    formats.getFormat (i)->findAllTypesForFile (found, bundle.getFullPathName());

                    if (! found.isEmpty())
                    {
                        drums = std::make_unique<juce::PluginDescription> (*found[0]);
                        audio.getKnownPlugins().addType (*drums);
                    }
                }

            if (drums == nullptr)
            {
                std::cout << "SKIP  Nightshift Drums isn't installed (" << bundle.getFullPathName() << ")" << std::endl;
                return 0;
            }

            const double sr = 48000.0;
            const int block = 512;
            juce::AudioBuffer<float> buffer (2, block);

            auto run = [&] (double seconds)
            {
                float loudest = 0.0f;

                for (int b = 0; b < (int) (seconds * sr) / block; ++b)
                {
                    buffer.clear();
                    audio.processHostedBlock (buffer);

                    if (b % 4 == 0)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

                    loudest = std::max (loudest, buffer.getMagnitude (0, block));
                }

                return loudest;
            };

            run (0.3);
            auto& transport = audio.getEdit().getTransport();
            const bool runningBefore = transport.getCurrentPlaybackContext() != nullptr;

            // Added as the sidebar adds it, the song never played; its window asks for the plugin
            auto& channel = song.addChannel (drums->name, InstrumentType::plugin);
            channel.pluginId = drums->createIdentifierString();
            song.selectedChannelId = channel.id;
            const int id = channel.id;
            song.notify (Song::Change::channels);
            settleMessages (20);
            audio.syncNow();

            auto* plugin = dynamic_cast<te::ExternalPlugin*> (audio.getInstrument (id));

            if (plugin != nullptr)
                (void) plugin->getAudioPluginInstance();

            // Hits as a click in its window makes them, straight into its track (nothing else
            // started), once a second while its kit loads: the first one heard ends it
            float heard = 0.0f;
            double after = 0.0;

            if (plugin != nullptr)
                if (auto* track = dynamic_cast<te::AudioTrack*> (plugin->getOwnerTrack()))
                    for (; after < 20.0 && heard < 0.001f; after += 1.0)
                    {
                        track->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 110), 0);
                        heard = run (0.5);
                        track->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, 38), 0);
                        run (0.5);
                    }

            check (plugin != nullptr, "Nightshift Drums loads on a new channel");
            check (transport.getCurrentPlaybackContext() != nullptr,
                   juce::String ("Tracktion's playback runs without Play having been pressed (before the channel came it was ")
                     + (runningBefore ? "running" : "not running") + ")");
            check (! transport.isPlaying() && heard > 0.001f,
                   "a hit on the new channel is heard, the song stopped (peak " + juce::String (juce::Decibels::gainToDecibels (heard), 1)
                     + " dB, " + juce::String (after, 0) + " s after it was added, its kit loading)");

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--keyboardtest"))
        {
            // The PC keyboard as a music keyboard (its top two letter rows like a piano's keys), as
            // you'd use it: on from the start, A playing middle C on the selected channel and the
            // rows up from it, chords, Z/X the octave and C/V the velocity (a held note keeping its
            // own), L a note, the gaps doing nothing, every shortcut taking Shift (Shift+M switches
            // the keyboard), Space playing, a held key sounding through the synth; then FL's way of
            // recording: the Record button arms, Space records what's played into the pattern as it
            // loops after a bar's count-in (notes played in the count-in not kept), snapped to the
            // grid, a second pass replacing rather than doubling, one undo for the take, Record
            // staying armed; Blend off replacing the channel's notes; starting on the first note;
            // Song mode putting the take in a new pattern clipped where playback started; and
            // punching in and out while playing. The keys themselves are stood in for here.
            Song song;
            song.loadDemo();
            AudioEngine audio (song, false);
            audio.syncNow();
            MainComponent main (song, audio);
            main.setSize (1600, 940);
            main.setVisible (true);

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto& kb = main.getComputerKeyboard();
            std::set<int> down;
            kb.isKeyDown = [&down] (int c) { return down.count ((int) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) c)) > 0; };
            kb.isAppInFront = [] { return true; };
            auto settle = [] { settleMessages (60); };

            std::vector<std::pair<int, int>> played;
            std::vector<int> stopped;
            auto noteOn = kb.onNoteOn;
            auto noteOff = kb.onNoteOff;
            kb.onNoteOn = [&] (int key, int velocity) { played.push_back ({ key, velocity }); noteOn (key, velocity); };
            kb.onNoteOff = [&] (int key) { stopped.push_back (key); noteOff (key); };

            auto press = [&] (char c)
            {
                down.insert (c);
                main.keyPressed (juce::KeyPress (c));
                main.keyStateChanged (true);
            };

            auto letGo = [&] (char c)
            {
                down.erase (c);
                main.keyStateChanged (false);
            };

            auto tap = [&] (char c) { press (c); letGo (c); };

            Channel* keys = nullptr;

            for (auto& c : song.channels)
                if (c.name == "Keys")
                    keys = &c;

            song.selectedChannelId = keys->id;
            song.notify (Song::Change::selection);

            check (kb.isEnabled() && kb.getLowestKey() == 60 && kb.getVelocity() == 100, "on from the start: A is middle C (C5), velocity 100");

            tap ('a');
            check (played.size() == 1 && played[0] == std::make_pair (60, 100) && stopped == std::vector<int> { 60 }, "A plays middle C and lets it go");

            played.clear();
            stopped.clear();

            for (char c : juce::String ("wkh;']"))
                tap (c);

            check (played.size() == 6 && played[0].first == 61 && played[1].first == 72 && played[2].first == 69 && played[3].first == 76 && played[4].first == 77
                     && played[5].first == 78,
                   "W is C#, K the C above, H is A, and ; ' and ] run on up to F#6");

            played.clear();
            stopped.clear();
            press ('a');
            press ('d');
            press ('g');
            check (played.size() == 3 && played[1].first == 64 && played[2].first == 67 && stopped.empty(), "a chord: A, D and G held together");
            letGo ('a');
            letGo ('d');
            letGo ('g');
            check (stopped.size() == 3, "and let go");

            played.clear();
            stopped.clear();
            tap ('x');
            tap ('a');
            tap ('z');
            tap ('z');
            tap ('c');
            tap ('a');
            check (played.size() == 2 && played[0] == std::make_pair (72, 100) && played[1] == std::make_pair (48, 80),
                   "X takes it up an octave, Z down, C softer (" + kb.describe() + ")");

            stopped.clear();
            press ('a');
            tap ('x');
            letGo ('a');
            check (stopped == std::vector<int> { 48 }, "a note held through an octave change stops itself, not the new octave's");

            tap ('v');
            tap ('v');
            check (kb.getVelocity() == 127 && kb.getLowestKey() == 60, "V twice from 80 is full velocity; back at middle C");

            played.clear();
            const auto mode = song.mode;
            tap ('l');
            check (song.mode == mode && played.size() == 1 && played[0].first == 74, "L plays D while the keyboard is on, instead of switching pattern / song");

            // Every shortcut takes a modifier; the keys where a piano has no black key do nothing
            const auto shift = juce::ModifierKeys::shiftModifier;
            const bool inspectorBefore = main.isInspectorVisible();
            played.clear();

            for (char c : juce::String ("qri[m"))
                tap (c);

            check (played.empty() && ! audio.isRecordingNotes() && ! audio.isRecording() && main.isInspectorVisible() == inspectorBefore && kb.isEnabled(),
                   "Q, R, I and [ (no black key there) and M play nothing and do nothing: no recording, no Inspector");

            main.keyPressed (juce::KeyPress ('l', shift, 0));
            const bool switched = song.mode != mode;
            main.keyPressed (juce::KeyPress ('l', shift, 0));
            main.keyPressed (juce::KeyPress ('i', shift, 0));
            const bool inspectorToggled = main.isInspectorVisible() != inspectorBefore;
            main.keyPressed (juce::KeyPress ('i', shift, 0));
            check (switched && song.mode == mode && inspectorToggled, "Shift+L switches pattern / song, Shift+I the Inspector");

            const int patternBefore = song.currentPatternId;
            main.keyPressed (juce::KeyPress ('=', shift, '+'));
            const int nextPattern = song.currentPatternId;
            main.keyPressed (juce::KeyPress ('-', shift, '_'));
            main.keyPressed (juce::KeyPress ('-'));
            check (nextPattern != patternBefore && song.currentPatternId == patternBefore, "Shift with + and - step through the patterns (- alone doesn't)");

            main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            const bool played1 = audio.isPlaying() && ! audio.isRecordingNotes();
            main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey, shift, 0));
            const bool played2 = audio.isPlaying() && ! audio.isRecordingNotes();
            main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey, shift, 0));
            check (played1 && played2 && ! audio.isPlaying(), "Space (and Shift+Space) plays and stops, recording nothing while Record isn't armed");

            main.keyPressed (juce::KeyPress ('m', shift, 0));
            played.clear();
            tap ('a');
            check (! kb.isEnabled() && played.empty() && ! audio.getOptions().computerKeyboard,
                   "Shift+M switches it off (remembered): A plays nothing");

            main.keyPressed (juce::KeyPress ('m', shift, 0));
            check (kb.isEnabled(), "Shift+M switches it back on");

            // With the key presses going to another of Nightshift's windows (a plugin's), the keys
            // are still read: held, A plays; let go, it stops
            played.clear();
            stopped.clear();
            down.insert ('a');
            settle();
            const bool heardElsewhere = played.size() == 1 && played[0].first == kb.getLowestKey();
            down.erase ('a');
            settle();
            check (heardElsewhere && stopped.size() == 1, "with a plugin's window taking the key presses, a held A still plays, and stops when let go");

            // A system dialog (Save as, Open, Export: the operating system's own, so none of
            // Nightshift's fields has the focus) takes the typing, a file's name say: nothing plays
            // while it's open, and once it has answered the keys play again
            played.clear();
            stopped.clear();
            bool silentWhileOpen = false;
            {
                const auto dialog = ComputerKeyboard::openSystemDialog();

                for (char c : juce::String ("dashed"))
                {
                    down.insert (c);
                    settle();
                    down.erase (c);
                    settle();
                }

                silentWhileOpen = played.empty() && ComputerKeyboard::isSystemDialogOpen();
            }
            down.insert ('a');
            settle();
            const bool playsAfter = played.size() == 1 && played[0].first == kb.getLowestKey() && ! ComputerKeyboard::isSystemDialogOpen();
            down.erase ('a');
            settle();
            check (silentWhileOpen && playsAfter, "typing a name into a system dialog (Save as) plays nothing; once it closes, A plays again");

            // F-keys stay shortcuts (F8's and F11's codes end in the same byte as W's and Z's)
            played.clear();
            const int lowestBefore = kb.getLowestKey();
            main.keyPressed (juce::KeyPress (juce::KeyPress::F11Key));
            const bool synthShown = main.isPanelVisible (MainComponent::Panel::synth);
            main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key));
            check (played.empty() && kb.getLowestKey() == lowestBefore && synthShown && main.isPanelVisible (MainComponent::Panel::drummer),
                   "F11 and F8 open their tabs, and play nothing and shift nothing");

            // Heard: a held key plays the synth (the transport stopped), and stops when let go
            const double sr = 48000.0;
            const int block = 512;
            juce::AudioBuffer<float> buffer (2, block);

            auto run = [&] (double seconds, std::function<bool()> until = {})
            {
                float loudest = 0.0f;

                for (int b = 0; b < (int) (seconds * sr) / block; ++b)
                {
                    buffer.clear();
                    audio.processHostedBlock (buffer);

                    if (b % 4 == 0)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

                    loudest = std::max (loudest, buffer.getMagnitude (0, block));

                    if (until != nullptr && until())
                        break;
                }

                return loudest;
            };

            if (audio.isPlaying())
                audio.stop();

            run (0.2);
            press ('a');
            const float held = run (0.4);
            letGo ('a');
            run (3.0);
            const float after = run (0.3);
            check (held > 0.01f && after < held * 0.01f, "a held key sounds through Keys' synth (peak " + juce::String (juce::Decibels::gainToDecibels (held), 1)
                                                         + " dB) and dies away once let go (" + juce::String (juce::Decibels::gainToDecibels (after), 1) + " dB)");

            // Recording into the pattern as it loops, onto the 1/16 grid (in pattern mode: Record
            // records in whichever mode it finds)
            song.mode = PlayMode::pattern;
            song.notify (Song::Change::transport);
            audio.syncNow();
            auto& pattern = song.currentPattern();
            const auto before = pattern.notesFor (keys->id);
            const int bar = song.patternLength (pattern);
            main.toggleRecordArm();
            check (audio.isRecordArmed() && ! audio.isPlaying() && ! audio.isRecordingNotes(), "the Record button arms recording; nothing starts yet");

            main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            check (audio.isRecordingNotes() && audio.isPlaying() && audio.isCountingIn() && song.mode == PlayMode::pattern,
                   "then Space records Keys into the pattern, after a bar's count-in (a " + juce::String (bar / ticksPerBeat) + "-beat pattern)");

            // Where the audio thread is, which is what notes are timed by (the transport's own
            // position is a copy that lags behind when blocks come faster than real time)
            auto heardTicks = [&]
            {
                auto context = audio.getEdit().getTransport().getCurrentPlaybackContext();
                return context != nullptr ? context->getPosition().inSeconds() * song.bpm / 60.0 * ticksPerBeat : 0.0;
            };

            auto playheadPast = [&] (double ticks) { return [&heardTicks, ticks] { return heardTicks() >= ticks; }; };
            auto has = [&] (int start, int length, int key, int velocity)
            {
                auto& notes = pattern.notesFor (keys->id);
                return std::count (notes.begin(), notes.end(), Note { start, length, key, velocity });
            };

            // Noodling in the count-in, well before the one, isn't kept
            run (10.0, [&] { return heardTicks() > -3.0 * ticksPerBeat; });
            press ('e');
            run (10.0, [&] { return heardTicks() > -2.5 * ticksPerBeat; });
            letGo ('e');
            check (pattern.notesFor (keys->id) == before && audio.isCountingIn(), "a note played in the count-in, well before the one, isn't kept");

            // A, a little late for beat 2, held about half a beat
            run (10.0, playheadPast (ticksPerBeat + 7));
            press ('h');
            run (10.0, playheadPast (ticksPerBeat + 7 + ticksPerBeat / 2 + 2));
            letGo ('h');

            check (has (ticksPerBeat, ticksPerBeat / 2, 69, 127) == 1 && pattern.notesFor (keys->id).size() == before.size() + 1,
                   "an A played a little late for beat 2 and held half a beat lands on beat 2, an eighth long");

            // A chord: written once every key is up
            run (10.0, playheadPast (2 * ticksPerBeat - 3));
            press ('a');
            press ('g');
            run (10.0, playheadPast (2 * ticksPerBeat + ticksPerStep + 2));
            letGo ('a');
            const bool waited = has (2 * ticksPerBeat, ticksPerStep, 60, 127) == 0;
            letGo ('g');
            check (waited && has (2 * ticksPerBeat, ticksPerStep, 60, 127) == 1 && has (2 * ticksPerBeat, ticksPerStep, 67, 127) == 1,
                   "a chord goes in once every key is up, both notes on beat 3");

            // Round the loop: the same note again replaces the first rather than doubling it
            run (20.0, [&] { return heardTicks() < ticksPerBeat; });
            run (10.0, playheadPast (ticksPerBeat + 2));
            press ('h');
            run (10.0, playheadPast (ticksPerBeat + ticksPerBeat / 2));
            letGo ('h');
            check (has (ticksPerBeat, ticksPerBeat / 2, 69, 127) == 1 && pattern.notesFor (keys->id).size() == before.size() + 3,
                   "next time round, the same note replaces itself instead of stacking");

            main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            check (! audio.isRecordingNotes() && ! audio.isPlaying() && audio.isRecordArmed(), "Space again stops it; Record stays armed, as FL's does");

            song.undo();
            check (song.currentPattern().notesFor (keys->id) == before, "one undo takes the whole take back out");

            auto setOptions = [&] (std::function<void (AudioEngine::Options&)> change)
            {
                auto o = audio.getOptions();
                change (o);
                audio.setOptions (o);
            };

            auto notesNow = [&] { return song.currentPattern().notesFor (keys->id); };
            auto countOf = [&] (const Note& n) { auto v = notesNow(); return (int) std::count (v.begin(), v.end(), n); };

            // Blend off: the take takes the place of what the channel had in the pattern
            setOptions ([] (auto& o) { o.blendNotes = false; o.countInBars = 0; });
            song.currentPattern().notesFor (keys->id) = { { 0, ticksPerStep, 60, 100 }, { 3 * ticksPerBeat, ticksPerStep, 72, 100 } };
            song.notify (Song::Change::notes);
            const auto beforeReplace = notesNow();
            main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            run (10.0, playheadPast (2 * ticksPerBeat + 2));
            press ('f');
            run (10.0, playheadPast (2 * ticksPerBeat + ticksPerStep + 2));
            letGo ('f');
            main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            check (notesNow() == std::vector<Note> { { 2 * ticksPerBeat, ticksPerStep, 65, 127 } } && ! beforeReplace.empty(),
                   "Blend off: the take replaces Keys' " + juce::String ((int) beforeReplace.size()) + " notes in the pattern with the one played");
            song.undo();
            check (notesNow() == beforeReplace, "and one undo brings them back");

            // Starting on the first note: Space waits; the first note starts the pattern, on its one
            setOptions ([] (auto& o) { o.blendNotes = true; o.startOnInput = true; });
            main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            const bool waiting = audio.isWaitingForInput() && ! audio.isPlaying();
            run (0.5);
            const bool stillWaiting = audio.isWaitingForInput() && ! audio.isPlaying();
            press ('d');
            const bool started = audio.isPlaying() && ! audio.isWaitingForInput();
            run (10.0, playheadPast (ticksPerBeat / 2));
            letGo ('d');
            main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            check (waiting && stillWaiting && started && countOf ({ 0, ticksPerBeat / 2, 64, 127 }) == 1,
                   "start on the first note: Space waits, and the first note starts the pattern and lands on its first beat");
            song.undo();

            // Song mode, FL's way: into a new pattern, clipped where playback started
            setOptions ([] (auto& o) { o.startOnInput = false; });
            song.mode = PlayMode::song;
            song.loopOn = false;
            song.notify (Song::Change::transport);
            audio.syncNow();
            const int barTicks = song.ticksPerBar();
            audio.setPlayheadTicks (8 * barTicks + ticksPerBeat);
            const auto patternsBefore = song.patterns.size();
            const auto clipsBefore = song.playlist.size();
            main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            const bool songTake = audio.isRecordingNotes() && audio.isPlaying() && song.mode == PlayMode::song;
            run (10.0, playheadPast (8 * barTicks + 2 * ticksPerBeat + 2));
            press ('g');
            run (10.0, playheadPast (8 * barTicks + 3 * ticksPerBeat + 2));
            letGo ('g');
            main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            const auto placed = song.playlist.back();
            const auto* made = song.findPattern (placed.patternId);
            const auto* madeNotes = made != nullptr ? made->findNotes (keys->id) : nullptr;
            check (songTake && song.patterns.size() == patternsBefore + 1 && song.playlist.size() == clipsBefore + 1 && placed.start == 8 * barTicks
                     && madeNotes != nullptr && *madeNotes == std::vector<Note> { { 2 * ticksPerBeat, ticksPerBeat, 67, 127 } } && song.currentPatternId == made->id,
                   "in Song mode the take goes into a new pattern, clipped at the bar playback started in (bar 9, past the song's end), the G on its beat 3");
            song.undo();
            check (song.patterns.size() == patternsBefore && song.playlist.size() == clipsBefore, "one undo takes the pattern and its clip away again");

            // Punching in and out while playing
            song.mode = PlayMode::pattern;
            song.notify (Song::Change::transport);
            audio.syncNow();
            main.toggleRecordArm();
            main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            run (10.0, playheadPast (ticksPerBeat / 2));
            const bool playingOnly = audio.isPlaying() && ! audio.isRecordingNotes();
            main.toggleRecordArm();
            const bool punchedIn = audio.isRecordingNotes() && ! audio.isCountingIn();
            run (10.0, playheadPast (3 * ticksPerBeat + 2));
            press ('j');
            run (10.0, playheadPast (3 * ticksPerBeat + ticksPerStep + 2));
            letGo ('j');
            main.toggleRecordArm();
            const bool punchedOut = ! audio.isRecordingNotes() && audio.isPlaying();
            main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            check (playingOnly && punchedIn && punchedOut && countOf ({ 3 * ticksPerBeat, ticksPerStep, 71, 127 }) == 1,
                   "arming Record while playing punches in at once; disarming punches out and it plays on");

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--metronometest"))
        {
            // The metronome, as Neural DSP's: clicks on the beat from the song's tempo and time
            // signature, the downbeat strong; pulses between beats (the rhythm); an accent per
            // beat, any of them off; a time signature's grouping setting its accents (7/8 as
            // 2+2+3); the sound set; volume and pan; only while recording; practising with the
            // song stopped; heard after the master (its meter and exports never see it); and
            // counting in before a take whether it's on or not. Measured from the output: an
            // empty pattern plays, so every sound out is the metronome's.
            Song song;
            song.loadDemo();
            song.bpm = 120.0;
            song.meterBeats = 4;
            song.meterUnit = 4;
            song.currentPatternId = song.addPattern ("Silence").id;
            song.mode = PlayMode::pattern;
            song.loopOn = false;
            song.notify (Song::Change::everything);
            AudioEngine audio (song, false);
            audio.syncNow();

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            const double sr = 48000.0;
            const int block = 512;
            auto db = [] (float gain) { return juce::String (juce::Decibels::gainToDecibels (gain), 1); };

            // The sounds: each its own, the downbeat louder than a beat, a beat than a pulse between
            {
                bool levelled = true, distinct = true;
                juce::String levels;

                for (int s = 0; s < metronome::soundNames().size(); ++s)
                {
                    const auto strong = MetronomePlugin::makeHit (s, metronome::strong, sr);
                    const auto normal = MetronomePlugin::makeHit (s, metronome::normal, sr);
                    const auto soft = MetronomePlugin::makeHit (s, metronome::soft, sr);
                    const auto between = MetronomePlugin::makeHit (s, metronome::off, sr);
                    const float ps = strong.getMagnitude (0, strong.getNumSamples()), pn = normal.getMagnitude (0, normal.getNumSamples());
                    const float pf = soft.getMagnitude (0, soft.getNumSamples()), pb = between.getMagnitude (0, between.getNumSamples());

                    levelled = levelled && std::abs (juce::Decibels::gainToDecibels (ps) + 6.0f) < 0.2f
                                        && std::abs (juce::Decibels::gainToDecibels (pn) + 9.0f) < 0.2f
                                        && pf < pn * 0.5f && pb < pn && strong.getNumSamples() < (int) (sr * 0.7);

                    // The downbeat's hit isn't just the beat's, louder
                    double difference = 0.0;

                    for (int i = 0; i < std::min (strong.getNumSamples(), normal.getNumSamples()); ++i)
                        difference += std::abs (strong.getSample (0, i) / ps - normal.getSample (0, i) / pn);

                    distinct = distinct && difference > 10.0;
                    levels << metronome::soundNames()[s] << " " << db (ps) << "/" << db (pn) << "/" << db (pf) << "/" << db (pb) << "  ";
                }

                check (levelled && distinct, juce::String (metronome::soundNames().size()) + " sounds, each with a downbeat of its own, louder than a beat, a beat than a soft one or a pulse between: " + levels);
            }

            // What comes out: stereo, a block at a time
            auto listen = [&] (double seconds)
            {
                juce::AudioBuffer<float> out (2, (int) (seconds * sr) / block * block), buffer (2, block);

                for (int b = 0; b < out.getNumSamples() / block; ++b)
                {
                    buffer.clear();
                    audio.processHostedBlock (buffer);

                    for (int ch = 0; ch < 2; ++ch)
                        out.copyFrom (ch, b * block, buffer, ch, 0, block);

                    if (b % 4 == 0)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
                }

                return out;
            };

            // Where clicks start (after 5 ms under the threshold) and how loud each is
            struct Onset { int at; float peak; };

            auto onsets = [] (const juce::AudioBuffer<float>& b, int channel)
            {
                std::vector<Onset> found;
                const float* x = b.getReadPointer (channel);
                int quiet = 100000;

                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float v = std::abs (x[i]);

                    if (v > 0.003f && quiet > 240)
                    {
                        float peak = 0.0f;

                        for (int j = i; j < std::min (b.getNumSamples(), i + 480); ++j)
                            peak = std::max (peak, std::abs (x[j]));

                        found.push_back ({ i, peak });
                    }

                    quiet = v <= 0.003f ? quiet + 1 : 0;
                }

                return found;
            };

            auto spacing = [] (const std::vector<Onset>& o, int expected)
            {
                for (size_t i = 1; i < o.size(); ++i)
                    if (std::abs (o[i].at - o[i - 1].at - expected) > 2)
                        return false;

                return true;
            };

            auto describe = [&] (const std::vector<Onset>& o)
            {
                juce::String s;

                for (auto& x : o)
                    s << juce::String (x.at / sr, 3) << "s " << db (x.peak) << "  ";

                return s.trim();
            };

            auto setOptions = [&] (std::function<void (AudioEngine::Options&)> change)
            {
                auto o = audio.getOptions();
                change (o);
                audio.setOptions (o);
            };

            auto restart = [&]
            {
                if (audio.isPlaying())
                    audio.stop();

                listen (0.3);       // the last hits ring out
                audio.togglePlay();
            };

            const float volume = MetronomePlugin::volumeToGain (0.7f);
            const float strongPeak = juce::Decibels::decibelsToGain (-6.0f) * volume, normalPeak = juce::Decibels::decibelsToGain (-9.0f) * volume;

            // Off: the song plays, and nothing clicks
            restart();
            auto out = listen (1.9);
            check (onsets (out, 0).empty(), "off: playing, nothing clicks");

            // On: a click a beat, the one strong; the master's meter hears none of it
            setOptions ([] (auto& o) { o.metronome = true; });
            restart();
            audio.getMasterPeak();
            out = listen (1.9);
            auto found = onsets (out, 0);
            const auto master = audio.getMasterPeak();
            check (found.size() == 4 && spacing (found, 24000) && found[0].at < (int) (sr * 0.01)
                     && std::abs (found[0].peak - strongPeak) < strongPeak * 0.05f && std::abs (found[1].peak - normalPeak) < normalPeak * 0.05f,
                   "on at 120 bpm in 4/4: a click every half second from the top, the one strong: " + describe (found));
            check (std::max (master.first, master.second) < 1.0e-4f, "after the master: its meter heard nothing of it (" + db (std::max (master.first, master.second)) + " dB)");

            // Two a beat: the pulses between quieter than the beats
            setOptions ([] (auto& o) { o.clickRhythm = 1; });
            restart();
            found = onsets (listen (1.9), 0);
            check (found.size() == 8 && spacing (found, 12000) && found[1].peak < found[2].peak * 0.8f,
                   "2 a beat: eight clicks in two seconds, a pulse between each beat, quieter: " + describe (found));

            // Triplets
            setOptions ([] (auto& o) { o.clickRhythm = 2; });
            restart();
            found = onsets (listen (0.95), 0);
            check (found.size() == 6 && spacing (found, 8000), "triplets: three clicks a beat (" + juce::String ((int) found.size()) + " in a second)");

            // An accent per beat: the third off
            setOptions ([] (auto& o) { o.clickRhythm = 0; });
            audio.setClickAccents ({ metronome::strong, metronome::normal, metronome::off, metronome::normal });
            restart();
            found = onsets (listen (1.9), 0);
            check (found.size() == 3 && std::abs (found[2].at - found[0].at - 72000) <= 2, "beat 3 switched off: it's skipped (" + describe (found) + ")");
            audio.setClickAccents (metronome::defaultAccents (audio.getClickSignature()));

            // 7/8 as 2+2+3: strong on 1, normal where each group starts (3 and 5), soft between
            song.meterBeats = 7;
            song.meterUnit = 8;
            song.notify (Song::Change::transport);
            audio.setClickGrouping (metronome::find (7, 8, "2+2+3"));
            restart();
            found = onsets (listen (1.74), 0);
            bool grouped = found.size() == 7 && spacing (found, 12000);

            for (size_t i = 0; grouped && i < found.size(); ++i)
            {
                const float expected = i == 0 ? strongPeak : (i == 2 || i == 4) ? normalPeak : normalPeak * 0.45f;
                grouped = std::abs (found[i].peak - expected) < expected * 0.06f;
            }

            check (grouped && metronome::accentsToText (audio.getClickAccents()) == "3121211",
                   "7/8 as 2+2+3: eighths, strong on 1, normal on 3 and 5, soft between: " + describe (found));

            audio.setClickGrouping (metronome::find (7, 8, "3+2+2"));
            check (metronome::accentsToText (audio.getClickAccents()) == "3112121", "7/8 as 3+2+2 accents 1, 4 and 6");

            song.meterBeats = 4;
            song.meterUnit = 4;
            song.notify (Song::Change::transport);

            // Another sound
            setOptions ([] (auto& o) { o.clickSound = 6; });
            restart();
            auto beep = listen (0.4);
            setOptions ([] (auto& o) { o.clickSound = 0; });
            restart();
            auto classic = listen (0.4);
            double soundDifference = 0.0;

            for (int i = 0; i < 2400; ++i)
                soundDifference += std::abs (beep.getSample (0, i) - classic.getSample (0, i));

            check (soundDifference > 5.0, "choosing Beep over Classic changes the click");

            // Pan and volume
            setOptions ([] (auto& o) { o.clickPan = -1.0f; });
            restart();
            out = listen (0.95);
            check (onsets (out, 0).size() == 2 && out.getMagnitude (1, 0, out.getNumSamples()) < 1.0e-5f, "panned hard left: nothing on the right");

            setOptions ([] (auto& o) { o.clickPan = 0.0f; o.clickVolume = 0.0f; });
            restart();
            check (listen (0.95).getMagnitude (0, 0, (int) (sr * 0.9)) < 1.0e-5f, "volume all the way down: silent");

            setOptions ([] (auto& o) { o.clickVolume = 0.4f; });
            restart();
            found = onsets (listen (0.4), 0);
            const float quieter = juce::Decibels::decibelsToGain (-6.0f) * MetronomePlugin::volumeToGain (0.4f);
            check (found.size() == 1 && std::abs (found[0].peak - quieter) < quieter * 0.05f, "volume 0.4: the one at " + db (found.empty() ? 0.0f : found[0].peak) + " dB");
            setOptions ([] (auto& o) { o.clickVolume = 0.7f; });

            // Only while recording: just playing, nothing
            setOptions ([] (auto& o) { o.metronomeRecordingOnly = true; });
            restart();
            check (onsets (listen (1.0), 0).empty(), "only while recording: playing without recording, nothing clicks");
            setOptions ([] (auto& o) { o.metronomeRecordingOnly = false; });

            // Practising: the song stopped, the metronome on its own
            audio.stop();
            listen (0.3);
            audio.setPractising (true);
            found = onsets (listen (1.9), 0);
            check (audio.isPractising() && ! audio.isPlaying() && found.size() == 4 && spacing (found, 24000) && found[0].peak > found[1].peak,
                   "practising, the song stopped: a click a beat from a downbeat: " + describe (found));

            audio.togglePlay();
            check (! audio.isPractising() && audio.isPlaying(), "Play takes over from practising");

            // Exports never have it
            audio.stop();
            auto file = juce::File::createTempFile (".wav");
            const bool rendered = audio.renderToFile (file, 0.5);
            float exported = 1.0f;

            {
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();

                if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr)
                {
                    juce::AudioBuffer<float> b ((int) reader->numChannels, (int) reader->lengthInSamples);
                    reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                    exported = b.getMagnitude (0, b.getNumSamples());
                }
            }

            file.deleteFile();
            check (rendered && exported < 1.0e-5f, "an export with the metronome on has none of it (peak " + db (exported) + " dB)");

            // Counting in, the metronome off: Record armed, Space counts a bar, then the take
            setOptions ([] (auto& o) { o.metronome = false; o.countInBars = 1; });
            Channel* keys = nullptr;

            for (auto& c : song.channels)
                if (c.name == "Keys")
                    keys = &c;

            song.selectedChannelId = keys->id;
            song.notify (Song::Change::selection);
            listen (0.3);
            audio.setRecordArmed (true);
            const auto notice = audio.togglePlay();
            const bool countingIn = audio.isCountingIn() && audio.isRecordingNotes();
            out = listen (3.0);
            found = onsets (out, 0);
            auto context = audio.getEdit().getTransport().getCurrentPlaybackContext();
            const double now = context != nullptr ? context->getPosition().inSeconds() : -1.0;
            check (countingIn && found.size() == 4 && spacing (found, 24000) && found[0].peak > found[1].peak && ! audio.isCountingIn()
                     && notice.contains ("count-in"),
                   "the metronome off, Record armed: Play counts in a bar (" + describe (found) + "), then only the take");
            check (now > 0.8 && now < 1.0, "after the count-in the pattern plays from its top and goes round (" + juce::String (now, 2) + " s into a 2 s pattern after 3 s, 2.1 s of it lead-in)");
            audio.stop();
            audio.setRecordArmed (false);

            // An audio take's count-in is Tracktion's; its own click is silenced, so what counts is
            // the metronome alone: four clicks, the one strong, nothing doubled
            {
                // (audio takes run the song: an empty one, so only the metronome is heard)
                song.playlist.clear();
                song.notify (Song::Change::playlist);
                const auto inputs = audio.getInputNames();
                auto& guitar = song.addChannel ("Guitar", InstrumentType::audio);
                guitar.input = inputs.isEmpty() ? juce::String() : inputs[0];
                guitar.armed = true;
                song.notify (Song::Change::channels);
                audio.syncNow();
                listen (0.3);

                const auto problem = audio.toggleRecord();
                listen (0.1);       // (the playhead moves back for the count-in with the first block)
                const bool countingInAudio = audio.isRecording() && audio.isCountingIn();
                found = onsets (listen (2.2), 0);
                const bool clean = found.size() == 4 && spacing (found, 24000) && std::abs (found[0].peak - strongPeak) < strongPeak * 0.05f
                                   && std::abs (found[1].peak - normalPeak) < normalPeak * 0.05f;
                audio.stop();
                listen (0.3);
                check (problem.isEmpty() && countingInAudio && clean,
                       "an audio take's count-in: the metronome's four clicks alone, Tracktion's own click silent: " + describe (found)
                         + (problem.isNotEmpty() ? " (" + problem + ")" : juce::String()));
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        // A phrase on Nightshift Guitar, rendered and measured (--guitarrender out.wav "High Gain"
        // [chords|riff|chug]): the heavy sounds chug, the rest strum chords, unless told
        if (args.has ("--guitarrender"))
        {
            const int at = args.tokens.indexOf ("--guitarrender");
            const juce::File out (args.valueAfter ("--guitarrender"));
            const auto preset = args.tokens[at + 2].unquoted();
            const bool heavy = preset == "High Gain" || preset == "Djent";
            const auto which = args.tokens.size() > at + 3 ? args.tokens[at + 3].unquoted() : juce::String (heavy ? "chug" : preset == "Crunch" || preset == "Punk" ? "riff" : "chords");

            if (! guitarsound::presetNames().contains (preset))
            {
                std::cout << "sounds: " << guitarsound::presetNames().joinIntoString (", ") << std::endl;
                return 1;
            }

            const auto hits = guitarsound::phrase (which);
            double end = 0.0;

            for (auto& h : hits)
                end = std::max (end, h.start + h.length);

            const auto r = guitarsound::render (preset, hits, end + 2.0);
            out.deleteFile();
            {
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (out);
                auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                                  .withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24));

                if (writer == nullptr)
                    return 1;

                writer->writeFromAudioSampleBuffer (r.audio, 0, r.audio.getNumSamples());
            }

            const auto m = mastering::measureFile (out);
            std::cout << "rendered " << out.getFullPathName() << ": " << preset << " playing " << which << ", integrated "
                      << juce::String (m.integratedLufs, 1) << " LUFS, loudest 400 ms " << juce::String (m.maxMomentaryLufs, 1)
                      << " LUFS, true peak " << juce::String (m.truePeakDb, 1) << " dBTP" << std::endl;
            return 0;
        }

        if (args.has ("--guitarsoundtest"))
        {
            // Nightshift Guitar: every string in tune (measured), a palm-muted note dying far faster
            // than an open one, dead notes short, legato (a hammer-on and a slide) with no new
            // pluck, a six-string chord, every sound clean of overs at about Nova's level, faster
            // than real time; then in the studio: a Guitar channel, its sound switched without
            // building it again, rendered, saved, named and drawn as a guitar.
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            constexpr double sr = 48000.0;
            auto keyHz = [] (double key) { return 440.0 * std::pow (2.0, (key - 69.0) / 12.0); };

            // The fundamental, from the autocorrelation of the low-passed note near its period
            auto pitchOf = [&] (const juce::AudioBuffer<float>& b, double from, double to, double expectHz)
            {
                const int start = (int) (from * sr), n = (int) ((to - from) * sr);
                std::vector<float> x ((size_t) n);

                for (int i = 0; i < n; ++i)
                    x[(size_t) i] = b.getSample (0, start + i) + b.getSample (1, start + i);

                for (int pass = 0; pass < 2; ++pass)
                {
                    juce::IIRFilter lp;
                    lp.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, expectHz * 1.4));
                    lp.processSamples (x.data(), n);
                }

                const double period = sr / expectHz;
                auto corr = [&] (int lag)
                {
                    double s = 0.0;

                    for (int i = 0; i + lag < n; ++i)
                        s += (double) x[(size_t) i] * x[(size_t) (i + lag)];

                    return s / (double) (n - lag);
                };

                int best = (int) period;
                double bestValue = -1.0e30;

                for (int lag = (int) (period * 0.96); lag <= (int) (period * 1.04) + 1; ++lag)
                    if (const double c = corr (lag); c > bestValue)
                    {
                        bestValue = c;
                        best = lag;
                    }

                const double l = corr (best - 1), c0 = corr (best), r = corr (best + 1);
                const double shift = 0.5 * (l - r) / (l - 2.0 * c0 + r);
                return sr / (best + (std::isfinite (shift) ? shift : 0.0));
            };

            auto levelDb = [&] (const juce::AudioBuffer<float>& b, double from, double to)
            {
                const int start = (int) (from * sr), end = juce::jmin (b.getNumSamples(), (int) (to * sr));
                double sum = 0.0;

                for (int i = start; i < end; ++i)
                    sum += 0.5 * ((double) b.getSample (0, i) * b.getSample (0, i) + (double) b.getSample (1, i) * b.getSample (1, i));

                return juce::Decibels::gainToDecibels ((float) std::sqrt (sum / juce::jmax (1, end - start)), -150.0f);
            };

            auto peakOf = [&] (const juce::AudioBuffer<float>& b, double from, double to)
            {
                float p = 0.0f;

                for (int ch = 0; ch < 2; ++ch)
                    p = juce::jmax (p, b.getMagnitude (ch, (int) (from * sr), (int) ((to - from) * sr)));

                return p;
            };

            // In tune: each open string of an eight-string, and two high frets
            {
                double worst = 0.0;
                juce::String report;

                for (int key : { 30, 35, 40, 45, 50, 55, 59, 64, 76, 88 })
                {
                    const auto r = guitarsound::render ("Jazz Box", { { key, 0.0, 1.5, 100 } }, 1.6);
                    const double hz = pitchOf (r.audio, 0.3, 1.3, keyHz (key));
                    const double cents = 1200.0 * std::log2 (hz / keyHz (key));
                    worst = std::max (worst, std::abs (cents));
                    report << key << ": " << juce::String (cents, 1) << "  ";
                }

                std::cout << "      cents off: " << report << std::endl;
                check (worst < 5.0, "every string in tune, F#1 to E4 and two high frets: worst " + juce::String (worst, 2) + " cents");
            }

            // Palm mute: the same note, a thump that's gone while the open one rings on
            {
                const auto open = guitarsound::render ("Jazz Box", { { 40, 0.0, 1.0, 110 } }, 1.0);
                const auto muted = guitarsound::render ("Jazz Box", { { guitarsound::palmMuteKey, 0.0, 1.0, 100 }, { 40, 0.0, 1.0, 110 } }, 1.0);
                const float openDrop = levelDb (open.audio, 0.02, 0.08) - levelDb (open.audio, 0.35, 0.45);
                const float mutedDrop = levelDb (muted.audio, 0.02, 0.08) - levelDb (muted.audio, 0.35, 0.45);
                check (mutedDrop > openDrop + 15.0f, "a palm-muted E dies " + juce::String (mutedDrop, 1) + " dB in 0.4 s, the open one " + juce::String (openDrop, 1) + " dB");
                // Its pitch once the pick has gone by (the first few periods are the pick's burst)
                const double hz = pitchOf (muted.audio, 0.06, 0.2, keyHz (40));
                check (std::abs (1200.0 * std::log2 (hz / keyHz (40))) < 25.0, "and still has its pitch (" + juce::String (hz, 1) + " Hz for 82.4)");
            }

            // Dead notes: a scratch, short
            {
                const auto dead = guitarsound::render ("Jazz Box", { { guitarsound::deadNoteKey, 0.0, 1.0, 100 }, { 40, 0.0, 0.5, 110 }, { 45, 0.008, 0.5, 110 } }, 0.6);
                const float drop = levelDb (dead.audio, 0.0, 0.03) - levelDb (dead.audio, 0.15, 0.25);
                // The strings are gone in 35 ms; what's left by 0.2 s is the room's tail
                check (drop > 24.0f, "dead notes are a short scratch: " + juce::String (drop, 1) + " dB down by 0.2 s");

                const auto funk = guitarsound::render ("Funk Clean", { { guitarsound::deadNoteKey, 0.0, 1.0, 100 }, { 52, 0.0, 0.3, 110 }, { 56, 0.008, 0.3, 110 } }, 0.5);
                const float funkDrop = levelDb (funk.audio, 0.0, 0.03) - levelDb (funk.audio, 0.15, 0.25);
                check (funkDrop > 20.0f, "and on Funk Clean, compressed, still short: " + juce::String (funkDrop, 1) + " dB down");
            }

            // Legato: no new pluck, the pitch moves on the same string
            {
                const auto hammer = guitarsound::render ("Jazz Box", { { 52, 0.0, 1.0, 110 }, { 54, 0.5, 0.5, 110 } }, 1.1);
                const float before = peakOf (hammer.audio, 0.44, 0.50), after = peakOf (hammer.audio, 0.50, 0.56);
                const double hz = pitchOf (hammer.audio, 0.6, 0.95, keyHz (54));
                check (after <= before * 1.2f && std::abs (1200.0 * std::log2 (hz / keyHz (54))) < 10.0,
                       "a hammer-on: no new attack (" + juce::String (after / juce::jmax (1.0e-6f, before), 2) + "x), now on F#3 (" + juce::String (hz, 1) + " Hz)");

                const auto slide = guitarsound::render ("Jazz Box", { { 52, 0.0, 1.2, 110 }, { 59, 0.5, 0.7, 110 } }, 1.25);
                const float sb = peakOf (slide.audio, 0.44, 0.50), sa = peakOf (slide.audio, 0.50, 0.56);
                const double shz = pitchOf (slide.audio, 0.65, 1.1, keyHz (59));
                check (sa <= sb * 1.2f && std::abs (1200.0 * std::log2 (shz / keyHz (59))) < 10.0,
                       "a slide from E3 up to B3: no new attack (" + juce::String (sa / juce::jmax (1.0e-6f, sb), 2) + "x), arriving on B3 (" + juce::String (shz, 1) + " Hz)");

                const auto fresh = guitarsound::render ("Jazz Box", { { 52, 0.0, 0.45, 110 }, { 54, 0.5, 0.5, 110 } }, 1.1);
                check (peakOf (fresh.audio, 0.50, 0.56) > peakOf (fresh.audio, 0.44, 0.50) * 1.5f, "(where a note that doesn't overlap is plucked again)");
            }

            // A chord on six strings
            {
                const auto chord = guitarsound::render ("Clean", { { 40, 0.0, 1.0, 100 }, { 47, 0.01, 1.0, 100 }, { 52, 0.02, 1.0, 100 },
                                                                   { 56, 0.03, 1.0, 100 }, { 59, 0.04, 1.0, 100 }, { 64, 0.05, 1.0, 100 } }, 1.2);
                check (chord.mostStrings == 6, "an open E chord sounds on six strings (" + juce::String (chord.mostStrings) + ")");
            }

            // Every sound: its phrase, finite, under 0 dBTP, near Nova's level
            {
                const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Nightshift guitar test");
                folder.createDirectory();
                bool ok = true;
                juce::String report;

                for (auto& preset : guitarsound::presetNames())
                {
                    const bool heavy = preset == "High Gain" || preset == "Djent";
                    const auto which = heavy ? "chug" : preset == "Crunch" || preset == "Punk" ? "riff" : "chords";
                    const auto hits = guitarsound::phrase (which);
                    double end = 0.0;

                    for (auto& h : hits)
                        end = std::max (end, h.start + h.length);

                    const auto r = guitarsound::render (preset, hits, end + 1.5);
                    bool finite = true;

                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < r.audio.getNumSamples(); i += 7)
                            finite = finite && std::isfinite (r.audio.getSample (ch, i));

                    const auto file = folder.getChildFile (preset + ".wav");
                    file.deleteFile();
                    {
                        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
                        auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                                          .withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24));

                        if (writer != nullptr)
                            writer->writeFromAudioSampleBuffer (r.audio, 0, r.audio.getNumSamples());
                    }

                    const auto m = mastering::measureFile (file);
                    const bool fine = finite && m.truePeakDb < 0.0f && m.maxMomentaryLufs > -22.0f && m.maxMomentaryLufs < -14.0f;
                    ok = ok && fine;
                    report << preset << " " << juce::String (m.maxMomentaryLufs, 1) << " (" << juce::String (m.integratedLufs, 1) << ", peak "
                           << juce::String (m.truePeakDb, 1) << ")" << (fine ? "" : " <-") << "; ";
                }

                folder.deleteRecursively();
                std::cout << "      loudest 400 ms (integrated, true peak): " << report << std::endl;
                check (ok, "every sound plays its phrase finite, under 0 dBTP, its loudest 400 ms between -22 and -14 LUFS");
            }

            // Faster than real time: the heaviest, double-tracked
            {
                std::vector<guitarsound::Hit> hits;

                for (int loop = 0; loop < 5; ++loop)
                    for (auto h : guitarsound::phrase ("chug"))
                    {
                        h.start += loop * 4 * 60.0 / 140.0 * 4.0;
                        hits.push_back (h);
                    }

                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                const auto r = guitarsound::render ("Djent", hits, 20.0);
                const double seconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
                const double factor = 20.0 / juce::jmax (1.0e-6, seconds);
                check (factor > 10.0, "20 s of double-tracked Djent chugs in " + juce::String (seconds, 2) + " s: " + juce::String (factor, 0) + "x real time");
            }

            // In the studio: a Guitar channel on High Gain, playing a power chord
            {
                Song song;
                song.loadBlank();
                AudioEngine audio (song, false);
                audio.syncNow();
                MainComponent main (song, audio);
                main.setSize (1600, 940);

                const int id = main.getRack().addGuitarChannel ("High Gain");
                auto* c = song.findChannel (id);
                check (c != nullptr && c->name == "Guitar" && isGuitar (*c) && guitarPresetOf (*c) == "High Gain" && song.selectedChannelId == id,
                       "addGuitarChannel makes a selected channel called Guitar, on High Gain");

                audio.syncNow();
                auto* plugin = dynamic_cast<GuitarPlugin*> (audio.getInstrument (id));
                check (plugin != nullptr && plugin->getPreset() == "High Gain", "the engine plays it on Nightshift Guitar, High Gain");

                auto& pattern = song.currentPattern();

                for (int key : { 40, 47, 52 })
                    pattern.notesFor (id).push_back ({ 0, ticksPerBeat * 2, key, 110 });

                song.mode = PlayMode::pattern;
                song.notify (Song::Change::notes);

                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-guitar-studio.wav");
                const bool rendered = audio.renderToFile (file);
                const auto m = mastering::measureFile (file);
                file.deleteFile();
                check (rendered && m.maxMomentaryLufs > -40.0f && m.truePeakDb < 0.0f, "an E5 power chord renders through the studio ("
                                                                                          + juce::String (m.maxMomentaryLufs, 1) + " LUFS, peak " + juce::String (m.truePeakDb, 1) + " dBTP)");

                song.checkpoint();
                audio.setGuitarPreset (id, "Jazz Box");
                audio.syncNow();
                check (guitarPresetOf (*song.findChannel (id)) == "Jazz Box" && audio.getInstrument (id) == plugin && plugin->getPreset() == "Jazz Box",
                       "setGuitarPreset switches it to Jazz Box without building it again");

                song.undo();
                audio.syncNow();
                check (guitarPresetOf (*song.findChannel (id)) == "High Gain", "undo puts High Gain back");

                const auto saved = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-guitar-test.nightshift");
                song.save (saved);
                Song loaded;
                loaded.load (saved);
                saved.deleteFile();
                const Channel* back = nullptr;

                for (auto& ch : loaded.channels)
                    if (ch.name == "Guitar")
                        back = &ch;

                check (back != nullptr && isGuitar (*back) && guitarPresetOf (*back) == "High Gain", "saved and loaded with its sound");
                check (instruments::displayName (c->instrument) == "Guitar, High Gain" && trackicons::forChannel (*song.findChannel (id)) == "guitar",
                       "named \"Guitar, High Gain\" and drawn as a guitar");

                main.getSidebar().setSearchText ("guitar");
                juce::MessageManager::getInstance()->runDispatchLoopUntil (60);     // the search box tells the list on the next message
                const auto shown = main.getSidebar().getShownSounds();
                bool listed = true;

                for (auto& name : guitarsound::presetNames())
                    listed = listed && shown.contains ("Guitar, " + name);

                check (listed, "the sidebar lists every guitar sound (" + juce::String (shown.size()) + " shown for \"guitar\": " + shown.joinIntoString (" | ") + ")");

                // An old project's sampled instrument is still a sampled instrument
                Channel piano;
                piano.type = InstrumentType::instrument;
                piano.instrument = "C:/somewhere/Piano.sfz";
                check (! isGuitar (piano) && guitarPresetOf (piano).isEmpty(), "a sampled instrument isn't taken for a guitar");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--synthtest"))
        {
            // Nova in the studio, as you'd find it: a synth channel's sound in the Synth tab (F11),
            // a control for every parameter, turning one changing the sound, the next factory sound
            // and undo back (knobs turned kept), the prompt bar staying the drummer's, the sound
            // saved with the song.
            Song song;
            song.loadDemo();
            AudioEngine audio (song, false);
            audio.syncNow();
            MainComponent main (song, audio);
            main.setSize (1600, 940);
            main.setVisible (true);

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto settle = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (40); };

            auto click = [&main] (juce::Component& on)
            {
                const auto at = main.getLocalPoint (&on, on.getLocalBounds().getCentre().toFloat());
                auto* target = main.getComponentAt (at.toInt());

                if (target == nullptr)
                    return false;

                const auto now = juce::Time::getCurrentTime();
                const auto p = target->getLocalPoint (&main, at);
                auto event = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p,
                                               juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                               target, target, now, p, now, 1, false);
                target->mouseDown (event);
                target->mouseUp (event.withNewPosition (p));
                return true;
            };

            auto reachable = [&main] (juce::Component& c)
            {
                auto* hit = main.getComponentAt (main.getLocalPoint (&c, c.getLocalBounds().getCentre()));
                return c.isVisible() && hit != nullptr && (hit == &c || c.isParentOf (hit));
            };

            auto& bar = main.getDrummerBar();
            auto& panel = main.getSynth();
            using Panel = MainComponent::Panel;

            auto channelNamed = [&song] (const juce::String& name) -> Channel*
            {
                for (auto& c : song.channels)
                    if (c.name == name)
                        return &c;

                return nullptr;
            };

            // The demo's Keys: selected, then F11
            auto* keys = channelNamed ("Keys");
            song.selectedChannelId = keys->id;
            song.notify (Song::Change::selection);
            settle();
            main.keyPressed (juce::KeyPress (juce::KeyPress::F11Key));
            settle();
            check (main.isPanelVisible (Panel::synth) && panel.getChannelId() == keys->id && bar.getMode() == DrummerBar::Mode::drummer,
                   "F11 opens the Synth tab on the selected synth; the prompt bar stays the drummer's");

            auto* nova = audio.getNova (keys->id);
            check (nova != nullptr && keys->synthPreset == "Velvet Keys", "the demo's Keys play Nova's Velvet Keys");

            // A control for every parameter, the shown page's on screen
            juce::StringArray missing;

            for (auto& p : nova::params())
                if (panel.findControl (p.id) == nullptr)
                    missing.add (p.id);

            check (missing.isEmpty() && panel.findControl ("osc1_shape") != nullptr && reachable (*panel.findControl ("osc1_shape")),
                   juce::String ((int) nova::params().size()) + " parameters each have a control, the oscillators' on screen"
                     + (missing.isEmpty() ? juce::String() : "; missing: " + missing.joinIntoString (", ")));

            panel.showPage (1);
            settle();
            auto* cutoff = dynamic_cast<controls::ParamKnob*> (panel.findControl ("filter_cutoff"));
            check (cutoff != nullptr && reachable (*cutoff), "the Filter page shows the cutoff");

            if (cutoff != nullptr)
                cutoff->slider.setValue (800.0, juce::sendNotificationSync);

            check (nova != nullptr && std::abs (nova->findParam ("filter_cutoff")->get() - 800.0f) < 1.0f, "turning the cutoff knob changes Nova's cutoff");

            // The next factory sound, then undo: back to Velvet Keys with the cutoff as it was turned
            check (click (panel.getNextButton()), "the next-sound arrow can be clicked");
            settle();
            check (keys->synthPreset == "Dusty Rhodes" && audio.getNova (keys->id) != nullptr
                     && std::abs (audio.getNova (keys->id)->findParam ("lfo1_rate")->get() - 4.2f) < 0.01f,
                   "it plays the next factory sound, Dusty Rhodes");

            song.undo();
            audio.syncNow();
            settle();
            nova = audio.getNova (keys->id);
            check (keys->synthPreset == "Velvet Keys" && nova != nullptr && std::abs (nova->findParam ("filter_cutoff")->get() - 800.0f) < 1.0f,
                   "undo brings back Velvet Keys with the cutoff where it was turned");
            check (panel.findControl ("filter_cutoff") != nullptr
                     && &static_cast<controls::ParamKnob*> (panel.findControl ("filter_cutoff"))->param == nova->findParam ("filter_cutoff"),
                   "the controls follow the rebuilt synth");

            // Saved and loaded with the song
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-synth-test.nightshift");
            audio.flushPluginStates();
            song.save (file);
            Song loaded;
            loaded.load (file);
            file.deleteFile();
            Channel* back = nullptr;

            for (auto& c : loaded.channels)
                if (c.name == "Keys")
                    back = &c;

            check (back != nullptr && back->synthPreset == "Velvet Keys" && back->pluginState.isNotEmpty(), "the sound is saved with the song");

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--drumbartest"))
        {
            // The AI drummer from the top of the studio, as you'd find it: its bar on screen in a
            // fresh studio, F8, a request typed and written at once, offline, a kit channel made
            // for it with the style's kit, the song's tempo kept (the demo has other parts), a
            // follow-up building on it (same style, same kit, a section added), another take of the
            // same words, the bar following the selected kit, an idea filling the bar.
            Song song;
            song.loadDemo();
            AudioEngine audio (song, false);
            audio.syncNow();
            MainComponent main (song, audio);
            main.setSize (1600, 940);
            main.setVisible (true);

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto settle = [] { settleMessages (30); };

            // A click delivered to whatever is under the point in the studio
            auto click = [&main] (juce::Component& on)
            {
                const auto at = main.getLocalPoint (&on, on.getLocalBounds().getCentre().toFloat());
                auto* target = main.getComponentAt (at.toInt());

                if (target == nullptr)
                    return false;

                const auto now = juce::Time::getCurrentTime();
                const auto p = target->getLocalPoint (&main, at);
                auto event = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p,
                                               juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                               target, target, now, p, now, 1, false);
                target->mouseDown (event);
                target->mouseUp (event.withNewPosition (p));
                return true;
            };

            // On screen: a click at its middle lands on it (isShowing is false without a window)
            auto reachable = [&main] (juce::Component& c)
            {
                auto* hit = main.getComponentAt (main.getLocalPoint (&c, c.getLocalBounds().getCentre()));
                return c.isVisible() && hit != nullptr && (hit == &c || c.isParentOf (hit));
            };

            auto& bar = main.getDrummerBar();
            auto& panel = main.getDrummer();
            using Panel = MainComponent::Panel;

            auto type = [&] (const juce::String& words)
            {
                bar.setText (words);
                bar.getEditor().keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                settle();
            };

            auto kitChannels = [&song]
            {
                std::vector<Channel*> found;

                for (auto& c : song.channels)
                    if (c.type == InstrumentType::kit)
                        found.push_back (&c);

                return found;
            };

            const int channelsBefore = (int) song.channels.size();
            const double bpmBefore = song.bpm;

            // The prompt lives in the AI tab (the workspace starts right under the top bar); F8 opens it
            // on the drummer, ready to type into
            check (main.getAiPanel().isParentOf (&bar) && main.getSidebarPanel().getY() <= nightshift::Toolbar::height + MainComponent::gap,
                   "the prompt is in the AI tab, and the workspace starts right under the top bar");
            check (panel.channel() == nullptr && kitChannels().empty(), "the demo has no drum kit, so the drummer will make one");

            main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key));
            check (main.isPanelVisible (Panel::drummer) && reachable (bar.getEditor()) && bar.getY() < main.getDrummer().getY(),
                   "F8 opens the AI Drummer tab with its prompt over the drummer's panel, and a click there lands in it");

            // Enter: written at once, on a kit channel made for it
            type ("Lofi jazz on brushes, lazy swing");
            auto kits = kitChannels();
            check (kits.size() == 1 && kits.front()->drummer.turns.size() == 1 && (int) song.channels.size() == channelsBefore + 1,
                   "Enter writes it at once, on a drum kit channel made for it");

            if (kits.size() == 1)
            {
                auto& drums = *kits.front();
                const bool haveBrushes = drummer::kitNamesFor (drums).contains ("DRS Brushes");
                check (! haveBrushes || drumkits::displayName (drums.kit) == "DRS Brushes",
                       "on brushes it chose the brushes kit (" + drumkits::displayName (drums.kit) + ")");

                const auto first = drummer::offline::requestOf (drums.drummer.turns.front());
                check (first.has_value() && first->style == "lofi" && first->brushes, "it understood lofi, on brushes");
                check (song.bpm == bpmBefore, "the demo has other parts, so its tempo stays (" + juce::String (song.bpm, 0) + " bpm)");
                check (drums.drummer.patterns.count ("Intro") == 1 && drums.drummer.patterns.count ("Verse") == 1,
                       "its sections became patterns (Intro, Verse)");
                check (bar.getText().isEmpty() && panel.getStatus().isEmpty() && main.isPanelVisible (Panel::drummer),
                       "the bar clears and the tab shows the conversation");
                check (panel.getSectionChip (0) != nullptr && reachable (*panel.getSectionChip (0)), "with a chip for each section");

                // A follow-up builds on it: the same style and kit, the hats busier, a bridge added
                const auto kit = drums.kit;
                type ("Busier hats, with a bridge");
                check (drums.drummer.turns.size() == 2 && drums.drummer.shown == 1, "a follow-up makes version 2 and plays it");

                if (drums.drummer.turns.size() == 2)
                {
                    const auto second = drummer::offline::requestOf (drums.drummer.turns[1]);
                    check (second.has_value() && second->style == "lofi" && second->brushes && second->busyHats > 0.0f
                             && second->sections.contains ("bridge") && drums.kit == kit,
                           "it's still lofi on brushes, on the same kit, the hats busier, a bridge added");
                    check (drums.drummer.patterns.count ("Bridge") == 1, "and the bridge became a pattern");

                    // The same words again: another take
                    type ("Busier hats, with a bridge");
                    check (drums.drummer.turns.size() == 3 && drums.drummer.turns[2].groove != drums.drummer.turns[1].groove,
                           "the same words again write another take");
                }

                type ("   ");
                check (drums.drummer.turns.size() == 3, "nothing typed writes nothing");

                // Another kit, selected, is the one it writes for; ideas fill the bar
                const int other = main.getRack().addDrumKitChannel (drumkits::ids()[1]);
                settle();
                check (panel.getChannelId() == other, "adding another kit makes it the one the drummer writes for");
                main.setPanelVisible (Panel::drummer, true);
                settle();
                check (panel.getIdeaChip (0) != nullptr && reachable (*panel.getIdeaChip (0)) && click (*panel.getIdeaChip (0)),
                       "a kit with no part yet shows ideas to start from");
                check (bar.getText().isNotEmpty(), "clicking one puts it in the bar (" + bar.getText() + ")");

                song.selectedChannelId = drums.id;
                song.notify (Song::Change::selection);
                check (panel.getChannelId() == drums.id, "selecting the first kit makes it the one again");

                main.setPanelVisible (Panel::rack, true);
                main.openChannel (drums.id);
                check (main.isPanelVisible (Panel::drummer), "double-clicking a drum kit channel opens the AI Drummer tab");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--inspectortest"))
        {
            // The whole studio on the demo, with real mouse events sent to whatever is under
            // the point: clicking a track in Arrange shows its channel in the Inspector, the
            // strip's fader, mute and solo work, and double-clicks open a track's channel, a
            // clip's pattern and a Steps row in the right editor
            Song song;
            song.loadDemo();
            AudioEngine audio (song, false);
            audio.syncNow();
            MainComponent main (song, audio);
            main.setSize (1600, 940);
            main.setVisible (true);     // or nothing is found under the mouse

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            // A press (and drag) at a point in `on`, delivered to what's under it in the studio
            auto send = [&main] (juce::Component& on, juce::Point<float> local, int clicks, juce::Point<float> dragBy = {})
            {
                const auto at = main.getLocalPoint (&on, local);
                auto* target = main.getComponentAt (at.toInt());

                if (target == nullptr)
                    return;

                const auto now = juce::Time::getCurrentTime();
                const auto start = target->getLocalPoint (&main, at);
                auto event = [&] (juce::Point<float> p, bool dragged)
                {
                    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p,
                                             juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                             target, target, now, start, now, clicks, dragged);
                };

                target->mouseDown (event (start, false));

                if (! dragBy.isOrigin())
                    target->mouseDrag (event (start + dragBy, true));

                target->mouseUp (event (start + dragBy, ! dragBy.isOrigin()));

                if (clicks == 2)
                    target->mouseDoubleClick (event (start, false));
            };

            auto click = [&send] (juce::Component& on, juce::Point<float> p) { send (on, p, 1); };
            auto doubleClick = [&send] (juce::Component& on, juce::Point<float> p) { send (on, p, 1); send (on, p, 2); };
            auto idOf = [&song] (const juce::String& name) { for (auto& c : song.channels) if (c.name == name) return c.id; return 0; };
            auto patternNamed = [&song] (const juce::String& name) { for (auto& p : song.patterns) if (p.name == name) return p.id; return 0; };

            auto& playlist = main.getPlaylist();
            auto& strip = main.getInspector().getChannelStrip();
            // On the name (the icon, left of it, opens the icon menu)
            auto header = [] (int row) { return juce::Point<float> (110.0f, (float) (Playlist::rulerHeight + row * Playlist::rowHeight + Playlist::rowHeight / 2)); };
            auto shown = [&song, &strip] (int id) { return song.selectedChannelId == id && strip.getChannel() == id; };
            const int keysRow = 0, drumsRow = 1, bassRow = 2, guitarRow = 3;     // 3: a new track under the demo's three
            using Panel = MainComponent::Panel;

            check (main.isInspectorVisible(), "at 1600 wide the Inspector is showing");

            // Clicking a track shows its channel
            click (playlist, header (bassRow));
            check (shown (idOf ("808")), "clicking the 808 track shows the 808 in the Inspector");

            click (playlist, header (keysRow));
            check (shown (idOf ("Keys")), "clicking the Keys track (Keys and Pad) shows Keys, the first of them");

            click (playlist, header (drumsRow));
            check (shown (idOf ("Kick")), "clicking the drums track shows Kick, the first drum");

            song.selectedChannelId = idOf ("Snare");
            song.notify (Song::Change::selection);
            click (playlist, header (drumsRow));
            check (shown (idOf ("Snare")), "clicking it again keeps the drum already showing (Snare)");

            // The strip: fader, mute, solo
            click (playlist, header (keysRow));
            auto keysChannel = [&song, &idOf] () -> Channel& { return *song.findChannel (idOf ("Keys")); };
            const float before = keysChannel().volume;
            auto& fader = strip.getFader();
            send (fader, fader.getLocalBounds().getCentre().toFloat(), 1, { 0.0f, 60.0f });
            const float dragged = keysChannel().volume;
            check (dragged < before - 0.05f, "dragging Keys' fader down turns it down (" + juce::String (before, 2) + " -> " + juce::String (dragged, 2) + ")");

            send (fader, fader.getLocalBounds().getCentre().toFloat(), 2);
            check (std::abs (keysChannel().volume - 0.78f) < 0.001f, "double-clicking the fader puts it back to 0 dB");

            song.undo();
            check (std::abs (keysChannel().volume - dragged) < 0.001f && std::abs (fader.getValue() - dragged) < 0.001,
                   "and undo takes that back, fader and all");

            song.selectedChannelId = idOf ("Keys");
            song.notify (Song::Change::selection);
            auto& keys = keysChannel();
            click (strip, strip.getMuteArea().getCentre().toFloat());
            const bool mutedByStrip = keys.muted;
            click (strip, strip.getMuteArea().getCentre().toFloat());
            check (mutedByStrip && ! keys.muted, "Mute on the strip mutes Keys, and again unmutes it");

            click (strip, strip.getSoloArea().getCentre().toFloat());
            bool othersMuted = ! keys.muted;

            for (auto& c : song.channels)
                othersMuted = othersMuted && (c.id == keys.id || c.muted);

            click (strip, strip.getSoloArea().getCentre().toFloat());
            bool allBack = true;

            for (auto& c : song.channels)
                allBack = allBack && ! c.muted;

            check (othersMuted && allBack, "Solo on the strip leaves only Keys playing, and again brings everything back");

            // Double-clicks
            main.setPanelVisible (Panel::rack, true);
            doubleClick (playlist, header (bassRow));
            check (shown (idOf ("808")) && main.isPanelVisible (Panel::pianoRoll) && song.currentPatternId == patternNamed ("808"),
                   "double-clicking the 808 track opens its notes in the piano roll");

            doubleClick (playlist, header (drumsRow));
            const auto selectedDrum = song.findChannel (song.selectedChannelId);
            check (main.isPanelVisible (Panel::rack) && song.currentPatternId == patternNamed ("Beat")
                     && selectedDrum != nullptr && selectedDrum->type == InstrumentType::sampler && selectedDrum->name != "808",
                   "double-clicking the drums track opens its first pattern (Beat) in Steps, with a drum selected");

            doubleClick (playlist, { (float) Playlist::headerWidth + 30.0f, header (keysRow).y });
            check (shown (idOf ("Keys")) && main.isPanelVisible (Panel::pianoRoll) && song.currentPatternId == patternNamed ("Keys"),
                   "double-clicking a Keys clip opens it in the piano roll, on Keys");

            // Steps: double-clicking a drum's name selects it and stays in Steps
            main.setPanelVisible (Panel::rack, true);
            auto& rack = main.getRack();
            const auto kickIndex = (int) std::distance (song.channels.begin(),
                                                        std::find_if (song.channels.begin(), song.channels.end(), [] (auto& c) { return c.name == "Kick"; }));
            doubleClick (rack, { 60.0f, (float) (ChannelRack::headerHeight + kickIndex * ChannelRack::rowHeight + ChannelRack::rowHeight / 2) });
            check (shown (idOf ("Kick")) && main.isPanelVisible (Panel::rack), "double-clicking Kick's name in Steps shows it, in Steps");

            // An audio track: clicking its clip shows it
            const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-inspectortest.wav");
            {
                wav.deleteFile();
                juce::AudioBuffer<float> silence (1, 48000);
                silence.clear();
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (wav);
                auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                                  .withSampleRate (48000.0)
                                                                                  .withNumChannels (1)
                                                                                  .withBitsPerSample (24));

                if (writer != nullptr)
                    writer->writeFromAudioSampleBuffer (silence, 0, silence.getNumSamples());
            }

            const int guitar = song.addChannel ("Guitar", InstrumentType::audio).id;
            AudioClip clip;
            clip.channelId = guitar;
            clip.track = guitarRow;
            clip.length = 1.0;
            clip.takes.push_back ({ wav.getFullPathName(), 0.0, 0.0, 1.0 });
            clip.comp.push_back ({ 0.0, 1.0, 0 });
            song.audioClips.push_back (clip);
            song.coverClipsWithTracks();
            song.notify (Song::Change::channels);
            song.notify (Song::Change::playlist);

            click (playlist, { (float) Playlist::headerWidth + 10.0f, header (guitarRow).y });
            check (shown (guitar), "clicking a guitar clip shows the Guitar track");

            click (playlist, header (keysRow));
            click (playlist, header (guitarRow));
            check (shown (guitar), "and so does clicking its track");

            // Narrow windows: the Inspector waits to be asked for
            main.setSize (1100, 640);
            const bool hiddenNarrow = ! main.isInspectorVisible();
            main.toggleInspector();
            check (hiddenNarrow && main.isInspectorVisible() && main.getInspector().getWidth() == Inspector::width,
                   "at 1100 wide the Inspector hides until I asks for it");

            wav.deleteFile();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--mutetest"))
        {
            // Mutes the Keys row in the middle of its chord while the song plays (only Keys
            // soloed), then listens: once the synth's release has had time, it must be silent.
            // Muting once rebuilt the clips and left the chord's notes held for ever
            Song song;
            song.loadDemo();
            song.mode = PlayMode::song;
            song.tracks[0].soloed = true;
            song.notify (Song::Change::transport);
            song.notify (Song::Change::playlist);
            AudioEngine audio (song, false);
            audio.syncNow();
            audio.togglePlay();

            const double sr = 48000.0;
            const int block = 512;
            juce::AudioBuffer<float> buffer (2, block);
            float sounding = 0.0f, after = 0.0f;

            for (int b = 0; b < (int) (sr * 4.0) / block; ++b)
            {
                const double t = b * block / sr;

                if (std::abs (t - 0.5) < 0.5 * block / sr)
                {
                    song.tracks[0].muted = true;
                    song.notify (Song::Change::trackState);
                }

                buffer.clear();
                audio.processHostedBlock (buffer);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

                const float level = buffer.getMagnitude (0, block);

                if (t > 0.2 && t < 0.5)
                    sounding = std::max (sounding, level);
                else if (t > 3.0)
                    after = std::max (after, level);
            }

            const auto db = [] (float g) { return juce::String (juce::Decibels::gainToDecibels (g, -120.0f), 1) + " dB"; };
            std::cout << "chord before muting peaks at " << db (sounding) << ", 2.5 s after muting " << db (after) << std::endl;
            const bool ok = sounding > 0.01f && after < 0.001f;
            std::cout << (ok ? "PASS  " : "FAIL  ") << "muting a row mid-chord lets its notes go" << std::endl;
            return ok ? 0 : 1;
        }

        if (args.has ("--arrangetest"))
        {
            // Arrange on the demo, with real mouse events: mute and solo in the track headers
            // change what the engine plays, the tools paint and erase, snap places clips, and
            // track names, mutes and solos survive saving
            Song song;
            song.loadDemo();
            song.mode = PlayMode::song;
            song.notify (Song::Change::transport);
            AudioEngine audio (song, false);
            audio.syncNow();
            Playlist playlist (song, audio);
            playlist.setSize (1600, 800);

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto click = [&playlist] (juce::Point<float> p)
            {
                const auto now = juce::Time::getCurrentTime();
                const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), p,
                                          juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          &playlist, &playlist, now, p, now, 1, false);
                playlist.mouseDown (e);
                playlist.mouseUp (e);
            };

            auto clipsPlaying = [&audio]
            {
                audio.syncNow();
                int n = 0;

                for (auto track : te::getAudioTracks (audio.getEdit()))
                    for (auto clip : track->getClips())
                        n += clip->isMuted() ? 0 : 1;

                return n;
            };

            // Header buttons sit at the right of each row's header: solo outermost, then mute
            auto buttonAt = [] (int row, int slot)
            {
                return juce::Point<float> (Playlist::headerWidth - 10.0f - slot * 22.0f - (slot - 1) * 3.0f + 11.0f,
                                           (float) (Playlist::rulerHeight + row * Playlist::rowHeight + Playlist::rowHeight / 2));
            };

            const int all = clipsPlaying();
            const int drumsRow = 1, bassRow = 2;

            click (buttonAt (drumsRow, 2));
            const int withoutDrums = clipsPlaying();
            check (song.tracks[drumsRow].muted && withoutDrums > 0 && withoutDrums < all,
                   "M on the drums row mutes it: " + juce::String (all) + " clips playing, " + juce::String (withoutDrums) + " without the drums");

            click (buttonAt (drumsRow, 2));
            check (! song.tracks[drumsRow].muted && clipsPlaying() == all, "M again brings them back");

            click (buttonAt (bassRow, 1));
            const int soloed = clipsPlaying();
            check (song.tracks[bassRow].soloed && soloed == 2, "S on the 808 row plays only its 2 clips (" + juce::String (soloed) + ")");

            click (buttonAt (bassRow, 1));
            check (! song.tracks[bassRow].soloed && clipsPlaying() == all, "S again plays everything");

            // Tools, on the Add track line under the last track; bar 3 and a half beat in
            const int tracksBefore = song.numTracks();
            const float emptyRowY = (float) (Playlist::rulerHeight + tracksBefore * Playlist::rowHeight + Playlist::rowHeight / 2);
            const float x = Playlist::headerWidth + 72.0f * 2.0f + 72.0f * 0.375f;
            const auto placed = song.playlist.size();

            playlist.setTool (Playlist::Tool::select);
            click ({ x, emptyRowY });
            check (song.playlist.size() == placed, "the select tool leaves empty space alone");

            playlist.setTool (Playlist::Tool::draw);
            playlist.setSnap (ticksPerBeat);
            click ({ x, emptyRowY });
            check (song.playlist.size() == placed + 1 && song.playlist.back().start == 2 * ticksPerBar + ticksPerBeat
                       && song.playlist.back().track == tracksBefore && song.numTracks() == tracksBefore + 1,
                   "the draw tool paints the pattern on a new track under the last, snapped to the beat (bar 3 beat 2)");

            playlist.setTool (Playlist::Tool::erase);
            click ({ x + 10.0f, emptyRowY });
            check (song.playlist.size() == placed, "the erase tool deletes it");

            // Names, mutes and solos are saved
            song.tracks[drumsRow].name = "Drums";
            song.tracks[drumsRow].icon = "guitar";
            song.tracks[bassRow].muted = true;
            song.tracks[0].soloed = true;
            Song copy;
            copy.fromVar (song.toVar());
            const bool same = copy.numTracks() == song.numTracks()
                              && std::equal (song.tracks.begin(), song.tracks.end(), copy.tracks.begin(), [] (const Track& a, const Track& b)
                                 {
                                     return a.name == b.name && a.icon == b.icon && a.channelId == b.channelId
                                              && a.muted == b.muted && a.soloed == b.soloed;
                                 });
            check (same, "track names, icons, mutes and solos survive saving and loading");

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--trackstest"))
        {
            // Arrange's tracks: only the ones added, Add track and drops making them, a sound
            // dropped on an instrument's track replacing its instrument (and the engine playing
            // the new one), painting on an instrument's track, clips dragged below the last
            // track, icons, and old projects' fixed rows
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            {
                Song empty;
                empty.loadEmpty();
                check (empty.numTracks() == 0, "a new song has no tracks (" + juce::String (empty.numTracks()) + ")");
            }

            Song song;
            song.loadDemo();
            song.mode = PlayMode::song;
            song.notify (Song::Change::transport);

            bool noneEmpty = true;

            for (int row = 0; row < song.numTracks(); ++row)
                noneEmpty = noneEmpty && ! song.isTrackEmpty (row);

            check (song.numTracks() == 3 && noneEmpty, "the demo has its three tracks and no empty ones (" + juce::String (song.numTracks()) + ")");

            // Old projects: twelve fixed rows, the used ones kept in order, names and mutes kept
            {
                Song old;
                old.loadDemo();

                for (auto& c : old.playlist)
                    c.track = c.track == 2 ? 6 : c.track;

                old.coverClipsWithTracks();
                auto v = old.toVar();
                auto* o = v.getDynamicObject();
                o->removeProperty ("arrangeTracks");
                o->setProperty ("playlistTracks", 12);
                auto* named = new juce::DynamicObject();
                named->setProperty ("row", 1);
                named->setProperty ("name", "Drums");
                named->setProperty ("mute", true);
                o->setProperty ("tracks", juce::Array<juce::var> { juce::var (named) });

                Song loaded;
                loaded.fromVar (v);
                int bassRow = -1;

                for (auto& c : loaded.playlist)
                    if (auto p = loaded.findPattern (c.patternId); p != nullptr && p->name == "808")
                        bassRow = c.track;

                check (loaded.numTracks() == 3 && bassRow == 2 && loaded.tracks[1].name == "Drums" && loaded.tracks[1].muted,
                       "an old project's twelve rows load as its three used tracks, the 808 moved up from row 7, Drums named and muted ("
                         + juce::String (loaded.numTracks()) + " tracks, 808 on " + juce::String (bassRow) + ")");
            }

            AudioEngine audio (song, false);
            audio.syncNow();
            ChannelRack rack (song, audio);
            Playlist playlist (song, audio);
            playlist.setSize (1600, 1400);
            playlist.onAddSound = [&rack] (const juce::String& d) { return rack.addSound (d); };
            playlist.onReplaceSound = [&rack] (int id, const juce::String& d) { return rack.replaceInstrument (id, d); };
            playlist.nameOfSound = [&rack] (const juce::String& d) { return rack.soundName (d); };
            playlist.onAddEffect = [&audio, &rack] (int id, const juce::String& d)
            {
                if (auto desc = audio.findEffectType (d.fromFirstOccurrenceOf ("effect:", false, false)))
                    rack.addEffect (id, *desc);
            };

            int addMenuAt = -2;
            playlist.onShowAddMenu = [&addMenuAt] (int at, juce::Rectangle<int>) { addMenuAt = at; };

            auto rowY = [] (int row, float within = 0.5f) { return (int) (Playlist::rulerHeight + (row + within) * Playlist::rowHeight); };
            auto dropAt = [&playlist] (const juce::String& what, int x, int y) { playlist.itemDropped ({ what, nullptr, { x, y } }); };
            const auto preset = nova::factory().front().name;
            const auto synthSound = juce::String (soundDragPrefix) + "synth:" + preset;
            auto patternNamed = [&song] (const juce::String& name) { for (auto& p : song.patterns) if (p.name == name) return p.id; return 0; };
            auto rowOfPattern = [&song] (int patternId) { for (auto& c : song.playlist) if (c.patternId == patternId) return c.track; return -1; };

            auto mouse = [&playlist] (juce::Point<float> p, int what)    // 0 down, 1 drag, 2 up
            {
                const auto now = juce::Time::getCurrentTime();
                const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), p,
                                          juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          &playlist, &playlist, now, p, now, 1, false);

                if (what == 0)       playlist.mouseDown (e);
                else if (what == 1)  playlist.mouseDrag (e);
                else                 playlist.mouseUp (e);
            };

            // The Add track line under the last track opens Add track's menu, for the end
            mouse ({ 100.0f, (float) rowY (3) }, 0);
            mouse ({ 100.0f, (float) rowY (3) }, 2);
            check (addMenuAt == -1, "clicking Add track under the last track asks for its menu, for a track at the end");

            // Add track's choices: an instrument gets a track of its own
            const int pad = rack.addSynthChannel (preset);
            playlist.addTrackFor (pad);
            check (song.numTracks() == 4 && song.tracks[3].channelId == pad && song.selectedChannelId == pad,
                   "adding a synth from Add track makes a track for it at the end, selected");

            const int guitar = rack.addAudioChannel ("Guitar");
            const int guitarRow = song.numTracks() - 1;
            check (song.tracks[(size_t) guitarRow].channelId == guitar && trackicons::forTrack (song, guitarRow) == "guitar",
                   "an audio track named Guitar comes with its track, and a guitar on it");

            const int guitar2 = rack.addAudioChannel ("Guitar");
            check (song.findChannel (guitar2) != nullptr && song.findChannel (guitar2)->name == "Guitar 2", "a second one is Guitar 2");

            playlist.addTrackFor (guitar2, 0);
            check (song.tracks[0].channelId == guitar2 && song.numTracks() == 6 && rowOfPattern (patternNamed ("Keys")) == 1,
                   "Add track under another (at the top here) puts it there, the clips below moving down");

            song.undo();
            check (song.numTracks() == 5 && song.findChannel (guitar2) == nullptr && song.tracks[0].channelId != guitar2,
                   "one undo takes Guitar 2 away with its track");

            // Drops
            const int channelsBefore = (int) song.channels.size();
            dropAt (synthSound, 800, rowY (song.numTracks()));
            const int dropped = song.channels.back().id;
            check ((int) song.channels.size() == channelsBefore + 1 && song.numTracks() == 6 && song.tracks[5].channelId == dropped,
                   "a sound dropped under the last track gets a new track, at the end");

            const int beatRow = rowOfPattern (patternNamed ("Beat"));
            dropAt (synthSound, 800, rowY (beatRow, 0.0f) + 2);
            const int between = song.channels.back().id;
            check (song.tracks[(size_t) beatRow].channelId == between && rowOfPattern (patternNamed ("Beat")) == beatRow + 1,
                   "dropped on the line above Beat, it goes in there and Beat moves down");

            const int beatNow = rowOfPattern (patternNamed ("Beat"));
            dropAt (synthSound, 800, rowY (beatNow));
            check (song.tracks[(size_t) beatNow + 1].channelId == song.channels.back().id && rowOfPattern (patternNamed ("Beat")) == beatNow,
                   "dropped on Beat (a pattern of seven drums), it gets a track of its own under Beat");

            // On an instrument's track: it plays that part instead
            int bassRow = rowOfPattern (patternNamed ("808"));
            const int bass = song.trackChannel (bassRow);
            const auto* bassPattern = song.findPattern (patternNamed ("808"));
            const auto notesBefore = bassPattern->findNotes (bass) != nullptr ? bassPattern->findNotes (bass)->size() : 0;
            const int channelsNow = (int) song.channels.size();
            dropAt (synthSound, 800, rowY (bassRow));
            const auto* swapped = song.findChannel (bass);
            check (swapped != nullptr && swapped->type == InstrumentType::synth && swapped->synthPreset == preset
                     && (int) song.channels.size() == channelsNow && song.findPattern (patternNamed ("808"))->findNotes (bass)->size() == notesBefore,
                   "a synth dropped on the 808's track replaces its instrument and keeps its " + juce::String ((int) notesBefore) + " notes");

            // and the engine plays it
            {
                for (auto& t : song.tracks)
                    t.soloed = false;

                song.tracks[(size_t) bassRow].soloed = true;
                song.notify (Song::Change::trackState);
                audio.syncNow();
                audio.setPlayheadTicks (4 * song.ticksPerBar());
                audio.togglePlay();
                juce::AudioBuffer<float> buffer (2, 512);
                float loudest = 0.0f;

                for (int b = 0; b < 48000 * 2 / 512; ++b)
                {
                    buffer.clear();
                    audio.processHostedBlock (buffer);

                    if (b % 8 == 0)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

                    loudest = std::max (loudest, buffer.getMagnitude (0, 512));
                }

                audio.stop();
                song.tracks[(size_t) bassRow].soloed = false;
                song.notify (Song::Change::trackState);
                check (loudest > 0.01f, "and it's heard: the 808's part on the synth peaks at " + juce::String (juce::Decibels::gainToDecibels (loudest), 1) + " dB");
            }

            song.undo();
            check (song.findChannel (bass) != nullptr && song.findChannel (bass)->type == InstrumentType::sampler && song.findChannel (bass)->name == "808",
                   "undo puts the 808 back");

            // An empty track takes whatever lands on it
            song.checkpoint();
            const int emptyRow = song.insertTrack ({});
            song.notify (Song::Change::playlist);
            dropAt (synthSound, 800, rowY (emptyRow));
            check (song.tracks[(size_t) emptyRow].channelId == song.channels.back().id && song.numTracks() == emptyRow + 1,
                   "a sound dropped on an empty track goes on that track");

            // A channel from Steps: a track for it, no new channel
            const int kick = song.channels.front().id;
            const int channelsHere = (int) song.channels.size();
            dropAt (juce::String (channelDragPrefix) + juce::String (kick), 800, rowY (song.numTracks()));
            check ((int) song.channels.size() == channelsHere && song.tracks.back().channelId == kick,
                   "Kick dragged from Steps under the last track gets a track of its own");

            // An effect on a track goes on its channel
            bassRow = rowOfPattern (patternNamed ("808"));
            const auto effectsBefore = song.findChannel (bass)->effects.size();
            dropAt (juce::String (soundDragPrefix) + "effect:" + stockfx::descriptions().getReference (0).createIdentifierString(), 800, rowY (bassRow));
            check (song.findChannel (bass)->effects.size() == effectsBefore + 1, "an effect dropped on the 808's track goes on the 808");

            // What a drop would do, as the highlight shows it
            {
                const auto onBass = playlist.dropTargetAt ({ 800, rowY (bassRow) }, synthSound);
                const auto onBeat = playlist.dropTargetAt ({ 800, rowY (rowOfPattern (patternNamed ("Beat"))) }, synthSound);
                const auto below = playlist.dropTargetAt ({ 800, rowY (song.numTracks()) }, synthSound);
                using Kind = Playlist::DropTarget::Kind;
                check (onBass.kind == Kind::replace && onBeat.kind == Kind::newTrack && below.kind == Kind::newTrack && below.row == song.numTracks(),
                       "the drop target says replace on the 808, a new track under Beat, a new one at the end");
            }

            // Patterns dragged from the sidebar: a clip where it's let go, FL's way
            {
                using Kind = Playlist::DropTarget::Kind;
                const int keys = patternNamed ("Keys");
                const auto keysDrag = juce::String (patternDragPrefix) + juce::String (keys);
                const int bar = song.ticksPerBar();
                const auto barX = [] (int b, int px = 10) { return Playlist::headerWidth + 72 * b + px; };   // one bar is 72 px here
                playlist.setSnap (bar);

                check (playlist.isInterestedInDragSource ({ keysDrag, nullptr, {} }), "Arrange takes a pattern dragged from the sidebar");

                const int beatTrack = rowOfPattern (patternNamed ("Beat"));
                const int tracks = song.numTracks();
                const auto clipsBefore = song.playlist;
                const auto clips = song.playlist.size();
                song.currentPatternId = patternNamed ("Beat");
                dropAt (keysDrag, barX (4, 40), rowY (beatTrack));
                const auto& placed = song.playlist.back();
                check (song.playlist.size() == clips + 1 && placed.patternId == keys && placed.track == beatTrack && placed.start == 4 * bar
                         && song.numTracks() == tracks && song.currentPatternId == keys,
                       "Keys dropped on Beat's track in bar 5 lands there, at the start of bar 5, and becomes the current pattern (no new track)");

                // Near the top edge of a track still means that track (only sounds insert a track there)
                dropAt (keysDrag, barX (8), rowY (beatTrack, 0.0f) + 2);
                check (song.playlist.back().track == beatTrack && song.playlist.back().start == 8 * bar && song.numTracks() == tracks,
                       "dropped right at the top of Beat's track it still goes on Beat's track");

                // A finer snap places it off the bar
                playlist.setSnap (song.ticksPerMeterBeat());
                dropAt (keysDrag, barX (2, 72 / 2 + 3), rowY (beatTrack));
                check (song.playlist.back().start == 2 * bar + 2 * song.ticksPerMeterBeat(),
                       "with the snap on beats it lands on the beat under the mouse (bar 3 beat 3)");
                playlist.setSnap (bar);

                // Under the last track: a new track with it on, gone again with one undo
                dropAt (keysDrag, barX (1), rowY (song.numTracks()));
                const bool newTrack = song.numTracks() == tracks + 1 && song.playlist.back().track == tracks && song.playlist.back().patternId == keys
                                        && song.tracks.back().channelId == 0;
                const auto clipsNow = song.playlist.size();
                song.undo();
                check (newTrack && song.numTracks() == tracks && song.playlist.size() == clipsNow - 1,
                       "dropped under the last track it gets a new track of its own, and one undo takes both away");

                // Over the headers it starts at the left of the view; on an audio track, a new track just under it
                int guitarAt = -1;

                for (int row = 0; row < song.numTracks(); ++row)
                    if (song.tracks[(size_t) row].channelId == guitar)
                        guitarAt = row;

                const auto overHeader = playlist.dropTargetAt ({ 60, rowY (beatTrack) }, keysDrag);
                const auto onGuitar = playlist.dropTargetAt ({ barX (3), rowY (guitarAt) }, keysDrag);
                check (overHeader.kind == Kind::clip && overHeader.row == beatTrack && overHeader.tick == 0
                         && guitarAt >= 0 && onGuitar.kind == Kind::newTrack && onGuitar.row == guitarAt + 1 && onGuitar.tick == 3 * bar,
                       "over the track names it goes in at the start; on the Guitar (audio) track it asks for a new track just under");

                check (playlist.dropTargetAt ({ barX (3), rowY (beatTrack) }, juce::String (patternDragPrefix) + "999999").kind == Kind::none,
                       "a pattern that's gone (deleted mid-drag) drops nowhere");

                // Take this test's clips away again, for the checks below
                song.playlist = clipsBefore;
                song.notify (Song::Change::playlist);
            }

            // Painting on an instrument's track, with Beat (drums) the current pattern: a pattern of its own
            song.currentPatternId = patternNamed ("Beat");
            int padRow = -1;

            for (int row = 0; row < song.numTracks(); ++row)
                if (song.tracks[(size_t) row].channelId == pad)
                    padRow = row;

            const auto patternsBefore = song.patterns.size();
            playlist.setTool (Playlist::Tool::draw);
            playlist.setSnap (song.ticksPerBar());
            const float barX = (float) Playlist::headerWidth + 72.0f;     // bar 2
            mouse ({ barX + 5.0f, (float) rowY (padRow) }, 0);
            mouse ({ barX + 5.0f, (float) rowY (padRow) }, 2);
            const auto* painted = song.findPattern (song.currentPatternId);
            check (padRow >= 0 && song.patterns.size() == patternsBefore + 1 && painted != nullptr && painted->name == song.findChannel (pad)->name
                     && song.playlist.back().track == padRow && song.selectedChannelId == pad,
                   "the draw tool on the synth's track paints a new pattern of its own (" + (painted != nullptr ? painted->name : juce::String()) + "), not Beat");

            song.currentPatternId = patternNamed ("Beat");
            mouse ({ barX + 72.0f * 3.0f + 5.0f, (float) rowY (padRow) }, 0);
            mouse ({ barX + 72.0f * 3.0f + 5.0f, (float) rowY (padRow) }, 2);
            check (song.patterns.size() == patternsBefore + 1 && song.playlist.back().patternId == painted->id,
                   "further along the same track it paints that pattern again");

            // A clip dragged below the last track gets a track; back up, the track goes again
            const int tracksNow = song.numTracks();
            const auto clipIndex = song.playlist.size() - 1;
            const float clipX = barX + 72.0f * 3.0f + 20.0f;
            mouse ({ clipX, (float) rowY (padRow) }, 0);
            mouse ({ clipX, (float) rowY (tracksNow) }, 1);
            const bool madeOne = song.numTracks() == tracksNow + 1 && song.playlist[clipIndex].track == tracksNow;
            mouse ({ clipX, (float) rowY (tracksNow + 1) }, 1);
            const bool stillOne = song.numTracks() == tracksNow + 1 && song.playlist[clipIndex].track == tracksNow;
            mouse ({ clipX, (float) rowY (padRow) }, 1);
            mouse ({ clipX, (float) rowY (padRow) }, 2);
            check (madeOne && stillOne && song.numTracks() == tracksNow && song.playlist[clipIndex].track == padRow,
                   "a clip dragged under the last track makes a track (just one), and dragged back the track goes");

            // A channel's new clips go on its track, or a new one under it when that's busy
            const int free = song.trackForChannel (pad, 8 * song.ticksPerBar(), 9 * song.ticksPerBar());
            const int busy = song.trackForChannel (pad, song.ticksPerBar(), 2 * song.ticksPerBar());
            check (free == padRow && busy == padRow + 1 && song.tracks[(size_t) busy].channelId == pad,
                   "a channel's new clip lands on its track, or a new track for it just under when that's taken");

            // Deleting a channel takes its empty tracks with it
            const int tracksBeforeRemove = song.numTracks();
            song.removeChannel (guitar);
            check (song.numTracks() == tracksBeforeRemove - 1, "deleting the Guitar channel takes its (empty) track too");

            // Icons from what a channel is
            {
                Channel c;
                auto iconOf = [&c] (InstrumentType type, const juce::String& name) { c = {}; c.type = type; c.name = name; return trackicons::forChannel (c); };
                c.type = InstrumentType::instrument;
                c.instrument = "VSCO/Keys/Harpsichord.sfz";
                const auto harpsichord = trackicons::forChannel (c);
                c.instrument = "VSCO/Strings/CelloEnsSus.sfz";
                const auto cello = trackicons::forChannel (c);
                check (iconOf (InstrumentType::audio, "Bass Guitar") == "bass" && iconOf (InstrumentType::audio, "Lead Vox") == "vocals"
                         && iconOf (InstrumentType::audio, "Audio 3") == "audio" && iconOf (InstrumentType::kit, "Drums") == "drums"
                         && iconOf (InstrumentType::plugin, "Surge XT") == "synth" && iconOf (InstrumentType::plugin, "Dexed") == "keys"
                         && harpsichord == "keys" && cello == "strings",
                       "icons: Bass Guitar bass, Lead Vox vocals, a drum kit drums, Surge a synth, Dexed keys, a harpsichord keys, cellos strings");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--closetest"))
        {
            // Closing with unsaved changes: Save writes the project and then carries on (closes);
            // Don't save carries on without writing; Cancel does neither. The buttons are pressed
            // the way clicking them does, by ending the dialog with their number
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-closetest.nsproj");
            Song song;
            song.loadDemo();
            song.save (file);

            AudioEngine audio (song, false);
            MainComponent main (song, audio);
            main.setSize (1600, 940);
            main.openProjectFile (file);

            auto pump = [] { for (int i = 0; i < 10; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (5); };
            auto answer = [&] (int button, bool& carriedOn)
            {
                carriedOn = false;
                song.bpm += 1.0;
                song.notify (Song::Change::transport);
                main.confirmDiscard ([&carriedOn] { carriedOn = true; });
                pump();
                // The question it asks (whatever else may be up)
                juce::AlertWindow* dialog = nullptr;
                auto* modal = juce::ModalComponentManager::getInstance();

                for (int i = 0; i < modal->getNumModalComponents(); ++i)
                {
                    auto* c = modal->getModalComponent (i);
                    if (auto* w = dynamic_cast<juce::AlertWindow*> (c); w != nullptr && w->getName() == "Save changes?")
                        dialog = w;
                }

                // Counted before answering: answered, it deletes itself
                const int buttons = dialog != nullptr ? dialog->getNumButtons() : 0;

                if (dialog != nullptr)
                    dialog->exitModalState (button);

                pump();
                return buttons;
            };

            bool carriedOn = false;
            const int buttons = answer (0, carriedOn);
            check (buttons == 4 && ! carriedOn && song.hasUnsavedChanges(), "the dialog has four buttons, and Cancel stays put with the changes ("
                     + juce::String (buttons) + " buttons, carried on " + juce::String ((int) carriedOn) + ", unsaved " + juce::String ((int) song.hasUnsavedChanges()) + ")");

            answer (3, carriedOn);
            Song onDisk;
            onDisk.load (file);
            check (carriedOn && onDisk.bpm == 88.0, "Don't save closes without writing (the file still says " + juce::String (onDisk.bpm) + " bpm)");

            answer (1, carriedOn);
            onDisk.load (file);
            check (carriedOn && onDisk.bpm == song.bpm && ! song.hasUnsavedChanges(),
                   "Save writes the project (" + juce::String (onDisk.bpm) + " bpm), then closes");

            file.deleteFile();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        // What the offline drummer writes for a request, as a drum grid to read (--offlinedrummer
        // "metalcore breakdown at 150" [--seed 7])
        if (args.has ("--offlinedrummer"))
        {
            Song song;
            song.loadBlank();
            const auto prompt = args.valueAfter ("--offlinedrummer").unquoted();
            const auto seed = args.has ("--seed") ? args.valueAfter ("--seed").getLargeIntValue() : (juce::int64) 1;
            const auto r = drummer::offline::write (prompt, song, nullptr, { "Crocell", "Muldjord", "DRS", "DRS Brushes", "Red Zeppelin", "Black Pearl",
                                                                             "Blonde Bop", "Blonde Bop Hot Rods", "Buskman's Holiday", "Nightshift 808", "Nightshift 909" },
                                                    true, {}, true, seed);
            std::cout << r.groove.title << "  (kit " << r.groove.kit << ", swing " << r.groove.swing << (r.groove.swingSixteenths ? " on 16ths" : "")
                      << ", loose " << r.groove.looseMs << " ms)" << std::endl << r.reply << std::endl;

            for (auto& s : r.groove.sections)
            {
                std::cout << std::endl << "== " << s.name << " (" << s.bars.size() << " bars)" << std::endl;

                for (size_t b = 0; b < std::min<size_t> (s.bars.size(), 8); ++b)
                {
                    std::cout << "  bar " << b + 1 << " (" << s.bars[b].steps << " steps)" << std::endl;

                    for (auto& l : s.bars[b].lanes)
                        std::cout << "    " << l.voice.paddedRight (' ', 11) << l.hits << std::endl;
                }
            }

            return 0;
        }

        // What the offline bassist writes, with the drummer it plays to (--offlinebass "funk in E"
        // [--drums "funk"] [--seed 7]): each bar's chord, the kick, then the notes, step:pitch/length
        if (args.has ("--offlinebass"))
        {
            Song song;
            song.loadBlank();
            const auto prompt = args.valueAfter ("--offlinebass").unquoted();
            const auto seed = args.has ("--seed") ? args.valueAfter ("--seed").getLargeIntValue() : (juce::int64) 1;
            std::optional<drummer::Groove> drums;

            if (args.has ("--drums"))
                drums = drummer::offline::write (args.valueAfter ("--drums").unquoted(), song, nullptr, {}, false, {}, true, seed).groove;

            const auto r = bass::offline::write (prompt, song, nullptr, drums.has_value() ? &*drums : nullptr, true, true, seed);
            auto name = [] (int p) { static const char* n[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }; return juce::String (n[p % 12]) + juce::String (p / 12 - 1); };

            std::cout << r.part.title << "  (sound " << r.part.sound << ", swing " << r.part.swing << ", push " << r.part.pushMs << " ms, loose " << r.part.looseMs << " ms)" << std::endl
                      << r.reply << std::endl;

            for (size_t s = 0; s < r.part.sections.size(); ++s)
            {
                const auto& sec = r.part.sections[s];
                std::cout << std::endl << "== " << sec.name << " (" << sec.bars.size() << " bars)" << std::endl;

                for (size_t b = 0; b < std::min<size_t> (sec.bars.size(), 8); ++b)
                {
                    const auto& bar = sec.bars[b];
                    juce::String kick;

                    if (s < r.drums.sections.size() && b < r.drums.sections[s].bars.size())
                        for (auto& l : r.drums.sections[s].bars[b].lanes)
                            if (l.voice == "kick")
                                kick = l.hits;

                    juce::String line;

                    for (auto& n : bar.notes)
                        line << n.step << ":" << name (n.pitch) << "/" << n.length << (n.slide ? "~" : "") << (n.ghost ? "x" : "") << " ";

                    std::cout << "  bar " << b + 1 << " " << bar.chord.paddedRight (' ', 6) << " kick " << kick.paddedRight ('.', bar.steps) << std::endl
                              << "        " << line << std::endl;
                }
            }

            return 0;
        }

        // What the offline guitarist writes, as tab (--offlineguitar "metalcore with a breakdown"
        // [--seed 7]): each bar's chord and kick, then the strings high to low, a fret per step
        // (P under a palm-muted step, x a scratch, h a hammer-on, / a slide into it)
        if (args.has ("--offlineguitar"))
        {
            Song song;
            song.loadBlank();
            const auto prompt = args.valueAfter ("--offlineguitar").unquoted();
            const auto seed = args.has ("--seed") ? args.valueAfter ("--seed").getLargeIntValue() : (juce::int64) 1;
            const auto r = guitar::offline::write (prompt, song, nullptr, nullptr, nullptr, true, true, seed);

            std::cout << r.part.title << "  (sound " << r.part.sound << ", " << guitar::tuningName (r.part.tuning) << ": " << guitar::tuningNotes (r.part.tuning)
                      << ", strum " << r.part.strumMs << " ms, swing " << r.part.swing << ")" << std::endl << r.reply << std::endl;

            for (size_t s = 0; s < r.part.sections.size(); ++s)
            {
                const auto& sec = r.part.sections[s];
                std::cout << std::endl << "== " << sec.name << " (" << sec.bars.size() << " bars)" << std::endl;

                for (size_t b = 0; b < std::min<size_t> (sec.bars.size(), 4); ++b)
                {
                    const auto& bar = sec.bars[b];
                    const int cells = bar.steps;
                    juce::String kick;

                    if (s < r.drums.sections.size() && b < r.drums.sections[s].bars.size())
                        for (auto& l : r.drums.sections[s].bars[b].lanes)
                            if (l.voice == "kick")
                                kick = l.hits;

                    std::cout << "  bar " << b + 1 << " " << bar.chord << std::endl;
                    std::vector<juce::String> lines (r.part.tuning.size(), juce::String::repeatedString ("--- ", cells));
                    juce::String marks = juce::String::repeatedString ("    ", cells);

                    for (auto& n : bar.notes)
                        if (n.string >= 0 && n.string < (int) lines.size())
                        {
                            auto cell = (n.muted ? juce::String ("x") : juce::String (n.fret)) + (n.hammer ? "h" : n.slide ? "/" : "");
                            auto& line = lines[(size_t) n.string];
                            line = line.substring (0, n.step * 4) + cell.paddedRight ('-', 3) + " " + line.substring (n.step * 4 + 4);

                            if (n.palmMute)
                                marks = marks.substring (0, n.step * 4) + "P   " + marks.substring (n.step * 4 + 4);
                        }

                    std::cout << "        " << marks << std::endl;

                    for (int str = (int) lines.size() - 1; str >= 0; --str)
                        std::cout << "     " << juce::String (guitar::tuningNotes ({ r.part.tuning[(size_t) str] })).paddedRight (' ', 2) << "|" << lines[(size_t) str] << std::endl;

                    juce::String k;

                    for (int i = 0; i < kick.length() && i < cells; ++i)
                        k << (kick[i] != '.' ? "K   " : "    ");

                    std::cout << "  kick  " << k << std::endl;
                }
            }

            return 0;
        }

        // What the offline keys player writes (--offlinekeys "lofi" [--seed 7]): each bar's chord, then
        // the notes of each hand by step, a chord's notes together
        if (args.has ("--offlinekeys"))
        {
            Song song;
            song.loadBlank();
            const auto prompt = args.valueAfter ("--offlinekeys").unquoted();
            const auto seed = args.has ("--seed") ? args.valueAfter ("--seed").getLargeIntValue() : (juce::int64) 1;
            const auto r = keys::offline::write (prompt, song, nullptr, nullptr, nullptr, true, true, seed);

            std::cout << r.part.title << "  (sound " << r.part.sound << (r.part.sampled.isNotEmpty() ? " (sampled)" : "") << ", roll " << r.part.rollMs
                      << " ms, swing " << r.part.swing << ")" << std::endl << r.reply << std::endl;

            for (auto& sec : r.part.sections)
            {
                std::cout << std::endl << "== " << sec.name << " (" << sec.bars.size() << " bars)" << std::endl;

                for (size_t b = 0; b < std::min<size_t> (sec.bars.size(), 4); ++b)
                {
                    const auto& bar = sec.bars[b];
                    std::map<int, juce::StringArray> left, right;

                    for (auto& n : bar.notes)
                        (n.left ? left : right)[n.step].add (keys::noteName (n.pitch) + (n.grace ? "~" : "") + "/" + juce::String (n.length));

                    auto line = [] (const std::map<int, juce::StringArray>& hand)
                    {
                        juce::String s;

                        for (auto& [step, names] : hand)
                            s << step << ":" << names.joinIntoString (" ") << "  ";

                        return s;
                    };

                    std::cout << "  bar " << b + 1 << " " << bar.chord << std::endl
                              << "     R  " << line (right) << std::endl
                              << "     L  " << line (left) << std::endl;
                }
            }

            return 0;
        }

        if (args.has ("--offlinesynth"))
        {
            // What the offline synth player plays for a request: each bar's chord and its notes
            Song song;
            song.loadBlank();
            const auto prompt = args.valueAfter ("--offlinesynth").unquoted();
            const auto seed = args.has ("--seed") ? args.valueAfter ("--seed").getLargeIntValue() : (juce::int64) 1;
            const auto r = synth::offline::write (prompt, song, nullptr, {}, {}, nullptr, nullptr, nullptr, true, true, seed);

            std::cout << r.part.title << "  (" << r.part.role << " on " << r.part.sound << ", level " << r.part.levelDb << " dB, push "
                      << r.part.pushMs << " ms, swing " << r.part.swing << ")" << std::endl << r.reply << std::endl;

            for (auto& sec : r.part.sections)
            {
                std::cout << std::endl << "== " << sec.name << " (" << sec.bars.size() << " bars)" << std::endl;

                for (size_t b = 0; b < std::min<size_t> (sec.bars.size(), (size_t) (args.has ("--allbars") ? 64 : 4)); ++b)
                {
                    const auto& bar = sec.bars[b];
                    std::map<int, juce::StringArray> at;

                    for (auto& n : bar.notes)
                        at[n.step].add ((n.slide ? "~" : "") + keys::noteName (n.pitch) + "/" + juce::String (n.length));

                    juce::String line;

                    for (auto& [step, names] : at)
                        line << step << ":" << names.joinIntoString (" ") << "  ";

                    std::cout << "  bar " << b + 1 << " " << bar.chord << "   " << line << std::endl;
                }
            }

            return 0;
        }

        if (args.has ("--offlinedrummertest"))
        {
            // The offline drummer: what it understands from a request, and that each style's
            // drummer plays like one - metalcore's half-time breakdown with the china, blasts,
            // trap's clap on 3 and its hat rolls, drill's triplet hats, jazz's spang-a-lang and
            // hats on 2 and 4, the late, swung pocket of lofi and neo-soul, djent's cycle across
            // the bar line, pop-punk's skate beat, rock and boom bap's backbeat. Then: every style
            // writes valid lanes, reads back from its JSON, is the same for the same seed, is
            // quick, picks its kit and tempo, and a follow-up builds on what was there.
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            using agents::Request;
            const juce::StringArray kitNames { "Crocell", "Muldjord", "DRS", "DRS Brushes", "Red Zeppelin", "Black Pearl",
                                               "Blonde Bop", "Blonde Bop Hot Rods", "Buskman's Holiday", "Nightshift 808", "Nightshift 909" };
            Song song;
            song.loadBlank();

            auto write = [&] (const juce::String& prompt, juce::int64 seed, const Request* previous = nullptr, bool blank = true)
            {
                return drummer::offline::write (prompt, song, previous, kitNames, true, {}, blank, seed);
            };

            auto lane = [] (const drummer::Bar& b, const juce::String& voice)
            {
                for (auto& l : b.lanes)
                    if (l.voice == voice)
                        return l.hits;

                return juce::String();
            };

            auto section = [] (const drummer::Groove& g, const juce::String& name) -> const drummer::Section*
            {
                for (auto& s : g.sections)
                    if (s.name == name && ! s.bars.empty())
                        return &s;

                return nullptr;
            };

            auto at = [] (const juce::String& hits, std::initializer_list<int> steps)
            {
                for (auto s : steps)
                    if (s >= hits.length() || hits[s] == '.')
                        return false;

                return true;
            };

            auto notAt = [] (const juce::String& hits, std::initializer_list<int> steps)
            {
                for (auto s : steps)
                    if (s < hits.length() && hits[s] != '.')
                        return false;

                return true;
            };

            auto count = [] (const juce::String& hits)
            {
                int n = 0;

                for (int i = 0; i < hits.length(); ++i)
                    n += hits[i] != '.' ? 1 : 0;

                return n;
            };

            auto names = [] (const drummer::Groove& g)
            {
                juce::StringArray n;

                for (auto& s : g.sections)
                    n.add (s.name);

                return n.joinIntoString (", ");
            };

            // What it understands
            {
                auto r = Request::parse ("Metalcore breakdown at 150 bpm");
                check (r.style == "metalcore" && r.tempo == 150 && r.sections == juce::StringArray { "breakdown" } && ! r.addToForm,
                       "\"Metalcore breakdown at 150 bpm\": metalcore, 150, just a breakdown");
                r = Request::parse ("metalcore with a breakdown");
                check (r.style == "metalcore" && r.sections == juce::StringArray { "breakdown" } && r.addToForm,
                       "\"metalcore with a breakdown\": the usual song with a breakdown in it");
                r = Request::parse ("intro, verse and chorus, pop punk");
                check (r.style == "poppunk" && r.sections == juce::StringArray { "intro", "verse", "chorus" } && ! r.addToForm,
                       "\"intro, verse and chorus, pop punk\": that song, as listed, pop-punk over punk");
                r = Request::parse ("deathcore blast beats");
                check (r.style == "deathcore" && r.blasts, "\"deathcore blast beats\": deathcore, with blasts (a technique, not a style)");
                r = Request::parse ("blast beats");
                check (r.style == "blast", "\"blast beats\" with nothing playing: death metal");
                r = Request::parse ("Lofi jazz on brushes, lazy swing");
                check (r.style == "lofi" && r.brushes && r.swing > 0.6f && r.sections.isEmpty(),
                       "\"Lofi jazz on brushes, lazy swing\": lofi (named first), brushes, a heavy swing, no sections");
                r = Request::parse ("metalcore in 7/8, 6 bars");
                check (r.meterBeats == 7 && r.meterUnit == 8 && r.bars == 6, "\"in 7/8, 6 bars\": the meter and the bars");

                auto previous = Request::parse ("pop punk");
                previous.sections = { "intro", "verse", "chorus" };
                r = Request::parse ("add a bridge", &previous);
                check (r.style == "poppunk" && r.sections == juce::StringArray { "intro", "verse", "chorus", "bridge" },
                       "a follow-up \"add a bridge\" keeps pop-punk and adds the bridge (" + r.sections.joinIntoString (", ") + ")");
                r = Request::parse ("busier hats, half time", &previous);
                check (r.style == "poppunk" && r.busyHats > 0.0f && r.halfTime, "\"busier hats, half time\" changes what's there");
                r = Request::parse ("trap", &previous);
                check (r.style == "trap" && ! r.halfTime && r.sections.isEmpty(), "naming another style starts afresh");
            }

            // Metalcore: the usual song with a breakdown, the snare half-time on 3, china on the hits
            {
                bool form = true, breakdown = true;

                for (juce::int64 seed = 1; seed <= 6; ++seed)
                {
                    const auto r = write ("metalcore with a breakdown", seed);
                    form = form && names (r.groove) == "Intro, Verse, Chorus, Breakdown";

                    if (auto* s = section (r.groove, "Breakdown"))
                    {
                        const auto& b = s->bars.front();
                        breakdown = breakdown && b.steps == 16 && at (lane (b, "snare"), { 8 }) && notAt (lane (b, "snare"), { 4, 12 })
                                      && count (lane (b, "china")) >= 2 && count (lane (b, "kick")) >= 3;
                    }
                    else
                        breakdown = false;
                }

                check (form, "metalcore with a breakdown: intro, verse, chorus, breakdown");
                check (breakdown, "its breakdown: the snare on 3 only, the china and the kick on the chug (6 takes)");
            }

            // Blasts: everything on the 8ths or the 16ths
            {
                bool blasts = true;

                for (juce::int64 seed = 1; seed <= 6; ++seed)
                {
                    const auto r = write ("deathcore blast beats", seed);
                    bool any = false;

                    for (auto& s : r.groove.sections)
                        for (auto& b : s.bars)
                            any = any || (b.steps == 16 && count (lane (b, "kick")) >= 8 && count (lane (b, "snare")) >= 8);

                    blasts = blasts && r.groove.title.startsWith ("Deathcore") && any;
                }

                check (blasts, "deathcore blast beats: deathcore, with bars of kick and snare on every 8th or more");
            }

            // Trap: half-time clap on 3, the hats rolling in triplets or 32nds
            {
                bool clap = true, roll = true;

                for (juce::int64 seed = 1; seed <= 6; ++seed)
                {
                    const auto r = write ("trap at 140", seed);

                    if (auto* s = section (r.groove, "Hook"))
                        clap = clap && at (lane (s->bars.front(), "clap"), { 8 }) && notAt (lane (s->bars.front(), "clap"), { 4, 12 });
                    else
                        clap = false;

                    bool rolled = false;

                    for (auto& sec : r.groove.sections)
                        for (auto& b : sec.bars)
                            rolled = rolled || ((b.steps == 32 || b.steps == 48) && count (lane (b, "hat_closed")) > b.steps / 3);

                    roll = roll && rolled && r.groove.tempo == 140;
                }

                check (clap, "trap: the clap on 3, not 2 and 4 (half-time)");
                check (roll, "trap: a hat roll in 16th triplets or 32nds, at the 140 asked for");
            }

            // Drill: triplet hats, the snare on 3
            {
                bool drill = true;

                for (juce::int64 seed = 1; seed <= 6; ++seed)
                {
                    const auto r = write ("drill", seed);
                    auto* s = section (r.groove, "Hook");
                    drill = drill && s != nullptr && s->bars.front().steps == 48 && count (lane (s->bars.front(), "hat_closed")) >= 8
                              && at (lane (s->bars.front(), "snare"), { 24 }) && r.groove.tempo == 142;
                }

                check (drill, "drill: triplet hats on a 48-step bar, the snare on 3, at 142");
            }

            // Jazz: ding, ding-a ding, ding-a on the ride, the hats on 2 and 4
            {
                bool ride = true, hats = true;

                for (juce::int64 seed = 1; seed <= 6; ++seed)
                {
                    const auto r = write ("jazz", seed);
                    auto* s = section (r.groove, "Head");

                    if (s == nullptr || s->bars.front().steps != 12)
                    {
                        ride = hats = false;
                        continue;
                    }

                    const auto& b = s->bars.front();
                    ride = ride && at (lane (b, "ride"), { 0, 3, 5, 6, 9, 11 }) && notAt (lane (b, "ride"), { 1, 2, 4, 7, 8, 10 });
                    hats = hats && at (lane (b, "hat_pedal"), { 3, 9 }) && notAt (lane (b, "hat_pedal"), { 0, 6 });
                }

                check (ride, "jazz: the ride's skip notes after 2 and 4, into 3 and 1 (12-step triplet bars)");
                check (hats, "jazz: the hat pedal on 2 and 4");
            }

            // Lofi and neo-soul: swung 16ths, the snare late
            {
                const auto lofi = write ("lofi", 1);
                const auto soul = write ("neo soul", 1);
                auto* verse = section (lofi.groove, "Verse");
                check (lofi.groove.swing >= 0.5f && lofi.groove.swingSixteenths && lofi.groove.pushMs.count ("snare") == 1 && lofi.groove.pushMs.at ("snare") > 0.0f
                         && verse != nullptr && at (lane (verse->bars.front(), "snare"), { 4, 12 }),
                       "lofi: swung 16ths, the snare behind the beat on 2 and 4");
                check (soul.groove.swing >= 0.5f && soul.groove.pushMs.count ("snare") == 1 && soul.groove.pushMs.count ("kick") == 1
                         && soul.groove.pushMs.at ("snare") > soul.groove.pushMs.at ("kick"),
                       "neo-soul: swung, the kick early and the snare late");
            }

            // Djent: a cycle that crosses the bar line, so the bars differ
            {
                bool crosses = true;

                for (juce::int64 seed = 1; seed <= 6; ++seed)
                {
                    const auto r = write ("djent", seed);
                    auto* s = section (r.groove, "Verse");
                    crosses = crosses && s != nullptr && s->bars.size() >= 2 && lane (s->bars[0], "kick") != lane (s->bars[1], "kick");
                }

                check (crosses, "djent: the verse's kick cycle crosses the bar line (bar 2 differs from bar 1)");
            }

            // Pop-punk's skate beat, rock's and boom bap's backbeat
            {
                const auto punk = write ("pop punk", 1);
                auto* verse = section (punk.groove, "Verse");
                check (verse != nullptr && at (lane (verse->bars.front(), "kick"), { 0, 4, 8, 12 }) && at (lane (verse->bars.front(), "snare"), { 2, 6, 10, 14 }),
                       "pop-punk: the skate beat, kick on the beats and snare on the offbeats");

                const auto rock = write ("rock", 1);
                verse = section (rock.groove, "Verse");
                check (verse != nullptr && at (lane (verse->bars.front(), "snare"), { 4, 12 }) && at (lane (verse->bars.front(), "kick"), { 0 }),
                       "rock: kick on 1, snare on 2 and 4");

                const auto bap = write ("boom bap", 1);
                verse = section (bap.groove, "Verse");
                check (bap.groove.swing > 0.5f && bap.groove.swingSixteenths && verse != nullptr && at (lane (verse->bars.front(), "snare"), { 4, 12 }),
                       "boom bap: swung 16ths, snare on 2 and 4");
            }

            // Funk: on the one, the backbeat with ghosts round it, 16ths on the hats
            {
                bool funk = true;

                for (juce::int64 seed = 1; seed <= 6; ++seed)
                {
                    const auto r = write ("funk", seed);
                    auto* verse = section (r.groove, "Verse");

                    if (verse == nullptr)
                    {
                        funk = false;
                        continue;
                    }

                    const auto& b = verse->bars.front();
                    funk = funk && r.groove.tempo == 102 && at (lane (b, "kick"), { 0 }) && lane (b, "kick")[0] == '9' && notAt (lane (b, "kick"), { 4, 12 })
                             && at (lane (b, "snare"), { 4, 12 }) && lane (b, "snare").containsChar ('g')
                             && count (lane (b, "hat_closed")) + count (lane (b, "hat_open")) >= 12;
                }

                check (funk, "funk: the kick hard on the one and never on 2 or 4, the snare on 2 and 4 with ghost notes, 16ths on the hats, at 102");

                const auto disco = write ("disco funk chorus", 2);
                auto* chorus = section (disco.groove, "Chorus");
                check (disco.groove.title.startsWith ("Funk") && chorus != nullptr, "\"disco funk chorus\": funk, a chorus");
            }

            // Odd meters: 7/8 bars
            {
                const auto r = write ("metalcore in 7/8", 1);
                bool sevens = r.groove.meterBeats == 7 && r.groove.meterUnit == 8;

                for (auto& s : r.groove.sections)
                    for (auto& b : s.bars)
                        sevens = sevens && b.steps % 7 == 0;

                check (sevens, "metalcore in 7/8: every bar in 7/8 (steps a multiple of 7)");
            }

            // Every style: valid lanes, back from its JSON, the same for the same seed, quickly
            {
                const auto& voices = drumkits::voiceNames();
                bool valid = true, roundTrip = true, same = true, picked = true;
                juce::StringArray bad;
                double slowest = 0.0, total = 0.0;

                for (auto& style : agents::styles())
                {
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    const auto r = write (style.words[0], 3);
                    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                    slowest = std::max (slowest, ms);
                    total += ms;

                    picked = picked && r.request.style == style.id && r.groove.kit.isNotEmpty() && kitNames.contains (r.groove.kit);

                    for (auto& s : r.groove.sections)
                        for (auto& b : s.bars)
                            for (auto& l : b.lanes)
                                if (! voices.contains (l.voice) || l.hits.length() != b.steps || ! l.hits.containsOnly (".123456789gfr"))
                                {
                                    valid = false;
                                    bad.addIfNotAlreadyThere (style.id);
                                }

                    valid = valid && ! r.groove.sections.empty() && r.groove.arrangement.size() == (int) r.groove.sections.size();

                    const auto json = drummer::offline::toJson (r.groove);
                    const auto back = drummer::Groove::fromJson (json);
                    roundTrip = roundTrip && back.has_value() && juce::JSON::toString (drummer::offline::toJson (*back)) == juce::JSON::toString (json);
                    same = same && juce::JSON::toString (drummer::offline::toJson (write (style.words[0], 3).groove)) == juce::JSON::toString (json);
                }

                check (valid, "all " + juce::String ((int) agents::styles().size()) + " styles write lanes of real drums, a hit a step"
                                + (bad.isEmpty() ? juce::String() : " (not: " + bad.joinIntoString (", ") + ")"));
                check (picked, "each understands its own name and picks one of the kits");
                check (roundTrip, "each reads back from its JSON as the same part");
                check (same, "the same seed writes the same part");
                check (slowest < 250.0, "quick: " + juce::String (total, 1) + " ms for all of them, the slowest " + juce::String (slowest, 1) + " ms");

                juce::StringArray takes;

                for (juce::int64 seed = 1; seed <= 4; ++seed)
                    takes.addIfNotAlreadyThere (juce::JSON::toString (drummer::offline::toJson (write ("metalcore", seed).groove)));

                check (takes.size() >= 3, "another seed, another take (" + juce::String (takes.size()) + " different in 4)");
            }

            // Kit and tempo
            {
                check (write ("lofi jazz on brushes", 1).groove.kit == "DRS Brushes", "brushes: the brushes kit");
                check (write ("trap", 1).groove.kit == "Nightshift 808", "trap: the 808");
                check (drummer::offline::write ("trap", song, nullptr, kitNames, false, "Black Pearl", true, 1).groove.kit == "Black Pearl",
                       "with a kit already chosen it keeps it");
                check (write ("metalcore", 1).groove.tempo == 150, "a blank song takes the style's tempo (metalcore 150)");
                check (write ("rock", 1, nullptr, false).groove.tempo == 0, "a song with other parts keeps its tempo");
                check (write ("metalcore at 172", 1, nullptr, false).groove.tempo == 172, "a tempo asked for is always taken");
            }

            // A follow-up builds on what was there, through the turn it's kept in
            {
                const auto first = write ("pop punk", 1);
                const auto turn = drummer::offline::turnFor ("pop punk", first);
                const auto kept = drummer::offline::requestOf (turn);
                check (kept.has_value() && kept->style == "poppunk" && drummer::Groove::fromJson (juce::JSON::parse (turn.groove)).has_value(),
                       "its turn keeps the part and what it understood");

                if (kept.has_value())
                {
                    const auto next = write ("half time with a breakdown", 2, &*kept);
                    check (next.request.style == "poppunk" && next.request.halfTime && next.request.sections.contains ("breakdown")
                             && section (next.groove, "Breakdown") != nullptr && section (next.groove, "Verse") != nullptr,
                           "\"half time with a breakdown\" after pop-punk: still pop-punk, half-time, a breakdown added (" + names (next.groove) + ")");
                }
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--offlinebasstest"))
        {
            // The offline bassist: what it understands (keys, tunings, how to play), and that each
            // style's bassist plays like one against the drummer it follows - metalcore's breakdown
            // doubling every kick on the root (a b2 at most), trap's 808 only on the kick and not
            // in the intro, drill's slides, jazz walking four to the bar into each change, funk's
            // one with octaves and dead notes, pop-punk's 8ths, neo-soul's slides and push. Then
            // every style: the drummer's sections, valid lines, back from JSON, the same for a
            // seed, quick, its sound; and into patterns, under the drummer's clips in Arrange.
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            using agents::Request;
            Song song;
            song.loadBlank();

            auto drumsFor = [&] (const juce::String& prompt, juce::int64 seed)
            {
                return drummer::offline::write (prompt, song, nullptr, {}, false, {}, true, seed).groove;
            };

            auto write = [&] (const juce::String& prompt, juce::int64 seed, const drummer::Groove* drums = nullptr, const Request* previous = nullptr)
            {
                return bass::offline::write (prompt, song, previous, drums, true, true, seed);
            };

            auto kicksIn = [] (const drummer::Bar& b)
            {
                std::vector<int> k;

                for (auto& l : b.lanes)
                    if (l.voice == "kick")
                        for (int i = 0; i < l.hits.length(); ++i)
                            if (l.hits[i] != '.' && l.hits[i] != 'g')
                                k.push_back (i);

                return k;
            };

            auto section = [] (const bass::Part& p, const juce::String& name) -> const bass::Section*
            {
                for (auto& s : p.sections)
                    if (s.name == name)
                        return &s;

                return nullptr;
            };

            auto drumSection = [] (const drummer::Groove& g, const juce::String& name) -> const drummer::Section*
            {
                for (auto& s : g.sections)
                    if (s.name == name)
                        return &s;

                return nullptr;
            };

            auto pc = [] (int pitch) { return ((pitch % 12) + 12) % 12; };

            // What it understands
            {
                auto r = Request::parse ("funk in A minor, slap the octaves");
                check (r.style == "funk" && r.keyRoot == 9 && r.mode == "minor" && r.slap && r.octaves, "\"funk in A minor, slap the octaves\": funk, A minor, slap, octaves");
                r = Request::parse ("metalcore in drop B, locked to the kick");
                check (r.style == "metalcore" && r.keyRoot == 11 && r.lowest == 23 && r.lockKick, "\"metalcore in drop B, locked to the kick\": B, lowest B0, locked");
                r = Request::parse ("trap in F# minor with slides");
                check (r.keyRoot == 6 && r.mode == "minor" && r.slides, "\"trap in F# minor with slides\": F# minor, slides");
                r = Request::parse ("walking bass, key of Bb");
                check (r.walking && r.keyRoot == 10, "\"walking bass, key of Bb\": walking, Bb");
                r = Request::parse ("lofi in a lazy way");
                check (r.keyRoot == -1 && r.style == "lofi", "\"lofi in a lazy way\": no key (\"in a\" isn't A)");
                r = Request::parse ("i am making a rock song in E");
                check (r.keyRoot == 4 && r.mode.isEmpty(), "\"i am making a rock song in E\": E (\"am\" isn't A minor)");
                r = Request::parse ("drill, C#m, dorian");
                check (r.keyRoot == 1 && r.mode == "dorian", "\"drill, C#m, dorian\": C#, dorian over the minor");
                r = Request::parse ("death metal with double pedal");
                check (! r.pedal, "\"double pedal\" is the drummer's, not a bass pedal tone");
            }

            // Metalcore: the breakdown doubles every kick on the low root, a b2 the only other note
            {
                bool covered = true, roots = true, low = true;

                for (juce::int64 seed = 1; seed <= 6; ++seed)
                {
                    const auto drums = drumsFor ("metalcore with a breakdown", seed);
                    const auto r = write ("metalcore with a breakdown", seed, &drums);
                    auto* bd = drumSection (drums, "Breakdown");
                    auto* bs = section (r.part, "Breakdown");

                    if (bd == nullptr || bs == nullptr || bd->bars.size() != bs->bars.size())
                    {
                        covered = false;
                        continue;
                    }

                    for (size_t b = 0; b < bs->bars.size(); ++b)
                    {
                        for (auto k : kicksIn (bd->bars[b]))
                            covered = covered && std::any_of (bs->bars[b].notes.begin(), bs->bars[b].notes.end(), [k] (const bass::Note& n) { return n.step == k; });

                        for (auto& n : bs->bars[b].notes)
                        {
                            roots = roots && (pc (n.pitch) == 0 || pc (n.pitch) == 1);
                            low = low && n.pitch >= 24 && n.pitch <= 37;
                        }
                    }
                }

                check (covered, "metalcore's breakdown: a note on every kick, in every bar (6 takes)");
                check (roots && low, "and only the low C (drop C) or its b2");
            }

            // Trap and drill: the 808 on the kick, nothing in the intro; drill slides
            {
                bool intro = true, locked = true, moves = true, slides = true;

                for (juce::int64 seed = 1; seed <= 6; ++seed)
                {
                    const auto drums = drumsFor ("trap", seed);
                    const auto r = write ("trap", seed, &drums);

                    if (auto* s = section (r.part, "Intro"))
                        for (auto& b : s->bars)
                            intro = intro && b.notes.empty();

                    bool any = false;

                    if (auto* s = section (r.part, "Hook"); s != nullptr && drumSection (drums, "Hook") != nullptr)
                        for (size_t b = 0; b < s->bars.size(); ++b)
                        {
                            const auto kicks = kicksIn (drumSection (drums, "Hook")->bars[b]);

                            for (auto& n : s->bars[b].notes)
                            {
                                locked = locked && std::find (kicks.begin(), kicks.end(), n.step) != kicks.end();
                                any = any || n.slide || pc (n.pitch) != pc (s->bars[b].notes.front().pitch) || n.pitch >= s->bars[b].notes.front().pitch + 12;
                            }
                        }

                    moves = moves && any;

                    const auto drillDrums = drumsFor ("drill", seed);
                    const auto d = write ("drill", seed, &drillDrums);
                    bool slid = false;

                    if (auto* s = section (d.part, "Hook"))
                        for (auto& b : s->bars)
                            for (auto& n : b.notes)
                                slid = slid || n.slide;

                    slides = slides && slid && d.part.sound == "808 Glide";
                }

                check (intro, "trap: no 808 in the intro (it waits for the hook)");
                check (locked, "trap: every 808 note in the hook lands on a kick");
                check (moves, "trap: the hook's 808 moves somewhere (a slide, a leap, the octave), not one note all the way");
                check (slides, "drill: the 808 slides, on the gliding 808 sound");
            }

            // Jazz: walking four to the bar, the root on the one, a step into each change
            {
                bool walking = true, roots = true, approaches = true;

                for (juce::int64 seed = 1; seed <= 6; ++seed)
                {
                    const auto drums = drumsFor ("jazz", seed);
                    const auto r = write ("jazz", seed, &drums);
                    auto* s = section (r.part, "Head");

                    if (s == nullptr)
                    {
                        walking = false;
                        continue;
                    }

                    for (size_t b = 0; b < s->bars.size(); ++b)
                    {
                        const auto& bar = s->bars[b];
                        std::vector<int> onsets;

                        for (auto& n : bar.notes)
                            if (! n.ghost)
                                onsets.push_back (n.step);

                        walking = walking && bar.steps == 12 && onsets == std::vector<int> { 0, 3, 6, 9 };

                        if (! bar.notes.empty())
                        {
                            // The chord's root from its name ("Bbmaj7" -> 10)
                            int root = juce::String ("C D EF G A B").indexOfChar (bar.chord[0]);
                            root += bar.chord[1] == '#' ? 1 : bar.chord[1] == 'b' ? -1 : 0;
                            roots = roots && pc (bar.notes.front().pitch) == (root + 12) % 12;
                        }

                        if (b + 1 < s->bars.size() && ! bar.notes.empty() && ! s->bars[b + 1].notes.empty())
                        {
                            int last = 0;

                            for (auto& n : bar.notes)
                                if (! n.ghost)
                                    last = n.pitch;

                            approaches = approaches && std::abs (s->bars[b + 1].notes.front().pitch - last) <= 7;
                        }
                    }
                }

                check (walking, "jazz: walking quarters, a note on each beat of the 12-step triplet bar");
                check (roots, "jazz: the root of each chord on the one");
                check (approaches, "jazz: each bar's last note leads into the next bar's first (a fifth at most away)");
            }

            // Funk: the one hard on the root, the octave popped, dead notes
            {
                bool one = true, octaves = true, ghosts = true;

                for (juce::int64 seed = 1; seed <= 6; ++seed)
                {
                    const auto drums = drumsFor ("funk", seed);
                    const auto r = write ("funk in E", seed, &drums);
                    auto* s = section (r.part, "Verse");

                    if (s == nullptr)
                    {
                        one = false;
                        continue;
                    }

                    bool high = false, ghost = false;

                    for (auto& bar : s->bars)
                    {
                        one = one && ! bar.notes.empty() && bar.notes.front().step == 0 && bar.notes.front().velocity >= 120;

                        for (auto& n : bar.notes)
                        {
                            high = high || (! n.ghost && n.pitch >= bar.notes.front().pitch + 12);
                            ghost = ghost || n.ghost;
                        }
                    }

                    octaves = octaves && high;
                    ghosts = ghosts && ghost;
                }

                check (one, "funk: every verse bar starts on the one, hard");
                check (octaves, "funk: the octave popped in every take");
                check (ghosts, "funk: dead notes ghosting in, in every take");
            }

            // Pop-punk's 8ths; neo-soul's slides, ghosts and push
            {
                const auto punkDrums = drumsFor ("pop punk", 1);
                const auto punk = write ("pop punk", 1, &punkDrums);
                bool eighths = true;

                if (auto* s = section (punk.part, "Chorus"))
                    for (auto& bar : s->bars)
                    {
                        int count = 0;

                        for (int t = 0; t < 16; t += 2)
                            count += std::any_of (bar.notes.begin(), bar.notes.end(), [t] (const bass::Note& n) { return n.step == t; }) ? 1 : 0;

                        eighths = eighths && count == 8;
                    }
                else
                    eighths = false;

                check (eighths, "pop-punk: the chorus on every 8th");

                bool soul = true;

                for (juce::int64 seed = 1; seed <= 4; ++seed)
                {
                    const auto drums = drumsFor ("neo soul", seed);
                    const auto r = write ("neo soul", seed, &drums);
                    bool feel = false;

                    for (auto& s : r.part.sections)
                        for (auto& bar : s.bars)
                            for (auto& n : bar.notes)
                                feel = feel || n.slide || n.ghost;

                    soul = soul && feel && r.part.swing > 0.4f;
                }

                check (soul, "neo-soul: slides or dead notes in every take, swung with the drums");
            }

            // The key asked for, and a drop tuning's floor
            {
                const auto drums = drumsFor ("funk", 2);
                const auto inA = write ("funk in A", 2, &drums);
                auto* verse = section (inA.part, "Verse");
                check (verse != nullptr && ! verse->bars.front().notes.empty() && pc (verse->bars.front().notes.front().pitch) == 9 && inA.part.key.startsWith ("A"),
                       "\"funk in A\": the verse starts on an A (" + inA.part.key + ")");

                const auto metal = write ("metalcore in drop B", 2);
                int lowest = 127;

                for (auto& s : metal.part.sections)
                    for (auto& bar : s.bars)
                        for (auto& n : bar.notes)
                            lowest = std::min (lowest, n.pitch);

                check (lowest >= 23 && metal.part.key.contains ("B"), "\"metalcore in drop B\": nothing under the low B (lowest " + juce::String (lowest) + ")");
            }

            // Every style: the drummer's sections and bars, valid lines, back from JSON, the same
            // for a seed, quick, a sound of its own
            {
                bool follows = true, valid = true, roundTrip = true, same = true, sounds = true;
                juce::StringArray bad;
                double slowest = 0.0, total = 0.0;

                for (auto& style : agents::styles())
                {
                    const auto drums = drumsFor (style.words[0], 4);
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    const auto r = write (style.words[0], 4, &drums);
                    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                    slowest = std::max (slowest, ms);
                    total += ms;

                    bool ok = r.part.sections.size() == drums.sections.size();

                    for (size_t s = 0; ok && s < r.part.sections.size(); ++s)
                    {
                        ok = r.part.sections[s].name == drums.sections[s].name && r.part.sections[s].bars.size() == drums.sections[s].bars.size();

                        for (size_t b = 0; ok && b < r.part.sections[s].bars.size(); ++b)
                            ok = r.part.sections[s].bars[b].steps == drums.sections[s].bars[b].steps;
                    }

                    follows = follows && ok;

                    for (auto& s : r.part.sections)
                        for (auto& bar : s.bars)
                        {
                            std::set<int> steps;

                            for (auto& n : bar.notes)
                            {
                                const bool fine = n.pitch >= 21 && n.pitch <= 72 && n.length >= 1 && n.step >= 0 && n.step < bar.steps && steps.insert (n.step).second;

                                if (! fine)
                                    bad.addIfNotAlreadyThere (style.id);

                                valid = valid && fine;
                            }
                        }

                    const auto json = juce::JSON::toString (r.part.toJson());
                    const auto back = bass::Part::fromJson (juce::JSON::parse (json));
                    roundTrip = roundTrip && back.has_value() && juce::JSON::toString (back->toJson()) == json;
                    same = same && juce::JSON::toString (write (style.words[0], 4, &drums).part.toJson()) == json;
                    sounds = sounds && r.part.sound.isNotEmpty() && nova::Patch::fromVar (r.part.patch).has_value();
                }

                check (follows, "every style plays the drummer's sections, bar for bar, step for step");
                check (valid, "every style's notes are in a bass's range, one at a time" + (bad.isEmpty() ? juce::String() : " (not: " + bad.joinIntoString (", ") + ")"));
                check (roundTrip, "each reads back from its JSON as the same part");
                check (same, "the same seed writes the same part");
                check (sounds, "each style has a bass sound of its own (a Nova patch)");
                check (slowest < 250.0, "quick: " + juce::String (total, 1) + " ms for all 19, the slowest " + juce::String (slowest, 1) + " ms");

                juce::StringArray takes;

                for (juce::int64 seed = 1; seed <= 4; ++seed)
                    takes.addIfNotAlreadyThere (juce::JSON::toString (write ("rock", seed).part.toJson()));

                check (takes.size() >= 3, "another seed, another take (" + juce::String (takes.size()) + " different in 4)");
            }

            // With no drummer, it plays to the drums its style would have, and says so
            {
                const auto r = write ("trap", 3);
                check (r.imaginedDrums && r.reply.contains ("No drums yet") && r.part.tempo == 140, "with no drums it plays to a trap drummer's, at trap's 140, and says so");
            }

            // A follow-up builds on what was there
            {
                const auto drums = drumsFor ("funk", 5);
                const auto first = write ("funk", 5, &drums);
                const auto kept = bass::offline::requestOf (bass::offline::turnFor ("funk", first));
                check (kept.has_value() && kept->style == "funk", "its turn keeps what it understood");

                if (kept.has_value())
                {
                    const auto next = write ("busier, in G", 6, &drums, &*kept);
                    check (next.request.style == "funk" && next.request.keyRoot == 7 && next.part.key.startsWith ("G"), "\"busier, in G\" after funk: still funk, now in G");
                }
            }

            // Into patterns, and under the drummer's clips in Arrange
            {
                Song s;
                s.loadBlank();
                const int drumsId = s.addChannel ("Drums", InstrumentType::kit).id;
                const int bassId = s.addChannel ("Bass", InstrumentType::synth).id;
                const auto groove = drumsFor ("funk", 7);
                drummer::render (s, drumsId, groove, [] (const juce::String&) { return 36; });
                drummer::placeInArrange (s, drumsId, groove);

                const auto r = bass::offline::write ("funk", s, nullptr, &groove, true, false, 7);
                bass::render (s, bassId, r.part);
                auto* c = s.findChannel (bassId);
                bool patterns = c != nullptr && c->bass.patterns.size() == r.part.sections.size();
                bool ghostsQuiet = true;

                if (c != nullptr)
                    for (auto& sec : r.part.sections)
                        if (auto it = c->bass.patterns.find (sec.name); it != c->bass.patterns.end())
                        {
                            auto* p = s.findPattern (it->second);
                            int written = 0;

                            for (auto& bar : sec.bars)
                                written += (int) bar.notes.size();

                            patterns = patterns && p != nullptr && p->name == "Bass " + sec.name && p->findNotes (bassId) != nullptr
                                         && (int) p->findNotes (bassId)->size() == written;

                            // One note at a time
                            if (p != nullptr && p->findNotes (bassId) != nullptr)
                            {
                                const auto& ns = *p->findNotes (bassId);

                                for (size_t i = 0; i + 1 < ns.size(); ++i)
                                    patterns = patterns && ns[i].end() <= ns[i + 1].start;

                                for (auto& n : ns)
                                    ghostsQuiet = ghostsQuiet && (n.velocity > 50 || n.length <= ticksPerStep * 3 / 2);
                            }
                        }

                check (patterns, "each section is a pattern named after it (Bass Verse...), every note in it, one note at a time");
                check (ghostsQuiet, "dead notes play quiet and short");

                c->bass.turns.push_back (bass::offline::turnFor ("funk", r));
                c->bass.shown = 0;
                bass::placeInArrange (s, bassId, r.part);
                int matched = 0, drumClips = 0;

                for (auto& clip : s.playlist)
                    for (auto& [name, id] : s.findChannel (drumsId)->drummer.patterns)
                        if (clip.patternId == id)
                        {
                            ++drumClips;
                            const int bassPattern = c->bass.patterns.count (name) > 0 ? c->bass.patterns.at (name) : -1;
                            matched += std::any_of (s.playlist.begin(), s.playlist.end(), [&] (const PlaylistClip& other)
                                                    { return other.patternId == bassPattern && other.start == clip.start && other.track != clip.track; }) ? 1 : 0;
                        }

                check (drumClips > 0 && matched == drumClips, "Put in Arrange puts each section under the drummer's, on a row of its own ("
                                                                  + juce::String (matched) + " of " + juce::String (drumClips) + ")");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--basstest"))
        {
            // The AI bassist from the top of the studio: a drum part first, then Shift+F8 (the AI
            // Bass tab, the bar talking to the bassist), a request making a Bass channel on the
            // style's sound with a pattern for each of the drummer's sections, a follow-up keeping
            // the style, Put in Arrange laying the drums and the bass out together, the bar back
            // to the drummer, the bass saved with the song.
            Song song;
            song.loadDemo();
            AudioEngine audio (song, false);
            audio.syncNow();
            MainComponent main (song, audio);
            main.setSize (1600, 940);
            main.setVisible (true);

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto settle = [] { settleMessages (30); };
            auto& bar = main.getDrummerBar();
            auto& panel = main.getBass();
            using Panel = MainComponent::Panel;

            auto type = [&] (const juce::String& words)
            {
                bar.setText (words);
                bar.getEditor().keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                settle();
            };

            auto reachable = [&main] (juce::Component& c)
            {
                auto* hit = main.getComponentAt (main.getLocalPoint (&c, c.getLocalBounds().getCentre()));
                return c.isVisible() && hit != nullptr && (hit == &c || c.isParentOf (hit));
            };

            // The drums first
            main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key));
            type ("funk");
            int drumsId = 0;

            for (auto& c : song.channels)
                if (c.type == InstrumentType::kit && ! c.drummer.turns.empty())
                    drumsId = c.id;

            check (drumsId != 0, "the drummer wrote a funk part");

            // Shift+F8: the bassist's tab, and the bar talks to it
            main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key, juce::ModifierKeys::shiftModifier, 0));
            settle();
            check (main.isPanelVisible (Panel::bass) && bar.getMode() == DrummerBar::Mode::bass, "Shift+F8 opens the AI Bass tab, and the prompt bar talks to the bassist");
            check (panel.getIdeaChip (0) != nullptr && reachable (*panel.getIdeaChip (0)), "before a part, it shows ideas to start from");

            const int channelsBefore = (int) song.channels.size();
            type ("funk, slap the octaves");
            Channel* bassChannel = nullptr;

            for (auto& c : song.channels)
                if (! c.bass.turns.empty())
                    bassChannel = &c;

            check (bassChannel != nullptr && (int) song.channels.size() == channelsBefore + 1, "Enter writes it at once, on a channel made for it");

            if (bassChannel != nullptr && song.findChannel (drumsId) != nullptr)
            {
                auto& b = *bassChannel;
                const auto* drums = song.findChannel (drumsId);
                check (b.name == "Bass" && b.type == InstrumentType::synth && b.synthPreset == "Funk Bass" && audio.getNova (b.id) != nullptr,
                       "a Bass channel on the bassist's funk sound (" + b.synthPreset + ")");

                bool sections = ! drums->drummer.patterns.empty();

                for (auto& [name, id] : drums->drummer.patterns)
                    sections = sections && b.bass.patterns.count (name) == 1;

                check (sections, "a bass pattern for each of the drummer's sections");
                check (bar.getText().isEmpty() && panel.getSectionChip (0) != nullptr && reachable (*panel.getSectionChip (0)),
                       "the bar clears and the tab shows the part, a chip for each section");

                type ("busier, in A");
                const auto second = b.bass.turns.size() == 2 ? bass::offline::requestOf (b.bass.turns[1]) : std::nullopt;
                check (second.has_value() && second->style == "funk" && second->keyRoot == 9 && b.bass.shown == 1,
                       "a follow-up (\"busier, in A\") makes version 2, still funk, in A");

                // Put in Arrange: the drums go in too, the bass under them
                const auto clipsBefore = song.playlist.size();
                static_cast<juce::Button&> (panel.getArrangeButton()).triggerClick();
                settle();
                int drumClips = 0, bassClips = 0;

                for (auto& clip : song.playlist)
                {
                    for (auto& [name, id] : drums->drummer.patterns)
                        drumClips += clip.patternId == id ? 1 : 0;

                    for (auto& [name, id] : b.bass.patterns)
                        bassClips += clip.patternId == id ? 1 : 0;
                }

                check (song.playlist.size() > clipsBefore && drumClips > 0 && bassClips == drumClips,
                       "Put in Arrange lays out the drums and the bass together (" + juce::String (drumClips) + " clips each)");

                // F8: back to the drummer
                main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key));
                settle();
                check (bar.getMode() == DrummerBar::Mode::drummer, "F8 gives the bar back to the drummer");

                // Saved with the song
                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-bass-test.nightshift");
                audio.flushPluginStates();
                song.save (file);
                Song loaded;
                loaded.load (file);
                file.deleteFile();
                const Channel* back = nullptr;

                for (auto& c : loaded.channels)
                    if (c.name == "Bass")
                        back = &c;

                check (back != nullptr && back->bass.turns.size() == 2 && back->bass.shown == 1 && back->bass.patterns.size() == b.bass.patterns.size(),
                       "the bassist's versions and patterns are saved with the song");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--offlineguitartest"))
        {
            // The offline guitarist: every style playing the drummer's sections and the bassist's
            // chords, every note where the tab says (its string and fret on the tuning, one note a
            // string at a time, a chord within reach), back from JSON, the same for a seed, quick;
            // each style's fingerprint over several takes; and into patterns with the palm-mute and
            // scratch keyswitches, a hammer-on running the note before on under it.
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            Song song;
            song.loadBlank();

            struct Band { drummer::Groove drums; bass::Part bass; };
            auto band = [&] (const juce::String& words, juce::int64 seed)
            {
                Band b;
                b.drums = drummer::offline::write (words, song, nullptr, {}, false, {}, true, seed).groove;
                b.bass = bass::offline::write (words, song, nullptr, &b.drums, false, true, seed).part;
                return b;
            };

            auto write = [&] (const juce::String& words, juce::int64 seed, const Band& b)
            {
                return guitar::offline::write (words, song, nullptr, &b.drums, &b.bass, true, true, seed);
            };

            auto section = [] (const guitar::Part& p, const juce::String& name) -> const guitar::Section*
            {
                for (auto& s : p.sections)
                    if (s.name == name)
                        return &s;

                return nullptr;
            };

            auto kicksIn = [] (const drummer::Groove& g, const juce::String& name, size_t bar)
            {
                std::vector<int> k;

                for (auto& s : g.sections)
                    if (s.name == name && bar < s.bars.size())
                        for (auto& l : s.bars[bar].lanes)
                            if (l.voice == "kick")
                                for (int i = 0; i < l.hits.length(); ++i)
                                    if (l.hits[i] != '.' && l.hits[i] != 'g')
                                        k.push_back (i);

                return k;
            };

            // Every style: the band's sections and chords, playable as written, back from JSON, the same
            // for a seed, quick
            {
                bool follows = true, playable = true, chords = true, roundTrip = true, same = true, sounds = true;
                juce::StringArray bad;
                double slowest = 0.0, total = 0.0;

                for (auto& style : agents::styles())
                {
                    const auto b = band (style.words[0], 4);
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    const auto r = write (style.words[0], 4, b);
                    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                    slowest = std::max (slowest, ms);
                    total += ms;

                    bool ok = r.part.sections.size() == b.drums.sections.size();

                    for (size_t s = 0; ok && s < r.part.sections.size(); ++s)
                    {
                        ok = r.part.sections[s].bars.size() == b.drums.sections[s].bars.size();

                        for (size_t i = 0; ok && i < r.part.sections[s].bars.size(); ++i)
                        {
                            ok = r.part.sections[s].bars[i].steps == b.drums.sections[s].bars[i].steps;

                            if (s < b.bass.sections.size() && i < b.bass.sections[s].bars.size())
                                chords = chords && r.part.sections[s].bars[i].chord == b.bass.sections[s].bars[i].chord;
                        }
                    }

                    follows = follows && ok;

                    for (auto& s : r.part.sections)
                        for (auto& bar : s.bars)
                        {
                            std::map<int, std::vector<const guitar::Note*>> atStep;

                            for (auto& n : bar.notes)
                                atStep[n.step].push_back (&n);

                            for (auto& [step, notes] : atStep)
                            {
                                std::set<int> strings;
                                int lo = 99, hi = -1;

                                for (auto* n : notes)
                                {
                                    const bool fine = n->string >= 0 && n->string < (int) r.part.tuning.size() && n->fret >= 0 && n->fret <= 22
                                                       && n->pitch == r.part.tuning[(size_t) n->string] + n->fret && strings.insert (n->string).second;

                                    if (n->fret > 0)
                                    {
                                        lo = std::min (lo, n->fret);
                                        hi = std::max (hi, n->fret);
                                    }

                                    if (! fine)
                                        bad.addIfNotAlreadyThere (style.id);

                                    playable = playable && fine;
                                }

                                if (hi - lo > 5)
                                {
                                    bad.addIfNotAlreadyThere (juce::String (style.id) + " (stretch " + juce::String (hi - lo) + ")");
                                    playable = false;
                                }
                            }
                        }

                    const auto json = juce::JSON::toString (r.part.toJson());
                    const auto back = guitar::Part::fromJson (juce::JSON::parse (json));
                    roundTrip = roundTrip && back.has_value() && juce::JSON::toString (back->toJson()) == json;
                    same = same && juce::JSON::toString (write (style.words[0], 4, b).part.toJson()) == json;
                    sounds = sounds && guitarsound::presetNames().contains (r.part.sound);
                }

                check (follows, "every style plays the drummer's sections, bar for bar, step for step");
                check (chords, "and the bassist's chord in every bar");
                check (playable, "every note is where the tab says, one a string, every chord within five frets"
                                   + (bad.isEmpty() ? juce::String() : " (not: " + bad.joinIntoString (", ") + ")"));
                check (roundTrip, "each reads back from its JSON as the same part");
                check (same, "the same seed writes the same part");
                check (sounds, "each style plays on one of the guitar's sounds");
                check (slowest < 300.0, "quick: " + juce::String (total, 1) + " ms for all 19, the slowest " + juce::String (slowest, 1) + " ms");
            }

            // Metalcore: drop C, the breakdown's kicks chugged on the open low string, palm-muted
            {
                bool tuned = true, locked = true, muted = true;

                for (juce::int64 seed = 1; seed <= 5; ++seed)
                {
                    const auto b = band ("metalcore with a breakdown", seed);
                    const auto r = write ("metalcore with a breakdown", seed, b);
                    tuned = tuned && r.part.tuning == std::vector<int> { 36, 43, 48, 53, 57, 62 } && r.part.sound == "High Gain";

                    if (auto* s = section (r.part, "Breakdown"))
                    {
                        int pm = 0, all = 0;

                        for (size_t i = 0; i < s->bars.size(); ++i)
                        {
                            for (auto k : kicksIn (r.drums, "Breakdown", i))
                                locked = locked && std::any_of (s->bars[i].notes.begin(), s->bars[i].notes.end(),
                                                                [k] (const guitar::Note& n) { return n.step == k && n.string == 0; });

                            for (auto& n : s->bars[i].notes)
                            {
                                pm += n.palmMute ? 1 : 0;
                                ++all;
                            }
                        }

                        muted = muted && all > 0 && pm * 3 >= all;
                    }
                    else
                        locked = false;
                }

                check (tuned, "metalcore: drop C (C G C F A D), on High Gain");
                check (locked, "its breakdown: every kick on the low string (5 takes)");
                check (muted, "and palm-muted, mostly");
            }

            // Funk: the 16th scratch, the stabs on the top strings
            {
                bool scratch = true, top = true;

                for (juce::int64 seed = 1; seed <= 5; ++seed)
                {
                    const auto b = band ("funk", seed);
                    const auto r = write ("funk", seed, b);

                    if (auto* s = section (r.part, "Verse"))
                    {
                        int dead = 0, all = 0;

                        for (auto& bar : s->bars)
                            for (auto& n : bar.notes)
                            {
                                dead += n.muted ? 1 : 0;
                                ++all;
                                top = top && n.string >= 3;
                            }

                        scratch = scratch && all > 0 && dead * 10 >= all * 4;
                    }
                    else
                        scratch = false;
                }

                check (scratch, "funk: the verse mostly the 16th scratch (dead notes), 5 takes");
                check (top, "and the ninth-chord stabs on the top three strings");
            }

            // Jazz: shells (root, third and seventh) on the 12-step triplet bars
            {
                bool shells = true;

                for (juce::int64 seed = 1; seed <= 5; ++seed)
                {
                    const auto b = band ("jazz", seed);
                    const auto r = write ("jazz", seed, b);

                    if (auto* s = section (r.part, "Head"))
                        for (auto& bar : s->bars)
                        {
                            std::map<int, int> perStep;

                            for (auto& n : bar.notes)
                                ++perStep[n.step];

                            for (auto& [step, count] : perStep)
                                shells = shells && bar.steps == 12 && (count == 3 || count == 4);
                        }
                    else
                        shells = false;
                }

                check (shells, "jazz: comping in three- and four-note shells, on the triplet bar");
            }

            // Pop-punk: power chords, palm-muted in the verse, open in the chorus, never a skipped string
            {
                const auto b = band ("pop punk", 2);
                const auto r = write ("pop punk", 2, b);
                bool power = true, verseMuted = true, chorusOpen = true;

                for (auto& s : r.part.sections)
                    for (auto& bar : s.bars)
                    {
                        std::map<int, std::vector<const guitar::Note*>> atStep;

                        for (auto& n : bar.notes)
                            atStep[n.step].push_back (&n);

                        for (auto& [step, notes] : atStep)
                        {
                            if (notes.size() < 2)
                                continue;

                            for (size_t i = 1; i < notes.size(); ++i)
                                power = power && (notes[i]->string == notes[i - 1]->string + 1      // an octave chord skips the string between
                                                  || (notes[i]->pitch - notes[i - 1]->pitch == 12 && notes[i]->string == notes[i - 1]->string + 2));

                            for (auto* n : notes)
                            {
                                // (the push into the next chord, on the last 8th, rings open)
                                verseMuted = verseMuted && (s.name != "Verse" || n->palmMute || n->step >= bar.steps - 2);
                                chorusOpen = chorusOpen && (s.name != "Chorus" || ! n->palmMute);
                            }
                        }
                    }

                check (power, "pop-punk: power chords on neighbouring strings, octaves over the one between");
                check (verseMuted && chorusOpen, "palm-muted in the verse, open in the chorus");
            }

            // Djent on a 7-string; neo-soul's hammer-ons and slides; trap's clean single notes
            {
                const auto d = write ("djent", 1, band ("djent", 1));
                check (d.part.tuning.size() == 7 && d.part.tuning.front() == 35, "djent: a 7-string in B standard (" + guitar::tuningName (d.part.tuning) + ")");

                bool legato = true;

                for (juce::int64 seed = 1; seed <= 4; ++seed)
                {
                    const auto r = write ("neo soul", seed, band ("neo soul", seed));
                    bool any = false;

                    for (auto& s : r.part.sections)
                        for (auto& bar : s.bars)
                            for (auto& n : bar.notes)
                                any = any || n.hammer || n.slide;

                    legato = legato && any;
                }

                check (legato, "neo-soul: hammer-ons or slides in every take");

                const auto t = write ("trap", 1, band ("trap", 1));
                bool single = true;

                if (auto* s = section (t.part, "Hook"))
                    for (auto& bar : s->bars)
                    {
                        std::set<int> steps;

                        for (auto& n : bar.notes)
                            single = single && steps.insert (n.step).second;
                    }

                check (single && t.part.sound == "Clean", "trap: a clean arpeggio, one note at a time");
            }

            // With no band yet, it plays to the one its style would have, and says so
            {
                const auto r = guitar::offline::write ("rock", song, nullptr, nullptr, nullptr, true, true, 3);
                check (r.imaginedDrums && r.imaginedBass && r.reply.contains ("No drums or bass yet"), "with no band it plays to a rock band's, and says so");
            }

            // Into patterns: the keyswitches, and a hammer-on running the note before on under it
            {
                Song s;
                s.loadBlank();
                const int id = s.addChannel ("Guitar", InstrumentType::instrument).id;

                guitar::Part part;
                part.tuning = { 40, 45, 50, 55, 59, 64 };
                guitar::Bar bar;
                bar.steps = 16;

                auto note = [] (int step, int length, int pitch, int string)
                {
                    guitar::Note n;
                    n.step = step;
                    n.length = length;
                    n.pitch = pitch;
                    n.string = string;
                    n.fret = pitch - std::vector<int> { 40, 45, 50, 55, 59, 64 }[(size_t) string];
                    return n;
                };

                for (int t : { 0, 2 })
                {
                    auto n = note (t, 2, 40, 0);
                    n.palmMute = true;
                    bar.notes.push_back (n);
                }

                auto dead = note (6, 1, 52, 3);
                dead.muted = true;
                bar.notes.push_back (dead);
                bar.notes.push_back (note (8, 2, 57, 2));
                auto hammer = note (10, 4, 59, 2);
                hammer.hammer = true;
                bar.notes.push_back (hammer);
                part.sections.push_back ({ "Riff", { bar } });

                guitar::render (s, id, part);
                const auto* p = s.findPattern (s.findChannel (id)->guitar.patterns["Riff"]);
                const auto* ns = p != nullptr ? p->findNotes (id) : nullptr;
                bool palm = false, scratchKey = false, overlaps = false;

                if (ns != nullptr)
                    for (auto& n : *ns)
                    {
                        palm = palm || (n.key == guitar::palmMuteKey && n.start <= 0 && n.end() > 2 * ticksPerStep);
                        scratchKey = scratchKey || (n.key == guitar::scratchKey && n.start < 6 * ticksPerStep && n.end() > 6 * ticksPerStep);
                        overlaps = overlaps || (n.key == 57 && n.end() > 10 * ticksPerStep);
                    }

                juce::String seen;

                if (ns != nullptr)
                    for (auto& n : *ns)
                        seen << n.key << "@" << n.start << "-" << n.end() << " ";

                check (palm && scratchKey, "palm mutes and the scratch hold their keyswitches over the notes they change (" + seen + ")");
                check (overlaps, "the note before a hammer-on runs on under it, so the guitar plays it legato");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--guitartest"))
        {
            // The AI guitarist from the top of the studio: drums and bass first, then Ctrl+F8 (the AI
            // Guitar tab, the bar talking to the guitarist), a request making a Guitar channel on the
            // style's sound with a pattern for each section, the part as tab, another section's tab,
            // a follow-up, Put in Arrange under the drummer's, saving.
            Song song;
            song.loadDemo();
            AudioEngine audio (song, false);
            audio.syncNow();
            MainComponent main (song, audio);
            main.setSize (1600, 940);
            main.setVisible (true);

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto settle = [] { settleMessages (30); };
            auto& bar = main.getDrummerBar();
            auto& panel = main.getGuitar();
            using Panel = MainComponent::Panel;

            auto type = [&] (const juce::String& words)
            {
                bar.setText (words);
                bar.getEditor().keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                settle();
            };

            main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key));
            type ("funk");
            main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key, juce::ModifierKeys::shiftModifier, 0));
            type ("funk");

            int drumsId = 0;

            for (auto& c : song.channels)
                if (c.type == InstrumentType::kit && ! c.drummer.turns.empty())
                    drumsId = c.id;

            main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key, juce::ModifierKeys::commandModifier, 0));
            settle();
            check (main.isPanelVisible (Panel::guitar) && bar.getMode() == DrummerBar::Mode::guitar, "Ctrl+F8 opens the AI Guitar tab, and the bar talks to the guitarist");

            const int channelsBefore = (int) song.channels.size();
            type ("funk chicken scratch");
            int guitarId = 0;

            for (auto& c : song.channels)
                if (! c.guitar.turns.empty())
                    guitarId = c.id;

            check (guitarId != 0 && (int) song.channels.size() == channelsBefore + 1, "Enter writes it at once, on a channel made for it");

            if (guitarId != 0 && drumsId != 0)
            {
                auto* g = song.findChannel (guitarId);
                check (isGuitar (*g) && guitarPresetOf (*g) == "Funk Clean", "a guitar on the funk sound (" + guitarPresetOf (*g) + ")");

                bool sections = true;

                for (auto& [name, id] : song.findChannel (drumsId)->drummer.patterns)
                    sections = sections && g->guitar.patterns.count (name) == 1;

                check (sections, "a guitar pattern for each of the drummer's sections");
                check (panel.getTabBarsShown() > 0 && panel.getShownSection() == "Intro", "the tab shows its first section (" + panel.getShownSection() + ", "
                                                                                              + juce::String (panel.getTabBarsShown()) + " bars)");
                panel.showSection ("Verse");
                check (panel.getShownSection() == "Verse" && panel.getTabBarsShown() == 8, "and another section's when asked");

                type ("busier");
                check (song.findChannel (guitarId)->guitar.turns.size() == 2, "a follow-up makes version 2");

                static_cast<juce::Button&> (panel.getArrangeButton()).triggerClick();
                settle();
                int guitarClips = 0;

                for (auto& clip : song.playlist)
                    for (auto& [name, id] : song.findChannel (guitarId)->guitar.patterns)
                        guitarClips += clip.patternId == id ? 1 : 0;

                check (guitarClips > 0, "Put in Arrange lays the guitar out (" + juce::String (guitarClips) + " clips)");

                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-guitar-test.nightshift");
                audio.flushPluginStates();
                song.save (file);
                Song loaded;
                loaded.load (file);
                file.deleteFile();
                const Channel* back = nullptr;

                for (auto& c : loaded.channels)
                    if (! c.guitar.turns.empty())
                        back = &c;

                check (back != nullptr && back->guitar.turns.size() == 2 && isGuitar (*back) && guitarPresetOf (*back) == "Funk Clean",
                       "the guitarist's versions and the sound are saved with the song");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--offlinekeystest"))
        {
            // The offline keys player: every style playing the drummer's sections and the bassist's
            // chords, in range, back from JSON, the same for a seed, quick, on a sound that's there;
            // each style's fingerprint (jazz's rootless voicings moving smoothly, neo-soul's grace
            // notes and pushes, pop-punk's pumping 8ths, trap's motif, funk's interlocking hands, the
            // metal piano out of the breakdown's way); and rolled chords in the patterns.
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            Song song;
            song.loadBlank();

            struct Band { drummer::Groove drums; bass::Part bass; };
            auto band = [&] (const juce::String& words, juce::int64 seed)
            {
                Band b;
                b.drums = drummer::offline::write (words, song, nullptr, {}, false, {}, true, seed).groove;
                b.bass = bass::offline::write (words, song, nullptr, &b.drums, false, true, seed).part;
                return b;
            };

            auto write = [&] (const juce::String& words, juce::int64 seed, const Band& b)
            {
                return keys::offline::write (words, song, nullptr, &b.drums, &b.bass, true, true, seed);
            };

            auto section = [] (const keys::Part& p, const juce::String& name) -> const keys::Section*
            {
                for (auto& s : p.sections)
                    if (s.name == name)
                        return &s;

                return nullptr;
            };

            auto pc = [] (int pitch) { return ((pitch % 12) + 12) % 12; };

            // Every style
            {
                bool follows = true, chords = true, inRange = true, roundTrip = true, same = true, sounds = true;
                double slowest = 0.0, total = 0.0;

                for (auto& style : agents::styles())
                {
                    const auto b = band (style.words[0], 4);
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    const auto r = write (style.words[0], 4, b);
                    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                    slowest = std::max (slowest, ms);
                    total += ms;

                    bool ok = r.part.sections.size() == b.drums.sections.size();

                    for (size_t s = 0; ok && s < r.part.sections.size(); ++s)
                    {
                        ok = r.part.sections[s].bars.size() == b.drums.sections[s].bars.size();

                        for (size_t i = 0; ok && i < r.part.sections[s].bars.size(); ++i)
                        {
                            ok = r.part.sections[s].bars[i].steps == b.drums.sections[s].bars[i].steps;

                            if (s < b.bass.sections.size() && i < b.bass.sections[s].bars.size())
                                chords = chords && r.part.sections[s].bars[i].chord == b.bass.sections[s].bars[i].chord;
                        }
                    }

                    follows = follows && ok;

                    for (auto& s : r.part.sections)
                        for (auto& bar : s.bars)
                            for (auto& n : bar.notes)
                                inRange = inRange && n.pitch >= 21 && n.pitch <= 108 && n.length >= 1 && n.step >= 0 && n.step < bar.steps;

                    const auto json = juce::JSON::toString (r.part.toJson());
                    const auto back = keys::Part::fromJson (juce::JSON::parse (json));
                    roundTrip = roundTrip && back.has_value() && juce::JSON::toString (back->toJson()) == json;
                    same = same && juce::JSON::toString (write (style.words[0], 4, b).part.toJson()) == json;
                    sounds = sounds && r.part.sound.isNotEmpty()
                               && (r.part.sampled.isNotEmpty() ? instruments::displayName (r.part.sampled).isNotEmpty() : nova::findFactory (r.part.sound) != nullptr);
                }

                check (follows, "every style plays the drummer's sections, bar for bar, step for step");
                check (chords, "and the bassist's chord in every bar");
                check (inRange, "every note on the keyboard, every length sound");
                check (roundTrip, "each reads back from its JSON as the same part");
                check (same, "the same seed writes the same part");
                check (sounds, "each style plays on a sound that's here (the sampled piano, or a Nova sound)");
                check (slowest < 300.0, "quick: " + juce::String (total, 1) + " ms for all 19, the slowest " + juce::String (slowest, 1) + " ms");
            }

            // Jazz: rootless voicings, led smoothly from chord to chord
            {
                bool rootless = true, smooth = true;

                for (juce::int64 seed = 1; seed <= 5; ++seed)
                {
                    const auto r = write ("jazz", seed, band ("jazz", seed));

                    if (auto* s = section (r.part, "Head"))
                    {
                        std::vector<int> last;
                        int moves = 0, travel = 0;

                        for (auto& bar : s->bars)
                        {
                            const auto ch = theory::parseChord (bar.chord);
                            std::map<int, std::vector<int>> voicings;

                            for (auto& n : bar.notes)
                                voicings[n.step].push_back (n.pitch);

                            for (auto& [step, v] : voicings)
                            {
                                // (the last of the bar is the next chord, pushed ahead of the one)
                                if (step >= bar.steps - 1)
                                    continue;

                                std::sort (v.begin(), v.end());
                                rootless = rootless && v.size() == 4 && pc (v.front()) != ch.root;

                                if (last.size() == v.size())
                                {
                                    for (size_t k = 0; k < v.size(); ++k)
                                        travel += std::abs (v[k] - last[k]);

                                    ++moves;
                                }

                                last = v;
                            }
                        }

                        smooth = smooth && moves > 0 && travel <= moves * 10;
                    }
                    else
                        rootless = false;
                }

                check (rootless, "jazz: comping in four-note rootless voicings, never the root at the bottom");
                check (smooth, "led from chord to chord: the voices move a few semitones between chords, not leaps");
            }

            // Neo-soul: grace notes sliding in, the chord pushed ahead of the one
            {
                bool graces = false, pushed = false;

                for (juce::int64 seed = 1; seed <= 5; ++seed)
                {
                    const auto r = write ("neo soul", seed, band ("neo soul", seed));

                    for (auto& s : r.part.sections)
                        for (size_t i = 0; i < s.bars.size(); ++i)
                        {
                            graces = graces || std::any_of (s.bars[i].notes.begin(), s.bars[i].notes.end(), [] (const keys::Note& n) { return n.grace; });

                            if (i > 0)
                                pushed = pushed || std::none_of (s.bars[i].notes.begin(), s.bars[i].notes.end(), [] (const keys::Note& n) { return ! n.left && n.step == 0; });
                        }
                }

                check (graces && pushed, "neo-soul: grace notes sliding into the chords, and chords pushed ahead of the one");
            }

            // Pop-punk: pumping 8ths, octaves on 1 and 3
            {
                const auto r = write ("pop punk", 2, band ("pop punk", 2));
                bool pumping = true;

                if (auto* s = section (r.part, "Verse"))
                    for (auto& bar : s->bars)
                    {
                        for (int t = 0; t < bar.steps; t += 2)
                            pumping = pumping && std::count_if (bar.notes.begin(), bar.notes.end(), [t] (const keys::Note& n) { return ! n.left && n.step == t; }) >= 3;

                        for (int t : { 0, 8 })
                            pumping = pumping && std::count_if (bar.notes.begin(), bar.notes.end(), [t] (const keys::Note& n) { return n.left && n.step == t; }) == 2;
                    }
                else
                    pumping = false;

                check (pumping, "pop-punk: a chord on every 8th, the left hand's octave on 1 and 3");
            }

            // Trap: the same motif every bar, high and clear
            {
                bool motif = true;

                for (juce::int64 seed = 1; seed <= 5; ++seed)
                {
                    const auto r = write ("trap", seed, band ("trap", seed));

                    if (auto* s = section (r.part, "Hook"))
                    {
                        std::set<int> first;

                        for (size_t i = 0; i < s->bars.size(); ++i)
                        {
                            std::set<int> steps;
                            int notes = 0;

                            for (auto& n : s->bars[i].notes)
                                if (! n.left)
                                {
                                    steps.insert (n.step);
                                    ++notes;
                                    motif = motif && n.pitch >= 60 && n.pitch <= 90;
                                }

                            motif = motif && notes >= 4;

                            if (i == 0)
                                first = steps;
                            else if (s->bars[i].steps == s->bars[0].steps)
                                motif = motif && steps == first;
                        }
                    }
                    else
                        motif = false;
                }

                check (motif, "trap: a motif of 4 or more notes, the same rhythm every bar, between C4 and F#6");
            }

            // Funk: the clav's two hands interlocking
            {
                bool interlock = true;
                juce::String clash;

                for (juce::int64 seed = 1; seed <= 5; ++seed)
                {
                    const auto r = write ("funk", seed, band ("funk", seed));

                    if (auto* s = section (r.part, "Verse"))
                    {
                        for (auto& bar : s->bars)
                            for (auto& n : bar.notes)
                                if (n.left && std::any_of (bar.notes.begin(), bar.notes.end(), [&n] (const keys::Note& o) { return ! o.left && o.step == n.step; }))
                                {
                                    interlock = false;

                                    if (clash.isEmpty())
                                        clash = "seed " + juce::String (seed) + ", " + bar.chord + ", step " + juce::String (n.step) + " of " + juce::String (bar.steps);
                                }
                    }
                    else
                        interlock = false;
                }

                check (interlock, "funk: the left hand never on a right-hand stab (they interlock)" + (clash.isEmpty() ? juce::String() : " (" + clash + ")"));
            }

            // Metalcore: out of the breakdown's way, big chords in the chorus
            {
                const auto r = write ("metalcore with a breakdown", 3, band ("metalcore with a breakdown", 3));
                int breakdownNotes = 0, bars = 0;

                if (auto* s = section (r.part, "Breakdown"))
                    for (auto& bar : s->bars)
                    {
                        breakdownNotes += (int) bar.notes.size();
                        ++bars;
                    }

                bool chorus = false;

                if (auto* s = section (r.part, "Chorus"))
                    chorus = std::all_of (s->bars.begin(), s->bars.end(), [] (const keys::Bar& bar)
                    {
                        return std::count_if (bar.notes.begin(), bar.notes.end(), [] (const keys::Note& n) { return n.left && n.step == 0; }) == 2
                                 && std::count_if (bar.notes.begin(), bar.notes.end(), [] (const keys::Note& n) { return ! n.left && n.step == 0; }) >= 3;
                    });

                check (bars > 0 && breakdownNotes <= bars, "metalcore: the keys stay out of the breakdown (" + juce::String (breakdownNotes) + " notes in " + juce::String (bars) + " bars)");
                check (chorus, "and hold big chords under the chorus, octaves in the left hand");
            }

            // With no band it plays to the one its style would have
            {
                const auto r = keys::offline::write ("lofi", song, nullptr, nullptr, nullptr, true, true, 3);
                check (r.imaginedDrums && r.imaginedBass && r.reply.contains ("No drums or bass yet") && r.part.rollMs > 0.0f,
                       "with no band it plays to a lofi band's, says so, and rolls its chords");
            }

            // Into patterns: a rolled chord, low to high, a little apart
            {
                Song s;
                s.loadBlank();
                const int id = s.addChannel ("Keys", InstrumentType::synth).id;
                keys::Part part;
                part.rollMs = 30.0f;
                keys::Bar bar;

                for (int p : { 60, 64, 67, 71 })
                {
                    keys::Note n;
                    n.step = 0;
                    n.length = 8;
                    n.pitch = p;
                    bar.notes.push_back (n);
                }

                part.sections.push_back ({ "Verse", { bar } });
                keys::render (s, id, part);
                const auto* p = s.findPattern (s.findChannel (id)->keys.patterns["Verse"]);
                bool rolled = p != nullptr && p->findNotes (id) != nullptr && p->findNotes (id)->size() == 4;

                if (rolled)
                {
                    const auto& ns = *p->findNotes (id);

                    for (size_t i = 1; i < ns.size(); ++i)
                        rolled = rolled && ns[i].start > ns[i - 1].start && ns[i].key > ns[i - 1].key;
                }

                check (rolled, "a rolled chord plays low to high, each note a little after the one under it");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--offlinesynthtest"))
        {
            // The offline synth player: every style, in each of its four jobs, playing the drummer's
            // sections and the bassist's chords, in range, back from JSON, the same for a seed, quick,
            // on a sound that's here and can play the job; the words for a job and a sound; and each
            // style's fingerprint (metalcore's arpeggio out of the verse and breakdown, trap's one
            // melody answering the keys, neo-soul's lead sliding, jazz's approach notes, djent's pad
            // led smoothly and floating over the breakdown, deathcore's low drone, funk's horns off
            // the kick and in octaves, pop-punk's octave hook, lofi's sparse lead behind the beat);
            // and a legato lead's notes in the patterns, one at a time but for its slides.
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            Song song;
            song.loadBlank();

            struct Band { drummer::Groove drums; bass::Part bass; };
            auto band = [&] (const juce::String& words, juce::int64 seed)
            {
                Band b;
                b.drums = drummer::offline::write (words, song, nullptr, {}, false, {}, true, seed).groove;
                b.bass = bass::offline::write (words, song, nullptr, &b.drums, false, true, seed).part;
                return b;
            };

            auto write = [&] (const juce::String& words, juce::int64 seed, const Band& b, const keys::Part* keysPart = nullptr)
            {
                return synth::offline::write (words, song, nullptr, {}, {}, &b.drums, &b.bass, keysPart, true, true, seed);
            };

            auto section = [] (const synth::Part& p, const juce::String& name) -> const synth::Section*
            {
                for (auto& s : p.sections)
                    if (s.name == name)
                        return &s;

                return nullptr;
            };

            auto notesIn = [] (const synth::Section* s)
            {
                int n = 0;

                if (s != nullptr)
                    for (auto& bar : s->bars)
                        n += (int) bar.notes.size();

                return n;
            };

            auto pc = [] (int pitch) { return ((pitch % 12) + 12) % 12; };

            // Every style, every job
            {
                bool follows = true, chords = true, inRange = true, roundTrip = true, same = true, sounds = true, plays = true;
                double slowest = 0.0, total = 0.0;
                juce::String quiet;
                int writes = 0;

                for (auto& style : agents::styles())
                {
                    const auto b = band (style.words[0], 4);

                    for (auto& job : juce::StringArray { "", "pad", "arp", "lead", "stabs" })
                    {
                        const auto words = style.words[0] + (job.isNotEmpty() ? " " + job : juce::String());
                        const auto t0 = juce::Time::getMillisecondCounterHiRes();
                        const auto r = write (words, 4, b);
                        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                        slowest = std::max (slowest, ms);
                        total += ms;
                        ++writes;

                        bool ok = r.part.sections.size() == b.drums.sections.size();

                        for (size_t s = 0; ok && s < r.part.sections.size(); ++s)
                        {
                            ok = r.part.sections[s].bars.size() == b.drums.sections[s].bars.size();

                            for (size_t i = 0; ok && i < r.part.sections[s].bars.size(); ++i)
                            {
                                ok = r.part.sections[s].bars[i].steps == b.drums.sections[s].bars[i].steps;

                                if (s < b.bass.sections.size() && i < b.bass.sections[s].bars.size())
                                    chords = chords && r.part.sections[s].bars[i].chord == b.bass.sections[s].bars[i].chord;
                            }
                        }

                        follows = follows && ok;
                        int count = 0;

                        for (auto& s : r.part.sections)
                            for (auto& bar : s.bars)
                                for (auto& n : bar.notes)
                                {
                                    inRange = inRange && n.pitch >= 36 && n.pitch <= 96 && n.length >= 1 && n.step >= 0 && n.step < bar.steps;
                                    ++count;
                                }

                        if (count == 0)
                        {
                            plays = false;
                            quiet << words << "; ";
                        }

                        const auto json = juce::JSON::toString (r.part.toJson());
                        const auto back = synth::Part::fromJson (juce::JSON::parse (json));
                        roundTrip = roundTrip && back.has_value() && juce::JSON::toString (back->toJson()) == json;
                        same = same && juce::JSON::toString (write (words, 4, b).part.toJson()) == json;
                        sounds = sounds && nova::findFactory (r.part.sound) != nullptr && synth::offline::fits (r.part.sound, r.part.role)
                                   && (job.isEmpty() ? r.part.role == synth::offline::roleFor (style.id) : r.part.role == job);
                    }
                }

                check (follows, "every style and job plays the drummer's sections, bar for bar, step for step");
                check (chords, "and the bassist's chord in every bar");
                check (inRange, "every note between C2 and C7, every length sound");
                check (plays, "every style plays something in every job" + (quiet.isEmpty() ? juce::String() : " (silent: " + quiet + ")"));
                check (roundTrip, "each reads back from its JSON as the same part");
                check (same, "the same seed writes the same part");
                check (sounds, "each plays the job asked for (or its style's own) on a Nova sound that can");
                check (slowest < 300.0, "quick: " + juce::String (total, 1) + " ms for all " + juce::String (writes) + ", the slowest " + juce::String (slowest, 1) + " ms");
            }

            // The words for a job and a sound
            {
                using synth::offline::askOf;
                const auto a = askOf ("trap bells"), b = askOf ("djent ambient pad"), c = askOf ("funk horn stabs"), d = askOf ("metalcore arpeggio"),
                           e = askOf ("neo soul moog lead"), f = askOf ("a jazz flute melody"), g = askOf ("deathcore choir");
                check (a.sound == "Crystal Bell" && b.role == "pad" && b.sound == "Aurora" && c.role == "stabs" && c.sound == "Synth Brass"
                         && d.role == "arp" && d.sound.isEmpty() && e.role == "lead" && e.sound == "Mono Lead" && f.role == "lead" && f.sound == "Soft Flute"
                         && g.sound == "Glass Choir",
                       "it reads the job and the sound from the words (bells, an ambient pad, horn stabs, an arpeggio, a Moog lead, a flute, a choir)");
                check (! synth::offline::fits ("Mono Lead", "pad") && ! synth::offline::fits ("Night Pad", "arp") && synth::offline::fits ("Supersaw Lead", "arp")
                         && synth::offline::fits ("Crystal Bell", "lead"),
                       "a one-note lead can't hold a pad, a slow pad can't play an arpeggio; a supersaw and a bell can play anything");

                const auto r = write ("trap bells", 2, band ("trap", 2));
                const auto choir = write ("deathcore choir", 2, band ("deathcore", 2));
                check (r.part.role == "lead" && r.part.sound == "Crystal Bell" && choir.part.role == "pad" && choir.part.sound == "Glass Choir",
                       "bells play a melody, a choir a pad");
            }

            // Metalcore: a 16th-note arpeggio in the chorus, out of the verse and the breakdown
            {
                const auto r = write ("metalcore with a breakdown", 3, band ("metalcore with a breakdown", 3));
                const auto* chorus = section (r.part, "Chorus");
                bool sixteenths = chorus != nullptr;

                if (chorus != nullptr)
                    for (auto& bar : chorus->bars)
                        sixteenths = sixteenths && bar.notes.size() >= 12;

                check (r.part.role == "arp" && sixteenths, "metalcore: an arpeggio in 16ths through the chorus");
                check (notesIn (section (r.part, "Verse")) == 0 && notesIn (section (r.part, "Breakdown")) == 0 && section (r.part, "Breakdown") != nullptr,
                       "out of the verse's and the breakdown's way");
            }

            // Trap: one melody, the beat's, the same rhythm every bar in every section, high
            {
                bool motif = true, high = true;
                int collisions = 0, alone = 0, bars = 0;

                for (juce::int64 seed = 1; seed <= 5; ++seed)
                {
                    const auto b = band ("trap", seed);
                    const auto keysPart = keys::offline::write ("trap", song, nullptr, &b.drums, &b.bass, true, true, seed).part;
                    const auto r = write ("trap", seed, b, &keysPart);
                    const auto plain = write ("trap", seed, b);
                    std::set<int> first;
                    int firstSteps = 0;

                    for (auto& s : r.part.sections)
                        for (size_t i = 0; i < s.bars.size(); ++i)
                        {
                            std::set<int> steps;

                            for (auto& n : s.bars[i].notes)
                            {
                                steps.insert (n.step);
                                high = high && n.pitch >= 72 && n.pitch <= 91;
                            }

                            if (i % 4 == 3 || steps.empty())
                                continue;

                            if (first.empty())
                            {
                                first = steps;
                                firstSteps = s.bars[i].steps;
                            }
                            else if (s.bars[i].steps == firstSteps)
                                motif = motif && steps == first;
                        }

                    // Where the keys play, the bells mostly don't
                    auto clashes = [&] (const synth::Part& p)
                    {
                        int n = 0;

                        for (auto& s : p.sections)
                            for (auto& ks : keysPart.sections)
                                if (ks.name == s.name)
                                    for (size_t i = 0; i < s.bars.size() && i < ks.bars.size(); ++i)
                                        if (s.bars[i].steps == ks.bars[i].steps)
                                            for (auto& n1 : s.bars[i].notes)
                                                n += std::any_of (ks.bars[i].notes.begin(), ks.bars[i].notes.end(),
                                                                  [&n1] (const keys::Note& k) { return ! k.left && k.step == n1.step; }) ? 1 : 0;

                        return n;
                    };

                    collisions += clashes (r.part);
                    alone += clashes (plain.part);

                    for (auto& s : r.part.sections)
                        bars += (int) s.bars.size();
                }

                check (motif, "trap: one melody, the same rhythm in every bar of every section (but each fourth)");
                check (high, "up high, C5 to G6, over the keys");
                check (collisions <= alone && collisions <= bars, "answering the keys: " + juce::String (collisions) + " notes on theirs in "
                                                                  + juce::String (bars) + " bars (" + juce::String (alone) + " not listening)");
            }

            // Neo-soul: a Moog lead sliding into its notes, behind the beat
            {
                int slides = 0;
                bool legato = true;

                for (juce::int64 seed = 1; seed <= 5; ++seed)
                {
                    const auto r = write ("neo soul", seed, band ("neo soul", seed));
                    legato = legato && r.part.sound == "Mono Lead" && synth::isLegato (r.part.sound) && r.part.pushMs > 0.0f;

                    for (auto& s : r.part.sections)
                        for (auto& bar : s.bars)
                            for (auto& n : bar.notes)
                                slides += n.slide ? 1 : 0;
                }

                check (legato && slides >= 10, "neo-soul: a Moog lead (Mono Lead, gliding) behind the beat, sliding into its notes (" + juce::String (slides) + " slides)");
            }

            // Jazz: a line with its strong beats on the chord, approached from a half step away
            {
                int beats = 0, onChord = 0, approaches = 0;

                for (juce::int64 seed = 1; seed <= 5; ++seed)
                {
                    const auto r = write ("jazz", seed, band ("jazz", seed));

                    for (auto& s : r.part.sections)
                        for (auto& bar : s.bars)
                        {
                            const auto ch = theory::parseChord (bar.chord);

                            for (size_t k = 0; k < bar.notes.size(); ++k)
                            {
                                const auto& n = bar.notes[k];

                                if (n.step == 0 || n.step * 2 == bar.steps)
                                {
                                    ++beats;
                                    const int d = pc (n.pitch - ch.root);
                                    onChord += ch.has (d) || d == 2 || d == 5 || d == 9 ? 1 : 0;
                                }

                                if (k + 1 < bar.notes.size() && std::abs (bar.notes[k + 1].pitch - n.pitch) == 1)
                                    ++approaches;
                            }
                        }
                }

                check (beats > 0 && onChord * 10 >= beats * 8, "jazz: the line on the chord (or its ninth, eleventh, thirteenth) on 1 and 3: "
                                                               + juce::String (onChord) + " of " + juce::String (beats));
                check (approaches >= 10, "approached from a half step away (" + juce::String (approaches) + " times)");
            }

            // Djent: an add9 pad led smoothly, floating on over the breakdown
            {
                const auto r = write ("djent with a breakdown", 2, band ("djent with a breakdown", 2));
                std::vector<int> last;
                int moves = 0, travel = 0;
                bool ninths = true;

                for (auto& s : r.part.sections)
                    for (auto& bar : s.bars)
                    {
                        std::vector<int> v;

                        for (auto& n : bar.notes)
                            if (n.step == 0)
                                v.push_back (n.pitch);

                        if (v.empty())
                            continue;

                        std::sort (v.begin(), v.end());
                        const auto ch = theory::parseChord (bar.chord);
                        ninths = ninths && std::any_of (v.begin(), v.end(), [&] (int p) { return pc (p - ch.root) == 2; });

                        if (last.size() == v.size())
                        {
                            for (size_t k = 0; k < v.size(); ++k)
                                travel += std::abs (v[k] - last[k]);

                            ++moves;
                        }

                        last = v;
                    }

                check (r.part.role == "pad" && ninths, "djent: an ambient pad, every chord with its ninth");
                check (moves > 0 && travel <= moves * 8, "led from chord to chord (" + juce::String (travel) + " semitones over " + juce::String (moves) + " changes)");
                check (notesIn (section (r.part, "Breakdown")) > 0, "and floating on over the breakdown");
            }

            // Deathcore: a low choir drone under the breakdown
            {
                const auto r = write ("deathcore with a breakdown", 2, band ("deathcore with a breakdown", 2));
                bool low = notesIn (section (r.part, "Breakdown")) > 0;

                if (auto* s = section (r.part, "Breakdown"))
                    for (auto& bar : s->bars)
                        for (auto& n : bar.notes)
                            low = low && n.pitch < 62;

                check (r.part.sound == "Glass Choir" && low, "deathcore: a choir, droning low under the breakdown");
            }

            // Funk: the horns between the kicks in the verse, a riff in octaves in the chorus
            {
                bool offKick = true, octaves = true;
                int stabs = 0;

                for (juce::int64 seed = 1; seed <= 5; ++seed)
                {
                    const auto b = band ("funk", seed);
                    const auto r = write ("funk", seed, b);

                    for (size_t si = 0; si < r.part.sections.size(); ++si)
                    {
                        const auto& s = r.part.sections[si];

                        if (s.name == "Verse")
                            for (size_t i = 0; i < s.bars.size(); ++i)
                            {
                                const auto kicks = theory::hitsIn (b.drums.sections[si].bars[i], { "kick" });

                                for (auto& n : s.bars[i].notes)
                                {
                                    offKick = offKick && std::find (kicks.begin(), kicks.end(), n.step) == kicks.end();
                                    ++stabs;
                                }
                            }

                        if (s.name == "Chorus")
                            for (auto& bar : s.bars)
                                for (auto& n : bar.notes)
                                    octaves = octaves && std::any_of (bar.notes.begin(), bar.notes.end(), [&n] (const synth::Note& o)
                                    {
                                        return o.step == n.step && std::abs (o.pitch - n.pitch) == 12;
                                    });
                    }
                }

                check (stabs > 0 && offKick, "funk: horn stabs between the kicks in the verse (" + juce::String (stabs) + " notes)");
                check (octaves, "and a riff in octaves in the chorus");
            }

            // Pop-punk: the hook in octaves in the chorus
            {
                const auto r = write ("pop punk", 2, band ("pop punk", 2));
                bool octaves = notesIn (section (r.part, "Chorus")) > 0;

                if (auto* s = section (r.part, "Chorus"))
                    for (auto& bar : s->bars)
                        for (auto& n : bar.notes)
                            octaves = octaves && std::any_of (bar.notes.begin(), bar.notes.end(), [&n] (const synth::Note& o)
                            {
                                return o.step == n.step && std::abs (o.pitch - n.pitch) == 12;
                            });

                check (r.part.role == "lead" && octaves, "pop-punk: the hook doubled in octaves in the chorus");
            }

            // Lofi: a few notes a bar, a bar's breath at the end of each phrase, behind the beat
            {
                int notes = 0, playing = 0;
                bool breathes = true, late = true;

                for (juce::int64 seed = 1; seed <= 5; ++seed)
                {
                    const auto r = write ("lofi", seed, band ("lofi", seed));
                    late = late && r.part.pushMs > 0.0f;

                    for (auto& s : r.part.sections)
                        for (size_t i = 0; i < s.bars.size(); ++i)
                        {
                            if (i % 4 == 3)
                                breathes = breathes && s.bars[i].notes.empty();
                            else if (! s.bars[i].notes.empty())
                            {
                                notes += (int) s.bars[i].notes.size();
                                ++playing;
                            }
                        }
                }

                check (playing > 0 && notes <= playing * 4 && breathes && late,
                       "lofi: a lazy lead, " + juce::String ((float) notes / (float) std::max (1, playing), 1) + " notes a bar, a breath every fourth bar, behind the beat");
            }

            // A follow-up keeps the job; a new style changes the sound it chose
            {
                const auto b = band ("trap", 3);
                const auto first = write ("trap pad", 3, b);
                const auto again = synth::offline::write ("darker", song, &first.request, first.part.role, first.part.sound, &b.drums, &b.bass, nullptr, false, true, 4);
                const auto other = synth::offline::write ("lofi", song, &first.request, first.part.role, first.part.sound, &b.drums, &b.bass, nullptr, false, true, 4);
                check (again.part.role == "pad" && again.part.sound.isEmpty(), "a follow-up keeps the pad, on the same sound");
                check (other.part.role == "pad" && other.part.sound == synth::offline::soundFor ("lofi", "pad"), "a new style moves the pad it chose to that style's (" + other.part.sound + ")");

                const auto mine = synth::offline::write ("trap arp", song, &first.request, "pad", "Glass Choir", &b.drums, &b.bass, nullptr, false, true, 4);
                check (mine.part.role == "arp" && mine.part.sound.isNotEmpty() && synth::offline::fits (mine.part.sound, "arp") && mine.reply.contains ("Glass Choir can't play"),
                       "a sound too slow for an arpeggio is swapped, and it says so");
            }

            // With no band it plays to the one its style would have
            {
                const auto r = synth::offline::write ("djent", song, nullptr, {}, {}, nullptr, nullptr, nullptr, true, true, 3);
                check (r.imaginedDrums && r.imaginedBass && r.reply.contains ("No drums or bass yet"), "with no band it plays to a djent band's, and says so");
            }

            // Into patterns: a legato lead one note at a time, running on into its slides
            {
                Song s;
                s.loadBlank();
                auto& ch = s.addChannel ("Lead", InstrumentType::synth);
                ch.synthPreset = "Mono Lead";
                const int id = ch.id;
                synth::Part part;
                synth::Bar bar;

                for (auto [step, pitch, slide] : { std::tuple { 0, 60, false }, std::tuple { 4, 62, true }, std::tuple { 8, 64, false }, std::tuple { 12, 67, false } })
                {
                    synth::Note n;
                    n.step = step;
                    n.length = 4;
                    n.pitch = pitch;
                    n.slide = slide;
                    bar.notes.push_back (n);
                }

                part.sections.push_back ({ "Verse", { bar } });
                synth::render (s, id, part);
                const auto* p = s.findPattern (s.findChannel (id)->synthPlayer.patterns["Verse"]);
                bool legato = p != nullptr && p->findNotes (id) != nullptr && p->findNotes (id)->size() == 4;

                if (legato)
                {
                    const auto& ns = *p->findNotes (id);
                    legato = ns[0].end() > ns[1].start            // into the slide: overlapping, so it glides
                          && ns[1].end() < ns[2].start             // the others: let go first
                          && ns[2].end() < ns[3].start;
                }

                check (legato, "a legato lead plays one note at a time, running on into a slide so it glides");

                // A pad's chord holds together
                ch.synthPreset = "Night Pad";
                synth::Part pad;
                synth::Bar held;

                for (int pitch : { 60, 64, 67 })
                {
                    synth::Note n;
                    n.length = 16;
                    n.pitch = pitch;
                    held.notes.push_back (n);
                }

                pad.sections.push_back ({ "Verse", { held } });
                synth::render (s, id, pad);
                const auto* q = s.findPattern (s.findChannel (id)->synthPlayer.patterns["Verse"]);
                const bool together = q != nullptr && q->findNotes (id) != nullptr && q->findNotes (id)->size() == 3
                                        && std::all_of (q->findNotes (id)->begin(), q->findNotes (id)->end(), [&] (const nightshift::Note& n)
                                           { return n.length == q->findNotes (id)->front().length; });
                check (together, "a pad's chord is held together");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--synthplayertest"))
        {
            // The AI synth player from the top of the studio: drums and bass, Ctrl+Shift+F8 (the AI
            // Synth tab, the bar talking to the synth player), a request making a Synth Lead channel
            // on the style's sound with a pattern per section and its fader set, a follow-up, a sound
            // picked for it, Put in Arrange, saving; and a pad on a new synth.
            Song song;
            song.loadDemo();
            AudioEngine audio (song, false);
            audio.syncNow();
            MainComponent main (song, audio);
            main.setSize (1600, 940);
            main.setVisible (true);

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto settle = [] { settleMessages (30); };
            auto& bar = main.getDrummerBar();
            auto& panel = main.getSynthPlayer();
            using Panel = MainComponent::Panel;

            auto type = [&] (const juce::String& words)
            {
                bar.setText (words);
                bar.getEditor().keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                settle();
            };

            main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key));
            type ("neo soul");
            main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key, juce::ModifierKeys::shiftModifier, 0));
            type ("neo soul");

            int drumsId = 0;

            for (auto& c : song.channels)
                if (c.type == InstrumentType::kit && ! c.drummer.turns.empty())
                    drumsId = c.id;

            main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key, juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
            settle();
            check (main.isPanelVisible (Panel::synthPlayer) && bar.getMode() == DrummerBar::Mode::synth,
                   "Ctrl+Shift+F8 opens the AI Synth tab, and the bar talks to the synth player");

            const int channelsBefore = (int) song.channels.size();
            type ("neo soul moog lead");
            int leadId = 0;

            for (auto& c : song.channels)
                if (! c.synthPlayer.turns.empty())
                    leadId = c.id;

            check (leadId != 0 && (int) song.channels.size() == channelsBefore + 1, "Enter writes it at once, on a channel made for it");

            if (leadId != 0 && drumsId != 0)
            {
                auto* k = song.findChannel (leadId);
                check (k->name == "Synth Lead" && k->type == InstrumentType::synth && k->synthPreset == "Mono Lead" && audio.getNova (leadId) != nullptr,
                       "a Synth Lead channel on the Moog-style lead (" + k->synthPreset + ")");
                check (std::abs (k->volume - 0.78f * std::pow (10.0f, synth::offline::levelFor ("neosoul", "lead") / 40.0f)) < 0.01f,
                       "its fader set for the style's mix");

                bool sections = true;

                for (auto& [name, id] : song.findChannel (drumsId)->drummer.patterns)
                    sections = sections && k->synthPlayer.patterns.count (name) == 1;

                check (sections, "a synth pattern for each of the drummer's sections");

                type ("more slides");
                check (song.findChannel (leadId)->synthPlayer.turns.size() == 2 && synth::shownPart (*song.findChannel (leadId))->role == "lead",
                       "a follow-up makes version 2, still a lead");

                // A sound picked for it: the part is played in again for it
                if (auto* screamer = nova::findFactory ("Screamer"))
                {
                    audio.applySynthPatch (leadId, *screamer);
                    synth::rerender (song, leadId);
                }

                check (song.findChannel (leadId)->synthPreset == "Screamer", "another sound can be picked for it");

                static_cast<juce::Button&> (panel.getArrangeButton()).triggerClick();
                settle();
                int clips = 0;

                for (auto& clip : song.playlist)
                    for (auto& [name, id] : song.findChannel (leadId)->synthPlayer.patterns)
                        clips += clip.patternId == id ? 1 : 0;

                check (clips > 0, "Put in Arrange lays the synth out (" + juce::String (clips) + " clips)");

                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-synth-test.nightshift");
                audio.flushPluginStates();
                song.save (file);
                Song loaded;
                loaded.load (file);
                file.deleteFile();
                const Channel* back = nullptr;

                for (auto& c : loaded.channels)
                    if (! c.synthPlayer.turns.empty())
                        back = &c;

                check (back != nullptr && back->synthPlayer.turns.size() == 2 && back->synthPreset == "Screamer", "the synth player's versions are saved with the song");
            }

            // A pad on a new synth: the style's pad sound, named for the job
            panel.setChannel (0);
            type ("neo soul pad");
            int padId = 0;

            for (auto& c : song.channels)
                if (! c.synthPlayer.turns.empty() && c.id != leadId)
                    padId = c.id;

            check (padId != 0 && song.findChannel (padId)->name == "Synth Pad" && song.findChannel (padId)->synthPreset == synth::offline::soundFor ("neosoul", "pad"),
                   "a pad on a new synth: a Synth Pad channel on the style's pad (" + (padId != 0 ? song.findChannel (padId)->synthPreset : juce::String()) + ")");

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--masterengineertest"))
        {
            // The AI mastering engineer, offline, on signals whose answers are known: it hears pink
            // noise as flat, a +6 dB bass shelf as about +6, mono as mono and two noises as wide;
            // reads the words; has a profile for every style; masters a beat to its style's target
            // without going over the ceiling, moving the tone the right way (air into a dull mix for
            // metalcore, out of a bright one for lofi) and checking its moves; follows up from where
            // the settings are (warmer, for Apple Music, louder); and says when it's silent.
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            constexpr double rate = 48000.0;

            // Pink noise (Paul Kellet's filter), its two sides from the same and a second noise
            struct Pink
            {
                juce::Random rng;
                float b[7] {};

                explicit Pink (juce::int64 seed) : rng (seed) {}

                float next()
                {
                    const float white = rng.nextFloat() * 2.0f - 1.0f;
                    b[0] = 0.99886f * b[0] + white * 0.0555179f;
                    b[1] = 0.99332f * b[1] + white * 0.0750759f;
                    b[2] = 0.96900f * b[2] + white * 0.1538520f;
                    b[3] = 0.86650f * b[3] + white * 0.3104856f;
                    b[4] = 0.55000f * b[4] + white * 0.5329522f;
                    b[5] = -0.7616f * b[5] - white * 0.0168980f;
                    const float out = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362f;
                    b[6] = white * 0.115926f;
                    return out * 0.11f;
                }
            };

            // seconds of it: `spread` 0 mono .. 1 two unrelated noises; a kick on every beat at 120 if asked
            auto noise = [&] (double seconds, float spread, float level, bool kicks)
            {
                juce::AudioBuffer<float> b (2, (int) (seconds * rate));
                Pink centre (1), side (2);

                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float c = centre.next(), s = side.next();
                    float l = (c + s * spread) * level, r = (c - s * spread) * level;

                    if (spread >= 0.99f)
                    {
                        l = c * level;                      // two unrelated noises
                        r = s * level;
                    }

                    if (kicks)
                    {
                        const double t = std::fmod (i / rate, 0.5);
                        const float kick = (float) (0.55 * std::exp (-t / 0.12) * std::sin (juce::MathConstants<double>::twoPi * (50.0 + 60.0 * std::exp (-t / 0.03)) * t));
                        l += kick;
                        r += kick;
                    }

                    b.setSample (0, i, l);
                    b.setSample (1, i, r);
                }

                return b;
            };

            auto shaped = [&] (const juce::AudioBuffer<float>& in, const Song::Mastering& m)
            {
                juce::AudioBuffer<float> out (in);
                mastering::Shaper shaper;
                shaper.prepare (rate);
                shaper.set (m);

                for (int pos = 0; pos < out.getNumSamples(); pos += 4096)
                    shaper.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), std::min (4096, out.getNumSamples() - pos));

                return out;
            };

            // Hearing
            {
                const auto pink = noise (20.0, 0.0f, 0.3f, false);
                const auto h = engineer::hear (pink, rate);
                bool flat = h.valid;

                for (auto band : h.bands)
                    flat = flat && std::abs (band) < 1.5f;

                check (flat, "pink noise is heard flat: " + juce::String (h.bands[0], 1) + ", " + juce::String (h.bands[1], 1) + ", "
                               + juce::String (h.bands[2], 1) + ", " + juce::String (h.bands[3], 1) + " dB an octave against the mids");

                Song::Mastering bass;
                bass.toneDb[0] = 6.0f;
                const auto boosted = engineer::hear (shaped (pink, bass), rate);
                const float lift = boosted.bands[0] - h.bands[0];
                check (lift > 4.0f && lift < 7.0f && std::abs (boosted.bands[2] - h.bands[2]) < 1.0f,
                       "a +6 dB bass shelf is heard as " + juce::String (lift, 1) + " dB more low end, the presence as it was");

                const auto wide = engineer::hear (noise (10.0, 1.0f, 0.3f, false), rate);
                check (h.sidesDb < -60.0f && h.correlation > 0.99f && std::abs (wide.sidesDb) < 1.5f && std::abs (wide.correlation) < 0.1f,
                       "mono is heard as mono (sides " + juce::String (h.sidesDb, 0) + " dB), two unrelated noises as wide (sides "
                         + juce::String (wide.sidesDb, 1) + " dB, correlation " + juce::String (wide.correlation, 2) + ")");
            }

            // The words
            {
                using engineer::askOf;
                const auto w = askOf ("warmer"), s = askOf ("for spotify"), l = askOf ("louder"), m = askOf ("loud metalcore master"),
                           h = askOf ("less harsh"), p = askOf ("more punch"), n = askOf ("narrower"), a = askOf ("master it again"),
                           q = askOf ("make it more dynamic");
                check (w.tone[1] > 0.0f && w.tone[2] < 0.0f && w.tone[3] < 0.0f && w.movesOnly() && s.target == "streaming" && l.louder == 1
                         && m.style == "metalcore" && m.target == "loud" && ! m.movesOnly() && h.tone[2] < 0.0f && p.punch > 0.0f && n.mono && n.width < 0.0f
                         && a.again && ! a.movesOnly() && q.louder == -1,
                       "it reads the words: warmer, for Spotify, louder, a loud metalcore master, less harsh, more punch, narrower, again, more dynamic");
            }

            // A profile for every style, each aiming somewhere the Master tab knows
            {
                bool all = true;

                for (auto& style : agents::styles())
                {
                    const auto& p = engineer::profileFor (style.id);
                    all = all && juce::String (p.style) == style.id && mastering::findTarget (p.target) != nullptr;
                }

                check (all && juce::String (engineer::profileFor ("metalcore").target) == "loud" && juce::String (engineer::profileFor ("trap").target) == "loud"
                         && juce::String (engineer::profileFor ("jazz").target) == "apple" && juce::String (engineer::profileFor ("lofi").target) == "streaming"
                         && engineer::profileFor ("trap").monoBassHz >= 150.0f && engineer::profileFor ("jazz").punch < 0.2f,
                       "every style has its master: metalcore and trap loud, jazz left dynamic for Apple Music, lofi for streaming, trap's bass in the middle");
            }

            // Mastering a beat
            juce::AudioBuffer<float> beat = noise (16.0, 0.4f, 0.12f, true);
            engineer::Result trap;
            {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                trap = engineer::master ("", beat, rate, "trap", nullptr);
                const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                check (trap.ok && trap.settings.target == "loud" && std::abs (trap.afterLufs + 9.0f) < 0.6f && trap.afterPeakDb <= -0.9f
                         && trap.settings.monoBassHz >= 150.0f && trap.reply.contains ("trap") && trap.compareDb < 0.0f,
                       "a beat mastered as trap: " + juce::String (trap.afterLufs, 2) + " LUFS (Loud, -9), peaks " + juce::String (trap.afterPeakDb, 2)
                         + " dB, the bass under 150 Hz in the middle, compare " + juce::String (trap.compareDb, 1) + " dB; "
                         + juce::String (ms / 1000.0, 1) + " s for 16 s of song");

                const auto turn = engineer::turnFor ("", trap);
                check (Song::Mastering::fromVar (juce::JSON::parse (turn.settings)) == trap.settings
                         && (float) juce::JSON::parse (turn.answer).getProperty ("compare_db", 0.0) == trap.compareDb,
                       "its version keeps the settings and what it heard");
            }

            // The tone, moved the right way and checked
            {
                // Dull: everything over 3 kHz rolled off steeply (pink noise is far brighter at the top than masters are)
                juce::AudioBuffer<float> dark (beat);

                for (int ch = 0; ch < 2; ++ch)
                    for (int pass = 0; pass < 2; ++pass)
                    {
                        juce::IIRFilter lowPass;
                        lowPass.setCoefficients (juce::IIRCoefficients::makeLowPass (rate, 3000.0));
                        lowPass.processSamples (dark.getWritePointer (ch), dark.getNumSamples());
                    }

                Song::Mastering bright;
                bright.toneDb = { 0.0f, 0.0f, 3.0f, 6.0f };
                const auto glassy = shaped (beat, bright);

                const auto metal = engineer::master ("", dark, rate, "metalcore", nullptr);
                const auto lofi = engineer::master ("", glassy, rate, "lofi", nullptr);
                check (metal.ok && metal.settings.toneDb[3] > 0.0f && lofi.ok && lofi.settings.toneDb[3] < 0.0f,
                       "air into a dull mix for metalcore (Air " + juce::String (metal.settings.toneDb[3], 1) + " dB), out of a bright one for lofi (Air "
                         + juce::String (lofi.settings.toneDb[3], 1) + " dB)");

                const float moved = metal.after.levels[3] - metal.before.levels[3];
                check (moved > 0.0f && std::abs (moved - metal.settings.toneDb[3]) < 3.0f,
                       "and it hears the move land: the air up " + juce::String (moved, 1) + " dB for Air " + juce::String (metal.settings.toneDb[3], 1));

                bool gentle = true;

                for (const engineer::Result* r : std::initializer_list<const engineer::Result*> { &metal, &lofi, &trap })
                    gentle = gentle && r->settings.toneDb[2] <= 3.0f && r->settings.toneDb[3] <= 3.0f;

                check (gentle, "no more than +3 dB at the top, where more turns harsh");
            }

            // Follow-ups, from the settings as they are
            {
                const auto warmer = engineer::master ("warmer", beat, rate, "trap", &trap.settings);
                check (warmer.ok && warmer.settings.toneDb[1] > trap.settings.toneDb[1] && warmer.settings.toneDb[3] < trap.settings.toneDb[3]
                         && warmer.settings.target == trap.settings.target && warmer.settings.punchAmount == trap.settings.punchAmount,
                       "\"warmer\": more body, less air, the rest as it was (Body " + juce::String (trap.settings.toneDb[1], 1) + " to "
                         + juce::String (warmer.settings.toneDb[1], 1) + ", Air " + juce::String (trap.settings.toneDb[3], 1) + " to " + juce::String (warmer.settings.toneDb[3], 1)
                         + ", Punch " + juce::String (trap.settings.punchAmount, 2) + " to " + juce::String (warmer.settings.punchAmount, 2) + ")");

                const auto apple = engineer::master ("for apple music", beat, rate, "trap", &trap.settings);
                check (apple.ok && apple.settings.target == "apple" && apple.settings.toneDb == trap.settings.toneDb && std::abs (apple.afterLufs + 16.0f) < 0.6f,
                       "\"for Apple Music\": the same tone, at " + juce::String (apple.afterLufs, 2) + " LUFS");

                auto streaming = trap.settings;
                streaming.target = "streaming";
                const auto louder = engineer::master ("louder", beat, rate, "trap", &streaming);
                check (louder.ok && louder.settings.target == "loud", "\"louder\" from Streaming goes to Loud");
            }

            // Silence
            {
                juce::AudioBuffer<float> quiet (2, (int) rate * 4);
                quiet.clear();
                const auto r = engineer::master ("", quiet, rate, "rock", nullptr);
                check (! r.ok && r.problem.contains ("silent"), "a silent song: it says there's nothing to master");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--masteraitest"))
        {
            // The AI mastering engineer from the studio: F12 opens the Master tab on it and the bar
            // talks to it; Master it lands the demo at its target; a follow-up typed in the bar;
            // the versions; Listen's Before and After (and an export never carrying them); words
            // naming a style; saving; undo; Shift+F12.
            Song song;
            song.loadDemo();
            AudioEngine audio (song, false);
            audio.syncNow();
            MainComponent main (song, audio);
            main.setSize (1600, 940);
            main.setVisible (true);

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto& bar = main.getDrummerBar();
            auto& engineerPanel = main.getMasterEngineer();
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NightShift master ai test");
            folder.createDirectory();

            auto wait = [&]
            {
                for (int i = 0; i < 2400 && (i < 4 || engineerPanel.isWorking()); ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            };

            auto type = [&] (const juce::String& words)
            {
                bar.setText (words);
                bar.getEditor().keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                wait();
            };

            auto loudness = [&] (const juce::String& name)
            {
                const auto file = folder.getChildFile (name + ".wav");
                audio.renderToFile (file);
                const auto r = mastering::measureFile (file);
                file.deleteFile();
                return r;
            };

            main.keyPressed (juce::KeyPress (juce::KeyPress::F12Key, juce::ModifierKeys::shiftModifier, 0));
            settleMessages (30);
            const bool tabShown = main.isPanelVisible (MainComponent::Panel::masterAi), inList = main.getAiPanel().getShown() == 6,
                       barTalks = bar.getMode() == DrummerBar::Mode::master, buttonThere = engineerPanel.isVisible() && engineerPanel.getMasterButton().isVisible();
            check (tabShown && inList && barTalks && buttonThere,
                   "Shift+F12 opens the AI tab on Master, Master it showing, and the bar talks to the engineer ("
                     + juce::String (tabShown ? "" : "not shown ") + (inList ? "" : "another agent ") + (barTalks ? "" : "bar elsewhere ") + (buttonThere ? "" : "no button") + ")");

            // The Master tab points to it
            main.showMasterPage (0);
            main.getMaster().getAiMasterButton().triggerClick();
            settleMessages (50);     // a click arrives as a message
            check (main.isPanelVisible (MainComponent::Panel::masterAi), "the Master tab's \"Let the AI master it\" opens it");

            static_cast<juce::Button&> (engineerPanel.getMasterButton()).triggerClick();
            wait();
            const auto first = song.mastering;
            const auto r1 = loudness ("first");
            check (song.masterTurns.size() == 1 && song.masterShown == 0 && first.target == "streaming" && first.pushDb != 0.0f
                     && std::abs (r1.integratedLufs + 14.0f) < 0.6f && r1.truePeakDb <= -0.9f && song.masterTurns[0].reply.contains ("Mastered for Streaming"),
                   "Master it masters the demo for Streaming (no style to go by): " + juce::String (r1.integratedLufs, 2) + " LUFS, peaks "
                     + juce::String (r1.truePeakDb, 2) + " dB");

            type ("warmer");
            check (song.masterTurns.size() == 2 && song.masterShown == 1 && song.mastering.toneDb[1] > first.toneDb[1] && song.mastering.toneDb[3] < first.toneDb[3]
                     && bar.getText().isEmpty(),
                   "\"warmer\" typed in the bar: version 2, more body and less air");

            engineerPanel.showVersion (0);
            check (song.mastering == first && song.masterShown == 0, "clicking version 1 puts its settings back");

            engineerPanel.setListen (1);
            const bool before = audio.getMasterCompare() == 1;
            engineerPanel.setListen (2);
            const bool after = audio.getMasterCompare() == 2;
            const auto exported = loudness ("compare");
            check (before && after && audio.getMasterCompare() == 2 && std::abs (exported.integratedLufs - r1.integratedLufs) < 0.3f,
                   "Listen: Before plays the mix, After the master at its loudness; an export meanwhile is the master as it is ("
                     + juce::String (exported.integratedLufs, 2) + " LUFS)");
            engineerPanel.setListen (0);

            type ("loud metalcore master");
            const auto answer = juce::JSON::parse (song.masterTurns.back().answer);
            check (song.masterTurns.size() == 3 && song.mastering.target == "loud" && answer.getProperty ("style", {}).toString() == "metalcore"
                     && song.mastering.monoBassHz >= 120.0f,
                   "\"loud metalcore master\": mastered as metalcore, for Loud, the bass kept in the middle");

            const auto file = folder.getChildFile ("saved.nightshift");
            audio.flushPluginStates();
            song.save (file);
            Song loaded;
            loaded.load (file);
            file.deleteFile();
            check (loaded.masterTurns.size() == 3 && loaded.masterShown == 2 && loaded.mastering == song.mastering,
                   "its versions are saved with the song");

            const auto metalcore = song.mastering;
            song.undo();
            check (! (song.mastering == metalcore) && song.masterTurns.size() == 2, "one undo takes the last master back");

            main.keyPressed (juce::KeyPress (juce::KeyPress::F12Key, juce::ModifierKeys::shiftModifier, 0));
            const bool hidden = ! main.isPanelVisible (MainComponent::Panel::masterAi);
            main.showMasterPage (0);
            main.keyPressed (juce::KeyPress (juce::KeyPress::F12Key, juce::ModifierKeys::shiftModifier, 0));
            check (hidden && main.isPanelVisible (MainComponent::Panel::masterAi) && bar.getMode() == DrummerBar::Mode::master,
                   "Shift+F12 closes it, and from another tab brings the engineer back");

            folder.deleteRecursively();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--songwritertest"))
        {
            // The song agent's plan, offline: who plays each style and what the words change; each
            // style's song form; the drummer's words (its sections, longer ones for fast styles, a
            // "drop C" tuning not taken for a drop); and every style's arrangement two minutes or
            // more, starting on its intro and ending on its outro, every section one the drummer wrote.
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            using namespace songwriter;

            check (lineupFor ("metalcore", "metalcore song") == juce::StringArray { "bass", "guitar", "keys", "synth" }
                     && lineupFor ("trap", "trap beat") == juce::StringArray { "bass", "keys", "synth" }
                     && lineupFor ("thrash", "thrash") == juce::StringArray { "bass", "guitar" }
                     && lineupFor ("jazz", "jazz trio with a flute") == juce::StringArray { "bass", "keys", "synth" },
                   "each style's band: metalcore's guitars, keys and synth; trap's 808, piano and bells; thrash's guitars only; jazz's trio and a flute");
            check (! lineupFor ("funk", "funk with horns, no keys").contains ("keys") && lineupFor ("funk", "funk with horns, no keys").contains ("synth")
                     && lineupFor ("lofi", "lofi beat with guitar").contains ("guitar") && ! lineupFor ("rock", "rock without the guitar").contains ("guitar")
                     && ! lineupFor ("trap", "trap, no piano").contains ("keys"),
                   "the words change it: no keys, with guitar, without the guitar, no piano");

            check (formFor ("metalcore", {}).contains ("breakdown") && formFor ("trap", {})[1] == "hook" && formFor ("rock", {}).contains ("bridge")
                     && formFor ("metalcore", {})[0] == "intro" && formFor ("metalcore", {}).strings.getLast() == "outro"
                     && formFor ("poppunk", "pop punk with a breakdown").contains ("breakdown"),
                   "each style's form: metal's breakdown, trap's hook first, rock's bridge, an intro and an outro; a breakdown asked for goes in");

            const auto words = drummerWords ("metalcore song in drop C", "metalcore");
            check (words.contains ("intro verse chorus breakdown outro") && words.contains ("drop c") && ! words.contains (" song ")
                     && ! agents::Request::parse ("metalcore song in drop C").sections.contains ("drop")
                     && agents::Request::parse ("edm with a drop").sections.contains ("drop")
                     && drummerWords ("thrash", "thrash").contains ("16 bars") && ! drummerWords ("lofi", "lofi").contains ("16 bars"),
                   "the drummer's words: its sections once each (\"" + words + "\"), drop C a tuning not a drop, 16-bar sections for fast thrash, 8 for slow lofi");

            {
                bool allLong = true, ends = true, known = true;
                juce::String worst;
                double shortest = 1.0e9;
                Song song;
                song.loadBlank();

                for (auto& style : agents::styles())
                {
                    const auto prompt = juce::String (style.words[0]) + " song";
                    const auto g = drummer::offline::write (drummerWords (prompt, style.id), song, nullptr, {}, false, {}, true, 3).groove;
                    const int beats = g.meterBeats > 0 ? g.meterBeats : 4, unit = g.meterUnit > 0 ? g.meterUnit : 4;
                    const double tempo = g.tempo > 0 ? g.tempo : style.usual;
                    const double secondsPerBar = beats * (4.0 / unit) * 60.0 / tempo;
                    const auto order = arrange (g, style.id, prompt, secondsPerBar);
                    const double seconds = lengthOf (g, order, secondsPerBar);

                    if (seconds < shortest)
                    {
                        shortest = seconds;
                        worst = juce::String (style.id) + " (" + order.joinIntoString (", ") + ")";
                    }

                    allLong = allLong && seconds >= 120.0 && seconds < 420.0;
                    ends = ends && theory::kindOf (order[0]) == "intro" && theory::kindOf (order[order.size() - 1]) == "outro";

                    for (auto& name : order)
                        known = known && std::any_of (g.sections.begin(), g.sections.end(), [&name] (const drummer::Section& s) { return s.name == name; });
                }

                check (allLong, "every style's song lasts two minutes or more (the shortest " + juce::String (shortest, 0) + " s: " + worst + ")");
                check (ends && known, "each starts on its intro and ends on its outro, every section one the drummer wrote");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--songtest"))
        {
            // The AI song agent from the studio: the AI tab's list of seven, a click on one; a song
            // typed in the bar making a new project with every player of its style, laid out two
            // minutes or more in Arrange, mastered for its style, and said so; rendered, as long as
            // that and as loud; a second song with the words changing the band; saved.
            Song song;
            song.loadDemo();
            song.markSaved();
            AudioEngine audio (song, false);
            audio.syncNow();
            MainComponent main (song, audio);
            main.setSize (1600, 940);
            main.setVisible (true);

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto& bar = main.getDrummerBar();
            auto& panel = main.getSongPanel();
            using Panel = MainComponent::Panel;

            auto make = [&] (const juce::String& words)
            {
                song.markSaved();
                bar.setText (words);
                bar.getEditor().keyPressed (juce::KeyPress (juce::KeyPress::returnKey));

                for (int i = 0; i < 4000 && (i < 4 || panel.isWorking()); ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            };

            auto players = [&]
            {
                juce::StringArray who;

                for (auto& c : song.channels)
                {
                    if (! c.drummer.turns.empty())          who.add ("drums");
                    if (! c.bass.turns.empty())             who.add ("bass");
                    if (! c.guitar.turns.empty())           who.add ("guitar");
                    if (! c.keys.turns.empty())             who.add ("keys");
                    if (! c.synthPlayer.turns.empty())      who.add ("synth");
                }

                return who;
            };

            // One AI tab, every agent in its list
            main.openAgent (Panel::song);
            check (main.isPanelVisible (Panel::song) && main.getAiPanel().getNumAgents() == 7 && main.getAiPanel().getShown() == 0 && bar.getMode() == DrummerBar::Mode::song,
                   "the AI tab lists all seven agents, Song first, and the bar talks to the song agent");

            main.getAiPanel().onPick (2);
            check (main.isPanelVisible (Panel::bass) && main.getAiPanel().getShown() == 2 && bar.getMode() == DrummerBar::Mode::bass,
                   "picking Bass in the list shows the bassist, and the bar talks to it");

            main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key, juce::ModifierKeys::altModifier, 0));
            check (main.isPanelVisible (Panel::keys) && main.getAiPanel().getShown() == 4, "Alt+F8 still goes straight to the keys player, in the AI tab");

            main.openAgent (Panel::song);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            make ("metalcore song in drop C");
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const double seconds = song.secondsAt (song.songLengthInTicks());

            const bool fresh = std::none_of (song.channels.begin(), song.channels.end(), [] (const Channel& c) { return c.sample.startsWith ("kit:"); });
            check (fresh && players() == juce::StringArray { "drums", "bass", "guitar", "keys", "synth" },
                   "a metalcore song in a new project, all of its band playing: " + players().joinIntoString (", "));
            check (seconds >= 120.0, "laid out " + juce::String (seconds, 1) + " s long in Arrange (" + juce::String (ms / 1000.0, 1) + " s to make)");

            bool everyoneThere = true;

            for (auto& c : song.channels)
            {
                int clips = 0;

                for (auto& clip : song.playlist)
                    if (auto p = song.findPattern (clip.patternId); p != nullptr && p->findNotes (c.id) != nullptr)
                        ++clips;

                everyoneThere = everyoneThere && clips > 0;
            }

            check (everyoneThere && ! song.playlist.empty(), "every player's parts are in Arrange");

            const auto answer = song.masterTurns.empty() ? juce::var() : juce::JSON::parse (song.masterTurns.back().answer);
            check (song.masterTurns.size() == 1 && song.mastering.target == "loud" && answer.getProperty ("style", {}).toString() == "metalcore",
                   "mastered as metalcore, for Loud");
            check (song.songReply.contains ("metalcore song") && song.songReply.contains ("Mastered") && song.songPrompt == "metalcore song in drop C"
                     && main.isPanelVisible (Panel::song) && panel.getNumPlayerChips() == 6,
                   "it says what it made, with a button for each player and the master: \"" + song.songReply.upToFirstOccurrenceOf (".", true, false) + "\"");

            {
                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-song-test.wav");
                audio.renderToFile (file);
                const auto r = mastering::measureFile (file);
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                double length = 0.0;

                if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr)
                    length = (double) reader->lengthInSamples / reader->sampleRate;

                file.deleteFile();
                check (length >= 120.0 && std::abs (r.integratedLufs + 9.0f) < 1.0f && r.truePeakDb <= -0.9f,
                       "rendered: " + juce::String (length, 1) + " s, " + juce::String (r.integratedLufs, 1) + " LUFS, peaks " + juce::String (r.truePeakDb, 1) + " dB");
            }

            // The words change the band
            make ("lofi beat with guitar, no keys");
            const auto band = players();
            check (band.contains ("guitar") && ! band.contains ("keys") && band.contains ("synth") && song.songPrompt.startsWith ("lofi")
                     && song.secondsAt (song.songLengthInTicks()) >= 120.0 && song.mastering.target == "streaming",
                   "\"lofi beat with guitar, no keys\": a new song, " + band.joinIntoString (", ") + ", for Streaming");

            // Saved with the project
            {
                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-song-test.nightshift");
                audio.flushPluginStates();
                song.save (file);
                Song loaded;
                loaded.load (file);
                file.deleteFile();
                check (loaded.songPrompt == song.songPrompt && loaded.songReply == song.songReply && loaded.masterTurns.size() == 1,
                       "what it made is saved with the project");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--keystest"))
        {
            // The AI keys player from the top of the studio: drums and bass, Alt+F8 (the AI Keys tab,
            // the bar talking to the keys player), a request making a Keys channel on the style's
            // sound with a pattern per section, a follow-up, Put in Arrange, saving; and jazz on new
            // keys getting the sampled grand piano when it's here.
            Song song;
            song.loadDemo();
            AudioEngine audio (song, false);
            audio.syncNow();
            MainComponent main (song, audio);
            main.setSize (1600, 940);
            main.setVisible (true);

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto settle = [] { settleMessages (30); };
            auto& bar = main.getDrummerBar();
            auto& panel = main.getKeys();
            using Panel = MainComponent::Panel;

            auto type = [&] (const juce::String& words)
            {
                bar.setText (words);
                bar.getEditor().keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                settle();
            };

            main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key));
            type ("neo soul");
            main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key, juce::ModifierKeys::shiftModifier, 0));
            type ("neo soul");

            int drumsId = 0;

            for (auto& c : song.channels)
                if (c.type == InstrumentType::kit && ! c.drummer.turns.empty())
                    drumsId = c.id;

            main.keyPressed (juce::KeyPress (juce::KeyPress::F8Key, juce::ModifierKeys::altModifier, 0));
            settle();
            check (main.isPanelVisible (Panel::keys) && bar.getMode() == DrummerBar::Mode::keys, "Alt+F8 opens the AI Keys tab, and the bar talks to the keys player");

            const int channelsBefore = (int) song.channels.size();
            type ("neo soul Rhodes");
            int keysId = 0;

            for (auto& c : song.channels)
                if (! c.keys.turns.empty())
                    keysId = c.id;

            check (keysId != 0 && (int) song.channels.size() == channelsBefore + 1, "Enter writes it at once, on a channel made for it");

            if (keysId != 0 && drumsId != 0)
            {
                auto* k = song.findChannel (keysId);
                check (k->name == "Keys" && k->type == InstrumentType::synth && k->synthPreset == "Dusty Rhodes" && audio.getNova (keysId) != nullptr,
                       "a Keys channel on the Rhodes (" + k->synthPreset + ")");

                bool sections = true;

                for (auto& [name, id] : song.findChannel (drumsId)->drummer.patterns)
                    sections = sections && k->keys.patterns.count (name) == 1;

                check (sections, "a keys pattern for each of the drummer's sections");

                type ("busier");
                check (song.findChannel (keysId)->keys.turns.size() == 2, "a follow-up makes version 2");

                static_cast<juce::Button&> (panel.getArrangeButton()).triggerClick();
                settle();
                int clips = 0;

                for (auto& clip : song.playlist)
                    for (auto& [name, id] : song.findChannel (keysId)->keys.patterns)
                        clips += clip.patternId == id ? 1 : 0;

                check (clips > 0, "Put in Arrange lays the keys out (" + juce::String (clips) + " clips)");

                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-keys-test.nightshift");
                audio.flushPluginStates();
                song.save (file);
                Song loaded;
                loaded.load (file);
                file.deleteFile();
                const Channel* back = nullptr;

                for (auto& c : loaded.channels)
                    if (! c.keys.turns.empty())
                        back = &c;

                check (back != nullptr && back->keys.turns.size() == 2 && back->synthPreset == "Dusty Rhodes", "the keys player's versions are saved with the song");
            }

            // New keys for jazz: the grand piano, sampled, when it's here
            const auto piano = keys::offline::soundFor ("jazz");
            panel.setChannel (0);
            type ("jazz comping");
            int jazzId = 0;

            for (auto& c : song.channels)
                if (! c.keys.turns.empty() && c.id != keysId)
                    jazzId = c.id;

            if (piano.sampled.isNotEmpty())
                check (jazzId != 0 && song.findChannel (jazzId)->type == InstrumentType::instrument && song.findChannel (jazzId)->instrument == piano.sampled,
                       "jazz on new keys gets the sampled grand piano");
            else
                check (jazzId != 0 && song.findChannel (jazzId)->type == InstrumentType::synth, "jazz on new keys gets a Nova keys sound (no sampled piano here)");

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--masteringtest"))
        {
            // Mastering, round one: the loudness meter reads EBU's reference tone right; the
            // limiter keeps everything under its ceiling, between the samples too, and leaves a
            // quiet signal as it was; the master volume drives it rather than going over;
            // "Set loudness" lands on its targets; the settings save; a 16-bit export is dithered
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            const double rate = 48000.0;
            constexpr float pi2 = juce::MathConstants<float>::twoPi;
            auto measure = [rate] (const juce::AudioBuffer<float>& b, int from = 0)
            {
                tracktion::LoudnessMeter meter;
                meter.prepare (rate, b.getNumChannels(), 4096);

                for (int pos = from; pos < b.getNumSamples(); pos += 4096)
                {
                    const int n = std::min (4096, b.getNumSamples() - pos);
                    const float* ch[2] = { b.getReadPointer (0, pos), b.getReadPointer (b.getNumChannels() - 1, pos) };
                    meter.process (ch, b.getNumChannels(), n);
                }

                meter.flush();
                return meter.getReadings();
            };

            auto through = [rate] (juce::AudioBuffer<float> b, float pushDb, float ceilingDb)
            {
                mastering::Maximizer limiter;
                limiter.prepare (rate);
                limiter.setCeilingDb (ceilingDb);
                limiter.setGainDb (pushDb, true);

                for (int pos = 0; pos < b.getNumSamples(); pos += 512)
                    limiter.process (b.getWritePointer (0, pos), b.getWritePointer (1, pos), std::min (512, b.getNumSamples() - pos));

                return b;
            };

            // EBU Tech 3341, test 1: a 1 kHz tone at -23 dBFS in both channels reads -23.0 LUFS
            {
                juce::AudioBuffer<float> tone (2, (int) (20 * rate));
                const float amp = juce::Decibels::decibelsToGain (-23.0f);

                for (int i = 0; i < tone.getNumSamples(); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        tone.setSample (ch, i, amp * std::sin (pi2 * 1000.0f * (float) i / (float) rate));

                const auto r = measure (tone);
                check (std::abs (r.integratedLufs + 23.0f) < 0.1f && std::abs (r.shortTermLufs + 23.0f) < 0.1f,
                       "the meter reads EBU's 1 kHz tone at -23 dBFS as " + juce::String (r.integratedLufs, 2) + " LUFS");
            }

            // The limiter: loud noise pushed 12 dB harder comes out under a -1 dB ceiling, true peak too
            {
                juce::AudioBuffer<float> noise (2, (int) (10 * rate));
                juce::Random random (7);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < noise.getNumSamples(); ++i)
                        noise.setSample (ch, i, 0.5f * (random.nextFloat() * 2.0f - 1.0f) * (0.6f + 0.4f * std::sin (pi2 * (float) i / (float) rate)));

                const auto out = measure (through (noise, 12.0f, -1.0f));
                check (out.samplePeakDb <= -1.0f + 0.01f && out.truePeakDb <= -1.0f + 0.2f,
                       "noise pushed 12 dB into a -1 dB ceiling peaks at " + juce::String (out.samplePeakDb, 2) + " dB, true peak "
                         + juce::String (out.truePeakDb, 2) + " dBTP");
            }

            // A peak between the samples: a tone at a quarter of the sample rate, a half sample off,
            // whose samples stay 3 dB under where it really peaks
            {
                juce::AudioBuffer<float> between (2, (int) (5 * rate));

                for (int i = 0; i < between.getNumSamples(); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        between.setSample (ch, i, 0.97f * std::sin (pi2 * 0.25f * (float) i + pi2 / 8.0f));

                const auto in = measure (between);
                const auto out = measure (through (between, 0.0f, -1.0f));
                check (in.samplePeakDb < -2.5f && in.truePeakDb > -0.5f && out.truePeakDb <= -1.0f + 0.2f,
                       "a peak between the samples (samples at " + juce::String (in.samplePeakDb, 1) + " dB, really " + juce::String (in.truePeakDb, 1)
                         + " dBTP) is brought under the ceiling too: " + juce::String (out.truePeakDb, 2) + " dBTP");
            }

            // A quiet signal goes through untouched, just late by the latency it reports
            {
                juce::AudioBuffer<float> quiet (2, (int) rate);

                for (int i = 0; i < quiet.getNumSamples(); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        quiet.setSample (ch, i, 0.1f * std::sin (pi2 * 440.0f * (float) i / (float) rate + (float) ch));

                mastering::Maximizer probe;
                probe.prepare (rate);
                const int latency = probe.getLatencySamples();
                const auto out = through (quiet, 0.0f, -1.0f);
                float worst = 0.0f;

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = latency; i < quiet.getNumSamples(); ++i)
                        worst = std::max (worst, std::abs (out.getSample (ch, i) - quiet.getSample (ch, i - latency)));

                check (worst < 1.0e-5f, "a quiet tone comes through untouched, " + juce::String (latency) + " samples late (as reported); worst difference "
                                          + juce::String (worst, 8));
            }

            // The shaping: Tone, Punch and Width, each doing what it says and nothing when left alone
            {
                auto shapeThrough = [rate] (juce::AudioBuffer<float> b, const Song::Mastering& m)
                {
                    mastering::Shaper shaper;
                    shaper.prepare (rate);
                    shaper.set (m);

                    for (int pos = 0; pos < b.getNumSamples(); pos += 512)
                        shaper.process (b.getWritePointer (0, pos), b.getWritePointer (1, pos), std::min (512, b.getNumSamples() - pos));

                    return b;
                };

                auto tone = [rate, pi2] (float hz, float amp, bool sidesOnly)
                {
                    juce::AudioBuffer<float> b (2, (int) (2 * rate));

                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        const float v = amp * std::sin (pi2 * hz * (float) i / (float) rate);
                        b.setSample (0, i, v);
                        b.setSample (1, i, sidesOnly ? -v : v);
                    }

                    return b;
                };

                const int settle = (int) (0.5 * rate);
                auto levelDb = [settle] (const juce::AudioBuffer<float>& b, int ch = 0)
                {
                    return juce::Decibels::gainToDecibels (b.getRMSLevel (ch, settle, b.getNumSamples() - settle));
                };

                auto change = [&] (const juce::AudioBuffer<float>& in, const Song::Mastering& m, int ch = 0)
                {
                    return levelDb (shapeThrough (in, m), ch) - levelDb (in, ch);
                };

                // Left alone: untouched, to the bit
                {
                    const auto in = tone (440.0f, 0.3f, false);
                    const auto out = shapeThrough (in, {});
                    float worst = 0.0f;

                    for (int i = 0; i < in.getNumSamples(); ++i)
                        worst = std::max (worst, std::abs (out.getSample (0, i) - in.getSample (0, i)));

                    check (worst == 0.0f, "the shaping left at its defaults doesn't touch the sound");
                }

                // Tone: +6 dB of bass lifts 50 Hz and leaves 2 kHz; air on the sides only lifts the sides
                {
                    Song::Mastering bass;
                    bass.toneDb[0] = 6.0f;
                    const float low = change (tone (50.0f, 0.2f, false), bass), mid = change (tone (2000.0f, 0.2f, false), bass);

                    Song::Mastering air;
                    air.toneDb[3] = 6.0f;
                    air.toneWhere[3] = 2;
                    const float centre = change (tone (15000.0f, 0.2f, false), air), sides = change (tone (15000.0f, 0.2f, true), air);

                    check (low > 4.5f && std::abs (mid) < 0.3f && std::abs (centre) < 0.2f && sides > 4.5f,
                           "Tone: Bass +6 lifts 50 Hz by " + juce::String (low, 1) + " dB and 2 kHz by " + juce::String (mid, 1)
                             + "; Air +6 on the sides lifts the sides by " + juce::String (sides, 1) + " dB and the centre by " + juce::String (centre, 1));
                }

                // Punch: loud bursts and quiet stretches come closer, the level about where it was
                {
                    juce::AudioBuffer<float> bursts (2, (int) (6 * rate));
                    juce::Random random (3);

                    for (int i = 0; i < bursts.getNumSamples(); ++i)
                    {
                        const float amp = ((i / (int) (0.5 * rate)) % 2 == 0) ? 0.5f : 0.05f;
                        const float v = amp * (random.nextFloat() * 2.0f - 1.0f);
                        bursts.setSample (0, i, v);
                        bursts.setSample (1, i, v);
                    }

                    auto spread = [rate] (const juce::AudioBuffer<float>& b)
                    {
                        // Loud halves against quiet ones, after the first two seconds
                        double loud = 0.0, quiet = 0.0;

                        for (int i = (int) (2 * rate); i < b.getNumSamples(); ++i)
                            (((i / (int) (0.5 * rate)) % 2 == 0) ? loud : quiet) += (double) b.getSample (0, i) * b.getSample (0, i);

                        return 10.0 * std::log10 (loud / quiet);
                    };

                    Song::Mastering punchy;
                    punchy.punchAmount = 1.0f;
                    const auto out = shapeThrough (bursts, punchy);
                    const double before = spread (bursts), after = spread (out);
                    const float level = levelDb (out) - levelDb (bursts);

                    Song::Mastering split;
                    split.punchAmount = 0.002f;
                    juce::AudioBuffer<float> noise (2, (int) (3 * rate));

                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < noise.getNumSamples(); ++i)
                            noise.setSample (ch, i, 0.05f * (random.nextFloat() * 2.0f - 1.0f));

                    const float flat = change (noise, split);

                    check (before - after > 3.0 && std::abs (level) < 2.0f && std::abs (flat) < 0.1f,
                           "Punch: loud and quiet come " + juce::String (before - after, 1) + " dB closer (from " + juce::String (before, 1)
                             + "), the level moving " + juce::String (level, 1) + " dB; its three bands add back up flat ("
                             + juce::String (flat, 2) + " dB)");
                }

                // Width: none makes it mono, twice doubles the sides; bass in the middle takes the low sides away only
                {
                    Song::Mastering mono;
                    mono.width = 0.0f;
                    const auto squashed = shapeThrough (tone (1000.0f, 0.2f, true), mono);

                    Song::Mastering wide;
                    wide.width = 2.0f;
                    const float wider = change (tone (1000.0f, 0.2f, true), wide);

                    Song::Mastering bassIn;
                    bassIn.monoBassHz = 120.0f;
                    const float lowSides = change (tone (50.0f, 0.2f, true), bassIn), highSides = change (tone (1000.0f, 0.2f, true), bassIn);
                    const float lowCentre = change (tone (50.0f, 0.2f, false), bassIn);

                    check (squashed.getRMSLevel (0, settle, squashed.getNumSamples() - settle) < 1.0e-6f && std::abs (wider - 6.02f) < 0.1f
                             && lowSides < -20.0f && std::abs (highSides) < 0.3f && std::abs (lowCentre) < 0.01f,
                           "Width: 0 is mono, 200% is " + juce::String (wider, 1) + " dB more side; mono bass below 120 Hz takes 50 Hz off the sides by "
                             + juce::String (-lowSides, 0) + " dB, leaving 1 kHz (" + juce::String (highSides, 2) + ") and the centre's 50 Hz ("
                             + juce::String (lowCentre, 2) + ")");
                }
            }

            // The settings save with the song; a project from before has the safe limiter
            {
                Song song;
                song.loadDemo();
                song.mastering = { "streaming", 7.5f, -1.5f };
                song.mastering.toneDb[1] = -2.5f;
                song.mastering.toneWhere[1] = 1;
                song.mastering.punchAmount = 0.6f;
                song.mastering.punchShare[2] = 0.4f;
                song.mastering.widthOn = false;
                song.mastering.width = 1.3f;
                song.mastering.monoBassHz = 110.0f;
                Song back;
                back.fromVar (song.toVar());
                Song old;
                auto v = song.toVar();
                v.getDynamicObject()->removeProperty ("mastering");
                old.fromVar (v);
                check (back.mastering == song.mastering && old.mastering == Song::Mastering(),
                       "mastering settings (the target, push, ceiling, Tone, Punch and Width) save and load; an older project gets the safe limiter it had");
            }

            // The demo through the engine
            Song song;
            song.loadDemo();
            song.mode = PlayMode::song;
            AudioEngine audio (song, false);
            audio.syncNow();
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-masteringtest");
            folder.deleteRecursively();
            folder.createDirectory();

            // The master volume all the way up drives the limiter instead of going over it
            {
                song.masterVolume = 1.0f;
                song.notify (Song::Change::mixer);
                const auto file = folder.getChildFile ("master-up.wav");
                audio.renderToFile (file);
                const auto r = mastering::measureFile (file);
                check (r.truePeakDb <= -1.0f + 0.2f, "with the master volume at +" + juce::String (masterVolumeToDb (1.0f), 1)
                                                       + " dB the demo still peaks at " + juce::String (r.truePeakDb, 2) + " dBTP (ceiling -1)");
                song.masterVolume = 0.8f;
                song.notify (Song::Change::mixer);
            }

            // "Set loudness" for each target: a capture, the push worked out from it, then the real thing
            for (auto id : { "streaming", "loud" })
            {
                const auto* target = mastering::findTarget (id);
                const auto capture = folder.getChildFile ("capture.wav");
                const auto started = juce::Time::getMillisecondCounterHiRes();
                const bool captured = audio.captureMix (capture);
                const auto capturedAt = juce::Time::getMillisecondCounterHiRes();
                const auto push = mastering::findPush (capture, target->lufs, target->ceilingDb);
                const auto foundAt = juce::Time::getMillisecondCounterHiRes();

                song.mastering = { target->id, push.pushDb, target->ceilingDb };
                song.notify (Song::Change::mixer);
                const auto file = folder.getChildFile (juce::String (id) + ".wav");
                audio.renderToFile (file);
                const auto r = mastering::measureFile (file);

                std::cout << "      " << id << ": " << juce::String (push.beforeLufs, 1) << " LUFS as mixed, push " << juce::String (push.pushDb, 2)
                          << " dB, squeezed up to " << juce::String (push.squeezeDb, 1) << " dB; capture "
                          << juce::String ((capturedAt - started) / 1000.0, 1) << " s, working out " << juce::String ((foundAt - capturedAt) / 1000.0, 1) << " s" << std::endl;

                check (captured && push.ok && std::abs (r.integratedLufs - target->lufs) < 0.5f && r.truePeakDb <= target->ceilingDb + 0.2f,
                       juce::String (target->name) + ": the export comes out at " + juce::String (r.integratedLufs, 2) + " LUFS (aiming for "
                         + juce::String (target->lufs, 0) + "), true peak " + juce::String (r.truePeakDb, 2) + " dBTP");
            }

            // 16-bit: dithered, and the file measures as the export's report would say
            {
                const auto file = folder.getChildFile ("sixteen.wav");
                audio.renderToFile (file, 2.0, 16);
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
                const auto r = mastering::measureFile (file);
                check (reader != nullptr && reader->bitsPerSample == 16 && r.integratedValid && std::abs (r.integratedLufs + 9.0f) < 0.6f,
                       "a 16-bit export is 16-bit (" + juce::String (reader != nullptr ? (int) reader->bitsPerSample : 0) + ") and measures "
                         + juce::String (r.integratedLufs, 2) + " LUFS");
            }

            // The Master tab, used as it would be: F12, a click on Streaming, Set loudness, undo, back to safe
            {
                MainComponent main (song, audio);
                main.setSize (1600, 940);
                main.setVisible (true);
                auto& master = main.getMaster();
                const auto pushBefore = song.mastering.pushDb;

                main.keyPressed (juce::KeyPress (juce::KeyPress::F12Key));
                const bool shown = main.isPanelVisible (MainComponent::Panel::master);

                const auto row = master.getTargetBounds ("streaming").getCentre().toFloat();
                const auto now = juce::Time::getCurrentTime();
                const juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(), row, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                              1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &master, &master, now, row, now, 1, false);
                master.mouseDown (click);
                check (shown && song.mastering.target == "streaming" && master.getSetButton().isEnabled()
                         && master.getSetButton().getButtonText() == "Set loudness for Streaming",
                       "F12 shows the Master tab; clicking Streaming picks it, and Set loudness is ready for it");

                master.getSetButton().triggerClick();

                for (int i = 0; i < 2400 && (i < 4 || master.isWorking()); ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

                const auto pushSet = song.mastering.pushDb;
                const auto file = folder.getChildFile ("from-the-tab.wav");
                audio.renderToFile (file);
                const auto r = mastering::measureFile (file);
                check (! master.isWorking() && master.getStatus().startsWith ("Done") && std::abs (r.integratedLufs + 14.0f) < 0.5f
                         && std::abs (master.getLouderKnob().getValue() - pushSet) < 0.01,
                       "Set loudness from the tab lands the song at " + juce::String (r.integratedLufs, 2) + " LUFS (push "
                         + juce::String (pushSet, 2) + " dB, the knob showing it): \"" + master.getStatus().upToFirstOccurrenceOf (".", true, false) + "\"");

                song.undo();
                check (std::abs (song.mastering.pushDb - pushBefore) < 0.01f, "one undo puts the push back as it was (" + juce::String (song.mastering.pushDb, 2) + " dB)");

                master.chooseTarget ("safe");
                check (song.mastering.pushDb == 0.0f && ! master.getSetButton().isEnabled(),
                       "back to just keeping it from clipping: no push, and nothing for Set loudness to do");

                mastering::Readings hot;
                hot.integratedValid = true;
                hot.integratedLufs = -8.2f;
                hot.truePeakDb = -0.3f;
                const auto report = MainComponent::describeExport (juce::File ("C:\\Music\\Song.wav"), 16, true, hot);
                check (report.contains ("16-bit with dither") && report.contains ("-8.2 LUFS") && report.contains ("above -1 dB"),
                       "the export's report says how it was saved, how loud it came out, and warns of a peak above -1 dB");
            }

            // The second page, used as it would be: a knob, a band's Everything / Centre / Sides, a
            // switch, Width all the way down (then the export is mono), and undo
            {
                MainComponent main (song, audio);
                main.setSize (1600, 940);
                main.setVisible (true);
                main.showMasterPage (1);
                auto& master = main.getMaster();
                const bool shown = master.getPage() == 1 && master.getToneKnob (0).isVisible() && ! master.getSetButton().isVisible();

                auto clickOn = [&master] (juce::Rectangle<int> area)
                {
                    const auto at = area.getCentre().toFloat();
                    const auto now = juce::Time::getCurrentTime();
                    const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                              1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &master, &master, now, at, now, 1, false);
                    master.mouseDown (e);
                };

                master.getToneKnob (3).setValue (3.0, juce::sendNotificationSync);
                clickOn (master.getToneWhereBounds (3));
                const bool airOnCentre = std::abs (song.mastering.toneDb[3] - 3.0f) < 0.01f && song.mastering.toneWhere[3] == 1;
                clickOn (master.getSwitchBounds (0));
                const bool toneOff = ! song.mastering.toneOn;
                master.getPunchKnob().setValue (0.4, juce::sendNotificationSync);
                master.getWidthKnob().setValue (0.0, juce::sendNotificationSync);

                const auto file = folder.getChildFile ("mono.wav");
                audio.renderToFile (file);
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                float widest = 1.0f;

                if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr)
                {
                    juce::AudioBuffer<float> b ((int) reader->numChannels, (int) reader->lengthInSamples);
                    reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                    widest = 0.0f;

                    for (int i = 0; i < b.getNumSamples(); ++i)
                        widest = std::max (widest, std::abs (b.getSample (0, i) - b.getSample (1, i)));
                }

                check (shown && airOnCentre && toneOff && std::abs (song.mastering.punchAmount - 0.4f) < 0.001f && widest < 1.0e-4f,
                       "the second page: Air +3 dB set to the centre, Tone switched off, Punch 40%, Width 0 - and the export is mono (L and R "
                         + juce::String (widest, 6) + " apart)");

                song.undo();
                check (song.mastering.toneOn && song.mastering.toneWhere[3] == 1, "undo brings Tone back on, Air still on the centre");
                song.mastering = {};
                song.notify (Song::Change::mixer);
            }

            folder.deleteRecursively();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--startscreentest"))
        {
            // The start screen and templates: a blank project is one empty track; each template
            // builds the tracks its card shows (icons and all), unsaved, at its tempo; clicking a
            // card (a real click, delivered to whatever's under it) makes that project and closes
            // the screen; New brings it back over a blank project and Esc leaves that; a recent
            // project opens; the startup switch sets the option
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            Song song;
            song.loadBlank();
            check (song.numTracks() == 1 && song.channels.empty() && song.patterns.size() == 1 && ! song.hasUnsavedChanges(),
                   "a blank project is one empty track, one pattern, no channels, nothing unsaved");

            AudioEngine audio (song, false);
            audio.syncNow();

            for (auto& t : templates::all())
            {
                templates::build (t.id, song, audio);
                juce::StringArray icons;
                bool tied = true;

                for (int row = 0; row < song.numTracks(); ++row)
                {
                    icons.add (trackicons::forTrack (song, row));
                    tied = tied && (t.id == "empty" || song.findChannel (song.tracks[(size_t) row].channelId) != nullptr);
                }

                // It plays: a block through the engine with the template's channels
                audio.syncNow();
                juce::AudioBuffer<float> buffer (2, 512);
                audio.processHostedBlock (buffer);

                check (icons == t.icons && tied && std::abs (song.bpm - t.bpm) < 0.01 && ! song.hasUnsavedChanges(),
                       t.name + ": " + icons.joinIntoString (", ") + " (the card shows " + t.icons.joinIntoString (", ") + ") at "
                         + juce::String (song.bpm) + " bpm, each track its channel's, unsaved");
            }

            templates::build ("songwriter", song, audio);
            {
                int armed = 0;
                juce::String armedName;

                for (auto& c : song.channels)
                    if (c.armed)
                    {
                        ++armed;
                        armedName = c.name;
                    }

                check (armed == 1 && armedName == "Guitar", "Songwriter arms its Guitar track (only), so Record and Space record it");
            }

            // The studio, with the screen over a blank project as NightShift opens
            song.loadBlank();
            MainComponent main (song, audio);
            main.setSize (1600, 940);
            main.setVisible (true);     // or nothing is found under the mouse

            auto click = [&main] (juce::Component& on, juce::Point<float> local)
            {
                const auto at = main.getLocalPoint (&on, local);
                auto* target = main.getComponentAt (at.toInt());

                if (target == nullptr)
                    return;

                const auto now = juce::Time::getCurrentTime();
                const auto p = target->getLocalPoint (&main, at);
                const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), p,
                                          juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          target, target, now, p, now, 1, false);
                target->mouseDown (e);
                target->mouseUp (e);
            };

            for (int i = 0; i < 20; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

            check (! song.hasUnsavedChanges(), "opened on a blank project, there is nothing to save (closing won't ask)");

            auto& screen = main.getStartScreen();
            main.showStartScreen();
            check (main.isStartScreenVisible() && main.getComponentAt (screen.getCard().getCentre()) == &screen,
                   "the start screen shows over the studio and takes the clicks");

            // The first time: NightShift is free, would you donate? (the page "opens" here, not in a browser)
            juce::String opened;
            links::opener = [&opened] (const juce::URL& page) { opened = page.toString (false); };

            if (donate::isSet())
            {
                check (screen.isDonationShown() && ! screen.getDonateBounds().isEmpty() && audio.getOptions().donationAsked,
                       "the first time it shows, a banner asks for a donation, and that it's asked is remembered");

                click (screen, screen.getDonateBounds().getCentre().toFloat());
                check (opened == donate::url && screen.isDonationThanked() && screen.getDonateBounds().isEmpty() && main.isStartScreenVisible(),
                       "Donate opens the page (" + opened + ") and the banner says thank you, the screen staying up");
            }
            else
                check (! screen.isDonationShown() && screen.getDonateBounds().isEmpty(),
                       "with no donation page set (donate::url empty), no banner asks for one");

            click (screen, screen.getTemplateBounds ("beat").getCentre().toFloat());
            for (int i = 0; i < 20; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

            check (! main.isStartScreenVisible() && song.numTracks() == 3 && song.channels.size() == 3 && std::abs (song.bpm - 90.0) < 0.01
                     && ! song.hasUnsavedChanges(),
                   "clicking Beat makes the Beat project (3 tracks at 90 bpm, nothing unsaved yet) and closes the screen");

            main.newProject();
            check (main.isStartScreenVisible() && song.numTracks() == 1 && song.channels.empty(),
                   "New (with nothing unsaved) shows it again, over a blank project");
            check (! screen.isDonationShown(), "and doesn't ask for a donation again: it asks once");

            opened = {};
            screen.setDonationShown (true);
            click (screen, screen.getNoThanksBounds().getCentre().toFloat());
            check (! screen.isDonationShown() && main.isStartScreenVisible() && opened.isEmpty(),
                   "No thanks takes the banner away, opening nothing; the screen stays");

            screen.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            check (! main.isStartScreenVisible() && song.numTracks() == 1 && song.isTrackEmpty (0),
                   "Esc closes it, leaving the blank project: one empty track");

            // Recent projects
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-startscreentest.nsproj");
            {
                Song demo;
                demo.loadDemo();
                demo.bpm = 97.0;
                demo.save (file);
            }

            main.rememberProject (file);
            main.rememberProject (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("no such project.nsproj"));
            main.showStartScreen();
            const auto row = screen.getRecentBounds (0);
            const bool missingSkipped = screen.getRecentBounds (1).isEmpty();
            click (screen, row.getCentre().toFloat());
            check (! row.isEmpty() && missingSkipped && ! main.isStartScreenVisible() && std::abs (song.bpm - 97.0) < 0.01
                     && main.getWindowTitle().contains ("nightshift-startscreentest"),
                   "a recent project is listed (one that's gone isn't) and clicking it opens it");

            // The startup switch
            main.showStartScreen();
            const bool before = audio.getOptions().startScreen;
            click (screen, screen.getStartupSwitchBounds().getCentre().toFloat());
            const bool after = audio.getOptions().startScreen;
            click (screen, screen.getCloseBounds().getCentre().toFloat());
            check (before && ! after && ! main.isStartScreenVisible(), "the switch turns showing it at startup off; the cross closes it");

            // About: Donate..., Licence... and Close (each pressed as clicking it does, by ending
            // the dialog with its number)
            auto pressInAbout = [&main] (int button)
            {
                main.showAbout();

                for (int i = 0; i < 20; ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

                juce::AlertWindow* about = nullptr;
                auto* modal = juce::ModalComponentManager::getInstance();

                for (int i = 0; i < modal->getNumModalComponents(); ++i)
                    if (auto* w = dynamic_cast<juce::AlertWindow*> (modal->getModalComponent (i)); w != nullptr && w->getName() == "NightShift " JUCE_APPLICATION_VERSION_STRING)
                        about = w;

                const int buttons = about != nullptr ? about->getNumButtons() : 0;

                if (about != nullptr)
                    about->exitModalState (button);

                for (int i = 0; i < 20; ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

                return buttons;
            };

            opened = {};
            const int aboutButtons = pressInAbout (1);
            check (aboutButtons == (donate::isSet() ? 3 : 2) && opened == donate::url,
                   donate::isSet() ? "About has Donate..., Licence... and Close, and Donate opens the page"
                                   : "About has Licence... and Close (no donation page set, so no Donate...)");

            opened = {};
            pressInAbout (2);
            const bool licenceBeside = licence::url().isLocalFile();     // beside the exe, or in a Mac bundle's Resources
            check (licenceBeside && opened.endsWith ("LICENSE.txt"),
                   "Licence... opens the GPL's text, LICENSE.txt beside the exe (" + opened.fromLastOccurrenceOf ("/", false, false) + ")");

            links::opener = nullptr;
            file.deleteFile();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--looptest"))
        {
            // Plays from a fresh start round the playlist loop and round a pattern, logging where
            // the playhead is once playback begins and where it lands each time it comes round
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            struct Run
            {
                juce::String name;
                std::function<void (Song&, AudioEngine&)> setUp;
                int loopFrom, loopTo;       // ticks the playhead should stay within
                int expectStart;
            };

            auto loopOver = [] (Song& song, int fromBar, int toBar)
            {
                song.mode = PlayMode::song;
                song.loopStart = fromBar * ticksPerBar;
                song.loopEnd = toBar * ticksPerBar;
                song.loopOn = true;
                song.notify (Song::Change::transport);
            };

            const std::vector<Run> runs {
                { "song loop over bars 1-4", [&] (Song& s, AudioEngine&) { loopOver (s, 0, 4); }, 0, 4 * ticksPerBar, 0 },
                { "song loop over bars 1-4, playhead left at bar 2", [&] (Song& s, AudioEngine& a)
                  {
                      a.setPlayheadTicks (ticksPerBar);     // a click on the ruler, as it was before the loop went on
                      loopOver (s, 0, 4);
                  }, 0, 4 * ticksPerBar, 0 },
                { "song loop over bars 5-8, playhead at bar 1", [&] (Song& s, AudioEngine&) { loopOver (s, 4, 8); }, 4 * ticksPerBar, 8 * ticksPerBar, 4 * ticksPerBar },
                { "pattern mode, Keys (4 bars)", [] (Song& s, AudioEngine&)
                  {
                      s.mode = PlayMode::pattern;
                      for (auto& p : s.patterns)
                          if (p.name == "Keys")
                              s.currentPatternId = p.id;
                      s.notify (Song::Change::transport);
                      s.notify (Song::Change::currentPattern);
                  }, 0, 4 * ticksPerBar, 0 },
                { "song mode, no loop", [] (Song& s, AudioEngine&)
                  {
                      s.mode = PlayMode::song;
                      s.loopOn = false;
                      s.notify (Song::Change::transport);
                  }, 0, 1 << 30, 0 },
            };

            const double sr = 48000.0;
            const int block = 512;

            for (auto& run : runs)
            {
                Song song;
                song.loadDemo();
                AudioEngine audio (song, false);
                audio.syncNow();
                run.setUp (song, audio);
                audio.syncNow();

                for (int pass = 1; pass <= 2; ++pass)
                {
                    audio.togglePlay();
                    juce::AudioBuffer<float> buffer (2, block);
                    double first = -1.0, previous = -1.0;
                    std::vector<double> landings;
                    const double loopSeconds = std::min (20.0, (run.loopTo - run.loopFrom) / (double) ticksPerBeat * 60.0 / song.bpm);

                    for (int b = 0; b < (int) (sr * loopSeconds * 2.3) / block; ++b)
                    {
                        buffer.clear();
                        audio.processHostedBlock (buffer);

                        // Now and then: a 1 ms wait lasts a whole Windows timer tick (~16 ms)
                        // unless something has raised the timer resolution, so pumping every
                        // block made this test take minutes
                        if (b % 64 == 0)
                            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

                        auto context = audio.getEdit().getCurrentPlaybackContext();
                        const double ticks = context != nullptr ? context->getPosition().inSeconds() * song.bpm / 60.0 * ticksPerBeat : -1.0;

                        if (first < 0.0)
                            first = ticks;
                        else if (ticks < previous - ticksPerBeat)
                            landings.push_back (ticks);

                        previous = ticks;
                    }

                    audio.stop();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

                    juce::StringArray landed;

                    for (auto t : landings)
                        landed.add (juce::String (t / ticksPerBar + 1.0, 2));

                    std::cout << "      " << run.name << ", play " << pass << ": starts at bar " << juce::String (first / ticksPerBar + 1.0, 2)
                              << (landed.isEmpty() ? juce::String() : ", comes round to bar " + landed.joinIntoString (", bar ")) << std::endl;

                    const double tolerance = block / sr * song.bpm / 60.0 * ticksPerBeat * 2.0;
                    bool landsRight = true;

                    for (auto t : landings)
                        landsRight = landsRight && std::abs (t - run.loopFrom) < tolerance;

                    check (std::abs (first - run.expectStart) < tolerance && landsRight,
                           run.name + ", play " + juce::String (pass) + ": starts at bar " + juce::String (run.expectStart / ticksPerBar + 1)
                               + " and comes round to bar " + juce::String (run.loopFrom / ticksPerBar + 1));
                }
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--editingtest"))
        {
            // The piano roll's editing, with real mouse events and keys: a box selects, Shift+click
            // adds a note, the selection moves and stretches together, Ctrl+A/C/V/B select,
            // copy, paste and duplicate, Delete, Ctrl+Q quantizes (the selected notes, or all),
            // the arrows transpose and nudge, a selected stem moves every selected velocity,
            // Ctrl+drag selects with the draw tool, Escape lets go, and undo puts it back
            Song song;
            song.loadBlank();
            const int leadId = song.addChannel ("Lead", InstrumentType::synth).id;
            song.selectedChannelId = leadId;
            const int bar = song.ticksPerBar(), beat = song.ticksPerMeterBeat();
            song.currentPattern().notesFor (leadId) = { { 0, ticksPerStep, 60, 100 }, { beat, ticksPerStep, 62, 90 },
                                                        { 2 * beat, ticksPerStep, 64, 100 }, { 3 * beat, ticksPerStep, 65, 100 } };
            song.notify (Song::Change::everything);
            AudioEngine audio (song, false);
            PianoRoll roll (song, audio);
            roll.setSize (1000, 500);
            juce::String toast;
            roll.onMessage = [&toast] (const juce::String& m) { toast = m; };

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            // The notes are looked up each time: undo rebuilds the song's patterns
            auto list = [&song, leadId]() -> std::vector<Note>& { return song.currentPattern().notesFor (leadId); };
            auto starts = [&] { std::vector<int> s; for (auto& n : list()) s.push_back (n.start); return s; };
            auto selectedStarts = [&] { std::vector<int> s; for (int i : roll.getSelection()) s.push_back (list()[(size_t) i].start); return s; };
            const auto cmd = juce::ModifierKeys::commandModifier, shift = juce::ModifierKeys::shiftModifier;
            auto key = [&roll] (int code, int mods = 0) { return roll.keyPressed (juce::KeyPress (code, juce::ModifierKeys (mods), 0)); };

            auto mouse = [&roll] (juce::Point<float> from, juce::Point<float> to, int extraMods = 0)
            {
                const auto now = juce::Time::getCurrentTime();
                const juce::ModifierKeys mods (juce::ModifierKeys::leftButtonModifier | extraMods);
                auto event = [&] (juce::Point<float> p, bool dragged)
                {
                    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                             &roll, &roll, now, from, now, 1, dragged);
                };

                roll.mouseDown (event (from, false));

                if (from != to)
                    for (int i = 1; i <= 6; ++i)
                        roll.mouseDrag (event (from + (to - from) * (i / 6.0f), true));

                roll.mouseUp (event (to, from != to));
            };

            // A box with the select tool, from empty space between the 2nd and 3rd notes
            roll.setTool (PianoRoll::Tool::select);
            {
                const auto b0 = roll.boundsOfNote (0), b1 = roll.boundsOfNote (1), b2 = roll.boundsOfNote (2);
                mouse ({ (b1.getRight() + b2.getX()) * 0.5f, std::max (b0.getBottom(), b1.getBottom()) + 30.0f },
                       { b0.getX() + 2.0f, std::min (b0.getY(), b1.getY()) - 30.0f });
            }
            check (roll.getSelection() == std::set<int> { 0, 1 } && list().size() == 4, "a box dragged with Select selects the two notes under it, and adds none");

            mouse (roll.boundsOfNote (3).getCentre(), roll.boundsOfNote (3).getCentre(), shift);
            check (roll.getSelection() == std::set<int> { 0, 1, 3 }, "Shift+click adds a note to the selection");
            mouse (roll.boundsOfNote (3).getCentre(), roll.boundsOfNote (3).getCentre(), shift);
            check (roll.getSelection() == std::set<int> { 0, 1 }, "and Shift+click again takes it out");

            // Dragging one selected note with the draw tool takes the other along: a bar (192 px)
            // later, a semitone (14 px) down
            roll.setTool (PianoRoll::Tool::draw);
            {
                const auto grab = roll.boundsOfNote (1).getCentre();
                mouse (grab, grab + juce::Point<float> (192.0f, 14.0f));
            }
            check (list().size() == 4 && list()[2] == Note { bar, ticksPerStep, 59, 100 } && list()[3] == Note { bar + beat, ticksPerStep, 61, 90 }
                       && roll.getSelection() == std::set<int> { 2, 3 },
                   "dragging a selected note moves both a bar later and a semitone down, still selected");

            song.undo();
            check (starts() == std::vector<int> { 0, beat, 2 * beat, 3 * beat } && list()[1].key == 62 && roll.getSelection().empty(),
                   "undo puts them back, and the selection goes");

            // Select all, copy, paste at bar 3, delete that copy, paste with the mouse elsewhere
            check (key ('a', cmd) && roll.getSelection().size() == 4, "Ctrl+A selects every note");
            check (key ('c', cmd), "Ctrl+C copies them");
            check (roll.edit (EditCommand::paste, 2 * bar) && list().size() == 8
                       && selectedStarts() == std::vector<int> { 2 * bar, 2 * bar + beat, 2 * bar + 2 * beat, 2 * bar + 3 * beat },
                   "pasting at bar 3 puts a copy there, selected");
            check (key (juce::KeyPress::deleteKey) && list().size() == 4 && roll.getSelection().empty(), "Delete deletes the selected notes");
            check (key ('v', cmd) && list().size() == 8 && selectedStarts() == std::vector<int> { bar, bar + beat, bar + 2 * beat, bar + 3 * beat },
                   "Ctrl+V with the mouse elsewhere pastes right after the bar it copied");
            check (key ('b', cmd) && list().size() == 12
                       && selectedStarts() == std::vector<int> { 2 * bar, 2 * bar + beat, 2 * bar + 2 * beat, 2 * bar + 3 * beat },
                   "Ctrl+B duplicates the selected bar into the next one");

            {
                bool pasteOn = false;
                auto menu = roll.createEditMenu();

                for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                    pasteOn = pasteOn || (it.getItem().text == "Paste" && it.getItem().isEnabled);

                check (pasteOn, "the Edit menu offers Paste once something's copied");
            }

            // Quantize: every note when none are selected, then only the selected one
            list() = { { 10, ticksPerStep, 60, 100 }, { 30, ticksPerStep, 62, 100 }, { 50, ticksPerStep, 64, 100 }, { 100, ticksPerStep, 65, 100 } };
            song.notify (Song::Change::notes);
            {
                const bool none = roll.getSelection().empty(), quantized = key ('q', cmd);
                check (none && quantized && starts() == std::vector<int> { 0, 24, 48, 96 } && toast.contains ("4 notes") && toast.contains ("4 moved"),
                       "Ctrl+Q with nothing selected puts every note's start on the 1/16 snap (" + toast + ")");
            }

            list() = { { 10, ticksPerStep, 60, 100 }, { 30, ticksPerStep, 62, 100 } };
            song.notify (Song::Change::notes);
            mouse (roll.boundsOfNote (1).getCentre(), roll.boundsOfNote (1).getCentre(), shift);
            check (key ('q', cmd) && starts() == std::vector<int> { 10, 24 }, "with one note selected, Ctrl+Q moves only that one");

            // The arrows: a semitone, an octave with Shift, a snap step left and right
            key ('a', cmd);
            check (key (juce::KeyPress::upKey) && list()[0].key == 61 && list()[1].key == 63, "Up moves the selected notes a semitone up");
            check (key (juce::KeyPress::upKey, shift) && list()[0].key == 73, "Shift+Up an octave");
            check (key (juce::KeyPress::downKey) && list()[0].key == 72, "Down a semitone down");
            check (key (juce::KeyPress::rightKey) && starts() == std::vector<int> { 34, 48 }, "Right moves them a snap step later");
            check (key (juce::KeyPress::leftKey) && starts() == std::vector<int> { 10, 24 }, "Left moves them a snap step earlier");
            check (key (juce::KeyPress::leftKey) && starts() == std::vector<int> { 0, 14 } && ! key (juce::KeyPress::leftKey)
                       && starts() == std::vector<int> { 0, 14 },
                   "and only as far as the pattern's start");

            // Stretching one selected note's end stretches both
            list() = { { 0, ticksPerStep, 60, 100 }, { beat, ticksPerStep, 62, 80 } };
            song.notify (Song::Change::notes);
            key ('a', cmd);
            {
                const auto end = juce::Point<float> (roll.boundsOfNote (0).getRight() - 2.0f, roll.boundsOfNote (0).getCentreY());
                mouse (end, end + juce::Point<float> (24.0f, 0.0f));
            }
            check (list()[0].length == 3 * ticksPerStep && list()[1].length == 3 * ticksPerStep, "stretching one selected note stretches both by as much");

            // A selected stem in the velocity lane (y 438 to 482 here) moves both velocities by as much
            {
                auto velocityY = [] (int v) { return 482.0f - 44.0f * (float) v / 127.0f; };
                const float stem = roll.boundsOfNote (0).getX() + 0.5f;
                mouse ({ stem, velocityY (100) }, { stem, velocityY (110) });
            }
            check (list()[0].velocity == 110 && list()[1].velocity == 90, "dragging a selected note's stem moves both selected velocities by as much");

            // Ctrl+drag with the draw tool selects instead of writing a note; Escape lets go
            key (juce::KeyPress::escapeKey);
            {
                // A narrow box over the first note only (the second starts further right)
                const auto b = roll.boundsOfNote (0);
                mouse ({ b.getX() + 20.0f, b.getBottom() + 30.0f }, { b.getX() + 1.0f, b.getY() - 30.0f }, cmd);
            }
            check (list().size() == 2 && roll.getSelection() == std::set<int> { 0 }, "Ctrl+drag on empty space with the draw tool selects, and writes nothing");
            check (key (juce::KeyPress::escapeKey) && roll.getSelection().empty(), "Escape lets go of the selection");

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--arrangeedittest"))
        {
            // Arrange's editing with real mouse events and keys, and what the engine then plays: a
            // clip split with the split tool and its second half moved plays only its own part
            // (a render's kicks), a box selects clips, the selection moves together and undoes,
            // copy and paste keep a split clip's part, Ctrl+B duplicates, Ctrl+T splits at the
            // song position, either end trims, Delete deletes, it all saves; a take folder splits,
            // trims and grows back, its dots drag out fades, and a render fades in and out
            Song song;
            song.loadEmpty();
            song.bpm = 120.0;
            song.mode = PlayMode::song;
            const int kick = song.channels.front().id;
            const int bar = song.ticksPerBar(), beat = song.ticksPerMeterBeat();
            const int patternId = song.currentPattern().id;

            // A two-bar pattern: a kick at the top of each bar, as one clip on the first of four tracks
            song.currentPattern().notesFor (kick) = { { 0, ticksPerStep, defaultRootKey, 127 }, { bar, ticksPerStep, defaultRootKey, 127 } };

            for (int i = 0; i < 4; ++i)
                song.insertTrack ({});

            song.playlist.push_back ({ patternId, 0, 0 });
            song.notify (Song::Change::everything);
            AudioEngine audio (song, false);
            audio.syncNow();
            Playlist playlist (song, audio);
            playlist.setSize (1600, 800);
            juce::String toast;
            playlist.onMessage = [&toast] (const juce::String& m) { toast = m; };

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            // The default layout: no scroll, a bar 72 px, rows 54 px under a 30 px ruler
            const float pxPerTick = 72.0f / bar;
            auto xOf = [&] (int tick) { return (float) Playlist::headerWidth + tick * pxPerTick; };
            auto yOf = [] (int row) { return (float) (Playlist::rulerHeight + row * Playlist::rowHeight + Playlist::rowHeight / 2); };
            const auto cmd = juce::ModifierKeys::commandModifier;
            auto key = [&playlist] (int code, int mods = 0) { return playlist.keyPressed (juce::KeyPress (code, juce::ModifierKeys (mods), 0)); };

            auto mouse = [&playlist] (juce::Point<float> from, juce::Point<float> to, int extraMods = 0)
            {
                const auto now = juce::Time::getCurrentTime();
                const juce::ModifierKeys mods (juce::ModifierKeys::leftButtonModifier | extraMods);
                auto event = [&] (juce::Point<float> p, bool dragged)
                {
                    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                             &playlist, &playlist, now, from, now, 1, dragged);
                };

                playlist.mouseDown (event (from, false));

                if (from != to)
                    for (int i = 1; i <= 6; ++i)
                        playlist.mouseDrag (event (from + (to - from) * (i / 6.0f), true));

                playlist.mouseUp (event (to, from != to));
            };

            // Where the kicks land in a render: the first loud sample after half a second of quiet
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            auto kicks = [&]
            {
                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-arrangeedittest.wav");
                audio.syncNow();
                audio.renderToFile (file, 0.5);
                std::vector<double> found;

                if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr)
                {
                    juce::AudioBuffer<float> b ((int) reader->numChannels, (int) reader->lengthInSamples);
                    reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                    const double rate = reader->sampleRate;
                    int quietFor = (int) rate;

                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        if (std::abs (b.getSample (0, i)) > 0.05f)
                        {
                            if (quietFor > rate / 2)
                                found.push_back (i / rate);

                            quietFor = 0;
                        }
                        else
                        {
                            ++quietFor;
                        }
                    }
                }

                file.deleteFile();
                return found;
            };

            auto describe = [] (const std::vector<double>& times)
            {
                juce::StringArray s;

                for (auto t : times)
                    s.add (juce::String (t, 3));

                return s.joinIntoString (", ") + " s";
            };

            auto heard = kicks();
            check (heard.size() == 2 && std::abs (heard[0]) < 0.02 && std::abs (heard[1] - 2.0) < 0.02, "the whole pattern plays kicks at 0 and 2 s (" + describe (heard) + ")");

            // The split tool at bar 2 (snap: a bar), then the second half dragged to bar 5
            playlist.setTool (Playlist::Tool::split);
            mouse ({ xOf (bar), yOf (0) }, { xOf (bar), yOf (0) });
            check (song.playlist.size() == 2 && song.playlist[0].length == bar && song.playlist[1].start == bar && song.playlist[1].offset == bar
                       && song.clipLength (song.playlist[1]) == bar,
                   "the split tool cuts the clip at bar 2: the second half plays the pattern from a bar in");

            playlist.setTool (Playlist::Tool::draw);
            mouse ({ xOf (bar + bar / 2), yOf (0) }, { xOf (bar + bar / 2) + 3 * 72.0f, yOf (0) });
            check (song.playlist[1].start == 4 * bar && song.playlist[1].offset == bar, "dragging the second half to bar 5 moves it there");

            heard = kicks();
            check (heard.size() == 2 && std::abs (heard[0]) < 0.02 && std::abs (heard[1] - 8.0) < 0.02,
                   "the engine plays the first half's kick at 0 s and the second half's at 8 s, nothing at 2 s (" + describe (heard) + ")");

            // A box with the select tool over both, then dragging one takes both a bar later and a track down
            playlist.setTool (Playlist::Tool::select);
            mouse ({ 1000.0f, yOf (3) }, { xOf (bar / 2), (float) Playlist::rulerHeight + 6.0f });
            check (playlist.getSelectedClips() == std::set<int> { 0, 1 }, "a box dragged with Select selects both clips");

            mouse ({ xOf (bar / 2), yOf (0) }, { xOf (bar / 2) + 72.0f, yOf (1) });
            check (song.playlist[0].start == bar && song.playlist[0].track == 1 && song.playlist[1].start == 5 * bar && song.playlist[1].track == 1,
                   "dragging one selected clip moves both a bar later and down a track");

            song.undo();
            check (song.playlist.size() == 2 && song.playlist[0].start == 0 && song.playlist[0].track == 0 && song.playlist[1].start == 4 * bar
                       && playlist.getSelectedClips().empty(),
                   "undo puts them back, and the selection goes");

            // Copy and paste keep each clip's part of the pattern
            playlist.setTool (Playlist::Tool::draw);
            mouse ({ xOf (4 * bar + bar / 2), yOf (0) }, { xOf (4 * bar + bar / 2), yOf (0) });
            check (playlist.getSelectedClips() == std::set<int> { 1 } && key ('c', cmd), "clicking the second half selects it, and Ctrl+C copies it");
            check (playlist.edit (EditCommand::paste, std::make_pair (8 * bar, 2)) && song.playlist.size() == 3
                       && song.playlist[2].start == 8 * bar && song.playlist[2].track == 2 && song.playlist[2].offset == bar && song.clipLength (song.playlist[2]) == bar,
                   "pasting it at bar 9 on the third track keeps it playing the pattern's second bar");

            // Ctrl+B: a copy of the first half right after it
            mouse ({ xOf (bar / 2), yOf (0) }, { xOf (bar / 2), yOf (0) });
            check (key ('b', cmd) && song.playlist.size() == 4 && song.playlist[3].start == bar && song.playlist[3].track == 0
                       && song.playlist[3].offset == 0 && song.clipLength (song.playlist[3]) == bar && playlist.getSelectedClips() == std::set<int> { 3 },
                   "Ctrl+B duplicates the first half into bar 2, selected");

            // Ctrl+T at the song position (beat 3) splits the selected clip there
            mouse ({ xOf (bar / 4), yOf (0) }, { xOf (bar / 4), yOf (0) });
            audio.setPlayheadTicks (2 * beat);
            {
                const bool split = key ('t', cmd);
                check (split && song.playlist.size() == 5 && song.playlist[0].length == 2 * beat && song.playlist[4].start == 2 * beat
                           && song.playlist[4].offset == 2 * beat && playlist.getSelectedClips() == std::set<int> { 0, 4 } && toast.contains ("beat 3"),
                       "Ctrl+T splits the selected clip at the song position, both halves selected (" + toast + ")");
            }

            // Trimming the duplicate's ends a beat in (snap: a beat; a beat is 18 px)
            playlist.setSnap (beat);
            const float dupLeft = xOf (bar) + 1.0f, dupRight = xOf (2 * bar) - 1.0f;
            mouse ({ dupRight - 2.0f, yOf (0) }, { dupRight - 2.0f - 18.0f, yOf (0) });
            check (song.playlist[3].start == bar && song.clipLength (song.playlist[3]) == bar - beat, "dragging its end a beat in trims a beat off");
            mouse ({ dupLeft + 2.0f, yOf (0) }, { dupLeft + 2.0f + 18.0f, yOf (0) });
            check (song.playlist[3].start == bar + beat && song.playlist[3].offset == beat && song.clipLength (song.playlist[3]) == bar - 2 * beat,
                   "dragging its start a beat in starts it a beat later, a beat into the pattern");

            {
                Song saved;
                saved.fromVar (song.toVar());
                check (saved.playlist.size() == song.playlist.size() && saved.playlist[3].offset == beat && saved.playlist[3].length == bar - 2 * beat
                           && saved.playlist[1].offset == bar,
                       "split and trimmed clips keep their parts when saved and opened");
            }

            check (key (juce::KeyPress::deleteKey) && song.playlist.size() == 4 && playlist.getSelectedClips().empty(), "Delete deletes the selected clip");
            check (key ('a', cmd) && playlist.getSelectedClips().size() == 4 && key (juce::KeyPress::escapeKey) && playlist.getSelectedClips().empty(),
                   "Ctrl+A selects every clip, Escape lets go");

            // A take folder: four seconds of a steady tone on an audio track
            const auto take = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-arrangeedit-take.wav");
            take.deleteFile();
            {
                juce::AudioBuffer<float> tone (1, 48000 * 4);

                for (int i = 0; i < tone.getNumSamples(); ++i)
                    tone.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / 48000.0));

                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (take);
                auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                                  .withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));

                if (writer != nullptr)
                    writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
            }

            const int guitar = song.addChannel ("Guitar", InstrumentType::audio).id;
            {
                AudioClip folder;
                folder.channelId = guitar;
                folder.track = 3;
                folder.length = 4.0;
                folder.takes = { { take.getFullPathName(), 0.0, 0.0, 4.0 } };
                folder.comp = { { 0.0, 4.0, 0 } };
                song.audioClips.push_back (folder);
            }
            song.notify (Song::Change::channels);
            song.notify (Song::Change::playlist);

            check (song.splitAudioClip (0, bar) && song.audioClips.size() == 2, "a take folder splits at bar 2, two seconds in");
            {
                const auto& first = song.audioClips[0];
                const auto& second = song.audioClips[1];
                check (std::abs (first.length - 2.0) < 1.0e-6 && second.start == bar && std::abs (second.length - 2.0) < 1.0e-6
                           && std::abs (second.takes[0].fileOffset - 2.0) < 1.0e-6 && second.comp.size() == 1 && std::abs (second.comp[0].to - 2.0) < 1.0e-6,
                       "the first half is two seconds; the second starts at bar 2, two seconds into the file");
            }

            auto second = song.audioClips[1];
            song.setAudioRange (second, bar + beat, second.length - 0.5);
            check (second.start == bar + beat && std::abs (second.takes[0].fileOffset - 2.5) < 1.0e-6 && second.comp.size() == 1
                       && std::abs (second.comp[0].from) < 1.0e-6 && std::abs (second.length - 1.5) < 1.0e-6,
                   "trimming its start a beat in starts it half a second further into the file");
            song.setAudioRange (second, bar, second.length + 0.5);
            check (second.start == bar && std::abs (second.takes[0].fileOffset - 2.0) < 1.0e-6 && second.comp.size() == 1
                       && std::abs (second.comp[0].from) < 1.0e-6 && std::abs (second.comp[0].to - 2.0) < 1.0e-6,
                   "growing it back out shows that audio again");

            // Its corner dots (the first half: x 217 to 287 on the fourth track; the dots 6 px in,
            // on the top of the waveform, 21 px under the clip's top)
            {
                const float top = (float) (Playlist::rulerHeight + 3 * Playlist::rowHeight + 4);
                mouse ({ xOf (0) + 7.0f, top + 21.0f }, { xOf (0) + 43.0f, top + 21.0f });
                const double expectIn = song.secondsAt ((43.0 - 0.0) / pxPerTick);
                check (std::abs (song.audioClips[0].fadeIn - expectIn) < 0.02, "dragging its fade-in dot right fades it in over "
                                                                                     + juce::String (song.audioClips[0].fadeIn, 2) + " s");
                mouse ({ xOf (bar) - 7.0f, top + 21.0f }, { xOf (bar) - 25.0f, top + 21.0f });
                check (song.audioClips[0].fadeOut > 0.6 && song.audioClips[0].fadeOut < 0.8 && song.audioClips[0].fadeIn + song.audioClips[0].fadeOut <= 2.0,
                       "and its fade-out dot left fades it out over " + juce::String (song.audioClips[0].fadeOut, 2) + " s");
            }

            // What a fade sounds like: the whole tone alone, a second in and a second out (in this
            // song and engine: no second engine in one process)
            {
                AudioClip c;
                c.channelId = guitar;
                c.track = 3;
                c.length = 4.0;
                c.takes = { { take.getFullPathName(), 0.0, 0.0, 4.0 } };
                c.comp = { { 0.0, 4.0, 0 } };
                c.fadeIn = c.fadeOut = 1.0;
                song.playlist.clear();
                song.audioClips = { c };
                song.notify (Song::Change::playlist);
                audio.syncNow();

                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-arrangeedit-fade.wav");
                audio.renderToFile (file, 0.2);
                float early = 0.0f, middle = 0.0f, late = 0.0f;

                if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr)
                {
                    juce::AudioBuffer<float> b ((int) reader->numChannels, (int) reader->lengthInSamples);
                    reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                    const int rate = (int) reader->sampleRate;
                    auto rms = [&] (double from) { return b.getRMSLevel (0, juce::jmin ((int) (from * rate), b.getNumSamples() - rate / 10), rate / 10); };
                    early = rms (0.0);
                    middle = rms (1.8);
                    late = rms (3.9);
                }

                file.deleteFile();
                check (middle > 0.1f && early < 0.4f * middle && late < 0.4f * middle,
                       "a render fades in and out: " + juce::String (early, 3) + " in the first 0.1 s, " + juce::String (middle, 3)
                           + " in the middle, " + juce::String (late, 3) + " in the last 0.1 s");
            }

            take.deleteFile();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--automationtest"))
        {
            // Automation from the song to the sound: a lane's value between its points; renders of
            // a steady tone where a channel's volume fades it out, its pan sweeps it left to right, a
            // filter's cutoff closes on it and the master volume fades the mix; Pattern mode leaving
            // every control where it is, an emptied lane giving the fader back; which lane a knob's
            // parameter is; then Arrange's lanes with real mouse events (adding, dragging, deleting,
            // undo); lanes saved, and gone with their channel
            Song song;
            song.loadBlank();
            song.bpm = 120.0;
            song.mode = PlayMode::song;
            const int bar = song.ticksPerBar();     // two seconds at 120

            const auto take = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-automation-tone.wav");
            take.deleteFile();
            {
                juce::AudioBuffer<float> tone (1, 48000 * 4);

                for (int i = 0; i < tone.getNumSamples(); ++i)
                    tone.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / 48000.0));

                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (take);
                auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                                  .withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));

                if (writer != nullptr)
                    writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
            }

            // Four seconds of it on an audio track, the only track
            const int guitar = song.addChannel ("Guitar", InstrumentType::audio).id;
            {
                AudioClip c;
                c.channelId = guitar;
                c.length = 4.0;
                c.takes = { { take.getFullPathName(), 0.0, 0.0, 4.0 } };
                c.comp = { { 0.0, 4.0, 0 } };
                song.audioClips.push_back (c);
                song.tracks[0].channelId = guitar;
            }

            song.notify (Song::Change::everything);
            AudioEngine audio (song, false);
            audio.syncNow();

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            auto render = [&]
            {
                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-automationtest.wav");
                song.notify (Song::Change::automation);
                audio.syncNow();
                audio.renderToFile (file, 0.2);
                juce::AudioBuffer<float> b;

                if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr)
                {
                    b.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
                    reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                }

                file.deleteFile();
                return b;
            };

            // A tenth of a second's level from `seconds` in, on one side (48 kHz)
            auto rms = [] (const juce::AudioBuffer<float>& b, int channel, double seconds)
            {
                const int from = juce::jlimit (0, std::max (0, b.getNumSamples() - 4800), (int) (seconds * 48000.0));
                return b.getNumSamples() > 0 ? b.getRMSLevel (juce::jmin (channel, b.getNumChannels() - 1), from, 4800) : 0.0f;
            };

            auto db = [] (float level) { return juce::String (juce::Decibels::gainToDecibels (level, -120.0f), 1) + " dB"; };

            {
                AutomationLane lane;
                lane.points = { { 0, 0.0f }, { bar, 1.0f } };
                check (std::abs (lane.valueAt (bar / 2) - 0.5f) < 1.0e-5f && lane.valueAt (-10) == 0.0f && lane.valueAt (3 * bar) == 1.0f,
                       "a lane is a straight line between its points, level before the first and after the last");
            }

            const auto steady = render();
            const float base = rms (steady, 0, 0.5);
            check (base > 0.1f, "the tone renders steadily (" + db (base) + ")");

            // The channel's volume: from 0 dB at the start down to nothing at bar 2 (two seconds in)
            {
                auto& volume = song.laneFor (guitar, "volume", "Volume");
                volume.points = { { 0, tracktion::decibelsToVolumeFaderPosition (0.0f) }, { bar, 0.0f } };
            }

            auto faded = render();
            {
                auto p = audio.findAutomationParameter (guitar, "volume");
                check (p != nullptr && p->getCurve().getNumPoints() == 2, "the lane's two points are on the volume's curve");
            }
            check (rms (faded, 0, 0.0) > 0.8f * base && rms (faded, 0, 1.0) < 0.6f * base && rms (faded, 0, 1.0) > 0.01f * base
                       && rms (faded, 0, 2.5) < 0.01f * base,
                   "a render fades it out: " + db (rms (faded, 0, 0.0)) + " at the start, " + db (rms (faded, 0, 1.0)) + " at 1 s, "
                       + db (rms (faded, 0, 2.5)) + " at 2.5 s");

            // Pattern mode: every control where it is
            song.mode = PlayMode::pattern;
            song.notify (Song::Change::transport);
            audio.syncNow();
            {
                auto p = audio.findAutomationParameter (guitar, "volume");
                check (p != nullptr && p->getCurve().getNumPoints() == 0
                           && std::abs (p->getCurrentValue() - tracktion::decibelsToVolumeFaderPosition (channelVolumeToDb (0.78f))) < 0.01f,
                       "Pattern mode takes the curve off and puts the fader's own value back");
            }
            song.mode = PlayMode::song;
            song.notify (Song::Change::transport);
            audio.syncNow();
            check (audio.findAutomationParameter (guitar, "volume")->getCurve().getNumPoints() == 2, "and Song mode puts it back on");

            // Emptied, the lane gives the fader back
            song.findLane (guitar, "volume")->points.clear();
            auto restored = render();
            check (audio.findAutomationParameter (guitar, "volume")->getCurve().getNumPoints() == 0 && rms (restored, 0, 2.5) > 0.9f * base,
                   "an emptied lane leaves the tone steady again (" + db (rms (restored, 0, 2.5)) + " at 2.5 s)");

            // Pan: hard left at the start to hard right at the end
            song.laneFor (guitar, "pan", "Pan").points = { { 0, -1.0f }, { 2 * bar, 1.0f } };
            auto swept = render();
            check (rms (swept, 0, 0.05) > 3.0f * rms (swept, 1, 0.05) && rms (swept, 1, 3.85) > 3.0f * rms (swept, 0, 3.85),
                   "its pan sweeps it from the left (" + db (rms (swept, 0, 0.05)) + " L, " + db (rms (swept, 1, 0.05)) + " R) to the right ("
                       + db (rms (swept, 0, 3.85)) + " L, " + db (rms (swept, 1, 3.85)) + " R)");
            std::erase_if (song.automation, [] (auto& l) { return l.param == "pan"; });

            // An effect's knob: a low pass whose cutoff closes from 20 kHz to 100 Hz by bar 2
            Effect filter;
            filter.id = song.createId();
            filter.pluginId = stockfx::descriptions()[5].createIdentifierString();
            filter.name = "Nightshift Filter";
            song.findChannel (guitar)->effects.push_back (filter);
            song.notify (Song::Change::mixer);
            audio.syncNow();

            const auto cutoffKey = "fx:" + juce::String (filter.id) + ":cutoff";
            {
                const auto targets = audio.automationTargets (guitar);
                const bool listed = std::any_of (targets.begin(), targets.end(), [&] (auto& t) { return t.param == cutoffKey && t.name == "Cutoff"; });
                check (listed && audio.findAutomationParameter (guitar, cutoffKey) != nullptr
                           && audio.automationName (guitar, cutoffKey) == "Nightshift Filter: Cutoff",
                       "the filter's Cutoff is offered for automation as \"" + audio.automationName (guitar, cutoffKey) + "\"");
            }

            song.laneFor (guitar, cutoffKey, "Nightshift Filter: Cutoff").points = { { 0, 20000.0f }, { bar, 100.0f } };
            auto closed = render();
            check (rms (closed, 0, 0.0) > 0.8f * base && rms (closed, 0, 2.5) < 0.3f * base,
                   "its automated cutoff closes on the tone: " + db (rms (closed, 0, 0.0)) + " at the start, " + db (rms (closed, 0, 2.5)) + " at 2.5 s");

            // Which lane a knob's parameter is (its right-click Automate)
            {
                auto cutoff = audio.findAutomationParameter (guitar, cutoffKey);
                auto volume = audio.findAutomationParameter (guitar, "volume");
                auto master = audio.findAutomationParameter (0, "volume");
                const auto a = cutoff != nullptr ? audio.automationKeyFor (*cutoff) : std::nullopt;
                const auto b = volume != nullptr ? audio.automationKeyFor (*volume) : std::nullopt;
                const auto c = master != nullptr ? audio.automationKeyFor (*master) : std::nullopt;
                check (a && a->first == guitar && a->second == cutoffKey && b && b->second == "volume" && c && c->first == 0 && c->second == "volume",
                       "a knob's parameter finds its lane: the filter's cutoff, the channel's volume, the master's volume");
            }

            std::erase_if (song.automation, [&] (auto& l) { return l.param == cutoffKey; });
            song.findChannel (guitar)->effects.clear();
            song.notify (Song::Change::mixer);

            // The master volume: as it is to bar 2, then down to nothing at the end
            song.laneFor (0, "volume", "Volume").points = { { bar, 0.8f }, { 2 * bar, 0.0f } };
            auto ending = render();
            check (rms (ending, 0, 1.0) > 0.8f * base && rms (ending, 0, 3.85) < 0.1f * base,
                   "the master's automation fades the mix: " + db (rms (ending, 0, 1.0)) + " at 1 s, " + db (rms (ending, 0, 3.85)) + " at 3.85 s");

            // Arrange: automation on, the guitar's volume lane under its track and the master's along the bottom
            std::erase_if (song.automation, [guitar] (auto& l) { return l.owner == guitar; });
            song.showAutomation = true;
            song.notify (Song::Change::automation);
            Playlist playlist (song, audio);
            playlist.setSize (1600, 800);

            auto laneOf = [&playlist] (int owner, const juce::String& param)
            {
                for (auto& l : playlist.visibleLanes())
                    if (l.owner == owner && l.param == param)
                        return l;

                return Playlist::LaneView { -1 };
            };

            const auto lane = laneOf (guitar, "volume");
            const auto masterLane = laneOf (0, "volume");
            check (lane.owner == guitar && lane.area.getY() == (float) (Playlist::rulerHeight + Playlist::automationLaneHeight + Playlist::rowHeight)
                       && masterLane.owner == 0 && masterLane.area.getBottom() <= 790.0f,
                   "automation shown: the guitar's volume lane under its track, the master's along the bottom");

            auto mouse = [&playlist] (juce::Point<float> from, juce::Point<float> to, int mods = juce::ModifierKeys::leftButtonModifier)
            {
                const auto now = juce::Time::getCurrentTime();
                auto event = [&] (juce::Point<float> p, bool dragged)
                {
                    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (mods), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                             &playlist, &playlist, now, from, now, 1, dragged);
                };

                playlist.mouseDown (event (from, false));

                if (from != to)
                    for (int i = 1; i <= 6; ++i)
                        playlist.mouseDrag (event (from + (to - from) * (i / 6.0f), true));

                playlist.mouseUp (event (to, from != to));
            };

            // A bar is 72 px; the lane's points sit 6 px in from its top and bottom
            const float pxPerTick = 72.0f / bar;
            auto xOf = [&] (int tick) { return (float) Playlist::headerWidth + tick * pxPerTick; };
            const float top = lane.area.getY() + 6.0f, bottom = lane.area.getBottom() - 6.0f;
            auto yOf = [&] (float norm) { return bottom - norm * (bottom - top); };
            auto volumeParam = audio.findAutomationParameter (guitar, "volume");

            mouse ({ xOf (bar), yOf (0.5f) }, { xOf (bar), yOf (0.5f) });
            {
                auto l = song.findLane (guitar, "volume");
                check (l != nullptr && l->points.size() == 1 && l->points[0].tick == bar
                           && std::abs (l->points[0].value - volumeParam->valueRange.convertFrom0to1 (0.5f)) < 0.02f,
                       "clicking the lane at bar 2 halfway up adds a point there (the lane becomes the song's)");
            }

            mouse ({ xOf (bar), yOf (0.5f) }, { xOf (2 * bar), yOf (0.95f) });
            {
                auto l = song.findLane (guitar, "volume");
                check (l != nullptr && l->points.size() == 1 && l->points[0].tick == 2 * bar
                           && std::abs (l->points[0].value - volumeParam->valueRange.convertFrom0to1 (0.95f)) < 0.02f,
                       "dragging it moves it to bar 3, near the top");
            }

            mouse ({ xOf (0) + 3.0f, yOf (0.1f) }, { xOf (0) + 3.0f, yOf (0.1f) });
            check (song.findLane (guitar, "volume")->points.size() == 2 && song.findLane (guitar, "volume")->points[0].tick == 0,
                   "another click at the start adds a point before it");

            mouse ({ xOf (2 * bar), yOf (0.95f) }, { xOf (2 * bar), yOf (0.95f) }, juce::ModifierKeys::rightButtonModifier);
            check (song.findLane (guitar, "volume")->points.size() == 1, "right-clicking a point deletes it");

            mouse ({ xOf (0) + 3.0f, yOf (0.1f) }, { xOf (0) + 3.0f, yOf (0.1f) });
            check (playlist.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)) && song.findLane (guitar, "volume")->points.empty(),
                   "clicking a point and pressing Delete deletes it");

            song.undo();
            check (song.findLane (guitar, "volume") != nullptr && song.findLane (guitar, "volume")->points.size() == 1, "undo puts it back");

            // Saved and opened again; gone with their channel
            {
                Song saved;
                saved.fromVar (song.toVar());
                auto a = saved.findLane (guitar, "volume");
                auto b = saved.findLane (0, "volume");
                check (a != nullptr && a->points == song.findLane (guitar, "volume")->points && b != nullptr && b->points.size() == 2 && saved.showAutomation,
                       "lanes and their points are saved and opened again");
            }

            song.removeChannel (guitar);
            check (song.lanesOf (guitar).empty() && song.findLane (0, "volume") != nullptr, "removing the channel takes its lanes; the master's stay");

            take.deleteFile();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--namtest"))
        {
            // The NAM Amp (Neural Amp Modeler), straight through applyToBuffer: NAM's own example models (a WaveNet,
            // an LSTM, a standard-size WaveNet) and ones made here whose sound is known exactly (Linear models: half
            // the level). What a model says about itself; its sound at the song's rate and at another (between NAM's
            // resamplers); input, output, the gate and evening out loudness; a cabinet; its state kept and opened
            // again; a missing file and one that isn't a model; its panel; and what a standard WaveNet costs
            Song song;
            song.loadEmpty();
            AudioEngine audio (song, false);
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            const auto examples = juce::File (NIGHTSHIFT_NAM_EXAMPLES);
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NightShift NAM test");
            folder.deleteRecursively();
            folder.createDirectory();

            std::vector<te::Plugin::Ptr> keep;    // the plugin cache lets go of plugins nobody holds
            auto make = [&audio, &keep] (double rate, int block) -> NightAmp*
            {
                auto plugin = audio.getEdit().getPluginCache().createNewPlugin (NightAmp::xmlTypeName, {});
                keep.push_back (plugin);
                auto amp = dynamic_cast<NightAmp*> (plugin.get());

                if (amp != nullptr)
                    amp->initialise ({ te::TimePosition(), rate, block });

                return amp;
            };

            auto reopen = [&audio, &keep] (juce::ValueTree saved) -> NightAmp*
            {
                saved.removeProperty (te::IDs::id, nullptr);    // a new effect, not the one it was copied from
                auto plugin = audio.getEdit().getPluginCache().createNewPlugin (saved);
                keep.push_back (plugin);
                return dynamic_cast<NightAmp*> (plugin.get());
            };

            auto set = [] (StockEffect& fx, const char* id, float v) { if (auto p = fx.findParam (id)) p->set (v); };

            auto run = [] (te::Plugin& p, juce::AudioBuffer<float>& b, double rate, int block)
            {
                for (int start = 0; start < b.getNumSamples(); start += block)
                {
                    const int n = std::min (block, b.getNumSamples() - start);
                    const te::TimeRange time (te::TimePosition::fromSeconds (start / rate), te::TimePosition::fromSeconds ((start + n) / rate));
                    te::PluginRenderContext rc (&b, start, n, nullptr, 0.0, time, true, false, true, false);
                    p.applyToBuffer (rc);
                }
            };

            auto sine = [] (double freq, float amplitude, int samples, double rate)
            {
                juce::AudioBuffer<float> b (2, samples);

                for (int i = 0; i < samples; ++i)
                    b.setSample (0, i, amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / rate));

                b.copyFrom (1, 0, b, 0, 0, samples);
                return b;
            };

            auto rmsDb = [] (const juce::AudioBuffer<float>& b, int from, int to)
            {
                return juce::Decibels::gainToDecibels (b.getRMSLevel (0, from, to - from), -200.0f);
            };

            auto finite = [] (const juce::AudioBuffer<float>& b)
            {
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        if (! std::isfinite (b.getSample (ch, i)))
                            return false;

                return true;
            };

            // The largest difference between a buffer and another times a gain
            auto worst = [] (const juce::AudioBuffer<float>& b, const juce::AudioBuffer<float>& reference, float gain)
            {
                float most = 0.0f;

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        most = std::max (most, std::abs (b.getSample (ch, i) - gain * reference.getSample (ch, i)));

                return most;
            };

            // A model made here: the Linear architecture with one weight, 0.5, so it plays at half the level
            auto linear = [&folder] (const juce::String& name, double rate, const juce::String& metadata)
            {
                auto f = folder.getChildFile (name + ".nam");
                f.replaceWithText ("{\"version\": \"0.5.4\", \"architecture\": \"Linear\", \"config\": {\"receptive_field\": 1, \"bias\": false}, "
                                   "\"weights\": [0.5], \"sample_rate\": " + juce::String (rate, 1) + metadata + "}");
                return f;
            };

            const auto wavenet = examples.getChildFile ("wavenet.nam"), lstm = examples.getChildFile ("lstm.nam");
            const auto standard = examples.getChildFile ("wavenet_a1_standard.nam");
            check (wavenet.existsAsFile() && lstm.existsAsFile() && standard.existsAsFile(),
                   "NAM's example models are where the build says (" + examples.getFullPathName() + ")");

            const auto& list = stockfx::descriptions();
            check (std::any_of (list.begin(), list.end(), [] (const juce::PluginDescription& d) { return d.name == "Nightshift NAM Amp"; }),
                   "the NAM Amp is in the effects' list, beside the EQ and the rest");

            // NAM's example WaveNet
            auto* amp = make (48000.0, 512);
            const auto loaded = amp != nullptr ? amp->loadModel (wavenet) : juce::String ("no amp");
            const auto about = amp != nullptr ? amp->describeModel() : NightAmp::Description();
            check (loaded.isEmpty() && amp->hasModel() && about.name == "Test Model" && about.gear == "Darkglass Electronics Microtubes 900 v2"
                       && about.toneType == "clean" && about.modeledBy == "Steve" && about.sampleRate == 48000.0 && about.levelled,
                   "it loads NAM's example WaveNet and reads what it says: " + about.name + ", " + about.gear + ", " + about.toneType
                       + ", captured by " + about.modeledBy + ", trained at " + juce::String (about.sampleRate) + (loaded.isEmpty() ? juce::String() : " (" + loaded + ")"));

            set (*amp, "evenOut", 0.0f);
            auto played = sine (220.0, 0.25f, 48000, 48000.0);
            const auto dry = played;
            run (*amp, played, 48000.0, 512);
            const float wetDb = rmsDb (played, 24000, 48000);
            const float changed = worst (played, dry, 1.0f);
            check (finite (played) && wetDb > -60.0f && changed > 0.01f && worst (played, played, 1.0f) == 0.0f
                       && played.getSample (0, 30000) == played.getSample (1, 30000),
                   "and plays it: " + juce::String (wetDb, 1) + " dB out for a -15 dB 220 Hz sine, its own sound (" + juce::String (changed, 3)
                       + " from the input at most), the same on both sides");

            // Exactly half
            auto* exact = make (48000.0, 512);
            const auto half = linear ("half", 48000.0, {});
            exact->loadModel (half);
            auto s = sine (1000.0, 0.5f, 4800, 48000.0);
            const auto reference = s;
            run (*exact, s, 48000.0, 512);
            const float halfError = worst (s, reference, 0.5f);
            check (exact->hasModel() && halfError < 1.0e-5f, "a Linear model made here (one weight, 0.5) plays at half the level, to " + juce::String (halfError, 7));

            set (*exact, "input", 6.0206f);
            s = reference;
            run (*exact, s, 48000.0, 512);
            const float inputError = worst (s, reference, 1.0f);
            set (*exact, "input", 0.0f);
            set (*exact, "output", -6.0206f);
            s = reference;
            run (*exact, s, 48000.0, 512);
            const float outputError = worst (s, reference, 0.25f);
            set (*exact, "output", 0.0f);
            check (inputError < 1.0e-4f && outputError < 1.0e-4f, "input +6 dB plays it at the level it came in, output -6 dB at a quarter");

            // Evening out loudness: a model that says it's -6 dB comes out 12 dB quieter, to sit at -18
            auto* even = make (48000.0, 512);
            even->loadModel (linear ("loud", 48000.0, ", \"metadata\": {\"loudness\": -6.0, \"name\": \"Loud test\"}"));
            s = reference;
            run (*even, s, 48000.0, 512);
            const float evenError = worst (s, reference, 0.5f * juce::Decibels::decibelsToGain (-12.0f));
            set (*even, "evenOut", 0.0f);
            s = reference;
            run (*even, s, 48000.0, 512);
            const float asItIsError = worst (s, reference, 0.5f);
            check (even->describeModel().levelled && evenError < 1.0e-4f && asItIsError < 1.0e-4f,
                   "evening out loudness: a model that says it's -6 dB comes out 12 dB quieter (at -18); switched off, as it is");

            // Another rate: NAM's LSTM (trained at 48 kHz) in a 44.1 kHz song
            auto* at44 = make (44100.0, 512);
            auto* at48 = make (48000.0, 512);
            at44->loadModel (lstm);
            at48->loadModel (lstm);
            set (*at44, "evenOut", 0.0f);
            set (*at48, "evenOut", 0.0f);
            auto s44 = sine (220.0, 0.25f, 44100, 44100.0);
            auto s48 = sine (220.0, 0.25f, 48000, 48000.0);
            run (*at44, s44, 44100.0, 512);
            run (*at48, s48, 48000.0, 512);
            const float db44 = rmsDb (s44, 22050, 44100), db48 = rmsDb (s48, 24000, 48000);
            check (at44->getLatencySeconds() > 0.0 && at48->getLatencySeconds() == 0.0 && finite (s44) && std::abs (db44 - db48) < 1.0f,
                   "at 44.1 kHz NAM's LSTM (trained at 48) runs between NAM's resamplers (" + juce::String (at44->getLatencySeconds() * 1000.0, 2)
                       + " ms, told to the engine) and sounds the same: " + juce::String (db44, 2) + " dB against " + juce::String (db48, 2) + " at 48");

            // The gate: shut under -40 dB in front of the amp, open over it
            auto* gated = make (48000.0, 512);
            gated->loadModel (half);
            set (*gated, "gate", -40.0f);
            gated->reset();
            auto hush = sine (220.0, 0.001f, 24000, 48000.0);         // -60 dB
            run (*gated, hush, 48000.0, 512);
            const float shutDb = rmsDb (hush, 12000, 24000);
            const bool shutLight = gated->takeGateOpen();
            auto loudBit = sine (220.0, 0.25f, 24000, 48000.0);
            const auto loudReference = loudBit;
            run (*gated, loudBit, 48000.0, 512);
            juce::AudioBuffer<float> settledPart (2, 12000), referencePart (2, 12000);

            for (int ch = 0; ch < 2; ++ch)
            {
                settledPart.copyFrom (ch, 0, loudBit, ch, 12000, 12000);
                referencePart.copyFrom (ch, 0, loudReference, ch, 12000, 12000);
            }

            check (shutDb < -100.0f && ! shutLight && worst (settledPart, referencePart, 0.5f) < 1.0e-4f && gated->takeGateOpen(),
                   "the gate at -40 dB: a -60 dB hum comes out at " + juce::String (shutDb, 0) + " dB, and a loud note opens it (its light on)");

            // A cabinet: an impulse response made here, a click and half of it 100 samples on
            const auto irFile = folder.getChildFile ("cabinet.wav");
            {
                juce::AudioBuffer<float> ir (1, 512);
                ir.clear();
                ir.setSample (0, 0, 1.0f);
                ir.setSample (0, 100, 0.5f);
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (irFile);
                auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                                  .withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));

                if (writer != nullptr)
                    writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
            }

            auto* cabbed = make (48000.0, 512);
            cabbed->loadModel (half);
            const auto cabLoaded = cabbed->loadCabinet (irFile);

            // JUCE's convolution reads it on a thread of its own and fades it in over a few blocks: clicks until the
            // second tap holds still
            auto click = [&run, cabbed]
            {
                juce::AudioBuffer<float> b (2, 1024);
                b.clear();
                b.setSample (0, 0, 1.0f);
                b.setSample (1, 0, 1.0f);
                run (*cabbed, b, 48000.0, 512);
                return b;
            };

            float tapRatio = 0.0f, previous = -1.0f;

            for (int tries = 0; tries < 150; ++tries)
            {
                const auto b = click();
                const float first = b.getSample (0, 0);
                tapRatio = std::abs (first) > 1.0e-4f ? b.getSample (0, 100) / first : 0.0f;

                if (tapRatio > 0.1f && std::abs (tapRatio - previous) < 1.0e-4f)
                    break;

                previous = tapRatio;
                juce::Thread::sleep (20);
            }

            set (*cabbed, "cabinetOn", 0.0f);
            const auto bypassed = click();
            set (*cabbed, "cabinetOn", 1.0f);
            check (cabLoaded.isEmpty() && cabbed->hasCabinet() && std::abs (tapRatio - 0.5f) < 0.02f && std::abs (bypassed.getSample (0, 100)) < 1.0e-6f
                       && std::abs (bypassed.getSample (0, 0) - 0.5f) < 1.0e-5f,
                   "a cabinet (.wav) after it: a click comes out as the impulse response has it (the second tap " + juce::String (tapRatio, 3)
                       + " of the first, 100 samples on); switched off, the click alone");

            // Kept in its state, and opened again
            set (*cabbed, "input", 3.0f);
            auto* again = reopen (cabbed->state.createCopy());
            check (again != nullptr && again->hasModel() && again->getModelFile() == half && again->hasCabinet() && again->getCabinetFile() == irFile
                       && std::abs (again->findParam ("input")->get() - 3.0f) < 1.0e-4f,
                   "kept in the song: opened again it has its amp, its cabinet and its settings");

            // A missing file, and one that isn't a model
            auto goneState = cabbed->state.createCopy();
            goneState.setProperty ("model", folder.getChildFile ("gone.nam").getFullPathName(), nullptr);
            auto* missing = reopen (goneState);
            check (missing != nullptr && ! missing->hasModel() && missing->getProblem().startsWith ("Can't find the amp model \"gone.nam\"")
                       && missing->getModelFile() == folder.getChildFile ("gone.nam"),
                   "a song whose amp model has gone says so, and keeps its name: " + (missing != nullptr ? missing->getProblem() : juce::String()));

            const auto junk = folder.getChildFile ("not a model.nam");
            junk.replaceWithText ("hello");
            auto* tried = make (48000.0, 512);
            const auto why = tried->loadModel (junk);
            check (why.contains ("isn't an amp model NightShift can open") && ! tried->hasModel(), "a file that isn't a model says so: " + why);

            // Its panel: which amp, and every word readable
            if (auto panel = createStockPanel (*amp, song, audio))
            {
                const auto runs = textaudit::collect (*panel);
                const bool named = std::any_of (runs.begin(), runs.end(), [] (const textaudit::Run& r) { return r.text == "Test Model"; });
                const bool readable = std::all_of (runs.begin(), runs.end(), [] (const textaudit::Run& r) { return r.fine(); });
                check (named && readable, "its panel says which amp it is (Test Model), and every word on it is readable");
            }
            else
            {
                check (false, "it has a panel");
            }

            // What a standard-size WaveNet costs: ten seconds through it
            auto* heavy = make (48000.0, 256);
            heavy->loadModel (standard);
            auto ten = sine (110.0, 0.3f, 480000, 48000.0);
            const double started = juce::Time::getMillisecondCounterHiRes();
            run (*heavy, ten, 48000.0, 256);
            const double took = juce::Time::getMillisecondCounterHiRes() - started;
            check (heavy->hasModel() && finite (ten) && took < 2500.0,
                   "a standard-size WaveNet (NAM's A1 standard) runs " + juce::String (10000.0 / took, 1) + " times faster than real time at 48 kHz (needs 4)");

            keep.clear();
            folder.deleteRecursively();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--texttest"))
        {
            // Every text in every view a snapshot can set up, read back as it's painted (the text check,
            // --textaudit, each view in a run of its own): nothing under 15 px (the small capitals: 14),
            // squeezed, clipped, fainter than 4.5 to 1 (a greyed-out control's: 3) or cut short. Cut short is
            // fine for the user's own words (textaudit::TheirWords: a channel's name, a sound's), never ours.
            // On a Mac it's Selawik, Segoe UI's open stand-in, sized to take the same room.
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            struct View { const char* name; const char* options; };
            static const View views[] {
                { "Arrange and Steps", "" }, { "a laptop's 1366 x 768", "--size 1366x768" }, { "the piano roll", "--raise piano" },
                { "the mixer", "--raise mixer" }, { "the mixer with buses", "--buses --raise mixer" },
                { "AI: Song", "--raise song" }, { "AI: Song, a song heard for its vibe", "--raise song --vibedemo" },
                { "AI: Song, a song heard, on a laptop's 1366 x 768", "--size 1366x768 --raise song --vibedemo" },
                { "AI: Song, listening to a song", "--raise song --vibelistening" }, { "AI: Drums", "--raise drummer" }, { "AI: Bass", "--raise bass" },
                { "AI: Guitar", "--raise guitar" }, { "AI: Keys", "--raise keys" }, { "AI: Synth", "--raise aisynth" },
                { "AI: Master", "--raise aimaster" }, { "the drummer's conversation", "--drummer panel" },
                { "the bassist's", "--bass \"funk slap bass\"" }, { "the guitarist's, with tab", "--guitar \"pop punk power chords\"" },
                { "the keys player's", "--keys \"neo soul rhodes\"" }, { "the synth player's", "--synthplayer \"trap pad\"" },
                { "the mastering engineer's", "--masterai" },
                { "Nova: Oscillators", "--moretracks --select \"Warm Analog Pad\" --raise synth --synthpage 0" },
                { "Nova: Filter", "--moretracks --select \"Warm Analog Pad\" --raise synth --synthpage 1" },
                { "Nova: Motion", "--moretracks --select \"Warm Analog Pad\" --raise synth --synthpage 2" },
                { "Nova: Mod matrix", "--moretracks --select \"Warm Analog Pad\" --raise synth --synthpage 3" },
                { "Nova: Effects", "--moretracks --select \"Warm Analog Pad\" --raise synth --synthpage 4" },
                { "Master: Loudness", "--masterpage 0" }, { "Master: Tone, punch & width", "--masterpage 1 --shapeexample" },
                { "the start screen", "--startscreen --recent \"Demo song,Late night beat\"" },
                { "the start screen after the first time", "--startscreen --asked --recent \"Demo song,Late night beat\"" }, { "Audio settings", "--settings" },
                { "the metronome", "--metronome" }, { "the EQ", "--effectpanel nightshiftEQ" },
                { "the compressor", "--effectpanel nightshiftCompressor" }, { "the filter", "--effectpanel nightshiftFilter" },
                { "the reverb", "--effectpanel nightshiftReverb" }, { "the delay", "--effectpanel nightshiftDelay" },
                { "the chorus", "--effectpanel nightshiftChorus" }, { "the NAM amp, empty", "--effectpanel nightshiftAmp" },
                { "the NAM amp with an amp in it", "--effectpanel nightshiftAmp --nammodel example" },
                { "Save changes?", "--closedialog" }, { "About", "--about 900" },
                { "Check for updates: a newer version", "--updatefound" }, { "Check for updates: up to date", "--uptodate" },
                { "automation lanes", "--automationdemo" }, { "editing", "--editdemo" }, { "every kind of track", "--moretracks" },
                { "an audio track's Inspector", "--moretracks --select Guitar" },
                { "a synth's Inspector", "--moretracks --select \"Warm Analog Pad\"" },
                { "a plugin's Inspector", "--moretracks --select \"Surge XT\"" },
                { "an instrument's Inspector", "--moretracks --select \"Violin Section\"" },
                { "a sound search", "--search piano" }, { "no drum kits yet", "--nokits --raise drummer" },
                { "a MIDI keyboard playing", "--midilight \"Keystation 49\"" }, { "the Lofi effects", "--lofi --select Kick" },
                { "armed to record", "--moretracks --armrecord" }
            };

            // Which font: Segoe UI, or Selawik standing in for it (a Mac; or NIGHTSHIFT_FONT=selawik, which the
            // views' snapshots inherit). Where Segoe UI is here too, Selawik has to take the room it takes
            std::cout << "font: " << (theme::selawik (false) != nullptr ? "Selawik, standing in for Segoe UI" : "Segoe UI") << std::endl;

            if (juce::Font::findAllTypefaceNames().contains ("Segoe UI"))
            {
                const juce::StringArray samples { juce::String (juce::CharPointer_UTF8 ("Pattern  \xc2\xb7  2 sounds")), "TIME SIG",
                                                  "a whole song, one prompt", "Use MiniFuse Core Audio", "Spotify, YouTube, Tidal, Amazon Music: -14 LUFS",
                                                  "Drum kit, AI drummer", "Find new instruments", "0123456789  -12.5 dB  120 BPM  4/4" };
                // Selawik's Regular has Segoe UI's widths exactly; its Semibold (NightShift's bold) runs 1 to 3%
                // wider than Segoe UI's (its "1" much more), nearer than its Bold, Segoe UI Bold's twin. The views,
                // drawn in Selawik (NIGHTSHIFT_FONT=selawik), are where that has to fit
                for (auto bold : { false, true })
                {
                    float worst = 0.0f;
                    juce::String widest;

                    for (auto size : { 14.0f, 15.0f, 16.0f, 17.0f, 21.0f })
                        for (auto& s : samples)
                        {
                            const auto segoe = juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions ("Segoe UI", bold ? "Semibold" : "Regular", size)), s);
                            const auto standIn = juce::GlyphArrangement::getStringWidth (theme::selawikFont (size, bold), s);
                            const auto off = standIn / segoe - 1.0f;

                            if (std::abs (off) >= std::abs (worst))
                            {
                                worst = off;
                                widest = "\"" + s + "\" at " + juce::String (size, 0) + ": " + juce::String (standIn, 1) + " px against " + juce::String (segoe, 1);
                            }
                        }

                    // (Regular: the same widths, its kerning a hair apart, 0.5% at most)
                    check (std::abs (worst) < (bold ? 0.05f : 0.01f),
                           juce::String ("Selawik ") + (bold ? "Semibold" : "Regular") + " (a Mac's stand-in for Segoe UI's) takes "
                             + (bold ? "about its room" : "its room") + ": widths within " + juce::String (worst * 100.0f, 2)
                             + "% (the most apart, " + widest + ")");
                }
            }

            // The symbols NightShift draws past what Selawik has (the arrow, the triangle and the tick, which no view
            // shows until the AI's busy or done) come from JUCE's font fallback: each has to be a real glyph, not the
            // empty box a font draws for one it hasn't (glyph 0), in whichever font draws it
            {
                const juce::String symbols (juce::CharPointer_UTF8 ("\xe2\x86\x92\xe2\x96\xb8\xe2\x9c\x93\xc2\xb7\xe2\x80\xa6\xc3\x97"));
                juce::GlyphArrangement drawn;
                drawn.addLineOfText (theme::font (16.0f), symbols, 0.0f, 0.0f);
                juce::StringArray boxes;
                int glyphs = 0;

                for (int i = 0; i < drawn.getNumGlyphs(); ++i)
                {
                    const auto& g = drawn.getGlyph (i);

                    if (g.isWhitespace())
                        continue;

                    ++glyphs;

                    if (g.getGlyphIndex() == 0)
                        boxes.add ("U+" + juce::String::toHexString ((int) g.getCharacter()).toUpperCase());
                }

                check (glyphs == symbols.length() && boxes.isEmpty(),
                       "the arrow, the triangle, the tick, the dot, the ellipsis and the times sign each draw as a glyph, not a box ("
                         + (boxes.isEmpty() ? juce::String (glyphs) + " of " + juce::String (symbols.length()) : "boxes for " + boxes.joinIntoString (", ")) + ")");
            }

            const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NightShift text test");
            folder.createDirectory();
            const auto started = juce::Time::getMillisecondCounter();
            int checkedRuns = 0;

            for (const auto& view : views)
            {
                juce::StringArray command { exe.getFullPathName(), "--snapshot",
                                            folder.getChildFile (juce::String (view.name).retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") + ".png").getFullPathName(),
                                            "--textaudit" };
                command.addTokens (view.options, true);

                for (auto& token : command)
                    token = token.unquoted();

                juce::ChildProcess child;

                if (! child.start (command))
                {
                    check (false, juce::String (view.name) + ": the snapshot ran");
                    continue;
                }

                // Read as it goes (waiting first could fill the pipe and stall it), then it's done
                const auto output = child.readAllProcessOutput();
                child.waitForProcessToFinish (10000);
                juce::StringArray problems;
                int runs = 0;

                for (auto line : juce::StringArray::fromLines (output))
                {
                    if (line.startsWith ("TEXT "))
                        runs = line.fromFirstOccurrenceOf ("TEXT ", false, false).getIntValue();

                    // (The user's own words, marked where they're drawn, may be cut short and aren't listed)
                    if (line.startsWith ("SMALL") || line.startsWith ("SQUEEZED") || line.startsWith ("CUT")
                        || line.startsWith ("CLIPPED") || line.startsWith ("FAINT"))
                        problems.add (line.upToFirstOccurrenceOf ("  at ", false, false));
                }

                checkedRuns += runs;
                check (runs > 0 && problems.isEmpty(), juce::String (view.name) + ": " + juce::String (runs) + " pieces of text, every one readable"
                                                          + (problems.isEmpty() ? juce::String() : "; not: " + problems.joinIntoString (" | ")));
            }

            std::cout << "(" << checkedRuns << " pieces of text in " << (int) std::size (views) << " views, "
                      << (juce::Time::getMillisecondCounter() - started) / 1000 << " s)" << std::endl;
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--tempotest"))
        {
            // Tempo and time signature changes along the song: the song's own timing (jumps and
            // glides) against Tracktion's; bars across time signature changes, a bar cut short;
            // saving; a take folder's end through a change; rendered kicks where the map puts them;
            // the metronome following both (and a pattern's own 7/8); the count-in; Arrange's tempo
            // lane, its dialog and the ruler with real mouse events; the top bar's tempo now
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto about = [] (double a, double b, double within) { return std::abs (a - b) <= within; };
            const int bar = ticksPerBeat * 4;

            // The song's timing on its own
            {
                Song song;
                song.loadBlank();
                song.bpm = 120.0;
                check (about (song.secondsAt (bar), 2.0, 1.0e-9) && about (song.tickAt (2.0), bar, 1.0e-6) && about (song.secondsAt (-bar), -2.0, 1.0e-9),
                       "at 120 bpm a 4/4 bar is 2 s, there and back (and before the start)");

                song.setTempoAt (2 * bar, 60.0);
                check (about (song.secondsAt (2 * bar), 4.0, 1.0e-9) && about (song.secondsAt (3 * bar), 8.0, 1.0e-9) && about (song.tickAt (8.0), 3 * bar, 1.0e-6)
                           && song.bpmAt (2 * bar - 1) == 120.0 && song.bpmAt (2 * bar) == 60.0,
                       "a jump to 60 at bar 3: bar 3 takes 4 s, from 4 s in");

                song.setTempoAt (4 * bar, 120.0, true);
                const double gliding = song.secondsAt (4 * bar) - song.secondsAt (2 * bar);
                check (gliding > 4.0 && gliding < 8.0 && about (song.bpmAt (3 * bar), 90.0, 1.0e-9) && about (song.tickAt (song.secondsAt (3 * bar + 17)), 3 * bar + 17, 1.0e-6),
                       "a glide from 60 to 120 over bars 3 and 4 takes " + juce::String (gliding, 3) + " s (between 4 and 8), 90 bpm halfway");

                // Bars across time signature changes
                song.setMeterAt (2 * bar, 7, 8);
                song.setMeterAt (2 * bar + 2 * 336, 3, 4);
                check (song.barStart (2) == 768 && song.barStart (3) == 768 + 336 && song.barStart (4) == 768 + 672 && song.barStart (5) == 768 + 672 + 288
                           && song.barAt (1200) == 3 && song.meterAt (1200).beats == 7 && song.barLength (3) == 336,
                       "4/4 to 7/8 at bar 3 and 3/4 at bar 5: bars of 384, 336 and 288 ticks, each where it should be");
                check (song.snapTick (1130, Song::beatGrid) == 1152 && song.snapTick (1130, Song::barGrid) == 1104 && song.snapTick (1130, Song::barGrid, 1) == 1440
                           && song.snapTick (1130, ticksPerStep, -1) == 1128,
                       "snapping follows them: an eighth's beat, its bars, 16ths from the bar");

                song.setMeterAt (0, 3, 4);
                check (song.barStart (2) == 576 && song.barLength (2) == 192 && song.barStart (3) == 768,
                       "the start in 3/4 instead: bar 3 is cut short (192 ticks) where the 7/8 begins");

                song.setMeterAt (0, 4, 4);
                song.setMeterAt (2 * bar + 2 * 336, 7, 8);
                check (song.meterChanges.size() == 1, "a change to the time signature already playing is taken away");

                // A pattern's own time signature
                auto& p = song.addPattern ("Seven");
                p.meterBeats = 7;
                p.meterUnit = 8;
                check (song.patternLength (p) == 336 && song.barTicksOf (p) == 336 && song.beatTicksOf (p) == 48, "a pattern of its own in 7/8 is a 336-tick bar");

                // Saved and opened
                Song saved;
                saved.fromVar (song.toVar());
                check (saved.tempoChanges == song.tempoChanges && saved.meterChanges == song.meterChanges && saved.findPattern (p.id) != nullptr
                           && saved.findPattern (p.id)->meterBeats == 7,
                       "tempo changes, time signature changes and a pattern's own are saved and opened again");

                // A take folder's end through a change: 4 s from bar 2 at 120 (2 s of it) then 60
                Song takes;
                takes.loadBlank();
                takes.bpm = 120.0;
                takes.setTempoAt (2 * bar, 60.0);
                AudioClip clip;
                clip.start = bar;
                clip.length = 4.0;
                check (takes.audioClipEnd (clip) == 2 * bar + 192, "a 4 s recording from bar 2 ends 2 s into the 60 bpm bar after the change (tick 960)");
            }

            // The engine: Tracktion's tempo map the song's, a render's kicks where it puts them
            {
                Song song;
                song.loadEmpty();
                song.bpm = 120.0;
                song.mode = PlayMode::song;
                const int kick = song.channels.front().id;
                auto& pattern = song.currentPattern();
                steps::set (pattern.notesFor (kick), 0, true, defaultRootKey, 127);
                song.setTempoAt (2 * bar, 60.0);
                song.setTempoAt (4 * bar, 120.0, true);

                for (int b = 0; b < 6; ++b)
                    song.playlist.push_back ({ pattern.id, 0, song.barStart (b) });

                song.notify (Song::Change::everything);
                AudioEngine audio (song, false);
                audio.syncNow();

                auto& tempo = audio.getEdit().tempoSequence;
                bool same = tempo.getNumTempos() == 3;

                for (int t : { 0, bar, 2 * bar, 2 * bar + 50, 3 * bar, 4 * bar - 1, 4 * bar, 6 * bar })
                    same = same && about (tempo.toTime (tracktion::BeatPosition::fromBeats (t / (double) ticksPerBeat)).inSeconds(), song.secondsAt (t), 1.0e-6);

                check (same, "Tracktion has the start's tempo and both changes, and its times are the song's to a microsecond (jump and glide)");

                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-tempotest.wav");
                audio.renderToFile (file, 0.5);
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                juce::Array<double> onsets;

                if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr)
                {
                    juce::AudioBuffer<float> b ((int) reader->numChannels, (int) reader->lengthInSamples);
                    reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                    int quietFor = 48000;

                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        if (std::abs (b.getSample (0, i)) > 0.05f)
                        {
                            if (quietFor > 24000)
                                onsets.add (i / 48000.0);

                            quietFor = 0;
                        }
                        else
                        {
                            ++quietFor;
                        }
                    }
                }

                file.deleteFile();
                juce::String where;
                bool placed = onsets.size() == 6;

                for (int b = 0; b < onsets.size(); ++b)
                {
                    where << juce::String (onsets[b], 3) << " s (" << juce::String (song.secondsAt (song.barStart (b)), 3) << ")  ";
                    placed = placed && about (onsets[b], song.secondsAt (song.barStart (b)), 0.005);
                }

                check (placed, "rendered, a kick on each bar lands where the tempo map says: " + where.trim());

                // A volume point at bar 4 sits where the map puts bar 4
                song.laneFor (kick, "volume", "Volume").points = { { 3 * bar, 0.5f } };
                song.notify (Song::Change::automation);
                audio.syncNow();
                auto* volume = audio.findAutomationParameter (kick, "volume");
                check (volume != nullptr && volume->getCurve().getNumPoints() == 1
                           && about (volume->getCurve().getPointTime (0).inSeconds(), song.secondsAt (3 * bar), 1.0e-6),
                       "an automation point at bar 4 is at " + juce::String (song.secondsAt (3 * bar), 3) + " s, through the glide");

                // Pattern mode plays the start's tempo
                song.mode = PlayMode::pattern;
                song.notify (Song::Change::transport);
                audio.syncNow();
                check (tempo.getNumTempos() == 1 && tempo.getTempo (0)->getBpm() == 120.0, "Pattern mode plays at the start's tempo, its changes off");
                song.mode = PlayMode::song;
                song.notify (Song::Change::transport);
                audio.syncNow();
                check (tempo.getNumTempos() == 3, "and Song mode puts them back");

                // The count-in: the bars before home as the song plays them
                check (about (audio.countInSeconds (song.secondsAt (3 * bar), 1), song.secondsAt (3 * bar) - song.secondsAt (2 * bar), 1.0e-6)
                           && about (audio.countInSeconds (0.0, 1), 2.0, 1.0e-6),
                       "a bar's count-in before bar 4 takes bar 3's " + juce::String (song.secondsAt (3 * bar) - song.secondsAt (2 * bar), 3)
                           + " s (at 60 bpm, gliding); before bar 1, 2 s at the start's 120");
            }

            // The metronome: clicks where the song's beats are through a tempo jump and a time
            // signature change, the downbeats strong; then a pattern's own 7/8 in Pattern mode
            {
                Song song;
                song.loadEmpty();
                song.bpm = 120.0;
                song.mode = PlayMode::song;
                song.loopOn = false;
                song.setTempoAt (bar, 240.0);
                song.setMeterAt (2 * bar, 3, 4);
                song.playlist.push_back ({ song.currentPattern().id, 0, song.barStart (8) });   // a song long enough
                song.notify (Song::Change::everything);
                AudioEngine audio (song, false);
                audio.syncNow();
                auto o = audio.getOptions();
                o.metronome = true;
                o.clickRhythm = 0;
                audio.setOptions (o);

                const double sr = 48000.0;
                const int block = 512;
                auto listen = [&] (double seconds)
                {
                    juce::AudioBuffer<float> out (2, (int) (seconds * sr) / block * block), buffer (2, block);

                    for (int b = 0; b < out.getNumSamples() / block; ++b)
                    {
                        buffer.clear();
                        audio.processHostedBlock (buffer);

                        for (int ch = 0; ch < 2; ++ch)
                            out.copyFrom (ch, b * block, buffer, ch, 0, block);

                        if (b % 4 == 0)
                            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
                    }

                    return out;
                };

                struct Onset { int at; float peak; };
                auto onsets = [] (const juce::AudioBuffer<float>& b)
                {
                    std::vector<Onset> found;
                    const float* x = b.getReadPointer (0);
                    int quiet = 100000;

                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        const float v = std::abs (x[i]);

                        if (v > 0.003f && quiet > 240)
                        {
                            float peak = 0.0f;

                            for (int j = i; j < std::min (b.getNumSamples(), i + 480); ++j)
                                peak = std::max (peak, std::abs (x[j]));

                            found.push_back ({ i, peak });
                        }

                        quiet = v <= 0.003f ? quiet + 1 : 0;
                    }

                    return found;
                };

                listen (0.3);
                audio.togglePlay();
                const auto found = onsets (listen (4.4));

                // 4/4 at 120 (bar 1), 4/4 at 240 (bar 2), then 3/4 at 240
                std::vector<double> expected;
                std::vector<bool> strong;

                for (int b = 0; b < 4; ++b)
                {
                    const auto m = song.meterAt (song.barStart (b));

                    for (int beat = 0; beat < m.beats; ++beat)
                    {
                        expected.push_back (song.secondsAt (song.barStart (b) + beat * m.beatTicks()));
                        strong.push_back (beat == 0);
                    }
                }

                bool timed = found.size() == expected.size();
                bool accented = timed;
                juce::String where;
                float strongest = 0.0f;

                for (auto& f : found)
                    strongest = std::max (strongest, f.peak);

                for (size_t i = 0; i < found.size(); ++i)
                {
                    where << juce::String ((found[i].at - found[0].at) / sr, 3) << (found[i].peak > strongest * 0.85f ? "! " : " ");

                    if (i < expected.size())
                    {
                        timed = timed && std::abs ((found[i].at - found[0].at) - (expected[i] - expected[0]) * sr) <= 2.0;
                        accented = accented && (found[i].peak > strongest * 0.85f) == strong[i];
                    }
                }

                check (timed, "playing, the clicks follow the jump to 240 at bar 2 and the 3/4 from bar 3: " + where.trim());
                check (accented, "the downbeats strong: bar 1, bar 2, then every third beat");

                // A pattern of its own in 7/8 in Pattern mode: eighths at 120, the one strong
                audio.stop();
                listen (0.3);
                auto& seven = song.currentPattern();
                seven.meterBeats = 7;
                seven.meterUnit = 8;
                song.mode = PlayMode::pattern;
                song.notify (Song::Change::patterns);
                song.notify (Song::Change::transport);
                audio.syncNow();
                const auto [beats, unit] = audio.getMeterNow();
                audio.togglePlay();
                const auto pattern = onsets (listen (1.9));
                bool sevens = pattern.size() >= 7;

                for (size_t i = 1; i < pattern.size(); ++i)
                    sevens = sevens && std::abs (pattern[i].at - pattern[i - 1].at - 12000) <= 2;

                check (beats == 7 && unit == 8 && sevens && pattern.size() >= 8 && pattern[7].peak > pattern[6].peak * 1.2f && pattern[0].peak > pattern[1].peak * 1.2f,
                       "Pattern mode with a 7/8 pattern: clicks an eighth apart (0.25 s), the first of every seven strong ("
                           + juce::String ((int) pattern.size()) + " clicks)");
                audio.stop();
            }

            // Arrange: the tempo lane and its dialog with real mouse events; the top bar's tempo now
            {
                Song song;
                song.loadBlank();
                song.bpm = 120.0;
                song.mode = PlayMode::song;
                song.showAutomation = true;
                song.notify (Song::Change::everything);
                AudioEngine audio (song, false);
                audio.syncNow();
                Playlist playlist (song, audio);
                playlist.setSize (1600, 800);

                auto mouse = [&playlist] (juce::Point<float> from, juce::Point<float> to, int mods = juce::ModifierKeys::leftButtonModifier, int clicks = 1)
                {
                    const auto now = juce::Time::getCurrentTime();
                    auto event = [&] (juce::Point<float> p, bool dragged)
                    {
                        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (mods), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                 &playlist, &playlist, now, from, now, clicks, dragged);
                    };

                    playlist.mouseDown (event (from, false));

                    if (from != to)
                        for (int i = 1; i <= 6; ++i)
                            playlist.mouseDrag (event (from + (to - from) * (i / 6.0f), true));

                    playlist.mouseUp (event (to, from != to));
                };

                // A bar is 72 px; the lane's tempos run from 100 to 140 (120 with 20 either side),
                // 8 px in from its top and bottom
                const float pxPerTick = 72.0f / bar;
                auto xOf = [&] (int tick) { return (float) Playlist::headerWidth + tick * pxPerTick; };
                const float laneTop = (float) Playlist::rulerHeight, laneBottom = laneTop + Playlist::automationLaneHeight;
                auto yOf = [&] (double bpm, double lo, double hi) { return laneBottom - 8.0f - (float) ((bpm - lo) / (hi - lo)) * (laneBottom - laneTop - 16.0f); };

                mouse ({ xOf (8 * bar), yOf (135.0, 100.0, 140.0) }, { xOf (8 * bar), yOf (135.0, 100.0, 140.0) });
                check (song.tempoChanges.size() == 1 && song.tempoChanges[0].tick == 8 * bar && about (song.tempoChanges[0].bpm, 135.0, 1.0) && ! song.tempoChanges[0].glide,
                       "clicking the tempo lane at bar 9 near the top adds a jump to " + juce::String (song.tempoChanges.empty() ? 0.0 : song.tempoChanges[0].bpm, 0) + " bpm there");

                // Dragged to bar 10 and down: the lane's range holds still while it moves (100-160 now)
                mouse ({ xOf (8 * bar), yOf (song.tempoChanges[0].bpm, 100.0, 160.0) }, { xOf (9 * bar), yOf (110.0, 100.0, 160.0) });
                check (song.tempoChanges.size() == 1 && song.tempoChanges[0].tick == 9 * bar && about (song.tempoChanges[0].bpm, 110.0, 1.5),
                       "dragging it moves it to bar 10, down to " + juce::String (song.tempoChanges[0].bpm, 0) + " bpm");

                // The dialog: a glide to 150 at bar 13
                playlist.askTempo (12 * bar);
                if (auto* window = dynamic_cast<juce::AlertWindow*> (juce::ModalComponentManager::getInstance()->getModalComponent (0)))
                {
                    if (auto* editor = window->getTextEditor ("bpm"))
                        editor->setText ("150");

                    if (auto* how = window->getComboBoxComponent ("how"))
                        how->setSelectedItemIndex (1);

                    window->exitModalState (1);
                }

                for (int i = 0; i < 10; ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

                check (song.tempoChanges.size() == 2 && song.tempoChanges[1].tick == 12 * bar && song.tempoChanges[1].bpm == 150.0 && song.tempoChanges[1].glide,
                       "the tempo dialog at bar 13: 150 bpm, gliding there from 110");

                // Right-clicks open menus (the point's, the ruler's)
                mouse ({ xOf (9 * bar), playlist.getHeight() > 0 ? yOf (song.tempoChanges[0].bpm, 90.0, 170.0) : 0.0f },
                       { xOf (9 * bar), yOf (song.tempoChanges[0].bpm, 90.0, 170.0) }, juce::ModifierKeys::rightButtonModifier);
                const bool pointMenu = juce::PopupMenu::dismissAllActiveMenus();
                mouse ({ xOf (4 * bar), (float) Playlist::rulerHeight - 8.0f }, { xOf (4 * bar), (float) Playlist::rulerHeight - 8.0f },
                       juce::ModifierKeys::rightButtonModifier);
                const bool rulerMenu = juce::PopupMenu::dismissAllActiveMenus();
                check (pointMenu && rulerMenu, "right-clicking a tempo point, or the ruler, opens its menu");

                // The top bar's tempo is the one where the song is; changing it changes that change
                audio.setPlayheadTicks (10 * bar);
                const double from = song.tempoChanges[0].bpm;
                check (about (audio.getTempoNow(), from + (150.0 - from) / 3.0, 1.0e-6), "at bar 11, a third of the way along the glide from bar 10 to bar 13: "
                           + juce::String (audio.getTempoNow(), 1) + " bpm");
                audio.setPlayheadTicks (9 * bar + 10);
                audio.setTempoNow (100.0);
                check (song.tempoChanges[0].bpm == 100.0 && song.bpm == 120.0, "setting the tempo at bar 10 changes bar 10's change, not the start's");

                song.undo();
                check (song.tempoChanges.size() == 1, "undo takes the dialog's change away");
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--mididevices"))
        {
            // The MIDI devices the system has, as JUCE sees them and as a headless engine using the
            // system's devices does (nothing is opened but what Tracktion opens to list them)
            for (auto& d : juce::MidiInput::getAvailableDevices())
                std::cout << "JUCE in:  " << d.name << "  [" << d.identifier << "]" << std::endl;

            for (auto& d : juce::MidiOutput::getAvailableDevices())
                std::cout << "JUCE out: " << d.name << "  [" << d.identifier << "]" << std::endl;

            AudioEngine::headlessMidiDevices = true;
            Song song;
            song.loadBlank();
            AudioEngine audio (song, false);

            for (int i = 0; i < 40; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            audio.getEngine().getDeviceManager().dispatchPendingUpdates();
            audio.syncNow();
            std::cout << "NightShift's MIDI inputs: " << audio.getMidiInputNames().joinIntoString (" | ") << std::endl;
            return 0;
        }

        if (args.has ("--miditest"))
        {
            // MIDI keyboards and controllers: the channel they play (the selected one, or the last
            // instrument when an audio track is selected); notes recorded from them as the PC
            // keyboard's are, the time they took to arrive taken off; the sustain pedal holding a
            // recorded note on until it comes up; MIDI learn linking a knob to a fader, a pan, the
            // master and an effect's knob, one knob per control, links saved and gone with their
            // channel; then, where the system has a MIDI loopback (Windows MIDI Services' test
            // loopback), real MIDI out of one end and into the engine through Tracktion: heard
            // through the selected channel's synth, lighting the light, and recorded
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            const double sr = 48000.0;
            const int block = 512;

            {
                Song song;
                song.loadBlank();
                song.bpm = 120.0;
                const int keys = song.addChannel ("Keys", InstrumentType::synth).id;
                const int guitar = song.addChannel ("Guitar", InstrumentType::audio).id;
                song.selectedChannelId = keys;
                song.notify (Song::Change::everything);
                AudioEngine audio (song, false);
                audio.syncNow();

                auto select = [&] (int id)
                {
                    song.selectedChannelId = id;
                    song.notify (Song::Change::selection);
                    audio.syncNow();
                };

                check (audio.getMidiTargetChannel() == keys, "MIDI keyboards play the selected channel, Keys");
                select (guitar);
                check (audio.getMidiTargetChannel() == keys, "with the Guitar audio track selected they keep playing Keys");
                select (keys);

                juce::AudioBuffer<float> buffer (2, block);
                auto heardTicks = [&]
                {
                    auto context = audio.getEdit().getTransport().getCurrentPlaybackContext();
                    return context != nullptr ? context->getPosition().inSeconds() * song.bpm / 60.0 * ticksPerBeat : 0.0;
                };

                auto runTo = [&] (double ticks)
                {
                    for (int b = 0; b < (int) (10.0 * sr) / block && heardTicks() < ticks; ++b)
                    {
                        buffer.clear();
                        audio.processHostedBlock (buffer);

                        if (b % 4 == 0)
                            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
                    }
                };

                const juce::String device ("Test keyboard");
                auto midi = [&] (const juce::MidiMessage& m, double secondsAgo = 0.0) { audio.handleMidiInput (device, m, secondsAgo); };

                // Recording, no count-in, into the one-bar pattern as it loops (1/16 snap)
                auto o = audio.getOptions();
                o.countInBars = 0;
                o.metronome = false;
                audio.setOptions (o);
                song.mode = PlayMode::pattern;
                song.notify (Song::Change::transport);
                audio.syncNow();
                auto& pattern = song.currentPattern();
                audio.setRecordArmed (true);
                audio.togglePlay();
                check (audio.isRecordingNotes() && audio.isPlaying(), "Record armed, Play records Keys");

                auto notes = [&] { return pattern.notesFor (keys); };
                auto has = [&] (int start, int length, int key, int velocity)
                {
                    auto list = notes();
                    return std::count (list.begin(), list.end(), Note { start, length, key, velocity }) > 0;
                };

                // A note on beat 2 for an eighth
                runTo (ticksPerBeat + 2);
                midi (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
                runTo (ticksPerBeat + ticksPerBeat / 2 + 2);
                midi (juce::MidiMessage::noteOff (1, 60));
                check (has (ticksPerBeat, ticksPerBeat / 2, 60, 100), "a note played on beat 2 for an eighth is written there, velocity 100");

                // Arriving a quarter of a second late (half a beat at 120): written where it was played
                runTo (3 * ticksPerBeat + 2);
                midi (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 90), 0.25);
                runTo (3 * ticksPerBeat + ticksPerStep + 2);
                midi (juce::MidiMessage::noteOff (1, 62), 0.25);
                check (has (2 * ticksPerBeat + ticksPerBeat / 2, ticksPerStep, 62, 90), "a note that took a quarter of a second to arrive is written half a beat earlier");

                // The sustain pedal: let go with it down, the note goes on until it comes up. Round
                // the loop to the bar's top first (the playhead goes back to 0 there)
                runTo (4 * ticksPerBeat - ticksPerStep);

                for (int b = 0; b < (int) (10.0 * sr) / block && heardTicks() > ticksPerBeat; ++b)
                {
                    buffer.clear();
                    audio.processHostedBlock (buffer);
                }

                midi (juce::MidiMessage::controllerEvent (1, 64, 127));
                runTo (4);
                midi (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 80));
                runTo (ticksPerBeat / 4 + 2);
                midi (juce::MidiMessage::noteOff (1, 64));
                const bool notYet = ! has (0, ticksPerBeat / 4, 64, 80);
                runTo (ticksPerBeat + 2);
                midi (juce::MidiMessage::controllerEvent (1, 64, 0));
                check (notYet && has (0, ticksPerBeat, 64, 80), "with the pedal down a note let go after a 16th sounds on: written a whole beat long when the pedal comes up");

                audio.stop();
                audio.setRecordArmed (false);

                // MIDI learn
                juce::String said;
                audio.learnMidiController (keys, "volume", [&said] (const juce::String& s) { said = s; });
                check (audio.isLearningMidi(), "learning: waiting for a knob");
                midi (juce::MidiMessage::controllerEvent (1, 21, 64));
                auto* c = song.findChannel (keys);
                check (! audio.isLearningMidi() && said == "CC 21 on Test keyboard" && std::abs (c->volume - 64.0f / 127.0f) < 1.0e-4f,
                       "moving CC 21 links it to Keys' volume (\"" + said + "\") and sets it");
                midi (juce::MidiMessage::controllerEvent (1, 21, 127));
                check (std::abs (c->volume - 1.0f) < 1.0e-4f, "turned right up, the fader goes right up");

                audio.learnMidiController (keys, "pan", {});
                midi (juce::MidiMessage::controllerEvent (1, 22, 0));
                check (std::abs (c->pan + 1.0f) < 1.0e-4f, "CC 22 linked to its pan, turned down, pans hard left");

                audio.learnMidiController (0, "volume", {});
                midi (juce::MidiMessage::controllerEvent (1, 21, 100));
                check (std::abs (song.masterVolume - 100.0f / 127.0f) < 1.0e-4f && song.findControllerLink (keys, "volume") == nullptr
                           && song.findControllerLink (0, "volume") != nullptr && song.controllerLinks.size() == 2,
                       "linking CC 21 to the master volume moves the link there (one knob per control)");

                // An effect's knob, through its parameter's own range
                Effect filter;
                filter.id = song.createId();
                filter.pluginId = stockfx::descriptions()[5].createIdentifierString();
                filter.name = "Nightshift Filter";
                c->effects.push_back (filter);
                song.notify (Song::Change::mixer);
                audio.syncNow();
                const auto cutoffKey = "fx:" + juce::String (filter.id) + ":cutoff";
                audio.learnMidiController (keys, cutoffKey, {});
                midi (juce::MidiMessage::controllerEvent (2, 23, 0));
                auto cutoff = audio.findAutomationParameter (keys, cutoffKey);
                const float low = cutoff != nullptr ? cutoff->getCurrentValue() : -1.0f;
                midi (juce::MidiMessage::controllerEvent (2, 23, 127));
                const float high = cutoff != nullptr ? cutoff->getCurrentValue() : -1.0f;
                midi (juce::MidiMessage::controllerEvent (1, 23, 0));
                check (std::abs (low - 20.0f) < 0.5f && std::abs (high - 20000.0f) < 1.0f && cutoff->getCurrentValue() == high,
                       "CC 23 on MIDI channel 2 sweeps the filter's cutoff from " + juce::String (low, 1) + " to " + juce::String (high, 0)
                           + " Hz (the same CC on channel 1 leaves it)");

                {
                    Song saved;
                    saved.fromVar (song.toVar());
                    check (saved.controllerLinks.size() == 3 && saved.findControllerLink (keys, cutoffKey) != nullptr
                               && saved.findControllerLink (keys, cutoffKey)->midiChannel == 2,
                           "links are saved and opened with the song");
                }

                song.removeChannel (keys);
                check (song.controllerLinks.size() == 1 && song.findControllerLink (0, "volume") != nullptr, "removing Keys takes its links; the master's stays");
            }

            // Through the engine: MIDI into its input (the headless one, routed as a keyboard's is)
            // plays the selected channel's synth and dies away once let go; and follows a new selection
            {
                AudioEngine::routeHostedMidi = true;
                Song song;
                song.loadBlank();
                const int lead = song.addChannel ("Lead", InstrumentType::synth).id;
                song.selectedChannelId = lead;
                song.notify (Song::Change::everything);
                AudioEngine audio (song, false);

                // Its inputs arrive a moment after it starts, as a keyboard plugged in does; the
                // engine hears the device list change and routes them
                auto hasInput = [&audio]
                {
                    for (auto& d : audio.getEdit().engine.getDeviceManager().getMidiInDevices())
                        if (tracktion::HostedAudioDeviceInterface::isHostedMidiInputDevice (*d))
                            return true;

                    return false;
                };

                for (int i = 0; i < 100 && ! hasInput(); ++i)
                    settleMessages (20);

                settleMessages (50);
                audio.syncNow();

                auto routedTo = [&audio] (int channelId)
                {
                    for (auto* instance : audio.getEdit().getAllInputDevices())
                        if (auto* d = dynamic_cast<tracktion::MidiInputDevice*> (&instance->getInputDevice());
                            d != nullptr && tracktion::HostedAudioDeviceInterface::isHostedMidiInputDevice (*d))
                            if (auto track = audio.getTrack (channelId))
                                return instance->getTargets().contains (track->itemID) && d->getMonitorMode() == tracktion::InputDevice::MonitorMode::on;

                    return false;
                };

                // What's there, for a failure: the engine's MIDI inputs and the instances on the Edit
                auto describe = [&audio]
                {
                    juce::StringArray parts;

                    for (auto& d : audio.getEdit().engine.getDeviceManager().getMidiInDevices())
                        parts.add ("input " + d->getName() + (d->isEnabled() ? " (on" : " (off")
                                   + (tracktion::HostedAudioDeviceInterface::isHostedMidiInputDevice (*d) ? ", hosted)" : ")"));

                    for (auto* instance : audio.getEdit().getAllInputDevices())
                        parts.add ("instance of " + instance->getInputDevice().getName() + " (type " + juce::String ((int) instance->getInputDevice().getDeviceType())
                                   + ", " + juce::String (instance->getTargets().size()) + " targets)");

                    parts.add (audio.getEdit().getTransport().getCurrentPlaybackContext() != nullptr ? "a playback context" : "no playback context");
                    return parts.joinIntoString ("; ");
                };

                {
                    const bool ok = routedTo (lead);
                    check (ok, "the engine's MIDI input plays Lead's track, always heard" + (ok ? juce::String() : " [" + describe() + "]"));
                }

                juce::AudioBuffer<float> buffer (2, block);
                auto run = [&] (double seconds, std::optional<juce::MidiMessage> first = std::nullopt)
                {
                    float loudest = 0.0f;

                    for (int b = 0; b < (int) (seconds * sr) / block; ++b)
                    {
                        juce::MidiBuffer midi;

                        if (b == 0 && first.has_value())
                            midi.addEvent (*first, 0);

                        buffer.clear();
                        audio.processHostedBlock (buffer, midi);

                        if (b % 4 == 0)
                            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

                        loudest = std::max (loudest, buffer.getMagnitude (0, block));
                    }

                    return loudest;
                };

                run (0.3);
                const float held = run (0.4, juce::MidiMessage::noteOn (1, 64, (juce::uint8) 110));
                run (2.5, juce::MidiMessage::noteOff (1, 64));
                const float after = run (0.3);
                check (held > 0.01f && after < held * 0.01f,
                       "a note into it plays Lead's synth through the engine (peak " + juce::String (juce::Decibels::gainToDecibels (held), 1)
                           + " dB), dying away once let go (" + juce::String (juce::Decibels::gainToDecibels (after), 1) + " dB)");

                const int bass = song.addChannel ("Bass", InstrumentType::synth).id;
                song.notify (Song::Change::channels);
                song.selectedChannelId = bass;
                song.notify (Song::Change::selection);
                audio.syncNow();
                check (routedTo (bass) && ! routedTo (lead), "selecting Bass moves it there");
                AudioEngine::routeHostedMidi = false;
            }

            // The real path, where the system has one: MIDI out of a loopback (Windows MIDI
            // Services') or a virtual port made here (a Mac's), into the engine as a keyboard's
            AudioEngine::headlessMidiDevices = true;
            Song song;
            song.loadBlank();
            song.bpm = 120.0;
            const int lead = song.addChannel ("Lead", InstrumentType::synth).id;
            song.selectedChannelId = lead;
            song.notify (Song::Change::everything);
            AudioEngine audio (song, false);
            std::unique_ptr<juce::MidiOutput> out;
            juce::String inName, outName;

           #if JUCE_MAC || JUCE_LINUX
            // A virtual MIDI port of our own, which the engine sees as a keyboard plugged in
            if ((out = juce::MidiOutput::createNewDevice ("NightShift Test Keyboard")))
            {
                outName = inName = "NightShift Test Keyboard";
                audio.getEngine().getDeviceManager().rescanMidiDeviceList();
            }
           #endif

            for (int i = 0; i < 60; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            audio.getEngine().getDeviceManager().dispatchPendingUpdates();
            audio.syncNow();

           #if JUCE_MAC || JUCE_LINUX
            // Core MIDI lists a new virtual port a moment after it's made, so the scan right after
            // can miss it; the app's 4 s rescan would find it, but a headless engine has none. Look again
            if (out != nullptr && ! audio.getMidiInputNames().contains (inName))
            {
                audio.getEngine().getDeviceManager().rescanMidiDeviceList();

                for (int i = 0; i < 60; ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

                audio.getEngine().getDeviceManager().dispatchPendingUpdates();
                audio.syncNow();
            }
           #endif

            if (out == nullptr)
            {
                for (auto& name : audio.getMidiInputNames())
                    if (name.containsIgnoreCase ("Loopback B"))
                        inName = name;

                for (auto& d : juce::MidiOutput::getAvailableDevices())
                    if (d.name.containsIgnoreCase ("Loopback A") && inName.isNotEmpty())
                        if ((out = juce::MidiOutput::openDevice (d.identifier)))
                            outName = d.name;
            }
            else if (! audio.getMidiInputNames().contains (inName))
            {
                inName = {};
            }

            if (inName.isEmpty() || out == nullptr)
            {
                std::cout << "SKIP  no MIDI loopback or virtual port here to send real MIDI through (inputs: "
                          << audio.getMidiInputNames().joinIntoString (", ") << ")" << std::endl;
            }
            else
            {
                const auto outInfo = juce::MidiDeviceInfo (outName, outName);
                // Only the loopback: the system's own keyboards aren't taken over by a test
                for (auto& name : audio.getMidiInputNames())
                    if (name != inName)
                        audio.setMidiInputEnabled (name, false);

                audio.syncNow();

                bool routed = false;

                for (auto* instance : audio.getEdit().getAllInputDevices())
                    if (instance->getInputDevice().getName() == inName)
                        if (auto slotTrack = audio.getTrack (lead))
                            routed = instance->getTargets().contains (slotTrack->itemID)
                                  && instance->getInputDevice().getMonitorMode() == tracktion::InputDevice::MonitorMode::on;

                check (routed, inName + " plays Lead's track, its input always heard");

                juce::AudioBuffer<float> buffer (2, block);
                auto run = [&] (double seconds)
                {
                    float loudest = 0.0f;

                    for (int b = 0; b < (int) (seconds * sr) / block; ++b)
                    {
                        buffer.clear();
                        audio.processHostedBlock (buffer);
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
                        loudest = std::max (loudest, buffer.getMagnitude (0, block));
                    }

                    return loudest;
                };

                auto settleMidi = [] { for (int i = 0; i < 10; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (5); };

                run (0.3);
                const auto before = audio.getLastMidiActivity();
                out->sendMessageNow (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 110));
                settleMidi();
                const float held = run (0.4);
                out->sendMessageNow (juce::MidiMessage::noteOff (1, 64));
                settleMidi();
                run (2.5);
                const float after = run (0.3);
                check (audio.getLastMidiActivity() != before, "a note sent into " + outInfo.name + " reaches NightShift (the MIDI light)");
                check (held > 0.01f && after < held * 0.01f,
                       "and plays Lead's synth through the engine (peak " + juce::String (juce::Decibels::gainToDecibels (held), 1)
                           + " dB), dying away once let go (" + juce::String (juce::Decibels::gainToDecibels (after), 1) + " dB)");

                // Recorded from the real input
                auto o = audio.getOptions();
                o.countInBars = 0;
                o.metronome = false;
                audio.setOptions (o);
                auto& pattern = song.currentPattern();
                pattern.notesFor (lead).clear();
                audio.setRecordArmed (true);
                audio.togglePlay();

                auto heardTicks = [&]
                {
                    auto context = audio.getEdit().getTransport().getCurrentPlaybackContext();
                    return context != nullptr ? context->getPosition().inSeconds() * song.bpm / 60.0 * ticksPerBeat : 0.0;
                };

                auto runTo = [&] (double ticks)
                {
                    for (int b = 0; b < (int) (10.0 * sr) / block && heardTicks() < ticks; ++b)
                    {
                        buffer.clear();
                        audio.processHostedBlock (buffer);
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
                    }
                };

                runTo (2 * ticksPerBeat + 2);
                out->sendMessageNow (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 96));
                settleMidi();
                runTo (2 * ticksPerBeat + ticksPerBeat / 2 + 2);
                out->sendMessageNow (juce::MidiMessage::noteOff (1, 67));
                settleMidi();
                audio.stop();
                audio.setRecordArmed (false);

                const auto recorded = pattern.notesFor (lead);
                check (recorded.size() == 1 && recorded[0].start == 2 * ticksPerBeat && recorded[0].key == 67 && recorded[0].velocity == 96
                           && recorded[0].length == ticksPerBeat / 2,
                       "a note played into it on beat 3 for an eighth is recorded there (" + juce::String ((int) recorded.size()) + " note"
                           + (recorded.size() == 1 ? ", at tick " + juce::String (recorded[0].start) + ", " + juce::String (recorded[0].length) + " long)" : juce::String ("s)")));
            }

            out.reset();
            AudioEngine::headlessMidiDevices = false;
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--midiouttest"))
        {
            // Synths plugged in (MIDI out), with two made-up outputs whose messages are caught with
            // their times: a channel's pattern out on its MIDI channel, each note heard where the song
            // has it (the graph's latency after the block that plays it); timing earlier (looking
            // ahead, round the loop) and later; the clock, start and stop; what a keyboard plays,
            // straight out; a mute, a program, a stop letting go, an edit while playing, Song mode
            // through a tempo change and from bar 2; saving; and nothing left held
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            const double sr = 48000.0;
            const int block = 512;
            MidiOutHub::testOutputs = { "Test synth", "Drum machine" };
            AudioEngine::routeHostedMidi = true;

            Song song;
            song.loadBlank();
            song.bpm = 120.0;
            auto& keysChannel = song.addChannel ("Keys", InstrumentType::midiOut);
            keysChannel.midiOutDevice = "Test synth";
            keysChannel.midiOutChannel = 2;
            const int keys = keysChannel.id;
            const int lead = song.addChannel ("Lead", InstrumentType::synth).id;
            song.selectedChannelId = keys;

            // One bar: beat 1 for an eighth, beat 2 for a quarter, the and of 3 for a 16th; Lead plays too
            auto& pattern = song.currentPattern();
            pattern.notesFor (keys) = { { 0, 48, 60, 100 }, { 96, 96, 64, 90 }, { 240, 24, 67, 80 } };
            pattern.notesFor (lead) = { { 0, 96, 48, 100 } };
            song.mode = PlayMode::pattern;
            song.notify (Song::Change::everything);
            AudioEngine audio (song, false);
            audio.syncNow();

            auto o = audio.getOptions();
            o.metronome = false;
            o.countInBars = 0;
            audio.setOptions (o);

            juce::AudioBuffer<float> buffer (2, block);
            auto run = [&] (double seconds, std::optional<juce::MidiMessage> first = std::nullopt)
            {
                for (int b = 0; b < (int) std::ceil (seconds * sr / block); ++b)
                {
                    juce::MidiBuffer midi;

                    if (b == 0 && first.has_value())
                        midi.addEvent (*first, 0);

                    buffer.clear();
                    audio.processHostedBlock (buffer, midi);

                    if (b % 8 == 0)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
                }
            };

            auto& hub = audio.getMidiOut();
            using Sent = MidiOutHub::Sent;

            auto take = [&]
            {
                hub.flush();
                return hub.takeSent();
            };

            auto onlyFrom = [] (const std::vector<Sent>& all, const juce::String& output, std::function<bool (const juce::MidiMessage&)> wanted)
            {
                std::vector<Sent> list;

                for (auto& s : all)
                    if (s.output == output && wanted (s.message))
                        list.push_back (s);

                return list;
            };

            auto isOn = [] (const juce::MidiMessage& m) { return m.isNoteOn(); };
            auto isNote = [] (const juce::MidiMessage& m) { return m.isNoteOnOrOff(); };

            // For a failure: what went out, when it's heard (from the first), and when it was sent
            auto listOf = [] (const std::vector<Sent>& list, size_t from = 0, size_t count = 12)
            {
                juce::StringArray lines;
                const double origin = list.empty() ? 0.0 : list.front().heardMs;

                for (size_t i = from; i < std::min (list.size(), from + count); ++i)
                    lines.add (juce::String (list[i].heardMs - origin, 3) + "/" + juce::String (list[i].sentMs - origin, 3) + " "
                               + list[i].message.getDescription());

                return " [" + lines.joinIntoString ("; ") + "]";
            };

            // Every note let go by the end: as many offs as ons, key by key
            auto balanced = [] (const std::vector<Sent>& all)
            {
                std::map<juce::String, int> count;

                for (auto& s : all)
                    if (s.message.isNoteOnOrOff())
                        count[s.output + ":" + juce::String (s.message.getChannel()) + ":" + juce::String (s.message.getNoteNumber())]
                            += s.message.isNoteOn() ? 1 : -1;

                return std::all_of (count.begin(), count.end(), [] (auto& c) { return c.second == 0; });
            };

            const auto names = audio.getMidiOutputNames();
            check (names == juce::StringArray { "Test synth", "Drum machine" }, "the outputs there are: " + names.joinIntoString (", "));

            // Pattern mode, round and round (a bar at 120 is 2 s)
            run (0.2);
            take();
            audio.togglePlay();
            run (4.6);
            audio.stop();
            run (0.2);
            auto sent = take();
            auto notes = onlyFrom (sent, "Test synth", isNote);
            auto ons = onlyFrom (sent, "Test synth", isOn);
            const double latency = hub.getLatencyMs();

            {
                bool channel2 = ! notes.empty();

                for (auto& n : notes)
                    channel2 = channel2 && n.message.getChannel() == 2;

                check (ons.size() >= 6 && channel2, "Keys' notes go out of Test synth on MIDI channel 2 (" + juce::String ((int) ons.size()) + " notes in 2.3 passes)");
            }

            if (ons.size() >= 6)
            {
                const double t0 = ons[0].heardMs;
                const bool keysRight = ons[0].message.getNoteNumber() == 60 && ons[1].message.getNoteNumber() == 64 && ons[2].message.getNoteNumber() == 67
                                    && ons[0].message.getVelocity() == 100 && ons[1].message.getVelocity() == 90 && ons[2].message.getVelocity() == 80;
                check (keysRight, "C, E and G at velocities 100, 90 and 80");
                check (std::abs (ons[1].heardMs - t0 - 500.0) < 0.05 && std::abs (ons[2].heardMs - t0 - 1250.0) < 0.05
                           && std::abs (ons[3].heardMs - t0 - 2000.0) < 0.05 && std::abs (ons[5].heardMs - t0 - 3250.0) < 0.05,
                       "each heard where the song has it: beat 2 at +500 ms, the and of 3 at +1250, round the loop at +2000 and +3250 ("
                           + juce::String (ons[1].heardMs - t0, 3) + ", " + juce::String (ons[2].heardMs - t0, 3) + ", "
                           + juce::String (ons[3].heardMs - t0, 3) + ", " + juce::String (ons[5].heardMs - t0, 3) + ")");

                // The first note's end, an eighth (250 ms) later
                const auto offs = onlyFrom (sent, "Test synth", [] (const juce::MidiMessage& m) { return m.isNoteOff() && m.getNoteNumber() == 60; });
                check (! offs.empty() && std::abs (offs[0].heardMs - t0 - 250.0) < 0.05, "and let go an eighth later (+"
                           + (offs.empty() ? juce::String ("never") : juce::String (offs[0].heardMs - t0, 3)) + " ms)");

                // Sent as it's heard: the block that plays it, plus what the graph holds it back
                const bool onTime = std::all_of (ons.begin(), ons.end(), [] (const Sent& s) { return std::abs (s.sentMs - s.heardMs) < 0.001; });
                check (onTime && latency > 0.0, "timing 0: each sent the moment it's heard, the graph's " + juce::String (latency, 2) + " ms after its block");
            }

            check (balanced (sent), "every note let go by the time it's stopped");
            check (onlyFrom (sent, "Drum machine", [] (const juce::MidiMessage&) { return true; }).empty(), "nothing to the drum machine, which nothing plays");

            // Lead, a built-in synth, never goes out
            check (std::none_of (sent.begin(), sent.end(), [] (const Sent& s) { return s.message.isNoteOn() && s.message.getNoteNumber() == 48; }),
                   "Lead's note (its own synth) goes to no output");

            // Earlier for a slow synth: 30 ms ahead, looking past the loop's end for the next bar's first note
            audio.setMidiOutSettings ("Test synth", { -30, false });
            audio.syncNow();
            audio.setPlayheadTicks (0);
            take();
            audio.togglePlay();
            run (4.6);
            audio.stop();
            run (0.2);
            sent = take();
            ons = onlyFrom (sent, "Test synth", isOn);

            if (ons.size() >= 6)
            {
                bool early = true;

                for (size_t i = 1; i < ons.size(); ++i)
                    early = early && std::abs (ons[i].sentMs - (ons[i].heardMs - 30.0)) < 0.001;

                // The second bar's downbeat goes out 30 ms before the bar starts, while the first is still playing
                check (early && ons[3].sentMs < ons[3].heardMs - 29.99,
                       "30 ms earlier: each note sent 30 ms before it's heard, the next pass's first note before the loop comes round ("
                           + juce::String (ons[3].heardMs - ons[3].sentMs, 3) + " ms ahead)");
                check (ons[0].sentMs <= ons[0].heardMs - std::min (30.0, latency) + 0.001,
                       "the very first, as early as it can go after Play (" + juce::String (ons[0].heardMs - ons[0].sentMs, 2) + " ms ahead)");
            }
            else
            {
                check (false, "30 ms earlier: only " + juce::String ((int) ons.size()) + " notes sent");
            }

            check (balanced (sent), "every note let go, sent early too");

            // Later
            audio.setMidiOutSettings ("Test synth", { 20, false });
            audio.syncNow();
            audio.setPlayheadTicks (0);
            take();
            audio.togglePlay();
            run (2.2);
            audio.stop();
            run (0.2);
            ons = onlyFrom (take(), "Test synth", isOn);
            check (! ons.empty() && std::all_of (ons.begin(), ons.end(), [] (const Sent& s) { return std::abs (s.sentMs - s.heardMs - 20.0) < 0.001; }),
                   "20 ms later: each sent 20 ms after it's heard");
            audio.setMidiOutSettings ("Test synth", {});
            check (audio.getOptions().midiOuts.empty(), "back on time, nothing kept for it");

            // The clock on the drum machine: a song position and start on the downbeat, 24 to the beat,
            // a start again as the pattern comes round, stop with Stop
            audio.setMidiOutSettings ("Drum machine", { 0, true });
            audio.syncNow();
            audio.setPlayheadTicks (0);
            take();
            audio.togglePlay();
            run (2.6);
            audio.stop();
            run (0.2);
            sent = take();
            auto clock = onlyFrom (sent, "Drum machine", [] (const juce::MidiMessage&) { return true; });

            {
                std::vector<double> ticks, starts;
                bool stopLast = ! clock.empty() && clock.back().message.getRawData()[0] == 0xfc;
                bool positionFirst = clock.size() > 2 && clock[0].message.isSongPositionPointer()
                                  && clock[0].message.getSongPositionPointerMidiBeat() == 0 && clock[1].message.isMidiStart();

                for (auto& c : clock)
                {
                    if (c.message.isMidiClock())
                        ticks.push_back (c.heardMs);

                    if (c.message.isMidiStart())
                        starts.push_back (c.heardMs);
                }

                bool steady = ticks.size() > 50;
                size_t odd = 0;

                for (size_t i = 1; i < ticks.size(); ++i)
                    if (std::abs (ticks[i] - ticks[i - 1] - 500.0 / 24.0) >= 0.01 && odd == 0)
                        odd = i;

                steady = steady && odd == 0;

                // Where it went wrong, if it did: the messages round the first odd gap
                size_t at = 0;

                for (size_t i = 0, k = 0; i < clock.size(); ++i)
                    if (clock[i].message.isMidiClock() && k++ == odd)
                        at = i;

                check (positionFirst, "the drum machine gets a song position of 0 and start first");
                check (steady, "then a clock every 20.833 ms, 24 to the beat at 120 (" + juce::String ((int) ticks.size()) + " in 2.6 s)"
                           + (steady ? juce::String() : listOf (clock, at >= 4 ? at - 4 : 0, 10)));
                check (starts.size() == 2 && ! ticks.empty() && std::abs (starts[1] - starts[0] - 2000.0) < 0.05 && std::abs (starts[0] - ticks[0]) < 0.001,
                       "start again as the bar comes round, 2 s on, the first clock with the first start (" + juce::String ((int) starts.size()) + " starts)"
                           + (starts.size() == 2 ? juce::String() : listOf (clock, 0, 6)));
                check (stopLast, "and stop when the song stops");
            }

            // Its notes out with the clock's: the song position before the downbeat's note at the same moment
            {
                const auto first = onlyFrom (sent, "Test synth", isOn);
                check (! first.empty() && ! clock.empty() && std::abs (first[0].heardMs - clock[1].heardMs) < 0.001,
                       "Keys' first note and the drum machine's start land together");
            }

            audio.setMidiOutSettings ("Drum machine", {});

            // What a keyboard plays on Keys goes out at once, on Keys' MIDI channel: one plugged in
            // (the engine's input, routed as a keyboard's) and the computer's
            {
                auto hasInput = [&audio]
                {
                    for (auto& d : audio.getEdit().engine.getDeviceManager().getMidiInDevices())
                        if (tracktion::HostedAudioDeviceInterface::isHostedMidiInputDevice (*d))
                            return true;

                    return false;
                };

                for (int i = 0; i < 100 && ! hasInput(); ++i)
                    settleMessages (20);

                settleMessages (50);
                audio.syncNow();
                run (0.1);
                take();
                const double before = hub.nowMs();
                run (0.05, juce::MidiMessage::noteOn (1, 72, (juce::uint8) 99));
                run (0.05, juce::MidiMessage::noteOff (1, 72));
                auto live = onlyFrom (take(), "Test synth", isNote);
                check (live.size() == 2 && live[0].live && live[0].message.getChannel() == 2 && live[0].message.getNoteNumber() == 72
                           && live[0].message.getVelocity() == 99 && live[0].sentMs - before < 12.0 && live[1].message.isNoteOff(),
                       "a key on a keyboard plugged in goes straight out on channel 2, within a block ("
                           + (live.empty() ? juce::String ("nothing") : juce::String (live[0].sentMs - before, 1) + " ms") + "), and its release");

                audio.liveNoteOn (keys, 74, 70);
                run (0.05);
                audio.liveNoteOff (keys, 74);
                run (0.05);
                live = onlyFrom (take(), "Test synth", isNote);
                check (live.size() == 2 && live[0].message.getNoteNumber() == 74 && live[0].message.getChannel() == 2 && live[1].message.isNoteOff(),
                       "the computer keyboard's too");

                audio.sendTestNote (keys);
                run (0.05);
                settleMessages (450);
                run (0.05);
                live = onlyFrom (take(), "Test synth", isNote);
                check (live.size() == 2 && live[0].message.getNoteNumber() == 60 && live[1].message.isNoteOff(), "a test note: middle C on and off");
            }

            // A program asked for goes first as playing starts; muted, it plays nothing
            {
                auto* c = song.findChannel (keys);
                c->midiProgram = 5;
                song.notify (Song::Change::channels);
                audio.syncNow();
                audio.setPlayheadTicks (0);
                take();
                audio.togglePlay();
                run (0.6);
                audio.stop();
                run (0.1);
                sent = onlyFrom (take(), "Test synth", [] (const juce::MidiMessage&) { return true; });
                check (! sent.empty() && sent[0].message.isProgramChange() && sent[0].message.getProgramChangeNumber() == 5
                           && sent[0].message.getChannel() == 2 && sent.size() > 1 && sent[1].message.isNoteOn(),
                       "program 5 on channel 2 goes out first, then the notes");

                c->muted = true;
                song.notify (Song::Change::mixer);
                audio.syncNow();
                audio.setPlayheadTicks (0);
                take();
                audio.togglePlay();
                run (1.0);
                audio.liveNoteOn (keys, 76, 70);
                run (0.05);
                audio.liveNoteOff (keys, 76);
                audio.stop();
                run (0.1);
                ons = onlyFrom (take(), "Test synth", isOn);
                check (ons.empty(), "muted, Keys plays nothing, its pattern or a key");
                c->muted = false;
                c->midiProgram = -1;
                song.notify (Song::Change::mixer);
                audio.syncNow();
            }

            // Stopping mid-note lets go of it; editing while playing leaves nothing held
            {
                pattern.notesFor (keys) = { { 0, 384, 62, 100 } };
                song.notify (Song::Change::notes);
                audio.syncNow();
                audio.setPlayheadTicks (0);
                take();
                audio.togglePlay();
                run (0.5);
                pattern.notesFor (keys) = { { 96, 48, 65, 100 } };
                song.notify (Song::Change::notes);
                audio.syncNow();
                run (2.5);
                audio.stop();
                run (0.1);
                sent = take();
                ons = onlyFrom (sent, "Test synth", isOn);
                check (ons.size() >= 2 && ons[0].message.getNoteNumber() == 62 && ons[1].message.getNoteNumber() == 65 && balanced (sent),
                       "a held note let go when its pattern changes under it, the new one played, nothing left held"
                           + listOf (onlyFrom (sent, "Test synth", isNote)));

                pattern.notesFor (keys) = { { 0, 384, 62, 100 } };
                song.notify (Song::Change::notes);
                audio.syncNow();
                audio.setPlayheadTicks (0);
                take();
                audio.togglePlay();
                run (0.5);
                audio.stop();
                run (0.1);
                sent = take();
                const auto offs = onlyFrom (sent, "Test synth", [] (const juce::MidiMessage& m) { return m.isNoteOff(); });
                check (offs.size() == 1 && balanced (sent), "stopped in the middle of a bar-long note, it's let go");
            }

            // Song mode: a clip at bar 1 and bar 3, the tempo halving at bar 3; played from bar 2
            {
                pattern.notesFor (keys) = { { 0, 48, 60, 100 } };
                song.mode = PlayMode::song;
                const int row = song.trackForChannel (keys, 0, 384);
                song.playlist = { { pattern.id, row, 0 }, { pattern.id, row, 2 * 384 } };
                song.setTempoAt (2 * 384, 60.0);
                song.notify (Song::Change::everything);
                audio.setMidiOutSettings ("Test synth", { 0, true });
                audio.syncNow();
                audio.setPlayheadTicks (0);
                take();
                audio.togglePlay();
                run (5.0);
                audio.stop();
                run (0.1);
                sent = onlyFrom (take(), "Test synth", [] (const juce::MidiMessage&) { return true; });
                ons = onlyFrom (sent, "Test synth", isOn);
                std::vector<double> ticks;

                for (auto& s : sent)
                    if (s.message.isMidiClock())
                        ticks.push_back (s.heardMs);

                check (ons.size() == 2 && std::abs (ons[1].heardMs - ons[0].heardMs - 4000.0) < 0.05,
                       "Song mode: bar 3's note 4 s after bar 1's (two bars at 120)");

                // The clock's beat is twice as long once the tempo halves
                bool halved = ticks.size() > 120;

                if (halved)
                {
                    const double changeAt = ons[0].heardMs + 4000.0;
                    double before = 0.0, after = 0.0;

                    for (size_t i = 1; i < ticks.size(); ++i)
                    {
                        if (ticks[i] < changeAt - 1.0)
                            before = ticks[i] - ticks[i - 1];
                        else if (ticks[i - 1] > changeAt + 1.0)
                            after = ticks[i] - ticks[i - 1];
                    }

                    halved = std::abs (before - 500.0 / 24.0) < 0.01 && std::abs (after - 1000.0 / 24.0) < 0.01;
                    check (halved, "the clock follows the tempo: every " + juce::String (before, 3) + " ms at 120, every " + juce::String (after, 3) + " ms at 60"
                               + (halved ? juce::String() : listOf (sent, 0, 14)));
                }
                else
                {
                    check (false, "the clock follows the tempo (only " + juce::String ((int) ticks.size()) + " clocks)");
                }

                // From bar 2: a song position of 16 sixteenths and continue
                audio.setPlayheadTicks (384);
                take();
                audio.togglePlay();
                run (0.5);
                audio.stop();
                run (0.1);
                sent = onlyFrom (take(), "Test synth", [] (const juce::MidiMessage&) { return true; });
                check (sent.size() > 2 && sent[0].message.isSongPositionPointer() && sent[0].message.getSongPositionPointerMidiBeat() == 16
                           && sent[1].message.isMidiContinue(),
                       "played from bar 2: song position 16 (16ths) and continue");
                audio.setMidiOutSettings ("Test synth", {});
            }

            // Saved with the song; switched to a built-in sound, it goes out no more
            {
                Song saved;
                saved.fromVar (song.toVar());
                auto* c = saved.findChannel (keys);
                check (c != nullptr && c->type == InstrumentType::midiOut && c->midiOutDevice == "Test synth" && c->midiOutChannel == 2,
                       "a synth plugged in is saved with the song: its output and MIDI channel");

                auto* k = song.findChannel (keys);
                k->type = InstrumentType::synth;
                k->synthPreset = "Velvet Keys";
                song.notify (Song::Change::channels);
                audio.syncNow();
                audio.setPlayheadTicks (0);
                take();
                audio.togglePlay();
                run (0.6);
                audio.stop();
                run (0.1);
                check (onlyFrom (take(), "Test synth", isOn).empty() && audio.getTrack (keys) != nullptr && ! audio.getTrack (keys)->getClips().isEmpty(),
                       "made a NightShift synth again, Keys plays its clips itself and nothing goes out");
            }

            // The AI drummer on a synth plugged in writes General MIDI's drums
            {
                Channel drums;
                drums.type = InstrumentType::midiOut;
                drums.midiOutChannel = 10;
                const auto map = drummer::keyMapFor (drums);
                check (map ("kick") == 36 && map ("snare") == 38 && map ("hat_closed") == 42, "the AI drummer plays a drum machine plugged in on General MIDI's keys");
            }

            AudioEngine::routeHostedMidi = false;
            MidiOutHub::testOutputs.clear();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--midifiletest"))
        {
            // MIDI files: a song written out and read back note for note (tempo jumps and a glide,
            // a 7/8 stretch, a split clip, a synth plugged in on its own channel, programs and
            // names), brought back in as channels; one pattern's; a file of another program's
            // (480 to the beat, every channel in one track, 3/8, a drum part, a note never let go);
            // and something that isn't one
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            drumkits::machines::ensure();
            const auto folder = juce::File::createTempFile ("midifiles");
            folder.createDirectory();

            Song song;
            song.loadBlank();
            song.bpm = 100.0;
            const int bar = song.ticksPerBar();

            auto& leadChannel = song.addChannel ("Lead", InstrumentType::synth);
            leadChannel.synthPreset = "Supersaw Lead";
            leadChannel.midiProgram = 81;
            const int lead = leadChannel.id;
            const int bass = song.addChannel ("Bass", InstrumentType::synth).id;
            auto& outChannel = song.addChannel ("Synth out", InstrumentType::midiOut);
            outChannel.midiOutDevice = "Test synth";
            outChannel.midiOutChannel = 3;
            outChannel.midiProgram = 5;
            const int out = outChannel.id;

            auto& riff = song.addPattern ("Riff");
            riff.notesFor (lead) = { { 0, 48, 72, 100 }, { 48, 24, 74, 64 }, { 96, 200, 76, 127 }, { 300, 84, 79, 1 } };
            riff.notesFor (bass) = { { 0, 96, 36, 110 }, { 192, 96, 43, 90 } };
            const int riffId = riff.id;
            auto& verse = song.addPattern ("Verse");
            verse.notesFor (out) = { { 0, 384, 60, 80 }, { 0, 384, 64, 80 }, { 384, 192, 67, 70 }, { 576, 1, 69, 70 } };
            const int verseId = verse.id;

            // Bar 1 and (split: its second half only) bar 3; the verse from bar 5, into the 7/8
            const int row = song.trackForChannel (lead, 0, bar);
            song.playlist = { { riffId, row, 0 }, { riffId, row, 2 * bar + 144, 144, 240 }, { verseId, song.trackForChannel (out, 4 * bar, 6 * bar), 4 * bar } };
            song.setTempoAt (2 * bar, 132.0);
            song.setTempoAt (6 * bar, 90.0, true);
            song.setMeterAt (4 * bar, 7, 8);
            song.setMeterAt (song.barStart (8), 4, 4);
            song.mode = PlayMode::song;

            const auto played = midifile::notesAsPlayed (song, 0, true);
            const auto file = folder.getChildFile ("song.mid");
            const bool saved = midifile::save (song, file);
            check (saved && file.getSize() > 100, "the song saves as a MIDI file (" + juce::String (file.getSize()) + " bytes)");

            juce::String error;
            const auto read = midifile::read (file, error);
            check (read.has_value(), "and reads back" + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));

            if (read.has_value())
            {
                const auto& parts = read->parts;
                check (parts.size() == 3 && parts[0].name == "Lead" && parts[1].name == "Bass" && parts[2].name == "Synth out",
                       "a part for each channel with notes, named after it (" + juce::String ((int) parts.size()) + ")");

                if (parts.size() == 3)
                {
                    check (parts[2].midiChannel == 3 && parts[0].midiChannel != 10 && parts[1].midiChannel != 10 && parts[0].midiChannel != parts[1].midiChannel
                               && parts[0].midiChannel != 3 && parts[1].midiChannel != 3,
                           "the synth plugged in on its own channel 3, the others each on one of their own (" + juce::String (parts[0].midiChannel)
                               + ", " + juce::String (parts[1].midiChannel) + ")");
                    check (parts[0].program == 81 && parts[1].program == -1 && parts[2].program == 5, "Lead's program 81 and the synth's 5 come back; Bass has none");

                    // Note for note: each channel's notes as the song plays them
                    bool same = true;
                    int count = 0;
                    const int ids[] = { lead, bass, out };

                    for (int p = 0; p < 3; ++p)
                    {
                        std::vector<Note> expected;

                        for (auto& n : played)
                            if (n.channelId == ids[p])
                                expected.push_back ({ (int) n.start, (int) (n.end - n.start), n.key, n.velocity });

                        same = same && expected == parts[(size_t) p].notes;
                        count += (int) expected.size();
                    }

                    check (same && count == 12, "every note back where it was, as long, on its key and as hard (" + juce::String (count)
                               + " notes, the split clip's second half only, cut at its end)");
                }

                check (std::abs (read->bpm - 100.0) < 1.0e-6 && read->meterBeats == 4 && read->meterUnit == 4, "100 to the minute in 4/4 at the start");

                const bool meters = read->meterChanges.size() == 2 && read->meterChanges[0] == MeterChange { 4 * bar, 7, 8 }
                                 && read->meterChanges[1] == MeterChange { song.barStart (8), 4, 4 };
                check (meters, "7/8 from bar 5 and 4/4 again from bar 9, where they were");

                // The tempo: the jump exact, the glide a 16th at a time, in time with it all the way
                Song back;
                back.loadBlank();
                back.bpm = read->bpm;
                back.meterBeats = read->meterBeats;
                back.meterUnit = read->meterUnit;
                back.tempoChanges = read->tempoChanges;
                back.meterChanges = read->meterChanges;
                back.tidyTimeline();
                double worst = 0.0;

                for (int t = 0; t <= 10 * bar; t += ticksPerStep)
                    worst = std::max (worst, std::abs (back.secondsAt (t) - song.secondsAt (t)));

                const bool jump = std::any_of (read->tempoChanges.begin(), read->tempoChanges.end(),
                                               [bar] (const TempoChange& t) { return t.tick == 2 * bar && std::abs (t.bpm - 132.0) < 1.0e-6; });
                check (jump && worst < 0.0005, "the tempo jump to 132 at bar 3 exact, the glide to 90 kept in time (worst "
                           + juce::String (worst * 1000.0, 3) + " ms over ten bars)");
                check (read->warnings.isEmpty(), "nothing it had to change" + (read->warnings.isEmpty() ? juce::String() : ": " + read->warnings.joinIntoString ("; ")));

                // Brought in as channels: the same notes play, the same tempo and time signatures
                Song in;
                in.loadBlank();
                const auto added = midifile::addToSong (in, *read, true);
                const auto again = midifile::notesAsPlayed (in, 0, true);
                bool same = added.size() == 3 && again.size() == played.size();

                for (size_t i = 0; same && i < again.size(); ++i)
                {
                    const auto& a = again[i];
                    const auto& b = played[i];
                    const int index = (int) (std::find (added.begin(), added.end(), a.channelId) - added.begin());
                    const int originalId = index == 0 ? lead : index == 1 ? bass : out;
                    same = a.start == b.start && a.end == b.end && a.key == b.key && a.velocity == b.velocity && b.channelId == originalId;
                }

                check (same, "brought in, its three channels play the same " + juce::String ((int) again.size()) + " notes");
                check (in.bpm == 100.0 && in.tempoChanges == back.tempoChanges && in.meterChanges == back.meterChanges && in.mode == PlayMode::song,
                       "with the file's tempo and time signatures, in Song mode");

                auto* c = added.size() == 3 ? in.findChannel (added[0]) : nullptr;
                check (c != nullptr && c->name == "Lead" && c->midiProgram == 81 && c->type == InstrumentType::synth && c->synthPreset == "Supersaw Lead",
                       "Lead comes in on Supersaw Lead (General MIDI's 82, a saw lead), its program kept");

                // Each channel's clips on a track of its own, from the bar its part starts
                std::set<int> rows;

                for (auto& clip : in.playlist)
                    rows.insert (clip.track);

                const auto* outPart = added.size() == 3 ? in.findChannel (added[2]) : nullptr;
                const auto clipFor = [&in] (int channelId) -> const PlaylistClip*
                {
                    for (auto& clip : in.playlist)
                        if (auto p = in.findPattern (clip.patternId); p != nullptr && p->findNotes (channelId) != nullptr)
                            return &clip;

                    return nullptr;
                };

                check (rows.size() == 3 && in.playlist.size() == 3 && outPart != nullptr && clipFor (outPart->id) != nullptr
                           && clipFor (outPart->id)->start == 4 * bar,
                       "a clip each on a track of its own, Synth out's from bar 5, where its notes start");
            }

            // One pattern's notes, at the start's tempo in its own time signature
            {
                const auto patternFile = folder.getChildFile ("riff.mid");
                midifile::save (song, patternFile, riffId);
                juce::String patternError;
                const auto one = midifile::read (patternFile, patternError);
                bool same = one.has_value() && one->parts.size() == 2 && one->tempoChanges.empty() && one->meterChanges.empty()
                         && std::abs (one->bpm - 100.0) < 1.0e-6;

                if (same)
                {
                    const auto expected = midifile::notesAsPlayed (song, riffId, true);
                    std::vector<Note> leadNotes;

                    for (auto& n : expected)
                        if (n.channelId == lead)
                            leadNotes.push_back ({ (int) n.start, (int) (n.end - n.start), n.key, n.velocity });

                    same = leadNotes == one->parts[0].notes && leadNotes.size() == 4;
                }

                check (same, "one pattern exported: its two channels' notes, at 100 with no changes");
            }

            // Another program's file: 480 to the beat, all of it in one track, 90 to the minute in 3/8
            {
                juce::MidiMessageSequence track;
                auto add = [&track] (juce::MidiMessage m, double tick) { m.setTimeStamp (tick); track.addEvent (m); };
                add (juce::MidiMessage::textMetaEvent (3, "Song"), 0);
                add (juce::MidiMessage::tempoMetaEvent (666667), 0);
                add (juce::MidiMessage::timeSignatureMetaEvent (3, 8), 0);
                add (juce::MidiMessage::programChange (1, 33), 0);
                add (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100), 0);
                add (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 0), 240);        // a note-on at 0 is a release
                add (juce::MidiMessage::noteOn (1, 43, (juce::uint8) 90), 480);
                add (juce::MidiMessage::noteOff (1, 43), 960);
                add (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 120), 0);
                add (juce::MidiMessage::noteOff (10, 36), 60);
                add (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 110), 480);
                add (juce::MidiMessage::noteOff (10, 38), 540);
                add (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 70), 120);       // never let go: it lasts to the end
                add (juce::MidiMessage::endOfTrack(), 1440);

                juce::MidiFile written;
                written.setTicksPerQuarterNote (480);
                written.addTrack (track);
                const auto otherFile = folder.getChildFile ("other.mid");

                {
                    juce::FileOutputStream stream (otherFile);
                    written.writeTo (stream, 0);
                }

                juce::String otherError;
                const auto other = midifile::read (otherFile, otherError);
                check (other.has_value() && other->parts.size() == 3, "another program's file (480 to the beat, every channel in one track) reads as 3 parts");

                if (other.has_value() && other->parts.size() == 3)
                {
                    const auto& bassPart = other->parts[0];
                    const auto& padPart = other->parts[1];
                    const auto& drumPart = other->parts[2];
                    check (bassPart.midiChannel == 1 && bassPart.program == 33 && bassPart.name == "Song - Electric Bass (finger)"
                               && drumPart.isDrums() && drumPart.name == "Song - Drums" && padPart.midiChannel == 2,
                           "split by channel and named for the track and what each plays (\"" + bassPart.name + "\", \"" + drumPart.name + "\")");
                    check (bassPart.notes == std::vector<Note> { { 0, 48, 40, 100 }, { 96, 96, 43, 90 } }, "the bass's notes in NightShift's ticks, a release by a silent note-on");
                    check (padPart.notes.size() == 1 && padPart.notes[0].start == 24 && padPart.notes[0].end() == 288, "a note never let go lasts to the track's end");
                    check (std::abs (other->bpm - 90.0) < 0.001 && other->meterBeats == 6 && other->meterUnit == 8
                               && other->warnings.contains ("3/8 plays as 6/8"),
                           "90 to the minute; 3/8, which NightShift hasn't, as 6/8 and said so (" + other->warnings.joinIntoString ("; ") + ")");

                    // Brought into a song with its own tempo: the bass on a bass sound, the drums on a kit
                    Song in;
                    in.loadBlank();
                    in.bpm = 140.0;
                    const auto added = midifile::addToSong (in, *other, false, 4 * in.ticksPerBar());
                    auto* bassChannel = added.size() == 3 ? in.findChannel (added[0]) : nullptr;
                    auto* drumChannel = added.size() == 3 ? in.findChannel (added[2]) : nullptr;
                    check (in.bpm == 140.0 && in.tempoChanges.empty() && in.meterBeats == 4, "dropped into a song, the song keeps its tempo and time signature");
                    juce::StringArray what;

                    for (auto id : added)
                        if (auto* c = in.findChannel (id))
                            what.add (c->name + ": type " + juce::String ((int) c->type) + " " + c->synthPreset + c->kit + c->instrument);

                    check (bassChannel != nullptr && bassChannel->type == InstrumentType::synth && bassChannel->synthPreset == "Finger Bass",
                           "the bass on Finger Bass (" + what.joinIntoString ("; ") + ")");

                    bool kitKeys = drumChannel != nullptr && drumChannel->type == InstrumentType::kit && drumChannel->kit.isNotEmpty();

                    if (kitKeys)
                    {
                        auto kit = drumkits::load (drumChannel->kit);
                        const Pattern* drumPattern = nullptr;

                        for (auto& p : in.patterns)
                            if (p.findNotes (drumChannel->id) != nullptr)
                                drumPattern = &p;

                        kitKeys = kit != nullptr && drumPattern != nullptr && drumPattern->findNotes (drumChannel->id)->size() == 2
                               && (*drumPattern->findNotes (drumChannel->id))[0].key == drumkits::keyForVoice (*kit, "kick")
                               && (*drumPattern->findNotes (drumChannel->id))[1].key == drumkits::keyForVoice (*kit, "snare");
                    }

                    check (kitKeys, "the drums on a drum kit" + (drumChannel != nullptr ? " (" + drumkits::displayName (drumChannel->kit) + ")" : juce::String())
                               + ", General MIDI's kick and snare on the kit's own");

                    const PlaylistClip* first = nullptr;

                    for (auto& clip : in.playlist)
                        if (first == nullptr || clip.start < first->start)
                            first = &clip;

                    check (first != nullptr && first->start == 4 * in.ticksPerBar(), "placed from bar 5, where it was dropped");
                }
            }

            // Not a MIDI file
            {
                const auto notMidi = folder.getChildFile ("not.mid");
                notMidi.replaceWithText ("hello");
                juce::String notError;
                const bool refused = ! midifile::read (notMidi, notError).has_value();
                check (refused && notError.isNotEmpty(), "a file that isn't one says so: \"" + notError + "\"");
            }

            folder.deleteRecursively();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--pianotest"))
        {
            // The piano roll's channel on the demo: it follows the rack's selection but never
            // onto an audio track (which once left it blank, with no way to pick a channel),
            // and its picker lists the pattern's channels first and switches between them
            Song song;
            song.loadDemo();
            AudioEngine audio (song, false);
            PianoRoll roll (song, audio);
            roll.setSize (1000, 500);

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            auto idOf = [&song] (const juce::String& name)
            {
                for (auto& c : song.channels)
                    if (c.name == name)
                        return c.id;

                return 0;
            };

            auto menuItems = [&roll]
            {
                juce::StringArray items;
                auto menu = roll.createChannelMenu();

                for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                {
                    auto& item = it.getItem();
                    items.add ((item.isSectionHeader ? "# " : item.isTicked ? "* " : "") + item.text);
                }

                return items;
            };

            auto click = [&roll] (juce::Point<float> p)
            {
                const auto now = juce::Time::getCurrentTime();
                const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), p,
                                          juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          &roll, &roll, now, p, now, 1, false);
                roll.mouseDown (e);
                roll.mouseUp (e);
            };

            auto& beat = song.currentPattern();
            check (beat.name == "Beat" && roll.getChannelId() == idOf ("Kick"), "opens on the selected channel, Kick, in Beat");

            // Adding an audio track selects it, as it did before recording
            auto& track = song.addChannel ("Audio 1", InstrumentType::audio);
            const int trackId = track.id;
            song.selectedChannelId = trackId;
            song.notify (Song::Change::channels);
            check (roll.getChannelId() == idOf ("Kick") && roll.getTitle().contains ("Kick"),
                   "selecting an audio track leaves the piano roll on Kick");

            // The picker: this pattern's channels first, in rack order, then the rest; no audio tracks
            juce::StringArray expected { "# In Beat" };
            juce::StringArray others;

            for (auto& c : song.channels)
            {
                if (c.type == InstrumentType::audio)
                    continue;

                const auto name = (c.id == roll.getChannelId() ? "* " : "") + c.name;
                auto list = beat.findNotes (c.id);
                (list != nullptr && ! list->empty() ? expected : others).add (name);
            }

            expected.add ("# Other channels");
            expected.addArray (others);
            const auto items = menuItems();
            std::cout << "      menu: " << items.joinIntoString (" | ") << std::endl;
            check (items == expected, "the picker lists Beat's channels, then the others, without the audio track");

            // Choosing Snare from the picker
            {
                auto menu = roll.createChannelMenu();

                for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                    if (it.getItem().text == "Snare" && it.getItem().action != nullptr)
                        it.getItem().action();
            }

            check (roll.getChannelId() == idOf ("Snare") && song.selectedChannelId == idOf ("Snare"),
                   "choosing Snare edits Snare and selects it in the rack");
            check (menuItems().contains ("* Snare"), "the picker ticks Snare");

            // Writing a note with the audio track selected again goes to Snare
            song.selectedChannelId = trackId;
            song.notify (Song::Change::selection);
            const auto snareBefore = beat.notesFor (idOf ("Snare")).size();
            click ({ 400.0f, 120.0f });
            check (beat.notesFor (idOf ("Snare")).size() == snareBefore + 1 && beat.findNotes (trackId) == nullptr,
                   "clicking the grid with the audio track selected writes to Snare, not the track");

            // Tools: select leaves empty space alone, erase deletes the note it's clicked on
            const auto snareNow = beat.notesFor (idOf ("Snare")).size();
            roll.setTool (PianoRoll::Tool::select);
            click ({ 600.0f, 140.0f });
            check (beat.notesFor (idOf ("Snare")).size() == snareNow, "the select tool leaves empty space alone");
            roll.setTool (PianoRoll::Tool::erase);
            click ({ 402.0f, 120.0f });
            check (beat.notesFor (idOf ("Snare")).size() == snareNow - 1, "the erase tool deletes the note it's clicked on");
            roll.setTool (PianoRoll::Tool::draw);

            // Removing the channel being edited falls back to the first with notes here
            song.removeChannel (idOf ("Snare"));
            song.notify (Song::Change::channels);
            check (roll.getChannelId() == idOf ("Kick"), "removing Snare falls back to Kick");

            // With no instruments at all, nothing is edited and clicks write nothing
            std::vector<int> instruments;

            for (auto& c : song.channels)
                if (c.type != InstrumentType::audio)
                    instruments.push_back (c.id);

            for (auto id : instruments)
                song.removeChannel (id);

            song.notify (Song::Change::channels);
            click ({ 400.0f, 120.0f });
            check (roll.getChannelId() == 0 && beat.findNotes (0) == nullptr && menuItems().size() == 1,
                   "with only an audio track left it edits nothing and the picker says so");

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--devices"))
        {
            // Opens the audio interface the way the studio does and reports what it costs
            // in latency at each buffer size, playing the demo with the master fully down
            Song song;
            song.loadDemo();
            song.masterVolume = 0.0f;
            song.notify (Song::Change::mixer);

            AudioEngine audio (song, true);
            auto& dm = audio.getDeviceManager();

            // The engine builds its input list asynchronously once the device is open
            for (int i = 0; i < 50; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            audio.getEngine().getDeviceManager().dispatchPendingUpdates();
            audio.syncNow();

            for (auto* type : dm.getAvailableDeviceTypes())
            {
                type->scanForDevices();
                std::cout << type->getTypeName() << ": " << type->getDeviceNames().joinIntoString (" | ") << std::endl;
            }

            auto report = [&audio] (const juce::String& label)
            {
                const auto info = audio.getLatencyInfo();
                std::cout << label << info.driver << " / " << info.device << " @ " << info.sampleRate << " Hz, "
                          << info.bufferSize << " samples: in " << juce::String (info.toMs (info.inputSamples), 2) << " ms + out "
                          << juce::String (info.toMs (info.outputSamples), 2) << " ms = " << juce::String (info.roundTripMs(), 2)
                          << " ms round trip, cpu " << juce::roundToInt (info.cpu * 100.0) << "%, xruns " << info.xruns << std::endl;
            };

            report ("open: ");
            std::cout << "inputs: " << audio.getInputNames().joinIntoString (", ") << std::endl;

            if (auto device = dm.getCurrentAudioDevice())
            {
                juce::StringArray sizes;

                for (auto size : device->getAvailableBufferSizes())
                    sizes.add (juce::String (size));

                std::cout << "buffer sizes: " << sizes.joinIntoString (" ") << std::endl;
                std::cout << "input channels: " << device->getInputChannelNames().joinIntoString (", ") << std::endl;
                std::cout << "output channels: " << device->getOutputChannelNames().joinIntoString (", ") << std::endl;

                if (args.has ("--probe"))
                {
                    const auto original = device->getCurrentBufferSizeSamples();

                    for (int size : { 16, 32, 64, 128 })
                    {
                        if (! device->getAvailableBufferSizes().contains (size))
                            continue;

                        juce::AudioDeviceManager::AudioDeviceSetup setup;
                        dm.getAudioDeviceSetup (setup);
                        setup.bufferSize = size;
                        dm.setAudioDeviceSetup (setup, true);
                        audio.getEngine().getDeviceManager().dispatchPendingUpdates();

                        const int xrunsBefore = dm.getCurrentAudioDevice()->getXRunCount();
                        audio.togglePlay();
                        double peakCpu = 0.0;
                        const auto start = juce::Time::getMillisecondCounter();

                        while (juce::Time::getMillisecondCounter() - start < 4000)
                        {
                            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                            peakCpu = std::max (peakCpu, dm.getCpuUsage());
                        }

                        const bool played = audio.isPlaying();
                        audio.stop();
                        report ("  ");
                        std::cout << "    playing the demo for 4 s: " << (played ? "ran" : "STOPPED") << ", peak cpu "
                                  << juce::roundToInt (peakCpu * 100.0) << "%, new xruns "
                                  << dm.getCurrentAudioDevice()->getXRunCount() - xrunsBefore << std::endl;
                    }

                    juce::AudioDeviceManager::AudioDeviceSetup setup;
                    dm.getAudioDeviceSetup (setup);
                    setup.bufferSize = original;
                    dm.setAudioDeviceSetup (setup, true);
                    report ("restored: ");
                }
            }

            return 0;
        }

        if (args.has ("--kitimporttest"))
        {
            // Drum kit from a folder: sample names read the way packs name them, then a made-up pack
            // (one-shots named every which way, a Kicks folder, a pad that isn't a drum) imported
            // through the studio: the right drums with their takes, the stray left out, each take
            // at its drum's level, a channel playing it with its drummer open
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            const std::pair<const char*, const char*> names[] = {
                { "BD_Punchy_01", "kick" }, { "Kick Hard", "kick" }, { "808 Kick", "kick" }, { "Kick Bottom", "kick" }, { "SD Tight", "snare" },
                { "Snare_02", "snare" }, { "Clap", "clap" }, { "Snap 1", "clap" }, { "CH 01", "hat_closed" }, { "Closed Hat", "hat_closed" },
                { "OH", "hat_open" }, { "OpenHat2", "hat_open" }, { "HH Pedal", "hat_pedal" }, { "Rimshot", "snare_rim" }, { "SideStick", "sidestick" },
                { "Rim 1", "sidestick" }, { "Tom Hi", "tom_high" }, { "Tom_Lo", "tom_floor" }, { "Mid Tom", "tom_mid" }, { "Floor Tom", "tom_floor" },
                { "Crash 18", "crash" }, { "Cymbal", "crash" }, { "Ride", "ride" }, { "RideBell", "ride_bell" }, { "Ride Bell 2", "ride_bell" },
                { "China", "china" }, { "Splash", "splash" }, { "Cowbell", "cowbell" }, { "Shaker 3", "shaker" }, { "Tambourine", "tambourine" },
                { "Pad C3", "" }, { "Vocal Chop", "" }, { "Primary Lead", "" } };
            juce::StringArray wrong;

            for (auto [name, voice] : names)
                if (drumkits::voiceForName (name) != voice)
                    wrong.add (juce::String (name) + " -> " + (drumkits::voiceForName (name).isEmpty() ? juce::String ("nothing") : drumkits::voiceForName (name)));

            check (wrong.isEmpty(), juce::String (std::size (names)) + " sample names read as the drums they are" + (wrong.isEmpty() ? juce::String() : " (" + wrong.joinIntoString ("; ") + ")"));

            // A pack: short hits at different levels
            const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Nightshift import test");
            temp.deleteRecursively();
            const auto pack = temp.getChildFile ("Dusty Pack");
            drumkits::folderOverride = temp.getChildFile ("Kits");
            drumkits::folderOverride.createDirectory();

            auto hit = [] (const juce::File& file, double hz, float level)
            {
                file.getParentDirectory().createDirectory();
                auto b = drumsynth::generate (44100.0, 0.3, [hz, level] (double t) { return (float) (std::sin (drumsynth::twoPi * hz * t) * drumsynth::decay (t, 0.05)) * level; });
                drumsynth::writeWav (file, b, 44100.0);
            };

            hit (pack.getChildFile ("BD_Punchy_01.wav"), 55.0, 0.9f);
            hit (pack.getChildFile ("BD_Punchy_02.wav"), 57.0, 0.5f);
            hit (pack.getChildFile ("Kicks").getChildFile ("01.wav"), 50.0, 0.7f);
            hit (pack.getChildFile ("Snare Tight.wav"), 200.0, 0.8f);
            hit (pack.getChildFile ("Clap 1.wav"), 1000.0, 0.6f);
            hit (pack.getChildFile ("Hats").getChildFile ("CH 01.wav"), 8000.0, 0.3f);
            hit (pack.getChildFile ("Hats").getChildFile ("OH 01.wav"), 7000.0, 0.4f);
            hit (pack.getChildFile ("Pad C3.wav"), 130.0, 0.5f);

            {
                Song song;
                song.loadEmpty();
                AudioEngine audio (song, false);
                MainComponent main (song, audio);
                main.setSize (1600, 940);
                main.setVisible (true);

                const auto report = main.importDrumKitFrom (pack);
                std::cout << "  " << report << std::endl;
                const auto id = drumkits::idForName ("Dusty Pack");
                check (id.isNotEmpty() && drumkits::ids().contains (id), "the pack becomes a kit named after its folder (" + id + ")");

                if (auto kit = drumkits::read (drumkits::folder().getChildFile (id), true))
                {
                    auto takes = [&kit] (const char* voice) { const int key = drumkits::keyForVoice (*kit, voice); return key >= 0 && kit->pieces.count (key) ? (int) kit->pieces.at (key).regions.size() : 0; };
                    check (takes ("kick") == 3 && takes ("snare") == 1 && takes ("clap") == 1, "three kick takes (two by name, one from the Kicks folder), a snare, a clap");
                    check (drumkits::voicesIn (*kit).contains ("hat_closed") && drumkits::voicesIn (*kit).contains ("hat_open"), "closed and open hats from the Hats folder");
                    check (kit->samples.size() == 7 && report.contains ("1 sound wasn't named like a drum"), "the pad left out, and the report says so");

                    // Each take played at its drum's level: a kick take's peak times its volume
                    bool levelled = true;

                    for (auto& r : kit->pieces.at (drumkits::keyForVoice (*kit, "kick")).regions)
                        levelled = levelled && std::abs (kit->samples[(size_t) r.sample].audio.getMagnitude (0, kit->samples[(size_t) r.sample].audio.getNumSamples()) * r.gain
                                                         - drumkits::peakFor ("kick")) < 0.01f;

                    check (levelled, "every kick take set to the kick's level, loud or quiet as it came");
                    check (drumkits::character (id).contains ("own samples"), "the drummer knows it's the musician's own samples");
                }
                else
                {
                    check (false, "the kit reads back");
                }

                bool playing = false;

                for (auto& c : song.channels)
                    playing = playing || (c.type == InstrumentType::kit && c.kit == id);

                check (playing && main.isPanelVisible (MainComponent::Panel::drummer), "a channel plays it, with its drummer open");

                juce::String nothing;
                check (drumkits::importFolder (temp.getChildFile ("nope"), nothing).isEmpty() && nothing.isNotEmpty(), "a folder that isn't there says so");
            }

            drumkits::folderOverride = juce::File();
            temp.deleteRecursively();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--machinetest"))
        {
            // The 808 and 909 written into a folder of their own: quick, every drum in three
            // layers of two takes, harder layers brighter, takes not identical, the kicks at their
            // pitch, closed hats short and open ones long, each drum at its level, and the kits
            // playable by the drummer's names
            drumkits::folderOverride = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Nightshift machine test");
            drumkits::folderOverride.deleteRecursively();

            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            const auto started = juce::Time::getMillisecondCounter();
            const auto error = drumkits::machines::ensure();
            const auto took = juce::Time::getMillisecondCounter() - started;
            check (error.isEmpty() && took < 3000, "both machines written in " + juce::String (took) + " ms " + error);

            const auto again = juce::Time::getMillisecondCounter();
            drumkits::machines::ensure();
            check (juce::Time::getMillisecondCounter() - again < 50, "and not written again once they're there");

            // Brightness: the share of a hit's energy over 2 kHz
            auto brightness = [] (const juce::AudioBuffer<float>& b, double rate)
            {
                auto high = drumsynth::Filter::highPass (rate, 2000.0);
                double all = 0.0, top = 0.0;

                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float x = b.getSample (0, i), h = high (x);
                    all += x * x;
                    top += h * h;
                }

                return all > 0.0 ? top / all : 0.0;
            };

            // A hit's pitch: zero crossings over its first 300 ms after 20 ms
            auto pitch = [] (const juce::AudioBuffer<float>& b, double rate)
            {
                const int from = (int) (rate * 0.02), to = std::min (b.getNumSamples(), (int) (rate * 0.32));
                int crossings = 0;

                for (int i = from + 1; i < to; ++i)
                    crossings += (b.getSample (0, i - 1) < 0.0f) != (b.getSample (0, i) < 0.0f) ? 1 : 0;

                return crossings * 0.5 / ((to - from) / rate);
            };

            for (auto id : drumkits::machines::ids())
            {
                auto kit = drumkits::read (drumkits::folder().getChildFile (id), true);

                if (kit == nullptr)
                {
                    check (false, id + " reads back");
                    continue;
                }

                std::cout << "== " << drumkits::displayName (id) << ": " << kit->pieces.size() << " drums, " << kit->samples.size() << " hits" << std::endl;
                bool layered = true, brighter = true, distinct = true, levels = true;
                juce::String notes;

                for (auto& [key, piece] : kit->pieces)
                {
                    layered = layered && piece.regions.size() == 6;
                    const auto& soft = kit->samples[(size_t) piece.regions.front().sample];
                    const auto& hard = kit->samples[(size_t) piece.regions.back().sample];
                    const auto& other = kit->samples[(size_t) piece.regions[piece.regions.size() - 2].sample];
                    const double softBright = brightness (soft.audio, soft.sampleRate), hardBright = brightness (hard.audio, hard.sampleRate);

                    // Cowbell, claves and maracas are one sound however hard they're hit; kicks and toms have
                    // next to nothing up there to compare
                    if (piece.name != "Cowbell" && piece.name != "Stick Click" && piece.name != "Shaker" && softBright > 0.005 && hardBright < softBright * 0.98)
                    {
                        brighter = false;
                        notes << " " << piece.name << " (" << juce::String (softBright * 100, 1) << "% > " << juce::String (hardBright * 100, 1) << "%)";
                    }

                    if (piece.name != "Cowbell" && piece.name != "Stick Click" && hard.audio.getNumSamples() == other.audio.getNumSamples()
                         && std::equal (hard.audio.getReadPointer (0), hard.audio.getReadPointer (0) + hard.audio.getNumSamples(), other.audio.getReadPointer (0)))
                        distinct = false;

                    const float peak = juce::Decibels::gainToDecibels (hard.audio.getMagnitude (0, hard.audio.getNumSamples()));
                    levels = levels && std::abs (peak + 6.0f - juce::Decibels::gainToDecibels (drumkits::peakFor ([&]
                    {
                        for (auto& v : drumkits::voiceNames())
                            if (drumkits::pieceFor (v).first == piece.name)
                                return v;

                        return juce::String();
                    }()))) < 0.2f;

                    std::cout << "  " << key << " " << piece.name << ": " << juce::String (hard.audio.getNumSamples() / hard.sampleRate, 2) << " s, peak "
                              << juce::String (peak, 1) << " dB, over 2 kHz " << juce::String (softBright * 100, 1) << "% soft / "
                              << juce::String (hardBright * 100, 1) << "% hard" << std::endl;
                }

                check (layered, "every drum has three layers of two takes");
                check (brighter, "a harder layer is brighter" + notes);
                check (distinct, "a layer's two takes aren't identical");
                check (levels, "each drum's hardest hit at its level");

                const auto& kick = kit->samples[(size_t) kit->pieces.at (36).regions.back().sample];
                const double hz = pitch (kick.audio, kick.sampleRate);
                check (hz > 40.0 && hz < 75.0, "the kick sits low (" + juce::String (hz, 1) + " Hz)");

                const auto lengthOf = [&] (int key) { const auto& s = kit->samples[(size_t) kit->pieces.at (key).regions.back().sample]; return s.audio.getNumSamples() / s.sampleRate; };
                check (lengthOf (42) < 0.4 && lengthOf (46) > 0.8, "closed hat short (" + juce::String (lengthOf (42), 2) + " s), open hat long ("
                                                                    + juce::String (lengthOf (46), 2) + " s)");

                juce::StringArray missing;

                for (auto voice : { "kick", "snare", "clap", "sidestick", "hat_closed", "hat_pedal", "hat_open", "tom_high", "tom_mid", "tom_floor",
                                    "crash", "cowbell", "shaker" })
                    if (drumkits::keyForVoice (*kit, voice) < 0)
                        missing.add (voice);

                check (missing.isEmpty(), "the drummer can play everything it has" + (missing.isEmpty() ? juce::String() : " (not " + missing.joinIntoString (", ") + ")"));
                check (drumkits::character (id).isNotEmpty(), "and knows what it suits");
            }

            drumkits::folderOverride.deleteRecursively();
            drumkits::folderOverride = juce::File();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--novatest") || args.has ("--novarender"))
        {
            // Nova, the synth: in tune, its steps and corners rounded off (little aliasing), the
            // filter, velocity, mono and legato, the delay in time with the song, a sound kept
            // through save and load, a song on the old synth still loading, patches read back as
            // written, and every factory sound playing at an even level without clipping, quickly.
            // --novarender <sound or patch.json> <out.wav> renders a sound's demo phrase instead
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-nova-test.wav");
            double rate = 48000.0;

            struct Hit { int key; double start, beats; int velocity = 100; };

            // Notes (in beats) on a synth channel playing `patch`, rendered with a tail
            auto render = [&] (const nova::Patch& patch, const std::vector<Hit>& hits, double bpm = 120.0, double tail = 3.0)
            {
                Song song;
                song.loadEmpty();
                song.bpm = bpm;
                AudioEngine audio (song, false);
                const int id = song.addChannel ("Nova", InstrumentType::synth).id;
                song.findChannel (id)->synthPreset = "Init";
                auto& notes = song.currentPattern().notesFor (id);

                for (auto& h : hits)
                    notes.push_back ({ juce::roundToInt (h.start * ticksPerBeat), juce::roundToInt (h.beats * ticksPerBeat), h.key, h.velocity });

                song.mode = PlayMode::pattern;
                song.notify (Song::Change::everything);
                audio.syncNow();
                audio.applySynthPatch (id, patch);

                juce::AudioBuffer<float> b;

                if (audio.renderToFile (temp, tail))
                    if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (temp)); reader != nullptr)
                    {
                        b.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
                        reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                        rate = reader->sampleRate;
                    }

                temp.deleteFile();
                return b;
            };

            auto withSettings = [] (std::initializer_list<std::pair<const char*, float>> settings)
            {
                nova::Patch p;
                p.name = "Test";

                for (auto& [id, v] : settings)
                    p.settings[id] = v;

                return p;
            };

            auto range = [&] (const juce::AudioBuffer<float>& b, double from, double seconds)
            {
                const int start = juce::jlimit (0, b.getNumSamples(), (int) (from * rate));
                return juce::Range<int> (start, juce::jlimit (start, b.getNumSamples(), start + (int) (seconds * rate)));
            };

            auto rmsDb = [&] (const juce::AudioBuffer<float>& b, double from, double seconds)
            {
                const auto r = range (b, from, seconds);

                if (r.isEmpty() || b.getNumChannels() == 0)
                    return -200.0f;

                float sum = 0.0f;

                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    sum += b.getRMSLevel (ch, r.getStart(), r.getLength());

                return juce::Decibels::gainToDecibels (sum / (float) b.getNumChannels(), -200.0f);
            };

            // Pitch by autocorrelation: the first lag (50 Hz to 3 kHz) that matches nearly as well as
            // the best, refined between samples
            auto pitchOf = [&] (const juce::AudioBuffer<float>& b, double from, double seconds)
            {
                const auto w = range (b, from, seconds);
                std::vector<float> x;

                for (int i = w.getStart(); i < w.getEnd(); ++i)
                    x.push_back (b.getSample (0, i) + b.getSample (b.getNumChannels() - 1, i));

                const int minLag = (int) (rate / 3000.0), maxLag = (int) (rate / 50.0);
                std::vector<double> r ((size_t) maxLag + 2, 0.0);

                for (int lag = minLag; lag <= maxLag + 1; ++lag)
                    for (size_t i = 0; i + (size_t) lag < x.size(); ++i)
                        r[(size_t) lag] += (double) x[i] * x[i + (size_t) lag];

                const double best = *std::max_element (r.begin() + minLag, r.end());

                for (int lag = minLag + 1; lag <= maxLag; ++lag)
                    if (r[(size_t) lag] > 0.9 * best && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1])
                    {
                        const double a = r[(size_t) lag - 1], c = r[(size_t) lag], d = r[(size_t) lag + 1];
                        return rate / (lag + 0.5 * (a - d) / (a - 2.0 * c + d));
                    }

                return 0.0;
            };

            // Power spectrum of 16384 samples from `from` (Hann window), left channel
            auto spectrum = [&] (const juce::AudioBuffer<float>& b, double from)
            {
                constexpr int order = 14, size = 1 << order;
                juce::dsp::FFT fft (order);
                std::vector<float> data ((size_t) size * 2, 0.0f);
                const int start = (int) (from * rate);

                for (int i = 0; i < size && start + i < b.getNumSamples(); ++i)
                    data[(size_t) i] = b.getSample (0, start + i) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (size - 1)));

                fft.performFrequencyOnlyForwardTransform (data.data());
                std::vector<double> power ((size_t) size / 2);

                for (size_t i = 0; i < power.size(); ++i)
                    power[i] = (double) data[i] * data[i];

                return power;
            };

            // How far above everything else the harmonics of f0 are, in dB (negative: below)
            auto aliasDb = [&] (const juce::AudioBuffer<float>& b, double from, double f0)
            {
                const auto power = spectrum (b, from);
                const double binHz = rate / 16384.0;
                double harmonic = 0.0, other = 0.0;

                for (size_t i = 2; i < power.size(); ++i)
                {
                    const double f = i * binHz, k = std::round (f / f0);
                    (k >= 1.0 && std::abs (f - k * f0) < 6.0 * binHz ? harmonic : other) += power[i];
                }

                return 10.0 * std::log10 ((other + 1.0e-20) / (harmonic + 1.0e-20));
            };

            auto bandDb = [&] (const juce::AudioBuffer<float>& b, double from, double low, double high)
            {
                const auto power = spectrum (b, from);
                const double binHz = rate / 16384.0;
                double sum = 1.0e-20;

                for (size_t i = 1; i < power.size(); ++i)
                    if (i * binHz >= low && i * binHz < high)
                        sum += power[i];

                return 10.0 * std::log10 (sum);
            };

            // Each category's phrase: basses and leads a line, the rest chords
            auto phraseFor = [] (const nova::Patch& p)
            {
                std::vector<Hit> hits;

                for (auto& n : nova::phraseFor (p.category))
                    hits.push_back ({ n.key, n.start, n.beats });

                return hits;
            };

            auto phraseSeconds = [&] (const nova::Patch& p)
            {
                double end = 0.0;

                for (auto& h : phraseFor (p))
                    end = std::max (end, (h.start + h.beats) * 0.5);

                return end;
            };

            // Roughly how loud it sounds: K-weighted (a high pass, then a lift above 1.5 kHz), the
            // loudest 400 ms while the notes play, in dB
            auto loudnessDb = [&] (const juce::AudioBuffer<float>& b, double seconds)
            {
                nightshift::dsp::Biquad highPass, shelf;
                highPass.c = nightshift::dsp::Coeffs::highPass (60.0, 0.5, rate);
                shelf.c = nightshift::dsp::Coeffs::highShelf (1500.0, 4.0, rate);
                const int n = std::min (b.getNumSamples(), (int) (seconds * rate));
                const int channels = std::min (2, b.getNumChannels()), window = (int) (0.4 * rate), hop = (int) (0.1 * rate);
                std::vector<double> squared ((size_t) n, 0.0);

                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < n; ++i)
                    {
                        const float y = shelf.process (highPass.process (b.getSample (ch, i), ch), ch);
                        squared[(size_t) i] += (double) y * y / channels;
                    }

                double loudest = 1.0e-20;

                for (int start = 0; start + window <= n; start += hop)
                    loudest = std::max (loudest, std::accumulate (squared.begin() + start, squared.begin() + start + window, 0.0) / window);

                return 10.0 * std::log10 (loudest);
            };

            if (args.has ("--novarender"))
            {
                const auto name = args.valueAfter ("--novarender");
                const auto out = juce::File (args.tokens[args.tokens.indexOf ("--novarender") + 2].unquoted());
                auto patch = juce::File (name).existsAsFile() ? nova::Patch::fromJson (juce::File (name).loadFileAsString())
                                                               : (nova::findFactory (name) != nullptr ? std::optional (*nova::findFactory (name)) : std::nullopt);

                if (! patch.has_value())
                {
                    std::cout << "No such sound: " << name << std::endl;
                    return 1;
                }

                const auto b = render (*patch, phraseFor (*patch));
                drumsynth::writeWav (out, b, rate);

                std::cout << patch->name << ": " << juce::String (loudnessDb (b, phraseSeconds (*patch)), 1) << " dB loudness, peak "
                          << juce::String (juce::Decibels::gainToDecibels (b.getMagnitude (0, b.getNumSamples())), 1) << " dB -> " << out.getFullPathName() << std::endl;
                return 0;
            }

            // In tune
            {
                const auto b = render (withSettings ({}), { { 69, 0.0, 2.0 } });
                const double a4 = pitchOf (b, 0.3, 0.4);
                check (std::abs (a4 - 440.0) < 1.5, "the init saw plays A4 at " + juce::String (a4, 1) + " Hz");

                const auto tuned = render (withSettings ({ { "osc1_shape", 0.0f }, { "osc1_octave", 1.0f }, { "osc1_semi", 7.0f } }), { { 57, 0.0, 2.0 } });
                const double fifth = pitchOf (tuned, 0.3, 0.4);
                check (std::abs (fifth - 659.26) < 3.0, "an octave and a fifth up from A3 is " + juce::String (fifth, 1) + " Hz (659.3)");
            }

            // Little aliasing, high up, where it would show
            for (auto [shape, name, limit] : { std::tuple { 2.0f, "saw", -30.0 }, std::tuple { 3.0f, "square", -30.0 }, std::tuple { 1.0f, "triangle", -40.0 } })
            {
                const auto b = render (withSettings ({ { "osc1_shape", shape } }), { { 96, 0.0, 2.0 } });
                const double ratio = aliasDb (b, 0.3, 440.0 * std::exp2 ((96 - 69) / 12.0));
                check (ratio < limit, juce::String ("a ") + name + " at C7 keeps its aliasing " + juce::String (-ratio, 1) + " dB under its harmonics");
            }

            // The filter
            {
                const auto open = render (withSettings ({}), { { 45, 0.0, 2.0 } });
                const auto closed = render (withSettings ({ { "filter_cutoff", 400.0f } }), { { 45, 0.0, 2.0 } });
                const double drop = bandDb (open, 0.3, 2000.0, 8000.0) - bandDb (closed, 0.3, 2000.0, 8000.0);
                check (drop > 30.0, "a 400 Hz low pass takes " + juce::String (drop, 1) + " dB off 2-8 kHz");

                const auto swept = render (withSettings ({ { "filter_cutoff", 200.0f }, { "filter_env", 6.0f }, { "fenv_decay", 0.3f } }), { { 45, 0.0, 2.0 } });
                const double sweep = bandDb (swept, 0.0, 2000.0, 8000.0) - bandDb (swept, 0.8, 2000.0, 8000.0);
                check (sweep > 15.0, "the filter envelope opens it then closes it (" + juce::String (sweep, 1) + " dB brighter at the start)");
            }

            // Velocity
            {
                const auto b = render (withSettings ({}), { { 60, 0.0, 1.0, 40 }, { 60, 2.0, 1.0, 120 } });
                const float soft = rmsDb (b, 0.2, 0.3), hard = rmsDb (b, 1.2, 0.3);
                check (hard - soft > 3.0f, "harder is louder (" + juce::String (hard - soft, 1) + " dB)");
            }

            // Mono and legato: one note at a time, the newest; poly plays both
            {
                const std::vector<Hit> overlap { { 57, 0.0, 4.0 }, { 64, 2.0, 2.0 } };
                const auto legato = render (withSettings ({ { "osc1_shape", 0.0f }, { "voice_mode", 2.0f } }), overlap);
                const auto poly = render (withSettings ({ { "osc1_shape", 0.0f } }), overlap);
                const double one = pitchOf (legato, 1.3, 0.4), both = pitchOf (poly, 1.3, 0.4);
                check (std::abs (one - 329.6) < 3.0 && std::abs (both - 329.6) > 20.0,
                       "legato plays only the newest key (" + juce::String (one, 1) + " Hz), poly both (" + juce::String (both, 1) + " Hz together)");

                const auto glide = render (withSettings ({ { "osc1_shape", 0.0f }, { "voice_mode", 2.0f }, { "glide", 0.4f } }), overlap);
                const double between = pitchOf (glide, 1.03, 0.04), after = pitchOf (glide, 1.6, 0.3);
                check (between > 225.0 && between < 320.0 && std::abs (after - 329.6) < 3.0,
                       "glide slides there (" + juce::String (between, 1) + " Hz on the way, " + juce::String (after, 1) + " Hz after)");
            }

            // The delay follows the song's tempo
            for (double bpm : { 120.0, 90.0 })
            {
                const auto b = render (withSettings ({ { "osc1_shape", 0.0f }, { "amp_decay", 0.04f }, { "amp_sustain", 0.0f }, { "amp_release", 0.02f },
                                                       { "delay_mix", 1.0f }, { "delay_time", 5.0f }, { "delay_feedback", 0.0f }, { "delay_pingpong", 0.0f } }),
                                       { { 69, 0.0, 0.25 } }, bpm);
                const double beat = 60.0 / bpm;
                const float echo = rmsDb (b, beat, 0.05), gap = rmsDb (b, beat * 0.5, 0.1);
                check (echo - gap > 20.0f, "at " + juce::String (bpm, 0) + " BPM the quarter-note echo comes " + juce::String (beat, 3) + " s later ("
                                            + juce::String (echo - gap, 1) + " dB over the gap)");
            }

            // Patches read back as written, choices by name, values kept in range
            {
                bool same = true;

                for (auto& p : nova::factory())
                {
                    auto back = nova::Patch::fromJson (p.toJson());
                    same = same && back.has_value() && back->name == p.name && back->settings == p.settings;
                }

                check (same, juce::String ((int) nova::factory().size()) + " factory sounds read back as written, in "
                             + juce::String (nova::categories().size()) + " categories");

                auto loose = nova::Patch::fromJson (R"({"name":"Loose","settings":{"filter_mode":"High pass","filter_cutoff":99999,"osc1_unison":4.4,"nonsense":1}})");
                check (loose.has_value() && loose->get ("filter_mode") == 2.0f && loose->get ("filter_cutoff") == 20000.0f && loose->get ("osc1_unison") == 4.0f
                         && loose->settings.count ("nonsense") == 0,
                       "a patch can name a choice, and values are kept to their ranges");
            }

            // Saved and loaded: the same sound, built from its saved state; an old-synth song still loads
            {
                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-nova-test.nightshift");
                auto patch = *nova::findFactory ("Aurora");
                patch.name = "Aurora Tweaked";
                patch.settings["filter_cutoff"] = 1234.0f;
                patch.settings["mod6_source"] = 6.0f;

                {
                    Song song;
                    song.loadEmpty();
                    AudioEngine audio (song, false);
                    const int id = song.addChannel ("Pad", InstrumentType::synth).id;
                    song.findChannel (id)->synthPreset = "Init";
                    song.notify (Song::Change::channels);
                    audio.syncNow();
                    audio.applySynthPatch (id, patch);
                    audio.flushPluginStates();
                    song.save (file);
                }

                Song song;
                song.load (file);
                AudioEngine audio (song, false);
                song.notify (Song::Change::everything);
                audio.syncNow();
                const auto pad = std::find_if (song.channels.begin(), song.channels.end(), [] (const Channel& c) { return c.name == "Pad"; });
                const auto nova = pad == song.channels.end() ? nullptr : audio.getNova (pad->id);
                bool same = nova != nullptr;

                for (auto& p : nova::params())
                    if (nova != nullptr && std::abs (nova->currentPatch().get (p.id) - patch.get (p.id)) >= 1.0e-4f)
                    {
                        std::cout << "      " << p.id << " came back " << nova->currentPatch().get (p.id) << ", not " << patch.get (p.id) << std::endl;
                        same = false;
                    }

                if (nova == nullptr)
                    std::cout << "      no Nova after loading" << std::endl;

                check (same && pad->synthPreset == "Aurora Tweaked",
                       "a sound is kept through save and load");
                file.deleteFile();

                Song old;
                old.loadEmpty();
                AudioEngine oldAudio (old, false);
                const int id = old.addChannel ("Old", InstrumentType::synth).id;
                old.findChannel (id)->synthPreset = "Night Pad";
                old.findChannel (id)->pluginState = oldAudio.getEdit().getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {})->state.toXmlString();
                old.notify (Song::Change::channels);
                oldAudio.syncNow();
                const bool stillOld = dynamic_cast<te::FourOscPlugin*> (oldAudio.getInstrument (id)) != nullptr && oldAudio.getNova (id) == nullptr;
                oldAudio.applySynthPatch (id, *nova::findFactory ("Glass Pluck"));
                check (stillOld && oldAudio.getNova (id) != nullptr && old.findChannel (id)->synthPreset == "Glass Pluck",
                       "a song saved with the old synth still plays it, until a Nova sound is chosen");
            }

            // Every factory sound: plays, never clips, at an even level; and quickly
            {
                juce::StringArray quiet, loud, clipped, misjudged;
                double widestGap = 0.0;
                juce::String widestOn;

                // Seeded as nova::loudness seeds itself: the same random unison start points (and noise
                // and random LFOs) both ways, so the two measure the very same notes. Unseeded, the
                // unison copies land differently each run and the level moves a dB or two with them
                NovaPlugin::seedForTests = 1;

                for (auto& p : nova::factory())
                {
                    if (p.category == "Init")
                        continue;

                    const auto b = render (p, phraseFor (p));
                    const float peak = b.getMagnitude (0, b.getNumSamples());
                    const double level = loudnessDb (b, phraseSeconds (p));
                    bool finite = true;

                    for (int ch = 0; ch < b.getNumChannels(); ++ch)
                        for (int i = 0; i < b.getNumSamples(); i += 7)
                            finite = finite && std::isfinite (b.getSample (ch, i));

                    // Nova measuring itself (as it levels the AI's sounds) agrees with the engine
                    const float own = nova::loudness (p);

                    if (std::abs (own - level) > widestGap)
                    {
                        widestGap = std::abs (own - level);
                        widestOn = p.name;
                    }

                    if (std::abs (own - level) > 2.0)
                        misjudged.add (p.name + " (" + juce::String (own, 1) + " vs " + juce::String (level, 1) + ")");

                    std::cout << "      " << p.name.paddedRight (' ', 18) << juce::String (level, 1).paddedLeft (' ', 6) << " dB  peak "
                              << juce::String (juce::Decibels::gainToDecibels (peak), 1).paddedLeft (' ', 5) << " dB" << std::endl;

                    if (! finite || peak >= 0.99f)
                        clipped.add (p.name);
                    else if (level < -22.0)
                        quiet.add (p.name);
                    else if (level > -14.0)
                        loud.add (p.name);

                }

                NovaPlugin::seedForTests.reset();
                check (misjudged.isEmpty(), "Nova measures its own loudness as the engine does (the widest gap "
                                               + juce::String (widestGap, 2) + " dB, " + widestOn + ")"
                                               + (misjudged.isEmpty() ? juce::String() : ": " + misjudged.joinIntoString (", ")));

                auto hot = *nova::findFactory ("Aurora");
                hot.settings["volume"] = 6.0f;
                hot.settings["filter_cutoff"] = 12000.0f;
                const auto level = nova::loudness (nova::levelled (hot));
                check (std::abs (level + 18.0f) < 1.0f, "a sound made too loud is levelled to " + juce::String (level, 1) + " dB");

                check (clipped.isEmpty(), "no factory sound clips" + (clipped.isEmpty() ? juce::String() : ": " + clipped.joinIntoString (", ")));
                check (quiet.isEmpty() && loud.isEmpty(), "every factory sound sits between -22 and -14 dB"
                                                           + (quiet.isEmpty() ? juce::String() : "; too quiet: " + quiet.joinIntoString (", "))
                                                           + (loud.isEmpty() ? juce::String() : "; too loud: " + loud.joinIntoString (", ")));
            }

            // Fast enough: the heaviest sound (two stacks of eight unison voices) playing an
            // eight-note chord, block by block as the audio thread would
            {
                Song song;
                song.loadEmpty();
                AudioEngine audio (song, false);
                const int id = song.addChannel ("Nova", InstrumentType::synth).id;
                song.findChannel (id)->synthPreset = "Aurora";
                song.notify (Song::Change::channels);
                audio.syncNow();

                if (auto nova = audio.getNova (id))
                {
                    constexpr int block = 256;
                    nova->initialise ({ tracktion::TimePosition(), 48000.0, block });
                    juce::AudioBuffer<float> buffer (2, block);
                    te::MidiMessageArray midi;
                    const auto source = te::createUniqueMPESourceID();

                    for (int key : { 48, 55, 60, 64, 67, 71, 74, 79 })
                        midi.addMidiMessage (juce::MidiMessage::noteOn (1, key, 0.8f), 0.0, source);

                    const double seconds = 10.0;
                    const auto started = juce::Time::getMillisecondCounterHiRes();

                    for (int done = 0; done < (int) (seconds * 48000.0); done += block)
                    {
                        te::PluginRenderContext rc (&buffer, 0, block, &midi, 0.0, { tracktion::TimePosition::fromSeconds (done / 48000.0),
                                                    tracktion::TimePosition::fromSeconds ((done + block) / 48000.0) }, true, false, false, false);
                        nova->applyToBuffer (rc);
                        midi.clear();
                    }

                    const double speed = seconds / ((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0);
                    check (speed > 10.0, "Aurora plays an eight-note chord at " + juce::String (speed, 0) + "x real time (" + juce::String (nova->getActiveVoices()) + " voices)");
                }
                else
                {
                    check (false, "Aurora plays an eight-note chord quickly");
                }
            }

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--instrumenttest"))
        {
            // The sampled instruments in <folder> (--instrumenttest <folder>, after --instrumentfetch):
            // every one loads, quickly; the piano plays in tune (A4 at 440, C6 too, from a stretched
            // sample), louder when harder, holds with the pedal; keyswitches are read
            SfzPlugin::folderOverride = juce::File (args.valueAfter ("--instrumenttest"));
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            const auto all = instruments::list();
            auto started = juce::Time::getMillisecondCounter();
            juce::StringArray broken;
            int regions = 0;

            for (auto& e : all)
            {
                auto inst = sfz::load (SfzPlugin::fileFor (e.path));
                regions += (int) inst->regions.size();

                if (inst->error.isNotEmpty() || inst->regions.empty() || ! inst->missing.isEmpty())
                    broken.add (e.name + ": " + inst->error + (inst->missing.isEmpty() ? juce::String() : juce::String (inst->missing.size()) + " samples missing, e.g. " + inst->missing[0]));
            }

            check (all.size() >= 70 && broken.isEmpty(), juce::String ((int) all.size()) + " instruments load (" + juce::String (regions) + " regions) in "
                                                            + juce::String ((juce::Time::getMillisecondCounter() - started) / 1000.0, 2) + " s"
                                                            + (broken.isEmpty() ? juce::String() : "; broken: " + broken.joinIntoString ("; ")));

            const auto piano = sfz::load (SfzPlugin::fileFor ("SalamanderGrandPiano/Salamander Grand Piano V3.sfz"));
            int releases = 0;

            for (auto& r : piano->regions)
                releases += r.release ? 1 : 0;

            check (piano->regions.size() > 400 && releases > 0 && piano->swDefault >= 0, "the grand piano: " + juce::String ((int) piano->regions.size())
                                                                                        + " regions, " + juce::String (releases) + " of them release sounds, keyswitch "
                                                                                        + (piano->switchLabels.count (piano->swDefault) ? piano->switchLabels.at (piano->swDefault) : juce::String ("none")));

            const auto cello = sfz::load (SfzPlugin::fileFor ("VSCO-2-CE/CelloEns-KS.sfz"));
            check (cello->switchLabels.size() >= 3, "the cello section's keyswitches: " + [&] { juce::StringArray s; for (auto& [k, v] : cello->switchLabels) s.add (v); return s.joinIntoString (", "); }());

            // Played through the engine: A4 soft and hard, C6, then A4 with the pedal held past its key
            Song song;
            song.loadEmpty();
            song.bpm = 60.0;
            AudioEngine audio (song, false);
            auto& ch = song.addChannel ("Piano", InstrumentType::instrument);
            ch.instrument = "SalamanderGrandPiano/Salamander Grand Piano V3.sfz";
            auto& pattern = song.currentPattern();
            const int beat = ticksPerBeat;
            auto& notes = pattern.notesFor (ch.id);
            notes.push_back ({ 0, beat, 69, 40 });
            notes.push_back ({ 2 * beat, beat, 69, 120 });
            notes.push_back ({ 4 * beat, beat, 84, 100 });
            song.mode = PlayMode::pattern;
            song.notify (Song::Change::everything);

            const auto out = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-piano-test.wav");
            started = juce::Time::getMillisecondCounter();
            check (audio.renderToFile (out), "the piano renders (" + juce::String ((juce::Time::getMillisecondCounter() - started) / 1000.0, 2) + " s)");

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (out)); reader != nullptr)
            {
                juce::AudioBuffer<float> b ((int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                const double rate = reader->sampleRate;
                auto window = [&] (double from, double seconds) { return juce::Range<int> ((int) (from * rate), (int) ((from + seconds) * rate)); };

                // Its pitch by autocorrelation: the first lag (50 Hz to 2 kHz) that matches nearly as
                // well as the best, refined between samples
                auto pitch = [&] (juce::Range<int> w)
                {
                    std::vector<float> x;

                    for (int i = w.getStart(); i < std::min (w.getEnd(), b.getNumSamples()); ++i)
                        x.push_back (b.getSample (0, i) + b.getSample (1, i));

                    const int minLag = (int) (rate / 2000.0), maxLag = (int) (rate / 50.0);
                    std::vector<double> r ((size_t) maxLag + 2, 0.0);

                    for (int lag = minLag; lag <= maxLag + 1; ++lag)
                        for (size_t i = 0; i + (size_t) lag < x.size(); ++i)
                            r[(size_t) lag] += (double) x[i] * x[i + (size_t) lag];

                    const double best = *std::max_element (r.begin() + minLag, r.end());

                    for (int lag = minLag + 1; lag <= maxLag; ++lag)
                        if (r[(size_t) lag] > 0.9 * best && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1])
                        {
                            const double a = r[(size_t) lag - 1], c = r[(size_t) lag], d = r[(size_t) lag + 1];
                            const double shift = 0.5 * (a - d) / (a - 2.0 * c + d);
                            return rate / (lag + shift);
                        }

                    return 0.0;
                };

                const float soft = b.getRMSLevel (0, window (0.05, 0.4).getStart(), window (0.05, 0.4).getLength());
                const float hard = b.getRMSLevel (0, window (2.05, 0.4).getStart(), window (2.05, 0.4).getLength());
                const double a4 = pitch (window (2.2, 0.5)), c6 = pitch (window (4.2, 0.4));

                check (soft > 0.001f && hard > soft * 2.0f, "louder when harder: " + juce::String (juce::Decibels::gainToDecibels (soft), 1) + " dB soft, "
                                                             + juce::String (juce::Decibels::gainToDecibels (hard), 1) + " dB hard");
                check (std::abs (a4 - 440.0) < 8.0, "A4 in tune (" + juce::String (a4, 1) + " Hz)");
                check (std::abs (c6 - 1046.5) < 20.0, "C6 in tune, from a stretched sample (" + juce::String (c6, 1) + " Hz)");
                check (b.getMagnitude (0, b.getNumSamples()) < 1.0f, "no clipping (peak " + juce::String (juce::Decibels::gainToDecibels (b.getMagnitude (0, b.getNumSamples())), 1) + " dB)");
            }
            else
            {
                check (false, "the render reads back");
            }

            out.deleteFile();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--instrumentfetch"))
        {
            // The piano and orchestra fetched for real by Get the piano and orchestra, into
            // <folder> (--instrumentfetch <folder>), then listed as the browser shows them
            SfzPlugin::folderOverride = juce::File (args.valueAfter ("--instrumentfetch"));
            drumkits::folderOverride = SfzPlugin::folderOverride.getChildFile ("kits");
            const auto started = juce::Time::getMillisecondCounter();
            int shown = -1;
            juce::String last;
            const auto error = instruments::fetch ([&] (float f, const juce::String& what)
            {
                if (const int step = (int) (f * 50.0f); step != shown || what != last)
                {
                    shown = step;
                    last = what;
                    std::cout << "  " << juce::String (f * 100.0f, 1) << "% " << what << " (" << (juce::Time::getMillisecondCounter() - started) / 1000 << " s)" << std::endl;
                }
            }, {});

            std::cout << (error.isEmpty() ? "fetched" : "FAILED " + error) << " in " << (juce::Time::getMillisecondCounter() - started) / 1000 << " s" << std::endl;

            for (auto& e : instruments::list())
                std::cout << "  " << e.category << " | " << e.name << " | " << e.path << std::endl;

            return error.isEmpty() ? 0 : 1;
        }

        if (args.has ("--updatetest"))
        {
            // Check for updates without the internet: versions compared number by number; GitHub's answer for the
            // latest release read (this computer's installer, its size and checksum, this release's "What's new"
            // headlines and not an older list's), a release without this computer's installer yet, GitHub's
            // refusals; SHA-256; a file checked against the release; the files and addresses it won't download;
            // and how the Windows installer is started
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            using updates::compare;
            check (compare ("0.4.10", "0.4.9") == 1 && compare ("0.4.9", "0.4.10") == -1 && compare ("1.0", "1.0.0") == 0
                     && compare ("v0.4.1", "0.4.1") == 0 && compare ("0.4.1", "0.5") == -1 && compare ("2.0.0", "1.99.99") == 1,
                   "versions compare number by number (0.4.10 after 0.4.9, 1.0 the same as 1.0.0, a v not counted)");
            check (updates::currentVersion() == JUCE_APPLICATION_VERSION_STRING && updates::currentVersion().containsChar ('.'),
                   "this NightShift's version is " + updates::currentVersion());

           #if JUCE_MAC
            const juce::String mine ("NightShift-0.4.2-mac.pkg");
            const juce::String mineSum ("5f918351aee5eabcc6b6e9963d5c3a849bd256242b440e2c88092472c7577a65");
            const juce::int64 mineBytes = 58922225;
           #else
            const juce::String mine ("NightShift-Setup-0.4.2.exe");
            const juce::String mineSum ("28701913e5cfb6438c9d5cb3253f1e2e6ebdab98858a94a12df7533f3236261a");
            const juce::int64 mineBytes = 17946072;
           #endif

            const juce::String json (R"({"tag_name":"v0.4.2","name":"NightShift 0.4.2","html_url":"https://github.com/ExfilBravo/nightshift/releases/tag/v0.4.2",
                "body":"NightShift is a free music studio.\r\n\r\n## What's new in 0.4.2\r\n\r\n- **Check for updates.** In the menu.\r\n- **Fewer clicks.** More.\r\n- A plain point\r\n\r\n## New in 0.4.1\r\n\r\n- **Older.** Not this.\r\n",
                "assets":[{"name":"NightShift-0.4.2-mac.pkg","size":58922225,"digest":"sha256:5F918351AEE5EABCC6B6E9963D5C3A849BD256242B440E2C88092472C7577A65","browser_download_url":"https://github.com/ExfilBravo/nightshift/releases/download/v0.4.2/NightShift-0.4.2-mac.pkg"},
                          {"name":"NightShift-Setup-0.4.2.exe","size":17946072,"digest":"sha256:28701913e5cfb6438c9d5cb3253f1e2e6ebdab98858a94a12df7533f3236261a","browser_download_url":"https://github.com/ExfilBravo/nightshift/releases/download/v0.4.2/NightShift-Setup-0.4.2.exe"}]})");

            updates::Release r;
            const auto error = updates::parse (json, r);
            check (error.isEmpty() && r.version == "0.4.2" && r.title == "NightShift 0.4.2" && r.page.endsWith ("/tag/v0.4.2"),
                   "GitHub's answer read: version " + r.version + ", \"" + r.title + "\"" + (error.isEmpty() ? juce::String() : " (" + error + ")"));
            check (r.installer == mine && r.installerUrl.endsWith ("/" + mine) && r.installerBytes == mineBytes && r.sha256 == mineSum,
                   "this computer's installer: " + r.installer + ", " + juce::String (r.installerBytes) + " bytes, its SHA-256 in lower case");
            check (r.whatsNew == juce::StringArray { "Check for updates.", "Fewer clicks.", "A plain point" },
                   "what's new: this release's headlines, not the older list's (" + r.whatsNew.joinIntoString (" | ") + ")");

            updates::Release none;
            const auto noneError = updates::parse (R"({"tag_name":"v0.4.3","name":"NightShift 0.4.3","assets":[]})", none);
            check (noneError.isEmpty() && none.version == "0.4.3" && none.installer.isEmpty() && none.whatsNew.isEmpty(),
                   "a release without this computer's installer yet: found, with none");

            updates::Release refused;
            const auto limited = updates::parse (R"({"message":"API rate limit exceeded for 192.0.2.1.","documentation_url":"https://docs.github.com"})", refused);
            check (limited.contains ("rate limit") && updates::parse ("<html>", refused).isNotEmpty(),
                   "GitHub's refusal is passed on (" + limited + "), and an answer that isn't JSON says so");

            // SHA-256, and a file checked against a release
            const auto file = juce::File::createTempFile (".bin");
            file.replaceWithData ("abc", 3);
            const juce::String abc ("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
            check (updates::sha256Of (file) == abc, "SHA-256 of \"abc\" is the standard's (" + updates::sha256Of (file).substring (0, 16) + "...)");

            updates::Release three;
            three.installerBytes = 3;
            three.sha256 = abc;
            const bool right = updates::isTheInstaller (three, file);
            three.sha256 = juce::String::repeatedString ("0", 64);
            const bool wrongSum = ! updates::isTheInstaller (three, file);
            three.sha256 = {};
            const bool sizeOnly = updates::isTheInstaller (three, file);
            three.installerBytes = 4;
            const bool wrongSize = ! updates::isTheInstaller (three, file);
            file.deleteFile();
            check (right && wrongSum && sizeOnly && wrongSize,
                   "a file is the installer only at its size and checksum (the size alone when GitHub gives no checksum)");

            // What it won't download (each stops before anything's asked for)
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-updatetest");
            juce::File got;
            auto odd = r;
            odd.installer = "..\\" + r.installer;
            const auto pathInName = updates::download (odd, folder, got);
            odd = r;
            odd.installerUrl = "http://example.com/" + r.installer;
            const auto plainHttp = updates::download (odd, folder, got);
            odd = r;
            odd.installer = {};
            odd.installerUrl = {};
            const auto missing = updates::download (odd, folder, got);
            check (pathInName.contains ("won't download") && plainHttp.contains ("won't download") && missing.contains ("no installer"),
                   "it won't download a file with a path in its name or over plain http, and says when there's no installer");
            folder.deleteRecursively();

           #if JUCE_WINDOWS
            const auto arguments = updates::windowsInstallerArguments();
            check (arguments.contains ("/SILENT") && ! arguments.contains ("/VERYSILENT") && arguments.contains ("/CLOSEAPPLICATIONS")
                     && arguments.contains ("/relaunch=yes"),
                   "the Windows installer runs quietly with its progress showing, closes NightShift and opens it again (" + arguments + ")");
           #endif

            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--updatecheck"))
        {
            // Check for updates for real, over the internet: the latest release GitHub has, against this NightShift;
            // --download <folder> also downloads this computer's installer there and checks it (it's never run)
            updates::Release r;

            if (const auto error = updates::fetchLatest (r); error.isNotEmpty())
            {
                std::cout << "FAILED  " << error << std::endl;
                return 1;
            }

            const auto current = updates::currentVersion();
            std::cout << "latest: " << r.title << " (" << r.version << "), " << r.page << std::endl
                      << "what's new: " << r.whatsNew.joinIntoString (" | ") << std::endl
                      << "this computer's installer: " << (r.installer.isEmpty() ? juce::String ("none yet")
                                                                                : r.installer + ", " + juce::String (r.installerBytes) + " bytes, sha256 " + r.sha256) << std::endl
                      << "this NightShift is " << current << ": " << (updates::compare (r.version, current) <= 0 ? juce::String ("You are up to date")
                                                                                                                : r.title + " is out") << std::endl;

            if (! args.has ("--download"))
                return 0;

            const juce::File folder (args.valueAfter ("--download"));
            const auto started = juce::Time::getMillisecondCounter();
            juce::File installer;
            int shown = -1;
            const auto failed = updates::download (r, folder, installer, [&shown] (double done)
            {
                if (const int tenth = (int) (done * 10.0); tenth != shown)
                {
                    shown = tenth;
                    std::cout << "  " << tenth * 10 << "%" << std::endl;
                }
            });

            std::cout << (failed.isEmpty() ? "downloaded and checked: " + installer.getFullPathName() + ", " + juce::String (installer.getSize())
                                                 + " bytes, sha256 " + updates::sha256Of (installer)
                                           : "FAILED  " + failed)
                      << " (" << (juce::Time::getMillisecondCounter() - started) / 1000.0 << " s)" << std::endl;
            return failed.isEmpty() ? 0 : 1;
        }

        if (args.has ("--vibepaneltest"))
        {
            // What it heard of a song, in the AI tab's Song panel at the sizes it gets (the editor at its smallest,
            // as it first opens, given the whole height; a narrow window and a wide one): Write a song with this
            // vibe in sight and clickable, Play it as and How close too, nothing on top of them, what it heard in
            // whole sentences (all of them with room), and no text cut or clipped. The user, 2026-11-16: "I gave it
            // a song now how do i generate it?" (in the editor as it opens, Write was below the panel's bottom)
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            // A long song, heard: two dozen sections, as a five-minute song with a long build has
            vibes::Vibe v;
            v.name = "Caramel";
            v.seconds = 302.0;
            v.tempo = 94.0;
            v.keyRoot = 5;
            v.mode = "minor";
            v.brightness = 0.7f;
            v.grit = 0.6f;
            v.lowEnd = 0.3f;
            const char* kinds[] = { "intro", "breakdown", "breakdown", "breakdown", "bridge", "bridge", "bridge", "bridge", "verse", "chorus",
                                    "bridge", "bridge", "bridge", "breakdown", "breakdown", "chorus", "bridge", "bridge", "bridge", "bridge",
                                    "bridge", "bridge", "outro" };
            int bar = 0;

            for (auto* kind : kinds)
            {
                vibes::Section s;
                s.kind = kind;
                s.label = juce::String::charToString ((juce::juce_wchar) ('A' + (bar / 4) % 6));
                s.startBar = bar;
                s.bars = 4;
                s.energyDb = (bar / 4) % 3 == 0 ? -8.0f : -2.0f;
                s.chords = juce::StringArray::fromTokens ("Fm Bbm7 Eb9 Ab9", " ", {});
                v.sections.push_back (s);
                bar += 4;
            }

            v.styles = { { "rock", 0.82f, "its sound, its chords" }, { "synthwave", 0.74f, "its tempo" }, { "deathcore", 0.7f, "its sound" } };
            const auto summary = v.summary();

            Song song;
            song.loadEmpty();
            SongPanel panel (song);
            panel.setVisible (true);        // as in the studio's window (clicks find nothing in a hidden one)
            panel.showHeard (v);

            auto inSight = [&panel] (juce::Component* c)
            {
                if (c == nullptr || ! c->isVisible() || ! panel.getLocalBounds().contains (c->getBounds()))
                    return false;

                auto* hit = panel.getComponentAt (c->getBounds().getCentre());
                return hit == c || c->isParentOf (hit);
            };

            // The panel is the editor less its header, and less the AI tab's list of agents across
            struct Size { const char* what; int width, height; bool words; };
            const Size sizes[] { { "the editor at its smallest, the smallest window", 840, 200 - DockPanel::headerHeight, false },
                                 { "the editor at its smallest, a wide window", 1300, 200 - DockPanel::headerHeight, true },
                                 { "the editor as it first opens", 1100, 260 - DockPanel::headerHeight, true },
                                 { "the editor given the whole height", 1300, 700, true } };

            for (auto& size : sizes)
            {
                panel.setSize (size.width, size.height);
                const auto shown = panel.getShownSummary();
                std::cout << "  " << size.width << " x " << size.height << ": Write at " << panel.getWriteButton().getBounds().toString()
                          << ", Play it as at " << (panel.getPlayAsChooser() != nullptr ? panel.getPlayAsChooser()->getBounds().toString() : juce::String ("-"))
                          << ", How close at " << panel.getClosenessChooser().getBounds().toString() << std::endl;
                juce::Image picture;
                const auto runs = textaudit::collect (panel, &picture);
                juce::StringArray problems;

                for (auto& r : runs)
                    if (! r.fine() || r.veiled)
                        problems.add ("\"" + r.text.substring (0, 40) + "\"");

                // --pictures <folder>: each size as it looks
                if (const auto folder = args.valueAfter ("--pictures"); folder.isNotEmpty() && picture.isValid())
                {
                    const auto file = juce::File (folder).getChildFile ("vibepanel-" + juce::String (size.width) + "x" + juce::String (size.height) + ".png");
                    file.deleteFile();
                    juce::FileOutputStream out (file);
                    juce::PNGImageFormat().writeImageToStream (picture, out);
                }

                juce::StringArray hidden;

                if (! inSight (&panel.getWriteButton()))        hidden.add ("Write");
                if (! inSight (panel.getPlayAsChooser()))       hidden.add ("Play it as");
                if (! inSight (&panel.getClosenessChooser()))   hidden.add ("How close");

                check (hidden.isEmpty(), juce::String (size.what) + ": Write a song with this vibe, Play it as and How close are in sight and clickable"
                                             + (hidden.isEmpty() ? juce::String() : " (not: " + hidden.joinIntoString (", ") + ")"));
                check ((shown.isNotEmpty() || ! size.words) && summary.startsWith (shown) && (shown.isEmpty() || shown == summary || shown.endsWithChar ('.')),
                       juce::String (size.what) + ": what it heard, in whole sentences (" + juce::String (shown.length()) + " of "
                           + juce::String (summary.length()) + " characters)");
                check (problems.isEmpty(), juce::String (size.what) + ": no text cut, clipped, small or covered "
                                               + (problems.isEmpty() ? juce::String() : "(" + problems.joinIntoString (", ") + ")"));
            }

            check (panel.getShownSummary() == summary, "given the whole height, it shows all it heard");
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--pluginfindtest"))
        {
            // The look for new plugins every start makes: a plugin is new once. Copies of Nightshift
            // Lofi's VST3s in a folder of their own, Vinyl without its moduleinfo.json (as Surge XT,
            // CHOWTapeModel and TAL's come, listed by the module inside the bundle): both new the first
            // time; the next look scans nothing and finds nothing; a new build of Haze is scanned, isn't
            // new, and its date reaches the list and its listeners (the studio writes Plugins.xml), so
            // the look after scans nothing; a plugin put there later is new, and only it
            int failures = 0;
            auto check = [&failures] (bool ok, const juce::String& what)
            {
                std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
                failures += ok ? 0 : 1;
            };

            const auto source = paths::userVst3();
            const auto haze = source.getChildFile ("Nightshift Haze.vst3");
            const auto vinyl = source.getChildFile ("Nightshift Vinyl.vst3");
            const auto crush = source.getChildFile ("Nightshift Crush.vst3");

            if (! haze.isDirectory() || ! vinyl.isDirectory() || ! crush.isDirectory())
            {
                std::cout << "SKIP  Nightshift Lofi's Haze, Vinyl and Crush aren't in " << source.getFullPathName() << std::endl;
                std::cout << "ALL PASSED" << std::endl;
                return 0;
            }

            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("nightshift-pluginfind-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
            folder.createDirectory();
            auto put = [&folder] (const juce::File& bundle) { return bundle.copyDirectoryTo (folder.getChildFile (bundle.getFileName())); };
            put (haze);
            put (vinyl);
            folder.getChildFile ("Nightshift Vinyl.vst3/Contents/Resources/moduleinfo.json").deleteFile();

            AudioEngine::pluginFolderOverride = folder;
            {
                Song song;
                song.loadEmpty();
                AudioEngine audio (song, false);
                auto& list = audio.getKnownPlugins();
                list.clear();
                list.clearBlacklistedFiles();

                struct Heard : public juce::ChangeListener
                {
                    int times = 0;
                    void changeListenerCallback (juce::ChangeBroadcaster*) override   { ++times; }
                } heard;

                list.addChangeListener (&heard);

                auto look = [&audio] (const juce::String& when)
                {
                    juce::StringArray found;
                    bool done = false;
                    audio.findNewPlugins ([&found, &done] (const juce::StringArray& names) { found = names; done = true; });

                    for (int i = 0; i < 6000 && ! done; ++i)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

                    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);     // the list's own news
                    std::cout << "  " << when << ": scanned " << audio.getPluginsScannedLastTime() << ", new: "
                              << (found.isEmpty() ? juce::String ("none") : found.joinIntoString (", ")) << std::endl;
                    return found;
                };

                auto listed = [&list] (const juce::String& name)
                {
                    for (auto& type : list.getTypes())
                        if (type.name == name)
                            return type;

                    return juce::PluginDescription();
                };

                const auto first = look ("first look");
                check (first.size() == 2 && first.contains ("Nightshift Haze") && first.contains ("Nightshift Vinyl")
                         && audio.getPluginsScannedLastTime() == 2,
                       "the first look finds both copies new");

                const auto vinylFile = listed ("Nightshift Vinyl").fileOrIdentifier;
                std::cout << "  Vinyl is listed as " << vinylFile << std::endl;
               #if JUCE_WINDOWS
                check (vinylFile.contains ("Contents"),
                       "Vinyl, without a moduleinfo.json, is listed by the module inside its bundle (as Surge XT is)");
               #endif

                const auto second = look ("second look");
                check (second.isEmpty() && audio.getPluginsScannedLastTime() == 0,
                       "the next look scans nothing and finds nothing new (Vinyl was \"new\" at every start)");

                // A new build of Haze: the bundle made again, so it has a new date
                folder.getChildFile (haze.getFileName()).deleteRecursively();
                juce::Thread::sleep (50);
                put (haze);
                const auto rebuilt = folder.getChildFile (haze.getFileName()).getLastModificationTime();
                const auto heardBefore = heard.times;
                const auto third = look ("after a new build of Haze");
                check (third.isEmpty() && audio.getPluginsScannedLastTime() == 1,
                       "a new build of Haze is scanned again, and isn't a new plugin");
                check (listed ("Nightshift Haze").lastFileModTime == rebuilt && heard.times > heardBefore,
                       "its new date is in the list, and the list's listeners heard (the studio writes Plugins.xml then)");

                const auto fourth = look ("the look after");
                check (fourth.isEmpty() && audio.getPluginsScannedLastTime() == 0,
                       "the look after that scans nothing: the new build's date was kept");

                put (crush);
                const auto fifth = look ("after Crush is put there");
                check (fifth.size() == 1 && fifth[0] == "Nightshift Crush" && audio.getPluginsScannedLastTime() == 1,
                       "a plugin put there later is new, and only it");

                list.removeChangeListener (&heard);
            }

            AudioEngine::pluginFolderOverride = juce::File();
            folder.deleteRecursively();
            std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--findplugins"))
        {
            // The new-plugin finder the studio runs at start, on this machine's plugins: what it
            // finds (each scanned in a process of its own) and how long it takes (nothing's saved)
            Song song;
            song.loadEmpty();
            AudioEngine audio (song, false);
            const auto before = audio.getKnownPlugins().getNumTypes();
            const auto started = juce::Time::getMillisecondCounter();
            std::atomic<bool> done { false };
            juce::StringArray found;

            // Asked twice (the second as Find new instruments would be, mid-scan): one scan,
            // and both hear what it found
            bool secondHeard = false;
            audio.findNewPlugins ([&] (const juce::StringArray& names) { found = names; done = true; });
            const bool busy = audio.isFindingPlugins();
            audio.findNewPlugins ([&] (const juce::StringArray&) { secondHeard = true; });

            for (int i = 0; i < 6000 && ! done; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

            std::cout << (busy && secondHeard && ! audio.isFindingPlugins() ? "PASS" : "FAIL")
                      << "  asked again mid-scan, the second asker hears the same scan's result" << std::endl;
            std::cout << "knew " << before << ", found " << found.size() << " new in " << (juce::Time::getMillisecondCounter() - started) / 1000.0 << " s:" << std::endl;

            for (auto& type : audio.getKnownPlugins().getTypes())
                std::cout << "  " << (found.contains (type.name) ? "NEW " : "    ") << type.name << " (" << type.manufacturerName << ", "
                          << (type.isInstrument ? "instrument" : "effect") << ")" << std::endl;

            std::cout << "  not loadable: " << audio.getKnownPlugins().getBlacklistedFiles().joinIntoString (", ") << std::endl;
            return done ? 0 : 1;
        }

        if (args.has ("--downloadfile"))
        {
            // Nightshift's downloader on its own (--downloadfile <url> <target>): what it says, and
            // the size it ends up
            const auto url = args.valueAfter ("--downloadfile");
            const juce::File target (args.valueAfter (url));
            const auto part = target.withFileExtension (target.getFileExtension() + ".part");
            std::cout << (part.existsAsFile() ? "carrying on from " + juce::String (part.getSize()) + " bytes" : juce::String ("starting")) << std::endl;
            const auto error = drumkits::downloadFile (url, target);
            std::cout << (error.isEmpty() ? "done: " + juce::String (target.getSize()) + " bytes" : "FAILED " + error) << std::endl;
            return error.isEmpty() ? 0 : 1;
        }

        if (args.has ("--kitfetch"))
        {
            // The real thing, one studio kit (--kitfetch <folder> <CrocellKit|MuldjordKit|DRSKit>):
            // downloaded from drumgizmo.org by Nightshift's own fetch into <folder>, carrying on
            // from a part already there, then mixed, as Get the studio kits does it
            drumkits::folderOverride = juce::File (args.valueAfter ("--kitfetch"));
            const auto name = args.valueAfter (args.valueAfter ("--kitfetch"));
            auto& list = drumkits::downloads();
            list.erase (std::remove_if (list.begin(), list.end(), [&name] (const drumkits::Download& d) { return d.name != name; }), list.end());

            if (list.empty())
                return 1;

            const auto part = drumkits::downloadsFolder().getChildFile (juce::URL (list.front().url).getFileName() + ".part");
            std::cout << name << ": " << (part.existsAsFile() ? "carrying on from " + juce::String (part.getSize() / 1048576) + " MB" : juce::String ("starting"))
                      << std::endl;

            const auto started = juce::Time::getMillisecondCounter();
            int shown = -1;
            const auto error = drumkits::fetch ([&shown, started] (float f, const juce::String& what)
            {
                if (const int step = (int) (f * 50.0f); step != shown)
                {
                    shown = step;
                    std::cout << "  " << juce::String (f * 100.0f, 1) << "% " << what << " ("
                              << (juce::Time::getMillisecondCounter() - started) / 1000 << " s)" << std::endl;
                }
            }, {});

            std::cout << (error.isEmpty() ? "fetched" : "FAILED " + error) << " in " << (juce::Time::getMillisecondCounter() - started) / 1000 << " s; kits: "
                      << drumkits::ids().joinIntoString (", ") << std::endl;
            return error.isEmpty() ? 0 : 1;
        }

        if (args.has ("--kitconvert"))
        {
            // A downloaded studio kit's zip converted the way Get the studio kits does it, into
            // <folder> (--kitconvert <zip> <folder>), with each kit's pieces and size listed
            const juce::File zip (args.valueAfter ("--kitconvert"));
            drumkits::folderOverride = juce::File (args.valueAfter (zip.getFullPathName()));
            drumkits::folderOverride.createDirectory();
            int failures = 0;

            for (auto& d : drumkits::downloads())
            {
                if (juce::URL (d.url).getFileName() != zip.getFileName())
                    continue;

                for (auto& recipe : d.recipes)
                {
                    const auto started = juce::Time::getMillisecondCounter();
                    int shown = -1;
                    const auto error = drumkits::drumgizmo::convert (zip, recipe, drumkits::folder().getChildFile (recipe.id), [&shown] (float f)
                    {
                        if (const int tenth = (int) (f * 10.0f); tenth != shown)
                            std::cout << "  " << (shown = tenth) * 10 << "%" << std::flush;
                    }, {});

                    std::cout << std::endl << recipe.name << ": " << (error.isEmpty() ? "converted" : "FAILED " + error) << " in "
                              << (juce::Time::getMillisecondCounter() - started) / 1000.0 << " s" << std::endl;
                    failures += error.isEmpty() ? 0 : 1;

                    if (auto kit = drumkits::read (drumkits::folder().getChildFile (recipe.id), true))
                    {
                        double seconds = 0.0;

                        for (auto& s : kit->samples)
                            seconds += s.audio.getNumSamples() / s.sampleRate;

                        std::cout << "  " << kit->samples.size() << " hits, " << juce::String (seconds, 1) << " s of stereo, "
                                  << juce::String (seconds * kit->samples.front().sampleRate * 2 * 4 / 1048576.0, 0) << " MB in memory, "
                                  << juce::String (kit->samples.front().sampleRate, 0) << " Hz" << std::endl;

                        for (auto& [key, piece] : kit->pieces)
                            std::cout << "  " << key << " " << piece.name << ": " << piece.regions.size() << " hits" << std::endl;

                        for (auto& voice : drumkits::voiceNames())
                            std::cout << (drumkits::keyForVoice (*kit, voice) >= 0 ? "" : "  (no " + voice + ")");

                        std::cout << std::endl;
                    }
                }
            }

            return failures == 0 ? 0 : 1;
        }

        if (args.has ("--kitaudition"))
        {
            // A kit played through the engine as a drummer would (--kitaudition <kit> <out.wav>
            // [--kits <folder>]): two bars of rock with ghost notes, a tom fill into a crash, a bar
            // on the ride, then single hits of each drum soft, medium and hard
            if (auto folder = args.valueAfter ("--kits"); folder.isNotEmpty())
                drumkits::folderOverride = juce::File (folder);

            const auto id = args.valueAfter ("--kitaudition");
            const juce::File out (args.valueAfter (id));
            Song song;
            song.loadEmpty();
            song.bpm = 100.0;
            AudioEngine audio (song, false);
            auto& channel = song.addChannel ("Drums", InstrumentType::kit);
            channel.kit = id;
            song.notify (Song::Change::channels);

            const juce::String groove = R"({ "title": "Audition", "message": "", "tempo": 0, "meter": "4/4",
                "feel": { "swing": 0, "swing_note": "8th", "looseness_ms": 0, "push": [] },
                "sections": [
                  { "name": "Groove", "bars": [
                    { "steps": 16, "lanes": [ { "drum": "kick", "hits": "8.....6.8......." }, { "drum": "snare", "hits": "....8..g.g..8..g" },
                                              { "drum": "hat_closed", "hits": "6.4.6.4.6.4.6.4." } ] },
                    { "steps": 16, "lanes": [ { "drum": "kick", "hits": "8.....6.8.....5." }, { "drum": "snare", "hits": "....8..g.g..9..." },
                                              { "drum": "hat_closed", "hits": "6.4.6.4.6.4." }, { "drum": "hat_open", "hits": "............6..." },
                                              { "drum": "hat_pedal", "hits": "..............5." } ] },
                    { "steps": 16, "lanes": [ { "drum": "kick", "hits": "8.......8......." }, { "drum": "snare", "hits": "....8.......7777" },
                                              { "drum": "tom_high", "hits": "........77......" }, { "drum": "tom_mid", "hits": "..........77...." },
                                              { "drum": "tom_floor", "hits": "............8888" }, { "drum": "hat_closed", "hits": "6.6.6.6........." } ] },
                    { "steps": 16, "lanes": [ { "drum": "kick", "hits": "9.....6.8......." }, { "drum": "crash", "hits": "9..............." },
                                              { "drum": "snare", "hits": "....8.......8..." }, { "drum": "ride", "hits": "..6.6.6.6.6.6.6." },
                                              { "drum": "ride_bell", "hits": "..............7." } ] } ] },
                  { "name": "Hits", "bars": [
                    { "steps": 16, "lanes": [ { "drum": "kick", "hits": "3...6...9......." }, { "drum": "snare", "hits": "........3...6..." } ] },
                    { "steps": 16, "lanes": [ { "drum": "snare", "hits": "9..............." }, { "drum": "sidestick", "hits": "....6..........." },
                                              { "drum": "crash_2", "hits": "........8......." } ] },
                    { "steps": 16, "lanes": [ { "drum": "china", "hits": "8..............." }, { "drum": "splash", "hits": "........8......." } ] } ] } ],
                "arrangement": [ "Groove", "Hits" ] })";

            auto parsed = drummer::Groove::fromJson (juce::JSON::parse (groove));

            if (! parsed.has_value())
                return 1;

            drummer::render (song, channel.id, *parsed, drummer::keyMapFor (channel));
            drummer::placeInArrange (song, channel.id, *parsed);
            song.mode = PlayMode::song;
            song.notify (Song::Change::everything);
            const bool ok = audio.renderToFile (out);
            std::cout << (ok ? "rendered " : "render failed ") << out.getFullPathName() << std::endl;
            return ok ? 0 : 1;
        }

        // The drummer's part and the bassist's line with it, rendered and measured (--bassrender
        // out.wav "trap" [--bassonly]), so how it sounds can be checked by more than ear
        if (args.has ("--bassrender"))
        {
            Song song;
            song.loadBlank();
            const juce::File out (args.valueAfter ("--bassrender"));
            const auto words = args.tokens[args.tokens.indexOf ("--bassrender") + 2].unquoted();

            if (drumkits::ids().isEmpty())
                return 1;

            const int drumsId = song.addChannel ("Drums", InstrumentType::kit).id;
            const int bassId = song.addChannel ("Bass", InstrumentType::synth).id;
            song.findChannel (drumsId)->kit = drumkits::ids()[0];
            song.findChannel (bassId)->synthPreset = "Finger Bass";

            const auto groove = drummer::offline::write (words, song, nullptr, drummer::kitNamesFor (*song.findChannel (drumsId)), true, {}, true, 1).groove;

            if (const auto kit = drumkits::idForName (groove.kit); kit.isNotEmpty())
                song.findChannel (drumsId)->kit = kit;

            drummer::render (song, drumsId, groove, drummer::keyMapFor (*song.findChannel (drumsId)));
            const auto r = bass::offline::write (words, song, nullptr, &groove, true, true, 1);

            AudioEngine audio (song, false);
            audio.syncNow();

            if (auto patch = nova::Patch::fromVar (r.part.patch))
                audio.applySynthPatch (bassId, *patch);

            bass::render (song, bassId, r.part);
            drummer::placeInArrange (song, drumsId, groove);
            bass::placeInArrange (song, bassId, r.part);
            song.mode = PlayMode::song;
            song.findChannel (drumsId)->muted = args.has ("--bassonly");
            song.notify (Song::Change::everything);
            audio.syncNow();

            const bool ok = audio.renderToFile (out);
            const auto m = mastering::measureFile (out);
            std::cout << (ok ? "rendered " : "render failed ") << out.getFullPathName() << std::endl
                      << r.part.title << " on " << r.part.sound << " (" << groove.title << ", kit " << groove.kit << ")" << std::endl
                      << "integrated " << juce::String (m.integratedLufs, 1) << " LUFS, true peak " << juce::String (m.truePeakDb, 1)
                      << " dBTP, range " << juce::String (m.loudnessRangeLu, 1) << " LU" << std::endl;
            return ok ? 0 : 1;
        }

        // The whole band, rendered and measured (--bandrender out.wav "metalcore" [--only drums|bass|guitar|keys|synth]
        // [--seed n]: another take from every player):
        // the drummer, then the bassist, the guitarist, the keys and the synth with it, each on the
        // sound it picks, so where each sits in the mix can be measured (the words may name the
        // synth's job: "metalcore pad"). With --master "words" the AI mastering engineer masters it
        // first (from a raw capture, as the Master tab does) and says what it did
        if (args.has ("--bandrender"))
        {
            Song song;
            song.loadBlank();
            const juce::File out (args.valueAfter ("--bandrender"));
            const auto words = args.tokens[args.tokens.indexOf ("--bandrender") + 2].unquoted();
            const auto only = args.valueAfter ("--only");
            const juce::int64 seed = args.has ("--seed") ? args.valueAfter ("--seed").getLargeIntValue() : 1;   // every player's

            // As a computer without the downloads plays it (a Mac's, so far): --nosamples hides the sampled
            // instruments (the keys on a Nova sound), --kit "808" forces the drum kit
            if (args.has ("--nosamples"))
            {
                SfzPlugin::folderOverride = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Nightshift no instruments");
                SfzPlugin::folderOverride.deleteRecursively();
            }

            if (drumkits::ids().isEmpty())
                return 1;

            const int drumsId = song.addChannel ("Drums", InstrumentType::kit).id;
            const int bassId = song.addChannel ("Bass", InstrumentType::synth).id;
            const int guitarId = song.addChannel ("Guitar", InstrumentType::instrument).id;
            song.findChannel (drumsId)->kit = drumkits::ids()[0];
            song.findChannel (bassId)->synthPreset = "Finger Bass";

            auto groove = drummer::offline::write (args.has ("--song") ? songwriter::drummerWords (words, agents::Request::parse (words).style) : words,
                                                   song, nullptr, drummer::kitNamesFor (*song.findChannel (drumsId)), true, {}, true, seed).groove;

            // --song: in the order the song agent plays it, two minutes or more (verses and choruses coming back)
            if (args.has ("--song"))
            {
                const double tempo = groove.tempo > 0 ? groove.tempo : song.bpm;
                groove.arrangement = songwriter::arrange (groove, agents::Request::parse (words).style, words, 4.0 * 60.0 / tempo);
            }

            if (const auto kit = drumkits::idForName (groove.kit); kit.isNotEmpty())
                song.findChannel (drumsId)->kit = kit;

            if (const auto forced = args.valueAfter ("--kit").unquoted(); forced.isNotEmpty())
            {
                // Its name, its id, or a part of its name ("808": Nightshift 808)
                auto kit = drumkits::idForName (forced);

                for (auto& id : drumkits::ids())
                    if (kit.isEmpty() && (id.equalsIgnoreCase (forced) || drumkits::displayName (id).containsIgnoreCase (forced)))
                        kit = id;

                if (kit.isNotEmpty())
                {
                    song.findChannel (drumsId)->kit = kit;
                    std::cout << "--kit: " << drumkits::displayName (kit) << std::endl;
                }
                else
                    std::cout << "--kit: no kit called \"" << forced << "\"" << std::endl;
            }

            drummer::render (song, drumsId, groove, drummer::keyMapFor (*song.findChannel (drumsId)));
            const auto b = bass::offline::write (words, song, nullptr, &groove, true, true, seed);
            const auto g = guitar::offline::write (words, song, nullptr, &groove, &b.part, true, true, seed);
            song.findChannel (guitarId)->instrument = guitarsound::pathFor (g.part.sound);
            const auto kp = keys::offline::write (words, song, nullptr, &groove, &b.part, true, true, seed);
            const int keysId = song.addChannel ("Keys", kp.part.sampled.isNotEmpty() ? InstrumentType::instrument : InstrumentType::synth).id;

            if (kp.part.sampled.isNotEmpty())
                song.findChannel (keysId)->instrument = kp.part.sampled;
            else
                song.findChannel (keysId)->synthPreset = kp.part.sound;

            song.findChannel (keysId)->volume = juce::jlimit (0.0f, 1.0f, 0.78f * std::pow (10.0f, kp.part.levelDb / 40.0f));
            const auto sp = synth::offline::write (words, song, nullptr, {}, {}, &groove, &b.part, &kp.part, true, true, seed);
            const int synthId = song.addChannel ("Synth", InstrumentType::synth).id;
            song.findChannel (synthId)->synthPreset = sp.part.sound;
            song.findChannel (synthId)->volume = juce::jlimit (0.0f, 1.0f, 0.78f * std::pow (10.0f, sp.part.levelDb / 40.0f));
            song.findChannel (guitarId)->volume = juce::jlimit (0.0f, 1.0f, 0.78f * std::pow (10.0f, g.part.levelDb / 40.0f));

            AudioEngine audio (song, false);
            audio.syncNow();

            if (auto patch = nova::Patch::fromVar (b.part.patch))
                audio.applySynthPatch (bassId, *patch);

            bass::render (song, bassId, b.part);
            guitar::render (song, guitarId, g.part);
            keys::render (song, keysId, kp.part);
            synth::render (song, synthId, sp.part);
            drummer::placeInArrange (song, drumsId, groove);
            bass::placeInArrange (song, bassId, b.part);
            guitar::placeInArrange (song, guitarId, g.part);
            keys::placeInArrange (song, keysId, kp.part);
            synth::placeInArrange (song, synthId, sp.part);
            song.mode = PlayMode::song;
            song.findChannel (drumsId)->muted = only.isNotEmpty() && only != "drums";
            song.findChannel (bassId)->muted = only.isNotEmpty() && only != "bass";
            song.findChannel (guitarId)->muted = only.isNotEmpty() && only != "guitar";
            song.findChannel (keysId)->muted = only.isNotEmpty() && only != "keys";
            song.findChannel (synthId)->muted = only.isNotEmpty() && only != "synth";
            song.notify (Song::Change::everything);
            audio.syncNow();

            if (args.has ("--master"))
            {
                const auto capture = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NightShift band master capture.wav");
                audio.captureMix (capture, true);
                const auto r = engineer::master (args.valueAfter ("--master").unquoted(), capture, agents::Request::parse (words).style, nullptr);
                capture.deleteFile();

                if (r.ok)
                {
                    song.mastering = r.settings;
                    song.notify (Song::Change::mixer);
                    audio.syncNow();
                }

                std::cout << (r.ok ? r.reply : "master failed: " + r.problem) << std::endl
                          << "settings " << juce::JSON::toString (r.settings.toVar(), true) << std::endl
                          << "bands before " << r.before.bands[0] << " " << r.before.bands[1] << " " << r.before.bands[2] << " " << r.before.bands[3]
                          << ", after " << r.after.bands[0] << " " << r.after.bands[1] << " " << r.after.bands[2] << " " << r.after.bands[3]
                          << ", wanted " << r.wanted[0] << " " << r.wanted[1] << " " << r.wanted[2] << " " << r.wanted[3]
                          << "; sides " << r.before.sidesDb << " dB, low sides " << r.before.lowSidesDb << " dB, correlation " << r.before.correlation
                          << ", peak to loudness " << r.before.peakToLoudness << std::endl;
            }

            const bool ok = audio.renderToFile (out);
            const auto m = mastering::measureFile (out);

            // Each section's level (RMS over its bars, in song order), to check the quiet parts stay quiet
            juce::String levels;
            {
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();

                if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (out)); reader != nullptr && song.bpm > 0.0)
                {
                    const double barSeconds = song.meterBeats * (4.0 / juce::jmax (1, song.meterUnit)) * 60.0 / song.bpm;
                    double at = 0.0;

                    for (auto& name : groove.arrangement)
                    {
                        int bars = 0;

                        for (auto& s : groove.sections)
                            if (s.name == name)
                                bars = (int) s.bars.size();

                        const int length = (int) (bars * barSeconds * reader->sampleRate);
                        juce::AudioBuffer<float> buffer ((int) reader->numChannels, juce::jmax (1, length));
                        reader->read (&buffer, 0, length, (juce::int64) (at * reader->sampleRate), true, true);
                        double sum = 0.0;

                        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                            for (auto* s = buffer.getReadPointer (ch), *end = s + length; s < end; ++s)
                                sum += (double) *s * *s;

                        const auto rms = (float) std::sqrt (sum / juce::jmax (1, length * buffer.getNumChannels()));
                        levels << (levels.isEmpty() ? "" : ", ") << name << " " << juce::String (juce::Decibels::gainToDecibels (rms, -100.0f), 1);
                        at += bars * barSeconds;
                    }
                }
            }

            std::cout << (ok ? "rendered " : "render failed ") << out.getFullPathName() << std::endl
                      << sp.part.title << " on " << sp.part.sound << ", " << kp.part.title << " on " << kp.part.sound << ", " << g.part.title << " on " << g.part.sound << ", " << b.part.title << " on " << b.part.sound << " (" << groove.title << ")" << std::endl
                      << "integrated " << juce::String (m.integratedLufs, 1) << " LUFS, true peak " << juce::String (m.truePeakDb, 1)
                      << " dBTP, range " << juce::String (m.loudnessRangeLu, 1) << " LU" << std::endl
                      << "sections (RMS dBFS) at " << song.bpm << " bpm: " << levels << std::endl;

            // The bassist's chords, section by section in the song's order (what the vibe listener should hear)
            for (auto& name : groove.arrangement)
                for (auto& s : b.part.sections)
                    if (s.name == name)
                    {
                        juce::StringArray chords;

                        for (auto& bar : s.bars)
                            chords.add (bar.chord);

                        std::cout << "chords " << name << ": " << chords.joinIntoString (" ") << std::endl;
                    }

            return ok ? 0 : 1;
        }

        // What the vibe listener hears in a song (--listen song.mp3 [--closeness 0..1] [--seed n] [--as style]): its
        // vibe, each section's chords bar by bar, the styles it's nearest, and the song agent's words for a new song with it
        if (args.has ("--listen"))
        {
            const juce::File file (args.valueAfter ("--listen").unquoted());
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto v = vibes::listen (file);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;

            if (! v.ok())
            {
                std::cout << "couldn't listen: " << v.problem << std::endl;
                return 1;
            }

            std::cout << "heard " << file.getFileName() << " in " << juce::String (ms / 1000.0, 1) << " s (" << juce::String (v.seconds, 1) << " s of audio)" << std::endl
                      << v.summary() << std::endl
                      << "first bar at " << juce::String (v.firstBar, 2) << " s; tempo " << juce::String (v.tempo, 1) << " (confidence " << juce::String (v.tempoConfidence, 2) << "), key " << v.keyName()
                      << " (" << juce::String (v.keyConfidence, 2) << "), swing " << juce::String (v.swing, 2) << (v.swingSixteenths ? " 16ths" : " 8ths")
                      << ", brightness " << juce::String (v.brightness, 2) << ", grit " << juce::String (v.grit, 2) << ", low end " << juce::String (v.lowEnd, 2) << std::endl;

            for (auto& s : v.sections)
                std::cout << "  " << s.label << " " << s.kind << " bar " << s.startBar + 1 << ", " << s.bars << " bars, " << juce::String (s.energyDb, 1) << " dB: "
                          << s.chords.joinIntoString (" ") << std::endl;

            for (size_t band = 0; band < 3; ++band)
            {
                std::cout << (band == 0 ? "  kick " : band == 1 ? "  snare" : "  hats ");

                for (auto x : v.pattern[band])
                    std::cout << " " << juce::String (x, 1);

                std::cout << std::endl;
            }

            for (size_t i = 0; i < std::min<size_t> (6, v.styles.size()); ++i)
                std::cout << "  " << v.styles[i].style << " " << juce::String (v.styles[i].score, 3) << (v.styles[i].why.isNotEmpty() ? " (" + v.styles[i].why + ")" : juce::String()) << std::endl;

            // --features: what it measured, on one line (tools/vibes/learn_ears.js reads it)
            if (args.has ("--features"))
            {
                auto* f = new juce::DynamicObject();
                juce::Array<juce::var> pattern;

                for (auto& band : v.pattern)
                    for (auto x : band)
                        pattern.add (x);

                f->setProperty ("tempo", v.tempo);
                f->setProperty ("swing", v.swing);
                f->setProperty ("swingSixteenths", v.swingSixteenths);
                f->setProperty ("mode", v.mode);
                f->setProperty ("pattern", pattern);
                f->setProperty ("highShare", v.highShare);
                f->setProperty ("heldFlatness", v.heldFlatness);
                f->setProperty ("lowShare", v.lowShare);
                f->setProperty ("minorShare", v.minorShare);
                f->setProperty ("seventhShare", v.seventhShare);
                f->setProperty ("powerShare", v.powerShare);
                f->setProperty ("firstBar", v.firstBar);
                std::cout << "features " << juce::JSON::toString (juce::var (f), true) << std::endl;
            }

            const auto plan = vibes::planFrom (v, (float) args.valueAfter ("--closeness").getDoubleValue() + (args.has ("--closeness") ? 0.0f : 1.0f),
                                               args.valueAfter ("--seed").getLargeIntValue(), args.valueAfter ("--as").unquoted());
            std::cout << "words: " << plan.words << std::endl
                      << "kept: " << plan.kept.joinIntoString ("; ") << std::endl
                      << "changed: " << plan.changed.joinIntoString ("; ") << std::endl;
            return 0;
        }

        if (args.has ("--render"))
        {
            Song song;
            applySongOptions (song, args);
            AudioEngine audio (song, false);

            const auto out = juce::File (args.valueAfter ("--render"));
            const bool ok = audio.renderToFile (out);
            std::cout << (ok ? "rendered " : "render failed ") << out.getFullPathName() << std::endl;
            return ok ? 0 : 1;
        }

        // Every track icon, small and large, to look over
        if (args.has ("--iconsheet"))
        {
            const auto& all = trackicons::all();
            const int cell = 150;
            juce::Image sheet (juce::Image::RGB, cell * 6, cell * (((int) all.size() + 5) / 6), true);
            {
                juce::Graphics g (sheet);
                g.fillAll (theme::panel);

                for (int i = 0; i < (int) all.size(); ++i)
                {
                    const auto area = juce::Rectangle<float> ((float) (i % 6) * cell, (float) (i / 6) * cell, (float) cell, (float) cell);
                    trackicons::draw (g, all[(size_t) i].id, area.withSizeKeepingCentre (72.0f, 72.0f).translated (0.0f, -22.0f), theme::text);

                    // As a track header shows it: a 32 px tile, the icon 20 px
                    auto tile = juce::Rectangle<float> (32.0f, 32.0f).withCentre ({ area.getCentreX() - 22.0f, area.getBottom() - 42.0f });
                    g.setColour (theme::accentCool.withAlpha (0.18f));
                    g.fillRoundedRectangle (tile, 8.0f);
                    trackicons::draw (g, all[(size_t) i].id, tile.reduced (6.0f), theme::accentCool.interpolatedWith (juce::Colours::white, 0.45f));
                    trackicons::draw (g, all[(size_t) i].id, juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ area.getCentreX() + 16.0f, tile.getCentreY() }),
                                      theme::textDim);

                    g.setColour (theme::textDim);
                    g.setFont (theme::font (15.0f, true));
                    g.drawText (all[(size_t) i].name, area.withTop (area.getBottom() - 22.0f), juce::Justification::centred, false);
                }
            }

            const juce::File out (args.valueAfter ("--iconsheet"));
            out.deleteFile();
            juce::FileOutputStream stream (out);
            return juce::PNGImageFormat().writeImageToStream (sheet, stream) ? 0 : 1;
        }

        // A menu as NightShift draws them (a track's right-click menu, more or less), to check it
        // reads at a glance: a heading, plain, ticked, hovered, greyed-out and submenu items, a
        // separator and a shortcut. Real menus are windows of their own, out of a snapshot's reach
        if (args.has ("--menupreview"))
        {
            NightLookAndFeel laf;
            struct Item { juce::String text, shortcut; bool separator = false, header = false, active = true, highlighted = false, ticked = false, sub = false; };
            const std::vector<Item> items {
                { "Bass", {}, false, true }, { "Rename..." }, { "Icon", {}, false, false, true, false, false, true },
                { "Mute", {}, false, false, true, false, true }, { "Solo", {}, false, false, true, true }, { {}, {}, true },
                { "Add a track under this one" }, { "Delete this track" }, { "No instruments found yet", {}, false, false, false },
                { {}, {}, true }, { "Save", "Ctrl+S" } };

            const int border = laf.getPopupMenuBorderSize();
            int width = 0, height = 0;
            std::vector<int> heights;

            for (auto& item : items)
            {
                int w = 0, h = 0;

                if (item.header)
                    laf.getIdealPopupMenuSectionHeaderSizeWithOptions (item.text, 0, w, h, juce::PopupMenu::Options());
                else
                    laf.getIdealPopupMenuItemSize (item.text + (item.shortcut.isNotEmpty() ? "   " + item.shortcut : juce::String()), item.separator, 0, w, h);

                width = std::max (width, w);
                heights.push_back (h);
                height += h;
            }

            width = std::max (width, 300);
            juce::Image sheet (juce::Image::RGB, width + border * 2 + 40, height + border * 2 + 40, true);
            {
                juce::Graphics g (sheet);
                g.fillAll (theme::background);
                g.setOrigin (20, 20);
                laf.drawPopupMenuBackground (g, width + border * 2, height + border * 2);
                int y = border;

                for (size_t i = 0; i < items.size(); ++i)
                {
                    const auto& item = items[i];
                    const juce::Rectangle<int> area (border, y, width, heights[i]);

                    if (item.header)
                        laf.drawPopupMenuSectionHeader (g, area, item.text);
                    else
                        laf.drawPopupMenuItem (g, area, item.separator, item.active, item.highlighted, item.ticked, item.sub,
                                               item.text, item.shortcut, nullptr, nullptr);

                    y += heights[i];
                }
            }

            const juce::File out (args.valueAfter ("--menupreview"));
            out.deleteFile();
            juce::FileOutputStream stream (out);
            return juce::PNGImageFormat().writeImageToStream (sheet, stream) ? 0 : 1;
        }

        if (args.has ("--snapshot"))
        {
            Song song;
            applySongOptions (song, args);

            // --live opens the real interface (for the Audio settings as you'd see them)
            AudioEngine audio (song, args.has ("--live"));

            // --nokits: as it is before the drum kits are fetched
            if (args.has ("--nokits"))
            {
                drumkits::folderOverride = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Nightshift no kits");
                drumkits::folderOverride.deleteRecursively();
            }

            int width = 1600, height = 940;

            if (auto size = args.valueAfter ("--size"); size.containsChar ('x'))
            {
                width = size.upToFirstOccurrenceOf ("x", false, false).getIntValue();
                height = size.fromFirstOccurrenceOf ("x", false, false).getIntValue();
            }

            // Below the studio window's own minimum the panels have nowhere to go
            if (width < MainComponent::minWidth || height < MainComponent::minHeight)
            {
                width = std::max (width, MainComponent::minWidth);
                height = std::max (height, MainComponent::minHeight);
                std::cout << "size raised to " << width << "x" << height << ", the smallest the studio allows" << std::endl;
            }

            MainComponent main (song, audio);
            main.setSize (width, height);

            if (auto show = args.valueAfter ("--show"); show.isNotEmpty())
            {
                main.setPanelVisible (MainComponent::Panel::playlist, show.contains ("playlist"));
                main.setPanelVisible (MainComponent::Panel::rack, show.contains ("rack"));
                main.setPanelVisible (MainComponent::Panel::pianoRoll, show.contains ("piano"));
            }

            auto panelNamed = [] (const juce::String& name)
            {
                return name == "rack" ? MainComponent::Panel::rack
                                      : name == "piano" ? MainComponent::Panel::pianoRoll
                                      : name == "mixer" ? MainComponent::Panel::mixer
                                      : name == "drummer" ? MainComponent::Panel::drummer
                                      : name == "bass" ? MainComponent::Panel::bass
                                      : name == "guitar" ? MainComponent::Panel::guitar
                                      : name == "keys" ? MainComponent::Panel::keys
                                      : name == "aisynth" ? MainComponent::Panel::synthPlayer
                                      : name == "song" ? MainComponent::Panel::song
                                      : name == "aimaster" ? MainComponent::Panel::masterAi
                                      : name == "synth" ? MainComponent::Panel::synth
                                      : name == "master" ? MainComponent::Panel::master
                                                        : MainComponent::Panel::playlist;
            };

            // Brings a window forward the way clicking it does
            if (auto raise = args.valueAfter ("--raise"); raise.isNotEmpty())
                main.setPanelVisible (panelNamed (raise), true);

            // The AI Song panel after it's heard a song for its vibe (a made-up one: what the listener says of the band's
            // funk), or while it listens
            if (args.has ("--vibedemo"))
            {
                vibes::Vibe v;
                v.name = "Midnight Drive";
                v.seconds = 131.2;
                v.tempo = 104.2;
                v.keyRoot = 4;
                v.mode = "minor";
                v.swing = 0.33f;
                v.swingSixteenths = true;
                v.brightness = 0.62f;
                v.grit = 0.3f;
                v.lowEnd = 0.55f;
                auto section = [&v] (const char* kind, const char* label, int start, int bars, float db, const juce::String& chords)
                {
                    vibes::Section s;
                    s.kind = kind;
                    s.label = label;
                    s.startBar = start;
                    s.bars = bars;
                    s.energyDb = db;
                    s.chords = juce::StringArray::fromTokens (chords, " ", {});
                    v.sections.push_back (s);
                };

                section ("intro", "A", 0, 4, -7.0f, "Em7 Em7 Em7 Em7");
                section ("verse", "B", 4, 8, -3.0f, "Em7 Em7 A9 Em7 Em7 Em7 A9 Em7");
                section ("chorus", "C", 12, 8, 0.0f, "A9 A9 Em7 Em7 A9 A9 Em7 Em7");
                section ("verse", "B", 20, 8, -3.0f, "Em7 Em7 A9 Em7 Em7 Em7 A9 Em7");
                section ("chorus", "C", 28, 8, 0.0f, "A9 A9 Em7 Em7 A9 A9 Em7 Em7");
                section ("bridge", "D", 36, 8, -2.0f, "D9 A9 D9 A9 D9 A9 D9 A9");
                section ("chorus", "C", 44, 8, 0.0f, "A9 A9 Em7 Em7 A9 A9 Em7 Em7");
                section ("outro", "B", 52, 4, -8.0f, "Em7 Em7 A9 Em7");
                v.styles = { { "funk", 0.9f, "its drums, its tempo, its sound" }, { "synthwave", 0.8f, "its tempo" }, { "hiphop", 0.76f, "its tempo" } };
                main.getSongPanel().showHeard (v);
            }

            if (args.has ("--vibelistening"))
                main.getSongPanel().showListening ("Midnight Drive", 0.42f);

            // A page of the Synth tab, as clicking its name does (0 is Oscillators)
            if (auto synthPage = args.valueAfter ("--synthpage"); synthPage.isNotEmpty())
                main.getSynth().showPage (synthPage.getIntValue());

            // Presses a window's toolbar button once, the way F5/F6/F7 do
            if (auto toggle = args.valueAfter ("--toggle"); toggle.isNotEmpty())
                main.togglePanel (panelNamed (toggle));

            // Arms audio channels by name, as clicking their round button does
            if (auto armed = args.valueAfter ("--arm"); armed.isNotEmpty())
            {
                for (auto& c : song.channels)
                    c.armed = c.type == InstrumentType::audio && armed.contains (c.name);

                song.notify (Song::Change::mixer);
            }

            // Selects a channel by name, as clicking its track or row does
            if (auto selected = args.valueAfter ("--select"); selected.isNotEmpty())
            {
                for (auto& c : song.channels)
                    if (c.name == selected)
                        song.selectedChannelId = c.id;

                song.notify (Song::Change::selection);
            }

            // Text in the sound search, as if typed after Ctrl+K
            if (auto text = args.valueAfter ("--search"); text.isNotEmpty())
                main.getSidebar().setSearchText (text);

            // Sound groups and developers opened, clicked in turn: --opensounds "Plugin effects;Nightshift" (a
            // developer in the group named before it: one can be in both plugin groups)
            if (auto names = args.valueAfter ("--opensounds"); names.isNotEmpty())
            {
                auto& sidebar = main.getSidebar();
                juce::String group;

                for (auto& name : juce::StringArray::fromTokens (names, ";", ""))
                {
                    if (Sidebar::isGroupName (name.trim()))
                        group = name.trim();

                    const auto area = sidebar.showSoundRow (name.trim(), group);

                    if (area.isEmpty())
                    {
                        std::cout << "--opensounds: no \"" << name.trim() << "\" showing in the sounds" << std::endl;
                        continue;
                    }

                    const auto at = area.getCentre().toFloat();
                    const auto now = juce::Time::getCurrentTime();
                    sidebar.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                                                         juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                         &sidebar, &sidebar, now, at, now, 1, false));
                }
            }

            // Editing as it looks: the first pattern clip split on a beat, a recording faded in and
            // out, every clip selected, and the piano roll's notes selected
            if (args.has ("--editdemo"))
            {
                if (! song.playlist.empty())
                {
                    const int beat = song.ticksPerMeterBeat();
                    song.splitClip (0, song.playlist[0].start + std::max (beat, song.clipLength (song.playlist[0]) / 2 / beat * beat));
                }

                for (auto& folder : song.audioClips)
                {
                    folder.fadeIn = folder.length * 0.2;
                    folder.fadeOut = folder.length * 0.3;
                }

                song.notify (Song::Change::playlist);
                main.getPlaylist().edit (EditCommand::selectAll);
                main.getPianoRoll().edit (EditCommand::selectAll);
                main.repaint();
            }

            // Automation as it looks: lanes shown, the first track's channel's volume drawn and its
            // pan swept, and the master fading out over the last two bars
            if (args.has ("--automationdemo"))
            {
                const int bar = song.ticksPerBar();
                int first = 0;

                for (int row = 0; row < song.numTracks() && first == 0; ++row)
                    first = song.trackChannel (row);

                if (first != 0)
                {
                    song.laneFor (first, "volume", "Volume").points = { { 0, 0.55f }, { 4 * bar, 0.35f }, { 6 * bar, 0.72f }, { 8 * bar, 0.72f } };
                    song.laneFor (first, "pan", "Pan").points = { { 0, -0.6f }, { 8 * bar, 0.6f } };
                }

                const int end = std::max (4 * bar, song.songLengthInTicks());
                song.laneFor (0, "volume", "Volume").points = { { end - 2 * bar, 0.8f }, { end, 0.0f } };
                song.showAutomation = true;
                song.notify (Song::Change::automation);
                audio.syncNow();
                main.repaint();
            }

            // Tempo and time signature changes as they look: a jump at bar 5, a glide back by bar 9,
            // 7/8 from bar 7 and 4/4 again from bar 9, the tempo lane shown, Song mode at bar 7
            if (args.has ("--tempodemo"))
            {
                const int bar = song.ticksPerBar();
                song.mode = PlayMode::song;
                song.setTempoAt (4 * bar, song.bpm + 32.0);
                song.setTempoAt (8 * bar, song.bpm, true);
                song.setMeterAt (6 * bar, 7, 8);
                song.setMeterAt (song.barStart (8), 4, 4);
                song.showAutomation = true;
                song.notify (Song::Change::transport);
                song.notify (Song::Change::automation);
                audio.syncNow();
                audio.setPlayheadTicks (song.barStart (6) + 48);
                main.repaint();
            }

            // A project from a template, as the start screen makes it
            if (auto id = args.valueAfter ("--template"); id.isNotEmpty())
                templates::build (id, song, audio);

            // The start screen as NightShift opens on it (--recent: projects for its list, made
            // in the temp folder from the demo; --asked: as every time after the first, the donation
            // banner asked already)
            if (args.has ("--startscreen"))
            {
                auto o = audio.getOptions();
                o.donationAsked = args.has ("--asked");

                for (auto name : juce::StringArray::fromTokens (args.valueAfter ("--recent"), ",", {}))
                    if (name.isNotEmpty())
                    {
                        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NightShift recent")
                                              .getChildFile (name + ".nsproj");
                        Song demo;
                        demo.loadDemo();
                        demo.save (file);
                        o.recentProjects.add (file.getFullPathName());
                    }

                audio.setOptions (o);
                main.showStartScreen();
            }

            // A sound held over Arrange mid-drag, at x,y in Arrange (--dragsound: what it is,
            // "pattern:<name>" for a pattern from the sidebar; else Nova's first sound)
            if (auto at = args.valueAfter ("--dragover"); at.containsChar (','))
            {
                auto sound = args.valueAfter ("--dragsound");

                if (sound.startsWith ("pattern:"))
                {
                    for (auto& p : song.patterns)
                        if (p.name == sound.fromFirstOccurrenceOf ("pattern:", false, false))
                            sound = juce::String (patternDragPrefix) + juce::String (p.id);
                }

                if (sound.isEmpty())
                    sound = juce::String (soundDragPrefix) + "synth:" + nova::factory().front().name;

                main.getPlaylist().itemDragMove ({ sound, nullptr, { at.upToFirstOccurrenceOf (",", false, false).getIntValue(),
                                                                     at.fromFirstOccurrenceOf (",", false, false).getIntValue() } });
            }

            // The Nightshift Lofi effects in the plugin list, with Crush and Haze on Kick and
            // Vinyl on the master
            if (args.has ("--lofi"))
            {
                addNightshiftLofi (audio);

                auto effectNamed = [&song, &audio] (const juce::String& name)
                {
                    Effect e;
                    e.id = song.createId();
                    e.name = name;

                    for (auto& d : audio.getKnownPlugins().getTypes())
                        if (d.name == name)
                            e.pluginId = d.createIdentifierString();

                    return e;
                };

                for (auto& c : song.channels)
                    if (c.name == "Kick")
                        c.effects = { effectNamed ("Nightshift Crush"), effectNamed ("Nightshift Haze") };

                song.masterEffects = { effectNamed ("Nightshift Vinyl") };
                song.notify (Song::Change::mixer);
            }

            // Buses as you might set them up: the kit into a Drums bus with a compressor, and the
            // keys and pad sending to a Reverb bus
            if (args.has ("--buses"))
            {
                auto stockEffect = [&song] (int index)
                {
                    Effect e;
                    e.id = song.createId();
                    e.pluginId = stockfx::descriptions()[index].createIdentifierString();
                    e.name = stockfx::descriptions()[index].name;
                    return e;
                };

                auto& drums = song.addBus ("Drums");
                drums.effects = { stockEffect (1), stockEffect (0) };
                const int drumsId = drums.id;
                auto& verb = song.addBus ("Reverb");
                verb.effects = { stockEffect (2) };
                const int verbId = verb.id;

                for (auto& c : song.channels)
                {
                    if (c.type == InstrumentType::sampler && c.name != "808")
                        c.output = drumsId;

                    if (c.name == "Keys" || c.name == "Pad" || c.name == "Snare")
                        c.sends.push_back ({ verbId, c.name == "Snare" ? 0.45f : 0.62f, false });
                }

                song.notify (Song::Change::channels);
            }

            // A drum kit channel with an AI drummer conversation already had (written here, nothing
            // asked): --drummer shows the studio, --drummer panel its AI Drummer tab, --drummer
            // piano its drums in the piano roll

            if (args.has ("--drummer") && ! drumkits::ids().isEmpty())
            {
                const int id = main.getRack().addDrumKitChannel (drumkits::ids()[0]);
                const juce::String verse = R"({ "title": "Dusty pocket", "message": "A lazy swung groove: feathered kick, a snare that sits back, ghost notes between, and the hat foot keeping the 2 and 4.", "tempo": 0, "meter": "4/4",
                    "feel": { "swing": 0.62, "swing_note": "16th", "looseness_ms": 8, "push": [ { "drum": "snare", "ms": 14 }, { "drum": "hat_closed", "ms": -3 } ] },
                    "sections": [
                      { "name": "Verse", "bars": [
                        { "steps": 16, "lanes": [ { "drum": "kick", "hits": "7.....5...6....." }, { "drum": "snare", "hits": "..g.8..g.g..8.g." },
                                                  { "drum": "hat_closed", "hits": "5.3.5.3.5.3.5.34" }, { "drum": "hat_pedal", "hits": "....4.......4..." } ] },
                        { "steps": 16, "lanes": [ { "drum": "kick", "hits": "7.....5...6...4." }, { "drum": "snare", "hits": "..g.8..g.g..8g.g" },
                                                  { "drum": "hat_closed", "hits": "5.3.5.3.5.3.5.3." }, { "drum": "hat_open", "hits": "..............5." } ] } ] },
                      { "name": "Chorus", "bars": [
                        { "steps": 16, "lanes": [ { "drum": "kick", "hits": "8.....6.5.7....." }, { "drum": "snare", "hits": "..g.9..g.g..9.g." },
                                                  { "drum": "ride", "hits": "6.4.6.4.6.4.6.4." }, { "drum": "crash", "hits": "7..............." } ] },
                        { "steps": 16, "lanes": [ { "drum": "kick", "hits": "8.....6.5.7....." }, { "drum": "snare", "hits": "..g.9..g.g..9.67" },
                                                  { "drum": "ride", "hits": "6.4.6.4.6.4....." }, { "drum": "tom_high", "hits": "..............5." },
                                                  { "drum": "tom_floor", "hits": "...............6" } ] } ] } ],
                    "arrangement": [ "Verse", "Verse", "Chorus", "Verse", "Chorus" ] })";

                auto& channel = *song.findChannel (id);
                channel.drummer.turns.push_back ({ "Lofi jazz on brushes, lazy swing and ghost notes", "A lazy swung groove: feathered kick, a snare that sits back and ghost notes between.",
                                                   verse, "[]" });
                channel.drummer.turns.push_back ({ "Ride in the chorus, and a little tom fill at the end of it",
                                                   "The chorus moves to the ride with a crash on the one, and the last two 16ths drop down the toms into the next verse.",
                                                   verse, "[]" });
                channel.drummer.shown = 1;
                drummer::rerender (song, id);

                if (auto it = channel.drummer.patterns.find ("Verse"); it != channel.drummer.patterns.end())
                {
                    song.currentPatternId = it->second;
                    song.notify (Song::Change::currentPattern);
                }

                if (args.valueAfter ("--drummer") == "piano")
                    main.openPianoRoll (id);

                if (args.valueAfter ("--drummer") == "panel")
                    main.openDrummer (id);
            }

            // The AI bassist's tab after a request (--bass "funk, slap the octaves"): the drummer
            // writes the same words first, then the bassist plays with it
            if (auto words = args.valueAfter ("--bass").unquoted(); words.isNotEmpty())
            {
                main.getDrummer().ask (words);
                main.getBass().ask (words);

                if (main.getBass().getChannelId() != 0)
                    main.openBass (main.getBass().getChannelId());
            }

            // The AI guitarist's tab after a request (--guitar "metalcore with a breakdown"): the
            // drummer and the bassist play the same words first, then the guitarist with them
            if (auto words = args.valueAfter ("--guitar").unquoted(); words.isNotEmpty())
            {
                main.getDrummer().ask (words);
                main.getBass().ask (words);
                main.getGuitar().ask (words);

                if (auto section = args.valueAfter ("--guitarsection"); section.isNotEmpty())
                    main.getGuitar().showSection (section);

                if (main.getGuitar().getChannelId() != 0)
                    main.openGuitar (main.getGuitar().getChannelId());
            }

            // The AI keys player's tab after a request (--keys "neo soul"): the drummer and the bassist
            // play the same words first, then the keys with them
            if (auto words = args.valueAfter ("--keys").unquoted(); words.isNotEmpty())
            {
                main.getDrummer().ask (words);
                main.getBass().ask (words);
                main.getKeys().ask (words);

                if (main.getKeys().getChannelId() != 0)
                    main.openKeys (main.getKeys().getChannelId());
            }

            // The AI synth player's tab after a request (--synthplayer "trap bells"): the drummer, the
            // bassist and the keys play the same words first, then the synth with them
            if (auto words = args.valueAfter ("--synthplayer").unquoted(); words.isNotEmpty())
            {
                main.getDrummer().ask (words);
                main.getBass().ask (words);
                main.getKeys().ask (words);
                main.getSynthPlayer().ask (words);

                if (main.getSynthPlayer().getChannelId() != 0)
                    main.openSynthPlayer (main.getSynthPlayer().getChannelId());
            }

            // The AI mastering engineer's page (--masterai "loud metalcore master", or --masterai "" for its
            // page before it has mastered anything; --masterai master for Master it, no words)
            if (args.has ("--masterai"))
            {
                main.openAgent (MainComponent::Panel::masterAi);
                const auto words = args.valueAfter ("--masterai");

                if (words.isNotEmpty())
                {
                    main.getMasterEngineer().ask (words == "master" ? juce::String() : words);

                    for (int i = 0; i < 2400 && main.getMasterEngineer().isWorking(); ++i)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                }
            }

            // The AI tab on the song agent (--song "" for it before a song, --song "metalcore song" to make one)
            if (args.has ("--song"))
            {
                main.openAgent (MainComponent::Panel::song);

                if (const auto words = args.valueAfter ("--song"); words.isNotEmpty())
                {
                    song.markSaved();
                    main.makeSong (words);

                    for (int i = 0; i < 4000 && (i < 4 || main.getSongPanel().isWorking()); ++i)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                }
            }

            // The Audio settings window (F10) on its own
            std::unique_ptr<AudioSettings> settings;

            if (args.has ("--settings"))
                settings = std::make_unique<AudioSettings> (audio);

            // The metronome's panel on its own (--metronome odd: 7/8 as 2+2+3, triplets, beat 6 off);
            // --armrecord arms Record
            std::unique_ptr<MetronomePanel> metronomePanel;

            if (args.has ("--metronome"))
            {
                if (args.valueAfter ("--metronome") == "odd")
                {
                    song.meterBeats = 7;
                    song.meterUnit = 8;
                    song.notify (Song::Change::transport);
                    audio.setClickGrouping (metronome::find (7, 8, "2+2+3"));
                    auto accents = audio.getClickAccents();
                    accents[5] = metronome::off;
                    audio.setClickAccents (accents);
                    auto o = audio.getOptions();
                    o.clickRhythm = 2;
                    o.metronome = true;
                    audio.setOptions (o);
                }

                metronomePanel = std::make_unique<MetronomePanel> (song, audio);
                metronomePanel->setVisible (true);
            }

            if (args.has ("--armrecord"))
                audio.setRecordArmed (true);

            // Paint once so lazily loaded things (waveforms) start, then let them and any
            // async layout settle before capturing
            (void) main.createComponentSnapshot (main.getLocalBounds(), true, 1.0f);

            for (int i = 0; i < 60; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (15);

            // A stock effect's panel on its own: --effectpanel nightshiftEQ (or Compressor, Reverb...)
            std::unique_ptr<juce::Component> effectPanel;
            te::Plugin::Ptr panelPlugin;

            if (auto type = args.valueAfter ("--effectpanel"); type.isNotEmpty())
            {
                panelPlugin = audio.getEdit().getPluginCache().createNewPlugin (type, {});

                if (auto fx = dynamic_cast<StockEffect*> (panelPlugin.get()))
                {
                    fx->initialise ({ te::TimePosition(), 48000.0, 512 });

                    // The NAM amp with a model in it (--nammodel <file.nam>, or "example": NAM's own wavenet) and a
                    // cabinet (--namcabinet <file.wav>)
                    if (auto amp = dynamic_cast<NightAmp*> (fx))
                    {
                        if (auto model = args.valueAfter ("--nammodel"); model.isNotEmpty())
                            amp->loadModel (model == "example" ? juce::File (NIGHTSHIFT_NAM_EXAMPLES).getChildFile ("wavenet.nam") : juce::File (model));

                        if (auto cab = args.valueAfter ("--namcabinet"); cab.isNotEmpty())
                            amp->loadCabinet (juce::File (cab));
                    }

                    effectPanel = createStockPanel (*fx, song, audio);
                }

                for (int i = 0; i < 10; ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (15);
            }

            // The unsaved-changes question closing asks (as it looks; nothing is answered)
            juce::Component* closeDialog = nullptr;

            if (args.has ("--closedialog"))
            {
                song.notify (Song::Change::transport);      // a change, so there's something unsaved
                main.confirmDiscard ([] {});
                closeDialog = juce::ModalComponentManager::getInstance()->getModalComponent (0);
            }

            // What the master's for (--mastertarget streaming), as clicking it in the Master tab does
            if (auto target = args.valueAfter ("--mastertarget"); target.isNotEmpty())
                main.getMaster().chooseTarget (target);

            // The Master tab's second page (--masterpage 1), with --shapeexample some tone, punch and width
            if (args.has ("--shapeexample"))
            {
                auto& m = song.mastering;
                m.toneDb = { 1.5f, -2.0f, 1.0f, 2.5f };
                m.toneWhere = { 0, 1, 0, 2 };
                m.punchAmount = 0.5f;
                m.width = 1.2f;
                m.monoBassHz = 120.0f;
                song.notify (Song::Change::mixer);
            }

            if (auto masterPage = args.valueAfter ("--masterpage"); masterPage.isNotEmpty())
                main.showMasterPage (masterPage.getIntValue());

            // The song played for a while first (--playfor seconds), so meters have something to show
            if (const double seconds = args.valueAfter ("--playfor").getDoubleValue(); seconds > 0.0 && ! args.has ("--live"))
            {
                audio.syncNow();
                audio.togglePlay();
                juce::AudioBuffer<float> buffer (2, 512);

                // In about real time, so the screen's timers see it as they would (a block is 10.7 ms)
                for (int b = 0; b < (int) (seconds * 48000.0 / 512.0); ++b)
                {
                    buffer.clear();
                    audio.processHostedBlock (buffer);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
                }

                for (int i = 0; i < 10; ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            }

            // The top bar as a keyboard plays (--midilight <its name>): the light lit, the selected channel its
            if (args.has ("--midilight"))
            {
                const auto* playing = song.findChannel (song.selectedChannelId);
                main.getToolbar().setMidi ({ args.valueAfter ("--midilight") }, playing != nullptr ? playing->name : juce::String());
                main.getToolbar().setMidiLit (true);
            }

            // About NightShift (with Donate...); --about 800 as on a screen 800 high (a laptop's)
            if (args.has ("--about"))
            {
                main.showAbout (args.valueAfter ("--about").getIntValue());
                closeDialog = juce::ModalComponentManager::getInstance()->getModalComponent (0);
            }

            // Check for updates' answers, as they look (nothing asked or downloaded): --updatefound a newer
            // release (a made-up 9.9.9 with two points of what's new), --uptodate this one the newest
            if (args.has ("--updatefound") || args.has ("--uptodate"))
            {
                updates::Release found;
                found.version = args.has ("--uptodate") ? updates::currentVersion() : juce::String ("9.9.9");
                found.title = "NightShift " + found.version;
                found.page = "https://github.com/" + updates::repository + "/releases";
                found.whatsNew = { "Check for updates, in the ... menu.", "Write a song with this vibe is always in sight." };
                found.installer = "NightShift-Setup-" + found.version + ".exe";
                found.installerUrl = found.page + "/download/v" + found.version + "/" + found.installer;
                found.installerBytes = 1;
                main.updateChecked (found, {});
                closeDialog = juce::ModalComponentManager::getInstance()->getModalComponent (0);
            }

            juce::Component& shown = closeDialog != nullptr ? *closeDialog
                                   : metronomePanel != nullptr ? static_cast<juce::Component&> (*metronomePanel)
                                   : effectPanel != nullptr ? *effectPanel
                                   : settings != nullptr ? static_cast<juce::Component&> (*settings)
                                                         : static_cast<juce::Component&> (main);
            juce::Image image;

            // --textaudit: the same picture, painted by the text check, which lists every piece of text that's
            // under 15 px (the small capitals: 14), squeezed, cut short or clipped (--alltext: every piece)
            if (args.has ("--textaudit"))
                std::cout << textaudit::report (textaudit::collect (shown, &image), args.has ("--alltext"));
            else
                image = shown.createComponentSnapshot (shown.getLocalBounds(), true, 1.0f);

            const auto out = juce::File (args.valueAfter ("--snapshot"));
            out.deleteFile();
            juce::FileOutputStream stream (out);
            const bool ok = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
            std::cout << (ok ? "snapshot " : "snapshot failed ") << out.getFullPathName() << std::endl;            return ok ? 0 : 1;
        }

        return -1;
    }
}

//==============================================================================
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (Song& song, AudioEngine& audio)
        : juce::DocumentWindow ("NightShift", theme::background, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;

        main = new MainComponent (song, audio);
        main->setSize (std::min (1680, area.getWidth() - 40), std::min (940, area.getHeight() - 80));
        setContentOwned (main, true);
        setResizable (true, false);
        setResizeLimits (MainComponent::minWidth, MainComponent::minHeight, 10000, 10000);
        centreWithSize (getWidth(), getHeight());

        main->onTitleChanged = [this] { setName (main->getWindowTitle()); };
        setName (main->getWindowTitle());
        setVisible (true);
        main->grabKeyboardFocus();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    MainComponent* main = nullptr;
};

//==============================================================================
class NightshiftApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }

    bool moreThanOneInstanceAllowed() override
    {
        // Command-line jobs (renders, snapshots, the plugin-scan worker) may run beside
        // the studio; a second plain launch hands over to the one already open
        if (isCommandLineJob (getCommandLineParameters()))
            return true;

        waitForOtherCopy();
        return false;
    }

    /** JUCE only listens for a hand-over once the first copy's initialise() has finished,
        and stops listening before its shutdown() runs, so a launch landing in either gap
        would be lost: this copy would quit and nothing would open. Hold on until the other
        copy has a window (it is listening) or has let go of the instance lock (it is gone). */
    void waitForOtherCopy()
    {
        juce::InterProcessLock probe ("juceAppLock_" + getApplicationName());   // JUCE's own name for it

        for (int waited = 0; waited < 6000; waited += 100)
        {
            if (probe.enter (0))          // free: this copy becomes the only one
            {
                probe.exit();
                return;
            }

            if (anotherCopyHasWindow())   // up: hand over to it
            {
                if (waited > 0)           // it was still starting; give it a moment to begin listening
                    juce::Thread::sleep (500);

                return;
            }

            juce::Thread::sleep (100);
        }
    }

   #if JUCE_WINDOWS
    /** True when a visible top-level window belongs to another process running this exe. */
    static bool anotherCopyHasWindow()
    {
        struct Scan { juce::String exe; DWORD self; bool found = false; } scan;
        scan.exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
        scan.self = GetCurrentProcessId();

        EnumWindows ([] (HWND hwnd, LPARAM param) -> BOOL
        {
            auto& s = *reinterpret_cast<Scan*> (param);
            DWORD pid = 0;
            GetWindowThreadProcessId (hwnd, &pid);

            if (pid == 0 || pid == s.self || ! IsWindowVisible (hwnd))
                return TRUE;

            if (auto process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
            {
                WCHAR path[MAX_PATH * 2];
                DWORD length = (DWORD) std::size (path);

                if (QueryFullProcessImageNameW (process, 0, path, &length) && juce::String (path).equalsIgnoreCase (s.exe))
                    s.found = true;

                CloseHandle (process);
            }

            return s.found ? FALSE : TRUE;
        }, reinterpret_cast<LPARAM> (&scan));

        return scan.found;
    }
   #else
    static bool anotherCopyHasWindow()    { return true; }
   #endif

    /** True when any argument is a --flag. Checked per token rather than at the start,
        because some launch paths put the exe's own path in front of the arguments. */
    static bool isCommandLineJob (const juce::String& commandLine)
    {
        for (auto& token : juce::StringArray::fromTokens (commandLine, true))
            if (token.startsWith ("--"))
                return true;

        return false;
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (window == nullptr)
            return;

        window->setMinimised (false);
        window->toFront (true);

        if (auto file = projectFileFrom (commandLine, false); file != juce::File())
            window->main->openProjectFile (file);
    }

    /** The .nsproj a launch (or an Explorer double-click) asked for, if any. A relative path
        is resolved against this process's working directory; a hand-over from a second
        instance can't know that directory, so it passes false and takes absolute paths only. */
    static juce::File projectFileFrom (const juce::String& commandLine, bool resolveRelative)
    {
        for (auto& token : juce::StringArray::fromTokens (commandLine, true))
        {
            const auto path = token.unquoted();

            if (! path.endsWithIgnoreCase (".nsproj"))
                continue;

            juce::File file;

            if (juce::File::isAbsolutePath (path))
                file = juce::File (path);
            else if (resolveRelative)
                file = juce::File::getCurrentWorkingDirectory().getChildFile (path);

            if (file.existsAsFile())
                return file;
        }

        return {};
    }

    void initialise (const juce::String& commandLine) override
    {
        if (te::PluginManager::startChildProcessPluginScan (commandLine))
            return;

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        if (isCommandLineJob (commandLine))
        {
            // Run once the message loop is up, as rendering pumps messages
            juce::MessageManager::callAsync ([this, commandLine]
            {
                setApplicationReturnValue (juce::jmax (0, runCommandLine (Args (commandLine))));
                quit();
            });
            return;
        }

        // A blank project, with "What do you want to do?" over it unless a project was opened
        // with NightShift (or the start screen is switched off)
        song.loadBlank();
        audio = std::make_unique<AudioEngine> (song, true);
        window = std::make_unique<MainWindow> (song, *audio);

        if (auto file = projectFileFrom (commandLine, true); file != juce::File())
            window->main->openProjectFile (file);
        else if (audio->getOptions().startScreen)
            window->main->showStartScreen();
    }

    void systemRequestedQuit() override
    {
        if (window == nullptr || window->main == nullptr)
        {
            quit();
            return;
        }

        window->main->confirmDiscard ([this] { quit(); });
    }

    void shutdown() override
    {
        window = nullptr;
        audio = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

private:
    NightLookAndFeel lookAndFeel;
    Song song;
    std::unique_ptr<AudioEngine> audio;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (NightshiftApplication)
