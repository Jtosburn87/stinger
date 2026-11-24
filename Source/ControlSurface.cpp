#include "ControlSurface.h"

ControlSurface::ControlSurface (ShowController& c, juce::PropertiesFile& s) : controller (c), settings (s)
{
    commands.fromVar (juce::JSON::parse (settings.getValue ("commands")));
    global = settings.getBoolValue ("globalKeys", false);
   #if JUCE_ANDROID
    global = false;   // Android (Samsung DeX) can't give an app keys from other apps
   #endif
    levels.playing = settings.getIntValue ("fbPlaying", 127);
    levels.next = settings.getIntValue ("fbNext", 64);
    levels.loaded = settings.getIntValue ("fbLoaded", 0);
    allInputs = settings.getBoolValue ("midiInputsAll", true);
    if (auto* list = juce::JSON::parse (settings.getValue ("midiInputsOn")).getArray())
        for (auto& v : *list)
            if (v["identifier"].toString().isNotEmpty() || v["name"].toString().isNotEmpty())
                tickedInputs.push_back ({ v["identifier"].toString(), v["name"].toString() });

    hotkeys.onKey = [this] (const juce::KeyPress& key) { handleKey (key); };
    controller.addChangeListener (this);
    refreshInputs();
    setFeedbackOutput (settings.getValue ("midiFeedbackOut"));
    updateGlobalKeys();
    startTimerHz (20);
}

ControlSurface::~ControlSurface()
{
    alive->store (false);
    controller.removeChangeListener (this);
    stopTimer();
    for (auto& in : inputs)
        in->stop();
    hotkeys.setKeys ({});
}

void ControlSurface::save()
{
    settings.setValue ("commands", juce::JSON::toString (commands.toVar(), true));
    settings.setValue ("globalKeys", global);
    settings.setValue ("midiFeedbackOut", feedbackId);
    settings.setValue ("fbPlaying", levels.playing);
    settings.setValue ("fbNext", levels.next);
    settings.setValue ("fbLoaded", levels.loaded);
    settings.setValue ("midiInputsAll", allInputs);
    juce::Array<juce::var> ticked;
    for (auto& t : tickedInputs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("identifier", t.identifier);
        o->setProperty ("name", t.name);
        ticked.add (juce::var (o));
    }
    settings.setValue ("midiInputsOn", juce::JSON::toString (juce::var (ticked), true));
    settings.saveIfNeeded();
}

juce::String ControlSurface::describe (const Binding& b)
{
    juce::StringArray parts;
    if (b.key.isNotEmpty())
        parts.add (b.keyPress().getTextDescriptionWithIcons());
    if (b.midi.isSet())
        parts.add (b.midi.describe());
    return parts.joinIntoString ("   |   ");
}

Binding ControlSurface::bindingFor (const juce::String& action) const
{
    if (action.startsWith ("cue:"))
    {
        const int id = action.fromFirstOccurrenceOf ("cue:", false, false).getIntValue();
        auto& show = const_cast<ShowController&> (controller).getShow();
        if (! show.isValid (id))
            return {};
        Binding b;
        b.key = show.cue (id).hotkey;
        b.midi = show.cue (id).midi;
        return b;
    }
    return commands.get (action);
}

void ControlSurface::setCommandBinding (const juce::String& id, const Binding& b)
{
    commands.bindings[id] = b;
    save();
    updateGlobalKeys();
    sendChangeMessage();
}

void ControlSurface::resetCommands()
{
    commands.resetToDefaults();
    save();
    updateGlobalKeys();
    sendChangeMessage();
}

// ---------------------------------------------------------------------------------------------
// Keys

