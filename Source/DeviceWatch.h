#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "Show.h"

/** Watches the audio device during a show. If it disappears (unplugged, driver gone, or it silently stops calling
    back) Stinger moves to the backup device by itself and what was playing carries on where it would have been.
    It stays on the backup until told to go back: moving mid-song on its own could be worse than staying.

    JUCE reopens the system default by itself when a device vanishes, which on a laptop means the built-in speakers,
    so a device other than the chosen one counts as a loss too, unless the user is in the Audio window. */
class DeviceWatch : public juce::ChangeBroadcaster,
                    private juce::Timer,
                    private juce::ChangeListener
{
public:
    enum class State { normal, onBackup, lost };   // lost = no backup to go to (or it failed)

    struct Backup
    {
        juce::String type, name;   // device type + device; no name = no backup
        int output = -1;           // first channel of the pair every song goes to, -1 = songs keep their own outputs
        bool isSet() const { return name.isNotEmpty(); }
    };

    explicit DeviceWatch (ShowController& controller);
    ~DeviceWatch() override;

    void setBackup (const Backup& b) { backup = b; }
    Backup getBackup() const { return backup; }
    juce::AudioDeviceManager& getManager() { return manager; }

    State getState() const { return state; }
    juce::String getMainName() const { return mainName; }
    /** The main device is listed again. (ASIO drivers stay listed even with the interface unplugged.) */
    bool isMainAvailable() const;
    /** Goes back to the main device; what's playing carries on. False (with the reason) if it can't be opened. */
    bool returnToMain (juce::String& error);

    /** While the Audio window is open, device changes are the user's. */
    void setUserChanging (bool changing);

    /** Checks now (the timer does this four times a second). */
    void check();
    /** For tests: closes the device as if it had been unplugged. */
    void simulateLossForTest();

private:
    void timerCallback() override { check(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { check(); }
    bool deviceIsCurrent (const juce::String& type, const juce::String& name) const;
    void rememberMain();
    void deviceLost();
    bool openDevice (const juce::String& type, const juce::String& name, double sampleRate, int bufferSize, juce::String& error);
    void setState (State s);

    ShowController& controller;
    PlaybackEngine& engine;
    juce::AudioDeviceManager& manager;
    Backup backup;
    State state = State::normal;
    juce::String mainType, mainName;
    double mainRate = 0.0;
    int mainBuffer = 0;
    juce::uint32 stalledSince = 0, lastRetry = 0;
    bool userChanging = false, switching = false;
};
