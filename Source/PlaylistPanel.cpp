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

    /** Name of the button an entry of the playlist plays, "" if none. */
    juce::String entryName (ShowController& c, int playlist, int entry)
    {
        auto* p = c.playlistAt (playlist);
        if (p == nullptr || entry < 0 || entry >= (int) p->entries.size() || ! c.getShow().isValid (p->entries[(size_t) entry].cueId))
            return {};
        return c.getShow().cue (p->entries[(size_t) entry].cueId).name;
    }

    const juce::String dash = juce::String::fromUTF8 ("\xe2\x80\x94");
}

// ---------------------------------------------------------------------------------------------

PlaylistRows::PlaylistRows (ShowController& c, int playlistUid) : controller (c), uid (playlistUid) {}

int PlaylistRows::entryCount()
{
    auto* p = playlist();
    return p == nullptr ? 0 : (int) p->entries.size();
}

int PlaylistRows::contentHeight() const
{
    auto* p = controller.playlistAt (index());
    const int n = p == nullptr ? 0 : (int) p->entries.size();
    return std::max (120, n * (rowHeight + chipHeight) + 8);
}

juce::Rectangle<int> PlaylistRows::rowBounds (int i) const
{
    return { 8, i * (rowHeight + chipHeight), getWidth() - 16, rowHeight };
}

juce::Rectangle<int> PlaylistRows::chipBounds (int i)
{
    auto* p = playlist();
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
    auto* p = playlist();
    if (p == nullptr || p->entries.empty())
    {
        g.setColour (Look::textFaint);
        g.setFont (Look::font (13.0f));
        g.drawFittedText ("Empty playlist.\nTurn on EDIT and drag buttons here,\nright-click a button > Add to playlist,\nor drop audio files.",
                          getLocalBounds().reduced (12).withHeight (80), juce::Justification::topLeft, 4);
    }
    else
    {
        const int onAir = controller.getOnAirEntry (index()), next = controller.getNextEntry (index());
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
    controller.selectPlaylist (index());
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
    controller.setNextEntry (index(), i);
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
        controller.moveEntry (index(), dragFrom, dropAt > dragFrom ? dropAt - 1 : dropAt);
    reordering = false;
    dragFrom = dropAt = -1;
    repaint();
}

void PlaylistRows::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = rowAt (e.y);
    if (i >= 0 && i < entryCount() && chipAt (e.getPosition()) < 0)
        controller.playEntry (index(), i);
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
        const int pl = safe->index();
        if (result == 1) c.playEntry (pl, i);
        else if (result == 2) c.setNextEntry (pl, i);
        else if (result == 3) c.removeEntry (pl, i);
    });
}

void PlaylistRows::showTransitionMenu (int i)
{
    auto* p = playlist();
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
        const int pl = safe->index();
        if (result == 1) c.setTransition (pl, i, Transition::stop, current.crossfadeSeconds);
        else if (result == 2) c.setTransition (pl, i, Transition::playNext, current.crossfadeSeconds);
        else if (result >= 10 && result < 16) c.setTransition (pl, i, Transition::crossfade, times[result - 10]);
        else if (result == 3) c.setTransition (pl, i, Transition::loopUntil, current.crossfadeSeconds);
        else if (result >= 30 && result < 36) c.setTransition (pl, i, Transition::loopUntil, times[result - 30]);
        else if (result >= 40 && result < 48) c.setTransition (pl, i, Transition::wait, waitTimes[result - 40]);
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
                    safe->controller.setTransition (safe->index(), i, wait ? Transition::wait : Transition::crossfade,
                                                    w->getTextEditorContents ("seconds").getDoubleValue());
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
    controller.addToPlaylist (index(), id, insertIndexAt (d.localPosition.y));
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
    addFiles (controller, index(), dropped, insertIndexAt (y), getBank ? getBank() : 0);
    dropAt = -1;
    repaint();
}

