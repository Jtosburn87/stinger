#pragma once
#include <juce_events/juce_events.h>
#include "Show.h"

/** The Stinger Stream Deck plugin connects here: a WebSocket server on 127.0.0.1 (this PC only).
    The plugin sends {"type":"press","action":"go"} etc.; Stinger sends the state of every button
    ({"type":"state", ...}) whenever it changes, and the plugin draws its keys from that.
    Works the same with any Stream Deck model: the plugin only draws keys. */
class StreamDeckServer : private juce::Thread,
                         private juce::Timer
{
public:
    static constexpr int defaultPort = 52817;

    explicit StreamDeckServer (ShowController& controller, int port = defaultPort);
    ~StreamDeckServer() override;

    /** Does an action pressed on the Stream Deck (same ids as keys / MIDI). */
    std::function<void (const juce::String& action)> perform;
    std::function<int()> shownBank;
    /** A button's colour as "#rrggbb" (the palette lives in the UI). */
    std::function<juce::String (const Cue& cue)> colourOf;

    int numClients() const;
    bool isListening() const { return listening.load(); }

    /** The state message (also used by the tests). */
    juce::String stateJson();

    /** RFC 6455: Sec-WebSocket-Accept for a Sec-WebSocket-Key. */
    static juce::String acceptKey (const juce::String& key);

private:
    class Client;

    void run() override;            // accepts connections
    void timerCallback() override;  // sends the state when it changes
    void received (const juce::String& text);

    ShowController& controller;
    const int port;
    juce::StreamingSocket server;
    std::atomic<bool> listening { false };
    juce::CriticalSection clientLock;
    juce::OwnedArray<Client> clients;
    juce::String lastState;
    int ticksSinceSent = 0;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
