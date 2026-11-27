// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Tone3000.h"
#include "model/Paths.h"
#include <random>

// The publishable key, written by CMake from tone3000-key.txt into a header only this file includes, so a new key
// builds this file again and nothing else (and the key is never on a compiler's command line)
#if __has_include ("Tone3000Key.h")
 #include "Tone3000Key.h"
#endif

#ifndef NIGHTSHIFT_TONE3000_KEY
 #define NIGHTSHIFT_TONE3000_KEY ""
#endif

namespace nightshift::tone3000
{

juce::File folderOverride;

juce::String builtInKey()
{
    // (CMake refuses a secret key already; never one here either)
    const auto key = juce::String (NIGHTSHIFT_TONE3000_KEY).trim();
    return key.startsWith ("t3k_cs_") ? juce::String() : key;
}

Config& config()
{
    static Config c = []
    {
        Config made;
        made.clientId = builtInKey();
        return made;
    }();

    return c;
}

bool hasKey()
{
    const auto& id = config().clientId;
    return id.isNotEmpty() && ! id.startsWith ("t3k_cs_");
}

juce::File folder()
{
    return folderOverride != juce::File() ? folderOverride : paths::localData().getChildFile ("TONE3000");
}

namespace
{
    constexpr int timeoutMs = 30000;

    // From the system's random source (std::random_device: the operating system's own on Windows and macOS)
    juce::String randomText (int length)
    {
        static const char* const unreserved = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
        std::random_device source;
        std::uniform_int_distribution<int> pick (0, 65);
        juce::String text;

        for (int i = 0; i < length; ++i)
            text += juce::String::charToString ((juce::juce_wchar) unreserved[pick (source)]);

        return text;
    }

    juce::String base64url (const juce::MemoryBlock& data)
    {
        return juce::Base64::toBase64 (data.getData(), data.getSize()).replaceCharacter ('+', '-').replaceCharacter ('/', '_')
                   .trimCharactersAtEnd ("=");
    }

    // A request and its JSON answer: POST sends the URL's parameters form-encoded in the body; a bearer token (the
    // user's access token) goes in the Authorization header. `status` is the HTTP status (0: no connection)
    juce::var call (const juce::URL& url, bool post, const juce::String& bearer, int& status)
    {
        juce::String headers;

        if (post)
            headers << "Content-Type: application/x-www-form-urlencoded\r\n";

        if (bearer.isNotEmpty())
            headers << "Authorization: Bearer " << bearer << "\r\n";

        headers << "Accept: application/json";
        status = 0;
        auto options = juce::URL::InputStreamOptions (post ? juce::URL::ParameterHandling::inPostData : juce::URL::ParameterHandling::inAddress)
                           .withConnectionTimeoutMs (timeoutMs)
                           .withExtraHeaders (headers)
                           .withStatusCode (&status);

        std::unique_ptr<juce::InputStream> in (url.createInputStream (options));
        return in != nullptr ? juce::JSON::parse (in->readEntireStreamAsString()) : juce::var();
    }

    const juce::String unreachable ("NightShift couldn't reach TONE3000. Check the internet connection and try again.");

    juce::String unexpected (int status)
    {
        return "TONE3000 didn't answer as NightShift expected (" + juce::String (status) + "). Try again in a minute.";
    }

    juce::String takeTokens (const juce::URL& url, Tokens& tokens, bool* expired)
    {
        int status = 0;
        const auto answer = call (url, true, {}, status);

        if (status == 400 || status == 401)
        {
            if (expired != nullptr)
                *expired = true;

            return "TONE3000 didn't take NightShift's sign-in. Sign in again.";
        }

        if (status == 0)
            return unreachable;

        const auto access = answer.getProperty ("access_token", {}).toString();

        if (status >= 300 || access.isEmpty())
            return unexpected (status);

        tokens.access = access;

        // (each answer may bring a new refresh token: the newest is the one to keep)
        if (const auto next = answer.getProperty ("refresh_token", {}).toString(); next.isNotEmpty())
            tokens.refresh = next;

        tokens.expires = juce::Time::currentTimeMillis() + (juce::int64) (1000.0 * (double) answer.getProperty ("expires_in", 3600));
        return {};
    }

    juce::String nameOf (const juce::var& user)
    {
        // A verified creator's display name, else the username
        const auto shown = user.getProperty ("display_name", {}).toString();
        return shown.isNotEmpty() ? shown : user.getProperty ("username", {}).toString();
    }

