#include "PlaylistPanel.h"
#include "CueGrid.h"
#include "Look.h"

namespace
{
    juce::String chipText (const PlaylistEntry& e)
    {
        switch (e.transition)
        {
            case Transition::stop:      return juce::String::fromUTF8 ("\xe2\x96\xa0  Stop");
            case Transition::playNext:  return juce::String::fromUTF8 ("\xe2\x86\x92  Play next");
            case Transition::crossfade: return juce::String::fromUTF8 ("\xe2\xa4\xab  Crossfade ") + juce::String (e.crossfadeSeconds, 1) + " s";
            case Transition::loopUntil: return juce::String::fromUTF8 ("\xe2\x86\xbb  Loop until GO");
            case Transition::wait:      return juce::String::fromUTF8 ("\xe2\x80\x96  Wait ")
                                               + juce::String (e.waitSeconds, e.waitSeconds < 10.0 ? 1 : 0) + " s, then next";
        }
        return {};
    }
}

// ---------------------------------------------------------------------------------------------

PlaylistRows::PlaylistRows (ShowController& c) : controller (c) {}

int PlaylistRows::entryCount()
{
    auto* p = controller.currentPlaylist();
    return p == nullptr ? 0 : (int) p->entries.size();
}

int PlaylistRows::contentHeight() const
{
    auto* p = const_cast<ShowController&> (controller).currentPlaylist();
    const int n = p == nullptr ? 0 : (int) p->entries.size();
    return std::max (120, n * (rowHeight + chipHeight) + 8);
}

juce::Rectangle<int> PlaylistRows::rowBounds (int i) const
{
    return { 8, i * (rowHeight + chipHeight), getWidth() - 16, rowHeight };
}

juce::Rectangle<int> PlaylistRows::chipBounds (int i)
{
    auto* p = controller.currentPlaylist();
    if (p == nullptr || i < 0 || i >= (int) p->entries.size())
        return {};
    const auto text = chipText (p->entries[(size_t) i]);
    const int w = (int) Look::textWidth (Look::font (12.0f), text) + 18;
    return { 38, i * (rowHeight + chipHeight) + rowHeight + 2, w, chipHeight - 4 };
}

int PlaylistRows::rowAt (int y) const
{
    const int i = y / (rowHeight + chipHeight);
    return (y - i * (rowHeight + chipHeight)) < rowHeight ? i : -1;
}

int PlaylistRows::chipAt (juce::Point<int> p)
{
    for (int i = 0; i < entryCount(); ++i)
        if (chipBounds (i).contains (p))
            return i;
    return -1;
}

int PlaylistRows::insertIndexAt (int y)
{
    return juce::jlimit (0, entryCount(), (y + (rowHeight + chipHeight) / 2) / (rowHeight + chipHeight));
}

