#include "PeakLimiter.h"

void PeakLimiter::prepare (double sampleRate, int numChannels)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    lookahead = std::max (2, (int) std::round (rate * 0.00125));
    const auto channels = (size_t) std::max (1, numChannels);
    delay.assign (channels, std::vector<float> ((size_t) (lookahead + detectorDelay), 0.0f));
    history.assign (channels, {});
    window.assign ((size_t) lookahead, 1.0f);
    ramp.assign ((size_t) lookahead, 1.0f);
    fastRelease = (float) (1.0 - std::exp (-1.0 / (rate * 0.03)));   // 30 ms back up after a lone peak
    slowFollow = (float) (1.0 - std::exp (-1.0 / (rate * 0.4)));     // limiting that lasts this long holds steady

    // a Blackman-windowed sinc: what the signal does between its samples
    for (int phase = 0; phase < 3; ++phase)
    {
        const double position = detectorDelay - (phase + 1) / 4.0;   // from the detector's sample towards the next
        double sum = 0.0;
        for (int k = 0; k < taps; ++k)
        {
            const double t = (double) k - position;
            const double pi = juce::MathConstants<double>::pi;
            const double sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (pi * t) / (pi * t);
            const double x = juce::jlimit (-1.0, 1.0, t / (double) detectorDelay);
            const double blackman = 0.42 + 0.5 * std::cos (pi * x) + 0.08 * std::cos (2.0 * pi * x);
            interpolator[(size_t) phase][(size_t) k] = (float) (sinc * blackman);
            sum += sinc * blackman;
        }
        for (auto& c : interpolator[(size_t) phase])
            c = (float) (c / sum);
    }
    reset();
}

void PeakLimiter::reset()
{
    for (auto& d : delay)
        std::fill (d.begin(), d.end(), 0.0f);
    for (auto& h : history)
        h.fill (0.0f);
    std::fill (window.begin(), window.end(), 1.0f);
    std::fill (ramp.begin(), ramp.end(), 1.0f);
    delayAt = windowAt = rampAt = 0;
    rampSum = (double) lookahead;
    envelope = sustained = 1.0f;
    reduction.store (0.0f);
}

float PeakLimiter::peakOf (int channel, bool betweenSamples) const
{
    const auto& h = history[(size_t) channel];
    float peak = std::abs (h[(size_t) detectorDelay]);
    if (betweenSamples)
        for (const auto& coefficients : interpolator)
        {
            float y = 0.0f;
            for (int k = 0; k < taps; ++k)
                y += coefficients[(size_t) k] * h[(size_t) k];
            peak = std::max (peak, std::abs (y));
        }
    return peak;
}

void PeakLimiter::process (float* const* buffers, int numChannels, int numSamples)
{
    if (delay.empty() || window.empty())
        return;

    const int channels = std::min (numChannels, (int) delay.size());
    const int length = lookahead + detectorDelay;
    const float ceiling = juce::Decibels::decibelsToGain (ceilingDb.load());
    const bool betweenSamples = interSample.load();
    float lowest = 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& h = history[(size_t) ch];
            std::copy_backward (h.begin(), h.end() - 1, h.end());
            h[0] = buffers[ch] != nullptr ? buffers[ch][i] : 0.0f;
            peak = std::max (peak, peakOf (ch, betweenSamples));
        }

        // the gain this sample needs; coming back up it heads for where the gain has been lately, so a lone
        // peak recovers in moments and steady limiting doesn't pump
        const float needed = peak > ceiling ? ceiling / peak : 1.0f;
        sustained += (needed - sustained) * slowFollow;
        envelope = std::min (needed, envelope + (std::max (sustained, envelope) - envelope) * fastRelease);

        // the lowest gain anywhere in the look-ahead, then smoothed over the same length: the gain is
        // already down by the time the delayed sample comes out, without a corner to hear
        window[(size_t) windowAt] = envelope;
        windowAt = (windowAt + 1) % lookahead;
        float minimum = 1.0f;
        for (const auto g : window)
            minimum = std::min (minimum, g);

        rampSum += (double) minimum - (double) ramp[(size_t) rampAt];
        ramp[(size_t) rampAt] = minimum;
        rampAt = (rampAt + 1) % lookahead;
        const auto gain = (float) (rampSum / (double) lookahead);
        lowest = std::min (lowest, gain);

        // late by the look-ahead less one (the average covers exactly the window that held this sample), plus the detector's own delay
        const int readAt = (delayAt + 1) % length;
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& line = delay[(size_t) ch];
            line[(size_t) delayAt] = buffers[ch] != nullptr ? buffers[ch][i] : 0.0f;
            if (buffers[ch] != nullptr)
                buffers[ch][i] = line[(size_t) readAt] * gain;
        }
        delayAt = readAt;
    }

    // the running sum again from scratch, so rounding can't build up over a long show
    rampSum = 0.0;
    for (const auto g : ramp)
        rampSum += (double) g;

    reduction.store (-juce::Decibels::gainToDecibels (lowest, -120.0f));
}
