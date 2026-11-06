#include "DeviceWatch.h"

namespace
{
    constexpr juce::uint32 silentMs = 600;      // no callback for this long = suspicious
    constexpr juce::uint32 stalledMs = 800;     // and still none this much later = gone
}

DeviceWatch::DeviceWatch (ShowController& c)
    : controller (c), engine (c.getEngine()), manager (c.getEngine().getDeviceManager())
{
    rememberMain();
    controller.resumeAfterRateChange = [this] { return state != State::normal; };
    manager.addChangeListener (this);
    startTimerHz (4);
}

DeviceWatch::~DeviceWatch()
{
    manager.removeChangeListener (this);
    controller.resumeAfterRateChange = nullptr;
}

void DeviceWatch::rememberMain()
{
    if (auto* device = manager.getCurrentAudioDevice())
    {
        mainType = device->getTypeName();
        mainName = device->getName();
        mainRate = device->getCurrentSampleRate();
        mainBuffer = device->getCurrentBufferSizeSamples();
    }
}

bool DeviceWatch::deviceIsCurrent (const juce::String& type, const juce::String& name) const
{
    auto* device = manager.getCurrentAudioDevice();
    return device != nullptr && device->getTypeName() == type && device->getName() == name;
}

bool DeviceWatch::isMainAvailable() const
{
    for (auto* type : manager.getAvailableDeviceTypes())
        if (type->getTypeName() == mainType)
            return type->getDeviceNames (false).contains (mainName);
    return false;
}

void DeviceWatch::setUserChanging (bool changing)
{
    userChanging = changing;
    if (! changing && state == State::normal)
        rememberMain();
}

void DeviceWatch::setState (State s)
{
    if (s == state)
        return;
    state = s;
    sendChangeMessage();
}

void DeviceWatch::check()
{
    if (switching)
        return;
    if (userChanging)
    {
        // a device picked by hand is the new main one
        const bool expected = state == State::normal ? deviceIsCurrent (mainType, mainName) : deviceIsCurrent (backup.type, backup.name);
        if (! expected)
        {
            engine.setBackupRouting (false, -1);
            rememberMain();
            setState (State::normal);
        }
        stalledSince = 0;
        return;
    }

    auto* device = manager.getCurrentAudioDevice();
    if (state == State::lost)
    {
        // nothing to play on: take the main device as soon as it's back; failing that the backup
        if (device == nullptr || ! device->isPlaying())
        {
            juce::String error;
            const auto now = juce::Time::getMillisecondCounter();
            if (isMainAvailable() && now - lastRetry >= 3000)
            {
                lastRetry = now;
                returnToMain (error);
            }
        }
        return;
    }

    const auto& type = state == State::normal ? mainType : backup.type;
    const auto& name = state == State::normal ? mainName : backup.name;
    if (name.isEmpty())
    {
        rememberMain();   // started with no device; the first one to open is the main one
        return;
    }
    if (! deviceIsCurrent (type, name) || ! device->isPlaying())
    {
        deviceLost();
        return;
    }

    const auto now = juce::Time::getMillisecondCounter();
    if (now - engine.getLastCallbackMs() < silentMs)
    {
        stalledSince = 0;
        if (state == State::normal)
            rememberMain();   // keeps the rate and buffer size the user last chose
        return;
    }
    if (stalledSince == 0)
        stalledSince = now;
    else if (now - stalledSince >= stalledMs)
        deviceLost();
}

bool DeviceWatch::openDevice (const juce::String& type, const juce::String& name, double sampleRate, int bufferSize, juce::String& error)
{
    engine.resyncOnNextStart (true);
    manager.setCurrentAudioDeviceType (type, true);
    auto setup = manager.getAudioDeviceSetup();
    setup.outputDeviceName = name;
    setup.inputDeviceName = {};
    setup.inputChannels.clear();
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = true;
    setup.sampleRate = sampleRate;      // the same rate if it can, so the audio doesn't have to load again
    setup.bufferSize = bufferSize;
    error = manager.setAudioDeviceSetup (setup, true);
    const bool ok = error.isEmpty() && deviceIsCurrent (type, name);
    if (! ok)
        engine.resyncOnNextStart (false);
    return ok;
}


void DeviceWatch::deviceLost()
{
    stalledSince = 0;
    switching = true;
    juce::String error;
    const bool wasOnBackup = state == State::onBackup;
    if (backup.isSet() && ! wasOnBackup && openDevice (backup.type, backup.name, mainRate, mainBuffer, error))
    {
        engine.setBackupRouting (true, backup.output);
        switching = false;
        setState (State::onBackup);
        return;
    }
    switching = false;
    engine.setBackupRouting (false, -1);
    setState (State::lost);
}

bool DeviceWatch::returnToMain (juce::String& error)
{
    switching = true;
    const bool ok = openDevice (mainType, mainName, mainRate, mainBuffer, error);
    switching = false;
    if (! ok)
    {
        if (error.isEmpty())
            error = mainName + " didn't open.";
        return false;
    }
    engine.setBackupRouting (false, -1);
    stalledSince = 0;
    setState (State::normal);
    return true;
}

void DeviceWatch::simulateLossForTest()
{
    manager.closeAudioDevice();
}
