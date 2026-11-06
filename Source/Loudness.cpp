#include "Loudness.h"
#include <cmath>

namespace
{
    /** One channel of the K-weighting filter pair at any sample rate (the BS.1770 48 kHz curve, re-derived). */
    struct KFilter
    {
        explicit KFilter (double sr)
        {
            {
                constexpr double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
                const double k = std::tan (juce::MathConstants<double>::pi * f0 / sr);
                const double vh = std::pow (10.0, g / 20.0), vb = std::pow (vh, 0.4996667741545416);
                const double a0 = 1.0 + k / q + k * k;
                b0 = (vh + vb * k / q + k * k) / a0;
                b1 = 2.0 * (k * k - vh) / a0;
                b2 = (vh - vb * k / q + k * k) / a0;
                a1 = 2.0 * (k * k - 1.0) / a0;
                a2 = (1.0 - k / q + k * k) / a0;
            }
            {
                constexpr double f0 = 38.13547087602444, q = 0.5003270373238773;
                const double k = std::tan (juce::MathConstants<double>::pi * f0 / sr);
                const double a0 = 1.0 + k / q + k * k;
                c1 = 2.0 * (k * k - 1.0) / a0;
                c2 = (1.0 - k / q + k * k) / a0;
            }
        }

        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            const double out = y - 2.0 * w1 + w2 - c1 * r1 - c2 * r2;
            w2 = w1;
            w1 = y;
            r2 = r1;
            r1 = out;
            return out;
        }

        double b0, b1, b2, a1, a2, c1, c2;
        double z1 = 0.0, z2 = 0.0, w1 = 0.0, w2 = 0.0, r1 = 0.0, r2 = 0.0;
    };
}

LoudnessResult measureLoudness (const CueAudio& audio, const std::vector<Segment>& parts)
{
    LoudnessResult result;
    const double sr = audio.sampleRate;
    const int channels = std::min (2, audio.numChannels());
    if (sr <= 0.0 || channels <= 0)
        return result;

    // energy of every 100 ms step; a gating block is four steps (400 ms, overlapping by 75%)
    const int step = (int) std::round (sr * 0.1);
    std::vector<double> steps;
    KFilter filters[2] = { KFilter (sr), KFilter (sr) };
    double energy = 0.0, peak = 0.0;
    int filled = 0;
    for (auto& part : parts)
    {
        const int first = juce::jlimit (0, audio.samples.getNumSamples(), (int) (part.start * sr));
        const int last = juce::jlimit (first, audio.samples.getNumSamples(), (int) (part.end * sr));
        for (int i = first; i < last; ++i)
        {
            double sum = 0.0;
            for (int ch = 0; ch < channels; ++ch)
            {
                const double x = audio.samples.getSample (ch, i);
                peak = std::max (peak, std::abs (x));
                const double y = filters[ch].process (x);
                sum += y * y;
            }
            energy += sum;
            if (++filled == step)
            {
                steps.push_back (energy / step);
                energy = 0.0;
                filled = 0;
            }
        }
    }
    result.peakDb = peak > 0.0 ? 20.0 * std::log10 (peak) : -120.0;
    if (steps.size() < 4)
        return result;

    std::vector<double> blocks;
    for (size_t i = 0; i + 3 < steps.size(); ++i)
        blocks.push_back ((steps[i] + steps[i + 1] + steps[i + 2] + steps[i + 3]) / 4.0);

    auto toLufs = [] (double power) { return -0.691 + 10.0 * std::log10 (power); };
    double sum = 0.0;
    int count = 0;
    for (auto b : blocks)
        if (b > 0.0 && toLufs (b) > -70.0)      // absolute gate
        {
            sum += b;
            ++count;
        }
    if (count == 0)
        return result;
    const double relativeGate = toLufs (sum / count) - 10.0;
    sum = 0.0;
    count = 0;
    for (auto b : blocks)
        if (b > 0.0 && toLufs (b) > relativeGate)
        {
            sum += b;
            ++count;
        }
    if (count > 0)
        result.lufs = std::min (-0.001, toLufs (sum / count));
    return result;
}

juce::String loudnessKey (const std::vector<Segment>& parts)
{
    juce::String key;
    for (auto& p : parts)
        key << juce::String (p.start, 2) << "-" << juce::String (p.end, 2) << ";";
    return juce::String (key.hashCode64());
}