    juce::String textOf (const juce::var& v)
    {
        // A gear field may be a word, a list or an object with a name
        if (v.isArray())
        {
            juce::StringArray words;

            for (auto& item : *v.getArray())
                words.add (textOf (item));

            words.removeEmptyStrings();
            return words.joinIntoString (", ");
        }

        if (v.isObject())
            return v.getProperty ("name", v.getProperty ("title", {})).toString();

        return v.toString();
    }
}

juce::String makeVerifier()
{
    return randomText (64);
}

juce::String challengeFor (const juce::String& verifier)
{
    const auto utf8 = verifier.toUTF8();
    return base64url (juce::SHA256 (utf8.getAddress(), utf8.sizeInBytes() - 1).getRawData());
}

juce::URL selectUrl (const juce::String& redirectUri, const juce::String& challenge, const juce::String& state)
{
    return juce::URL (config().base + "/api/v1/oauth/authorize")
               .withParameter ("client_id", config().clientId)
               .withParameter ("redirect_uri", redirectUri)
               .withParameter ("response_type", "code")
               .withParameter ("code_challenge", challenge)
               .withParameter ("code_challenge_method", "S256")
               .withParameter ("state", state)
               .withParameter ("prompt", "select_tone")
               .withParameter ("format", "nam");
}

juce::String exchangeCode (const juce::String& code, const juce::String& verifier, const juce::String& redirectUri, Tokens& tokens)
{
    return takeTokens (juce::URL (config().base + "/api/v1/oauth/token")
                           .withParameter ("grant_type", "authorization_code")
                           .withParameter ("code", code)
                           .withParameter ("code_verifier", verifier)
                           .withParameter ("redirect_uri", redirectUri)
                           .withParameter ("client_id", config().clientId),
                       tokens, nullptr);
}

juce::String refresh (Tokens& tokens, bool* expired)
{
    if (tokens.refresh.isEmpty())
    {
        if (expired != nullptr)
            *expired = true;

        return "Sign in to TONE3000 first.";
    }

    return takeTokens (juce::URL (config().base + "/api/v1/oauth/token")
                           .withParameter ("grant_type", "refresh_token")
                           .withParameter ("refresh_token", tokens.refresh)
                           .withParameter ("client_id", config().clientId),
                       tokens, expired);
}

juce::String fetchTone (const juce::String& toneId, const Tokens& tokens, Tone& tone)
{
    int status = 0;
    const auto answer = call (juce::URL (config().base + "/api/v1/tones/" + juce::URL::addEscapeChars (toneId, true)), false, tokens.access, status);

    if (status == 0)
        return unreachable;

    if (status >= 300 || ! answer.isObject())
        return unexpected (status);

    tone.id = toneId;
    tone.title = answer.getProperty ("title", {}).toString();
    tone.creator = nameOf (answer.getProperty ("user", {}));
    tone.licence = answer.getProperty ("license", {}).toString();
    tone.gear = textOf (answer.getProperty ("gear", {}));
    tone.url = answer.getProperty ("url", {}).toString();
    return {};
}

juce::String fetchModels (const juce::String& toneId, const Tokens& tokens, std::vector<Model>& models)
{
    // TONE3000's default is A2 models; a tone of A1 models (the standard WaveNet, as most are) is asked for again.
    // NAM's core here runs both
    for (auto architecture : { "", "1", "custom" })
    {
        auto url = juce::URL (config().base + "/api/v1/models").withParameter ("tone_id", toneId).withParameter ("page_size", "100");

        if (juce::String (architecture).isNotEmpty())
            url = url.withParameter ("architecture", architecture);

        int status = 0;
        const auto answer = call (url, false, tokens.access, status);

        if (status == 0)
            return unreachable;

        if (status >= 300 || ! answer.isObject())
            return unexpected (status);

        if (auto* data = answer.getProperty ("data", {}).getArray())
            for (auto& m : *data)
                if (const auto link = m.getProperty ("model_url", {}).toString(); link.isNotEmpty())
                    models.push_back ({ m.getProperty ("id", {}).toString(), m.getProperty ("name", {}).toString(),
                                        m.getProperty ("size", {}).toString(), link });

        if (! models.empty())
            break;
    }

    return {};
}

juce::String download (const Model& model, const Tokens& tokens, const juce::File& target)
{
    int status = 0;
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs (timeoutMs)
                       .withExtraHeaders ("Authorization: Bearer " + tokens.access)
                       .withStatusCode (&status);

    std::unique_ptr<juce::InputStream> in (juce::URL (model.url).createInputStream (options));

    if (in == nullptr || status >= 300)
        return status == 0 ? unreachable : unexpected (status);

    target.getParentDirectory().createDirectory();
    const auto part = target.getSiblingFile (target.getFileName() + ".part");
    {
        juce::FileOutputStream out (part);

        if (! out.openedOk() || out.writeFromInputStream (*in, -1) <= 0)
        {
            part.deleteFile();
            return "NightShift couldn't save " + model.name + " in " + target.getParentDirectory().getFullPathName() + ".";
        }
    }

    target.deleteFile();
    return part.moveFileTo (target) ? juce::String() : "NightShift couldn't save " + model.name + ".";
}

juce::String getAgain (const juce::String& toneId, const juce::String& modelName, juce::String& refreshToken, juce::File& file)
{
    Tokens tokens;
    tokens.refresh = refreshToken;
    bool expired = false;

    if (auto problem = refresh (tokens, &expired); problem.isNotEmpty())
    {
        if (expired)
            refreshToken = {};

        return problem;
    }

    refreshToken = tokens.refresh;
    std::vector<Model> models;

    if (auto problem = fetchModels (toneId, tokens, models); problem.isNotEmpty())
        return problem;

    if (models.empty())
        return "That tone has no models on TONE3000 now.";

    auto model = std::find_if (models.begin(), models.end(), [&modelName] (const Model& m) { return m.name == modelName; });

    if (model == models.end())
        model = models.begin();

    const auto name = juce::File::createLegalFileName (model->name.isNotEmpty() ? model->name : model->id);
    file = folder().getChildFile (juce::File::createLegalFileName (toneId)).getChildFile ((name.isNotEmpty() ? name : juce::String ("Model")) + ".nam");
    return download (*model, tokens, file);
}

juce::String licenceName (const juce::String& licence)
{
    const auto id = licence.trim().toLowerCase();

    if (id == "t3k")            return "TONE3000 licence";
    if (id == "cco" || id == "cc0")  return "CC0: free for anything";
    if (id == "cc-by")          return "CC BY: credit the creator";
    if (id == "cc-by-sa")       return "CC BY-SA: credit, share alike";
    if (id == "cc-by-nc")       return "CC BY-NC: credit, not for sale";
    if (id == "cc-by-nc-sa")    return "CC BY-NC-SA: credit, not for sale, share alike";
    if (id == "cc-by-nd")       return "CC BY-ND: credit, no changes";
    if (id == "cc-by-nc-nd")    return "CC BY-NC-ND: credit, not for sale, no changes";
    return licence.isNotEmpty() ? licence : juce::String ("Licence not given");
}

//==============================================================================
Picker::Picker (std::function<void (const Result&)> done)
    : juce::Thread ("TONE3000"), onDone (std::move (done))
{
}

Picker::~Picker()
{
    signalThreadShouldExit();
    listener.close();
    stopThread (5000);
}

bool Picker::start (juce::String& problem)
{
    if (! hasKey())
    {
        problem = "This build of NightShift has no TONE3000 key.";
        return false;
    }

    // A one-shot listener on this computer only, on a port the system picks (RFC 8252's loopback redirect)
    if (! listener.createListener (0, "127.0.0.1") || listener.getBoundPort() <= 0)
    {
        problem = "NightShift couldn't listen for TONE3000's answer on this computer.";
        return false;
    }

    port = listener.getBoundPort();
    verifier = makeVerifier();
    state = makeVerifier().substring (0, 32);
    redirectUri = "http://127.0.0.1:" + juce::String (port) + "/callback";
    address = selectUrl (redirectUri, challengeFor (verifier), state);
    startThread();

    if (config().openBrowser != nullptr)
        config().openBrowser (address);
    else
        address.launchInDefaultBrowser();

    return true;
}

void Picker::cancel()
{
    signalThreadShouldExit();
    listener.close();
}

namespace
{
    // The page the browser shows once TONE3000 sends it back here
    void answerBrowser (juce::StreamingSocket& socket, int status, const juce::String& heading, const juce::String& words)
    {
        const auto body = "<!doctype html><html><head><meta charset=\"utf-8\"><title>NightShift</title></head>"
                          "<body style=\"font-family:system-ui,sans-serif;background:#0f1219;color:#e9ecf4;padding:48px\"><h1>" + heading
                          + "</h1><p style=\"font-size:18px\">" + words + "</p></body></html>";
        const juce::String head = "HTTP/1.1 " + juce::String (status) + (status == 200 ? " OK" : status == 403 ? " Forbidden" : " Not Found")
                                  + "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + juce::String ((int) body.getNumBytesAsUTF8())
                                  + "\r\nConnection: close\r\n\r\n";
        socket.write (head.toRawUTF8(), (int) head.getNumBytesAsUTF8());
        socket.write (body.toRawUTF8(), (int) body.getNumBytesAsUTF8());
    }
}

void Picker::run()
{
    Result result;
    juce::StringPairArray answer;
    bool answered = false;
    const auto until = juce::Time::getMillisecondCounter() + 10u * 60u * 1000u;

    // The browser's visit to /callback (anything else it asks for is turned away)
    while (! threadShouldExit() && ! answered && juce::Time::getMillisecondCounter() < until)
    {
        if (listener.waitUntilReady (true, 200) != 1)
            continue;

        std::unique_ptr<juce::StreamingSocket> client (listener.waitForNextConnection());

        if (client == nullptr)
            continue;

        juce::MemoryBlock head;
        char buffer[2048];

        while (head.getSize() < 16384 && client->waitUntilReady (true, 3000) == 1)
        {
            const int got = client->read (buffer, (int) sizeof (buffer), false);

            if (got <= 0)
                break;

            head.append (buffer, (size_t) got);

            if (head.toString().contains ("\r\n\r\n"))
                break;
        }

        const auto line = head.toString().upToFirstOccurrenceOf ("\r\n", false, false);
        const auto target = line.fromFirstOccurrenceOf (" ", false, false).upToLastOccurrenceOf (" ", false, false);

        if (! line.startsWith ("GET ") || ! (target == "/callback" || target.startsWith ("/callback?")))
        {
            answerBrowser (*client, 404, "Not here", "This is NightShift waiting for TONE3000.");
            continue;
        }

        juce::StringPairArray query;

        for (auto& pair : juce::StringArray::fromTokens (target.fromFirstOccurrenceOf ("?", false, false), "&", {}))
            query.set (juce::URL::removeEscapeChars (pair.upToFirstOccurrenceOf ("=", false, false)),
                       juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false)));

