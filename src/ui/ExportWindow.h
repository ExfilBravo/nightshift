// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

namespace nightshift
{

/** Export's window (Ctrl+R, the top bar's Export): what to make - the song as a WAV file or an MP3,
    stems (each channel and bus as a WAV of its own), or the notes as a MIDI file - at what quality, and
    for stems whether they go through the master's effects; then Choose where to save... */
class ExportWindow : public juce::Component
{
public:
    enum Kind { wav = 0, mp3 = 1, stems = 2, midi = 3 };

    /** What's chosen: the kind, a WAV's (or stems') bits, an MP3's bit rate, stems through the master. */
    struct Choice
    {
        int kind = wav, bits = 24, kbps = 320;
        bool throughMaster = false;
    };

    /** How a WAV can be saved, in plain words: 24-bit, 16-bit with dither, 32-bit float. */
    struct Quality { int bits; const char* name; const char* shortName; };
    static const std::vector<Quality>& wavQualities();

    /** An MP3's bit rate in plain words ("320 kbps: the best an MP3 can be"). */
    static juce::String describeBitRate (int kbps);

    /** `wholeSong`: Song mode (the export is the song), else the pattern playing. */
    ExportWindow (const Choice&, bool wholeSong);

    Choice getChoice() const;
    void setKind (int);

    /** Choose where to save... (with the choice), or Cancel. */
    std::function<void (const Choice&)> onChosen;
    std::function<void()> onCancel;

    juce::TextButton& getChooseButton()     { return choose; }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 660;

private:
    bool wholeSong;
    juce::ComboBox kindBox, qualityBox, masterBox;
    juce::Label kindLabel { {}, "Make" }, qualityLabel { {}, "Quality" }, masterLabel { {}, "Stems" };
    juce::TextButton choose { "Choose where to save..." }, cancel { "Cancel" };
    Choice choice;
    juce::Rectangle<int> titleArea, noteArea;

    void fillQualities();
    juce::String note() const;
};

} // namespace nightshift
