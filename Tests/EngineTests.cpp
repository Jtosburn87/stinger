// Engine tests: synthetic audio through the playback engine, no audio device needed.

#include <juce_audio_formats/juce_audio_formats.h>
#include "PlaybackEngine.h"
#include "Show.h"
#include "StreamDeckServer.h"
#include "Downloader.h"
#include "Autosave.h"
#include "DeviceWatch.h"
#include "Loudness.h"
#include "OscBridge.h"
#include "BackupLink.h"
#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what);
        if (! ok)
            ++failures;
    }

    bool near (float a, float b, float tol = 1.0e-3f) { return std::abs (a - b) <= tol; }

    constexpr double rate = 48000.0;
    constexpr int block = 480;   // 10 ms

    /** Output buffers for the engine callback. */
    struct Outputs
    {
        explicit Outputs (int channels) : data ((size_t) channels, std::vector<float> (block, 1.0f))
        {
            for (auto& v : data)
                ptrs.push_back (v.data());
        }
        void run (PlaybackEngine& e, int blocks = 1)
        {
            for (int b = 0; b < blocks; ++b)
                e.audioDeviceIOCallbackWithContext (nullptr, 0, ptrs.data(), (int) ptrs.size(), block, {});
        }
        float at (int ch, int i = block - 1) const { return data[(size_t) ch][(size_t) i]; }
        std::vector<std::vector<float>> data;
        std::vector<float*> ptrs;
    };

    /** seconds of audio at 48 kHz; value(i) per sample, same on every channel. */
    template <typename F>
    std::shared_ptr<CueAudio> makeAudio (int channels, double seconds, F value)
    {
        auto a = std::make_shared<CueAudio>();
        a->sampleRate = a->fileSampleRate = rate;
        a->fileChannels = channels;
        a->samples.setSize (channels, (int) (seconds * rate));
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < a->samples.getNumSamples(); ++i)
                a->samples.setSample (ch, i, value (ch, i));
        return a;
    }

    std::shared_ptr<CueAudio> constant (int channels, double seconds, float v)
    {
        return makeAudio (channels, seconds, [v] (int, int) { return v; });
    }

    void play (PlaybackEngine& e, const Cue& cue, const CueAudio& audio, int id = 0)
    {
        PlayPlan plan;
        PlayPlan::make (cue, audio, id, plan);
        e.play (plan);
    }

    Cue monoOut (int channel)
    {
        Cue c;
        c.file = juce::File::getCurrentWorkingDirectory().getChildFile ("test.wav");
        c.output = { channel, false };
        return c;
    }
}

static void testFadeCurves()
{
    std::puts ("Fade curves");
    bool ends = true;
    for (int c = 0; c < 4; ++c)
        ends = ends && near (fadeGain ((FadeCurve) c, 0.0f), 0.0f) && near (fadeGain ((FadeCurve) c, 1.0f), 1.0f);
    check (ends, "every curve runs 0 -> 1");
    check (near (fadeGain (FadeCurve::linear, 0.25f), 0.25f), "linear is linear");
    check (near (fadeGain (FadeCurve::sCurve, 0.5f), 0.5f), "S-curve is half way at the middle");
    check (fadeGain (FadeCurve::exponential, 0.5f) < 0.2f && fadeGain (FadeCurve::logarithmic, 0.5f) > 0.8f,
           "exponential starts slow, logarithmic starts fast");
}

static void testSegments()
{
    std::puts ("Trim, cuts and start point");
    Cue c;
    c.trimIn = 1.0;
    c.trimOut = 9.0;
    c.cuts = { { 6.0, 7.0 }, { 3.0, 4.0 } };
    auto s = c.segments (10.0);
    check (s.size() == 3 && near ((float) s[0].start, 1.0f) && near ((float) s[0].end, 3.0f)
           && near ((float) s[1].start, 4.0f) && near ((float) s[2].end, 9.0f), "trim 1..9 minus two cuts = 3 segments");
    check (near ((float) c.playLength (10.0), 6.0f), "plays 6 s");
    check (near ((float) c.timelinePosition (10.0, 5.0), 3.0f), "5 s in the file = 3 s into what plays");
    check (near ((float) c.timelinePosition (10.0, 3.5), 2.0f), "a time inside a cut = where the cut ends");

    // pastes: 1..2 copied, pasted at 5 (inside 4..6) and at 6.5 (inside the cut: plays where the cut ends)
    c.pastes = { { 6.5, { { 1.0, 2.0, true } } }, { 5.0, { { 1.0, 2.0, true } } } };
    s = c.segments (10.0);
    check (s.size() == 6 && near ((float) s[1].end, 5.0f) && s[2].pasted && near ((float) s[2].start, 1.0f)
           && near ((float) s[3].start, 5.0f) && s[4].pasted && near ((float) s[5].start, 7.0f),
           "a paste splits the section it lands in; one in a cut plays where the cut ends");
    check (near ((float) c.playLength (10.0), 8.0f), "each paste adds its length");
    check (near ((float) c.timelinePosition (10.0, 8.0), 7.0f), "file times after a paste move on by its length");
    juce::var saved = c.toVar();
    const auto back = Cue::fromVar (juce::JSON::parse (juce::JSON::toString (saved)));
    check (back.pastes.size() == 2 && back.pastes[0].pieces == c.pastes[0].pieces, "pastes are saved in the show");
    c.pastes.clear();
    c.trimOut = -1.0;
    c.cuts.clear();
    check (near ((float) c.playLength (10.0), 9.0f), "trim out < 0 = end of file");
}

static void testRouting()
{
    std::puts ("Outputs, level and pan");
    PlaybackEngine e;
    e.prepareForTest (rate, 8);
    Outputs out (8);
    auto mono = constant (1, 1.0, 0.5f);
    auto stereo = makeAudio (2, 1.0, [] (int ch, int) { return ch == 0 ? 0.2f : 0.6f; });

    play (e, monoOut (2), *mono);
    out.run (e);
    check (near (out.at (2), 0.5f), "mono file on mono output 3 at 0 dB");
    check (near (out.at (0), 0.0f) && near (out.at (3), 0.0f), "other outputs silent (and cleared)");
    e.stopAll (false);
    out.run (e, 2);

    auto c = monoOut (2);
    c.levelDb = 6.0f;
    play (e, c, *mono);
    out.run (e);
    check (near (out.at (2), 0.5f * juce::Decibels::decibelsToGain (6.0f)), "+6 dB level");
    e.setLive (0, juce::Decibels::decibelsToGain (-20.0f), 0.0f);
    out.run (e, 5);
    check (near (out.at (2), 0.05f), "live level change to -20 dB while playing");
    e.stopAll (false);
    out.run (e, 2);

    Cue pair;
    pair.output = { 4, true };
    play (e, pair, *mono);
    out.run (e);
    check (near (out.at (4), 0.5f * 0.7071f) && near (out.at (5), 0.5f * 0.7071f), "mono file on a stereo pair: centred, -3 dB each side");
    e.stopAll (false);
    out.run (e, 2);

    pair.pan = -1.0f;
    play (e, pair, *mono);
    out.run (e);
    check (near (out.at (4), 0.5f) && near (out.at (5), 0.0f), "pan hard left");
    e.stopAll (false);
    out.run (e, 2);

    pair.pan = 0.0f;
    play (e, pair, *stereo);
    out.run (e);
    check (near (out.at (4), 0.2f) && near (out.at (5), 0.6f), "stereo file on a pair keeps left and right");
    e.stopAll (false);
    out.run (e, 2);

    play (e, monoOut (7), *stereo);
    out.run (e);
    check (near (out.at (7), 0.4f), "stereo file on a mono output = average of both sides");
    e.stopAll (false);
    out.run (e, 2);

    play (e, monoOut (6), *mono, 1);
    play (e, monoOut (6), *mono, 2);
    out.run (e);
    check (near (out.at (6), 1.0f), "two cues on the same output add up");
    check (e.getVoices().size() == 2 && e.isCuePlaying (1) && e.isCuePlaying (2), "both show as playing");
    check (e.takeOutputPeak (6) > 0.99f && e.takeOutputPeak (6) == 0.0f, "output peak meter reads and resets");
}

static void testEdits()
{
    std::puts ("Cuts, start point and fades in playback");
    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    Outputs out (2);
    auto ramp = makeAudio (1, 2.0, [] (int, int i) { return (float) (i / rate); });   // value = file time

    Cue c = monoOut (0);
    c.cuts = { { 0.2, 0.5 } };
    c.trimIn = 0.1;
    play (e, c, *ramp);
    out.run (e, 20);   // 0.2 s on: past the cut
    check (near (out.at (0), 0.6f - 1.0f / (float) rate, 2.0e-4f), "after the cut it plays from 0.5 s on");
    e.stopAll (false);
    out.run (e, 2);

    auto dc = constant (1, 1.0, 1.0f);
    Cue f = monoOut (0);
    f.fadeIn = { 0.2, FadeCurve::linear };
    f.fadeOut = { 0.2, FadeCurve::linear };
    play (e, f, *dc);
    out.run (e, 10);
    check (near (out.at (0), 0.5f, 0.01f), "linear fade in is half way after 0.1 s of 0.2");
    out.run (e, 80);   // 0.9 s
    check (near (out.at (0), 0.5f, 0.01f), "fade out is half way 0.1 s before the end");
    out.run (e, 11);
    check (! e.isCuePlaying (0) && near (out.at (0), 0.0f), "stops at the end");

    Cue l = monoOut (0);
    l.loop = true;
    auto shortDc = constant (1, 0.05, 1.0f);
    play (e, l, *shortDc);
    out.run (e, 20);
    check (e.isCuePlaying (0) && near (out.at (0, 200), 1.0f), "loop keeps playing past the end");
    e.stopAll (false);
    out.run (e, 2);
    check (! e.isCuePlaying (0), "stop all stops a loop");
}

static void testStopping()
{
    std::puts ("Stop, fade out, fade others");
    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    Outputs out (2);
    auto dc = constant (1, 5.0, 1.0f);

    Cue a = monoOut (0);
    a.stopFade = { 0.5, FadeCurve::linear };
    play (e, a, *dc, 1);
    out.run (e, 5);
    e.stopCue (1, true);
    out.run (e, 25);
    check (e.isCuePlaying (1) && near (out.at (0), 0.5f, 0.01f), "fade out is half way after 0.25 s of 0.5");
    check (e.getVoices().front().stopping, "shows as stopping");
    e.stopCue (1, false);
    out.run (e, 2);
    check (! e.isCuePlaying (1), "stop during a fade cuts it short");

    play (e, a, *dc, 1);
    out.run (e, 5);
    Cue b = monoOut (1);
    b.fadeOthers = true;
    play (e, b, *dc, 2);
    out.run (e, 60);
    check (! e.isCuePlaying (1) && e.isCuePlaying (2), "a cue with Fade others fades out what was playing");

    e.stopAll (true);
    out.run (e, 5);
    check (e.isCuePlaying (2), "fade all fades (doesn't cut)");
    out.run (e, 300);
    check (e.getVoices().empty(), "and then everything has stopped");
}

static void testPlaybackModes()
{
    std::puts ("Playback modes");
    PlaybackEngine e;
    e.prepareForTest (rate, 6);
    Outputs out (6);
    auto dc = constant (1, 5.0, 1.0f);
    auto start = [&] (Cue c, int id, PlaybackMode mode, int tag = 0, int playlist = 0)
    {
        PlayPlan plan;
        PlayPlan::make (c, *dc, id, plan);
        plan.takeOver = mode;
        plan.tag = tag;
        plan.playlist = playlist;
        e.play (plan);
    };
    Cue pair12, pair34, mono1;
    pair12.output = { 0, true };
    pair34.output = { 2, true };
    mono1.output = { 1, false };

    start (pair12, 1, PlaybackMode::oneAtATime);
    out.run (e, 2);
    start (pair34, 2, PlaybackMode::oneAtATime);
    out.run (e, 2);
    bool firstStopping = false;
    for (auto& v : e.getVoices())
        firstStopping = firstStopping || (v.cueId == 1 && v.stopping);
    check (firstStopping, "one at a time: the first starts stopping at once");
    out.run (e, 5);   // 50 ms
    check (! e.isCuePlaying (1) && e.isCuePlaying (2), "one at a time: it's gone after 50 ms, the new one plays");
    e.stopAll (false);
    out.run (e, 2);

    start (pair12, 1, PlaybackMode::onePerOutput);
    start (pair34, 2, PlaybackMode::onePerOutput);
    out.run (e, 10);
    check (e.isCuePlaying (1) && e.isCuePlaying (2), "one per output: different outputs both play");
    start (mono1, 3, PlaybackMode::onePerOutput);
    out.run (e, 10);
    check (! e.isCuePlaying (1) && e.isCuePlaying (2) && e.isCuePlaying (3), "one per output: a mono send on 2 takes over the 1-2 pair only");
    e.stopAll (false);
    out.run (e, 2);

    start (pair12, 1, PlaybackMode::overlap);
    start (pair12, 2, PlaybackMode::overlap);
    out.run (e, 10);
    check (e.isCuePlaying (1) && e.isCuePlaying (2), "overlap: both play");
    e.stopAll (false);
    out.run (e, 2);

    start (pair12, 1, PlaybackMode::oneAtATime, 7, 1);           // a playlist song
    start (pair34, 2, PlaybackMode::oneAtATime);                 // a button
    out.run (e, 10);
    check (! e.isCuePlaying (1) && e.isCuePlaying (2), "a button takes over from a playlist song");
    start (pair12, 3, PlaybackMode::oneAtATime, 8, 1);           // GO: leaves its own playlist's songs to their own fade
    start (pair12, 4, PlaybackMode::overlap, 9, 1);              // a playlist follow
    out.run (e, 10);
    check (! e.isCuePlaying (2) && e.isCuePlaying (3) && e.isCuePlaying (4), "GO takes over from buttons, playlist transitions overlap");
    start (pair12, 5, PlaybackMode::oneAtATime, 10, 1);          // GO again in the same playlist
    out.run (e, 10);
    check (e.isCuePlaying (3) && e.isCuePlaying (4) && e.isCuePlaying (5), "GO leaves its own playlist's songs alone");
    start (pair34, 6, PlaybackMode::oneAtATime, 11, 2);          // GO in another playlist
    out.run (e, 10);
    check (! e.isCuePlaying (3) && ! e.isCuePlaying (5) && e.isCuePlaying (6), "one at a time: another playlist's GO takes over");
    e.stopAll (false);
    out.run (e, 2);
    start (pair12, 7, PlaybackMode::onePerOutput, 12, 1);
    start (pair34, 8, PlaybackMode::onePerOutput, 13, 2);
    out.run (e, 10);
    check (e.isCuePlaying (7) && e.isCuePlaying (8), "one per output: two playlists on different outputs both play");
    e.stopAll (false);
    out.run (e, 2);

    Show s;
    check (s.playbackMode == PlaybackMode::oneAtATime, "new shows play one at a time");
    s.playbackMode = PlaybackMode::onePerOutput;
    Show back;
    back.fromVar (juce::JSON::parse (juce::JSON::toString (s.toVar())));
    check (back.playbackMode == PlaybackMode::onePerOutput, "the mode is saved in the show");
}

