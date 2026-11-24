#include "Show.h"
#include "Autosave.h"
#include "Loudness.h"
#include <optional>

juce::String PlaylistEntry::transitionName() const
{
    switch (transition)
    {
        case Transition::stop:      return "Stop";
        case Transition::playNext:  return "Play next";
        case Transition::crossfade: return "Crossfade " + juce::String (crossfadeSeconds, 1) + " s";
        case Transition::loopUntil: return "Loop until GO";
        case Transition::wait:      return "Wait " + juce::String (waitSeconds, 1) + " s, then next";
    }
    return {};
}

void Show::reset()
{
    banks.clear();
    for (auto name : { "Walk-ups", "Awards", "Breaks", "SFX" })
    {
        Bank b;
        b.name = name;
        banks.push_back (b);
    }
    playlists.clear();
    playlists.push_back ({ "Playlist 1", {} });
    songs.clear();
    run.clear();
    schedule.clear();
    playbackMode = PlaybackMode::oneAtATime;
    selfDestruct = false;
}

juce::var Show::toVar() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "Stinger show");
    root->setProperty ("version", 1);
    root->setProperty ("playbackMode", (int) playbackMode);
    root->setProperty ("selfDestruct", selfDestruct);
    juce::Array<juce::var> bankList;
    for (auto& b : banks)
    {
        auto* bank = new juce::DynamicObject();
        bank->setProperty ("name", b.name);
        juce::Array<juce::var> cues;
        for (auto& c : b.cues)
            cues.add (c.isEmpty() && c.name.isEmpty() ? juce::var() : c.toVar());
        bank->setProperty ("cues", cues);
        bankList.add (bank);
    }
    root->setProperty ("banks", bankList);
    if (! songs.empty())
    {
        juce::Array<juce::var> songList;   // index = place (an empty one is null)
        for (auto& c : songs)
            songList.add (c.isEmpty() ? juce::var() : c.toVar());
        root->setProperty ("songs", songList);
    }
    juce::Array<juce::var> playlistList;
    for (auto& p : playlists)
    {
        auto* list = new juce::DynamicObject();
        list->setProperty ("name", p.name);
        if (p.preshow)
            list->setProperty ("preshow", true);
        juce::Array<juce::var> entries;
        for (auto& e : p.entries)
        {
            auto* entry = new juce::DynamicObject();
            entry->setProperty ("cue", e.cueId);
            entry->setProperty ("transition", (int) e.transition);
            entry->setProperty ("crossfade", e.crossfadeSeconds);
            entry->setProperty ("wait", e.waitSeconds);
            entries.add (entry);
        }
        list->setProperty ("entries", entries);
        playlistList.add (list);
    }
    root->setProperty ("playlists", playlistList);
    juce::Array<juce::var> runList, scheduleList;
    for (auto& r : run)
        runList.add (r.toVar());
    for (auto& s : schedule)
        scheduleList.add (s.toVar());
    root->setProperty ("run", runList);
    root->setProperty ("schedule", scheduleList);
    return root;
}

bool Show::fromVar (const juce::var& v)
{
    if (v.getProperty ("format", {}).toString() != "Stinger show")
        return false;
    playbackMode = (PlaybackMode) juce::jlimit (0, 2, (int) v.getProperty ("playbackMode", 0));
    selfDestruct = (bool) v.getProperty ("selfDestruct", false);
    banks.clear();
    if (auto* list = v.getProperty ("banks", {}).getArray())
        for (auto& bv : *list)
        {
            Bank b;
            b.name = bv.getProperty ("name", {}).toString();
            if (auto* cues = bv.getProperty ("cues", {}).getArray())
                for (int i = 0; i < std::min (cuesPerBank, cues->size()); ++i)
                    b.cues[(size_t) i] = Cue::fromVar ((*cues)[i]);
            banks.push_back (b);
        }
    songs.clear();
    if (auto* list = v.getProperty ("songs", {}).getArray())
        for (auto& sv : *list)
            songs.push_back (Cue::fromVar (sv));
    playlists.clear();
    int uid = 0;
    if (auto* list = v.getProperty ("playlists", {}).getArray())
        for (auto& pv : *list)
        {
            Playlist p;
            p.name = pv.getProperty ("name", {}).toString();
            p.preshow = (bool) pv.getProperty ("preshow", false);
            if (auto* entries = pv.getProperty ("entries", {}).getArray())
                for (auto& ev : *entries)
                {
                    PlaylistEntry e;
                    e.cueId = (int) ev.getProperty ("cue", -1);
                    e.transition = (Transition) juce::jlimit (0, 4, (int) ev.getProperty ("transition", 0));
                    e.crossfadeSeconds = juce::jlimit (0.1, 60.0, (double) ev.getProperty ("crossfade", 1.0));
                    e.waitSeconds = juce::jlimit (0.1, 600.0, (double) ev.getProperty ("wait", 5.0));
                    e.uid = ++uid;
                    if (e.cueId >= 0)
                        p.entries.push_back (e);
                }
            playlists.push_back (p);
        }
    run.clear();
    if (auto* list = v.getProperty ("run", {}).getArray())
        for (auto& r : *list)
            run.push_back (RunRow::fromVar (r));
    schedule.clear();
    if (auto* list = v.getProperty ("schedule", {}).getArray())
        for (auto& s : *list)
            schedule.push_back (ScheduleItem::fromVar (s));
    if (banks.empty())
        reset();
    if (playlists.empty())
        playlists.push_back ({ "Playlist 1", {} });
    return true;
}

