#pragma once
#include "Cue.h"
#include "CueAudio.h"
#include "PlaybackEngine.h"
#include "ShowCheck.h"
#include <juce_events/juce_events.h>
#include <optional>
#include <map>
#include <memory>
#include <set>

/** "5:00", "1:02:30". */
juce::String durationText (double seconds);

/** What happens when a playlist song ends. */
enum class Transition { stop = 0, playNext, crossfade, loopUntil, wait };

struct PlaylistEntry
{
    int cueId = -1;                         // the button it plays (its file, trims, output, level...)
    Transition transition = Transition::stop;
    double crossfadeSeconds = 1.0;         // crossfade time; for Loop until GO: how long Fade to next takes
    double waitSeconds = 5.0;              // Wait: silence after the song before the next one starts
    int uid = 0;                            // runtime only: stays the same while entries move around

    juce::String transitionName() const;
};

struct Playlist
{
    juce::String name;
    std::vector<PlaylistEntry> entries;
    // runtime only
    int uid = 0;                            // stays the same while playlists come and go
    int nextUid = 0, onAirUid = 0;          // its entries GO plays next / that's playing (0 = none)

    juce::String displayName (int index) const { return name.isEmpty() ? "Playlist " + juce::String (index + 1) : name; }
};


/** One line of the run of show: a person and song, or just a note (cueId -1). */
struct RunRow
{
    int cueId = -1;                 // the button GO plays, -1 = a note: GO just moves on
    juce::String time, person, notes;
    int uid = 0;                    // runtime only

    juce::var toVar() const;
    static RunRow fromVar (const juce::var& v);
};

/** A button that starts by itself: at a time of day, or a delay after the schedule's Start. */
struct ScheduleItem
{
    enum Kind { clock = 0, delay };
    enum State { waiting = 0, fired, missed, defeated };   // defeated: GO pressed during a hold

    int cueId = -1;
    Kind kind = clock;
    int secondsOfDay = 0;           // clock
    double delaySeconds = 60.0;     // delay
    // runtime only
    int uid = 0;
    State state = waiting;
    juce::int64 fireAt = 0;         // ms since 1970
    bool held = false;
    double heldSeconds = 0.0;

    juce::var toVar() const;
    static ScheduleItem fromVar (const juce::var& v);
};
/** A show file: banks of buttons and playlists. Saved as JSON (*.stinger); audio files are referenced,
    never copied or changed. */
struct Show
{
    static constexpr int columns = 6, rows = 4, cuesPerBank = columns * rows;

    struct Bank
    {
        juce::String name;
        std::array<Cue, cuesPerBank> cues;
    };

    std::vector<Bank> banks;
    std::vector<Playlist> playlists;
    std::vector<RunRow> run;
    std::vector<ScheduleItem> schedule;
    PlaybackMode playbackMode = PlaybackMode::oneAtATime;
    bool selfDestruct = false;   // buttons go black after they've played

    Show() { reset(); }
    void reset();

    static int cueId (int bank, int slot) { return bank * cuesPerBank + slot; }
    static int bankOf (int id) { return id / cuesPerBank; }
    static int slotOf (int id) { return id % cuesPerBank; }

    bool isValid (int id) const { return id >= 0 && bankOf (id) < (int) banks.size(); }
    Cue& cue (int id) { return banks[(size_t) bankOf (id)].cues[(size_t) slotOf (id)]; }
    const Cue& cue (int id) const { return banks[(size_t) bankOf (id)].cues[(size_t) slotOf (id)]; }

    juce::var toVar() const;
    bool fromVar (const juce::var& v);
    bool save (const juce::File& f, juce::String& error) const;
    bool load (const juce::File& f, juce::String& error);
};

/** The show plus its loaded audio, on the message thread: loads files, starts and stops cues,
    and keeps audio alive while the engine still plays it. Broadcasts a change when anything changes. */
class PluginHost;
class Autosave;