void PlaylistRows::addFiles (ShowController& controller, int playlist, const juce::StringArray& dropped, int at, int startBank)
{
    // each file (in name order) goes onto the next empty button (from the shown bank on) and into the playlist
    auto files = dropped;
    std::sort (files.begin(), files.end(), [] (const juce::String& a, const juce::String& b)
               { return juce::File (a).getFileName().compareNatural (juce::File (b).getFileName()) < 0; });
    auto& show = controller.getShow();
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
        controller.addToPlaylist (playlist, free, at);
        if (at >= 0)
            ++at;
    }
}

// ---------------------------------------------------------------------------------------------

PlaylistView::PlaylistView (ShowController& c, int playlistUid) : controller (c), uid (playlistUid), rows (c, playlistUid)
{
    menuButton.setTooltip ("Rename or delete this playlist");
    menuButton.setWantsKeyboardFocus (false);
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
    goButton.setTooltip ("Play the next song in this playlist");
    goButton.setWantsKeyboardFocus (false);
    goButton.onClick = [this] { controller.selectPlaylist (index()); controller.go (index()); };
    addAndMakeVisible (goButton);

    fadeNextButton.setTooltip ("Crossfade into this playlist's next song now");
    fadeNextButton.setWantsKeyboardFocus (false);
    fadeNextButton.onClick = [this] { controller.selectPlaylist (index()); controller.fadeToNext (index()); };
    addAndMakeVisible (fadeNextButton);

    setWantsKeyboardFocus (true);
    addMouseListener (this, true);   // any click gives this window's keys to the view (then on to the main window)
    startTimerHz (5);
    controller.addChangeListener (this);
    refresh();
}

PlaylistView::~PlaylistView()
{
    controller.removeChangeListener (this);
}

void PlaylistView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void PlaylistView::refresh()
{
    const int pl = index();
    const int n = controller.getNextEntry (pl);
    juce::String next = "Next: " + (n >= 0 ? entryName (controller, pl, n) : dash);
    shownLooping = controller.isLoopingUntilGo (pl);
    if (shownLooping)
        next = juce::String::fromUTF8 ("\xe2\x86\xbb Looping \xe2\x80\x94 GO finishes it, FADE TO NEXT fades");
    nextLabel.setText (next, juce::dontSendNotification);
    goButton.setEnabled (n >= 0 || shownLooping);
    fadeNextButton.setEnabled (n >= 0);

    rows.setSize (viewport.getMaximumVisibleWidth() > 0 ? viewport.getMaximumVisibleWidth() : getWidth(), rows.contentHeight());
    rows.repaint();
    repaint (0, 0, getWidth(), 48);
}

void PlaylistView::paint (juce::Graphics& g)
{
    g.fillAll (Look::sidePanel);
    const int pl = index();
    auto* p = controller.playlistAt (pl);
    const bool selected = pl == controller.getSelectedPlaylist();
    g.setColour (juce::Colour (0xffc9d0d8));
    g.setFont (Look::condensed (18.0f));
    g.drawText (p != nullptr ? p->displayName (pl).toUpperCase() : juce::String(), 16, 12, getWidth() - 64, 24,
                juce::Justification::centredLeft, true);
    if (selected)
    {
        g.setColour (Look::blue);
        g.fillRect (0, 0, getWidth(), 3);
    }
    g.setColour (Look::line);
    g.fillRect (0, getHeight() - 140, getWidth(), 1);
}

void PlaylistView::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (48).reduced (12, 10);
    menuButton.setBounds (head.removeFromRight (30));
    menuButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Rename playlist...");
        m.addItem (2, "Delete playlist", controller.getShow().playlists.size() > 1);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton),
                         [safe = juce::Component::SafePointer<PlaylistView> (this)] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto& c = safe->controller;
            const int pl = safe->index();
            if (result == 2)
            {
                c.removePlaylist (pl);   // its window closes with it
                return;
            }
            auto* w = new juce::AlertWindow ("Rename playlist", {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", c.playlistAt (pl) != nullptr ? c.playlistAt (pl)->name : juce::String());
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int ok)
            {
                if (safe != nullptr && ok == 1)
                    safe->controller.renamePlaylist (safe->index(), w->getTextEditorContents ("name"));
            }), true);
        });
    };

    auto bottom = r.removeFromBottom (140).reduced (12);
    nextLabel.setBounds (bottom.removeFromTop (22));
    bottom.removeFromTop (6);
    fadeNextButton.setBounds (bottom.removeFromBottom (30));
    bottom.removeFromBottom (6);
    goButton.setBounds (bottom);

    viewport.setBounds (r);
    refresh();
}

