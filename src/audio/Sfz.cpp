// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Sfz.h"
#include "model/Paths.h"
#include <regex>
#include <mutex>
#include <set>
#include <thread>

namespace nightshift
{

namespace sfz
{

namespace
{
    using Opcodes = std::map<juce::String, juce::String>;

    /** #include files (anywhere in a line, paths from the main file's folder) and #define
        macros ($NAME), applied in order. */
    juce::String preprocess (const juce::File& file, const juce::File& root, std::vector<std::pair<juce::String, juce::String>>& defines, int depth)
    {
        juce::String out;

        for (auto line : juce::StringArray::fromLines (file.loadFileAsString()))
        {
            const auto trimmed = line.trim();

            if (trimmed.startsWith ("#define"))
            {
                const auto rest = trimmed.substring (7).trim();
                const auto name = rest.upToFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf ("\t", false, false);
                defines.push_back ({ name, rest.substring (name.length()).trim() });
                std::sort (defines.begin(), defines.end(), [] (auto& a, auto& b) { return a.first.length() > b.first.length(); });
                continue;
            }

            if (line.containsChar ('$'))
                for (auto& [name, value] : defines)
                    line = line.replace (name, value);

            // Includes, wherever they are in the line
            while (line.contains ("#include"))
            {
                const auto before = line.upToFirstOccurrenceOf ("#include", false, false);
                const auto after = line.fromFirstOccurrenceOf ("#include", false, false).trimStart();
                const auto name = after.fromFirstOccurrenceOf ("\"", false, false).upToFirstOccurrenceOf ("\"", false, false);
                const auto rest = after.fromFirstOccurrenceOf ("\"", false, false).fromFirstOccurrenceOf ("\"", false, false);
                const auto included = root.getChildFile (name.replaceCharacter ('\\', '/'));
                line = before + "\n" + (depth < 10 && included.existsAsFile() ? preprocess (included, root, defines, depth + 1) : juce::String()) + "\n" + rest;
            }

            out << line << "\n";
        }

        return out;
    }

    juce::String withoutComments (const juce::String& text)
    {
        juce::String out;
        bool block = false;

        for (auto line : juce::StringArray::fromLines (text))
        {
            if (block)
            {
                if (! line.contains ("*/"))
                    continue;

                line = line.fromFirstOccurrenceOf ("*/", false, false);
                block = false;
            }

            while (line.contains ("/*"))
            {
                const auto after = line.fromFirstOccurrenceOf ("/*", false, false);

                if (after.contains ("*/"))
                {
                    line = line.upToFirstOccurrenceOf ("/*", false, false) + " " + after.fromFirstOccurrenceOf ("*/", false, false);
                }
                else
                {
                    line = line.upToFirstOccurrenceOf ("/*", false, false);
                    block = true;
                }
            }

            out << line.upToFirstOccurrenceOf ("//", false, false) << "\n";
        }

        return out;
    }

    /** ARIA's built-in curves for CC modulation, and <curve> ones by index. */
    struct Curves
    {
        std::map<int, std::array<float, 128>> custom;

        float apply (int index, float x) const
        {
            switch (index)
            {
                case 0: return x;
                case 1: return 2.0f * x - 1.0f;
                case 2: return 1.0f - x;
                case 3: return 1.0f - 2.0f * x;
                case 4: return x * x;
                case 5: return std::sqrt (x);
                case 6: return std::sqrt (1.0f - x);
                default: break;
            }

            if (auto it = custom.find (index); it != custom.end())
                return it->second[(size_t) juce::jlimit (0, 127, juce::roundToInt (x * 127.0f))];

            return x;
        }
    };

