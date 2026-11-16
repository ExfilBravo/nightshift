// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Kits.h"
#include "model/Paths.h"

namespace nightshift
{

namespace drumkits
{

const Piece* Kit::findPiece (int key) const
{
    auto it = pieces.find (key);
    return it != pieces.end() ? &it->second : nullptr;
}

juce::File folder()
{
    if (folderOverride != juce::File())
        return folderOverride;

    return paths::appData().getChildFile ("Kits");
}

std::vector<Download>& downloads()
{
    using drumgizmo::Recipe;

    // Our drums from each kit's instruments, by its General MIDI map
    static std::vector<Download> all {
        { "CrocellKit", "https://drumgizmo.org/kits/CrocellKit/CrocellKit1_1.zip", 5385, { "CROCELL_KIT" },
          { Recipe { "CROCELL_KIT", "Crocell",
                     "a metal kit from a Danish metal band's album, double kick, four toms, three crashes, two chinas, two splashes, close mics, "
                     "overheads and room: metalcore, metal, deathcore, djent, hard rock, punk",
                     "CrocellKit/CrocellKit_full.xml",
                     { { "kick", "KDrumR|KDrumL" }, { "snare", "Snare" }, { "sidestick", "SnareRim" }, { "snare_rim", "SnareRimShot" },
                       { "hat_closed", "HihatClosed" }, { "hat_pedal", "HihatPedal" }, { "hat_open", "HihatOpen" },
                       { "tom_high", "Tom1" }, { "tom_mid", "Tom2" }, { "tom_floor", "FTom2" }, { "ride", "RideR" }, { "ride_bell", "RideRBell" },
                       { "crash", "CrashL" }, { "crash_2", "CrashR" }, { "china", "ChinaR" }, { "splash", "SplashL" } } } } },
        { "MuldjordKit", "https://drumgizmo.org/kits/MuldjordKit/MuldjordKit3.zip", 2420, { "MULDJORD_KIT" },
          { Recipe { "MULDJORD_KIT", "Muldjord",
                     "a Tama Superstar recorded for a metal album, big and roomy, close mics, overheads and room: heavy rock, metal, doom, "
                     "stoner, grunge, classic rock",
                     "MuldjordKit3/MuldjordKit3.xml",
                     { { "kick", "KdrumR|KdrumL" }, { "snare", "Snare" }, { "sidestick", "SnareRest" }, { "hat_closed", "HihatClosed" },
                       { "hat_open", "HihatOpen" }, { "tom_high", "Tom1" }, { "tom_mid", "Tom2" }, { "tom_floor", "Tom4" },
                       { "ride", "RideR" }, { "ride_bell", "RideRBell" }, { "crash", "CrashL" }, { "crash_2", "CrashR" }, { "china", "China" } } } } },
        { "DRSKit", "https://drumgizmo.org/kits/DRSKit/DRSKit2_1.zip", 2674, { "DRS_KIT", "DRS_BRUSHES" },
          { Recipe { "DRS_KIT", "DRS",
                     "a handcrafted kit with Paiste cymbals, warm and open, close mics, overheads and room: jazz, funk, soul, R&B, pop, indie, "
                     "lofi, gospel, rock",
                     "DRSKit/DRSKit_full.xml",
                     { { "kick", "Kdrum_with_contact" }, { "snare", "Snare" }, { "sidestick", "Snare_rim" }, { "hat_closed", "Hihat_closed" },
                       { "hat_pedal", "Hihat_foot" }, { "hat_open", "Hihat_open" }, { "tom_high", "Tom1" }, { "tom_mid", "Tom2" },
                       { "tom_floor", "Tom3" }, { "ride", "Ride_tip" }, { "ride_bell", "Ride_tip_bell" }, { "crash", "Crash_left_shank" },
                       { "crash_2", "Crash_right_shank" } } },
            Recipe { "DRS_BRUSHES", "DRS Brushes",
                     "the DRS kit played with brushes (snare, hats, toms and cymbals): jazz ballads, lofi, bossa nova, folk, unplugged",
                     "DRSKit/DRSKit_full.xml",
                     { { "kick", "Kdrum_with_contact" }, { "snare", "Snare_whisker" }, { "sidestick", "Snare_rim" },
                       { "hat_closed", "Hihat_closed_whisker" }, { "hat_pedal", "Hihat_foot" }, { "hat_open", "Hihat_open_whisker" },
                       { "tom_high", "Tom1_whisker" }, { "tom_mid", "Tom2_whisker" }, { "tom_floor", "Tom3_whisker" }, { "ride", "Ride_whisker" },
                       { "crash", "Crash_left_whisker" }, { "crash_2", "Crash_right_whisker" } } } } } };
    return all;
}

bool haveDownloads()
{
    const auto have = ids();

    for (auto& d : downloads())
        for (auto& kit : d.kits)
            if (! have.contains (kit))
                return false;

    return true;
}

juce::File downloadsFolder()
{
    if (folderOverride != juce::File())
        return folderOverride.getChildFile (".downloading");

    return paths::localData().getChildFile ("Downloads");
}

juce::String unpack (const juce::File& zipFile, const juce::String& id)
{
    juce::ZipFile zip (zipFile);

    if (zip.getNumEntries() == 0)
        return "The " + displayName (id) + " download isn't a kit (the zip is empty or broken). Try again.";

    // Out into a folder of its own first, so a kit is never found half there
    const auto staging = folder().getChildFile (".unpacking");
    staging.deleteRecursively();
    const auto kitFolder = staging.getChildFile (id);
    kitFolder.createDirectory();
    int sfzFiles = 0;

    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto name = zip.getEntry (i)->filename.replaceCharacter ('\\', '/');
        const auto inner = name.fromFirstOccurrenceOf (id + "/", false, false);
        const auto type = inner.fromLastOccurrenceOf (".", false, false).toLowerCase();

        // Only the kit's own samples and SFZ, straight inside its folder
        if (! name.startsWith (id + "/") || inner.isEmpty() || inner.containsAnyOf ("/:") || inner.contains ("..")
             || ! (type == "wav" || type == "sfz" || type == "txt"))
            continue;

        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
        juce::FileOutputStream out (kitFolder.getChildFile (inner));

        const bool written = in != nullptr && out.openedOk() && out.writeFromInputStream (*in, -1) >= 0;
        out.flush();

        if (! written || out.getStatus().failed())
        {
            staging.deleteRecursively();
            return "Couldn't unpack " + displayName (id) + " into " + folder().getFullPathName() + ".";
        }

        sfzFiles += type == "sfz" ? 1 : 0;
    }