static void testFollow()
{
    std::puts ("Playlist links in the engine");
    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    Outputs out (2);
    auto quarter = constant (1, 0.1, 0.25f);   // 4800 samples = 10 blocks
    auto half = constant (1, 1.0, 0.5f);

    // back to back, same output: the next song starts on the very next sample
    PlayPlan a, b;
    PlayPlan::make (monoOut (0), *quarter, 1, a);
    PlayPlan::make (monoOut (0), *half, 2, b);
    a.tag = 10;
    b.tag = 11;
    e.play (a);
    e.follow (10, b, 0);
    out.run (e, 9);
    check (near (out.at (0), 0.25f), "first song playing");
    check (e.getVoices().front().hasFollow, "the link shows on the voice");
    out.run (e);
    check (near (out.at (0, block - 1), 0.25f), "first song to its last sample");
    out.run (e);
    check (near (out.at (0, 0), 0.5f) && e.isCuePlaying (2) && ! e.isCuePlaying (1), "next song from the very next sample, no gap");
    check (e.getVoices().front().tag == 11, "it carries its own tag");
    e.stopAll (false);
    out.run (e, 2);

    // part way into a block
    auto odd = constant (1, 1000.0 / rate, 0.25f);   // 1000 samples: ends 40 samples into block 3
    PlayPlan c;
    PlayPlan::make (monoOut (0), *odd, 3, c);
    c.tag = 20;
    e.play (c);
    e.follow (20, b, 0);
    out.run (e, 3);
    check (near (out.at (0, 39), 0.25f) && near (out.at (0, 40), 0.5f), "switches on the exact sample inside a block");
    e.stopAll (false);
    out.run (e, 2);

    // crossfade: first on output 1, next on output 2, so each side can be read
    auto one = constant (1, 1.0, 1.0f);
    PlayPlan x, y;
    PlayPlan::make (monoOut (0), *one, 4, x);
    PlayPlan::make (monoOut (1), *one, 5, y);
    x.tag = 30;
    y.tag = 31;
    e.play (x);
    e.follow (30, y, (juce::int64) (0.2 * rate));
    out.run (e, 80);   // 0.8 s: the crossfade starts on the last sample of this run
    check (near (out.at (1), 0.0f), "next song not started before the crossfade");
    out.run (e, 10);   // half way through the 0.2 s crossfade
    check (near (out.at (0), 0.5f, 0.02f) && near (out.at (1), 0.5f, 0.02f), "half way: one fading out, one fading in");
    check (near (out.at (0) + out.at (1), 1.0f, 0.01f), "S-curves add up to full level");
    out.run (e, 10);
    check (! e.isCuePlaying (4) && e.isCuePlaying (5) && near (out.at (1), 1.0f, 0.01f), "first song gone at its end, next at full level");
    e.stopAll (false);
    out.run (e, 2);

    // stopping by hand cancels the link
    e.play (a);
    e.follow (10, b, 0);
    out.run (e, 2);
    e.stopCue (1, false);
    out.run (e, 15);
    check (e.getVoices().empty(), "stop cancels the next song");

    e.play (a);
    e.follow (10, b, 0);
    e.cancelFollow (10);
    out.run (e, 15);
    check (e.getVoices().empty(), "a cancelled link doesn't play");

    // Wait: 0.1 s of silence (10 blocks) between the songs
    e.play (a);
    e.follow (10, b, -(juce::int64) (0.1 * rate));
    out.run (e, 10);
    check (near (out.at (0, block - 1), 0.25f), "first song to its end");
    out.run (e, 3);
    auto waiting = e.getVoices();
    check (near (out.at (0), 0.0f) && waiting.size() == 1 && waiting.front().waiting, "then silence, the voice shows it's waiting");
    check (! waiting.empty() && std::abs (waiting.front().waitLeft - 0.07) < 0.011, "with the time left before the next song");
    out.run (e, 7);
    check (near (out.at (0), 0.0f) && ! e.isCuePlaying (2), "still silent up to the last sample of the wait");
    out.run (e);
    check (near (out.at (0, 0), 0.5f) && e.isCuePlaying (2) && ! e.isCuePlaying (1), "next song starts after exactly the wait");
    e.stopAll (false);
    out.run (e, 2);

    e.play (a);
    e.follow (10, b, -(juce::int64) (0.1 * rate));
    out.run (e, 12);
    e.stopCue (1, false);
    out.run (e, 20);
    check (e.getVoices().empty(), "stopping during the wait cancels the next song");
}

static void testSeekAndLoop()
{
    std::puts ("Seek, loop, swap while playing");
    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    Outputs out (2);
    // the sample value is its own position in seconds, so the output tells where playback is
    auto ramp = makeAudio (1, 1.0, [] (int, int i) { return (float) i / (float) rate; });
    PlayPlan p;
    PlayPlan::make (monoOut (0), *ramp, 7, p);
    e.play (p);
    out.run (e, 10);   // 0.1 s
    e.seekVoice (0, (juce::int64) (0.6 * rate));
    out.run (e);
    check (out.at (0, 0) < 0.11f && out.at (0, 0) > 0.09f, "a seek dips the old place first (no click)");
    out.run (e);
    check (near (out.at (0), 0.6f + (2.0f * block - 241.0f) / (float) rate, 0.002f), "then plays from the new place");
    const auto v = e.getVoices();
    check (! v.empty() && v.front().position > 0.6 && v.front().position < 0.63, "position follows the seek");

    e.setVoiceLoop (0, true);
    out.run (e, 45);   // past the end: loops round
    check (e.isCuePlaying (7) && e.getVoices().front().looping && e.getVoices().front().position < 0.5, "Loop on: goes round");
    e.setVoiceLoop (0, false);
    out.run (e, 55);
    check (e.isCuePlaying (7), "Loop off: finishes the pass it's in");
    out.run (e, 50);
    check (! e.isCuePlaying (7), "then stops at the end");

    e.play (p);
    e.swapCues (7, 3);
    out.run (e);
    check (e.isCuePlaying (3) && ! e.isCuePlaying (7), "swapping buttons: the voice carries on as the new button");
    e.stopAll (false);
    out.run (e, 2);
}

static void testLoopPoints()
{
    std::puts ("Loop points");
    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    Outputs out (2);
    // the sample value is its own position in seconds, so the output tells where in the file playback is
    auto ramp = makeAudio (1, 4.0, [] (int, int i) { return (float) i / (float) rate; });
    auto filePos = [&e] { const auto v = e.getVoices(); return v.empty() ? -1.0 : v.front().filePosition; };

    Cue c = monoOut (0);
    c.trimIn = 0.5;
    c.trimOut = 3.5;
    c.loopIn = 1.0;
    c.loopOut = 2.0;
    c.loop = true;
    PlayPlan p;
    check (PlayPlan::make (c, *ramp, 1, p) && p.loop && p.loopStart == (juce::int64) (0.5 * rate) && p.loopEnd == (juce::int64) (1.5 * rate)
           && p.length == (juce::int64) (3.0 * rate) && p.numSegments == 1,
           "a Loop button with loop points: intro + loop + outro on one timeline (no joins where it carries straight on)");

    e.play (p);
    out.run (e, 20);
    check (std::abs (filePos() - 0.7) < 0.015, "starts at In (the part before Loop In is an intro)");
    out.run (e, 140);   // 1.6 s in: past Loop Out (after 1.5 s), back to Loop In
    check (std::abs (filePos() - 1.1) < 0.015 && e.getVoices().front().looping, "Loop Out jumps back to Loop In");
    e.setVoiceLoop (0, false);
    out.run (e, 60);
    check (std::abs (filePos() - 1.7) < 0.015, "Loop off: plays on through the loop...");
    out.run (e, 100);
    check (std::abs (filePos() - 2.7) < 0.015, "...past Loop Out without jumping back (the outro)");
    out.run (e, 90);
    check (! e.isCuePlaying (1), "and stops at the Out point");

    // loop start option: straight at Loop In
    c.loopFromLoopIn = true;
    PlayPlan::make (c, *ramp, 1, p);
    e.play (p);
    out.run (e, 10);
    check (std::abs (filePos() - 1.1) < 0.015, "\"Loop starts at Loop In\": no intro");
    e.stopAll (false);
    out.run (e, 2);
    c.loopFromLoopIn = false;

    // played normally, then Loop pressed (Now Playing): by the loop points from where it is
    Cue once = c;
    once.loop = false;
    PlayPlan normal, loopPlan;
    PlayPlan::make (once, *ramp, 2, normal);
    check (! normal.loop && normal.loopEnd < 0, "Play once: plays In to Out");
    PlayPlan::makeLoop (once, *ramp, 2, loopPlan);
    e.play (normal);
    out.run (e, 30);    // file 0.8, before the loop
    e.setVoiceLoop (0, true, &loopPlan);
    out.run (e, 100);
    check (std::abs (filePos() - 1.8) < 0.015 && e.getVoices().front().looping, "Loop pressed while playing: carries on from the same place");
    out.run (e, 40);    // past Loop Out (2.0)
    check (std::abs (filePos() - 1.2) < 0.015, "Loop Out jumps back to Loop In (no restart at In)");
    e.stopAll (false);
    out.run (e, 2);

    e.play (normal);
    out.run (e, 200);   // file 2.5: already past Loop Out
    e.setVoiceLoop (0, true, &loopPlan);
    out.run (e, 50);
    check (std::abs (filePos() - 3.0) < 0.015 && e.getVoices().front().looping, "pressed after Loop Out: plays on to the Out point");
    out.run (e, 70);    // 0.2 s past Out
    check (std::abs (filePos() - 1.2) < 0.015, "then goes to Loop In");
    e.stopAll (false);
    out.run (e, 2);

    // loop points anywhere: before the In point
    Cue early = c;
    early.loopIn = 0.1;
    early.loopOut = 0.4;
    PlayPlan::make (early, *ramp, 3, p);
    e.play (p);
    out.run (e, 5);
    check (std::abs (filePos() - 0.15) < 0.015, "loop points before In: the loop starts straight away");
    out.run (e, 30);
    check (std::abs (filePos() - 0.15) < 0.015, "and goes round");
    e.stopAll (false);
    out.run (e, 2);

    // no loop points: the whole clip loops (as before)
    Cue whole = c;
    whole.loopIn = whole.loopOut = -1.0;
    PlayPlan::make (whole, *ramp, 4, p);
    check (p.loop && p.loopEnd < 0 && p.length == (juce::int64) (3.0 * rate), "no loop points: the whole clip loops");

    // a looping button stops when another button on its output is pressed, in every playback mode
    ShowController sc (e);
    sc.setPlaybackMode (PlaybackMode::overlap);
    sc.getShow().cue (0) = c;
    sc.getShow().cue (1) = monoOut (0);
    sc.getShow().cue (2) = monoOut (1);
    for (int i = 0; i < 3; ++i)
        sc.setAudioForTest (i, ramp);
    sc.play (0);
    out.run (e, 10);
    sc.play (2);
    out.run (e, 10);
    check (e.isCuePlaying (0) && e.isCuePlaying (2), "overlap: a button on another output leaves the loop playing");
    sc.play (1);
    out.run (e, 110);   // its stop fade (1 s)
    check (! e.isCuePlaying (0) && e.isCuePlaying (1) && e.isCuePlaying (2), "a button on its output stops the loop (with its stop fade)");
    e.stopAll (false);
    out.run (e, 2);

    auto loaded = Cue::fromVar (juce::JSON::parse (juce::JSON::toString (c.toVar())));
    check (loaded.loopIn == 1.0 && loaded.loopOut == 2.0 && loaded.loop && ! loaded.loopFromLoopIn, "loop points are saved in the show");
}

static void testSelfDestruct()
{
    std::puts ("Self Destruct");
    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    Outputs out (2);
    ShowController sc (e);
    auto& show = sc.getShow();
    for (int i = 0; i < 3; ++i)
    {
        show.cue (i) = monoOut (0);
        sc.setAudioForTest (i, constant (1, 0.3, 0.2f));
    }
    auto tick = [&] (int blocks) { for (int b = 0; b < blocks; ++b) { out.run (e); sc.updateSelfDestruct(); } };

    sc.play (0);
    tick (40);
    check (! show.cue (0).spent, "mode off: a button keeps its colour after playing");

    sc.setSelfDestruct (true);
    sc.play (0);
    tick (5);
    check (! show.cue (0).spent, "still its colour while it plays");
    tick (40);
    check (show.cue (0).spent, "black once it has played to the end");
    sc.play (1);
    tick (5);
    e.stopCue (1, false);
    tick (5);
    check (show.cue (1).spent && ! show.cue (2).spent, "black when stopped by hand too; others untouched");

    PlayPlan plan;   // a preview from the waveform editor doesn't count
    PlayPlan::make (show.cue (2), *sc.getAudio (2), 2, plan);
    plan.preview = true;
    e.play (plan);
    tick (40);
    check (! show.cue (2).spent, "a preview in the waveform editor doesn't count");

    auto loaded = Cue::fromVar (juce::JSON::parse (juce::JSON::toString (show.cue (0).toVar())));
    Show copy;
    copy.fromVar (juce::JSON::parse (juce::JSON::toString (show.toVar())));
    check (loaded.spent && copy.selfDestruct, "black buttons and the mode are saved in the show");

    sc.resetColour (0);
    check (! show.cue (0).spent && show.cue (1).spent, "Reset colour: just that button");
    sc.resetColours();
    check (! sc.anySpent(), "Reset: every button gets its colour back");
    sc.setSelfDestruct (false);
    sc.play (0);
    tick (40);
    check (! show.cue (0).spent, "mode off again: colours stay");
}

static void testShowCheck()
{
    std::puts ("Show check");
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("StingerCheckTest");
    folder.deleteRecursively();
    folder.getChildFile ("Moved").getChildFile ("Deeper").createDirectory();
    const auto real = folder.getChildFile ("real.wav");
    const auto moved = folder.getChildFile ("Moved").getChildFile ("Deeper").getChildFile ("Gone.WAV");
    real.replaceWithText ("x");
    moved.replaceWithText ("x");

    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    ShowController sc (e);
    auto& show = sc.getShow();
    auto kinds = [&sc] (ShowIssue::Kind k) { int n = 0; for (auto& i : sc.checkShow()) n += i.kind == k ? 1 : 0; return n; };

    check (sc.checkShow().empty(), "an empty show is fine");
    show.cue (0) = monoOut (0);
    show.cue (0).file = real;
    check (sc.checkShow().empty(), "a good button is fine");

    show.cue (1) = monoOut (0);
    show.cue (1).file = folder.getChildFile ("gone.wav");
    show.cue (1).trimIn = 0.02;
    show.cue (1).name = "Walk-in";
    show.cue (2) = show.cue (1);   // two buttons sharing a file
    check (kinds (ShowIssue::Kind::missingAudio) == 2, "missing audio is reported per button");

    show.cue (3) = monoOut (5);
    show.cue (3).file = real;
    check (kinds (ShowIssue::Kind::noSuchOutput) == 1, "an output this device doesn't have");
    show.cue (3).output = {};

    show.cue (3).plugins[1].description = "<PLUGIN name=\"Some Reverb\"/>";
    check (kinds (ShowIssue::Kind::missingPlugin) == 1, "a plugin that isn't loaded");
    show.cue (3).plugins[1] = {};

    sc.addToPlaylist (0, 7);
    check (kinds (ShowIssue::Kind::emptyEntry) == 1, "a playlist song whose button is empty");
    sc.removeEntry (0, 0);

    FileSearch search;
    search.start ({ "gone.wav", "never there.wav" }, { folder });
    for (int i = 0; i < 6000 && search.isSearching(); ++i)
        juce::Thread::sleep (5);
    const auto found = search.takeFound();
    check (found.size() == 1 && found.count ("gone.wav") == 1 && found.at ("gone.wav") == moved,
           "the search finds a file by name, any case, in folders inside folders");
    check (! search.isSearching(), "the search ends by itself");

    sc.relinkFile (show.cue (1).file, moved);
    check (show.cue (1).file == moved && show.cue (2).file == moved, "both buttons follow the found file");
    check (near ((float) show.cue (1).trimIn, 0.02f) && show.cue (1).name == "Walk-in", "the button keeps its trims and name");
    check (sc.hasUnsavedChanges(), "that's a change to save");
    check (kinds (ShowIssue::Kind::missingAudio) == 0, "nothing missing any more");
    for (int i = 0; i < 400 && sc.getLoader().pending() > 0; ++i)
        juce::Thread::sleep (5);
    search.start ({ "x.wav" }, FileSearch::wholeComputer());
    juce::Thread::sleep (20);
    search.cancel();
    for (int i = 0; i < 6000 && search.isSearching(); ++i)
        juce::Thread::sleep (5);
    check (! search.isSearching(), "a search can be stopped");
    folder.deleteRecursively();
}