/** Each cue's path relative to the show's folder is saved too ("fileRel", forward slashes), so a show
    folder copied to another drive or computer (Windows <-> Mac) still finds its songs. */
static void forEachCueVar (const juce::var& v, std::function<void (const juce::var& cue, int bank, int index)> f)
{
    if (auto* list = v.getProperty ("banks", {}).getArray())
        for (int b = 0; b < list->size(); ++b)
            if (auto* cues = (*list)[b].getProperty ("cues", {}).getArray())
                for (int i = 0; i < cues->size(); ++i)
                    if ((*cues)[i].isObject())
                        f ((*cues)[i], b, i);
    if (auto* songs = v.getProperty ("songs", {}).getArray())   // playlist songs: bank -1
        for (int i = 0; i < songs->size(); ++i)
            if ((*songs)[i].isObject())
                f ((*songs)[i], -1, i);
}

std::vector<int> Show::allCueIds() const
{
    std::vector<int> ids;
    for (int b = 0; b < (int) banks.size(); ++b)
        for (int s = 0; s < cuesPerBank; ++s)
            ids.push_back (cueId (b, s));
    for (int i = 0; i < (int) songs.size(); ++i)
        ids.push_back (songBase + i);
    return ids;
}

bool Show::save (const juce::File& f, juce::String& error) const
{
    const auto v = toVar();
    const auto folder = f.getParentDirectory();
    forEachCueVar (v, [&folder] (const juce::var& cue, int, int)
    {
        const auto path = cue.getProperty ("file", {}).toString();
        if (path.isNotEmpty() && juce::File (path).isAChildOf (folder))
            if (auto* o = cue.getDynamicObject())
                o->setProperty ("fileRel", juce::File (path).getRelativePathFrom (folder).replaceCharacter ('\\', '/'));
    });
    const auto text = juce::JSON::toString (v);
    juce::TemporaryFile temp (f);
    if (! temp.getFile().replaceWithText (text) || ! temp.overwriteTargetFileWithTemporary())
    {
        error = "Couldn't write " + f.getFullPathName();
        return false;
    }
    return true;
}

bool Show::load (const juce::File& f, juce::String& error)
{
    juce::var v;
    const auto result = juce::JSON::parse (f.loadFileAsString(), v);
    if (result.failed() || ! fromVar (v))
    {
        error = f.getFileName() + " isn't a Stinger show.";
        return false;
    }
    const auto folder = f.getParentDirectory();
    forEachCueVar (v, [this, &folder] (const juce::var& cue, int b, int i)
    {
        const auto rel = cue.getProperty ("fileRel", {}).toString();
        if (rel.isEmpty() || b >= (int) banks.size() || i >= (b < 0 ? (int) songs.size() : cuesPerBank))
            return;
        auto& c = b < 0 ? songs[(size_t) i] : banks[(size_t) b].cues[(size_t) i];
        if (! c.file.existsAsFile())
            if (auto moved = folder.getChildFile (rel); moved.existsAsFile())
                c.file = moved;
    });
    return true;
}

// ---------------------------------------------------------------------------------------------

ShowController::ShowController (PlaybackEngine& e) : engine (e)
{
    loadedRate = engine.getSampleRate();
    assignUids();
    assignRunUids();
    markPositionSaved();
    startTimerHz (10);
}

ShowController::~ShowController()
{
    stopTimer();
    loudnessAlive->store (false);
    loudnessPool.removeAllJobs (true, 10000);
}

juce::File ShowController::showFileFor (const juce::String& name) const
{
    auto legal = juce::File::createLegalFileName (name.trim());
    if (legal.isEmpty())
        legal = "Show";
    return homeFolder.getChildFile (legal).getChildFile (legal + ".stinger");
}

juce::File ShowController::getAudioFolder() const
{
    if (showFile != juce::File())
        return showFile.getSiblingFile ("Audio");
    return homeFolder.getChildFile ("Unsaved").getChildFile ("Audio");
}

juce::File ShowController::importFile (const juce::File& file)
{
    const auto folder = getAudioFolder();
    if (! file.existsAsFile() || file.isAChildOf (folder))
        return file;
    auto target = folder.getChildFile (file.getFileName());
    if (target.existsAsFile() && target.getSize() == file.getSize())
        return target;   // copied before (the same file onto another button)
    if (! folder.createDirectory())
        return file;
    if (target.exists())
        target = target.getNonexistentSibling();
    return file.copyFileTo (target) ? target : file;
}

void ShowController::moveUnsavedAudio (const juce::File& audioFolder)
{
    const auto unsaved = getAudioFolder();
    std::map<juce::String, juce::File> moved;   // old path -> new (buttons can share a file)
    for (const int id : show.allCueIds())
    {
        auto& c = show.cue (id);
        if (! c.file.isAChildOf (unsaved))
            continue;
        const auto key = c.file.getFullPathName();
        if (moved.count (key) == 0)
        {
            auto target = audioFolder.getChildFile (c.file.getFileName());
            if (target.exists())
                target = target.getNonexistentSibling();
            moved[key] = audioFolder.createDirectory() && c.file.moveFileTo (target) ? target : c.file;
        }
        c.file = moved[key];
        if (loading.count (id) > 0)
            loadAudio (id);   // a load on its way was for the old place
    }
}

