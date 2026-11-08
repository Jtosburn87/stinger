#include "SchedulePanel.h"
#include "CountdownDisplay.h"
#include "Look.h"

void chooseButton (ShowController& controller, juce::Component& target, bool allowNone, std::function<void (int)> done)
{
    const auto& show = controller.getShow();
    juce::PopupMenu menu;
    if (allowNone)
    {
        menu.addItem (1, "No song (a note)");
        for (int p = 0; p < (int) show.playlists.size(); ++p)
            menu.addItem (5000 + p, "Playlist: " + show.playlists[(size_t) p].displayName (p));
        menu.addSeparator();
    }
    for (int b = 0; b < (int) show.banks.size(); ++b)
    {
        juce::PopupMenu bank;
        for (int s = 0; s < Show::cuesPerBank; ++s)
            if (const int id = Show::cueId (b, s); ! show.cue (id).isEmpty())
                bank.addItem (id + 2, juce::String (s + 1) + ". " + show.cue (id).name);
        if (bank.getNumItems() > 0)
            menu.addSubMenu (juce::String::charToString ((juce::juce_wchar) ('A' + b)) + juce::String::fromUTF8 (" \xc2\xb7 ") + show.banks[(size_t) b].name, bank);
    }
    if (menu.getNumItems() == 0)
        menu.addItem (-1, "No buttons have audio yet", false);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [done] (int result)
    {
        if (result >= 5000)
            done (-2 - (result - 5000));     // a playlist
        else if (result > 0)
            done (result - 2);
    });
}


double parseDuration (const juce::String& text)
{
    const auto t = text.trim().retainCharacters ("0123456789:.");
    if (t.isEmpty())
        return -1.0;
    double seconds = 0.0;
    for (auto& part : juce::StringArray::fromTokens (t, ":", {}))
        seconds = seconds * 60.0 + part.getDoubleValue();
    return seconds;
}

juce::String timeOfDayText (int seconds)
{
    return juce::String (seconds / 3600).paddedLeft ('0', 2) + ":" + juce::String ((seconds / 60) % 60).paddedLeft ('0', 2);
}

juce::String delayText (double seconds)
{
    const int s = juce::jlimit (0, 99 * 60 + 59, (int) std::round (seconds));
    return juce::String (s / 60).paddedLeft ('0', 2) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}


// ---------------------------------------------------------------------------------------------

ScheduleStrip::ScheduleStrip (ShowController& c) : controller (c)
{
    start.setColour (juce::TextButton::buttonColourId, Look::goGreen);
    go.setColour (juce::TextButton::buttonColourId, Look::goGreen);
    start.setTooltip ("Start the countdown. Any time already past is marked missed; nothing fires late.");
    go.setTooltip ("Start the next trigger's button now");
    list.setTooltip ("The list of triggers");
    start.onClick = [this]
    {
        juce::Component::unfocusAllComponents();    // a time still being typed goes in first (it re-arms itself)
        if (controller.isScheduleRunning()) controller.stopSchedule(); else controller.startSchedule();
    };
    go.onClick = [this] { controller.scheduleGoNow(); };
    list.onClick = [this] { if (onOpenSchedule) onOpenSchedule(); };
    for (auto* b : { &start, &go, &list, &hold1, &hold2, &hold5, &hold10 })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    auto hold = [this] (juce::TextButton& b, int minutes)
    {
        b.setTooltip ("Hold: push the next trigger back " + juce::String (minutes) + " minutes. GO during a hold defeats the countdown.");
        b.onClick = [this, minutes] { controller.holdSchedule (minutes); };
    };
    hold (hold1, 1);
    hold (hold2, 2);
    hold (hold5, 5);
    hold (hold10, 10);
    startTimerHz (10);
}

void ScheduleStrip::timerCallback()
{
    const bool running = controller.isScheduleRunning(), next = controller.getNextScheduled() >= 0;
    start.setButtonText (running ? "STOP" : "START");
    start.setColour (juce::TextButton::buttonColourId, running ? Look::raised : Look::goGreen);
    go.setEnabled (next);
    for (auto* b : { &hold1, &hold2, &hold5, &hold10 })
        b->setEnabled (next);
    repaint();
}

