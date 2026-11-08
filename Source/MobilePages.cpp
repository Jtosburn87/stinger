#include "MobilePages.h"
#include "Look.h"
#include "AndroidFiles.h"

namespace
{
    constexpr int barHeight = 52, stripHeight = 46, tabHeight = 40, rowHeight = 46, wideFrom = 700;
    constexpr int minTransportHeight = 130;   // GO, STOP and FADE: must be hit first time, without looking
    constexpr int gestureBarHeight = 28;   // the system's swipe handle stays on screen even in full screen

    /** Hold or tap: the same question either way. A dialog rather than a menu: a menu opened under a held finger
        takes the finger coming off as picking the item under it. */
    void askToClear (ShowController& controller, int id)
    {
        auto* window = new juce::AlertWindow ("Clear " + controller.getShow().cue (id).name + "?",
                                              "Its edits and effects go too, and it comes off any playlists.\nThe audio file isn't deleted.", juce::MessageBoxIconType::NoIcon);
        window->addButton ("Clear button", 1);
        window->addButton ("Cancel", 0);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([&controller, id] (int result)
        {
            if (result == 1)
                controller.clearCue (id);
        }), true);
    }

    const juce::String collapsedArrow = juce::String::fromUTF8 ("\xe2\x96\xb8  "), openArrow = juce::String::fromUTF8 ("\xe2\x96\xbe  ");

    void styleHeading (juce::Label& l, juce::Colour colour = Look::textDim)
    {
        l.setFont (Look::condensed (17.0f));
        l.setColour (juce::Label::textColourId, colour);
    }

    void flat (juce::TextButton& b)
    {
        b.setWantsKeyboardFocus (false);
        b.setConnectedEdges (0);
    }
}

// ---------------------------------------------------------------------------------------------

MobileRow::MobileRow (const juce::String& name)
{
    label.setText (name, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, Look::textDim);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, true, 78, 30);
    slider.setViewportIgnoreDragFlag (true);   // dragging a slider moves the slider, not the page
    slider.setScrollWheelEnabled (false);
    addAndMakeVisible (label);
    addAndMakeVisible (slider);
}

void MobileRow::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromLeft (92));
    slider.setBounds (r);
}

void MobileReductionMeter::setDb (float db)
{
    const auto now = juce::Time::getMillisecondCounter();
    const bool holdOver = now - heldAt > 2000;
    if (db >= held || holdOver)
    {
        held = db;
        heldAt = now;
    }
    if (std::abs (db - shown) > 0.05f || holdOver)
    {
        shown = db;
        repaint();
    }
}

void MobileReductionMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.0f, 8.0f);
    auto text = r.removeFromRight (78.0f);
    g.setColour (Look::raised);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (Look::amber);
    g.fillRoundedRectangle (r.withLeft (r.getRight() - r.getWidth() * juce::jmin (shown / 24.0f, 1.0f)), 3.0f);
    if (held > 0.05f)
    {
        g.setColour (Look::text);
        g.fillRect (r.getRight() - r.getWidth() * juce::jmin (held / 24.0f, 1.0f), r.getY(), 2.0f, r.getHeight());
    }
    g.setColour (Look::textDim);
    g.setFont (Look::mono (12.0f));
    g.drawText ((shown >= 0.05f ? "-" : "") + juce::String (shown, 1) + " dB", text, juce::Justification::centredRight);
}

// ---------------------------------------------------------------------------------------------

