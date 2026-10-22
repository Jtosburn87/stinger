#include "WaveformEditor.h"
#include "CueGrid.h"
#include "Look.h"

namespace
{
    const juce::Colour fadeBlue { 0xff7cb8f2 };
    constexpr int nearEnd = 28;   // px: how close to an end the fade / trim tools take over

    juce::String formatPrecise (double seconds)
    {
        const auto ms = (juce::int64) std::llround (std::max (0.0, seconds) * 1000.0);
        return juce::String (ms / 60000) + ":" + juce::String ((ms / 1000) % 60).paddedLeft ('0', 2)
             + "." + juce::String (ms % 1000).paddedLeft ('0', 3);
    }

    /** "1:02.5", "62.5" or "1:02" -> seconds; < 0 if it can't be read. */
    double parseTime (const juce::String& text)
    {
        const auto t = text.trim();
        if (t.isEmpty() || ! t.containsOnly ("0123456789:."))
            return -1.0;
        if (t.contains (":"))
            return t.upToFirstOccurrenceOf (":", false, false).getIntValue() * 60.0
                 + t.fromFirstOccurrenceOf (":", false, false).getDoubleValue();
        return t.getDoubleValue();
    }

    // ---- smart tool cursors (drawn, white with a dark outline) ----

    juce::MouseCursor makeCursor (std::function<void (juce::Path&)> draw, int hotX, int hotY)
    {
        juce::Image image (juce::Image::ARGB, 24, 24, true);
        juce::Graphics g (image);
        juce::Path p;
        draw (p);
        g.setColour (juce::Colours::black);
        g.strokePath (p, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return juce::MouseCursor (image, hotX, hotY);
    }

    const juce::MouseCursor& trimCursor (bool left)
    {
        static const juce::MouseCursor l = makeCursor ([] (juce::Path& p)
        {
            p.startNewSubPath (11, 3); p.lineTo (7, 3); p.lineTo (7, 21); p.lineTo (11, 21);   // [
            p.startNewSubPath (10, 12); p.lineTo (20, 12);                                    // arrow
            p.startNewSubPath (17, 9); p.lineTo (20, 12); p.lineTo (17, 15);
        }, 7, 12);
        static const juce::MouseCursor r = makeCursor ([] (juce::Path& p)
        {
            p.startNewSubPath (13, 3); p.lineTo (17, 3); p.lineTo (17, 21); p.lineTo (13, 21);  // ]
            p.startNewSubPath (14, 12); p.lineTo (4, 12);
            p.startNewSubPath (7, 9); p.lineTo (4, 12); p.lineTo (7, 15);
        }, 17, 12);
        return left ? l : r;
    }

    const juce::MouseCursor& fadeCursor (bool in)
    {
        static const juce::MouseCursor up = makeCursor ([] (juce::Path& p)
        {
            p.addRectangle (3.0f, 3.0f, 18.0f, 18.0f);
            p.startNewSubPath (4, 20); p.quadraticTo (14, 19, 20, 4);
        }, 3, 12);
        static const juce::MouseCursor down = makeCursor ([] (juce::Path& p)
        {
            p.addRectangle (3.0f, 3.0f, 18.0f, 18.0f);
            p.startNewSubPath (4, 4); p.quadraticTo (10, 19, 20, 20);
        }, 21, 12);
        return in ? up : down;
    }

    const juce::MouseCursor& razorCursor()
    {
        static const juce::MouseCursor c = makeCursor ([] (juce::Path& p)
        {
            p.startNewSubPath (12, 2); p.lineTo (12, 22);                 // the cut line
            p.addRoundedRectangle (14.0f, 4.0f, 7.0f, 11.0f, 1.5f);       // blade
            p.startNewSubPath (17.5f, 15); p.lineTo (17.5f, 21);          // handle
        }, 12, 12);
        return c;
    }
}

// ---------------------------------------------------------------------------------------------
// Edits

bool WaveformEditor::Edits::operator== (const Edits& o) const
{
    return trimIn == o.trimIn && trimOut == o.trimOut && cuts == o.cuts && pastes == o.pastes
        && fadeIn.seconds == o.fadeIn.seconds && fadeIn.curve == o.fadeIn.curve
        && fadeOut.seconds == o.fadeOut.seconds && fadeOut.curve == o.fadeOut.curve;
}

WaveformEditor::Edits WaveformEditor::Edits::of (const Cue& c)
{
    Edits e;
    e.trimIn = c.trimIn;
    e.trimOut = c.trimOut;
    e.cuts = c.cuts;
    e.pastes = c.pastes;
    e.fadeIn = c.fadeIn;
    e.fadeOut = c.fadeOut;
    return e;
}

void WaveformEditor::Edits::applyTo (Cue& c) const
{
    c.trimIn = trimIn;
    c.trimOut = trimOut;
    c.cuts = cuts;
    c.pastes = pastes;
    c.fadeIn = fadeIn;
    c.fadeOut = fadeOut;
}

double WaveformEditor::snapToZero (const CueAudio& audio, double t)
{
    return audio.nearestZeroCrossing (t);
}

// ---------------------------------------------------------------------------------------------
// Canvas: the waveform itself, with the smart tool

class WaveformEditor::Canvas : public juce::Component
{
public:
    explicit Canvas (WaveformEditor& e) : editor (e) {}

    enum class Tool { none, pointer, fadeIn, fadeOut, trimIn, trimOut, razor };

    std::function<void (const juce::String&)> onHint;

    float xOf (double t) const { return (float) ((t - editor.viewStart) / editor.viewLength * getWidth()); }
    double tAt (float x) const { return editor.viewStart + (double) x / getWidth() * editor.viewLength; }

    Tool toolAt (juce::Point<float> p)
    {
        auto* c = editor.cue();
        if (c == nullptr || editor.audio() == nullptr)
            return Tool::none;
        const float h = (float) getHeight();
        const float inX = xOf (editor.playIn()), outX = xOf (editor.playOut());

        if (p.y >= h * 2.0f / 3.0f)
            return Tool::razor;
        if (p.y < h / 3.0f)
        {
            const float fadeInEnd = xOf (editor.playIn() + c->fadeIn.seconds);
            const float fadeOutStart = xOf (editor.playOut() - c->fadeOut.seconds);
            const bool nearIn = p.x > inX - nearEnd && p.x < fadeInEnd + nearEnd;
            const bool nearOut = p.x < outX + nearEnd && p.x > fadeOutStart - nearEnd;
            if (nearIn && nearOut)
                return std::abs (p.x - fadeInEnd) < std::abs (p.x - fadeOutStart) ? Tool::fadeIn : Tool::fadeOut;
            if (nearIn) return Tool::fadeIn;
            if (nearOut) return Tool::fadeOut;
        }
        else
        {
            const float dIn = std::abs (p.x - inX), dOut = std::abs (p.x - outX);
            if (std::min (dIn, dOut) < nearEnd)
                return dIn <= dOut ? Tool::trimIn : Tool::trimOut;
        }
        return Tool::pointer;
    }

