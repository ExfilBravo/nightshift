// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "DrumGizmo.h"
#include "Kits.h"
#include "ZipReader.h"
#include <set>

namespace nightshift::drumkits::drumgizmo
{

namespace
{
    enum class Mic { close, overhead, room, skip };

    struct Channel
    {
        juce::String in, out;
        bool main = false;
    };

    struct Instrument
    {
        juce::String name, file, group;
        std::vector<Channel> channels;
        bool hasMain = false;
    };

    struct AudioRef
    {
        juce::String channel, file;
        int fileChannel = 1;
    };

    struct Hit
    {
        float power = 0.0f;
        std::vector<AudioRef> audio;
        juce::String folder;                // where the instrument's own paths start, in the zip
        const Instrument* instrument = nullptr;
    };

    struct Mixed
    {
        juce::AudioBuffer<float> audio;
        float decibels = 0.0f;
    };

    /** The drums a mix puts in the middle. */
    bool centred (const juce::String& voice)
    {
        return voice == "kick" || voice == "snare" || voice == "sidestick" || voice == "snare_rim";
    }

    juce::String folderOf (const juce::String& path)
    {
        return path.containsChar ('/') ? path.upToLastOccurrenceOf ("/", false, false) : juce::String();
    }

    juce::String join (const juce::String& folder, juce::String relative)
    {
        relative = relative.replaceCharacter ('\\', '/');

        while (relative.startsWith ("./"))
            relative = relative.substring (2);

        return folder.isEmpty() ? relative : folder + "/" + relative;
    }

    std::unique_ptr<juce::XmlElement> readXml (const ZipReader& zip, const juce::String& path)
    {
        const int index = zip.indexOf (path);

        if (index < 0)
            return {};

        auto in = zip.open (index);
        return in != nullptr ? juce::parseXML (in->readEntireStreamAsString()) : nullptr;
    }

    /** A WAV inside the zip, copied out first (the reader wants to seek). */
    bool readAudio (const ZipReader& zip, const juce::String& path, const juce::File& scratch, juce::AudioBuffer<float>& audio, double& rate)
    {
        const int index = zip.indexOf (path);

        if (index < 0)
            return false;

        {
            auto in = zip.open (index);
            scratch.deleteFile();
            juce::FileOutputStream out (scratch);

            if (in == nullptr || ! out.openedOk() || out.writeFromInputStream (*in, -1) < 0)
                return false;
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (scratch.createInputStream().release(), true));

        if (reader == nullptr || reader->numChannels == 0)
            return false;

        rate = reader->sampleRate;
        audio.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
        return reader->read (audio.getArrayOfWritePointers(), audio.getNumChannels(), 0, audio.getNumSamples());
    }

    Mic classify (const juce::String& channel)
    {
        const auto n = channel.toLowerCase();

        if (n.contains ("trigger"))
            return Mic::skip;

        if (n.contains ("amb") || n.contains ("room"))
            return Mic::room;

        if (n.startsWith ("oh") || n.contains ("overhead"))
            return Mic::overhead;

        return Mic::close;
    }

    /** Where a room or overhead mic sits: -1 left, 1 right, 0 the middle. */
    float sideOf (const juce::String& channel)
    {
        const auto n = channel.toLowerCase().removeCharacters ("_- ");

        if (n.contains ("center") || n.contains ("centre") || n.contains ("mid"))
            return 0.0f;

        if (n.contains ("left") || n.endsWithChar ('l'))
            return -1.0f;

        if (n.contains ("right") || n.endsWithChar ('r'))
            return 1.0f;

        return 0.0f;
    }

    /** "Tom1", "FTom1", "Tom1_whisker" -> "tom1", "ftom1", "tom1": which tom a name is about. */
    juce::String tomOf (const juce::String& name)
    {
        const auto n = name.toLowerCase();
        const int at = n.indexOf ("tom");

        if (at < 0)
            return {};

        return (at > 0 && n[at - 1] == 'f' ? "f" : "") + juce::String ("tom") + n.substring (at + 3).initialSectionContainingOnly ("0123456789");
    }

    /** Whether a close mic is the drum's own (else it's bleed), for kits that don't say. */
    bool ownMic (const juce::String& instrument, const juce::String& channel)
    {
        const auto i = instrument.toLowerCase(), c = channel.toLowerCase();
        auto kick = [] (const juce::String& s) { return s.contains ("kdrum") || s.contains ("kick"); };

        if (kick (i))
            return kick (c);

        for (auto word : { "snare", "hihat", "ride" })
            if (i.contains (word))
                return c.contains (word);

        if (tomOf (i).isNotEmpty())
            return tomOf (i) == tomOf (c);

        return false;
    }

