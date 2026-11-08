#include "MultibandLimiter.h"

namespace
{
    // where the L3's bands meet, measured with quiet tones under a loud one
    constexpr std::array<double, MultibandLimiter::maxBands - 1> splitsHz { 120.0, 500.0, 1700.0, 5400.0 };
}

float MultibandLimiter::Filter::tick (float in, int channel)
{
    auto& s1 = z1[(size_t) channel];
    auto& s2 = z2[(size_t) channel];
    const float out = b0 * in + s1;
    s1 = b1 * in - a1 * out + s2;
    s2 = b2 * in - a2 * out;
    return out;
}

void MultibandLimiter::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    lookahead = std::max (2, (int) std::round (rate * 0.00125));

    for (size_t i = 0; i < lows.size(); ++i)
    {
        // 12 dB per octave, gentle like the L3's overlaps. Low minus high is an all-pass, which is why the
        // bands that skip this crossover go through that same all-pass: then everything stays in step
        const double w = juce::MathConstants<double>::twoPi * std::min (splitsHz[i], rate * 0.45) / rate;
        const double c = std::tan (w * 0.5), c2 = c * c, a0 = 1.0 + 2.0 * c + c2;

        lows[i].b0 = lows[i].b2 = (float) (c2 / a0);
        lows[i].b1 = (float) (2.0 * c2 / a0);
        highs[i].b0 = highs[i].b2 = (float) (1.0 / a0);
        highs[i].b1 = (float) (-2.0 / a0);
        for (auto* f : { &lows[i], &highs[i] })
        {
            f->a1 = (float) (2.0 * (c2 - 1.0) / a0);
            f->a2 = (float) ((1.0 - 2.0 * c + c2) / a0);
        }

        const auto allPass = (float) ((c - 1.0) / (c + 1.0));
        for (auto& band : keepInStep)
        {
            band[i].b0 = allPass;
            band[i].b1 = 1.0f;
            band[i].b2 = 0.0f;
            band[i].a1 = allPass;
            band[i].a2 = 0.0f;
        }
    }
    for (auto& b : bands)
    {
        for (auto& d : b.delay)
            d.assign ((size_t) lookahead, 0.0f);
        b.window.assign ((size_t) lookahead, 1.0f);
        b.ramp.assign ((size_t) lookahead, 1.0f);
    }
    last.prepare (rate, 2);
    last.setCeilingDb (0.0f);
    appliedRelease = -1.0f;
    reset();
}

void MultibandLimiter::reset()
{
    auto clear = [] (Filter& f) { f.z1 = {}; f.z2 = {}; };
    for (size_t i = 0; i < lows.size(); ++i)
    {
        clear (lows[i]);
        clear (highs[i]);
        for (auto& band : keepInStep)
            clear (band[i]);
    }
    for (auto& b : bands)
    {
        for (auto& d : b.delay)
            std::fill (d.begin(), d.end(), 0.0f);
        std::fill (b.window.begin(), b.window.end(), 1.0f);
        std::fill (b.ramp.begin(), b.ramp.end(), 1.0f);
        b.first = b.second = 0.0f;
        b.held = 1.0f;
        b.minimum = 1.0f;
        b.rampSum = (double) lookahead;
    }
    at = 0;
    last.reset();
    reduction.store (0.0f);
}

void MultibandLimiter::updateRelease (float amount)
{
    if (amount == appliedRelease)
        return;
    appliedRelease = amount;
    // the L3's release is two smoothing stages in a row: measured time constants 33 ms at 0.1, 105 ms at 1, 550 ms at 10
    const float position = std::log10 (juce::jlimit (0.1f, 10.0f, amount));
    const float timeMs = position < 0.0f ? juce::jmap (position, -1.0f, 0.0f, 1.519f, 2.021f)
                                         : juce::jmap (position, 0.0f, 1.0f, 2.021f, 2.740f);
    releaseCoefficient = 1.0f - (float) std::exp (-1.0 / (rate * 0.001 * std::pow (10.0, (double) timeMs)));
}

