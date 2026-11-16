// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "SynthPanel.h"
#include "ParamControls.h"
#include "ChannelRack.h"
#include "Theme.h"

namespace nightshift
{

using namespace theme;
using namespace controls;

namespace
{
    const juce::Colour osc1Colour { 0xffffb054 }, osc2Colour { 0xff8f9bff }, mixColour { 0xff5fd0c5 }, filterColour { 0xffff7eb6 },
                       envColour { 0xff6fdc8c }, lfoColour { 0xffc792ea }, fxColour { 0xff5fd0c5 };

    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    float frac (float x)                        { return x - std::floor (x); }

    /** A wave for drawing: as Nova morphs it, without the rounding that only matters to the ear. */
    float waveAt (float shape, float pw, float t)
    {
        auto at = [pw] (int s, float x)
        {
            switch (s)
            {
                case 0:  return std::sin (twoPi * x);
                case 1:  return 1.0f - 4.0f * std::abs (frac (x + 0.25f) - 0.5f);
                case 2:  return 2.0f * frac (x + 0.5f) - 1.0f;
                default: return frac (x) < pw ? 1.0f : -1.0f;
            }
        };

        const int below = std::min (2, (int) shape);
        const float f = shape - (float) below;
        return at (below, t) + (at (below + 1, t) - at (below, t)) * f;
    }

    void drawWave (juce::Graphics& g, juce::Rectangle<float> r, float shape, float pw, int unison, float detune, float level, juce::Colour colour)
    {
        g.setColour (hairlineStrong);
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

        // Unison copies drift apart: drawn faint, slipped by their detune
        const int copies = unison > 1 ? 2 : 0;

        for (int c = -copies; c <= copies; ++c)
        {
            if (copies > 0 && c == 0)
                continue;

            juce::Path p;
            const float slip = c == 0 ? 0.0f : c * 0.05f * (0.3f + detune);

            for (int i = 0; i <= 160; ++i)
            {
                const float x = r.getX() + r.getWidth() * i / 160.0f;
                const float y = r.getCentreY() - waveAt (shape, pw, 2.0f * i / 160.0f + slip) * r.getHeight() * 0.42f * (0.25f + 0.75f * level);

                if (i == 0)
                    p.startNewSubPath (x, y);
                else
                    p.lineTo (x, y);
            }

            g.setColour (colour.withAlpha (c == 0 ? 0.95f : 0.35f));
            g.strokePath (p, juce::PathStrokeType (c == 0 ? 2.0f : 1.3f));
        }

        if (copies > 0)
        {
            juce::Path p;

            for (int i = 0; i <= 160; ++i)
            {
                const float x = r.getX() + r.getWidth() * i / 160.0f;
                const float y = r.getCentreY() - waveAt (shape, pw, 2.0f * i / 160.0f) * r.getHeight() * 0.42f * (0.25f + 0.75f * level);
                i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
            }

            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (2.0f));
        }
    }

    /** The filter's response in dB at f, as Nova's state-variable filter has it. */
    float responseDb (int mode, bool steep, float cutoff, float q, float f)
    {
        auto stage = [mode, f, cutoff] (float stageQ)
        {
            const float w = f / cutoff, den = std::sqrt ((1.0f - w * w) * (1.0f - w * w) + (w / stageQ) * (w / stageQ));

            switch (mode)
            {
                case 1:  return (w / stageQ) / den;
                case 2:  return w * w / den;
                case 3:  return std::abs (1.0f - w * w) / den;
                default: return 1.0f / den;
            }
        };

        const float gain = steep ? stage (0.7071f) * stage (q) : stage (q);
        return juce::Decibels::gainToDecibels (gain, -60.0f);
    }

    void drawFilter (juce::Graphics& g, juce::Rectangle<float> r, int mode, bool steep, float cutoff, float reso, float envOctaves, juce::Colour colour)
    {
        auto xFor = [r] (float f) { return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); };
        auto yFor = [r] (float db) { return r.getY() + r.getHeight() * (24.0f - juce::jlimit (-36.0f, 24.0f, db)) / 60.0f; };