void ShowController::assignFile (int id, const juce::File& original)
{
    if (! show.isValid (id))
        return;
    const auto file = importFile (original);
    auto& c = show.cue (id);
    if (c.file != file)
    {
        const auto keep = c;
        c = Cue();
        c.colour = keep.colour;
        c.output = keep.output;
        c.levelDb = keep.levelDb;
        c.hotkey = keep.hotkey;
        c.fadeOthers = keep.fadeOthers;
        c.plugins = keep.plugins;   // the plugins stay on the button
        c.customColour = keep.customColour;
        c.midi = keep.midi;
        c.file = file;
        c.name = original.getFileNameWithoutExtension();
    }
    engine.stopCue (id, false);
    loadAudio (id);
    dirty = true;
    sendChangeMessage();
}

void ShowController::clearCue (int id)
{
    if (! show.isValid (id))
        return;
    engine.stopCue (id, false);
    auto& c = show.cue (id);
    const auto colour = c.colour;
    c = Cue();
    c.colour = colour;
    if (auto it = media.find (id); it != media.end())
    {
        retire (it->second);
        media.erase (it);
    }
    retireFx (id);
    errors.erase (id);
    loading.erase (id);
    // no blank rows: it comes off every playlist too
    for (int pl = 0; pl < (int) show.playlists.size(); ++pl)
        for (int i = (int) show.playlists[(size_t) pl].entries.size() - 1; i >= 0; --i)
            if (show.playlists[(size_t) pl].entries[(size_t) i].cueId == id)
                removeEntry (pl, i);
    dirty = true;
    sendChangeMessage();
}

void ShowController::restoreCue (int id, const Cue& c)
{
    if (! show.isValid (id))
        return;
    engine.stopCue (id, false);
    auto& cue = show.cue (id);
    const auto plugins = cue.plugins;
    const bool sameFile = cue.file == c.file;
    cue = c;
    cue.plugins = plugins;   // the running plugins belong to the button, not to the take
    if (! sameFile)
    {
        if (auto it = media.find (id); it != media.end())
        {
            retire (it->second);
            media.erase (it);
        }
        errors.erase (id);
        loading.erase (id);
        loadAudio (id);   // (nothing to load when the button goes back to empty)
    }
    dirty = true;
    sendChangeMessage();
}

void ShowController::cueChanged (int id)
{
    if (! show.isValid (id))
        return;
    const auto& c = show.cue (id);
    engine.setLive (id, juce::Decibels::decibelsToGain (c.levelDb), c.pan);
    dirty = true;
    sendChangeMessage();
}

void ShowController::swapCues (int a, int b)
{
    if (a == b || ! show.isValid (a) || ! show.isValid (b))
        return;
    std::swap (show.cue (a), show.cue (b));
    auto swapIn = [a, b] (auto& map)
    {
        auto ia = map.find (a), ib = map.find (b);
        auto va = ia != map.end() ? std::optional (ia->second) : std::nullopt;
        auto vb = ib != map.end() ? std::optional (ib->second) : std::nullopt;
        map.erase (a);
        map.erase (b);
        if (vb) map[a] = *vb;
        if (va) map[b] = *va;
    };
    swapIn (media);
    swapIn (errors);
    swapIn (fx);
    const bool loadingA = loading.count (a) > 0, loadingB = loading.count (b) > 0;
    loading.erase (a);
    loading.erase (b);
    if (loadingA) loadAudio (b);   // loads on their way are for the old places: start them again
    if (loadingB) loadAudio (a);
    for (auto& p : show.playlists)
        for (auto& e : p.entries)
            e.cueId = e.cueId == a ? b : e.cueId == b ? a : e.cueId;
    for (auto& r : show.run)
        r.cueId = r.cueId == a ? b : r.cueId == b ? a : r.cueId;
    for (auto& s : show.schedule)
        s.cueId = s.cueId == a ? b : s.cueId == b ? a : s.cueId;
    engine.swapCues (a, b);
    requeueFollows();
    dirty = true;
    sendChangeMessage();
}

int ShowController::splitToNewButton (int id, juce::Range<double> section, const juce::String& name)
{
    if (! show.isValid (id) || show.cue (id).isEmpty() || section.getLength() <= 0.0)
        return -1;
    int to = nextEmptyCue (id + 1);
    if (to < 0)
        to = nextEmptyCue (0);
    if (to < 0)
        return -1;
    auto c = show.cue (id);
    c.name = name;
    c.trimIn = section.getStart();
    c.trimOut = section.getEnd();
    std::vector<juce::Range<double>> cuts;
    for (auto cut : c.cuts)
        if (auto inside = cut.getIntersectionWith (section); inside.getLength() > 0.0)
            cuts.push_back (inside);
    c.cuts = cuts;
    std::vector<Paste> pastes;
    for (auto& p : c.pastes)
        if (p.at > section.getStart() && p.at < section.getEnd())
            pastes.push_back (p);
    c.pastes = pastes;
    c.fadeIn.seconds = c.fadeOut.seconds = 0.0;
    c.hotkey.clear();
    c.midi = {};
    show.cue (to) = c;
    if (auto audio = getAudio (id))
        media[to] = audio;   // the same file: share what's loaded
    else
        loadAudio (to);
    if (c.hasPlugins())
        createPlugins (to);
    dirty = true;
    sendChangeMessage();
    return to;
}

