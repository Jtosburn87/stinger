#include "CountdownDisplay.h"
#include "CueGrid.h"
#include "Look.h"

namespace
{
    juce::String clockText (double seconds)
    {
        const int s = (int) std::ceil (std::max (0.0, seconds));
        const int h = s / 3600, m = (s / 60) % 60;
        return (h > 0 ? juce::String (h) + ":" + juce::String (m).paddedLeft ('0', 2) : juce::String (m))
             + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }

    /** A window with nothing around it, filling a monitor. */
    struct FullScreenWindow : juce::Component
    {
        FullScreenWindow (ShowController& c, CountdownView::Mode mode, juce::Rectangle<int> area, bool mainScreen, std::function<void()> closed)
            : view (c, mode), onClosed (std::move (closed))
        {
            addAndMakeVisible (view);
            setOpaque (true);
            setBounds (area);
            setAlwaysOnTop (! mainScreen);   // on the screen Stinger itself is on it must be possible to get back
            addToDesktop (0);
            setVisible (true);
        }
        void resized() override { view.setBounds (getLocalBounds()); }
        void mouseDoubleClick (const juce::MouseEvent&) override { if (onClosed) onClosed(); }   // the way out
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
        CountdownView view;
        std::function<void()> onClosed;
    };

    struct OrdinaryWindow : juce::DocumentWindow
    {
        OrdinaryWindow (ShowController& c, CountdownView::Mode mode, std::function<void()> closed)
            : juce::DocumentWindow ("Stinger countdown", juce::Colours::black, juce::DocumentWindow::allButtons), onClosed (std::move (closed))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new CountdownView (c, mode), false);
            setResizable (true, false);
            centreWithSize (960, 540);
            setVisible (true);
        }
        void closeButtonPressed() override { if (onClosed) onClosed(); }
        std::function<void()> onClosed;
    };
}

CountdownView::CountdownView (ShowController& c, Mode m) : controller (c), mode (m)
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
    startTimerHz (10);
}


juce::Colour CountdownView::urgencyColour (ShowController::Urgency u)
{
    return u == ShowController::Urgency::red ? juce::Colour (0xffff3b3b)
         : u == ShowController::Urgency::yellow ? juce::Colour (0xffffd60a)
                                                : juce::Colour (0xff35d07f);
}

void CountdownView::bigText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Colour colour)
{
    auto font = Look::mono ((float) area.getHeight(), true);
    const float width = Look::textWidth (font, text);
    if (width > (float) area.getWidth())
        font = font.withHeight (font.getHeight() * (float) area.getWidth() / width);
    g.setFont (font);
    g.setColour (colour);
    g.drawText (text, area, juce::Justification::centred);
}

void CountdownView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    auto r = getLocalBounds().reduced (getWidth() / 24, getHeight() / 16);
    const auto now = juce::Time::getCurrentTime();
    const auto clockNow = now.toString (false, true, true, true);
    const int nameHeight = r.getHeight() / 6;

    auto caption = [&] (const juce::String& text, juce::Rectangle<int> area, juce::Colour colour)
    {
        g.setColour (colour);
        g.setFont (Look::condensed ((float) area.getHeight() * 0.8f));
        g.drawFittedText (text, area, juce::Justification::centred, 1, 0.6f);
    };

    if (mode == nextCue)
    {
        const int next = controller.getNextScheduled();
        if (next < 0)
        {
            bigText (g, "--:--", r.withSizeKeepingCentre (r.getWidth(), r.getHeight() / 2), juce::Colour (0xff555555));
            caption (controller.isScheduleRunning() ? "No more cues" : "Schedule not started", r.removeFromBottom (nameHeight), juce::Colour (0xff777777));
            return;
        }
        const auto& item = controller.getShow().schedule[(size_t) next];
        caption (controller.getScheduleName (next), r.removeFromBottom (nameHeight), juce::Colours::white);
        if (item.held)
            caption ("HOLD +" + clockText (item.heldSeconds), r.removeFromTop (nameHeight), juce::Colour (0xffffd60a));
        bigText (g, clockText (controller.getScheduleRemaining()), r, urgencyColour (controller.getScheduleUrgency()));
        return;
    }

    const auto playing = controller.newestVoice();
    const auto colour = playing.looping ? Look::blue
                      : playing.left <= 10.0 ? urgencyColour (ShowController::Urgency::red)
                                             : juce::Colours::white;
    if (mode == timeOnly)
    {
        if (playing.any)
            bigText (g, playing.looping ? juce::String ("LOOP") : clockText (playing.left), r, colour);
        else
            bigText (g, "--:--", r.withSizeKeepingCentre (r.getWidth(), r.getHeight() / 2), juce::Colour (0xff555555));
        return;
    }

    if (! playing.any)
    {
        bigText (g, clockNow, r.withSizeKeepingCentre (r.getWidth(), r.getHeight() / 2), juce::Colours::white);
        return;
    }
    caption (playing.name, r.removeFromBottom (nameHeight), juce::Colours::white);
    bigText (g, playing.looping ? juce::String ("LOOP") : clockText (playing.left), r, colour);
}

// ---------------------------------------------------------------------------------------------

