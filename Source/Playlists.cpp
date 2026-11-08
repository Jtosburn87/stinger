#include "Show.h"

// ShowController: playlists (GO, what's on air / next, links between songs). Each playlist keeps its own
// Next / On air; voices remember the playlist they were started for.

void ShowController::assignUids()
{
    for (auto& p : show.playlists)
    {
        p.uid = newUid();
        p.nextUid = p.onAirUid = 0;
        for (auto& e : p.entries)
            e.uid = newUid();
    }
}

void ShowController::selectPlaylist (int index)
{
    index = juce::jlimit (0, (int) show.playlists.size() - 1, index);
    if (index == selectedPlaylist)
        return;
    selectedPlaylist = index;
    sendChangeMessage();
}

Playlist* ShowController::playlistAt (int index)
{
    return index >= 0 && index < (int) show.playlists.size() ? &show.playlists[(size_t) index] : nullptr;
}

const Playlist* ShowController::playlistAt (int index) const
{
    return index >= 0 && index < (int) show.playlists.size() ? &show.playlists[(size_t) index] : nullptr;
}

int ShowController::addPlaylist (const juce::String& name)
{
    Playlist p;
    p.name = name;
    p.uid = newUid();
    show.playlists.push_back (p);
    dirty = true;
    selectedPlaylist = (int) show.playlists.size() - 1;
    sendChangeMessage();
    return selectedPlaylist;
}

void ShowController::removePlaylist (int index)
{
    if (index < 0 || index >= (int) show.playlists.size())
        return;
    show.playlists.erase (show.playlists.begin() + index);
    for (auto& r : show.run)
        r.playlist = r.playlist == index ? -1 : r.playlist > index ? r.playlist - 1 : r.playlist;
    if (show.playlists.empty())
    {
        Playlist p;
        p.name = "Playlist 1";
        p.uid = newUid();
        show.playlists.push_back (p);
    }
    if (selectedPlaylist > index || selectedPlaylist >= (int) show.playlists.size())
        selectedPlaylist = std::max (0, selectedPlaylist - 1);
    dirty = true;
    requeueFollows();   // its songs play on, but nothing follows them now
    sendChangeMessage();
}

void ShowController::renamePlaylist (int index, const juce::String& name)
{
    auto* p = playlistAt (index);
    if (p == nullptr || p->name == name.trim())
        return;
    p->name = name.trim();
    dirty = true;
    sendChangeMessage();
}

int ShowController::playlistIndexOf (int playlistUid) const
{
    for (int i = 0; i < (int) show.playlists.size(); ++i)
        if (show.playlists[(size_t) i].uid == playlistUid)
            return i;
    return -1;
}

int ShowController::playlistOfTag (int tag) const
{
    auto it = tagPlaylist.find (tag);
    return it == tagPlaylist.end() ? -1 : playlistIndexOf (it->second);
}

int ShowController::indexOfUid (int playlist, int uid) const
{
    auto* p = playlistAt (playlist);
    if (uid == 0 || p == nullptr)
        return -1;
    for (int i = 0; i < (int) p->entries.size(); ++i)
        if (p->entries[(size_t) i].uid == uid)
            return i;
    return -1;
}

void ShowController::addToPlaylist (int playlist, int cueId, int insertAt)
{
    auto* p = playlistAt (playlist);
    if (p == nullptr || ! show.isValid (cueId))
        return;
    PlaylistEntry e;
    e.cueId = cueId;
    e.uid = newUid();
    if (insertAt < 0 || insertAt > (int) p->entries.size())
        insertAt = (int) p->entries.size();
    // new songs play on into the next one; the last song stops (the one it follows now plays on into it)
    e.transition = Transition::playNext;
    if (insertAt == (int) p->entries.size())
    {
        e.transition = Transition::stop;
        if (! p->entries.empty() && p->entries.back().transition == Transition::stop)
            p->entries.back().transition = Transition::playNext;
    }
    p->entries.insert (p->entries.begin() + insertAt, e);
    dirty = true;
    requeueFollows();
    sendChangeMessage();
}

void ShowController::removeEntry (int playlist, int index)
{
    auto* p = playlistAt (playlist);
    if (p == nullptr || index < 0 || index >= (int) p->entries.size())
        return;
    if (p->entries[(size_t) index].uid == p->nextUid)
        p->nextUid = index + 1 < (int) p->entries.size() ? p->entries[(size_t) index + 1].uid : 0;
    p->entries.erase (p->entries.begin() + index);
    dirty = true;
    requeueFollows();
    sendChangeMessage();
}

