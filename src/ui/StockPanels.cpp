// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "StockPanels.h"
#include "ComputerKeyboard.h"
#include "Theme.h"
#include "Widgets.h"
#include "ParamControls.h"
#include "TextAudit.h"

namespace nightshift
{

using namespace theme;
using namespace controls;

namespace
{
    //==============================================================================
    /** What every stock panel has: its name and what it does along the top, a display, and
        controls kept up to date at 30 Hz. Reset puts every parameter back to its default. */
    class StockPanel : public juce::Component,
                       private juce::Timer
    {
    public:
        StockPanel (StockEffect& effect, juce::Colour c)
            : fx (effect), colour (c)
        {
            resetButton.setFilled (false);
            resetButton.setFramed (true);
            resetButton.setLabel ("Reset");
            resetButton.setTooltip ("Put every setting back to its default");
            resetButton.onClick = [this] { fx.resetParams(); };
            addAndMakeVisible (resetButton);
            startTimerHz (30);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (panel);

            auto top = getLocalBounds().reduced (18, 0).removeFromTop (58);
            g.setColour (colour);
            g.fillRoundedRectangle ((float) top.getX(), 17.0f, 4.0f, 28.0f, 2.0f);
            g.setColour (text);
            g.setFont (font (20.0f, true));
            g.drawText (fx.getName().fromFirstOccurrenceOf ("Nightshift ", false, false), top.withTrimmedLeft (14).withHeight (34).withY (12),
                        juce::Justification::centredLeft, true);
            g.setColour (textFaint);
            g.setFont (font (15.0f));
            g.drawText (stockfx::blurb (fx.getPluginType()), top.withTrimmedLeft (14).withTrimmedRight (110).withHeight (18).withY (36),
                        juce::Justification::centredLeft, true);

            if (! display.isEmpty())
            {
                auto d = display.toFloat();
                g.setColour (background);
                g.fillRoundedRectangle (d, 10.0f);

                {
                    juce::Graphics::ScopedSaveState state (g);
                    g.reduceClipRegion (display.reduced (1));
                    paintDisplay (g, d);
                }

                g.setColour (hairline);
                g.drawRoundedRectangle (d.reduced (0.5f), 10.0f, 1.0f);
            }
        }

        void resized() override
        {
            resetButton.setBounds (getWidth() - 18 - 92, 16, 92, 32);
            layout (getLocalBounds().reduced (18, 0).withTrimmedTop (66).withTrimmedBottom (18));
        }

    protected:
        StockEffect& fx;
        juce::Colour colour;
        juce::Rectangle<int> display;
        juce::OwnedArray<ParamControl> controls;
        IconButton resetButton { "Reset", icons::loop() };

        virtual void layout (juce::Rectangle<int> area) = 0;
        virtual void paintDisplay (juce::Graphics&, juce::Rectangle<float>) {}
        virtual void tick() {}

        StockEffect::Param& param (const char* id)          { return *fx.findParam (id); }
        float value (const char* id)                        { return fx.findParam (id)->get(); }

        ParamKnob& knob (const char* id, const juce::String& label = {})
        {
            return *static_cast<ParamKnob*> (adopt (new ParamKnob (param (id), colour, label)));
        }

        ParamValue& field (const juce::String& id, const juce::String& caption)
        {
            return *static_cast<ParamValue*> (adopt (new ParamValue (*fx.findParam (id), caption)));
        }

        ParamToggle& toggle (const juce::String& id, const juce::String& label)
        {
            return *static_cast<ParamToggle*> (adopt (new ParamToggle (*fx.findParam (id), colour, label)));
        }

        ParamChoice& choice (const char* id, const juce::StringArray& options)
        {
            return *static_cast<ParamChoice*> (adopt (new ParamChoice (param (id), options)));
        }

        /** Spreads controls evenly across an area. */
        static void row (const std::vector<juce::Component*>& items, juce::Rectangle<int> area)
        {
            if (items.empty())
                return;

            const int w = area.getWidth() / (int) items.size();

            for (auto* c : items)
                c->setBounds (area.removeFromLeft (w));
        }

        // The display's frequency axis, 20 Hz to 20 kHz
        static float freqToX (double f, juce::Rectangle<float> d)
        {
            return d.getX() + d.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0));
        }

