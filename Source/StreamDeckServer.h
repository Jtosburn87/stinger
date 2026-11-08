#pragma once
#include <juce_events/juce_events.h>
#include "Show.h"

/** The Stinger Stream Deck plugin connects here: a WebSocket server on 127.0.0.1 (this PC only).
    The plugin sends {"type":"press","action":"go"} etc.; Stinger sends the state of every button
    ({"type":"state", ...}) whenever it changes, and the plugin draws its keys from that.
    Works the same with any Stream Deck model: the plugin only draws keys.

    The Wi-Fi remote is a second server of this class: it listens on every network adapter, needs the
    secret key in the link (?k=...) and serves the remote's web page as well as the WebSocket. */
class StreamDeckServer : private juce::Thread,
                         private juce::Timer
{
public:
    static constexpr int defaultPort = 52817;

    struct Options
    {
        juce::String address = "127.0.0.1";   // "" = every network adapter
        juce::String key;                     // not empty: every request needs ?k=<key>
        juce::String page;                    // not empty: served for GET / (the remote's web page)
    };

    explicit StreamDeckServer (ShowController& controller, int port = defaultPort);
    StreamDeckServer (ShowController& controller, int port, Options options);
    ~StreamDeckServer() override;

    /** Does an action pressed on the Stream Deck (same ids as keys / MIDI). */
    std::function<void (const juce::String& action)> perform;
    std::function<int()> shownBank;
    /** A button's colour as "#rrggbb" (the palette lives in the UI). */
    std::function<juce::String (const Cue& cue)> colourOf;
    /** Messages other than presses (the remote's {"type":"layout"}). */
    std::function<void (const juce::var& message)> onMessage;
    /** Adds more to every state message (the remote's layouts). */
    std::function<void (juce::DynamicObject& state)> addToState;

    int numClients() const;
    bool isListening() const { return listening.load(); }
    /** True once the listener couldn't be opened (port taken). */
    bool failed() const { return listenFailed.load(); }
    /** Sends the state to everyone on the next tick even if it didn't change. */
    void resend() { lastState = {}; }

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
    const Options options;
    juce::StreamingSocket server;
    std::atomic<bool> listening { false }, listenFailed { false };
    juce::CriticalSection clientLock;
    juce::OwnedArray<Client> clients;
    juce::String lastState;
    int ticksSinceSent = 0;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
