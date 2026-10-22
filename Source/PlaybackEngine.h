#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "Cue.h"
#include "CueAudio.h"
#include "CueFx.h"
#include <array>
#include <atomic>
#include <map>
#include <optional>

/** The show's playback mode: what a newly started button does to what's already playing
    (it stops with a 50 ms fade). */
enum class PlaybackMode { oneAtATime = 0, onePerOutput, overlap };

/** Everything the audio thread needs to play a cue, worked out on the message thread.
    Positions are samples of the cue's audio; the "timeline" is what plays (segments end to end). */
struct PlayPlan
{
    static constexpr int maxSegments = 64;
    struct Span { juce::int64 start = 0, end = 0, timelineStart = 0; };

    const CueAudio* audio = nullptr;
    int cueId = -1;
    std::array<Span, maxSegments> segments {};
    int numSegments = 0;
    juce::int64 length = 0;         // timeline length
    juce::int64 startAt = 0;        // timeline position it starts from (0; Preview starts at its cursor)
    juce::int64 fadeInLength = 0, fadeOutLength = 0, stopFadeLength = 0;
    FadeCurve fadeInCurve = FadeCurve::sCurve, fadeOutCurve = FadeCurve::sCurve, stopFadeCurve = FadeCurve::sCurve;
    float gain = 1.0f;              // linear, from the level
    float pan = 0.0f;
    OutputRoute output;
    bool loop = false;
    bool fadeOthers = false;
    PlaybackMode takeOver = PlaybackMode::overlap;   // one at a time / per output: stops the others (overlap = none)
    int tag = 0;                   // playlist entry this was started for (0 = none)
    int playlist = 0;               // playlist (uid) it was started for, 0 = a button. Taking over leaves the
                                    // songs of its own playlist alone (GO fades those itself)
    bool preview = false;           // the waveform editor's transport (not the button being played)
    CueFx* fx = nullptr;           // the button's plugins (null = none): played through its own bus
    juce::int64 entryFade = 0;      // extra fade in from where it starts (the incoming side of a crossfade)

    /** Builds a plan from a cue and its loaded audio. Returns false if nothing would play. */
    static bool make (const Cue& cue, const CueAudio& audio, int cueId, PlayPlan& plan);
};

/** Plays cues to the audio device. Every cue becomes a voice that writes to its own output
    (one channel or a pair). Control calls come from the message thread through a lock-free queue. */
