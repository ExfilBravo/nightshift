// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Updates.h"
#include "audio/Kits.h"

namespace nightshift::updates
{
namespace
{
    juce::String withoutV (const juce::String& version)
    {
        const auto v = version.trim();
        return v.startsWithIgnoreCase ("v") ? v.substring (1) : v;
    }

    /** The headlines of the release notes' first "What's new" list: "- **Headline.** More..." gives "Headline." */
    juce::StringArray whatsNewIn (const juce::String& notes)
    {
        juce::StringArray points;
        bool inside = false;

        for (auto line : juce::StringArray::fromLines (notes))
        {
            line = line.trim();

            if (line.startsWith ("#"))
            {
                if (inside)
                    break;

                inside = line.containsIgnoreCase ("new");
                continue;
            }

            if (! inside || ! (line.startsWith ("- ") || line.startsWith ("* ")))
                continue;

            auto point = line.substring (2).trim();

            if (const int end = point.indexOf (2, "**"); point.startsWith ("**") && end > 2)
                point = point.substring (2, end);

            point = point.removeCharacters ("*`").trim();

            if (point.isNotEmpty())
                points.add (point);
        }

        return points;
    }
}

juce::String currentVersion()
{
    return JUCE_APPLICATION_VERSION_STRING;
}

int compare (const juce::String& a, const juce::String& b)
{
    // Numbers only, up to any "-beta" and the like
    auto numbers = [] (const juce::String& v) { return juce::StringArray::fromTokens (withoutV (v).upToFirstOccurrenceOf ("-", false, false), ".", {}); };
    const auto x = numbers (a), y = numbers (b);

    for (int i = 0; i < std::max (x.size(), y.size()); ++i)
    {
        const auto p = x[i].getLargeIntValue(), q = y[i].getLargeIntValue();     // a part that isn't there is 0

        if (p != q)
            return p < q ? -1 : 1;
    }

    return 0;
}

bool isInstallerForThisComputer (const juce::String& fileName)
{
   #if JUCE_WINDOWS
    return fileName.startsWithIgnoreCase ("NightShift-Setup-") && fileName.endsWithIgnoreCase (".exe");
   #elif JUCE_MAC
    return fileName.startsWithIgnoreCase ("NightShift-") && fileName.endsWithIgnoreCase ("-mac.pkg");
   #else
    juce::ignoreUnused (fileName);
    return false;
   #endif
}

juce::String parse (const juce::String& json, Release& out)
{
    const auto answer = juce::JSON::parse (json);

    if (! answer.isObject())
        return "GitHub's answer didn't make sense. Try again later.";

    const auto tag = answer["tag_name"].toString();

    if (tag.isEmpty())
        return answer["message"].toString().isNotEmpty() ? "GitHub said \"" + answer["message"].toString() + "\". Try again later."
                                                         : juce::String ("GitHub's answer had no version in it. Try again later.");

    out = {};
    out.version = withoutV (tag);
    out.title = answer["name"].toString().isNotEmpty() ? answer["name"].toString() : "NightShift " + out.version;
    out.page = answer["html_url"].toString();
    out.whatsNew = whatsNewIn (answer["body"].toString());

    if (auto* assets = answer["assets"].getArray())
        for (auto& asset : *assets)
        {
            const auto name = asset["name"].toString();

            if (! isInstallerForThisComputer (name))
                continue;

            out.installer = name;
            out.installerUrl = asset["browser_download_url"].toString();
            out.installerBytes = (juce::int64) asset["size"];

            if (const auto digest = asset["digest"].toString(); digest.startsWithIgnoreCase ("sha256:"))
                out.sha256 = digest.substring (7).trim().toLowerCase();

            break;
        }

    return {};
}

juce::String fetchLatest (Release& out)
{
    int status = 0;
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withExtraHeaders ("Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28")
                       .withConnectionTimeoutMs (15000)
                       .withNumRedirectsToFollow (5)
                       .withStatusCode (&status);

    auto in = juce::URL ("https://api.github.com/repos/" + repository + "/releases/latest").createInputStream (options);

    if (in == nullptr)
        return "NightShift couldn't reach GitHub. Check the internet connection and try again.";

    const auto json = in->readEntireStreamAsString();

    if (status == 404)
        return "There's no NightShift release on GitHub yet.";

    if (status == 403 || status == 429)
        return "GitHub is asking to wait: it allows 60 checks an hour from one connection. Try again later.";

    if (status != 0 && status != 200)
        return "GitHub answered " + juce::String (status) + ". Try again later.";

    return parse (json, out);
}

juce::File downloadFolder()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NightShift update");
}

juce::String sha256Of (const juce::File& file)
{
    juce::FileInputStream in (file);
    return in.openedOk() ? juce::SHA256 (in).toHexString().toLowerCase() : juce::String();
}

bool isTheInstaller (const Release& release, const juce::File& file)
{
    return file.existsAsFile() && release.installerBytes > 0 && file.getSize() == release.installerBytes
             && (release.sha256.isEmpty() || sha256Of (file) == release.sha256);
}

juce::String download (const Release& release, const juce::File& folder, juce::File& installer,
                       std::function<void (double)> progress, std::function<bool()> shouldStop)
{
    if (release.installerUrl.isEmpty() || release.installer.isEmpty())
        return release.title + " has no installer for this computer yet. Try again in a little while.";

    // Only ever its own file name in its own folder, whatever GitHub's answer says it's called
    if (release.installer.containsAnyOf ("/\\:") || release.installer.startsWithChar ('.') || ! release.installerUrl.startsWithIgnoreCase ("https://"))
        return "GitHub's answer named a file NightShift won't download (" + release.installer + ").";

    if (! folder.createDirectory())
        return "NightShift couldn't make " + folder.getFullPathName() + ".";

    installer = folder.getChildFile (release.installer);

    // One already there that isn't this release's (another build of it, or broken): start again
    if (installer.existsAsFile() && ! isTheInstaller (release, installer))
        installer.deleteFile();

    if (auto error = drumkits::downloadFile (release.installerUrl, installer, progress, shouldStop, release.installerBytes); error.isNotEmpty())
        return error;

    if (! isTheInstaller (release, installer))
    {
        installer.deleteFile();
        return "The download isn't the file GitHub has (its size or its checksum is different), so NightShift didn't run it. Try again.";
    }

    return {};
}

juce::String windowsInstallerArguments()
{
    return "/SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /relaunch=yes";
}

bool startInstaller (const juce::File& installer)
{
   #if JUCE_WINDOWS
    return installer.startAsProcess (windowsInstallerArguments());
   #else
    return installer.startAsProcess();
   #endif
}
} // namespace nightshift::updates
