#include "CueGrid.h"
#include "Look.h"

CueGrid::CueGrid (ShowController& c) : controller (c)
{
    controller.addChangeListener (this);
    startTimerHz (20);
}

CueGrid::~CueGrid()
{
    controller.removeChangeListener (this);
}

void CueGrid::setBank (int b)
{
    bank = juce::jlimit (0, (int) controller.getShow().banks.size() - 1, b);
    select (Show::cueId (bank, Show::slotOf (selected)));
}

void CueGrid::select (int id)
{
    if (! controller.getShow().isValid (id))
        return;
    selected = id;
    repaint();
    if (onSelect)
        onSelect (id);
}

juce::KeyPress CueGrid::defaultKey (int slot)
{
    static const char* rows[Show::rows] = { "123456", "QWERTY", "ASDFGH", "ZXCVBN" };
    const int row = slot / Show::columns, column = slot % Show::columns;
    if (row < 0 || row >= Show::rows)
        return {};
    return juce::KeyPress (rows[row][column]);
}

juce::KeyPress CueGrid::keyFor (int slot) const
{
    const auto& cue = controller.getShow().cue (Show::cueId (bank, slot));
    if (cue.hotkey.isNotEmpty())
        return juce::KeyPress::createFromDescription (cue.hotkey);
    return slotKey ? slotKey (slot) : defaultKey (slot);
}

juce::String CueGrid::formatTime (double seconds)
{
    const int total = (int) std::floor (std::max (0.0, seconds));
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

juce::String CueGrid::formatTimeTenths (double seconds)
{
    const int tenths = (int) std::floor (std::max (0.0, seconds) * 10.0 + 0.5);
    return juce::String (tenths / 600) + ":" + juce::String ((tenths / 10) % 60).paddedLeft ('0', 2) + "." + juce::String (tenths % 10);
}

juce::String CueGrid::outputLabel (const OutputRoute& route)
{
    const int first = route.firstChannel + 1;
    return route.stereo ? "Out " + juce::String (first) + juce::String::fromUTF8 ("\xe2\x80\x93") + juce::String (first + 1)
                        : "Out " + juce::String (first);
}

juce::Rectangle<float> CueGrid::slotBounds (int slot) const
{
    const float gap = 10.0f;
    const auto area = getLocalBounds().toFloat();
    const float w = (area.getWidth() - gap * (Show::columns - 1)) / Show::columns;
    const float h = (area.getHeight() - gap * (Show::rows - 1)) / Show::rows;
    const int row = slot / Show::columns, column = slot % Show::columns;
    return { column * (w + gap), row * (h + gap), w, h };
}

int CueGrid::slotAt (juce::Point<int> p) const
{
    for (int s = 0; s < Show::cuesPerBank; ++s)
        if (slotBounds (s).contains (p.toFloat()))
            return s;
    return -1;
}

void CueGrid::paint (juce::Graphics& g)
{
    for (int s = 0; s < Show::cuesPerBank; ++s)
        paintSlot (g, s);
}

void CueGrid::paintSlot (juce::Graphics& g, int slot)
{
    const int id = Show::cueId (bank, slot);
    const auto& cue = controller.getShow().cue (id);
    auto r = slotBounds (slot);
    const bool empty = cue.isEmpty();
    const bool isSelected = id == selected;

    PlaybackEngine::VoiceInfo voice;
    bool playing = false;
    for (auto& v : controller.getEngine().getVoices())
        if (v.cueId == id)
        {
            voice = v;
            playing = true;
            break;
        }

    if (empty)
    {
        g.setColour (Look::edge);
        const float dashes[] = { 5.0f, 4.0f };
        juce::Path p;
        p.addRoundedRectangle (r.reduced (0.5f), 10.0f);
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
        g.fillPath (dashed);
    }
    else
    {
        // the button's colour tints all of it; brighter while it plays
        // (Self Destruct: a spent button is solid black)
        const auto colour = Look::colourOf (cue);
        g.setColour (Look::shade (colour, cue.spent ? 1.0f : playing ? 0.62f : 0.26f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (playing ? Look::text : editMode ? Look::amber.withAlpha (0.6f) : cue.spent ? Look::edge : colour.withAlpha (0.45f));
        g.drawRoundedRectangle (r.reduced (playing ? 1.5f : 0.5f), 10.0f, playing ? 3.0f : 1.0f);

        // colour bar
        juce::Path bar;
        bar.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), 6.0f, 10.0f, 10.0f, true, true, false, false);
        g.setColour (colour);
        g.fillPath (bar);
    }
    if (isSelected)
    {
        g.setColour (Look::blue.withAlpha (0.8f));
        g.drawRoundedRectangle (r.expanded (2.5f), 12.0f, 2.0f);
    }
    if (dropSlot == slot)
    {
        g.setColour (Look::green.withAlpha (0.25f));
        g.fillRoundedRectangle (r, 10.0f);
    }

    auto inner = r.reduced (10.0f, 10.0f).withTrimmedTop (2.0f);

    // top row: key + length
    auto top = inner.removeFromTop (18.0f);
    const auto key = keyFor (slot);
    if (key.isValid() && ! empty)
    {
        auto keyText = key.getTextDescriptionWithIcons().toUpperCase();
        const float kw = Look::textWidth (Look::mono (11.0f), keyText) + 10.0f;
        auto kr = top.removeFromLeft (kw);
        g.setColour (Look::ground);
        g.fillRoundedRectangle (kr, 4.0f);
        g.setColour (juce::Colour (0xffc9d0d8));
        g.setFont (Look::mono (11.0f));
        g.drawText (keyText, kr, juce::Justification::centred);
    }

    const auto onFill = empty ? Look::text : Look::textOn (Look::shade (Look::colourOf (cue), cue.spent ? 1.0f : playing ? 0.62f : 0.26f));
    auto audio = controller.getAudio (id);
    juce::String length;
    if (playing)
        length = "-" + formatTime (std::ceil (std::max (0.0, voice.length - voice.position)));   // counts down while it plays
    else if (controller.isLoading (id))
        length = "loading";
    else if (audio != nullptr)
        length = formatTime (cue.playLength (audio->seconds()));
    else if (! empty)
        length = "missing";
    g.setColour (length == "missing" ? juce::Colour (0xffe5484d) : playing ? onFill : onFill.withAlpha (0.7f));
    g.setFont (Look::mono (playing ? 15.0f : 12.0f, playing));
    g.drawText (length, top, juce::Justification::centredRight);

    // name
    inner.removeFromTop (6.0f);
    auto badges = inner.removeFromBottom (16.0f);
    g.setColour (empty ? Look::textFaint : onFill);
    g.setFont (Look::font (empty ? 13.0f : 15.0f, ! empty));
    g.drawFittedText (empty ? juce::String::fromUTF8 ("Empty \xe2\x80\x94 drop a file or double-click") : cue.name,
                      inner.toNearestInt(), juce::Justification::topLeft, 3, 0.9f);

    if (! empty)
    {
        auto badge = [&] (const juce::String& text, juce::Colour back, juce::Colour fore)
        {
            g.setFont (Look::mono (10.0f));
            const float w = Look::textWidth (g.getCurrentFont(), text) + 10.0f;
            auto b = badges.removeFromLeft (w);
            badges.removeFromLeft (4.0f);
            g.setColour (back);
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (fore);
            g.drawText (text, b, juce::Justification::centred);
        };
        badge (outputLabel (cue.output), Look::line, juce::Colour (0xffc9d0d8));
        if (cue.hasPlugins())
            badge ("FX", juce::Colour (0xff2b2540), juce::Colour (0xffc6b8f5));
        if (cue.loop)
            badge ("LOOP", juce::Colour (0xff172636), juce::Colour (0xffa9d0f7));
    }

    if (playing && voice.length > 0.0)
    {
        auto bar = r.reduced (1.0f).removeFromBottom (5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (bar);
        g.setColour (voice.stopping ? Look::textDim : onFill);
        g.fillRect (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, voice.position / voice.length)));
    }
}

