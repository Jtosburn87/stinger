#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include "MultibandLimiter.h"

/** Stinger's own effects. They sit in a button's plugin slots like any plugin, so they save with the show
    and work on every platform; on Android, which has no VST3, they are the effects there are. */
class StockFx : public juce::AudioPluginInstance
{
public:
    enum class Kind { eq, compressor, limiter };

    struct Param
    {
        const char* id;
        const char* name;
        const char* unit;
        float minimum, maximum, initial, step;
        bool logarithmic;   // frequencies and times: the slider's midpoint is the geometric middle
        bool onOff = false; // a switch, not a slider: 0 or 1
    };

    static juce::PluginDescription describe (Kind kind);
    /** Is this description one of ours? */
    static bool isStock (const juce::PluginDescription& d);
    /** Makes the effect a description names (null if it isn't one of ours). */
    static std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& d);
    static std::unique_ptr<juce::AudioPluginInstance> create (Kind kind);

    Kind getKind() const { return kind; }
    int numParams() const { return (int) params.size(); }
    const Param& param (int i) const { return params[(size_t) i]; }
    /** Safe from any thread; the audio thread picks the value up at its next block. */
    float get (int i) const { return values[(size_t) i].load(); }
    void set (int i, float value);
    /** The limiter's Threshold or Output moved by hand: with Link on, the other fader moves the same amount. */
    void moveLimiterFader (int i, float value);

    /** How far the signal is being turned down right now, in dB (0 or more). */
    float gainReductionDb() const { return reduction.load(); }

    // juce::AudioProcessor
    const juce::String getName() const override;
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = describe (kind); }

    // parameter numbers
    enum EqParam { eqLowCut, eqLowGain, eqMidGain, eqMidFreq, eqHighGain };
    enum CompParam { compThreshold, compRatio, compAttack, compRelease, compMakeup };
    enum LimitParam { limitThreshold, limitOutput, limitRelease, limitWideBand, limitLink, limitInterSample };

    /** One filter section (transposed direct form II, so changing it while it runs doesn't click). */
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        std::array<float, 2> z1 {}, z2 {};
        void reset() { z1 = {}; z2 = {}; }
        void run (float* samples, int channel, int numSamples);
        void highPass (double sampleRate, double frequency);
        void lowShelf (double sampleRate, double frequency, double gainDb);
        void highShelf (double sampleRate, double frequency, double gainDb);
        void peak (double sampleRate, double frequency, double gainDb, double q);
    };

private:
    explicit StockFx (Kind kind);
    void runEq (juce::AudioBuffer<float>&, int numChannels, int numSamples);
    void runCompressor (juce::AudioBuffer<float>&, int numChannels, int numSamples);
    void runLimiter (juce::AudioBuffer<float>&, int numChannels, int numSamples);
    void updateEq();

    const Kind kind;
    std::vector<Param> params;
    std::array<std::atomic<float>, 8> values;
    double rate = 48000.0;
    std::atomic<bool> eqChanged { true };

    Biquad lowCut, low, mid, high;
    bool lowCutOn = false;

    float envelopeDb = 0.0f;   // compressor: smoothed gain reduction
    MultibandLimiter limiter;
    float driveGain = 1.0f, outputGain = 1.0f;   // limiter: where the last block's gains ended
    std::atomic<float> reduction { 0.0f };
};
