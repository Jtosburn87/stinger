#include "BackupLink.h"
#include <cstring>
#include <deque>
#include <set>

namespace
{
    constexpr juce::uint32 lostAfterMs = 1500;     // nothing heard for this long: the main laptop is gone
    constexpr juce::uint32 retryMs = 2000;
    constexpr juce::uint32 restartGuardMs = 250;   // a voice just asked for may not be playing yet
    constexpr double driftLimit = 0.05;            // seconds; further out than this, the backup's song jumps
    constexpr double linkDelay = 0.01;             // typical one-way time on a network, plus a block of audio
    constexpr int chunkSize = 64 * 1024;
    constexpr juce::uint32 maxFrame = 512u * 1024u * 1024u;
    enum FrameType : juce::uint8 { textFrame = 1, fileStart = 2, fileData = 3 };
}

// ---- One connection, on its own thread: nothing here ever waits on the message thread, or it on the network ------

class BackupLink::Connection : public juce::Thread
{
public:
    Connection (BackupLink& o, std::unique_ptr<juce::StreamingSocket> s, const juce::String& h, int p, juce::File f)
        : juce::Thread ("Stinger backup link"), owner (o), socket (std::move (s)), host (h), port (p), folder (std::move (f)), alive (o.alive)
    {
    }

    ~Connection() override
    {
        signalThreadShouldExit();
        if (socket != nullptr)
            socket->close();
        stopThread (3000);
    }

    /** The newest list of voices replaces one that hasn't gone yet: only the latest matters. */
    void sendVoices (const juce::String& text)
    {
        const juce::ScopedLock sl (lock);
        voices = text;
    }

    void send (const juce::String& text)
    {
        const juce::ScopedLock sl (lock);
        queue.push_back (text);
    }

    void sendFile (const juce::File& file)
    {
        const juce::ScopedLock sl (lock);
        files.push_back (file);
    }

    bool isUp() const { return up.load(); }

    /** Connected socket for the server side: the first thing through must be the code. */
    static bool readFrame (juce::StreamingSocket& s, juce::uint8& type, juce::MemoryBlock& payload)
    {
        juce::uint8 header[5];
        if (s.read (header, 5, true) != 5)
            return false;
        type = header[0];
        const auto size = (juce::uint32) juce::ByteOrder::littleEndianInt (header + 1);
        if (size > maxFrame)
            return false;
        payload.setSize (size);
        return size == 0 || s.read (payload.getData(), (int) size, true) == (int) size;
    }

    static bool writeFrame (juce::StreamingSocket& s, juce::uint8 type, const void* data, size_t size)
    {
        juce::uint8 header[5] = { type };
        const auto littleEndian = juce::ByteOrder::swapIfBigEndian ((juce::uint32) size);
        std::memcpy (header + 1, &littleEndian, 4);
        return s.write (header, 5) == 5 && (size == 0 || s.write (data, (int) size) == (int) size);
    }


private:
    void run() override
    {
        if (socket == nullptr)
        {
            auto s = std::make_unique<juce::StreamingSocket>();
            if (! s->connect (host, port, 2000))
            {
                finish();
                return;
            }
            socket = std::move (s);
        }
        const auto hello = pendingHello();
        if (hello.isNotEmpty() && ! writeFrame (*socket, textFrame, hello.toRawUTF8(), hello.getNumBytesAsUTF8()))
        {
            finish();
            return;
        }
        up = true;

        std::unique_ptr<juce::FileInputStream> sending;
        juce::int64 sendLeft = 0;
        juce::MemoryBlock chunk (chunkSize);
        while (! threadShouldExit())
        {
            const int ready = socket->waitUntilReady (true, sending != nullptr ? 0 : 2);
            if (ready < 0)
                break;
            if (ready > 0 && ! readOne())
                break;

            juce::String text, voice;
            juce::File nextFile;
            {
                const juce::ScopedLock sl (lock);
                voice = std::exchange (voices, {});
                if (! queue.empty())
                {
                    text = queue.front();
                    queue.pop_front();
                }
                if (sending == nullptr && ! files.empty())
                {
                    nextFile = files.front();
                    files.pop_front();
                }
            }
            if (voice.isNotEmpty() && ! writeFrame (*socket, textFrame, voice.toRawUTF8(), voice.getNumBytesAsUTF8()))
                break;
            if (text.isNotEmpty() && ! writeFrame (*socket, textFrame, text.toRawUTF8(), text.getNumBytesAsUTF8()))
                break;
            if (nextFile.existsAsFile())
            {
                sending = std::make_unique<juce::FileInputStream> (nextFile);
                sendLeft = sending->openedOk() ? nextFile.getSize() : 0;
                if (! sending->openedOk())
                    sending = nullptr;
                else
                {
                    const auto head = juce::JSON::toString (juce::var (makeHead (nextFile)), true);
                    if (! writeFrame (*socket, fileStart, head.toRawUTF8(), head.getNumBytesAsUTF8()))
                        break;
                }
            }
            if (sending != nullptr)
            {
                const int n = sending->read (chunk.getData(), (int) std::min<juce::int64> (chunkSize, sendLeft));
                if (n <= 0 || ! writeFrame (*socket, fileData, chunk.getData(), (size_t) n))
                    break;
                sendLeft -= n;
                if (sendLeft <= 0)
                    sending = nullptr;
            }
        }
        finish();
    }

