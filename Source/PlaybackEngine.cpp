#include "PlaybackEngine.h"
#include <cmath>

bool PlayPlan::make (const Cue& cue, const CueAudio& audio, int cueId, PlayPlan& plan)
{
    plan = PlayPlan();
    const double sr = audio.sampleRate;
    const double fileSeconds = audio.seconds();
    const juce::int64 total = audio.samples.getNumSamples();
    if (sr <= 0.0 || total <= 0)
        return false;

    plan.audio = &audio;
    plan.cueId = cueId;
    juce::int64 timeline = 0;
    for (auto& s : cue.segments (fileSeconds))
    {
        if (plan.numSegments == maxSegments)
            break;
        const auto start = juce::jlimit<juce::int64> (0, total, (juce::int64) std::llround (s.start * sr));
        const auto end = juce::jlimit<juce::int64> (start, total, (juce::int64) std::llround (s.end * sr));
        if (end <= start)
            continue;
        plan.segments[(size_t) plan.numSegments++] = { start, end, timeline };
        timeline += end - start;
    }
    if (timeline <= 0)
        return false;

    plan.length = timeline;
    plan.startAt = 0;
    plan.fadeInLength = std::min (timeline, (juce::int64) std::llround (cue.fadeIn.seconds * sr));
    plan.fadeOutLength = std::min (timeline, (juce::int64) std::llround (cue.fadeOut.seconds * sr));
    plan.stopFadeLength = (juce::int64) std::llround (cue.stopFade.seconds * sr);
    plan.fadeInCurve = cue.fadeIn.curve;
    plan.fadeOutCurve = cue.fadeOut.curve;
    plan.stopFadeCurve = cue.stopFade.curve;
    plan.gain = juce::Decibels::decibelsToGain (juce::jlimit (Cue::minLevelDb, Cue::maxLevelDb, cue.levelDb));
    plan.pan = juce::jlimit (-1.0f, 1.0f, cue.pan);
    plan.output = cue.output;
    plan.loop = cue.loop;
    plan.fadeOthers = cue.fadeOthers;
    return true;
}

PlaybackEngine::PlaybackEngine()
{
    inputIndex.fill (-1);
    silence.clear();
    for (auto& p : inputPeaks)
        p.store (0.0f);
    allocateBuses (4096);
    bufferIndex.fill (-1);
    for (auto& p : peaks)
        p.store (0.0f);
}

PlaybackEngine::~PlaybackEngine()
{
    stopRecording();
    writerThread.stopThread (2000);
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

void PlaybackEngine::initialise (const juce::XmlElement* savedState)
{
    // no inputs at the start: they open only while recording is armed (enableAllChannels)
    if (savedState != nullptr)
        inputChoice = savedState->getStringAttribute ("stingerInputDevice");
    deviceManager.initialise (0, 2, savedState, true);
    deviceManager.addChangeListener (this);
    enableAllChannels();
    deviceManager.addAudioCallback (this);
}

std::unique_ptr<juce::XmlElement> PlaybackEngine::createStateXml() const
{
    auto xml = deviceManager.createStateXml();
    if (xml == nullptr)
        xml = std::make_unique<juce::XmlElement> ("DEVICESETUP");
    xml->setAttribute ("stingerInputDevice", inputChoice);
    return xml;
}

juce::String PlaybackEngine::inputDeviceFor (juce::AudioIODeviceType& type, const juce::String& outputDeviceName) const
{
    if (! type.hasSeparateInputsAndOutputs())   // ASIO: inputs and outputs are one driver
        return outputDeviceName;
    const auto names = type.getDeviceNames (true);
    if (names.contains (inputChoice))
        return inputChoice;
    return names[std::max (0, type.getDefaultDeviceIndex (true))];
}

void PlaybackEngine::shutdownAudio()
{
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

void PlaybackEngine::enableAllChannels()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    auto* type = deviceManager.getCurrentDeviceTypeObject();
    if (device == nullptr || type == nullptr)
        return;
    auto setup = deviceManager.getAudioDeviceSetup();
    if (type->hasSeparateInputsAndOutputs() && setup.inputDeviceName.isNotEmpty())
        inputChoice = setup.inputDeviceName;   // picked in the Audio window (or what was open): use it when armed

    juce::BigInteger outs, ins;
    outs.setRange (0, std::min (maxOutputs, device->getOutputChannelNames().size()), true);
    // Inputs only while recording is armed, and only the chosen one(s). Disarmed, no input device is open at
    // all (Windows Audio would otherwise keep the input stream running even with no channels).
    juce::String inputName;
    if (armedInput.has_value())
    {
        inputName = inputDeviceFor (*type, setup.outputDeviceName);
        const int count = std::min (maxInputs, getInputNames().size());
        for (int ch = armedInput->firstChannel; ch < armedInput->firstChannel + armedInput->numChannels(); ++ch)
            if (ch < count)
                ins.setBit (ch);
    }
    if (ins.isZero())
        inputName = {};
    if (setup.outputChannels == outs && setup.inputChannels == ins && setup.inputDeviceName == inputName
        && ! setup.useDefaultOutputChannels && ! setup.useDefaultInputChannels)
        return;
    setup.outputChannels = outs;
    setup.inputChannels = ins;
    setup.inputDeviceName = inputName;
    setup.useDefaultOutputChannels = false;
    setup.useDefaultInputChannels = false;
    deviceManager.setAudioDeviceSetup (setup, true);
}

void PlaybackEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A cue can go to any output, so every output stays open; inputs follow the arm switch.
    inputNamesCache.clear();   // devices may have come or gone
    enableAllChannels();
}

