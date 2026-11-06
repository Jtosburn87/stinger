#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "DeviceWatch.h"

/** The Audio window: the interface as usual, plus the backup device Stinger moves to if that interface is lost. */
class AudioPanel : public juce::Component
{
public:
    AudioPanel (DeviceWatch& watch, juce::PropertiesFile& settings);
    ~AudioPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The backup saved in the settings. */
    static DeviceWatch::Backup loadBackup (juce::PropertiesFile& settings);

private:
    void fillTypes();
    void fillDevices();
    void fillOutputs();
    void store();

    DeviceWatch& watch;
    juce::PropertiesFile& settings;
    juce::AudioDeviceSelectorComponent selector;
    juce::Label heading { {}, "BACKUP" }, typeLabel { {}, "Device type" }, deviceLabel { {}, "Backup device" }, outputLabel { {}, "Backup output" };
    juce::ComboBox type, device, output;
};
