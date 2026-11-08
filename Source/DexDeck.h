#pragma once
#include "StreamDeckServer.h"
#include "Remote.h"

/** Stinger's key server on the phone itself (Android only), from when the app starts:
      - the deck page on the phone's screen in Samsung DeX (the Wi-Fi remote's page, same keys and layouts)
      - a real Stream Deck plugged into the phone (DexStreamDeck draws its keys from the same state)
    127.0.0.1 only, its own port and a new private key every start: it works with the Wi-Fi remote off and nothing is
    open to the network. Its keys act on whichever screen is in charge: the phone pages, or the desktop screen in DeX. */
class DexDeck
{
public:
    static constexpr int port = 52820;

    explicit DexDeck (ShowController& controller);
    ~DexDeck();

    /** Where key presses go and which bank the keys show (the phone pages, or the desktop screen in DeX). */
    void setTarget (std::function<void (const juce::String& action)> perform, std::function<int()> shownBank);
    /** The Wi-Fi remote whose layouts the deck page shares (the desktop screen's), or nullptr. */
    void setLayouts (Remote* remote);

    StreamDeckServer& getServer() { return *server; }
    /** The page for the deck's web view. */
    juce::String url() const { return link; }

private:
    std::unique_ptr<StreamDeckServer> server;
    std::function<void (const juce::String&)> target;
    std::function<int()> bank;
    Remote* layouts = nullptr;
    juce::String link;
};