    std::shared_ptr<const Sample> openSample (const juce::File& file)
    {
        static std::mutex mutex;
        static std::map<juce::String, std::weak_ptr<const Sample>> cache;
        const std::lock_guard<std::mutex> l (mutex);

        auto& slot = cache[file.getFullPathName()];

        if (auto existing = slot.lock())
            return existing;

        // A FLAC with a WAV of itself beside it (Nightshift converts them on install): the WAV
        auto source = file;

        if (! file.hasFileExtension ("wav;aif;aiff") && file.withFileExtension ("wav").existsAsFile())
            source = file.withFileExtension ("wav");

        auto sample = std::make_shared<Sample>();
        sample->file = source;
        std::unique_ptr<juce::MemoryMappedAudioFormatReader> mapped;

        if (source.hasFileExtension ("wav"))
            mapped.reset (juce::WavAudioFormat().createMemoryMappedReader (source));
        else if (source.hasFileExtension ("aif;aiff"))
            mapped.reset (juce::AiffAudioFormat().createMemoryMappedReader (source));

        juce::AudioFormatReader* reader = nullptr;
        std::unique_ptr<juce::AudioFormatReader> decoded;

        if (mapped != nullptr && mapped->mapEntireFile() && ! mapped->getMappedSection().isEmpty())
        {
            reader = mapped.get();
            sample->mapped = std::move (mapped);
        }
        else
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            decoded.reset (formats.createReaderFor (source));
            reader = decoded.get();
        }

        if (reader == nullptr)
            return nullptr;

        sample->sampleRate = reader->sampleRate;
        sample->length = reader->lengthInSamples;
        sample->channels = (int) reader->numChannels;

        if (const auto start = reader->metadataValues.getValue ("Loop0Start", {}); start.isNotEmpty())
        {
            sample->loopStart = start.getLargeIntValue();
            sample->loopEnd = reader->metadataValues.getValue ("Loop0End", {}).getLargeIntValue();
        }

        if (decoded != nullptr)
        {
            sample->loaded.setSize (std::min (2, sample->channels), (int) sample->length);
            reader->read (&sample->loaded, 0, (int) sample->length, 0, true, sample->channels > 1);
        }

