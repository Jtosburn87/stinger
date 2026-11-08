#include "DexDeck.h"
#include "Look.h"
#include "BinaryData.h"

DexDeck::DexDeck (ShowController& controller)
{
    StreamDeckServer::Options options;
    options.address = "127.0.0.1";   // the phone itself only
    options.key = juce::Uuid().toString();
    options.page = juce::String::fromUTF8 (BinaryData::remote_html, BinaryData::remote_htmlSize);
    server = std::make_unique<StreamDeckServer> (controller, port, options);

    // the keys do what the Stream Deck plugin's and the Wi-Fi remote's do, on whichever screen is in charge
    server->perform = [this] (const juce::String& action) { if (target) target (action); };
    server->shownBank = [this] { return bank ? bank() : 0; };
    server->colourOf = [] (const Cue& cue) { return "#" + Look::colourOf (cue).toDisplayString (false).toLowerCase(); };

    // layouts are the Wi-Fi remote's: edited here or on a phone, both get them
    server->addToState = [this] (juce::DynamicObject& state) { if (layouts != nullptr) state.setProperty ("layouts", layouts->getLayouts()); };
    server->onMessage = [this] (const juce::var& m)
    {
        if (layouts != nullptr && m.getProperty ("type", {}).toString() == "layout")
        {
            layouts->setLayout (m.getProperty ("device", {}).toString(), m.getProperty ("keys", {}));
            server->resend();
        }
    };

    link = "http://127.0.0.1:" + juce::String (port) + "/?k=" + options.key;
}

DexDeck::~DexDeck()
{
    server = nullptr;
}

void DexDeck::setTarget (std::function<void (const juce::String&)> perform, std::function<int()> shownBank)
{
    target = std::move (perform);
    bank = std::move (shownBank);
    server->resend();   // the other screen's bank
}

void DexDeck::setLayouts (Remote* remote)
{
    layouts = remote;
    server->resend();
}
