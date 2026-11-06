#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "OscBridge.h"

/** File > OSC. */
class OscPanel : public juce::Component
{
public:
    OscPanel (OscBridge& osc, juce::PropertiesFile& settings);
    void paint (juce::Graphics&) override;
    void resized() override;
    /** Starts OSC if it was on when Stinger last closed. */
    static void restore (OscBridge& osc, juce::PropertiesFile& settings);

private:
    void apply();

    OscBridge& osc;
    juce::PropertiesFile& settings;
    juce::ToggleButton on { "OSC on" };
    juce::Label inLabel { {}, "Listen on port" }, hostLabel { {}, "Send to" }, portLabel { {}, "port" }, status;
    juce::TextEditor inPort, host, outPort, help;
};
