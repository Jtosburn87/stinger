#include "Cue.h"
#include <algorithm>
#include <cmath>

float fadeGain (FadeCurve curve, float x)
{
    x = juce::jlimit (0.0f, 1.0f, x);
    switch (curve)
    {
        case FadeCurve::linear:      return x;
        case FadeCurve::sCurve:      return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * x);
        case FadeCurve::exponential: return x * x * x;                           // slow start, fast end
        case FadeCurve::logarithmic: return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);  // fast start
    }
    return x;
}

juce::String fadeCurveName (FadeCurve curve)
{
    switch (curve)
    {
        case FadeCurve::linear:      return "Linear";
        case FadeCurve::sCurve:      return "S-curve";
        case FadeCurve::exponential: return "Exponential";
        case FadeCurve::logarithmic: return "Logarithmic";
    }
    return {};
}

std::vector<Segment> Cue::segments (double fileSeconds) const
{
    const auto base = baseSegments (fileSeconds);
    if (pastes.empty() || base.empty())
        return base;
    auto sorted = pastes;
    std::stable_sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.at < b.at; });

    // where a paste plays: its point moved onto what plays (before the start: the start; in a cut: where the cut ends)
    auto placeOf = [&base] (double at)
    {
        for (auto& s : base)
            if (at < s.end)
                return std::max (at, s.start);
        return base.back().end;
    };
    std::vector<Segment> result;
    auto addPieces = [&] (const Paste& p)
    {
        for (auto piece : p.pieces)
        {
            piece.start = juce::jlimit (0.0, fileSeconds, piece.start);
            piece.end = juce::jlimit (piece.start, fileSeconds, piece.end);
            piece.pasted = true;
            if (piece.length() > 0.0)
                result.push_back (piece);
        }
    };
    size_t next = 0;
    for (auto& s : base)
    {
        double pos = s.start;
        for (; next < sorted.size() && placeOf (sorted[next].at) < s.end; ++next)
        {
            const double at = placeOf (sorted[next].at);
            if (at > pos)
            {
                result.push_back ({ pos, at });
                pos = at;
            }
            addPieces (sorted[next]);
        }
        if (s.end > pos)
            result.push_back ({ pos, s.end });
    }
    for (; next < sorted.size(); ++next)
        addPieces (sorted[next]);   // pasted at the very end
    return result;
}

std::vector<Segment> Cue::baseSegments (double fileSeconds) const
{
    const double in = juce::jlimit (0.0, fileSeconds, trimIn);
    const double out = trimOut < 0.0 ? fileSeconds : juce::jlimit (in, fileSeconds, trimOut);

    auto sorted = cuts;
    std::sort (sorted.begin(), sorted.end(), [] (auto a, auto b) { return a.getStart() < b.getStart(); });

    std::vector<Segment> result;
    double pos = in;
    for (auto c : sorted)
    {
        if (c.getEnd() <= pos || c.getStart() >= out)
            continue;
        if (c.getStart() > pos)
            result.push_back ({ pos, c.getStart() });
        pos = std::max (pos, c.getEnd());
    }
    if (out > pos)
        result.push_back ({ pos, out });
    return result;
}

double Cue::playLength (double fileSeconds) const
{
    double total = 0.0;
    for (auto& s : segments (fileSeconds))
        total += s.length();
    return total;
}

double Cue::timelinePosition (double fileSeconds, double fileTime) const
{
    double offset = 0.0;
    for (auto& s : segments (fileSeconds))
    {
        if (! s.pasted && fileTime < s.end)
            return offset + std::max (0.0, fileTime - s.start);   // inside a cut: next segment's start
        offset += s.length();
    }
    return 0.0;   // past the end: from the top
}

namespace
{
    juce::var fadeToVar (const Fade& f)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("seconds", f.seconds);
        o->setProperty ("curve", (int) f.curve);
        return o;
    }

    Fade fadeFromVar (const juce::var& v, Fade fallback)
    {
        if (! v.isObject())
            return fallback;
        Fade f;
        f.seconds = std::max (0.0, (double) v.getProperty ("seconds", fallback.seconds));
        f.curve = (FadeCurve) juce::jlimit (0, 3, (int) v.getProperty ("curve", (int) fallback.curve));
        return f;
    }
}

