#include "ShowCheck.h"
#include <set>
#include <utility>
#include <vector>

void FileSearch::start (const juce::StringArray& names, const juce::Array<juce::File>& roots)
{
    stopThread (10000);
    {
        std::lock_guard<std::mutex> sl (lock);
        wanted.clear();
        for (auto& n : names)
            wanted.addIfNotAlreadyThere (n.toLowerCase());
        startAt = roots;
        found.clear();
        current.clear();
    }
    folders = 0;
    startThread (juce::Thread::Priority::low);
}

juce::String FileSearch::getProgress() const
{
    std::lock_guard<std::mutex> sl (lock);
    return current;
}

std::map<juce::String, juce::File> FileSearch::takeFound()
{
    std::lock_guard<std::mutex> sl (lock);
    return std::exchange (found, {});
}

juce::Array<juce::File> FileSearch::wholeComputer()
{
    juce::Array<juce::File> roots;
    juce::File::findFileSystemRoots (roots);
    return roots;
}

/** System folders can't hold a show's audio, and walking them takes most of the time on a whole-computer search. */
static bool skipFolder (const juce::File& dir)
{
    static const std::set<juce::String> names { "windows", "$recycle.bin", "system volume information", "$windows.~bt", "$windows.~ws",
                                                "programdata", "node_modules", ".git", ".trash", ".trashes",
                                                ".spotlight-v100", ".fseventsd", "appdata" };
    return dir.isSymbolicLink() || names.count (dir.getFileName().toLowerCase()) > 0;
}

void FileSearch::run()
{
    std::set<juce::String> names;
    std::vector<juce::File> stack;
    {
        std::lock_guard<std::mutex> sl (lock);
        for (auto& n : wanted)
            names.insert (n);
        for (int i = startAt.size(); --i >= 0;)
            stack.push_back (startAt[i]);
    }
    while (! stack.empty() && ! names.empty() && ! threadShouldExit())
    {
        const auto dir = stack.back();
        stack.pop_back();
        {
            std::lock_guard<std::mutex> sl (lock);
            current = dir.getFullPathName();
        }
        ++folders;
        std::vector<juce::File> below;
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
        {
            if (threadShouldExit())
                return;
            const auto f = entry.getFile();
            if (entry.isDirectory())
            {
                if (! skipFolder (f))
                    below.push_back (f);
            }
            else if (const auto name = f.getFileName().toLowerCase(); names.count (name) > 0)
            {
                names.erase (name);
                std::lock_guard<std::mutex> sl (lock);
                found[name] = f;
            }
        }
        stack.insert (stack.end(), below.rbegin(), below.rend());
    }
    std::lock_guard<std::mutex> sl (lock);
    current.clear();
}
