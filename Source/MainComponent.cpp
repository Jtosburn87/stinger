#include "MainComponent.h"
#include "ControlsWindow.h"
#include "Look.h"

namespace
{
    constexpr int topBarHeight = 60, playlistWidth = 280, nowPlayingWidth = 300, editorHeight = 360;
    const char* showWildcard = "*.stinger";
}

MainComponent::MainComponent (ShowController& c, juce::PropertiesFile& s)
    : controller (c), settings (s), surface (c, s), streamDeck (c), grid (c), playlist (c), nowPlaying (c), cueSettings (c)
{
    addAndMakeVisible (grid);
    addAndMakeVisible (playlist);
    playlist.getRows().getBank = [this] { return grid.getBank(); };
    addAndMakeVisible (nowPlaying);
    addAndMakeVisible (cueSettings);

    showName.setColour (juce::Label::textColourId, Look::textDim);
    deviceInfo.setColour (juce::Label::textColourId, Look::textDim);
    deviceInfo.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (showName);
    addAndMakeVisible (deviceInfo);

    fileButton.onClick = [this] { showFileMenu(); };
    fileButton.setWantsKeyboardFocus (false);
    audioButton.setWantsKeyboardFocus (false);
    audioButton.onClick = [this] { showAudioSettings(); };
    audioButton.setTooltip ("Audio interface, sample rate and buffer size");
    addAndMakeVisible (fileButton);
    addAndMakeVisible (audioButton);

    editButton.setClickingTogglesState (true);
    editButton.setWantsKeyboardFocus (false);
    editButton.setTooltip ("Edit mode (Ctrl+E): clicking a button selects it without playing; drag buttons onto other buttons "
                           "to swap them (over a bank tab to change bank) or onto the playlist");
    editButton.onClick = [this] { setEditMode (editButton.getToggleState()); };
    addAndMakeVisible (editButton);

    importButton.setWantsKeyboardFocus (false);
    importButton.setTooltip ("Import audio: several files at once, or YouTube / Dropbox / OneDrive links");
    importButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Audio files...");
        m.addItem (2, "YouTube / links...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (importButton),
                         [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                         { if (safe != nullptr && result > 0) safe->showImport (result == 2); });
    };
    addAndMakeVisible (importButton);
    cueSettings.setDownloader (&downloader);

    surface.perform = [this] (const juce::String& action) { perform (action); };
    surface.shownBank = [this] { return grid.getBank(); };
    streamDeck.perform = [this] (const juce::String& action) { perform (action); };
    streamDeck.shownBank = [this] { return grid.getBank(); };
    streamDeck.colourOf = [] (const Cue& cue) { return "#" + Look::colourOf (cue).toDisplayString (false).toLowerCase(); };
    grid.slotKey = [this] (int slot) { return surface.getCommands().get ("slot" + juce::String (slot + 1)).keyPress(); };
    cueSettings.setControlSurface (&surface);
    cueSettings.setSettings (&settings);
    surface.addChangeListener (this);

    globalButton.setClickingTogglesState (true);
    globalButton.setWantsKeyboardFocus (false);
    globalButton.setTooltip ("On: Stinger's keys work from any app (and other apps don't get those keys). Turn off to type in other apps.");
    globalButton.onClick = [this] { surface.setGlobal (globalButton.getToggleState()); };
    addAndMakeVisible (globalButton);
    controlsButton.setWantsKeyboardFocus (false);
    controlsButton.setTooltip ("Keys, MIDI and light feedback for every command");
    controlsButton.onClick = [this] { showControls(); };
    addAndMakeVisible (controlsButton);

    grid.onSelect = [this] (int id) { cueSettings.setCue (id); };
    cueSettings.setCue (grid.getSelected());

    // bank rename: Enter or clicking away keeps it, Esc cancels
    bankNameEditor.setInputRestrictions (30);
    bankNameEditor.setJustification (juce::Justification::centred);
    bankNameEditor.onReturnKey = [this] { finishRenamingBank (true); };
    bankNameEditor.onEscapeKey = [this] { finishRenamingBank (false); };
    bankNameEditor.onFocusLost = [this] { finishRenamingBank (true); };
    addChildComponent (bankNameEditor);

    controller.addChangeListener (this);
    controller.getEngine().getDeviceManager().addChangeListener (this);
    rebuildBanks();
    changeListenerCallback (nullptr);

    setWantsKeyboardFocus (true);
    setSize (1440, 960);
}

