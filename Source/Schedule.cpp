#include "Show.h"

// ShowController: the run of show (an ordered list of people, songs and notes with its own GO) and the schedule
// (buttons that start by themselves at a time, with a countdown and holds).

namespace
{
    constexpr juce::int64 lateLimitMs = 2000;   // this far past its time (a stalled PC) a trigger is missed, not fired
}

juce::var RunRow::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("cue", cueId);
    o->setProperty ("time", time);
    o->setProperty ("person", person);
    o->setProperty ("notes", notes);
    return o;
}

RunRow RunRow::fromVar (const juce::var& v)
{
    RunRow r;
    r.cueId = (int) v.getProperty ("cue", -1);
    r.time = v.getProperty ("time", {}).toString();
    r.person = v.getProperty ("person", {}).toString();
    r.notes = v.getProperty ("notes", {}).toString();
    return r;
}

juce::var ScheduleItem::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("cue", cueId);
    o->setProperty ("kind", (int) kind);
    o->setProperty ("time", secondsOfDay);
    o->setProperty ("delay", delaySeconds);
    return o;
}

ScheduleItem ScheduleItem::fromVar (const juce::var& v)
{
    ScheduleItem s;
    s.cueId = (int) v.getProperty ("cue", -1);
    s.kind = (Kind) juce::jlimit (0, 1, (int) v.getProperty ("kind", 0));
    s.secondsOfDay = juce::jlimit (0, 86399, (int) v.getProperty ("time", 0));
    s.delaySeconds = juce::jlimit (0.0, 86400.0, (double) v.getProperty ("delay", 60.0));
    return s;
}

// ---- Run of show ----------------------------------------------------------------------------

void ShowController::assignRunUids()
{
    for (auto& r : show.run)
        r.uid = newUid();
    runNextUid = show.run.empty() ? 0 : show.run.front().uid;
    runOnAirUid = 0;
}

int ShowController::runIndexOf (int uid) const
{
    for (int i = 0; i < (int) show.run.size(); ++i)
        if (show.run[(size_t) i].uid == uid)
            return i;
    return -1;
}

int ShowController::addRunRow (const RunRow& row, int insertAt)
{
    auto r = row;
    r.uid = newUid();
    if (insertAt < 0 || insertAt > (int) show.run.size())
        insertAt = (int) show.run.size();
    show.run.insert (show.run.begin() + insertAt, r);
    if (runNextUid == 0 && runOnAirUid == 0)
        runNextUid = r.uid;
    dirty = true;
    sendChangeMessage();
    return insertAt;
}

void ShowController::removeRunRow (int index)
{
    if (index < 0 || index >= (int) show.run.size())
        return;
    if (show.run[(size_t) index].uid == runNextUid)
        runNextUid = index + 1 < (int) show.run.size() ? show.run[(size_t) index + 1].uid : 0;
    show.run.erase (show.run.begin() + index);
    dirty = true;
    sendChangeMessage();
}

void ShowController::moveRunRow (int from, int to)
{
    if (from < 0 || from >= (int) show.run.size())
        return;
    to = juce::jlimit (0, (int) show.run.size() - 1, to);
    if (from == to)
        return;
    const auto row = show.run[(size_t) from];
    show.run.erase (show.run.begin() + from);
    show.run.insert (show.run.begin() + to, row);
    dirty = true;
    sendChangeMessage();
}

void ShowController::setRunRow (int index, const RunRow& row)
{
    if (index < 0 || index >= (int) show.run.size())
        return;
    const int uid = show.run[(size_t) index].uid;
    show.run[(size_t) index] = row;
    show.run[(size_t) index].uid = uid;
    dirty = true;
    sendChangeMessage();
}

void ShowController::setRunNext (int index)
{
    if (index >= 0 && index < (int) show.run.size())
    {
        runNextUid = show.run[(size_t) index].uid;
        sendChangeMessage();
    }
}

void ShowController::runGo()
{
    goPressed();
    const int index = getRunNext();
    if (index < 0)
        return;
    const auto row = show.run[(size_t) index];
    if (show.isValid (row.cueId) && ! show.cue (row.cueId).isEmpty())
        play (row.cueId);
    autosaveNow = true;
    runOnAirUid = row.uid;
    runNextUid = index + 1 < (int) show.run.size() ? show.run[(size_t) index + 1].uid : 0;
    sendChangeMessage();
}

// ---- Schedule -------------------------------------------------------------------------------

juce::int64 ShowController::nowMs() const
{
    return clockForTest ? clockForTest() : juce::Time::currentTimeMillis();
}

void ShowController::addScheduleItem (const ScheduleItem& item)
{
    auto s = item;
    s.uid = newUid();
    show.schedule.push_back (s);
    if (scheduleRunning)
        armItem (show.schedule.back(), nowMs());
    dirty = true;
    sendChangeMessage();
}

void ShowController::removeScheduleItem (int index)
{
    if (index < 0 || index >= (int) show.schedule.size())
        return;
    show.schedule.erase (show.schedule.begin() + index);
    dirty = true;
    sendChangeMessage();
}

void ShowController::setScheduleItem (int index, const ScheduleItem& item)
{
    if (index < 0 || index >= (int) show.schedule.size())
        return;
    auto s = item;
    s.uid = show.schedule[(size_t) index].uid;
    show.schedule[(size_t) index] = s;
    if (scheduleRunning)
        armItem (show.schedule[(size_t) index], nowMs());
    dirty = true;
    sendChangeMessage();
}