        g.setColour (hairline);

        for (float f : { 100.0f, 1000.0f, 10000.0f })
            g.drawVerticalLine ((int) xFor (f), r.getY(), r.getBottom());

        g.drawHorizontalLine ((int) yFor (0.0f), r.getX(), r.getRight());

        // Where the envelope takes the cutoff
        if (std::abs (envOctaves) > 0.05f)
        {
            const float reach = juce::jlimit (20.0f, 20000.0f, cutoff * std::exp2 (envOctaves));
            const float x1 = xFor (std::min (cutoff, reach)), x2 = xFor (std::max (cutoff, reach));
            g.setColour (colour.withAlpha (0.08f));
            g.fillRect (x1, r.getY(), x2 - x1, r.getHeight());
            g.setColour (colour.withAlpha (0.5f));
            g.drawVerticalLine ((int) xFor (reach), r.getY(), r.getBottom());
        }

        const float q = 0.7071f * std::pow (35.0f, reso);
        juce::Path line;

        for (int i = 0; i <= 200; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, i / 200.0f);
            const float x = xFor (f), y = yFor (responseDb (mode, steep, cutoff, q, f));
            i == 0 ? line.startNewSubPath (x, y) : line.lineTo (x, y);
        }

        auto fill = line;
        fill.lineTo (r.getRight(), r.getBottom());
        fill.lineTo (r.getX(), r.getBottom());
        fill.closeSubPath();
        g.setColour (colour.withAlpha (0.14f));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (line, juce::PathStrokeType (2.0f));
    }

    void drawEnvelope (juce::Graphics& g, juce::Rectangle<float> r, float a, float d, float s, float rel, juce::Colour colour)
    {
        // Each stage gets room by the square root of its time, so short and long both show
        const float wa = std::sqrt (a + 0.002f), wd = std::sqrt (d + 0.002f), wr = std::sqrt (rel + 0.002f);
        const float hold = r.getWidth() * 0.22f, scale = (r.getWidth() - hold) / (wa + wd + wr);
        const float top = r.getY() + 2.0f, bottom = r.getBottom() - 1.0f;
        auto yFor = [=] (float level) { return bottom - (bottom - top) * level; };

        juce::Path p;
        p.startNewSubPath (r.getX(), bottom);
        float x = r.getX();

        // Attack: heading for 1.3, stopping at 1 (as Nova does)
        for (int i = 1; i <= 24; ++i)
        {
            const float t = i / 24.0f, level = std::min (1.0f, 1.3f * (1.0f - std::pow (0.3f / 1.3f, t)));
            p.lineTo (x + wa * scale * t, yFor (level));
        }

        x += wa * scale;

        for (int i = 1; i <= 24; ++i)
        {
            const float t = i / 24.0f;
            p.lineTo (x + wd * scale * t, yFor (s + (1.0f - s) * std::exp (-4.6f * t)));
        }

        x += wd * scale;
        p.lineTo (x + hold, yFor (s));
        x += hold;

        for (int i = 1; i <= 24; ++i)
        {
            const float t = i / 24.0f;
            p.lineTo (x + wr * scale * t, yFor (s * std::exp (-4.6f * t)));
        }

        auto fill = p;
        fill.lineTo (r.getRight(), bottom);
        fill.closeSubPath();
        g.setColour (colour.withAlpha (0.14f));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (2.0f));

        // Where the key is let go
        g.setColour (hairlineStrong);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
    }

    void drawLfo (juce::Graphics& g, juce::Rectangle<float> r, int shape, float fade, juce::Colour colour)
    {
        // A fixed scatter stands in for the random shapes
        static const float steps[] = { 0.6f, -0.3f, 0.9f, -0.8f, 0.1f, -0.5f, 0.7f, -0.1f };

        auto value = [shape] (float t)
        {
            const float p = frac (t);
            const int cycle = (int) std::floor (t) & 7;

            switch (shape)
            {
                case 0:  return std::sin (twoPi * p);
                case 1:  return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
                case 2:  return 1.0f - 2.0f * p;
                case 3:  return p < 0.5f ? 1.0f : -1.0f;
                case 4:  return steps[cycle];
                default: return steps[cycle] + (steps[(cycle + 1) & 7] - steps[cycle]) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * p));
            }
        };

        g.setColour (hairlineStrong);
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

        juce::Path p;
        const float cycles = shape >= 4 ? 4.0f : 2.0f;

        for (int i = 0; i <= 200; ++i)
        {
            const float t = cycles * i / 200.0f;
            const float depth = fade > 0.01f ? std::min (1.0f, t / (cycles * std::min (1.0f, fade / 2.0f))) : 1.0f;
            const float x = r.getX() + r.getWidth() * i / 200.0f, y = r.getCentreY() - value (t) * depth * r.getHeight() * 0.42f;
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }

        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }
}