    if (sfzFiles == 0)
    {
        staging.deleteRecursively();
        return "The " + displayName (id) + " download has no kit in it.";
    }

    const auto destination = folder().getChildFile (id);
    destination.deleteRecursively();

    if (! kitFolder.moveFileTo (destination))
    {
        staging.deleteRecursively();
        return "Couldn't put " + displayName (id) + " in " + folder().getFullPathName() + ".";
    }

    staging.deleteRecursively();
    return {};
}

namespace
{
    /** Downloads `url` to `target`, carrying on from a part already there when the server can,
        reporting the fraction done. Returns what went wrong, or "". */
    juce::String download (const juce::String& url, const juce::File& target, const juce::String& name,
                           std::function<void (double)> progress, std::function<bool()> shouldStop, juce::int64 expected = -1)
    {
        if (target.existsAsFile())
            return {};

        const auto part = target.withFileExtension (target.getFileExtension() + ".part");
        const juce::URL source (url);
        const bool local = source.isLocalFile();
        juce::int64 have = local ? 0 : std::max<juce::int64> (0, part.getSize());

        if (local)
            part.deleteFile();

        int status = 0;
        juce::StringPairArray headers;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withConnectionTimeoutMs (20000)
                           .withNumRedirectsToFollow (5)
                           .withStatusCode (&status)
                           .withResponseHeaders (&headers)
                           .withExtraHeaders (have > 0 ? "Range: bytes=" + juce::String (have) + "-" : juce::String());

        auto in = source.createInputStream (options);

        // All of it already there (cut short just as it finished): nothing left to carry on from
        if (have > 0 && status == 416)
            return part.moveFileTo (target) ? juce::String() : "Couldn't finish the download of " + name + ".";

        // A part the server won't carry on from starts again
        if (in != nullptr && have > 0 && status != 206)
        {
            have = 0;
            part.deleteFile();
        }

        if (in == nullptr || (status != 0 && status != 200 && status != 206))
            return "Couldn't download " + name + (status > 0 ? " (the server answered " + juce::String (status) + ")" : juce::String())
                     + ". Check the connection and try again: it carries on from where it got to.";

        // The size from the headers: getTotalLength is 32 bits on Windows, and the big kits are bigger
        juce::int64 total = -1;

        if (const auto range = headers["Content-Range"]; range.containsChar ('/') && ! range.endsWithChar ('*'))
            total = range.fromLastOccurrenceOf ("/", false, false).trim().getLargeIntValue();
        else if (const auto length = headers["Content-Length"].trim(); length.isNotEmpty())
            total = have + length.getLargeIntValue();
        else if (const auto remaining = in->getTotalLength(); remaining > 0)
            total = have + remaining;

        auto got = have;

        {
            juce::FileOutputStream out (part);

            if (! out.openedOk())
                return "Couldn't write to " + part.getParentDirectory().getFullPathName() + ".";

            if (have == 0)
            {
                out.setPosition (0);
                out.truncate();
            }

            juce::HeapBlock<char> buffer (1 << 17);

            for (;;)
            {
                if (shouldStop != nullptr && shouldStop())
                    return "Stopped.";

                const int n = in->read (buffer, 1 << 17);

                if (n <= 0)
                    break;

                if (! out.write (buffer, (size_t) n))
                    return "Couldn't write to " + part.getParentDirectory().getFullPathName() + " (is the disk full?).";

                got += n;

                // A size the server doesn't say (a zip made as it's sent): judged from the one expected
                if (progress != nullptr && (total > 0 || expected > 0))
                    progress (total > 0 ? (double) got / (double) total : std::min (0.99, (double) got / (double) expected));
            }

            out.flush();
        }   // closed before it's moved (Windows won't move an open file)

        if (total > 0 && got != total)
            return "The download of " + name + " stopped part way. Try again: it carries on from where it got to.";

        return part.moveFileTo (target) ? juce::String() : "Couldn't finish the download of " + name + ".";
    }
}

juce::String downloadFile (const juce::String& url, const juce::File& target, std::function<void (double)> progress, std::function<bool()> shouldStop,
                           juce::int64 expectedBytes)
{
    return download (url, target, target.getFileName(), progress, shouldStop, expectedBytes);
}

juce::String fetch (std::function<void (float, const juce::String&)> progress, std::function<bool()> shouldStop)
{
    const auto have = ids();
    std::vector<const Download*> wanted;
    double total = 0.0, done = 0.0;

    for (auto& d : downloads())
        for (auto& kit : d.kits)
            if (! have.contains (kit))
            {
                wanted.push_back (&d);
                total += d.megabytes;
                break;
            }

    if (wanted.empty())
        return {};

    if (! folder().createDirectory() || ! downloadsFolder().createDirectory())
        return "Couldn't make " + folder().getFullPathName() + ".";

    auto tell = [&] (double megabytes, const juce::String& what) { if (progress != nullptr) progress ((float) (megabytes / total), what); };

    for (auto* d : wanted)
    {
        // Downloading is most of the time, mixing the rest
        const auto zip = downloadsFolder().getChildFile (juce::URL (d->url).getFileName());
        tell (done, "Downloading " + d->name);

        if (auto error = download (d->url, zip, d->name, [&] (double f) { tell (done + f * d->megabytes * 0.85, "Downloading " + d->name); }, shouldStop,
                                   (juce::int64) d->megabytes * 1048576);
            error.isNotEmpty())
            return error;

        if (d->recipes.empty())
        {
            tell (done + d->megabytes * 0.85, "Unpacking " + d->name);

            if (auto error = unpack (zip, d->kits[0]); error.isNotEmpty())
                return error;
        }
        else
        {
            for (size_t r = 0; r < d->recipes.size(); ++r)
            {
                const auto& recipe = d->recipes[r];
                const double from = done + d->megabytes * (0.85 + 0.15 * (double) r / (double) d->recipes.size());
                const double span = d->megabytes * 0.15 / (double) d->recipes.size();

                if (auto error = drumgizmo::convert (zip, recipe, folder().getChildFile (recipe.id),
                                                     [&] (float f) { tell (from + f * span, "Mixing the " + recipe.name + " kit"); }, shouldStop);
                    error.isNotEmpty())
                    return error;
            }
        }

        zip.deleteFile();
        done += d->megabytes;
    }

    tell (total, "Done");
    return {};
}

juce::StringArray ids()
{
    // The studio kits (metal to jazz), Nightshift's machines, the rest, the older AVL kits last
    juce::StringArray found;

    for (auto& d : folder().findChildFiles (juce::File::findDirectories, false))
        if (! d.getFileName().startsWithChar ('.') && ! d.findChildFiles (juce::File::findFiles, false, "*.sfz").isEmpty())
            found.add (d.getFileName());

    found.sort (true);
    juce::StringArray ordered, older { "RED_ZEPPELIN", "BLACK_PEARL", "BLONDE_BOP_SFZ", "BLONDE_BOP_HR", "BUSKMANS" };

    for (auto prefix : { "CROCELL", "MULDJORD", "DRS_KIT", "DRS_BRUSHES", "NS_" })
        for (auto& id : found)
            if (id.startsWithIgnoreCase (prefix))
                ordered.addIfNotAlreadyThere (id);

    for (auto& id : found)
        if (! std::any_of (older.begin(), older.end(), [&id] (const juce::String& p) { return id.startsWithIgnoreCase (p); }))
            ordered.addIfNotAlreadyThere (id);

    for (auto prefix : older)
        for (auto& id : found)
            if (id.startsWithIgnoreCase (prefix))
                ordered.addIfNotAlreadyThere (id);

    for (auto& id : found)
        ordered.addIfNotAlreadyThere (id);

    return ordered;
}

namespace
{
    /** A kit's own kit.txt, if it has one: its name, what it suits, then its credits. */
    juce::StringArray infoFor (const juce::String& id)
    {
        static std::mutex mutex;
        static std::map<juce::String, std::pair<juce::Time, juce::StringArray>> cache;

        const auto file = folder().getChildFile (id).getChildFile ("kit.txt");

        if (id.isEmpty() || ! file.existsAsFile())
            return {};

        const std::lock_guard<std::mutex> lock (mutex);
        auto& entry = cache[file.getFullPathName()];

        if (entry.first != file.getLastModificationTime())
            entry = { file.getLastModificationTime(), juce::StringArray::fromLines (file.loadFileAsString().trim()) };

        return entry.second;
    }
}

juce::String displayName (const juce::String& id)
{
    if (const auto info = infoFor (id); info[0].trim().isNotEmpty())
        return info[0].trim();

    auto s = id.replace ("_2023_repack", "").replace ("_SFZ", "");

    if (s.endsWithIgnoreCase ("_HR"))
        s = s.dropLastCharacters (3) + " Hot Rods";

    s = s.replaceCharacter ('_', ' ').toLowerCase();

    if (s.startsWith ("buskmans"))
        s = "buskman's" + s.substring (8);

    // Title case
    juce::String out;
    bool start = true;

    for (auto c : s)
    {
        out << (start ? juce::CharacterFunctions::toUpperCase (c) : c);
        start = c == ' ';
    }

    return out;
}

juce::String character (const juce::String& id)
{
    if (const auto info = infoFor (id); info[1].trim().isNotEmpty())
        return info[1].trim();

    // The AVL kits, from their samples: one mono mic a drum, so plainer than the studio kits
    if (id.startsWithIgnoreCase ("RED_ZEPPELIN"))   return "an older, simple Ludwig kit (mono samples, 26\" kick): classic rock when nothing better fits";
    if (id.startsWithIgnoreCase ("BLACK_PEARL"))    return "an older, simple Pearl kit (mono samples): rock and pop when nothing better fits";
    if (id.startsWithIgnoreCase ("BLONDE_BOP_HR"))  return "an older, simple small jazz kit played with Hot Rods (mono samples)";
    if (id.startsWithIgnoreCase ("BLONDE_BOP"))     return "an older, simple small bebop kit (mono samples)";
    if (id.startsWithIgnoreCase ("BUSKMANS"))       return "a cajon and hand percussion: unplugged, folk, acoustic and latin";
    return {};
}

std::pair<juce::String, int> pieceFor (const juce::String& voice)
{
    static const std::map<juce::String, std::pair<juce::String, int>> all {
        { "kick", { "Kick", 36 } }, { "snare", { "Snare", 38 } }, { "snare_rim", { "Snare Edge", 40 } }, { "sidestick", { "Sidestick", 37 } },
        { "clap", { "Clap", 39 } }, { "hat_closed", { "Hat Closed", 42 } }, { "hat_pedal", { "Hat Pedal", 44 } }, { "hat_open", { "Hat Open", 46 } },
        { "tom_high", { "Rack Tom 1", 50 } }, { "tom_mid", { "Rack Tom 2", 47 } }, { "tom_floor", { "Floor Tom", 43 } },
        { "ride", { "Ride", 51 } }, { "ride_bell", { "Ride Bell", 53 } }, { "crash", { "Crash", 49 } }, { "crash_2", { "Crash 2", 57 } },
        { "china", { "China", 52 } }, { "splash", { "Splash", 55 } }, { "tambourine", { "Tambourine", 54 } }, { "cowbell", { "Cowbell", 56 } },
        { "shaker", { "Shaker", 70 } }, { "stick_click", { "Stick Click", 31 } } };

    auto it = all.find (voice);
    return it != all.end() ? it->second : std::pair<juce::String, int> { voice, -1 };
}

float peakFor (const juce::String& voice)
{
    static const std::map<juce::String, float> peaks {
        { "kick", -1.0f }, { "snare", -1.0f }, { "snare_rim", -2.0f }, { "sidestick", -7.0f }, { "clap", -4.0f },
        { "tom_high", -2.0f }, { "tom_mid", -2.0f }, { "tom_floor", -2.0f }, { "hat_closed", -10.0f }, { "hat_pedal", -12.0f },
        { "hat_open", -6.0f }, { "ride", -8.0f }, { "ride_bell", -7.0f }, { "crash", -4.0f }, { "crash_2", -4.0f },
        { "china", -4.0f }, { "splash", -7.0f }, { "tambourine", -10.0f }, { "cowbell", -8.0f }, { "shaker", -12.0f },
        { "stick_click", -10.0f } };

    auto it = peaks.find (voice);
    return juce::Decibels::decibelsToGain (it != peaks.end() ? it->second : -6.0f);
}

juce::String idForName (const juce::String& name)
{
    for (auto& id : ids())
        if (displayName (id).equalsIgnoreCase (name.trim()))
            return id;

    return {};
}

namespace
{
    /** "// 42-Zildjian-13-HatClosed-HR-" -> "Hat Closed" (with the size kept for telling
        cymbals apart later). */
    std::pair<juce::String, juce::String> pieceNameFrom (const juce::String& comment)
    {
        auto tokens = juce::StringArray::fromTokens (comment.fromFirstOccurrenceOf ("//", false, false).trim(), "-", "");
        tokens.removeEmptyStrings();
        tokens.removeString ("HR");

        if (! tokens.isEmpty() && tokens[0].containsOnly ("0123456789"))
            tokens.remove (0);

        juce::String size;

        // Maker, size, then the piece ("Ludwig-26-Kick"); or maker and piece ("Meinl-Cajon-Thump")
        if (tokens.size() >= 3 && tokens[1].containsOnly ("0123456789"))
        {
            size = tokens[1];
            tokens.removeRange (0, 2);

            while (tokens.size() > 1 && tokens[0].containsOnly ("0123456789"))
            {
                size = tokens[0];
                tokens.remove (0);
            }
        }
        else if (tokens.size() >= 3)
        {
            tokens.remove (0);
        }

        // "HatClosed" -> "Hat Closed"
        juce::String name;

        for (auto& t : tokens)
        {
            for (int i = 0; i < t.length(); ++i)
            {
                const auto c = t[i];

                if (i > 0 && juce::CharacterFunctions::isUpperCase (c) && ! juce::CharacterFunctions::isUpperCase (t[i - 1]))
                    name << ' ';

                name << c;
            }

            name << ' ';
        }

        return { name.trim(), size };
    }
}

std::shared_ptr<Kit> read (const juce::File& kitFolder, bool withSamples)
{
    auto sfzFiles = kitFolder.findChildFiles (juce::File::findFiles, false, "*.sfz");

    if (sfzFiles.isEmpty())
        return {};

    auto kit = std::make_shared<Kit>();
    kit->id = kitFolder.getFileName();
    kit->name = displayName (kit->id);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::map<juce::String, int> sampleIndex;
    std::map<int, juce::String> sizes;

    // A group's name is the comment before it, kept when the group starts: its last region is
    // only finished once the next group's comment has been read
    juce::String lastComment, groupComment;
    std::map<juce::String, juce::String> group, region;
    bool inRegion = false;

    auto finishRegion = [&]
    {
        if (! inRegion)
            return;

        auto opcode = [&] (const char* name) -> juce::String
        {
            if (auto it = region.find (name); it != region.end())
                return it->second;

            if (auto it = group.find (name); it != group.end())
                return it->second;

            return {};
        };

        const auto keyText = opcode ("key").isNotEmpty() ? opcode ("key") : opcode ("lokey");
        const int key = keyText.getIntValue();
        const auto file = opcode ("sample").replaceCharacter ('\\', '/');

        if (keyText.isEmpty() || file.isEmpty())
            return;

        auto& piece = kit->pieces[key];

        if (piece.regions.empty())
        {
            piece.key = key;
            auto [name, size] = pieceNameFrom (groupComment);
            const auto label = opcode ("group_label").replaceCharacter ('_', ' ');
            piece.name = label.isNotEmpty() ? label : name.isNotEmpty() ? name : "Key " + juce::String (key);
            sizes[key] = size;
            piece.pan = juce::jlimit (-1.0f, 1.0f, opcode ("pan").getFloatValue() / 100.0f);
            piece.group = opcode ("group").getIntValue();
            piece.offBy = (opcode ("off_by").isNotEmpty() ? opcode ("off_by") : opcode ("offby")).getIntValue();
        }

        Region r;
        r.lowVelocity = opcode ("lovel").isNotEmpty() ? opcode ("lovel").getIntValue() : 1;
        r.highVelocity = opcode ("hivel").isNotEmpty() ? opcode ("hivel").getIntValue() : 127;
        r.lowRandom = opcode ("lorand").isNotEmpty() ? opcode ("lorand").getFloatValue() : 0.0f;
        r.highRandom = opcode ("hirand").isNotEmpty() ? opcode ("hirand").getFloatValue() : 1.0f;
        r.gain = juce::Decibels::decibelsToGain (opcode ("volume").getFloatValue());
        r.velocityTracking = opcode ("amp_veltrack").isNotEmpty() ? juce::jlimit (0.0f, 1.0f, opcode ("amp_veltrack").getFloatValue() / 100.0f) : 1.0f;

        if (withSamples)
        {
            if (auto it = sampleIndex.find (file); it != sampleIndex.end())
            {
                r.sample = it->second;
            }
            else if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (kitFolder.getChildFile (file))); reader != nullptr)
            {
                Sample s;
                s.sampleRate = reader->sampleRate;
                s.audio.setSize ((int) std::min<juce::uint32> (2, reader->numChannels), (int) reader->lengthInSamples);
                reader->read (&s.audio, 0, s.audio.getNumSamples(), 0, true, true);
                r.sample = (int) kit->samples.size();
                sampleIndex[file] = r.sample;
                kit->samples.push_back (std::move (s));
            }
        }