    juce::DynamicObject* makeHead (const juce::File& f)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", f.getFileName());
        o->setProperty ("size", f.getSize());
        return o;
    }

    bool readOne()
    {
        juce::uint8 type = 0;
        juce::MemoryBlock payload;
        if (! readFrame (*socket, type, payload))
            return false;
        if (type == textFrame)
        {
            post ([text = payload.toString()] (BackupLink& o) { o.received (text); });
        }
        else if (type == fileStart)
        {
            const auto head = juce::JSON::parse (payload.toString());
            incoming = nullptr;
            incomingLeft = (juce::int64) head.getProperty ("size", 0);
            // the name is only ever a name: createLegalFileName takes out any path
            target = folder.getChildFile (juce::String (incomingLeft) + "_" + juce::File::createLegalFileName (head.getProperty ("name", {}).toString()));
            partFile = target.withFileExtension ("part");
            partFile.deleteFile();
            if (folder.createDirectory().wasOk())
                incoming = std::make_unique<juce::FileOutputStream> (partFile);
        }
        else if (type == fileData && incoming != nullptr)
        {
            incoming->write (payload.getData(), payload.getSize());
            incomingLeft -= (juce::int64) payload.getSize();
            if (incomingLeft <= 0)
            {
                incoming = nullptr;
                partFile.moveFileTo (target);
                post ([] (BackupLink& o) { o.fileArrived(); });
            }
        }
        return true;
    }

    template <typename F>
    void post (F&& f)
    {
        juce::MessageManager::callAsync ([a = alive, o = &owner, f = std::forward<F> (f)]
        {
            if (a->load())
                f (*o);
        });
    }

    juce::String pendingHello()
    {
        const juce::ScopedLock sl (lock);
        return hi;
    }

    void finish()
    {
        up = false;
        post ([c = this] (BackupLink& o) { o.disconnected (c); });
    }

public:
    juce::String hi;     // client side: sent first

private:
    BackupLink& owner;
    std::unique_ptr<juce::StreamingSocket> socket;
    const juce::String host;
    const int port;
    const juce::File folder;
    std::shared_ptr<std::atomic<bool>> alive;
    juce::CriticalSection lock;
    std::deque<juce::String> queue;
    std::deque<juce::File> files;
    juce::String voices;
    std::atomic<bool> up { false };
    std::unique_ptr<juce::FileOutputStream> incoming;
    juce::int64 incomingLeft = 0;
    juce::File target, partFile;
};

// ---- Main laptop: waits for the backup -------------------------------------------------------------------------

class BackupLink::Listener : public juce::Thread
{
public:
    Listener (BackupLink& o, const juce::String& c, int p) : juce::Thread ("Stinger backup listener"), owner (o), code (c), port (p), alive (o.alive) {}

