#include "MainComponent.h"
#include "ControlsWindow.h"
#include "Look.h"

namespace
{
    constexpr int topBarHeight = 60, playlistWidth = 280, nowPlayingWidth = 300, editorHeight = 360;
    const char* showWildcard = "*.stinger";

    /** File > Credits. */
    class Credits : public juce::Component
    {
    public:
        Credits()
        {
            email.setFont (Look::font (14.0f), false, juce::Justification::centredLeft);
            email.setColour (juce::HyperlinkButton::textColourId, Look::amber);
            email.setWantsKeyboardFocus (false);
            addAndMakeVisible (email);

            message.append ("My name is Justin Osburn. Stinger is a free piece of software I created for myself. It is free now and "
                            "will always be free. If you ever see someone trying to sell it email me directly. You are welcome to use "
                            "Stinger all you want and share it with all your friends. If you are enjoying Stinger, think it's made "
                            "your job easier or just want to say thanks, my PayPal is",
                            Look::font (14.0f), Look::text);
            message.setLineSpacing (3.0f);
            builtWith.append (juce::String::fromUTF8 ("Built with JUCE \xc2\xb7 VST3 and ASIO by Steinberg \xc2\xb7 downloads by yt-dlp, "
                                                      "FFmpeg and Deno. VST and ASIO are trademarks of Steinberg Media Technologies GmbH."),
                              Look::font (12.0f), Look::textDim);
            builtWith.setLineSpacing (2.0f);
            setSize (520, 0);
            layout();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Look::panel);
            g.setColour (Look::text);
            g.setFont (Look::condensed (34.0f).withExtraKerningFactor (0.12f));
            g.drawText ("STINGER", 28, 22, getWidth() - 56, 38, juce::Justification::centredLeft);
            g.setColour (Look::amber);
            g.setFont (Look::condensed (15.0f).withExtraKerningFactor (0.06f));
            g.drawText (juce::String ("by LEFTNoiseAudio  ") + juce::String::fromUTF8 ("\xc2\xb7  version ") + JUCE_APPLICATION_VERSION_STRING,
                        29, 60, getWidth() - 56, 20, juce::Justification::centredLeft);

            juce::TextLayout text;
            text.createLayout (message, (float) textWidth());
            text.draw (g, messageArea.toFloat());
            g.setColour (Look::text);
            g.setFont (Look::font (14.0f));
            g.drawText ("Enjoy!", enjoyArea, juce::Justification::topLeft);
            g.drawText ("-Justin", enjoyArea.translated (0, 22), juce::Justification::topLeft);

            g.setColour (Look::line);
            g.fillRect (28, builtArea.getY() - 14, getWidth() - 56, 1);
            juce::TextLayout built;
            built.createLayout (builtWith, (float) textWidth());
            built.draw (g, builtArea.toFloat());
        }

    private:
        int textWidth() const { return getWidth() - 56; }

        void layout()
        {
            juce::TextLayout text;
            text.createLayout (message, (float) textWidth());
            messageArea = { 28, 100, textWidth(), (int) std::ceil (text.getHeight()) };
            email.setBounds (24, messageArea.getBottom() + 2, 220, 22);
            enjoyArea = { 28, email.getBottom() + 18, textWidth(), 44 };
            juce::TextLayout built;
            built.createLayout (builtWith, (float) textWidth());
            builtArea = { 28, enjoyArea.getBottom() + 28, textWidth(), (int) std::ceil (built.getHeight()) };
            setSize (getWidth(), builtArea.getBottom() + 24);
        }

        juce::AttributedString message, builtWith;
        juce::HyperlinkButton email { "Justinosburn@live.com", juce::URL ("mailto:Justinosburn@live.com") };
        juce::Rectangle<int> messageArea, enjoyArea, builtArea;
    };
}

juce::Image MainComponent::creditsImage()
{
    Credits c;
    return c.createComponentSnapshot (c.getLocalBounds());
}

void MainComponent::showCredits()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new Credits());
    o.dialogTitle = "Credits";
    o.dialogBackgroundColour = Look::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = this;
    o.launchAsync();
}