MobileEditorPage::MobileEditorPage (ShowController& c) : controller (c), waveform (c)
{
    flat (backButton);
    backButton.onClick = [this] { if (onBack) onBack(); };
    addAndMakeVisible (backButton);
    title.setFont (Look::condensed (20.0f));
    title.setColour (juce::Label::textColourId, Look::text);
    addAndMakeVisible (title);
    waveform.setTouchLayout (true);
    addAndMakeVisible (waveform);

    viewport.setViewedComponent (&settings, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible (viewport);

    auto add = [this] (juce::Component& child) { settings.addAndMakeVisible (child); };

    buttonHeading.setText ("BUTTON", juce::dontSendNotification);
    styleHeading (buttonHeading, Look::amber);
    effectsHeading.setText ("EFFECTS", juce::dontSendNotification);
    styleHeading (effectsHeading, Look::amber);
    eqHeading.setText ("EQ", juce::dontSendNotification);
    compHeading.setText ("Compressor", juce::dontSendNotification);
    limiterHeading.setText ("Limiter", juce::dontSendNotification);
    for (auto* l : { &eqHeading, &compHeading, &limiterHeading })
        styleHeading (*l, Look::text);
    fileInfo.setColour (juce::Label::textColourId, Look::textDim);
    fileInfo.setFont (Look::font (13.0f));
    for (auto* l : { &buttonHeading, &effectsHeading, &eqHeading, &compHeading, &limiterHeading, &fileInfo })
        add (*l);

    flat (chooseButton);
    chooseButton.onClick = [this] { chooseAudio (false); };
    add (chooseButton);
    flat (colourButton);
    colourButton.onClick = [this] { Look::showColourMenu (colourButton, controller, cueId); };
    add (colourButton);
    flat (clearButton);
    clearButton.onClick = [this] { askToClear (controller, cueId); };
    add (clearButton);

    name.setTextToShowWhenEmpty ("Name", Look::textFaint);
    name.setFont (Look::font (16.0f));
    name.onTextChange = [this] { if (auto* q = cue()) { q->name = name.getText(); changed(); } };
    add (name);

    output.onChange = [this]
    {
        if (auto* q = cue(); q != nullptr && output.getSelectedId() > 0)
        {
            const int id = output.getSelectedId();
            q->output = { id % 1000, id < 2000 };
            changed();
        }
    };
    add (output);

    auto dbText = [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; };
    level.slider.setRange (Cue::minLevelDb, Cue::maxLevelDb, 0.5);
    level.slider.textFromValueFunction = dbText;
    level.slider.onValueChange = [this] { if (auto* q = cue()) { q->levelDb = (float) level.slider.getValue(); changed(); } };
    add (level);

    pan.slider.setRange (-1.0, 1.0, 0.01);
    pan.slider.onValueChange = [this] { if (auto* q = cue()) { q->pan = (float) pan.slider.getValue(); changed(); } };
    add (pan);

    auto setupFade = [this, &add] (MobileRow& row, Fade Cue::* member)
    {
        row.slider.setRange (0.0, 30.0, 0.1);
        row.slider.setSkewFactorFromMidPoint (3.0);
        row.slider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " s"; };
        row.slider.onValueChange = [this, &row, member]
        {
            if (auto* q = cue())
            {
                (q->*member).seconds = row.slider.getValue();
                changed();
            }
        };
        add (row);
    };
    setupFade (fadeIn, &Cue::fadeIn);
    setupFade (fadeOut, &Cue::fadeOut);
    setupFade (stopFade, &Cue::stopFade);

    loop.onClick = [this] { if (auto* q = cue()) { q->loop = loop.getToggleState(); changed(); } };
    fadeOthers.onClick = [this] { if (auto* q = cue()) { q->fadeOthers = fadeOthers.getToggleState(); changed(); } };
    add (loop);
    add (fadeOthers);

    // ---- effects ----
    auto setupEffect = [this, &add] (EffectSection& section, StockFx::Kind kind)
    {
        section.kind = kind;
        const auto sample = StockFx::create (kind);
        auto& spec = dynamic_cast<StockFx&> (*sample);
        section.power.setButtonText ("On");
        section.power.onClick = [this, &section] { if (! updating) setEffectOn (section, section.power.getToggleState()); };
        add (section.power);
        for (int i = 0; i < spec.numParams(); ++i)
        {
            const auto p = spec.param (i);
            if (p.onOff)
            {
                auto toggle = std::make_unique<juce::ToggleButton> (p.name);
                toggle->onClick = [this, &section, i]
                {
                    if (auto* fx = effect (section.kind); fx != nullptr && ! updating)
                    {
                        fx->set (i, section.switches[i]->getToggleState() ? 1.0f : 0.0f);
                        changed();
                    }
                };
                add (*toggle);
                section.switches[i] = std::move (toggle);
                section.rows.add (nullptr);   // keeps the rows numbered by setting
                continue;
            }
            auto* row = section.rows.add (new MobileRow (p.name));
            auto& s = row->slider;
            s.setRange (p.minimum, p.maximum, p.step);
            if (p.logarithmic)
                s.setSkewFactorFromMidPoint (std::sqrt ((double) p.minimum * p.maximum));
            const juce::String unit (p.unit);
            const bool lowCut = kind == StockFx::Kind::eq && i == StockFx::eqLowCut;
            const bool fader = kind == StockFx::Kind::limiter;
            const float bottom = p.minimum;
            s.textFromValueFunction = [unit, lowCut, bottom] (double v)
            {
                if (lowCut && v <= bottom + 0.5)
                    return juce::String ("Off");
                if (unit.isEmpty())
                    return juce::String (v, 2);
                return juce::String (v, v < 10.0 && unit != "Hz" ? 1 : 0) + (unit == ":1" ? unit : " " + unit);
            };
            s.onValueChange = [this, &section, i, row, fader]
            {
                if (updating)
                    return;
                if (auto* fx = effect (section.kind))
                {
                    if (fader)
                        fx->moveLimiterFader (i, (float) row->slider.getValue());
                    else
                        fx->set (i, (float) row->slider.getValue());
                    changed();
                }
            };
            add (*row);
        }
        add (section.meter);
    };
    setupEffect (eq, StockFx::Kind::eq);
    setupEffect (compressor, StockFx::Kind::compressor);
    setupEffect (limiter, StockFx::Kind::limiter);
    eq.meter.setVisible (false);   // an EQ doesn't turn anything down

    controller.addChangeListener (this);
}