MainComponent::~MainComponent()
{
    if (importWindow != nullptr)
        delete importWindow.getComponent();   // it shows importPanel without owning it
    surface.removeChangeListener (this);
    controlsWindow = nullptr;
    controller.getEngine().getDeviceManager().removeChangeListener (this);
    controller.removeChangeListener (this);
}

juce::String MainComponent::windowTitle() const
{
    const auto f = controller.getShowFile();
    return juce::String ("Stinger ") + JUCE_APPLICATION_VERSION_STRING + juce::String::fromUTF8 (" \xe2\x80\x94 ")
         + (f == juce::File() ? juce::String ("Untitled show") : f.getFileNameWithoutExtension())
         + (controller.hasUnsavedChanges() ? " *" : "");
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    globalButton.setToggleState (surface.isGlobal(), juce::dontSendNotification);
    globalButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5a1f22));
    const auto f = controller.getShowFile();
    showName.setText ((f == juce::File() ? juce::String ("Untitled show") : f.getFileName())
                      + (controller.hasUnsavedChanges() ? "  (not saved)" : ""), juce::dontSendNotification);
    if (auto* device = controller.getEngine().getDeviceManager().getCurrentAudioDevice())
        deviceInfo.setText (device->getTypeName() + juce::String::fromUTF8 (" \xc2\xb7 ") + device->getName()
                            + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " kHz"
                            + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (device->getOutputChannelNames().size()) + " outputs",
                            juce::dontSendNotification);
    else
        deviceInfo.setText ("No audio device - click Audio", juce::dontSendNotification);
    audioButton.setTooltip (deviceInfo.getText() + " (click to change)");
    if ((int) bankButtons.size() != (int) controller.getShow().banks.size())
        rebuildBanks();
    for (int b = 0; b < bankButtons.size(); ++b)
        bankButtons[b]->setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + b))
                                       + juce::String::fromUTF8 (" \xc2\xb7 ") + controller.getShow().banks[(size_t) b].name);
    if (onTitleChanged)
        onTitleChanged();
}

void MainComponent::setEditMode (bool on)
{
    editButton.setToggleState (on, juce::dontSendNotification);
    grid.setEditMode (on);
    repaint();
}

void MainComponent::rebuildBanks()
{
    bankButtons.clear();
    for (int b = 0; b < (int) controller.getShow().banks.size(); ++b)
    {
        auto* button = bankButtons.add (new BankTab());
        button->onDragOver = [this, b] { showBank (b); };   // a button dragged over the tab: show that bank to drop it in
        button->setRadioGroupId (1);
        button->setClickingTogglesState (true);
        button->setToggleState (b == grid.getBank(), juce::dontSendNotification);
        button->onClick = [this, b] { grid.setBank (b); };
        button->onRightClick = [this, b] { startRenamingBank (b); };
        button->setTooltip ("Ctrl+" + juce::String (b + 1) + " (right-click to rename)");
        button->setWantsKeyboardFocus (false);
        addAndMakeVisible (button);
    }
    resized();
}

void MainComponent::startRenamingBank (int bank)
{
    if (bank < 0 || bank >= bankButtons.size())
        return;
    finishRenamingBank (true);
    renamingBank = bank;
    bankNameEditor.setText (controller.getShow().banks[(size_t) bank].name, juce::dontSendNotification);
    bankNameEditor.setBounds (bankButtons[bank]->getBounds());
    bankNameEditor.setVisible (true);
    bankNameEditor.toFront (true);
    bankNameEditor.grabKeyboardFocus();
    bankNameEditor.selectAll();
}

