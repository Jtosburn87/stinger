#pragma once
#include "Cue.h"
#include "CueAudio.h"

/** ITU-R BS.1770 integrated loudness (the measure behind LUFS meters) of what a button plays. */
struct LoudnessResult
{
    double lufs = 0.0;      // integrated loudness; 0 = nothing measured (silence, or shorter than one 400 ms block)
    double peakDb = -120.0; // highest sample, dBFS
    bool isValid() const { return lufs < 0.0; }
};

/** Measures these parts of the audio (file seconds). The plugins and the button's level aren't part of it. */
LoudnessResult measureLoudness (const CueAudio& audio, const std::vector<Segment>& parts);

/** Identifies a set of parts: a saved measurement holds while this stays the same. */
juce::String loudnessKey (const std::vector<Segment>& parts);