int ShowController::nextEmptyCue (int id) const
{
    for (id = std::max (0, id); show.isValid (id); ++id)
        if (Show::slotOf (id) < buttonsPerBank && show.cue (id).isEmpty())
            return id;
    return -1;
}

int ShowController::assignFiles (juce::Array<juce::File> files, int startId)
{
    std::sort (files.begin(), files.end(), [] (const juce::File& x, const juce::File& y)
               { return x.getFileName().compareNatural (y.getFileName()) < 0; });
    int placed = 0;
    int id = std::max (0, startId);
    for (auto& f : files)
    {
        id = nextEmptyCue (id);
        if (id < 0 && (int) show.banks.size() < maxBanks)
        {
            // every button is in use: a new bank
            Show::Bank bank;
            bank.name = "Bank " + juce::String::charToString ((juce::juce_wchar) ('A' + show.banks.size()));
            show.banks.push_back (bank);
            id = Show::cueId ((int) show.banks.size() - 1, 0);
        }
        if (id < 0)
            break;
        assignFile (id, f);
        ++placed;
        ++id;
    }
    return placed;
}

void ShowController::trigger (int id)
{
    if (const int i = getNextScheduled(); i >= 0 && show.schedule[(size_t) i].held && show.schedule[(size_t) i].cueId == id)
        show.schedule[(size_t) i].state = ScheduleItem::defeated;   // its button started by hand during a hold
    if (engine.isCuePlaying (id))
        engine.stopCue (id, true);
    else
        play (id);
}

void ShowController::play (int id)
{
    auto audio = getAudio (id);
    if (audio == nullptr || ! show.isValid (id))
        return;
    PlayPlan plan;
    if (PlayPlan::make (show.cue (id), *audio, id, plan))
    {
        plan.takeOver = show.playbackMode;
        plan.stopsLoops = true;
        attachFx (plan);
        engine.play (plan);
    }
}

void ShowController::setVoiceLoop (int voice, bool loop)
{
    for (auto& v : engine.getVoices())
        if (v.voice == voice && show.isValid (v.cueId))
        {
            // a song with loop points playing normally: it plays to its Out point, then loops (from In, or Loop In)
            PlayPlan loopPlan;
            auto audio = getAudio (v.cueId);
            if (loop && audio != nullptr && show.cue (v.cueId).hasLoopPoints()
                && PlayPlan::makeLoop (show.cue (v.cueId), *audio, v.cueId, loopPlan))
                engine.setVoiceLoop (voice, true, &loopPlan);
            else
                engine.setVoiceLoop (voice, loop);
            return;
        }
}

std::shared_ptr<CueAudio> ShowController::getAudio (int id) const
{
    auto it = media.find (id);
    return it == media.end() ? nullptr : it->second;
}

juce::String ShowController::getError (int id) const
{
    auto it = errors.find (id);
    return it == errors.end() ? juce::String() : it->second;
}

void ShowController::loadAudio (int id)
{
    const auto file = show.cue (id).file;
    if (file == juce::File())
        return;
    loading[id] = file;
    errors.erase (id);
    const double rate = engine.getSampleRate();
    loader.load (file, rate, [this, id, file, rate] (std::shared_ptr<CueAudio> audio, juce::String error)
    {
        auto it = loading.find (id);
        if (it == loading.end() || it->second != file || ! show.isValid (id) || show.cue (id).file != file)
            return;   // the button changed while this was loading
        loading.erase (it);
        if (rate != engine.getSampleRate())
        {
            loadAudio (id);   // the device changed rate meanwhile
            return;
        }
        if (auto old = media.find (id); old != media.end())
            retire (old->second);
        if (audio != nullptr)
            media[id] = audio;
        else
        {
            media.erase (id);
            errors[id] = error;
        }
        sendChangeMessage();
    });
}

bool ShowController::loadNow (int id, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto audio = CueAudio::load (show.cue (id).file, engine.getSampleRate(), formats, error);
    if (audio == nullptr)
        return false;
    media[id] = audio;
    return true;
}

void ShowController::reloadAll()
{
    loadedRate = engine.getSampleRate();
    for (const int id : show.allCueIds())
        if (! show.cue (id).isEmpty())
            loadAudio (id);
}

void ShowController::retire (std::shared_ptr<CueAudio> audio)
{
    if (audio != nullptr)
        retired.push_back (std::move (audio));
}

void ShowController::setSelfDestruct (bool on)
{
    if (on == show.selfDestruct)
        return;
    show.selfDestruct = on;
    dirty = true;
    sendChangeMessage();
}

