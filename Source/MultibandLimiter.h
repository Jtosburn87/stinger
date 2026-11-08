#pragma once
#include "PeakLimiter.h"
#include <array>
#include <atomic>
#include <vector>

/** A maximizer: splits the sound into 5 bands and holds each at full scale on its own, so a loud kick doesn't
    pull the vocal down with it. If the bands add up to more than full scale, the loudest gives way first and
    the quiet ones stay put. A last brickwall stage then catches whatever is left. Behaviour measured from the
    Waves L3 Ultramaximizer (band edges, release shape), but with almost no delay: the L3 adds 80 ms, too long
    for a button that has to fire on the beat. This adds about 2.6 ms. The crossovers are Linkwitz-Riley: the
    bands stay in step with each other at every frequency, so they can't cancel when their gains differ, and
    together they add back at the level that went in. Wide band mode is one band, the same release and the
    same last stage. */
class MultibandLimiter
{
public:
    static constexpr int maxBands = 5;

    void prepare (double sampleRate);
    void reset();

    // any thread
    void setRelease (float amount) { release.store (amount); }     // 0.1 .. 10, 1 = about 300 ms
    void setWideBand (bool on) { wideBand.store (on); }
    void setInterSample (bool on) { last.setInterSample (on); }
    /** How far the sound as a whole is being turned down right now, in dB (0 or more). */
    float gainReductionDb() const { return reduction.load(); }
    int getLatency() const { return lookahead - 1 + last.getLatency(); }

    /** The audio thread: 2 channels in place, held to full scale. */
    void process (float* const* channels, int numSamples);

private:
    struct Filter
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        std::array<float, 2> z1 {}, z2 {};
        float tick (float in, int channel);
    };
    struct Band
    {
        float first = 0.0f, second = 0.0f;              // the two release stages, dB of reduction
        float held = 1.0f;                              // the gain they come to
        std::array<std::vector<float>, 2> delay;
        std::vector<float> window, ramp;                // the gains still to come
        float minimum = 1.0f;
        double rampSum = 0.0;
    };

    void updateRelease (float amount);

    std::atomic<float> release { 1.0f }, reduction { 0.0f };
    std::atomic<bool> wideBand { false };
    float appliedRelease = -1.0f, releaseCoefficient = 0.01f;

    double rate = 48000.0;
    int lookahead = 60, at = 0;
    bool wasWide = false;
    std::array<Filter, maxBands - 1> lows, highs;
    std::array<std::array<Filter, maxBands - 1>, maxBands - 1> keepInStep;   // [band][crossover it didn't go through]
    std::array<Band, maxBands> bands;
    PeakLimiter last;
};