        slot = sample;
        return sample;
    }
}

void Sample::read (juce::int64 start, int count, float* left, float* right) const
{
    std::fill (left, left + count, 0.0f);
    std::fill (right, right + count, 0.0f);

    const auto from = std::max<juce::int64> (0, start), to = std::min<juce::int64> (length, start + count);

    if (to <= from)
        return;

    const int offset = (int) (from - start), n = (int) (to - from);

    if (mapped != nullptr)
    {
        float* dest[2] = { left + offset, right + offset };
        mapped->read (dest, channels > 1 ? 2 : 1, from, n);

        if (channels == 1)
            std::copy (left + offset, left + offset + n, right + offset);
    }
    else if (loaded.getNumSamples() > 0)
    {
        std::copy (loaded.getReadPointer (0, (int) from), loaded.getReadPointer (0, (int) from) + n, left + offset);
        const int r = loaded.getNumChannels() > 1 ? 1 : 0;
        std::copy (loaded.getReadPointer (r, (int) from), loaded.getReadPointer (r, (int) from) + n, right + offset);
    }
}

int keyFromText (const juce::String& text)
{
    const auto t = text.trim();

    if (t.isEmpty())
        return -1;

    if (t.containsOnly ("-0123456789"))
        return juce::jlimit (-1, 127, t.getIntValue());

    static const int semis[] = { 9, 11, 0, 2, 4, 5, 7 };    // a b c d e f g
    const auto letter = juce::CharacterFunctions::toLowerCase (t[0]);

    if (letter < 'a' || letter > 'g')
        return -1;

    int key = semis[letter - 'a'];
    int i = 1;

    if (t[i] == '#')        { ++key; ++i; }
    else if (t[i] == 'b')   { --key; ++i; }

    // C4 is middle C, 60
    return juce::jlimit (-1, 127, 12 * (t.substring (i).getIntValue() + 1) + key);
}

std::shared_ptr<const Instrument> load (const juce::File& file)
{
    static std::mutex mutex;
    static std::map<juce::String, std::weak_ptr<const Instrument>> cache;

    {
        const std::lock_guard<std::mutex> l (mutex);

        if (auto existing = cache[file.getFullPathName()].lock())
            return existing;
    }

    auto inst = std::make_shared<Instrument>();
    inst->file = file;
    inst->name = file.getFileNameWithoutExtension();

    if (! file.existsAsFile())
    {
        inst->error = "There's no " + file.getFullPathName() + ".";
        return inst;
    }

    std::vector<std::pair<juce::String, juce::String>> defines;
    auto text = withoutComments (preprocess (file, file.getParentDirectory(), defines, 0));
    text = text.replace ("<", "\n<").replace (">", ">\n");

    // The CCs' starting values (ARIA's: volume 100, pan centre, expression full), then the file's
    std::map<int, float> cc { { 7, 100.0f / 127.0f }, { 10, 0.5f }, { 11, 1.0f } };
    Curves curves;
    Opcodes control, global, master, group, region, curve;
    juce::String header;
    std::vector<Opcodes> regions;
    bool inRegion = false;

    auto finishCurve = [&]
    {
        if (curve.empty())
            return;

        // Points given, straight lines between
        std::array<float, 128> points {};
        std::vector<std::pair<int, float>> given;

        for (auto& [k, v] : curve)
            if (k.startsWithChar ('v') && k.substring (1).containsOnly ("0123456789"))
                given.push_back ({ k.substring (1).getIntValue(), v.getFloatValue() });

        std::sort (given.begin(), given.end());

        for (int i = 0; i < 128; ++i)
        {
            if (given.empty())
                break;

            auto after = std::find_if (given.begin(), given.end(), [i] (auto& p) { return p.first >= i; });
            const auto& hi = after != given.end() ? *after : given.back();
            const auto& lo = after != given.begin() ? *(after - 1) : given.front();
            points[(size_t) i] = hi.first == lo.first ? hi.second : lo.second + (hi.second - lo.second) * (float) (i - lo.first) / (float) (hi.first - lo.first);
        }

        curves.custom[curve["curve_index"].getIntValue()] = points;
        curve.clear();
    };

    auto finishRegion = [&]
    {
        if (! inRegion)
            return;

        Opcodes merged = global;

        for (auto* level : { &master, &group, &region })
            for (auto& [k, v] : *level)
                merged[k] = v;

        // The sample folder in force where it's written (a file can change it between groups)
        merged["default_path"] = control["default_path"];

        regions.push_back (std::move (merged));
        region.clear();
        inRegion = false;
    };

    const std::regex opcode ("([A-Za-z0-9_]+)=");

    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.trim();

        if (line.isEmpty())
            continue;

        if (line.startsWithChar ('<'))
        {
            finishRegion();

            if (header == "curve")
                finishCurve();

            header = line.substring (1).upToFirstOccurrenceOf (">", false, false).trim();

            if (header == "global")         { global.clear(); master.clear(); group.clear(); }
            else if (header == "master")    { master.clear(); group.clear(); }
            else if (header == "group")     { group.clear(); }
            else if (header == "region")    { inRegion = true; }

            continue;
        }

        // name=value pairs; a value runs up to the next name= (sample paths have spaces)
        const auto s = line.toStdString();
        std::vector<std::pair<size_t, size_t>> found;     // name start, value start

        for (auto it = std::sregex_iterator (s.begin(), s.end(), opcode); it != std::sregex_iterator(); ++it)
            if (it->position (0) == 0 || std::isspace ((unsigned char) s[(size_t) it->position (0) - 1]))
                found.push_back ({ (size_t) it->position (1), (size_t) (it->position (0) + it->length (0)) });

        for (size_t i = 0; i < found.size(); ++i)
        {
            const auto name = juce::String (s.substr (found[i].first, found[i].second - 1 - found[i].first));
            const auto end = i + 1 < found.size() ? found[i + 1].first : s.size();
            const auto value = juce::String (s.substr (found[i].second, end - found[i].second)).trim();

            if (header == "control")        control[name] = value;
            else if (header == "global")    global[name] = value;
            else if (header == "master")    master[name] = value;
            else if (header == "group")     group[name] = value;
            else if (header == "region")    region[name] = value;
            else if (header == "curve")     curve[name] = value;
        }
    }