        piece.regions.push_back (r);
    };

    for (auto line : juce::StringArray::fromLines (sfzFiles.getFirst().loadFileAsString()))
    {
        line = line.trim();

        if (line.startsWith ("//"))
        {
            lastComment = line;
            continue;
        }

        for (auto& token : juce::StringArray::fromTokens (line, " \t", ""))
        {
            if (token == "<group>")
            {
                finishRegion();
                inRegion = false;
                group.clear();
                groupComment = lastComment;
            }
            else if (token == "<region>")
            {
                finishRegion();
                inRegion = true;
                region.clear();
            }
            else if (token.containsChar ('='))
            {
                (inRegion ? region : group)[token.upToFirstOccurrenceOf ("=", false, false)] = token.fromFirstOccurrenceOf ("=", false, false);
            }
        }
    }

    finishRegion();

    // Pieces with the same name told apart by size ("Crash 16", "Crash 17")
    std::map<juce::String, int> counts;

    for (auto& [key, piece] : kit->pieces)
        ++counts[piece.name];

    for (auto& [key, piece] : kit->pieces)
        if (counts[piece.name] > 1 && sizes[key].isNotEmpty())
            piece.name << " " << sizes[key];

    return kit;
}

std::shared_ptr<const Kit> load (const juce::String& id)
{
    static std::mutex mutex;
    static std::map<juce::String, std::weak_ptr<const Kit>> cache;

    const std::lock_guard<std::mutex> lock (mutex);

    if (auto existing = cache[id].lock())
        return existing;

    std::shared_ptr<const Kit> kit = read (folder().getChildFile (id), true);
    cache[id] = kit;
    return kit;
}