    ~Listener() override
    {
        signalThreadShouldExit();
        server.close();
        stopThread (3000);
    }

    bool open() { return server.createListener (port, {}); }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            std::unique_ptr<juce::StreamingSocket> client (server.waitForNextConnection());
            if (client == nullptr)
            {
                if (threadShouldExit())
                    return;
                wait (50);
                continue;
            }
            // the first frame has to arrive soon and carry the code
            juce::uint8 type = 0;
            juce::MemoryBlock payload;
            if (client->waitUntilReady (true, 3000) != 1 || ! Connection::readFrame (*client, type, payload) || type != textFrame)
                continue;
            const auto hello = juce::JSON::parse (payload.toString());
            if (hello.getProperty ("t", {}).toString() != "hello" || hello.getProperty ("code", {}).toString() != code)
                continue;
            juce::MessageManager::callAsync ([a = alive, o = &owner, raw = client.release()]
            {
                std::unique_ptr<juce::StreamingSocket> s (raw);
                if (a->load())
                    o->connectionUp (std::make_unique<Connection> (*o, std::move (s), juce::String(), 0, juce::File()));
            });
        }
    }

    BackupLink& owner;
    const juce::String code;
    const int port;
    std::shared_ptr<std::atomic<bool>> alive;
    juce::StreamingSocket server;
};

// -----------------------------------------------------------------------------------------------------------------

BackupLink::BackupLink (ShowController& c) : controller (c), engine (c.getEngine())
{
}

BackupLink::~BackupLink()
{
    alive->store (false);
    stop();
}

juce::String BackupLink::newCode()
{
    return juce::Uuid().toString().substring (0, 8).toUpperCase();
}

bool BackupLink::isConnected() const
{
    return connection != nullptr && connection->isUp();
}

bool BackupLink::startMain (const juce::String& c, int p)
{
    stop();
    code = c;
    port = p;
    listener = std::make_unique<Listener> (*this, code, port);
    if (! listener->open())
    {
        listener = nullptr;
        return false;
    }
    listener->startThread();
    role = Role::main;
    lastShow = lastVoices = lastPosition = {};
    startTimer (25);
    sendChangeMessage();
    return true;
}

void BackupLink::startBackup (const juce::String& h, const juce::String& c, Mode m, const juce::File& f, int p)
{
    stop();
    host = h;
    code = c;
    mode = m;
    folder = f;
    port = p;
    role = Role::backup;
    following = true;
    mainLost = false;
    synced = false;
    filesLeft = 0;
    lastHeard = juce::Time::getMillisecondCounter();
    lastAttempt = 0;
    controller.newShow();    // what comes across replaces it: Save must not write the copy over the show that was open
    controller.setMirroring (true);
    engine.setMuted (mode == Mode::warm);
    startTimer (25);
    sendChangeMessage();
}

void BackupLink::stop()
{
    stopTimer();
    listener = nullptr;
    connection = nullptr;
    if (role == Role::backup)
    {
        controller.setMirroring (false);
        engine.setMuted (false);
    }
    role = Role::off;
    following = mainLost = false;
    sendChangeMessage();
}

void BackupLink::takeOver()
{
    if (role != Role::backup)
        return;
    following = false;
    stopTimer();
    connection = nullptr;
    controller.setMirroring (false);
    engine.setMuted (false);
    mainLost = false;
    sendChangeMessage();
}

juce::String BackupLink::getStatus() const
{
    if (role == Role::main)
        return isConnected() ? "Backup connected" : "Waiting for a backup";
    if (role == Role::backup)
    {
        if (! following) return "Taken over: this laptop is running the show";
        if (mainLost)    return "MAIN LAPTOP LOST";
        if (! isConnected()) return "Connecting to the main laptop...";
        if (filesLeft > 0)   return "Copying audio: " + juce::String (filesLeft) + (filesLeft == 1 ? " file left" : " files left");
        if (! synced)        return "Waiting for the show...";
        return mode == Mode::warm ? "Following silently (warm standby)" : "Following, sound on (hot standby)";
    }
    return "Off";
}