    finishRegion();

    if (header == "curve")
        finishCurve();

    for (auto& [k, v] : control)
    {
        if (k.startsWith ("set_hdcc"))      cc[k.substring (8).getIntValue()] = juce::jlimit (0.0f, 1.0f, v.getFloatValue());
        else if (k.startsWith ("set_cc"))   cc[k.substring (6).getIntValue()] = juce::jlimit (0.0f, 1.0f, v.getFloatValue() / 127.0f);
    }

    const auto folder = file.getParentDirectory();
    juce::StringArray missing;

    for (auto& o : regions)
    {
        auto get = [&o] (const char* name, const juce::String& fallback = {}) { auto it = o.find (name); return it != o.end() ? it->second : fallback; };
        auto number = [&] (const char* name, float fallback) { auto v = get (name); return v.isNotEmpty() ? v.getFloatValue() : fallback; };

        // What the CCs add at their starting values: name_onccN times its curve (name_curveccN)
        auto onCC = [&] (const juce::String& name, std::function<void (float)> each)
        {
            const auto prefix = name + "_oncc";

            for (auto& [k, v] : o)
                if (k.startsWith (prefix))
                {
                    const int n = k.substring (prefix.length()).getIntValue();
                    const int curve = get ((name + "_curvecc" + juce::String (n)).toRawUTF8()).getIntValue();
                    each (v.getFloatValue() * curves.apply (curve, cc.count (n) ? cc[n] : 0.0f));
                }
        };

        // Regions only for CC values other than the starting ones are left out
        bool playable = true;

        for (auto& [k, v] : o)
            if ((k.startsWith ("locc") || k.startsWith ("hicc")) && k.substring (4).containsOnly ("0123456789"))
            {
                const int n = k.substring (4).getIntValue();
                const float value = (cc.count (n) ? cc[n] : 0.0f) * 127.0f;
                playable = playable && (k.startsWith ("locc") ? value >= v.getFloatValue() : value <= v.getFloatValue());
            }

        const auto sampleName = get ("sample");

        if (! playable || sampleName.isEmpty() || sampleName.startsWithChar ('*'))
            continue;

        Region r;
        const auto path = (get ("default_path") + sampleName).replaceCharacter ('\\', '/');
        r.sample = openSample (folder.getChildFile (path));

        if (r.sample == nullptr)
        {
            missing.addIfNotAlreadyThere (path);
            continue;
        }

        if (const int key = keyFromText (get ("key")); key >= 0)
            r.loKey = r.hiKey = r.keyCenter = key;

        if (const int k = keyFromText (get ("lokey")); k >= 0)            r.loKey = k;
        if (const int k = keyFromText (get ("hikey")); k >= 0)            r.hiKey = k;
        if (const int k = keyFromText (get ("pitch_keycenter")); k >= 0)  r.keyCenter = k;

        r.loVel = (int) number ("lovel", 1.0f);
        r.hiVel = (int) number ("hivel", 127.0f);
        r.loRand = number ("lorand", 0.0f);
        r.hiRand = number ("hirand", 1.0f);
        r.seqLength = std::max (1, (int) number ("seq_length", 1.0f));
        r.seqPosition = std::max (1, (int) number ("seq_position", 1.0f));
        r.swLast = keyFromText (get ("sw_last"));
        r.release = get ("trigger") == "release" || get ("trigger") == "release_key";
        r.rtDecay = number ("rt_decay", 0.0f);

        float tune = number ("tune", 0.0f) + 100.0f * number ("transpose", 0.0f);
        onCC ("tune", [&tune] (float v) { tune += v; });
        r.tuneCents = tune;
        r.keytrack = number ("pitch_keytrack", 100.0f);

        float volume = number ("volume", 0.0f), amplitude = number ("amplitude", 100.0f) / 100.0f, pan = number ("pan", 0.0f);
        float velTrack = number ("amp_veltrack", 100.0f);
        onCC ("volume", [&volume] (float v) { volume += v; });
        onCC ("amplitude", [&amplitude] (float v) { amplitude *= v / 100.0f; });
        onCC ("pan", [&pan] (float v) { pan += v; });
        onCC ("amp_veltrack", [&velTrack] (float v) { velTrack += v; });
        r.gain = juce::Decibels::decibelsToGain (volume) * std::max (0.0f, amplitude);
        r.pan = juce::jlimit (-1.0f, 1.0f, pan / 100.0f);
        r.velTrack = juce::jlimit (0.0f, 1.0f, velTrack / 100.0f);

        auto envelope = [&] (const char* name, float fallback)
        {
            float value = number ((juce::String ("ampeg_") + name).toRawUTF8(), fallback);
            onCC (juce::String ("ampeg_") + name, [&value] (float v) { value += v; });
            return std::max (0.0f, value);
        };

        r.attack = std::max (0.001f, envelope ("attack", 0.0f));
        r.hold = envelope ("hold", 0.0f);
        r.decay = envelope ("decay", 0.0f);
        r.sustain = juce::jlimit (0.0f, 1.0f, envelope ("sustain", 100.0f) / 100.0f);
        r.releaseTime = std::max (0.005f, envelope ("release", 0.03f));

        r.group = (int) number ("group", 0.0f);
        r.offBy = (int) number ("off_by", 0.0f);
        r.offTime = get ("off_mode") == "normal" ? r.releaseTime : std::max (0.002f, number ("off_time", 0.006f));
        r.notePolyphony = (int) number ("note_polyphony", 0.0f);
        r.offset = (juce::int64) number ("offset", 0.0f);

        // Loops: as asked, else the sample's own
        const auto mode = get ("loop_mode", get ("loopmode"));
        r.loopStart = (juce::int64) number ("loop_start", number ("loopstart", (float) r.sample->loopStart));
        r.loopEnd = (juce::int64) number ("loop_end", number ("loopend", (float) r.sample->loopEnd));

        if (mode == "one_shot")                 r.loop = Loop::oneShot;
        else if (mode == "loop_continuous")     r.loop = Loop::continuous;
        else if (mode == "loop_sustain")        r.loop = Loop::sustain;
        else if (mode.isEmpty() && r.sample->loopEnd > r.sample->loopStart && r.sample->loopStart >= 0)
                                                r.loop = Loop::continuous;

        if (r.loop == Loop::continuous || r.loop == Loop::sustain)
            if (r.loopEnd <= r.loopStart || r.loopStart < 0)
                r.loop = Loop::none;

        // Keyswitches: the range, the starting one, and what each is called
        if (const int k = keyFromText (get ("sw_lokey")); k >= 0)       inst->swLoKey = k;
        if (const int k = keyFromText (get ("sw_hikey")); k >= 0)       inst->swHiKey = k;
        if (const int k = keyFromText (get ("sw_default")); k >= 0)     inst->swDefault = k;

        if (r.swLast >= 0 && get ("sw_label").isNotEmpty())
            inst->switchLabels[r.swLast] = get ("sw_label");

        if (! r.release)
        {
            inst->loKey = std::min (inst->loKey, r.loKey);
            inst->hiKey = std::max (inst->hiKey, r.hiKey);
        }

        inst->regions.push_back (std::move (r));
    }

