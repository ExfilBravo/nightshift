// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Nova.h"

namespace nightshift::nova
{

namespace
{
    using Settings = std::initializer_list<std::pair<const char*, float>>;

    Patch make (const char* name, const char* category, Settings settings)
    {
        Patch p;
        p.name = name;
        p.category = category;

        for (auto& [id, value] : settings)
        {
            jassert (findInfo (id) != nullptr);
            p.settings[id] = value;
        }

        return p;
    }

    // Mod matrix sources and destinations, by number (see Nova.cpp)
    constexpr float lfo1 = 1, lfo2 = 2, filterEnv = 3, modEnv = 4, velocity = 5, modWheel = 6;
    constexpr float toPitch = 1, toOsc2Pitch = 3, toOsc1Shape = 4, toPulseWidth = 6, toFm = 9, toNoise = 12, toCutoff = 13,
                    toVolume = 16, toPan = 17;

    // Divisions (see the table in Nova.cpp)
    constexpr float twoBars = 1, oneBar = 2, half = 3, dottedQuarter = 4, quarter = 5, dottedEighth = 7, eighth = 8, sixteenth = 11;

    std::vector<Patch> build()
    {
        return {
            //==================================================================== Keys
            make ("Velvet Keys", "Keys", {
                { "osc1_shape", 0 }, { "osc2_shape", 0 }, { "fm", 0.22f },
                { "mod1_source", filterEnv }, { "mod1_dest", toFm }, { "mod1_amount", 0.3f },
                { "mod2_source", velocity }, { "mod2_dest", toFm }, { "mod2_amount", 0.18f },
                { "fenv_attack", 0.0f }, { "fenv_decay", 1.2f }, { "fenv_sustain", 0.0f }, { "fenv_release", 0.4f },
                { "amp_decay", 3.0f }, { "amp_sustain", 0.3f }, { "amp_release", 0.45f }, { "amp_velocity", 0.7f },
                { "filter_slope", 0 }, { "filter_cutoff", 7000.0f }, { "filter_keytrack", 0.5f },
                { "chorus_mix", 0.35f }, { "chorus_rate", 0.6f }, { "chorus_depth", 0.4f },
                { "reverb_mix", 0.18f }, { "reverb_size", 0.5f }, { "reverb_decay", 1.8f }, { "volume", -13.1f } }),

            make ("Dusty Rhodes", "Keys", {
                { "osc1_shape", 0 }, { "osc2_shape", 0 }, { "fm", 0.26f },
                { "mod1_source", filterEnv }, { "mod1_dest", toFm }, { "mod1_amount", 0.25f },
                { "mod2_source", lfo1 }, { "mod2_dest", toVolume }, { "mod2_amount", 0.22f },
                { "mod3_source", lfo1 }, { "mod3_dest", toPan }, { "mod3_amount", 0.45f },
                { "lfo1_rate", 4.2f }, { "lfo1_retrigger", 0 },
                { "fenv_attack", 0.0f }, { "fenv_decay", 0.9f }, { "fenv_sustain", 0.0f },
                { "amp_decay", 3.5f }, { "amp_sustain", 0.25f }, { "amp_release", 0.4f }, { "amp_velocity", 0.75f },
                { "filter_slope", 0 }, { "filter_cutoff", 4500.0f }, { "filter_keytrack", 0.5f },
                { "drive", 0.15f }, { "reverb_mix", 0.2f }, { "reverb_size", 0.45f }, { "reverb_decay", 1.6f }, { "volume", -10.5f } }),

            make ("Wurli Bite", "Keys", {
                { "osc1_shape", 1.3f }, { "osc2_shape", 0 }, { "osc2_octave", 1 }, { "fm", 0.16f },
                { "mod1_source", velocity }, { "mod1_dest", toFm }, { "mod1_amount", 0.25f },
                { "mod2_source", lfo1 }, { "mod2_dest", toVolume }, { "mod2_amount", 0.15f },
                { "lfo1_rate", 5.5f }, { "lfo1_retrigger", 0 },
                { "filter_slope", 0 }, { "filter_cutoff", 2600.0f }, { "filter_env", 1.5f }, { "filter_keytrack", 0.6f },
                { "fenv_decay", 0.5f }, { "fenv_sustain", 0.2f },
                { "amp_decay", 2.2f }, { "amp_sustain", 0.35f }, { "amp_release", 0.25f }, { "amp_velocity", 0.7f },
                { "filter_drive", 0.3f }, { "reverb_mix", 0.15f }, { "reverb_decay", 1.4f }, { "volume", -12.9f } }),

            make ("Drawbar Organ", "Keys", {
                { "osc1_shape", 0 }, { "osc2_shape", 0 }, { "osc2_octave", 1 }, { "osc2_level", 0.45f }, { "sub_level", 0.6f },
                { "amp_attack", 0.004f }, { "amp_release", 0.06f }, { "amp_velocity", 0.0f },
                { "mod1_source", lfo1 }, { "mod1_dest", toPitch }, { "mod1_amount", 0.003f },
                { "lfo1_rate", 6.2f }, { "lfo1_retrigger", 0 },
                { "chorus_mix", 0.5f }, { "chorus_rate", 4.5f }, { "chorus_depth", 0.35f },
                { "drive", 0.25f }, { "reverb_mix", 0.15f }, { "reverb_decay", 1.5f }, { "volume", -13.1f } }),

            make ("Clav Funk", "Keys", {
                { "osc1_shape", 3 }, { "osc1_pw", 0.22f },
                { "filter_cutoff", 700.0f }, { "filter_reso", 0.35f }, { "filter_env", 4.0f }, { "filter_velocity", 0.4f },
                { "filter_keytrack", 0.5f },
                { "fenv_decay", 0.18f }, { "fenv_sustain", 0.1f }, { "fenv_release", 0.1f },
                { "amp_decay", 0.6f }, { "amp_sustain", 0.4f }, { "amp_release", 0.06f }, { "amp_velocity", 0.7f },
                { "reverb_mix", 0.1f }, { "reverb_size", 0.3f }, { "reverb_decay", 0.9f }, { "volume", -13.8f } }),

            make ("Felt Keys", "Keys", {
                { "osc1_shape", 1 }, { "osc2_shape", 0 }, { "osc2_octave", 1 }, { "osc2_level", 0.3f },
                { "noise_tone", 0.3f },
                { "mod1_source", modEnv }, { "mod1_dest", toNoise }, { "mod1_amount", 0.12f },
                { "env3_attack", 0.0f }, { "env3_decay", 0.06f }, { "env3_sustain", 0.0f },
                { "filter_slope", 0 }, { "filter_cutoff", 1500.0f }, { "filter_env", 2.0f }, { "filter_keytrack", 0.7f },
                { "filter_velocity", 0.3f }, { "fenv_decay", 0.6f },
                { "amp_decay", 2.5f }, { "amp_sustain", 0.0f }, { "amp_release", 0.4f }, { "amp_velocity", 0.75f },
                { "chorus_mix", 0.15f }, { "reverb_mix", 0.25f }, { "reverb_size", 0.55f }, { "reverb_decay", 2.2f }, { "volume", -10.8f } }),

            //==================================================================== Pads
            make ("Night Pad", "Pad", {
                { "osc1_unison", 5 }, { "osc1_detune", 0.25f }, { "osc1_width", 0.8f },
                { "osc2_level", 0.5f }, { "osc2_octave", -1 }, { "osc2_unison", 3 }, { "osc2_detune", 0.2f },
                { "filter_cutoff", 900.0f }, { "filter_reso", 0.15f }, { "filter_env", 1.5f },
                { "fenv_attack", 1.2f }, { "fenv_decay", 2.5f }, { "fenv_sustain", 0.5f }, { "fenv_release", 2.0f },
                { "amp_attack", 0.8f }, { "amp_release", 2.0f }, { "amp_velocity", 0.3f },
                { "mod1_source", lfo2 }, { "mod1_dest", toCutoff }, { "mod1_amount", 0.06f },
                { "lfo2_shape", 5 }, { "lfo2_rate", 0.3f },
                { "chorus_mix", 0.4f }, { "reverb_mix", 0.4f }, { "reverb_size", 0.85f }, { "reverb_decay", 4.0f }, { "volume", -14.9f } }),

            make ("Warm Analog Pad", "Pad", {
                { "osc1_unison", 3 }, { "osc1_detune", 0.15f },
                { "osc2_level", 0.6f }, { "osc2_shape", 3 }, { "osc2_pw", 0.4f }, { "osc2_fine", 7.0f },
                { "mod1_source", lfo1 }, { "mod1_dest", toPulseWidth }, { "mod1_amount", 0.3f },
                { "lfo1_shape", 1 }, { "lfo1_rate", 0.3f }, { "lfo1_retrigger", 0 },
                { "filter_cutoff", 1500.0f }, { "filter_reso", 0.2f },
                { "amp_attack", 0.5f }, { "amp_release", 1.5f }, { "amp_velocity", 0.3f },
                { "chorus_mix", 0.3f }, { "reverb_mix", 0.3f }, { "reverb_size", 0.7f }, { "reverb_decay", 3.0f }, { "volume", -20.6f } }),

            make ("Glass Choir", "Pad", {
                { "osc1_shape", 1 }, { "osc1_unison", 4 }, { "osc1_detune", 0.12f },
                { "osc2_level", 0.4f }, { "osc2_shape", 0 }, { "osc2_octave", 1 },
                { "filter_slope", 0 }, { "filter_cutoff", 3000.0f },
                { "amp_attack", 1.2f }, { "amp_release", 2.5f }, { "amp_velocity", 0.3f },
                { "mod1_source", lfo1 }, { "mod1_dest", toPitch }, { "mod1_amount", 0.01f },
                { "lfo1_rate", 5.0f }, { "lfo1_fade", 1.0f },
                { "chorus_mix", 0.5f }, { "reverb_mix", 0.55f }, { "reverb_size", 0.9f }, { "reverb_decay", 5.0f }, { "volume", -14.5f } }),

            make ("Aurora", "Pad", {
                { "osc1_unison", 8 }, { "osc1_detune", 0.35f }, { "osc1_width", 1.0f },
                { "osc2_level", 0.4f }, { "osc2_octave", 1 }, { "osc2_unison", 8 }, { "osc2_detune", 0.3f }, { "osc2_width", 1.0f },
                { "filter_cutoff", 5000.0f },
                { "mod1_source", lfo2 }, { "mod1_dest", toCutoff }, { "mod1_amount", 0.08f },
                { "lfo2_shape", 5 }, { "lfo2_rate", 0.2f },
                { "amp_attack", 0.6f }, { "amp_release", 2.0f }, { "amp_velocity", 0.3f },
                { "chorus_mix", 0.25f }, { "delay_mix", 0.15f }, { "delay_time", dottedQuarter }, { "delay_feedback", 0.4f },
                { "reverb_mix", 0.5f }, { "reverb_size", 0.9f }, { "reverb_decay", 5.0f }, { "volume", -15.5f } }),

            make ("Dark Matter", "Pad", {
                { "osc1_unison", 4 }, { "osc1_detune", 0.2f },
                { "osc2_level", 0.7f }, { "osc2_shape", 3 }, { "osc2_octave", -1 },
                { "noise_level", 0.05f }, { "noise_tone", 0.2f },
                { "filter_cutoff", 300.0f }, { "filter_reso", 0.35f },
                { "mod1_source", lfo1 }, { "mod1_dest", toCutoff }, { "mod1_amount", 0.15f },
                { "lfo1_shape", 1 }, { "lfo1_sync", 1 }, { "lfo1_division", twoBars }, { "lfo1_retrigger", 0 },
                { "amp_attack", 2.0f }, { "amp_release", 3.0f }, { "amp_velocity", 0.2f },
                { "reverb_mix", 0.5f }, { "reverb_size", 0.9f }, { "reverb_decay", 6.0f }, { "volume", -16.4f } }),

            make ("String Machine", "Pad", {
                { "osc1_unison", 6 }, { "osc1_detune", 0.18f },
                { "osc2_level", 0.35f }, { "osc2_octave", 1 }, { "osc2_unison", 4 }, { "osc2_detune", 0.15f },
                { "filter_slope", 0 }, { "filter_cutoff", 3500.0f },
                { "amp_attack", 0.35f }, { "amp_release", 0.9f }, { "amp_velocity", 0.3f },
                { "chorus_mix", 0.6f }, { "chorus_rate", 0.8f }, { "chorus_depth", 0.7f },
                { "reverb_mix", 0.3f }, { "reverb_size", 0.7f }, { "reverb_decay", 2.8f }, { "volume", -15.7f } }),

            make ("Breath Pad", "Pad", {
                { "osc1_shape", 1 }, { "osc1_level", 0.6f }, { "osc2_level", 0.25f }, { "osc2_shape", 0 }, { "osc2_octave", 1 },
                { "noise_level", 0.25f }, { "noise_tone", 0.5f },
                { "filter_mode", 1 }, { "filter_slope", 0 }, { "filter_cutoff", 900.0f }, { "filter_reso", 0.3f }, { "filter_keytrack", 0.6f },
                { "mod1_source", lfo2 }, { "mod1_dest", toCutoff }, { "mod1_amount", 0.1f },
                { "lfo2_shape", 5 }, { "lfo2_rate", 0.25f },
                { "amp_attack", 1.5f }, { "amp_release", 2.0f }, { "amp_velocity", 0.3f },
                { "reverb_mix", 0.5f }, { "reverb_size", 0.85f }, { "reverb_decay", 4.5f }, { "volume", -0.8f } }),

            //==================================================================== Leads
            make ("Supersaw Lead", "Lead", {
                { "osc1_unison", 7 }, { "osc1_detune", 0.3f }, { "osc1_width", 0.9f },
                { "osc2_level", 0.4f }, { "osc2_octave", 1 }, { "osc2_unison", 5 }, { "osc2_detune", 0.25f },
                { "filter_cutoff", 6000.0f },
                { "amp_attack", 0.005f }, { "amp_release", 0.25f }, { "amp_velocity", 0.3f },
                { "delay_mix", 0.2f }, { "delay_time", eighth }, { "delay_feedback", 0.35f },
                { "reverb_mix", 0.25f }, { "reverb_size", 0.7f }, { "reverb_decay", 2.5f }, { "volume", -13.4f } }),

            make ("Mono Lead", "Lead", {
                { "osc2_level", 0.8f }, { "osc2_fine", 8.0f }, { "sub_level", 0.3f },
                { "filter_cutoff", 2500.0f }, { "filter_reso", 0.25f }, { "filter_env", 2.0f }, { "filter_keytrack", 0.5f },
                { "fenv_decay", 0.4f }, { "fenv_sustain", 0.4f },
                { "voice_mode", 2 }, { "glide", 0.08f },
                { "mod1_source", lfo1 }, { "mod1_dest", toPitch }, { "mod1_amount", 0.012f },
                { "lfo1_rate", 5.5f }, { "lfo1_fade", 0.6f },
                { "drive", 0.2f }, { "delay_mix", 0.15f }, { "reverb_mix", 0.15f }, { "volume", -13.0f } }),

            make ("Chip Lead", "Lead", {
                { "osc1_shape", 3 }, { "osc1_pw", 0.25f },
                { "voice_mode", 2 }, { "glide", 0.03f },
                { "mod1_source", lfo1 }, { "mod1_dest", toPitch }, { "mod1_amount", 0.015f },
                { "lfo1_rate", 6.0f }, { "lfo1_fade", 0.3f },
                { "amp_release", 0.1f }, { "amp_velocity", 0.2f },
                { "delay_mix", 0.2f }, { "delay_time", eighth }, { "volume", -15.3f } }),

            make ("Screamer", "Lead", {
                { "osc1_unison", 3 }, { "osc1_detune", 0.12f },
                { "osc2_shape", 0 }, { "osc2_semi", 7 }, { "fm", 0.2f },
                { "mod1_source", modEnv }, { "mod1_dest", toFm }, { "mod1_amount", 0.3f },
                { "env3_decay", 0.5f }, { "env3_sustain", 0.2f },
                { "filter_cutoff", 4000.0f }, { "filter_reso", 0.3f }, { "filter_drive", 0.4f },
                { "voice_mode", 2 }, { "glide", 0.05f },
                { "mod2_source", lfo1 }, { "mod2_dest", toPitch }, { "mod2_amount", 0.015f },
                { "lfo1_rate", 5.8f }, { "lfo1_fade", 0.4f },
                { "drive", 0.35f }, { "delay_mix", 0.18f }, { "reverb_mix", 0.18f }, { "volume", -12.2f } }),

            make ("Soft Flute", "Lead", {
                { "osc1_shape", 0.6f }, { "noise_level", 0.08f }, { "noise_tone", 0.6f },
                { "filter_slope", 0 }, { "filter_cutoff", 3500.0f }, { "filter_keytrack", 0.6f },
                { "amp_attack", 0.06f }, { "amp_release", 0.25f }, { "amp_velocity", 0.4f },
                { "voice_mode", 2 }, { "glide", 0.04f },
                { "mod1_source", lfo1 }, { "mod1_dest", toPitch }, { "mod1_amount", 0.012f },
                { "lfo1_rate", 5.0f }, { "lfo1_fade", 0.5f },
                { "reverb_mix", 0.25f }, { "reverb_size", 0.6f }, { "reverb_decay", 2.2f }, { "volume", -11.3f } }),

            make ("Lofi Lead", "Lead", {
                { "osc1_shape", 1 },
                { "mod1_source", lfo2 }, { "mod1_dest", toPitch }, { "mod1_amount", 0.006f },
                { "lfo2_shape", 5 }, { "lfo2_rate", 0.7f },
                { "filter_slope", 0 }, { "filter_cutoff", 2500.0f },
                { "amp_release", 0.3f }, { "amp_velocity", 0.5f },
                { "drive", 0.15f }, { "chorus_mix", 0.2f }, { "delay_mix", 0.18f }, { "delay_time", dottedEighth },
                { "reverb_mix", 0.2f }, { "volume", -9.9f } }),

            //==================================================================== Bass
            make ("Sub Bass", "Bass", {
                { "osc1_shape", 0 }, { "osc2_shape", 0 }, { "osc2_octave", 1 }, { "osc2_level", 0.15f },
                { "amp_attack", 0.004f }, { "amp_decay", 0.4f }, { "amp_sustain", 0.85f }, { "amp_release", 0.1f },
                { "amp_velocity", 0.3f }, { "voice_mode", 1 }, { "drive", 0.1f }, { "volume", -7.9f } }),

            make ("Reese", "Bass", {
                { "osc1_unison", 2 }, { "osc1_detune", 0.15f }, { "osc1_width", 0.2f },
                { "osc2_level", 0.8f }, { "osc2_fine", 14.0f }, { "sub_level", 0.4f },
                { "filter_cutoff", 900.0f }, { "filter_reso", 0.15f },
                { "mod1_source", lfo2 }, { "mod1_dest", toCutoff }, { "mod1_amount", 0.05f },
                { "lfo2_shape", 5 }, { "lfo2_rate", 0.25f },
                { "amp_release", 0.12f }, { "amp_velocity", 0.3f },
                { "voice_mode", 1 }, { "drive", 0.3f }, { "volume", -10.6f } }),

            make ("Acid Line", "Bass", {
                { "filter_cutoff", 250.0f }, { "filter_reso", 0.78f }, { "filter_env", 4.5f }, { "filter_velocity", 0.4f },
                { "fenv_decay", 0.22f }, { "fenv_sustain", 0.0f },
                { "amp_decay", 0.4f }, { "amp_sustain", 0.7f }, { "amp_release", 0.06f }, { "amp_velocity", 0.5f },
                { "voice_mode", 2 }, { "glide", 0.07f },
                { "drive", 0.35f }, { "delay_mix", 0.12f }, { "delay_time", dottedEighth }, { "volume", -9.0f } }),

            make ("Wobble Bass", "Bass", {
                { "osc1_unison", 3 }, { "osc1_detune", 0.12f },
                { "osc2_level", 0.6f }, { "osc2_shape", 3 }, { "osc2_octave", -1 }, { "sub_level", 0.4f },
                { "filter_cutoff", 200.0f }, { "filter_reso", 0.45f },
                { "mod1_source", lfo1 }, { "mod1_dest", toCutoff }, { "mod1_amount", 0.55f },
                { "lfo1_sync", 1 }, { "lfo1_division", eighth },
                { "amp_release", 0.1f }, { "amp_velocity", 0.3f },
                { "voice_mode", 1 }, { "drive", 0.45f }, { "volume", -6.1f } }),

            make ("Finger Bass", "Bass", {
                { "filter_cutoff", 350.0f }, { "filter_env", 2.5f }, { "filter_keytrack", 0.4f }, { "filter_velocity", 0.3f },
                { "fenv_decay", 0.25f }, { "fenv_sustain", 0.15f },
                { "amp_decay", 0.8f }, { "amp_sustain", 0.3f }, { "amp_release", 0.08f }, { "amp_velocity", 0.6f },
                { "sub_level", 0.3f }, { "voice_mode", 1 }, { "volume", -3.8f } }),

            make ("Moog Bass", "Bass", {
                { "osc2_level", 0.7f }, { "osc2_shape", 3 }, { "osc2_octave", -1 }, { "osc2_fine", -6.0f },
                { "filter_cutoff", 500.0f }, { "filter_reso", 0.3f }, { "filter_env", 2.5f },
                { "fenv_decay", 0.3f }, { "fenv_sustain", 0.2f },
                { "amp_release", 0.1f }, { "amp_velocity", 0.4f },
                { "voice_mode", 2 }, { "glide", 0.03f }, { "drive", 0.3f }, { "volume", -9.0f } }),

            make ("808 Bass", "Bass", {
                { "osc1_shape", 0 },
                { "mod1_source", modEnv }, { "mod1_dest", toPitch }, { "mod1_amount", 0.25f },
                { "env3_attack", 0.0f }, { "env3_decay", 0.06f }, { "env3_sustain", 0.0f },
                { "amp_decay", 3.0f }, { "amp_sustain", 0.0f }, { "amp_release", 0.3f }, { "amp_velocity", 0.3f },
                { "filter_slope", 0 }, { "filter_cutoff", 3000.0f },
                { "voice_mode", 1 }, { "glide", 0.05f }, { "drive", 0.35f }, { "volume", -8.0f } }),

            //==================================================================== Plucks
            make ("Glass Pluck", "Pluck", {
                { "osc1_shape", 3 }, { "osc1_pw", 0.3f }, { "osc2_level", 0.3f }, { "osc2_shape", 0 }, { "osc2_octave", 1 },
                { "filter_slope", 0 }, { "filter_cutoff", 450.0f }, { "filter_reso", 0.25f }, { "filter_env", 4.0f },
                { "filter_keytrack", 0.6f }, { "fenv_decay", 0.3f },
                { "amp_decay", 0.45f }, { "amp_sustain", 0.0f }, { "amp_release", 0.35f },
                { "delay_mix", 0.25f }, { "delay_time", dottedEighth }, { "delay_feedback", 0.4f },
                { "reverb_mix", 0.2f }, { "volume", -12.9f } }),

            make ("Pizz Pluck", "Pluck", {
                { "filter_cutoff", 300.0f }, { "filter_env", 4.5f }, { "filter_keytrack", 0.6f }, { "filter_velocity", 0.3f },
                { "fenv_decay", 0.15f },
                { "amp_decay", 0.35f }, { "amp_sustain", 0.0f }, { "amp_release", 0.2f }, { "amp_velocity", 0.6f },
                { "reverb_mix", 0.3f }, { "reverb_size", 0.6f }, { "reverb_decay", 2.0f }, { "volume", -7.2f } }),

            make ("Kalimba", "Pluck", {
                { "osc1_shape", 0 }, { "osc2_shape", 0 }, { "osc2_octave", 2 }, { "osc2_semi", 4 }, { "fm", 0.1f },
                { "mod1_source", modEnv }, { "mod1_dest", toFm }, { "mod1_amount", 0.3f },
                { "env3_attack", 0.0f }, { "env3_decay", 0.08f }, { "env3_sustain", 0.0f },
                { "amp_decay", 1.0f }, { "amp_sustain", 0.0f }, { "amp_release", 0.5f }, { "amp_velocity", 0.6f },
                { "delay_mix", 0.1f }, { "delay_time", eighth }, { "reverb_mix", 0.25f }, { "reverb_decay", 1.8f }, { "volume", -9.7f } }),

            make ("Harp Pluck", "Pluck", {
                { "osc1_shape", 1 }, { "osc2_level", 0.25f }, { "osc2_octave", 1 },
                { "filter_slope", 0 }, { "filter_cutoff", 1200.0f }, { "filter_env", 3.0f }, { "filter_keytrack", 0.6f },
                { "fenv_decay", 0.25f },
                { "amp_decay", 2.0f }, { "amp_sustain", 0.0f }, { "amp_release", 1.0f }, { "amp_velocity", 0.6f },
                { "reverb_mix", 0.35f }, { "reverb_size", 0.7f }, { "reverb_decay", 3.0f }, { "volume", -10.4f } }),

            make ("Future Pluck", "Pluck", {
                { "osc1_unison", 7 }, { "osc1_detune", 0.25f },
                { "filter_cutoff", 400.0f }, { "filter_env", 5.0f }, { "filter_keytrack", 0.5f },
                { "fenv_decay", 0.2f },
                { "amp_decay", 0.5f }, { "amp_sustain", 0.0f }, { "amp_release", 0.3f }, { "amp_velocity", 0.4f },
                { "delay_mix", 0.25f }, { "delay_time", eighth }, { "reverb_mix", 0.3f }, { "volume", -8.1f } }),

            //==================================================================== Bells
            make ("Crystal Bell", "Bell", {
                { "osc1_shape", 0 }, { "osc2_shape", 0 }, { "osc2_octave", 1 }, { "osc2_semi", 7 }, { "fm", 0.3f },
                { "mod1_source", modEnv }, { "mod1_dest", toFm }, { "mod1_amount", 0.2f },
                { "env3_attack", 0.0f }, { "env3_decay", 1.5f }, { "env3_sustain", 0.0f },
                { "amp_decay", 4.0f }, { "amp_sustain", 0.0f }, { "amp_release", 2.0f }, { "amp_velocity", 0.6f },
                { "reverb_mix", 0.4f }, { "reverb_size", 0.8f }, { "reverb_decay", 4.0f }, { "volume", -14.9f } }),

            make ("Music Box", "Bell", {
                { "osc1_shape", 0 }, { "osc1_octave", 1 }, { "osc2_shape", 0 }, { "osc2_octave", 3 }, { "osc2_level", 0.12f },
                { "fm", 0.1f },
                { "amp_decay", 1.6f }, { "amp_sustain", 0.0f }, { "amp_release", 0.9f }, { "amp_velocity", 0.5f },
                { "reverb_mix", 0.35f }, { "reverb_size", 0.6f }, { "reverb_decay", 2.5f }, { "volume", -12.0f } }),

            make ("Church Bell", "Bell", {
                { "osc1_shape", 0 }, { "osc2_shape", 0 }, { "osc2_semi", 3 }, { "osc2_fine", 40.0f }, { "fm", 0.4f },
                { "mod1_source", modEnv }, { "mod1_dest", toFm }, { "mod1_amount", 0.15f },
                { "env3_attack", 0.0f }, { "env3_decay", 2.5f }, { "env3_sustain", 0.0f },
                { "amp_decay", 6.0f }, { "amp_sustain", 0.0f }, { "amp_release", 3.0f }, { "amp_velocity", 0.5f },
                { "reverb_mix", 0.45f }, { "reverb_size", 0.9f }, { "reverb_decay", 5.0f }, { "volume", -14.3f } }),

            //==================================================================== Brass
            make ("Synth Brass", "Brass", {
                { "osc1_unison", 3 }, { "osc1_detune", 0.12f }, { "osc2_level", 0.7f }, { "osc2_fine", 6.0f },
                { "filter_cutoff", 700.0f }, { "filter_env", 2.5f }, { "filter_keytrack", 0.5f }, { "filter_velocity", 0.3f },
                { "fenv_attack", 0.06f }, { "fenv_decay", 0.5f }, { "fenv_sustain", 0.5f },
                { "amp_attack", 0.03f }, { "amp_sustain", 0.9f }, { "amp_release", 0.25f }, { "amp_velocity", 0.4f },
                { "chorus_mix", 0.2f }, { "reverb_mix", 0.2f }, { "volume", -16.0f } }),

            make ("90s Stab", "Brass", {
                { "osc1_unison", 4 }, { "osc1_detune", 0.2f }, { "osc2_level", 0.5f }, { "osc2_shape", 3 }, { "osc2_octave", 1 },
                { "filter_cutoff", 1200.0f }, { "filter_env", 3.5f }, { "fenv_decay", 0.2f },
                { "amp_decay", 0.35f }, { "amp_sustain", 0.0f }, { "amp_release", 0.2f }, { "amp_velocity", 0.4f },
                { "delay_mix", 0.15f }, { "delay_time", dottedEighth }, { "reverb_mix", 0.3f }, { "volume", -10.9f } }),

            //==================================================================== Arps
            make ("Arp Sparkle", "Arp", {
                { "osc1_shape", 3 }, { "osc1_pw", 0.35f },
                { "filter_cutoff", 1500.0f }, { "filter_env", 3.0f }, { "filter_keytrack", 0.5f }, { "fenv_decay", 0.12f },
                { "amp_decay", 0.25f }, { "amp_sustain", 0.0f }, { "amp_release", 0.15f }, { "amp_velocity", 0.5f },
                { "delay_mix", 0.3f }, { "delay_time", dottedEighth }, { "delay_feedback", 0.45f },
                { "reverb_mix", 0.25f }, { "volume", -9.8f } }),

            make ("Sequence Saw", "Arp", {
                { "osc1_unison", 2 }, { "osc1_detune", 0.1f },
                { "filter_cutoff", 600.0f }, { "filter_reso", 0.4f }, { "filter_env", 3.0f }, { "fenv_decay", 0.15f },
                { "mod1_source", lfo1 }, { "mod1_dest", toCutoff }, { "mod1_amount", 0.12f },
                { "lfo1_shape", 1 }, { "lfo1_sync", 1 }, { "lfo1_division", oneBar }, { "lfo1_retrigger", 0 },
                { "amp_decay", 0.3f }, { "amp_sustain", 0.2f }, { "amp_release", 0.1f }, { "amp_velocity", 0.5f },
                { "delay_mix", 0.2f }, { "delay_time", sixteenth }, { "delay_feedback", 0.3f }, { "volume", -9.1f } }),

            //==================================================================== Textures
            make ("Riser", "Texture", {
                { "osc1_level", 0.0f }, { "noise_level", 0.8f }, { "noise_tone", 1.0f },
                { "filter_mode", 1 }, { "filter_slope", 0 }, { "filter_cutoff", 300.0f }, { "filter_reso", 0.4f },
                { "mod1_source", modEnv }, { "mod1_dest", toCutoff }, { "mod1_amount", 0.75f },
                { "env3_attack", 8.0f }, { "env3_sustain", 1.0f }, { "env3_release", 2.0f },
                { "amp_attack", 6.0f }, { "amp_release", 2.0f }, { "amp_velocity", 0.0f },
                { "reverb_mix", 0.5f }, { "reverb_size", 0.9f }, { "reverb_decay", 6.0f }, { "volume", -5.7f } }),

            make ("Space Drone", "Texture", {
                { "osc1_unison", 6 }, { "osc1_detune", 0.3f }, { "osc2_level", 0.8f }, { "osc2_shape", 0 }, { "osc2_octave", -1 },
                { "filter_cutoff", 250.0f }, { "filter_reso", 0.5f },
                { "mod1_source", lfo2 }, { "mod1_dest", toCutoff }, { "mod1_amount", 0.2f },
                { "lfo2_shape", 5 }, { "lfo2_rate", 0.08f },
                { "amp_attack", 3.0f }, { "amp_release", 5.0f }, { "amp_velocity", 0.0f },
                { "delay_mix", 0.25f }, { "delay_time", half }, { "delay_feedback", 0.6f }, { "delay_tone", 0.4f },
                { "reverb_mix", 0.7f }, { "reverb_size", 1.0f }, { "reverb_decay", 9.0f }, { "volume", -22.3f } }),

            make ("Init", "Init", {}),
        };
    }
}

const std::vector<Patch>& factory()
{
    static const std::vector<Patch> list = build();
    return list;
}

const Patch* findFactory (const juce::String& name)
{
    for (auto& p : factory())
        if (p.name.equalsIgnoreCase (name))
            return &p;

    return nullptr;
}

juce::StringArray factoryNames()
{
    juce::StringArray names;

    for (auto& p : factory())
        names.add (p.name);

    return names;
}

juce::StringArray categories()
{
    juce::StringArray list;

    for (auto& p : factory())
        list.addIfNotAlreadyThere (p.category);

    return list;
}

juce::String defaultPreset()
{
    return "Velvet Keys";
}

} // namespace nightshift::nova
