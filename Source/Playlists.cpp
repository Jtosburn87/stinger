#include "Show.h"

// ShowController: playlists (GO, what's on air / next, links between songs).

void ShowController::setPlaylistIndex (int index)
{
    playlistIndex = juce::jlimit (0, (int) show.playlists.size() - 1, index);
    nextUid = 0;
    sendChangeMessage();
}

Playlist* ShowController::currentPlaylist()
{
    if (show.playlists.empty())
        return nullptr;
    playlistIndex = juce::jlimit (0, (int) show.playlists.size() - 1, playlistIndex);
    return &show.playlists[(size_t) playlistIndex];
}

int ShowController::addPlaylist (const juce::String& name)
{
    show.playlists.push_back ({ name, {} });
    dirty = true;
    setPlaylistIndex ((int) show.playlists.size() - 1);
    return playlistIndex;
}

void ShowController::removePlaylist (int index)
{
    if (index < 0 || index >= (int) show.playlists.size())
        return;
    show.playlists.erase (show.playlists.begin() + index);
    if (show.playlists.empty())
        show.playlists.push_back ({ "Playlist 1", {} });
    dirty = true;
    requeueFollows();
    setPlaylistIndex (std::min (index, (int) show.playlists.size() - 1));
}

int ShowController::indexOfUid (int uid) const
{
    if (uid == 0 || show.playlists.empty())
        return -1;
    const auto& entries = show.playlists[(size_t) juce::jlimit (0, (int) show.playlists.size() - 1, playlistIndex)].entries;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].uid == uid)
            return i;
    return -1;
}

void ShowController::addToPlaylist (int cueId, int insertAt)
{
    auto* p = currentPlaylist();
    if (p == nullptr || ! show.isValid (cueId))
        return;
    PlaylistEntry e;
    e.cueId = cueId;
    e.uid = newUid();
    if (insertAt < 0 || insertAt > (int) p->entries.size())
        insertAt = (int) p->entries.size();
    p->entries.insert (p->entries.begin() + insertAt, e);
    dirty = true;
    requeueFollows();
    sendChangeMessage();
}

void ShowController::removeEntry (int index)
{
    auto* p = currentPlaylist();
    if (p == nullptr || index < 0 || index >= (int) p->entries.size())
        return;
    if (p->entries[(size_t) index].uid == nextUid)
        nextUid = index + 1 < (int) p->entries.size() ? p->entries[(size_t) index + 1].uid : 0;
    p->entries.erase (p->entries.begin() + index);
    dirty = true;
    requeueFollows();
    sendChangeMessage();
}

