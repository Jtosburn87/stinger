#include "CueSettings.h"
#include "CueGrid.h"
#include "Look.h"
#include "ControlSurface.h"

// ---------------------------------------------------------------------------------------------
// Settings

CueSettings::CueSettings (ShowController& c) : controller (c), waveform (c), fxPanel (c)
{
    addAndMakeVisible (waveform);
    addAndMakeVisible (fxPanel);
    title.setFont (Look::condensed (18.0f));
    title.setColour (juce::Label::textColourId, juce::Colour (0xffc9d0d8));
    info.setColour (juce::Label::textColourId, Look::textDim);
    addAndMakeVisible (title);
    addAndMakeVisible (info);

    for (auto* l : { &nameLabel, &outputLabel, &levelLabel, &panLabel, &keyLabel })
    {
        l->setColour (juce::Label::textColourId, Look::textDim);
        addAndMakeVisible (l);
    }
    keyText.setFont (Look::mono (13.0f));
    addAndMakeVisible (keyText);

    colourButton.setWantsKeyboardFocus (false);
    colourButton.setTooltip ("The button's colour (also on the Stream Deck)");
    colourButton.onClick = [this] { Look::showColourMenu (colourButton, controller, cueId); };
    addAndMakeVisible (colourButton);

    name.onTextChange = [this] { if (auto* q = cue()) { q->name = name.getText(); changed(); } };
    addAndMakeVisible (name);

    output.onChange = [this]
    {
        if (auto* q = cue(); q != nullptr && output.getSelectedId() > 0)
        {
            const int id = output.getSelectedId();
            q->output = { id % 1000, id < 2000 };
            changed();
        }
    };
    addAndMakeVisible (output);

    level.setRange (Cue::minLevelDb, Cue::maxLevelDb, 0.5);
    level.setDoubleClickReturnValue (true, 0.0);
    level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    level.textFromValueFunction = [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; };
    level.onValueChange = [this] { if (auto* q = cue()) { q->levelDb = (float) level.getValue(); changed(); } };
    addAndMakeVisible (level);

    pan.setRange (-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    pan.setColour (juce::Slider::thumbColourId, Look::blue);
    pan.setColour (juce::Slider::trackColourId, Look::edge);
    pan.textFromValueFunction = [] (double v)
    {
        const int p = juce::roundToInt (v * 100.0);
        return p == 0 ? juce::String ("C") : p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p);
    };
    pan.onValueChange = [this] { if (auto* q = cue()) { q->pan = (float) pan.getValue(); changed(); } };
    pan.updateText();
    level.updateText();
    addAndMakeVisible (pan);

    setupFade (fadeIn, "Fade in");
    setupFade (fadeOut, "Fade out");
    setupFade (stopFade, "Stop fade");
    fadeIn.seconds.onValueChange = [this] { if (auto* q = cue()) { q->fadeIn.seconds = fadeIn.seconds.getValue(); changed(); } };
    fadeOut.seconds.onValueChange = [this] { if (auto* q = cue()) { q->fadeOut.seconds = fadeOut.seconds.getValue(); changed(); } };
    stopFade.seconds.onValueChange = [this] { if (auto* q = cue()) { q->stopFade.seconds = stopFade.seconds.getValue(); changed(); } };
    fadeIn.curve.onChange = [this] { if (auto* q = cue()) { q->fadeIn.curve = (FadeCurve) (fadeIn.curve.getSelectedId() - 1); changed(); } };
    fadeOut.curve.onChange = [this] { if (auto* q = cue()) { q->fadeOut.curve = (FadeCurve) (fadeOut.curve.getSelectedId() - 1); changed(); } };
    stopFade.curve.onChange = [this] { if (auto* q = cue()) { q->stopFade.curve = (FadeCurve) (stopFade.curve.getSelectedId() - 1); changed(); } };
    stopFade.seconds.setTooltip ("How long Fade out (and Fade all) takes for this cue");

    fadeOthers.setTooltip ("Starting this cue fades out everything else that's playing");
    fadeOthers.onClick = [this] { if (auto* q = cue()) { q->fadeOthers = fadeOthers.getToggleState(); changed(); } };
    loop.onClick = [this] { if (auto* q = cue()) { q->loop = loop.getToggleState(); changed(); } };
    addAndMakeVisible (fadeOthers);
    addAndMakeVisible (loop);

    learn.setClickingTogglesState (true);
    for (juce::Button* b : std::initializer_list<juce::Button*> { &learn, &clearKey, &fadeOthers, &loop })
        b->setWantsKeyboardFocus (false);
    for (auto* box : { &output, &fadeIn.curve, &fadeOut.curve, &stopFade.curve })
        box->setWantsKeyboardFocus (false);   // Space and Enter stay GO and Fade to next
    learn.setTooltip ("Press Learn, then the key or the MIDI note / CC / footswitch that should play this button");
    learn.onClick = [this]
    {
        if (surface == nullptr)
            return;
        if (learn.getToggleState())
            surface->learn ("cue:" + juce::String (cueId));
        else
            surface->cancelLearn();
    };
    clearKey.setTooltip ("Remove this button's own key and MIDI (it keeps the key of its position)");
    clearKey.onClick = [this] { if (auto* q = cue()) { q->hotkey.clear(); q->midi = {}; changed(); refresh(); } };
    addAndMakeVisible (learn);
    addAndMakeVisible (clearKey);

    recordLabel.setColour (juce::Label::textColourId, Look::textDim);
    recordTime.setFont (Look::mono (13.0f));
    recordTime.setColour (juce::Label::textColourId, juce::Colour (0xffff6b6b));
    recordInput.setWantsKeyboardFocus (false);
    recordInput.setTooltip ("The input to record from: one channel (mono) or a pair (stereo)");
    recordInput.onChange = [this]
    {
        if (! updating && settings != nullptr && recordInput.getSelectedId() > 0)
            settings->setValue ("recordInput", recordInput.getSelectedId());
    };
    recordButton.setWantsKeyboardFocus (false);
    recordButton.setTooltip ("Record from the input into this button (arm it first; press again to stop)");
    recordButton.onClick = [this] { toggleRecording(); };
    armButton.setWantsKeyboardFocus (false);
    armButton.setClickingTogglesState (true);
    armButton.setTooltip ("Arm recording: REC only works while this is lit (it switches off after each recording)");
    armButton.setColour (juce::TextButton::buttonOnColourId, Look::red.darker (0.2f));
    armButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    armButton.setColour (juce::TextButton::textColourOffId, Look::textDim);
    for (auto* comp : std::initializer_list<juce::Component*> { &recordLabel, &recordTime, &recordInput, &armButton, &recordButton })
        addAndMakeVisible (comp);
    fillInputs();

    linkLabel.setColour (juce::Label::textColourId, Look::textDim);
    linkText.setTextToShowWhenEmpty ("Paste a YouTube, Dropbox or OneDrive link", Look::textFaint);
    linkText.setTooltip ("A link to a song: Pull WAV downloads it as a 24-bit WAV at the device's sample rate and puts it on this button");
    linkText.onReturnKey = [this] { pullOrCancel(); };
    pullButton.setWantsKeyboardFocus (false);
    pullButton.setTooltip ("Download the link into this button");
    pullButton.onClick = [this] { pullOrCancel(); };
    for (auto* comp : std::initializer_list<juce::Component*> { &linkLabel, &linkText, &pullButton })
        addAndMakeVisible (comp);

    controller.addChangeListener (this);
    controller.getEngine().getDeviceManager().addChangeListener (this);
    startTimerHz (30);
}

