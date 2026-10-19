#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Show.h"

/** The selected cue's waveform, edited like a DAW clip with a smart tool (the cursor changes with where
    the mouse is):
      - top third near the start / end of what plays: fade in / fade out (drag the length)
      - middle third near the In / Out handle: trim
      - bottom third, anywhere: razor (drag across a section to cut it out, click a cut to bring it back)
      - elsewhere: click = preview cursor (where Preview plays from), drag = select (Cut / Copy / Paste / Split).
    Zoom: H / V buttons, Alt + wheel (across), Ctrl + wheel (height); wheel scrolls; the bar underneath
    shows the whole file and the part in view. The audio file is never changed. */
class WaveformEditor : public juce::Component
{
public:
    explicit WaveformEditor (ShowController& controller);
    ~WaveformEditor() override;

    void setCue (int id);
    int getCue() const { return cueId; }

    bool undo();
    bool redo();

    // Selection edits (drag in the middle of the waveform to select). Nothing changes the audio file.
    bool hasSelection() const { return selection.getLength() > 0.0; }
    /** Copies what plays in the selection. */
    bool copySelection();
    /** Takes the selection out (like the razor); with keepCopy it's copied first. */
    bool cutSelection (bool keepCopy);
    /** Plays the copied section again at the cursor (or the selection start). Same audio file only. */
    bool paste();
    /** The selection becomes a new button of its own (the next empty one). */
    bool splitSelection();

    /** Lists the device outputs for Preview (call when the audio device changes). */
    void fillPreviewOutputs();

    /** Repaints the playhead etc. (called by the settings panel's timer). */
    void tick();

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Edit fields of a cue (what undo keeps). */
    struct Edits
    {
        double trimIn = 0.0, trimOut = -1.0;
        std::vector<juce::Range<double>> cuts;
        std::vector<Paste> pastes;
        Fade fadeIn, fadeOut;
        bool operator== (const Edits& o) const;
        static Edits of (const Cue& c);
        void applyTo (Cue& c) const;
    };

    /** Nearest zero crossing to t within a few milliseconds (so trims and cuts don't click). */
    static double snapToZero (const CueAudio& audio, double t);

private:
    class Canvas;
    class NavBar;
    friend class Canvas;
    friend class NavBar;

    Cue* cue();
    std::shared_ptr<CueAudio> audio() const;
    double fileSeconds() const;
    double playIn();      // first second that plays
    double playOut();     // last second that plays

    // view (file seconds)
    void setView (double start, double length);
    void zoomAround (double t, double factor);
    void fit();
    double viewEnd() const { return viewStart + viewLength; }

    // edits
    void beginEdit();
    void endEdit();
    void edited();
    void restoreEdits (int id, const Edits& e);
    void refreshFields();
    void setFromField (juce::Label& field);

    void togglePreview();
    void updateEditButtons();
    void addCut (juce::Range<double> range);
    void showHint (const juce::String& text);

    /** What Copy took: the file and the sections of it. */
    struct Clipboard { juce::File file; std::vector<Segment> pieces; };
    static Clipboard& clipboard();

    ShowController& controller;
    int cueId = -1;
    double viewStart = 0.0, viewLength = 0.0;
    float verticalZoom = 1.0f;
    double cursor = -1.0;                  // preview cursor (file seconds), < 0 = none
    juce::Range<double> selection;         // file seconds, empty = none

    struct Step { int cueId; juce::File file; Edits before, after; };   // file: skipped if the button got another song
    std::vector<Step> undoSteps, redoSteps;
    Edits editStart;
    bool editing = false;

    std::unique_ptr<Canvas> canvas;
    std::unique_ptr<NavBar> nav;
    juce::TextButton hOut { "H-" }, hIn { "H+" }, vOut { "V-" }, vIn { "V+" }, fitButton { "Fit" },
                     undoButton { "Undo" }, redoButton { "Redo" }, preview { "Preview" },
                     cutButton { "Cut" }, copyButton { "Copy" }, pasteButton { "Paste" }, splitButton { "Split" };
    juce::ComboBox previewOut;
    juce::Label inLabel { {}, "In" }, outLabel { {}, "Out" }, inField, outField, hint;
};