static void testDeviceLoss()
{
    std::puts ("Device loss");
    {
        PlaybackEngine e;
        e.prepareForTest (rate, 8);
        Outputs out (8);
        auto ramp = makeAudio (1, 4.0, [] (int, int i) { return (float) i / 192000.0f; });

        PlayPlan plan;
        auto c = monoOut (6);
        PlayPlan::make (c, *ramp, 0, plan);
        e.play (plan);
        out.run (e, 10);
        check (e.getVoices().size() == 1 && near ((float) e.getVoices()[0].position, 0.1f, 0.02f), "playing at 0.1 s");
        e.advanceVoices ((juce::int64) (rate * 1.5));
        out.run (e, 10);
        check (near ((float) e.getVoices()[0].position, 1.7f, 0.02f), "after a 1.5 s dropout the song is where it would have been");
        check (out.at (6) > 0.3f, "and it's sounding from that place");
        e.advanceVoices ((juce::int64) (rate * 10.0));
        out.run (e, 2);
        check (e.getVoices().empty(), "a song that would have finished by now is done");

        // loops wrap
        auto l = monoOut (0);
        l.loop = true;
        PlayPlan::make (l, *ramp, 0, plan);
        plan.loop = true;
        e.play (plan);
        out.run (e, 5);
        e.advanceVoices ((juce::int64) (rate * 9.0));
        out.run (e, 5);
        check (e.getVoices().size() == 1 && near ((float) e.getVoices()[0].position, 1.1f, 0.05f), "a loop wraps round");
        e.stopAll (false);
        out.run (e, 2);

        // routing on the backup
        auto c2 = monoOut (6);
        PlayPlan::make (c2, *ramp, 0, plan);
        e.setBackupRouting (true, 2);
        e.play (plan);
        out.run (e, 20);
        check (out.at (2) > 0.0f && out.at (3) > 0.0f && std::abs (out.at (6)) < 1.0e-6f, "a chosen backup output takes every song");
        e.stopAll (false);
        out.run (e, 2);
    }
    {
        PlaybackEngine e;
        e.prepareForTest (rate, 2);
        Outputs out (2);
        auto ramp = constant (1, 1.0, 0.25f);
        PlayPlan plan;
        auto c = monoOut (6);
        PlayPlan::make (c, *ramp, 0, plan);
        e.setBackupRouting (true, -1);
        e.play (plan);
        out.run (e, 20);
        check (out.at (0) > 0.1f, "a backup with fewer outputs: the song comes out of 1-2, not silence");
        e.setBackupRouting (false, -1);
        out.run (e, 1);
        check (std::abs (out.at (0)) < 1.0e-6f, "back on the main device: its own output again");
        e.stopAll (false);
        out.run (e, 2);
    }
    {
        // a backup at another sample rate: the audio reloads, then the song restarts where it would be
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("StingerDeviceTest");
        folder.deleteRecursively();
        folder.createDirectory();
        const auto wav = folder.getChildFile ("song.wav");
        {
            juce::AudioBuffer<float> buffer (1, 480000);
            for (int i = 0; i < 480000; ++i)
                buffer.setSample (0, i, 0.25f);
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::FileOutputStream (wav), 48000.0, 1, 24, {}, 0));
            writer->writeFromAudioSampleBuffer (buffer, 0, 480000);
        }
        PlaybackEngine e;
        e.prepareForTest (rate, 2);
        Outputs out (2);
        ShowController sc (e);
        sc.getShow().cue (0) = monoOut (0);
        sc.getShow().cue (0).file = wav;
        juce::String error;
        check (sc.loadNow (0, error), "song loaded");
        bool lost = false;
        sc.resumeAfterRateChange = [&lost] { return lost; };
        auto pump = [&] (int ms)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
            out.run (e, 1);
        };
        sc.play (0);
        for (int i = 0; i < 10; ++i) { out.run (e, 10); pump (30); }
        check (e.getVoices().size() == 1, "playing before the change");

        // a rate picked by hand: stopped, as it always was
        e.prepareForTest (44100.0, 2);
        for (int i = 0; i < 40 && sc.isLoading (0) == false; ++i) pump (20);
        for (int i = 0; i < 400 && sc.isLoading (0); ++i) pump (20);
        pump (150);
        check (e.getVoices().empty(), "a rate changed by hand stops playback");

        sc.play (0);
        for (int i = 0; i < 10; ++i) { out.run (e, 10); pump (30); }
        check (e.getVoices().size() == 1, "playing again at the new rate");
        const auto before = e.getVoices()[0].position;
        lost = true;
        e.prepareForTest (rate, 2);
        for (int i = 0; i < 40 && sc.isLoading (0) == false; ++i) pump (20);
        for (int i = 0; i < 400 && sc.isLoading (0); ++i) pump (20);
        for (int i = 0; i < 40 && e.getVoices().empty(); ++i) pump (30);
        const auto voices = e.getVoices();
        check (voices.size() == 1, "the device lost and the rate different: the song plays again");
        check (! voices.empty() && voices[0].position > before - 0.3, "from where it was, not from the top (the test runs audio faster than real time)");
        e.stopAll (false);
        out.run (e, 2);
        folder.deleteRecursively();
    }
}

static void testLoudness()
{
    std::puts ("Loudness");
    auto sine = [] (int channels, double seconds, double dbfs)
    {
        const double a = std::pow (10.0, dbfs / 20.0), w = juce::MathConstants<double>::twoPi * 997.0 / rate;
        return makeAudio (channels, seconds, [a, w] (int, int i) { return (float) (a * std::sin (w * i)); });
    };
    // the EBU reference: a stereo 1 kHz sine at -23 dBFS reads -23 LUFS
    auto reference = sine (2, 20.0, -23.0);
    const auto all = std::vector<Segment> { { 0.0, 20.0 } };
    auto r = measureLoudness (*reference, all);
    std::printf ("  stereo -23 dBFS sine: %.2f LUFS, peak %.2f\n", r.lufs, r.peakDb);
    check (std::abs (r.lufs - -23.0) < 0.1, "reference tone reads -23.0 LUFS");
    check (std::abs (r.peakDb - -23.0) < 0.05, "and its peak");
    auto mono = measureLoudness (*sine (1, 20.0, -23.0), all);
    check (std::abs (mono.lufs - -26.0) < 0.1, "the same tone on one channel reads 3 LU lower");

    // only what's trimmed counts: quiet first half, loud second half
    auto half = makeAudio (2, 20.0, [] (int, int i)
    {
        const double a = std::pow (10.0, (i < 480000 ? -40.0 : -20.0) / 20.0);
        return (float) (a * std::sin (juce::MathConstants<double>::twoPi * 997.0 / rate * i));
    });
    auto loud = measureLoudness (*half, { { 10.0, 20.0 } });
    check (std::abs (loud.lufs - -20.0) < 0.15, "a trimmed song is measured by what plays");
    auto gated = measureLoudness (*half, all);
    check (std::abs (gated.lufs - -20.0) < 0.3, "quiet passages far below the song are gated out");
    check (! measureLoudness (*constant (2, 5.0, 0.0f), all).isValid(), "silence has no reading");
    check (! measureLoudness (*sine (2, 0.2, -20.0), { { 0.0, 0.2 } }).isValid(), "too short to measure");

    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    ShowController sc (e);
    auto& show = sc.getShow();
    show.cue (0) = monoOut (0);
    show.cue (0).file = juce::File::getCurrentWorkingDirectory().getChildFile ("music.wav");
    sc.setAudioForTest (0, sine (2, 20.0, -20.0));
    show.cue (1) = show.cue (0);
    show.cue (1).voiceOver = true;
    sc.setAudioForTest (1, sine (2, 20.0, -20.0));
    auto waitFor = [&sc] (int cue, const juce::String& not_)
    {
        for (int i = 0; i < 300 && sc.getShow().cue (cue).lufsKey == not_; ++i)
        {
            sc.updateLoudness();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }
    };
    waitFor (0, {});
    waitFor (1, {});
    check (std::abs (show.cue (0).lufs - -20.0) < 0.1, "measured in the background after loading");
    const auto music = sc.suggestedLevel (0), voice = sc.suggestedLevel (1);
    check (music.has_value() && near (*music, 4.0f, 0.1f), "music: suggests reaching -16 LUFS");
    check (voice.has_value() && near (*voice, 0.0f, 0.1f), "voice-over: its own, lower target");
    check (near (show.cue (0).levelDb, 0.0f), "nothing changes until it's applied");
    sc.applyLoudness (0);
    check (near (show.cue (0).levelDb, 4.0f, 0.1f), "Apply sets the level");

    const auto key = show.cue (0).lufsKey;
    show.cue (0).trimOut = 12.0;
    waitFor (0, key);
    check (show.cue (0).lufsKey != key, "new trims are measured again");
    auto saved = Cue::fromVar (juce::JSON::parse (juce::JSON::toString (show.cue (1).toVar())));
    check (saved.voiceOver && saved.lufsKey == show.cue (1).lufsKey && saved.lufs < 0.0, "kind and reading are saved with the button");
    sc.setLoudnessTargets (-14.0, -18.0);
    check (near (*sc.suggestedLevel (1), 2.0f, 0.1f), "targets can be changed");
}

static void testRunOfShow()
{
    std::puts ("Run of show");
    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    Outputs out (2);
    ShowController sc (e);
    auto& show = sc.getShow();
    for (int i = 0; i < 2; ++i)
    {
        show.cue (i) = monoOut (0);
        show.cue (i).file = juce::File::getCurrentWorkingDirectory().getChildFile ("song" + juce::String (i) + ".wav");
        sc.setAudioForTest (i, constant (1, 2.0, 0.2f));
    }
    sc.addRunRow ({ 0, "7:00", "Host", "Opening" });
    sc.addRunRow ({ -1, "7:05", "Dr. Lee", "Speech, 10 minutes" });
    sc.addRunRow ({ 1, "7:15", "Award winner", {} });
    check (sc.getRunNext() == 0 && sc.getRunOnAir() < 0, "starts at the top");
    sc.runGo();
    out.run (e, 2);
    check (e.isCuePlaying (0) && sc.getRunNext() == 1 && sc.getRunOnAir() == 0, "GO plays the row and moves on");
    sc.runGo();
    out.run (e, 2);
    check (e.isCuePlaying (0) && sc.getRunNext() == 2 && sc.getRunOnAir() == 1, "a note plays nothing and moves on");
    sc.runGo();
    out.run (e, 8);
    check (e.isCuePlaying (1) && sc.getRunNext() < 0, "the last row: nothing left");
    sc.runGo();
    check (sc.getRunOnAir() == 2, "GO past the end does nothing");
    sc.setRunNext (1);
    check (sc.getRunNext() == 1, "Next can be set by hand");
    sc.moveRunRow (2, 0);
    check (show.run[0].person == "Award winner" && sc.getRunNext() == 2, "Next stays on its row when rows move");
    sc.swapCues (0, 1);
    check (show.run[0].cueId == 0 && show.run[1].cueId == 1 && show.run[2].cueId == -1, "rows follow swapped buttons");
    Show copy;
    copy.fromVar (juce::JSON::parse (juce::JSON::toString (show.toVar())));
    check (copy.run.size() == 3 && copy.run[2].notes == "Speech, 10 minutes" && copy.run[1].time == "7:00", "saved in the show");
    sc.removeRunRow (2);
    check (sc.getRunNext() < 0, "removing the Next row");
    e.stopAll (false);
    out.run (e, 2);
}