void BackupLink::connectionUp (std::unique_ptr<Connection> c)
{
    connection = std::move (c);      // a new backup replaces an old one
    lastShow = lastVoices = lastPosition = {};
    connection->startThread();
    sendChangeMessage();
}

void BackupLink::disconnected (Connection* c)
{
    if (c != connection.get())
        return;
    if (role == Role::backup)
        lastAttempt = juce::Time::getMillisecondCounter();
    connection = nullptr;
    sendChangeMessage();
}

void BackupLink::timerCallback()
{
    if (role == Role::main)
        tickMain();
    else if (role == Role::backup)
        tickBackup();
}

void BackupLink::tickMain()
{
    if (! isConnected())
        return;

    juce::Array<juce::var> list;
    for (auto& v : engine.getVoices())
        if (! v.preview && ! v.waiting)
            list.add (juce::Array<juce::var> { v.cueId, v.position + (v.stopping ? 0.0 : linkDelay), v.looping, v.stopping });
    const auto voices = juce::JSON::toString (juce::var (list), true);
    const auto now = juce::Time::getMillisecondCounter();
    if (voices != lastVoices || now - lastVoicesSent >= 250)      // the heartbeat, too
    {
        lastVoices = voices;
        lastVoicesSent = now;
        connection->sendVoices ("{\"t\":\"v\",\"v\":" + voices + "}");
    }

    if (++ticks % 20 == 0)
    {
        const auto position = juce::JSON::toString (controller.positionVar(), true);
        if (position != lastPosition)
        {
            lastPosition = position;
            connection->send ("{\"t\":\"pos\",\"pos\":" + position + "}");
        }
    }
    if (ticks % 40 == 0)
    {
        if (engine.getVoices().empty())
            controller.storePluginStates();   // never while playing: a plugin can glitch if asked for them while it runs
        const auto show = juce::JSON::toString (controller.getShow().toVar(), true);
        if (show != lastShow)
        {
            lastShow = show;
            manifest.clear();
            juce::Array<juce::var> files;
            std::set<juce::String> seen;
            const auto& s = controller.getShow();
            for (auto& bank : s.banks)
                for (auto& cue : bank.cues)
                    if (cue.file.existsAsFile() && seen.insert (cue.file.getFullPathName()).second)
                    {
                        auto* o = new juce::DynamicObject();
                        o->setProperty ("i", (int) manifest.size());
                        o->setProperty ("path", cue.file.getFullPathName());
                        o->setProperty ("name", cue.file.getFileName());
                        o->setProperty ("size", cue.file.getSize());
                        files.add (o);
                        manifest.push_back (cue.file);
                    }
            connection->send ("{\"t\":\"show\",\"show\":" + show + ",\"files\":" + juce::JSON::toString (juce::var (files), true) + "}");
        }
    }
}

void BackupLink::tickBackup()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (connection == nullptr)
    {
        if (now - lastAttempt >= retryMs)
        {
            lastAttempt = now;
            connection = std::make_unique<Connection> (*this, nullptr, host, port, folder);
            auto* hello = new juce::DynamicObject();
            hello->setProperty ("t", "hello");
            hello->setProperty ("code", code);
            connection->hi = juce::JSON::toString (juce::var (hello), true);
            connection->startThread();
        }
    }
    const bool lost = now - lastHeard > lostAfterMs;
    if (lost != mainLost)
    {
        mainLost = lost;
        sendChangeMessage();
    }
}

juce::File BackupLink::localCopy (const juce::String& name, juce::int64 size) const
{
    return folder.getChildFile (juce::String (size) + "_" + juce::File::createLegalFileName (name));
}