MobileEditorPage::~MobileEditorPage()
{
    controller.removeChangeListener (this);
}

Cue* MobileEditorPage::cue()
{
    return controller.getShow().isValid (cueId) ? &controller.getShow().cue (cueId) : nullptr;
}

void MobileEditorPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (! isShowing())
        return;
    waveform.setCue (cueId);   // same button: its In and Out fields follow
    refresh();
}

void MobileEditorPage::changed()
{
    if (! updating)
        controller.cueChanged (cueId);
}

void MobileEditorPage::setCue (int id)
{
    cueId = id;
    waveform.setCue (id);
    viewport.setViewPosition (0, 0);
    refresh();
    startTimerHz (20);
}

int MobileEditorPage::slotOf (StockFx::Kind kind) const
{
    if (auto* chain = controller.getFx (cueId))
        for (int s = 0; s < CueFx::numSlots; ++s)
            if (auto* stock = dynamic_cast<StockFx*> (chain->getPlugin (s)); stock != nullptr && stock->getKind() == kind)
                return s;
    return -1;
}

StockFx* MobileEditorPage::effect (StockFx::Kind kind) const
{
    const int slot = slotOf (kind);
    return slot >= 0 ? dynamic_cast<StockFx*> (controller.getFx (cueId)->getPlugin (slot)) : nullptr;
}

void MobileEditorPage::setEffectOn (EffectSection& section, bool on)
{
    if (const int slot = slotOf (section.kind); slot >= 0)
    {
        controller.setPluginBypassed (cueId, slot, ! on);
        return;
    }
    if (! on || cue() == nullptr)
        return;

    // always EQ, compressor, limiter, whichever is turned on first; then whatever is free
    const auto& plugins = cue()->plugins;
    int slot = (int) section.kind;
    if (! plugins[(size_t) slot].isEmpty())
    {
        slot = -1;
        for (int s = 0; s < CueFx::numSlots; ++s)
            if (plugins[(size_t) s].isEmpty())
            {
                slot = s;
                break;
            }
    }
    juce::String error;
    if (slot < 0 || ! controller.setPlugin (cueId, slot, StockFx::describe (section.kind), error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Effects",
                                                slot < 0 ? "All this button's effect slots are in use by plugins from a computer." : error);
        refresh();
    }
}

void MobileEditorPage::refreshEffect (EffectSection& section)
{
    const int slot = slotOf (section.kind);
    auto* fx = effect (section.kind);
    const bool on = fx != nullptr && ! controller.getFx (cueId)->isBypassed (slot);
    section.power.setToggleState (on, juce::dontSendNotification);
    for (auto& [i, toggle] : section.switches)
    {
        toggle->setEnabled (fx != nullptr);
        toggle->setToggleState (fx != nullptr && fx->get (i) >= 0.5f, juce::dontSendNotification);
    }
    for (int i = 0; i < section.rows.size(); ++i)
    {
        if (section.rows[i] == nullptr)
            continue;
        auto& row = *section.rows[i];
        row.setEnabled (fx != nullptr);
        if (fx != nullptr)
            row.slider.setValue (fx->get (i), juce::dontSendNotification);
        else
        {
            const auto sample = StockFx::create (section.kind);
            row.slider.setValue (dynamic_cast<StockFx&> (*sample).param (i).initial, juce::dontSendNotification);
        }
        row.slider.updateText();
    }
}