MainComponent::MainComponent (ShowController& c, juce::PropertiesFile& s)
    : controller (c), settings (s), surface (c, s), streamDeck (c), remote (c, s), grid (c), playlist (c), nowPlaying (c), cueSettings (c)
{
    addAndMakeVisible (grid);
    addAndMakeVisible (playlist);
    playlist.getBank = [this] { return grid.getBank(); };
    playlist.onKey = [this] (const juce::KeyPress& key) { return keyPressed (key); };
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

    playbackModeButton.setWantsKeyboardFocus (false);
    playbackModeButton.setTooltip ("What a button does to what's already playing (saved in the show)");
    playbackModeButton.onClick = [this]
    {
        const auto mode = controller.getShow().playbackMode;
        juce::PopupMenu m;
        m.addSectionHeader ("When a button starts");
        m.addItem (1, "One at a time: it stops whatever is playing", true, mode == PlaybackMode::oneAtATime);
        m.addItem (2, "One per output: it stops what's on its outputs", true, mode == PlaybackMode::onePerOutput);
        m.addItem (3, "Overlap: everything keeps playing", true, mode == PlaybackMode::overlap);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (playbackModeButton),
                         [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                         { if (safe != nullptr && result > 0) safe->controller.setPlaybackMode ((PlaybackMode) (result - 1)); });
    };
    addAndMakeVisible (playbackModeButton);

    selfDestructButton.setClickingTogglesState (true);
    selfDestructButton.setWantsKeyboardFocus (false);
    selfDestructButton.setTooltip ("Self Destruct: each button goes black once it has played (saved in the show). Reset brings the colours back.");
    selfDestructButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff8a1c1f));
    selfDestructButton.onClick = [this] { controller.setSelfDestruct (selfDestructButton.getToggleState()); };
    addAndMakeVisible (selfDestructButton);
    resetColoursButton.setWantsKeyboardFocus (false);
    resetColoursButton.setTooltip ("Give every black button its colour back");
    resetColoursButton.onClick = [this] { controller.resetColours(); };
    resetColoursButton.setEnabled (false);
    addAndMakeVisible (resetColoursButton);
    cueSettings.setDownloader (&downloader);

    surface.perform = [this] (const juce::String& action) { perform (action); };
    surface.shownBank = [this] { return grid.getBank(); };
    streamDeck.perform = [this] (const juce::String& action) { perform (action); };
    streamDeck.shownBank = [this] { return grid.getBank(); };
    streamDeck.colourOf = [] (const Cue& cue) { return "#" + Look::colourOf (cue).toDisplayString (false).toLowerCase(); };
    remote.perform = streamDeck.perform;
    remote.shownBank = streamDeck.shownBank;
    remote.colourOf = streamDeck.colourOf;
    remote.restore();
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
    addMouseListener (this, true);   // see mouseDown
    setSize (1440, 960);
}

MainComponent::~MainComponent()
{
    if (importWindow != nullptr)
        delete importWindow.getComponent();   // it shows importPanel without owning it
    surface.removeChangeListener (this);
    controlsWindow = nullptr;
    remoteWindow = nullptr;
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
    selfDestructButton.setToggleState (controller.isSelfDestruct(), juce::dontSendNotification);
    resetColoursButton.setEnabled (controller.anySpent());
    const auto mode = controller.getShow().playbackMode;
    playbackModeButton.setButtonText (juce::String ("Playback: ") + (mode == PlaybackMode::oneAtATime   ? "one at a time"
                                                                   : mode == PlaybackMode::onePerOutput ? "one per output"
                                                                                                        : "overlap")
                                      + juce::String::fromUTF8 (" \xe2\x96\xbe"));
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
        // after the click is done: mouseDown below would otherwise take the focus straight back and close the box
        button->onRightClick = [safe = juce::Component::SafePointer<MainComponent> (this), b]
        {
            juce::MessageManager::callAsync ([safe, b] { if (safe != nullptr) safe->startRenamingBank (b); });
        };
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
    g.drawText (grid.isEditMode() ? juce::String::fromUTF8 ("EDIT MODE \xe2\x80\x94 clicks select without playing \xc2\xb7 drag a button onto another to swap, or onto a playlist \xc2\xb7 keys still play")
                                  : juce::String::fromUTF8 ("Click to play, again to fade out \xc2\xb7 right-click for more \xc2\xb7 Space = GO \xc2\xb7 Esc stops all"),
                heading.withTrimmedLeft ((int) Look::textWidth (Look::condensed (18.0f), bankTitle) + 16)
                       .withTrimmedRight (playbackModeWidth + selfDestructWidth + 16),
                juce::Justification::centredLeft);
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
    auto headingRow = r.reduced (16, 0).withTrimmedTop (12).removeFromTop (24);
    playbackModeButton.setBounds (headingRow.removeFromRight (playbackModeWidth));
    headingRow.removeFromRight (8);
    auto destruct = headingRow.removeFromRight (selfDestructWidth);
    resetColoursButton.setBounds (destruct.removeFromRight (64));
    destruct.removeFromRight (4);
    selfDestructButton.setBounds (destruct);
    grid.setBounds (r.reduced (16, 0).withTrimmedTop (46).withTrimmedBottom (14));
}

