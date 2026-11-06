#include "OscPanel.h"
#include "Look.h"

void OscPanel::restore (OscBridge& osc, juce::PropertiesFile& settings)
{
    if (settings.getBoolValue ("oscOn", false))
        osc.start (settings.getIntValue ("oscIn", 9000), settings.getValue ("oscHost"), settings.getIntValue ("oscOut", 9001));
}

OscPanel::OscPanel (OscBridge& o, juce::PropertiesFile& s) : osc (o), settings (s)
{
    on.setToggleState (osc.isOn(), juce::dontSendNotification);
    on.setWantsKeyboardFocus (false);
    on.onClick = [this] { apply(); };
    addAndMakeVisible (on);
    inPort.setText (juce::String (settings.getIntValue ("oscIn", 9000)));
    host.setText (settings.getValue ("oscHost", "127.0.0.1"));
    outPort.setText (juce::String (settings.getIntValue ("oscOut", 9001)));
    host.setTooltip ("The computer that gets Stinger's names and times (127.0.0.1 = this one). Empty = send nothing.");
    for (auto* e : { &inPort, &host, &outPort })
    {
        e->onReturnKey = [this] { apply(); };
        e->onFocusLost = [this] { apply(); };
        addAndMakeVisible (e);
    }
    for (auto* l : { &inLabel, &hostLabel, &portLabel, &status })
        addAndMakeVisible (l);
    help.setMultiLine (true);
    help.setReadOnly (true);
    help.setFont (Look::mono (12.5f));
    help.setText (OscBridge::helpText());
    addAndMakeVisible (help);
    status.setText (osc.getError(), juce::dontSendNotification);
    setSize (640, 520);
}

void OscPanel::apply()
{
    const int in = juce::jlimit (1, 65535, inPort.getText().getIntValue());
    const int out = juce::jlimit (1, 65535, outPort.getText().getIntValue());
    settings.setValue ("oscOn", on.getToggleState());
    settings.setValue ("oscIn", in);
    settings.setValue ("oscHost", host.getText().trim());
    settings.setValue ("oscOut", out);
    settings.saveIfNeeded();
    if (on.getToggleState())
        osc.start (in, host.getText().trim(), out);
    else
        osc.stop();
    status.setText (on.getToggleState() && osc.getError().isEmpty() ? juce::String ("Listening on port " + juce::String (in)) : osc.getError(),
                    juce::dontSendNotification);
}

void OscPanel::paint (juce::Graphics& g)
{
    g.fillAll (Look::panel);
}

void OscPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    auto top = r.removeFromTop (28);
    on.setBounds (top.removeFromLeft (100));
    status.setBounds (top);
    r.removeFromTop (8);
    auto row = r.removeFromTop (28);
    inLabel.setBounds (row.removeFromLeft (110));
    inPort.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (24);
    hostLabel.setBounds (row.removeFromLeft (60));
    host.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (8);
    portLabel.setBounds (row.removeFromLeft (40));
    outPort.setBounds (row.removeFromLeft (70));
    r.removeFromTop (12);
    help.setBounds (r);
}
