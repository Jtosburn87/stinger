#include "StockFx.h"

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    const char* const formatName = "Stinger";
    const char* const eqId = "stinger:eq";
    const char* const compressorId = "stinger:compressor";
    const char* const limiterId = "stinger:limiter";
    constexpr double lowShelfHz = 120.0, highShelfHz = 8000.0, midQ = 1.0;
    constexpr float lowCutOff = 20.0f;   // the Low Cut slider's bottom = no filter
    constexpr float kneeDb = 6.0f;

    std::vector<StockFx::Param> paramsFor (StockFx::Kind kind)
    {
        if (kind == StockFx::Kind::eq)
            return { { "lowCut",   "Low cut",  "Hz", lowCutOff, 400.0f,  lowCutOff, 1.0f,  true },
                     { "lowGain",  "Low",      "dB", -12.0f,    12.0f,   0.0f,      0.5f,  false },
                     { "midGain",  "Mid",      "dB", -12.0f,    12.0f,   0.0f,      0.5f,  false },
                     { "midFreq",  "Mid freq", "Hz", 200.0f,    5000.0f, 1000.0f,   10.0f, true },
                     { "highGain", "High",     "dB", -12.0f,    12.0f,   0.0f,      0.5f,  false } };
        if (kind == StockFx::Kind::limiter)
            return { { "threshold",   "Threshold",   "dB", -30.0f, 0.0f,  0.0f, 0.1f,  false },
                     { "output",      "Out ceiling", "dB", -30.0f, 0.0f,  0.0f, 0.1f,  false },
                     { "release",     "Release",     "",   0.1f,   10.0f, 1.0f, 0.01f, true },
                     { "wideBand",    "Wide band",   "",   0.0f,   1.0f,  0.0f, 1.0f,  false, true },
                     { "link",        "Link",        "",   0.0f,   1.0f,  0.0f, 1.0f,  false, true },
                     { "interSample", "ISP",         "",   0.0f,   1.0f,  0.0f, 1.0f,  false, true } };
        return { { "threshold", "Threshold", "dB", -60.0f, 0.0f,    -18.0f, 0.5f, false },
                 { "ratio",     "Ratio",     ":1", 1.0f,   20.0f,   4.0f,   0.1f, true },
                 { "attack",    "Attack",    "ms", 0.1f,   100.0f,  10.0f,  0.1f, true },
                 { "release",   "Release",   "ms", 10.0f,  1000.0f, 150.0f, 1.0f, true },
                 { "makeup",    "Makeup",    "dB", 0.0f,   24.0f,   0.0f,   0.5f, false } };
    }

    void setCoefficients (StockFx::Biquad& f, double b0, double b1, double b2, double a0, double a1, double a2)
    {
        f.b0 = (float) (b0 / a0);
        f.b1 = (float) (b1 / a0);
        f.b2 = (float) (b2 / a0);
        f.a1 = (float) (a1 / a0);
        f.a2 = (float) (a2 / a0);
    }

    float flushed (float state)
    {
        return std::abs (state) < 1.0e-15f ? 0.0f : state;   // no denormals in the tail
    }
}

// The filters are the usual RBJ cookbook shapes.
void StockFx::Biquad::run (float* samples, int channel, int numSamples)
{
    float s1 = z1[(size_t) channel], s2 = z2[(size_t) channel];
    for (int i = 0; i < numSamples; ++i)
    {
        const float in = samples[i];
        const float out = b0 * in + s1;
        s1 = b1 * in - a1 * out + s2;
        s2 = b2 * in - a2 * out;
        samples[i] = out;
    }
    z1[(size_t) channel] = flushed (s1);
    z2[(size_t) channel] = flushed (s2);
}