// A click on anything that doesn't take typing (buttons, the grid, the waveform) gives the keys back to the
// window. Otherwise JUCE passes the focus on to the first box that wants it - the button's Name field -
// and hotkeys or a key being learnt would be typed into it.
void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == nullptr || e.eventComponent->getWantsKeyboardFocus())
        return;
    grabKeyboardFocus();
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
        // edit mode: Delete clears the selected button (as "Clear button" in its menu)
        if (key == juce::KeyPress::deleteKey && grid.isEditMode())
        {
            const int id = grid.getSelected();
            if (controller.getShow().isValid (id) && ! controller.getShow().cue (id).isEmpty())
                controller.clearCue (id);
            return true;
        }
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
    if (action.startsWith ("go") && action.length() > 2 && action.substring (2).containsOnly ("0123456789"))
    {
        const int pl = action.substring (2).getIntValue() - 1;   // GO playlist N
        if (pl >= 0 && pl < (int) show.playlists.size())
            controller.go (pl);
        return;
    }
    if (action == "go")              controller.go (controller.getSelectedPlaylist());
    else if (action == "fadeNext")   controller.fadeToNext (controller.getSelectedPlaylist());
    else if (action == "stopAll")    controller.getEngine().stopAll (false);
    else if (action == "fadeAll")    controller.getEngine().stopAll (true);
    else if (action == "bankNext")   showBank ((grid.getBank() + 1) % (int) show.banks.size());
    else if (action == "bankPrev")   showBank ((grid.getBank() + (int) show.banks.size() - 1) % (int) show.banks.size());
    else if (action == "edit")       setEditMode (! grid.isEditMode());
    else if (action == "globalKeys") surface.setGlobal (! surface.isGlobal());
    else if (action == "selfDestruct") controller.setSelfDestruct (! controller.isSelfDestruct());
    else if (action == "resetColours") controller.resetColours();
    else if (action == "nextDown" || action == "nextUp")
    {
        auto* p = controller.currentPlaylist();
        if (p == nullptr || p->entries.empty())
            return;
        const int pl = controller.getSelectedPlaylist();
        const int n = std::max (0, controller.getNextEntry (pl));
        controller.setNextEntry (pl, juce::jlimit (0, (int) p->entries.size() - 1, n + (action == "nextDown" ? 1 : -1)));
    }
}

void MainComponent::showControls()
{
    if (controlsWindow == nullptr)
        controlsWindow = std::make_unique<ControlsWindow> (surface, &streamDeck);
    controlsWindow->setVisible (true);
    controlsWindow->toFront (true);
}

void MainComponent::showRemote()
{
    if (remoteWindow == nullptr)
        remoteWindow = std::make_unique<RemoteWindow> (remote);
    remoteWindow->setVisible (true);
    remoteWindow->toFront (true);
}

void MainComponent::showFileMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "New show");
    m.addItem (2, "Open show...");
    m.addItem (3, "Save show", controller.getShowFile() != juce::File());
    m.addItem (4, "Save show as...");
    m.addSeparator();
    m.addItem (6, remote.isOn() ? "Wi-Fi remote (on)..." : "Wi-Fi remote...");
    m.addSeparator();
    m.addItem (5, "Credits");
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
                const auto start = safe->controller.getShowFile() != juce::File() ? safe->controller.getShowFile()
                                                                                  : safe->controller.getHomeFolder();
                safe->chooser = std::make_unique<juce::FileChooser> ("Open show", start, showWildcard);
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
        else if (result == 5)
            safe->showCredits();
        else if (result == 6)
            safe->showRemote();
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
    // just a name: the show goes in Documents\Stinger\<name>\<name>.stinger (its audio in Audio next to it)
    const auto current = controller.getShowFile();
    auto* w = new juce::AlertWindow ("Save show", "Show name\n(saved in " + controller.getHomeFolder().getFullPathName() + ")",
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", current != juce::File() ? current.getFileNameWithoutExtension() : juce::String ("Show"));
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MainComponent> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, write, done, current] (int result)
    {
        if (safe == nullptr)
            return;
        const auto name = w->getTextEditorContents ("name").trim();
        if (result != 1 || name.isEmpty())
        {
            if (done) done (false);
            return;
        }
        const auto f = safe->controller.showFileFor (name);
        if (! f.existsAsFile() || f == current)
        {
            write (f);
            return;
        }
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Save show",
                                            "There's already a show called \"" + f.getFileNameWithoutExtension() + "\". Replace it?",
                                            "Replace", "Cancel", nullptr,
                                            juce::ModalCallbackFunction::create ([safe, write, done, f] (int ok)
        {
            if (safe == nullptr)
                return;
            if (ok == 1)
                write (f);
            else if (done)
                done (false);
        }));
    }), true);
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