    inst->missing = missing;

    if (inst->swLoKey >= 0 && inst->swHiKey < inst->swLoKey)
        inst->swHiKey = inst->swLoKey;

    if (inst->regions.empty())
        inst->error = missing.isEmpty() ? "There's nothing to play in " + file.getFileName() + "."
                                        : "The samples aren't there (" + missing[0] + (missing.size() > 1 ? " and " + juce::String (missing.size() - 1) + " more" : juce::String()) + ").";

    // The starts of its samples read in ahead, so the first notes don't wait on the disk
    std::thread ([keep = inst]
    {
        std::set<const Sample*> seen;

        for (auto& r : keep->regions)
            if (r.sample->mapped != nullptr && seen.insert (r.sample.get()).second)
                for (juce::int64 i = 0; i < std::min<juce::int64> (r.sample->length, (juce::int64) (r.sample->sampleRate * 0.4)); i += 512)
                    r.sample->mapped->touchSample (i);
    }).detach();

    const std::lock_guard<std::mutex> l (mutex);
    cache[file.getFullPathName()] = inst;
    return inst;
}

} // namespace sfz

//==============================================================================
const char* SfzPlugin::xmlTypeName = "nightshiftSfz";

SfzPlugin::SfzPlugin (tracktion::PluginCreationInfo info)
    : tracktion::Plugin (info)
{
    instrumentPath.referTo (state, juce::Identifier ("instrument"), nullptr);
    scratch.setSize (2, chunk * 8 + 16);

    if (instrumentPath.get().isNotEmpty())
        instrument = sfz::load (fileFor (instrumentPath.get()));

    keyswitch = instrument != nullptr ? instrument->swDefault : -1;
}