class PlaybackEngine : public juce::AudioIODeviceCallback,
                       private juce::ChangeListener
{
public:
    static constexpr int maxVoices = 32;
    static constexpr int maxOutputs = 64;
    static constexpr int maxInputs = 64;

    PlaybackEngine();
    ~PlaybackEngine() override;

    /** Opens the audio device (saved state from the settings file, or the default output). */
    void initialise (const juce::XmlElement* savedState);
    /** The device manager's settings plus the input device to open when recording is armed. */
    std::unique_ptr<juce::XmlElement> createStateXml() const;
    /** Stops the audio device (before the plugins and the show go away). */
    void shutdownAudio();
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    int getBlockSize() const { return blockSize.load(); }

    /** For tests: run without a device at this rate and channel count. */
    void prepareForTest (double sampleRate, int numOutputs, int numInputs = 0);

    double getSampleRate() const { return sampleRate.load(); }
    int getNumOutputs() const { return numOutputs.load(); }
    juce::StringArray getOutputNames() const;
    int getNumInputs() const { return numInputs.load(); }
    juce::StringArray getInputNames() const;

    // ---- recording (message thread) ----
    /** Records the input (one channel or a pair) into a 24-bit WAV at the device rate, written on a background thread. */
    bool startRecording (const juce::File& file, OutputRoute input, juce::String& error);
    /** Stops and closes the file; returns it (or no file if nothing was recording). */
    juce::File stopRecording();
    bool isRecording() const { return recording.load(); }
    double getRecordedSeconds() const { return recordedSamples.load() / std::max (1.0, sampleRate.load()); }
    /** Peak level on a device input since the last call. */
    float takeInputPeak (int channel);
    /** Recording armed: the device opens only this input (one channel or a pair); nullopt = no inputs open.
        Changing it reopens the device, so the outputs drop out for a moment. */
    void setArmedInput (std::optional<OutputRoute> input);
    std::optional<OutputRoute> getArmedInput() const { return armedInput; }

    // ---- message thread ----
    void play (const PlayPlan& plan);
    void stopCue (int cueId, bool fade);
    void stopVoice (int voice, bool fade);
    void stopAll (bool fade);
    /** Level / pan changes while a cue is playing (gain is linear). */
    void setLive (int cueId, float gain, float pan);
    /** Playlist chaining: when the voice tagged afterTag gets to overlap samples before its end, 'next' starts
        on the exact sample, fading in over the overlap while the first fades out (overlap 0 = back to back,
        negative = that many samples of silence after the end). Stopping or fading the first voice cancels it. */
    void follow (int afterTag, const PlayPlan& next, juce::int64 overlap);
    void cancelFollow (int afterTag);
    /** Fades out the voices started with this tag over length samples (S-curve). */
    void fadeTag (int tag, juce::int64 length);
    /** Ends the looping of the voices with this tag: they play to the end of the current pass (then their follow, if any). */
    void releaseLoop (int tag);
    /** Jumps a playing voice to a timeline position (samples), dipping for a few ms so it doesn't click. */
    void seekVoice (int voice, juce::int64 position);
    /** Loop on / off for one playing voice. Off: it finishes the pass it's in. */
    void setVoiceLoop (int voice, bool loop);
    /** Two buttons swapped places: voices playing one carry on as the other. */
    void swapCues (int a, int b);
    /** A button's plugins changed while it plays: its voices (and songs queued to follow as it) go
        through these from now on (null = straight to the output). */
    void setCueFx (int cueId, CueFx* fx);

    struct VoiceInfo
    {
        int voice = -1, cueId = -1, tag = 0;
        double position = 0.0, length = 0.0;   // seconds of the timeline
        bool stopping = false;
        bool looping = false;
        bool hasFollow = false;
        bool waiting = false;                  // the song is over: silent until the next one (a playlist wait)
        bool preview = false;                  // the waveform editor's transport
        double waitLeft = 0.0;                 // seconds until the next song starts, while waiting
        juce::uint32 order = 0;                // higher = started later
    };
    /** Playing voices, newest first. */
    std::vector<VoiceInfo> getVoices() const;
    bool isCuePlaying (int cueId) const;
    bool isUsing (const CueAudio* audio) const;
    /** True while the engine may still run these plugins (a sting playing through them or its tail). */
    bool isUsingFx (const CueFx* fx) const;
    bool hasPendingCommands() const { return fifo.getNumReady() > 0; }
    /** Peak level on a device output since the last call (0..). */
    float takeOutputPeak (int channel);

    // ---- AudioIODeviceCallback ----
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs,
                                           int numOutputChannels, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    struct Command
    {
        enum Type { play, stopCue, stopVoice, stopAll, live, follow, cancelFollow, fadeTag, releaseLoop,
                    seek, setLoop, swapCues, setFx } type = play;
        bool fade = false;
        int id = -1;
        float gain = 1.0f, pan = 0.0f;
        PlayPlan plan;
        juce::int64 overlap = 0;
    };

    struct Voice
    {
        bool active = false;
        PlayPlan plan;
        juce::int64 pos = 0;            // timeline position
        int segment = 0;
        bool firstPass = true;
        juce::int64 startRamp = 0;      // declick when starting part way in
        bool stopping = false;
        juce::int64 stopLength = 0, stopLeft = 0;
        float stopScale = 1.0f;         // gain already lost when a stop is replaced by a shorter one
        FadeCurve stopCurve = FadeCurve::linear;
        juce::SmoothedValue<float> gain, pan;
        juce::uint32 order = 0;
        bool hasFollow = false;
        PlayPlan next;
        juce::int64 followAt = 0, followOverlap = 0;
        int blockDelay = 0;             // started part way into this block (a follow)
        bool skipFadeOut = false;       // loop released inside the fade-out: no jump in level
        juce::int64 seekTo = -1;        // where a seek jumps once the dip reaches silence (-1 = none)
        juce::int64 seekLeft = 0;       // samples left of that dip
    };

    struct VoiceStatus
    {
        std::atomic<int> cueId { -1 };
        std::atomic<juce::int64> position { 0 }, length { 0 };
        std::atomic<bool> stopping { false };
        std::atomic<const CueAudio*> audio { nullptr };
        std::atomic<const CueFx*> fx { nullptr };
        std::atomic<juce::uint32> order { 0 };
        std::atomic<int> tag { 0 };
        std::atomic<bool> preview { false };
        std::atomic<bool> hasFollow { false };
        std::atomic<bool> looping { false };
        std::atomic<juce::int64> followAt { 0 };
    };

    void push (const Command& c);
    void handleCommands();
    Voice* startVoice (const PlayPlan& plan, int blockDelay = 0);
    void beginStop (Voice& v, bool fade);
    void stopOver (Voice& v, juce::int64 length, FadeCurve curve);
    void render (Voice& v, float* const* outputs, int numOutputChannels, int numSamples);
    void publishStatus();
    void enableAllChannels();
    void handleInputs (const float* const* inputs, int numInputChannels, int numSamples);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::AudioDeviceManager deviceManager;
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> numOutputs { 0 };
    std::atomic<int> blockSize { 512 };
    std::array<int, maxOutputs> bufferIndex {};   // device output -> index in the callback's buffers (-1 = off)
    std::array<int, maxInputs> inputIndex {};     // device input -> index in the callback's input buffers
    std::atomic<int> numInputs { 0 };
    std::array<std::atomic<float>, maxInputs> inputPeaks {};

    juce::TimeSliceThread writerThread { "Recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::SpinLock writerLock;
    OutputRoute recordInput;
    std::optional<OutputRoute> armedInput;   // message thread
    juce::String inputChoice;                // input device to open when armed (Windows Audio / Mac); "" = default
    mutable std::map<juce::String, juce::StringArray> inputNamesCache;   // "type|device" -> its input names
    juce::String inputDeviceFor (juce::AudioIODeviceType& type, const juce::String& outputDeviceName) const;
    std::atomic<bool> recording { false };
    std::atomic<juce::int64> recordedSamples { 0 };
    juce::File recordingFile;
    juce::AudioBuffer<float> silence { 1, 8192 };

    juce::AbstractFifo fifo { 256 };
    std::array<Command, 256> commands;
    juce::SpinLock pushLock;                      // only between message-thread pushers

    std::array<Voice, maxVoices> voices;
    std::array<VoiceStatus, maxVoices> status;
    std::array<std::atomic<float>, maxOutputs> peaks {};
    juce::uint32 nextOrder = 1;

    /** A button with plugins plays through a stereo bus: its voices mix in, the plugins run, the result goes
        to the button's output. The bus keeps running after the voices end until the tail is quiet. */
    struct FxBus
    {
        CueFx* fx = nullptr;
        OutputRoute route;
        juce::AudioBuffer<float> buffer;
        bool active = false, used = false, muted = false;
        int quiet = 0, tail = 0;
        int muteLeft = 0, muteLength = 0;   // the fade when Stop all mutes a tail
    };
    FxBus* busFor (CueFx* fx, const OutputRoute& route, int numSamples);
    void runBuses (float* const* outputs, int numOutputChannels, int numSamples);
    void allocateBuses (int size);
    std::array<FxBus, maxVoices> buses;
    std::array<std::atomic<const void*>, maxVoices> busStatus {};

    struct PendingFollow { PlayPlan plan; int offset = 0; juce::int64 overlap = 0; };
    std::array<PendingFollow, maxVoices> pendingFollows;
    std::array<PendingFollow, maxVoices> followsStarting;   // (not on the audio thread's stack: plans are big)
    int numPendingFollows = 0;
};
