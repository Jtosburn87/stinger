#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

/** A button's effects: 3 plugin slots run in order on the button's audio (stereo, after its fades and
    level, before it goes to its output), so a reverb or delay rings out after the sting stops.
    Plugins are made and changed on the message thread; the audio thread runs them (and skips a block
    rather than wait while a slot is being swapped). */
class CueFx
{
public:
    static constexpr int numSlots = 3;

    CueFx() = default;
    ~CueFx();

    // ---- message thread ----
    /** Puts a plugin in a slot (null empties it). The plugin is prepared first. */
    void setSlot (int slot, std::unique_ptr<juce::AudioProcessor> plugin, const juce::PluginDescription& description);
    void setBypassed (int slot, bool bypassed);
    bool isBypassed (int slot) const;
    juce::AudioProcessor* getPlugin (int slot) const;
    juce::PluginDescription getDescription (int slot) const;
    bool isEmpty() const;
    /** How long to keep running after the last sound: the longest tail a plugin reports, at least 2 s
        (an echo can come after a silence), at most 30 s. */
    double tailSeconds() const { return tail.load(); }
    /** New device rate / block size: prepares every plugin again. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Plugin states for the show file (base64), "" for an empty slot. */
    juce::String getState (int slot) const;
    void setState (int slot, const juce::String& base64);

    // ---- audio thread ----
    /** Runs the plugins on a stereo buffer in place. Returns false if it couldn't this time (being changed). */
    bool process (juce::AudioBuffer<float>& stereo, int numSamples);

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioProcessor> plugin;
        juce::PluginDescription description;
        std::atomic<bool> bypassed { false };
    };
    void prepareOne (juce::AudioProcessor& p);

    std::array<Slot, numSlots> slots;
    juce::SpinLock lock;            // held by the audio thread while it runs the plugins
    double sampleRate = 48000.0;
    int blockSize = 512;
    juce::AudioBuffer<float> work;  // the plugins' own channel count (some want more than 2)
    juce::MidiBuffer midi;
    std::atomic<double> tail { 2.0 };
    void updateTail();
};