void MobileEditorPage::refresh()
{
    const juce::ScopedValueSetter<bool> busy (updating, true);
    fillOutputs();
    const auto* q = cue();
    const bool has = q != nullptr && ! q->isEmpty();

    title.setText (has ? q->name : juce::String ("Empty button"), juce::dontSendNotification);
    chooseButton.setButtonText (has ? "Replace audio" : "Choose audio");
    for (auto* c : std::initializer_list<juce::Component*> { &name, &colourButton, &clearButton, &output, &level, &pan, &fadeIn, &fadeOut, &stopFade, &loop, &fadeOthers,
                                                            &eq.power, &compressor.power, &limiter.power })
        c->setEnabled (has);

    if (q != nullptr)
    {
        juce::String details = has ? q->file.getFileName() : juce::String ("No audio on this button yet.");
        if (has)
            if (auto audio = controller.getAudio (cueId))
                details << juce::String::fromUTF8 (" \xc2\xb7 ") << CueGrid::formatTime (audio->seconds());
        if (const auto error = controller.getError (cueId); error.isNotEmpty())
            details = error;
        fileInfo.setText (details, juce::dontSendNotification);

        if (! name.hasKeyboardFocus (true))
            name.setText (q->name, false);
        output.setSelectedId ((q->output.stereo ? 1000 : 2000) + q->output.firstChannel, juce::dontSendNotification);
        colourButton.setColour (juce::TextButton::buttonColourId, Look::baseColourOf (*q));
        colourButton.setColour (juce::TextButton::textColourOffId, Look::textOn (Look::baseColourOf (*q)));
        level.slider.setValue (q->levelDb, juce::dontSendNotification);
        pan.slider.setValue (q->pan, juce::dontSendNotification);
        fadeIn.slider.setValue (q->fadeIn.seconds, juce::dontSendNotification);
        fadeOut.slider.setValue (q->fadeOut.seconds, juce::dontSendNotification);
        stopFade.slider.setValue (q->stopFade.seconds, juce::dontSendNotification);
        loop.setToggleState (q->loop, juce::dontSendNotification);
        fadeOthers.setToggleState (q->fadeOthers, juce::dontSendNotification);
        for (auto* row : { &level, &pan, &fadeIn, &fadeOut, &stopFade })
            row->slider.updateText();
    }

    refreshEffect (eq);
    refreshEffect (compressor);
    refreshEffect (limiter);
}

void MobileEditorPage::fillOutputs()
{
    const int count = std::max (2, controller.getEngine().getOutputNames().size());
    if (output.getNumItems() == count + count / 2)
        return;
    output.clear (juce::dontSendNotification);
    for (int ch = 0; ch + 1 < count; ch += 2)
        output.addItem (CueGrid::outputLabel ({ ch, true }), 1000 + ch);
    for (int ch = 0; ch < count; ++ch)
        output.addItem (CueGrid::outputLabel ({ ch, false }), 2000 + ch);
}

void MobileEditorPage::chooseAudio (bool backIfCancelled)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose an audio file", juce::File(), controller.getLoader().wildcards());
    juce::Component::SafePointer<MobileEditorPage> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, backIfCancelled] (const juce::FileChooser& fc)
    {
        if (safe == nullptr)
            return;
        if (! fc.getURLResult().isEmpty())
            safe->importAudio (fc.getURLResult());
        else if (backIfCancelled && safe->onBack)
            safe->onBack();
    });
}

/** The system picker hands over a content link, not a file: its bytes are copied into the show's Audio folder
    (off the message thread, a song can be big), and that copy goes on the button. */
void MobileEditorPage::importAudio (const juce::URL& url)
{
    const int id = cueId;
    fileInfo.setText ("Copying the song...", juce::dontSendNotification);
    juce::Component::SafePointer<MobileEditorPage> safe (this);
    AndroidFiles::copyIn ({ url }, controller.getAudioFolder(), [safe, id] (const juce::Array<juce::File>& copies)
    {
        if (safe == nullptr)
            return;
        if (copies[0] != juce::File())
            safe->controller.assignFile (id, copies[0]);
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Choose audio", "Couldn't copy that file.");
        safe->refresh();
    });
}

void MobileEditorPage::timerCallback()
{
    if (! isShowing())
    {
        stopTimer();
        return;
    }
    waveform.tick();
    const bool playing = controller.getEngine().isCuePlaying (cueId);
    for (auto* section : { &compressor, &limiter })
    {
        auto* fx = effect (section->kind);
        section->meter.setDb (fx != nullptr && playing ? fx->gainReductionDb() : 0.0f);
    }
}

void MobileEditorPage::paint (juce::Graphics& g)
{
    g.fillAll (Look::ground);
    g.setColour (Look::panel);
    g.fillRect (getLocalBounds().removeFromTop (barHeight));
}

