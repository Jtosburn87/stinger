#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>
#include "Show.h"
#include "CueGrid.h"
#include "NowPlaying.h"
#include "PlaylistPanel.h"
#include "WaveformEditor.h"
#include "StockFx.h"

/** A text box inside a scrolling page: a tap brings up the keyboard, a swipe that starts on it only scrolls. */
struct MobileTextEditor : juce::TextEditor
{
    MobileTextEditor() { setMouseClickGrabsKeyboardFocus (false); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::TextEditor::mouseUp (e);
        if (! e.mouseWasDraggedSinceMouseDown())
            grabKeyboardFocus();
    }
};

/** A label, a slider and what the slider reads, one row of the editor. */
struct MobileRow : juce::Component
{
    explicit MobileRow (const juce::String& name);
    void resized() override;
    juce::Label label;
    juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
};

/** How far a compressor or limiter is turning the sound down right now. */
struct MobileReductionMeter : juce::Component
{
    void setDb (float db);
    void paint (juce::Graphics&) override;
    float shown = 0.0f;
    float held = 0.0f;              // the deepest lately, marked on the bar
    juce::uint32 heldAt = 0;
};

/** The editor, a page of its own: the button's waveform, its settings and the effects section. */
class MobileEditorPage : public juce::Component,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit MobileEditorPage (ShowController& controller);
    ~MobileEditorPage() override;

    void setCue (int id);
    std::function<void()> onBack;
    /** The system file picker, for this button. Cancelled with backIfCancelled, it goes back a page. */
    void chooseAudio (bool backIfCancelled);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** One effect on this button: on/off and a slider per setting. */
    struct EffectSection
    {
        StockFx::Kind kind;
        juce::ToggleButton power;
        juce::OwnedArray<MobileRow> rows;
        std::map<int, std::unique_ptr<juce::ToggleButton>> switches;   // by setting
        MobileReductionMeter meter;
    };

    void refresh();
    void refreshEffect (EffectSection& section);
    void layoutSettings();
    void fillOutputs();
    void importAudio (const juce::URL& url);
    Cue* cue();
    void changed();
    /** Which slot the button's stock effect is in, -1 if it hasn't got one. */
    int slotOf (StockFx::Kind kind) const;
    StockFx* effect (StockFx::Kind kind) const;
    void setEffectOn (EffectSection& section, bool on);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    ShowController& controller;
    int cueId = -1;
    bool updating = false;

    juce::TextButton backButton { juce::String::fromUTF8 ("\xe2\x80\xb9 Back") };
    juce::Label title;
    WaveformEditor waveform;
    juce::Viewport viewport;
    juce::Component settings;

    juce::Label buttonHeading, effectsHeading, eqHeading, compHeading, limiterHeading, fileInfo;
    juce::TextButton chooseButton { "Choose audio" }, colourButton { "Color" }, clearButton { "Clear button" };
    MobileTextEditor name;
    juce::ComboBox output;
    MobileRow level { "Level" }, pan { "Pan" }, fadeIn { "Fade in" }, fadeOut { "Fade out" }, stopFade { "Stop fade" };
    juce::ToggleButton loop { "Loop" }, fadeOthers { "Fade others when played" };

    EffectSection eq, compressor, limiter;

    std::unique_ptr<juce::FileChooser> chooser;
};

/** Which audio device and buffer to use. */
class MobileAudioPage : public juce::Component
{
public:
    explicit MobileAudioPage (ShowController& controller);
    std::function<void()> onBack;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::TextButton backButton { juce::String::fromUTF8 ("\xe2\x80\xb9 Back") };
    juce::Label title;
    juce::AudioDeviceSelectorComponent selector;
};

/** The main page: banks, the button grid, and Playlist and Now Playing, each of which folds away to a strip
    that keeps its show controls: GO, and Fade all / Stop all. Upright the sections stack; on a wide screen
    they are columns either side of the grid. */
class MobileMainPage : public juce::Component,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    MobileMainPage (ShowController& controller, juce::PropertiesFile& settings);
    ~MobileMainPage() override;

    std::function<void (int id)> onOpenEditor;
    std::function<void (int id)> onChooseSong;
    std::function<void()> onOpenAudio;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** A Surface Duo spanned over both screens: the menu bar, Playlist and Now Playing go in "first" (the top screen
        with the phone on its side), the banks, buttons, GO, FADE and STOP in "second". Empty rectangles = one screen. */
    void setSplit (juce::Rectangle<int> first, juce::Rectangle<int> second, bool across);
    bool isSplit() const { return ! splitFirst.isEmpty(); }

private:
    void layoutSplit();
    juce::Rectangle<int> splitFirst, splitSecond;
    bool splitAcross = false;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override { refresh(); }
    void refresh();
    void rebuildBanks();
    void showMenu();
    void saveAs();
    void save();
    void saveTo (const juce::String& showName);
    void open (const juce::File& file);
    void askName (std::function<void (juce::String)> done);
    void message (const juce::String& text);
    void setOpen (bool playlistSection, bool open);
public:
    /** A key from the deck or a Stream Deck (same ids as the desktop's: "slot7", "go", "bankNext"...). */
    void perform (const juce::String& action);
    int getBank() const { return bank; }
private:
    void showBank (int index);
    void setButtonCount (int count);
    /** Says so if songs sit on buttons the page isn't showing. */
    void warnAboutHiddenButtons();

    ShowController& controller;
    juce::PropertiesFile& settings;
    bool playlistOpen, nowOpen;
    int buttonCount = 12;

    juce::TextButton menuButton { juce::String::fromUTF8 ("\xe2\x98\xb0") }, editButton { "EDIT" };
    juce::Label showName;
    juce::OwnedArray<juce::TextButton> bankButtons;
    int bank = 0;

    juce::TextButton playlistHeader, nowHeader, goButton { "GO" }, fadeAllButton { "FADE" }, stopAllButton { "STOP" };
    PlaylistPanel playlist;
    CueGrid grid;
    NowPlaying nowPlaying;
};

/** The whole app: one page at a time. */
class MobileRoot : public juce::Component,
                   public juce::DragAndDropContainer
{
public:
    MobileRoot (ShowController& controller, juce::PropertiesFile& settings);

    void resized() override;
    void paint (juce::Graphics&) override;
    /** Surface Duo: the hinge in this component (empty = one screen). Spanned, the editor and Audio open on the
        first screen and the buttons stay on the second. */
    void setHinge (juce::Rectangle<int> hinge, bool across);
    /** The phone's back gesture: true if it took a page back. */
    bool goBack();

private:
    void show (juce::Component& page);
    juce::Rectangle<int> hinge;
    bool hingeAcross = false;

    MobileMainPage mainPage;
    MobileEditorPage editorPage;
    MobileAudioPage audioPage;

public:
    MobileMainPage& getMainPage() { return mainPage; }
};