//==============================================================================
/** A titled group of controls on a page, with what they do drawn above them. */
class SynthPanel::Section : public juce::Component
{
public:
    Section (const juce::String& t, juce::Colour c) : title (t), colour (c) {}

    juce::String title;
    juce::Colour colour;
    float weight = 1.0f;            // its share of the page's width
    int displayHeight = 0;
    std::function<void (juce::Graphics&, juce::Rectangle<float>)> paintDisplay;
    std::vector<std::vector<juce::Component*>> rows;

    juce::Rectangle<int> displayBounds() const
    {
        return getLocalBounds().reduced (12, 0).withTrimmedTop (32).withHeight (displayHeight);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (panelLight.withAlpha (0.6f));
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (hairline);
        g.drawRoundedRectangle (b, 10.0f, 1.0f);

        auto top = getLocalBounds().reduced (12, 0).removeFromTop (32);
        g.setColour (colour);
        g.fillRoundedRectangle ((float) top.getX(), 10.0f, 3.0f, 14.0f, 1.5f);
        g.setColour (textDim);
        g.setFont (labelFont());
        g.drawText (title.toUpperCase(), top.withTrimmedLeft (10), juce::Justification::centredLeft, true);

        if (displayHeight > 0 && paintDisplay != nullptr)
        {
            auto d = displayBounds();
            g.setColour (background);
            g.fillRoundedRectangle (d.toFloat(), 8.0f);

            {
                juce::Graphics::ScopedSaveState state (g);
                g.reduceClipRegion (d.reduced (1));
                paintDisplay (g, d.toFloat().reduced (8.0f, 6.0f));
            }

            g.setColour (hairline);
            g.drawRoundedRectangle (d.toFloat().reduced (0.5f), 8.0f, 1.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 0).withTrimmedTop (32 + (displayHeight > 0 ? displayHeight + 8 : 0)).withTrimmedBottom (10);

        // Rows of fields and switches take what they need; rows of knobs share the rest
        int fixed = 0, knobRows = 0;

        for (auto& row : rows)
        {
            if (isKnobRow (row))
                ++knobRows;
            else
                fixed += 38;
        }

        const int knobHeight = knobRows > 0 ? juce::jlimit (60, 100, (area.getHeight() - fixed) / knobRows) : 0;

        for (auto& row : rows)
        {
            const bool knobs = isKnobRow (row);
            auto line = area.removeFromTop (knobs ? knobHeight : 30);

            if (! knobs)
                area.removeFromTop (8);

            // Knobs share evenly; a row of choices gives each room for its options
            const int gap = knobs ? 0 : 8, n = (int) row.size();
            std::vector<float> widths;
            float total = 0.0f;

            for (auto* c : row)
            {
                auto* choice = dynamic_cast<ParamChoice*> (c);
                widths.push_back (choice != nullptr ? (float) choice->getIdealWidth() : 100.0f);
                total += widths.back();
            }

            const float space = (float) (line.getWidth() - gap * (n - 1));

            for (size_t i = 0; i < row.size(); ++i)
            {
                row[i]->setBounds (line.removeFromLeft (knobs ? juce::roundToInt (space / n) : juce::roundToInt (space * widths[i] / total)));
                line.removeFromLeft (gap);
            }
        }
    }

    static bool isKnobRow (const std::vector<juce::Component*>& row)
    {
        return ! row.empty() && dynamic_cast<ParamKnob*> (row.front()) != nullptr;
    }
};

//==============================================================================
SynthPanel::SynthPanel (Song& s, AudioEngine& a)
    : song (s), audio (a)
{
    channelChip.setTooltip ("The synth channel it shows");
    channelChip.onClick = [this] { showChannelMenu(); };
    addAndMakeVisible (channelChip);

    presetChip.setTooltip ("The sound: pick one of Nova's factory sounds");
    presetChip.onClick = [this]
    {
        if (auto c = channel())
            ChannelRack::createSoundMenu ([this] (const juce::String& name) { choosePreset (name); }, c->synthPreset)
                .showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetChip));
    };
    addAndMakeVisible (presetChip);

    previousButton.setTooltip ("The previous factory sound");
    previousButton.onClick = [this] { stepPreset (-1); };
    nextButton.setTooltip ("The next factory sound");
    nextButton.onClick = [this] { stepPreset (1); };

    for (auto* b : { &previousButton, &nextButton })
        addAndMakeVisible (b);

    addButton.setLabel ("Add a Nova synth");
    addButton.setFramed (true);
    addButton.onClick = [this]
    {
        if (onNeedChannel != nullptr)
            setChannel (onNeedChannel());
    };
    addChildComponent (addButton);

    pageTabs.onChange = [this] (int index) { showPage (index); };
    addAndMakeVisible (pageTabs);

    song.addListener (this);
    startTimerHz (30);
    refresh();
}

