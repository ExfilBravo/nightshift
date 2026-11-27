// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** Picking an amp tone on TONE3000 (tone3000.com, its API: tone3000.com/api) for the NAM amp, its Select flow:
    the user picks a tone on TONE3000's own pages in the system browser, signed in to TONE3000, and NightShift
    downloads that tone's models (only then: nothing of the catalog is fetched or kept before) into its own data
    folder, with the tone's name, creator and licence.

    OAuth 2.0 with PKCE (S256) and a state check, the publishable key as client_id (no secret: never a t3k_cs_ key),
    the redirect a one-shot listener on http://127.0.0.1:<free port>/callback (RFC 8252's loopback redirect, so
    nothing is registered with the system). The refresh token is kept in NightShift's settings on this computer and
    never written to a log. TONE3000 has no way to revoke one: signing out forgets it. */
namespace nightshift::tone3000
{
    /** Where TONE3000 is, and the app's publishable key (its OAuth client_id, public by design, from the build's
        tone3000-key.txt). Tests point it at a server of their own with a key of their own, and catch the browser. */
    struct Config
    {
        juce::String base { "https://www.tone3000.com" };
        juce::String clientId;
        std::function<void (const juce::URL&)> openBrowser;     // the system browser when not set
    };

    Config& config();

    /** The key this build was made with ("" when it had no tone3000-key.txt). */
    juce::String builtInKey();

    /** config() has a publishable key to sign in with. */
    bool hasKey();

    /** The website, for "Powered by TONE3000" and a build with no key. */
    inline constexpr const char* website = "https://www.tone3000.com";

    /** PKCE (RFC 7636): a verifier of 64 characters from the unreserved set, from the system's random source; its
        S256 challenge (base64url of its SHA-256, no padding); and a state of the same randomness. */
    juce::String makeVerifier();
    juce::String challengeFor (const juce::String& verifier);

    /** The Select flow's address: authorize, response_type=code, prompt=select_tone, NAM models. */
    juce::URL selectUrl (const juce::String& redirectUri, const juce::String& challenge, const juce::String& state);

    struct Tokens
    {
        juce::String access, refresh;
        juce::int64 expires = 0;            // when the access token runs out (ms since 1970)
        bool usable() const                 { return access.isNotEmpty() && juce::Time::currentTimeMillis() < expires - 30000; }
    };

    /** The token endpoint, form-encoded: a code (with its verifier and the redirect it came back to), or a refresh
        token, for new tokens (the refresh token kept is the newest). Blocking. Returns what went wrong, or "";
        `expired` is set when TONE3000 no longer takes the refresh token (sign in again). */
    juce::String exchangeCode (const juce::String& code, const juce::String& verifier, const juce::String& redirectUri, Tokens&);
    juce::String refresh (Tokens&, bool* expired = nullptr);

    /** A tone as TONE3000 gives it, and its models. */
    struct Tone
    {
        juce::String id, title, creator, licence, gear, url;
    };

    struct Model
    {
        juce::String id, name, size, url;
    };

    juce::String fetchTone (const juce::String& toneId, const Tokens&, Tone&);
    juce::String fetchModels (const juce::String& toneId, const Tokens&, std::vector<Model>&);

    /** A model's file (its model_url, with the access token as TONE3000 asks) to `target`. */
    juce::String download (const Model&, const Tokens&, const juce::File& target);

    /** A model picked before, fetched again because it's gone from this computer: the kept refresh token for an access
        token, the tone's models, the one of that name (else the first) downloaded into folder(). Blocking. Returns what
        went wrong, or ""; `file` gets where the model is now; `refreshToken` the newest (emptied when TONE3000 no longer
        takes it). */
    juce::String getAgain (const juce::String& toneId, const juce::String& modelName, juce::String& refreshToken, juce::File& file);

    /** Plain words for a tone's licence ("cc-by" -> "CC BY: credit the creator"). */
    juce::String licenceName (const juce::String& licence);

    /** Where picked tones' models go: NightShift's own data folder (never Documents), a folder a tone.
        Tests set their own. */
    juce::File folder();
    extern juce::File folderOverride;

    /** Picking a tone: the Select flow in the browser, the listener on 127.0.0.1 waiting for its answer, the code
        exchanged for tokens (the state checked first), the tone and its models fetched, the models downloaded into
        folder(). Started on the message thread; onDone comes back on it. */
    class Picker : private juce::Thread
    {
    public:
        struct Result
        {
            bool ok = false;
            bool cancelled = false;         // closed in the browser, or Cancel: nothing to say
            juce::String problem;           // what went wrong, in plain words
            Tone tone;
            std::vector<Model> models;
            std::vector<juce::File> files;  // the models downloaded, in the tone's order
            juce::String refreshToken;      // the newest, to keep
        };

        explicit Picker (std::function<void (const Result&)> onDone);
        ~Picker() override;

        /** Opens the Select flow in the browser and waits for its answer (ten minutes at most). False when it can't
            (no key, or no port to listen on): `problem` says why. */
        bool start (juce::String& problem);
        void cancel();

        /** The listener's port, state and the address opened, and how many answers it turned away for not carrying
            its state (tests). */
        int getPort() const                 { return port; }
        juce::String getState() const       { return state; }
        juce::URL getSelectUrl() const      { return address; }
        int getRefused() const              { return refused.load(); }

    private:
        std::function<void (const Result&)> onDone;
        juce::StreamingSocket listener;
        juce::String verifier, state, redirectUri;
        juce::URL address;
        int port = 0;
        std::atomic<int> refused { 0 };

        void run() override;
        void finish (Result);
    };
}
