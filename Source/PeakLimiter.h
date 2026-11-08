#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>

/** A look-ahead peak limiter, all channels linked. It looks 1.25 ms ahead, so nothing passes the ceiling: the
    gain has already come down when a peak arrives. Release is automatic: a lone peak lets go quickly, while
    limiting that goes on holds steady instead of pumping. With inter-sample detection it also sees the peaks
    that fall between samples (4 x oversampled, on the detector only, so the sound itself is not resampled). */
class PeakLimiter
{
public:
    /** Before audio starts. */
    void prepare (double sampleRate, int numChannels);

    // any thread
    void setCeilingDb (float db) { ceilingDb.store (db); }
    void setInterSample (bool on) { interSample.store (on); }
    /** How far it is turning everything down right now, in dB (0 or more). */
    float gainReductionDb() const { return reduction.load(); }
    /** Samples of delay it adds. */
    int getLatency() const { return lookahead - 1 + detectorDelay; }

    /** Clears what it holds. */
    void reset();

    /** The audio thread, in place. */
    void process (float* const* channels, int numChannels, int numSamples);

private:
    static constexpr int taps = 12, detectorDelay = taps / 2;   // detection looks at the sample this far back
    float peakOf (int channel, bool betweenSamples) const;

    std::atomic<float> ceilingDb { 0.0f }, reduction { 0.0f };
    std::atomic<bool> interSample { false };

    double rate = 48000.0;
    int lookahead = 60;                         // samples
    std::vector<std::vector<float>> delay;      // per channel
    std::vector<std::array<float, taps>> history;   // per channel, newest first
    std::array<std::array<float, taps>, 3> interpolator {};   // for 1/4, 2/4 and 3/4 of the way between samples
    int delayAt = 0;
    std::vector<float> window, ramp;            // the gains still to come, for the minimum and the smoothing
    int windowAt = 0, rampAt = 0;
    double rampSum = 0.0;
    float envelope = 1.0f, sustained = 1.0f;    // the gain, and where it has been lately
    float fastRelease = 0.0f, slowFollow = 0.0f;
};