void PlaylistView::timerCallback()
{
    if (controller.isLoopingUntilGo (index()) != shownLooping)
        refresh();
}

// ---------------------------------------------------------------------------------------------

/** One playlist in the left column: name, what's on air / next, GO, open-in-window and menu buttons.
    Click = select it (Space / Enter play the selected one), double-click = open its window, right-click = menu.
    A button or audio files dropped on it go to the end. */
class PlaylistPanel::Box : public juce::Component,
                           public juce::DragAndDropTarget,
                           public juce::FileDragAndDropTarget
{
public:
    Box (PlaylistPanel& p, int playlistUid) : uid (playlistUid), panel (p), controller (p.controller)
    {
        goButton.setColour (juce::TextButton::buttonColourId, Look::goGreen);
        goButton.getProperties().set ("big", true);
        goButton.setTooltip ("Play this playlist's next song");
        goButton.setWantsKeyboardFocus (false);
        goButton.onClick = [this] { controller.selectPlaylist (index()); controller.go (index()); };
        addAndMakeVisible (goButton);

        windowButton.setTooltip ("Open this playlist in its own window (all its songs and transitions)");
        windowButton.setWantsKeyboardFocus (false);
        windowButton.onClick = [this] { panel.openWindow (index()); };
        addAndMakeVisible (windowButton);

        menuButton.setTooltip ("GO, fade to next, rename, delete...");
        menuButton.setWantsKeyboardFocus (false);
        menuButton.onClick = [this] { panel.showMenu (index(), &menuButton); };
        addAndMakeVisible (menuButton);
    }

    const int uid;

    int index() const { return controller.playlistIndexOf (uid); }

    /** Updates GO and repaints if what the box shows has changed. */
    void update()
    {
        const int pl = index();
        const bool looping = controller.isLoopingUntilGo (pl);
        const int next = controller.getNextEntry (pl), onAir = controller.getOnAirEntry (pl);
        auto* p = controller.playlistAt (pl);
        const auto state = juce::String (pl) + "|" + (p != nullptr ? p->displayName (pl) + "|" + juce::String ((int) p->entries.size()) : juce::String())
                         + "|" + juce::String (next) + entryName (controller, pl, next) + "|" + juce::String (onAir) + entryName (controller, pl, onAir)
                         + "|" + juce::String ((int) looping) + juce::String ((int) (pl == controller.getSelectedPlaylist()));
        goButton.setEnabled (next >= 0 || looping);
        if (state != shown)
        {
            shown = state;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const int pl = index();
        auto* p = controller.playlistAt (pl);
        if (p == nullptr)
            return;
        const bool selected = pl == controller.getSelectedPlaylist();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (dragOver ? juce::Colour (0xff172636) : Look::panel);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (dragOver ? Look::blue : selected ? Look::blue.withAlpha (0.85f) : Look::edge);
        g.drawRoundedRectangle (r, 7.0f, selected || dragOver ? 2.0f : 1.0f);

        auto in = getLocalBounds().reduced (10, 6);
        auto head = in.removeFromTop (26).withTrimmedRight (64);
        g.setColour (selected ? Look::text : juce::Colour (0xffc9d0d8));
        g.setFont (Look::condensed (17.0f));
        const auto name = p->displayName (pl).toUpperCase();
        const int nameWidth = std::min (head.getWidth(), (int) Look::textWidth (g.getCurrentFont(), name) + 2);
        g.drawText (name, head.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);
        g.setColour (Look::textFaint);
        g.setFont (Look::font (11.0f));
        const int n = (int) p->entries.size();
        g.drawText (juce::String::fromUTF8 ("  \xc2\xb7  ") + juce::String (n) + (n == 1 ? " song" : " songs"), head,
                    juce::Justification::centredLeft, true);

        in.removeFromTop (4);
        auto lines = in.withTrimmedRight (goWidth + 8);
        auto line = [&] (const juce::String& tag, juce::Colour tagColour, juce::Colour tagText, const juce::String& text, juce::Colour textColour)
        {
            auto row = lines.removeFromTop (lines.getHeight() / 2 + (lines.getHeight() % 2));
            if (row.getHeight() > 24)
                row = row.withSizeKeepingCentre (row.getWidth(), 24);
            g.setFont (Look::font (9.5f, true));
            auto t = row.removeFromLeft (46).withSizeKeepingCentre (44, 15).toFloat();
            g.setColour (tagColour);
            g.fillRoundedRectangle (t, 3.0f);
            g.setColour (tagText);
            g.drawText (tag, t, juce::Justification::centred);
            g.setColour (textColour);
            g.setFont (Look::font (13.0f));
            g.drawText (text, row.withTrimmedLeft (6), juce::Justification::centredLeft, true);
        };
        const int onAir = controller.getOnAirEntry (pl), next = controller.getNextEntry (pl);
        if (onAir >= 0)
            line ("ON AIR", Look::amber, juce::Colour (0xff1a1206), entryName (controller, pl, onAir), juce::Colour (0xfff5c77e));
        else
            line ("ON AIR", Look::raised, Look::textFaint, dash, Look::textFaint);
        if (controller.isLoopingUntilGo (pl))
            line ("NEXT", Look::goGreen, juce::Colours::white, juce::String::fromUTF8 ("\xe2\x86\xbb looping, GO finishes it"), Look::textDim);
        else if (next >= 0)
            line ("NEXT", Look::goGreen, juce::Colours::white, entryName (controller, pl, next), Look::text);
        else
            line ("NEXT", Look::raised, Look::textFaint, n == 0 ? juce::String ("drop buttons here") : dash, Look::textFaint);
    }

    void resized() override
    {
        auto in = getLocalBounds().reduced (10, 6);
        auto head = in.removeFromTop (26);
        menuButton.setBounds (head.removeFromRight (28).reduced (0, 1));
        head.removeFromRight (4);
        windowButton.setBounds (head.removeFromRight (28).reduced (0, 1));
        in.removeFromTop (4);
        goButton.setBounds (in.removeFromRight (goWidth));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        controller.selectPlaylist (index());
        if (e.mods.isPopupMenu())
            panel.showMenu (index(), nullptr);
    }

    /** Dragging the title takes the playlist along, onto the Run of Show window. */
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getMouseDownY() > 32 || e.getDistanceFromDragStart() < 6)
            return;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this); container != nullptr && ! container->isDragAndDropActive())
        {
            auto image = createComponentSnapshot (getLocalBounds().withHeight (36)).convertedToFormat (juce::Image::ARGB);
            image.multiplyAllAlphas (0.7f);
            container->startDragging ("playlist:" + juce::String (index()), this, juce::ScaledImage (image), true);
        }
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { panel.openWindow (index()); }

    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("cue:"); }
    void itemDragEnter (const SourceDetails&) override { setDragOver (true); }
    void itemDragExit (const SourceDetails&) override { setDragOver (false); }
    void itemDropped (const SourceDetails& d) override
    {
        setDragOver (false);
        controller.addToPlaylist (index(), d.description.toString().fromFirstOccurrenceOf ("cue:", false, false).getIntValue());
    }

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { setDragOver (true); }
    void fileDragExit (const juce::StringArray&) override { setDragOver (false); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        setDragOver (false);
        PlaylistRows::addFiles (controller, index(), files, -1, panel.getBank ? panel.getBank() : 0);
    }

