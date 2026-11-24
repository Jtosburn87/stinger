#include "MidiInputList.h"
#include "Look.h"

namespace { constexpr int headingHeight = 24; }

MidiInputList::MidiInputList (ControlSurface& s, int height) : surface (s), rowHeight (height)
{
    all.setWantsKeyboardFocus (false);
    all.setTooltip ("On: Stinger listens to every MIDI input, including ones plugged in later. "
                    "Off: only the ticked ones (a device seen for the first time stays off until you tick it)");
    all.onClick = [this] { surface.setAllInputs (all.getToggleState()); };
    addAndMakeVisible (all);

   #if JUCE_ANDROID
    empty.setText ("No MIDI devices found (plug in a USB MIDI device, or pair Bluetooth MIDI in Android's Settings first)", juce::dontSendNotification);
   #elif JUCE_MAC
    empty.setText ("No MIDI devices found (pair Bluetooth MIDI in Audio MIDI Setup first)", juce::dontSendNotification);
   #else
    empty.setText ("No MIDI devices found (pair Bluetooth MIDI in Windows Settings first)", juce::dontSendNotification);
   #endif
    empty.setColour (juce::Label::textColourId, Look::textDim);
    empty.setFont (Look::font (13.0f));
    empty.setMinimumHorizontalScale (1.0f);
    addChildComponent (empty);

    surface.addChangeListener (this);
    refresh();
}

MidiInputList::~MidiInputList()
{
    surface.removeChangeListener (this);
}

int MidiInputList::getIdealHeight() const
{
    const int rows = 1 + juce::jmax (1, (int) shown.size());
    return headingHeight + rows * rowHeight + 4;
}

void MidiInputList::refresh()
{
    const int oldHeight = getIdealHeight();
    all.setToggleState (surface.isAllInputs(), juce::dontSendNotification);

    const auto choices = surface.getInputChoices();
    const bool sameDevices = choices.size() == shown.size()
                          && std::equal (choices.begin(), choices.end(), shown.begin(), [] (auto& a, auto& b)
                                         { return a.identifier == b.identifier && a.name == b.name && a.connected == b.connected; });
    shown = choices;
    if (! sameDevices)
    {
        boxes.clear();
        for (size_t i = 0; i < shown.size(); ++i)
        {
            auto* box = boxes.add (new juce::ToggleButton (shown[i].connected ? shown[i].name : shown[i].name + "  (not connected)"));
            box->setWantsKeyboardFocus (false);
            box->onClick = [this, i, box]
            {
                if (i < shown.size())
                    surface.setInputOn (shown[i], box->getToggleState());
            };
            addAndMakeVisible (box);
        }
    }
    for (size_t i = 0; i < shown.size(); ++i)
    {
        boxes[(int) i]->setToggleState (shown[i].on, juce::dontSendNotification);
        boxes[(int) i]->setEnabled (! surface.isAllInputs());   // "All inputs" ticks every one
        boxes[(int) i]->setTooltip (surface.isAllInputs() ? "Untick \"All inputs\" to choose" : juce::String());
    }
    empty.setVisible (shown.empty());

    if (getIdealHeight() != oldHeight && onHeightChanged)
        onHeightChanged();
    resized();
    repaint();
}

void MidiInputList::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xffc9d0d8));
    g.setFont (Look::condensed (16.0f));
    g.drawText ("MIDI INPUTS", 0, 0, getWidth(), headingHeight, juce::Justification::centredLeft);
}

void MidiInputList::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (headingHeight);
    all.setBounds (r.removeFromTop (rowHeight));
    r.removeFromLeft (rowHeight);   // the devices sit under "All inputs"
    if (shown.empty())
        empty.setBounds (r.removeFromTop (rowHeight));
    for (auto* box : boxes)
        box->setBounds (r.removeFromTop (rowHeight));
}