SfzPlugin::~SfzPlugin()
{
    notifyListenersOfDeletion();
}

juce::File SfzPlugin::folder()
{
    if (folderOverride != juce::File())
        return folderOverride;

    return paths::localData().getChildFile ("Instruments");
}

juce::File SfzPlugin::fileFor (const juce::String& path)
{
    return juce::File::isAbsolutePath (path) ? juce::File (path) : folder().getChildFile (path);
}

void SfzPlugin::setInstrument (const juce::String& path)
{
    if (instrumentPath.get() != path)
        instrumentPath = path;      // loads it through valueTreePropertyChanged
}

std::shared_ptr<const sfz::Instrument> SfzPlugin::getInstrument() const
{
    const juce::ScopedLock sl (lock);
    return instrument;
}

void SfzPlugin::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id)
{
    if (v == state && id == juce::Identifier ("instrument"))
    {
        auto loaded = sfz::load (fileFor (instrumentPath.get()));
        const juce::ScopedLock sl (lock);

        for (auto& voice : voices)
            voice.active = false;

        instrument = std::move (loaded);
        keyswitch = instrument->swDefault;
    }

    tracktion::Plugin::valueTreePropertyChanged (v, id);
}

void SfzPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    tracktion::copyPropertiesToCachedValues (v, instrumentPath);
}

bool SfzPlugin::hasNameForMidiNoteNumber (int note, int, juce::String& name)
{
    if (auto inst = getInstrument(); inst != nullptr && inst->isSwitch (note))
    {
        auto it = inst->switchLabels.find (note);
        name = it != inst->switchLabels.end() ? it->second : "Switch";
        return true;
    }

    return false;
}

void SfzPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;
    reset();
}

void SfzPlugin::reset()
{
    const juce::ScopedLock sl (lock);

    for (auto& v : voices)
        v.active = false;

    pedal = false;
}