void BackupLink::received (const juce::String& text)
{
    juce::var v;
    if (juce::JSON::parse (text, v).failed())
        return;
    const auto type = v.getProperty ("t", {}).toString();
    if (role == Role::main)
    {
        if (type == "need" && connection != nullptr)
            if (auto* wanted = v.getProperty ("files", {}).getArray())
                for (auto& w : *wanted)
                    if (const int i = (int) w; i >= 0 && i < (int) manifest.size())     // only files of the show
                        connection->sendFile (manifest[(size_t) i]);
        return;
    }
    if (role != Role::backup)
        return;

    lastHeard = juce::Time::getMillisecondCounter();
    if (type == "v")
    {
        if (following && ! mainLost)
            follow (v.getProperty ("v", {}));
    }
    else if (type == "pos")
    {
        pendingPosition = v.getProperty ("pos", {});
        if (synced)
            controller.applyPosition (pendingPosition);
    }
    else if (type == "show")
    {
        pendingShow = v.getProperty ("show", {});
        fileMap.clear();
        juce::Array<juce::var> need;
        if (auto* files = v.getProperty ("files", {}).getArray())
            for (auto& f : *files)
            {
                const auto size = (juce::int64) f.getProperty ("size", 0);
                const auto copy = localCopy (f.getProperty ("name", {}).toString(), size);
                fileMap[f.getProperty ("path", {}).toString()] = copy;
                if (! copy.existsAsFile() || copy.getSize() != size)
                    need.add (f.getProperty ("i", -1));
            }
        filesLeft = need.size();
        if (need.isEmpty())
            applyPendingShow();
        else if (connection != nullptr)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("t", "need");
            o->setProperty ("files", need);
            connection->send (juce::JSON::toString (juce::var (o), true));
        }
        sendChangeMessage();
    }
}

void BackupLink::fileArrived()
{
    if (role != Role::backup)
        return;
    lastHeard = juce::Time::getMillisecondCounter();
    if (--filesLeft <= 0)
    {
        filesLeft = 0;
        applyPendingShow();
    }
    sendChangeMessage();
}

void BackupLink::applyPendingShow()
{
    if (pendingShow.isVoid())
        return;
    controller.applyMirror (pendingShow, [this] (const juce::File& f)
    {
        auto it = fileMap.find (f.getFullPathName());
        return it != fileMap.end() ? it->second : juce::File();   // nothing outside the copy folder, ever
    });
    pendingShow = juce::var();
    if (! pendingPosition.isVoid())
        controller.applyPosition (pendingPosition);
    synced = true;
    sendChangeMessage();
}

void BackupLink::follow (const juce::var& voices)
{
    if (! synced)
        return;
    const auto now = juce::Time::getMillisecondCounter();
    const auto local = engine.getVoices();
    const double rate = engine.getSampleRate();
    std::set<int> onMain;

    if (auto* list = voices.getArray())
        for (auto& item : *list)
        {
            auto* fields = item.getArray();
            if (fields == nullptr || fields->size() < 4)
                continue;
            const int id = (int) (*fields)[0];
            const double position = (double) (*fields)[1];
            const bool looping = (bool) (*fields)[2], stopping = (bool) (*fields)[3];
            onMain.insert (id);

            const PlaybackEngine::VoiceInfo* mine = nullptr;
            for (auto& l : local)
                if (l.cueId == id && ! l.preview && ! l.stopping)
                {
                    mine = &l;
                    break;
                }
            if (stopping)
            {
                if (mine != nullptr)
                    engine.stopCue (id, true);
                continue;
            }
            if (mine == nullptr)
            {
                if (auto s = started.find (id); s == started.end() || now - s->second >= restartGuardMs)
                {
                    started[id] = now;
                    controller.playMirrored (id, position, looping);
                }
                continue;
            }
            if (std::abs (mine->position - position) > driftLimit)
                if (auto s = sought.find (id); s == sought.end() || now - s->second >= restartGuardMs)
                {
                    sought[id] = now;
                    engine.seekVoice (mine->voice, (juce::int64) (position * rate));
                }
            if (mine->looping != looping)
                controller.setVoiceLoop (mine->voice, looping);
        }

    for (auto& l : local)
        if (! l.preview && ! l.stopping && onMain.count (l.cueId) == 0)
            if (auto s = started.find (l.cueId); s == started.end() || now - s->second >= restartGuardMs)
                engine.stopCue (l.cueId, true);
}
