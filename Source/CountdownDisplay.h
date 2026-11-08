#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Show.h"

/** What the big countdown shows, full size. */
class CountdownView : public juce::Component,
                      private juce::Timer
{
public:
    enum Mode { timeNameClock = 0, timeOnly, nextCue };

    CountdownView (ShowController& controller, Mode mode);
    void setMode (Mode m) { mode = m; repaint(); }
    void paint (juce::Graphics&) override;

    /** The colour of a countdown at this many seconds left: red in the last 10 s, yellow in the last minute. */
    static juce::Colour urgencyColour (ShowController::Urgency u);

private:
    void timerCallback() override { repaint(); }
    void bigText (juce::Graphics&, const juce::String& text, juce::Rectangle<int> area, juce::Colour colour);

    ShowController& controller;
    Mode mode;
};

/** The countdown window: full screen on the chosen monitor, or an ordinary window. If that monitor is unplugged it closes
    quietly, and it comes back when the monitor does. */
class CountdownDisplay : private juce::Timer
{
public:
    CountdownDisplay (ShowController& controller, juce::PropertiesFile& settings);
    ~CountdownDisplay() override;

    void setOn (bool on);
    bool isOn() const { return enabled; }
    /** "" = an ordinary window, else the monitor's area ("x y w h"). */
    void setMonitor (const juce::String& key);
    juce::String getMonitor() const { return monitor; }
    void setMode (CountdownView::Mode m);
    CountdownView::Mode getMode() const { return mode; }

    /** The monitors plugged in now: key, name. */
    static juce::Array<std::pair<juce::String, juce::String>> monitors();

private:
    void timerCallback() override { update(); }
    void update();
    void close();
    void store();

    ShowController& controller;
    juce::PropertiesFile& settings;
    bool enabled = false;
    juce::String monitor;
    CountdownView::Mode mode = CountdownView::timeNameClock;
    std::unique_ptr<juce::Component> window;
    juce::String shownOn = "(none)";
};

/** File > Countdown display. */
class CountdownPanel : public juce::Component,
                       private juce::Timer
{
public:
    explicit CountdownPanel (CountdownDisplay& display);
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override { fillMonitors(); }
    void fillMonitors();

    CountdownDisplay& display;
    juce::ToggleButton on { "Show the countdown" };
    juce::Label monitorLabel { {}, "Show it on" }, modeLabel { {}, "Show" };
    juce::ComboBox monitor, mode;
    juce::Array<std::pair<juce::String, juce::String>> listed;
};

/** Large, at the top of the left column: the time of day or a countdown. Right-click to choose, and for 12 or 24 hours.
    A countdown with nothing to count shows the time of day, labelled. */
class BigClock : public juce::Component,
                 public juce::SettableTooltipClient,
                 private juce::Timer
{
public:
    enum Mode { timeOfDay = 0, songLeft, nextTrigger };

    BigClock (ShowController& controller, juce::PropertiesFile& settings);
    static constexpr int preferredHeight = 72;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    static juce::String timeText (juce::Time t, bool twentyFour);

private:
    struct Face
    {
        juce::String time, caption;
        juce::Colour colour;
        bool counting = false;
    };
    Face currentFace() const;
    void timerCallback() override;

    ShowController& controller;
    juce::PropertiesFile& settings;
    Mode mode = timeOfDay;
    bool use24Hour = true;
    juce::String shown;
};