void CueGrid::timerCallback()
{
    if (! controller.getEngine().getVoices().empty() || isMouseOver())
        repaint();
}

void CueGrid::mouseDown (const juce::MouseEvent& e)
{
    const int slot = slotAt (e.getPosition());
    if (slot < 0)
        return;
    const int id = Show::cueId (bank, slot);
    select (id);
    if (e.mods.isPopupMenu())
        showMenu (id);
    else if (controller.getShow().cue (id).isEmpty())
        return;   // empty: just selected (double-click chooses a file)
    else if (editMode || e.mods.isCommandDown())
        return;   // edit mode / Ctrl: select and drag, no playing
    else
        controller.trigger (id);
}

void CueGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int slot = slotAt (e.getPosition());
    if (slot >= 0 && ! e.mods.isPopupMenu() && controller.getShow().cue (Show::cueId (bank, slot)).isEmpty())
        chooseFile (Show::cueId (bank, slot));
}

void CueGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (! (editMode || e.mods.isCommandDown()) || e.getDistanceFromDragStart() < 6)
        return;
    const int slot = slotAt (e.getMouseDownPosition());
    if (slot < 0 || controller.getShow().cue (Show::cueId (bank, slot)).isEmpty())
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this); container != nullptr && ! container->isDragAndDropActive())
    {
        const auto r = slotBounds (slot).toNearestInt();
        auto image = createComponentSnapshot (r).convertedToFormat (juce::Image::ARGB);
        image.multiplyAllAlphas (0.7f);
        container->startDragging ("cue:" + juce::String (Show::cueId (bank, slot)), this, juce::ScaledImage (image), true);
    }
}