void MainComponent::finishRenamingBank (bool keep)
{
    if (renamingBank < 0)
        return;
    const int bank = std::exchange (renamingBank, -1);
    bankNameEditor.setVisible (false);
    if (keep)
        controller.renameBank (bank, bankNameEditor.getText());   // the tab text follows via the change message
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Look::ground);
    auto bar = getLocalBounds().removeFromTop (topBarHeight);
    g.setColour (Look::panel);
    g.fillRect (bar);
    g.setColour (Look::line);
    g.fillRect (bar.removeFromBottom (1));

    // logo: a swell line + the name
    juce::Path swell;
    swell.startNewSubPath (22, 30);
    swell.lineTo (28, 30);
    swell.lineTo (33, 16);
    swell.lineTo (40, 44);
    swell.lineTo (45, 30);
    swell.lineTo (53, 30);
    g.setColour (Look::amber);
    g.strokePath (swell, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (Look::text);
    g.setFont (Look::condensed (26.0f).withExtraKerningFactor (0.12f));
    g.drawText ("STINGER", 60, 6, 140, 30, juce::Justification::centredLeft);
    g.setColour (Look::textFaint);
    g.setFont (Look::condensed (12.5f).withExtraKerningFactor (0.06f));
    g.drawText ("by LEFTNoiseAudio", 61, 34, 140, 16, juce::Justification::centredLeft);

    // grid heading
    auto gridArea = getLocalBounds().withTrimmedTop (topBarHeight).withTrimmedBottom (editorHeight)
                                    .withTrimmedRight (nowPlayingWidth).withTrimmedLeft (playlistWidth);
    auto heading = gridArea.reduced (16, 0).withTrimmedTop (12).removeFromTop (24);
    const int bank = grid.getBank();
    g.setColour (juce::Colour (0xffc9d0d8));
    g.setFont (Look::condensed (18.0f));
    const auto bankTitle = "BANK " + juce::String::charToString ((juce::juce_wchar) ('A' + bank)) + juce::String::fromUTF8 (" \xc2\xb7 ")
                         + controller.getShow().banks[(size_t) bank].name.toUpperCase();
    g.drawText (bankTitle, heading, juce::Justification::centredLeft);
    g.setColour (Look::textDim);
    g.setFont (Look::font (12.0f));
    if (grid.isEditMode())
        g.setColour (Look::amber);
    g.drawText (grid.isEditMode() ? juce::String::fromUTF8 ("EDIT MODE \xe2\x80\x94 clicks select without playing \xc2\xb7 drag a button onto another to swap, or onto the playlist \xc2\xb7 keys still play")
                                  : juce::String::fromUTF8 ("Click to play, again to fade out \xc2\xb7 drop files onto buttons \xc2\xb7 right-click for more \xc2\xb7 EDIT to move buttons \xc2\xb7 Space = GO \xc2\xb7 Esc stops all"),
                heading.withTrimmedLeft ((int) Look::textWidth (Look::condensed (18.0f), bankTitle) + 16), juce::Justification::centredLeft);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (topBarHeight).reduced (20, 12);
    bar.removeFromLeft (190);
    showName.setBounds (bar.removeFromLeft (170));
    audioButton.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (8);
    fileButton.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (8);
    editButton.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (8);
    controlsButton.setBounds (bar.removeFromRight (84));
    bar.removeFromRight (8);
    globalButton.setBounds (bar.removeFromRight (110));
    bar.removeFromRight (8);
    importButton.setBounds (bar.removeFromRight (76));
    bar.removeFromRight (12);
    // bank tabs: 118 wide while there's room (up to 8 banks share what's left)
    const int tabWidth = bankButtons.isEmpty() ? 0 : std::min (118, (bar.getWidth() - 4 * bankButtons.size()) / bankButtons.size());
    for (auto* b : bankButtons)
    {
        b->setBounds (bar.removeFromLeft (tabWidth));
        bar.removeFromLeft (4);
    }
    deviceInfo.setBounds (bar);
    deviceInfo.setVisible (bar.getWidth() >= 180);

    cueSettings.setBounds (r.removeFromBottom (editorHeight));
    playlist.setBounds (r.removeFromLeft (playlistWidth));
    nowPlaying.setBounds (r.removeFromRight (nowPlayingWidth));
    grid.setBounds (r.reduced (16, 0).withTrimmedTop (46).withTrimmedBottom (14));
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (surface.learning().isEmpty())
    {
        if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
            return cueSettings.getEditor().undo() || true;
        if (key == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)
            || key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
            return cueSettings.getEditor().redo() || true;
        if (dynamic_cast<juce::TextEditor*> (getCurrentlyFocusedComponent()) != nullptr)
            return false;
        // the waveform's selection
        auto& editor = cueSettings.getEditor();
        if (key == juce::KeyPress ('x', juce::ModifierKeys::commandModifier, 0))
            return editor.cutSelection (true) || true;
        if (key == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0))
            return editor.copySelection() || true;
        if (key == juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0))
            return editor.paste() || true;
        if (key == juce::KeyPress::deleteKey && editor.hasSelection())
            return editor.cutSelection (false);
    }
    return surface.handleKey (key);
}