private:
    void setDragOver (bool on)
    {
        dragOver = on;
        repaint();
    }

    static constexpr int goWidth = 70;
    PlaylistPanel& panel;
    ShowController& controller;
    juce::TextButton goButton { "GO" }, windowButton { juce::String::fromUTF8 ("\xe2\xa7\x89") },
                     menuButton { juce::String::fromUTF8 ("\xe2\x8b\xaf") };
    juce::String shown;
    bool dragOver = false;
};

/** A playlist in its own window. It doesn't block the main window; keys typed in it work as they do there. */
class PlaylistPanel::Window : public juce::DocumentWindow
{
public:
    Window (PlaylistPanel& p, int playlistUid)
        : juce::DocumentWindow ("Playlist", Look::panel, juce::DocumentWindow::allButtons), panel (p), uid (playlistUid)
    {
        auto* view = new PlaylistView (p.controller, playlistUid);
        view->onKey = [&p] (const juce::KeyPress& key) { return p.onKey != nullptr && p.onKey (key); };
        view->getRows().getBank = [&p] { return p.getBank ? p.getBank() : 0; };
        setUsingNativeTitleBar (true);
        setContentOwned (view, false);
        setResizable (true, false);
        setResizeLimits (300, 360, 1200, 2000);
        setAlwaysOnTop (true);   // stays in view while the main window is used
        auto screen = p.getScreenBounds();
        setBounds (screen.getRight() + 12, screen.getY(), 380, std::max (480, std::min (760, screen.getHeight())));
        if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (screen))
            setBounds (getBounds().constrainedWithin (display->userArea));
        updateTitle();
        setVisible (true);
    }

    void updateTitle()
    {
        const int pl = panel.controller.playlistIndexOf (uid);
        if (auto* p = panel.controller.playlistAt (pl))
            setName (p->displayName (pl) + juce::String::fromUTF8 (" \xe2\x80\x94 Stinger"));
    }

    void closeButtonPressed() override
    {
        // the panel owns the window: let it go after this call has returned
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PlaylistPanel> (&panel), id = uid]
                                         { if (safe != nullptr) safe->windows.erase (id); });
    }