bool ControlSurface::handleKey (const juce::KeyPress& key)
{
    if (learnTarget.isNotEmpty())
    {
        assign (learnTarget, &key, nullptr);
        return true;
    }
    if (! perform)
        return false;

    auto& show = controller.getShow();
    const int bank = shownBank ? shownBank() : 0;
    auto cueKey = [&] (int id)
    {
        const auto& c = show.cue (id);
        return ! c.isEmpty() && c.hotkey.isNotEmpty() && juce::KeyPress::createFromDescription (c.hotkey) == key;
    };
    // the shown bank's own keys, then the commands (incl. "button N of the shown bank"), then other banks' keys
    for (int s = 0; s < Show::cuesPerBank; ++s)
        if (cueKey (Show::cueId (bank, s)))
        {
            perform ("cue:" + juce::String (Show::cueId (bank, s)));
            return true;
        }
    if (auto id = commands.commandForKey (key); id.isNotEmpty())
    {
        perform (id);
        return true;
    }
    for (int b = 0; b < (int) show.banks.size(); ++b)
        for (int s = 0; s < Show::cuesPerBank; ++s)
            if (b != bank && cueKey (Show::cueId (b, s)))
            {
                perform ("cue:" + juce::String (Show::cueId (b, s)));
                return true;
            }
    return false;
}

void ControlSurface::setGlobal (bool on)
{
   #if JUCE_ANDROID
    on = false;   // see the constructor
   #endif
    global = on;
    save();
    updateGlobalKeys();
    sendChangeMessage();
}

void ControlSurface::updateGlobalKeys()
{
    juce::Array<juce::KeyPress> keys;
    if (global && ! typing)   // registered keys reach Stinger before any text box does: a 1 typed would play button 1
    {
        for (auto& [id, b] : commands.bindings)
            if (b.key.isNotEmpty())
                keys.addIfNotAlreadyThere (b.keyPress());
        for (auto& bank : controller.getShow().banks)
            for (auto& c : bank.cues)
                if (! c.isEmpty() && c.hotkey.isNotEmpty())
                    keys.addIfNotAlreadyThere (juce::KeyPress::createFromDescription (c.hotkey));
    }
    const auto refused = hotkeys.setKeys (keys);
    if (refused != refusedKeys)
    {
        refusedKeys = refused;
        sendChangeMessage();
    }
}

// ---------------------------------------------------------------------------------------------
// Learning

void ControlSurface::learn (const juce::String& target)
{
    learnTarget = target;
    sendChangeMessage();
}

void ControlSurface::cancelLearn()
{
    learnTarget.clear();
    sendChangeMessage();
}

void ControlSurface::assign (juce::String target, const juce::KeyPress* key, const MidiTrigger* midi)
{
    // target is a copy: it's usually learnTarget itself, cleared next
    learnTarget.clear();
    if (target.startsWith ("cue:"))
    {
        const int id = target.fromFirstOccurrenceOf ("cue:", false, false).getIntValue();
        auto& show = controller.getShow();
        if (show.isValid (id))
        {
            if (key != nullptr) show.cue (id).hotkey = key->getTextDescription();
            if (midi != nullptr) show.cue (id).midi = *midi;
            controller.cueChanged (id);
        }
    }
    else
    {
        auto b = commands.get (target);
        if (key != nullptr) b.key = key->getTextDescription();
        if (midi != nullptr) b.midi = *midi;
        commands.bindings[target] = b;
        save();
    }
    updateGlobalKeys();
    sendChangeMessage();
}

// ---------------------------------------------------------------------------------------------
// MIDI in