CueSettings::~CueSettings()
{
    if (surface != nullptr)
        surface->removeChangeListener (this);
    controller.getEngine().getDeviceManager().removeChangeListener (this);
    controller.removeChangeListener (this);
}

void CueSettings::setupFade (FadeRow& row, const juce::String& text)
{
    row.label.setText (text, juce::dontSendNotification);
    row.label.setColour (juce::Label::textColourId, Look::textDim);
    row.seconds.setRange (0.0, 30.0, 0.1);
    row.seconds.setSkewFactorFromMidPoint (4.0);
    row.seconds.setTextValueSuffix (" s");
    row.seconds.setColour (juce::Slider::trackColourId, juce::Colour (0xff172636));
    row.seconds.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff3f6a95));
    for (int c = 0; c < 4; ++c)
        row.curve.addItem (fadeCurveName ((FadeCurve) c), c + 1);
    addAndMakeVisible (row.label);
    addAndMakeVisible (row.seconds);
    addAndMakeVisible (row.curve);
}

Cue* CueSettings::cue()
{
    if (updating || ! controller.getShow().isValid (cueId))
        return nullptr;
    return &controller.getShow().cue (cueId);
}

void CueSettings::changed()
{
    controller.cueChanged (cueId);
}

void CueSettings::setCue (int id)
{
    cueId = id;
    if (surface != nullptr && surface->learning().startsWith ("cue:"))
        surface->cancelLearn();
    waveform.setCue (id);
    fxPanel.setCue (id);
    refresh();
}

