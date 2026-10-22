#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Show.h"

/** The entries of one playlist, each followed by its transition chip (Stop / Play next /
    Crossfade). Click = set as next, double-click = play now, drag to reorder, click a chip to change it.
    Buttons dragged from the grid (or audio files from Explorer) drop in where they're let go. */
class PlaylistRows : public juce::Component,
                     public juce::DragAndDropTarget,
                     public juce::FileDragAndDropTarget
{
public:
    PlaylistRows (ShowController& controller, int playlistUid);

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

    /** Puts audio files (name order) onto the next empty buttons from the shown bank on and into the playlist
        from insertAt (-1 = the end). */
    static void addFiles (ShowController& controller, int playlist, const juce::StringArray& files, int insertAt, int startBank);

private:
    int index() const { return controller.playlistIndexOf (uid); }
    Playlist* playlist() { return controller.playlistAt (index()); }
    int entryCount();
    int rowAt (int y) const;                 // entry whose row is at y, -1 = none
    int chipAt (juce::Point<int> p);         // entry whose transition chip is at p, -1 = none
    int insertIndexAt (int y);               // where a drop at y goes
    juce::Rectangle<int> rowBounds (int i) const;
    juce::Rectangle<int> chipBounds (int i);
    void showTransitionMenu (int i);
    void showRowMenu (int i);

    ShowController& controller;
    const int uid;
    int dragFrom = -1, dropAt = -1;
    bool reordering = false;
};

/** One playlist in full, for its own window: name + menu, entries, Next, GO and FADE TO NEXT. */
class PlaylistView : public juce::Component,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    PlaylistView (ShowController& controller, int playlistUid);
    ~PlaylistView() override;

    PlaylistRows& getRows() { return rows; }
    /** Keys pressed in the window (Space, Enter, hotkeys...) go to the main window. */
    std::function<bool (const juce::KeyPress&)> onKey;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override { return onKey != nullptr && onKey (key); }
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void timerCallback() override;
    int index() const { return controller.playlistIndexOf (uid); }
    bool shownLooping = false;

    ShowController& controller;
    const int uid;
    juce::TextButton menuButton { juce::String::fromUTF8 ("\xe2\x8b\xaf") }, goButton { "GO" }, fadeNextButton { "FADE TO NEXT" };
    juce::Viewport viewport;
    PlaylistRows rows;
    juce::Label nextLabel;
};

/** Left column: a box per playlist (Playlist 1 at the top), scrolling when they don't all fit. A box shows
    what's on air and next, with its own GO; a button dropped on it goes to the end. The selected box is the
    one Space / Enter play. Each can open in its own window with the full list. */
class PlaylistPanel : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit PlaylistPanel (ShowController& controller);
    ~PlaylistPanel() override;

    /** The bank audio files dropped on a playlist start looking for empty buttons from. */
    std::function<int()> getBank;
    /** Keys pressed in a playlist window go here (the main window's key handling). */
    std::function<bool (const juce::KeyPress&)> onKey;

    /** Opens the playlist in its own window (or brings its window to the front). */
    void openWindow (int playlist);
    /** Shows the playlist menu (GO, Fade to next, window, rename, delete, new) at the mouse. */
    void showMenu (int playlist, juce::Component* target);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int boxHeight = 92, boxGap = 8;

private:
    class Box;
    class Window;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();
    void layoutBoxes();
    void rename (int playlist);

    ShowController& controller;
    juce::TextButton addButton { "+" };
    juce::Viewport viewport;
    juce::Component boxHolder;
    juce::OwnedArray<Box> boxes;
    std::map<int, std::unique_ptr<Window>> windows;   // by playlist uid
};