void StockFx::Biquad::highPass (double sampleRate, double frequency)
{
    const double w = twoPi * frequency / sampleRate, c = std::cos (w), alpha = std::sin (w) / (2.0 * std::sqrt (0.5));
    setCoefficients (*this, (1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

void StockFx::Biquad::lowShelf (double sampleRate, double frequency, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0), w = twoPi * frequency / sampleRate, c = std::cos (w);
    const double beta = 2.0 * std::sqrt (A) * std::sin (w) / (2.0 * std::sqrt (0.5));
    setCoefficients (*this, A * ((A + 1) - (A - 1) * c + beta), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - beta),
                     (A + 1) + (A - 1) * c + beta, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - beta);
}

void StockFx::Biquad::highShelf (double sampleRate, double frequency, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0), w = twoPi * frequency / sampleRate, c = std::cos (w);
    const double beta = 2.0 * std::sqrt (A) * std::sin (w) / (2.0 * std::sqrt (0.5));
    setCoefficients (*this, A * ((A + 1) + (A - 1) * c + beta), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - beta),
                     (A + 1) - (A - 1) * c + beta, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - beta);
}

void StockFx::Biquad::peak (double sampleRate, double frequency, double gainDb, double q)
{
    const double A = std::pow (10.0, gainDb / 40.0), w = twoPi * frequency / sampleRate, c = std::cos (w);
    const double alpha = std::sin (w) / (2.0 * q);
    setCoefficients (*this, 1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A);
}

juce::PluginDescription StockFx::describe (Kind kind)
{
    juce::PluginDescription d;
    d.name = kind == Kind::eq ? "Stinger EQ" : kind == Kind::compressor ? "Stinger Compressor" : "Stinger Limiter";
    d.descriptiveName = d.name;
    d.pluginFormatName = formatName;
    d.category = "Effect";
    d.manufacturerName = "LEFTNoiseAudio";
    d.version = "1.0";
    d.fileOrIdentifier = kind == Kind::eq ? eqId : kind == Kind::compressor ? compressorId : limiterId;
    d.uniqueId = kind == Kind::eq ? 0x53744571 : kind == Kind::compressor ? 0x5374436d : 0x53744c6d;
    d.deprecatedUid = d.uniqueId;
    d.isInstrument = false;
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
    return d;
}

bool StockFx::isStock (const juce::PluginDescription& d)
{
    return d.pluginFormatName == formatName;
}

std::unique_ptr<juce::AudioPluginInstance> StockFx::create (Kind kind)
{
    return std::unique_ptr<juce::AudioPluginInstance> (new StockFx (kind));
}

std::unique_ptr<juce::AudioPluginInstance> StockFx::create (const juce::PluginDescription& d)
{
    if (d.fileOrIdentifier == eqId)
        return create (Kind::eq);
    if (d.fileOrIdentifier == compressorId)
        return create (Kind::compressor);
    if (d.fileOrIdentifier == limiterId)
        return create (Kind::limiter);
    return nullptr;
}

StockFx::StockFx (Kind k)
    : AudioPluginInstance (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true)
                                            .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
      kind (k), params (paramsFor (k))
{
    for (size_t i = 0; i < params.size(); ++i)
        values[i].store (params[i].initial);
}

void StockFx::moveLimiterFader (int i, float value)
{
    if (kind != Kind::limiter || (i != limitThreshold && i != limitOutput))
        return;
    const int other = i == limitThreshold ? limitOutput : limitThreshold;
    if (get (limitLink) < 0.5f)
    {
        set (i, value);
        return;
    }
    // both move by the same amount, as far as they both can
    const auto& p = params[(size_t) other];
    const auto& q = params[(size_t) i];
    const float delta = juce::jlimit (juce::jmax (p.minimum - get (other), q.minimum - get (i)),
                                      juce::jmin (p.maximum - get (other), q.maximum - get (i)), value - get (i));
    set (i, get (i) + delta);
    set (other, get (other) + delta);
}

void StockFx::set (int i, float value)
{
    if (i < 0 || i >= numParams())
        return;
    values[(size_t) i].store (juce::jlimit (params[(size_t) i].minimum, params[(size_t) i].maximum, value));
    eqChanged.store (true);
}

const juce::String StockFx::getName() const
{
    return describe (kind).name;
}

void StockFx::prepareToPlay (double sampleRate, int)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    lowCut.reset();
    low.reset();
    mid.reset();
    high.reset();
    envelopeDb = 0.0f;
    limiter.prepare (rate);
    if (kind == Kind::limiter)
    {
        driveGain = juce::Decibels::decibelsToGain (-get (limitThreshold));
        outputGain = juce::Decibels::decibelsToGain (get (limitOutput));
        setLatencySamples (limiter.getLatency());
    }
    reduction.store (0.0f);
    eqChanged.store (true);
}