void ShowController::updateSelfDestruct()
{
    std::set<int> playing;
    for (auto& v : engine.getVoices())
        if (! v.preview && ! Show::isSong (v.cueId))   // playlist songs have no button to go black
            playing.insert (v.cueId);
    if (show.selfDestruct)
        destructing.insert (playing.begin(), playing.end());
    bool changed = false;
    for (auto it = destructing.begin(); it != destructing.end();)
    {
        if (playing.count (*it) > 0)
        {
            ++it;
            continue;
        }
        if (show.isValid (*it) && ! show.cue (*it).isEmpty() && ! show.cue (*it).spent)
        {
            show.cue (*it).spent = true;   // played and stopped (or ended): black from now on
            autosaveNow = true;
            changed = true;
        }
        it = destructing.erase (it);
    }
    if (changed)
    {
        dirty = true;
        sendChangeMessage();
    }
}

void ShowController::resetColours()
{
    bool changed = false;
    for (auto& b : show.banks)
        for (auto& c : b.cues)
            if (c.spent)
            {
                c.spent = false;
                changed = true;
            }
    if (changed)
    {
        dirty = true;
        sendChangeMessage();
    }
}

void ShowController::resetColour (int id)
{
    if (! show.isValid (id) || ! show.cue (id).spent)
        return;
    show.cue (id).spent = false;
    dirty = true;
    sendChangeMessage();
}

bool ShowController::anySpent() const
{
    for (auto& b : show.banks)
        for (auto& c : b.cues)
            if (c.spent)
                return true;
    return false;
}

void ShowController::timerCallback()
{
    retiredFx.erase (std::remove_if (retiredFx.begin(), retiredFx.end(),
                                     [this] (auto& f) { return ! engine.isUsingFx (f.get()); }),
                      retiredFx.end());
    if (std::abs (engine.getSampleRate() - fxRate) > 0.5 || engine.getBlockSize() != fxBlock)
    {
        fxRate = engine.getSampleRate();
        fxBlock = engine.getBlockSize();
        for (auto& [id, chain] : fx)
            chain->prepare (fxRate, fxBlock);   // new device rate / buffer size
    }

    if (std::abs (engine.getSampleRate() - loadedRate) > 0.5)
    {
        // what's playing right now. (The engine reports seconds by the new rate, but its voices are still in samples
        // of the old one.) Older than this tick's snapshot would miss a song started in the last moment.
        resumeVoices.clear();
        const double scale = engine.getSampleRate() / std::max (1.0, loadedRate);
        for (auto& v : engine.getVoices())
            if (! v.preview && ! v.stopping && ! v.waiting)
                resumeVoices.push_back ({ v.cueId, v.tag, v.position * scale, v.looping });
        lastVoicesMs = juce::Time::getMillisecondCounter();
        resumePending = ! resumeVoices.empty();
        engine.stopAll (false);
        reloadAll();   // audio is kept at the device rate
    }
    if (resumePending)
    {
        if (loading.empty())
            resumeKeptVoices();
    }
    else
    {
        updatePlaylist();
        if (! mirroring)
            updateSelfDestruct();
        if (! mirroring)
            updateSchedule();
        if (! mirroring)
            updateLoudness();
    }
    if (! mirroring)
        updateAutosave();

    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [this] (auto& a) { return ! engine.isUsing (a.get()); }),
                   retired.end());
}


void ShowController::resumeKeptVoices()
{
    resumePending = false;
    if (! resumeAfterRateChange || ! resumeAfterRateChange())
        return;
    const double elapsed = (juce::Time::getMillisecondCounter() - lastVoicesMs) / 1000.0;
    const double sr = engine.getSampleRate();
    for (auto it = resumeVoices.rbegin(); it != resumeVoices.rend(); ++it)
    {
        const auto audio = getAudio (it->cueId);
        if (audio == nullptr || ! show.isValid (it->cueId))
            continue;
        PlayPlan plan;
        const auto& cue = show.cue (it->cueId);
        if (! (it->looping ? PlayPlan::makeLoop (cue, *audio, it->cueId, plan) : PlayPlan::make (cue, *audio, it->cueId, plan)))
            continue;
        const double length = (double) plan.length / sr;
        double position = it->position + elapsed;
        if (position >= length)
        {
            if (! it->looping)
                continue;
            position = std::fmod (position, length);
        }
        plan.startAt = (juce::int64) (position * sr);
        plan.tag = it->tag;
        if (auto t = tagPlaylist.find (it->tag); t != tagPlaylist.end())
            plan.playlist = t->second;
        attachFx (plan);
        chained.erase (it->tag);   // its link to the next song is queued again
        engine.play (plan);
    }
    resumeVoices.clear();
}

void ShowController::newShow()
{
    engine.stopAll (false);
    for (auto& [id, audio] : media)
        retire (audio);
    media.clear();
    for (auto& [id, chain] : fx)
    {
        pluginsGoing (*chain);
        retiredFx.push_back (chain);
    }
    fx.clear();
    errors.clear();
    loading.clear();
    show.reset();
    showFile = juce::File();
    selectedPlaylist = 0;
    assignUids();
    assignRunUids();
    scheduleRunning = false;
    tagUid.clear();
    tagPlaylist.clear();
    chained.clear();
    seen.clear();
    destructing.clear();
    dirty = false;
    markPositionSaved();
    if (autosave != nullptr)
        autosave->discard();
    sendChangeMessage();
}