void CueSettings::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    waveform.setCue (cueId);   // same cue: refreshes its time fields
    if (source == &controller.getEngine().getDeviceManager())
    {
        fillOutputs();
        waveform.fillPreviewOutputs();
        fillInputs();
    }
    refresh();
}

void CueSettings::fillOutputs()
{
    const juce::ScopedValueSetter<bool> busy (updating, true);
    output.clear (juce::dontSendNotification);
    const auto names = controller.getEngine().getOutputNames();
    const int count = std::max (2, names.size());
    output.addSectionHeading ("Stereo");
    for (int ch = 0; ch + 1 < count; ch += 2)
        output.addItem (CueGrid::outputLabel ({ ch, true }) + (ch + 1 < names.size() ? "  (" + names[ch] + " / " + names[ch + 1] + ")" : juce::String()), 1000 + ch);
    output.addSectionHeading ("Mono");
    for (int ch = 0; ch < count; ++ch)
        output.addItem (CueGrid::outputLabel ({ ch, false }) + (ch < names.size() ? "  (" + names[ch] + ")" : juce::String()), 2000 + ch);
}

void CueSettings::refresh()
{
    const juce::ScopedValueSetter<bool> busy (updating, true);
    auto& show = controller.getShow();
    const bool valid = show.isValid (cueId);
    const bool has = valid && ! show.cue (cueId).isEmpty();
    for (auto* c : std::initializer_list<juce::Component*> { &name, &colourButton, &output, &level, &pan, &fadeIn.seconds, &fadeIn.curve,
                                                            &fadeOut.seconds, &fadeOut.curve, &stopFade.seconds, &stopFade.curve,
                                                            &fadeOthers, &loop, &learn, &clearKey })
        c->setEnabled (has);
    if (output.getNumItems() == 0)
        fillOutputs();

    if (! valid)
        return;
    const auto& c = show.cue (cueId);
    title.setText ("EDIT " + juce::String::fromUTF8 ("\xc2\xb7 ") + (has ? c.name.toUpperCase() : juce::String ("EMPTY BUTTON")), juce::dontSendNotification);

    juce::String details;
    if (has)
    {
        details = c.file.getFileName();
        if (auto audio = controller.getAudio (cueId))
            details << juce::String::fromUTF8 (" \xc2\xb7 ") << CueGrid::formatTime (audio->seconds())
                    << juce::String::fromUTF8 (" \xc2\xb7 ") << (audio->fileChannels == 1 ? "mono" : "stereo")
                    << juce::String::fromUTF8 (" \xc2\xb7 plays ") << CueGrid::formatTime (c.playLength (audio->seconds()));
        else if (controller.getError (cueId).isNotEmpty())
            details = controller.getError (cueId);
    }
    info.setText (details, juce::dontSendNotification);

    if (! name.hasKeyboardFocus (true))
        name.setText (c.name, false);
    output.setSelectedId ((c.output.stereo ? 1000 : 2000) + c.output.firstChannel, juce::dontSendNotification);
    colourButton.setColour (juce::TextButton::buttonColourId, Look::colourOf (c));
    colourButton.setColour (juce::TextButton::textColourOffId, Look::textOn (Look::colourOf (c)));
    level.setValue (c.levelDb, juce::dontSendNotification);
    pan.setValue (c.pan, juce::dontSendNotification);
    fadeIn.seconds.setValue (c.fadeIn.seconds, juce::dontSendNotification);
    fadeOut.seconds.setValue (c.fadeOut.seconds, juce::dontSendNotification);
    stopFade.seconds.setValue (c.stopFade.seconds, juce::dontSendNotification);
    fadeIn.curve.setSelectedId ((int) c.fadeIn.curve + 1, juce::dontSendNotification);
    fadeOut.curve.setSelectedId ((int) c.fadeOut.curve + 1, juce::dontSendNotification);
    stopFade.curve.setSelectedId ((int) c.stopFade.curve + 1, juce::dontSendNotification);
    fadeOthers.setToggleState (c.fadeOthers, juce::dontSendNotification);
    loop.setToggleState (c.loop, juce::dontSendNotification);

    const bool learningThis = surface != nullptr && surface->learning() == "cue:" + juce::String (cueId);
    learn.setToggleState (learningThis, juce::dontSendNotification);
    if (learningThis)
        keyText.setText ("Press a key or send MIDI...", juce::dontSendNotification);
    else
    {
        auto text = surface != nullptr ? ControlSurface::describe (surface->bindingFor ("cue:" + juce::String (cueId))) : juce::String();
        if (c.hotkey.isEmpty())
        {
            const auto slotKey = surface != nullptr ? surface->getCommands().get ("slot" + juce::String (Show::slotOf (cueId) + 1)).keyPress()
                                                    : CueGrid::defaultKey (Show::slotOf (cueId));
            if (slotKey.isValid())
                text = slotKey.getTextDescriptionWithIcons() + " (its position)" + (text.isNotEmpty() ? "   |   " + text : juce::String());
        }
        keyText.setText (text.isNotEmpty() ? text : juce::String ("none"), juce::dontSendNotification);
    }
    repaint();
}

