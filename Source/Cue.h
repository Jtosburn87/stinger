#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include "Controls.h"

/** Shape of a fade. All curves go from 0 to 1 as x goes from 0 to 1. */
enum class FadeCurve { linear = 0, sCurve, exponential, logarithmic };

/** Gain of a rising fade at x (0..1). A falling fade uses fadeGain (curve, 1 - x). */
float fadeGain (FadeCurve curve, float x);
juce::String fadeCurveName (FadeCurve curve);

struct Fade
{
    double seconds = 0.0;
    FadeCurve curve = FadeCurve::sCurve;
};

/** Where a cue plays: one device output (mono) or a pair starting at firstChannel (stereo). 0-based. */
struct OutputRoute
{
    int firstChannel = 0;
    bool stereo = true;

    int numChannels() const { return stereo ? 2 : 1; }
    bool operator== (const OutputRoute& o) const { return firstChannel == o.firstChannel && stereo == o.stereo; }
};

/** One section of the file that plays, in file seconds. */
struct Segment
{
    double start = 0.0, end = 0.0;
    bool pasted = false;                    // part of a pasted copy (plays a second time)
    double length() const { return end - start; }
    bool operator== (const Segment& o) const { return start == o.start && end == o.end && pasted == o.pasted; }
};

/** A copied section pasted back in: when playback gets to file time `at`, the pieces play, then it carries on. */
struct Paste
{
    double at = 0.0;
    std::vector<Segment> pieces;
    double length() const { double l = 0.0; for (auto& p : pieces) l += p.length(); return l; }
    bool operator== (const Paste& o) const { return at == o.at && pieces == o.pieces; }
};

/** Everything about one button. Times are seconds in the audio file; the file itself is never changed. */
struct Cue
{
    juce::String name;
    juce::File file;
    int colour = 0;                         // index into the button palette
    juce::uint32 customColour = 0;          // ARGB of a colour picked by hand (0 = the palette colour)
    bool spent = false;                     // Self Destruct: played, shown black until reset (colour kept)

    double trimIn = 0.0;                    // first second that plays
    double trimOut = -1.0;                  // last second that plays; < 0 = end of file
    std::vector<juce::Range<double>> cuts;  // sections taken out (razor), file seconds
    std::vector<Paste> pastes;              // copies pasted in (Ctrl+V), in file order of `at`

    Fade fadeIn, fadeOut;                   // at the start / end of the trimmed region
    Fade stopFade { 1.0, FadeCurve::sCurve };  // used by "Fade out" while playing
    float levelDb = 0.0f;                   // -20 .. +20
    float pan = 0.0f;                       // -1 (left) .. +1 (right)
    OutputRoute output;
    bool fadeOthers = false;                // starting this fades out everything else
    bool loop = false;                      // plays until stopped (pressed again, another button on its output, Stop all)
    double loopIn = -1.0, loopOut = -1.0;   // loop points, file seconds, anywhere in the file (< 0 = none: the whole clip loops)
    bool loopFromLoopIn = false;            // a looping play starts at Loop In (else at In: the part up to the loop is an intro)

    bool hasLoopPoints() const { return loopIn >= 0.0 && loopOut > loopIn; }
    /** What plays when looping with loop points: the intro (In up to the loop), the loop (Loop In..Out) and the
        outro (Loop Out..Out, played once the loop is turned off). Cuts are left out; pastes don't play. */
    void loopSections (double fileSeconds, std::vector<Segment>& intro, std::vector<Segment>& body, std::vector<Segment>& outro) const;
    juce::String hotkey;                    // juce::KeyPress description, empty = none
    MidiTrigger midi;                       // plays this button from MIDI

    /** The 3 plugin slots as saved in the show (the running plugins live in the ShowController). */
    struct PluginSlot
    {
        juce::String description;           // juce::PluginDescription as XML, "" = empty
        juce::String state;                 // the plugin's settings, base64
        bool bypassed = false;
        bool isEmpty() const { return description.isEmpty(); }
    };
    std::array<PluginSlot, 3> plugins;
    bool hasPlugins() const { for (auto& p : plugins) if (! p.isEmpty()) return true; return false; }

    static constexpr float minLevelDb = -20.0f, maxLevelDb = 20.0f;

    bool isEmpty() const { return file == juce::File(); }

    /** The parts of the file that play, in order, for a file fileSeconds long:
        trim in..out with the cuts taken out, and the pasted copies played where they were pasted. */
    std::vector<Segment> segments (double fileSeconds) const;
    /** The same without the pastes (what the trims and cuts leave). */
    std::vector<Segment> baseSegments (double fileSeconds) const;

    /** Length of what plays (all segments), in seconds. */
    double playLength (double fileSeconds) const;

    /** A time in the file as a position in what plays (0 = trim in; inside a cut = where the cut ends;
        pasted copies before it count, the copies themselves aren't matched). */
    double timelinePosition (double fileSeconds, double fileTime) const;

    juce::var toVar() const;
    static Cue fromVar (const juce::var& v);
};