        static double xToFreq (float x, juce::Rectangle<float> d)
        {
            return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) ((x - d.getX()) / d.getWidth())));
        }

        static void paintFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> d)
        {
            g.setFont (font (15.0f));

            for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            {
                const float x = freqToX (f, d);
                g.setColour (hairline);
                g.fillRect (x, d.getY(), 1.0f, d.getHeight());
                g.setColour (textFaint);
                g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f),
                            juce::Rectangle<float> (x + 3.0f, d.getBottom() - 21.0f, 44.0f, 18.0f), juce::Justification::centredLeft, true);
            }
        }

    private:
        ParamControl* adopt (ParamControl* c)
        {
            controls.add (c);
            addAndMakeVisible (c);
            return c;
        }

        void timerCallback() override
        {
            for (auto* c : controls)
                c->refresh();

            tick();
            repaint (display);
        }
    };

    //==============================================================================
    class EqPanel : public StockPanel
    {
    public:
        explicit EqPanel (NightEQ& e) : StockPanel (e, accentCool), eq (e)
        {
            const char* const captions[] = { "Freq", "Gain", "Q" };

            for (int b = 0; b < NightEQ::numBands; ++b)
            {
                auto& col = columns[b];

                for (int f = 0; f < 3; ++f)
                    if (auto id = NightEQ::paramId (b, f == 0 ? "Freq" : f == 1 ? "Gain" : "Q"); id.isNotEmpty())
                        col.fields.push_back (&field (id, captions[f]));

                if (auto id = NightEQ::paramId (b, "On"); id.isNotEmpty())
                    col.on = &toggle (id, "On");

                if (auto id = NightEQ::paramId (b, "Steep"); id.isNotEmpty())
                    col.steep = &toggle (id, "24 dB");
            }

            output = &knob ("output", "Output");
            fft = std::make_unique<juce::dsp::FFT> (fftOrder);
            window.resize (fftSize);
            juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), fftSize, juce::dsp::WindowingFunction<float>::hann, false);
            spectrum.assign (fftSize / 2, -120.0f);
            setSize (820, 520);
        }

        void layout (juce::Rectangle<int> area) override
        {
            display = area.removeFromTop (280);
            area.removeFromTop (14);

            auto outputColumn = area.removeFromRight (86);
            output->setBounds (outputColumn.withTrimmedTop (22).withHeight (96));
            area.removeFromRight (8);

            const int w = area.getWidth() / NightEQ::numBands;

            for (int b = 0; b < NightEQ::numBands; ++b)
            {
                auto col = area.removeFromLeft (w).reduced (3, 0);
                columns[b].label = col.removeFromTop (20);

                for (auto* f : columns[b].fields)
                {
                    f->setBounds (col.removeFromTop (26));
                    col.removeFromTop (4);
                }

                if (columns[b].on != nullptr)
                {
                    columns[b].on->setBounds (col.removeFromTop (26));
                    col.removeFromTop (4);
                }

                if (columns[b].steep != nullptr)
                    columns[b].steep->setBounds (col.removeFromTop (26));
            }
        }

        void paint (juce::Graphics& g) override
        {
            StockPanel::paint (g);

            for (int b = 0; b < NightEQ::numBands; ++b)
            {
                g.setColour (bandColour (b));
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ columns[b].label.getX() + 5.0f, (float) columns[b].label.getCentreY() }));
                g.setColour (textDim);
                g.setFont (labelFont());
                g.drawText (NightEQ::bandName (b).toUpperCase(), columns[b].label.withTrimmedLeft (14), juce::Justification::centredLeft, true);
            }
        }

        void paintDisplay (juce::Graphics& g, juce::Rectangle<float> d) override
        {
            paintFrequencyGrid (g, d);

            for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
            {
                g.setColour (db == 0.0f ? hairlineStrong : hairline);
                g.fillRect (d.getX(), gainToY (db, d), d.getWidth(), 1.0f);
                g.setColour (textFaint);
                g.setFont (font (15.0f));
                g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (d.getRight() - 40.0f, gainToY (db, d) - 18.0f, 36.0f, 17.0f),
                            juce::Justification::centredRight, true);
            }

            // The analyser behind: what comes out, tilted 3 dB an octave like the ear hears it
            juce::Path spectrumPath;
            spectrumPath.startNewSubPath (d.getX(), d.getBottom());
            const double rate = eq.getSampleRate();

            for (float x = d.getX(); x <= d.getRight(); x += 2.0f)
            {
                const double f = xToFreq (x, d);
                const int bin = juce::jlimit (1, fftSize / 2 - 1, (int) (f * fftSize / rate));
                const float db = spectrum[(size_t) bin] + 3.0f * (float) std::log2 (f / 1000.0);
                spectrumPath.lineTo (x, juce::jmap (juce::jlimit (-90.0f, 0.0f, db), -90.0f, 0.0f, d.getBottom(), d.getY() + d.getHeight() * 0.15f));
            }

            spectrumPath.lineTo (d.getRight(), d.getBottom());
            spectrumPath.closeSubPath();
            g.setColour (colour.withAlpha (0.10f));
            g.fillPath (spectrumPath);
            g.setColour (colour.withAlpha (0.25f));
            g.strokePath (spectrumPath, juce::PathStrokeType (1.0f));

            // Each band faintly, then the whole curve
            for (int b = 0; b < NightEQ::numBands; ++b)
            {
                if (! eq.settingsFor (b).on || (b != NightEQ::highPass && b != NightEQ::lowPass && std::abs (eq.settingsFor (b).gainDb) < 0.05))
                    continue;

                juce::Path band;

                for (float x = d.getX(); x <= d.getRight(); x += 2.0f)
                {
                    const float y = gainToY ((float) eq.bandResponseDb (b, xToFreq (x, d)), d);
                    x == d.getX() ? band.startNewSubPath (x, y) : band.lineTo (x, y);
                }

                g.setColour (bandColour (b).withAlpha (0.35f));
                g.strokePath (band, juce::PathStrokeType (1.2f));
            }

            juce::Path curve;

            for (float x = d.getX(); x <= d.getRight(); x += 1.5f)
            {
                const float y = gainToY ((float) eq.responseDb (xToFreq (x, d)), d);
                x == d.getX() ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
            }

            g.setColour (text);
            g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // The handles
            for (int b = 0; b < NightEQ::numBands; ++b)
            {
                const auto p = handlePosition (b, d);
                const bool on = eq.settingsFor (b).on;
                auto dot = juce::Rectangle<float> (18.0f, 18.0f).withCentre (p);
                g.setColour (on ? bandColour (b) : background);
                g.fillEllipse (dot);
                g.setColour (on ? bandColour (b).brighter (0.4f) : bandColour (b).withAlpha (0.6f));
                g.drawEllipse (dot, b == dragging ? 2.4f : 1.4f);
                g.setColour (on ? background : bandColour (b));
                g.setFont (font (15.0f, true));
                g.drawText (juce::String (b + 1), dot, juce::Justification::centred, true);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = handleAt (e.position);

            if (dragging >= 0 && (dragging == NightEQ::highPass || dragging == NightEQ::lowPass) && ! eq.settingsFor (dragging).on)
                eq.findParam (NightEQ::paramId (dragging, "On"))->set (1.0f);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging < 0)
                return;

            auto d = display.toFloat();
            eq.findParam (NightEQ::paramId (dragging, "Freq"))->set ((float) xToFreq (e.position.x, d));

            if (auto gain = eq.findParam (NightEQ::paramId (dragging, "Gain")))
                gain->set (juce::jlimit (-18.0f, 18.0f, yToGain (e.position.y, d)));
        }

        void mouseUp (const juce::MouseEvent&) override     { dragging = -1; }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            // A bell or shelf back to flat; a high or low pass on or off
            const int b = handleAt (e.position);

            if (auto gain = b >= 0 ? eq.findParam (NightEQ::paramId (b, "Gain")) : nullptr)
                gain->set (0.0f);
            else if (auto on = b >= 0 ? eq.findParam (NightEQ::paramId (b, "On")) : nullptr)
                on->set (on->get() >= 0.5f ? 0.0f : 1.0f);
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            // Over a bell: narrower or wider
            const int b = handleAt (e.position);

            if (auto q = b >= 0 ? eq.findParam (NightEQ::paramId (b, "Q")) : nullptr)
                q->set (q->range.snapToLegalValue (q->get() * (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f)));
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            setMouseCursor (handleAt (e.position) >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        }

        void tick() override
        {
            std::vector<float> data ((size_t) fftSize * 2, 0.0f);
            eq.copyAnalysis (data.data(), fftSize);

            for (int i = 0; i < fftSize; ++i)
                data[(size_t) i] *= window[(size_t) i];

            fft->performFrequencyOnlyForwardTransform (data.data());

            // Up at once, down gently
            const float scale = 4.0f / fftSize;

            for (int i = 0; i < fftSize / 2; ++i)
            {
                const float db = juce::Decibels::gainToDecibels (data[(size_t) i] * scale, -120.0f);
                spectrum[(size_t) i] = std::max (db, spectrum[(size_t) i] - 1.2f);
            }
        }

    private:
        struct Column
        {
            juce::Rectangle<int> label;
            std::vector<ParamValue*> fields;
            ParamToggle* on = nullptr;
            ParamToggle* steep = nullptr;
        };

        static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;

        NightEQ& eq;
        Column columns[NightEQ::numBands];
        ParamKnob* output = nullptr;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window, spectrum;
        int dragging = -1;

        static juce::Colour bandColour (int b)
        {
            const juce::uint32 colours[] = { 0xff8f9bff, 0xffffb054, 0xff5fd0c5, 0xffff7aa8, 0xff9be08a, 0xffd38bff, 0xff8f9bff };
            return juce::Colour (colours[b]);
        }

        static float gainToY (float db, juce::Rectangle<float> d)
        {
            return juce::jmap (db, -20.0f, 20.0f, d.getBottom() - 8.0f, d.getY() + 8.0f);
        }

        static float yToGain (float y, juce::Rectangle<float> d)
        {
            return juce::jmap (y, d.getBottom() - 8.0f, d.getY() + 8.0f, -20.0f, 20.0f);
        }

        juce::Point<float> handlePosition (int b, juce::Rectangle<float> d) const
        {
            const auto s = eq.settingsFor (b);
            const bool pass = b == NightEQ::highPass || b == NightEQ::lowPass;
            return { freqToX (s.freq, d), gainToY (pass ? 0.0f : (float) s.gainDb, d) };
        }

        int handleAt (juce::Point<float> p) const
        {
            auto d = display.toFloat();

            for (int b = NightEQ::numBands; --b >= 0;)
                if (handlePosition (b, d).getDistanceFrom (p) < 12.0f)
                    return b;

            return -1;
        }
    };

    //==============================================================================
    class CompressorPanel : public StockPanel
    {
    public:
        CompressorPanel (NightCompressor& c, Song& s) : StockPanel (c, accent), comp (c), song (s)
        {
            for (auto id : { "threshold", "ratio", "knee", "attack", "release", "makeup", "mix" })
                knobs.push_back (&knob (id));

            autoMakeup = &toggle ("autoMakeup", "Auto makeup");
            highPass = &field ("scHighPass", "Listen above");

            sidechainChip.setTooltip ("Duck to another channel: the compressor listens to it instead of to what it's on");
            sidechainChip.onClick = [this] { showSidechainMenu(); };
            addAndMakeVisible (sidechainChip);
            setSize (700, 470);
        }

        void layout (juce::Rectangle<int> area) override
        {
            display = area.removeFromTop (220);
            area.removeFromTop (14);
            row (knobs, area.removeFromTop (106));
            area.removeFromTop (12);

            auto bottom = area.removeFromTop (30);
            autoMakeup->setBounds (bottom.removeFromLeft (150));
            bottom.removeFromLeft (12);
            sidechainChip.setBounds (bottom.removeFromLeft (230));
            bottom.removeFromLeft (12);
            highPass->setBounds (bottom.removeFromLeft (170));
        }

        void paintDisplay (juce::Graphics& g, juce::Rectangle<float> d) override
        {
            // The curve: input against output, -60 to 0 dB
            auto curveArea = d.reduced (14.0f).removeFromLeft (d.getHeight() - 28.0f);
            auto toPoint = [curveArea] (float inDb, float outDb)
            {
                return juce::Point<float> (juce::jmap (inDb, -60.0f, 0.0f, curveArea.getX(), curveArea.getRight()),
                                           juce::jmap (juce::jlimit (-60.0f, 6.0f, outDb), -60.0f, 0.0f, curveArea.getBottom(), curveArea.getY()));
            };

            g.setColour (hairline);

            for (float db = -60.0f; db <= 0.0f; db += 12.0f)
            {
                g.fillRect (toPoint (db, -60.0f).x, curveArea.getY(), 1.0f, curveArea.getHeight());
                g.fillRect (curveArea.getX(), toPoint (-60.0f, db).y, curveArea.getWidth(), 1.0f);
            }

            g.setColour (hairlineStrong);
            g.drawLine ({ toPoint (-60.0f, -60.0f), toPoint (0.0f, 0.0f) }, 1.0f);

            const float t = value ("threshold"), r = value ("ratio"), k = value ("knee"), makeup = comp.makeupDb();
            juce::Path curve;

            for (float in = -60.0f; in <= 0.0f; in += 0.5f)
            {
                const auto p = toPoint (in, NightCompressor::curveDb (in, t, r, k) + makeup);
                in == -60.0f ? curve.startNewSubPath (p) : curve.lineTo (p);
            }

            g.setColour (colour);
            g.strokePath (curve, juce::PathStrokeType (2.2f));

            if (inputDb > -59.0f)
            {
                const auto p = toPoint (inputDb, NightCompressor::curveDb (inputDb, t, r, k) + makeup);
                g.setColour (text);
                g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (p));
            }

            g.setColour (textFaint);
            g.setFont (font (15.0f));
            g.drawText ("Threshold " + juce::String (t, 1) + " dB", curveArea.withHeight (16.0f).translated (6.0f, 2.0f), juce::Justification::centredLeft, true);

            // Meters: in, gain reduction (down from the top), out; then the last few seconds
            auto meters = d.reduced (14.0f).withTrimmedLeft (curveArea.getWidth() + 30.0f).removeFromLeft (180.0f);
            const float w = std::min (40.0f, meters.getWidth() / 3.0f - 12.0f);
            const juce::String names[] = { "IN", "GR", "OUT" };
            const float values[] = { inputDb, reductionDb, outputDb };

            for (int i = 0; i < 3; ++i)
            {
                auto bar = meters.removeFromLeft (w + 18.0f).withWidth (w).withTrimmedBottom (18.0f);
                g.setColour (panelLight);
                g.fillRoundedRectangle (bar, 4.0f);

                if (i == 1)
                {
                    const float h = bar.getHeight() * juce::jlimit (0.0f, 1.0f, values[i] / 24.0f);
                    g.setColour (accent);
                    g.fillRoundedRectangle (bar.withHeight (h), 4.0f);
                }
                else
                {
                    const float h = bar.getHeight() * juce::jlimit (0.0f, 1.0f, (values[i] + 60.0f) / 60.0f);
                    g.setColour (ledOn);
                    g.fillRoundedRectangle (bar.withTrimmedTop (bar.getHeight() - h), 4.0f);
                }

                g.setColour (textFaint);
                g.setFont (labelFont());
                g.drawText (names[i], bar.withY (bar.getBottom() + 2.0f).withHeight (16.0f), juce::Justification::centred, true);
                g.setColour (textDim);
                g.setFont (monoFont (15.0f, true));
                const juce::String readout = i == 1 ? juce::String (values[i], 1) : values[i] <= -59.0f ? juce::String ("-") : juce::String (values[i], 0);
                g.drawText (readout, bar.withHeight (16.0f).translated (0.0f, 4.0f), juce::Justification::centred, true);
            }

            auto historyArea = d.reduced (14.0f).withTrimmedLeft (curveArea.getWidth() + 30.0f + 196.0f).withTrimmedBottom (18.0f);

            if (historyArea.getWidth() > 60.0f)
            {
                g.setColour (panelLight.withAlpha (0.5f));
                g.fillRoundedRectangle (historyArea, 4.0f);
                juce::Path level, reduction;
                const int n = (int) history.size();

                for (int i = 0; i < n; ++i)
                {
                    const auto& h = history[(size_t) ((historyStart + i) % n)];
                    const float x = historyArea.getX() + historyArea.getWidth() * i / (n - 1);
                    const float levelY = juce::jmap (juce::jlimit (-60.0f, 0.0f, h.first), -60.0f, 0.0f, historyArea.getBottom(), historyArea.getY());
                    const float grY = historyArea.getY() + historyArea.getHeight() * juce::jlimit (0.0f, 1.0f, h.second / 24.0f);
                    i == 0 ? level.startNewSubPath (x, levelY) : level.lineTo (x, levelY);
                    i == 0 ? reduction.startNewSubPath (x, grY) : reduction.lineTo (x, grY);
                }

                g.setColour (textFaint);
                g.strokePath (level, juce::PathStrokeType (1.2f));
                g.setColour (colour);
                g.strokePath (reduction, juce::PathStrokeType (1.8f));
                g.setColour (textFaint);
                g.setFont (labelFont());
                g.drawText ("LEVEL AND REDUCTION", historyArea.withY (historyArea.getBottom() + 2.0f).withHeight (16.0f), juce::Justification::centredLeft, true);
            }
        }

        void tick() override
        {
            // Peaks since last time; meters fall back gently
            inputDb = std::max (comp.takeInputDb(), inputDb - 1.5f);
            outputDb = std::max (comp.takeOutputDb(), outputDb - 1.5f);
            reductionDb = std::max (comp.takeReductionDb(), reductionDb - 0.8f);

            history[(size_t) historyStart] = { inputDb, reductionDb };
            historyStart = (historyStart + 1) % (int) history.size();

            auto source = song.findChannel (comp.sidechain.get());
            sidechainChip.setLabel (source != nullptr ? "Sidechain: " + source->name : juce::String ("Sidechain: off"));
            sidechainChip.setSwatch (source != nullptr ? source->colour : juce::Colours::transparentBlack);
        }

    private:
        NightCompressor& comp;
        Song& song;
        std::vector<juce::Component*> knobs;
        ParamToggle* autoMakeup = nullptr;
        ParamValue* highPass = nullptr;
        Chip sidechainChip { "Sidechain: off" };
        float inputDb = -100.0f, outputDb = -100.0f, reductionDb = 0.0f;
        std::vector<std::pair<float, float>> history = std::vector<std::pair<float, float>> (150, { -100.0f, 0.0f });
        int historyStart = 0;

        void showSidechainMenu()
        {
            juce::PopupMenu menu;
            menu.addSectionHeader ("Listen to");
            menu.addItem ("Its own input", true, comp.sidechain.get() == 0, [this] { setSidechain (0); });

            for (auto& c : song.channels)
            {
                juce::PopupMenu::Item item (c.name);
                item.colour = c.colour;
                item.isTicked = comp.sidechain.get() == c.id;
                item.action = [this, id = c.id] { setSidechain (id); };
                menu.addItem (item);
            }

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sidechainChip));
        }

        void setSidechain (int channelId)
        {
            comp.sidechain = channelId;
            song.notify (Song::Change::mixer);
        }
    };

    //==============================================================================
    class ReverbPanel : public StockPanel
    {
    public:
        explicit ReverbPanel (NightReverb& r) : StockPanel (r, effectColour)
        {
            for (auto id : { "preDelay", "size", "decay", "damping", "lowCut", "width", "mix" })
                knobs.push_back (&knob (id));

            struct Preset { const char* name; float size, decay, damping, preDelay, lowCut; };
            static const Preset presets[] = { { "Room", 0.35f, 0.8f, 7000.0f, 5.0f, 150.0f },
                                              { "Plate", 0.55f, 1.8f, 11000.0f, 0.0f, 200.0f },
                                              { "Hall", 0.8f, 3.2f, 6000.0f, 25.0f, 120.0f },
                                              { "Cathedral", 1.0f, 7.0f, 4500.0f, 40.0f, 90.0f } };

            for (auto& p : presets)
            {
                auto* chip = presetChips.add (new juce::TextButton (p.name));
                chip->setTooltip ("A starting point: size, decay, damping, pre-delay and low cut for a " + juce::String (p.name).toLowerCase());
                chip->onClick = [this, p]
                {
                    param ("size").set (p.size);
                    param ("decay").set (p.decay);
                    param ("damping").set (p.damping);
                    param ("preDelay").set (p.preDelay);
                    param ("lowCut").set (p.lowCut);
                };
                addAndMakeVisible (chip);
            }

            setSize (680, 430);
        }

        void layout (juce::Rectangle<int> area) override
        {
            auto presetRow = area.removeFromTop (30);

            for (auto* chip : presetChips)
            {
                chip->setBounds (presetRow.removeFromLeft (juce::GlyphArrangement::getStringWidthInt (font (16.0f, true), chip->getButtonText()) + 32));
                presetRow.removeFromLeft (8);
            }

            area.removeFromTop (12);
            display = area.removeFromTop (170);
            area.removeFromTop (14);
            row (knobs, area.removeFromTop (106));
        }

        void paintDisplay (juce::Graphics& g, juce::Rectangle<float> d) override
        {
            // The tail over time: pre-delay, then the decay (highs faster with more damping), as
            // level so it curves the way it sounds
            const float decay = value ("decay"), preDelay = value ("preDelay") * 0.001f;
            const float span = std::max (1.0f, (decay + preDelay) * 1.15f);
            auto area = d.reduced (14.0f, 12.0f);

            auto envelope = [&] (float seconds, float rt60)
            {
                juce::Path p;
                p.startNewSubPath (area.getX(), area.getBottom());

                for (float x = area.getX(); x <= area.getRight(); x += 2.0f)
                {
                    const float t = (x - area.getX()) / area.getWidth() * span - seconds;
                    const float db = t < 0.0f ? -60.0f : -60.0f * t / rt60;
                    const float level = std::pow (juce::Decibels::decibelsToGain (db), 0.6f);
                    p.lineTo (x, juce::jmap (level, 0.0f, 1.0f, area.getBottom(), area.getY()));
                }

                p.lineTo (area.getRight(), area.getBottom());
                p.closeSubPath();
                return p;
            };

            g.setColour (hairline);

            for (float s = 0.5f; s < span; s += span > 6.0f ? 2.0f : 0.5f)
            {
                const float x = area.getX() + area.getWidth() * s / span;
                g.fillRect (x, area.getY(), 1.0f, area.getHeight());
                g.setColour (textFaint);
                g.setFont (font (15.0f));

                // Along the top, clear of the decay's fill (on it they were 1.9 to 1), and only where the whole
                // label fits before the edge
                if (x + 50.0f <= area.getRight())
                    g.drawText (juce::String (s, 1) + " s", juce::Rectangle<float> (x + 3.0f, area.getY() + 3.0f, 46.0f, 18.0f), juce::Justification::centredLeft, true);

                g.setColour (hairline);
            }

            const float highs = decay * juce::jmap (std::log (value ("damping") / 1000.0f) / std::log (18.0f), 0.0f, 1.0f, 0.35f, 0.95f);
            g.setColour (colour.withAlpha (0.25f));
            g.fillPath (envelope (preDelay, decay));
            g.setColour (colour.withAlpha (0.35f));
            g.fillPath (envelope (preDelay, highs));
            g.setColour (colour);
            g.strokePath (envelope (preDelay, decay), juce::PathStrokeType (1.6f));

            g.setColour (textDim);
            g.setFont (font (15.0f, true));
            g.drawText ("Highs", area.withTrimmedLeft (area.getWidth() * (preDelay + highs * 0.25f) / span).withHeight (16.0f).translated (6.0f, area.getHeight() * 0.32f),
                        juce::Justification::centredLeft, true);
        }

    private:
        std::vector<juce::Component*> knobs;
        juce::OwnedArray<juce::TextButton> presetChips;
    };

    //==============================================================================
    class DelayPanel : public StockPanel
    {
    public:
        DelayPanel (NightDelay& d, Song& s) : StockPanel (d, juce::Colour (0xff9be08a)), delay (d), song (s)
        {
            time = &knob ("time");

            for (auto id : { "feedback", "lowCut", "highCut", "mod", "width", "mix" })
                knobs.push_back (&knob (id));

            sync = &toggle ("sync", "In time");
            note = &field ("division", "Note");
            pingPong = &toggle ("pingPong", "Ping-pong");
            setSize (700, 440);
        }

        void layout (juce::Rectangle<int> area) override
        {
            display = area.removeFromTop (160);
            area.removeFromTop (14);

            auto switches = area.removeFromTop (30);
            sync->setBounds (switches.removeFromLeft (120));
            switches.removeFromLeft (10);
            note->setBounds (switches.removeFromLeft (150));
            switches.removeFromLeft (10);
            pingPong->setBounds (switches.removeFromLeft (130));

            area.removeFromTop (14);
            std::vector<juce::Component*> all { time };
            all.insert (all.end(), knobs.begin(), knobs.end());
            row (all, area.removeFromTop (106));
        }

        void tick() override
        {
            const bool synced = value ("sync") >= 0.5f;
            time->setDimmed (synced);
            note->setDimmed (! synced);
        }

        void paintDisplay (juce::Graphics& g, juce::Rectangle<float> d) override
        {
            // The echoes over two seconds: each the feedback quieter, crossing sides in ping-pong
            auto area = d.reduced (14.0f, 12.0f);
            const double ms = delay.delayMs (song.bpm);
            const float fb = value ("feedback");
            const bool pong = value ("pingPong") >= 0.5f;
            const float mid = area.getCentreY();

            g.setColour (hairlineStrong);
            g.fillRect (area.getX(), mid, area.getWidth(), 1.0f);

            float level = 1.0f;
            int n = 0;

            for (double t = ms; t < 2000.0 && level > 0.01f; t += ms, ++n)
            {
                const float x = area.getX() + area.getWidth() * (float) (t / 2000.0);
                const float h = (area.getHeight() * 0.5f - 4.0f) * level;
                const bool left = ! pong || n % 2 == 0;
                g.setColour (pong ? (left ? colour : accentCool) : colour);

                if (pong)
                    g.fillRoundedRectangle (x - 3.0f, left ? mid - h : mid + 1.0f, 6.0f, h, 3.0f);
                else
                    g.fillRoundedRectangle (x - 3.0f, mid - h, 6.0f, h * 2.0f, 3.0f);

                level *= fb;
            }

            g.setColour (textDim);
            g.setFont (font (15.0f, true));
            const juce::String what = value ("sync") >= 0.5f ? NightDelay::divisionName (juce::roundToInt (value ("division"))) + "  =  " : juce::String();
            g.drawText (what + juce::String (juce::roundToInt (ms)) + " ms" + (pong ? "   (left, then right)" : ""),
                        area.withHeight (18.0f), juce::Justification::centredLeft, true);
        }

    private:
        NightDelay& delay;
        Song& song;
        ParamKnob* time = nullptr;
        std::vector<juce::Component*> knobs;
        ParamToggle* sync = nullptr;
        ParamToggle* pingPong = nullptr;
        ParamValue* note = nullptr;
    };

    //==============================================================================
    class ChorusPanel : public StockPanel
    {
    public:
        explicit ChorusPanel (NightChorus& c) : StockPanel (c, juce::Colour (0xffd38bff))
        {
            for (auto id : { "rate", "depth", "delay", "feedback", "width", "mix" })
                knobs.push_back (&knob (id));

            setSize (620, 350);
        }

        void layout (juce::Rectangle<int> area) override
        {
            display = area.removeFromTop (140);
            area.removeFromTop (14);
            row (knobs, area.removeFromTop (106));
        }

        void paintDisplay (juce::Graphics& g, juce::Rectangle<float> d) override
        {
            // Two seconds of each side's sweep (the right a quarter turn behind)
            auto area = d.reduced (14.0f, 14.0f);
            const float rate = value ("rate"), depth = value ("depth");

            for (int side = 0; side < 2; ++side)
            {
                juce::Path p;

                for (float x = area.getX(); x <= area.getRight(); x += 2.0f)
                {
                    const float t = (x - area.getX()) / area.getWidth() * 2.0f;
                    const float y = area.getCentreY() - std::sin (juce::MathConstants<float>::twoPi * (rate * t - side * 0.25f)) * depth * area.getHeight() * 0.45f;
                    x == area.getX() ? p.startNewSubPath (x, y) : p.lineTo (x, y);
                }

                g.setColour (side == 0 ? colour : accentCool);
                g.strokePath (p, juce::PathStrokeType (2.0f));
            }

            g.setColour (colour);
            g.setFont (font (15.0f, true));
            g.drawText ("Left", area.withHeight (16.0f), juce::Justification::centredLeft, true);
            g.setColour (accentCool);
            g.drawText ("Right", area.withHeight (16.0f).translated (40.0f, 0.0f), juce::Justification::centredLeft, true);
        }

    private:
        std::vector<juce::Component*> knobs;
    };

    //==============================================================================
    class FilterPanel : public StockPanel
    {
    public:
        explicit FilterPanel (NightFilter& f) : StockPanel (f, juce::Colour (0xffff7aa8)), filter (f)
        {
            mode = &choice ("mode", { "Low pass", "Band pass", "High pass" });
            steep = &toggle ("steep", "24 dB/oct");

            for (auto id : { "cutoff", "resonance", "drive", "mix" })
                knobs.push_back (&knob (id));

            setSize (620, 430);
        }

        void layout (juce::Rectangle<int> area) override
        {
            auto top = area.removeFromTop (30);
            mode->setBounds (top.removeFromLeft (mode->getIdealWidth()));
            top.removeFromLeft (12);
            steep->setBounds (top.removeFromLeft (130));

            area.removeFromTop (12);
            display = area.removeFromTop (180);
            area.removeFromTop (14);
            row (knobs, area.removeFromTop (106));
        }

        void paintDisplay (juce::Graphics& g, juce::Rectangle<float> d) override
        {
            paintFrequencyGrid (g, d);
            auto toY = [d] (double db) { return responseToY (db, d); };

            g.setColour (hairlineStrong);
            g.fillRect (d.getX(), toY (0.0), d.getWidth(), 1.0f);

            juce::Path curve;

            for (float x = d.getX(); x <= d.getRight(); x += 1.5f)
            {
                const float y = toY (filter.responseDb (xToFreq (x, d)));
                x == d.getX() ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
            }

            juce::Path fill (curve);
            fill.lineTo (d.getRight(), d.getBottom());
            fill.lineTo (d.getX(), d.getBottom());
            fill.closeSubPath();
            g.setColour (colour.withAlpha (0.14f));
            g.fillPath (fill);
            g.setColour (colour);
            g.strokePath (curve, juce::PathStrokeType (2.2f));

            const auto handle = handlePosition (d);
            g.setColour (text);
            g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (handle));
            g.setColour (colour);
            g.drawEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (handle), 2.0f);
        }

        // The handle, on the curve at the cutoff: drag across for the cutoff, up and down for resonance
        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = handlePosition (display.toFloat()).getDistanceFrom (e.position) < 14.0f;
            resonanceAtStart = value ("resonance");
        }
        void mouseUp (const juce::MouseEvent&) override        { dragging = false; }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! dragging)
                return;

            auto d = display.toFloat();
            param ("cutoff").set ((float) xToFreq (e.position.x, d));
            param ("resonance").set (juce::jlimit (0.0f, 1.0f, resonanceAtStart - e.getDistanceFromDragStartY() / 160.0f));
        }

    private:
        NightFilter& filter;
        ParamChoice* mode = nullptr;
        ParamToggle* steep = nullptr;
        std::vector<juce::Component*> knobs;
        bool dragging = false;
        float resonanceAtStart = 0.0f;

        static float responseToY (double db, juce::Rectangle<float> d)
        {
            return juce::jmap ((float) juce::jlimit (-48.0, 24.0, db), -48.0f, 24.0f, d.getBottom() - 6.0f, d.getY() + 6.0f);
        }

        juce::Point<float> handlePosition (juce::Rectangle<float> d)
        {
            return { freqToX (value ("cutoff"), d), responseToY (filter.responseDb (value ("cutoff")), d) };
        }
    };
}