    static juce::String hintFor (Tool t)
    {
        switch (t)
        {
            case Tool::fadeIn:    return "Fade in: drag to set its length (right-click for the curve)";
            case Tool::fadeOut:   return "Fade out: drag to set its length (right-click for the curve)";
            case Tool::trimIn:    return "Trim: drag the In point";
            case Tool::trimOut:   return "Trim: drag the Out point";
            case Tool::razor:     return "Razor: drag across a section to cut it out, click a cut to bring it back";
            case Tool::pointer:   return juce::String::fromUTF8 ("Click: preview cursor here \xc2\xb7 drag: select (then Cut, Copy, Paste or Split) \xc2\xb7 Split with nothing selected splits here");
            case Tool::none:      break;
        }
        return {};
    }

    /** The paste whose marker is under x (within a few px), -1 = none. */
    int pasteAt (float x)
    {
        if (auto* c = editor.cue())
            for (int i = 0; i < (int) c->pastes.size(); ++i)
                if (std::abs (xOf (c->pastes[(size_t) i].at) - x) < 6.0f)
                    return i;
        return -1;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (const int p = pasteAt (e.position.x); p != hoverPaste)
        {
            hoverPaste = p;   // shows where that paste was copied from
            repaint();
        }
        const auto t = toolAt (e.position);
        switch (t)
        {
            case Tool::fadeIn:    setMouseCursor (fadeCursor (true)); break;
            case Tool::fadeOut:   setMouseCursor (fadeCursor (false)); break;
            case Tool::trimIn:    setMouseCursor (trimCursor (true)); break;
            case Tool::trimOut:   setMouseCursor (trimCursor (false)); break;
            case Tool::razor:     setMouseCursor (razorCursor()); break;
            default:              setMouseCursor (juce::MouseCursor::NormalCursor); break;
        }
        if (onHint)
            onHint (hintFor (t));
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (onHint)
            onHint ({});
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragTool = toolAt (e.position);
        auto* c = editor.cue();
        if (c == nullptr || dragTool == Tool::none)
            return;
        if (e.mods.isPopupMenu())
        {
            showMenu (e);
            dragTool = Tool::none;
            return;
        }
        razorFrom = tAt (e.position.x);
        razorTo = razorFrom;
        if (dragTool == Tool::pointer)
        {
            editor.cursor = juce::jlimit (0.0, editor.fileSeconds(), razorFrom);
            editor.selection = {};
            editor.updateEditButtons();
            repaint();
            return;
        }
        editor.beginEdit();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* c = editor.cue();
        auto audio = editor.audio();
        if (c == nullptr || audio == nullptr)
            return;
        const double file = editor.fileSeconds();
        const double t = juce::jlimit (0.0, file, tAt (e.position.x));
        const double in = editor.playIn(), out = editor.playOut();
        const double minLength = 0.05;

        switch (dragTool)
        {
            case Tool::fadeIn:
                c->fadeIn.seconds = juce::jlimit (0.0, std::max (0.0, out - in), t - in);
                break;
            case Tool::fadeOut:
                c->fadeOut.seconds = juce::jlimit (0.0, std::max (0.0, out - in), out - t);
                break;
            case Tool::trimIn:
                c->trimIn = juce::jlimit (0.0, std::max (0.0, out - minLength), WaveformEditor::snapToZero (*audio, t));
                break;
            case Tool::trimOut:
            {
                const auto v = juce::jlimit (std::min (file, in + minLength), file, WaveformEditor::snapToZero (*audio, t));
                c->trimOut = v >= file - 1.0e-6 ? -1.0 : v;
                break;
            }
            case Tool::razor:
                razorTo = t;
                break;
            case Tool::pointer:
                if (e.getDistanceFromDragStart() < 3)
                    return;
                {
                    // drag: select (snapped so edits don't click)
                    const auto from = juce::jlimit (0.0, file, razorFrom);
                    editor.selection = juce::Range<double>::between (WaveformEditor::snapToZero (*audio, from),
                                                                     WaveformEditor::snapToZero (*audio, t));
                    editor.cursor = editor.selection.getStart();
                    editor.updateEditButtons();
                    repaint();
                }
                return;
            default:
                return;
        }
        if (dragTool != Tool::razor)
            editor.edited();
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto* c = editor.cue();
        auto audio = editor.audio();
        if (dragTool == Tool::razor && c != nullptr && audio != nullptr)
        {
            if (e.getDistanceFromDragStart() < 3)
            {
                // click on a cut: bring that section back
                const double t = tAt (e.position.x);
                for (auto it = c->cuts.begin(); it != c->cuts.end(); ++it)
                    if (it->contains (t))
                    {
                        c->cuts.erase (it);
                        break;
                    }
            }
            else
            {
                editor.addCut (juce::Range<double>::between (razorFrom, razorTo));
            }
            editor.edited();
        }
        if (dragTool != Tool::none && dragTool != Tool::pointer)
            editor.endEdit();
        dragTool = Tool::none;
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float delta = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : w.deltaX;
        if (delta == 0.0f)
            return;
        if (e.mods.isAltDown())
            editor.zoomAround (tAt (e.position.x), delta > 0 ? 0.8 : 1.25);
        else if (e.mods.isCommandDown())
        {
            editor.verticalZoom = juce::jlimit (1.0f, 32.0f, editor.verticalZoom * (delta > 0 ? 1.25f : 0.8f));
            editor.repaint();
        }
        else
            editor.setView (editor.viewStart - delta * editor.viewLength * 0.25, editor.viewLength);
    }