void MobileEditorPage::layoutSettings()
{
    const int width = viewport.getMaximumVisibleWidth();
    int y = 8;
    auto place = [&] (juce::Component& c, int height) { c.setBounds (12, y, width - 24, height); y += height + 4; };
    auto heading = [&] (juce::Label& l) { y += 10; place (l, 28); };
    auto headingWithPower = [&] (juce::Label& l, juce::ToggleButton& power)
    {
        y += 8;
        l.setBounds (12, y, width - 110, 34);
        power.setBounds (width - 92, y, 80, 34);
        y += 38;
    };

    place (buttonHeading, 28);
    place (fileInfo, 24);
    {
        auto row = juce::Rectangle<int> (12, y, width - 24, 42);
        const int third = (row.getWidth() - 16) / 3;
        chooseButton.setBounds (row.removeFromLeft (third));
        row.removeFromLeft (8);
        colourButton.setBounds (row.removeFromLeft (third));
        row.removeFromLeft (8);
        clearButton.setBounds (row);
        y += 46;
    }
    place (name, 42);
    place (output, 42);
    for (auto* row : { &level, &pan, &fadeIn, &fadeOut, &stopFade })
        place (*row, rowHeight);
    {
        auto row = juce::Rectangle<int> (12, y, width - 24, 40);
        loop.setBounds (row.removeFromLeft (90));
        fadeOthers.setBounds (row);
        y += 44;
    }

    heading (effectsHeading);
    auto rowsOf = [&] (EffectSection& section)
    {
        for (auto* row : section.rows)
            if (row != nullptr)
                place (*row, rowHeight);
        if (! section.switches.empty())
        {
            auto line = juce::Rectangle<int> (12, y, width - 24, 40);
            for (auto& [i, toggle] : section.switches)
                toggle->setBounds (line.removeFromLeft (110));
            y += 44;
        }
    };
    headingWithPower (eqHeading, eq.power);
    rowsOf (eq);
    headingWithPower (compHeading, compressor.power);
    rowsOf (compressor);
    place (compressor.meter, 28);
    headingWithPower (limiterHeading, limiter.power);
    rowsOf (limiter);
    place (limiter.meter, 28);

    settings.setSize (width, y + 40);   // room to scroll the last row clear of the system's swipe handle
}

void MobileEditorPage::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (barHeight).reduced (8, 7);
    backButton.setBounds (bar.removeFromLeft (84));
    bar.removeFromLeft (10);
    title.setBounds (bar);

    if (getWidth() >= wideFrom)
    {
        viewport.setBounds (r.removeFromRight (std::max (360, getWidth() * 2 / 5)));
        r.removeFromBottom (gestureBarHeight);
        waveform.setBounds (r.reduced (8));
    }
    else
    {
        waveform.setBounds (r.removeFromTop (std::min (440, r.getHeight() * 3 / 5)).reduced (8, 6));
        viewport.setBounds (r);
    }
    layoutSettings();
}

// ---------------------------------------------------------------------------------------------

MobileAudioPage::MobileAudioPage (ShowController& controller)
    : selector (controller.getEngine().getDeviceManager(), 0, 0, 2, PlaybackEngine::maxOutputs, false, false, true, false)
{
    flat (backButton);
    backButton.onClick = [this] { if (onBack) onBack(); };
    addAndMakeVisible (backButton);
    title.setText ("Audio", juce::dontSendNotification);
    title.setFont (Look::condensed (20.0f));
    addAndMakeVisible (title);
    addAndMakeVisible (selector);
}

void MobileAudioPage::paint (juce::Graphics& g)
{
    g.fillAll (Look::ground);
    g.setColour (Look::panel);
    g.fillRect (getLocalBounds().removeFromTop (barHeight));
}

void MobileAudioPage::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (barHeight).reduced (8, 7);
    backButton.setBounds (bar.removeFromLeft (84));
    bar.removeFromLeft (10);
    title.setBounds (bar);
    selector.setBounds (r.reduced (8));
}

// ---------------------------------------------------------------------------------------------

MobileMainPage::MobileMainPage (ShowController& c, juce::PropertiesFile& s)
    : controller (c), settings (s), playlist (c), grid (c), nowPlaying (c)
{
    // the song buttons get the room: Playlist and Now Playing start folded
    playlistOpen = settings.getBoolValue ("playlistOpen", false);
    nowOpen = settings.getBoolValue ("nowPlayingOpen", false);
    buttonCount = settings.getIntValue ("buttonsPerBank", 12);
    if (buttonCount != 8 && buttonCount != 16)
        buttonCount = 12;
    controller.setButtonsPerBank (buttonCount);

    flat (menuButton);
    menuButton.onClick = [this] { showMenu(); };
    addAndMakeVisible (menuButton);
    flat (editButton);
    editButton.setClickingTogglesState (true);
    editButton.setColour (juce::TextButton::buttonOnColourId, Look::amber);
    editButton.setColour (juce::TextButton::textColourOnId, Look::ground);
    editButton.onClick = [this]
    {
        grid.setEditMode (editButton.getToggleState());
        resized();   // Playlist opens while editing: it's where buttons get dragged to
        refresh();
    };
    addAndMakeVisible (editButton);
    showName.setFont (Look::condensed (20.0f));
    showName.setColour (juce::Label::textColourId, Look::text);
    addAndMakeVisible (showName);

    for (auto* b : { &playlistHeader, &nowHeader, &goButton, &fadeAllButton, &stopAllButton })
    {
        flat (*b);
        addAndMakeVisible (*b);
    }
    for (auto* b : { &goButton, &fadeAllButton, &stopAllButton })
        b->getProperties().set ("huge", true);
    nowPlaying.setAllButtonsShown (false);
    playlistHeader.onClick = [this] { setOpen (true, ! playlistOpen); };
    nowHeader.onClick = [this] { setOpen (false, ! nowOpen); };
    goButton.setColour (juce::TextButton::buttonColourId, Look::goGreen);
    goButton.onClick = [this] { controller.go (controller.getSelectedPlaylist()); };
    fadeAllButton.onClick = [this] { controller.getEngine().stopAll (true); };
    stopAllButton.setColour (juce::TextButton::buttonColourId, Look::red);
    stopAllButton.onClick = [this] { controller.getEngine().stopAll (false); };

    playlist.getBank = [this] { return bank; };
    addChildComponent (playlist);
    addChildComponent (nowPlaying);

    grid.setShape (Show::rows, buttonCount, false);
    grid.onTapInEditMode = [this] (int id)
    {
        if (onOpenEditor)
            onOpenEditor (id);
    };
    grid.onHoldInEditMode = [this] (int id) { askToClear (controller, id); };
    grid.onChooseFile = [this] (int id)
    {
        if (onChooseSong)
            onChooseSong (id);
    };
    addAndMakeVisible (grid);

    rebuildBanks();
    controller.addChangeListener (this);
    startTimerHz (4);
    refresh();
}

