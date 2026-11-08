#include "RunOfShowPanel.h"
#include "SchedulePanel.h"
#include "Look.h"

namespace
{
    enum { numberColumn = 1, timeColumn, personColumn, songColumn, notesColumn };

    struct TextCell : juce::Label
    {
        TextCell (ShowController& c, int col) : controller (c), column (col)
        {
            setEditable (true, true, false);
            setColour (juce::Label::textColourId, Look::text);
            onTextChange = [this]
            {
                auto& rows = controller.getShow().run;
                if (row < 0 || row >= (int) rows.size())
                    return;
                auto r = rows[(size_t) row];
                if (column == timeColumn)
                {
                    // strict: four digits or nothing at all; anything unfinished puts the old time back
                    const auto typed = getText();
                    if (typed.isEmpty())
                        r.time.clear();
                    else
                    {
                        const auto now = juce::Time::getCurrentTime();
                        const int t = typed.length() == 5 ? parseTimeOfDay (typed, now.getHours() * 3600 + now.getMinutes() * 60 + now.getSeconds()) : -1;
                        if (t >= 0)
                            r.time = timeOfDayText (t);
                    }
                }
                else
                    (column == personColumn ? r.person : r.notes) = getText();
                controller.setRunRow (row, r);
                show (row);
            };
        }

        juce::TextEditor* createEditorComponent() override
        {
            auto* editor = juce::Label::createEditorComponent();
            if (column == timeColumn)
            {
                editor->setInputRestrictions (5, "0123456789");
                editor->setKeyboardType (juce::TextInputTarget::numericKeyboard);
            }
            return editor;
        }

        void editorShown (juce::TextEditor* editor) override
        {
            if (column != timeColumn)
                return;
            editor->setText (maskTime (getText(), false), false);
            editor->selectAll();
            editor->onTextChange = [editor]
            {
                const auto masked = maskTime (editor->getText(), false);
                if (masked != editor->getText())
                {
                    editor->setText (masked, false);
                    editor->moveCaretToEnd();
                }
            };
        }

        void show (int index)
        {
            row = index;
            if (isBeingEdited())
                return;
            const auto& r = controller.getShow().run[(size_t) index];
            setText (column == timeColumn ? r.time : column == personColumn ? r.person : r.notes, juce::dontSendNotification);
            if (column == timeColumn)
                setTooltip ("Hours and minutes, four digits: 1905 for 19:05. This is for reading: it doesn't start anything by itself.");
        }
        ShowController& controller;
        const int column;
        int row = -1;
    };

    struct SongCell : juce::TextButton
    {
        explicit SongCell (ShowController& c) : controller (c)
        {
            setWantsKeyboardFocus (false);
            onClick = [this]
            {
                chooseButton (controller, *this, true, [this] (int id)
                {
                    auto& rows = controller.getShow().run;
                    if (row >= 0 && row < (int) rows.size())
                    {
                        auto r = rows[(size_t) row];
                        r.cueId = id >= 0 ? id : -1;
                        r.playlist = id <= -2 ? -2 - id : -1;
                        controller.setRunRow (row, r);
                    }
                });
            };
        }
        void show (int index)
        {
            row = index;
            const auto& show = controller.getShow();
            const auto& rowData = show.run[(size_t) index];
            if (rowData.playlist >= 0 && rowData.playlist < (int) show.playlists.size())
            {
                setButtonText ("Playlist: " + show.playlists[(size_t) rowData.playlist].displayName (rowData.playlist));
                return;
            }
            const int id = rowData.cueId;
            setButtonText (id < 0 ? juce::String ("(note)") : show.isValid (id) && ! show.cue (id).isEmpty() ? show.cue (id).name : juce::String ("(empty button)"));
        }
        ShowController& controller;
        int row = -1;
    };
}

RunOfShowPanel::RunOfShowPanel (ShowController& c, std::function<int()> selected) : controller (c), selectedButton (std::move (selected))
{
    table.setModel (this);
    table.setRowHeight (32);
    table.setColour (juce::ListBox::backgroundColourId, Look::sidePanel);
    auto& header = table.getHeader();
    header.addColumn ("#", numberColumn, 70, 50, 90, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Time", timeColumn, 80, 50, 140, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Who", personColumn, 190, 80, 400, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Song", songColumn, 220, 80, 500, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Notes", notesColumn, 300, 80, 900, juce::TableHeaderComponent::notSortable);
    table.setWantsKeyboardFocus (false);
    addAndMakeVisible (table);

    go.setColour (juce::TextButton::buttonColourId, Look::goGreen);
    go.getProperties().set ("big", true);
    go.setTooltip ("Play the Next row and move on (a note just moves on)");
    go.onClick = [this] { controller.runGo(); };
    addSong.setTooltip ("A new row with the button selected in the main window. You can also drag buttons here (Edit mode, or Ctrl+drag), or a playlist by its title.");
    addSong.onClick = [this] { addRow (selectedButton ? selectedButton() : -1); };
    addNote.setTooltip ("A row with no audio, like a speech");
    addNote.onClick = [this] { addRow (-1); };
    remove.onClick = [this] { controller.removeRunRow (table.getSelectedRow()); };
    auto move = [this] (int by)
    {
        const int row = table.getSelectedRow();
        if (row < 0)
            return;
        controller.moveRunRow (row, row + by);
        table.selectRow (juce::jlimit (0, getNumRows() - 1, row + by));
    };
    up.onClick = [move] { move (-1); };
    down.onClick = [move] { move (1); };
    setNext.setTooltip ("Make the selected row the one GO plays (double-clicking a row number does the same)");
    setNext.onClick = [this] { controller.setRunNext (table.getSelectedRow()); };
    for (auto* b : { &go, &addSong, &addNote, &remove, &up, &down, &setNext })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    controller.addChangeListener (this);
    setSize (920, 560);
}

