#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "Controls.h"
#include "GlobalHotkeys.h"
#include "Show.h"

/** Turns keys and MIDI into actions: the fixed commands (GO, Stop all, banks, "button N of the shown
    bank"...) and each button's own key / MIDI. Learns new bindings, keeps the global hotkeys
    registered, listens to every MIDI input and sends light feedback to a MIDI output.
    Actions are ids: a command id (see CommandMap) or "cue:<id>". */
class ControlSurface : public juce::ChangeBroadcaster,
                       private juce::MidiInputCallback,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    ControlSurface (ShowController& controller, juce::PropertiesFile& settings);
    ~ControlSurface() override;

    /** Does an action (set by the main window). */
    std::function<void (const juce::String& action)> perform;
    /** The bank the grid shows ("button N of the shown bank"). */
    std::function<int()> shownBank;

    const CommandMap& getCommands() const { return commands; }
    void setCommandBinding (const juce::String& id, const Binding& b);
    void resetCommands();

    /** Keys pressed in Stinger's window. Returns true if one did something. */
    bool handleKey (const juce::KeyPress& key);

    /** Global keys: every bound key works from any app (and other apps don't get it). */
    bool isGlobal() const { return global; }
    void setGlobal (bool on);
    /** Keys Windows wouldn't register (another app has them). */
    const juce::Array<juce::KeyPress>& getRefusedKeys() const { return refusedKeys; }

    /** Learn: the next key press or MIDI message becomes the binding of target (a command id or "cue:<id>"). */
    void learn (const juce::String& target);
    void cancelLearn();
    juce::String learning() const { return learnTarget; }

    juce::StringArray getInputNames() const;
    juce::String getLastMidi() const { return lastMidi; }

    /** Light feedback for controllers with lit pads: velocities sent to each bound note / CC. */
    juce::Array<juce::MidiDeviceInfo> getOutputs() const { return juce::MidiOutput::getAvailableDevices(); }
    juce::String getFeedbackOutput() const { return feedbackId; }
    void setFeedbackOutput (const juce::String& identifier);
    struct FeedbackLevels { int playing = 127, next = 64, loaded = 0; };
    FeedbackLevels getFeedbackLevels() const { return levels; }
    void setFeedbackLevels (FeedbackLevels l);

    /** What an action's binding is called, e.g. "Space  |  Note C3 ch 1". */
    static juce::String describe (const Binding& b);
    Binding bindingFor (const juce::String& action) const;

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleMidi (const juce::MidiMessage& m);
    void assign (juce::String target, const juce::KeyPress* key, const MidiTrigger* midi);
    void updateGlobalKeys();
    void refreshInputs();
    void sendFeedback();
    void save();
    void changeListenerCallback (juce::ChangeBroadcaster*) override { updateGlobalKeys(); }
    void timerCallback() override;

    ShowController& controller;
    juce::PropertiesFile& settings;
    CommandMap commands;
    GlobalHotkeys hotkeys;
    bool global = false;
    juce::Array<juce::KeyPress> refusedKeys;
    juce::String learnTarget;
    ControllerEdges edges;
    juce::String lastMidi;

    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
    juce::String feedbackId;
    std::unique_ptr<juce::MidiOutput> feedback;
    FeedbackLevels levels;
    std::map<juce::String, int> sent;   // feedback already sent, per "type/channel/number"
    int ticks = 0;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
