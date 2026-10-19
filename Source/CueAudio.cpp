#include "CueAudio.h"
#include <juce_events/juce_events.h>

juce::int64 CueAudio::wavSamplesInFile (const juce::File& file, int bytesPerFrame)
{
    // ffmpeg writing to a pipe can't go back to fill in the sizes, so the data chunk says 0xFFFFFFFF bytes:
    // what's really there is the rest of the file.
    juce::FileInputStream in (file);
    if (! in.openedOk() || bytesPerFrame <= 0 || in.readInt() != (int) juce::ByteOrder::littleEndianInt ("RIFF"))
        return -1;
    in.readInt();
    if (in.readInt() != (int) juce::ByteOrder::littleEndianInt ("WAVE"))
        return -1;
    while (in.getNumBytesRemaining() >= 8)
    {
        const int id = in.readInt();
        const auto size = (juce::uint32) in.readInt();
        if (id == (int) juce::ByteOrder::littleEndianInt ("data"))
            return std::min<juce::int64> (size, in.getNumBytesRemaining()) / bytesPerFrame;
        in.setPosition (in.getPosition() + size + (size & 1));
    }
    return -1;
}

std::shared_ptr<CueAudio> CueAudio::load (const juce::File& file, double targetRate,
                                          juce::AudioFormatManager& formats, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = file.existsAsFile() ? "Can't read this file type: " + file.getFileName()
                                    : "File not found: " + file.getFullPathName();
        return nullptr;
    }
    if (file.hasFileExtension ("wav;wave"))
    {
        const auto real = wavSamplesInFile (file, (int) (reader->numChannels * reader->bitsPerSample / 8));
        if (real >= 0 && real < reader->lengthInSamples)
            reader->lengthInSamples = real;
    }
    if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
    {
        error = "The file is empty: " + file.getFileName();
        return nullptr;
    }
    if (reader->lengthInSamples > std::numeric_limits<int>::max() - 16
        || reader->lengthInSamples / reader->sampleRate > maxSeconds)
    {
        error = "The file is too long: " + file.getFileName();
        return nullptr;
    }

    auto audio = std::make_shared<CueAudio>();
    audio->fileSampleRate = reader->sampleRate;
    audio->fileChannels = (int) reader->numChannels;
    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    const int fileLength = (int) reader->lengthInSamples;

    juce::AudioBuffer<float> raw (channels, fileLength + 8);   // a little zero padding for the interpolator
    raw.clear();
    reader->read (&raw, 0, fileLength, 0, true, channels > 1);

    if (targetRate <= 0.0 || std::abs (targetRate - reader->sampleRate) < 0.01)
    {
        raw.setSize (channels, fileLength, true);
        audio->samples = std::move (raw);
        audio->sampleRate = reader->sampleRate;
    }
    else
    {
        const double ratio = reader->sampleRate / targetRate;   // input samples per output sample
        const int outLength = std::max (1, (int) std::floor (fileLength / ratio));
        audio->samples.setSize (channels, outLength);
        for (int ch = 0; ch < channels; ++ch)
        {
            juce::LagrangeInterpolator interpolator;
            interpolator.process (ratio, raw.getReadPointer (ch), audio->samples.getWritePointer (ch), outLength);
        }
        audio->sampleRate = targetRate;
    }

    const int n = audio->samples.getNumSamples();
    const int blocks = (n + peakBlock - 1) / peakBlock;
    audio->peakMin.assign ((size_t) blocks, 0.0f);
    audio->peakMax.assign ((size_t) blocks, 0.0f);
    for (int b = 0; b < blocks; ++b)
    {
        const int start = b * peakBlock, count = std::min (peakBlock, n - start);
        float lo = 0.0f, hi = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            auto r = juce::FloatVectorOperations::findMinAndMax (audio->samples.getReadPointer (ch, start), count);
            lo = std::min (lo, r.getStart());
            hi = std::max (hi, r.getEnd());
        }
        audio->peakMin[(size_t) b] = lo;
        audio->peakMax[(size_t) b] = hi;
    }
    return audio;
}

AudioLoader::AudioLoader()
{
    formats.registerBasicFormats();
}

AudioLoader::~AudioLoader()
{
    alive->store (false);
    pool.removeAllJobs (true, 30000);
}

void AudioLoader::load (const juce::File& file, double targetRate, Done done)
{
    ++queued;
    pool.addJob ([this, file, targetRate, done = std::move (done), alive = alive]
    {
        if (! alive->load())
            return;
        juce::String error;
        auto audio = CueAudio::load (file, targetRate, formats, error);
        juce::MessageManager::callAsync ([this, alive, audio, error, done]
        {
            if (! alive->load())
                return;
            --queued;
            done (audio, error);
        });
    });
}

juce::String AudioLoader::wildcards() const
{
    return formats.getWildcardForAllFormats();
}

double CueAudio::nearestZeroCrossing (double t) const
{
    const int n = samples.getNumSamples();
    if (n < 2 || sampleRate <= 0.0)
        return t;
    const int centre = juce::jlimit (1, n - 1, (int) std::llround (t * sampleRate));
    const int reach = (int) (sampleRate * 0.003);
    auto value = [this] (int i)
    {
        float v = 0.0f;
        for (int ch = 0; ch < samples.getNumChannels(); ++ch)
            v += samples.getSample (ch, i);
        return v;
    };
    for (int d = 0; d <= reach; ++d)
        for (int i : { centre - d, centre + d })
            if (i >= 1 && i < n)
            {
                const float a = value (i - 1), b = value (i);
                if (b == 0.0f || (a < 0.0f) != (b < 0.0f))
                    return i / sampleRate;
            }
    return t;
}