//==============================================================================
void SfzPlugin::trigger (int note, int velocity, bool release, double heldFor, int delay)
{
    if (instrument == nullptr)
        return;

    const float roll = random.nextFloat();
    const int seq = seqCounter[(size_t) note]++;

    for (auto& r : instrument->regions)
    {
        if (r.release != release || note < r.loKey || note > r.hiKey || velocity < r.loVel || velocity > r.hiVel
             || roll < r.loRand || roll >= r.hiRand || (seq % r.seqLength) + 1 != r.seqPosition
             || (r.swLast >= 0 && r.swLast != keyswitch))
            continue;

        // A choke group: what this one cuts off goes quickly
        if (r.group != 0)
            for (auto& v : voices)
                if (v.active && v.region->offBy == r.group && v.stage < 5)
                {
                    v.stage = 5;
                    v.releaseFactor = (float) std::exp (std::log (0.001) / (v.region->offTime * sampleRate));
                }

        // One voice a note where it asks (a piano's string): the last one fades
        if (r.notePolyphony > 0 && ! release)
            for (auto& v : voices)
                if (v.active && v.note == note && ! v.region->release && v.stage < 5)
                {
                    v.stage = 5;
                    v.releaseFactor = (float) std::exp (std::log (0.001) / (std::max (0.002f, r.offTime) * sampleRate));
                }

        // A free voice, else the quietest of the released, else the oldest
        Voice* slot = nullptr;

        for (auto& v : voices)
            if (! v.active)
            {
                slot = &v;
                break;
            }

        if (slot == nullptr)
            for (auto& v : voices)
                if (slot == nullptr || (v.stage == 5 && (slot->stage != 5 || v.level < slot->level)) || (slot->stage != 5 && v.age < slot->age))
                    slot = &v;

        auto& v = *slot;
        v = Voice();
        v.active = true;
        v.region = &r;
        v.note = note;
        v.velocity = velocity;
        v.age = counter++;
        v.delay = delay;
        v.position = (double) r.offset;

        const double semitones = (note - r.keyCenter) * r.keytrack / 100.0 + r.tuneCents / 100.0;
        v.step = std::pow (2.0, semitones / 12.0) * r.sample->sampleRate / sampleRate;

        const float velocityCurve = (float) (velocity * velocity) / (127.0f * 127.0f);
        float gain = r.gain * (1.0f - r.velTrack * (1.0f - velocityCurve));

        if (release && r.rtDecay > 0.0f)
            gain *= juce::Decibels::decibelsToGain (-r.rtDecay * (float) heldFor);

        const float angle = (r.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        v.left = gain * std::cos (angle) * juce::MathConstants<float>::sqrt2;
        v.right = gain * std::sin (angle) * juce::MathConstants<float>::sqrt2;
        v.keyDown = ! release;
    }
}

void SfzPlugin::releaseVoice (Voice& v)
{
    if (v.region->loop == sfz::Loop::oneShot || v.stage == 5)
        return;

    v.stage = 5;
    v.releaseFactor = (float) std::exp (std::log (0.001) / (v.region->releaseTime * sampleRate));
}

void SfzPlugin::noteOn (int note, int velocity, int delay)
{
    if (instrument == nullptr)
        return;

    // A keyswitch: the articulation from here on
    if (instrument->isSwitch (note))
    {
        keyswitch = note;
        return;
    }

    noteVelocity[(size_t) note] = velocity;
    noteOnTime[(size_t) note] = clock + delay / sampleRate;
    trigger (note, velocity, false, 0.0, delay);
}

void SfzPlugin::noteOff (int note, int delay)
{
    if (instrument == nullptr || instrument->isSwitch (note))
        return;

    bool any = false;

    for (auto& v : voices)
        if (v.active && v.note == note && v.keyDown && ! v.region->release)
        {
            v.keyDown = false;
            any = true;

            if (pedal)
                v.held = true;
            else
                releaseVoice (v);
        }

    // Release samples (the damper, the strings settling) unless the pedal holds the note
    if (any && ! pedal)
        trigger (note, std::max (1, noteVelocity[(size_t) note]), true, clock + delay / sampleRate - noteOnTime[(size_t) note], delay);
}

void SfzPlugin::pedalUp (int delay)
{
    std::set<int> notes;

    for (auto& v : voices)
        if (v.active && v.held)
        {
            v.held = false;
            releaseVoice (v);
            notes.insert (v.note);
        }

    for (auto note : notes)
        trigger (note, std::max (1, noteVelocity[(size_t) note]), true, clock + delay / sampleRate - noteOnTime[(size_t) note], delay);
}

void SfzPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.bufferNumSamples == 0)
        return;

    auto& buffer = *fc.destBuffer;
    const int numSamples = fc.bufferNumSamples;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, fc.bufferStartSample, numSamples);

    const juce::ScopedLock sl (lock);

    if (fc.bufferForMidiMessages != nullptr)
    {
        if (fc.bufferForMidiMessages->isAllNotesOff)
        {
            for (auto& v : voices)
                if (v.active)
                {
                    v.stage = 5;
                    v.releaseFactor = (float) std::exp (std::log (0.001) / (0.02 * sampleRate));
                }

            pedal = false;
        }

        for (auto& m : *fc.bufferForMidiMessages)
        {
            const int at = juce::jlimit (0, numSamples - 1, juce::roundToInt (m.getTimeStamp() * sampleRate));

            if (m.isNoteOn())
                noteOn (m.getNoteNumber(), m.getVelocity(), at);
            else if (m.isNoteOff())
                noteOff (m.getNoteNumber(), at);
            else if (m.isSustainPedalOn())
                pedal = true;
            else if (m.isSustainPedalOff())
            {
                pedal = false;
                pedalUp (at);
            }
        }
    }

    float* left = buffer.getWritePointer (0, fc.bufferStartSample);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, fc.bufferStartSample) : left;

    for (int done = 0; done < numSamples; done += chunk)
        render (left + done, right + done, std::min (chunk, numSamples - done));

    clock += numSamples / sampleRate;
}