bool ShowController::openShow (const juce::File& f, juce::String& error)
{
    Show loaded;
    if (! loaded.load (f, error))
        return false;
    newShow();
    show = loaded;
    missingPlugins.clear();
    for (const int id : show.allCueIds())
        if (show.cue (id).hasPlugins())
            createPlugins (id);
    assignUids();
    assignRunUids();
    showFile = f;
    reloadAll();
    dirty = false;
    markPositionSaved();
    sendChangeMessage();
    return true;
}

bool ShowController::saveShow (const juce::File& f, juce::String& error)
{
    storePluginStates();
    if (! f.getParentDirectory().createDirectory())
    {
        error = "Couldn't make the folder " + f.getParentDirectory().getFullPathName();
        return false;
    }
    if (showFile == juce::File())
        moveUnsavedAudio (f.getSiblingFile ("Audio"));
    if (! show.save (f, error))
        return false;
    showFile = f;
    dirty = false;
    markPositionSaved();
    if (autosave != nullptr)
        autosave->discard();
    sendChangeMessage();
    return true;
}

juce::var ShowController::recoveryVar() const
{
    auto v = show.toVar();
    v.getDynamicObject()->setProperty ("recovery", positionVar());
    return v;
}


bool ShowController::openRecovery (const juce::File& f, juce::String& error)
{
    juce::var v;
    if (juce::JSON::parse (f.loadFileAsString(), v).failed())
    {
        error = "The recovery copy can't be read.";
        return false;
    }
    const auto info = v.getProperty ("recovery", {});
    const auto path = info.getProperty ("showFile", {}).toString();
    if (! openShow (f, error))
        return false;
    showFile = path.isNotEmpty() ? juce::File (path) : juce::File();
    applyPosition (info);
    dirty = true;          // nothing of this is in the show file yet
    autosaveNow = true;    // opening it dropped the old copy: write it again straight away
    sendChangeMessage();
    return true;
}

std::vector<ShowIssue> ShowController::checkShow() const
{
    std::vector<ShowIssue> issues;
    auto add = [&issues] (ShowIssue::Kind kind, int id, juce::String text, juce::File file = juce::File())
    {
        issues.push_back ({ kind, id, std::move (text), std::move (file) });
    };
    auto label = [this] (int id)
    {
        const auto& c = show.cue (id);
        if (Show::isSong (id))
            return "Playlist song \"" + c.name + "\"";
        return "Button " + juce::String::charToString ((juce::juce_wchar) ('A' + Show::bankOf (id))) + juce::String (Show::slotOf (id) + 1)
             + (c.name.isNotEmpty() ? " \"" + c.name + "\"" : juce::String());
    };

    const int outputs = engine.getNumOutputs();
    if (outputs <= 0)
        add (ShowIssue::Kind::noDevice, -1, "No audio device is open.");

    for (const int id : show.allCueIds())
        {
            const auto& c = show.cue (id);
            if (c.isEmpty())
                continue;
            if (! c.file.existsAsFile())
                add (ShowIssue::Kind::missingAudio, id, label (id) + ": can't find " + c.file.getFullPathName(), c.file);
            else if (const auto error = getError (id); error.isNotEmpty())
                add (ShowIssue::Kind::unreadableAudio, id, label (id) + ": " + error, c.file);
            if (outputs > 0 && c.output.firstChannel + c.output.numChannels() > outputs)
                add (ShowIssue::Kind::noSuchOutput, id, label (id) + " plays on output " + juce::String (c.output.firstChannel + 1)
                     + (c.output.stereo ? "-" + juce::String (c.output.firstChannel + 2) : juce::String())
                     + ", but this device only has " + juce::String (outputs) + ".");
            for (int p = 0; p < (int) c.plugins.size(); ++p)
                if (const auto& slot = c.plugins[(size_t) p]; ! slot.isEmpty())
                {
                    const auto* chain = getFx (id);
                    if (chain == nullptr || chain->getPlugin (p) == nullptr)
                    {
                        const auto xml = juce::parseXML (slot.description);
                        add (ShowIssue::Kind::missingPlugin, id, label (id) + ": plugin "
                             + (xml != nullptr ? xml->getStringAttribute ("name", "(unknown)") : juce::String ("(unknown)"))
                             + " isn't loaded.");
                    }
                }
        }

    for (int p = 0; p < (int) show.playlists.size(); ++p)
    {
        const auto& list = show.playlists[(size_t) p];
        for (int i = 0; i < (int) list.entries.size(); ++i)
            if (const int id = list.entries[(size_t) i].cueId; ! show.isValid (id) || show.cue (id).isEmpty())
                add (ShowIssue::Kind::emptyEntry, id, list.displayName (p) + ", song " + juce::String (i + 1)
                     + (Show::isSong (id) ? juce::String (": its song is missing.") : juce::String (": its button is empty.")));
    }
    for (int i = 0; i < (int) show.run.size(); ++i)
        if (const int id = show.run[(size_t) i].cueId; id >= 0 && (! show.isValid (id) || show.cue (id).isEmpty()))
            add (ShowIssue::Kind::emptyEntry, id, "Run of show, row " + juce::String (i + 1) + ": its button is empty.");
    for (int i = 0; i < (int) show.run.size(); ++i)
        if (const auto* list = playlistAt (show.run[(size_t) i].playlist); list != nullptr && list->entries.empty())
            add (ShowIssue::Kind::emptyEntry, -1, "Run of show, row " + juce::String (i + 1) + ": its playlist is empty.");
    for (int i = 0; i < (int) show.schedule.size(); ++i)
        if (const int id = show.schedule[(size_t) i].cueId; ! show.isValid (id) || show.cue (id).isEmpty())
            add (ShowIssue::Kind::emptyEntry, id, "Schedule, trigger " + juce::String (i + 1) + ": its button is empty.");
    return issues;
}

