#include "Autosave.h"

Autosave::Autosave (juce::File f) : juce::Thread ("Stinger autosave"), folder (std::move (f))
{
    startThread (juce::Thread::Priority::low);
}

Autosave::~Autosave()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (5000);
}

void Autosave::submit (juce::var snapshot)
{
    {
        const juce::ScopedLock sl (lock);
        waiting = std::move (snapshot);
        hasWaiting = true;
        deleteWaiting = false;
    }
    wake.signal();
}

void Autosave::discard()
{
    {
        const juce::ScopedLock sl (lock);
        waiting = juce::var();
        hasWaiting = false;
        deleteWaiting = true;
    }
    wake.signal();
}

bool Autosave::isIdle() const
{
    const juce::ScopedLock sl (lock);
    return ! hasWaiting && ! deleteWaiting && ! busy.load();
}

void Autosave::run()
{
    while (! threadShouldExit())
    {
        wake.wait (-1);
        drain();
    }
    drain();   // a discard asked for as Stinger closes
}

void Autosave::drain()
{
    for (;;)
    {
        juce::var snapshot;
        bool write = false, remove = false;
        {
            const juce::ScopedLock sl (lock);
            write = hasWaiting;
            remove = deleteWaiting;
            snapshot = std::move (waiting);
            waiting = juce::var();
            hasWaiting = deleteWaiting = false;
            busy = write || remove;
        }
        if (remove)
        {
            fileIn (folder).deleteFile();
            lastWritten.clear();
        }
        else if (write)
        {
            auto text = juce::JSON::toString (snapshot);
            if (text != lastWritten && folder.createDirectory().wasOk())
            {
                // written beside the copy, then swapped in: a crash in the middle leaves the last good one
                juce::TemporaryFile temp (fileIn (folder));
                if (temp.getFile().replaceWithText (text) && temp.overwriteTargetFileWithTemporary())
                {
                    lastWritten = std::move (text);
                    ++writes;
                }
            }
        }
        busy = false;
        if (! write && ! remove)
            return;
    }
}

std::optional<Autosave::Found> Autosave::find (const juce::File& folder)
{
    const auto file = fileIn (folder);
    if (! file.existsAsFile())
        return std::nullopt;
    juce::var v;
    if (juce::JSON::parse (file.loadFileAsString(), v).failed())
        return std::nullopt;
    Found found;
    found.file = file;
    found.saved = file.getLastModificationTime();
    if (const auto path = v.getProperty ("recovery", {}).getProperty ("showFile", {}).toString(); path.isNotEmpty())
        found.showFile = juce::File (path);
    return found;
}

void Autosave::setAside (const juce::File& folder)
{
    const auto file = fileIn (folder);
    if (file.existsAsFile())
        file.moveFileTo (folder.getChildFile ("last-discarded.stinger"));
}