//==============================================================================
const juce::StringArray& voiceNames()
{
    static const juce::StringArray names { "kick", "snare", "snare_rim", "sidestick", "clap", "hat_closed", "hat_pedal", "hat_open",
                                           "tom_high", "tom_mid", "tom_floor", "ride", "ride_bell", "crash", "crash_2", "china",
                                           "splash", "tambourine", "cowbell", "shaker", "stick_click" };
    return names;
}

namespace
{
    juce::String squash (const juce::String& s)     { return s.toLowerCase().removeCharacters (" "); }

    /** The kit's own piece for a voice, by name, or -1. */
    int directKey (const Kit& kit, const juce::String& voice)
    {
        std::vector<int> matches;

        for (auto& [key, piece] : kit.pieces)
        {
            const auto n = squash (piece.name);
            const bool edge = n.contains ("edge"), choke = n.contains ("choke");
            bool match = false;

            if (voice == "kick")              match = n.contains ("kick");
            else if (voice == "snare")        match = n.contains ("snare") && ! edge && ! n.contains ("side");
            else if (voice == "snare_rim")    match = n.contains ("snare") && edge;
            else if (voice == "sidestick")    match = n.contains ("sidestick");
            else if (voice == "clap")         match = n.contains ("clap");
            else if (voice == "hat_closed")   match = n.contains ("hatclosed");
            else if (voice == "hat_pedal")    match = n.contains ("hatpedal");
            else if (voice == "hat_open")     match = n.contains ("hatsemi") || n.contains ("hatopen");
            else if (voice == "tom_floor")    match = n.contains ("floortom") && ! edge;
            else if (voice == "tom_high" || voice == "tom_mid")
                                              match = n.contains ("tom") && ! n.contains ("floor") && ! edge;
            else if (voice == "ride")         match = n.contains ("ride") && ! n.contains ("bell") && ! choke && ! n.contains ("shank");
            else if (voice == "ride_bell")    match = n.contains ("ridebell");
            else if (voice == "crash" || voice == "crash_2")
                                              match = n.contains ("crash") && ! choke && ! n.contains ("china");
            else if (voice == "china")        match = n.contains ("china");
            else if (voice == "splash")       match = n.contains ("splash");
            else if (voice == "tambourine")   match = n.contains ("tambourine");
            else if (voice == "cowbell")      match = n.contains ("cowbell");
            else if (voice == "shaker")       match = n.contains ("shaker") || n.contains ("maraca");
            else if (voice == "stick_click")  match = n.contains ("stickclick");

            if (match)
                matches.push_back (key);
        }

        if (matches.empty())
            return -1;

        // The second crash or tom where there is one
        if ((voice == "crash_2" || voice == "tom_high") && matches.size() > 1)
            return matches[1];

        return matches.front();
    }
}