void PlaybackEngine::setArmedInput (std::optional<OutputRoute> input)
{
    if (armedInput == input)
        return;
    armedInput = input;
    if (! input.has_value())
        for (auto& p : inputPeaks)
            p.store (0.0f);
    enableAllChannels();
}

void PlaybackEngine::prepareForTest (double rate, int outputs, int inputs)
{
    sampleRate.store (rate);
    numOutputs.store (std::min (outputs, maxOutputs));
    numInputs.store (std::min (inputs, maxInputs));
    for (int i = 0; i < maxOutputs; ++i)
        bufferIndex[(size_t) i] = i < outputs ? i : -1;
    for (int i = 0; i < maxInputs; ++i)
        inputIndex[(size_t) i] = i < inputs ? i : -1;
}

juce::StringArray PlaybackEngine::getOutputNames() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getOutputChannelNames();
    juce::StringArray names;
    for (int i = 0; i < numOutputs.load(); ++i)
        names.add ("Output " + juce::String (i + 1));
    return names;
}

juce::StringArray PlaybackEngine::getInputNames() const
{
    // the inputs arming would open, also while they're closed
    auto* device = deviceManager.getCurrentAudioDevice();
    auto* type = deviceManager.getCurrentDeviceTypeObject();
    if (device != nullptr && type != nullptr)
    {
        const auto setup = deviceManager.getAudioDeviceSetup();
        const auto name = inputDeviceFor (*type, setup.outputDeviceName);
        if (! type->hasSeparateInputsAndOutputs() || name == setup.inputDeviceName)
            return device->getInputChannelNames();
        if (name.isEmpty())
            return {};
        const auto key = type->getTypeName() + "|" + name;
        if (auto it = inputNamesCache.find (key); it != inputNamesCache.end())
            return it->second;
        // an unopened device only for its channel names (JUCE does the same to compare sample rates)
        std::unique_ptr<juce::AudioIODevice> temp (type->createDevice ({}, name));
        return inputNamesCache[key] = temp != nullptr ? temp->getInputChannelNames() : juce::StringArray();
    }
    juce::StringArray names;
    for (int i = 0; i < numInputs.load(); ++i)
        names.add ("Input " + juce::String (i + 1));
    return names;
}

// ---------------------------------------------------------------------------------------------
// Recording

bool PlaybackEngine::startRecording (const juce::File& file, OutputRoute input, juce::String& error)
{
    stopRecording();
    const double rate = sampleRate.load();
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
    {
        error = "Couldn't write " + file.getFullPathName();
        return false;
    }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, (unsigned int) input.numChannels(), 24, {}, 0));
    if (writer == nullptr)
    {
        error = "Couldn't make a WAV file at " + juce::String (rate) + " Hz";
        return false;
    }
    stream.release();   // the writer owns it now
    writerThread.startThread();
    auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, 32768);
    recordedSamples.store (0);
    {
        const juce::SpinLock::ScopedLockType sl (writerLock);
        recordInput = input;
        threadedWriter = std::move (threaded);
        recording.store (true);
    }
    recordingFile = file;
    return true;
}

juce::File PlaybackEngine::stopRecording()
{
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> finished;
    {
        const juce::SpinLock::ScopedLockType sl (writerLock);
        recording.store (false);
        finished = std::move (threadedWriter);
    }
    finished = nullptr;   // flushes the rest and closes the file
    auto f = recordingFile;
    recordingFile = juce::File();
    return f;
}

