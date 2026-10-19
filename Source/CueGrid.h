#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Show.h"

/** The 6 x 4 grid of cue buttons for one bank. Click = play (again = fade out), right-click = menu,
    drop audio files onto a button to load them (several: onto the empty buttons from there on, in name order).
    In edit mode (or with Ctrl) a button dragged onto another swaps places with it. */
class CueGrid : public juce::Component,
                public juce::FileDragAndDropTarget,
                public juce::DragAndDropTarget,
                private juce::ChangeListener,
                private juce::Timer
{
public:
    explicit CueGrid (ShowController& controller);
    ~CueGrid() override;

    void setBank (int bank);
    int getBank() const { return bank; }

    /** Edit mode: clicks select (no playing) and buttons can be dragged onto the playlist. */
    void setEditMode (bool on) { editMode = on; repaint(); }
    bool isEditMode() const { return editMode; }

    int getSelected() const { return selected; }
    void select (int id);
    std::function<void (int id)> onSelect;

    /** The key that plays a slot when the cue has no hotkey of its own: rows 1-6, Q-Y, A-H, Z-N. */
    static juce::KeyPress defaultKey (int slot);
    /** The key that plays this cue in the shown bank (its own hotkey, else the default for its slot). */
    juce::KeyPress keyFor (int slot) const;
    /** The key of "button N of the shown bank" (set by the main window). */
    std::function<juce::KeyPress (int slot)> slotKey;

    static juce::String formatTime (double seconds);
    /** m:ss.t, for points that need tenths (start point, trims). */
    static juce::String formatTimeTenths (double seconds);
    static juce::String outputLabel (const OutputRoute& route);

    void chooseFile (int id);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;   // edit mode (or Ctrl): drag a button into a playlist

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    /** Audio files among these paths (a folder counts as the audio files in it). */
    juce::Array<juce::File> audioFilesIn (const juce::StringArray& paths) const;

private:
    juce::Rectangle<float> slotBounds (int slot) const;
    int slotAt (juce::Point<int> p) const;
    void paintSlot (juce::Graphics&, int slot);
    void showMenu (int id);
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void timerCallback() override;

    ShowController& controller;
    int bank = 0;
    int selected = 0;
    int dropSlot = -1;
    bool editMode = false;
    std::unique_ptr<juce::FileChooser> chooser;
};