void MainComponent::showBank (int b)
{
    if (b < 0 || b >= (int) controller.getShow().banks.size())
        return;
    grid.setBank (b);
    if (b < bankButtons.size())
        bankButtons[b]->setToggleState (true, juce::dontSendNotification);
    repaint();
}

void MainComponent::perform (const juce::String& action)
{
    auto& show = controller.getShow();
    if (action.startsWith ("cue:"))
    {
        controller.trigger (action.fromFirstOccurrenceOf ("cue:", false, false).getIntValue());
        return;
    }
    if (action.startsWith ("slot"))
    {
        const int id = Show::cueId (grid.getBank(), action.fromFirstOccurrenceOf ("slot", false, false).getIntValue() - 1);
        if (show.isValid (id) && ! show.cue (id).isEmpty())
            controller.trigger (id);
        return;
    }
    if (action.startsWith ("bank") && action.substring (4).containsOnly ("0123456789") && action.length() > 4)
    {
        showBank (action.substring (4).getIntValue() - 1);
        return;
    }
    if (action == "go")              controller.go();
    else if (action == "fadeNext")   controller.fadeToNext();
    else if (action == "stopAll")    controller.getEngine().stopAll (false);
    else if (action == "fadeAll")    controller.getEngine().stopAll (true);
    else if (action == "bankNext")   showBank ((grid.getBank() + 1) % (int) show.banks.size());
    else if (action == "bankPrev")   showBank ((grid.getBank() + (int) show.banks.size() - 1) % (int) show.banks.size());
    else if (action == "edit")       setEditMode (! grid.isEditMode());
    else if (action == "globalKeys") surface.setGlobal (! surface.isGlobal());
    else if (action == "nextDown" || action == "nextUp")
    {
        auto* p = controller.currentPlaylist();
        if (p == nullptr || p->entries.empty())
            return;
        const int n = std::max (0, controller.getNextEntry());
        controller.setNextEntry (juce::jlimit (0, (int) p->entries.size() - 1, n + (action == "nextDown" ? 1 : -1)));
    }
}

void MainComponent::showControls()
{
    if (controlsWindow == nullptr)
        controlsWindow = std::make_unique<ControlsWindow> (surface, &streamDeck);
    controlsWindow->setVisible (true);
    controlsWindow->toFront (true);
}