void PlaylistRows::paint (juce::Graphics& g)
{
    auto* p = controller.currentPlaylist();
    if (p == nullptr || p->entries.empty())
    {
        g.setColour (Look::textFaint);
        g.setFont (Look::font (13.0f));
        g.drawFittedText ("Empty playlist.\nTurn on EDIT and drag buttons here,\nright-click a button > Add to playlist,\nor drop audio files.",
                          getLocalBounds().reduced (12).withHeight (80), juce::Justification::topLeft, 4);
    }
    else
    {
        const int onAir = controller.getOnAirEntry(), next = controller.getNextEntry();
        auto& show = controller.getShow();
        for (int i = 0; i < (int) p->entries.size(); ++i)
        {
            const auto& e = p->entries[(size_t) i];
            auto r = rowBounds (i);
            const bool valid = show.isValid (e.cueId);
            const Cue* cue = valid ? &show.cue (e.cueId) : nullptr;

            juce::Colour text = Look::text;
            if (i == onAir)
            {
                g.setColour (Look::amberDark);
                g.fillRoundedRectangle (r.toFloat(), 6.0f);
                text = juce::Colour (0xfff5c77e);
            }
            else if (i == next)
            {
                g.setColour (juce::Colour (0xff16281f));
                g.fillRoundedRectangle (r.toFloat(), 6.0f);
                g.setColour (Look::goGreen);
                g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
            }
            else if (onAir >= 0 && i < onAir)
                text = juce::Colour (0xff7c8693);   // already played

            auto in = r.reduced (8, 0);
            g.setColour (juce::Colour (0xff8c96a3));
            g.setFont (Look::mono (12.0f));
            g.drawText (juce::String (i + 1).paddedLeft ('0', 2), in.removeFromLeft (24), juce::Justification::centredLeft);

            juce::String length;
            if (auto audio = controller.getAudio (e.cueId); audio != nullptr && cue != nullptr)
                length = CueGrid::formatTime (cue->playLength (audio->seconds()));
            g.setColour (Look::textDim);
            g.drawText (length, in.removeFromRight (38), juce::Justification::centredRight);

            if (i == onAir || i == next)
            {
                const juce::String tag = i == onAir ? "ON AIR" : "NEXT";
                g.setFont (Look::font (10.0f, true));
                const int w = (int) Look::textWidth (g.getCurrentFont(), tag) + 10;
                auto t = in.removeFromRight (w).withSizeKeepingCentre (w, 16).toFloat();
                in.removeFromRight (6);
                g.setColour (i == onAir ? Look::amber : Look::goGreen);
                g.fillRoundedRectangle (t, 3.0f);
                g.setColour (i == onAir ? juce::Colour (0xff1a1206) : juce::Colours::white);
                g.drawText (tag, t, juce::Justification::centred);
            }

            if (cue != nullptr && ! cue->isEmpty())
            {
                g.setColour (Look::colourOf (*cue));
                g.fillEllipse (in.getX() + 0.0f, (float) in.getCentreY() - 4.0f, 8.0f, 8.0f);
                in.removeFromLeft (12);
            }
            g.setColour (cue != nullptr && ! cue->isEmpty() ? text : Look::textFaint);
            g.setFont (Look::font (14.0f));
            g.drawText (cue != nullptr && ! cue->isEmpty() ? cue->name : juce::String ("(empty button)"), in.withTrimmedLeft (4),
                        juce::Justification::centredLeft, true);

            // transition chip, joined to its row by a short line
            auto chip = chipBounds (i).toFloat();
            const bool last = i == (int) p->entries.size() - 1;
            g.setColour (Look::edge);
            g.fillRect (26.0f, (float) r.getBottom(), 1.0f, (float) chipHeight);
            g.setColour (e.transition == Transition::stop ? Look::raised : juce::Colour (0xff172636));
            g.fillRoundedRectangle (chip, 4.0f);
            g.setColour (e.transition == Transition::stop ? Look::edge : juce::Colour (0xff3f6a95));
            g.drawRoundedRectangle (chip.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (e.transition == Transition::stop ? Look::textDim : juce::Colour (0xffa9d0f7));
            if (last)
                g.setColour (Look::textFaint);
            g.setFont (Look::font (12.0f));
            g.drawText (chipText (e), chip, juce::Justification::centred);
        }
    }

    if (dropAt >= 0)
    {
        g.setColour (Look::blue);
        g.fillRect (8, dropAt * (rowHeight + chipHeight) - 2, getWidth() - 16, 3);
    }
}

void PlaylistRows::mouseDown (const juce::MouseEvent& e)
{
    reordering = false;
    dragFrom = -1;
    if (const int c = chipAt (e.getPosition()); c >= 0)
    {
        showTransitionMenu (c);
        return;
    }
    const int i = rowAt (e.y);
    if (i < 0 || i >= entryCount())
        return;
    if (e.mods.isPopupMenu())
    {
        showRowMenu (i);
        return;
    }
    dragFrom = i;
    controller.setNextEntry (i);
}

void PlaylistRows::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0 || e.getDistanceFromDragStart() < 6)
        return;
    reordering = true;
    dropAt = insertIndexAt (e.y);
    repaint();
}