static void testSchedule()
{
    std::puts ("Schedule");
    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    Outputs out (2);
    ShowController sc (e);
    juce::int64 now = juce::Time (2026, 10, 6, 19, 0, 0, 0, true).toMilliseconds();
    sc.clockForTest = [&now] { return now; };
    auto& show = sc.getShow();
    for (int i = 0; i < 4; ++i)
    {
        show.cue (i) = monoOut (0);
        show.cue (i).name = "Song " + juce::String (i);
        show.cue (i).file = juce::File::getCurrentWorkingDirectory().getChildFile ("song" + juce::String (i) + ".wav");
        sc.setAudioForTest (i, constant (1, 30.0, 0.2f));
    }
    show.playbackMode = PlaybackMode::overlap;
    auto clockItem = [] (int cue, int h, int m) { ScheduleItem s; s.cueId = cue; s.kind = ScheduleItem::clock; s.secondsOfDay = h * 3600 + m * 60; return s; };
    auto delayItem = [] (int cue, double seconds) { ScheduleItem s; s.cueId = cue; s.kind = ScheduleItem::delay; s.delaySeconds = seconds; return s; };
    auto advance = [&] (double seconds) { now += (juce::int64) (seconds * 1000.0); sc.updateSchedule(); out.run (e, 2); };
    using U = ShowController::Urgency;

    sc.addScheduleItem (clockItem (0, 18, 0));   // already past
    sc.addScheduleItem (clockItem (1, 19, 5));
    sc.addScheduleItem (delayItem (2, 120.0));
    check (sc.getNextScheduled() == -1, "nothing runs before Start");
    advance (1.0);
    now -= 1000;
    sc.startSchedule();
    check (show.schedule[0].state == ScheduleItem::missed && sc.getMissedCount() == 1, "a time already past is missed");
    advance (10.0);
    check (! e.isCuePlaying (0), "and it never fires late");
    check (sc.getNextScheduled() == 2 && near ((float) sc.getScheduleRemaining(), 110.0f, 0.1f), "the delay counts from Start, next in line");
    check (sc.getScheduleUrgency() == U::green, "green when started");
    advance (55.0);
    check (sc.getScheduleUrgency() == U::yellow, "yellow at 1 minute");
    advance (46.0);
    check (sc.getScheduleUrgency() == U::red, "red at 10 seconds");
    advance (10.0);
    check (e.isCuePlaying (2) && show.schedule[2].state == ScheduleItem::fired, "fires on time");
    check (sc.getNextScheduled() == 1, "on to the next trigger");

    // holds
    now = juce::Time (2026, 10, 6, 19, 4, 50, 0, true).toMilliseconds();
    check (sc.getScheduleUrgency() == U::red, "10 seconds to go");
    sc.holdSchedule (2);
    check (near ((float) sc.getScheduleRemaining(), 130.0f, 0.1f) && sc.getScheduleUrgency() == U::green, "a 2 minute hold adds 2 minutes");
    sc.holdSchedule (5);
    check (near ((float) sc.getScheduleRemaining(), 430.0f, 0.1f), "holds add up");
    advance (11.0);
    check (! e.isCuePlaying (1), "held: doesn't fire at its old time");
    sc.go (0);
    check (show.schedule[1].state == ScheduleItem::defeated && sc.getNextScheduled() == -1, "GO during a hold defeats the countdown");
    advance (500.0);
    check (! e.isCuePlaying (1), "a defeated trigger never fires");

    sc.stopSchedule();
    sc.setScheduleItem (1, delayItem (3, 60.0));
    sc.startSchedule();
    sc.holdSchedule (1);
    sc.trigger (3);
    check (show.schedule[1].state == ScheduleItem::defeated, "its own button pressed during a hold defeats it too");
    sc.stopSchedule();
    e.stopAll (false);
    out.run (e, 2);

    sc.setScheduleItem (1, delayItem (3, 60.0));
    sc.startSchedule();
    sc.go (0);
    check (show.schedule[1].state == ScheduleItem::waiting, "GO with no hold leaves the countdown running");
    sc.scheduleGoNow();
    out.run (e, 2);
    check (e.isCuePlaying (3) && show.schedule[1].state == ScheduleItem::fired, "the countdown's own GO fires it now");
    sc.stopSchedule();
    e.stopAll (false);
    out.run (e, 2);

    // a stalled PC: long past the time, it doesn't fire
    sc.setScheduleItem (1, delayItem (3, 5.0));
    sc.startSchedule();
    advance (30.0);
    check (! e.isCuePlaying (3) && show.schedule[1].state == ScheduleItem::missed, "woken long after its time: missed, not late");
    sc.stopSchedule();
    sc.setScheduleItem (1, delayItem (3, 5.0));
    sc.startSchedule();
    sc.trigger (3);
    out.run (e, 2);
    advance (5.0);
    check (e.isCuePlaying (3) && show.schedule[1].state == ScheduleItem::fired, "already playing at its time: left alone");
    sc.stopSchedule();
    {
        // nothing fires during a hold; a trigger beyond the hold keeps its time
        auto e2 = std::make_unique<PlaybackEngine>();
        e2->prepareForTest (rate, 2);
        ShowController hold (*e2);
        hold.clockForTest = [&now] { return now; };
        const auto a = delayItem (0, 60.0), b = delayItem (1, 120.0), c = delayItem (2, 3600.0);
        hold.addScheduleItem (a);
        hold.addScheduleItem (b);
        hold.addScheduleItem (c);
        hold.startSchedule();
        const auto far = hold.getShow().schedule[2].fireAt;
        hold.holdSchedule (5);
        const auto& list = hold.getShow().schedule;
        check (list[1].fireAt > list[0].fireAt, "a trigger landing inside a hold moves with it");
        check (list[2].fireAt == far, "one beyond the hold keeps its time");
        check (hold.getNextScheduled() == 0, "the held trigger is still next");
    }

    Show copy;
    copy.fromVar (juce::JSON::parse (juce::JSON::toString (show.toVar())));
    check (copy.schedule.size() == 3 && copy.schedule[1].cueId == 3 && copy.schedule[0].secondsOfDay == 18 * 3600, "saved in the show");

    // crash in the middle of a countdown
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("StingerScheduleTest");
    folder.deleteRecursively();
    {
        Autosave autosave (folder);
        sc.setAutosave (&autosave);
        sc.addRunRow ({ 0, {}, "Host", {} });
        sc.addRunRow ({ 1, {}, "Guest", {} });
        sc.runGo();
        sc.setScheduleItem (1, delayItem (3, 300.0));
        sc.startSchedule();
        sc.holdSchedule (2);
        sc.setAutosaveIntervalForTest (0);
        sc.setSelfDestruct (true);
        sc.updateAutosave();
        for (int i = 0; i < 6000 && ! autosave.isIdle(); ++i)
            juce::Thread::sleep (5);
        sc.setAutosave (nullptr);
    }
    {
        ShowController back (e);
        back.clockForTest = sc.clockForTest;
        juce::String error;
        check (back.openRecovery (Autosave::fileIn (folder), error), "recovers");
        check (back.isScheduleRunning() && back.getNextScheduled() == 2 && near ((float) back.getScheduleRemaining(), 240.0f, 1.0f),
               "a running countdown comes back, hold included");
        check (back.getRunNext() == 1 && back.getRunOnAir() == 0, "the run of show keeps its place");
    }
    {
        now += 3600000;
        ShowController back (e);
        back.clockForTest = sc.clockForTest;
        juce::String error;
        back.openRecovery (Autosave::fileIn (folder), error);
        check (back.getNextScheduled() < 0 && back.getMissedCount() >= 1, "recovered after its time: missed, not fired");
    }
    e.stopAll (false);
    out.run (e, 2);
    folder.deleteRecursively();
}

static void testOsc()
{
    std::puts ("OSC");
    auto action = [] (const char* address, std::optional<int> value = {})
    {
        juce::OSCMessage m { juce::OSCAddressPattern (address) };
        if (value)
            m.addInt32 (*value);
        return OscBridge::actionFor (m);
    };
    check (action ("/stinger/go") == "go", "GO");
    check (action ("/stinger/go/2") == "go2" && action ("/stinger/go", 3) == "go3", "GO a playlist, by address or value");
    check (action ("/stinger/stop") == "stopAll" && action ("/stinger/fade") == "fadeAll" && action ("/stinger/fadenext") == "fadeNext", "stop, fade, fade to next");
    check (action ("/stinger/slot/5") == "slot5", "button of the shown bank");
    check (action ("/stinger/cue/2/3") == "cue:" + juce::String (Show::cueId (1, 2)) && action ("/stinger/cue/b/3") == "cue:" + juce::String (Show::cueId (1, 2)), "bank and button, by number or letter");
    check (action ("/stinger/bank/2") == "bank2" && action ("/stinger/bank/next") == "bankNext", "banks");
    check (action ("/stinger/run/go") == "runGo" && action ("/stinger/schedule/start") == "schedStart" && action ("/stinger/hold/5") == "hold5", "run of show, schedule, holds");
    check (action ("/stinger/go", 1) == "go1" && action ("/stinger/stop", 1) == "stopAll", "a press with value 1");
    check (action ("/stinger/stop", 0).isEmpty(), "value 0 (the release) is ignored");
    check (action ("/other/go").isEmpty() && action ("/stinger/nonsense").isEmpty() && action ("/stinger/cue/1/99").isEmpty(), "anything else is ignored");

    // the real thing through UDP
    constexpr int inPort = 52901, outPort = 52902;
    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    Outputs out (2);
    ShowController sc (e);
    sc.getShow().cue (0) = monoOut (0);
    sc.getShow().cue (0).name = "Walk-in";
    sc.getShow().cue (0).file = juce::File::getCurrentWorkingDirectory().getChildFile ("x.wav");
    sc.setAudioForTest (0, constant (1, 2.0, 0.2f));

    OscBridge osc (sc);
    juce::StringArray performed;
    osc.perform = [&] (const juce::String& a) { performed.add (a); sc.trigger (0); };
    osc.shownBank = [] { return 0; };
    check (osc.start (inPort, "127.0.0.1", outPort), "opens its port");

    struct Listen : juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
    {
        void oscMessageReceived (const juce::OSCMessage& m) override
        {
            const juce::ScopedLock sl (lock);
            auto text = m[0].isString() ? m[0].getString() : m[0].isInt32() ? juce::String (m[0].getInt32()) : juce::String (m[0].getFloat32(), 1);
            got[m.getAddressPattern().toString()] = text;
        }
        juce::String value (const juce::String& a) { const juce::ScopedLock sl (lock); return got.count (a) > 0 ? got[a] : juce::String(); }
        juce::CriticalSection lock;
        std::map<juce::String, juce::String> got;
    } listen;
    juce::OSCReceiver receiver;
    receiver.addListener (&listen);
    check (receiver.connect (outPort), "test listener");

    juce::OSCSender desk;
    desk.connect ("127.0.0.1", inPort);
    desk.send ("/stinger/slot/1", 1);
    for (int i = 0; i < 100 && performed.isEmpty(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    check (performed.size() == 1 && performed[0] == "slot1", "a message from the network does the action");
    out.run (e, 2);
    osc.sendChanges();
    for (int i = 0; i < 100 && listen.value ("/stinger/now/name").isEmpty(); ++i)
        juce::Thread::sleep (5);
    check (listen.value ("/stinger/now/name") == "Walk-in" && listen.value ("/stinger/now/playing") == "1", "sends what's playing");
    check (listen.value ("/stinger/slot/1/name") == "Walk-in" && listen.value ("/stinger/slot/1/playing") == "1", "and the shown bank's buttons");
    check (listen.value ("/stinger/now/time") == "0:02", "with the time left");

    receiver.removeListener (&listen);
    receiver.disconnect();
    e.stopAll (false);
    out.run (e, 2);
}

static void testBackupLink()
{
    std::puts ("Backup laptop");
    constexpr int port = 52903;
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("StingerBackupTest");
    root.deleteRecursively();
    const auto mainFolder = root.getChildFile ("Main"), copies = root.getChildFile ("Copies");
    mainFolder.createDirectory();
    const auto wav = mainFolder.getChildFile ("Walk in.wav");
    {
        juce::AudioBuffer<float> buffer (1, 960000);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, 0.25f);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::FileOutputStream (wav), 48000.0, 1, 24, {}, 0));
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }
    auto mainEngine = std::make_unique<PlaybackEngine>();
    auto backupEngine = std::make_unique<PlaybackEngine>();
    mainEngine->prepareForTest (rate, 2);
    backupEngine->prepareForTest (rate, 2);
    auto mainOut = std::make_unique<Outputs> (2);
    auto backupOut = std::make_unique<Outputs> (2);
    ShowController main (*mainEngine), backup (*backupEngine);
    BackupLink mainLink (main), backupLink (backup);

    auto pump = [&] (int ms)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        while (juce::Time::getMillisecondCounter() < end)
        {
            mainOut->run (*mainEngine);
            backupOut->run (*backupEngine);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }
    };
    auto until = [&] (std::function<bool()> done, int ms)
    {
        for (int waited = 0; waited < ms && ! done(); waited += 20)
            pump (20);
        return done();
    };

    main.getShow().cue (0) = monoOut (0);
    main.getShow().cue (0).file = wav;
    main.getShow().cue (0).name = "Walk in";
    main.getShow().cue (0).trimIn = 0.5;
    juce::String error;
    check (main.loadNow (0, error), "main loads its song");

    // the wrong code gets nowhere
    check (mainLink.startMain ("RIGHT", port), "the main laptop listens");
    backupLink.startBackup ("127.0.0.1", "WRONG", BackupLink::Mode::warm, copies, port);
    pump (700);
    check (! mainLink.isConnected() && ! backupLink.isSynced() && ! copies.getChildFile ("960000").exists(), "a wrong code is turned away");

    backupLink.startBackup ("127.0.0.1", "RIGHT", BackupLink::Mode::warm, copies, port);
    check (until ([&] { return mainLink.isConnected(); }, 4000), "the right code connects");
    check (until ([&] { return backupLink.isSynced(); }, 8000), "the show arrives");
    const auto& copy = backup.getShow().cue (0);
    check (copy.name == "Walk in" && near ((float) copy.trimIn, 0.5f), "with the button's settings");
    check (copy.file != wav && copy.file.isAChildOf (copies) && copy.file.getSize() == wav.getSize(), "and the audio copied into the backup's folder");
    check (until ([&] { return backup.getAudio (0) != nullptr; }, 3000), "the copy loads");

    // following
    main.play (0);
    check (until ([&] { return backupEngine->isCuePlaying (0); }, 1000), "a song started on the main laptop plays on the backup");
    pump (400);
    backupOut->run (*backupEngine);
    check (std::abs (backupOut->at (0)) < 1.0e-6f, "warm standby: not a sound");
    auto drift = [&]
    {
        auto a = mainEngine->getVoices(), b = backupEngine->getVoices();
        return a.empty() || b.empty() ? 1.0 : std::abs (a[0].position - b[0].position);
    };
    std::printf ("  the two laptops are %.0f ms apart\n", drift() * 1000.0);
    check (drift() < 0.1, "in step with the main laptop");

    const auto voice = mainEngine->getVoices()[0];
    mainEngine->seekVoice (voice.voice, (juce::int64) (5.0 * rate));
    pump (300);
    std::printf ("  after a jump on main: %.0f ms apart\n", drift() * 1000.0);
    check (drift() < 0.1, "a jump on the main laptop is followed");

    mainEngine->stopCue (0, false);
    check (until ([&] { return ! backupEngine->isCuePlaying (0); }, 1500), "stopped on main: stopped here");

    // changes come across while connected, and the file isn't sent twice
    const auto stamp = copy.file.getLastModificationTime();
    main.getShow().cue (0).name = "Renamed";
    main.getShow().cue (0).levelDb = -3.0f;
    main.setSelfDestruct (true);
    main.addRunRow ({ 0, "7:00", "Host", {} });
    main.addRunRow ({ 0, "7:05", "Guest", {} });
    main.setRunNext (1);
    check (until ([&] { return backup.getShow().cue (0).name == "Renamed"; }, 3000), "an edit on main reaches the backup");
    check (until ([&] { return backup.getRunNext() == 1; }, 3000), "and the run of show's position");
    check (backup.getShow().selfDestruct && near (backup.getShow().cue (0).levelDb, -3.0f), "settings too");
    check (copy.file.getLastModificationTime() == stamp, "the audio isn't copied again");

    // the schedule never runs by itself on a backup
    ScheduleItem item;
    item.cueId = 0;
    item.kind = ScheduleItem::delay;
    item.delaySeconds = 0.1;
    main.addScheduleItem (item);
    main.startSchedule();
    check (until ([&] { return backup.getShow().schedule.size() == 1; }, 3000), "the schedule comes across");
    check (until ([&] { return mainEngine->isCuePlaying (0); }, 1000), "main fires it");
    check (until ([&] { return backupEngine->isCuePlaying (0); }, 1000), "the backup plays it because main does");

    // take over
    backupLink.takeOver();
    check (! backupLink.isFollowing() && ! backup.isMirroring(), "Take over: on its own now");
    pump (100);
    backupOut->run (*backupEngine);
    check (backupEngine->isCuePlaying (0) && backupOut->at (0) > 0.05f, "the song carries on, sound up");
    backup.runGo();
    pump (50);
    check (backup.getRunNext() < 0 && backup.getRunOnAir() == 1, "the show runs from here, from the right place");
    mainEngine->stopAll (false);
    pump (300);
    check (backupEngine->isCuePlaying (0), "the main laptop no longer stops it");
    backupEngine->stopAll (false);
    backupOut->run (*backupEngine, 2);

    // the main laptop dies during a show
    main.getShow().schedule.clear();
    main.stopSchedule();
    backupLink.startBackup ("127.0.0.1", "RIGHT", BackupLink::Mode::hot, copies, port);
    check (until ([&] { return mainLink.isConnected() && backupLink.isSynced(); }, 6000), "reconnects in hot standby");
    main.play (0);
    check (until ([&] { return backupEngine->isCuePlaying (0); }, 1500), "following");
    pump (300);
    backupOut->run (*backupEngine);
    check (backupOut->at (0) > 0.05f, "hot standby: sounding along with main");
    mainLink.stop();
    check (until ([&] { return backupLink.isMainLost(); }, 3500), "the lost main laptop is noticed");
    check (backupEngine->isCuePlaying (0), "and the song plays on here");
    backupLink.stop();
    backupEngine->stopAll (false);
    backupOut->run (*backupEngine, 2);
    mainEngine->stopAll (false);
    mainOut->run (*mainEngine, 2);
    root.deleteRecursively();
}