CountdownDisplay::CountdownDisplay (ShowController& c, juce::PropertiesFile& s) : controller (c), settings (s)
{
    enabled = settings.getBoolValue ("countdownOn", false);
    monitor = settings.getValue ("countdownMonitor");
    mode = (CountdownView::Mode) juce::jlimit (0, 2, settings.getIntValue ("countdownMode", 0));
    startTimer (1000);
    update();
}

CountdownDisplay::~CountdownDisplay()
{
    close();
}

void CountdownDisplay::close()
{
    window = nullptr;
    shownOn = "(none)";
}

void CountdownDisplay::store()
{
    settings.setValue ("countdownOn", enabled);
    settings.setValue ("countdownMonitor", monitor);
    settings.setValue ("countdownMode", (int) mode);
    settings.saveIfNeeded();
}

void CountdownDisplay::setOn (bool on)
{
    enabled = on;
    store();
    update();
}

void CountdownDisplay::setMonitor (const juce::String& key)
{
    monitor = key;
    store();
    update();
}

void CountdownDisplay::setMode (CountdownView::Mode m)
{
    mode = m;
    store();
    close();
    update();
}

juce::Array<std::pair<juce::String, juce::String>> CountdownDisplay::monitors()
{
    juce::Array<std::pair<juce::String, juce::String>> list;
    int n = 1;
    for (auto& d : juce::Desktop::getInstance().getDisplays().displays)
    {
        const auto a = d.totalArea;
        list.add ({ a.toString(), "Monitor " + juce::String (n++) + "  (" + juce::String (a.getWidth()) + " x " + juce::String (a.getHeight())
                                    + (d.isMain ? ", main screen)" : ")") });
    }
    return list;
}

void CountdownDisplay::update()
{
    if (! enabled)
    {
        close();
        return;
    }
    if (monitor.isEmpty())
    {
        if (shownOn != "window")
        {
            close();
            window = std::make_unique<OrdinaryWindow> (controller, mode, [this] { juce::MessageManager::callAsync ([this] { setOn (false); }); });
            shownOn = "window";
        }
        return;
    }
    bool plugged = false;
    for (auto& m : monitors())
        plugged = plugged || m.first == monitor;
    if (! plugged)
    {
        close();   // it comes back when the monitor does
        return;
    }
    if (shownOn != monitor)
    {
        close();
        bool mainScreen = false;
        for (auto& d : juce::Desktop::getInstance().getDisplays().displays)
            mainScreen = mainScreen || (d.totalArea.toString() == monitor && d.isMain);
        window = std::make_unique<FullScreenWindow> (controller, mode, juce::Rectangle<int>::fromString (monitor), mainScreen,
                                                     [this] { juce::MessageManager::callAsync ([this] { setOn (false); }); });
        shownOn = monitor;
    }
}

// ---------------------------------------------------------------------------------------------

CountdownPanel::CountdownPanel (CountdownDisplay& d) : display (d)
{
    on.setToggleState (display.isOn(), juce::dontSendNotification);
    on.setWantsKeyboardFocus (false);
    on.onClick = [this] { display.setOn (on.getToggleState()); };
    addAndMakeVisible (on);
    for (auto* l : { &monitorLabel, &modeLabel })
        addAndMakeVisible (l);
    for (auto* c : { &monitor, &mode })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (c);
    }
    mode.addItem ("Time left, song name, and the clock when nothing plays", 1);
    mode.addItem ("Time left only", 2);
    mode.addItem ("Countdown to the next cue (schedule)", 3);
    mode.setSelectedId ((int) display.getMode() + 1, juce::dontSendNotification);
    mode.onChange = [this] { display.setMode ((CountdownView::Mode) (mode.getSelectedId() - 1)); };
    monitor.onChange = [this]
    {
        const int i = monitor.getSelectedId() - 2;
        display.setMonitor (i >= 0 && i < listed.size() ? listed[i].first : juce::String());
    };
    fillMonitors();
    startTimer (2000);
    setSize (520, 150);
}

void CountdownPanel::fillMonitors()
{
    const auto now = CountdownDisplay::monitors();
    if (now == listed && monitor.getNumItems() > 0)
        return;
    listed = now;
    monitor.clear (juce::dontSendNotification);
    monitor.addItem ("An ordinary window on this screen", 1);
    monitor.setSelectedId (1, juce::dontSendNotification);
    bool found = display.getMonitor().isEmpty();
    for (int i = 0; i < listed.size(); ++i)
    {
        monitor.addItem (listed[i].second, i + 2);
        if (listed[i].first == display.getMonitor())
        {
            monitor.setSelectedId (i + 2, juce::dontSendNotification);
            found = true;
        }
    }
    if (! found)
    {
        monitor.addItem ("The chosen monitor (not plugged in)", 1000);
        monitor.setSelectedId (1000, juce::dontSendNotification);
    }
}

void CountdownPanel::paint (juce::Graphics& g)
{
    g.fillAll (Look::panel);
}

void CountdownPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    on.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    auto row = r.removeFromTop (30);
    monitorLabel.setBounds (row.removeFromLeft (90));
    monitor.setBounds (row);
    r.removeFromTop (8);
    row = r.removeFromTop (30);
    modeLabel.setBounds (row.removeFromLeft (90));
    mode.setBounds (row);
}