void PlaylistRows::mouseUp (const juce::MouseEvent&)
{
    if (reordering && dragFrom >= 0 && dropAt >= 0)
        controller.moveEntry (dragFrom, dropAt > dragFrom ? dropAt - 1 : dropAt);
    reordering = false;
    dragFrom = dropAt = -1;
    repaint();
}

void PlaylistRows::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = rowAt (e.y);
    if (i >= 0 && i < entryCount() && chipAt (e.getPosition()) < 0)
        controller.playEntry (i);
}

void PlaylistRows::showRowMenu (int i)
{
    juce::PopupMenu m;
    m.addItem (1, "Play now");
    m.addItem (2, "Set as next");
    m.addSeparator();
    m.addItem (3, "Remove from playlist");
    juce::Component::SafePointer<PlaylistRows> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, i] (int result)
    {
        if (safe == nullptr)
            return;
        auto& c = safe->controller;
        if (result == 1) c.playEntry (i);
        else if (result == 2) c.setNextEntry (i);
        else if (result == 3) c.removeEntry (i);
    });
}

void PlaylistRows::showTransitionMenu (int i)
{
    auto* p = controller.currentPlaylist();
    if (p == nullptr || i >= (int) p->entries.size())
        return;
    const auto current = p->entries[(size_t) i];
    juce::PopupMenu m;
    m.addSectionHeader ("When this song ends");
    m.addItem (1, "Stop", true, current.transition == Transition::stop);
    m.addItem (2, "Play next song", true, current.transition == Transition::playNext);
    juce::PopupMenu cross;
    const double times[] = { 1.0, 2.0, 3.0, 5.0, 8.0, 10.0 };
    for (int t = 0; t < 6; ++t)
        cross.addItem (10 + t, juce::String (times[t], 0) + " s", true,
                       current.transition == Transition::crossfade && std::abs (current.crossfadeSeconds - times[t]) < 0.01);
    cross.addSeparator();
    cross.addItem (20, "Other time...");
    m.addSubMenu ("Crossfade into next song", cross, true, nullptr, current.transition == Transition::crossfade);
    juce::PopupMenu waits;
    const double waitTimes[] = { 1.0, 2.0, 3.0, 5.0, 10.0, 15.0, 30.0, 60.0 };
    for (int t = 0; t < 8; ++t)
        waits.addItem (40 + t, juce::String (waitTimes[t], 0) + " s", true,
                       current.transition == Transition::wait && std::abs (current.waitSeconds - waitTimes[t]) < 0.01);
    waits.addSeparator();
    waits.addItem (21, "Other time...");
    m.addSubMenu ("Wait, then play next song", waits, true, nullptr, current.transition == Transition::wait);
    juce::PopupMenu loopTimes;
    loopTimes.addSectionHeader ("Fade to next (Enter) takes");
    for (int t = 0; t < 6; ++t)
        loopTimes.addItem (30 + t, juce::String (times[t], 0) + " s", true,
                           current.transition == Transition::loopUntil && std::abs (current.crossfadeSeconds - times[t]) < 0.01);
    m.addSeparator();
    m.addItem (3, "Loop until GO", true, current.transition == Transition::loopUntil);
    m.addSubMenu ("Loop until GO, fade to next in", loopTimes, true, nullptr, false);

    juce::Component::SafePointer<PlaylistRows> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (chipBounds (i))),
                     [safe, i, current, times, waitTimes] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& c = safe->controller;
        if (result == 1) c.setTransition (i, Transition::stop, current.crossfadeSeconds);
        else if (result == 2) c.setTransition (i, Transition::playNext, current.crossfadeSeconds);
        else if (result >= 10 && result < 16) c.setTransition (i, Transition::crossfade, times[result - 10]);
        else if (result == 3) c.setTransition (i, Transition::loopUntil, current.crossfadeSeconds);
        else if (result >= 30 && result < 36) c.setTransition (i, Transition::loopUntil, times[result - 30]);
        else if (result >= 40 && result < 48) c.setTransition (i, Transition::wait, waitTimes[result - 40]);
        else if (result == 20 || result == 21)
        {
            const bool wait = result == 21;
            auto* w = new juce::AlertWindow (wait ? "Wait" : "Crossfade",
                                             wait ? "Seconds of silence before the next song (0.1 - 600):"
                                                  : "Crossfade time in seconds (0.1 - 60):", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("seconds", juce::String (wait ? current.waitSeconds : current.crossfadeSeconds, 1));
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, i, w, wait] (int ok)
            {
                if (safe != nullptr && ok == 1)
                    safe->controller.setTransition (i, wait ? Transition::wait : Transition::crossfade, w->getTextEditorContents ("seconds").getDoubleValue());
            }), true);
        }
    });
}