private:
    PlaylistPanel& panel;
    const int uid;
};

// ---------------------------------------------------------------------------------------------

PlaylistPanel::PlaylistPanel (ShowController& c) : controller (c)
{
    addButton.setTooltip ("New playlist");
    addButton.setWantsKeyboardFocus (false);
    addButton.onClick = [this]
    {
        controller.addPlaylist ({});
        refresh();
        if (! boxes.isEmpty())
            viewport.setViewPosition (0, boxHolder.getHeight());   // show the new one at the bottom
    };
    addAndMakeVisible (addButton);

    viewport.setViewedComponent (&boxHolder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    startTimerHz (5);
    controller.addChangeListener (this);
    refresh();
}

PlaylistPanel::~PlaylistPanel()
{
    windows.clear();
    controller.removeChangeListener (this);
}

void PlaylistPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void PlaylistPanel::timerCallback()
{
    for (auto* b : boxes)
        b->update();   // Loop until GO starts and ends without a change message
}

void PlaylistPanel::refresh()
{
    auto& lists = controller.getShow().playlists;
    bool same = boxes.size() == (int) lists.size();
    for (int i = 0; same && i < boxes.size(); ++i)
        same = boxes[i]->uid == lists[(size_t) i].uid;
    if (! same)
    {
        boxes.clear();
        for (auto& p : lists)
            boxHolder.addAndMakeVisible (boxes.add (new Box (*this, p.uid)));
        layoutBoxes();
    }
    for (auto* b : boxes)
        b->update();

    // windows of playlists that have gone close; the rest follow renames
    for (auto it = windows.begin(); it != windows.end();)
    {
        if (controller.playlistIndexOf (it->first) < 0)
            it = windows.erase (it);
        else
        {
            it->second->updateTitle();
            ++it;
        }
    }
}

void PlaylistPanel::layoutBoxes()
{
    const int width = viewport.getMaximumVisibleWidth() > 0 ? viewport.getMaximumVisibleWidth() : getWidth();
    boxHolder.setSize (width, boxGap + boxes.size() * (boxHeight + boxGap));
    for (int i = 0; i < boxes.size(); ++i)
        boxes[i]->setBounds (12, boxGap + i * (boxHeight + boxGap), width - 22, boxHeight);
}

void PlaylistPanel::openWindow (int playlist)
{
    auto* p = controller.playlistAt (playlist);
    if (p == nullptr)
        return;
    controller.selectPlaylist (playlist);
    auto& w = windows[p->uid];
    if (w == nullptr)
        w = std::make_unique<Window> (*this, p->uid);
    w->setVisible (true);
    w->toFront (true);
}

void PlaylistPanel::rename (int playlist)
{
    auto* p = controller.playlistAt (playlist);
    if (p == nullptr)
        return;
    const int uid = p->uid;
    auto* w = new juce::AlertWindow ("Rename playlist", {}, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", p->name);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<PlaylistPanel> (this), w, uid] (int ok)
    {
        if (safe != nullptr && ok == 1)
            safe->controller.renamePlaylist (safe->controller.playlistIndexOf (uid), w->getTextEditorContents ("name"));
    }), true);
}

