#include "Show.h"
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
    juce::Array<juce::var> playlistList;
    for (auto& p : playlists)
    {
        auto* list = new juce::DynamicObject();
        list->setProperty ("name", p.name);
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
    playlists.clear();
    int uid = 0;
    if (auto* list = v.getProperty ("playlists", {}).getArray())
        for (auto& pv : *list)
        {
            Playlist p;
            p.name = pv.getProperty ("name", {}).toString();
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
        if (rel.isEmpty() || b >= (int) banks.size() || i >= cuesPerBank)
            return;
        auto& c = banks[(size_t) b].cues[(size_t) i];
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
    startTimerHz (10);
}

ShowController::~ShowController()
{
    stopTimer();
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
    for (int b = 0; b < (int) show.banks.size(); ++b)
        for (int s = 0; s < Show::cuesPerBank; ++s)
        {
            const int id = Show::cueId (b, s);
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
        if (show.cue (id).isEmpty())
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
        attachFx (plan);
        engine.play (plan);
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
    assignUids();
    for (int b = 0; b < (int) show.banks.size(); ++b)
        for (int s = 0; s < Show::cuesPerBank; ++s)
            if (! show.cue (Show::cueId (b, s)).isEmpty())
                loadAudio (Show::cueId (b, s));
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
        if (! v.preview)
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
    updatePlaylist();
    updateSelfDestruct();

    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [this] (auto& a) { return ! engine.isUsing (a.get()); }),
                   retired.end());

    if (std::abs (engine.getSampleRate() - loadedRate) > 0.5)
    {
        engine.stopAll (false);
        reloadAll();   // audio is kept at the device rate
    }
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
    tagUid.clear();
    tagPlaylist.clear();
    chained.clear();
    seen.clear();
    destructing.clear();
    dirty = false;
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
    for (int b = 0; b < (int) show.banks.size(); ++b)
        for (int s = 0; s < Show::cuesPerBank; ++s)
            if (show.cue (Show::cueId (b, s)).hasPlugins())
                createPlugins (Show::cueId (b, s));
    assignUids();
    showFile = f;
    reloadAll();
    dirty = false;
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
    sendChangeMessage();
    return true;
}
