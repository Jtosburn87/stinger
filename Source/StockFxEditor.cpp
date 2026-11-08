#include "StockFxEditor.h"
#include "Look.h"

namespace
{
    constexpr int rowHeight = 34, labelWidth = 110, meterHeight = 40, margin = 16;
}

StockFxEditor::StockFxEditor (StockFx& effect, std::function<void()> onChange) : fx (effect), changed (std::move (onChange))
{
    const bool limiter = fx.getKind() == StockFx::Kind::limiter;
    for (int i = 0; i < fx.numParams(); ++i)
    {
        const auto p = fx.param (i);
        auto* label = labels.add (new juce::Label ({}, p.name));
        label->setColour (juce::Label::textColourId, Look::textDim);
        addAndMakeVisible (label);

        if (p.onOff)
        {
            sliders.add (nullptr);
            auto* toggle = switches.add (new juce::ToggleButton ("On"));
            toggle->setToggleState (fx.get (i) >= 0.5f, juce::dontSendNotification);
            toggle->onClick = [this, i, toggle] { fx.set (i, toggle->getToggleState() ? 1.0f : 0.0f); if (changed) changed(); };
            addAndMakeVisible (toggle);
            continue;
        }

        switches.add (nullptr);
        auto* s = sliders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight));
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 24);
        s->setRange (p.minimum, p.maximum, p.step);
        if (p.logarithmic)
            s->setSkewFactorFromMidPoint (std::sqrt ((double) p.minimum * p.maximum));
        const juce::String unit (p.unit);
        const bool lowCut = fx.getKind() == StockFx::Kind::eq && i == StockFx::eqLowCut;
        const float bottom = p.minimum;
        s->textFromValueFunction = [unit, lowCut, bottom] (double v)
        {
            if (lowCut && v <= bottom + 0.5)
                return juce::String ("Off");
            if (unit.isEmpty())
                return juce::String (v, 2);
            return juce::String (v, v < 10.0 && unit != "Hz" ? 1 : 0) + (unit == ":1" ? unit : " " + unit);
        };
        s->setValue (fx.get (i), juce::dontSendNotification);
        s->onValueChange = [this, i, s, limiter]
        {
            if (limiter)
                fx.moveLimiterFader (i, (float) s->getValue());   // Link: Threshold and Output move together
            else
                fx.set (i, (float) s->getValue());
            if (changed)
                changed();
        };
        addAndMakeVisible (s);
    }

    const bool meter = fx.getKind() != StockFx::Kind::eq;
    setSize (480, margin * 2 + fx.numParams() * rowHeight + (meter ? meterHeight : 0));
    if (meter)
        startTimerHz (20);
    else
        startTimerHz (4);   // only to follow Link moving the other limiter fader (no meter)
}

void StockFxEditor::timerCallback()
{
    // follow changes made elsewhere (Link, the phone's page, Undo)
    for (int i = 0; i < fx.numParams(); ++i)
    {
        if (auto* s = sliders[i]; s != nullptr && ! s->isMouseButtonDown() && std::abs (s->getValue() - fx.get (i)) > 1.0e-4)
            s->setValue (fx.get (i), juce::dontSendNotification);
        if (auto* t = switches[i]; t != nullptr)
            t->setToggleState (fx.get (i) >= 0.5f, juce::dontSendNotification);
    }
    if (fx.getKind() != StockFx::Kind::eq && std::abs (fx.gainReductionDb() - shownReduction) > 0.05f)
    {
        shownReduction = fx.gainReductionDb();
        repaint();
    }
}

void StockFxEditor::paint (juce::Graphics& g)
{
    g.fillAll (Look::panel);
    if (fx.getKind() == StockFx::Kind::eq)
        return;

    auto r = getLocalBounds().reduced (margin).removeFromBottom (meterHeight).reduced (0, 10).toFloat();
    g.setColour (Look::textDim);
    g.setFont (Look::font (13.0f));
    g.drawText ("Reduction", r.removeFromLeft ((float) labelWidth), juce::Justification::centredLeft);
    auto text = r.removeFromRight (80.0f);
    const auto db = juce::jmax (0.0f, shownReduction);
    g.setColour (Look::raised);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (Look::amber);
    g.fillRoundedRectangle (r.withLeft (r.getRight() - r.getWidth() * juce::jmin (db / 24.0f, 1.0f)), 3.0f);
    g.setColour (Look::textDim);
    g.setFont (Look::mono (12.0f));
    g.drawText ((db >= 0.05f ? "-" : "") + juce::String (db, 1) + " dB", text, juce::Justification::centredRight);
}

void StockFxEditor::resized()
{
    auto r = getLocalBounds().reduced (margin);
    for (int i = 0; i < fx.numParams(); ++i)
    {
        auto row = r.removeFromTop (rowHeight).reduced (0, 4);
        labels[i]->setBounds (row.removeFromLeft (labelWidth));
        if (auto* s = sliders[i])
            s->setBounds (row);
        if (auto* t = switches[i])
            t->setBounds (row.removeFromLeft (80));
    }
}