void ControlSurface::refreshInputs()
{
    const auto available = juce::MidiInput::getAvailableDevices();
    juce::StringArray ids;
    for (auto& d : available)
        ids.add (d.identifier + "|" + d.name);
    bool changed = ids != lastAvailable;
    lastAvailable = ids;

    auto wanted = [&] (const juce::MidiDeviceInfo& d) { return allInputs || isTicked (d.identifier, d.name); };
    // close inputs that went away or were unticked
    const auto before = inputs.size();
    inputs.erase (std::remove_if (inputs.begin(), inputs.end(), [&] (auto& in)
    {
        for (auto& d : available)
            if (d.identifier == in->getIdentifier() && wanted (d))
                return false;
        in->stop();
        return true;
    }), inputs.end());
    changed = changed || inputs.size() != before;
    // open new ones (a Bluetooth footswitch that just connected, a controller plugged in, one just ticked...)
    for (auto& d : available)
    {
        const bool open = std::any_of (inputs.begin(), inputs.end(), [&] (auto& in) { return in->getIdentifier() == d.identifier; });
        if (open || ! wanted (d))
            continue;
        if (auto in = juce::MidiInput::openDevice (d.identifier, this))
        {
            in->start();
            inputs.push_back (std::move (in));
            changed = true;
        }
    }
    if (changed)
        sendChangeMessage();
}

juce::StringArray ControlSurface::getInputNames() const
{
    juce::StringArray names;
    for (auto& in : inputs)
        names.add (in->getName());
    return names;
}

bool ControlSurface::isTicked (const juce::String& identifier, const juce::String& name) const
{
    // the identifier first; a device that comes back with a new one (another USB port, re-paired) by its name
    for (auto& t : tickedInputs)
        if (t.identifier.isNotEmpty() && t.identifier == identifier)
            return true;
    for (auto& t : tickedInputs)
        if (t.name.isNotEmpty() && t.name == name)
            return true;
    return false;
}

std::vector<ControlSurface::InputChoice> ControlSurface::getInputChoices() const
{
    std::vector<InputChoice> list;
    for (auto& d : juce::MidiInput::getAvailableDevices())
        list.push_back ({ d.name, d.identifier, true, allInputs || isTicked (d.identifier, d.name) });
    if (! allInputs)
        for (auto& t : tickedInputs)
            if (std::none_of (list.begin(), list.end(), [&] (auto& c) { return c.identifier == t.identifier || c.name == t.name; }))
                list.push_back ({ t.name, t.identifier, false, true });
    return list;
}

void ControlSurface::setAllInputs (bool on)
{
    if (on == allInputs)
        return;
    if (! on)
    {
        // nothing changes until something is unticked: every input there now starts ticked
        tickedInputs.clear();
        for (auto& d : juce::MidiInput::getAvailableDevices())
            tickedInputs.push_back ({ d.identifier, d.name });
    }
    allInputs = on;
    save();
    refreshInputs();
    sendChangeMessage();
}

void ControlSurface::setInputOn (const InputChoice& input, bool on)
{
    if (allInputs)
        return;
    tickedInputs.erase (std::remove_if (tickedInputs.begin(), tickedInputs.end(), [&] (auto& t)
    {
        return (t.identifier.isNotEmpty() && t.identifier == input.identifier) || (t.name.isNotEmpty() && t.name == input.name);
    }), tickedInputs.end());
    if (on)
        tickedInputs.push_back ({ input.identifier, input.name });
    save();
    refreshInputs();
    sendChangeMessage();
}

void ControlSurface::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    if (! (m.isNoteOn() || m.isController() || m.isProgramChange()))
        return;
    const auto name = source != nullptr ? source->getName() : juce::String();
    juce::MessageManager::callAsync ([this, m, name, alive = alive]
    {
        if (! alive->load())
            return;
        lastMidi = name + ": " + m.getDescription();
        handleMidi (m);
    });
}

void ControlSurface::handleMidi (const juce::MidiMessage& m)
{
    const bool press = edges.isPress (m);
    if (learnTarget.isNotEmpty())
    {
        const auto t = MidiTrigger::fromMessage (m);
        if (t.isSet() && press)
            assign (learnTarget, nullptr, &t);
        return;
    }
    if (! press || ! perform)
        return;

    auto& show = controller.getShow();
    for (int b = 0; b < (int) show.banks.size(); ++b)
        for (int s = 0; s < Show::cuesPerBank; ++s)
        {
            const int id = Show::cueId (b, s);
            if (! show.cue (id).isEmpty() && show.cue (id).midi.fires (m))
            {
                perform ("cue:" + juce::String (id));
                return;
            }
        }
    if (auto id = commands.commandForMidi (m); id.isNotEmpty())
        perform (id);
}