int keyForVoice (const Kit& kit, const juce::String& voice)
{
    static const std::map<juce::String, juce::StringArray> standIns {
        { "tom_mid", { "tom_high", "tom_floor" } }, { "tom_high", { "tom_mid" } }, { "tom_floor", { "tom_mid" } },
        { "crash_2", { "crash" } }, { "china", { "crash_2", "crash" } }, { "splash", { "crash_2", "crash" } },
        { "ride_bell", { "ride" } }, { "ride", { "crash" } }, { "hat_pedal", { "hat_closed" } }, { "hat_open", { "hat_closed" } },
        { "clap", { "snare" } }, { "snare_rim", { "snare" } }, { "sidestick", { "snare_rim", "snare" } },
        { "shaker", { "tambourine" } }, { "tambourine", { "shaker" } }, { "cowbell", { "ride_bell" } }, { "stick_click", { "sidestick" } } };

    if (const int key = directKey (kit, voice); key >= 0)
        return key;

    if (auto it = standIns.find (voice); it != standIns.end())
        for (auto& other : it->second)
            if (const int key = directKey (kit, other); key >= 0)
                return key;

    return -1;
}

int generalMidiKey (const juce::String& voice)
{
    static const std::map<juce::String, int> keys {
        { "kick", 36 }, { "snare", 38 }, { "snare_rim", 40 }, { "sidestick", 37 }, { "clap", 39 }, { "hat_closed", 42 },
        { "hat_pedal", 44 }, { "hat_open", 46 }, { "tom_high", 50 }, { "tom_mid", 47 }, { "tom_floor", 43 }, { "ride", 51 },
        { "ride_bell", 53 }, { "crash", 49 }, { "crash_2", 57 }, { "china", 52 }, { "splash", 55 }, { "tambourine", 54 },
        { "cowbell", 56 }, { "shaker", 70 }, { "stick_click", 31 } };

    auto it = keys.find (voice);
    return it != keys.end() ? it->second : -1;
}