float PlaybackEngine::takeInputPeak (int channel)
{
    if (channel < 0 || channel >= maxInputs)
        return 0.0f;
    return inputPeaks[(size_t) channel].exchange (0.0f);
}

void PlaybackEngine::handleInputs (const float* const* inputs, int numInputChannels, int numSamples)
{
    auto source = [&] (int deviceChannel) -> const float*
    {
        if (deviceChannel < 0 || deviceChannel >= maxInputs)
            return nullptr;
        const int index = inputIndex[(size_t) deviceChannel];
        return index >= 0 && index < numInputChannels ? inputs[index] : nullptr;
    };
    for (int ch = 0; ch < maxInputs; ++ch)
        if (auto* in = source (ch))
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (in, numSamples);
            const float peak = std::max (-range.getStart(), range.getEnd());
            auto& p = inputPeaks[(size_t) ch];
            if (peak > p.load())
                p.store (peak);
        }

    if (! recording.load())
        return;
    const juce::SpinLock::ScopedTryLockType sl (writerLock);
    if (! sl.isLocked() || threadedWriter == nullptr || numSamples > silence.getNumSamples())
        return;
    const float* channels[2] = { source (recordInput.firstChannel), recordInput.stereo ? source (recordInput.firstChannel + 1) : nullptr };
    for (auto& c : channels)
        if (c == nullptr)
            c = silence.getReadPointer (0);   // an input that isn't there records silence
    threadedWriter->write (channels, numSamples);
    recordedSamples.fetch_add (numSamples);
}

// ---------------------------------------------------------------------------------------------
// Message thread

void PlaybackEngine::push (const Command& c)
{
    const juce::SpinLock::ScopedLockType lock (pushLock);
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0)
        commands[(size_t) scope.startIndex1] = c;
}

void PlaybackEngine::play (const PlayPlan& plan)
{
    Command c;
    c.type = Command::play;
    c.plan = plan;
    push (c);
}

void PlaybackEngine::stopCue (int cueId, bool fade)
{
    Command c;
    c.type = Command::stopCue;
    c.id = cueId;
    c.fade = fade;
    push (c);
}

void PlaybackEngine::stopVoice (int voice, bool fade)
{
    Command c;
    c.type = Command::stopVoice;
    c.id = voice;
    c.fade = fade;
    push (c);
}

void PlaybackEngine::stopAll (bool fade)
{
    Command c;
    c.type = Command::stopAll;
    c.fade = fade;
    push (c);
}

void PlaybackEngine::setLive (int cueId, float gain, float pan)
{
    Command c;
    c.type = Command::live;
    c.id = cueId;
    c.gain = gain;
    c.pan = pan;
    push (c);
}

void PlaybackEngine::follow (int afterTag, const PlayPlan& next, juce::int64 overlap)
{
    Command c;
    c.type = Command::follow;
    c.id = afterTag;
    c.plan = next;
    c.overlap = overlap;
    push (c);
}

void PlaybackEngine::seekVoice (int voice, juce::int64 position)
{
    Command c;
    c.type = Command::seek;
    c.id = voice;
    c.overlap = std::max<juce::int64> (0, position);
    push (c);
}

void PlaybackEngine::setVoiceLoop (int voice, bool loop)
{
    Command c;
    c.type = Command::setLoop;
    c.id = voice;
    c.fade = loop;
    push (c);
}

void PlaybackEngine::setCueFx (int cueId, CueFx* fx)
{
    Command c;
    c.type = Command::setFx;
    c.id = cueId;
    c.plan.fx = fx;
    push (c);
}

void PlaybackEngine::swapCues (int a, int b)
{
    Command c;
    c.type = Command::swapCues;
    c.id = a;
    c.overlap = b;
    push (c);
}

void PlaybackEngine::fadeTag (int tag, juce::int64 length)
{
    Command c;
    c.type = Command::fadeTag;
    c.id = tag;
    c.overlap = length;
    push (c);
}

void PlaybackEngine::releaseLoop (int tag)
{
    Command c;
    c.type = Command::releaseLoop;
    c.id = tag;
    push (c);
}

void PlaybackEngine::cancelFollow (int afterTag)
{
    Command c;
    c.type = Command::cancelFollow;
    c.id = afterTag;
    push (c);
}

