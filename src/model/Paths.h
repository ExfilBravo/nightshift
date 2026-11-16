// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** Where NightShift keeps its things on this computer, as each system expects:

    - appData(): settings-like files (the plugin list, the built-in kit, the converted drum kits) -
      Windows: %APPDATA%\Nightshift; macOS: ~/Library/Application Support/Nightshift.
    - localData(): big things that stay on this machine (sampled instruments, downloads) -
      Windows: %LOCALAPPDATA%\Nightshift; macOS: the same Application Support folder (a Mac has no
      roaming profile to keep them out of).
    - userVst3(): where plugin installers without admin rights put VST3s -
      Windows: %LOCALAPPDATA%\Programs\Common\VST3; macOS: ~/Library/Audio/Plug-Ins/VST3. */
namespace nightshift::paths
{
    inline juce::File appData()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("Application Support").getChildFile ("Nightshift");
       #else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Nightshift");
       #endif
    }

    inline juce::File localData()
    {
       #if JUCE_WINDOWS
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getSiblingFile ("Local").getChildFile ("Nightshift");
       #else
        return appData();
       #endif
    }

    inline juce::File userVst3()
    {
       #if JUCE_WINDOWS
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getSiblingFile ("Local")
                 .getChildFile ("Programs").getChildFile ("Common").getChildFile ("VST3");
       #elif JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/VST3");
       #else
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".vst3");
       #endif
    }
}