void PlaylistPanel::showMenu (int playlist, juce::Component* target)
{
    auto* p = controller.playlistAt (playlist);
    if (p == nullptr)
        return;
    const int uid = p->uid;
    const bool looping = controller.isLoopingUntilGo (playlist);
    const bool hasNext = controller.getNextEntry (playlist) >= 0;
    juce::PopupMenu m;
    m.addSectionHeader (p->displayName (playlist));
    m.addItem (1, looping ? "GO (finish the loop)" : "GO", hasNext || looping);
    m.addItem (2, "Fade to next", hasNext);
    m.addSeparator();
    m.addItem (3, "Open in its own window");
    m.addItem (4, "Rename...");
    m.addItem (5, "Delete playlist", controller.getShow().playlists.size() > 1);
    m.addSeparator();
    m.addItem (6, "New playlist");
    auto options = target != nullptr ? juce::PopupMenu::Options().withTargetComponent (target)
                                     : juce::PopupMenu::Options().withMousePosition();
    m.showMenuAsync (options, [safe = juce::Component::SafePointer<PlaylistPanel> (this), uid] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& c = safe->controller;
        const int pl = c.playlistIndexOf (uid);
        if (pl < 0 && result != 6)
            return;
        switch (result)
        {
            case 1: c.go (pl); break;
            case 2: c.fadeToNext (pl); break;
            case 3: safe->openWindow (pl); break;
            case 4: safe->rename (pl); break;
            case 5:
            {
                auto* list = c.playlistAt (pl);
                if (list->entries.empty())
                    c.removePlaylist (pl);
                else
                    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon,
                                                      "Delete playlist", "Delete \"" + list->displayName (pl) + "\" and its "
                                                      + juce::String ((int) list->entries.size()) + " songs? (The buttons stay.)",
                                                      "Delete", "Cancel", safe.getComponent()),
                                                  [safe, uid] (int ok)
                                                  {
                                                      if (safe != nullptr && ok == 1)
                                                          safe->controller.removePlaylist (safe->controller.playlistIndexOf (uid));
                                                  });
                break;
            }
            case 6: c.addPlaylist ({}); break;
            default: break;
        }
    });
}

void PlaylistPanel::paint (juce::Graphics& g)
{
    g.fillAll (Look::sidePanel);
    g.setColour (Look::line);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
    g.setColour (juce::Colour (0xffc9d0d8));
    g.setFont (Look::condensed (18.0f));
    g.drawText ("PLAYLISTS", 16, 12, 140, 24, juce::Justification::centredLeft);
}

void PlaylistPanel::resized()
{
    auto r = getLocalBounds().withTrimmedRight (1);
    auto head = r.removeFromTop (48).reduced (12, 10);
    addButton.setBounds (head.removeFromRight (30));
    viewport.setBounds (r);
    layoutBoxes();
}
