#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ControlSurface.h"
#include "StreamDeckServer.h"

/** The Controls window: every command with its key and MIDI (Learn / Clear), global keys,
    the MIDI inputs being listened to, and light feedback to a MIDI output. */
class ControlsPanel : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    ControlsPanel (ControlSurface& surface, StreamDeckServer* streamDeck);
    ~ControlsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override { return surface.handleKey (key); }

private:
    class Row;
    void refresh();
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;

    ControlSurface& surface;
    StreamDeckServer* streamDeck;
    juce::Label deckStatus;
    juce::ToggleButton global { "Global keys: Stinger's keys work from any app (other apps don't get those keys)" };
    juce::Label refused, inputs, lastMidi, feedbackLabel { {}, "Light feedback to" };
    juce::ComboBox feedbackOut;
    juce::Label playingLabel { {}, "Playing" }, nextLabel { {}, "Next" }, loadedLabel { {}, "Loaded" };
    juce::Slider playing, next, loaded;
    juce::TextButton reset { "Reset all to defaults" };
    juce::Viewport viewport;
    juce::Component list;
    juce::OwnedArray<Row> rows;
};

class ControlsWindow : public juce::DocumentWindow
{
public:
    ControlsWindow (ControlSurface& surface, StreamDeckServer* streamDeck);
    void closeButtonPressed() override { setVisible (false); }
};
