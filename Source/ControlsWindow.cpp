#include "ControlsWindow.h"
#include "Look.h"

/** One command: name, its key / MIDI, Learn (next key or MIDI message), Clear key, Clear MIDI. */
class ControlsPanel::Row : public juce::Component
{
public:
    Row (ControlSurface& s, const juce::String& commandId, const juce::String& name) : surface (s), id (commandId)
    {
        label.setText (name, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, Look::text);
        binding.setFont (Look::mono (13.0f));
        binding.setColour (juce::Label::textColourId, Look::textDim);
        learn.setClickingTogglesState (true);
        learn.setTooltip ("Then press a key, or play the MIDI note / CC / footswitch");
        learn.onClick = [this]
        {
            if (learn.getToggleState())
                surface.learn (id);
            else
                surface.cancelLearn();
        };
        clearKey.onClick = [this] { auto b = surface.getCommands().get (id); b.key.clear(); surface.setCommandBinding (id, b); };
        clearMidi.onClick = [this] { auto b = surface.getCommands().get (id); b.midi = {}; surface.setCommandBinding (id, b); };
        clearKey.setTooltip ("Remove the key");
        clearMidi.setTooltip ("Remove the MIDI");
        for (auto* c : std::initializer_list<juce::Component*> { &label, &binding, &learn, &clearKey, &clearMidi })
        {
            c->setWantsKeyboardFocus (false);
            addAndMakeVisible (c);
        }
        refresh();
    }

    void refresh()
    {
        const bool learning = surface.learning() == id;
        learn.setToggleState (learning, juce::dontSendNotification);
        const auto b = surface.getCommands().get (id);
        binding.setText (learning ? juce::String ("Press a key or send MIDI...")
                                  : (ControlSurface::describe (b).isNotEmpty() ? ControlSurface::describe (b) : juce::String ("-")),
                         juce::dontSendNotification);
        binding.setColour (juce::Label::textColourId, learning ? Look::amber : Look::textDim);
        clearKey.setEnabled (b.key.isNotEmpty());
        clearMidi.setEnabled (b.midi.isSet());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 3);
        clearMidi.setBounds (r.removeFromRight (78));
        r.removeFromRight (4);
        clearKey.setBounds (r.removeFromRight (72));
        r.removeFromRight (4);
        learn.setBounds (r.removeFromRight (64));
        r.removeFromRight (10);
        label.setBounds (r.removeFromLeft (250));
        binding.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Look::line);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

private:
    ControlSurface& surface;
    juce::String id;
    juce::Label label, binding;
    juce::TextButton learn { "Learn" }, clearKey { "Clear key" }, clearMidi { "Clear MIDI" };
};

// ---------------------------------------------------------------------------------------------

ControlsPanel::ControlsPanel (ControlSurface& s, StreamDeckServer* deck) : surface (s), streamDeck (deck)
{
    global.setWantsKeyboardFocus (false);
    global.onClick = [this] { surface.setGlobal (global.getToggleState()); };
    addAndMakeVisible (global);

    for (auto* l : { &refused, &inputs, &lastMidi, &deckStatus })
    {
        l->setColour (juce::Label::textColourId, Look::textDim);
        l->setFont (Look::font (13.0f));
        addAndMakeVisible (l);
    }
    refused.setColour (juce::Label::textColourId, juce::Colour (0xffe5484d));

    feedbackLabel.setColour (juce::Label::textColourId, Look::textDim);
    addAndMakeVisible (feedbackLabel);
    feedbackOut.setWantsKeyboardFocus (false);
    feedbackOut.onChange = [this]
    {
        const int i = feedbackOut.getSelectedId() - 2;
        const auto outs = surface.getOutputs();
        surface.setFeedbackOutput (i >= 0 && i < outs.size() ? outs[i].identifier : juce::String());
    };
    addAndMakeVisible (feedbackOut);

    for (auto* sl : { &playing, &next, &loaded })
    {
        sl->setSliderStyle (juce::Slider::LinearBar);
        sl->setRange (0, 127, 1);
        sl->setWantsKeyboardFocus (false);
        sl->setTooltip ("Velocity / value sent to a pad in this state (what colour it lights depends on the controller)");
        sl->onValueChange = [this]
        {
            ControlSurface::FeedbackLevels l;
            l.playing = (int) playing.getValue();
            l.next = (int) next.getValue();
            l.loaded = (int) loaded.getValue();
            surface.setFeedbackLevels (l);
        };
        addAndMakeVisible (sl);
    }
    for (auto* l : { &playingLabel, &nextLabel, &loadedLabel })
    {
        l->setColour (juce::Label::textColourId, Look::textDim);
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }

    reset.setWantsKeyboardFocus (false);
    reset.onClick = [this]
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon,
                                          "Reset controls", "Set every command's key and MIDI back to the defaults?", "Reset", "Cancel", this),
                                      [this] (int r) { if (r == 1) surface.resetCommands(); });
    };
    addAndMakeVisible (reset);

    for (auto& c : CommandMap::commands())
        list.addAndMakeVisible (rows.add (new Row (surface, c.id, c.name)));
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    surface.addChangeListener (this);
    refresh();
    setWantsKeyboardFocus (true);
    startTimerHz (4);
    setSize (760, 720);
}