void CueSettings::setControlSurface (ControlSurface* s)
{
    if (surface != nullptr)
        surface->removeChangeListener (this);
    surface = s;
    if (surface != nullptr)
        surface->addChangeListener (this);
    refresh();
}

void CueSettings::paint (juce::Graphics& g)
{
    g.fillAll (Look::panel);
    g.setColour (Look::line);
    g.fillRect (0, 0, getWidth(), 1);
    g.fillRect (getWidth() - 640, 0, 1, getHeight());
    g.fillRect (getWidth() - 240, 0, 1, getHeight());

    // input level for recording
    if (! meterArea.isEmpty())
    {
        g.setColour (juce::Colour (0xff23282f));
        g.fillRect (meterArea);
        const float db = juce::Decibels::gainToDecibels (inputLevel, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (db > -1.0f ? juce::Colour (0xffe5484d) : db > -9.0f ? Look::amber : Look::green);
        g.fillRect (meterArea.toFloat().withWidth (meterArea.getWidth() * frac));
    }
    // Pull WAV progress
    if (pullJob != 0 && ! pullBar.isEmpty())
    {
        g.setColour (juce::Colour (0xff23282f));
        g.fillRect (pullBar);
        g.setColour (Look::blue);
        g.fillRect (pullBar.toFloat().withWidth (pullBar.getWidth() * (float) pullProgress));
    }
}

void CueSettings::resized()
{
    auto r = getLocalBounds();
    fxPanel.setBounds (r.removeFromRight (240).reduced (14, 10));
    auto side = r.removeFromRight (400).reduced (16, 10);
    r.reduce (16, 12);

    auto head = r.removeFromTop (28);
    title.setBounds (head.removeFromLeft (360));
    info.setBounds (head);
    r.removeFromTop (8);
    waveform.setBounds (r);

    auto row = [&side] (int h) { auto x = side.removeFromTop (h); side.removeFromTop (5); return x; };
    auto labelled = [&] (juce::Label& l, int h)
    {
        auto x = row (h);
        l.setBounds (x.removeFromLeft (84));
        return x;
    };
    {
        auto nameRow = labelled (nameLabel, 28);
        colourButton.setBounds (nameRow.removeFromRight (70));
        nameRow.removeFromRight (6);
        name.setBounds (nameRow);
    }
    output.setBounds (labelled (outputLabel, 28));
    level.setBounds (labelled (levelLabel, 26));
    pan.setBounds (labelled (panLabel, 26));
    for (auto* f : { &fadeIn, &fadeOut, &stopFade })
    {
        auto x = labelled (f->label, 24);
        f->curve.setBounds (x.removeFromRight (120));
        x.removeFromRight (6);
        f->seconds.setBounds (x);
    }
    auto toggles = row (24);
    fadeOthers.setBounds (toggles.removeFromLeft (200));
    loop.setBounds (toggles);
    auto keys = labelled (keyLabel, 26);
    clearKey.setBounds (keys.removeFromRight (60));
    keys.removeFromRight (6);
    learn.setBounds (keys.removeFromRight (64));
    keys.removeFromRight (6);
    keyText.setBounds (keys);
    side.removeFromTop (2);
    auto rec = labelled (recordLabel, 26);
    recordTime.setBounds (rec.removeFromRight (44));
    rec.removeFromRight (4);
    recordButton.setBounds (rec.removeFromRight (72));
    rec.removeFromRight (4);
    armButton.setBounds (rec.removeFromRight (50));
    rec.removeFromRight (6);
    recordInput.setBounds (rec);
    meterArea = { rec.getX(), rec.getBottom() + 2, rec.getWidth(), 4 };
    side.removeFromTop (4);
    auto link = labelled (linkLabel, 26);
    pullButton.setBounds (link.removeFromRight (80));
    link.removeFromRight (6);
    linkText.setBounds (link);
    pullBar = { link.getX(), link.getBottom() + 2, link.getWidth(), 3 };
}

// ---------------------------------------------------------------------------------------------
// Recording into the button

void CueSettings::fillInputs()
{
    const juce::ScopedValueSetter<bool> busy (updating, true);
    const int previous = recordInput.getSelectedId();
    recordInput.clear (juce::dontSendNotification);
    const auto names = controller.getEngine().getInputNames();
    if (names.isEmpty())
    {
        recordInput.addItem ("No inputs (choose them in Audio)", 1);
        recordInput.setItemEnabled (1, false);
        return;
    }
    recordInput.addSectionHeading ("Mono");
    for (int ch = 0; ch < names.size(); ++ch)
        recordInput.addItem ("In " + juce::String (ch + 1) + "  (" + names[ch] + ")", 2000 + ch);
    recordInput.addSectionHeading ("Stereo");
    for (int ch = 0; ch + 1 < names.size(); ch += 2)
        recordInput.addItem ("In " + juce::String (ch + 1) + juce::String::fromUTF8 ("\xe2\x80\x93") + juce::String (ch + 2)
                             + "  (" + names[ch] + " / " + names[ch + 1] + ")", 1000 + ch);
    int id = settings != nullptr ? settings->getIntValue ("recordInput", previous > 0 ? previous : 2000) : (previous > 0 ? previous : 2000);
    if (recordInput.indexOfItemId (id) < 0)
        id = 2000;
    recordInput.setSelectedId (id, juce::dontSendNotification);
}

juce::File CueSettings::recordingFolder() const
{
    const auto show = controller.getShowFile();
    if (show != juce::File())
        return show.getParentDirectory().getChildFile (show.getFileNameWithoutExtension() + " Recordings");
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Stinger Recordings");
}

void CueSettings::toggleRecording()
{
    auto& engine = controller.getEngine();
    if (engine.isRecording())
    {
        const auto file = engine.stopRecording();
        const int id = recordingCue;
        recordingCue = -1;
        armButton.setToggleState (false, juce::dontSendNotification);   // disarmed after each take
        if (file.existsAsFile() && controller.getShow().isValid (id))
        {
            const auto oldName = controller.getShow().cue (id).name;
            controller.assignFile (id, file);
            controller.getShow().cue (id).name = oldName.isNotEmpty() ? oldName
                                                                     : "Recording " + juce::Time::getCurrentTime().formatted ("%H:%M");
            controller.cueChanged (id);
        }
        refresh();
        return;
    }
    if (! armButton.getToggleState() || ! controller.getShow().isValid (cueId) || recordInput.getSelectedId() < 1000)
        return;
    if (! controller.getShow().cue (cueId).isEmpty())
    {
        juce::Component::SafePointer<CueSettings> safe (this);
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Record",
                                                                     "Record over \"" + controller.getShow().cue (cueId).name
                                                                         + "\"? Its file stays on disk; the button gets the new recording.",
                                                                     "Record", "Cancel", this);
        juce::AlertWindow::showAsync (options, [safe] (int result) { if (safe != nullptr && result == 1) safe->startRecording(); });
        return;
    }
    startRecording();
}

