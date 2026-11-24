#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ControlSurface.h"

/** MIDI INPUTS: "All inputs" and a tick box per MIDI interface (Controls window, Audio window, phone pages).
    Its height follows the number of devices: parents lay out again on onHeightChanged. */
class MidiInputList : public juce::Component,
                      private juce::ChangeListener
{
public:
    /** rowHeight: 26 on a computer, bigger for fingers. */
    MidiInputList (ControlSurface& surface, int rowHeight = 26);
    ~MidiInputList() override;

    int getIdealHeight() const;
    std::function<void()> onHeightChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refresh();
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    ControlSurface& surface;
    const int rowHeight;
    juce::ToggleButton all { "All inputs (new ones too)" };
    juce::OwnedArray<juce::ToggleButton> boxes;
    std::vector<ControlSurface::InputChoice> shown;
    juce::Label empty;
};