static void testScheduleOnRemotes()
{
    std::puts ("Schedule on the phone and Stream Deck");
    auto e = std::make_unique<PlaybackEngine>();
    e->prepareForTest (rate, 2);
    ShowController sc (*e);
    juce::int64 now = juce::Time (2026, 10, 6, 19, 0, 0, 0, true).toMilliseconds();
    sc.clockForTest = [&now] { return now; };
    auto& show = sc.getShow();
    show.cue (0) = monoOut (0);
    show.cue (0).name = "Walk-in";
    show.cue (0).file = juce::File::getCurrentWorkingDirectory().getChildFile ("x.wav");
    StreamDeckServer server (sc, 52904);
    auto state = [&server] { return juce::JSON::parse (server.stateJson()); };

    auto s = state();
    check (! (bool) s["schedule"]["running"] && s["schedule"]["next"].isVoid(), "nothing scheduled: no next cue");
    check (! (bool) s["run"]["hasNext"], "an empty run of show");

    ScheduleItem item;
    item.cueId = 0;
    item.kind = ScheduleItem::delay;
    item.delaySeconds = 120.0;
    sc.addScheduleItem (item);
    sc.startSchedule();
    s = state();
    check ((bool) s["schedule"]["running"] && s["schedule"]["next"]["name"] == "Walk-in", "the next cue's name");
    check ((int) s["schedule"]["next"]["remaining"] == 120 && s["schedule"]["next"]["urgency"] == "green", "its time and colour");
    now += 70000;
    check (state()["schedule"]["next"]["urgency"] == "yellow", "yellow at 1 minute");
    now += 45000;
    check (state()["schedule"]["next"]["urgency"] == "red", "red at 10 seconds");
    sc.holdSchedule (5);
    s = state();
    check ((bool) s["schedule"]["next"]["held"] && (int) s["schedule"]["next"]["heldSeconds"] == 300 && s["schedule"]["next"]["urgency"] == "green", "a hold shows");

    sc.addRunRow ({ -1, {}, "Dr. Lee", {} });
    sc.addRunRow ({ 0, {}, {}, {} });
    s = state();
    check ((bool) s["run"]["hasNext"] && s["run"]["next"] == "Dr. Lee", "the run of show's next row, by person");
    sc.setRunNext (1);
    check (state()["run"]["next"] == "Walk-in", "or by song if no person is named");
}

static void testAutosave()
{
    std::puts ("Autosave and crash recovery");
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("StingerAutosaveTest");
    folder.deleteRecursively();
    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    Outputs out (2);
    const auto recovery = Autosave::fileIn (folder);
    auto settle = [] (Autosave& a) { for (int i = 0; i < 6000 && ! a.isIdle(); ++i) juce::Thread::sleep (5); };   // its thread is low priority by design
    {
        Autosave autosave (folder);
        auto engine = std::make_unique<PlaybackEngine>();
        engine->prepareForTest (rate, 2);
        ShowController fresh (*engine);
        fresh.setAutosave (&autosave);
        fresh.setAutosaveIntervalForTest (0);
        fresh.updateAutosave();
        settle (autosave);
        check (! recovery.existsAsFile(), "a new, untouched show leaves nothing to recover");
        fresh.setAutosave (nullptr);
    }
    {
        Autosave autosave (folder);
        ShowController sc (e);
        sc.setAutosave (&autosave);
        sc.setAutosaveIntervalForTest (1000000);
        auto& show = sc.getShow();
        for (int i = 0; i < 2; ++i)
        {
            show.cue (i) = monoOut (0);
            show.cue (i).file = juce::File::getCurrentWorkingDirectory().getChildFile ("song" + juce::String (i) + ".wav");
            show.cue (i).name = "Song " + juce::String (i);
            sc.setAudioForTest (i, constant (1, 0.1, 0.2f));
            sc.addToPlaylist (0, i);
        }
        sc.setNextEntry (0, 1);
        sc.setSelfDestruct (true);
        sc.updateAutosave();   // the first change is copied straight away
        settle (autosave);
        check (recovery.existsAsFile(), "the first change makes a copy at once");
        const int first = autosave.getWriteCount();
        show.cue (1).name = "Renamed";
        sc.updateAutosave();
        settle (autosave);
        check (autosave.getWriteCount() == first, "no new copy before the autosave time");

        sc.play (0);
        for (int b = 0; b < 30; ++b) { out.run (e); sc.updateSelfDestruct(); sc.updateAutosave(); }
        settle (autosave);
        check (show.cue (0).spent && autosave.getWriteCount() > first, "a button going black is written at once");
        const int writes = autosave.getWriteCount();
        sc.setAutosaveIntervalForTest (0);
        sc.updateAutosave();
        settle (autosave);
        check (autosave.getWriteCount() == writes, "an unchanged show isn't written again");

        // a big show: the part on the message thread has to stay tiny
        for (int i = 0; i < Show::cuesPerBank * 4; ++i)
            if (i >= 2)
                show.cue (i).name = "A long enough song name " + juce::String (i);
        sc.setSelfDestruct (true);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 20; ++i)
        {
            show.cue (5).name = "Name " + juce::String (i);
            sc.updateAutosave();
        }
        const auto each = (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0;
        std::printf ("  (%.2f ms on the calling thread per autosave)\n", each);
        check (each < 5.0, "an autosave costs the calling thread almost nothing");
        settle (autosave);
        sc.setAutosave (nullptr);   // "crash": the controller goes without saving or discarding
    }
    const auto found = Autosave::find (folder);
    check (found.has_value() && found->showFile == juce::File(), "a crash leaves a copy; a never saved show comes back unnamed");
    {
        Autosave autosave (folder);
        ShowController sc (e);
        sc.setAutosave (&autosave);
        juce::String error;
        check (found.has_value() && sc.openRecovery (found->file, error), "the copy opens");
        check (sc.getShow().cue (0).spent && ! sc.getShow().cue (1).spent, "black Self Destruct buttons come back");
        check (sc.getShow().selfDestruct, "Self Destruct mode comes back");
        check (sc.getNextEntry (0) == 1, "the playlist's Next comes back");
        check (sc.hasUnsavedChanges(), "the recovered show counts as unsaved");
        const auto saved = folder.getChildFile ("real").getChildFile ("real.stinger");
        sc.updateAutosave();
        settle (autosave);
        check (recovery.existsAsFile(), "recovering writes the copy again (a second crash loses nothing)");
        check (sc.saveShow (saved, error), "saves");
        settle (autosave);
        check (! recovery.existsAsFile() && saved.existsAsFile(), "saving removes the recovery copy");
        sc.getShow().cue (1).spent = true;
        sc.setSelfDestruct (false);
        sc.setAutosaveIntervalForTest (0);
        sc.updateAutosave();
        settle (autosave);
        const auto again = Autosave::find (folder);
        check (again.has_value() && again->showFile == saved, "a saved show's copy remembers its name");
        check (Show().load (saved, error) && ! [&] { Show s; s.load (saved, error); return s.cue (1).spent; }(),
               "autosaving never touches the show file itself");
        sc.setAutosave (nullptr);
    }
    {
        // a saved, unchanged show still has something to lose once it's running
        const auto saved = folder.getChildFile ("clean").getChildFile ("clean.stinger");
        Autosave autosave (folder);
        auto engine = std::make_unique<PlaybackEngine>();
        engine->prepareForTest (rate, 2);
        ShowController sc (*engine);
        sc.setAutosave (&autosave);
        sc.addRunRow ({ -1, {}, "First", {} });
        sc.addRunRow ({ -1, {}, "Second", {} });
        juce::String error;
        check (sc.saveShow (saved, error), "a clean show");
        settle (autosave);
        sc.updateAutosave();
        settle (autosave);
        check (! recovery.existsAsFile(), "nothing to recover from a clean, untouched show");
        sc.runGo();
        sc.updateAutosave();
        settle (autosave);
        check (recovery.existsAsFile(), "moving down the run of show is written at once");
        sc.setAutosave (nullptr);
        settle (autosave);
    }
    Autosave::setAside (folder);
    check (! recovery.existsAsFile() && ! Autosave::find (folder).has_value(), "Don't recover: not asked again");
    folder.deleteRecursively();
}

static void testShowFolders()
{
    std::puts ("Show folders");
    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    ShowController sc (e);
    auto& show = sc.getShow();
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory);
    const auto home = temp.getChildFile ("Stinger folders test");
    home.deleteRecursively();
    sc.setHomeFolderForTest (home);
    const auto source = temp.getChildFile ("Stinger folders source").getChildFile ("Walk up.wav");
    source.getParentDirectory().createDirectory();
    source.replaceWithText ("not really audio");

    sc.assignFile (0, source);
    const auto unsaved = home.getChildFile ("Unsaved/Audio/Walk up.wav");
    check (show.cue (0).file == unsaved && unsaved.existsAsFile() && source.existsAsFile() && show.cue (0).name == "Walk up",
           "unsaved show: a file put on a button is copied into Unsaved\\Audio");
    sc.assignFile (1, source);
    check (show.cue (1).file == unsaved && ! home.getChildFile ("Unsaved/Audio/Walk up (2).wav").exists(),
           "the same file on another button: no second copy");

    juce::String error;
    const auto showFile = sc.showFileFor ("My Show");
    check (showFile == home.getChildFile ("My Show/My Show.stinger"), "a show is saved as Stinger\\<name>\\<name>.stinger");
    check (sc.saveShow (showFile, error) && showFile.existsAsFile(), "saving makes the show's folder");
    const auto moved = home.getChildFile ("My Show/Audio/Walk up.wav");
    check (show.cue (0).file == moved && show.cue (1).file == moved && moved.existsAsFile() && ! unsaved.exists(),
           "first save: the show's audio moves into its own Audio folder");
    check (sc.getAudioFolder() == home.getChildFile ("My Show/Audio"), "recordings and downloads go to the show's Audio folder");

    check (sc.saveShow (sc.showFileFor ("Copy"), error) && show.cue (0).file == moved, "Save as: the copy uses the first show's audio");
    check (sc.getAudioFolder() == home.getChildFile ("Copy/Audio"), "and new audio goes to the copy's own folder");
    check (sc.showFileFor ("a/b:c") .getParentDirectory().getParentDirectory() == home, "a name with odd characters still makes one folder");
    sc.newShow();
    home.deleteRecursively();
    source.getParentDirectory().deleteRecursively();
}

static void testMovingButtons()
{
    std::puts ("Moving buttons, several files at once");
    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    ShowController sc (e);
    auto& show = sc.getShow();
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory);
    sc.setHomeFolderForTest (dir.getChildFile ("Stinger moving test"));

    show.cue (0) = monoOut (0);
    show.cue (0).name = "Intro";
    show.cue (0).hotkey = "F5";
    show.cue (5) = monoOut (1);
    show.cue (5).name = "Outro";
    sc.addToPlaylist (0, 0);
    sc.swapCues (0, 5);
    check (show.cue (5).name == "Intro" && show.cue (5).hotkey == "F5" && show.cue (0).name == "Outro", "swap: the buttons change places, keys go with the song");
    check (sc.currentPlaylist()->entries[0].cueId == 5, "the playlist follows the song to its new place");
    sc.swapCues (5, 7);
    check (show.cue (7).name == "Intro" && show.cue (5).isEmpty(), "onto an empty button: it moves");

    // several files: name order (2 before 10), empty buttons only, then the next bank
    show.cue (2) = monoOut (0);
    show.cue (2).name = "Keep me";
    for (int s = 8; s < Show::cuesPerBank; ++s)
        show.cue (s) = monoOut (0);
    juce::Array<juce::File> files;
    for (auto name : { "Song 10.wav", "Song 2.wav", "Song 1.wav", "Song 3.wav", "Song 4.wav", "Song 5.wav", "Song 6.wav" })
        files.add (dir.getChildFile (name));
    const int placed = sc.assignFiles (files, 1);
    check (placed == 7, "every file found a button");
    check (show.cue (1).name == "Song 1" && show.cue (3).name == "Song 2" && show.cue (2).name == "Keep me",
           "in name order, skipping the button in use");
    check (show.cue (6).name == "Song 5" && show.cue (24).name == "Song 6" && show.cue (25).name == "Song 10",
           "full bank: on into the next bank, 10 after 6");

    // split: a section of a song onto the next empty button, sharing the loaded audio
    show.cue (30) = monoOut (0);
    show.cue (30).name = "Anthem";
    show.cue (30).hotkey = "F9";
    show.cue (30).cuts = { { 1.0, 2.0 }, { 5.0, 6.0 } };
    sc.setAudioForTest (30, constant (1, 10.0, 0.5f));
    const int part = sc.splitToNewButton (30, { 4.0, 8.0 }, "Anthem chorus");
    check (part == 31 && show.cue (31).trimIn == 4.0 && show.cue (31).trimOut == 8.0 && show.cue (31).cuts.size() == 1,
           "split: the section on the next empty button, with the cut inside it");
    check (show.cue (31).hotkey.isEmpty() && show.cue (31).file == show.cue (30).file && sc.getAudio (31) == sc.getAudio (30),
           "same song (shared audio), no key");

    // undo of a recording: the button gets its old song back, its other settings stay
    const auto before = show.cue (30);
    show.cue (30).file = dir.getChildFile ("Take 1.wav");
    show.cue (30).name = "Take";
    show.cue (30).trimIn = 0.0;
    sc.restoreCue (30, before);
    check (show.cue (30).file == before.file && show.cue (30).name == "Anthem" && show.cue (30).trimIn == before.trimIn
           && show.cue (30).cuts.size() == 2 && show.cue (30).hotkey == "F9", "restore a button: its old song and edits come back");
    sc.restoreCue (30, Cue());
    check (show.cue (30).isEmpty() && sc.getAudio (30) == nullptr, "restore to an empty button");
}