    double energy (const float* s, int from, int count)
    {
        double sum = 0.0;

        for (int i = from; i < from + count; ++i)
            sum += (double) s[i] * s[i];

        return sum;
    }

    /** Whether `other` is the opposite polarity to `reference` (a snare's bottom mic to its top,
        a kick's outside mic to its inside): the strongest correlation within 3 ms is negative. */
    bool flipped (const float* reference, const float* other, int length, double rate)
    {
        const int window = std::min (length, (int) (rate * 0.03)), maxLag = (int) (rate * 0.003);
        double strongest = 0.0;

        for (int lag = -maxLag; lag <= maxLag; ++lag)
        {
            double sum = 0.0;

            for (int i = std::max (0, -lag); i < window && i + lag < length; ++i)
                sum += (double) reference[i] * other[i + lag];

            if (std::abs (sum) > std::abs (strongest))
                strongest = sum;
        }

        return strongest < 0.0;
    }

    void panInto (juce::AudioBuffer<float>& out, const float* source, int length, float gain, float pan)
    {
        const float angle = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        out.addFrom (0, 0, source, std::min (length, out.getNumSamples()), gain * std::cos (angle));
        out.addFrom (1, 0, source, std::min (length, out.getNumSamples()), gain * std::sin (angle));
    }

    /** Cut the ring-out where it's 70 dB under the peak, with a short fade. */
    void trimTail (juce::AudioBuffer<float>& audio, double rate)
    {
        const float peak = audio.getMagnitude (0, audio.getNumSamples());

        if (peak <= 0.0f)
            return;

        const float floor = peak * juce::Decibels::decibelsToGain (-70.0f);
        int last = 0;

        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            for (int i = audio.getNumSamples() - 1; i > last; --i)
                if (std::abs (audio.getSample (ch, i)) > floor)
                {
                    last = i;
                    break;
                }

        const int fade = (int) (rate * 0.02);
        const int length = std::min (audio.getNumSamples(), last + fade);
        audio.setSize (audio.getNumChannels(), length, true);
        audio.applyGainRamp (std::max (0, length - fade), std::min (fade, length), 1.0f, 0.0f);
    }
}

//==============================================================================
juce::String convert (const juce::File& zipFile, const Recipe& recipe, const juce::File& destination,
                      std::function<void (float)> progress, std::function<bool()> shouldStop)
{
    ZipReader zip (zipFile);
    auto kitXml = readXml (zip, recipe.kitFile);

    if (kitXml == nullptr || ! kitXml->hasTagName ("drumkit"))
        return "The " + recipe.name + " download isn't the DrumGizmo kit it should be (no " + recipe.kitFile + " in it).";

    const auto kitFolder = folderOf (recipe.kitFile);

    // The kit's instruments and which of their mics are their own
    std::map<juce::String, Instrument> instruments;

    if (auto* list = kitXml->getChildByName ("instruments"))
        for (auto* e : list->getChildWithTagNameIterator ("instrument"))
        {
            Instrument in;
            in.name = e->getStringAttribute ("name");
            in.file = join (kitFolder, e->getStringAttribute ("file"));
            in.group = e->getStringAttribute ("group");

            for (auto* map : e->getChildWithTagNameIterator ("channelmap"))
            {
                in.channels.push_back ({ map->getStringAttribute ("in"), map->getStringAttribute ("out"), map->getBoolAttribute ("main") });
                in.hasMain = in.hasMain || map->getBoolAttribute ("main");
            }

            instruments[in.name] = std::move (in);
        }

    // Each drum's hits, from its instruments
    struct Drum
    {
        juce::String voice;
        std::vector<Hit> hits;
    };

    std::vector<Drum> drums;

    for (auto& [voice, names] : recipe.instruments)
    {
        Drum drum { voice, {} };

        for (auto& name : juce::StringArray::fromTokens (names, "|", ""))
        {
            auto it = instruments.find (name);

            if (it == instruments.end())
                return "The " + recipe.name + " kit has no instrument called " + name + ".";

            auto xml = readXml (zip, it->second.file);

            if (xml == nullptr)
                return "Couldn't read " + it->second.file + " in the " + recipe.name + " download.";

            if (auto* samples = xml->getChildByName ("samples"))
                for (auto* s : samples->getChildWithTagNameIterator ("sample"))
                {
                    Hit hit;
                    hit.power = (float) s->getDoubleAttribute ("power", (double) drum.hits.size() + 1.0);
                    hit.folder = folderOf (it->second.file);
                    hit.instrument = &it->second;

                    for (auto* a : s->getChildWithTagNameIterator ("audiofile"))
                        hit.audio.push_back ({ a->getStringAttribute ("channel"), a->getStringAttribute ("file"), a->getIntAttribute ("filechannel", 1) });

                    if (! hit.audio.empty())
                        drum.hits.push_back (std::move (hit));
                }
        }

        if (drum.hits.empty())
            return "The " + recipe.name + " kit's " + names + " has no hits.";

        // Softest to hardest, then up to maxHits of them spread across that range
        std::sort (drum.hits.begin(), drum.hits.end(), [] (const Hit& a, const Hit& b) { return a.power < b.power; });

        // (Cymbals ring for seconds: half as many of them, so a kit fits in memory)
        const bool cymbal = juce::StringArray { "crash", "crash_2", "china", "splash", "ride", "ride_bell", "hat_open" }.contains (voice);
        const int most = cymbal ? std::max (2, recipe.maxHits / 2) : recipe.maxHits;

        if ((int) drum.hits.size() > most)
        {
            std::vector<Hit> picked;

            for (int i = 0; i < most; ++i)
                picked.push_back (drum.hits[(size_t) juce::roundToInt ((double) i * (double) (drum.hits.size() - 1) / (most - 1))]);

            drum.hits = std::move (picked);
        }

        drums.push_back (std::move (drum));
    }

    int totalHits = 0, doneHits = 0;

    for (auto& d : drums)
        totalHits += (int) d.hits.size();

    const auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("Nightshift kit", ".wav");
    double rate = 48000.0;

    // A hit's mics, by name
    auto load = [&] (const Hit& hit, std::map<juce::String, std::pair<const float*, int>>& mics, std::map<juce::String, juce::AudioBuffer<float>>& files)
    {
        for (auto& a : hit.audio)
        {
            const auto path = join (hit.folder, a.file);

            if (files.count (path) == 0 && ! readAudio (zip, path, scratch, files[path], rate))
                return false;

            auto& audio = files[path];

            if (juce::isPositiveAndBelow (a.fileChannel - 1, audio.getNumChannels()))
                mics[a.channel] = { audio.getReadPointer (a.fileChannel - 1), audio.getNumSamples() };
        }

        return true;
    };

    auto isOwn = [] (const Instrument& in, const juce::String& channel)
    {
        if (in.hasMain)
            for (auto& c : in.channels)
                if (c.out == channel)
                    return c.main;

        return ownMic (in.name, channel);
    };

    // Polarity, from each drum with two mics of its own (a snare's top and bottom, a kick's inside
    // and outside), checked on its hardest hit; and where each drum sits between the overheads,
    // and how loud its own mics are against them, to place and balance it
    std::set<juce::String> flips;
    std::map<const Drum*, float> pans, closeGains;

    for (auto& drum : drums)
    {
        std::map<juce::String, std::pair<const float*, int>> mics;
        std::map<juce::String, juce::AudioBuffer<float>> files;

        if (! load (drum.hits.back(), mics, files))
            return "Couldn't read the " + recipe.name + " kit's " + drum.voice + " samples.";

        const auto* instrument = drum.hits.back().instrument;
        juce::StringArray own;
        double left = 0.0, right = 0.0, overheads = 0.0, closeEnergy = 0.0;

        for (auto& [name, mic] : mics)
        {
            const int window = std::min (mic.second, (int) (rate * 0.3));
            const auto kind = classify (name);

            if (kind == Mic::overhead)
            {
                const double e = energy (mic.first, 0, window);
                overheads += e;

                if (sideOf (name) < 0.0f)
                    left += e;
                else if (sideOf (name) > 0.0f)
                    right += e;
            }
            else if (kind == Mic::close && isOwn (*instrument, name))
            {
                own.add (name);
                closeEnergy += energy (mic.first, 0, window);
            }
        }

        // The loudest own mic is the reference; the others flipped if they're the other way round
        if (own.size() > 1)
        {
            juce::String reference;
            double most = -1.0;

            for (auto& name : own)
                if (const double e = energy (mics[name].first, 0, std::min (mics[name].second, (int) (rate * 0.05))); e > most)
                {
                    most = e;
                    reference = name;
                }

            for (auto& name : own)
                if (name != reference && flips.count (name) == 0
                     && flipped (mics[reference].first, mics[name].first, std::min (mics[reference].second, mics[name].second), rate))
                    flips.insert (name);
        }

        // Kick and snare in the middle, as a mix has them; the rest where the overheads put them
        pans[&drum] = centred (drum.voice) || left + right <= 0.0 ? 0.0f : juce::jlimit (-0.7f, 0.7f, (float) ((right - left) / (right + left)));

        // Close mics a few dB over the overheads for drums (their punch), level with them for
        // the hats and ride
        const auto v = drum.voice;
        const float wanted = v == "kick" ? 8.0f : (v.startsWith ("snare") || v.startsWith ("tom") || v == "sidestick") ? 5.0f
                           : (v.startsWith ("hat") || v.startsWith ("ride")) ? 0.0f : 0.0f;
        float gain = 1.0f;

        if (closeEnergy > 0.0 && overheads > 0.0)
        {
            const float now = (float) (10.0 * std::log10 (closeEnergy / overheads));
            gain = juce::Decibels::decibelsToGain (juce::jlimit (-9.0f, 9.0f, wanted - now));
        }

        closeGains[&drum] = gain;
    }

    // Mix every hit
    std::vector<std::vector<Mixed>> mixed (drums.size());
    std::vector<float> loudest (drums.size(), 0.0f);

    for (size_t d = 0; d < drums.size(); ++d)
    {
        auto& drum = drums[d];
        const float pan = pans[&drum], closeGain = closeGains[&drum];

        for (auto& hit : drum.hits)
        {
            if (shouldStop != nullptr && shouldStop())
            {
                scratch.deleteFile();
                return "Stopped.";
            }

            std::map<juce::String, std::pair<const float*, int>> mics;
            std::map<juce::String, juce::AudioBuffer<float>> files;

            if (! load (hit, mics, files))
            {
                scratch.deleteFile();
                return "Couldn't read the " + recipe.name + " kit's " + drum.voice + " samples.";
            }

            int length = 0;

            for (auto& [name, mic] : mics)
                length = std::max (length, mic.second);

            Mixed m;
            m.audio.setSize (2, length);
            m.audio.clear();
            m.decibels = 10.0f * std::log10 (std::max (hit.power, 1.0e-9f));

            for (auto& [name, mic] : mics)
            {
                const auto kind = classify (name);

                if (kind == Mic::skip)
                    continue;

                const float polarity = flips.count (name) > 0 ? -1.0f : 1.0f;

                if (kind == Mic::room)
                    panInto (m.audio, mic.first, mic.second, 0.7f * polarity, sideOf (name));
                else if (kind == Mic::overhead)
                    panInto (m.audio, mic.first, mic.second, polarity, sideOf (name));
                else
                    panInto (m.audio, mic.first, mic.second, (isOwn (*hit.instrument, name) ? closeGain : 0.5f) * polarity, pan);
            }

            trimTail (m.audio, rate);
            loudest[d] = std::max (loudest[d], m.audio.getMagnitude (0, m.audio.getNumSamples()));
            mixed[d].push_back (std::move (m));

            if (progress != nullptr)
                progress ((float) ++doneHits / (float) std::max (1, totalHits) * 0.95f);
        }

        // A drum that belongs in the middle is, however lopsided the overheads and room hear it:
        // its two sides evened out on its hardest hit (by at most 6 dB), the same for every hit
        if (centred (drum.voice) && ! mixed[d].empty())
        {
            const auto& hardest = mixed[d].back().audio;
            const int window = std::min (hardest.getNumSamples(), (int) (rate * 0.3));
            const float l = hardest.getRMSLevel (0, 0, window), r = hardest.getRMSLevel (1, 0, window);

            if (l > 0.0f && r > 0.0f)
            {
                const float tilt = juce::jlimit (0.5f, 2.0f, std::sqrt (r / l));
                loudest[d] = 0.0f;

                for (auto& m : mixed[d])
                {
                    m.audio.applyGain (0, 0, m.audio.getNumSamples(), tilt);
                    m.audio.applyGain (1, 0, m.audio.getNumSamples(), 1.0f / tilt);
                    loudest[d] = std::max (loudest[d], m.audio.getMagnitude (0, m.audio.getNumSamples()));
                }
            }
        }
    }

    scratch.deleteFile();

    // Into a folder of its own, then in place: each drum at its level (peakFor), the SFZ with
    // velocity layers from their loudness, three alternates to a layer
    const auto staging = destination.getSiblingFile (".converting");
    staging.deleteRecursively();
    staging.getChildFile ("samples").createDirectory();
    juce::String sfz;
    sfz << "// " << recipe.name << ": made by Nightshift from " << kitXml->getStringAttribute ("name") << " by DrumGizmo.org (CC BY 4.0)\n"
        << "// each hit's mics mixed to stereo; layers by loudness, alternates at random\n\n";

    juce::WavAudioFormat wav;

    for (size_t d = 0; d < drums.size(); ++d)
    {
        const auto& drum = drums[d];
        const auto [label, key] = pieceFor (drum.voice);
        const bool hat = drum.voice.startsWith ("hat_");

        sfz << "<group> key=" << key << " group_label=" << juce::String (label).replaceCharacter (' ', '_') << " amp_veltrack=30"
            << (hat ? " group=1 off_by=1" : "") << "\n";

        const auto& hits = mixed[d];
        const float scale = loudest[d] > 0.0f ? peakFor (drum.voice) / loudest[d] : 1.0f;
        const float low = hits.front().decibels, high = hits.back().decibels;
        auto velocityOf = [&] (float decibels) { return high > low ? 1.0f + 126.0f * (decibels - low) / (high - low) : 127.0f; };

        // Layers of up to three neighbouring hits, each layer spanning half way to the next
        const int perLayer = 3, layers = ((int) hits.size() + perLayer - 1) / perLayer;
        std::vector<float> centres;

        for (int l = 0; l < layers; ++l)
        {
            float sum = 0.0f;
            int count = 0;

            for (int i = l * perLayer; i < std::min ((int) hits.size(), (l + 1) * perLayer); ++i, ++count)
                sum += velocityOf (hits[(size_t) i].decibels);

            centres.push_back (sum / (float) count);
        }

        int previousHigh = 0;

        for (int l = 0; l < layers; ++l)
        {
            const int lowVel = std::min (127, previousHigh + 1);
            const int highVel = (l == layers - 1 || lowVel >= 126) ? 127
                                : juce::jlimit (lowVel, 126, juce::roundToInt ((centres[(size_t) l] + centres[(size_t) l + 1]) * 0.5f));
            previousHigh = highVel;
            const int first = l * perLayer, count = std::min ((int) hits.size(), first + perLayer) - first;

            for (int i = 0; i < count; ++i)
            {
                const auto name = drum.voice + "_" + juce::String (first + i + 1).paddedLeft ('0', 2) + ".wav";
                auto audio = hits[(size_t) (first + i)].audio;
                audio.applyGain (scale);

                const auto file = staging.getChildFile ("samples").getChildFile (name);
                std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
                std::unique_ptr<juce::AudioFormatWriter> writer (stream != nullptr ? wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0) : nullptr);

                if (writer == nullptr)
                {
                    staging.deleteRecursively();
                    return "Couldn't write the " + recipe.name + " kit into " + destination.getParentDirectory().getFullPathName() + ".";
                }

                stream.release();       // the writer owns it now
                writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
                writer.reset();

                sfz << "<region> lovel=" << lowVel << " hivel=" << highVel
                    << " lorand=" << juce::String ((float) i / (float) count, 3) << " hirand=" << juce::String ((float) (i + 1) / (float) count, 3)
                    << " sample=samples/" << name << "\n";
            }
        }

        sfz << "\n";
    }

    staging.getChildFile (recipe.id + ".sfz").replaceWithText (sfz);
    staging.getChildFile ("kit.txt").replaceWithText (recipe.name + "\n" + recipe.description + "\n"
                                                      + "Drum samples provided by DrumGizmo.org (" + kitXml->getStringAttribute ("name") + ", CC BY 4.0).\n");

    destination.deleteRecursively();

    if (! staging.moveFileTo (destination))
    {
        staging.deleteRecursively();
        return "Couldn't put the " + recipe.name + " kit in " + destination.getParentDirectory().getFullPathName() + ".";
    }

    if (progress != nullptr)
        progress (1.0f);

    return {};
}

} // namespace nightshift::drumkits::drumgizmo
