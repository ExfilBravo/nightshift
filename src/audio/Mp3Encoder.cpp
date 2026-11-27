// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Mp3Encoder.h"

// LAME 3.100 (GNU LGPL 2 or later), built from its source by CMakeLists.txt into nightshift_lame
#include <lame.h>

namespace nightshift::mp3
{

const std::vector<int>& bitRates()
{
    static const std::vector<int> rates { 320, 256, 192, 128 };
    return rates;
}

juce::String lameVersion()
{
    return get_lame_version();
}

juce::String encode (const juce::File& source, const juce::File& mp3, int kbps, const juce::String& title,
                     std::function<bool (float)> progress)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return "NightShift couldn't read " + source.getFullPathName();

    const int channels = reader->numChannels > 1 ? 2 : 1;
    auto* lame = lame_init();

    if (lame == nullptr)
        return "the MP3 encoder couldn't start";

    struct Closer
    {
        lame_t lame;
        ~Closer()   { lame_close (lame); }
    } closer { lame };

    lame_set_in_samplerate (lame, (int) reader->sampleRate);
    lame_set_num_channels (lame, channels);
    lame_set_mode (lame, channels == 1 ? MONO : JOINT_STEREO);
    lame_set_VBR (lame, vbr_off);
    lame_set_brate (lame, kbps);
    lame_set_quality (lame, 2);                 // LAME's -h: its high quality, not much slower
    lame_set_bWriteVbrTag (lame, 1);            // the tag at the front, written once the length's known
    lame_set_write_id3tag_automatic (lame, 0);  // the ID3 tag is written here, so the tag's place is known

    if (title.isNotEmpty())
    {
        id3tag_init (lame);
        id3tag_add_v2 (lame);
        id3tag_v2_only (lame);

        // Plain ASCII as it is, anything else as UTF-16 with its byte order mark
        if (title.containsOnly (" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"))
        {
            id3tag_set_title (lame, title.toRawUTF8());
        }
        else
        {
            std::vector<unsigned short> utf16 { 0xfeff };

            for (auto* unit = title.toUTF16().getAddress(); *unit != 0; ++unit)
                utf16.push_back ((unsigned short) *unit);

            utf16.push_back (0);
            id3tag_set_textinfo_utf16 (lame, "TIT2", utf16.data());
        }
    }

    if (lame_init_params (lame) < 0)
        return "the MP3 encoder couldn't use " + juce::String (kbps) + " kbps at " + juce::String ((int) reader->sampleRate) + " Hz";

    mp3.deleteFile();
    mp3.getParentDirectory().createDirectory();
    juce::FileOutputStream out (mp3);

    if (! out.openedOk())
        return "NightShift couldn't write to " + mp3.getFullPathName();

    // The ID3 tag first; LAME's own tag goes in the frame just after it
    size_t tagBytes = 0;

    if (title.isNotEmpty())
    {
        tagBytes = lame_get_id3v2_tag (lame, nullptr, 0);

        if (tagBytes > 0)
        {
            std::vector<unsigned char> tag (tagBytes);
            tagBytes = lame_get_id3v2_tag (lame, tag.data(), tag.size());
            out.write (tag.data(), tagBytes);
        }
    }

    constexpr int block = 4096;
    juce::AudioBuffer<float> audio (channels, block);
    std::vector<unsigned char> encoded ((size_t) (block + block / 4 + 7200));   // LAME's worst case for a block

    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += block)
    {
        const int n = (int) std::min<juce::int64> (block, reader->lengthInSamples - pos);
        reader->read (&audio, 0, n, pos, true, channels > 1);

        const int bytes = lame_encode_buffer_ieee_float (lame, audio.getReadPointer (0), audio.getReadPointer (channels - 1), n,
                                                         encoded.data(), (int) encoded.size());

        if (bytes < 0)
            return "the MP3 encoder stopped (" + juce::String (bytes) + ")";

        out.write (encoded.data(), (size_t) bytes);

        if (progress != nullptr && ! progress ((float) (pos + n) / (float) reader->lengthInSamples))
        {
            out.flush();
            mp3.deleteFile();
            return "stopped";
        }
    }

    const int last = lame_encode_flush (lame, encoded.data(), (int) encoded.size());

    if (last < 0)
        return "the MP3 encoder couldn't finish (" + juce::String (last) + ")";

    out.write (encoded.data(), (size_t) last);

    // LAME's tag, now that it knows the length: over the first frame, which it left for it
    if (const auto size = lame_get_lametag_frame (lame, nullptr, 0); size > 0)
    {
        std::vector<unsigned char> frame (size);

        if (lame_get_lametag_frame (lame, frame.data(), frame.size()) == size && out.setPosition ((juce::int64) tagBytes))
            out.write (frame.data(), size);
    }

    out.flush();

    if (out.getStatus().failed())
        return "NightShift couldn't write to " + mp3.getFullPathName() + ": " + out.getStatus().getErrorMessage();

    return {};
}

} // namespace nightshift::mp3