SynthPanel::~SynthPanel()
{
    song.removeListener (this);
    stopTimer();
    sections.clear();
    paramControls.clear();
}

Channel* SynthPanel::channel() const
{
    auto c = song.findChannel (channelId);
    return c != nullptr && c->type == InstrumentType::synth ? c : nullptr;
}

NovaPlugin* SynthPanel::nova() const
{
    return channel() != nullptr ? audio.getNova (channelId) : nullptr;
}

void SynthPanel::setChannel (int id)
{
    channelId = id;
    bindControls();
    refresh();
}

//==============================================================================
void SynthPanel::bindControls()
{
    auto* current = nova();

    if (current == bound.get())
        return;

    // Controls first: they hold on to the old plugin's parameters
    for (auto* s : sections)
        removeChildComponent (s);

    pages.clear();
    sections.clear();
    paramControls.clear();
    bound = current;

    if (current == nullptr)
    {
        resized();
        repaint();
        return;
    }

    auto& fx = *current;
    Section* section = nullptr;
    auto value = [&fx] (const juce::String& id) { return fx.findParam (id)->get(); };

    auto adopt = [this, &section] (ParamControl* c) -> juce::Component*
    {
        paramControls.add (c);
        section->addAndMakeVisible (c);
        return c;
    };

    auto knob = [&] (const juce::String& id, const juce::String& label = {}) { return adopt (new ParamKnob (*fx.findParam (id), section->colour, label)); };
    auto menu = [&] (const juce::String& id, const juce::String& caption) { return adopt (new ParamMenu (*fx.findParam (id), caption, nova::findInfo (id)->choiceNames())); };
    auto toggle = [&] (const juce::String& id, const juce::String& label) { return adopt (new ParamToggle (*fx.findParam (id), section->colour, label)); };
    auto choice = [&] (const juce::String& id, const juce::StringArray& labels = {})
    {
        return adopt (new ParamChoice (*fx.findParam (id), labels.isEmpty() ? nova::findInfo (id)->choiceNames() : labels));
    };
    auto field = [&] (const juce::String& id, const juce::String& caption) { return adopt (new ParamValue (*fx.findParam (id), caption)); };

    auto begin = [&] (size_t pageIndex, const juce::String& title, juce::Colour colour, float weight, int displayHeight = 0)
    {
        section = sections.add (new Section (title, colour));
        section->weight = weight;
        section->displayHeight = displayHeight;

        if (pages.size() <= pageIndex)
            pages.resize (pageIndex + 1);

        pages[pageIndex].push_back (section);
        addChildComponent (section);
    };

    auto envelope = [&] (const juce::String& prefix)
    {
        section->rows.push_back ({ knob (prefix + "_attack"), knob (prefix + "_decay"), knob (prefix + "_sustain"), knob (prefix + "_release") });
        section->paintDisplay = [value, prefix, colour = section->colour] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            drawEnvelope (g, r, value (prefix + "_attack"), value (prefix + "_decay"), value (prefix + "_sustain"), value (prefix + "_release"), colour);
        };
    };

    // Oscillators
    for (int o = 1; o <= 2; ++o)
    {
        const juce::String p = "osc" + juce::String (o) + "_";
        begin (0, "Osc " + juce::String (o), o == 1 ? osc1Colour : osc2Colour, 1.3f, 56);
        section->rows = { { knob (p + "shape"), knob (p + "pw", "PW"), knob (p + "level"), knob (p + "unison"), knob (p + "detune") },
                          { knob (p + "octave"), knob (p + "semi", "Semi"), knob (p + "fine"), knob (p + "width", "Spread") } };
        section->paintDisplay = [value, p, colour = section->colour] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            drawWave (g, r, value (p + "shape"), value (p + "pw"), juce::roundToInt (value (p + "unison")), value (p + "detune"), value (p + "level"), colour);
        };
    }

    begin (0, "Mix", mixColour, 0.6f);
    section->rows = { { knob ("fm"), knob ("sub_level") }, { knob ("noise_level"), knob ("noise_tone", "Tone") } };

    // Filter and envelopes
    begin (1, "Filter", filterColour, 1.45f, 80);
    section->rows = { { choice ("filter_mode", { "Low", "Band", "High", "Notch" }) },
                      { choice ("filter_slope", { "12 dB/oct", "24 dB/oct" }) },
                      { knob ("filter_cutoff"), knob ("filter_reso", "Reso"), knob ("filter_drive", "Drive"), knob ("filter_env", "Env"),
                        knob ("filter_keytrack", "Key"), knob ("filter_velocity", "Vel") } };
    section->paintDisplay = [value, colour = section->colour] (juce::Graphics& g, juce::Rectangle<float> r)
    {
        drawFilter (g, r, juce::roundToInt (value ("filter_mode")), value ("filter_slope") >= 0.5f, value ("filter_cutoff"), value ("filter_reso"),
                    value ("filter_env"), colour);
    };

    begin (1, "Amp envelope", envColour, 1.3f, 80);
    envelope ("amp");
    section->rows.back().push_back (knob ("amp_velocity", "Vel"));

    begin (1, "Filter envelope", filterColour, 1.1f, 80);
    envelope ("fenv");

    // Motion
    for (int l = 1; l <= 2; ++l)
    {
        const juce::String p = "lfo" + juce::String (l) + "_";
        begin (2, "LFO " + juce::String (l), lfoColour, 1.0f, 64);
        section->rows = { { menu (p + "shape", "Shape"), menu (p + "division", "Note") },
                          { toggle (p + "sync", "Sync to tempo"), toggle (p + "retrigger", "Each note") },
                          { knob (p + "rate"), knob (p + "fade", "Fade in") } };
        section->paintDisplay = [value, p, colour = section->colour] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            drawLfo (g, r, juce::roundToInt (value (p + "shape")), value (p + "fade"), colour);
        };
    }

    begin (2, "Mod envelope", envColour, 1.0f, 80);
    envelope ("env3");

    // The mod matrix, and how notes play
    begin (3, "Mod matrix", lfoColour, 2.0f);

    for (int m = 1; m <= 6; ++m)
    {
        const juce::String p = "mod" + juce::String (m) + "_";
        section->rows.push_back ({ menu (p + "source", juce::String (m)), menu (p + "dest", "to"), field (p + "amount", "Amount") });
    }

    begin (3, "Voice", osc1Colour, 1.0f);
    section->rows = { { choice ("voice_mode") }, { knob ("polyphony"), knob ("glide"), knob ("bend_range", "Bend") } };

    // Effects
    begin (4, "Output", fxColour, 0.6f);
    section->rows = { { knob ("drive") }, { knob ("volume") } };

    begin (4, "Chorus", fxColour, 0.9f);
    section->rows = { { knob ("chorus_mix", "Mix"), knob ("chorus_rate"), knob ("chorus_depth") } };

    begin (4, "Delay", fxColour, 1.2f);
    section->rows = { { menu ("delay_time", "Time"), toggle ("delay_pingpong", "Ping-pong") },
                      { knob ("delay_mix", "Mix"), knob ("delay_feedback"), knob ("delay_tone") } };

    begin (4, "Reverb", fxColour, 1.1f);
    section->rows = { { knob ("reverb_mix", "Mix"), knob ("reverb_size"), knob ("reverb_decay"), knob ("reverb_tone") } };

    resized();
    repaint();
}