void ScheduleStrip::paint (juce::Graphics& g)
{
    g.fillAll (Look::sidePanel);
    g.setColour (Look::line);
    g.fillRect (0, 0, 1, getHeight());
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    auto r = getLocalBounds().reduced (12, 8);
    auto head = r.removeFromTop (20);
    g.setColour (juce::Colour (0xffc9d0d8));
    g.setFont (Look::condensed (16.0f));
    g.drawText ("NEXT TRIGGER", head, juce::Justification::centredLeft);
    g.setColour (Look::textDim);
    g.setFont (Look::mono (13.0f));
    g.drawText (juce::Time::getCurrentTime().toString (false, true, true, true), head, juce::Justification::centredRight);

    const int next = controller.getNextScheduled();
    auto timeArea = r.removeFromTop (46);
    g.setFont (Look::mono (38.0f, true));
    if (next >= 0)
    {
        g.setColour (CountdownView::urgencyColour (controller.getScheduleUrgency()));
        g.drawText (durationText (std::ceil (controller.getScheduleRemaining())), timeArea, juce::Justification::centredLeft);
        const auto& item = controller.getShow().schedule[(size_t) next];
        if (item.held)
        {
            g.setColour (CountdownView::urgencyColour (ShowController::Urgency::yellow));
            g.setFont (Look::condensed (16.0f));
            g.drawText ("HOLD +" + durationText (item.heldSeconds), timeArea, juce::Justification::centredRight);
        }
    }
    else
    {
        g.setColour (Look::textFaint);
        g.drawText ("--:--", timeArea, juce::Justification::centredLeft);
    }
    auto nameRow = r.removeFromTop (18);
    g.setFont (Look::font (13.0f));
    const int missed = controller.getMissedCount();
    if (missed > 0)
    {
        g.setColour (Look::red);
        g.drawText (juce::String (missed) + " MISSED", nameRow.removeFromRight (70), juce::Justification::centredRight);
    }
    g.setColour (Look::text);
    g.drawFittedText (next >= 0 ? controller.getScheduleName (next)
                                : controller.isScheduleRunning() ? juce::String ("No more triggers") : juce::String ("Not started"),
                      nameRow, juce::Justification::centredLeft, 1, 0.8f);
}

void ScheduleStrip::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (20 + 46 + 18 + 4);
    auto holds = r.removeFromTop (24);
    const int w = (holds.getWidth() - 12) / 4;
    for (auto* b : { &hold1, &hold2, &hold5, &hold10 })
    {
        b->setBounds (holds.removeFromLeft (w));
        holds.removeFromLeft (4);
    }
    r.removeFromTop (4);
    auto row = r.removeFromTop (26);
    const int third = (row.getWidth() - 8) / 3;
    start.setBounds (row.removeFromLeft (third));
    row.removeFromLeft (4);
    go.setBounds (row.removeFromLeft (third));
    row.removeFromLeft (4);
    list.setBounds (row);
}

// ---------------------------------------------------------------------------------------------

namespace
{
    enum { whenColumn = 1, buttonColumn, statusColumn };

    /** The time or delay of a trigger. Strict: four digits, the colon goes in by itself, and a digit that can't be part of a
        real time doesn't go in at all. Left unfinished, the old time stays. */
    struct WhenCell : juce::Label
    {
        WhenCell (ShowController& c) : controller (c)
        {
            setEditable (true, true, false);
            setColour (juce::Label::textColourId, Look::text);
            onTextChange = [this]
            {
                auto& list = controller.getShow().schedule;
                if (row < 0 || row >= (int) list.size())
                    return;
                auto item = list[(size_t) row];
                const auto typed = getText();
                if (typed.length() != 5)
                {
                    show (row);   // not a whole time: back to the one that was there
                    return;
                }
                if (item.kind == ScheduleItem::clock)
                {
                    const auto now = juce::Time::getCurrentTime();
                    if (const int t = parseTimeOfDay (typed, now.getHours() * 3600 + now.getMinutes() * 60 + now.getSeconds()); t >= 0)
                        item.secondsOfDay = t;
                }
                else if (const auto d = parseDuration (typed); d >= 0.0)
                    item.delaySeconds = d;
                controller.setScheduleItem (row, item);
                show (row);
            };
        }