bool PlaylistRows::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith ("cue:");
}

void PlaylistRows::itemDragMove (const SourceDetails& d)
{
    dropAt = insertIndexAt (d.localPosition.y);
    repaint();
}

void PlaylistRows::itemDragExit (const SourceDetails&)
{
    dropAt = -1;
    repaint();
}

void PlaylistRows::itemDropped (const SourceDetails& d)
{
    const int id = d.description.toString().fromFirstOccurrenceOf ("cue:", false, false).getIntValue();
    controller.addToPlaylist (id, insertIndexAt (d.localPosition.y));
    dropAt = -1;
    repaint();
}

void PlaylistRows::fileDragMove (const juce::StringArray&, int, int y)
{
    dropAt = insertIndexAt (y);
    repaint();
}

void PlaylistRows::fileDragExit (const juce::StringArray&)
{
    dropAt = -1;
    repaint();
}

void PlaylistRows::filesDropped (const juce::StringArray& dropped, int, int y)
{
    // each file (in name order) goes onto the next empty button (from the shown bank on) and into the playlist
    auto files = dropped;
    std::sort (files.begin(), files.end(), [] (const juce::String& a, const juce::String& b)
               { return juce::File (a).getFileName().compareNatural (juce::File (b).getFileName()) < 0; });
    int at = insertIndexAt (y);
    auto& show = controller.getShow();
    const int startBank = getBank ? getBank() : 0;
    for (auto& path : files)
    {
        int free = -1;
        for (int b = 0; b < (int) show.banks.size() && free < 0; ++b)
            for (int s = 0; s < Show::cuesPerBank && free < 0; ++s)
            {
                const int id = Show::cueId ((startBank + b) % (int) show.banks.size(), s);
                if (show.cue (id).isEmpty())
                    free = id;
            }
        if (free < 0)
            break;
        controller.assignFile (free, juce::File (path));
        controller.addToPlaylist (free, at++);
    }
    dropAt = -1;
    repaint();
}

// ---------------------------------------------------------------------------------------------