        // Only the answer to the sign-in NightShift started: anything else is turned away, and it waits on
        if (query["state"] != state)
        {
            ++refused;
            answerBrowser (*client, 403, "Not for NightShift", "This answer didn't come from the sign-in NightShift started, so NightShift ignored it.");
            continue;
        }

        answer = query;
        const bool picked = answer["code"].isNotEmpty() && answer["error"].isEmpty() && answer["canceled"] != "true";
        answerBrowser (*client, 200, picked ? "Back to NightShift" : "Nothing picked",
                       picked ? "Your tone is on its way into NightShift's amp. You can close this tab."
                              : "NightShift didn't get a tone this time. You can close this tab.");
        answered = true;
    }

    listener.close();

    if (threadShouldExit())
        return;

    if (! answered)
    {
        result.problem = "TONE3000 didn't answer within ten minutes. Try again when you're ready.";
        finish (result);
        return;
    }

    if (answer["canceled"] == "true" || answer["error"] == "access_denied" || (answer["code"].isEmpty() && answer["error"].isEmpty()))
    {
        result.cancelled = true;
        finish (result);
        return;
    }

    if (answer["error"].isNotEmpty())
    {
        result.problem = "TONE3000 couldn't give NightShift a tone (" + answer["error"] + "). Try again.";
        finish (result);
        return;
    }

    Tokens tokens;
    const auto toneId = answer["tone_id"];

    if (auto problem = exchangeCode (answer["code"], verifier, redirectUri, tokens); problem.isNotEmpty())
    {
        result.problem = problem;
        finish (result);
        return;
    }

    result.refreshToken = tokens.refresh;

    if (toneId.isEmpty())
    {
        result.cancelled = true;        // signed in, nothing picked
        finish (result);
        return;
    }

    result.problem = fetchTone (toneId, tokens, result.tone);

    if (result.problem.isEmpty())
        result.problem = fetchModels (toneId, tokens, result.models);

    if (result.problem.isEmpty() && result.models.empty())
        result.problem = "\"" + result.tone.title + "\" has no amp models NightShift can load (NAM's .nam files).";

    // Its models into a folder of the tone's own (only now: the user asked for this one)
    const auto dir = folder().getChildFile (juce::File::createLegalFileName (toneId));

    for (size_t i = 0; result.problem.isEmpty() && i < result.models.size() && i < 24; ++i)
    {
        if (threadShouldExit())
            return;

        const auto& model = result.models[i];
        const auto name = juce::File::createLegalFileName (model.name.isNotEmpty() ? model.name : model.id);
        const auto file = dir.getChildFile ((name.isNotEmpty() ? name : "Model " + juce::String ((int) i + 1)) + ".nam");

        if (auto problem = download (model, tokens, file); problem.isNotEmpty())
            result.problem = problem;
        else
            result.files.push_back (file);
    }

    result.ok = result.problem.isEmpty() && ! result.files.empty();
    finish (result);
}

void Picker::finish (Result result)
{
    juce::MessageManager::callAsync ([done = onDone, result] { if (done != nullptr) done (result); });
}

} // namespace nightshift::tone3000
