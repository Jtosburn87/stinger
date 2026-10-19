#include "Controls.h"

MidiTrigger MidiTrigger::fromMessage (const juce::MidiMessage& m)
{
    MidiTrigger t;
    t.channel = m.getChannel();
    if (m.isNoteOn())
    {
        t.type = note;
        t.number = m.getNoteNumber();
    }
    else if (m.isController() && m.getControllerValue() >= 64)
    {
        t.type = controller;
        t.number = m.getControllerNumber();
    }
    else if (m.isProgramChange())
    {
        t.type = program;
        t.number = m.getProgramChangeNumber();
    }
    return t;
}

bool MidiTrigger::fires (const juce::MidiMessage& m) const
{
    if (type == none || (channel != 0 && m.getChannel() != channel))
        return false;
    switch (type)
    {
        case note:       return m.isNoteOn() && m.getNoteNumber() == number;
        case controller: return m.isController() && m.getControllerNumber() == number && m.getControllerValue() >= 64;
        case program:    return m.isProgramChange() && m.getProgramChangeNumber() == number;
        case none:       break;
    }
    return false;
}

juce::String MidiTrigger::describe() const
{
    const juce::String ch = channel == 0 ? juce::String (" any ch") : " ch " + juce::String (channel);
    switch (type)
    {
        case note:       return "Note " + juce::MidiMessage::getMidiNoteName (number, true, true, 3) + ch;
        case controller: return "CC " + juce::String (number) + ch;
        case program:    return "Program " + juce::String (number + 1) + ch;
        case none:       break;
    }
    return {};
}

juce::var MidiTrigger::toVar() const
{
    if (type == none)
        return {};
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", (int) type);
    o->setProperty ("channel", channel);
    o->setProperty ("number", number);
    return o;
}

MidiTrigger MidiTrigger::fromVar (const juce::var& v)
{
    MidiTrigger t;
    if (! v.isObject())
        return t;
    t.type = (Type) juce::jlimit (0, 3, (int) v.getProperty ("type", 0));
    t.channel = juce::jlimit (0, 16, (int) v.getProperty ("channel", 0));
    t.number = juce::jlimit (0, 127, (int) v.getProperty ("number", 0));
    return t;
}

// ---------------------------------------------------------------------------------------------

const std::vector<CommandMap::Command>& CommandMap::commands()
{
    static const std::vector<Command> list = []
    {
        std::vector<Command> c;
        auto add = [&c] (const char* id, const juce::String& name, const juce::String& key = {})
        {
            Binding b;
            b.key = key;
            c.push_back ({ id, name, b });
        };
        add ("go", "GO (playlist)", juce::KeyPress (juce::KeyPress::spaceKey).getTextDescription());
        add ("fadeNext", "Fade to next (playlist)", juce::KeyPress (juce::KeyPress::returnKey).getTextDescription());
        add ("stopAll", "Stop all", juce::KeyPress (juce::KeyPress::escapeKey).getTextDescription());
        add ("fadeAll", "Fade all");
        add ("nextDown", "Playlist: NEXT one down");
        add ("nextUp", "Playlist: NEXT one up");
        add ("bankNext", "Next bank");
        add ("bankPrev", "Previous bank");
        for (int b = 0; b < 8; ++b)
            add (("bank" + juce::String (b + 1)).toRawUTF8(), "Bank " + juce::String::charToString ((juce::juce_wchar) ('A' + b)),
                 b < 4 ? juce::KeyPress ('1' + b, juce::ModifierKeys::commandModifier, 0).getTextDescription() : juce::String());
        add ("edit", "Edit mode on/off", juce::KeyPress ('e', juce::ModifierKeys::commandModifier, 0).getTextDescription());
        add ("globalKeys", "Global keys on/off");
        static const char* rows[4] = { "123456", "QWERTY", "ASDFGH", "ZXCVBN" };
        for (int s = 0; s < 24; ++s)
            add (("slot" + juce::String (s + 1)).toRawUTF8(), "Button " + juce::String (s + 1) + " of the shown bank",
                 juce::KeyPress (rows[s / 6][s % 6]).getTextDescription());
        return c;
    }();
    return list;
}

juce::String CommandMap::nameOf (const juce::String& id)
{
    for (auto& c : commands())
        if (c.id == id)
            return c.name;
    return id;
}

void CommandMap::resetToDefaults()
{
    bindings.clear();
    for (auto& c : commands())
        bindings[c.id] = c.defaults;
}

Binding CommandMap::get (const juce::String& id) const
{
    auto it = bindings.find (id);
    return it == bindings.end() ? Binding() : it->second;
}

juce::String CommandMap::commandForKey (const juce::KeyPress& key) const
{
    for (auto& c : commands())   // in list order: stable when two commands share a key
        if (auto b = get (c.id); b.key.isNotEmpty() && b.keyPress() == key)
            return c.id;
    return {};
}

juce::String CommandMap::commandForMidi (const juce::MidiMessage& m) const
{
    for (auto& c : commands())
        if (get (c.id).midi.fires (m))
            return c.id;
    return {};
}

juce::var CommandMap::toVar() const
{
    auto* o = new juce::DynamicObject();
    for (auto& [id, b] : bindings)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty ("key", b.key);
        entry->setProperty ("midi", b.midi.toVar());
        o->setProperty (id, entry);
    }
    return o;
}

void CommandMap::fromVar (const juce::var& v)
{
    resetToDefaults();
    if (auto* o = v.getDynamicObject())
        for (auto& p : o->getProperties())
        {
            Binding b;
            b.key = p.value.getProperty ("key", {}).toString();
            b.midi = MidiTrigger::fromVar (p.value.getProperty ("midi", {}));
            bindings[p.name.toString()] = b;
        }
}

// ---------------------------------------------------------------------------------------------

bool ControllerEdges::isPress (const juce::MidiMessage& m)
{
    if (m.isNoteOn() || m.isProgramChange())
        return true;
    if (! m.isController())
        return false;
    auto& previous = last[(size_t) ((m.getChannel() - 1) * 128 + m.getControllerNumber())];
    const auto value = (juce::uint8) m.getControllerValue();
    const bool press = value >= 64 && previous < 64;
    previous = value;
    return press;
}
