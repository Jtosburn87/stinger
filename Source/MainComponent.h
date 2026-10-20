#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Show.h"
#include "CueGrid.h"
#include "NowPlaying.h"
#include "CueSettings.h"
#include "PlaylistPanel.h"
#include "ControlSurface.h"
#include "StreamDeckServer.h"
#include "Downloader.h"
#include "ImportPanel.h"

/** The main window's content: top bar (banks, file, audio), button grid, now playing, cue editor. */
class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      private juce::ChangeListener
{
public:
    MainComponent (ShowController& controller, juce::PropertiesFile& settings);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    void openShow (const juce::File& f);
    /** Asks to save unsaved changes, then calls next(true) to go ahead or next(false) if cancelled. */
    void askToSave (std::function<void (bool proceed)> next);

    ControlSurface& getSurface() { return surface; }
    StreamDeckServer& getStreamDeck() { return streamDeck; }
    std::function<void()> onTitleChanged;
    juce::String windowTitle() const;

private:
    void rebuildBanks();
    void showFileMenu();
    void showAudioSettings();
    void saveShow (bool askForFile, std::function<void (bool saved)> done = {});
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    ShowController& controller;
    juce::PropertiesFile& settings;
    ControlSurface surface;
    StreamDeckServer streamDeck;
    CueGrid grid;
    PlaylistPanel playlist;
    NowPlaying nowPlaying;
    CueSettings cueSettings;
    /** A bank tab; a button dragged over it opens that bank (so buttons can move between banks).
        Right-click renames it. */
    struct BankTab : juce::TextButton, juce::DragAndDropTarget
    {
        std::function<void()> onDragOver, onRightClick;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && onRightClick)
                onRightClick();
            else
                juce::TextButton::mouseDown (e);
        }
        bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("cue:"); }
        void itemDragEnter (const SourceDetails&) override { if (onDragOver) onDragOver(); }
        void itemDropped (const SourceDetails&) override {}
    };
    juce::OwnedArray<BankTab> bankButtons;
    juce::TextEditor bankNameEditor;   // over a tab while it's renamed
    int renamingBank = -1;
    void startRenamingBank (int bank);
    void finishRenamingBank (bool keep);
    juce::TextButton fileButton { "File" }, audioButton { "Audio" }, editButton { "EDIT" },
                     globalButton { "GLOBAL KEYS" }, controlsButton { "Controls" }, importButton { "Import" };
    void showImport (bool links);
    Downloader downloader;
    std::unique_ptr<ImportPanel> importPanel;          // kept while the window is closed: downloads carry on
    juce::Component::SafePointer<juce::DialogWindow> importWindow;
    void setEditMode (bool on);
    void showBank (int bank);
    void perform (const juce::String& action);
    void showControls();
    std::unique_ptr<juce::DocumentWindow> controlsWindow;
    juce::Label showName, deviceInfo;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this };
};