juce::var Cue::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", name);
    o->setProperty ("file", file.getFullPathName());
    o->setProperty ("colour", colour);
    if (customColour != 0)
        o->setProperty ("customColour", juce::String::toHexString ((juce::int64) customColour));
    o->setProperty ("trimIn", trimIn);
    o->setProperty ("trimOut", trimOut);
    juce::Array<juce::var> cutList;
    for (auto c : cuts)
        cutList.add (juce::Array<juce::var> { c.getStart(), c.getEnd() });
    o->setProperty ("cuts", cutList);
    if (! pastes.empty())
    {
        juce::Array<juce::var> pasteList;
        for (auto& p : pastes)
        {
            auto* paste = new juce::DynamicObject();
            paste->setProperty ("at", p.at);
            juce::Array<juce::var> pieces;
            for (auto& s : p.pieces)
                pieces.add (juce::Array<juce::var> { s.start, s.end });
            paste->setProperty ("pieces", pieces);
            pasteList.add (paste);
        }
        o->setProperty ("pastes", pasteList);
    }
    o->setProperty ("fadeIn", fadeToVar (fadeIn));
    o->setProperty ("fadeOut", fadeToVar (fadeOut));
    o->setProperty ("stopFade", fadeToVar (stopFade));
    o->setProperty ("levelDb", levelDb);
    o->setProperty ("pan", pan);
    o->setProperty ("outFirst", output.firstChannel);
    o->setProperty ("outStereo", output.stereo);
    o->setProperty ("fadeOthers", fadeOthers);
    o->setProperty ("loop", loop);
    o->setProperty ("hotkey", hotkey);
    o->setProperty ("midi", midi.toVar());
    if (hasPlugins())
    {
        juce::Array<juce::var> list;
        for (auto& p : plugins)
        {
            auto* slot = new juce::DynamicObject();
            slot->setProperty ("description", p.description);
            slot->setProperty ("state", p.state);
            slot->setProperty ("bypassed", p.bypassed);
            list.add (slot);
        }
        o->setProperty ("plugins", list);
    }
    return o;
}

Cue Cue::fromVar (const juce::var& v)
{
    Cue c;
    if (! v.isObject())
        return c;
    c.name = v.getProperty ("name", {}).toString();
    const auto path = v.getProperty ("file", {}).toString();
    if (juce::File::isAbsolutePath (path))
        c.file = juce::File (path);
    c.colour = (int) v.getProperty ("colour", 0);
    c.customColour = (juce::uint32) v.getProperty ("customColour", {}).toString().getHexValue64();
    c.trimIn = std::max (0.0, (double) v.getProperty ("trimIn", 0.0));
    c.trimOut = (double) v.getProperty ("trimOut", -1.0);
    if (auto* list = v.getProperty ("cuts", {}).getArray())
        for (auto& pair : *list)
            if (auto* p = pair.getArray(); p != nullptr && p->size() == 2)
                c.cuts.push_back (juce::Range<double>::between ((double) (*p)[0], (double) (*p)[1]));
    if (auto* list = v.getProperty ("pastes", {}).getArray())
        for (auto& pv : *list)
        {
            Paste paste;
            paste.at = std::max (0.0, (double) pv.getProperty ("at", 0.0));
            if (auto* pieces = pv.getProperty ("pieces", {}).getArray())
                for (auto& pair : *pieces)
                    if (auto* p = pair.getArray(); p != nullptr && p->size() == 2)
                        paste.pieces.push_back ({ std::min ((double) (*p)[0], (double) (*p)[1]), std::max ((double) (*p)[0], (double) (*p)[1]), true });
            if (! paste.pieces.empty())
                c.pastes.push_back (paste);
        }
    c.fadeIn = fadeFromVar (v.getProperty ("fadeIn", {}), c.fadeIn);
    c.fadeOut = fadeFromVar (v.getProperty ("fadeOut", {}), c.fadeOut);
    c.stopFade = fadeFromVar (v.getProperty ("stopFade", {}), c.stopFade);
    c.levelDb = juce::jlimit (minLevelDb, maxLevelDb, (float) v.getProperty ("levelDb", 0.0f));
    c.pan = juce::jlimit (-1.0f, 1.0f, (float) v.getProperty ("pan", 0.0f));
    c.output.firstChannel = std::max (0, (int) v.getProperty ("outFirst", 0));
    c.output.stereo = (bool) v.getProperty ("outStereo", true);
    c.fadeOthers = (bool) v.getProperty ("fadeOthers", false);
    c.loop = (bool) v.getProperty ("loop", false);
    c.hotkey = v.getProperty ("hotkey", {}).toString();
    c.midi = MidiTrigger::fromVar (v.getProperty ("midi", {}));
    if (auto* list = v.getProperty ("plugins", {}).getArray())
        for (int i = 0; i < std::min (3, list->size()); ++i)
        {
            c.plugins[(size_t) i].description = (*list)[i].getProperty ("description", {}).toString();
            c.plugins[(size_t) i].state = (*list)[i].getProperty ("state", {}).toString();
            c.plugins[(size_t) i].bypassed = (bool) (*list)[i].getProperty ("bypassed", false);
        }
    return c;
}
