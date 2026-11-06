#include "ShowCheckPanel.h"
#include "Look.h"

ShowCheckPanel::ShowCheckPanel (ShowController& c) : controller (c)
{
    list.setModel (this);
    list.setRowHeight (30);
    list.setColour (juce::ListBox::backgroundColourId, Look::sidePanel);
    list.setWantsKeyboardFocus (false);
    addAndMakeVisible (list);

    checkAgain.setTooltip ("Look through the show again");
    findFolder.setTooltip ("Look for every missing file, by name, in a folder and the folders inside it");
    findAll.setTooltip ("Look for every missing file, by name, on every drive of this computer. It runs in the background and doesn't affect playback.");
    findManual.setTooltip ("Pick the selected song's file yourself (other missing songs in that folder are found too)");
    for (auto* b : { &checkAgain, &findFolder, &findAll, &findManual, &stop })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    checkAgain.onClick = [this] { refresh(); };
    findFolder.onClick = [this] { searchFolder(); };
    findAll.onClick = [this] { startSearch (FileSearch::wholeComputer()); };
    findManual.onClick = [this] { findByHand (list.getSelectedRow()); };
    stop.onClick = [this] { search.cancel(); };
    controller.addChangeListener (this);
    setSize (760, 460);
    refresh();
}

ShowCheckPanel::~ShowCheckPanel()
{
    controller.removeChangeListener (this);
}

void ShowCheckPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (isShowing())
        refresh();
}

void ShowCheckPanel::refresh()
{
    const int selected = list.getSelectedRow();
    issues = controller.checkShow();
    list.updateContent();
    list.repaint();
    if (selected >= 0 && selected < (int) issues.size())
        list.selectRow (selected);
    else if (! issues.empty())
        list.selectRow (0);
    updateButtons();
    repaint();
}

juce::StringArray ShowCheckPanel::missingNames() const
{
    juce::StringArray names;
    for (auto& i : issues)
        if (i.kind == ShowIssue::Kind::missingAudio)
            names.addIfNotAlreadyThere (i.file.getFileName());
    return names;
}

void ShowCheckPanel::updateButtons()
{
    const bool missing = ! missingNames().isEmpty();
    const int row = list.getSelectedRow();
    findFolder.setEnabled (missing && ! searching);
    findAll.setEnabled (missing && ! searching);
    findManual.setEnabled (! searching && row >= 0 && row < (int) issues.size() && issues[(size_t) row].kind == ShowIssue::Kind::missingAudio);
    stop.setVisible (searching);
}

void ShowCheckPanel::startSearch (const juce::Array<juce::File>& roots)
{
    const auto names = missingNames();
    if (names.isEmpty() || roots.isEmpty())
        return;
    foundCount = 0;
    searchResult.clear();
    searching = true;
    search.start (names, roots);
    startTimerHz (5);
    updateButtons();
}

void ShowCheckPanel::searchFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Search for the missing audio in...",
                                                   juce::File::getSpecialLocation (juce::File::userHomeDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<ShowCheckPanel> (this)] (const juce::FileChooser& fc)
    {
        if (safe != nullptr && fc.getResult().isDirectory())
            safe->startSearch ({ fc.getResult() });
    });
}

void ShowCheckPanel::findByHand (int row)
{
    if (row < 0 || row >= (int) issues.size() || issues[(size_t) row].kind != ShowIssue::Kind::missingAudio)
        return;
    const auto wanted = issues[(size_t) row].file;
    chooser = std::make_unique<juce::FileChooser> ("Where is " + wanted.getFileName() + "?",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<ShowCheckPanel> (this), wanted] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || ! fc.getResult().existsAsFile())
            return;
        safe->controller.relinkFile (wanted, fc.getResult());
        // the rest of a moved folder is usually in the same place
        for (auto& issue : safe->controller.checkShow())
            if (issue.kind == ShowIssue::Kind::missingAudio)
                if (auto beside = fc.getResult().getSiblingFile (issue.file.getFileName()); beside.existsAsFile())
                    safe->controller.relinkFile (issue.file, beside);
        safe->refresh();
    });
}

void ShowCheckPanel::applyFound()
{
    const auto found = search.takeFound();
    if (found.empty())
        return;
    const auto current = controller.checkShow();
    for (auto& [name, file] : found)
        for (auto& issue : current)
            if (issue.kind == ShowIssue::Kind::missingAudio && issue.file.getFileName().toLowerCase() == name)
            {
                controller.relinkFile (issue.file, file);
                ++foundCount;
            }
    refresh();
}

void ShowCheckPanel::timerCallback()
{
    applyFound();
    if (! search.isSearching())
    {
        stopTimer();
        searching = false;
        applyFound();
        searchResult = "Search finished: " + juce::String (foundCount) + (foundCount == 1 ? " song" : " songs") + " found.";
        refresh();
        return;
    }
    repaint();
}

void ShowCheckPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) issues.size())
        return;
    if (selected)
        g.fillAll (Look::raised);
    const auto& issue = issues[(size_t) row];
    g.setColour (issue.kind == ShowIssue::Kind::emptyEntry ? Look::amber : Look::red);
    g.fillEllipse (10.0f, (float) height / 2.0f - 4.0f, 8.0f, 8.0f);
    g.setColour (Look::text);
    g.setFont (Look::font (13.0f));
    g.drawFittedText (issue.text, 28, 0, width - 36, height, juce::Justification::centredLeft, 1, 0.8f);
}

void ShowCheckPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (! searching)
        findByHand (row);
}

void ShowCheckPanel::paint (juce::Graphics& g)
{
    g.fillAll (Look::panel);
    auto r = getLocalBounds().reduced (16).removeFromTop (54);
    const bool good = issues.empty();
    g.setColour (good ? Look::green : Look::red);
    g.setFont (Look::condensed (24.0f));
    g.drawText (good ? juce::String ("SHOW READY") : juce::String ((int) issues.size()) + (issues.size() == 1 ? " PROBLEM" : " PROBLEMS"),
                r.removeFromTop (30), juce::Justification::centredLeft);
    g.setColour (Look::textDim);
    g.setFont (Look::font (12.5f));
    const auto line = searching ? "Searching (" + juce::String (search.getFoldersSearched()) + " folders): " + search.getProgress()
                    : searchResult.isNotEmpty() ? searchResult
                    : good ? juce::String ("Every song, plugin and output is where the show expects it.")
                           : juce::String ("Double-click a missing song to find it by hand.");
    g.drawFittedText (line, r, juce::Justification::centredLeft, 1, 0.7f);
}

void ShowCheckPanel::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (60);
    auto buttons = r.removeFromBottom (32);
    checkAgain.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (8);
    findManual.setBounds (buttons.removeFromLeft (130));
    buttons.removeFromLeft (8);
    findFolder.setBounds (buttons.removeFromLeft (150));
    buttons.removeFromLeft (8);
    findAll.setBounds (buttons.removeFromLeft (170));
    stop.setBounds (buttons.withTrimmedLeft (8));
    r.removeFromBottom (10);
    list.setBounds (r);
}

ShowCheckWindow::ShowCheckWindow (ShowController& controller)
    : juce::DocumentWindow ("Show check", Look::panel, juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new ShowCheckPanel (controller), true);
    setResizable (true, false);
    setResizeLimits (640, 320, 2000, 1400);
    centreWithSize (getWidth(), getHeight());
}