static void testPlaylists()
{
    std::puts ("Playlists");
    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    Outputs out (2);
    ShowController sc (e);
    auto& show = sc.getShow();
    const float levels[] = { 0.1f, 0.2f, 0.3f, 0.4f };
    for (int i = 0; i < 4; ++i)
    {
        show.cue (i) = monoOut (0);
        show.cue (i).name = "Song " + juce::String (i + 1);
        sc.setAudioForTest (i, constant (1, 0.5, levels[i]));
        sc.addToPlaylist (0, i);
    }
    {
        const auto& en = show.playlists[0].entries;
        check (en[0].transition == Transition::playNext && en[2].transition == Transition::playNext
               && en[3].transition == Transition::stop, "new songs play next, the last one stops");
        sc.addToPlaylist (0, 0, 1);
        check (show.playlists[0].entries[1].transition == Transition::playNext && show.playlists[0].entries.back().transition == Transition::stop,
               "a song put in the middle plays next too");
        sc.removeEntry (0, 1);
    }
    sc.setTransition (0, 0, Transition::playNext, 0.0);
    sc.setTransition (0, 1, Transition::crossfade, 0.1);
    sc.setTransition (0, 2, Transition::stop, 0.0);   // 3 -> 4: stop

    auto tick = [&] (int blocks) { for (int b = 0; b < blocks; ++b) { out.run (e); sc.updatePlaylist(); } };
    check (sc.getNextEntry (0) == 0 && sc.getOnAirEntry (0) == -1, "next is the top before anything plays");
    sc.go (0);
    tick (1);
    check (sc.getOnAirEntry (0) == 0 && sc.getNextEntry (0) == 1 && near (out.at (0), 0.1f), "GO plays song 1, song 2 is next");
    tick (50);   // 0.5 s: song 1 ended, song 2 followed
    check (sc.getOnAirEntry (0) == 1 && sc.getNextEntry (0) == 2 && near (out.at (0), 0.2f), "Play next: song 2 followed by itself");
    tick (40);   // 0.9 s: in the crossfade into song 3 (starts at 0.9 s)
    tick (5);
    check (e.getVoices().size() == 2, "crossfade: songs 2 and 3 together");
    tick (10);
    check (sc.getOnAirEntry (0) == 2 && sc.getNextEntry (0) == 3 && near (out.at (0), 0.3f, 0.01f), "song 3 on air after the crossfade");
    tick (60);
    check (e.getVoices().empty() && sc.getNextEntry (0) == 3, "Stop: the playlist stops after song 3, song 4 waits for GO");
    sc.go (0);
    tick (5);
    check (sc.getOnAirEntry (0) == 3 && near (out.at (0), 0.4f), "GO plays song 4");
    tick (60);

    // change a link while the song plays
    sc.setNextEntry (0, 0);
    sc.go (0);
    tick (5);
    sc.setTransition (0, 0, Transition::stop, 0.0);
    tick (60);
    check (e.getVoices().empty(), "changing Play next to Stop while playing takes effect");

    // GO while a playlist song plays: it fades out, the next one starts
    sc.setNextEntry (0, 1);
    sc.go (0);
    tick (5);
    sc.go (0);
    tick (1);
    check (sc.getOnAirEntry (0) == 2 && e.getVoices().size() == 2 && e.getVoices().back().stopping, "GO moves on: the old song fades out");
    e.stopAll (false);
    tick (3);

    // Loop until GO: loops; GO lets it finish the pass, then the next song follows with no gap
    sc.setTransition (0, 0, Transition::loopUntil, 0.2);
    sc.setTransition (0, 1, Transition::stop, 0.0);
    sc.setNextEntry (0, 0);
    sc.go (0);
    tick (130);   // 1.3 s: well past the 0.5 s song
    check (sc.getOnAirEntry (0) == 0 && sc.isLoopingUntilGo (0) && near (out.at (0), 0.1f), "Loop until GO keeps looping");
    sc.go (0);      // 1.3 s into the song: the pass ends at 1.5 s
    tick (15);
    check (sc.getOnAirEntry (0) == 0 && ! sc.isLoopingUntilGo (0) && near (out.at (0), 0.1f), "GO: the loop finishes its pass first");
    tick (10);
    check (sc.getOnAirEntry (0) == 1 && e.getVoices().size() == 1 && near (out.at (0), 0.2f), "then the next song, no gap");
    e.stopAll (false);
    tick (3);

    // Fade to next: crossfade now, over the looping song's fade time
    sc.setNextEntry (0, 0);
    sc.go (0);
    tick (70);
    sc.fadeToNext (0);
    tick (10);   // 0.1 s into a 0.2 s fade
    check (e.getVoices().size() == 2 && near (out.at (0), 0.5f * 0.1f + 0.5f * 0.2f, 0.01f), "Fade to next: half way, both at half");
    tick (12);
    check (sc.getOnAirEntry (0) == 1 && e.getVoices().size() == 1 && near (out.at (0), 0.2f, 0.005f), "Fade to next: the next song on its own");
    e.stopAll (false);
    tick (3);

    // changing Loop until GO to something else while it loops lets it end
    sc.setNextEntry (0, 0);
    sc.go (0);
    tick (70);
    sc.setTransition (0, 0, Transition::stop, 0.0);
    tick (40);
    check (e.getVoices().empty(), "a loop changed to Stop plays to its end and stops");
    sc.setTransition (0, 0, Transition::playNext, 0.0);
    sc.setTransition (0, 1, Transition::crossfade, 0.1);

    // reorder, remove, save and load
    sc.moveEntry (0, 3, 0);
    check (show.playlists[0].entries[0].cueId == 3, "move an entry");
    sc.removeEntry (0, 0);
    check (show.playlists[0].entries.size() == 3, "remove an entry");
    Show t;
    t.fromVar (juce::JSON::parse (juce::JSON::toString (show.toVar())));
    check (t.playlists.size() == 1 && t.playlists[0].entries.size() == 3
           && t.playlists[0].entries[1].transition == Transition::crossfade
           && near ((float) t.playlists[0].entries[1].crossfadeSeconds, 0.1f), "playlists and transitions saved in the show");

    // two playlists: each has its own Next / On air and GO
    e.stopAll (false);
    tick (3);
    show.playbackMode = PlaybackMode::overlap;
    const int b = sc.addPlaylist ("Second");
    check (b == 1 && sc.getSelectedPlaylist() == 1, "a new playlist is selected");
    sc.addToPlaylist (b, 1);
    sc.addToPlaylist (b, 2);
    sc.setNextEntry (0, 0);
    sc.go (0);
    tick (2);
    sc.go (b);
    tick (2);
    check (sc.getOnAirEntry (0) == 0 && sc.getNextEntry (0) == 1 && sc.getOnAirEntry (b) == 0 && sc.getNextEntry (b) == 1,
           "GO in each playlist moves only its own Next");
    check (e.getVoices().size() == 2, "overlap: both playlists play");
    sc.go (b);
    tick (2);
    int fading = 0;
    for (auto& v : e.getVoices())
        fading += v.stopping ? 1 : 0;
    check (sc.getOnAirEntry (b) == 1 && sc.getOnAirEntry (0) == 0 && fading == 1, "GO again fades only that playlist's song");
    e.stopAll (false);
    tick (150);
    check (sc.getOnAirEntry (0) == -1 && sc.getOnAirEntry (b) == -1, "nothing on air once everything stops");

    show.playbackMode = PlaybackMode::oneAtATime;
    sc.setNextEntry (0, 0);
    sc.go (0);
    tick (2);
    sc.setNextEntry (b, 0);
    sc.go (b);
    tick (10);
    check (e.getVoices().size() == 1 && sc.getOnAirEntry (b) == 0 && sc.getOnAirEntry (0) == -1,
           "one at a time: another playlist's GO takes over");
    e.stopAll (false);
    tick (3);

    sc.selectPlaylist (0);
    sc.removePlaylist (0);
    check (show.playlists.size() == 1 && show.playlists[0].name == "Second" && sc.getSelectedPlaylist() == 0
           && sc.getNextEntry (0) >= 0, "delete a playlist: the others keep their songs and state");
}

static void testControls()
{
    std::puts ("Keys and MIDI");
    CommandMap map;
    check (map.commandForKey (juce::KeyPress (juce::KeyPress::spaceKey)) == "go"
           && map.commandForKey (juce::KeyPress (juce::KeyPress::escapeKey)) == "stopAll"
           && map.commandForKey (juce::KeyPress ('q')) == "slot7"
           && map.commandForKey (juce::KeyPress ('2', juce::ModifierKeys::commandModifier, 0)) == "bank2",
           "default keys: Space GO, Esc stop all, Q button 7, Ctrl+2 bank B");

    auto note = MidiTrigger::fromMessage (juce::MidiMessage::noteOn (3, 60, (juce::uint8) 100));
    check (note.type == MidiTrigger::note && note.channel == 3 && note.number == 60, "learns a note");
    check (MidiTrigger::fromMessage (juce::MidiMessage::noteOff (3, 60)).type == MidiTrigger::none, "a note-off isn't learnt");
    check (note.fires (juce::MidiMessage::noteOn (3, 60, (juce::uint8) 1)) && ! note.fires (juce::MidiMessage::noteOn (4, 60, (juce::uint8) 100)),
           "fires on that note and channel only");
    MidiTrigger anyChannel = note;
    anyChannel.channel = 0;
    check (anyChannel.fires (juce::MidiMessage::noteOn (9, 60, (juce::uint8) 100)), "channel 0 = any channel");

    // a footswitch: CC 64 127 on press (repeated while held), 0 on release
    auto pedal = MidiTrigger::fromMessage (juce::MidiMessage::controllerEvent (1, 64, 127));
    check (pedal.type == MidiTrigger::controller && pedal.number == 64, "learns a footswitch (CC)");
    check (MidiTrigger::fromMessage (juce::MidiMessage::controllerEvent (1, 64, 0)).type == MidiTrigger::none, "the release isn't learnt");
    ControllerEdges edges;
    const bool first = edges.isPress (juce::MidiMessage::controllerEvent (1, 64, 127));
    const bool held = edges.isPress (juce::MidiMessage::controllerEvent (1, 64, 127));
    const bool up = edges.isPress (juce::MidiMessage::controllerEvent (1, 64, 0));
    const bool again = edges.isPress (juce::MidiMessage::controllerEvent (1, 64, 127));
    check (first && ! held && ! up && again, "a held footswitch fires once per press");

    Binding b;
    b.key = juce::KeyPress (juce::KeyPress::F5Key).getTextDescription();
    b.midi = pedal;
    map.bindings["go"] = b;
    CommandMap loaded;
    loaded.fromVar (juce::JSON::parse (juce::JSON::toString (map.toVar())));
    check (loaded.commandForKey (juce::KeyPress (juce::KeyPress::F5Key)) == "go"
           && loaded.commandForMidi (juce::MidiMessage::controllerEvent (1, 64, 127)) == "go"
           && loaded.commandForKey (juce::KeyPress (juce::KeyPress::spaceKey)).isEmpty(),
           "reassigned keys and MIDI are saved and read back");

    Cue c;
    c.midi = note;
    check (Cue::fromVar (juce::JSON::parse (juce::JSON::toString (c.toVar()))).midi == note, "a button's MIDI is saved in the show");
}

static void testStreamDeck()
{
    std::puts ("Stream Deck connection");
    check (StreamDeckServer::acceptKey ("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "WebSocket handshake key (RFC 6455 example)");

    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    ShowController sc (e);
    sc.getShow().cue (5) = monoOut (0);
    sc.getShow().cue (5).name = "Winner swell";
    sc.setAudioForTest (5, constant (1, 1.0, 0.5f));
    StreamDeckServer server (sc, 52899);   // not the real port: a running Stinger may have it
    juce::String pressed;
    server.perform = [&pressed] (const juce::String& a) { pressed = a; };
    for (int i = 0; i < 50 && ! server.isListening(); ++i)
        juce::Thread::sleep (20);
    check (server.isListening(), "listening on 127.0.0.1");

    juce::StreamingSocket client;
    if (! client.connect ("127.0.0.1", 52899, 2000))
    {
        check (false, "plugin connects");
        return;
    }
    const juce::String request = "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                 "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
    client.write (request.toRawUTF8(), (int) request.getNumBytesAsUTF8());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);   // the server's timer sends the state

    juce::MemoryBlock got;
    char buffer[65536];
    while (client.waitUntilReady (true, 300) > 0)
    {
        const int n = client.read (buffer, sizeof (buffer), false);
        if (n <= 0)
            break;
        got.append (buffer, (size_t) n);
    }
    const std::string raw (static_cast<const char*> (got.getData()), got.getSize());   // frames have binary headers
    const juce::String text (raw.substr (0, raw.find ("\r\n\r\n") + 4));
    check (text.startsWith ("HTTP/1.1 101") && text.contains ("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="), "handshake answered");
    check (raw.find ("\"state\"") != std::string::npos && raw.find ("Winner swell") != std::string::npos, "sends the buttons' state");

    // a masked text frame, as a browser sends it
    const juce::String press = "{\"type\":\"press\",\"action\":\"go\"}";
    juce::MemoryBlock frame;
    const juce::uint8 mask[4] = { 1, 2, 3, 4 };
    const juce::uint8 head[2] = { 0x81, (juce::uint8) (0x80 | press.length()) };
    frame.append (head, 2);
    frame.append (mask, 4);
    for (int i = 0; i < press.length(); ++i)
    {
        const auto b = (juce::uint8) ((juce::uint8) press[i] ^ mask[i % 4]);
        frame.append (&b, 1);
    }
    client.write (frame.getData(), (int) frame.getSize());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    check (pressed == "go", "a key press on the Stream Deck reaches Stinger");
    check (server.numClients() == 1, "one plugin connected");

    check (! server.stateJson().contains ("\"now\""), "countdown: nothing playing");
    sc.play (5);
    Outputs out (2);
    out.run (e, 25);   // 0.25 s into the 1 s sting
    const auto now = juce::JSON::parse (server.stateJson()).getProperty ("now", {});
    check (now.getProperty ("name", {}).toString() == "Winner swell" && std::abs ((double) now.getProperty ("remaining", 0.0) - 0.8) < 0.051,
           "countdown: the sting just started, 0.8 s left (tenths in the last 10 s)");
    client.close();
}

/** Sends an HTTP request, returns everything the server answered until it closed (or went quiet). */
static std::string httpExchange (int port, const juce::String& request)
{
    juce::StreamingSocket client;
    if (! client.connect ("127.0.0.1", port, 2000))
        return {};
    client.write (request.toRawUTF8(), (int) request.getNumBytesAsUTF8());
    juce::MemoryBlock got;
    std::vector<char> buffer (65536);
    for (int quiet = 0; quiet < 10;)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        if (client.waitUntilReady (true, 30) <= 0)
        {
            ++quiet;
            continue;
        }
        const int n = client.read (buffer.data(), (int) buffer.size(), false);
        if (n <= 0)
            break;
        got.append (buffer.data(), (size_t) n);
    }
    return std::string (static_cast<const char*> (got.getData()), got.getSize());
}

