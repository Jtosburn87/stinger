#pragma once
#include "Cue.h"
#include "CueAudio.h"
#include "PlaybackEngine.h"
#include <juce_events/juce_events.h>
#include <map>
#include <memory>
#include <set>

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

class ShowController : public juce::ChangeBroadcaster,
                       private juce::Timer
{
public:
    explicit ShowController (PlaybackEngine& engine);
    ~ShowController() override;

    Show& getShow() { return show; }
    PlaybackEngine& getEngine() { return engine; }
    AudioLoader& getLoader() { return loader; }

    /** Puts a file on a button (keeps the button's other settings; resets edits if the file changes). */
    void assignFile (int id, const juce::File& file);
    void clearCue (int id);
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

    std::shared_ptr<CueAudio> getAudio (int id) const;
    bool isLoading (int id) const { return loading.count (id) > 0; }
    juce::String getError (int id) const;

    // Show file
    void newShow();
    bool openShow (const juce::File& f, juce::String& error);
    bool saveShow (const juce::File& f, juce::String& error);
    juce::File getShowFile() const { return showFile; }
    bool hasUnsavedChanges() const { return dirty; }

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
    int getPlaylistIndex() const { return playlistIndex; }
    void setPlaylistIndex (int index);
    Playlist* currentPlaylist();
    int addPlaylist (const juce::String& name);
    void removePlaylist (int index);

    void addToPlaylist (int cueId, int insertAt = -1);
    void removeEntry (int index);
    void moveEntry (int from, int to);
    void setTransition (int index, Transition t, double crossfadeSeconds);
    /** After changing a playlist directly (its name): marks the show changed. */
    void playlistEdited() { dirty = true; sendChangeMessage(); }
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

    /** The entry GO plays (index in the current playlist, -1 = none). */
    int getNextEntry() const;
    void setNextEntry (int index);
    /** The entry playing most recently started from the playlist, -1 = none. */
    int getOnAirEntry() const;

    /** GO: if a playlist song is looping until GO, it finishes its pass and the next song follows with no gap.
        Otherwise plays the next entry; playlist songs still playing fade out (over the crossfade time if their
        transition is a crossfade, else their stop fade). */
    void go();
    /** Fade to next: crossfades from what the playlist is playing into the next entry now, over the
        playing entry's crossfade time. */
    void fadeToNext();
    void playEntry (int index, double fadeInSeconds = 0.0);
    /** True while a playlist song loops until GO. */
    bool isLoopingUntilGo() const;

    /** Call often (the controller's timer does): follows the playlist as songs start, queues the next links. */
    void updatePlaylist();

private:
    int indexOfUid (int uid) const;
    int newUid() { return ++lastUid; }
    void queueFollow (int tag, int uid);
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
    void attachFx (PlayPlan& plan) const;
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
    bool dirty = false;

    int playlistIndex = 0;
    int nextUid = 0, onAirUid = 0;          // entries by uid (0 = none)
    int lastUid = 0, lastTag = 0;
    std::map<int, int> tagUid;              // voice tag -> the entry it plays
    std::set<int> chained;                  // tags whose next song has been queued in the engine
    std::set<int> seen;                     // tags already seen playing
};