void ShowController::moveEntry (int playlist, int from, int to)
{
    auto* p = playlistAt (playlist);
    if (p == nullptr || from < 0 || from >= (int) p->entries.size())
        return;
    to = juce::jlimit (0, (int) p->entries.size() - 1, to);
    if (from == to)
        return;
    const auto e = p->entries[(size_t) from];
    p->entries.erase (p->entries.begin() + from);
    p->entries.insert (p->entries.begin() + to, e);
    dirty = true;
    requeueFollows();
    sendChangeMessage();
}

void ShowController::setTransition (int playlist, int index, Transition t, double crossfadeSeconds)
{
    auto* p = playlistAt (playlist);
    if (p == nullptr || index < 0 || index >= (int) p->entries.size())
        return;
    auto& e = p->entries[(size_t) index];
    if (e.transition == Transition::loopUntil && t != Transition::loopUntil)
        for (auto& [tag, uid] : tagUid)
            if (uid == e.uid)
                engine.releaseLoop (tag);   // no longer loops: it plays to its end
    e.transition = t;
    if (t == Transition::wait)
        e.waitSeconds = juce::jlimit (0.1, 600.0, crossfadeSeconds);
    else
        e.crossfadeSeconds = juce::jlimit (0.1, 60.0, crossfadeSeconds);
    dirty = true;
    requeueFollows();
    sendChangeMessage();
}

int ShowController::getNextEntry (int playlist) const
{
    auto* p = playlistAt (playlist);
    if (p == nullptr)
        return -1;
    if (p->nextUid != 0)
        return indexOfUid (playlist, p->nextUid);
    if (p->onAirUid == 0 && ! p->entries.empty())
        return 0;   // nothing played yet: the top
    return -1;
}

void ShowController::setNextEntry (int playlist, int index)
{
    auto* p = playlistAt (playlist);
    if (p != nullptr && index >= 0 && index < (int) p->entries.size())
    {
        p->nextUid = p->entries[(size_t) index].uid;
        sendChangeMessage();
    }
}

int ShowController::getOnAirEntry (int playlist) const
{
    auto* p = playlistAt (playlist);
    return p == nullptr ? -1 : indexOfUid (playlist, p->onAirUid);
}

bool ShowController::makePlan (const PlaylistEntry& entry, int tag, PlayPlan& plan)
{
    const int cueId = entry.cueId;
    auto audio = getAudio (cueId);
    if (audio == nullptr || ! show.isValid (cueId))
        return false;
    // Loop until GO: the song's loop points if it has them (GO then plays on through Loop Out to the Out point)
    const bool made = entry.transition == Transition::loopUntil ? PlayPlan::makeLoop (show.cue (cueId), *audio, cueId, plan)
                                                                : PlayPlan::make (show.cue (cueId), *audio, cueId, plan);
    if (! made)
        return false;
    plan.tag = tag;
    if (auto it = tagPlaylist.find (tag); it != tagPlaylist.end())
        plan.playlist = it->second;
    attachFx (plan);
    return true;
}

double ShowController::fadeSecondsFor (int tag)
{
    // how long a playlist song takes to fade when the playlist moves on: its crossfade / fade-to-next time,
    // else its stop fade
    const int playlist = playlistOfTag (tag);
    if (auto it = tagUid.find (tag); playlist >= 0 && it != tagUid.end())
        if (const int i = indexOfUid (playlist, it->second); i >= 0)
        {
            const auto& e = show.playlists[(size_t) playlist].entries[(size_t) i];
            if (e.transition == Transition::crossfade || e.transition == Transition::loopUntil)
                return e.crossfadeSeconds;
            return show.cue (e.cueId).stopFade.seconds;
        }
    return 1.0;
}

bool ShowController::isLoopingUntilGo (int playlist) const
{
    for (auto& v : engine.getVoices())
        if (v.tag != 0 && v.looping && ! v.stopping && playlistOfTag (v.tag) == playlist)
            return true;
    return false;
}

void ShowController::go (int playlist)
{
    goPressed();
    if (isLoopingUntilGo (playlist))
    {
        // the loop finishes its pass, then its link starts the next song with no gap
        for (auto& v : engine.getVoices())
            if (v.tag != 0 && v.looping && ! v.stopping && playlistOfTag (v.tag) == playlist)
                engine.releaseLoop (v.tag);
        sendChangeMessage();
        return;
    }
    const int index = getNextEntry (playlist);
    if (index >= 0)
        playEntry (playlist, index);
}

void ShowController::fadeToNext (int playlist)
{
    goPressed();
    const int index = getNextEntry (playlist);
    if (index < 0)
        return;
    double seconds = 0.0;
    for (auto& v : engine.getVoices())
        if (v.tag != 0 && ! v.stopping && playlistOfTag (v.tag) == playlist)
            seconds = std::max (seconds, fadeSecondsFor (v.tag));
    playEntry (playlist, index, seconds);
}