/** The Wi-Fi remote's server: the key in the link, the page, the WebSocket, layout messages. */
static void testRemoteServer()
{
    std::puts ("Wi-Fi remote server");
    PlaybackEngine e;
    e.prepareForTest (48000.0, 512);
    ShowController sc (e);
    sc.getShow().cue (5).name = "Winner swell";
    sc.getShow().cue (5).file = juce::File::getCurrentWorkingDirectory().getChildFile ("winner.wav");
    sc.setAudioForTest (5, constant (1, 1.0, 0.5f));

    StreamDeckServer::Options options;
    options.address = "127.0.0.1";   // (the real remote: every adapter; here no firewall prompt)
    options.key = "secret123";
    options.page = "<html>remote page</html>";
    StreamDeckServer server (sc, 52898, options);
    juce::var layoutMessage;
    juce::String pressed;
    server.perform = [&pressed] (const juce::String& a) { pressed = a; };
    server.onMessage = [&layoutMessage] (const juce::var& m) { layoutMessage = m; };
    server.addToState = [] (juce::DynamicObject& state) { state.setProperty ("layouts", "phone keys"); };
    for (int i = 0; i < 50 && ! server.isListening(); ++i)
        juce::Thread::sleep (20);
    check (server.isListening(), "remote listening");

    const auto page = httpExchange (52898, "GET /?k=secret123 HTTP/1.1\r\nHost: x\r\n\r\n");
    check (page.rfind ("HTTP/1.1 200", 0) == 0 && page.find ("remote page") != std::string::npos, "the page is served with the right key");
    const auto wrong = httpExchange (52898, "GET /?k=old HTTP/1.1\r\nHost: x\r\n\r\n");
    check (wrong.rfind ("HTTP/1.1 403", 0) == 0 && wrong.find ("remote page") == std::string::npos, "a wrong key gets nothing");
    const auto none = httpExchange (52898, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    check (none.rfind ("HTTP/1.1 403", 0) == 0, "no key gets nothing");
    const auto socketNoKey = httpExchange (52898, "GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                                  "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
    check (socketNoKey.rfind ("HTTP/1.1 403", 0) == 0, "no WebSocket without the key");

    juce::StreamingSocket client;
    if (! client.connect ("127.0.0.1", 52898, 2000))
    {
        check (false, "phone connects");
        return;
    }
    const juce::String request = "GET /ws?k=secret123 HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                 "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
    client.write (request.toRawUTF8(), (int) request.getNumBytesAsUTF8());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
    juce::MemoryBlock got;
    std::vector<char> buffer (65536);
    while (client.waitUntilReady (true, 300) > 0)
    {
        const int n = client.read (buffer.data(), (int) buffer.size(), false);
        if (n <= 0)
            break;
        got.append (buffer.data(), (size_t) n);
    }
    const std::string raw (static_cast<const char*> (got.getData()), got.getSize());
    check (raw.rfind ("HTTP/1.1 101", 0) == 0 && raw.find ("Winner swell") != std::string::npos, "WebSocket with the key gets the state");
    check (raw.find ("phone keys") != std::string::npos, "the state carries the layouts");

    for (const juce::String text : { juce::String ("{\"type\":\"layout\",\"device\":\"phone\",\"keys\":[]}"), juce::String ("{\"type\":\"press\",\"action\":\"stopAll\"}") })
    {
        juce::MemoryBlock frame;
        const juce::uint8 mask[4] = { 9, 8, 7, 6 };
        const juce::uint8 head[2] = { 0x81, (juce::uint8) (0x80 | text.length()) };
        frame.append (head, 2);
        frame.append (mask, 4);
        for (int i = 0; i < text.length(); ++i)
        {
            const auto b = (juce::uint8) ((juce::uint8) text[i] ^ mask[i % 4]);
            frame.append (&b, 1);
        }
        client.write (frame.getData(), (int) frame.getSize());
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    check (layoutMessage.getProperty ("device", {}).toString() == "phone", "a layout from the phone reaches Stinger");
    check (pressed == "stopAll", "a key press on the phone reaches Stinger");
    client.close();
}

namespace
{
    /** Stand-ins for VST3 effects. */
    struct TestPlugin : juce::AudioProcessor
    {
        TestPlugin() : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                                       .withOutput ("Out", juce::AudioChannelSet::stereo())) {}
        const juce::String getName() const override { return "Test"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock& m) override { m.append ("abc", 3); }
        void setStateInformation (const void*, int size) override { restored = size; }
        int restored = 0;
    };

    struct Doubler : TestPlugin
    {
        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override { b.applyGain (2.0f); }
    };

    /** 0.5 s delay, wet only: what comes out after the sting stops is the "tail". */
    struct Delay : TestPlugin
    {
        std::vector<float> line = std::vector<float> ((size_t) (rate / 2), 0.0f);
        size_t pos = 0;
        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float in = b.getSample (0, i);
                const float out = line[pos];
                line[pos] = in;
                pos = (pos + 1) % line.size();
                b.setSample (0, i, out);
                b.setSample (1, i, out);
            }
        }
    };
}

static void testPlugins()
{
    std::puts ("Plugins on a button");
    PlaybackEngine e;
    e.prepareForTest (rate, 4);
    Outputs out (4);
    ShowController sc (e);
    auto& show = sc.getShow();
    show.cue (0) = monoOut (2);
    sc.setAudioForTest (0, constant (1, 1.0, 0.25f));
    sc.setPluginForTest (0, 1, std::make_unique<Doubler>());
    check (sc.getFx (0) != nullptr, "the button has plugins");

    sc.play (0);
    out.run (e, 5);
    check (near (out.at (2), 0.5f), "plays through the plugin (x2) to its own output");
    check (near (out.at (0), 0.0f) && near (out.at (1), 0.0f), "nothing elsewhere");
    sc.setPluginBypassed (0, 1, true);
    out.run (e);
    check (near (out.at (2), 0.25f), "bypassed: straight through");
    sc.setPluginBypassed (0, 1, false);
    e.stopAll (false);
    out.run (e, 30);

    // a delay: the echo comes after the sting has stopped
    show.cue (1) = monoOut (3);
    sc.setAudioForTest (1, constant (1, 0.1, 0.5f));
    sc.setPluginForTest (1, 0, std::make_unique<Delay>());
    sc.play (1);
    out.run (e, 20);   // 0.2 s: the sting (0.1 s) is over, the echo not here yet
    check (! e.isCuePlaying (1) && near (out.at (3), 0.0f), "sting over, echo still to come");
    check (e.isUsingFx (sc.getFx (1)), "its plugins keep running for the tail");
    out.run (e, 35);   // 0.55 s: inside the echo (0.5 .. 0.6 s)
    check (near (out.at (3), 0.5f), "the echo rings out after the sting stopped");
    out.run (e, 60);   // 1.15 s: echo over
    check (e.isUsingFx (sc.getFx (1)), "plugins run on for at least 2 s (an echo can come after a silence)");
    out.run (e, 150);  // 2.65 s: past the 2 s and quiet
    check (! e.isUsingFx (sc.getFx (1)), "and then the plugins stop running");

    // Stop all silences tails too
    sc.play (1);
    out.run (e, 20);
    e.stopAll (false);
    out.run (e, 35);
    check (near (out.at (3), 0.0f), "Stop all: no echo afterwards");

    // saved with the show
    sc.storePluginStates();
    show.cue (0).plugins[1].description = "<PLUGIN name=\"Test\"/>";
    auto loaded = Cue::fromVar (juce::JSON::parse (juce::JSON::toString (show.cue (0).toVar())));
    check (loaded.plugins[1].state == juce::MemoryBlock ("abc", 3).toBase64Encoding() && ! loaded.plugins[0].isEmpty() == false
           && loaded.plugins[1].description.contains ("Test"), "plugin and its settings are saved in the show");
    sc.removePlugin (0, 1);
    check (sc.getFx (0) == nullptr && show.cue (0).plugins[1].isEmpty(), "remove empties the slot");

    // put in / taken out while the button plays: heard straight away
    show.cue (2) = monoOut (0);
    sc.setAudioForTest (2, constant (1, 2.0, 0.25f));
    sc.play (2);
    out.run (e, 5);
    check (near (out.at (0), 0.25f), "playing with no plugins");
    sc.setPluginForTest (2, 0, std::make_unique<Doubler>());
    out.run (e);
    check (near (out.at (0), 0.5f), "a plugin put in while it plays is heard at once");
    sc.removePlugin (2, 0);
    out.run (e);
    check (near (out.at (0), 0.25f), "and taken out at once");
    e.stopAll (false);
    out.run (e, 30);
}

static void testRecording()
{
    std::puts ("Recording into a button");
    PlaybackEngine e;
    e.prepareForTest (rate, 2, 4);
    Outputs out (2);
    std::vector<std::vector<float>> in (4, std::vector<float> (block, 0.0f));
    std::vector<const float*> inPtrs;
    for (auto& v : in)
        inPtrs.push_back (v.data());
    juce::int64 t = 0;
    auto run = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i, ++t)
            {
                in[2][(size_t) i] = 0.5f * std::sin ((float) t * 0.01f);   // input 3
                in[3][(size_t) i] = -0.25f;                                // input 4
            }
            e.audioDeviceIOCallbackWithContext (inPtrs.data(), 4, out.ptrs.data(), 2, block, {});
        }
    };

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("stinger-record-test.wav");
    juce::String error;
    check (e.startRecording (file, { 2, true }, error) && e.isRecording(), "starts recording inputs 3-4");
    run (50);   // 0.5 s
    check (std::abs (e.getRecordedSeconds() - 0.5) < 0.001, "counts the time");
    check (e.takeInputPeak (2) > 0.4f && e.takeInputPeak (0) == 0.0f, "input meters");
    check (e.stopRecording() == file && ! e.isRecording(), "stops and hands back the file");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader != nullptr && reader->numChannels == 2 && reader->lengthInSamples == 24000 && reader->bitsPerSample == 24
           && reader->sampleRate == rate, "a 24-bit stereo WAV, 0.5 s at the device rate");
    if (reader != nullptr)
    {
        juce::AudioBuffer<float> b (2, 24000);
        reader->read (&b, 0, 24000, 0, true, true);
        check (near (b.getSample (0, 1000), 0.5f * std::sin (1000.0f * 0.01f), 1.0e-4f) && near (b.getSample (1, 1000), -0.25f, 1.0e-4f),
               "what came in is what's in the file");
    }
    reader = nullptr;

    check (e.startRecording (file, { 1, false }, error), "mono: one input");
    run (10);
    e.stopRecording();
    reader.reset (formats.createReaderFor (file));
    check (reader != nullptr && reader->numChannels == 1 && reader->lengthInSamples == 4800, "a mono WAV");
    reader = nullptr;
    file.deleteFile();
}

static void testShowFile()
{
    std::puts ("Show file");
    Show s;
    auto& c = s.cue (Show::cueId (1, 5));
    c.name = "Winner swell";
    c.file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("swell.wav");
    c.trimIn = 1.5;
    c.cuts = { { 3.0, 4.0 } };
    c.levelDb = 12.0f;
    c.pan = -0.5f;
    c.output = { 6, false };
    c.fadeIn = { 0.7, FadeCurve::exponential };
    c.hotkey = "F5";
    Show t;
    check (t.fromVar (juce::JSON::parse (juce::JSON::toString (s.toVar()))), "reads back");
    const auto& d = t.cue (Show::cueId (1, 5));
    check (d.name == c.name && d.file == c.file && d.trimIn == 1.5 && d.cuts.size() == 1 && d.levelDb == 12.0f
           && d.pan == -0.5f && d.output == c.output && d.fadeIn.curve == FadeCurve::exponential && d.hotkey == "F5",
           "every setting survives");
    check (t.banks.size() == 4 && t.banks[1].name == "Awards", "banks and names");
    check (! t.fromVar (juce::var ("nonsense")), "refuses other files");
}

static void testFileLoading()
{
    std::puts ("Loading files");
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("stinger-test-44k.wav");
    {
        juce::WavAudioFormat wav;
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), 44100.0, 1, 24, {}, 0));
        if (writer != nullptr)
            stream.release();
        juce::AudioBuffer<float> b (1, 44100);
        for (int i = 0; i < 44100; ++i)
            b.setSample (0, i, 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 441.0f * (float) i / 44100.0f));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    juce::String error;
    auto audio = CueAudio::load (file, 48000.0, formats, error);
    check (audio != nullptr, "reads a 44.1 kHz mono WAV");
    if (audio != nullptr)
    {
        check (audio->isMono() && std::abs (audio->samples.getNumSamples() - 48000) <= 2, "resampled to 48 kHz, still 1 s, still mono");
        const auto range = audio->samples.findMinMax (0, 1000, 40000);
        check (range.getEnd() > 0.48f && range.getEnd() < 0.52f, "level kept");
        check (! audio->peakMax.empty() && audio->peakMax[50] > 0.4f && audio->peakMin[50] < -0.4f, "waveform overview made (both halves)");
    }
    if (audio != nullptr)
    {
        // 441 Hz at 48 kHz: zero crossings every 1/882 s; 0.0105 s is between two of them
        const double z = audio->nearestZeroCrossing (0.0105);
        const int i = (int) std::llround (z * 48000.0);
        const float a = audio->samples.getSample (0, i - 1), b = audio->samples.getSample (0, i);
        check (std::abs (z - 0.0105) < 0.003 && (a < 0.0f) != (b < 0.0f), "edits snap to the nearest zero crossing");
    }
    {
        // a WAV written as a stream (ffmpeg to a pipe): RIFF and data sizes 0xFFFFFFFF
        juce::MemoryBlock bytes;
        file.loadFileAsData (bytes);
        auto* p = static_cast<char*> (bytes.getData());
        std::memset (p + 4, 0xff, 4);
        for (size_t i = 12; i + 8 <= bytes.getSize(); ++i)
            if (std::memcmp (p + i, "data", 4) == 0)
            {
                std::memset (p + i + 4, 0xff, 4);
                break;
            }
        file.replaceWithData (bytes.getData(), bytes.getSize());
        auto streamed = CueAudio::load (file, 0.0, formats, error);
        check (streamed != nullptr && streamed->samples.getNumSamples() == 44100, "a streamed WAV (sizes 0xFFFFFFFF) is as long as its data");
    }
    file.deleteFile();
    check (CueAudio::load (file, 48000.0, formats, error) == nullptr && error.isNotEmpty(), "missing file reports an error");
}

/** EngineTests --plugin <file.vst3>: a real plugin on a button, played through the engine. */
static int testRealPlugin (const juce::String& path)
{
    std::printf ("Real plugin: %s\n", path.toRawUTF8());
    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> found;
    formats.getFormat (0)->findAllTypesForFile (found, path);
    check (! found.isEmpty(), "found in the file");
    if (found.isEmpty())
        return 1;
    juce::String error;
    auto plugin = formats.createPluginInstance (*found[0], rate, block, error);
    check (plugin != nullptr, ("created: " + found[0]->name + " " + error).toRawUTF8());
    if (plugin == nullptr)
        return 1;
    std::printf ("    %d in / %d out, tail %.1f s\n", plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels(),
                 plugin->getTailLengthSeconds());

    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    Outputs out (2);
    ShowController sc (e);
    Cue c;
    c.file = juce::File::getCurrentWorkingDirectory().getChildFile ("click.wav");
    c.output = { 0, true };
    sc.getShow().cue (0) = c;
    sc.setAudioForTest (0, makeAudio (2, 0.05, [] (int, int i) { return i < 200 ? 0.8f : 0.0f; }));   // a click
    sc.setPluginForTest (0, 0, std::move (plugin));
    sc.play (0);
    float during = 0.0f, after = 0.0f;
    bool finite = true;
    for (int b = 0; b < 200; ++b)   // 2 s
    {
        out.run (e);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float v = out.at (ch, i);
                finite = finite && std::isfinite (v);
                auto& peak = b < 10 ? during : after;
                peak = std::max (peak, std::abs (v));
            }
    }
    check (finite, "output is clean numbers");
    check (during > 0.0f, "sound comes through the plugin");
    std::printf ("    peak in the first 0.1 s %.3f, after the click (tail) %.4f\n", during, after);
    check (! e.isCuePlaying (0), "the sting itself is over");
    return failures == 0 ? 0 : 1;
}