std::vector<PlaybackEngine::VoiceInfo> PlaybackEngine::getVoices() const
{
    std::vector<VoiceInfo> list;
    const double sr = sampleRate.load();
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& s = status[(size_t) i];
        const int cue = s.cueId.load();
        if (cue < 0)
            continue;
        VoiceInfo info;
        info.voice = i;
        info.cueId = cue;
        info.tag = s.tag.load();
        info.preview = s.preview.load();
        const auto position = s.position.load(), length = s.length.load();
        info.position = std::min (position, length) / sr;
        info.length = length / sr;
        info.stopping = s.stopping.load();
        info.hasFollow = s.hasFollow.load();
        info.looping = s.looping.load();
        info.waiting = position >= length && info.hasFollow;
        if (info.waiting)
            info.waitLeft = std::max<juce::int64> (0, s.followAt.load() - position) / sr;
        info.order = s.order.load();
        list.push_back (info);
    }
    std::sort (list.begin(), list.end(), [] (auto& a, auto& b) { return a.order > b.order; });
    return list;
}

bool PlaybackEngine::isCuePlaying (int cueId) const
{
    for (auto& s : status)
        if (s.cueId.load() == cueId)
            return true;
    return false;
}

bool PlaybackEngine::isUsing (const CueAudio* audio) const
{
    if (fifo.getNumReady() > 0)
        return true;   // a play command for it may still be on its way
    for (auto& s : status)
        if (s.audio.load() == audio)
            return true;
    return false;
}

float PlaybackEngine::takeOutputPeak (int channel)
{
    if (channel < 0 || channel >= maxOutputs)
        return 0.0f;
    return peaks[(size_t) channel].exchange (0.0f);
}

// ---------------------------------------------------------------------------------------------
// Audio thread

void PlaybackEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate.store (device->getCurrentSampleRate());
    const auto active = device->getActiveOutputChannels();
    const int count = std::min (maxOutputs, device->getOutputChannelNames().size());
    int index = 0;
    for (int ch = 0; ch < maxOutputs; ++ch)
        bufferIndex[(size_t) ch] = (ch < count && active[ch]) ? index++ : -1;
    numOutputs.store (count);
    const auto activeIns = device->getActiveInputChannels();
    const int inCount = std::min (maxInputs, device->getInputChannelNames().size());
    int inIndex = 0;
    for (int ch = 0; ch < maxInputs; ++ch)
        inputIndex[(size_t) ch] = (ch < inCount && activeIns[ch]) ? inIndex++ : -1;
    numInputs.store (inCount);
    blockSize.store (device->getCurrentBufferSizeSamples());
    allocateBuses (device->getCurrentBufferSizeSamples());
}

void PlaybackEngine::audioDeviceStopped() {}

