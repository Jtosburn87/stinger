#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Show.h"

/** The run of show: who, what song, notes and time, in order, with its own Next and GO. */
class RunOfShowPanel : public juce::Component,
                       private juce::TableListBoxModel,
                       private juce::ChangeListener
{
public:
    RunOfShowPanel (ShowController& controller, std::function<int()> selectedButton);
    ~RunOfShowPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return (int) controller.getShow().run.size(); }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    juce::Component* refreshComponentForCell (int row, int column, bool selected, juce::Component* existing) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void addRow (int cueId);

    ShowController& controller;
    std::function<int()> selectedButton;
    juce::TableListBox table;
    juce::TextButton go { "GO" }, addSong { "Add selected button" }, addNote { "Add a note" }, remove { "Remove" },
                     up { "Move up" }, down { "Move down" }, setNext { "Set as Next" };
};

class RunOfShowWindow : public juce::DocumentWindow
{
public:
    RunOfShowWindow (ShowController& controller, std::function<int()> selectedButton);
    void closeButtonPressed() override { setVisible (false); }
    /** Keys go to the main window, so hotkeys keep working with this one in front. */
    std::function<bool (const juce::KeyPress&)> onKey;
    bool keyPressed (const juce::KeyPress& key) override { return onKey ? onKey (key) : false; }
};
