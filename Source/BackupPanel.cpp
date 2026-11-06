#include "BackupPanel.h"
#include "Remote.h"
#include "Look.h"

BackupPanel::BackupPanel (BackupLink& l, ShowController& c, juce::PropertiesFile& s) : link (l), controller (c), settings (s)
{
    role.addItem ("Off", 1);
    role.addItem ("Main laptop (a backup can connect to it)", 2);
    role.addItem ("Backup laptop (follows the main one)", 3);
    role.setSelectedId (link.getRole() == BackupLink::Role::main ? 2 : link.getRole() == BackupLink::Role::backup ? 3 : 1, juce::dontSendNotification);
    role.onChange = [this] { apply(); };
    mode.addItem ("Warm: follows silently until TAKE OVER", 1);
    mode.addItem ("Hot: plays along, you switch at the mixer", 2);
    mode.setSelectedId (settings.getIntValue ("backupMode", 1), juce::dontSendNotification);
    mode.onChange = [this] { settings.setValue ("backupMode", mode.getSelectedId()); if (role.getSelectedId() == 3) apply(); };
    for (auto* b : { &role, &mode })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    address.setText (settings.getValue ("backupHost"));
    code.setText (settings.getValue ("backupCode", BackupLink::newCode()));
    settings.setValue ("backupCode", code.getText());
    code.setTooltip ("The same code goes in on both laptops");
    addAndMakeVisible (address);
    addAndMakeVisible (code);
    for (auto* l2 : { &roleLabel, &addressLabel, &codeLabel, &modeLabel, &thisAddress, &status })
        addChildComponent (l2);
    for (auto* l2 : { &roleLabel, &codeLabel, &status })
        l2->setVisible (true);
    status.setFont (Look::condensed (17.0f));

    newCode.onClick = [this]
    {
        code.setText (BackupLink::newCode());
        apply();
    };
    connect.onClick = [this] { apply(); };
    takeOver.setColour (juce::TextButton::buttonColourId, Look::red);
    takeOver.setTooltip ("Stop following and run the show from this laptop, sound on");
    takeOver.onClick = [this] { link.takeOver(); };
    for (auto* b : { &newCode, &connect, &takeOver })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    link.addChangeListener (this);
    startTimerHz (2);
    setSize (560, 300);
    refresh();
}

BackupPanel::~BackupPanel()
{
    link.removeChangeListener (this);
}

void BackupPanel::apply()
{
    settings.setValue ("backupHost", address.getText().trim());
    settings.setValue ("backupCode", code.getText().trim());
    settings.saveIfNeeded();
    if (role.getSelectedId() == 2)
    {
        if (! link.startMain (code.getText().trim()))
            status.setText ("Port " + juce::String (BackupLink::defaultPort) + " can't be opened", juce::dontSendNotification);
    }
    else if (role.getSelectedId() == 3)
    {
        const auto begin = [safe = juce::Component::SafePointer<BackupPanel> (this)] (bool go)
        {
            if (safe == nullptr)
                return;
            if (! go)
            {
                safe->role.setSelectedId (1, juce::dontSendNotification);
                safe->refresh();
                return;
            }
            safe->link.startBackup (safe->address.getText().trim(), safe->code.getText().trim(),
                                    safe->mode.getSelectedId() == 2 ? BackupLink::Mode::hot : BackupLink::Mode::warm,
                                    safe->controller.getHomeFolder().getChildFile ("Backup Audio"));
            safe->refresh();
        };
        if (address.getText().trim().isEmpty())
            return;
        if (link.getRole() != BackupLink::Role::backup && askToSave)
            askToSave (begin);
        else
            begin (true);
    }
    else
        link.stop();
    refresh();
}

void BackupPanel::refresh()
{
    const bool main = role.getSelectedId() == 2, backup = role.getSelectedId() == 3;
    thisAddress.setVisible (main);
    addressLabel.setVisible (backup);
    address.setVisible (backup);
    modeLabel.setVisible (backup);
    mode.setVisible (backup);
    connect.setVisible (backup);
    takeOver.setVisible (link.isFollowing());
    if (main)
    {
        juce::StringArray ips;
        for (auto& a : Remote::addresses())
            ips.add (a.ip);
        thisAddress.setText ("This laptop's address: " + ips.joinIntoString ("  or  "), juce::dontSendNotification);
    }
    status.setColour (juce::Label::textColourId, link.isMainLost() ? Look::red : link.isConnected() ? Look::green : Look::textDim);
    status.setText (link.getStatus(), juce::dontSendNotification);
}

void BackupPanel::paint (juce::Graphics& g)
{
    g.fillAll (Look::panel);
}

void BackupPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    auto row = [&r] (juce::Label& l)
    {
        auto x = r.removeFromTop (30);
        r.removeFromTop (8);
        l.setBounds (x.removeFromLeft (150));
        return x;
    };
    role.setBounds (row (roleLabel));
    auto codeRow = row (codeLabel);
    newCode.setBounds (codeRow.removeFromRight (90));
    codeRow.removeFromRight (6);
    code.setBounds (codeRow);
    auto addressRow = row (addressLabel);
    connect.setBounds (addressRow.removeFromRight (90));
    addressRow.removeFromRight (6);
    address.setBounds (addressRow);
    mode.setBounds (row (modeLabel));
    thisAddress.setBounds (addressLabel.getBounds().withRight (getWidth() - 16));
    auto bottom = r.removeFromBottom (40);
    takeOver.setBounds (bottom.removeFromRight (150));
    status.setBounds (bottom);
}