void MultibandLimiter::process (float* const* channels, int numSamples)
{
    if (bands[0].window.empty())
        return;

    const bool wide = wideBand.load();
    if (wide != wasWide)
    {
        wasWide = wide;
        reset();
    }
    updateRelease (release.load());
    const int count = wide ? 1 : maxBands;
    double energyIn = 0.0, energyOut = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < numSamples; ++i)
            energyIn += (double) channels[ch][i] * channels[ch][i];

    for (int i = 0; i < numSamples; ++i)
    {
        std::array<std::array<float, maxBands>, 2> split;
        std::array<float, maxBands> gain;
        gain.fill (1.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& parts = split[(size_t) ch];
            float rest = channels[ch][i];
            for (int b = 0; b < count - 1; ++b)
            {
                parts[(size_t) b] = lows[(size_t) b].tick (rest, ch);
                rest = -highs[(size_t) b].tick (rest, ch);
            }
            parts[(size_t) (count - 1)] = rest;
            for (int b = 0; b < count - 1; ++b)
                for (int later = b + 1; later < count - 1; ++later)
                    parts[(size_t) b] = keepInStep[(size_t) b][(size_t) later].tick (parts[(size_t) b], ch);
        }

        // each band held to full scale on its own
        for (int b = 0; b < count; ++b)
        {
            const float top = std::max (std::abs (split[0][(size_t) b]), std::abs (split[1][(size_t) b]));
            gain[(size_t) b] = std::min (bands[(size_t) b].held, top > 1.0f ? 1.0f / top : 1.0f);
        }

        // too much added together: the loudest give way, all down to one level just low enough to fit,
        // and whatever is under that level isn't touched
        for (int ch = 0; ch < 2 && count > 1; ++ch)
        {
            float sum = 0.0f;
            for (int b = 0; b < count; ++b)
                sum += split[(size_t) ch][(size_t) b] * gain[(size_t) b];
            if (std::abs (sum) <= 1.0f)
                continue;

            const float direction = sum > 0.0f ? 1.0f : -1.0f;
            std::array<float, maxBands> parts;
            int numParts = 0;
            float room = 1.0f;    // full scale, plus whatever pushes the other way
            for (int b = 0; b < count; ++b)
            {
                const float part = split[(size_t) ch][(size_t) b] * gain[(size_t) b] * direction;
                if (part > 0.0f)
                    parts[(size_t) numParts++] = part;
                else
                    room -= part;
            }
            std::sort (parts.begin(), parts.begin() + numParts, std::greater<float>());

            float level = room, below = 0.0f;
            for (int p = 0; p < numParts; ++p)
                below += parts[(size_t) p];
            for (int top = 1; top <= numParts; ++top)
            {
                below -= parts[(size_t) (top - 1)];
                level = (room - below) / (float) top;
                if (top == numParts || level >= parts[(size_t) top])
                    break;
            }
            for (int b = 0; b < count; ++b)
            {
                const float part = split[(size_t) ch][(size_t) b] * gain[(size_t) b] * direction;
                if (part > level)
                    gain[(size_t) b] *= level / part;
            }
        }

        for (int b = 0; b < count; ++b)
        {
            auto& band = bands[(size_t) b];

            // up at once, back down through the two release stages
            // (only a gain lower than the band already has asks for more: otherwise it would hold itself down for ever)
            const float wanted = gain[(size_t) b] < band.held * 0.99999f ? -20.0f * std::log10 (gain[(size_t) b]) : 0.0f;
            band.first = std::max (wanted, band.first * (1.0f - releaseCoefficient));
            band.second = std::max (wanted, band.second + (band.first - band.second) * releaseCoefficient);
            const float moving = std::pow (10.0f, -band.second * 0.05f);
            band.held = moving;

            // the lowest gain anywhere in the look-ahead, then smoothed over the same length
            const float leaving = band.window[(size_t) at];
            band.window[(size_t) at] = moving;
            if (moving <= band.minimum)
                band.minimum = moving;
            else if (leaving <= band.minimum)
                band.minimum = *std::min_element (band.window.begin(), band.window.end());

            band.rampSum += (double) band.minimum - (double) band.ramp[(size_t) at];
            band.ramp[(size_t) at] = band.minimum;
            const auto smooth = (float) (band.rampSum / (double) lookahead);

            const int readAt = (at + 1) % lookahead;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& line = band.delay[(size_t) ch];
                line[(size_t) at] = split[(size_t) ch][(size_t) b];
                split[(size_t) ch][(size_t) b] = line[(size_t) readAt] * smooth;
            }
        }
        at = (at + 1) % lookahead;

        for (int ch = 0; ch < 2; ++ch)
        {
            float sum = 0.0f;
            for (int b = 0; b < count; ++b)
                sum += split[(size_t) ch][(size_t) b];
            channels[ch][i] = sum;
        }
    }

    for (auto& band : bands)
    {
        band.rampSum = 0.0;   // from scratch, so rounding can't build up over a long show
        for (const auto g : band.ramp)
            band.rampSum += (double) g;
    }

    last.process (channels, 2, numSamples);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < numSamples; ++i)
            energyOut += (double) channels[ch][i] * channels[ch][i];
    const auto ratio = energyOut > 1.0e-12 && energyIn > 1.0e-12 ? 10.0 * std::log10 (energyIn / energyOut) : 0.0;
    reduction.store ((float) std::max (0.0, ratio));
}