namespace
{
    //==============================================================================
    /** The audio interface's input level, one setting for every NAM amp (kept with the app, not the song): a field
        dragged up and down in half-dB steps (Shift finer), stepped by the wheel; double-click for 12. */
    class InterfaceLevelField : public juce::Component,
                                public juce::SettableTooltipClient
    {
    public:
        explicit InterfaceLevelField (AudioEngine& a) : audio (a)
        {
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            setTooltip ("How loud a sound your audio interface's input takes: its manual's maximum input level, in dBu "
                        "(12 if you don't know it). Every amp uses it to match its input. Drag up and down; double-click for 12");
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (isMouseOverOrDragging() ? titleBar : panelLight);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (hairline);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

            auto area = getLocalBounds().reduced (10, 0);
            g.setColour (textDim);
            g.setFont (font (15.0f));
            g.drawText ("Your interface", area, juce::Justification::centredLeft, true);
            g.setColour (text);
            g.setFont (monoFont (15.0f, true));
            g.drawText (juce::String (NightAmp::getInterfaceLevel(), 1) + " dBu", area, juce::Justification::centredRight, true);
        }

        void mouseDown (const juce::MouseEvent&) override          { start = NightAmp::getInterfaceLevel(); }
        void mouseDrag (const juce::MouseEvent& e) override        { set (start - e.getDistanceFromDragStartY() * (e.mods.isShiftDown() ? 0.02 : 0.1)); }
        void mouseDoubleClick (const juce::MouseEvent&) override   { set (NightAmp::defaultInterfaceLevel); }
        void mouseEnter (const juce::MouseEvent&) override         { repaint(); }
        void mouseExit (const juce::MouseEvent&) override          { repaint(); }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            set (NightAmp::getInterfaceLevel() + (w.deltaY > 0 ? 0.5 : -0.5));
        }

