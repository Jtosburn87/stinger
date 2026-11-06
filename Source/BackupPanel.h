#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BackupLink.h"

/** File > Backup laptop: which laptop this is, and how the other finds it. */
class BackupPanel : public juce::Component,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    BackupPanel (BackupLink& link, ShowController& controller, juce::PropertiesFile& settings);
    /** Offers to save the open show (a backup laptop replaces it); calls next(true) to go ahead. */
    std::function<void (std::function<void (bool)> next)> askToSave;
    ~BackupPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override { refresh(); }
    void refresh();
    void apply();

    BackupLink& link;
    ShowController& controller;
    juce::PropertiesFile& settings;
    juce::Label roleLabel { {}, "This laptop" }, addressLabel { {}, "Main laptop's address" }, codeLabel { {}, "Code" },
                modeLabel { {}, "Standby" }, thisAddress, status;
    juce::ComboBox role, mode;
    juce::TextEditor address, code;
    juce::TextButton newCode { "New code" }, connect { "Connect" }, takeOver { "TAKE OVER" };
    bool filling = false;
};