void CueSettings::startRecording()
{
    const int id = recordInput.getSelectedId();
    const OutputRoute input { id % 1000, id < 2000 };
    auto& c = controller.getShow().cue (cueId);
    const auto label = c.name.isNotEmpty() ? c.name
                                           : "Button " + juce::String::charToString ((juce::juce_wchar) ('A' + Show::bankOf (cueId)))
                                               + juce::String (Show::slotOf (cueId) + 1);
    const auto file = recordingFolder().getChildFile (juce::File::createLegalFileName (label)
                                                      + juce::Time::getCurrentTime().formatted (" %Y-%m-%d %H-%M-%S") + ".wav");
    juce::String error;
    if (! controller.getEngine().startRecording (file, input, error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Record", error);
        return;
    }
    controller.getEngine().stopCue (cueId, false);
    recordingCue = cueId;
    refresh();
}

juce::File CueSettings::downloadFolder (const ShowController& c)
{
    const auto show = c.getShowFile();
    if (show != juce::File())
        return show.getParentDirectory().getChildFile (show.getFileNameWithoutExtension() + " Downloads");
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Stinger Downloads");
}

void CueSettings::pullOrCancel()
{
    if (downloader == nullptr)
        return;
    if (pullJob != 0)
    {
        downloader->cancel (pullJob);
        return;
    }
    const auto links = Downloader::linksIn (linkText.getText());
    if (links.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Pull WAV",
                                                "Paste a link first (it starts with https://).");
        return;
    }
    if (! controller.getShow().isValid (cueId))
        return;
    const int id = cueId;
    if (! controller.getShow().cue (id).isEmpty())
    {
        juce::Component::SafePointer<CueSettings> safe (this);
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Pull WAV",
                                                                     "Put the song from the link on this button instead of \""
                                                                         + controller.getShow().cue (id).name + "\"? Its file stays on disk.",
                                                                     "Pull", "Cancel", this);
        juce::AlertWindow::showAsync (options, [safe, id, link = links[0]] (int result)
                                      { if (safe != nullptr && result == 1) safe->startPull (id, link); });
        return;
    }
    startPull (id, links[0]);
}