juce::StringArray voicesIn (const Kit& kit)
{
    juce::StringArray voices;

    for (auto& v : voiceNames())
        if (directKey (kit, v) >= 0)
            voices.add (v);

    return voices;
}

juce::String generalMidiName (int key)
{
    static const char* const names[] = { "Acoustic Bass Drum", "Kick", "Side Stick", "Snare", "Clap", "Electric Snare", "Low Floor Tom",
                                         "Hat Closed", "High Floor Tom", "Hat Pedal", "Low Tom", "Hat Open", "Low-Mid Tom", "Hi-Mid Tom",
                                         "Crash 1", "High Tom", "Ride 1", "China", "Ride Bell", "Tambourine", "Splash", "Cowbell",
                                         "Crash 2", "Vibraslap", "Ride 2", "Hi Bongo", "Low Bongo", "Mute Hi Conga", "Open Hi Conga",
                                         "Low Conga", "High Timbale", "Low Timbale", "High Agogo", "Low Agogo", "Cabasa", "Maracas" };

    if (key == 31)
        return "Sticks";

    return juce::isPositiveAndBelow (key - 35, (int) std::size (names)) ? names[key - 35] : juce::String();
}

} // namespace drumkits

//==============================================================================
const char* DrumKitPlugin::xmlTypeName = "nightshiftDrumKit";

