#pragma once
#include "DexDeck.h"

/** A real Elgato Stream Deck on Android (phone pages or Samsung DeX), driven by Stinger itself (StreamDeckUsb.java does the USB).
    Fixed layouts for now:
      15 keys (5 x 3): buttons 1-12 of the shown bank, then GO, Stop all, Next bank
      XL (8 x 4):      the 24 buttons where they are in Stinger (6 x 4), then two columns:
                       GO / Fade to next, Stop all / Fade all, Previous / Next bank, Countdown / Self Destruct
    The pictures follow the Stinger Stream Deck plugin's look, from the same state message (StreamDeckServer).
    Android only. */
class DexStreamDeck : private juce::Timer
{
public:
    explicit DexStreamDeck (DexDeck& server);
    ~DexStreamDeck() override;

    /** The deck as StreamDeckUsb reported it (connected or not). Message thread. */
    void deckChanged();
    void keyPressed (int key);

private:
    struct Key
    {
        juce::String action;   // "slot7", "go", ... ("" = display only)
        int slot = -1;         // a button of the shown bank, else -1
        juce::String kind;     // "button", "go", "fadeNext", "stopAll", "fadeAll", "bankPrev", "bankNext", "countdown", "selfDestruct"
    };

    void timerCallback() override;
    Key keyAt (int index) const;
    juce::Image draw (const Key& key, const juce::var& state, int pixels) const;
    void send (int index, const juce::Image& picture);

    DexDeck& server;
    juce::String lastState;
    std::vector<juce::String> shown;   // per key: what its picture shows (a new picture only when this changes)
};
