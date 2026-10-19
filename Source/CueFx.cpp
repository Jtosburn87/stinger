#include "CueFx.h"

CueFx::~CueFx()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& s : slots)
        if (s.plugin != nullptr)
            s.plugin->releaseResources();
}

void CueFx::prepareOne (juce::AudioProcessor& p)
{
    // stereo in, stereo out where the plugin allows it
    juce::AudioProcessor::BusesLayout stereo;
    stereo.inputBuses.add (juce::AudioChannelSet::stereo());
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    if (p.getBusCount (true) > 0 && p.getBusCount (false) > 0)
        p.setBusesLayout (stereo);
    p.setRateAndBufferSizeDetails (sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
}

void CueFx::setSlot (int slot, std::unique_ptr<juce::AudioProcessor> plugin, const juce::PluginDescription& description)
{
    if (slot < 0 || slot >= numSlots)
        return;
    if (plugin != nullptr)
        prepareOne (*plugin);
    std::unique_ptr<juce::AudioProcessor> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (slots[(size_t) slot].plugin);
        slots[(size_t) slot].plugin = std::move (plugin);
        slots[(size_t) slot].description = description;
        slots[(size_t) slot].bypassed = false;
        int channels = 2;
        for (auto& s : slots)
            if (s.plugin != nullptr)
                channels = std::max ({ channels, s.plugin->getTotalNumInputChannels(), s.plugin->getTotalNumOutputChannels() });
        work.setSize (channels, blockSize, false, false, true);
    }
    updateTail();
    if (old != nullptr)
        old->releaseResources();   // deleted here, outside the lock
}

void CueFx::updateTail()
{
    double longest = 2.0;
    for (auto& s : slots)
        if (s.plugin != nullptr)
            longest = std::max (longest, s.plugin->getTailLengthSeconds());
    tail = std::min (longest, 30.0);
}

void CueFx::setBypassed (int slot, bool bypassed)
{
    if (slot >= 0 && slot < numSlots)
        slots[(size_t) slot].bypassed = bypassed;
}

bool CueFx::isBypassed (int slot) const
{
    return slot >= 0 && slot < numSlots && slots[(size_t) slot].bypassed.load();
}

juce::AudioProcessor* CueFx::getPlugin (int slot) const
{
    return slot >= 0 && slot < numSlots ? slots[(size_t) slot].plugin.get() : nullptr;
}

juce::PluginDescription CueFx::getDescription (int slot) const
{
    return slot >= 0 && slot < numSlots ? slots[(size_t) slot].description : juce::PluginDescription();
}

bool CueFx::isEmpty() const
{
    for (auto& s : slots)
        if (s.plugin != nullptr)
            return false;
    return true;
}

void CueFx::prepare (double rate, int maxBlockSize)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    sampleRate = rate;
    blockSize = std::max (32, maxBlockSize);
    int channels = 2;
    for (auto& s : slots)
        if (s.plugin != nullptr)
        {
            s.plugin->releaseResources();
            prepareOne (*s.plugin);
            channels = std::max ({ channels, s.plugin->getTotalNumInputChannels(), s.plugin->getTotalNumOutputChannels() });
        }
    work.setSize (channels, blockSize, false, false, true);
}

juce::String CueFx::getState (int slot) const
{
    auto* p = getPlugin (slot);
    if (p == nullptr)
        return {};
    juce::MemoryBlock mb;
    p->getStateInformation (mb);
    return mb.toBase64Encoding();
}

void CueFx::setState (int slot, const juce::String& base64)
{
    auto* p = getPlugin (slot);
    juce::MemoryBlock mb;
    if (p != nullptr && base64.isNotEmpty() && mb.fromBase64Encoding (base64))
        p->setStateInformation (mb.getData(), (int) mb.getSize());
}

bool CueFx::process (juce::AudioBuffer<float>& stereo, int numSamples)
{
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked())
        return false;
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (numSamples - done, work.getNumSamples());
        for (auto& s : slots)
        {
            if (s.plugin == nullptr || s.bypassed.load())
                continue;
            auto& p = *s.plugin;
            const int channels = std::max (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
            if (channels <= 0 || channels > work.getNumChannels())
                continue;
            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), channels, n);
            view.clear();
            const int ins = std::min (2, std::max (1, p.getTotalNumInputChannels()));
            if (ins == 1)
            {
                view.copyFrom (0, 0, stereo, 0, done, n);
                view.addFrom (0, 0, stereo, 1, done, n);
                view.applyGain (0, 0, n, 0.5f);
            }
            else
            {
                view.copyFrom (0, 0, stereo, 0, done, n);
                view.copyFrom (1, 0, stereo, 1, done, n);
            }
            midi.clear();
            p.processBlock (view, midi);
            const int outs = p.getTotalNumOutputChannels();
            stereo.copyFrom (0, done, view, 0, 0, n);
            stereo.copyFrom (1, done, view, outs >= 2 ? 1 : 0, 0, n);
        }
        done += n;
    }
    return true;
}
