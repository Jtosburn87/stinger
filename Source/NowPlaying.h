#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Show.h"

/** Right-hand column: what's playing (time left, Fade out / Loop / Stop each, click or drag the bar to jump), meters for the outputs
    the show uses, and Fade all / Stop all. */
class NowPlaying : public juce::Component,
                   private juce::Timer
{
public:
    explicit NowPlaying (ShowController& controller);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Card
    {
        int voice = -1;
        double length = 0.0;   // seconds
        bool looping = false;
        juce::Rectangle<float> fade, loop, stop, bar;
    };

    /** Click / drag on a card's progress bar: jump there. */
    void seek (const Card& c, juce::Point<float> p);

    void timerCallback() override;
    std::vector<OutputRoute> usedOutputs() const;

    ShowController& controller;
    juce::TextButton fadeAll { "FADE ALL" }, stopAll { "STOP ALL" };
    std::vector<Card> cards;
    int scrubVoice = -1;                // voice whose bar is being dragged
    double scrubFraction = -1.0;        // where it was dragged to (drawn until the engine catches up)
    std::array<float, PlaybackEngine::maxOutputs> meter {};
};
