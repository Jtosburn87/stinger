#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Show.h"
#include "WaveformEditor.h"
#include "FxPanel.h"
#include "Downloader.h"

class ControlSurface;

/** Bottom panel: the selected cue's waveform and settings. */
class CueSettings : public juce::Component,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit CueSettings (ShowController& controller);
    ~CueSettings() override;

    void setCue (int id);
    int getCue() const { return cueId; }

    /** Where the cue's key / MIDI is learnt (set by the main window). */
    void setControlSurface (ControlSurface* s);
    WaveformEditor& getEditor() { return waveform; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refresh();
    void fillOutputs();
    void changed();
    Cue* cue();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    // recording into the button
    void fillInputs();
    void applyArm();   // opens the chosen input on the device while ARM is lit, none otherwise
    void toggleRecording();
    void startRecording();
    juce::File recordingFolder() const;

    // pulling a song from a link into the button
    void pullOrCancel();
    void startPull (int id, const juce::String& link);
    juce::Label linkLabel { {}, "Link" };
    juce::TextEditor linkText;
    juce::TextButton pullButton { "Pull WAV" };
    juce::Rectangle<int> pullBar;
    Downloader* downloader = nullptr;
    int pullJob = 0;                    // the download running for this panel (0 = none)
    double pullProgress = 0.0;
    juce::Label recordLabel { {}, "Record" }, recordTime;
    juce::ComboBox recordInput;
    juce::TextButton armButton { "ARM" }, recordButton { "REC" };   // REC does nothing until armed (disarms after each take)
    juce::Rectangle<int> meterArea;
    float inputLevel = 0.0f;
    int recordingCue = -1;
    juce::PropertiesFile* settings = nullptr;

public:
    /** Where the record input choice is remembered (set by the main window). */
    void setSettings (juce::PropertiesFile* s) { settings = s; fillInputs(); }
    /** Where Pull WAV downloads (set by the main window). */
    void setDownloader (Downloader* d) { downloader = d; }
    /** Where downloads go: "<show> Downloads" next to the show, else Documents\Stinger Downloads. */
    static juce::File downloadFolder (const ShowController&);

private:

    struct FadeRow
    {
        juce::Label label;
        juce::Slider seconds { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
        juce::ComboBox curve;
    };
    void setupFade (FadeRow& row, const juce::String& name);

    ShowController& controller;
    int cueId = -1;
    bool updating = false;
    ControlSurface* surface = nullptr;

    WaveformEditor waveform;
    FxPanel fxPanel;
    juce::Label title, info;
    juce::Label nameLabel { {}, "Name" }, outputLabel { {}, "Output" }, levelLabel { {}, "Level" },
                panLabel { {}, "Pan" }, keyLabel { {}, "Key / MIDI" }, keyText;
    juce::TextEditor name;
    juce::ComboBox output;
    juce::Slider level { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 pan { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    FadeRow fadeIn, fadeOut, stopFade;
    juce::ToggleButton fadeOthers { "Fade others when played" }, loop { "Loop" };
    juce::TextButton learn { "Learn" }, clearKey { "Clear" }, colourButton { "Colour" };
};
