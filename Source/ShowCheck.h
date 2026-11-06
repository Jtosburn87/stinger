#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <map>
#include <mutex>

/** One thing wrong with a show (ShowController::checkShow). */
struct ShowIssue
{
    enum class Kind { noDevice, missingAudio, unreadableAudio, missingPlugin, noSuchOutput, emptyEntry };
    Kind kind = Kind::missingAudio;
    int cueId = -1;                 // the button it's about, -1 = none
    juce::String text;
    juce::File file;                // missingAudio: the file that's gone
};

/** Looks for files by name under some folders, on its own low priority thread, so a search of the whole computer
    can run without touching playback or the screen. Poll isSearching() / takeFound() from the message thread. */
class FileSearch : private juce::Thread
{
public:
    FileSearch() : juce::Thread ("Stinger file search") {}
    ~FileSearch() override { stopThread (10000); }

    /** names: file names to find (any case). Stops by itself once every name has been found. */
    void start (const juce::StringArray& names, const juce::Array<juce::File>& roots);
    void cancel() { signalThreadShouldExit(); }
    bool isSearching() const { return isThreadRunning(); }
    /** The folder being looked through right now. */
    juce::String getProgress() const;
    /** What's been found since the last call: file name (lower case) -> where. */
    std::map<juce::String, juce::File> takeFound();
    int getFoldersSearched() const { return folders.load(); }

    /** Every drive on this computer. */
    static juce::Array<juce::File> wholeComputer();

private:
    void run() override;

    mutable std::mutex lock;
    juce::String current;
    std::map<juce::String, juce::File> found;
    juce::StringArray wanted;
    juce::Array<juce::File> startAt;
    std::atomic<int> folders { 0 };
};