void CueSettings::startPull (int id, const juce::String& link)
{
    juce::Component::SafePointer<CueSettings> safe (this);
    pullProgress = 0.0;
    pullJob = downloader->add (link, downloadFolder (controller), controller.getEngine().getSampleRate(),
                               [safe, id, link] (const Downloader::Job& job)
    {
        if (safe == nullptr)
            return;
        safe->pullJob = 0;
        safe->linkText.setReadOnly (false);
        safe->pullButton.setButtonText ("Pull WAV");
        if (job.state == Downloader::State::done && safe->controller.getShow().isValid (id))
        {
            safe->controller.assignFile (id, job.result);
            safe->controller.getShow().cue (id).name = job.title;
            safe->controller.cueChanged (id);
            safe->linkText.clear();
        }
        else
        {
            safe->linkText.setText (link, false);
            if (job.state == Downloader::State::failed)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Pull WAV", job.status);
        }
        safe->repaint();
    });
    linkText.setReadOnly (true);
    pullButton.setButtonText ("Cancel");
}

void CueSettings::timerCallback()
{
    waveform.tick();

    if (pullJob != 0 && downloader != nullptr)
    {
        Downloader::Job job;
        if (downloader->getJob (pullJob, job))
        {
            pullProgress = job.progress;
            const auto text = job.status + (job.title != job.url ? juce::String::fromUTF8 (" \xc2\xb7 ") + job.title : juce::String());
            if (linkText.getText() != text)
                linkText.setText (text, false);
            repaint (pullBar.expanded (2));
        }
    }

    auto& engine = controller.getEngine();
    const int id = recordInput.getSelectedId();
    float peak = 0.0f;
    if (id >= 1000)
    {
        const OutputRoute input { id % 1000, id < 2000 };
        for (int ch = 0; ch < input.numChannels(); ++ch)
            peak = std::max (peak, engine.takeInputPeak (input.firstChannel + ch));
    }
    inputLevel = std::max (peak, inputLevel * 0.85f);

    const bool rec = engine.isRecording();
    recordButton.setButtonText (rec ? juce::String::fromUTF8 ("\xe2\x96\xa0 STOP") : juce::String::fromUTF8 ("\xe2\x97\x8f REC"));
    const bool blink = rec && (juce::Time::getMillisecondCounter() / 500) % 2 == 0;
    recordButton.setColour (juce::TextButton::buttonColourId, rec ? (blink ? Look::red : Look::red.darker (0.4f)) : Look::raised);
    recordButton.setColour (juce::TextButton::textColourOffId, rec ? juce::Colours::white : juce::Colour (0xffff6b6b));
    recordTime.setText (rec ? CueGrid::formatTime (engine.getRecordedSeconds()) : juce::String(), juce::dontSendNotification);
    recordInput.setEnabled (! rec);
    recordButton.setEnabled (rec || armButton.getToggleState());
    armButton.setEnabled (! rec);
    repaint (meterArea.expanded (2));
}