    void showMenu (const juce::MouseEvent& e)
    {
        auto* c = editor.cue();
        if (c == nullptr)
            return;
        const double t = tAt (e.position.x);
        const auto tool = toolAt (e.position);
        int cutIndex = -1;
        for (int i = 0; i < (int) c->cuts.size(); ++i)
            if (c->cuts[(size_t) i].contains (t))
                cutIndex = i;

        juce::PopupMenu m;
        if (tool == Tool::fadeIn || tool == Tool::fadeOut)
        {
            const auto& fade = tool == Tool::fadeIn ? c->fadeIn : c->fadeOut;
            m.addSectionHeader (tool == Tool::fadeIn ? "Fade in curve" : "Fade out curve");
            for (int k = 0; k < 4; ++k)
                m.addItem (10 + k, fadeCurveName ((FadeCurve) k), true, (int) fade.curve == k);
            m.addItem (14, "No fade", fade.seconds > 0.0);
            m.addSeparator();
        }
        if (cutIndex >= 0)
            m.addItem (1, "Restore this section");
        const int pasteIndex = pasteAt (e.position.x);
        if (pasteIndex >= 0)
            m.addItem (2, "Remove this paste");
        if (editor.hasSelection())
        {
            m.addSeparator();
            m.addItem (20, "Cut selection (Ctrl+X)");
            m.addItem (21, "Copy selection (Ctrl+C)");
            m.addItem (22, "Delete selection (Del)");
            m.addItem (23, "Split selection to a new button");
        }
        else if (t > editor.playIn() && t < editor.playOut())
        {
            m.addSeparator();
            m.addItem (25, "Split here (the rest to a new button)");
        }
        m.addItem (24, "Paste here (Ctrl+V)", ! editor.clipboard().pieces.empty());
        m.addSeparator();
        m.addItem (4, "Remove all cuts", ! c->cuts.empty());
        m.addItem (7, "Remove all pastes", ! c->pastes.empty());
        m.addItem (5, "Reset trims");
        m.addItem (6, "Remove fades", c->fadeIn.seconds > 0.0 || c->fadeOut.seconds > 0.0);

        juce::Component::SafePointer<Canvas> safe (this);
        const bool fadeInMenu = tool == Tool::fadeIn;
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, t, cutIndex, pasteIndex, fadeInMenu] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto& ed = safe->editor;
            switch (result)
            {
                case 20: ed.cutSelection (true); return;
                case 21: ed.copySelection(); return;
                case 22: ed.cutSelection (false); return;
                case 23: ed.splitSelection(); return;
                case 25: ed.cursor = t; ed.splitAtCursor(); return;
                case 24: ed.selection = {}; ed.cursor = juce::jlimit (0.0, ed.fileSeconds(), t); ed.paste(); return;
                default: break;
            }
            auto* cue = ed.cue();
            auto audio = ed.audio();
            if (cue == nullptr || audio == nullptr)
                return;
            ed.beginEdit();
            auto& fade = fadeInMenu ? cue->fadeIn : cue->fadeOut;
            if (result >= 10 && result < 14) fade.curve = (FadeCurve) (result - 10);
            else if (result == 14) fade.seconds = 0.0;
            else if (result == 1 && cutIndex >= 0 && cutIndex < (int) cue->cuts.size()) cue->cuts.erase (cue->cuts.begin() + cutIndex);
            else if (result == 2 && pasteIndex >= 0 && pasteIndex < (int) cue->pastes.size()) cue->pastes.erase (cue->pastes.begin() + pasteIndex);
            else if (result == 4) cue->cuts.clear();
            else if (result == 7) cue->pastes.clear();
            else if (result == 5) { cue->trimIn = 0.0; cue->trimOut = -1.0; }
            else if (result == 6) { cue->fadeIn.seconds = 0.0; cue->fadeOut.seconds = 0.0; }
            ed.edited();
            ed.endEdit();
        });
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Look::ground);
        g.fillRoundedRectangle (r, 6.0f);

        auto* c = editor.cue();
        auto audio = editor.audio();
        if (c == nullptr || c->isEmpty() || audio == nullptr)
        {
            g.setColour (Look::textFaint);
            g.setFont (Look::font (14.0f));
            g.drawText (editor.controller.isLoading (editor.cueId) ? "Loading..." : "No audio on this button",
                        r, juce::Justification::centred);
            return;
        }

        const auto segments = c->segments (audio->seconds());
        auto plays = [&] (double t)
        {
            for (auto& s : segments)
                if (t >= s.start && t < s.end)
                    return true;
            return false;
        };

        // thirds: faint guides for the smart tool zones
        g.setColour (Look::line.withAlpha (0.6f));
        for (int k = 1; k < 3; ++k)
            g.fillRect (0.0f, r.getHeight() * k / 3.0f, r.getWidth(), 1.0f);

        // waveform: min / max per pixel column, from the overview or (zoomed in) the samples
        const float mid = r.getCentreY(), half = r.getHeight() * 0.46f * editor.verticalZoom;
        const int columns = getWidth();
        const double sr = audio->sampleRate;
        const int n = audio->samples.getNumSamples();
        for (int x = 0; x < columns; ++x)
        {
            const double t0 = tAt ((float) x), t1 = tAt ((float) x + 1.0f);
            if (t1 <= 0.0 || t0 >= audio->seconds())
                continue;
            const int s0 = juce::jlimit (0, n - 1, (int) (t0 * sr)), s1 = juce::jlimit (s0 + 1, n, (int) (t1 * sr) + 1);
            float lo = 0.0f, hi = 0.0f;
            if (s1 - s0 < CueAudio::peakBlock * 2)
            {
                for (int ch = 0; ch < audio->numChannels(); ++ch)
                {
                    const auto range = juce::FloatVectorOperations::findMinAndMax (audio->samples.getReadPointer (ch, s0), s1 - s0);
                    lo = std::min (lo, range.getStart());
                    hi = std::max (hi, range.getEnd());
                }
            }
            else
            {
                const int b0 = s0 / CueAudio::peakBlock, b1 = std::min ((int) audio->peakMax.size(), s1 / CueAudio::peakBlock + 1);
                for (int b = b0; b < b1; ++b)
                {
                    lo = std::min (lo, audio->peakMin[(size_t) b]);
                    hi = std::max (hi, audio->peakMax[(size_t) b]);
                }
            }
            const float top = juce::jmax (r.getY(), mid - hi * half), bottom = juce::jmin (r.getBottom(), mid - lo * half);
            g.setColour (plays ((t0 + t1) * 0.5) ? Look::blue : juce::Colour (0xff33404f));
            g.fillRect ((float) x, top, 1.0f, std::max (1.0f, bottom - top));
        }

        const double in = editor.playIn(), out = editor.playOut();
        const float inX = xOf (in), outX = xOf (out);

        // trimmed-off ends
        g.setColour (Look::ground.withAlpha (0.72f));
        g.fillRect (r.withRight (inX));
        g.fillRect (r.withLeft (outX));

        // cuts (and the razor being dragged)
        auto drawCut = [&] (juce::Range<double> cut, bool pending)
        {
            auto cr = r.withLeft (xOf (cut.getStart())).withRight (xOf (cut.getEnd()));
            if (cr.getWidth() <= 0.0f)
                return;
            g.saveState();
            g.reduceClipRegion (cr.toNearestInt());
            g.setColour (Look::red.withAlpha (pending ? 0.35f : 0.22f));
            g.fillRect (cr);
            g.setColour (Look::ground.withAlpha (0.5f));
            for (float x = cr.getX() - cr.getHeight(); x < cr.getRight(); x += 12.0f)
                g.drawLine (x, cr.getBottom(), x + cr.getHeight(), cr.getY(), 5.0f);
            g.restoreState();
            g.setColour (Look::red);
            g.fillRect (cr.getX(), r.getY(), 1.0f, r.getHeight());
            g.fillRect (cr.getRight() - 1.0f, r.getY(), 1.0f, r.getHeight());
            if (! pending && cr.getWidth() > 60.0f)
            {
                g.setFont (Look::mono (10.0f));
                const auto text = "CUT " + CueGrid::formatTimeTenths (cut.getStart()) + juce::String::fromUTF8 ("\xe2\x80\x93") + CueGrid::formatTimeTenths (cut.getEnd());
                auto label = juce::Rectangle<float> (cr.getX() + 4.0f, r.getY() + 6.0f, Look::textWidth (g.getCurrentFont(), text) + 10.0f, 16.0f);
                g.setColour (Look::red);
                g.fillRoundedRectangle (label, 3.0f);
                g.setColour (juce::Colours::white);
                g.drawText (text, label, juce::Justification::centred);
            }
        };
        for (auto cut : c->cuts)
            drawCut (cut, false);
        if (dragTool == Tool::razor && std::abs (razorTo - razorFrom) > 0.0)
            drawCut (juce::Range<double>::between (razorFrom, razorTo), true);

        // selection
        if (editor.hasSelection())
        {
            auto sr2 = r.withLeft (xOf (editor.selection.getStart())).withRight (xOf (editor.selection.getEnd()));
            g.setColour (juce::Colours::white.withAlpha (0.13f));
            g.fillRect (sr2);
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.fillRect (sr2.getX(), r.getY(), 1.0f, r.getHeight());
            g.fillRect (sr2.getRight() - 1.0f, r.getY(), 1.0f, r.getHeight());
        }

        // pastes: a purple marker where each copy plays; hovering one outlines where it was copied from
        const juce::Colour purple (0xffb18cf2);
        for (int i = 0; i < (int) c->pastes.size(); ++i)
        {
            const auto& p = c->pastes[(size_t) i];
            if (i == hoverPaste)
                for (auto& piece : p.pieces)
                {
                    auto pr = r.withLeft (xOf (piece.start)).withRight (xOf (piece.end)).reduced (0.0f, 2.0f);
                    g.setColour (purple.withAlpha (0.15f));
                    g.fillRect (pr);
                    g.setColour (purple);
                    g.drawRect (pr, 1.5f);
                }
            const float x = xOf (p.at);
            g.setColour (purple);
            g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
            juce::Path flag;
            flag.addTriangle (x - 6.0f, r.getY(), x + 6.0f, r.getY(), x, r.getY() + 8.0f);
            g.fillPath (flag);
            g.setFont (Look::mono (10.0f));
            const auto text = "PASTE +" + juce::String (p.length(), 1) + " s";
            auto label = juce::Rectangle<float> (x + 4.0f, r.getY() + 22.0f, Look::textWidth (g.getCurrentFont(), text) + 10.0f, 16.0f);
            g.fillRoundedRectangle (label, 3.0f);
            g.setColour (Look::ground);
            g.drawText (text, label, juce::Justification::centred);
        }

        // fades: curve over the region, shading above it, handle square at the inner end
        auto drawFade = [&] (double from, double to, FadeCurve curve, bool rising)
        {
            if (to <= from)
                return;
            juce::Path line, shade;
            for (int i = 0; i <= 48; ++i)
            {
                const float f = (float) i / 48.0f;
                const float x = xOf (from + (to - from) * f);
                const float y = r.getBottom() - fadeGain (curve, rising ? f : 1.0f - f) * r.getHeight();
                if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            shade = line;
            shade.lineTo (xOf (to), r.getY());
            shade.lineTo (xOf (from), r.getY());
            shade.closeSubPath();
            g.setColour (Look::ground.withAlpha (0.45f));
            g.fillPath (shade);
            g.setColour (fadeBlue);
            g.strokePath (line, juce::PathStrokeType (2.0f));
            const float hx = rising ? xOf (to) : xOf (from);
            g.fillRect (hx - 5.0f, r.getY(), 10.0f, 10.0f);
        };
        drawFade (in, std::min (out, in + c->fadeIn.seconds), c->fadeIn.curve, true);
        drawFade (std::max (in, out - c->fadeOut.seconds), out, c->fadeOut.curve, false);

        auto tag = [&] (const juce::String& text, float x, float y, juce::Colour back, juce::Colour fore, bool alignRight = false)
        {
            g.setFont (Look::mono (10.0f));
            const float w = Look::textWidth (g.getCurrentFont(), text) + 10.0f;
            auto b = juce::Rectangle<float> (alignRight ? x - w : x, y, w, 16.0f);
            g.setColour (back);
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (fore);
            g.drawText (text, b, juce::Justification::centred);
        };
        if (c->fadeIn.seconds > 0.0)
            tag ("FADE IN " + juce::String (c->fadeIn.seconds, 1) + " s", xOf (in + c->fadeIn.seconds) + 7.0f, r.getY() + 2.0f,
                 juce::Colour (0xff172636), juce::Colour (0xffa9d0f7));
        if (c->fadeOut.seconds > 0.0)
            tag ("FADE OUT " + juce::String (c->fadeOut.seconds, 1) + " s", xOf (out - c->fadeOut.seconds) - 7.0f, r.getY() + 2.0f,
                 juce::Colour (0xff172636), juce::Colour (0xffa9d0f7), true);

        // trim handles
        g.setColour (Look::text);
        g.fillRect (inX - 1.0f, r.getY(), 3.0f, r.getHeight());
        g.fillRect (outX - 2.0f, r.getY(), 3.0f, r.getHeight());
        tag ("IN " + CueGrid::formatTimeTenths (in), inX + 4.0f, r.getBottom() - 20.0f, Look::text, Look::ground);
        tag ("OUT " + CueGrid::formatTimeTenths (out), outX - 4.0f, r.getBottom() - 20.0f, Look::text, Look::ground, true);

        // preview cursor
        if (editor.cursor >= 0.0)
        {
            g.setColour (Look::text.withAlpha (0.7f));
            const float x = xOf (editor.cursor);
            for (float y = r.getY(); y < r.getBottom(); y += 6.0f)
                g.fillRect (x, y, 1.0f, 3.0f);
        }

        // playhead (timeline position mapped back to the file)
        for (auto& v : editor.controller.getEngine().getVoices())
            if (v.cueId == editor.cueId)
            {
                double t = std::fmod (v.position, std::max (1.0e-6, c->playLength (audio->seconds())));
                double file = in;
                for (auto& s : segments)
                {
                    if (t < s.length()) { file = s.start + t; break; }
                    t -= s.length();
                    file = s.end;
                }
                g.setColour (Look::amber);
                g.fillRect (xOf (file) - 1.0f, r.getY(), 2.0f, r.getHeight());
            }
    }