static void testLinks()
{
    std::puts ("Links");
    check (Downloader::directLink ("https://www.dropbox.com/scl/fi/abc/Song.mp3?rlkey=xyz&dl=0")
               == "https://www.dropbox.com/scl/fi/abc/Song.mp3?rlkey=xyz&dl=1", "Dropbox share link -> dl=1 (the file, not the page)");
    check (Downloader::directLink ("https://www.dropbox.com/s/abc/Song.wav") == "https://www.dropbox.com/s/abc/Song.wav?dl=1",
           "Dropbox link without dl= gets one");
    check (Downloader::directLink ("https://1drv.ms/u/s!AbCd").startsWith ("https://api.onedrive.com/v1.0/shares/u!aHR0cHM6Ly8xZHJ2Lm1zL3UvcyFBYkNk"),
           "OneDrive share link -> the shares API (base64url of the link)");
    check (Downloader::directLink ("https://www.youtube.com/watch?v=jNQXAC9IVRw").isEmpty(), "YouTube goes to yt-dlp");
    check (Downloader::directLink ("https://example.com/files/walkup.mp3") == "https://example.com/files/walkup.mp3", "a link to an audio file is fetched as it is");
    const auto links = Downloader::linksIn ("https://youtu.be/a1\nhttps://youtu.be/b2 , https://youtu.be/a1\nnot a link");
    check (links.size() == 2 && links[0] == "https://youtu.be/a1" && links[1] == "https://youtu.be/b2", "links picked out of pasted text, no repeats");
}

/** EngineTests --download <url>: the real thing (yt-dlp / ffmpeg from tools\bin) into the temp folder. */
static int testDownload (const juce::String& url)
{
    Downloader d;
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Stinger download test");
    bool finished = false;
    Downloader::Job result;
    d.add (url, folder, 48000.0, [&] (const Downloader::Job& j) { result = j; finished = true; });
    juce::String last;
    for (int i = 0; i < 6000 && ! finished; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        auto jobs = d.getJobs();
        if (! jobs.empty() && jobs.front().status != last)
            std::printf ("  %s\n", (last = jobs.front().status).toRawUTF8());
    }
    std::printf ("  title: %s\n  file: %s\n", result.title.toRawUTF8(), result.result.getFullPathName().toRawUTF8());
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (result.result));
    check (result.state == Downloader::State::done && reader != nullptr, "downloaded and readable");
    if (reader != nullptr)
    {
        std::printf ("  %d ch, %.0f Hz, %d bit, %.1f s\n", (int) reader->numChannels, reader->sampleRate, (int) reader->bitsPerSample,
                     reader->lengthInSamples / reader->sampleRate);
        check (reader->bitsPerSample == 24 && reader->sampleRate == 48000.0, "24-bit WAV at the device rate");
    }
    return failures == 0 ? 0 : 1;
}

/** EngineTests --arm: on each device type's default device, the inputs open only while recording is armed. */
static int testArming()
{
    PlaybackEngine e;
    e.initialise (nullptr);
    auto& dm = e.getDeviceManager();
    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
    for (auto* type : dm.getAvailableDeviceTypes())
    {
        dm.setCurrentAudioDeviceType (type->getTypeName(), true);
        pump (300);
        auto* d = dm.getCurrentAudioDevice();
        if (d == nullptr)
        {
            std::printf ("%s: no device\n", type->getTypeName().toRawUTF8());
            continue;
        }
        const int numIns = e.getInputNames().size();   // what arming can open (the device may have none open now)
        std::printf ("%s: %s, %d inputs\n", type->getTypeName().toRawUTF8(), d->getName().toRawUTF8(), numIns);
        auto show = [&] (const char* what, std::optional<OutputRoute> arm, int expected)
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            e.setArmedInput (arm);
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            pump (200);
            auto* dev = dm.getCurrentAudioDevice();
            const auto active = dev != nullptr ? dev->getActiveInputChannels() : juce::BigInteger();
            const auto outs = dev != nullptr ? dev->getActiveOutputChannels().countNumberOfSetBits() : 0;
            const auto inputDevice = dm.getAudioDeviceSetup().inputDeviceName;
            std::printf ("  %-22s inputs open: %-4s input device: %-30s (%d outs, %4.0f ms)\n", what, active.toString (2).toRawUTF8(),
                         inputDevice.isEmpty() ? "none" : inputDevice.toRawUTF8(), outs, ms);
            const int open = std::min (expected, numIns);
            check (active.countNumberOfSetBits() == open && (open > 0) == inputDevice.isNotEmpty(), what);
        };
        show ("disarmed", std::nullopt, 0);
        show ("armed In 1 (mono)", OutputRoute { 0, false }, 1);
        show ("armed In 1-2 (stereo)", OutputRoute { 0, true }, 2);
        show ("disarmed again", std::nullopt, 0);
    }
    return failures == 0 ? 0 : 1;
}

/** Opens every device of every type the way the Audio window does, timing each step (a hang shows as the
    last line printed). Plays nothing. */
static int testDeviceSwitching (const juce::File& showFile)
{
    PlaybackEngine e;
    e.initialise (nullptr);
    auto& dm = e.getDeviceManager();
    ShowController controller (e);
    if (showFile.existsAsFile())
    {
        juce::String error;
        controller.openShow (showFile, error);
        std::printf ("  show %s %s\n", showFile.getFileName().toRawUTF8(), error.toRawUTF8());
    }
    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
    auto step = [&] (const juce::String& what, std::function<void()> fn)
    {
        std::printf ("  %-70s", what.toRawUTF8());
        std::fflush (stdout);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        fn();
        pump (300);
        auto* d = dm.getCurrentAudioDevice();
        while (controller.getLoader().pending() > 0)
            pump (50);
        juce::int64 bytes = 0;
        for (int id = 0; id < Show::cuesPerBank * (int) controller.getShow().banks.size(); ++id)
            if (auto a = controller.getAudio (id))
                bytes += (juce::int64) a->samples.getNumSamples() * a->numChannels() * 4;
        std::printf (" %6.0f ms  %5d MB -> %s\n", juce::Time::getMillisecondCounterHiRes() - t0 - 300, (int) (bytes >> 20),
                     d != nullptr ? (d->getName() + " " + juce::String (d->getCurrentSampleRate()) + " Hz, "
                                     + juce::String (e.getNumOutputs()) + " outs").toRawUTF8() : "no device");
        std::fflush (stdout);
    };
    for (int round = 0; round < 2; ++round)
        for (auto* type : dm.getAvailableDeviceTypes())
        {
            const auto typeName = type->getTypeName();
            step ("type " + typeName, [&] { dm.setCurrentAudioDeviceType (typeName, true); });
            type->scanForDevices();
            for (auto& name : type->getDeviceNames (false))
                step ("  " + typeName + ": " + name, [&]
                {
                    auto setup = dm.getAudioDeviceSetup();
                    setup.outputDeviceName = name;
                    if (! type->hasSeparateInputsAndOutputs())
                        setup.inputDeviceName = name;
                    const auto err = dm.setAudioDeviceSetup (setup, true);
                    if (err.isNotEmpty())
                        std::printf (" [%s]", err.toRawUTF8());
                });
        }
    e.shutdownAudio();
    std::printf ("device switching done\n");
    return 0;
}

/** EngineTests --remote-serve page.html seconds: the remote page on 127.0.0.1:52897/?k=test with a made-up show (for a browser). */
static int remoteServe (const juce::String& pageFile, int seconds)
{
    PlaybackEngine e;
    e.prepareForTest (48000.0, 512);
    ShowController sc (e);
    const char* names[] = { "Walk-up: Jordan", "Award swell", "Welcome to stage", "Drum roll", "Applause", "Big finish" };
    for (int i = 0; i < 6; ++i)
    {
        sc.getShow().cue (i).name = names[i];
        sc.getShow().cue (i).colour = i;
        sc.getShow().cue (i).file = juce::File::getCurrentWorkingDirectory().getChildFile ("x" + juce::String (i) + ".wav");
        sc.setAudioForTest (i, constant (1, 30.0, 0.5f));
    }
    StreamDeckServer::Options options;
    options.key = "test";
    options.page = juce::File (pageFile).loadFileAsString();
    StreamDeckServer server (sc, 52897, options);
    server.colourOf = [] (const Cue& c) { return c.colour == 0 ? juce::String ("#e5484d") : c.colour == 1 ? juce::String ("#f2a33a") : juce::String ("#3ccf7a"); };
    server.perform = [&sc] (const juce::String& a) { std::printf ("press %s\n", a.toRawUTF8()); std::fflush (stdout); if (a.startsWith ("slot")) sc.play (a.substring (4).getIntValue() - 1); };
    server.onMessage = [] (const juce::var& m) { std::printf ("message %s\n", juce::JSON::toString (m, true).toRawUTF8()); std::fflush (stdout); };
    ScheduleItem item;
    item.cueId = 2;
    item.kind = ScheduleItem::delay;
    item.delaySeconds = 55.0;
    sc.addScheduleItem (item);
    sc.startSchedule();
    sc.addRunRow ({ -1, {}, "Dr. Lee", {} });
    sc.play (1);
    Outputs out (2);
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) (seconds * 1000);
    while (juce::Time::getMillisecondCounter() < end)
    {
        out.run (e, 1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }
    return 0;
}

/** EngineTests --failover: real devices. A silent song plays while the device is closed underneath it, or replaced behind
    Stinger's back as JUCE does when a device vanishes; the watcher has to move to the backup with the song in step. */
static int testFailover()
{
    PlaybackEngine e;
    e.initialise (nullptr);
    auto& manager = e.getDeviceManager();
    auto* device = manager.getCurrentAudioDevice();
    if (device == nullptr) { std::puts ("no audio device"); return 1; }
    std::printf ("main: %s / %s at %.0f\n", device->getTypeName().toRawUTF8(), device->getName().toRawUTF8(), device->getCurrentSampleRate());

    DeviceWatch::Backup backup;
    for (auto* t : manager.getAvailableDeviceTypes())
        if (t->getTypeName() == device->getTypeName())
            for (auto& name : t->getDeviceNames (false))
                if (name != device->getName() && ! name.containsIgnoreCase ("Dante") && ! name.containsIgnoreCase ("SoundGrid"))
                {
                    backup.type = t->getTypeName();
                    backup.name = name;
                }
    if (! backup.isSet()) { std::puts ("no second output on this device type"); return 1; }
    std::printf ("backup: %s / %s\n", backup.type.toRawUTF8(), backup.name.toRawUTF8());

    ShowController sc (e);
    DeviceWatch watch (sc);
    watch.setBackup (backup);
    const double sr = e.getSampleRate();
    sc.getShow().cue (0) = monoOut (0);
    sc.setAudioForTest (0, std::make_shared<CueAudio>());
    auto silent = std::make_shared<CueAudio>();
    silent->sampleRate = silent->fileSampleRate = sr;
    silent->fileChannels = 1;
    silent->samples.setSize (1, (int) (60.0 * sr));
    silent->samples.clear();
    sc.setAudioForTest (0, silent);

    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
    auto position = [&e] { auto v = e.getVoices(); return v.empty() ? -1.0 : v[0].position; };
    sc.play (0);
    pump (800);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto p0 = position();
    check (p0 > 0.3, "the song is running on the main device");

    watch.simulateLossForTest();
    for (int i = 0; i < 100 && watch.getState() != DeviceWatch::State::onBackup; ++i)
        pump (50);
    const auto moved = juce::Time::getMillisecondCounterHiRes();
    std::printf ("  moved after %.0f ms\n", moved - t0);
    check (watch.getState() == DeviceWatch::State::onBackup, "device closed: moved to the backup by itself");
    check (manager.getCurrentAudioDevice() != nullptr && manager.getCurrentAudioDevice()->getName() == backup.name, "the backup is the open device");
    pump (600);
    auto drift = [&] { return position() - p0 - (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0; };
    std::printf ("  song is %.0f ms from where it would have been\n", drift() * 1000.0);
    check (std::abs (drift()) < 0.25, "the song carried on where it would have been");

    juce::String error;
    check (watch.isMainAvailable(), "the main device is still listed");
    check (watch.returnToMain (error), "back to the main device");
    pump (600);
    std::printf ("  song is %.0f ms from where it would have been\n", drift() * 1000.0);
    check (std::abs (drift()) < 0.25, "still in step after going back");

    // JUCE reopens another device by itself when one vanishes: that counts as lost too
    auto setup = manager.getAudioDeviceSetup();
    setup.outputDeviceName = backup.name;
    manager.setAudioDeviceSetup (setup, true);
    for (int i = 0; i < 40 && watch.getState() == DeviceWatch::State::normal; ++i)
        pump (50);
    check (watch.getState() == DeviceWatch::State::onBackup, "another device taking over unasked counts as a loss");
    check (watch.returnToMain (error), "back again");

    watch.setUserChanging (true);
    setup = manager.getAudioDeviceSetup();
    setup.outputDeviceName = backup.name;
    manager.setAudioDeviceSetup (setup, true);
    pump (500);
    watch.setUserChanging (false);
    pump (500);
    check (watch.getState() == DeviceWatch::State::normal, "a device picked in the Audio window is simply the new one");

    e.stopAll (false);
    pump (200);
    e.shutdownAudio();
    std::printf (failures == 0 ? "FAILOVER OK\n" : "FAILOVER FAILED\n");
    return failures;
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // a crash still shows how far it got
    juce::ScopedJuceInitialiser_GUI juce;
    if (argc == 3 && juce::String (argv[1]) == "--plugin")
        return testRealPlugin (juce::String (argv[2]));
    if (argc == 3 && juce::String (argv[1]) == "--peaks")   // EngineTests --peaks file: overall min / max of a file
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        juce::String error;
        auto a = CueAudio::load (juce::File (juce::String (argv[2])), 0.0, formats, error);
        if (a == nullptr) { std::puts (error.toRawUTF8()); return 1; }
        const auto r = a->samples.findMinMax (0, 0, a->samples.getNumSamples());
        std::printf ("%d ch, %.0f Hz, min %.3f max %.3f\n", a->numChannels(), a->sampleRate, r.getStart(), r.getEnd());
        return 0;
    }
    if (argc == 3 && juce::String (argv[1]) == "--download")
        return testDownload (juce::String (argv[2]));
    if (argc == 2 && juce::String (argv[1]) == "--device-loss")
    {
        testDeviceLoss();
        return failures;
    }
    if (argc == 2 && juce::String (argv[1]) == "--failover")
        return testFailover();
    if (argc == 2 && juce::String (argv[1]) == "--arm")
        return testArming();
    if (argc == 4 && juce::String (argv[1]) == "--remote-serve")
        return remoteServe (juce::String (argv[2]), juce::String (argv[3]).getIntValue());
    if (argc >= 2 && juce::String (argv[1]) == "--devices")  // EngineTests --devices [show]: switch through every real device
        return testDeviceSwitching (argc >= 3 ? juce::File (juce::String (argv[2])) : juce::File());
    testFadeCurves();
    testSegments();
    testRouting();
    testEdits();
    testStopping();
    testPlaybackModes();
    testFollow();
    testSeekAndLoop();
    testLoopPoints();
    testMovingButtons();
    testShowFolders();
    testAutosave();
    testShowCheck();
    testDeviceLoss();
    testLoudness();
    testRunOfShow();
    testSchedule();
    testOsc();
    testScheduleOnRemotes();
    testBackupLink();
    testSelfDestruct();
    testPlaylists();
    testControls();
    testStreamDeck();
    testRemoteServer();
    testPlugins();
    testRecording();
    testShowFile();
    testFileLoading();
    testLinks();
    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