void ShowController::playEntry (int playlist, int index, double fadeInSeconds)
{
    auto* p = playlistAt (playlist);
    if (p == nullptr || index < 0 || index >= (int) p->entries.size())
        return;
    const auto entry = p->entries[(size_t) index];

    // the playlist moves on: songs it started fade out
    const double sr = engine.getSampleRate();
    for (auto& v : engine.getVoices())
        if (v.tag != 0 && ! v.stopping && playlistOfTag (v.tag) == playlist)
            engine.fadeTag (v.tag, (juce::int64) (std::max (0.01, fadeSecondsFor (v.tag)) * sr));

    const int tag = ++lastTag;
    tagPlaylist[tag] = p->uid;
    PlayPlan plan;
    if (! makePlan (entry, tag, plan))
    {
        tagPlaylist.erase (tag);
        return;
    }
    plan.entryFade = (juce::int64) (std::max (0.0, fadeInSeconds) * sr);
    plan.takeOver = show.playbackMode;   // takes over from buttons and other playlists; its own songs fade as above
    tagUid[tag] = entry.uid;
    engine.play (plan);
    seen.insert (tag);
    p->onAirUid = entry.uid;
    p->nextUid = index + 1 < (int) p->entries.size() ? p->entries[(size_t) index + 1].uid : 0;
    queueFollow (tag);
    sendChangeMessage();
}

void ShowController::queueFollow (int tag)
{
    chained.insert (tag);
    const int playlist = playlistOfTag (tag);
    auto* p = playlistAt (playlist);
    auto t = tagUid.find (tag);
    const int index = t == tagUid.end() ? -1 : indexOfUid (playlist, t->second);
    if (p == nullptr || index < 0 || index + 1 >= (int) p->entries.size())
        return;
    const auto& e = p->entries[(size_t) index];
    if (e.transition == Transition::stop)
        return;
    const auto& next = p->entries[(size_t) index + 1];
    const int nextTag = ++lastTag;
    tagPlaylist[nextTag] = p->uid;
    PlayPlan plan;
    if (! makePlan (next, nextTag, plan))
    {
        tagPlaylist.erase (nextTag);
        return;
    }
    tagUid[nextTag] = next.uid;
    // Loop until GO links like Play next: the engine holds it until the loop is released. Wait: a negative
    // overlap is a gap of silence.
    const double sr = engine.getSampleRate();
    const auto overlap = e.transition == Transition::crossfade ? (juce::int64) (e.crossfadeSeconds * sr)
                       : e.transition == Transition::wait      ? -(juce::int64) (e.waitSeconds * sr)
                                                               : 0;
    engine.follow (tag, plan, overlap);
}

void ShowController::requeueFollows()
{
    for (auto& v : engine.getVoices())
        if (v.tag != 0 && chained.count (v.tag) > 0)
        {
            engine.cancelFollow (v.tag);
            chained.erase (v.tag);
        }
    // the next update queues them again from the playlists as they are now
}

void ShowController::updatePlaylist()
{
    const auto voices = engine.getVoices();   // newest first
    bool changed = false;
    std::set<int> playing;
    for (auto it = voices.rbegin(); it != voices.rend(); ++it)
    {
        if (it->tag == 0)
            continue;
        playing.insert (it->tag);
        if (seen.insert (it->tag).second)
        {
            // a linked song started by itself: it's on air now, and the one after it is next
            auto t = tagUid.find (it->tag);
            const int playlist = playlistOfTag (it->tag);
            if (auto* p = playlistAt (playlist); p != nullptr && t != tagUid.end())
            {
                p->onAirUid = t->second;
                const int index = indexOfUid (playlist, p->onAirUid);
                p->nextUid = index >= 0 && index + 1 < (int) p->entries.size() ? p->entries[(size_t) index + 1].uid : 0;
                changed = true;
            }
        }
    }
    for (auto& v : voices)
        if (v.tag != 0 && ! v.stopping && chained.count (v.tag) == 0 && tagUid.count (v.tag) > 0)
            queueFollow (v.tag);

    // forget songs that have finished (not while commands are still on their way to the engine)
    if (! engine.hasPendingCommands())
    {
        for (auto it = seen.begin(); it != seen.end();)
        {
            if (playing.count (*it) == 0)
            {
                chained.erase (*it);
                tagUid.erase (*it);
                tagPlaylist.erase (*it);
                it = seen.erase (it);
            }
            else
                ++it;
        }
        for (auto& p : show.playlists)
        {
            if (p.onAirUid == 0)
                continue;
            bool on = false;
            for (int tag : playing)
                if (auto t = tagPlaylist.find (tag); t != tagPlaylist.end() && t->second == p.uid)
                    on = true;
            if (! on)
            {
                p.onAirUid = 0;
                changed = true;
            }
        }
    }
    if (changed)
        sendChangeMessage();
}
