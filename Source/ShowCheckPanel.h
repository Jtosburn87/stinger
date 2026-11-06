#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Show.h"

/** File > Show check: what's wrong with the show, and looking for missing audio (a folder, the whole computer, or by hand). */
class ShowCheckPanel : public juce::Component,
                       private juce::ListBoxModel,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit ShowCheckPanel (ShowController& controller);
    ~ShowCheckPanel() override;

    void refresh();
    int numProblems() const { return (int) issues.size(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return (int) issues.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int) override { updateButtons(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    juce::StringArray missingNames() const;
    void startSearch (const juce::Array<juce::File>& roots);
    void searchFolder();
    void findByHand (int row);
    void applyFound();
    void updateButtons();

    ShowController& controller;
    std::vector<ShowIssue> issues;
    FileSearch search;
    bool searching = false;
    int foundCount = 0;
    juce::String searchResult;
    juce::ListBox list;
    juce::TextButton checkAgain { "Check again" }, findFolder { "Search a folder..." }, findAll { "Search this computer" },
                     findManual { "Find by hand..." }, stop { "Stop search" };
    std::unique_ptr<juce::FileChooser> chooser;
};

class ShowCheckWindow : public juce::DocumentWindow
{
public:
    explicit ShowCheckWindow (ShowController& controller);
    ShowCheckPanel& getPanel() { return *dynamic_cast<ShowCheckPanel*> (getContentComponent()); }
    void closeButtonPressed() override { setVisible (false); }
};
