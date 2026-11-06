#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "Show.h"

/** Two laptops running one show. The main laptop serves its show, audio and what's playing over the network; the backup
    copies the audio into its own folder and follows along, voice by voice, staying within a few tens of milliseconds.

    Warm standby: the backup follows silently until Take Over. Hot standby: both are audible and the switch is at the mixer.
    Either way the backup keeps playing by itself if the main laptop disappears.

    A shared code keeps other machines out. Everything a backup is told to play comes out of its own copy folder: file paths
    from the main laptop are only ever names to look up there. */
class BackupLink : public juce::ChangeBroadcaster,
                   private juce::Timer
{
public:
    enum class Role { off, main, backup };
    enum class Mode { warm, hot };
    static constexpr int defaultPort = 52819;

    explicit BackupLink (ShowController& controller);
    ~BackupLink() override;

    /** This laptop is the main one: waits for a backup that knows the code. False if the port can't be opened. */
    bool startMain (const juce::String& code, int port = defaultPort);
    /** This laptop is the backup: connects to the main one (and keeps trying). Audio is copied into folder. */
    void startBackup (const juce::String& host, const juce::String& code, Mode mode, const juce::File& folder, int port = defaultPort);
    void stop();

    Role getRole() const { return role; }
    Mode getMode() const { return mode; }
    bool isConnected() const;
    /** Backup: still following the main laptop (not taken over). */
    bool isFollowing() const { return role == Role::backup && following; }
    /** Backup: the link to the main laptop has gone quiet. */
    bool isMainLost() const { return mainLost; }
    /** Backup: the show has arrived and all its audio with it. */
    bool isSynced() const { return synced; }
    int getFilesLeft() const { return filesLeft; }
    /** Backup: stop following and run the show from here, sound on. */
    void takeOver();
    juce::String getStatus() const;

    static juce::String newCode();

private:
    class Connection;
    class Listener;
    friend class Connection;
    friend class Listener;

    void timerCallback() override;
    void tickMain();
    void tickBackup();
    void connectionUp (std::unique_ptr<Connection> c);
    void received (const juce::String& text);
    void disconnected (Connection* c);
    void fileArrived();
    void applyPendingShow();
    void follow (const juce::var& voices);
    juce::File localCopy (const juce::String& name, juce::int64 size) const;

    ShowController& controller;
    PlaybackEngine& engine;
    Role role = Role::off;
    Mode mode = Mode::warm;
    bool following = false, mainLost = false, synced = false;
    int filesLeft = 0, port = defaultPort;
    juce::String host, code;
    juce::File folder;
    std::unique_ptr<Listener> listener;
    std::unique_ptr<Connection> connection;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    // main
    int ticks = 0;
    juce::String lastVoices, lastPosition, lastShow;
    std::vector<juce::File> manifest;
    juce::uint32 lastVoicesSent = 0;

    // backup
    juce::var pendingShow, pendingPosition;
    std::map<juce::String, juce::File> fileMap;     // main laptop's path -> the copy here
    juce::uint32 lastHeard = 0, lastAttempt = 0;
    std::map<int, juce::uint32> started, sought;
};