void PlaybackEngine::handleCommands()
{
    const auto scope = fifo.read (fifo.getNumReady());
    auto run = [this] (int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            auto& c = commands[(size_t) i];
            switch (c.type)
            {
                case Command::play:
                    startVoice (c.plan);
                    break;
                case Command::stopCue:
                    for (auto& v : voices)
                        if (v.active && v.plan.cueId == c.id)
                            beginStop (v, c.fade);
                    break;
                case Command::stopVoice:
                    if (c.id >= 0 && c.id < maxVoices && voices[(size_t) c.id].active)
                        beginStop (voices[(size_t) c.id], c.fade);
                    break;
                case Command::stopAll:
                    for (auto& v : voices)
                        if (v.active)
                            beginStop (v, c.fade);
                    if (! c.fade)
                        for (auto& b : buses)
                            if (b.active && ! b.muted)
                            {
                                b.muted = true;   // Stop all: tails too (faded out, then they run on silently until quiet)
                                b.muteLeft = b.muteLength = std::max (1, (int) (sampleRate.load() * 0.02));
                            }
                    break;
                case Command::follow:
                    for (auto& v : voices)
                        if (v.active && ! v.stopping && c.id != 0 && v.plan.tag == c.id)
                        {
                            v.hasFollow = true;
                            v.next = c.plan;
                            v.followOverlap = std::min (c.overlap, v.plan.length);   // < 0: a gap after the end
                            v.followAt = v.plan.length - v.followOverlap;
                        }
                    break;
                case Command::fadeTag:
                    for (auto& v : voices)
                        if (v.active && c.id != 0 && v.plan.tag == c.id)
                        {
                            v.hasFollow = false;
                            stopOver (v, c.overlap, FadeCurve::sCurve);
                        }
                    break;
                case Command::releaseLoop:
                    for (auto& v : voices)
                        if (v.active && c.id != 0 && v.plan.tag == c.id && v.plan.loop)
                        {
                            v.plan.loop = false;
                            v.skipFadeOut = v.pos >= v.plan.length - v.plan.fadeOutLength;
                        }
                    break;
                case Command::cancelFollow:
                    for (auto& v : voices)
                        if (v.active && c.id != 0 && v.plan.tag == c.id)
                            v.hasFollow = false;
                    break;
                case Command::seek:
                    if (c.id >= 0 && c.id < maxVoices)
                        if (auto& v = voices[(size_t) c.id]; v.active && ! v.stopping && v.plan.length > 0)
                        {
                            v.seekTo = std::min (c.overlap, v.plan.length - 1);
                            if (v.pos >= v.plan.length)
                                v.seekLeft = 0;   // waiting for the next song: silent already, jump now
                            else if (v.seekLeft <= 0)
                                v.seekLeft = std::max<juce::int64> (1, (juce::int64) (sampleRate.load() * 0.005));
                        }
                    break;
                case Command::setLoop:
                    if (c.id >= 0 && c.id < maxVoices)
                        if (auto& v = voices[(size_t) c.id]; v.active && v.plan.loop != c.fade)
                        {
                            v.plan.loop = c.fade;
                            v.skipFadeOut = ! c.fade && v.pos >= v.plan.length - v.plan.fadeOutLength;
                        }
                    break;
                case Command::swapCues:
                    for (auto& v : voices)
                        if (v.active)
                        {
                            if (v.plan.cueId == c.id)
                                v.plan.cueId = (int) c.overlap;
                            else if (v.plan.cueId == (int) c.overlap)
                                v.plan.cueId = c.id;
                        }
                    break;
                case Command::setFx:
                    for (auto& v : voices)
                        if (v.active)
                        {
                            if (v.plan.cueId == c.id)
                                v.plan.fx = c.plan.fx;
                            if (v.hasFollow && v.next.cueId == c.id)
                                v.next.fx = c.plan.fx;
                        }
                    break;
                case Command::live:
                    for (auto& v : voices)
                        if (v.active && v.plan.cueId == c.id)
                        {
                            v.gain.setTargetValue (c.gain);
                            v.pan.setTargetValue (c.pan);
                        }
                    break;
            }
        }
    };
    run (scope.startIndex1, scope.blockSize1);
    run (scope.startIndex2, scope.blockSize2);
}

PlaybackEngine::Voice* PlaybackEngine::startVoice (const PlayPlan& plan, int blockDelay)
{
    if (plan.audio == nullptr || plan.length <= 0)
        return nullptr;

    if (plan.fadeOthers)
        for (auto& v : voices)
            if (v.active)
                beginStop (v, true);

    if (plan.takeOver != PlaybackMode::overlap)
    {
        const auto takeOverFade = std::max<juce::int64> (1, (juce::int64) (sampleRate.load() * 0.05));
        const int first = plan.output.firstChannel, last = first + plan.output.numChannels();
        for (auto& v : voices)
        {
            if (! v.active || (plan.playlist != 0 && v.plan.playlist == plan.playlist))
                continue;
            const int vFirst = v.plan.output.firstChannel, vLast = vFirst + v.plan.output.numChannels();
            if (plan.takeOver == PlaybackMode::onePerOutput && (vLast <= first || vFirst >= last))
                continue;   // no channel in common
            v.hasFollow = false;
            stopOver (v, takeOverFade, FadeCurve::linear);
        }
    }

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active)
        {
            target = &v;
            break;
        }
    if (target == nullptr)   // all busy: take the oldest
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.order < target->order)
                target = &v;
    }

    auto& v = *target;
    v.plan = plan;
    v.active = true;
    v.pos = plan.startAt;
    v.segment = 0;
    while (v.segment < plan.numSegments - 1
           && v.pos >= plan.segments[(size_t) v.segment + 1].timelineStart)
        ++v.segment;
    v.firstPass = true;
    const auto declick = (juce::int64) (sampleRate.load() * 0.005);
    v.startRamp = plan.startAt > 0 && plan.startAt >= plan.fadeInLength ? 0 : declick;   // from the top: as recorded
    v.stopping = false;
    v.stopScale = 1.0f;
    v.gain.reset (sampleRate.load(), 0.02);
    v.gain.setCurrentAndTargetValue (plan.gain);
    v.pan.reset (sampleRate.load(), 0.02);
    v.pan.setCurrentAndTargetValue (plan.pan);
    v.order = nextOrder++;
    v.hasFollow = false;
    v.blockDelay = blockDelay;
    v.skipFadeOut = false;
    v.seekTo = -1;
    v.seekLeft = 0;
    return &v;
}