MobileMainPage::~MobileMainPage()
{
    controller.removeChangeListener (this);
}

void MobileMainPage::rebuildBanks()
{
    bankButtons.clear();
    const auto& banks = controller.getShow().banks;
    bank = juce::jlimit (0, (int) banks.size() - 1, bank);
    for (int i = 0; i < (int) banks.size(); ++i)
    {
        auto* b = bankButtons.add (new juce::TextButton (banks[(size_t) i].name));
        flat (*b);
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonColourId, i == bank ? Look::amberDark : Look::raised);
        b->setColour (juce::TextButton::textColourOffId, i == bank ? Look::amber : Look::textDim);
        b->onClick = [this, i]
        {
            bank = i;
            grid.setBank (i);
            rebuildBanks();
        };
        addAndMakeVisible (b);
    }
    grid.setBank (bank);
    resized();
}

void MobileMainPage::showBank (int index)
{
    const int count = (int) controller.getShow().banks.size();
    if (count <= 0)
        return;
    bank = ((index % count) + count) % count;
    grid.setBank (bank);
    rebuildBanks();
}

void MobileMainPage::perform (const juce::String& action)
{
    auto& show = controller.getShow();
    const int list = controller.getSelectedPlaylist();
    if (action.startsWith ("slot"))
    {
        const int id = Show::cueId (bank, action.fromFirstOccurrenceOf ("slot", false, false).getIntValue() - 1);
        if (show.isValid (id) && ! show.cue (id).isEmpty())
            controller.trigger (id);
    }
    else if (action.startsWith ("bank") && action.length() > 4 && action.substring (4).containsOnly ("0123456789"))
        showBank (action.substring (4).getIntValue() - 1);
    else if (action.startsWith ("go") && action.length() > 2 && action.substring (2).containsOnly ("0123456789"))
    {
        const int pl = action.substring (2).getIntValue() - 1;   // GO playlist N
        if (pl >= 0 && pl < (int) show.playlists.size())
            controller.go (pl);
    }
    else if (action == "go")           controller.go (list);
    else if (action == "fadeNext")     controller.fadeToNext (list);
    else if (action == "stopAll")      controller.getEngine().stopAll (false);
    else if (action == "fadeAll")      controller.getEngine().stopAll (true);
    else if (action == "bankNext")     showBank (bank + 1);
    else if (action == "bankPrev")     showBank (bank - 1);
    else if (action == "selfDestruct") controller.setSelfDestruct (! controller.isSelfDestruct());
    else if (action == "resetColours") controller.resetColours();
    else if (action == "runGo")        controller.runGo();
    else if (action == "schedGo")      controller.scheduleGoNow();
    else if (action.startsWith ("hold")) controller.holdSchedule (action.substring (4).getIntValue());
}

void MobileMainPage::refresh()
{
    if (bankButtons.size() != (int) controller.getShow().banks.size())
        rebuildBanks();

    const auto file = controller.getShowFile();
    showName.setText ((file == juce::File() ? juce::String ("New show") : file.getFileNameWithoutExtension())
                          + (controller.hasUnsavedChanges() ? " *" : ""), juce::dontSendNotification);

    // what a folded section still has to say
    const int list = controller.getSelectedPlaylist();
    juce::String next = "PLAYLIST";
    if (auto* p = controller.playlistAt (list))
    {
        next = p->name.toUpperCase();
        if (const int entry = controller.getNextEntry (list); entry >= 0 && entry < (int) p->entries.size())
        {
            const auto id = p->entries[(size_t) entry].cueId;
            if (controller.getShow().isValid (id))
                next << ": " << controller.getShow().cue (id).name;
        }
    }
    playlistHeader.setButtonText ((playlistOpen || editButton.getToggleState() ? openArrow : collapsedArrow) + next);

    const auto playing = controller.newestVoice();
    nowHeader.setButtonText ((nowOpen ? openArrow : collapsedArrow)
                             + (playing.any ? playing.name + "  " + CueGrid::formatTime (playing.left) : juce::String ("NOW PLAYING")));
}