void ShowController::relinkFile (juce::File from, const juce::File& to)
{
    if (from == to)
        return;
    bool changed = false;
    for (const int id : show.allCueIds())
        if (auto& c = show.cue (id); c.file == from)
        {
            c.file = to;
            errors.erase (id);
            loadAudio (id);
            changed = true;
        }
    if (changed)
    {
        dirty = true;
        sendChangeMessage();
    }
}

std::optional<float> ShowController::suggestedLevel (int id) const
{
    if (! show.isValid (id) || show.cue (id).lufs >= 0.0)
        return std::nullopt;
    const auto& c = show.cue (id);
    return (float) juce::jlimit ((double) Cue::minLevelDb, (double) Cue::maxLevelDb,
                                 (c.voiceOver ? voiceTarget : musicTarget) - c.lufs);
}

void ShowController::applyLoudness (int id)
{
    if (const auto level = suggestedLevel (id))
    {
        show.cue (id).levelDb = *level;
        cueChanged (id);
    }
}

void ShowController::updateLoudness()
{
    if (measuring)
        return;
    for (auto& [id, audio] : media)
    {
        if (audio == nullptr || ! show.isValid (id))
            continue;
        auto& c = show.cue (id);
        const auto parts = c.segments (audio->seconds());
        const auto key = loudnessKey (parts);
        if (key == c.lufsKey)
            continue;
        measuring = true;
        loudnessPool.addJob ([this, audio = audio, parts, key, id = id, alive = loudnessAlive]
        {
            const auto result = measureLoudness (*audio, parts);
            juce::MessageManager::callAsync ([this, result, key, id, alive]
            {
                if (! alive->load())
                    return;
                measuring = false;
                if (show.isValid (id))
                {
                    auto& cue = show.cue (id);
                    cue.lufs = result.isValid() ? result.lufs : 0.0;
                    cue.peakDb = result.peakDb;
                    cue.lufsKey = key;
                    sendChangeMessage();
                }
            });
        });
        return;
    }
}

juce::var ShowController::positionVar() const
{
    auto* info = new juce::DynamicObject();
    info->setProperty ("showFile", showFile == juce::File() ? juce::String() : showFile.getFullPathName());
    info->setProperty ("selectedPlaylist", selectedPlaylist);
    juce::Array<juce::var> next;
    for (int p = 0; p < (int) show.playlists.size(); ++p)
        next.add (getNextEntry (p));
    info->setProperty ("next", next);
    juce::Array<juce::var> resumes;   // preshow: [entry, seconds] per playlist (entry -1 = none)
    for (int p = 0; p < (int) show.playlists.size(); ++p)
        resumes.add (juce::Array<juce::var> { getResumeEntry (p), getResumeSeconds (p) });
    info->setProperty ("resume", resumes);
    info->setProperty ("runNext", getRunNext());
    info->setProperty ("runOnAir", getRunOnAir());
    info->setProperty ("scheduleRunning", scheduleRunning);
    juce::Array<juce::var> items;
    for (auto& s : show.schedule)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("state", (int) s.state);
        o->setProperty ("fireAt", s.fireAt);
        o->setProperty ("held", s.held);
        o->setProperty ("heldSeconds", s.heldSeconds);
        items.add (o);
    }
    info->setProperty ("scheduleItems", items);
    return info;
}

void ShowController::applyPosition (const juce::var& info)
{
    selectedPlaylist = juce::jlimit (0, std::max (0, (int) show.playlists.size() - 1), (int) info.getProperty ("selectedPlaylist", 0));
    if (auto* next = info.getProperty ("next", {}).getArray())
        for (int p = 0; p < std::min (next->size(), (int) show.playlists.size()); ++p)
            if ((int) (*next)[p] >= 0)
                setNextEntry (p, (int) (*next)[p]);
    if (auto* resumes = info.getProperty ("resume", {}).getArray())
        for (int p = 0; p < std::min (resumes->size(), (int) show.playlists.size()); ++p)
        {
            auto& list = show.playlists[(size_t) p];
            const int entry = (int) (*resumes)[p][0];
            list.resumeUid = entry >= 0 && entry < (int) list.entries.size() ? list.entries[(size_t) entry].uid : 0;
            list.resumeSeconds = (double) (*resumes)[p][1];
        }
    if (const int r = (int) info.getProperty ("runNext", -1); r >= 0 && r < (int) show.run.size())
        runNextUid = show.run[(size_t) r].uid;
    else
        runNextUid = 0;
    if (const int r = (int) info.getProperty ("runOnAir", -1); r >= 0 && r < (int) show.run.size())
        runOnAirUid = show.run[(size_t) r].uid;
    if (auto* items = info.getProperty ("scheduleItems", {}).getArray())
        for (int i = 0; i < std::min (items->size(), (int) show.schedule.size()); ++i)
        {
            auto& s = show.schedule[(size_t) i];
            const auto& o = (*items)[i];
            s.state = (ScheduleItem::State) juce::jlimit (0, 3, (int) o.getProperty ("state", 0));
            s.fireAt = (juce::int64) o.getProperty ("fireAt", 0);
            s.held = (bool) o.getProperty ("held", false);
            s.heldSeconds = (double) o.getProperty ("heldSeconds", 0.0);
            if (s.state == ScheduleItem::waiting && s.fireAt < nowMs() - 2000)
                s.state = ScheduleItem::missed;   // passed while nobody was running it: never fires late
        }
    scheduleRunning = (bool) info.getProperty ("scheduleRunning", false);
    sendChangeMessage();
}