void PlaybackEngine::beginStop (Voice& v, bool fade)
{
    const auto hard = std::max<juce::int64> (1, (juce::int64) (sampleRate.load() * 0.01));
    auto length = fade ? v.plan.stopFadeLength : hard;
    if (length <= 0)
        length = hard;
    v.hasFollow = false;   // stopped by hand: the playlist doesn't carry on
    stopOver (v, length, fade ? v.plan.stopFadeCurve : FadeCurve::linear);
}

void PlaybackEngine::stopOver (Voice& v, juce::int64 length, FadeCurve curve)
{
    length = std::max<juce::int64> (1, length);
    if (v.stopping)
    {
        if (v.stopLeft <= length)
            return;   // already fading out sooner than this would
        v.stopScale *= fadeGain (v.stopCurve, (float) v.stopLeft / (float) v.stopLength);   // carry on from where it is
    }
    v.stopping = true;
    v.stopLength = length;
    v.stopLeft = length;
    v.stopCurve = curve;
}

void PlaybackEngine::render (Voice& v, float* const* outputs, int numOutputChannels, int numSamples)
{
    const auto& p = v.plan;
    const auto& audio = *p.audio;
    const float* src0 = audio.samples.getReadPointer (0);
    const float* src1 = audio.numChannels() > 1 ? audio.samples.getReadPointer (1) : src0;
    const bool monoSource = audio.numChannels() == 1;

    auto destination = [&] (int deviceChannel) -> float*
    {
        if (deviceChannel < 0 || deviceChannel >= maxOutputs)
            return nullptr;
        const int index = bufferIndex[(size_t) deviceChannel];
        return index >= 0 && index < numOutputChannels ? outputs[index] : nullptr;
    };
    float* out0 = nullptr;
    float* out1 = nullptr;
    if (p.fx != nullptr)
    {
        // through the button's plugins: into its stereo bus (routed to the output after the plugins)
        if (auto* bus = busFor (p.fx, p.output, numSamples))
        {
            out0 = bus->buffer.getWritePointer (0);
            out1 = bus->buffer.getWritePointer (1);
        }
    }
    else
    {
        out0 = destination (p.output.firstChannel);
        out1 = p.output.stereo ? destination (p.output.firstChannel + 1) : nullptr;
    }

    const bool busMono = p.fx != nullptr && ! p.output.stereo && out0 != nullptr && out1 != nullptr;

    // Pan for this block. Mono source on a pair: constant power. Stereo source: balance.
    const float panNow = v.pan.getNextValue();
    v.pan.skip (numSamples - 1);
    float left = 1.0f, right = 1.0f;
    if (monoSource)
    {
        const float angle = (panNow + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        left = std::cos (angle);
        right = std::sin (angle);
    }
    else
    {
        left = std::min (1.0f, 1.0f - panNow);
        right = std::min (1.0f, 1.0f + panNow);
    }

    const double sr = sampleRate.load();
    const auto joinRamp = std::max<juce::int64> (1, (juce::int64) (sr * 0.002));   // at cuts: no clicks
    const auto declick = std::max<juce::int64> (1, (juce::int64) (sr * 0.005));

    const int first = std::min (v.blockDelay, numSamples);
    v.blockDelay = 0;
    for (int i = first; i < numSamples; ++i)
    {
        if (v.hasFollow && ! v.stopping && ! p.loop && v.pos >= v.followAt)
        {
            // playlist: the next song starts on this sample; with an overlap, this one fades out across it
            v.hasFollow = false;
            if (numPendingFollows < maxVoices)
                pendingFollows[(size_t) numPendingFollows++] = { v.next, i, v.followOverlap };
            if (v.followOverlap > 0)
                stopOver (v, v.followOverlap, FadeCurve::sCurve);
        }
        if (v.seekTo >= 0 && v.seekLeft <= 0)
        {
            // a seek: the dip has reached silence, jump and come back in
            v.pos = v.seekTo;
            v.seekTo = -1;
            v.segment = 0;
            v.startRamp = 0;
        }
        if (v.pos >= p.length)
        {
            if (! p.loop && v.hasFollow && ! v.stopping)
            {
                // a playlist wait: silent until the next song starts at followAt
                const auto n = std::min<juce::int64> (numSamples - i, std::max<juce::int64> (1, v.followAt - v.pos));
                v.pos += n;
                i += (int) n - 1;
                continue;
            }
            if (! p.loop)
            {
                v.active = false;
                return;
            }
            v.pos = 0;
            v.segment = 0;
            v.firstPass = false;
        }
        while (v.segment < p.numSegments - 1 && v.pos >= p.segments[(size_t) v.segment + 1].timelineStart)
            ++v.segment;

        const auto& seg = p.segments[(size_t) v.segment];
        const auto offset = v.pos - seg.timelineStart;
        const auto segLength = seg.end - seg.start;
        const auto index = (int) (seg.start + offset);

        float env = v.gain.getNextValue();
        if (v.firstPass && v.pos < p.fadeInLength)
            env *= fadeGain (p.fadeInCurve, (float) v.pos / (float) p.fadeInLength);
        if (! p.loop && ! v.skipFadeOut && p.fadeOutLength > 0 && v.pos >= p.length - p.fadeOutLength)
            env *= fadeGain (p.fadeOutCurve, (float) (p.length - v.pos) / (float) p.fadeOutLength);
        if ((v.segment > 0 || ! v.firstPass) && offset < joinRamp)
            env *= (float) offset / (float) joinRamp;
        if ((v.segment < p.numSegments - 1 || p.loop) && segLength - offset < joinRamp)
            env *= (float) (segLength - offset) / (float) joinRamp;
        if (v.firstPass && p.entryFade > 0 && v.pos - p.startAt < p.entryFade)
            env *= fadeGain (FadeCurve::sCurve, (float) (v.pos - p.startAt) / (float) p.entryFade);
        if (v.startRamp < declick)
            env *= (float) v.startRamp++ / (float) declick;
        if (v.seekTo >= 0)
            env *= (float) v.seekLeft-- / (float) declick;
        if (v.stopping)
        {
            env *= v.stopScale * fadeGain (v.stopCurve, (float) v.stopLeft / (float) v.stopLength);
            if (--v.stopLeft <= 0)
            {
                v.active = false;
                return;
            }
        }

        const float l = src0[index], r = src1[index];
        if (busMono)
        {
            // a mono output through plugins: the same signal on both sides of the bus, no pan law
            const float m = (monoSource ? l : 0.5f * (l + r)) * env;
            out0[i] += m;
            out1[i] += m;
        }
        else if (out1 != nullptr)
        {
            if (out0 != nullptr)
                out0[i] += l * left * env;
            out1[i] += (monoSource ? l : r) * right * env;
        }
        else if (out0 != nullptr)
        {
            out0[i] += (monoSource ? l : 0.5f * (l + r)) * env;
        }
        ++v.pos;
    }
}

void PlaybackEngine::publishStatus()
{
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        auto& s = status[(size_t) i];
        s.position.store (v.pos);
        s.length.store (v.plan.length);
        s.stopping.store (v.stopping);
        s.order.store (v.order);
        s.tag.store (v.plan.tag);
        s.preview.store (v.plan.preview);
        s.hasFollow.store (v.active && v.hasFollow);
        s.looping.store (v.active && v.plan.loop);
        s.followAt.store (v.followAt);
        s.audio.store (v.active ? v.plan.audio : nullptr);
        s.fx.store (v.active ? v.plan.fx : nullptr);
        s.cueId.store (v.active ? v.plan.cueId : -1);
    }
}

