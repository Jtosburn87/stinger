#include "OscBridge.h"

OscBridge::OscBridge (ShowController& c) : controller (c)
{
    receiver.addListener (this);
}

OscBridge::~OscBridge()
{
    stop();
    receiver.removeListener (this);
}

bool OscBridge::start (int inputPort, const juce::String& sendHost, int sendPort)
{
    stop();
    error.clear();
    if (! receiver.connect (inputPort))
    {
        error = "Port " + juce::String (inputPort) + " can't be opened (is something else using it?)";
        return false;
    }
    canSend = sendHost.isNotEmpty() && sendPort > 0 && sender.connect (sendHost, sendPort);
    on = true;
    lastSent.clear();
    startTimerHz (10);
    return true;
}

void OscBridge::stop()
{
    stopTimer();
    receiver.disconnect();
    sender.disconnect();
    on = canSend = false;
}

juce::String OscBridge::actionFor (const juce::OSCMessage& m)
{
    if (m.size() == 1 && ((m[0].isInt32() && m[0].getInt32() == 0) || (m[0].isFloat32() && m[0].getFloat32() == 0.0f)))
        return {};
    const auto parts = juce::StringArray::fromTokens (m.getAddressPattern().toString().toLowerCase(), "/", {});
    // "" "stinger" "go" ...
    if (parts.size() < 3 || parts[0].isNotEmpty() || parts[1] != "stinger")
        return {};
    const auto word = parts[2];
    const auto arg = parts[3];
    const auto number = arg.isNotEmpty() ? arg.getIntValue() : (m.size() == 1 && m[0].isInt32() ? m[0].getInt32() : 0);

    if (word == "go")          return number > 0 ? "go" + juce::String (number) : juce::String ("go");
    if (word == "fadenext")    return "fadeNext";
    if (word == "stop")        return "stopAll";
    if (word == "fade")        return "fadeAll";
    if (word == "selfdestruct") return "selfDestruct";
    if (word == "resetcolours") return "resetColours";
    if (word == "hold")        return number > 0 ? "hold" + juce::String (number) : juce::String();
    if (word == "slot")        return number > 0 ? "slot" + juce::String (number) : juce::String();
    if (word == "bank")
    {
        if (arg == "next") return "bankNext";
        if (arg == "prev") return "bankPrev";
        return number > 0 ? "bank" + juce::String (number) : juce::String();
    }
    if (word == "cue" && parts.size() >= 5)     // /stinger/cue/<bank>/<button>, both from 1 (or a bank letter)
    {
        const int bank = arg.containsOnly ("0123456789") ? arg.getIntValue() - 1 : (int) (arg[0] - 'a');
        const int slot = parts[4].getIntValue() - 1;
        if (bank >= 0 && slot >= 0 && slot < Show::cuesPerBank)
            return "cue:" + juce::String (Show::cueId (bank, slot));
        return {};
    }
    if (word == "run")
    {
        if (arg == "go")   return "runGo";
        if (arg == "next") return "runNext";
        if (arg == "prev") return "runPrev";
    }
    if (word == "schedule")
    {
        if (arg == "start") return "schedStart";
        if (arg == "go")    return "schedGo";
    }
    return {};
}

void OscBridge::oscMessageReceived (const juce::OSCMessage& m)
{
    if (m.getAddressPattern().toString().equalsIgnoreCase ("/stinger/refresh"))
    {
        lastSent.clear();
        return;
    }
    if (const auto action = actionFor (m); action.isNotEmpty() && perform)
        perform (action);
}

void OscBridge::sendText (const juce::String& address, const juce::String& value)
{
    auto& last = lastSent[address];
    if (last == value && last.isNotEmpty())
        return;
    last = value;
    sender.send (juce::OSCAddressPattern (address), value);
}

void OscBridge::sendFloat (const juce::String& address, float value)
{
    auto& last = lastSent[address];
    const auto text = juce::String (value, 1);
    if (last == text)
        return;
    last = text;
    sender.send (juce::OSCAddressPattern (address), value);
}

void OscBridge::sendInt (const juce::String& address, int value)
{
    auto& last = lastSent[address];
    const juce::String text (value);
    if (last == text)
        return;
    last = text;
    sender.send (juce::OSCAddressPattern (address), value);
}

