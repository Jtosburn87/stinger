#pragma once
#include <juce_osc/juce_osc.h>
#include "Show.h"

/** OSC in and out, for lighting desks, show control and Bitfocus Companion (its Generic OSC connection speaks this).
    In: the addresses in helpText() do what the same keys and MIDI do. Out: names, times and states, sent when they change. */
class OscBridge : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>,
                  private juce::Timer
{
public:
    explicit OscBridge (ShowController& controller);
    ~OscBridge() override;

    std::function<void (const juce::String& action)> perform;
    std::function<int()> shownBank;

    /** Opens the input port and points the output. False (with a reason in getError) if the port is taken. */
    bool start (int inputPort, const juce::String& sendHost, int sendPort);
    void stop();
    bool isOn() const { return on; }
    juce::String getError() const { return error; }

    /** The action an OSC message asks for, "" = none. (0 as the value is a button's release: ignored.) */
    static juce::String actionFor (const juce::OSCMessage& message);
    static juce::String helpText();

    /** Sends whatever changed (the timer does this ten times a second). */
    void sendChanges();

private:
    void oscMessageReceived (const juce::OSCMessage& message) override;
    void timerCallback() override { sendChanges(); }
    void sendText (const juce::String& address, const juce::String& value);
    void sendFloat (const juce::String& address, float value);
    void sendInt (const juce::String& address, int value);

    ShowController& controller;
    juce::OSCReceiver receiver;
    juce::OSCSender sender;
    bool on = false, canSend = false;
    juce::String error;
    std::map<juce::String, juce::String> lastSent;
    int ticks = 0;
};