void MobileMainPage::setOpen (bool playlistSection, bool open)
{
    (playlistSection ? playlistOpen : nowOpen) = open;
    settings.setValue (playlistSection ? "playlistOpen" : "nowPlayingOpen", open);
    settings.saveIfNeeded();
    resized();
    refresh();
}

void MobileMainPage::paint (juce::Graphics& g)
{
    g.fillAll (Look::ground);
    g.setColour (Look::panel);
    g.fillRect (getLocalBounds().removeFromTop (barHeight));
}

void MobileMainPage::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (barHeight).reduced (8, 7);
    menuButton.setBounds (bar.removeFromLeft (46));
    bar.removeFromLeft (10);
    editButton.setBounds (bar.removeFromRight (72));
    bar.removeFromRight (8);
    showName.setBounds (bar);

    auto tabs = r.removeFromTop (tabHeight).reduced (8, 3);
    if (! bankButtons.isEmpty())
    {
        const int width = (tabs.getWidth() - 4 * (bankButtons.size() - 1)) / bankButtons.size();
        for (auto* b : bankButtons)
        {
            b->setBounds (tabs.removeFromLeft (width));
            tabs.removeFromLeft (4);
        }
    }

    const bool wide = getWidth() >= wideFrom;
    const int perRow = wide || buttonCount == 16 ? 4 : buttonCount == 12 ? 3 : 2;
    grid.setShape (perRow, buttonCount, false);

    const bool playlistShown = playlistOpen || editButton.getToggleState();
    playlist.setVisible (playlistShown);
    nowPlaying.setVisible (nowOpen);

    // always on screen, whatever is folded away
    r.removeFromBottom (gestureBarHeight);
    auto transport = r.removeFromBottom (std::max (minTransportHeight, getHeight() / 6)).reduced (8, 4);
    const int gap = 8, quarter = (transport.getWidth() - 2 * gap) / 4;
    fadeAllButton.setBounds (transport.removeFromLeft (quarter));
    transport.removeFromLeft (gap);
    stopAllButton.setBounds (transport.removeFromLeft (quarter));
    transport.removeFromLeft (gap);
    goButton.setBounds (transport);

    if (wide)
    {
        const int side = 250;
        auto columnFor = [&] (bool open, bool left)
        {
            if (! open)
                return juce::Rectangle<int>();
            return left ? r.removeFromLeft (side) : r.removeFromRight (side);
        };
        auto strips = r.removeFromBottom (stripHeight);
        playlistHeader.setBounds (strips.removeFromLeft (getWidth() / 2).reduced (8, 3));
        nowHeader.setBounds (strips.reduced (8, 3));
        playlist.setBounds (columnFor (playlistShown, true));
        nowPlaying.setBounds (columnFor (nowOpen, false));
        grid.setBounds (r.reduced (6));
        return;
    }

    playlistHeader.setBounds (r.removeFromTop (stripHeight).reduced (8, 3));
    if (playlistShown)
        playlist.setBounds (r.removeFromTop (std::min (170, r.getHeight() / 3)));   // the grid keeps room for the song names
    if (nowOpen)
        nowPlaying.setBounds (r.removeFromBottom (std::min (210, r.getHeight() / 3)));
    nowHeader.setBounds (r.removeFromBottom (stripHeight).reduced (8, 3));
    grid.setBounds (r.reduced (6, 4));
}

void MobileMainPage::message (const juce::String& text)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Stinger", text);
}

void MobileMainPage::askName (std::function<void (juce::String)> done)
{
    auto* window = new juce::AlertWindow ("Save show", "Name of the show:", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", showName.getText().trimCharactersAtEnd (" *"));
    window->addButton ("Save", 1);
    window->addButton ("Cancel", 0);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, done] (int result)
    {
        if (result == 1)
            done (window->getTextEditorContents ("name").trim());
    }), true);
}

void MobileMainPage::saveTo (const juce::String& name)
{
    if (name.isEmpty())
        return;
    juce::String error;
    if (controller.saveShow (controller.showFileFor (name), error))
    {
        settings.setValue ("lastShow", controller.getShowFile().getFullPathName());
        settings.saveIfNeeded();
    }
    else
        message (error);
    refresh();
}

void MobileMainPage::save()
{
    if (controller.getShowFile() == juce::File())
        saveAs();
    else
        saveTo (controller.getShowFile().getFileNameWithoutExtension());
}