        /** In half dB (a tenth with Shift's finer drag), kept with the app; every amp hears it at once. */
        void set (double dBu)
        {
            auto options = audio.getOptions();
            options.interfaceInputDbu = juce::jlimit (-20.0, 40.0, std::round (dBu * 10.0) / 10.0);

            if (options.interfaceInputDbu != audio.getOptions().interfaceInputDbu)
                audio.setOptions (options);

            repaint();
        }

    private:
        AudioEngine& audio;
        double start = 0.0;
    };

    //==============================================================================
    /** A slimmable model's size: a step for each, the smallest at the left, the whole model at the right; the words
        say which. Click or drag along it, or use the wheel. */
    class AmpSizeControl : public juce::Component,
                           public juce::SettableTooltipClient
    {
    public:
        AmpSizeControl (NightAmp& a, juce::Colour c) : amp (a), colour (c)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTooltip ("This amp comes in sizes. A smaller one takes less of your computer and sounds a little less like "
                        "the amp; full size sounds the most like it");
        }

        static juce::String describe (int size, int sizes)
        {
            return size >= sizes - 1 ? juce::String ("Full size") : "Size " + juce::String (size + 1) + " of " + juce::String (sizes);
        }

        void paint (juce::Graphics& g) override
        {
            const int sizes = amp.numSizes(), size = amp.getSize();
            auto area = getLocalBounds();

            g.setColour (textDim);
            g.setFont (font (15.0f));
            g.drawText ("Size", area.removeFromLeft (labelWidth), juce::Justification::centredLeft, true);

            g.setColour (text);
            g.setFont (font (15.0f, true));
            g.drawText (describe (size, sizes), area.removeFromRight (wordsWidth), juce::Justification::centredRight, true);

            // The steps: a line through them, each a dot, the ones up to the size picked lit
            const auto line = steps();
            g.setColour (hairlineStrong);
            g.fillRoundedRectangle (line.getX(), line.getCentreY() - 1.5f, line.getWidth(), 3.0f, 1.5f);

            for (int i = 0; i < sizes; ++i)
            {
                const auto centre = stepCentre (i, sizes);
                g.setColour (i <= size ? colour : panelLight);
                g.fillEllipse (juce::Rectangle<float> (i == size ? 14.0f : 9.0f, i == size ? 14.0f : 9.0f).withCentre (centre));

                if (i > size)
                {
                    g.setColour (hairlineStrong);
                    g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (centre), 1.0f);
                }
            }
        }

        void mouseDown (const juce::MouseEvent& e) override    { pick (e.position.x); }
        void mouseDrag (const juce::MouseEvent& e) override    { pick (e.position.x); }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            amp.setSize (amp.getSize() + (w.deltaY > 0 ? 1 : -1));
            repaint();
        }

        static constexpr int labelWidth = 40, wordsWidth = 92;

    private:
        NightAmp& amp;
        juce::Colour colour;

        juce::Rectangle<float> steps() const
        {
            return getLocalBounds().toFloat().withTrimmedLeft ((float) labelWidth + 8.0f).withTrimmedRight ((float) wordsWidth + 10.0f);
        }

        juce::Point<float> stepCentre (int i, int sizes) const
        {
            const auto line = steps();
            return { sizes > 1 ? line.getX() + line.getWidth() * (float) i / (float) (sizes - 1) : line.getCentreX(), line.getCentreY() };
        }

        void pick (float x)
        {
            const int sizes = amp.numSizes();
            const auto line = steps();

            if (sizes < 2 || line.getWidth() <= 0.0f)
                return;

            const int index = juce::roundToInt ((x - line.getX()) / line.getWidth() * (float) (sizes - 1));

            if (juce::jlimit (0, sizes - 1, index) != amp.getSize())
            {
                amp.setSize (juce::jlimit (0, sizes - 1, index));
                repaint();
            }
        }
    };

    //==============================================================================
    /** The NAM amp: what's loaded (the amp and its cabinet, or how to get one), the two files to load, the switches,
        input calibration (matching the amp's input level from the interface's) and a slimmable model's size, and
        input, gate and output. */
    class AmpPanel : public StockPanel
    {
    public:
        AmpPanel (NightAmp& a, AudioEngine& engine) : StockPanel (a, juce::Colour (0xffff8f5a)), amp (a), interfaceLevel (engine), sizeControl (a, colour)
        {
            for (auto id : { "input", "gate", "output" })
                knobs.push_back (&knob (id));

            evenOut = &toggle ("evenOut", "Even out loudness");
            cabinetOn = &toggle ("cabinetOn", "Cabinet");
            calibrate = &toggle ("calibrate", "Match input level");
            calibrate->setTooltip ("Plays your guitar into the amp as loud as the real amp was played when it was captured, "
                                   "from your interface's level beside it. Off, Input alone sets how hard you hit it");
            interfaceLevel.setComponentID ("interfaceLevel");
            sizeControl.setComponentID ("size");
            addAndMakeVisible (interfaceLevel);
            addChildComponent (sizeControl);

            for (auto* b : { &loadAmp, &loadCabinet })
            {
                b->setFilled (false);
                b->setFramed (true);
                addAndMakeVisible (*b);
            }

            loadAmp.setLabel ("Load amp...");
            loadAmp.setTooltip ("Pick a Neural Amp Modeler file (.nam) of the amp, pedal or rig you want");
            loadAmp.onClick = [this] { choose (false); };
            loadCabinet.setLabel ("Load cabinet...");
            loadCabinet.setTooltip ("Pick a speaker cabinet's impulse response (.wav), for a model of the amp alone");
            loadCabinet.onClick = [this] { choose (true); };
            setSize (680, 476);
            tick();     // the size control showing from the start for a slimmable model
        }

        void layout (juce::Rectangle<int> area) override
        {
            display = area.removeFromTop (176);
            area.removeFromTop (14);

            auto buttons = area.removeFromTop (36);
            loadAmp.setBounds (buttons.removeFromLeft (loadAmp.getIdealWidth (36)));
            buttons.removeFromLeft (10);
            loadCabinet.setBounds (buttons.removeFromLeft (loadCabinet.getIdealWidth (36)));
            cabinetOn->setBounds (buttons.removeFromRight (120).withSizeKeepingCentre (120, 30));
            buttons.removeFromRight (10);
            evenOut->setBounds (buttons.removeFromRight (200).withSizeKeepingCentre (200, 30));

            // Matching the amp's input, then (a slimmable model) its size
            area.removeFromTop (10);
            auto matching = area.removeFromTop (32);
            calibrate->setBounds (matching.removeFromLeft (190).withSizeKeepingCentre (190, 30));
            matching.removeFromLeft (10);
            interfaceLevel.setBounds (matching.removeFromLeft (196).withSizeKeepingCentre (196, 30));
            sizeControl.setBounds (matching.removeFromRight (226));

            area.removeFromTop (14);
            row (knobs, area.removeFromTop (106));
        }

        void tick() override
        {
            const auto about = amp.describeModel();
            cabinetOn->setDimmed (! amp.hasCabinet());
            evenOut->setDimmed (! amp.hasModel() || ! about.levelled);

            // Matching needs a model that says its level; the interface's is every amp's, so it stays bright while it's used
            const bool canMatch = amp.hasModel() && about.hasInputLevel;
            calibrate->setDimmed (! canMatch);
            interfaceLevel.setAlpha (canMatch && calibrate->isOn() ? 1.0f : 0.65f);

            if (sizeControl.isVisible() != (amp.numSizes() > 1))
                sizeControl.setVisible (amp.numSizes() > 1);

            sizeControl.repaint();
            interfaceLevel.repaint();
        }

        /** What the line under the cabinet says about matching the amp's input (tests). */
        static juce::String describeMatching (const NightAmp& amp, bool on)
        {
            // As the model gives it (14.76, 18.3, 12), not rounded to a tenth
            auto number = [] (double v)
            {
                const auto s = juce::String (v, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
                return s == "-0" ? juce::String ("0") : s;
            };

            const auto about = amp.describeModel();
            const auto level = number (about.inputLevelDbu) + " dBu";

            if (! about.hasInputLevel)
                return "It doesn't say how loud it was played when captured: set Input by ear";

            if (! on)
                return "Not matched to your interface (it was captured at " + level + ")";

            const float db = amp.calibrationDb();
            return "Input matched to the amp: " + juce::String (db > 0.004f ? "+" : "") + number (db) + " dB (it was captured at " + level + ")";
        }

        void paintDisplay (juce::Graphics& g, juce::Rectangle<float> d) override
        {
            auto area = d.reduced (20.0f, 16.0f);
            const auto problem = amp.getProblem();

            if (! amp.hasModel())
            {
                g.setColour (text);
                g.setFont (font (21.0f, true));
                g.drawText ("No amp loaded", area.removeFromTop (30.0f), juce::Justification::centredLeft, true);
                g.setColour (textDim);
                g.setFont (font (16.0f));
                g.drawFittedText ("Load a Neural Amp Modeler file (.nam) of the amp you want. Thousands are free at tonehunt.org.",
                                  area.removeFromTop (46.0f).toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
            }
            else
            {
                const auto about = amp.describeModel();
                g.setColour (text);
                g.setFont (font (21.0f, true));
                {
                    textaudit::TheirWords theirs;   // the model's name, as its maker gave it
                    g.drawText (about.name, area.removeFromTop (30.0f), juce::Justification::centredLeft, true);
                }

                juce::StringArray facts;

                if (about.gear.isNotEmpty())        facts.add (about.gear);
                if (about.toneType.isNotEmpty())    facts.add (about.toneType.substring (0, 1).toUpperCase() + about.toneType.substring (1));
                if (about.modeledBy.isNotEmpty())   facts.add ("captured by " + about.modeledBy);
                if (about.sampleRate > 0.0)         facts.add ("trained at " + juce::String (about.sampleRate / 1000.0, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " kHz");

                g.setColour (textDim);
                g.setFont (font (16.0f));
                {
                    textaudit::TheirWords theirs;
                    g.drawText (facts.joinIntoString (juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "))), area.removeFromTop (24.0f),
                                juce::Justification::centredLeft, true);
                }

                g.setColour (amp.hasCabinet() ? textDim : textFaint);
                {
                    textaudit::TheirWords theirs;
                    g.drawText (amp.hasCabinet() ? "Cabinet: " + amp.getCabinetFile().getFileNameWithoutExtension()
                                                 : juce::String ("No cabinet: as the model sounds on its own"),
                                area.removeFromTop (24.0f), juce::Justification::centredLeft, true);
                }

                // Input calibration: how much it adds, from the level the amp was captured at
                g.setColour (about.hasInputLevel && calibrate->isOn() ? textDim : textFaint);
                g.drawText (describeMatching (amp, calibrate->isOn()), area.removeFromTop (24.0f), juce::Justification::centredLeft, true);

                // The level coming out, and the gate's light
                auto meter = area.removeFromBottom (8.0f).withTrimmedRight (110.0f);
                level = std::max (amp.takeOutputDb(), level - 1.5f);
                g.setColour (panelLight);
                g.fillRoundedRectangle (meter, 3.0f);
                g.setColour (colour);
                g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, (level + 60.0f) / 60.0f)), 3.0f);

                const bool open = amp.takeGateOpen();
                auto light = juce::Rectangle<float> (d.getRight() - 112.0f, meter.getCentreY() - 10.0f, 92.0f, 20.0f);
                g.setColour (open ? ledOn : textFaint);
                g.fillEllipse (light.removeFromLeft (9.0f).withSizeKeepingCentre (9.0f, 9.0f));
                g.setColour (textDim);
                g.setFont (font (15.0f));
                g.drawText (value ("gate") <= -99.5f ? "No gate" : open ? "Gate open" : "Gate shut", light.withTrimmedLeft (8.0f),
                            juce::Justification::centredLeft, true);
            }

            if (problem.isNotEmpty())
            {
                g.setColour (danger);
                g.setFont (font (16.0f));
                g.drawFittedText (problem, area.removeFromTop (44.0f).toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
            }
        }

    private:
        NightAmp& amp;
        std::vector<juce::Component*> knobs;
        ParamToggle* evenOut = nullptr;
        ParamToggle* cabinetOn = nullptr;
        ParamToggle* calibrate = nullptr;
        InterfaceLevelField interfaceLevel;
        AmpSizeControl sizeControl;
        IconButton loadAmp { "Load amp", icons::plusLine() }, loadCabinet { "Load cabinet", icons::plusLine() };
        std::unique_ptr<juce::FileChooser> chooser;
        float level = -100.0f;

        void choose (bool forCabinet)
        {
            // From the folder of what's loaded now, else Documents
            const auto now = forCabinet ? amp.getCabinetFile() : amp.getModelFile();
            const auto from = now.existsAsFile() ? now.getParentDirectory()
                                                 : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
            chooser = std::make_unique<juce::FileChooser> (forCabinet ? "Pick a cabinet (an impulse response, .wav)" : "Pick an amp model (.nam)",
                                                           from, forCabinet ? "*.wav" : "*.nam");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, forCabinet, dialog = ComputerKeyboard::openSystemDialog()] (const juce::FileChooser& c)
                                  {
                                      if (const auto f = c.getResult(); f.existsAsFile())
                                          forCabinet ? amp.loadCabinet (f) : amp.loadModel (f);

                                      repaint();
                                  });
        }
    };
}

std::unique_ptr<juce::Component> createStockPanel (StockEffect& fx, Song& song, AudioEngine& audio)
{
    if (auto a = dynamic_cast<NightAmp*> (&fx))          return std::make_unique<AmpPanel> (*a, audio);

    if (auto e = dynamic_cast<NightEQ*> (&fx))           return std::make_unique<EqPanel> (*e);
    if (auto c = dynamic_cast<NightCompressor*> (&fx))   return std::make_unique<CompressorPanel> (*c, song);
    if (auto r = dynamic_cast<NightReverb*> (&fx))       return std::make_unique<ReverbPanel> (*r);
    if (auto d = dynamic_cast<NightDelay*> (&fx))        return std::make_unique<DelayPanel> (*d, song);
    if (auto c = dynamic_cast<NightChorus*> (&fx))       return std::make_unique<ChorusPanel> (*c);
    if (auto f = dynamic_cast<NightFilter*> (&fx))       return std::make_unique<FilterPanel> (*f);
    return {};
}

} // namespace nightshift
