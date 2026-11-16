// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

namespace nightshift
{

/** A desktop window showing a plugin's editor: a hosted plugin's own (or a generic one), or
    the panel given for a stock effect. */
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (tracktion::Plugin&, std::unique_ptr<juce::Component> panel, std::function<void()> onClose);
    ~PluginWindow() override;

    tracktion::Plugin* getPlugin() const        { return &plugin; }
    juce::Component* getPanel() const           { return panel.get(); }
    void closeButtonPressed() override;

    /** Keys the plugin leaves alone go to the studio (Space, R, the PC keyboard's octave keys). */
    std::function<bool (const juce::KeyPress&)> onKey;
    bool keyPressed (const juce::KeyPress& key) override   { return onKey != nullptr && onKey (key); }

private:
    tracktion::Plugin& plugin;
    std::unique_ptr<juce::Component> panel;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

} // namespace nightshift