// ---------------------------------------------------------------------------------------------
// MIDI light feedback

void ControlSurface::setFeedbackOutput (const juce::String& identifier)
{
    feedback.reset();
    sent.clear();
    feedbackId = identifier;
    if (identifier.isNotEmpty())
        feedback = juce::MidiOutput::openDevice (identifier);
    save();
    sendChangeMessage();
}

void ControlSurface::setFeedbackLevels (FeedbackLevels l)
{
    levels = l;
    sent.clear();
    save();
}

void ControlSurface::sendFeedback()
{
    if (feedback == nullptr)
        return;
    auto& show = controller.getShow();
    auto& engine = controller.getEngine();
    int nextCue = -1;
    if (auto* p = controller.currentPlaylist(); p != nullptr)
        if (const int n = controller.getNextEntry (controller.getSelectedPlaylist()); n >= 0 && n < (int) p->entries.size())
            nextCue = p->entries[(size_t) n].cueId;

    auto stateOf = [&] (int id)
    {
        if (! show.isValid (id) || show.cue (id).isEmpty())
            return 0;
        if (engine.isCuePlaying (id))
            return levels.playing;
        if (id == nextCue)
            return levels.next;
        return levels.loaded;
    };
    auto send = [&] (const MidiTrigger& t, int value)
    {
        if (! t.isSet() || t.type == MidiTrigger::program)
            return;
        const int channel = t.channel == 0 ? 1 : t.channel;
        const auto key = juce::String ((int) t.type) + "/" + juce::String (channel) + "/" + juce::String (t.number);
        if (auto it = sent.find (key); it != sent.end() && it->second == value)
            return;
        sent[key] = value;
        feedback->sendMessageNow (t.type == MidiTrigger::note
                                    ? juce::MidiMessage::noteOn (channel, t.number, (juce::uint8) juce::jlimit (0, 127, value))
                                    : juce::MidiMessage::controllerEvent (channel, t.number, juce::jlimit (0, 127, value)));
    };

    for (int b = 0; b < (int) show.banks.size(); ++b)
        for (int s = 0; s < Show::cuesPerBank; ++s)
            send (show.cue (Show::cueId (b, s)).midi, stateOf (Show::cueId (b, s)));
    const int bank = shownBank ? shownBank() : 0;
    for (int s = 0; s < Show::cuesPerBank; ++s)
        send (commands.get ("slot" + juce::String (s + 1)).midi, stateOf (Show::cueId (bank, s)));
    auto goState = [&] (int pl) { return controller.isLoopingUntilGo (pl) || controller.getNextEntry (pl) >= 0 ? levels.next : 0; };
    send (commands.get ("go").midi, goState (controller.getSelectedPlaylist()));
    for (int p = 0; p < 8; ++p)
        send (commands.get ("go" + juce::String (p + 1)).midi, p < (int) show.playlists.size() ? goState (p) : 0);
    bool anyPlaying = ! engine.getVoices().empty();
    send (commands.get ("stopAll").midi, anyPlaying ? levels.playing : 0);
    send (commands.get ("fadeAll").midi, anyPlaying ? levels.playing : 0);
    for (int b = 0; b < (int) show.banks.size() && b < 8; ++b)
        send (commands.get ("bank" + juce::String (b + 1)).midi, b == bank ? levels.playing : levels.loaded);
}

void ControlSurface::timerCallback()
{
    if (++ticks % 40 == 0)   // every 2 s: new / gone MIDI devices
        refreshInputs();
    if (const auto* text = dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()); (text != nullptr && ! text->isReadOnly()) != typing)
    {
        typing = ! typing;
        updateGlobalKeys();
    }
    sendFeedback();
}