        juce::TextEditor* createEditorComponent() override
        {
            auto* editor = juce::Label::createEditorComponent();
            editor->setInputRestrictions (5, "0123456789");
            editor->setKeyboardType (juce::TextInputTarget::numericKeyboard);
            return editor;
        }

        void editorShown (juce::TextEditor* editor) override
        {
            const bool delay = isDelay();
            editor->setText (maskTime (getText(), delay), false);
            editor->selectAll();     // typing replaces the whole time
            editor->onTextChange = [editor, delay]
            {
                const auto masked = maskTime (editor->getText(), delay);
                if (masked != editor->getText())
                {
                    editor->setText (masked, false);
                    editor->moveCaretToEnd();
                }
            };
        }

        bool isDelay() const
        {
            const auto& list = controller.getShow().schedule;
            return row >= 0 && row < (int) list.size() && list[(size_t) row].kind == ScheduleItem::delay;
        }

        void show (int r)
        {
            row = r;
            if (isBeingEdited())
                return;   // an update from elsewhere must not throw away what's being typed
            const auto& item = controller.getShow().schedule[(size_t) r];
            setText (item.kind == ScheduleItem::clock ? timeOfDayText (item.secondsOfDay) : "+" + delayText (item.delaySeconds), juce::dontSendNotification);
            setTooltip (item.kind == ScheduleItem::clock ? "Hours and minutes, four digits: 1905 for 19:05. A morning time that has already gone by means the afternoon."
                                                         : "Minutes and seconds after START, four digits: 0130 for 1:30");
        }
        ShowController& controller;
        int row = -1;
    };

    struct ButtonCell : juce::TextButton
    {
        ButtonCell (ShowController& c) : controller (c)
        {
            setWantsKeyboardFocus (false);
            onClick = [this]
            {
                chooseButton (controller, *this, false, [this] (int id)
                {
                    auto& list = controller.getShow().schedule;
                    if (row >= 0 && row < (int) list.size())
                    {
                        auto item = list[(size_t) row];
                        item.cueId = id;
                        controller.setScheduleItem (row, item);
                    }
                });
            };
        }
        void show (int r)
        {
            row = r;
            setButtonText (controller.getScheduleName (r));
        }
        ShowController& controller;
        int row = -1;
    };
}

SchedulePanel::SchedulePanel (ShowController& c, std::function<int()> selected) : controller (c), selectedButton (std::move (selected))
{
    table.setModel (this);
    table.setRowHeight (30);
    table.setColour (juce::ListBox::backgroundColourId, Look::sidePanel);
    table.getHeader().addColumn ("When", whenColumn, 150, 80, 240, juce::TableHeaderComponent::notSortable);
    table.getHeader().addColumn ("Button", buttonColumn, 300, 120, 600, juce::TableHeaderComponent::notSortable);
    table.getHeader().addColumn ("Status", statusColumn, 170, 80, 300, juce::TableHeaderComponent::notSortable);
    table.setWantsKeyboardFocus (false);
    addAndMakeVisible (table);

    auto add = [this] (ScheduleItem::Kind kind)
    {
        controller.addScheduleItem (ScheduleItem::make (selectedButton ? selectedButton() : -1, kind, juce::Time::getCurrentTime()));
        selectLastRow();
    };
    addTime.setTooltip ("A new trigger for the selected button, at a time of day");
    addDelay.setTooltip ("A new trigger for the selected button, a set time after START");
    addTime.onClick = [add] { add (ScheduleItem::clock); };
    addDelay.onClick = [add] { add (ScheduleItem::delay); };
    remove.onClick = [this] { controller.removeScheduleItem (table.getSelectedRow()); };
    start.setTooltip ("Nothing plays until the schedule is started. Times already past are marked missed.");
    start.onClick = [this]
    {
        juce::Component::unfocusAllComponents();    // a time still being typed goes in first (it re-arms itself)
        if (controller.isScheduleRunning()) controller.stopSchedule(); else controller.startSchedule();
    };
    updateStart();
    for (auto* b : { &addTime, &addDelay, &remove, &start })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    controller.addChangeListener (this);
    setSize (680, 420);
}

