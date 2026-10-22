#include "StreamDeckServer.h"

namespace
{
    /** SHA-1 (only for the WebSocket handshake). */
    std::array<juce::uint8, 20> sha1 (const juce::String& text)
    {
        juce::MemoryBlock data (text.toRawUTF8(), text.getNumBytesAsUTF8());
        const juce::uint64 bitLength = (juce::uint64) data.getSize() * 8;
        data.append ("\x80", 1);
        while (data.getSize() % 64 != 56)
            data.append ("\0", 1);
        for (int i = 7; i >= 0; --i)
        {
            const auto b = (char) ((bitLength >> (i * 8)) & 0xff);
            data.append (&b, 1);
        }

        juce::uint32 h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
        auto rol = [] (juce::uint32 v, int n) { return (v << n) | (v >> (32 - n)); };
        const auto* bytes = static_cast<const juce::uint8*> (data.getData());
        for (size_t chunk = 0; chunk < data.getSize(); chunk += 64)
        {
            juce::uint32 w[80];
            for (int i = 0; i < 16; ++i)
                w[i] = (juce::uint32) bytes[chunk + (size_t) i * 4] << 24 | (juce::uint32) bytes[chunk + (size_t) i * 4 + 1] << 16
                     | (juce::uint32) bytes[chunk + (size_t) i * 4 + 2] << 8 | (juce::uint32) bytes[chunk + (size_t) i * 4 + 3];
            for (int i = 16; i < 80; ++i)
                w[i] = rol (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            juce::uint32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
            for (int i = 0; i < 80; ++i)
            {
                juce::uint32 f, k;
                if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
                else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
                else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
                const juce::uint32 t = rol (a, 5) + f + e + k + w[i];
                e = d; d = c; c = rol (b, 30); b = a; a = t;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
        }
        std::array<juce::uint8, 20> out {};
        for (int i = 0; i < 20; ++i)
            out[(size_t) i] = (juce::uint8) (h[i / 4] >> (24 - (i % 4) * 8));
        return out;
    }

}

juce::String StreamDeckServer::acceptKey (const juce::String& key)
{
    const auto digest = sha1 (key.trim() + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    return juce::Base64::toBase64 (digest.data(), digest.size());
}

// ---------------------------------------------------------------------------------------------

/** One connected plugin: reads its frames on its own thread, sends from the message thread. */
class StreamDeckServer::Client : private juce::Thread
{
public:
    Client (std::unique_ptr<juce::StreamingSocket> s, std::function<void (const juce::String&)> onText)
        : juce::Thread ("Stream Deck client"), socket (std::move (s)), received (std::move (onText))
    {
        startThread();
    }

    ~Client() override
    {
        signalThreadShouldExit();
        socket->close();
        stopThread (2000);
    }

    bool isOpen() const { return open.load(); }
    bool isNew() { return fresh.exchange (false); }

    void send (const juce::String& text)
    {
        if (! open.load() || ! handshakeDone.load())
            return;
        const auto size = (size_t) text.getNumBytesAsUTF8();
        juce::MemoryBlock frame;
        juce::uint8 head[10];
        int headSize = 2;
        head[0] = 0x81;   // final frame, text
        if (size < 126)
            head[1] = (juce::uint8) size;
        else if (size < 65536)
        {
            head[1] = 126;
            head[2] = (juce::uint8) (size >> 8);
            head[3] = (juce::uint8) size;
            headSize = 4;
        }
        else
        {
            head[1] = 127;
            for (int i = 0; i < 8; ++i)
                head[2 + i] = (juce::uint8) ((juce::uint64) size >> (56 - i * 8));
            headSize = 10;
        }
        frame.append (head, (size_t) headSize);
        frame.append (text.toRawUTF8(), size);
        const juce::ScopedLock sl (writeLock);
        if (socket->write (frame.getData(), (int) frame.getSize()) < 0)
            open.store (false);
    }

private:
    bool readExactly (void* dest, int bytes)
    {
        auto* p = static_cast<char*> (dest);
        while (bytes > 0 && ! threadShouldExit())
        {
            const int ready = socket->waitUntilReady (true, 200);
            if (ready < 0)
                return false;
            if (ready == 0)
                continue;
            const int n = socket->read (p, bytes, false);
            if (n <= 0)
                return false;
            p += n;
            bytes -= n;
        }
        return bytes == 0;
    }

    bool handshake()
    {
        juce::String request;
        char c = 0;
        while (request.length() < 8192 && ! request.endsWith ("\r\n\r\n"))
        {
            if (! readExactly (&c, 1))
                return false;
            request += c;
        }
        juce::String key;
        for (auto& line : juce::StringArray::fromLines (request))
            if (line.startsWithIgnoreCase ("Sec-WebSocket-Key:"))
                key = line.fromFirstOccurrenceOf (":", false, false).trim();
        if (key.isEmpty())
            return false;
        const auto reply = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: " + acceptKey (key) + "\r\n\r\n";
        const juce::ScopedLock sl (writeLock);
        return socket->write (reply.toRawUTF8(), (int) reply.getNumBytesAsUTF8()) > 0;
    }

    void run() override
    {
        if (! handshake())
        {
            open.store (false);
            return;
        }
        handshakeDone.store (true);
        fresh.store (true);
        while (! threadShouldExit())
        {
            juce::uint8 head[2];
            if (! readExactly (head, 2))
                break;
            const int opcode = head[0] & 0x0f;
            const bool masked = (head[1] & 0x80) != 0;
            juce::uint64 length = head[1] & 0x7f;
            if (length == 126)
            {
                juce::uint8 ext[2];
                if (! readExactly (ext, 2)) break;
                length = (juce::uint64) ext[0] << 8 | ext[1];
            }
            else if (length == 127)
            {
                juce::uint8 ext[8];
                if (! readExactly (ext, 8)) break;
                length = 0;
                for (auto b : ext)
                    length = length << 8 | b;
            }
            if (length > (1u << 20))
                break;   // nothing the plugin sends is that big
            juce::uint8 mask[4] = {};
            if (masked && ! readExactly (mask, 4))
                break;
            juce::MemoryBlock payload ((size_t) length);
            if (length > 0 && ! readExactly (payload.getData(), (int) length))
                break;
            auto* p = static_cast<juce::uint8*> (payload.getData());
            for (size_t i = 0; i < (size_t) length; ++i)
                p[i] ^= mask[i % 4];

            if (opcode == 0x8)          // close
                break;
            if (opcode == 0x9)          // ping -> pong
            {
                juce::uint8 pong[2] = { 0x8a, 0 };
                const juce::ScopedLock sl (writeLock);
                socket->write (pong, 2);
            }
            else if (opcode == 0x1)
                received (payload.toString());
        }
        open.store (false);
    }

    std::unique_ptr<juce::StreamingSocket> socket;
    std::function<void (const juce::String&)> received;
    juce::CriticalSection writeLock;
    std::atomic<bool> open { true }, handshakeDone { false }, fresh { false };
};

// ---------------------------------------------------------------------------------------------

StreamDeckServer::StreamDeckServer (ShowController& c, int listenPort)
    : juce::Thread ("Stream Deck server"), controller (c), port (listenPort)
{
    startThread();
    startTimerHz (10);
}

StreamDeckServer::~StreamDeckServer()
{
    alive->store (false);
    stopTimer();
    signalThreadShouldExit();
    server.close();
    stopThread (3000);
    const juce::ScopedLock sl (clientLock);
    clients.clear();
}

int StreamDeckServer::numClients() const
{
    const juce::ScopedLock sl (clientLock);
    int n = 0;
    for (auto* c : clients)
        n += c->isOpen() ? 1 : 0;
    return n;
}

void StreamDeckServer::run()
{
    if (! server.createListener (port, "127.0.0.1"))
        return;   // port taken (another Stinger?): no Stream Deck
    listening.store (true);
    while (! threadShouldExit())
    {
        std::unique_ptr<juce::StreamingSocket> socket (server.waitForNextConnection());
        if (socket == nullptr)
            continue;
        auto onText = [this, stillHere = alive] (const juce::String& text)
        {
            juce::MessageManager::callAsync ([this, stillHere, text]
            {
                if (stillHere->load())
                    received (text);
            });
        };
        const juce::ScopedLock sl (clientLock);
        clients.add (new Client (std::move (socket), onText));
    }
    listening.store (false);
}

void StreamDeckServer::received (const juce::String& text)
{
    const auto v = juce::JSON::parse (text);
    if (v.getProperty ("type", {}).toString() == "press" && perform)
    {
        const auto action = v.getProperty ("action", {}).toString();
        if (action.isNotEmpty())
            perform (action);
    }
}

juce::String StreamDeckServer::stateJson()
{
    auto& show = controller.getShow();
    auto& engine = controller.getEngine();
    const auto voices = engine.getVoices();

    int nextCue = -1;
    juce::String nextName;
    if (auto* p = controller.currentPlaylist(); p != nullptr)
        if (const int n = controller.getNextEntry (controller.getSelectedPlaylist()); n >= 0 && n < (int) p->entries.size())
        {
            nextCue = p->entries[(size_t) n].cueId;
            if (show.isValid (nextCue))
                nextName = show.cue (nextCue).name;
        }

    auto* root = new juce::DynamicObject();
    root->setProperty ("type", "state");
    root->setProperty ("bank", shownBank ? shownBank() : 0);
    root->setProperty ("selfDestruct", show.selfDestruct);
    juce::Array<juce::var> banks;
    for (auto& b : show.banks)
        banks.add (b.name);
    root->setProperty ("banks", banks);

    juce::Array<juce::var> cues;
    for (int b = 0; b < (int) show.banks.size(); ++b)
        for (int s = 0; s < Show::cuesPerBank; ++s)
        {
            const int id = Show::cueId (b, s);
            const auto& c = show.cue (id);
            if (c.isEmpty())
                continue;
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", id);
            o->setProperty ("bank", b);
            o->setProperty ("slot", s);
            o->setProperty ("name", c.name);
            o->setProperty ("colour", colourOf ? colourOf (c) : juce::String ("#5aa9f0"));
            o->setProperty ("next", id == nextCue);
            if (c.spent)
                o->setProperty ("spent", true);   // Self Destruct: drawn black
            for (auto& v : voices)
                if (v.cueId == id)
                {
                    o->setProperty ("playing", true);
                    o->setProperty ("stopping", v.stopping);
                    o->setProperty ("looping", v.looping);
                    if (v.waiting)
                        o->setProperty ("waitLeft", (int) std::ceil (v.waitLeft));
                    o->setProperty ("remaining", (int) std::ceil (std::max (0.0, v.length - v.position)));
                    o->setProperty ("progress", std::round (v.position / std::max (0.001, v.length) * 50.0) / 50.0);
                    break;
                }
            cues.add (o);
        }
    root->setProperty ("cues", cues);

    auto* go = new juce::DynamicObject();
    go->setProperty ("next", nextName);
    go->setProperty ("looping", controller.isLoopingUntilGo (controller.getSelectedPlaylist()));
    go->setProperty ("playlist", controller.getSelectedPlaylist());
    root->setProperty ("go", go);

    // every playlist, for GO keys set to one playlist
    juce::Array<juce::var> playlists;
    for (int pl = 0; pl < (int) show.playlists.size(); ++pl)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", show.playlists[(size_t) pl].displayName (pl));
        juce::String next;
        if (const int n = controller.getNextEntry (pl); n >= 0 && show.isValid (show.playlists[(size_t) pl].entries[(size_t) n].cueId))
            next = show.cue (show.playlists[(size_t) pl].entries[(size_t) n].cueId).name;
        o->setProperty ("next", next);
        o->setProperty ("looping", controller.isLoopingUntilGo (pl));
        playlists.add (o);
    }
    root->setProperty ("playlists", playlists);
    root->setProperty ("anyPlaying", ! voices.empty());

    // the countdown key: the sting started last (one still playing on rather than fading out, if any)
    const PlaybackEngine::VoiceInfo* now = nullptr;
    for (auto& v : voices)   // newest first
        if (show.isValid (v.cueId) && (now == nullptr || (now->stopping && ! v.stopping)))
            now = &v;
    if (now != nullptr)
    {
        const auto& c = show.cue (now->cueId);
        const double left = std::max (0.0, now->length - now->position);
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", now->cueId);
        o->setProperty ("name", c.name);
        o->setProperty ("colour", colourOf ? colourOf (c) : juce::String ("#5aa9f0"));
        // tenths in the last 10 seconds, whole seconds before that (fewer messages)
        o->setProperty ("remaining", left < 10.0 ? std::ceil (left * 10.0) / 10.0 : std::ceil (left));
        o->setProperty ("progress", std::round (now->position / std::max (0.001, now->length) * 100.0) / 100.0);
        o->setProperty ("looping", now->looping);
        o->setProperty ("stopping", now->stopping);
        if (now->waiting)
        {
            // a playlist Wait: counting down to the next song
            o->setProperty ("waitLeft", now->waitLeft < 10.0 ? std::ceil (now->waitLeft * 10.0) / 10.0 : std::ceil (now->waitLeft));
            o->setProperty ("nextName", nextName);
        }
        o->setProperty ("others", (int) voices.size() - 1);
        root->setProperty ("now", o);
    }
    return juce::JSON::toString (juce::var (root), true);
}

void StreamDeckServer::timerCallback()
{
    // drop plugins that went away
    {
        const juce::ScopedLock sl (clientLock);
        for (int i = clients.size(); --i >= 0;)
            if (! clients[i]->isOpen())
                clients.remove (i);
    }
    if (numClients() == 0)
        return;

    const auto state = stateJson();
    const bool changed = state != lastState || ++ticksSinceSent >= 20;   // and every 2 s anyway
    const juce::ScopedLock sl (clientLock);
    for (auto* c : clients)
        if (c->isNew() || changed)
            c->send (state);
    if (changed)
    {
        lastState = state;
        ticksSinceSent = 0;
    }
}