void CueGrid::showMenu (int id)
{
    const bool empty = controller.getShow().cue (id).isEmpty();
    const auto& cue = controller.getShow().cue (id);
    juce::PopupMenu colours;
    for (int i : Look::paletteOrder())
        colours.addColouredItem (100 + i, Look::cueColourName (i), Look::cueColour (i), true,
                                 cue.customColour == 0 && cue.colour == i);
    colours.addSeparator();
    colours.addItem (4, "Custom colour...", true, cue.customColour != 0);
    juce::PopupMenu m;
    m.addItem (1, empty ? "Choose file..." : "Replace file...");
    // Add to playlist: straight in when there is one, else a submenu (ids 1000 + playlist)
    auto& lists = controller.getShow().playlists;
    if (lists.size() == 1)
        m.addItem (1000, "Add to playlist", ! empty);
    else
    {
        juce::PopupMenu add;
        for (int pl = 0; pl < (int) lists.size(); ++pl)
            add.addItem (1000 + pl, lists[(size_t) pl].displayName (pl));
        m.addSubMenu ("Add to playlist", add, ! empty);
    }
    m.addSubMenu ("Colour", colours);
    m.addItem (5, "Reset colour (Self Destruct)", cue.spent);
    m.addSeparator();
    // what a press does: play once and stop (default), or loop until pressed again / another button on its output / Stop all
    m.addItem (6, "Play once", ! empty, ! cue.loop);
    m.addItem (7, cue.hasLoopPoints() ? "Loop (by its loop points)" : "Loop (the whole clip)", ! empty, cue.loop);
    m.addSeparator();
    m.addItem (2, "Clear button", ! empty);
    juce::Component::SafePointer<CueGrid> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, id] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& c = safe->controller;
        if (result == 1)
            safe->chooseFile (id);
        else if (result == 2)
            c.clearCue (id);
        else if (result >= 1000)
            c.addToPlaylist (result - 1000, id);
        else if (result == 5)
            c.resetColour (id);
        else if (result == 6 || result == 7)
        {
            c.getShow().cue (id).loop = result == 7;
            c.cueChanged (id);
        }
        else if (result == 4)
            Look::showColourPicker (safe->localAreaToGlobal (safe->slotBounds (Show::slotOf (id)).toNearestInt()), c, id);
        else if (result >= 100)
        {
            c.getShow().cue (id).colour = result - 100;
            c.getShow().cue (id).customColour = 0;
            c.cueChanged (id);
        }
    });
}

void CueGrid::chooseFile (int id)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose an audio file", juce::File(), controller.getLoader().wildcards());
    juce::Component::SafePointer<CueGrid> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, id] (const juce::FileChooser& fc)
    {
        if (safe != nullptr && fc.getResult() != juce::File())
            safe->controller.assignFile (id, fc.getResult());
    });
}

void CueGrid::fileDragMove (const juce::StringArray&, int x, int y)
{
    const int s = slotAt ({ x, y });
    if (s != dropSlot)
    {
        dropSlot = s;
        repaint();
    }
}

void CueGrid::fileDragExit (const juce::StringArray&)
{
    dropSlot = -1;
    repaint();
}

void CueGrid::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropSlot = -1;
    const int slot = slotAt ({ x, y });
    if (slot < 0)
        return;
    const auto audio = audioFilesIn (files);
    if (audio.size() == 1 && ! juce::File (files[0]).isDirectory())
    {
        controller.assignFile (Show::cueId (bank, slot), audio[0]);   // one file: onto that button, in use or not
        select (Show::cueId (bank, slot));
    }
    else if (audio.size() > 0)
    {
        // several: the empty buttons from here on, in name order, on into the next banks
        const int placed = controller.assignFiles (audio, Show::cueId (bank, slot));
        if (placed < audio.size())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Drop files",
                                                    juce::String (audio.size() - placed) + " of the files didn't fit: every button in all "
                                                        + juce::String (ShowController::maxBanks) + " banks is in use.");
    }
    repaint();
}

juce::Array<juce::File> CueGrid::audioFilesIn (const juce::StringArray& paths) const
{
    const auto patterns = juce::StringArray::fromTokens (controller.getLoader().wildcards(), ";", {});
    auto isAudio = [&] (const juce::File& f)
    {
        for (auto& p : patterns)
            if (f.getFileName().matchesWildcard (p.trim(), true))
                return true;
        return false;
    };
    juce::Array<juce::File> list;
    for (auto& path : paths)
    {
        const juce::File f (path);
        if (f.isDirectory())
        {
            for (auto& child : f.findChildFiles (juce::File::findFiles, false))
                if (isAudio (child))
                    list.add (child);
        }
        else if (isAudio (f))
            list.add (f);
    }
    return list;
}

bool CueGrid::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith ("cue:");
}

void CueGrid::itemDragMove (const SourceDetails& d)
{
    const int s = slotAt (d.localPosition.toInt());
    if (s != dropSlot)
    {
        dropSlot = s;
        repaint();
    }
}

void CueGrid::itemDragExit (const SourceDetails&)
{
    dropSlot = -1;
    repaint();
}

void CueGrid::itemDropped (const SourceDetails& d)
{
    // a button dragged onto another: they swap places (onto an empty one: it moves there)
    dropSlot = -1;
    const int from = d.description.toString().fromFirstOccurrenceOf ("cue:", false, false).getIntValue();
    const int slot = slotAt (d.localPosition.toInt());
    if (slot >= 0)
    {
        const int to = Show::cueId (bank, slot);
        controller.swapCues (from, to);
        select (to);
    }
    repaint();
}
