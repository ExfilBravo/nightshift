// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** What NightShift opens outside itself, from About and the start screen: the donation page and
    the licence. Tests catch them with links::opener instead of a browser or Notepad opening. */
namespace nightshift::links
{
    inline std::function<void (const juce::URL&)> opener;

    inline void open (const juce::URL& page)
    {
        if (opener != nullptr)
            opener (page);
        else
            page.launchInDefaultBrowser();
    }
}

/** Donations: NightShift is free, and asks (gently, and only once by itself) for support. The start
    screen shows a banner the first time it opens; About always has Donate. Both open `url` in the
    browser. */
namespace nightshift::donate
{
    /** Where Donate goes: the real page (Ko-fi, PayPal.me, Buy Me a Coffee, GitHub Sponsors...).
        Empty hides every Donate there is, as it is until there's a page to send people to. */
    inline const juce::String url {};

    inline bool isSet()                                 { return url.isNotEmpty(); }

    /** What the asks say, under "NightShift is free". */
    inline const juce::String pitch { "If it helps your music, a donation keeps it going. It's entirely up to you." };

    inline void open()
    {
        if (isSet())
            links::open (juce::URL (url));
    }
}

/** NightShift's licence: the GNU GPL, version 3 or later (LICENSE in the source; LICENSE.txt beside
    the installed exe, where install.cmd, the setup and the build put it). */
namespace nightshift::licence
{
    inline const juce::String copyright { "Copyright (C) 2026 NightWriter" };

    /** What About says (the GPL's "appropriate legal notices" for a program with a user interface). */
    inline const juce::String notice { "NightShift is free software under the GNU General Public License, version 3 or later: "
                                       "you can share it and change it. It comes with no warranty. Licence... shows the terms." };

    /** LICENSE.txt beside the running exe (in a Mac app, its Resources), or the GPL's own page when it isn't there. */
    inline juce::URL url()
    {
       #if JUCE_MAC
        // Inside the app bundle: Contents/Resources, beside Contents/MacOS where the program is
        const auto beside = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory()
                                .getSiblingFile ("Resources").getChildFile ("LICENSE.txt");
       #else
        const auto beside = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("LICENSE.txt");
       #endif
        return beside.existsAsFile() ? juce::URL (beside) : juce::URL ("https://www.gnu.org/licenses/gpl-3.0.html");
    }

    inline void open()                                  { links::open (url()); }
}
