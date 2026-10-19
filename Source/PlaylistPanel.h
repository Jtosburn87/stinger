#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Show.h"

/** The entries of the current playlist, each followed by its transition chip (Stop / Play next /
    Crossfade). Click = set as next, double-click = play now, drag to reorder, click a chip to change it.
    Buttons Ctrl+dragged from the grid (or audio files from Explorer) can be dropped in. */
class PlaylistRows : public juce::Component,
                     public juce::DragAndDropTarget,
                     public juce::FileDragAndDropTarget
{
public:
    explicit PlaylistRows (ShowController& controller);

    static constexpr int rowHeight = 36, chipHeight = 24;
    int contentHeight() const;
    /** Where audio files dropped on the playlist go: the first empty button from this bank on. */
    std::function<int()> getBank;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragMove (const juce::StringArray&, int, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    int entryCount();
    int rowAt (int y) const;                 // entry whose row is at y, -1 = none
    int chipAt (juce::Point<int> p);         // entry whose transition chip is at p, -1 = none
    int insertIndexAt (int y);               // where a drop at y goes
    juce::Rectangle<int> rowBounds (int i) const;
    juce::Rectangle<int> chipBounds (int i);
    void showTransitionMenu (int i);
    void showRowMenu (int i);

    ShowController& controller;
    int dragFrom = -1, dropAt = -1;
    bool reordering = false;
};

/** Left column: playlist picker, entries, Next + GO. */
class PlaylistPanel : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit PlaylistPanel (ShowController& controller);
    ~PlaylistPanel() override;

    PlaylistRows& getRows() { return rows; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void showPlaylistMenu();
    void timerCallback() override;
    bool shownLooping = false;

    ShowController& controller;
    juce::ComboBox picker;
    juce::TextButton menuButton { juce::String::fromUTF8 ("\xe2\x8b\xaf") }, goButton { "GO" }, fadeNextButton { "FADE TO NEXT" };
    juce::Viewport viewport;
    PlaylistRows rows;
    juce::Label nextLabel;
};