void SfzPlugin::render (float* left, float* right, int numSamples)
{
    float* srcL = scratch.getWritePointer (0);
    float* srcR = scratch.getWritePointer (1);

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const auto& r = *v.region;
        const auto& sample = *r.sample;
        int i = std::min (v.delay, numSamples);
        v.delay = std::max (0, v.delay - numSamples);

        while (i < numSamples && v.active)
        {
            // Up to the loop's end (while it loops) or the chunk's, in one read from the sample
            const bool looping = (r.loop == sfz::Loop::continuous || (r.loop == sfz::Loop::sustain && v.stage < 5));
            int n = numSamples - i;

            if (looping)
                n = std::max (1, std::min (n, (int) std::ceil ((double) (r.loopEnd - v.position) / v.step)));

            const auto first = (juce::int64) std::floor (v.position) - 1;
            const int frames = std::min (scratch.getNumSamples(), (int) std::ceil (n * v.step) + 4);
            n = std::min (n, (int) ((frames - 4) / v.step));

            if (n <= 0)
                n = 1;

            sample.read (first, frames, srcL, srcR);

            for (int k = 0; k < n; ++k, ++i)
            {
                const double at = v.position - (double) first;
                const int index = (int) at;
                const float frac = (float) (at - index);

                if (index + 2 >= frames)
                    break;

                auto hermite = [index, frac] (const float* s)
                {
                    const float y0 = s[index - 1], y1 = s[index], y2 = s[index + 1], y3 = s[index + 2];
                    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                    return ((c3 * frac + c2) * frac + c1) * frac + y1;
                };

                // The envelope: attack, hold, decay to the sustain level, release
                const double dt = 1.0 / sampleRate;

                switch (v.stage)
                {
                    case 0:
                        v.level = std::min (1.0f, (float) (v.stageTime / r.attack));
                        if ((v.stageTime += dt) >= r.attack) { v.stage = r.hold > 0.0f ? 2 : 3; v.stageTime = 0.0; v.level = 1.0f; }
                        break;
                    case 2:
                        v.level = 1.0f;
                        if ((v.stageTime += dt) >= r.hold) { v.stage = 3; v.stageTime = 0.0; }
                        break;
                    case 3:
                        if (r.decay <= 0.0f) { v.level = r.sustain; v.stage = 4; break; }
                        v.level = r.sustain + (1.0f - r.sustain) * (float) std::exp (-5.0 * v.stageTime / r.decay);
                        if ((v.stageTime += dt) >= r.decay) { v.stage = 4; v.level = r.sustain; }
                        break;
                    case 5:
                        v.level *= v.releaseFactor;
                        break;
                    default:
                        break;
                }

                left[i] += hermite (srcL) * v.left * v.level;
                right[i] += hermite (srcR) * v.right * v.level;
                v.position += v.step;
            }

            // Round the loop, or the end of the sample (or the release) ends it
            if (looping && v.position >= (double) r.loopEnd)
                v.position -= (double) (r.loopEnd - r.loopStart);

            if (v.position >= (double) sample.length || (v.stage == 5 && v.level < 0.0001f) || (v.stage == 4 && r.sustain <= 0.0001f))
                v.active = false;
        }
    }
}

} // namespace nightshift