void MobileMainPage::saveAs()
{
    juce::Component::SafePointer<MobileMainPage> safe (this);
    askName ([safe] (juce::String name) { if (safe != nullptr) safe->saveTo (name); });
}

void MobileMainPage::open (const juce::File& file)
{
    juce::String error;
    if (controller.openShow (file, error))
    {
        settings.setValue ("lastShow", file.getFullPathName());
        settings.saveIfNeeded();
        warnAboutHiddenButtons();
    }
    else
        message (error);
    rebuildBanks();
    refresh();
}

void MobileMainPage::setButtonCount (int count)
{
    buttonCount = count;
    controller.setButtonsPerBank (count);
    settings.setValue ("buttonsPerBank", count);
    settings.saveIfNeeded();
    resized();
    warnAboutHiddenButtons();
}

void MobileMainPage::warnAboutHiddenButtons()
{
    int hidden = 0;
    for (auto& b : controller.getShow().banks)
        for (int s = buttonCount; s < Show::cuesPerBank; ++s)
            if (! b.cues[(size_t) s].isEmpty())
                ++hidden;
    if (hidden > 0)
        message (juce::String (hidden) + (hidden == 1 ? " song is" : " songs are") + " on buttons past number " + juce::String (buttonCount)
                 + ". They stay in the show, and playlists can still play them, but they aren't on this page.");
}

void MobileMainPage::showMenu()
{
    juce::PopupMenu shows;
    auto files = controller.getHomeFolder().findChildFiles (juce::File::findFiles, true, "*.stinger");
    files.sort();
    for (auto& f : files)
        shows.addItem (f.getFileNameWithoutExtension(), [this, f] { open (f); });
    if (files.isEmpty())
        shows.addItem ("No saved shows", false, false, [] {});

    juce::PopupMenu mode;
    const auto current = controller.getShow().playbackMode;
    auto addMode = [&] (const juce::String& text, PlaybackMode m)
    {
        mode.addItem (text, true, current == m, [this, m] { controller.setPlaybackMode (m); });
    };
    addMode ("One at a time", PlaybackMode::oneAtATime);
    addMode ("One per output", PlaybackMode::onePerOutput);
    addMode ("Overlap", PlaybackMode::overlap);

    juce::PopupMenu count;
    for (const int n : { 8, 12, 16 })
        count.addItem (juce::String (n) + " buttons", true, buttonCount == n, [this, n] { setButtonCount (n); });

    juce::PopupMenu menu;
    menu.addItem ("Save show", [this] { save(); });
    menu.addItem ("Save as...", [this] { saveAs(); });
    menu.addSubMenu ("Open show", shows);
    menu.addItem ("New show", [this] { controller.newShow(); rebuildBanks(); refresh(); });
    menu.addSeparator();
    menu.addSubMenu ("Buttons", count);
    menu.addSubMenu ("Playback", mode);
    menu.addItem ("Self Destruct", true, controller.isSelfDestruct(), [this] { controller.setSelfDestruct (! controller.isSelfDestruct()); });
    menu.addItem ("Reset black buttons", controller.anySpent(), false, [this] { controller.resetColours(); });
    menu.addSeparator();
    menu.addItem ("Audio...", [this] { if (onOpenAudio) onOpenAudio(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton).withMinimumWidth (220).withStandardItemHeight (44));
}

// ---------------------------------------------------------------------------------------------

MobileRoot::MobileRoot (ShowController& controller, juce::PropertiesFile& settings)
    : mainPage (controller, settings), editorPage (controller), audioPage (controller)
{
    addAndMakeVisible (mainPage);
    addChildComponent (editorPage);
    addChildComponent (audioPage);
    mainPage.onOpenEditor = [this] (int id)
    {
        editorPage.setCue (id);
        show (editorPage);
    };
    mainPage.onChooseSong = [this] (int id)
    {
        editorPage.setCue (id);
        show (editorPage);
        editorPage.chooseAudio (true);
    };
    mainPage.onOpenAudio = [this] { show (audioPage); };
    editorPage.onBack = [this] { show (mainPage); };
    audioPage.onBack = [this] { show (mainPage); };
}

void MobileRoot::show (juce::Component& page)
{
    juce::Component::unfocusAllComponents();   // takes the keyboard away with the page
    for (auto* p : std::initializer_list<juce::Component*> { &mainPage, &editorPage, &audioPage })
        p->setVisible (p == &page);
}

bool MobileRoot::goBack()
{
    if (mainPage.isVisible())
        return false;
    show (mainPage);
    return true;
}

void MobileRoot::resized()
{
    for (auto* p : std::initializer_list<juce::Component*> { &mainPage, &editorPage, &audioPage })
        p->setBounds (getLocalBounds());
}