class ShowController : public juce::ChangeBroadcaster,
                       private juce::Timer
{
public:
    explicit ShowController (PlaybackEngine& engine);
    ~ShowController() override;

    Show& getShow() { return show; }
    PlaybackEngine& getEngine() { return engine; }
    AudioLoader& getLoader() { return loader; }

    /** Puts a file on a button (keeps the button's other settings; resets edits if the file changes).
        A file from outside the show's Audio folder is copied into it first. */
    void assignFile (int id, const juce::File& file);
    void clearCue (int id);
    /** Puts a button back as it was (undo / redo of a recording): file, name, edits and settings. The plugins
        stay as they are now. */
    void restoreCue (int id, const Cue& c);
    /** Two buttons change places (everything goes with the song: settings, keys, plugins, playlist entries). */
    void swapCues (int a, int b);
    /** The first empty button at or after id (on through the later banks), -1 if none. */
    int nextEmptyCue (int id) const;
    /** Puts audio files onto empty buttons from startId on, in name order (natural: "2" before "10"),
        skipping buttons in use and spilling into the next banks (adding banks up to maxBanks).
        Returns how many found a button. */
    int assignFiles (juce::Array<juce::File> files, int startId);
    static constexpr int maxBanks = 8;
    /** A new button playing only this section (file seconds) of the button's song, on the next empty button.
        Same file, output, level, colour and plugins; no keys. Returns its id, -1 if there's no empty button. */
    int splitToNewButton (int id, juce::Range<double> section, const juce::String& name);
    /** Call after changing a cue's settings: saves state, updates a playing cue's level / pan. */
    void cueChanged (int id);

    /** Button press: plays the cue, or fades it out if it's already playing. */
    void trigger (int id);
    void play (int id);
    /** Now Playing's Loop button. On: a song with loop points playing normally carries on by them from where it is
        (Loop Out jumps to Loop In; past Loop Out: the Out point goes to Loop In). Off: it plays on to the Out point. */
    void setVoiceLoop (int voice, bool loop);

    std::shared_ptr<CueAudio> getAudio (int id) const;
    bool isLoading (int id) const { return loading.count (id) > 0; }

    /** Asked after a sample rate change, once the audio is back: true = the device was lost, so what was playing
        carries on where it would have been. Otherwise (a rate picked by hand) it stays stopped. */
    std::function<bool()> resumeAfterRateChange;
    juce::String getError (int id) const;

    // Show file
    void newShow();
    bool openShow (const juce::File& f, juce::String& error);
    /** Saves (making the show's folder). The first save of a new show moves its audio from Unsaved\Audio
        into the show's own Audio folder. */
    bool saveShow (const juce::File& f, juce::String& error);
    juce::File getShowFile() const { return showFile; }

    // Folders: Documents\Stinger\<show>\<show>.stinger + Audio\ (recordings, downloads, imported files)
    juce::File getHomeFolder() const { return homeFolder; }
    void setHomeFolderForTest (const juce::File& f) { homeFolder = f; }
    /** Where a show called this is saved: Documents\Stinger\<name>\<name>.stinger. */
    juce::File showFileFor (const juce::String& name) const;
    /** The show's Audio folder (next to the show file), or Documents\Stinger\Unsaved\Audio before it's saved. */
    juce::File getAudioFolder() const;
    /** Copies a file into the Audio folder (unless it's there already; same name and size = already copied).
        Returns the copy, or the file itself if it couldn't be copied. */
    juce::File importFile (const juce::File& file);
    bool hasUnsavedChanges() const { return dirty; }

    /** Everything that would go wrong in the show as it stands: audio that is missing or unreadable, plugins that
        are not loaded, outputs this device lacks, playlist songs whose button is empty. */
    std::vector<ShowIssue> checkShow() const;
    /** Points every button using `from` at `to`, keeping its trims, cuts and settings, and loads it. */
    void relinkFile (juce::File from, const juce::File& to);

    // ---- Autosave + crash recovery ----
    /** Where recovery copies go (null = no autosaving). Must outlive this controller or be cleared first. */
    void setAutosave (Autosave* a) { autosave = a; }
    /** Opens a recovery copy: the show as it was (black Self Destruct buttons, Next positions) under its own name, unsaved. */
    bool openRecovery (const juce::File& recoveryFile, juce::String& error);
    /** Call often (the controller's timer does): every autosaveMs while there are unsaved changes, at once when a
        button goes black. Plugin settings are read only when nothing is playing (a plugin can glitch if asked for
        them while it runs); the rest is a copy in memory, written by Autosave's own thread. */
    void updateAutosave();

    // ---- Backup laptop ----
    /** Where the show stands: selected playlist, Next positions, run of show, schedule. */
    juce::var positionVar() const;
    void applyPosition (const juce::var& position);
    /** While on, this laptop follows another: its schedule, Self Destruct and autosave leave everything to the main one. */
    void setMirroring (bool on);
    bool isMirroring() const { return mirroring; }
    /** Takes on a show sent from the main laptop, reloading only what changed. Every file path goes through localFile. */
    void applyMirror (const juce::var& incoming, const std::function<juce::File (const juce::File&)>& localFile);
    /** Starts a button from a place in it, leaving whatever else plays alone. */
    void playMirrored (int id, double position, bool looping);
    void setAutosaveIntervalForTest (juce::uint32 ms) { autosaveMs = ms; }

    /** For tests: load a cue's audio now, on this thread. */
    bool loadNow (int id, juce::String& error);
    void setAudioForTest (int id, std::shared_ptr<CueAudio> audio) { media[id] = std::move (audio); }

    // ---- Plugins (3 slots per button) ----
    void setPluginHost (PluginHost* h) { host = h; }
    /** Called just before a plugin is deleted (its editor window must close first). */
    std::function<void (juce::AudioProcessor*)> onPluginGoing;
    PluginHost* getPluginHost() const { return host; }
    /** The button's running plugins, null if it has none. */
    CueFx* getFx (int id) const;
    /** Gives a play its button's plugins (every play of a button goes through this). */
    void attachFx (PlayPlan& plan) const;
    bool setPlugin (int id, int slot, const juce::PluginDescription& description, juce::String& error);
    void removePlugin (int id, int slot);
    void setPluginBypassed (int id, int slot, bool bypassed);
    /** Copies the running plugins' settings into the show (done before saving). */
    void storePluginStates();
    /** Plugins a show asked for that aren't on this PC (from the last open). */
    juce::StringArray getMissingPlugins() const { return missingPlugins; }
    /** For tests: a processor straight into a slot. */
    void setPluginForTest (int id, int slot, std::unique_ptr<juce::AudioProcessor> p);

    // ---- Playlists ----
    // Every playlist has its own Next / On air and its own GO. The selected one (the box clicked last) is the one
    // Space, Enter and the plain GO / Fade to next commands play.
    int getSelectedPlaylist() const { return selectedPlaylist; }
    void selectPlaylist (int index);
    Playlist* playlistAt (int index);
    Playlist* currentPlaylist() { return playlistAt (selectedPlaylist); }
    /** Index of the playlist with this uid (Playlist::uid), -1 if it has gone. */
    int playlistIndexOf (int playlistUid) const;
    /** Index of the playlist a voice (VoiceInfo::tag) was started for, -1 = none. */
    int playlistOfTag (int tag) const;
    int addPlaylist (const juce::String& name);
    void removePlaylist (int index);
    /** An empty name goes back to "Playlist N". */
    void renamePlaylist (int index, const juce::String& name);

    void addToPlaylist (int playlist, int cueId, int insertAt = -1);
    void removeEntry (int playlist, int index);
    void moveEntry (int playlist, int from, int to);
    void setTransition (int playlist, int index, Transition t, double crossfadeSeconds);
    void setPlaybackMode (PlaybackMode mode)
    {
        if (mode == show.playbackMode)
            return;
        show.playbackMode = mode;
        dirty = true;
        sendChangeMessage();
    }
    /** Self Destruct: while on, each button goes black once it has played (ended or stopped). Saved in the show. */
    void setSelfDestruct (bool on);
    bool isSelfDestruct() const { return show.selfDestruct; }
    /** Gives every black (spent) button its colour back. */
    void resetColours();
    void resetColour (int id);
    bool anySpent() const;
    /** Renames a bank (the tab at the top; an empty name keeps the old one). */
    void renameBank (int bank, const juce::String& name)
    {
        const auto trimmed = name.trim();
        if (bank < 0 || bank >= (int) show.banks.size() || trimmed.isEmpty() || trimmed == show.banks[(size_t) bank].name)
            return;
        show.banks[(size_t) bank].name = trimmed;
        dirty = true;
        sendChangeMessage();
    }

    /** The entry GO plays (index in the playlist, -1 = none). */
    int getNextEntry (int playlist) const;
    void setNextEntry (int playlist, int index);
    /** The entry playing most recently started from the playlist, -1 = none. */
    int getOnAirEntry (int playlist) const;

    /** GO: if one of the playlist's songs is looping until GO, it finishes its pass and the next song follows
        with no gap. Otherwise plays the next entry; the playlist's songs still playing fade out (over the crossfade
        time if their transition is a crossfade, else their stop fade). Other playlists' songs go by the show's
        playback mode, as they do for a button. */
    void go (int playlist);
    /** Fade to next: crossfades from what the playlist is playing into its next entry now, over the playing
        entry's crossfade time. */
    void fadeToNext (int playlist);
    void playEntry (int playlist, int index, double fadeInSeconds = 0.0);
    /** True while one of the playlist's songs loops until GO. */
    bool isLoopingUntilGo (int playlist) const;

    /** Call often (the controller's timer does): follows the playlist as songs start, queues the next links. */
    void updatePlaylist();
    /** Call often (the controller's timer does): Self Destruct turns buttons black once they stop. */
    void updateSelfDestruct();

    /** The newest song playing (several at once: the latest started). */
    struct Playing { bool any = false; double left = 0.0; juce::String name; bool looping = false; };
    Playing newestVoice() const;

    // ---- Run of show ----
    int addRunRow (const RunRow& row, int insertAt = -1);
    void removeRunRow (int index);
    void moveRunRow (int from, int to);
    void setRunRow (int index, const RunRow& row);
    int getRunNext() const { return runIndexOf (runNextUid); }
    int getRunOnAir() const { return runIndexOf (runOnAirUid); }
    /** What a row is called on a key: the person, else its button, else "(note)". Empty for no row. */
    juce::String getRunName (int row) const;
    void setRunNext (int index);
    /** Plays the next row's button (a note just moves on) and moves Next down one. */
    void runGo();

    // ---- Schedule ----
    enum class Urgency { green, yellow, red };   // the countdown: red in the last 10 s, yellow in the last minute
    void addScheduleItem (const ScheduleItem& item);
    void removeScheduleItem (int index);
    void setScheduleItem (int index, const ScheduleItem& item);
    /** Starts the countdown. Times already past are marked missed: nothing ever fires late. */
    void startSchedule();
    void stopSchedule();
    bool isScheduleRunning() const { return scheduleRunning; }
    /** The next trigger (index), -1 = none. */
    int getNextScheduled() const;
    double getScheduleRemaining() const;
    Urgency getScheduleUrgency() const;
    juce::String getScheduleName (int index) const;
    int getMissedCount() const;
    /** Pushes the next trigger back by this many minutes (holds add up). */
    void holdSchedule (int minutes);
    /** Fires the next trigger now. */
    void scheduleGoNow();
    /** A GO was pressed: if the next trigger is on a hold, its countdown is defeated (it never fires). */
    void goPressed();
    /** Call often (the controller's timer does). */
    void updateSchedule();
    std::function<juce::int64()> clockForTest;

    // ---- Loudness ----
    /** Targets in LUFS: music, and voice-over (lower: speech sounds louder than a full song at the same reading). */
    void setLoudnessTargets (double music, double voice) { musicTarget = music; voiceTarget = voice; sendChangeMessage(); }
    double getMusicTarget() const { return musicTarget; }
    double getVoiceTarget() const { return voiceTarget; }
    /** The level this button needs to reach its target, if it has been measured. */
    std::optional<float> suggestedLevel (int id) const;
    /** Sets the level to the suggestion. */
    void applyLoudness (int id);
    /** Measures buttons whose audio or trims changed, one at a time in the background (the timer calls this). */
    void updateLoudness();
    bool isMeasuring() const { return measuring; }

private:
    const Playlist* playlistAt (int index) const;
    int indexOfUid (int playlist, int uid) const;
    int newUid() { return ++lastUid; }
    void assignUids();
    void queueFollow (int tag);
    void requeueFollows();
    bool makePlan (const PlaylistEntry& entry, int tag, PlayPlan& plan);
    double fadeSecondsFor (int tag);

    void loadAudio (int id);
    void reloadAll();
    void retire (std::shared_ptr<CueAudio> audio);
    CueFx& fxFor (int id);
    void retireFx (int id);
    void pluginsGoing (CueFx& chain, int onlySlot = -1);
    void createPlugins (int id);
    void timerCallback() override;

    PlaybackEngine& engine;
    Show show;
    AudioLoader loader;
    std::map<int, std::shared_ptr<CueAudio>> media;
    std::map<int, juce::String> errors;
    std::map<int, juce::File> loading;
    std::vector<std::shared_ptr<CueAudio>> retired;
    PluginHost* host = nullptr;
    std::map<int, std::shared_ptr<CueFx>> fx;
    std::vector<std::shared_ptr<CueFx>> retiredFx;
    double fxRate = 0.0;
    int fxBlock = 0;
    juce::StringArray missingPlugins;
    double loadedRate = 0.0;
    juce::File showFile;
    juce::File homeFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Stinger");
    void moveUnsavedAudio (const juce::File& audioFolder);
    std::set<int> destructing;   // buttons seen playing in Self Destruct mode: black once they stop
    bool dirty = false;
    Autosave* autosave = nullptr;
    juce::uint32 autosaveMs = 30000, lastAutosave = 0;
    bool autosaveNow = false;
    bool mirroring = false;
    juce::String savedPosition;
    void markPositionSaved();

    struct KeptVoice { int cueId = -1, tag = 0; double position = 0.0; bool looping = false; };
    std::vector<KeptVoice> resumeVoices;    // what was playing when the rate changed
    juce::uint32 lastVoicesMs = 0;
    bool resumePending = false;
    void resumeKeptVoices();

    // run of show + schedule
    int runIndexOf (int uid) const;
    void assignRunUids();
    juce::int64 nowMs() const;
    void armItem (ScheduleItem& s, juce::int64 now);
    void fireItem (ScheduleItem& s);
    int runNextUid = 0, runOnAirUid = 0;
    bool scheduleRunning = false;

    // loudness
    double musicTarget = -16.0, voiceTarget = -20.0;
    bool measuring = false;
    juce::ThreadPool loudnessPool { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("Loudness") };
    std::shared_ptr<std::atomic<bool>> loudnessAlive = std::make_shared<std::atomic<bool>> (true);
    juce::var recoveryVar() const;

    int selectedPlaylist = 0;
    int lastUid = 0, lastTag = 0;
    std::map<int, int> tagUid;              // voice tag -> the entry it plays
    std::map<int, int> tagPlaylist;         // voice tag -> the playlist (uid) it was started for
    std::set<int> chained;                  // tags whose next song has been queued in the engine
    std::set<int> seen;                     // tags already seen playing
};
