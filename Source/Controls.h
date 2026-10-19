#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>   // juce::KeyPress
#include <juce_core/juce_core.h>
#include <array>
#include <map>

/** A MIDI message that triggers something: a note (note-on), a controller (pressed = goes from below 64
    to 64 or more, so a footswitch fires once per press) or a program change. Channel 0 = any. */
struct MidiTrigger
{
    enum Type { none = 0, note, controller, program };
    Type type = none;
    int channel = 0;       // 1-16, 0 = any
    int number = 0;        // note / controller / program number

    bool isSet() const { return type != none; }
    bool operator== (const MidiTrigger& o) const { return type == o.type && channel == o.channel && number == o.number; }

    /** The trigger a message would set when learning (none for note-offs, releases etc.). */
    static MidiTrigger fromMessage (const juce::MidiMessage& m);
    /** True if this message matches the trigger (a controller: value 64 or more). Use ControllerEdges to fire once per press. */
    bool fires (const juce::MidiMessage& m) const;

    juce::String describe() const;
    juce::var toVar() const;
    static MidiTrigger fromVar (const juce::var& v);
};

/** A key and / or a MIDI trigger. */
struct Binding
{
    juce::String key;      // juce::KeyPress description, empty = none
    MidiTrigger midi;

    juce::KeyPress keyPress() const { return key.isEmpty() ? juce::KeyPress() : juce::KeyPress::createFromDescription (key); }
};

/** The fixed commands and their bindings (kept in the app settings: they belong to the controllers,
    not to a show). Buttons' own key / MIDI live in the show with each button. */
struct CommandMap
{
    struct Command
    {
        juce::String id, name;
        Binding defaults;
    };

    /** Every command in the order the Controls window lists them. "slotN" = button N of the shown bank. */
    static const std::vector<Command>& commands();
    static juce::String nameOf (const juce::String& id);

    std::map<juce::String, Binding> bindings;   // id -> binding

    CommandMap() { resetToDefaults(); }
    void resetToDefaults();
    Binding get (const juce::String& id) const;

    /** The command a key / MIDI message triggers, empty if none. */
    juce::String commandForKey (const juce::KeyPress& key) const;
    juce::String commandForMidi (const juce::MidiMessage& m) const;

    juce::var toVar() const;
    void fromVar (const juce::var& v);
};

/** Keeps the last controller value per channel / number, so a held footswitch (CC 127 repeated) fires once. */
class ControllerEdges
{
public:
    /** True if this message is a "press": a note-on, a program change, or a controller crossing up past 63. */
    bool isPress (const juce::MidiMessage& m);

private:
    std::array<juce::uint8, 16 * 128> last {};
};
