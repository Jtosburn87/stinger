#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "StockFx.h"

/** The window for one of Stinger's own effects (EQ, compressor, limiter) on the desktop screen in Samsung DeX:
    a slider or switch per setting, and a gain-reduction meter for the compressor and limiter.
    Android only (on a computer the slots hold VST3 plugins, with their own windows). */
class StockFxEditor : public juce::Component,
                      private juce::Timer
{
public:
    /** onChange: a setting moved (the show has unsaved changes). */
    StockFxEditor (StockFx& effect, std::function<void()> onChange);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    StockFx& fx;
    std::function<void()> changed;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::Slider> sliders;          // nullptr where the setting is a switch
    juce::OwnedArray<juce::ToggleButton> switches;   // nullptr where it's a slider
    float shownReduction = -1.0f;
};