juce::Component* SynthPanel::findControl (const juce::String& paramId) const
{
    for (auto* c : paramControls)
        if (c->getComponentID() == paramId)
            return c;

    return nullptr;
}

void SynthPanel::showPage (int index)
{
    page = juce::jlimit (0, pageNames().size() - 1, index);
    pageTabs.setSelected (page);
    layoutPage();
}

void SynthPanel::layoutPage()
{
    for (auto* s : sections)
        s->setVisible (false);

    if (bound == nullptr || (size_t) page >= pages.size() || pageArea.isEmpty())
        return;

    auto& shown = pages[(size_t) page];
    float total = 0.0f;

    for (auto* s : shown)
        total += s->weight;

    auto area = pageArea;
    const int gap = 10, width = area.getWidth() - gap * ((int) shown.size() - 1);

    for (auto* s : shown)
    {
        s->setBounds (area.removeFromLeft (juce::roundToInt (width * s->weight / total)));
        area.removeFromLeft (gap);
        s->setVisible (true);
    }
}

//==============================================================================
void SynthPanel::keepState()
{
    // The knobs turned since: into the channel, so undo goes back to them
    if (auto n = nova())
        if (auto c = channel())
        {
            n->flushPluginStateToValueTree();
            c->pluginState = n->state.toXmlString();
        }
}