void MainComponent::showFileMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "New show");
    m.addItem (2, "Open show...");
    m.addItem (3, "Save show", controller.getShowFile() != juce::File());
    m.addItem (4, "Save show as...");
    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fileButton), [safe] (int result)
    {
        if (safe == nullptr)
            return;
        if (result == 1)
            safe->askToSave ([safe] (bool go) { if (safe != nullptr && go) safe->controller.newShow(); });
        else if (result == 2)
            safe->askToSave ([safe] (bool go)
            {
                if (safe == nullptr || ! go)
                    return;
                safe->chooser = std::make_unique<juce::FileChooser> ("Open show", safe->controller.getShowFile(), showWildcard);
                safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                            [safe] (const juce::FileChooser& fc)
                {
                    if (safe != nullptr && fc.getResult() != juce::File())
                        safe->openShow (fc.getResult());
                });
            });
        else if (result == 3)
            safe->saveShow (false);
        else if (result == 4)
            safe->saveShow (true);
    });
}

void MainComponent::openShow (const juce::File& f)
{
    juce::String error;
    if (controller.openShow (f, error))
    {
        settings.setValue ("lastShow", f.getFullPathName());
        if (! controller.getMissingPlugins().isEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Plugins not found",
                                                    "This show uses plugins that aren't on this PC (or haven't been scanned yet - Plugins... in the effects panel):\n\n"
                                                    + controller.getMissingPlugins().joinIntoString ("\n"));
    }
    else
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open show", error);
    grid.setBank (0);
    rebuildBanks();
}

void MainComponent::saveShow (bool askForFile, std::function<void (bool)> done)
{
    auto write = [this, done] (const juce::File& f)
    {
        juce::String error;
        const bool ok = controller.saveShow (f, error);
        if (ok)
            settings.setValue ("lastShow", f.getFullPathName());
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save show", error);
        if (done)
            done (ok);
    };
    if (! askForFile && controller.getShowFile() != juce::File())
    {
        write (controller.getShowFile());
        return;
    }
    const auto start = controller.getShowFile() != juce::File()
                         ? controller.getShowFile()
                         : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Show.stinger");
    chooser = std::make_unique<juce::FileChooser> ("Save show", start, showWildcard);
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, write, done] (const juce::FileChooser& fc)
    {
        if (safe == nullptr)
            return;
        auto f = fc.getResult();
        if (f == juce::File())
        {
            if (done) done (false);
            return;
        }
        write (f.withFileExtension ("stinger"));
    });
}

void MainComponent::askToSave (std::function<void (bool)> next)
{
    if (! controller.hasUnsavedChanges())
    {
        next (true);
        return;
    }
    juce::Component::SafePointer<MainComponent> safe (this);
    auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel (juce::MessageBoxIconType::QuestionIcon, "Unsaved changes",
                                                                    "Save the changes to this show first?", "Save", "Don't save", "Cancel", this);
    juce::AlertWindow::showAsync (options, [safe, next] (int result)
    {
        if (safe == nullptr)
            return;
        if (result == 1)
            safe->saveShow (false, [next] (bool saved) { next (saved); });
        else
            next (result == 2);
    });
}

void MainComponent::showImport (bool links)
{
    if (importPanel == nullptr)
        importPanel = std::make_unique<ImportPanel> (controller, downloader, [this] { return grid.getBank(); });
    importPanel->showLinksTab (links);
    if (importWindow != nullptr)
    {
        importWindow->toFront (true);
        return;
    }
    juce::DialogWindow::LaunchOptions o;
    o.content.setNonOwned (importPanel.get());
    o.dialogTitle = "Import audio";
    o.dialogBackgroundColour = Look::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    importWindow = o.launchAsync();
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (controller.getEngine().getDeviceManager(),
                                                             0, PlaybackEngine::maxInputs, 2, PlaybackEngine::maxOutputs, false, false, false, false);
    selector->setSize (560, 560);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio";
    o.dialogBackgroundColour = Look::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}
