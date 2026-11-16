// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "PluginWindow.h"
#include "Theme.h"

namespace nightshift
{

PluginWindow::PluginWindow (tracktion::Plugin& p, std::unique_ptr<juce::Component> stockPanel, std::function<void()> closeCallback)
    : juce::DocumentWindow (p.getName(), theme::panel, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      plugin (p), panel (std::move (stockPanel)), onClose (std::move (closeCallback))
{
    setUsingNativeTitleBar (true);

    if (panel != nullptr)
    {
        setContentNonOwned (panel.get(), true);
        setResizable (false, false);
    }
    else if (auto external = dynamic_cast<tracktion::ExternalPlugin*> (&plugin))
    {
        if (auto instance = external->getAudioPluginInstance())
        {
            editor.reset (instance->createEditorAndMakeActive());

            if (editor == nullptr)
                editor = std::make_unique<juce::GenericAudioProcessorEditor> (*instance);
        }

        if (editor != nullptr)
        {
            setContentNonOwned (editor.get(), true);
            setResizable (editor->isResizable(), false);
        }
    }

    if (getContentComponent() == nullptr)
        setSize (360, 120);

    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

PluginWindow::~PluginWindow()
{
    plugin.edit.flushPluginStateIfNeeded (plugin);
    clearContentComponent();
    editor = nullptr;
    panel = nullptr;
}

void PluginWindow::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();
}

} // namespace nightshift
