#pragma once
#include <juce_core/juce_core.h>
#include <optional>

/** Keeps a recovery copy of the show in its own folder; the real show file is only ever touched by Save.

    Nothing here runs on the audio thread and the caller never waits for the disk: submit() hands over a show that's
    already in memory, and a low priority thread turns it into text and writes it. If the disk is slow, the newest
    copy replaces the one waiting, so copies can't pile up. An unchanged show isn't written at all. */
class Autosave : private juce::Thread
{
public:
    explicit Autosave (juce::File folder);
    ~Autosave() override;

    /** Queues this show (a Show::toVar) to be written. It must not be touched by the caller afterwards. */
    void submit (juce::var snapshot);
    /** The show is safe (saved, replaced, or Stinger is closing properly): the recovery copy goes. */
    void discard();

    /** What a crash left behind. */
    struct Found
    {
        juce::File file;              // the recovery copy
        juce::File showFile;          // the show it came from (no file = never saved)
        juce::Time saved;             // when the copy was written
    };
    static std::optional<Found> find (const juce::File& folder);
    /** Moves the recovery copy aside (kept as the one last-discarded copy, in case No was the wrong answer). */
    static void setAside (const juce::File& folder);

    static juce::File fileIn (const juce::File& folder) { return folder.getChildFile ("recovery.stinger"); }

    /** For tests: true once everything handed over has been dealt with. */
    bool isIdle() const;
    int getWriteCount() const { return writes.load(); }

private:
    void run() override;
    void drain();

    const juce::File folder;
    mutable juce::CriticalSection lock;       // held only to swap the waiting copy, never around disk work
    juce::var waiting;
    bool hasWaiting = false, deleteWaiting = false;
    std::atomic<bool> busy { false };
    std::atomic<int> writes { 0 };
    juce::WaitableEvent wake;
    juce::String lastWritten;                 // worker thread only
};