ControlsPanel::~ControlsPanel()
{
    surface.removeChangeListener (this);
    if (surface.learning().isNotEmpty() && ! surface.learning().startsWith ("cue:"))
        surface.cancelLearn();
}

void ControlsPanel::refresh()
{
    global.setToggleState (surface.isGlobal(), juce::dontSendNotification);
    juce::StringArray refusedNames;
    for (auto& k : surface.getRefusedKeys())
        refusedNames.add (k.getTextDescriptionWithIcons());
    refused.setText (refusedNames.isEmpty() ? juce::String()
                                            : "Windows wouldn't give Stinger these keys (another app has them): " + refusedNames.joinIntoString (", "),
                     juce::dontSendNotification);

    const auto names = surface.getInputNames();
    inputs.setText (names.isEmpty() ? juce::String ("MIDI in: no devices found (pair Bluetooth MIDI in Windows Settings first)")
                                    : "MIDI in, listening to all: " + names.joinIntoString (", "),
                    juce::dontSendNotification);

    feedbackOut.clear (juce::dontSendNotification);
    feedbackOut.addItem ("None", 1);
    const auto outs = surface.getOutputs();
    for (int i = 0; i < outs.size(); ++i)
    {
        feedbackOut.addItem (outs[i].name, i + 2);
        if (outs[i].identifier == surface.getFeedbackOutput())
            feedbackOut.setSelectedId (i + 2, juce::dontSendNotification);
    }
    if (feedbackOut.getSelectedId() == 0)
        feedbackOut.setSelectedId (1, juce::dontSendNotification);
    const auto l = surface.getFeedbackLevels();
    playing.setValue (l.playing, juce::dontSendNotification);
    next.setValue (l.next, juce::dontSendNotification);
    loaded.setValue (l.loaded, juce::dontSendNotification);

    for (auto* r : rows)
        r->refresh();
}

void ControlsPanel::timerCallback()
{
    const auto text = surface.getLastMidi();
    lastMidi.setText (text.isNotEmpty() ? "Last MIDI: " + text : juce::String ("Last MIDI: nothing yet"), juce::dontSendNotification);
    if (streamDeck != nullptr)
    {
        const int n = streamDeck->numClients();
        deckStatus.setText (! streamDeck->isListening() ? juce::String ("Stream Deck: not available (another Stinger has the connection)")
                            : n > 0 ? "Stream Deck: plugin connected"
                                    : juce::String ("Stream Deck: waiting for the Stinger plugin (double-click Stinger.streamDeckPlugin to install it)"),
                            juce::dontSendNotification);
    }
}

void ControlsPanel::paint (juce::Graphics& g)
{
    g.fillAll (Look::panel);
    g.setColour (juce::Colour (0xffc9d0d8));
    g.setFont (Look::condensed (16.0f));
    g.drawText ("COMMANDS", 16, viewport.getY() - 26, 200, 22, juce::Justification::centredLeft);
}

void ControlsPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    global.setBounds (r.removeFromTop (26));
    refused.setBounds (r.removeFromTop (refused.getText().isEmpty() ? 0 : 22));
    r.removeFromTop (6);
    inputs.setBounds (r.removeFromTop (22));
    lastMidi.setBounds (r.removeFromTop (22));
    deckStatus.setBounds (r.removeFromTop (22));
    r.removeFromTop (6);
    auto fb = r.removeFromTop (28);
    feedbackLabel.setBounds (fb.removeFromLeft (120));
    feedbackOut.setBounds (fb.removeFromLeft (230));
    for (auto [label, slider] : { std::pair { &playingLabel, &playing }, std::pair { &nextLabel, &next }, std::pair { &loadedLabel, &loaded } })
    {
        label->setBounds (fb.removeFromLeft (64));
        fb.removeFromLeft (4);
        slider->setBounds (fb.removeFromLeft (52));
    }
    auto bottom = r.removeFromBottom (30);
    reset.setBounds (bottom.removeFromRight (170));
    r.removeFromBottom (8);
    r.removeFromTop (34);
    viewport.setBounds (r);
    const int rowH = 32;
    list.setSize (viewport.getMaximumVisibleWidth(), rows.size() * rowH);
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowH, list.getWidth() - 4, rowH);
}

ControlsWindow::ControlsWindow (ControlSurface& surface, StreamDeckServer* streamDeck)
    : DocumentWindow ("Controls", Look::panel, DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new ControlsPanel (surface, streamDeck), true);
    setResizable (true, false);
    setResizeLimits (700, 420, 2000, 2000);
    centreWithSize (getWidth(), getHeight());
}