void ShowController::setMirroring (bool on)
{
    mirroring = on;
    if (on && autosave != nullptr)
        autosave->discard();     // the show here is the main laptop's, kept there
}

void ShowController::applyMirror (const juce::var& incoming, const std::function<juce::File (const juce::File&)>& localFile)
{
    Show next;
    if (! next.fromVar (incoming))
        return;
    auto mirrorCue = [&] (int id)
    {
        auto c = next.cue (id);
        c.file = c.isEmpty() ? juce::File() : localFile (c.file);
        auto& mine = show.cue (id);
        const bool newFile = mine.file != c.file;
        bool newPlugins = false;
        for (size_t p = 0; p < c.plugins.size(); ++p)
            newPlugins = newPlugins || c.plugins[p].description != mine.plugins[p].description || c.plugins[p].state != mine.plugins[p].state;
        mine = c;
        if (newFile)
        {
            engine.stopCue (id, false);
            if (auto it = media.find (id); it != media.end())
            {
                retire (it->second);
                media.erase (it);
            }
            errors.erase (id);
            loading.erase (id);
            loadAudio (id);
        }
        if (newPlugins)
        {
            retireFx (id);
            if (c.hasPlugins())
                createPlugins (id);
        }
    };
    while (show.banks.size() < next.banks.size())
        show.banks.emplace_back();
    for (int b = 0; b < (int) next.banks.size(); ++b)
    {
        show.banks[(size_t) b].name = next.banks[(size_t) b].name;
        for (int s = 0; s < Show::cuesPerBank; ++s)
            mirrorCue (Show::cueId (b, s));
    }
    while (show.banks.size() > next.banks.size())
    {
        for (int s = 0; s < Show::cuesPerBank; ++s)
            clearCue (Show::cueId ((int) show.banks.size() - 1, s));
        show.banks.pop_back();
    }
    // playlist songs the same way
    while (show.songs.size() < next.songs.size())
        show.songs.emplace_back();
    for (int i = 0; i < (int) next.songs.size(); ++i)
        mirrorCue (Show::songBase + i);
    while (show.songs.size() > next.songs.size())
    {
        clearCue (Show::songBase + (int) show.songs.size() - 1);
        show.songs.pop_back();
    }
    show.playlists = next.playlists;
    show.run = next.run;
    show.schedule = next.schedule;
    show.playbackMode = next.playbackMode;
    show.selfDestruct = next.selfDestruct;
    assignUids();
    assignRunUids();
    selectedPlaylist = juce::jlimit (0, std::max (0, (int) show.playlists.size() - 1), selectedPlaylist);
    dirty = false;
    sendChangeMessage();
}

void ShowController::playMirrored (int id, double position, bool looping)
{
    auto audio = getAudio (id);
    if (audio == nullptr || ! show.isValid (id))
        return;
    PlayPlan plan;
    const auto& cue = show.cue (id);
    if (! (looping ? PlayPlan::makeLoop (cue, *audio, id, plan) : PlayPlan::make (cue, *audio, id, plan)))
        return;
    plan.startAt = juce::jlimit<juce::int64> (0, std::max<juce::int64> (0, plan.length - 1), (juce::int64) (position * engine.getSampleRate()));
    plan.takeOver = PlaybackMode::overlap;   // what stops is the main laptop's decision
    attachFx (plan);
    engine.play (plan);
}

void ShowController::updateAutosave()
{
    if (autosave == nullptr)
        return;
    const auto now = juce::Time::getMillisecondCounter();
    // lastAutosave 0 = never written: due now, however soon after the computer started (the counter starts at boot)
    if (! autosaveNow && lastAutosave != 0 && now - lastAutosave < autosaveMs)
        return;
    autosaveNow = false;
    lastAutosave = now;
    // a saved show running a countdown or part way down its run of show has something to lose too
    if (! dirty && juce::JSON::toString (positionVar(), true) == savedPosition)
        return;
    if (! fx.empty() && engine.getVoices().empty())
        storePluginStates();
    autosave->submit (recoveryVar());
}

void ShowController::markPositionSaved()
{
    savedPosition = juce::JSON::toString (positionVar(), true);
}