namespace
{
    constexpr int maxVoices = 96, maxVoicesPerKey = 6;
}

DrumKitPlugin::DrumKitPlugin (tracktion::PluginCreationInfo info)
    : tracktion::Plugin (info)
{
    kitId.referTo (state, juce::Identifier ("kit"), nullptr);
    voices.reserve (maxVoices);

    if (kitId.get().isNotEmpty())
        kit = drumkits::load (kitId.get());
}

DrumKitPlugin::~DrumKitPlugin()
{
    notifyListenersOfDeletion();
}

void DrumKitPlugin::setKit (const juce::String& id)
{
    if (kitId.get() != id)
        kitId = id;     // loads it through valueTreePropertyChanged
}

std::shared_ptr<const drumkits::Kit> DrumKitPlugin::getKit() const
{
    const juce::ScopedLock sl (lock);
    return kit;
}

void DrumKitPlugin::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id)
{
    if (v == state && id == juce::Identifier ("kit"))
    {
        // Loaded here, on the message thread, then swapped in
        auto loaded = drumkits::load (kitId.get());
        const juce::ScopedLock sl (lock);
        voices.clear();
        kit = std::move (loaded);
    }

    tracktion::Plugin::valueTreePropertyChanged (v, id);
}

void DrumKitPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    tracktion::copyPropertiesToCachedValues (v, kitId);
}

bool DrumKitPlugin::hasNameForMidiNoteNumber (int note, int, juce::String& name)
{
    if (auto k = getKit())
        if (auto piece = k->findPiece (note))
        {
            name = piece->name;
            return true;
        }

    return false;
}

void DrumKitPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;
    reset();
}

void DrumKitPlugin::reset()
{
    const juce::ScopedLock sl (lock);
    voices.clear();
}