RunOfShowPanel::~RunOfShowPanel()
{
    controller.removeChangeListener (this);
}

void RunOfShowPanel::addRow (int cueId)
{
    const auto& show = controller.getShow();
    RunRow r;
    r.cueId = show.isValid (cueId) && ! show.cue (cueId).isEmpty() ? cueId : -1;
    const int selected = table.getSelectedRow();
    const int at = controller.addRunRow (r, selected >= 0 ? selected + 1 : -1);
    table.selectRow (at);
}

void RunOfShowPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    table.updateContent();
    table.repaint();
}

void RunOfShowPanel::cellDoubleClicked (int row, int column, const juce::MouseEvent&)
{
    if (column == numberColumn)
        controller.setRunNext (row);
}

void RunOfShowPanel::paintRowBackground (juce::Graphics& g, int row, int, int height, bool selected)
{
    if (row == controller.getRunOnAir())
        g.fillAll (Look::shade (Look::amber, 0.22f));
    else if (row == controller.getRunNext())
        g.fillAll (Look::shade (Look::blue, 0.22f));
    if (selected)
    {
        g.setColour (Look::blue);
        g.fillRect (0, 0, 3, height);
    }
}

void RunOfShowPanel::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (column != numberColumn)
        return;
    const bool onAir = row == controller.getRunOnAir(), next = row == controller.getRunNext();
    g.setColour (onAir ? Look::amber : next ? Look::blue : Look::textDim);
    g.setFont (Look::condensed (14.0f));
    g.drawText (onAir ? juce::String ("ON AIR") : next ? juce::String ("NEXT") : juce::String (row + 1),
                6, 0, width - 8, height, juce::Justification::centredLeft);
}

juce::Component* RunOfShowPanel::refreshComponentForCell (int row, int column, bool, juce::Component* existing)
{
    if (row < 0 || row >= getNumRows() || column == numberColumn)
    {
        delete existing;
        return nullptr;
    }
    if (column == songColumn)
    {
        auto* cell = dynamic_cast<SongCell*> (existing);
        if (cell == nullptr)
        {
            delete existing;
            cell = new SongCell (controller);
        }
        cell->show (row);
        return cell;
    }
    auto* cell = dynamic_cast<TextCell*> (existing);
    if (cell == nullptr || cell->column != column)
    {
        delete existing;
        cell = new TextCell (controller, column);
    }
    cell->show (row);
    return cell;
}

void RunOfShowPanel::paint (juce::Graphics& g)
{
    g.fillAll (Look::panel);
}

void RunOfShowPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto bottom = r.removeFromBottom (44);
    go.setBounds (bottom.removeFromRight (150));
    bottom.removeFromRight (16);
    bottom = bottom.withSizeKeepingCentre (bottom.getWidth(), 30);
    for (auto* b : { &addSong, &addNote, &remove, &up, &down, &setNext })
    {
        b->setBounds (bottom.removeFromLeft (b == &addSong ? 150 : 96));
        bottom.removeFromLeft (6);
    }
    r.removeFromBottom (8);
    table.setBounds (r);
}

RunOfShowWindow::RunOfShowWindow (ShowController& controller, std::function<int()> selected)
    : juce::DocumentWindow ("Run of show", Look::panel, juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new RunOfShowPanel (controller, std::move (selected)), true);
    setResizable (true, false);
    setResizeLimits (860, 300, 3000, 2000);
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);   // stays in view while the main window is used
    centreWithSize (getWidth(), getHeight());
}

int RunOfShowPanel::insertIndexAt (juce::Point<int> position) const
{
    const auto inTable = table.getLocalPoint (this, position);
    const int row = table.getRowContainingPosition (inTable.x, inTable.y);
    if (row < 0)
        return controller.getShow().run.empty() || inTable.y > table.getHeaderHeight() ? (int) controller.getShow().run.size() : 0;
    const auto bounds = table.getRowPosition (row, true);
    return inTable.y > bounds.getCentreY() ? row + 1 : row;
}

bool RunOfShowPanel::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith ("cue:") || d.description.toString().startsWith ("playlist:");
}

void RunOfShowPanel::itemDragMove (const SourceDetails& d)
{
    dropAt = insertIndexAt (d.localPosition);
    repaint();
}

void RunOfShowPanel::itemDragExit (const SourceDetails&)
{
    dropAt = -1;
    repaint();
}

void RunOfShowPanel::itemDropped (const SourceDetails& d)
{
    RunRow r;
    const auto what = d.description.toString();
    if (what.startsWith ("playlist:"))
        r.playlist = what.fromFirstOccurrenceOf ("playlist:", false, false).getIntValue();
    else
        r.cueId = what.fromFirstOccurrenceOf ("cue:", false, false).getIntValue();
    const int at = controller.addRunRow (r, insertIndexAt (d.localPosition));
    dropAt = -1;
    table.updateContent();
    table.selectRow (at);
    repaint();
}

void RunOfShowPanel::paintOverChildren (juce::Graphics& g)
{
    if (dropAt < 0)
        return;
    const int rows = getNumRows();
    const int rowHeight = table.getRowHeight();
    const int y = table.getY() + table.getHeaderHeight() + std::min (dropAt, rows) * rowHeight
                - (table.getViewport() != nullptr ? table.getViewport()->getViewPositionY() : 0);
    g.setColour (Look::amber);
    g.fillRect (table.getX(), juce::jlimit (table.getY(), table.getBottom() - 3, y - 1), table.getWidth(), 3);
}
