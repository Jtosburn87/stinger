#include "NowPlaying.h"
#include "CueGrid.h"
#include "Look.h"

NowPlaying::NowPlaying (ShowController& c) : controller (c)
{
    fadeAll.setTooltip ("Fade out everything that's playing (each cue's stop fade)");
    stopAll.setTooltip ("Stop everything now (Esc)");
    stopAll.setColour (juce::TextButton::buttonColourId, Look::red);
    fadeAll.setWantsKeyboardFocus (false);
    stopAll.setWantsKeyboardFocus (false);
    fadeAll.onClick = [this] { controller.getEngine().stopAll (true); };
    stopAll.onClick = [this] { controller.getEngine().stopAll (false); };
    addAndMakeVisible (fadeAll);
    addAndMakeVisible (stopAll);
    startTimerHz (30);
}

void NowPlaying::setAllButtonsShown (bool shown)
{
    allButtonsShown = shown;
    fadeAll.setVisible (shown);
    stopAll.setVisible (shown);
    repaint();
}

void NowPlaying::resized()
{
    auto r = getLocalBounds().reduced (12).removeFromBottom (52);
    stopAll.setBounds (r.removeFromRight ((r.getWidth() - 8) * 13 / 23));
    r.removeFromRight (8);
    fadeAll.setBounds (r);
}

std::vector<OutputRoute> NowPlaying::usedOutputs() const
{
    std::vector<OutputRoute> list;
    for (auto& bank : controller.getShow().banks)
        for (auto& c : bank.cues)
            if (! c.isEmpty() && std::find (list.begin(), list.end(), c.output) == list.end())
                list.push_back (c.output);
    std::sort (list.begin(), list.end(), [] (auto& a, auto& b)
               { return a.firstChannel != b.firstChannel ? a.firstChannel < b.firstChannel : a.stereo < b.stereo; });
    return list;
}

void NowPlaying::timerCallback()
{
    auto& engine = controller.getEngine();
    for (int ch = 0; ch < PlaybackEngine::maxOutputs; ++ch)
        meter[(size_t) ch] = std::max (engine.takeOutputPeak (ch), meter[(size_t) ch] * 0.85f);
    repaint();
}

