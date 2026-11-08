#include "DexDeck.h"
#include "BinaryData.h"

DexDeck::DexDeck (ShowController& controller, MainComponent& desktop)
{
    StreamDeckServer::Options options;
    options.address = "127.0.0.1";   // the phone itself only
    options.key = juce::Uuid().toString();
    options.page = juce::String::fromUTF8 (BinaryData::remote_html, BinaryData::remote_htmlSize);
    server = std::make_unique<StreamDeckServer> (controller, port, options);

    // the keys do what the Stream Deck's and the Wi-Fi remote's do
    auto& deck = desktop.getStreamDeck();
    server->perform = deck.perform;
    server->shownBank = deck.shownBank;
    server->colourOf = deck.colourOf;

    // layouts are the Wi-Fi remote's: edited here or on a phone, both get them
    auto& remote = desktop.getRemote();
    server->addToState = [&remote] (juce::DynamicObject& state) { state.setProperty ("layouts", remote.getLayouts()); };
    auto* s = server.get();
    server->onMessage = [&remote, s] (const juce::var& m)
    {
        if (m.getProperty ("type", {}).toString() == "layout")
        {
            remote.setLayout (m.getProperty ("device", {}).toString(), m.getProperty ("keys", {}));
            s->resend();
        }
    };

    link = "http://127.0.0.1:" + juce::String (port) + "/?k=" + options.key;
}

DexDeck::~DexDeck()
{
    server = nullptr;
}