void ShowController::moveEntry (int from, int to)
{
    auto* p = currentPlaylist();
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

void ShowController::setTransition (int index, Transition t, double crossfadeSeconds)
{
    auto* p = currentPlaylist();
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

int ShowController::getNextEntry() const
{
    if (nextUid != 0)
        return indexOfUid (nextUid);
    if (onAirUid == 0 && ! show.playlists.empty()
        && ! show.playlists[(size_t) juce::jlimit (0, (int) show.playlists.size() - 1, playlistIndex)].entries.empty())
        return 0;   // nothing played yet: the top
    return -1;
}

void ShowController::setNextEntry (int index)
{
    auto* p = currentPlaylist();
    if (p != nullptr && index >= 0 && index < (int) p->entries.size())
    {
        nextUid = p->entries[(size_t) index].uid;
        sendChangeMessage();
    }
}

int ShowController::getOnAirEntry() const
{
    return indexOfUid (onAirUid);
}

bool ShowController::makePlan (const PlaylistEntry& entry, int tag, PlayPlan& plan)
{
    const int cueId = entry.cueId;
    auto audio = getAudio (cueId);
    if (audio == nullptr || ! show.isValid (cueId) || ! PlayPlan::make (show.cue (cueId), *audio, cueId, plan))
        return false;
    plan.tag = tag;
    attachFx (plan);
    if (entry.transition == Transition::loopUntil)
        plan.loop = true;
    return true;
}

double ShowController::fadeSecondsFor (int tag)
{
    // how long a playlist song takes to fade when the playlist moves on: its crossfade / fade-to-next time,
    // else its stop fade
    auto* p = currentPlaylist();
    if (auto it = tagUid.find (tag); p != nullptr && it != tagUid.end())
        if (const int i = indexOfUid (it->second); i >= 0)
        {
            const auto& e = p->entries[(size_t) i];
            if (e.transition == Transition::crossfade || e.transition == Transition::loopUntil)
                return e.crossfadeSeconds;
            return show.cue (e.cueId).stopFade.seconds;
        }
    return 1.0;
}

bool ShowController::isLoopingUntilGo() const
{
    for (auto& v : engine.getVoices())
        if (v.tag != 0 && v.looping && ! v.stopping)
            return true;
    return false;
}

void ShowController::go()
{
    if (isLoopingUntilGo())
    {
        // the loop finishes its pass, then its link starts the next song with no gap
        for (auto& v : engine.getVoices())
            if (v.tag != 0 && v.looping && ! v.stopping)
                engine.releaseLoop (v.tag);
        sendChangeMessage();
        return;
    }
    const int index = getNextEntry();
    if (index >= 0)
        playEntry (index);
}

void ShowController::fadeToNext()
{
    const int index = getNextEntry();
    if (index < 0)
        return;
    double seconds = 0.0;
    for (auto& v : engine.getVoices())
        if (v.tag != 0 && ! v.stopping)
            seconds = std::max (seconds, fadeSecondsFor (v.tag));
    playEntry (index, seconds);
}

void ShowController::playEntry (int index, double fadeInSeconds)
{
    auto* p = currentPlaylist();
    if (p == nullptr || index < 0 || index >= (int) p->entries.size())
        return;
    const auto entry = p->entries[(size_t) index];

    // the playlist moves on: songs it started fade out
    const double sr = engine.getSampleRate();
    for (auto& v : engine.getVoices())
        if (v.tag != 0 && ! v.stopping)
            engine.fadeTag (v.tag, (juce::int64) (std::max (0.01, fadeSecondsFor (v.tag)) * sr));

    const int tag = ++lastTag;
    PlayPlan plan;
    if (! makePlan (entry, tag, plan))
        return;
    plan.entryFade = (juce::int64) (std::max (0.0, fadeInSeconds) * sr);
    plan.takeOver = show.playbackMode;   // takes over from buttons; playlist songs fade out as above
    plan.takeOverPlaylist = false;
    tagUid[tag] = entry.uid;
    engine.play (plan);
    seen.insert (tag);
    onAirUid = entry.uid;
    nextUid = index + 1 < (int) p->entries.size() ? p->entries[(size_t) index + 1].uid : 0;
    queueFollow (tag, entry.uid);
    sendChangeMessage();
}

void ShowController::queueFollow (int tag, int uid)
{
    chained.insert (tag);
    auto* p = currentPlaylist();
    const int index = indexOfUid (uid);
    if (p == nullptr || index < 0 || index + 1 >= (int) p->entries.size())
        return;
    const auto& e = p->entries[(size_t) index];
    if (e.transition == Transition::stop)
        return;
    const auto& next = p->entries[(size_t) index + 1];
    const int nextTag = ++lastTag;
    PlayPlan plan;
    if (! makePlan (next, nextTag, plan))
        return;
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
    // the next update queues them again from the playlist as it is now
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
            if (auto t = tagUid.find (it->tag); t != tagUid.end())
            {
                onAirUid = t->second;
                const int index = indexOfUid (onAirUid);
                auto* p = currentPlaylist();
                nextUid = (p != nullptr && index >= 0 && index + 1 < (int) p->entries.size())
                            ? p->entries[(size_t) index + 1].uid : 0;
                changed = true;
            }
        }
    }
    for (auto& v : voices)
        if (v.tag != 0 && ! v.stopping && chained.count (v.tag) == 0)
            if (auto t = tagUid.find (v.tag); t != tagUid.end())
                queueFollow (v.tag, t->second);

    // forget songs that have finished (not while commands are still on their way to the engine)
    if (! engine.hasPendingCommands())
    {
        for (auto it = seen.begin(); it != seen.end();)
        {
            if (playing.count (*it) == 0)
            {
                chained.erase (*it);
                tagUid.erase (*it);
                it = seen.erase (it);
            }
            else
                ++it;
        }
        if (playing.empty() && onAirUid != 0)
        {
            onAirUid = 0;
            changed = true;
        }
    }
    if (changed)
        sendChangeMessage();
}