void DrumKitPlugin::startVoice (int key, int velocity, int delay)
{
    if (kit == nullptr)
        return;

    auto piece = kit->findPiece (key);

    if (piece == nullptr)
        return;

    // The layer for this velocity, and of its alternates one picked at random (not the last one
    // again, where there's another)
    const drumkits::Region* region = nullptr;
    std::array<const drumkits::Region*, 64> layer {};
    size_t layerSize = 0;

    for (auto& r : piece->regions)
        if (velocity >= r.lowVelocity && velocity <= r.highVelocity && layerSize < layer.size())
            layer[layerSize++] = &r;

    for (int attempt = 0; attempt < 4 && layerSize > 0; ++attempt)
    {
        const float roll = random.nextFloat();
        region = layer[layerSize - 1];

        for (size_t i = 0; i < layerSize; ++i)
            if (roll >= layer[i]->lowRandom && roll < layer[i]->highRandom)
                region = layer[i];

        if (layerSize == 1 || region->sample != lastSample[(size_t) key & 127])
            break;
    }

    if (region == nullptr || ! juce::isPositiveAndBelow (region->sample, (int) kit->samples.size()))
        return;

    // A hit in a choke group silences what it chokes (pedal closes the open hat)
    if (piece->group != 0)
        for (auto& v : voices)
            if (v.offBy == piece->group && v.fadeStep == 0.0f)
                v.fadeStep = 1.0f / (float) (0.006 * sampleRate);

    // Room for it: the oldest of this key's ring-outs, else the oldest of all
    int sameKey = 0;

    for (auto& v : voices)
        sameKey += v.key == key ? 1 : 0;

    auto steal = [this] (bool sameKeyOnly, int key)
    {
        auto oldest = voices.end();

        for (auto it = voices.begin(); it != voices.end(); ++it)
            if ((! sameKeyOnly || it->key == key) && (oldest == voices.end() || it->age < oldest->age))
                oldest = it;

        if (oldest != voices.end())
            voices.erase (oldest);
    };

    if (sameKey >= maxVoicesPerKey)
        steal (true, key);
    else if ((int) voices.size() >= maxVoices)
        steal (false, key);

    // The layers change the tone; the level follows the velocity on top of that, as a player's
    // does (about 8 dB down at 40, 12 at a ghost note's 28)
    const auto& sample = kit->samples[(size_t) region->sample];
    lastSample[(size_t) key & 127] = region->sample;
    const float curve = std::pow ((float) velocity / 127.0f, 0.75f);
    const float gain = (1.0f - region->velocityTracking * (1.0f - curve)) * region->gain * headroom;
    const float angle = (piece->pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;

    Voice v;
    v.sample = &sample;
    v.step = sample.sampleRate / sampleRate;
    v.left = gain * std::cos (angle) * juce::MathConstants<float>::sqrt2;
    v.right = gain * std::sin (angle) * juce::MathConstants<float>::sqrt2;
    v.key = key;
    v.group = piece->group;
    v.offBy = piece->offBy;
    v.delay = delay;
    v.age = counter++;
    voices.push_back (v);
}

void DrumKitPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
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
            for (auto& v : voices)
                v.fadeStep = std::max (v.fadeStep, 1.0f / (float) (0.02 * sampleRate));

        for (auto& m : *fc.bufferForMidiMessages)
            if (m.isNoteOn())
                startVoice (m.getNoteNumber(), m.getVelocity(),
                            juce::jlimit (0, numSamples - 1, juce::roundToInt (m.getTimeStamp() * sampleRate)));
    }

    float* left = buffer.getWritePointer (0, fc.bufferStartSample);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, fc.bufferStartSample) : nullptr;

    for (auto it = voices.begin(); it != voices.end();)
    {
        auto& v = *it;
        const auto& audio = v.sample->audio;
        const int length = audio.getNumSamples();
        const float* srcL = audio.getReadPointer (0);
        const float* srcR = audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0);
        bool finished = false;

        for (int i = v.delay; i < numSamples; ++i)
        {
            const int index = (int) v.position;

            if (index + 1 >= length || v.fade <= 0.0f)
            {
                finished = true;
                break;
            }

            // Cubic (Hermite) between the samples around it: far less dulling and aliasing than a
            // straight line when the kit's rate isn't the device's
            const float frac = (float) (v.position - index);
            auto hermite = [index, length, frac] (const float* s)
            {
                const float y0 = s[index > 0 ? index - 1 : 0], y1 = s[index], y2 = s[index + 1], y3 = s[index + 2 < length ? index + 2 : index + 1];
                const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                return ((c3 * frac + c2) * frac + c1) * frac + y1;
            };
            const float l = hermite (srcL);
            const float r = hermite (srcR);

            left[i] += l * v.left * v.fade;

            if (right != nullptr)
                right[i] += r * v.right * v.fade;

            v.position += v.step;
            v.fade -= v.fadeStep;
        }

        v.delay = 0;
        it = finished ? voices.erase (it) : it + 1;
    }
}

} // namespace nightshift
