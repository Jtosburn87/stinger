#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <functional>
#include <memory>

/** A cue's audio file, read into memory at the device sample rate (1 or 2 channels), plus a
    waveform overview. Never changed after loading, so the audio thread can read it freely. */
struct CueAudio
{
    juce::AudioBuffer<float> samples;
    double sampleRate = 0.0;        // rate of `samples` (the device rate it was loaded for)
    double fileSampleRate = 0.0;
    int fileChannels = 0;           // channels in the file (only the first 2 are used)

    /** Waveform overview: min / max of all channels over each block of peakBlock samples. */
    static constexpr int peakBlock = 256;
    std::vector<float> peakMin, peakMax;

    double seconds() const { return sampleRate > 0.0 ? samples.getNumSamples() / sampleRate : 0.0; }
    /** The zero crossing nearest to t (seconds) within 3 ms, else t: edits there don't click. */
    double nearestZeroCrossing (double t) const;
    int numChannels() const { return samples.getNumChannels(); }
    bool isMono() const { return samples.getNumChannels() == 1; }

    /** Longer files are refused: the whole file is held in memory (an hour of stereo at 48k is about 1.4 GB). */
    static constexpr double maxSeconds = 2 * 3600.0;

    /** Reads and resamples a file. Returns null with error set if it can't be read. */
    static std::shared_ptr<CueAudio> load (const juce::File& file, double targetRate,
                                           juce::AudioFormatManager& formats, juce::String& error);

    /** Frames really in a WAV file's data chunk (a streamed WAV's header says 0xFFFFFFFF bytes), -1 if not a WAV. */
    static juce::int64 wavSamplesInFile (const juce::File& file, int bytesPerFrame);
};

/** Loads files on a background thread, one at a time, and reports back on the message thread. */
class AudioLoader
{
public:
    using Done = std::function<void (std::shared_ptr<CueAudio> audio, juce::String error)>;

    AudioLoader();
    ~AudioLoader();

    /** done is called on the message thread (not called if the loader is gone by then). */
    void load (const juce::File& file, double targetRate, Done done);

    int pending() const { return queued.load(); }

    /** The file types the loader can read, as a wildcard list for file choosers. */
    juce::String wildcards() const;

private:
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("Audio loader") };
    juce::AudioFormatManager formats;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::atomic<int> queued { 0 };
};