// ---------------------------------------------------------------------------------------------
// Plugin buses

void PlaybackEngine::allocateBuses (int size)
{
    for (auto& b : buses)
    {
        b.buffer.setSize (2, std::max (size, 4096));
        b.buffer.clear();
        b.active = false;
        b.fx = nullptr;
    }
}

PlaybackEngine::FxBus* PlaybackEngine::busFor (CueFx* fx, const OutputRoute& route, int numSamples)
{
    if (numSamples > buses[0].buffer.getNumSamples())
        return nullptr;
    FxBus* free = nullptr;
    for (auto& b : buses)
    {
        if (b.active && b.fx == fx)
        {
            b.route = route;
            b.used = true;
            b.muted = false;
            return &b;
        }
        if (! b.active && free == nullptr)
            free = &b;
    }
    if (free == nullptr)
        return nullptr;
    free->fx = fx;
    free->route = route;
    free->active = true;
    free->used = true;
    free->muted = false;
    free->quiet = free->tail = 0;
    free->muteLeft = free->muteLength = 0;
    free->buffer.clear (0, numSamples);
    return free;
}

void PlaybackEngine::runBuses (float* const* outputs, int numOutputChannels, int numSamples)
{
    const double sr = sampleRate.load();
    auto destination = [&] (int deviceChannel) -> float*
    {
        if (deviceChannel < 0 || deviceChannel >= maxOutputs)
            return nullptr;
        const int index = bufferIndex[(size_t) deviceChannel];
        return index >= 0 && index < numOutputChannels ? outputs[index] : nullptr;
    };

    for (auto& b : buses)
    {
        if (! b.active)
            continue;
        b.fx->process (b.buffer, numSamples);

        const auto range0 = juce::FloatVectorOperations::findMinAndMax (b.buffer.getReadPointer (0), numSamples);
        const auto range1 = juce::FloatVectorOperations::findMinAndMax (b.buffer.getReadPointer (1), numSamples);
        const float peak = std::max ({ -range0.getStart(), range0.getEnd(), -range1.getStart(), range1.getEnd() });

        bool audible = ! b.muted;
        if (b.muted && b.muteLeft > 0)
        {
            const float g0 = (float) b.muteLeft / (float) b.muteLength;
            b.muteLeft = std::max (0, b.muteLeft - numSamples);
            const float g1 = (float) b.muteLeft / (float) b.muteLength;
            for (int ch = 0; ch < 2; ++ch)
                b.buffer.applyGainRamp (ch, 0, numSamples, g0, g1);
            audible = true;   // still fading out
        }
        if (audible)
        {
            if (b.route.stereo)
            {
                if (auto* o = destination (b.route.firstChannel))
                    juce::FloatVectorOperations::add (o, b.buffer.getReadPointer (0), numSamples);
                if (auto* o = destination (b.route.firstChannel + 1))
                    juce::FloatVectorOperations::add (o, b.buffer.getReadPointer (1), numSamples);
            }
            else if (auto* o = destination (b.route.firstChannel))
            {
                juce::FloatVectorOperations::addWithMultiply (o, b.buffer.getReadPointer (0), 0.5f, numSamples);
                juce::FloatVectorOperations::addWithMultiply (o, b.buffer.getReadPointer (1), 0.5f, numSamples);
            }
        }

        // after the last voice: keep going while the tail rings, stop once it's quiet (or after 30 s)
        if (b.used)
            b.quiet = b.tail = 0;
        else
        {
            b.tail += numSamples;
            b.quiet = peak < 1.0e-4f ? b.quiet + numSamples : 0;
            const bool pastTail = b.tail > (int) (sr * b.fx->tailSeconds());
            if ((pastTail && b.quiet > (int) (sr * 0.25)) || b.tail > (int) (sr * 30.0))
            {
                b.active = false;
                b.fx = nullptr;
            }
        }
        b.used = false;
    }
}

