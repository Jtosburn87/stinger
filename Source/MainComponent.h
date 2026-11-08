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
#include "Remote.h"
#include "ShowCheckPanel.h"
#include "DeviceWatch.h"
#include "AudioPanel.h"
#include "CountdownDisplay.h"
#include "SchedulePanel.h"
#include "RunOfShowPanel.h"
#include "OscPanel.h"
#include "BackupPanel.h"
#include "Autosave.h"

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
    void mouseDown (const juce::MouseEvent& e) override;
    /** The credits window (File > Credits). */
    void showCredits();
    /** For dev snapshots: the credits page as an image. */
    static juce::Image creditsImage();
    /** For dev snapshots: one of the settings panels. */
    juce::Image panelImage (const juce::String& name);

    void openShow (const juce::File& f);
    /** A crash left a recovery copy: asks whether to bring it back. No opens `otherwise` (if it exists) as usual. */
    void offerRecovery (const Autosave::Found& found, const juce::File& recoveryFolder, const juce::File& otherwise);
    /** Asks to save unsaved changes, then calls next(true) to go ahead or next(false) if cancelled. */
    void askToSave (std::function<void (bool proceed)> next);

    ControlSurface& getSurface() { return surface; }
    StreamDeckServer& getStreamDeck() { return streamDeck; }
    Remote& getRemote() { return remote; }
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
    Remote remote;
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
    juce::TextButton playbackModeButton;   // above the grid, right of the bank heading
    static constexpr int playbackModeWidth = 190;
    juce::TextButton selfDestructButton { "SELF DESTRUCT" }, resetColoursButton { "Reset" };   // left of the playback button
    static constexpr int selfDestructWidth = 190;
    juce::TextEditor bankNameEditor;   // over a tab while it's renamed
    int renamingBank = -1;
    void startRenamingBank (int bank);
    void finishRenamingBank (bool keep);
    juce::TextButton fileButton { "File" }, audioButton { "Audio" }, editButton { "EDIT" },
                     globalButton { "GLOBAL KEYS" }, controlsButton { "Controls" }, importButton { "Import" }, runButton { "Run of Show" };
    void showImport (bool links);
    Downloader downloader;
    std::unique_ptr<ImportPanel> importPanel;          // kept while the window is closed: downloads carry on
    juce::Component::SafePointer<juce::DialogWindow> importWindow;
    void setEditMode (bool on);
    void showBank (int bank);
    void perform (const juce::String& action);
    void showControls();
    std::unique_ptr<juce::DocumentWindow> controlsWindow;
    void showRemote();
    /** The Show check window. onlyIfProblems: stays closed when the show is fine (the check as a show opens). */
    void showShowCheck (bool onlyIfProblems);
    /** Waits for the songs to finish loading, then checks. */
    void checkOnceLoaded();
    std::unique_ptr<ShowCheckWindow> showCheckWindow;
    std::unique_ptr<juce::DocumentWindow> remoteWindow;
    juce::Label showName, deviceInfo;
    DeviceWatch deviceWatch;
    juce::Label deviceBanner;
    juce::TextButton backToMain { "Back to main interface" };
    void updateDeviceBanner();
    void showRunOfShow();
    void showSchedule();
    void showCountdown();
    void showLoudnessTargets();
    void showOsc();
    OscBridge osc;
    BackupLink backupLink;
    juce::Label backupBanner;
    juce::TextButton takeOverButton { "TAKE OVER" };
    void showBackup();
    void updateBackupBanner();
    CountdownDisplay countdown;
    BigClock bigClock;
    ScheduleStrip scheduleStrip;
    std::unique_ptr<RunOfShowWindow> runWindow;
    std::unique_ptr<ScheduleWindow> scheduleWindow;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this };
};