PlaylistPanel::PlaylistPanel (ShowController& c) : controller (c), rows (c)
{
    picker.onChange = [this]
    {
        if (picker.getSelectedItemIndex() >= 0)
            controller.setPlaylistIndex (picker.getSelectedItemIndex());
    };
    picker.setWantsKeyboardFocus (false);
    addAndMakeVisible (picker);

    menuButton.setTooltip ("New, rename or delete playlists");
    menuButton.setWantsKeyboardFocus (false);
    menuButton.onClick = [this] { showPlaylistMenu(); };
    addAndMakeVisible (menuButton);

    viewport.setViewedComponent (&rows, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    nextLabel.setColour (juce::Label::textColourId, Look::textDim);
    nextLabel.setFont (Look::font (12.0f));
    addAndMakeVisible (nextLabel);

    goButton.setColour (juce::TextButton::buttonColourId, Look::goGreen);
    goButton.getProperties().set ("big", true);
    goButton.setTooltip ("Play the next song in the playlist (Space)");
    goButton.setWantsKeyboardFocus (false);
    goButton.onClick = [this] { controller.go(); };
    addAndMakeVisible (goButton);

    fadeNextButton.setTooltip ("Crossfade into the next song now (Enter)");
    fadeNextButton.setWantsKeyboardFocus (false);
    fadeNextButton.onClick = [this] { controller.fadeToNext(); };
    addAndMakeVisible (fadeNextButton);
    startTimerHz (5);

    controller.addChangeListener (this);
    refresh();
}

PlaylistPanel::~PlaylistPanel()
{
    controller.removeChangeListener (this);
}

void PlaylistPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void PlaylistPanel::refresh()
{
    auto& lists = controller.getShow().playlists;
    picker.clear (juce::dontSendNotification);
    for (int i = 0; i < (int) lists.size(); ++i)
        picker.addItem (lists[(size_t) i].name.isEmpty() ? "Playlist " + juce::String (i + 1) : lists[(size_t) i].name, i + 1);
    picker.setSelectedItemIndex (controller.getPlaylistIndex(), juce::dontSendNotification);

    juce::String next = "Next: ";
    const int n = controller.getNextEntry();
    auto* p = controller.currentPlaylist();
    if (p != nullptr && n >= 0 && n < (int) p->entries.size() && controller.getShow().isValid (p->entries[(size_t) n].cueId))
        next << controller.getShow().cue (p->entries[(size_t) n].cueId).name;
    else
        next << juce::String::fromUTF8 ("\xe2\x80\x94");
    shownLooping = controller.isLoopingUntilGo();
    if (shownLooping)
        next = juce::String::fromUTF8 ("\xe2\x86\xbb Looping \xe2\x80\x94 GO finishes it, Enter fades to next");
    nextLabel.setText (next, juce::dontSendNotification);
    goButton.setEnabled (n >= 0 || shownLooping);
    fadeNextButton.setEnabled (n >= 0);

    rows.setSize (viewport.getMaximumVisibleWidth() > 0 ? viewport.getMaximumVisibleWidth() : getWidth(), rows.contentHeight());
    rows.repaint();
}

void PlaylistPanel::showPlaylistMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "New playlist");
    m.addItem (2, "Rename playlist...");
    m.addItem (3, "Delete playlist", controller.getShow().playlists.size() > 1);
    juce::Component::SafePointer<PlaylistPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton), [safe] (int result)
    {
        if (safe == nullptr)
            return;
        auto& c = safe->controller;
        if (result == 1)
            c.addPlaylist ("Playlist " + juce::String ((int) c.getShow().playlists.size() + 1));
        else if (result == 2)
        {
            auto* w = new juce::AlertWindow ("Rename playlist", {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", c.currentPlaylist() != nullptr ? c.currentPlaylist()->name : juce::String());
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int ok)
            {
                if (safe == nullptr || ok != 1)
                    return;
                if (auto* p = safe->controller.currentPlaylist())
                {
                    p->name = w->getTextEditorContents ("name").trim();
                    safe->controller.playlistEdited();
                }
            }), true);
        }
        else if (result == 3)
            c.removePlaylist (c.getPlaylistIndex());
    });
}

void PlaylistPanel::paint (juce::Graphics& g)
{
    g.fillAll (Look::sidePanel);
    g.setColour (Look::line);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
    g.setColour (juce::Colour (0xffc9d0d8));
    g.setFont (Look::condensed (18.0f));
    g.drawText ("PLAYLIST", 16, 12, 120, 24, juce::Justification::centredLeft);
    g.setColour (Look::line);
    g.fillRect (0, getHeight() - 140, getWidth() - 1, 1);
}

void PlaylistPanel::resized()
{
    auto r = getLocalBounds().withTrimmedRight (1);
    auto head = r.removeFromTop (48).reduced (12, 10).withTrimmedLeft (96);
    menuButton.setBounds (head.removeFromRight (30));
    head.removeFromRight (4);
    picker.setBounds (head);

    auto bottom = r.removeFromBottom (140).reduced (12);
    nextLabel.setBounds (bottom.removeFromTop (22));
    bottom.removeFromTop (6);
    fadeNextButton.setBounds (bottom.removeFromBottom (30));
    bottom.removeFromBottom (6);
    goButton.setBounds (bottom);

    viewport.setBounds (r);
    refresh();
}

void PlaylistPanel::timerCallback()
{
    if (controller.isLoopingUntilGo() != shownLooping)
        refresh();
}