private:
    WaveformEditor& editor;
    Tool dragTool = Tool::none;
    double razorFrom = 0.0, razorTo = 0.0;   // razor drag (and the selection's anchor)
    int hoverPaste = -1;
};

// ---------------------------------------------------------------------------------------------
// NavBar: the whole file, with the part in view; drag to scroll, drag the edges to zoom

class WaveformEditor::NavBar : public juce::Component
{
public:
    explicit NavBar (WaveformEditor& e) : editor (e) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Look::raised);
        g.fillRoundedRectangle (r, 4.0f);
        auto audio = editor.audio();
        const double file = editor.fileSeconds();
        if (audio == nullptr || file <= 0.0 || audio->peakMax.empty())
            return;

        const float mid = r.getCentreY(), half = r.getHeight() * 0.42f;
        const int blocks = (int) audio->peakMax.size();
        g.setColour (Look::textFaint);
        for (int x = 0; x < getWidth(); ++x)
        {
            const int b0 = (int) ((double) x / getWidth() * blocks), b1 = std::max (b0 + 1, (int) ((double) (x + 1) / getWidth() * blocks));
            float hi = 0.0f;
            for (int b = b0; b < std::min (b1, blocks); ++b)
                hi = std::max (hi, std::max (audio->peakMax[(size_t) b], -audio->peakMin[(size_t) b]));
            g.fillRect ((float) x, mid - hi * half, 1.0f, std::max (1.0f, hi * half * 2.0f));
        }

        auto view = viewRect();
        g.setColour (Look::blue.withAlpha (0.18f));
        g.fillRoundedRectangle (view, 3.0f);
        g.setColour (Look::blue);
        g.drawRoundedRectangle (view.reduced (0.5f), 3.0f, 1.5f);
        g.fillRect (view.getX(), view.getY() + 6.0f, 3.0f, view.getHeight() - 12.0f);
        g.fillRect (view.getRight() - 3.0f, view.getY() + 6.0f, 3.0f, view.getHeight() - 12.0f);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto v = viewRect();
        const bool edge = std::abs (e.position.x - v.getX()) < 6.0f || std::abs (e.position.x - v.getRight()) < 6.0f;
        setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor
                             : v.contains (e.position) ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::PointingHandCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto v = viewRect();
        startView = editor.viewStart;
        startLength = editor.viewLength;
        if (std::abs (e.position.x - v.getX()) < 6.0f) mode = Mode::left;
        else if (std::abs (e.position.x - v.getRight()) < 6.0f) mode = Mode::right;
        else if (v.contains (e.position)) mode = Mode::move;
        else
        {
            // click outside the window: centre the view there
            mode = Mode::move;
            editor.setView (tAt (e.position.x) - editor.viewLength * 0.5, editor.viewLength);
            startView = editor.viewStart;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const double dt = (double) e.getDistanceFromDragStartX() / getWidth() * editor.fileSeconds();
        if (mode == Mode::move)
            editor.setView (startView + dt, startLength);
        else if (mode == Mode::left)
            editor.setView (std::min (startView + dt, startView + startLength - 0.01), startLength - dt);
        else if (mode == Mode::right)
            editor.setView (startView, startLength + dt);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { editor.fit(); }

private:
    enum class Mode { move, left, right };

    double tAt (float x) const { return (double) x / getWidth() * editor.fileSeconds(); }
    juce::Rectangle<float> viewRect() const
    {
        const double file = std::max (1.0e-6, editor.fileSeconds());
        const float x0 = (float) (editor.viewStart / file * getWidth()), x1 = (float) (editor.viewEnd() / file * getWidth());
        return { x0, 1.0f, std::max (8.0f, x1 - x0), (float) getHeight() - 2.0f };
    }

    WaveformEditor& editor;
    Mode mode = Mode::move;
    double startView = 0.0, startLength = 0.0;
};

