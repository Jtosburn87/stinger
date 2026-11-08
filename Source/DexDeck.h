#pragma once
#include "MainComponent.h"

/** Samsung DeX: the deck on the phone's own screen while Stinger is on the monitor. It's the Wi-Fi remote's page
    (same keys, same layouts, edited on either), served by Stinger to the phone itself only: 127.0.0.1, its own
    port and a new private key every time, so it works with the Wi-Fi remote off and nothing is open to the network.
    Android only. */
class DexDeck
{
public:
    static constexpr int port = 52820;

    DexDeck (ShowController& controller, MainComponent& desktop);
    ~DexDeck();

    /** The page for the deck's web view. */
    juce::String url() const { return link; }

private:
    std::unique_ptr<StreamDeckServer> server;
    juce::String link;
};
