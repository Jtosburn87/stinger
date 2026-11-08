#include "AudioPanel.h"
#include "Look.h"

namespace
{
   #if JUCE_ANDROID
    constexpr int backupHeight = 0;     // Samsung DeX: no backup device on Android (DeviceWatch::check)
   #else
    constexpr int backupHeight = 170;
   #endif
}

DeviceWatch::Backup AudioPanel::loadBackup (juce::PropertiesFile& settings)
{
    DeviceWatch::Backup b;
    b.type = settings.getValue ("backupType");
    b.name = settings.getValue ("backupDevice");
    b.output = settings.getIntValue ("backupOutput", -1);
    return b;
}

AudioPanel::AudioPanel (DeviceWatch& w, juce::PropertiesFile& s)
    : watch (w), settings (s),
      selector (w.getManager(), 0, PlaybackEngine::maxInputs, 2, PlaybackEngine::maxOutputs, false, false, false, false)
{
    watch.setUserChanging (true);
    addAndMakeVisible (selector);
    heading.setFont (Look::condensed (17.0f));
    heading.setColour (juce::Label::textColourId, Look::amber);
   #if ! JUCE_ANDROID
    for (auto* l : { &heading, &typeLabel, &deviceLabel, &outputLabel })
        addAndMakeVisible (l);
    for (auto* c : { &type, &device, &output })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (c);
    }
   #endif
    type.setTooltip ("The kind of driver the backup device uses");
    device.setTooltip ("If the interface is lost, Stinger moves here by itself and what's playing carries on");
    output.setTooltip ("Where the songs go on the backup device");
    fillTypes();
    type.onChange = [this] { fillDevices(); store(); };
    device.onChange = [this] { fillOutputs(); store(); };
    output.onChange = [this] { store(); };
    setSize (560, 560 + backupHeight);
}

AudioPanel::~AudioPanel()
{
    watch.setUserChanging (false);
}

void AudioPanel::fillTypes()
{
    const auto current = watch.getBackup();
    type.clear (juce::dontSendNotification);
    int id = 1;
    for (auto* t : watch.getManager().getAvailableDeviceTypes())
    {
        type.addItem (t->getTypeName(), id);
        if (t->getTypeName() == current.type)
            type.setSelectedId (id, juce::dontSendNotification);
        ++id;
    }
    if (type.getSelectedId() == 0 && type.getNumItems() > 0)
        type.setSelectedItemIndex (0, juce::dontSendNotification);
    fillDevices();
}

void AudioPanel::fillDevices()
{
    const auto current = watch.getBackup();
    device.clear (juce::dontSendNotification);
    device.addItem ("No backup", 1);
    device.setSelectedId (1, juce::dontSendNotification);
    int id = 2;
    for (auto* t : watch.getManager().getAvailableDeviceTypes())
        if (t->getTypeName() == type.getText())
            for (auto& name : t->getDeviceNames (false))
            {
                device.addItem (name, id);
                if (name == current.name && t->getTypeName() == current.type)
                    device.setSelectedId (id, juce::dontSendNotification);
                ++id;
            }
    fillOutputs();
}

void AudioPanel::fillOutputs()
{
    const auto current = watch.getBackup();
    output.clear (juce::dontSendNotification);
    output.addItem ("Same outputs as the main interface", 1);
    output.setSelectedId (1, juce::dontSendNotification);
    if (device.getSelectedId() > 1)
        for (auto* t : watch.getManager().getAvailableDeviceTypes())
            if (t->getTypeName() == type.getText())
                if (std::unique_ptr<juce::AudioIODevice> d (t->createDevice (device.getText(), {})); d != nullptr)
                {
                    const auto names = d->getOutputChannelNames();
                    for (int ch = 0; ch + 1 < names.size(); ch += 2)
                    {
                        output.addItem ("All songs on " + names[ch] + " + " + names[ch + 1], ch + 2);
                        if (ch == current.output)
                            output.setSelectedId (ch + 2, juce::dontSendNotification);
                    }
                }
    output.setEnabled (device.getSelectedId() > 1);
}

void AudioPanel::store()
{
    DeviceWatch::Backup b;
    if (device.getSelectedId() > 1)
    {
        b.type = type.getText();
        b.name = device.getText();
        b.output = output.getSelectedId() > 1 ? output.getSelectedId() - 2 : -1;
    }
    watch.setBackup (b);
    settings.setValue ("backupType", b.type);
    settings.setValue ("backupDevice", b.name);
    settings.setValue ("backupOutput", b.output);
    settings.saveIfNeeded();
}

void AudioPanel::paint (juce::Graphics& g)
{
    g.fillAll (Look::panel);
    g.setColour (Look::line);
    g.fillRect (0, getHeight() - backupHeight, getWidth(), 1);
}

void AudioPanel::resized()
{
    auto r = getLocalBounds();
    auto bottom = r.removeFromBottom (backupHeight).reduced (16, 12);
    selector.setBounds (r);
    heading.setBounds (bottom.removeFromTop (26));
    auto row = [&bottom] (juce::Label& l, juce::ComboBox& c)
    {
        auto line = bottom.removeFromTop (30);
        bottom.removeFromTop (6);
        l.setBounds (line.removeFromLeft (120));
        c.setBounds (line);
    };
    row (typeLabel, type);
    row (deviceLabel, device);
    row (outputLabel, output);
}