void NowPlaying::paint (juce::Graphics& g)
{
    g.fillAll (Look::sidePanel);
    g.setColour (Look::line);
    g.fillRect (0, 0, 1, getHeight());

    auto r = getLocalBounds().reduced (12, 0);
    r.removeFromTop (12);
    g.setColour (juce::Colour (0xffc9d0d8));
    g.setFont (Look::condensed (18.0f));
    g.drawText ("NOW PLAYING", r.removeFromTop (24), juce::Justification::centredLeft);
    r.removeFromTop (8);

    auto bottom = r.removeFromBottom (allButtonsShown ? 52 + 12 : 0);
    juce::ignoreUnused (bottom);

    // output meters
    const auto outputs = usedOutputs();
    const auto names = controller.getEngine().getOutputNames();
    const int meterRows = (int) std::min<size_t> (outputs.size(), 8);
    auto meters = r.removeFromBottom (meterRows > 0 ? 30 + meterRows * 22 : 0);
    if (meterRows > 0)
    {
        g.setColour (Look::line);
        g.fillRect (meters.removeFromTop (1).withX (0).withWidth (getWidth()));
        meters.removeFromTop (8);
        g.setColour (juce::Colour (0xffc9d0d8));
        g.setFont (Look::condensed (15.0f));
        g.drawText ("OUTPUTS", meters.removeFromTop (20), juce::Justification::centredLeft);
        for (int i = 0; i < meterRows; ++i)
        {
            auto row = meters.removeFromTop (22).reduced (0, 4);
            const auto& o = outputs[(size_t) i];
            g.setColour (Look::textDim);
            g.setFont (Look::font (13.0f));
            juce::String label = CueGrid::outputLabel (o);
            if (o.firstChannel >= names.size())
                label << " (off)";
            g.drawText (label, row.removeFromLeft (96), juce::Justification::centredLeft);
            for (int side = 0; side < o.numChannels(); ++side)
            {
                auto bar = (o.stereo ? (side == 0 ? row.withHeight (row.getHeight() / 2 - 1)
                                                  : row.withTrimmedTop (row.getHeight() / 2 + 1))
                                     : row).toFloat();
                g.setColour (juce::Colour (0xff23282f));
                g.fillRect (bar);
                const float level = meter[(size_t) juce::jlimit (0, PlaybackEngine::maxOutputs - 1, o.firstChannel + side)];
                const float db = juce::Decibels::gainToDecibels (level, -60.0f);
                const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
                g.setColour (db > -1.0f ? juce::Colour (0xffe5484d) : db > -9.0f ? Look::amber : Look::green);
                g.fillRect (bar.withWidth (bar.getWidth() * frac));
            }
        }
    }

    // playing cues, newest first
    cards.clear();
    for (auto& v : controller.getEngine().getVoices())
    {
        if (r.getHeight() < 124)
            break;
        if (! controller.getShow().isValid (v.cueId))
            continue;
        const auto& cue = controller.getShow().cue (v.cueId);
        auto card = r.removeFromTop (116).toFloat();
        r.removeFromTop (10);
        g.setColour (Look::raised);
        g.fillRoundedRectangle (card, 10.0f);
        g.setColour (Look::colourOf (cue));
        g.fillRoundedRectangle (card.withWidth (6.0f), 3.0f);
        g.setColour (v.stopping ? Look::edge : juce::Colour (0xff3b3020));
        g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);

        auto in = card.reduced (12.0f, 10.0f);
        auto top = in.removeFromTop (20.0f);
        g.setFont (Look::mono (10.0f));
        const auto out = CueGrid::outputLabel (cue.output).toUpperCase();
        const float ow = Look::textWidth (g.getCurrentFont(), out) + 10.0f;
        auto ob = top.removeFromRight (ow).withSizeKeepingCentre (ow, 16.0f);
        g.setColour (Look::line);
        g.fillRoundedRectangle (ob, 3.0f);
        g.setColour (juce::Colour (0xffc9d0d8));
        g.drawText (out, ob, juce::Justification::centred);
        g.setColour (Look::text);
        g.setFont (Look::font (15.0f, true));
        g.drawText (cue.name, top, juce::Justification::centredLeft, true);

        double position = v.position;
        if (scrubVoice == v.voice && scrubFraction >= 0.0)
            position = scrubFraction * v.length;   // being dragged: show where it's going

        auto times = in.removeFromTop (34.0f);
        g.setColour (Look::textDim);
        g.setFont (Look::mono (12.0f));
        if (v.waiting)
        {
            // a playlist Wait: the song is over, the next one starts when the count reaches 0
            juce::String next = "Next song";
            const int pl = controller.playlistOfTag (v.tag);
            if (auto* p = controller.playlistAt (pl); p != nullptr)
                if (const int n = controller.getNextEntry (pl); n >= 0 && n < (int) p->entries.size()
                                                             && controller.getShow().isValid (p->entries[(size_t) n].cueId))
                    next = controller.getShow().cue (p->entries[(size_t) n].cueId).name;
            g.drawText ("Next: " + next, times.withTrimmedRight (90.0f), juce::Justification::bottomLeft, true);
            g.setColour (Look::blue);
            g.setFont (Look::mono (28.0f, true));
            g.drawText (CueGrid::formatTime (std::ceil (v.waitLeft)), times, juce::Justification::centredRight);
            g.setFont (Look::condensed (13.0f));
            g.drawText ("WAITING", times.withTrimmedRight (86.0f), juce::Justification::topLeft);
        }
        else
        {
            g.drawText (CueGrid::formatTime (position) + " / " + CueGrid::formatTime (v.length), times, juce::Justification::bottomLeft);
            g.setColour (v.stopping ? Look::textDim : Look::amber);
            if (v.looping && ! v.stopping)
            {
                g.setFont (Look::condensed (22.0f));
                g.drawText (juce::String::fromUTF8 ("\xe2\x86\xbb LOOPING"), times, juce::Justification::centredRight);
            }
            else
            {
                g.setFont (Look::mono (28.0f, true));
                g.drawText ("-" + CueGrid::formatTime (std::ceil (v.length - position)), times, juce::Justification::centredRight);
            }
        }

        in.removeFromTop (4.0f);
        auto bar = in.removeFromTop (6.0f);
        g.setColour (Look::edge);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (v.waiting ? Look::blue : v.stopping ? Look::textDim : Look::amber);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, position / std::max (0.001, v.length))), 3.0f);

        in.removeFromTop (8.0f);
        auto buttons = in.removeFromTop (30.0f);
        Card c;
        c.voice = v.voice;
        c.length = v.length;
        c.looping = v.looping;
        c.bar = bar.expanded (0.0f, 7.0f);   // easier to hit than the 6 px it's drawn
        const float w = (buttons.getWidth() - 12.0f) / 3.0f;
        c.fade = buttons.removeFromLeft (w);
        buttons.removeFromLeft (6.0f);
        c.loop = buttons.removeFromLeft (w);
        buttons.removeFromLeft (6.0f);
        c.stop = buttons;
        for (auto [rect, label] : { std::pair { c.fade, v.stopping ? "Fading..." : "Fade out" },
                                    std::pair { c.loop, "Loop" }, std::pair { c.stop, "Stop" } })
        {
            const bool lit = rect == c.loop && v.looping;
            g.setColour (lit ? Look::amber.withAlpha (0.22f) : Look::line);
            g.fillRoundedRectangle (rect, 6.0f);
            g.setColour (lit ? Look::amber : juce::Colour (0xff3a414b));
            g.drawRoundedRectangle (rect.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (lit ? Look::amber : Look::text);
            g.setFont (Look::font (14.0f));
            g.drawText (label, rect, juce::Justification::centred);
        }
        cards.push_back (c);
    }
    if (cards.empty())
    {
        g.setColour (Look::textFaint);
        g.setFont (Look::font (14.0f));
        g.drawText ("Nothing playing", r.removeFromTop (30), juce::Justification::centredLeft);
    }
}

void NowPlaying::seek (const Card& c, juce::Point<float> p)
{
    scrubVoice = c.voice;
    scrubFraction = juce::jlimit (0.0, 1.0, (double) ((p.x - c.bar.getX()) / c.bar.getWidth()));
    auto& engine = controller.getEngine();
    engine.seekVoice (c.voice, (juce::int64) (scrubFraction * c.length * engine.getSampleRate()));
    repaint();
}

void NowPlaying::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    scrubVoice = -1;
    for (auto& c : cards)
    {
        if (c.fade.contains (p))
            controller.getEngine().stopVoice (c.voice, true);
        else if (c.stop.contains (p))
            controller.getEngine().stopVoice (c.voice, false);
        else if (c.loop.contains (p))
            controller.setVoiceLoop (c.voice, ! c.looping);   // off: it finishes this pass (loop points: on to the Out point)
        else if (c.bar.contains (p))
            seek (c, p);
    }
}

void NowPlaying::mouseDrag (const juce::MouseEvent& e)
{
    for (auto& c : cards)
        if (c.voice == scrubVoice)
            seek (c, e.position);
}

void NowPlaying::mouseUp (const juce::MouseEvent&)
{
    scrubVoice = -1;
    scrubFraction = -1.0;
}