SchedulePanel::~SchedulePanel()
{
    controller.removeChangeListener (this);
}

void SchedulePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateStart();
    table.updateContent();
    table.repaint();
}

void SchedulePanel::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected)
        g.fillAll (Look::raised);
    else if (row == controller.getNextScheduled())
        g.fillAll (Look::amberDark);
}

void SchedulePanel::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    const auto& list = controller.getShow().schedule;
    if (column != statusColumn || row < 0 || row >= (int) list.size())
        return;
    const auto& s = list[(size_t) row];
    juce::String text = "Waiting for START";
    auto colour = controller.isScheduleRunning() ? Look::textDim : Look::amber;
    if (controller.isScheduleRunning())
    {
        text = "Waiting";
        if (row == controller.getNextScheduled())
        {
            text = "NEXT";
            colour = Look::amber;
        }
        if (s.state == ScheduleItem::fired)    { text = "Played"; colour = Look::green; }
        if (s.state == ScheduleItem::missed)
        {
            text = controller.getAudio (s.cueId) == nullptr ? "MISSED: no audio on the button" : "MISSED";
            colour = Look::red;
        }
        if (s.state == ScheduleItem::defeated) { text = "Defeated (GO during hold)"; colour = Look::textDim; }
        if (s.state == ScheduleItem::waiting && s.held)
            text << ", hold +" << durationText (s.heldSeconds);
    }
    g.setColour (colour);
    g.setFont (Look::font (13.0f));
    g.drawFittedText (text, 6, 0, width - 8, height, juce::Justification::centredLeft, 1, 0.8f);
}

juce::Component* SchedulePanel::refreshComponentForCell (int row, int column, bool, juce::Component* existing)
{
    if (row < 0 || row >= getNumRows() || column == statusColumn)
    {
        delete existing;
        return nullptr;
    }
    if (column == whenColumn)
    {
        auto* cell = dynamic_cast<WhenCell*> (existing);
        if (cell == nullptr)
        {
            delete existing;
            cell = new WhenCell (controller);
        }
        cell->show (row);
        return cell;
    }
    auto* cell = dynamic_cast<ButtonCell*> (existing);
    if (cell == nullptr)
    {
        delete existing;
        cell = new ButtonCell (controller);
    }
    cell->show (row);
    return cell;
}

void SchedulePanel::paint (juce::Graphics& g)
{
    g.fillAll (Look::panel);
}

void SchedulePanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto buttons = r.removeFromBottom (30);
    addTime.setBounds (buttons.removeFromLeft (130));
    buttons.removeFromLeft (8);
    addDelay.setBounds (buttons.removeFromLeft (150));
    remove.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    start.setBounds (buttons.removeFromRight (110));
    r.removeFromBottom (8);
    table.setBounds (r);
}

ScheduleWindow::ScheduleWindow (ShowController& controller, std::function<int()> selected)
    : juce::DocumentWindow ("Schedule", Look::panel, juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new SchedulePanel (controller, std::move (selected)), true);
    setResizable (true, false);
    setResizeLimits (560, 260, 1600, 1200);
    centreWithSize (getWidth(), getHeight());
}

void SchedulePanel::selectLastRow()
{
    table.updateContent();
    table.selectRow (getNumRows() - 1);
}

void SchedulePanel::updateStart()
{
    const bool running = controller.isScheduleRunning();
    start.setButtonText (running ? "STOP" : "START");
    start.setColour (juce::TextButton::buttonColourId, running ? Look::raised : Look::goGreen);
}