void OscBridge::sendChanges()
{
    if (! canSend)
        return;
    if (++ticks >= 50)
    {
        ticks = 0;
        lastSent.clear();   // every 5 s everything again, for whoever connected late
    }
    auto& show = controller.getShow();
    const auto playing = controller.newestVoice();
    sendInt ("/stinger/now/playing", playing.any ? 1 : 0);
    sendText ("/stinger/now/name", playing.any ? playing.name : juce::String (" "));
    sendText ("/stinger/now/time", playing.any ? (playing.looping ? juce::String ("LOOP") : durationText (std::ceil (playing.left))) : juce::String ("--:--"));
    sendFloat ("/stinger/now/seconds", (float) playing.left);

    const int selected = controller.getSelectedPlaylist();
    const int next = controller.getNextEntry (selected);
    const auto* list = controller.playlistAt (selected);
    const int nextCue = list != nullptr && next >= 0 && next < (int) list->entries.size() ? list->entries[(size_t) next].cueId : -1;
    sendText ("/stinger/next/name", show.isValid (nextCue) ? show.cue (nextCue).name : juce::String (" "));

    auto runText = [&show] (int row)
    {
        if (row < 0 || row >= (int) show.run.size())
            return juce::String (" ");
        const auto& r = show.run[(size_t) row];
        return r.person.isNotEmpty() ? r.person : show.isValid (r.cueId) ? show.cue (r.cueId).name : juce::String ("(note)");
    };
    sendText ("/stinger/run/next", runText (controller.getRunNext()));
    sendText ("/stinger/run/onair", runText (controller.getRunOnAir()));

    const int scheduled = controller.getNextScheduled();
    sendText ("/stinger/schedule/name", scheduled >= 0 ? controller.getScheduleName (scheduled) : juce::String (" "));
    sendText ("/stinger/schedule/time", scheduled >= 0 ? durationText (std::ceil (controller.getScheduleRemaining())) : juce::String ("--:--"));
    sendFloat ("/stinger/schedule/seconds", (float) controller.getScheduleRemaining());
    const auto urgency = controller.getScheduleUrgency();
    sendText ("/stinger/schedule/colour", scheduled < 0 ? juce::String ("off")
                                        : urgency == ShowController::Urgency::red ? juce::String ("red")
                                        : urgency == ShowController::Urgency::yellow ? juce::String ("yellow") : juce::String ("green"));

    const int bank = shownBank ? shownBank() : 0;
    if (bank >= 0 && bank < (int) show.banks.size())
    {
        sendText ("/stinger/bank", show.banks[(size_t) bank].name);
        for (int s = 0; s < Show::cuesPerBank; ++s)
        {
            const int id = Show::cueId (bank, s);
            const juce::String base ("/stinger/slot/" + juce::String (s + 1));
            sendText (base + "/name", show.cue (id).isEmpty() ? juce::String (" ") : show.cue (id).name);
            sendInt (base + "/playing", controller.getEngine().isCuePlaying (id) ? 1 : 0);
        }
    }
}

juce::String OscBridge::helpText()
{
    return "STINGER LISTENS FOR\n"
           "/stinger/go  (selected playlist)    /stinger/go/2  (playlist 2)\n"
           "/stinger/fadenext    /stinger/stop    /stinger/fade\n"
           "/stinger/slot/5  (button 5 of the shown bank)\n"
           "/stinger/cue/1/5  or  /stinger/cue/a/5  (bank, button)\n"
           "/stinger/bank/2    /stinger/bank/next    /stinger/bank/prev\n"
           "/stinger/run/go    /stinger/run/next    /stinger/run/prev\n"
           "/stinger/schedule/start    /stinger/schedule/go\n"
           "/stinger/hold/1  /2  /5  /10\n"
           "/stinger/selfdestruct    /stinger/resetcolours\n"
           "/stinger/refresh  (send everything again)\n"
           "A value of 0 is ignored, so a button that sends 1 then 0 fires once.\n\n"
           "STINGER SENDS\n"
           "/stinger/now/name  /now/time  /now/seconds  /now/playing\n"
           "/stinger/next/name    /stinger/run/next    /stinger/run/onair\n"
           "/stinger/schedule/name  /time  /seconds  /colour  (green, yellow, red, off)\n"
           "/stinger/bank    /stinger/slot/N/name    /stinger/slot/N/playing\n";
}