// ---------------------------------------------------------------------------------------------

WaveformEditor::WaveformEditor (ShowController& c) : controller (c)
{
    canvas = std::make_unique<Canvas> (*this);
    nav = std::make_unique<NavBar> (*this);
    addAndMakeVisible (*canvas);
    addAndMakeVisible (*nav);
    canvas->onHint = [this] (const juce::String& text)
    {
        hint.setText (text.isNotEmpty() ? text
                                        : juce::String::fromUTF8 ("Top third near an end: fade \xc2\xb7 middle: trim, or drag to select \xc2\xb7 bottom: razor \xc2\xb7 click: preview cursor"),
                      juce::dontSendNotification);
    };
    canvas->onHint ({});

    hOut.onClick = [this] { zoomAround (viewStart + viewLength * 0.5, 1.5); };
    hIn.onClick = [this] { zoomAround (viewStart + viewLength * 0.5, 1.0 / 1.5); };
    vOut.onClick = [this] { verticalZoom = juce::jmax (1.0f, verticalZoom / 1.5f); repaint(); };
    vIn.onClick = [this] { verticalZoom = juce::jmin (32.0f, verticalZoom * 1.5f); repaint(); };
    fitButton.onClick = [this] { fit(); verticalZoom = 1.0f; repaint(); };
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    playFromStart.onClick = [this] { playPreview (false); };
    playFromCursor.onClick = [this] { playPreview (true); };
    stopButton.onClick = [this] { stopPreview(); };
    cutButton.onClick = [this] { cutSelection (true); };
    copyButton.onClick = [this] { copySelection(); };
    pasteButton.onClick = [this] { paste(); };
    splitButton.onClick = [this] { if (hasSelection()) splitSelection(); else splitAtCursor(); };
    cutButton.setTooltip ("Take the selection out and keep a copy (Ctrl+X). Drag across the middle of the waveform to select.");
    copyButton.setTooltip ("Copy the selection (Ctrl+C)");
    pasteButton.setTooltip ("Play the copy again at the dashed cursor (Ctrl+V). Same song only.");
    splitButton.setTooltip ("Make the selection a new button of its own (the next empty one). With nothing selected, split at the "
                            "dashed cursor: this button ends there and the rest goes to a new button.");
    for (auto* b : { &cutButton, &copyButton, &pasteButton, &splitButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    hOut.setTooltip ("Zoom out across (Alt + wheel)");
    hIn.setTooltip ("Zoom in across (Alt + wheel)");
    vOut.setTooltip ("Less height (Ctrl + wheel)");
    vIn.setTooltip ("More height (Ctrl + wheel)");
    fitButton.setTooltip ("Whole file, normal height");
    undoButton.setTooltip ("Ctrl+Z");
    redoButton.setTooltip ("Ctrl+Y");
    playFromStart.setTooltip ("Preview from the beginning (the In point) to the output on the right");
    playFromCursor.setTooltip ("Preview from the dashed cursor (click the waveform to place it) to the output on the right");
    stopButton.setTooltip ("Stop the preview");
    for (juce::Button* b : std::initializer_list<juce::Button*> { &hOut, &hIn, &vOut, &vIn, &fitButton, &undoButton, &redoButton,
                                                                  &playFromStart, &playFromCursor, &stopButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    previewOut.setWantsKeyboardFocus (false);
    previewOut.setTooltip ("Where Preview plays");
    addAndMakeVisible (previewOut);
    fillPreviewOutputs();

    for (auto* l : { &inLabel, &outLabel })
    {
        l->setColour (juce::Label::textColourId, Look::textDim);
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }
    for (auto* f : { &inField, &outField })
    {
        f->setEditable (true);
        f->setFont (Look::mono (13.0f));
        f->setColour (juce::Label::backgroundColourId, Look::raised);
        f->setColour (juce::Label::outlineColourId, Look::edge);
        f->setTooltip ("Type a time (1:02.5 or 62.5)");
        f->onTextChange = [this, f] { setFromField (*f); };
        addAndMakeVisible (f);
    }
    hint.setColour (juce::Label::textColourId, Look::textDim);
    hint.setFont (Look::font (12.0f));
    addAndMakeVisible (hint);
}

WaveformEditor::~WaveformEditor() = default;

Cue* WaveformEditor::cue()
{
    return controller.getShow().isValid (cueId) ? &controller.getShow().cue (cueId) : nullptr;
}

std::shared_ptr<CueAudio> WaveformEditor::audio() const
{
    return controller.getAudio (cueId);
}

double WaveformEditor::fileSeconds() const
{
    auto a = audio();
    return a != nullptr ? a->seconds() : 0.0;
}

double WaveformEditor::playIn()
{
    auto* c = cue();
    return c == nullptr ? 0.0 : juce::jlimit (0.0, fileSeconds(), c->trimIn);
}

double WaveformEditor::playOut()
{
    auto* c = cue();
    const double file = fileSeconds();
    if (c == nullptr || c->trimOut < 0.0)
        return file;
    return juce::jlimit (playIn(), file, c->trimOut);
}

void WaveformEditor::setCue (int id)
{
    const bool changed = id != cueId;
    if (editing && changed)
        endEdit();
    cueId = id;
    if (changed)
    {
        cursor = -1.0;
        selection = {};
        verticalZoom = 1.0f;
        fit();
    }
    refreshFields();
    updateEditButtons();
    repaint();
}

WaveformEditor::Clipboard& WaveformEditor::clipboard()
{
    static Clipboard c;
    return c;
}

void WaveformEditor::updateEditButtons()
{
    const bool has = cue() != nullptr && audio() != nullptr;
    for (auto* b : { &cutButton, &copyButton })
        b->setEnabled (has && hasSelection());
    splitButton.setEnabled (has && (hasSelection() || canSplitAtCursor()));
    pasteButton.setEnabled (has && ! clipboard().pieces.empty());
}

void WaveformEditor::showHint (const juce::String& text)
{
    hint.setText (text, juce::dontSendNotification);
}

void WaveformEditor::addCut (juce::Range<double> range)
{
    auto* c = cue();
    auto a = audio();
    if (c == nullptr || a == nullptr)
        return;
    range = { snapToZero (*a, std::max (0.0, range.getStart())), snapToZero (*a, std::min (fileSeconds(), range.getEnd())) };
    if (range.getLength() <= 0.001)
        return;
    // merge with cuts it touches
    std::vector<juce::Range<double>> merged;
    for (auto cut : c->cuts)
    {
        if (cut.intersects (range) || cut.getEnd() == range.getStart() || cut.getStart() == range.getEnd())
            range = range.getUnionWith (cut);
        else
            merged.push_back (cut);
    }
    merged.push_back (range);
    std::sort (merged.begin(), merged.end(), [] (auto x, auto y) { return x.getStart() < y.getStart(); });
    c->cuts = merged;
}

bool WaveformEditor::copySelection()
{
    auto* c = cue();
    if (c == nullptr || audio() == nullptr || ! hasSelection())
        return false;
    // what plays inside the selection (cut parts left out)
    std::vector<Segment> pieces;
    for (auto& s : c->baseSegments (fileSeconds()))
    {
        const auto part = juce::Range<double> (s.start, s.end).getIntersectionWith (selection);
        if (part.getLength() > 0.0)
            pieces.push_back ({ part.getStart(), part.getEnd(), true });
    }
    if (pieces.empty())
    {
        showHint ("Nothing plays in the selection (it's all cut or trimmed off).");
        return false;
    }
    clipboard() = { c->file, pieces };
    double length = 0.0;
    for (auto& p : pieces)
        length += p.length();
    showHint ("Copied " + juce::String (length, 1) + " s. Click where it should play again, then Paste (Ctrl+V).");
    updateEditButtons();
    return true;
}

bool WaveformEditor::cutSelection (bool keepCopy)
{
    if (cue() == nullptr || audio() == nullptr || ! hasSelection())
        return false;
    if (keepCopy)
        copySelection();
    beginEdit();
    addCut (selection);
    selection = {};
    edited();
    endEdit();
    updateEditButtons();
    return true;
}

bool WaveformEditor::paste()
{
    auto* c = cue();
    if (c == nullptr || audio() == nullptr || clipboard().pieces.empty())
        return false;
    if (clipboard().file != c->file)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Paste",
            "The copy is from \"" + clipboard().file.getFileName() + "\". Paste works within the same song only "
            "(a button made with Split counts as the same song).");
        return false;
    }
    if (c->pastes.size() >= 16)
    {
        showHint ("A button can hold 16 pastes.");
        return false;
    }
    const double at = hasSelection() ? selection.getStart() : cursor >= 0.0 ? cursor : playIn();
    beginEdit();
    c->pastes.push_back ({ at, clipboard().pieces });
    std::stable_sort (c->pastes.begin(), c->pastes.end(), [] (auto& x, auto& y) { return x.at < y.at; });
    edited();
    endEdit();
    showHint ("Pasted at " + CueGrid::formatTimeTenths (at) + ": the copy plays there, then the song carries on. "
              "Right-click the purple marker to remove it.");
    return true;
}

bool WaveformEditor::splitSelection()
{
    auto* c = cue();
    if (c == nullptr || audio() == nullptr || ! hasSelection())
        return false;
    const auto name = c->name + " (" + CueGrid::formatTimeTenths (selection.getStart()) + juce::String::fromUTF8 ("\xe2\x80\x93")
                    + CueGrid::formatTimeTenths (selection.getEnd()) + ")";
    const int id = controller.splitToNewButton (cueId, selection, name);
    if (id < 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Split", "There's no empty button to put it on.");
        return false;
    }
    showHint ("Split to button " + juce::String::charToString ((juce::juce_wchar) ('A' + Show::bankOf (id)))
              + juce::String (Show::slotOf (id) + 1) + ".");
    return true;
}

bool WaveformEditor::canSplitAtCursor()
{
    return cue() != nullptr && audio() != nullptr && cursor > playIn() + 0.01 && cursor < playOut() - 0.01;
}

bool WaveformEditor::splitAtCursor()
{
    if (! canSplitAtCursor())
        return false;
    const double at = snapToZero (*audio(), cursor);
    const auto fadeOut = cue()->fadeOut;
    const int id = controller.splitToNewButton (cueId, { at, playOut() }, cue()->name + " (from " + CueGrid::formatTimeTenths (at) + ")");
    if (id < 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Split", "There's no empty button to put it on.");
        return false;
    }
    // like a DAW split: the new button keeps the fade out, this one keeps the fade in and ends at the cursor
    controller.getShow().cue (id).fadeOut = fadeOut;
    controller.cueChanged (id);
    auto* c = cue();
    beginEdit();
    c->trimOut = at;
    c->fadeOut.seconds = 0.0;
    c->fadeIn.seconds = std::min (c->fadeIn.seconds, at - c->trimIn);
    edited();
    endEdit();
    updateEditButtons();
    showHint ("Split at " + CueGrid::formatTimeTenths (at) + ": the rest is on button "
              + juce::String::charToString ((juce::juce_wchar) ('A' + Show::bankOf (id))) + juce::String (Show::slotOf (id) + 1) + ".");
    return true;
}

void WaveformEditor::tick()
{
    if (viewLength <= 0.0 && fileSeconds() > 0.0)
        fit();   // the audio arrived
    const bool playing = controller.getEngine().isCuePlaying (cueId);
    if (playFromStart.lit != (playing && ! previewFromCursor) || playFromCursor.lit != (playing && previewFromCursor))
    {
        playFromStart.lit = playing && ! previewFromCursor;
        playFromCursor.lit = playing && previewFromCursor;
        playFromStart.repaint();
        playFromCursor.repaint();
    }
    canvas->repaint();
}

void WaveformEditor::fit()
{
    viewStart = 0.0;
    viewLength = fileSeconds();
    repaint();
}

void WaveformEditor::setView (double start, double length)
{
    const double file = fileSeconds();
    if (file <= 0.0)
        return;
    const double minLength = std::min (file, 0.01);
    viewLength = juce::jlimit (minLength, file, length);
    viewStart = juce::jlimit (0.0, file - viewLength, start);
    repaint();
}

void WaveformEditor::zoomAround (double t, double factor)
{
    const double newLength = viewLength * factor;
    const double frac = viewLength > 0.0 ? (t - viewStart) / viewLength : 0.5;
    setView (t - frac * newLength, newLength);
}

void WaveformEditor::beginEdit()
{
    if (auto* c = cue())
    {
        editStart = Edits::of (*c);
        editing = true;
    }
}

void WaveformEditor::edited()
{
    controller.cueChanged (cueId);
    refreshFields();
    repaint();
}

void WaveformEditor::endEdit()
{
    auto* c = cue();
    if (! editing || c == nullptr)
        return;
    editing = false;
    const auto now = Edits::of (*c);
    if (! (now == editStart))
    {
        undoSteps.push_back ({ cueId, c->file, editStart, now });
        if (undoSteps.size() > 200)
            undoSteps.erase (undoSteps.begin());
        redoSteps.clear();
    }
}

void WaveformEditor::restoreEdits (int id, const Edits& e)
{
    if (! controller.getShow().isValid (id))
        return;
    e.applyTo (controller.getShow().cue (id));
    controller.cueChanged (id);
    refreshFields();
    repaint();
}

bool WaveformEditor::undo()
{
    if (undoSteps.empty())
        return false;
    const auto step = undoSteps.back();
    undoSteps.pop_back();
    redoSteps.push_back (step);
    if (step.recording)
        restoreSong (step.cueId, step.cueAfter.file, step.cueBefore);
    else if (controller.getShow().isValid (step.cueId) && controller.getShow().cue (step.cueId).file == step.file)
        restoreEdits (step.cueId, step.before);   // (not if the button has another song now: moved or replaced)
    return true;
}

void WaveformEditor::recordedOver (int id, const Cue& before)
{
    if (! controller.getShow().isValid (id))
        return;
    Step step { id, controller.getShow().cue (id).file, {}, {} };
    step.recording = true;
    step.cueBefore = before;
    step.cueAfter = controller.getShow().cue (id);
    undoSteps.push_back (step);
    if (undoSteps.size() > 200)
        undoSteps.erase (undoSteps.begin());
    redoSteps.clear();
}

void WaveformEditor::restoreSong (int id, const juce::File& expected, const Cue& song)
{
    // only if the button still has the song the step left on it (not moved, replaced or cleared since)
    auto& show = controller.getShow();
    if (! show.isValid (id) || show.cue (id).file != expected)
        return;
    // the song (file, name, edits) goes back; the button's other settings stay as they are now
    Cue c = show.cue (id);
    c.file = song.file;
    c.name = song.name;
    Edits::of (song).applyTo (c);
    controller.restoreCue (id, c);
    selection = {};
    cursor = -1.0;
    if (id == cueId)
        fit();   // the other song: the view fits it again once its audio has loaded (tick)
    updateEditButtons();
    refreshFields();
    repaint();
}

bool WaveformEditor::redo()
{
    if (redoSteps.empty())
        return false;
    const auto step = redoSteps.back();
    redoSteps.pop_back();
    undoSteps.push_back (step);
    if (step.recording)
        restoreSong (step.cueId, step.cueBefore.file, step.cueAfter);
    else if (controller.getShow().isValid (step.cueId) && controller.getShow().cue (step.cueId).file == step.file)
        restoreEdits (step.cueId, step.after);
    return true;
}

void WaveformEditor::refreshFields()
{
    auto* c = cue();
    const bool has = c != nullptr && ! c->isEmpty() && audio() != nullptr;
    for (auto* f : { &inField, &outField })
        f->setEnabled (has);
    if (! has)
    {
        for (auto* f : { &inField, &outField })
            f->setText ({}, juce::dontSendNotification);
        return;
    }
    if (! inField.isBeingEdited())
        inField.setText (formatPrecise (playIn()), juce::dontSendNotification);
    if (! outField.isBeingEdited())
        outField.setText (formatPrecise (playOut()), juce::dontSendNotification);
}

void WaveformEditor::setFromField (juce::Label& field)
{
    auto* c = cue();
    if (c == nullptr || audio() == nullptr)
        return;
    const double t = parseTime (field.getText());
    const double file = fileSeconds();
    beginEdit();
    if (&field == &inField && t >= 0.0)
        c->trimIn = juce::jlimit (0.0, std::max (0.0, playOut() - 0.05), t);
    else if (&field == &outField && t >= 0.0)
    {
        const auto v = juce::jlimit (std::min (file, playIn() + 0.05), file, t);
        c->trimOut = v >= file - 1.0e-6 ? -1.0 : v;
    }
    edited();
    endEdit();
}

void WaveformEditor::fillPreviewOutputs()
{
    previewOut.clear (juce::dontSendNotification);
    previewOut.addItem ("Cue's output", 1);
    const int count = std::max (2, controller.getEngine().getOutputNames().size());
    for (int ch = 0; ch + 1 < count; ch += 2)
        previewOut.addItem (CueGrid::outputLabel ({ ch, true }), 1000 + ch);
    for (int ch = 0; ch < count; ++ch)
        previewOut.addItem (CueGrid::outputLabel ({ ch, false }), 2000 + ch);
    previewOut.setSelectedId (1, juce::dontSendNotification);
}

void WaveformEditor::TransportButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), over, down);
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (12.0f, 12.0f);
    g.setColour (! isEnabled() ? Look::textFaint : lit ? Look::green : Look::text);
    juce::Path p;
    if (icon == Icon::stop)
    {
        g.fillRect (r.reduced (1.0f));
        return;
    }
    if (icon == Icon::playFromStart)
    {
        g.fillRect (r.removeFromLeft (2.0f));   // the line at the start
        r.removeFromLeft (2.0f);
    }
    p.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
    g.fillPath (p);
}

