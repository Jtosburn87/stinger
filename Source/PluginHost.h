#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

/** Knows the installed VST3 effects and makes them.

    Scanning runs out of process: for each plugin file Stinger starts another copy of itself with
    --scan-plugin. If that copy crashes or hangs (60 s), only it dies and the plugin goes on the failed
    list, which is kept with the plugin list in the settings. */
class PluginHost : private juce::ChangeListener
{
public:
    explicit PluginHost (juce::PropertiesFile& settings);
    ~PluginHost() override;

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;

    /** Effects (no instruments), sorted by maker then name. */
    juce::Array<juce::PluginDescription> effects() const;

    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& d, double sampleRate, int blockSize,
                                                       juce::String& error);

    /** For a crash during a scan: the plugin being scanned is noted here and skipped next time. */
    juce::File deadMansPedalFile() const;
    juce::PropertiesFile& getSettings() { return settings; }

    /** Child process: Stinger.exe --scan-plugin <plugin file> <result.xml>. Returns the exit code. */
    static int runScanner (const juce::StringArray& args);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    juce::PropertiesFile& settings;
};
