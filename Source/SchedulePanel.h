#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Show.h"

/** Picks a button from a menu of the show's banks; calls done with its id. */
void chooseButton (ShowController& controller, juce::Component& target, bool allowNone, std::function<void (int cueId)> done);

/** "19:05", "7:05 pm", "19:05:30" -> seconds of the day, -1 if it isn't a time. */
int parseTimeOfDay (const juce::String& text);
/** "90", "5:00", "1:02:30" -> seconds, -1 if it isn't a length of time. */
double parseDuration (const juce::String& text);
juce::String timeOfDayText (int seconds);

/** Top of the right column: the countdown to the next trigger (green from Start, yellow at 1 minute, red at 10 seconds),
    hold buttons and GO. Shown when the show has a schedule. */
class ScheduleStrip : public juce::Component,
                      private juce::Timer
{
public:
    explicit ScheduleStrip (ShowController& controller);
    static constexpr int preferredHeight = 156;
    std::function<void()> onOpenSchedule;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    ShowController& controller;
    juce::TextButton start { "START" }, go { "GO NOW" }, list { "Schedule" };
    juce::TextButton hold1 { "+1" }, hold2 { "+2" }, hold5 { "+5" }, hold10 { "+10" };
};

/** File > Schedule: the triggers. */
class SchedulePanel : public juce::Component,
                      private juce::TableListBoxModel,
                      private juce::ChangeListener
{
public:
    SchedulePanel (ShowController& controller, std::function<int()> selectedButton);
    ~SchedulePanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return (int) controller.getShow().schedule.size(); }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    juce::Component* refreshComponentForCell (int row, int column, bool selected, juce::Component* existing) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    ShowController& controller;
    std::function<int()> selectedButton;
    juce::TableListBox table;
    juce::TextButton addTime { "Add at a time" }, addDelay { "Add after a delay" }, remove { "Remove" };
};

class ScheduleWindow : public juce::DocumentWindow
{
public:
    ScheduleWindow (ShowController& controller, std::function<int()> selectedButton);
    void closeButtonPressed() override { setVisible (false); }
};