void StockFx::updateEq()
{
    const auto cut = get (eqLowCut);
    lowCutOn = cut > lowCutOff + 0.5f;
    if (lowCutOn)
        lowCut.highPass (rate, cut);
    low.lowShelf (rate, lowShelfHz, get (eqLowGain));
    mid.peak (rate, get (eqMidFreq), get (eqMidGain), midQ);
    high.highShelf (rate, std::min (highShelfHz, rate * 0.45), get (eqHighGain));
}

void StockFx::runEq (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples)
{
    if (eqChanged.exchange (false))
        updateEq();
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* s = buffer.getWritePointer (ch);
        if (lowCutOn)
            lowCut.run (s, ch, numSamples);
        low.run (s, ch, numSamples);
        mid.run (s, ch, numSamples);
        high.run (s, ch, numSamples);
    }
}

void StockFx::runCompressor (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples)
{
    const float threshold = get (compThreshold), slope = 1.0f - 1.0f / get (compRatio);
    const float makeup = juce::Decibels::decibelsToGain (get (compMakeup));
    const auto attack = (float) std::exp (-1.0 / (rate * get (compAttack) * 0.001));
    const auto release = (float) std::exp (-1.0 / (rate * get (compRelease) * 0.001));
    float deepest = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;   // both sides move together, so the stereo picture holds still
        for (int ch = 0; ch < numChannels; ++ch)
            peak = std::max (peak, std::abs (buffer.getReadPointer (ch)[i]));

        const float over = juce::Decibels::gainToDecibels (peak, -120.0f) - threshold;
        float wanted = 0.0f;
        if (over >= kneeDb * 0.5f)
            wanted = slope * over;
        else if (over > -kneeDb * 0.5f)
            wanted = slope * (over + kneeDb * 0.5f) * (over + kneeDb * 0.5f) / (2.0f * kneeDb);

        const float coefficient = wanted > envelopeDb ? attack : release;
        envelopeDb = wanted + coefficient * (envelopeDb - wanted);
        deepest = std::max (deepest, envelopeDb);

        const float gain = juce::Decibels::decibelsToGain (-envelopeDb) * makeup;
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= gain;
    }
    reduction.store (deepest);
}

void StockFx::runLimiter (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples)
{
    // Threshold brings the sound up against full scale, the limiter holds it there, Out ceiling sets the final
    // level: a lower threshold is louder, and a lower ceiling turns all of it down without limiting any harder
    const float drive = juce::Decibels::decibelsToGain (-get (limitThreshold));
    const float output = juce::Decibels::decibelsToGain (get (limitOutput));
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.applyGainRamp (ch, 0, numSamples, driveGain, drive);
    driveGain = drive;

    limiter.setRelease (get (limitRelease));
    limiter.setWideBand (get (limitWideBand) >= 0.5f);
    limiter.setInterSample (get (limitInterSample) >= 0.5f);
    limiter.process (buffer.getArrayOfWritePointers(), numSamples);
    reduction.store (limiter.gainReductionDb());

    for (int ch = 0; ch < numChannels; ++ch)
        buffer.applyGainRamp (ch, 0, numSamples, outputGain, output);
    outputGain = output;
}

void StockFx::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int channels = std::min (2, buffer.getNumChannels());
    if (kind == Kind::eq)
        runEq (buffer, channels, buffer.getNumSamples());
    else if (kind == Kind::compressor)
        runCompressor (buffer, channels, buffer.getNumSamples());
    else
        runLimiter (buffer, channels, buffer.getNumSamples());
}

void StockFx::getStateInformation (juce::MemoryBlock& out)
{
    juce::XmlElement xml ("STOCKFX");
    for (int i = 0; i < numParams(); ++i)
        xml.setAttribute (params[(size_t) i].id, get (i));
    copyXmlToBinary (xml, out);
}

void StockFx::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        for (int i = 0; i < numParams(); ++i)
            set (i, (float) xml->getDoubleAttribute (params[(size_t) i].id, params[(size_t) i].initial));
}