bool PlaybackEngine::isUsingFx (const CueFx* fx) const
{
    if (fifo.getNumReady() > 0)
        return true;
    for (auto& s : busStatus)
        if (s.load() == fx)
            return true;
    for (auto& s : status)
        if (s.fx.load() == fx)
            return true;
    return false;
}

void PlaybackEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputChannels, float* const* outputs,
                                                       int numOutputChannels, int numSamples,
                                                       const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    handleInputs (inputs, numInputChannels, numSamples);
    handleCommands();

    for (auto& b : buses)
        if (b.active)
            b.buffer.clear (0, std::min (numSamples, b.buffer.getNumSamples()));

    numPendingFollows = 0;
    for (auto& v : voices)
        if (v.active)
            render (v, outputs, numOutputChannels, numSamples);

    // playlist follows that started part way into this block (a very short song can start another)
    for (int round = 0; round < 4 && numPendingFollows > 0; ++round)
    {
        const int count = numPendingFollows;
        auto& starting = followsStarting;
        std::copy_n (pendingFollows.begin(), count, starting.begin());
        numPendingFollows = 0;
        for (int f = 0; f < count; ++f)
        {
            auto plan = starting[(size_t) f].plan;
            plan.entryFade = std::max<juce::int64> (0, starting[(size_t) f].overlap);
            if (auto* v = startVoice (plan, starting[(size_t) f].offset))
                render (*v, outputs, numOutputChannels, numSamples);
        }
    }

    runBuses (outputs, numOutputChannels, numSamples);
    publishStatus();
    for (int i = 0; i < maxVoices; ++i)
        busStatus[(size_t) i].store (buses[(size_t) i].active ? buses[(size_t) i].fx : nullptr);

    for (int ch = 0; ch < maxOutputs; ++ch)
    {
        const int index = bufferIndex[(size_t) ch];
        if (index < 0 || index >= numOutputChannels || outputs[index] == nullptr)
            continue;
        const auto range = juce::FloatVectorOperations::findMinAndMax (outputs[index], numSamples);
        const float peak = std::max (-range.getStart(), range.getEnd());
        auto& p = peaks[(size_t) ch];
        if (peak > p.load())
            p.store (peak);
    }
}
