#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Show.h"
#include "Downloader.h"

/** Import Audio (top bar): two tabs.
      Files - choose several audio files at once.
      Links - paste YouTube (or Dropbox / OneDrive) links; each has its own Download button and progress bar,
              Download all queues the lot.
    Everything goes onto the empty buttons from button 1 of the shown bank on, in order, skipping buttons in use and
    carrying on into the next banks. The panel belongs to the main window, so downloads carry on when it's closed. */
class ImportPanel : public juce::Component,
                    private juce::ChangeListener
{
public:
    ImportPanel (ShowController& controller, Downloader& downloader, std::function<int()> shownBank);
    ~ImportPanel() override;

    void showLinksTab (bool links);
    void resized() override;
    void paint (juce::Graphics&) override;

    /** Puts a downloaded / chosen file on the next empty button from the shown bank on; returns its id (-1 = no room). */
    int place (const juce::File& file, const juce::String& name);

private:
    class FilesTab;
    class LinksTab;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    ShowController& controller;
    Downloader& downloader;
    std::function<int()> shownBank;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<FilesTab> filesTab;
    std::unique_ptr<LinksTab> linksTab;
};