void SynthPanel::play (Channel& c, const nova::Patch& patch)
{
    // A channel named after its sound keeps being named after it
    const bool namedAfterSound = c.name == c.synthPreset;
    audio.applySynthPatch (c.id, patch);

    if (namedAfterSound && patch.name.isNotEmpty())
        c.name = patch.name;
}

void SynthPanel::choosePreset (const juce::String& name)
{
    if (channel() == nullptr && onNeedChannel != nullptr)
        channelId = onNeedChannel();

    auto c = channel();
    auto patch = nova::findFactory (name);

    if (c == nullptr || patch == nullptr)
        return;

    keepState();
    song.checkpoint();
    play (*c, *patch);
    song.notify (Song::Change::channels);
    bindControls();
    refresh();
}

void SynthPanel::stepPreset (int delta)
{
    auto c = channel();

    if (c == nullptr)
        return;

    const auto names = nova::factoryNames();
    const int index = names.indexOf (c->synthPreset);
    const int next = index < 0 ? (delta > 0 ? 0 : names.size() - 1) : (index + delta + names.size()) % names.size();
    choosePreset (names[next]);
}

//==============================================================================
void SynthPanel::showChannelMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Show the synth on");

    for (auto& c : song.channels)
    {
        if (c.type != InstrumentType::synth)
            continue;

        juce::PopupMenu::Item item (c.name + "  (" + c.synthPreset + ")");
        item.colour = c.colour;
        item.isTicked = c.id == channelId;
        item.action = [this, id = c.id] { setChannel (id); };
        menu.addItem (item);
    }

    menu.addSeparator();
    menu.addItem ("A new Nova synth", onNeedChannel != nullptr, false, [this] { setChannel (onNeedChannel()); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&channelChip));
}