void ShowController::armItem (ScheduleItem& s, juce::int64 now)
{
    s.held = false;
    s.heldSeconds = 0.0;
    if (s.kind == ScheduleItem::delay)
        s.fireAt = now + (juce::int64) (s.delaySeconds * 1000.0);
    else
    {
        const juce::Time t (now);
        const auto midnight = juce::Time (t.getYear(), t.getMonth(), t.getDayOfMonth(), 0, 0, 0, 0, true).toMilliseconds();
        s.fireAt = midnight + (juce::int64) s.secondsOfDay * 1000;
    }
    s.state = s.fireAt < now ? ScheduleItem::missed : ScheduleItem::waiting;   // never fires late
}

void ShowController::startSchedule()
{
    const auto now = nowMs();
    for (auto& s : show.schedule)
        armItem (s, now);
    scheduleRunning = true;
    autosaveNow = true;
    sendChangeMessage();
}

void ShowController::stopSchedule()
{
    scheduleRunning = false;
    for (auto& s : show.schedule)
    {
        s.state = ScheduleItem::waiting;
        s.held = false;
        s.heldSeconds = 0.0;
    }
    sendChangeMessage();
}

int ShowController::getNextScheduled() const
{
    if (! scheduleRunning)
        return -1;
    int best = -1;
    for (int i = 0; i < (int) show.schedule.size(); ++i)
    {
        const auto& s = show.schedule[(size_t) i];
        if (s.state == ScheduleItem::waiting && (best < 0 || s.fireAt < show.schedule[(size_t) best].fireAt))
            best = i;
    }
    return best;
}

double ShowController::getScheduleRemaining() const
{
    const int i = getNextScheduled();
    return i < 0 ? 0.0 : std::max (0.0, (double) (show.schedule[(size_t) i].fireAt - nowMs()) / 1000.0);
}

ShowController::Urgency ShowController::getScheduleUrgency() const
{
    const auto left = getScheduleRemaining();
    return left <= 10.0 ? Urgency::red : left <= 60.0 ? Urgency::yellow : Urgency::green;
}

juce::String ShowController::getScheduleName (int index) const
{
    if (index < 0 || index >= (int) show.schedule.size())
        return {};
    const int id = show.schedule[(size_t) index].cueId;
    if (! show.isValid (id))
        return "(no button)";
    return show.cue (id).name.isNotEmpty() ? show.cue (id).name : juce::String ("(empty button)");
}

int ShowController::getMissedCount() const
{
    int n = 0;
    if (scheduleRunning)
        for (auto& s : show.schedule)
            n += s.state == ScheduleItem::missed ? 1 : 0;
    return n;
}

void ShowController::holdSchedule (int minutes)
{
    const int i = getNextScheduled();
    if (i < 0 || minutes <= 0)
        return;
    auto& s = show.schedule[(size_t) i];
    s.fireAt += (juce::int64) minutes * 60000;
    s.held = true;
    autosaveNow = true;
    s.heldSeconds += minutes * 60.0;
    // a held show: nothing may fire during the hold, so triggers that would land inside it move with it. Those beyond it stay.
    const auto push = (juce::int64) minutes * 60000;
    for (auto& other : show.schedule)
        if (&other != &s && other.state == ScheduleItem::waiting && other.fireAt < s.fireAt)
            other.fireAt += push;
    sendChangeMessage();
}

void ShowController::scheduleGoNow()
{
    const int i = getNextScheduled();
    if (i >= 0)
        fireItem (show.schedule[(size_t) i]);
}

void ShowController::fireItem (ScheduleItem& s)
{
    s.state = ScheduleItem::fired;
    autosaveNow = true;
    if (show.isValid (s.cueId) && ! show.cue (s.cueId).isEmpty() && ! engine.isCuePlaying (s.cueId))
        play (s.cueId);
    sendChangeMessage();
}

void ShowController::goPressed()
{
    if (const int i = getNextScheduled(); i >= 0 && show.schedule[(size_t) i].held)
    {
        show.schedule[(size_t) i].state = ScheduleItem::defeated;
        autosaveNow = true;
        sendChangeMessage();
    }
}

void ShowController::updateSchedule()
{
    if (! scheduleRunning)
        return;
    const auto now = nowMs();
    bool changed = false;
    for (auto& s : show.schedule)
        if (s.state == ScheduleItem::waiting && now >= s.fireAt)
        {
            if (now - s.fireAt > lateLimitMs)
            {
                s.state = ScheduleItem::missed;
                changed = true;
            }
            else
                fireItem (s);
        }
    if (changed)
        sendChangeMessage();
}

juce::String durationText (double seconds)
{
    const int s = (int) std::round (seconds);
    const int h = s / 3600;
    return (h > 0 ? juce::String (h) + ":" + juce::String ((s / 60) % 60).paddedLeft ('0', 2) : juce::String (s / 60))
         + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

ShowController::Playing ShowController::newestVoice() const
{
    Playing p;
    for (int round = 0; round < 2 && ! p.any; ++round)     // a song fading out only counts if nothing else plays
        for (auto& v : engine.getVoices())
            if (! v.preview && (round == 1 || ! v.stopping))
            {
                p.any = true;
                p.left = v.waiting ? v.waitLeft : std::max (0.0, v.length - v.position);
                p.looping = v.looping;
                p.name = show.isValid (v.cueId) ? show.cue (v.cueId).name : juce::String();
                break;
            }
    return p;
}
