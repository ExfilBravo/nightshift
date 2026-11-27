// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "ExportWindow.h"
#include "Theme.h"
#include "audio/Mp3Encoder.h"

namespace nightshift
{

using namespace theme;

namespace
{
    constexpr int margin = 24, rowHeight = 36, labelWidth = 92, gapBetween = 12;
}

const std::vector<ExportWindow::Quality>& ExportWindow::wavQualities()
{
    static const std::vector<Quality> qualities {
        { 24, "Best quality (24-bit): for more mixing or mastering later",      "24-bit" },
        { 16, "CD quality (16-bit, smoothed with dither): CDs and most uploads", "16-bit with dither" },
        { 32, "Studio float (32-bit): nothing lost, even above 0 dB",            "32-bit float" },
    };

    return qualities;
}

juce::String ExportWindow::describeBitRate (int kbps)
{
    switch (kbps)
    {
        case 320: return "320 kbps: the best an MP3 can be";
        case 256: return "256 kbps: hard to tell from 320, a little smaller";
        case 192: return "192 kbps: smaller, a touch less clear";
        case 128: return "128 kbps: the smallest, for sending a quick idea";
        default:  break;
    }

    return juce::String (kbps) + " kbps";
}

ExportWindow::ExportWindow (const Choice& start, bool song)
    : wholeSong (song), choice (start)
{
    kindBox.addItemList ({ "The song as a WAV file", "The song as an MP3 file", "Each channel and bus as its own WAV (stems)",
                           "The notes as a MIDI file" }, 1);
    masterBox.addItemList ({ "Before the master's effects: for mixing them elsewhere",
                             "Through the master's effects and loudness, as in the song" }, 1);

    for (auto* label : { &kindLabel, &qualityLabel, &masterLabel })
    {
        label->setFont (font (16.0f, true));
        label->setColour (juce::Label::textColourId, textDim);
        label->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*label);
    }

    for (auto* box : { &kindBox, &qualityBox, &masterBox })
        addAndMakeVisible (*box);

    addAndMakeVisible (choose);
    addAndMakeVisible (cancel);

    kindBox.setSelectedItemIndex (juce::jlimit (0, 3, choice.kind), juce::dontSendNotification);
    masterBox.setSelectedItemIndex (choice.throughMaster ? 1 : 0, juce::dontSendNotification);
    fillQualities();

    kindBox.onChange = [this]
    {
        choice = getChoice();
        choice.kind = kindBox.getSelectedItemIndex();
        fillQualities();
        resized();
        repaint();
    };

    choose.onClick = [this] { if (onChosen != nullptr) onChosen (getChoice()); };
    cancel.onClick = [this] { if (onCancel != nullptr) onCancel(); };

    setSize (width, 10);
    resized();
}

void ExportWindow::setKind (int kind)
{
    kindBox.setSelectedItemIndex (juce::jlimit (0, 3, kind), juce::sendNotificationSync);
}

void ExportWindow::fillQualities()
{
    // A WAV's (and stems') bits, or an MP3's bit rate; what was picked before stays picked
    qualityBox.clear (juce::dontSendNotification);

    if (choice.kind == mp3)
    {
        int index = 0;

        for (size_t i = 0; i < mp3::bitRates().size(); ++i)
        {
            qualityBox.addItem (describeBitRate (mp3::bitRates()[i]), (int) i + 1);

            if (mp3::bitRates()[i] == choice.kbps)
                index = (int) i;
        }

        qualityBox.setSelectedItemIndex (index, juce::dontSendNotification);
    }
    else
    {
        int index = 0;

        for (size_t i = 0; i < wavQualities().size(); ++i)
        {
            qualityBox.addItem (wavQualities()[i].name, (int) i + 1);

            if (wavQualities()[i].bits == choice.bits)
                index = (int) i;
        }

        qualityBox.setSelectedItemIndex (index, juce::dontSendNotification);
    }

    // (a MIDI file has notes, not sound: no quality to choose)
    qualityLabel.setVisible (choice.kind != midi);
    qualityBox.setVisible (choice.kind != midi);
    masterLabel.setVisible (choice.kind == stems);
    masterBox.setVisible (choice.kind == stems);
}

ExportWindow::Choice ExportWindow::getChoice() const
{
    auto c = choice;
    c.kind = juce::jlimit (0, 3, kindBox.getSelectedItemIndex());
    const int q = qualityBox.getSelectedItemIndex();

    if (c.kind == mp3)
        c.kbps = mp3::bitRates()[(size_t) juce::jlimit (0, (int) mp3::bitRates().size() - 1, q)];
    else
        c.bits = wavQualities()[(size_t) juce::jlimit (0, (int) wavQualities().size() - 1, q)].bits;

    c.throughMaster = masterBox.getSelectedItemIndex() == 1;
    return c;
}

juce::String ExportWindow::note() const
{
    const auto what = wholeSong ? juce::String ("the whole song") : juce::String ("the pattern playing (Song mode exports the whole song)");

    switch (choice.kind)
    {
        case mp3:
            return "One MP3 of " + what + ", mastered as the Master tab says. NightShift makes it itself, with the free LAME encoder.";

        case stems:
            return "A WAV for each channel and bus that makes a sound, named after it. Each is as long as " + what
                     + ", so they line up when you bring them into other music software.";

        case midi:
            return "The notes, with the tempo and time signature, for other music software or a synth. Next you choose "
                   "which: the whole song or one pattern.";

        default:
            break;
    }

    return "One WAV of " + what + ", mastered as the Master tab says.";
}

void ExportWindow::paint (juce::Graphics& g)
{
    g.fillAll (panelLight);

    g.setColour (text);
    g.setFont (font (21.0f, true));
    g.drawText ("Export", titleArea, juce::Justification::centredLeft, true);

    g.setColour (textDim);
    g.setFont (font (16.0f));
    g.drawFittedText (note(), noteArea, juce::Justification::topLeft, 3, 1.0f);
}

void ExportWindow::resized()
{
    auto area = juce::Rectangle<int> (width, 1000).reduced (margin);
    titleArea = area.removeFromTop (30);
    area.removeFromTop (16);

    auto row = [&area] (juce::Label& label, juce::ComboBox& box)
    {
        auto r = area.removeFromTop (rowHeight);
        label.setBounds (r.removeFromLeft (labelWidth));
        box.setBounds (r);
        area.removeFromTop (gapBetween);
    };

    row (kindLabel, kindBox);

    if (choice.kind != midi)
        row (qualityLabel, qualityBox);

    if (choice.kind == stems)
        row (masterLabel, masterBox);

    // The note, measured: as many lines as it takes at this width
    juce::GlyphArrangement lines;
    lines.addJustifiedText (font (16.0f), note(), 0.0f, 0.0f, (float) area.getWidth(), juce::Justification::topLeft);
    const int noteHeight = (int) std::ceil (lines.getBoundingBox (0, -1, true).getHeight()) + 4;
    area.removeFromTop (4);
    noteArea = area.removeFromTop (noteHeight);
    area.removeFromTop (20);

    auto buttons = area.removeFromTop (40);
    const int chooseWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font (16.0f, true), choose.getButtonText())) + 44;
    choose.setBounds (buttons.removeFromRight (chooseWidth));
    buttons.removeFromRight (12);
    cancel.setBounds (buttons.removeFromRight (110));

    const int height = buttons.getBottom() + margin;

    if (getHeight() != height)
        setSize (width, height);
}

} // namespace nightshift