void SynthPanel::refresh()
{
    auto c = channel();
    const bool hasNova = bound != nullptr;

    channelChip.setLabel (c != nullptr ? c->name : juce::String ("No synth"));
    channelChip.setSwatch (c != nullptr ? c->colour : textFaint);
    presetChip.setLabel (c != nullptr ? c->synthPreset : juce::String ("Pick a sound"));
    presetChip.setEnabled (c != nullptr);
    previousButton.setEnabled (c != nullptr);
    nextButton.setEnabled (c != nullptr);
    pageTabs.setVisible (hasNova);
    addButton.setVisible (c == nullptr);
    resized();
    repaint();
}

//==============================================================================
void SynthPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    auto c = channel();

    // The sound's category beside its name
    if (c != nullptr)
        if (auto factory = nova::findFactory (c->synthPreset))
        {
            g.setColour (textFaint);
            g.setFont (font (15.0f));
            g.drawText (factory->category, nextButton.getBounds().translated (nextButton.getWidth() + 8, 0).withWidth (120),
                        juce::Justification::centredLeft, true);
        }

    // Nothing to show yet
    if (! emptyArea.isEmpty())
    {
        g.setColour (textDim);
        g.setFont (font (17.0f));
        g.drawFittedText (c == nullptr ? "No synth channel yet: add one, then pick a sound."
                                       : "This channel plays NightShift's old synth. Pick a sound above to switch it to Nova.",
                          emptyArea.withTrimmedBottom (emptyArea.getHeight() / 2), juce::Justification::centredBottom, 2);
    }
}

void SynthPanel::resized()
{
    auto r = getLocalBounds();

    // The sound's name and the pages along the top
    auto header = r.removeFromTop (54).reduced (16, 11);
    channelChip.setBounds (header.removeFromLeft (juce::jlimit (110, 220, channelChip.getIdealWidth())));
    header.removeFromLeft (14);
    previousButton.setBounds (header.removeFromLeft (32));
    presetChip.setBounds (header.removeFromLeft (juce::jlimit (150, 260, presetChip.getIdealWidth())));
    nextButton.setBounds (header.removeFromLeft (32));
    pageTabs.setBounds (header.removeFromRight (std::min (header.getWidth() - 120, pageTabs.getIdealWidth())));

    // The pages
    pageArea = r.reduced (12, 8);
    emptyArea = bound == nullptr ? pageArea : juce::Rectangle<int>();
    addButton.setBounds (emptyArea.isEmpty() ? juce::Rectangle<int>() : emptyArea.withTrimmedTop (emptyArea.getHeight() / 2 + 12).withHeight (36).withSizeKeepingCentre (190, 36));
    layoutPage();
}

//==============================================================================
void SynthPanel::songChanged (Song::Change change)
{
    if (change == Song::Change::channels || change == Song::Change::everything)
    {
        bindControls();
        refresh();
    }
}

void SynthPanel::timerCallback()
{
    bindControls();

    if (bound != nullptr && (size_t) page < pages.size())
    {
        auto* n = static_cast<NovaPlugin*> (bound.get());
        auto value = [n] (const juce::String& id) { return n->findParam (id)->get(); };

        for (auto* s : pages[(size_t) page])
            for (auto& row : s->rows)
                for (auto* control : row)
                    if (auto* pc = dynamic_cast<ParamControl*> (control))
                    {
                        // What does nothing as things are set is dimmed
                        const auto& id = pc->param.id;
                        const auto prefix = id.upToFirstOccurrenceOf ("_", true, false);
                        bool idle = false;

                        if (id.endsWith ("_pw"))
                            idle = value (prefix + "shape") < 2.5f;
                        else if (id.endsWith ("_detune") || id.endsWith ("_width"))
                            idle = value (prefix + "unison") < 1.5f;
                        else if (id.endsWith ("_rate") && id.startsWith ("lfo"))
                            idle = value (prefix + "sync") >= 0.5f;
                        else if (id.endsWith ("_division"))
                            idle = value (prefix + "sync") < 0.5f;
                        else if (id == "noise_tone")
                            idle = value ("noise_level") < 0.0005f;

                        if (pc->dimmed != idle)
                            pc->setDimmed (idle);

                        pc->refresh();
                    }

        for (auto* s : pages[(size_t) page])
            if (s->displayHeight > 0)
                s->repaint (s->displayBounds());
    }
}

} // namespace nightshift
