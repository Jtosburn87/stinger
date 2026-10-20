// Engine tests: synthetic audio through the playback engine, no audio device needed.

#include <juce_audio_formats/juce_audio_formats.h>
#include "PlaybackEngine.h"
#include "Show.h"
#include "StreamDeckServer.h"
#include "Downloader.h"
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
    auto start = [&] (Cue c, int id, PlaybackMode mode, int tag = 0, bool playlistToo = true)
    {
        PlayPlan plan;
        PlayPlan::make (c, *dc, id, plan);
        plan.takeOver = mode;
        plan.tag = tag;
        plan.takeOverPlaylist = playlistToo;
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

    start (pair12, 1, PlaybackMode::oneAtATime, 7);              // a playlist song
    start (pair34, 2, PlaybackMode::oneAtATime);                 // a button
    out.run (e, 10);
    check (! e.isCuePlaying (1) && e.isCuePlaying (2), "a button takes over from a playlist song");
    start (pair12, 3, PlaybackMode::oneAtATime, 8, false);       // GO: leaves playlist songs to their own fade
    start (pair12, 4, PlaybackMode::overlap, 9);                 // a playlist follow
    out.run (e, 10);
    check (! e.isCuePlaying (2) && e.isCuePlaying (3) && e.isCuePlaying (4), "GO takes over from buttons, playlist transitions overlap");

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

static void testMovingButtons()
{
    std::puts ("Moving buttons, several files at once");
    PlaybackEngine e;
    e.prepareForTest (rate, 2);
    ShowController sc (e);
    auto& show = sc.getShow();
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory);

    show.cue (0) = monoOut (0);
    show.cue (0).name = "Intro";
    show.cue (0).hotkey = "F5";
    show.cue (5) = monoOut (1);
    show.cue (5).name = "Outro";
    sc.addToPlaylist (0);
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
        sc.addToPlaylist (i);
    }
    sc.setTransition (0, Transition::playNext, 0.0);
    sc.setTransition (1, Transition::crossfade, 0.1);
    // 2 -> 3: stop

    auto tick = [&] (int blocks) { for (int b = 0; b < blocks; ++b) { out.run (e); sc.updatePlaylist(); } };
    check (sc.getNextEntry() == 0 && sc.getOnAirEntry() == -1, "next is the top before anything plays");
    sc.go();
    tick (1);
    check (sc.getOnAirEntry() == 0 && sc.getNextEntry() == 1 && near (out.at (0), 0.1f), "GO plays song 1, song 2 is next");
    tick (50);   // 0.5 s: song 1 ended, song 2 followed
    check (sc.getOnAirEntry() == 1 && sc.getNextEntry() == 2 && near (out.at (0), 0.2f), "Play next: song 2 followed by itself");
    tick (40);   // 0.9 s: in the crossfade into song 3 (starts at 0.9 s)
    tick (5);
    check (e.getVoices().size() == 2, "crossfade: songs 2 and 3 together");
    tick (10);
    check (sc.getOnAirEntry() == 2 && sc.getNextEntry() == 3 && near (out.at (0), 0.3f, 0.01f), "song 3 on air after the crossfade");
    tick (60);
    check (e.getVoices().empty() && sc.getNextEntry() == 3, "Stop: the playlist stops after song 3, song 4 waits for GO");
    sc.go();
    tick (5);
    check (sc.getOnAirEntry() == 3 && near (out.at (0), 0.4f), "GO plays song 4");
    tick (60);

    // change a link while the song plays
    sc.setNextEntry (0);
    sc.go();
    tick (5);
    sc.setTransition (0, Transition::stop, 0.0);
    tick (60);
    check (e.getVoices().empty(), "changing Play next to Stop while playing takes effect");

    // GO while a playlist song plays: it fades out, the next one starts
    sc.setNextEntry (1);
    sc.go();
    tick (5);
    sc.go();
    tick (1);
    check (sc.getOnAirEntry() == 2 && e.getVoices().size() == 2 && e.getVoices().back().stopping, "GO moves on: the old song fades out");
    e.stopAll (false);
    tick (3);

    // Loop until GO: loops; GO lets it finish the pass, then the next song follows with no gap
    sc.setTransition (0, Transition::loopUntil, 0.2);
    sc.setTransition (1, Transition::stop, 0.0);
    sc.setNextEntry (0);
    sc.go();
    tick (130);   // 1.3 s: well past the 0.5 s song
    check (sc.getOnAirEntry() == 0 && sc.isLoopingUntilGo() && near (out.at (0), 0.1f), "Loop until GO keeps looping");
    sc.go();      // 1.3 s into the song: the pass ends at 1.5 s
    tick (15);
    check (sc.getOnAirEntry() == 0 && ! sc.isLoopingUntilGo() && near (out.at (0), 0.1f), "GO: the loop finishes its pass first");
    tick (10);
    check (sc.getOnAirEntry() == 1 && e.getVoices().size() == 1 && near (out.at (0), 0.2f), "then the next song, no gap");
    e.stopAll (false);
    tick (3);

    // Fade to next: crossfade now, over the looping song's fade time
    sc.setNextEntry (0);
    sc.go();
    tick (70);
    sc.fadeToNext();
    tick (10);   // 0.1 s into a 0.2 s fade
    check (e.getVoices().size() == 2 && near (out.at (0), 0.5f * 0.1f + 0.5f * 0.2f, 0.01f), "Fade to next: half way, both at half");
    tick (12);
    check (sc.getOnAirEntry() == 1 && e.getVoices().size() == 1 && near (out.at (0), 0.2f, 0.005f), "Fade to next: the next song on its own");
    e.stopAll (false);
    tick (3);

    // changing Loop until GO to something else while it loops lets it end
    sc.setNextEntry (0);
    sc.go();
    tick (70);
    sc.setTransition (0, Transition::stop, 0.0);
    tick (40);
    check (e.getVoices().empty(), "a loop changed to Stop plays to its end and stops");
    sc.setTransition (0, Transition::playNext, 0.0);
    sc.setTransition (1, Transition::crossfade, 0.1);

    // reorder, remove, save and load
    sc.moveEntry (3, 0);
    check (show.playlists[0].entries[0].cueId == 3, "move an entry");
    sc.removeEntry (0);
    check (show.playlists[0].entries.size() == 3, "remove an entry");
    Show t;
    t.fromVar (juce::JSON::parse (juce::JSON::toString (show.toVar())));
    check (t.playlists.size() == 1 && t.playlists[0].entries.size() == 3
           && t.playlists[0].entries[1].transition == Transition::crossfade
           && near ((float) t.playlists[0].entries[1].crossfadeSeconds, 0.1f), "playlists and transitions saved in the show");
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

int main (int argc, char** argv)
{
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
    if (argc == 2 && juce::String (argv[1]) == "--arm")
        return testArming();
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
    testMovingButtons();
    testPlaylists();
    testControls();
    testStreamDeck();
    testPlugins();
    testRecording();
    testShowFile();
    testFileLoading();
    testLinks();
    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