void WaveformEditor::stopPreview()
{
    controller.getEngine().stopCue (cueId, false);
}

void WaveformEditor::playPreview (bool fromCursor)
{
    auto& engine = controller.getEngine();
    if (engine.isCuePlaying (cueId))
        engine.stopCue (cueId, false);   // play again = start over
    auto* c = cue();
    auto a = audio();
    if (c == nullptr || a == nullptr)
        return;
    PlayPlan plan;
    if (! PlayPlan::make (*c, *a, cueId, plan))
        return;
    previewFromCursor = fromCursor;
    if (fromCursor && cursor >= 0.0)
        plan.startAt = juce::jlimit<juce::int64> (0, plan.length - 1,
                                                  (juce::int64) std::llround (c->timelinePosition (a->seconds(), cursor) * a->sampleRate));
    const int id = previewOut.getSelectedId();
    if (id >= 1000)
        plan.output = { id % 1000, id < 2000 };
    plan.fadeOthers = false;
    plan.preview = true;
    controller.attachFx (plan);   // preview with the button's effects
    engine.play (plan);
}

void WaveformEditor::paint (juce::Graphics&) {}

void WaveformEditor::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (28);
    for (auto* b : { &hOut, &hIn, &vOut, &vIn })
    {
        b->setBounds (bar.removeFromLeft (34));
        bar.removeFromLeft (3);
    }
    fitButton.setBounds (bar.removeFromLeft (40));
    bar.removeFromLeft (12);
    undoButton.setBounds (bar.removeFromLeft (52));
    bar.removeFromLeft (3);
    redoButton.setBounds (bar.removeFromLeft (52));
    bar.removeFromLeft (12);
    for (auto* b : { &cutButton, &copyButton, &pasteButton, &splitButton })
    {
        b->setBounds (bar.removeFromLeft (48));
        bar.removeFromLeft (3);
    }
    previewOut.setBounds (bar.removeFromRight (130));
    bar.removeFromRight (6);
    for (auto* b : { &stopButton, &playFromCursor, &playFromStart })
    {
        b->setBounds (bar.removeFromRight (32));
        bar.removeFromRight (3);
    }

    r.removeFromTop (6);
    auto footer = r.removeFromBottom (24);
    r.removeFromBottom (6);
    nav->setBounds (r.removeFromBottom (26));
    r.removeFromBottom (4);
    canvas->setBounds (r);

    auto field = [&footer] (juce::Label& label, juce::Label& f)
    {
        label.setBounds (footer.removeFromLeft (40));
        footer.removeFromLeft (4);
        f.setBounds (footer.removeFromLeft (86));
        footer.removeFromLeft (8);
    };
    field (inLabel, inField);
    field (outLabel, outField);
    footer.removeFromLeft (8);
    hint.setBounds (footer);
}
