#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

/** Pulls the audio from a link and makes it a 24-bit WAV at a given sample rate:
      - YouTube and the other sites yt-dlp knows: yt-dlp.exe fetches the audio
      - Dropbox / OneDrive share links and links straight to a file: downloaded directly
    then ffmpeg.exe converts it. Both programs live in "tools" next to Stinger.exe (the installer puts them there).
    One download at a time, the rest wait their turn. Changes are broadcast on the message thread. */
class Downloader : public juce::ChangeBroadcaster,
                   private juce::Thread
{
public:
    enum class State { queued, running, done, failed, cancelled };

    struct Job
    {
        int id = 0;
        juce::String url;
        juce::String title;             // the song's title once known (else the link)
        State state = State::queued;
        double progress = 0.0;          // 0..1
        juce::String status;            // "Downloading 42%", "Converting", the error...
        juce::File result;              // the WAV, when done
        juce::File folder;              // where the WAV goes
        double sampleRate = 48000.0;
    };
    using Done = std::function<void (const Job&)>;

    Downloader();
    ~Downloader() override;

    /** Queues a link. done is called on the message thread when it has finished (done, failed or cancelled). */
    int add (const juce::String& url, const juce::File& folder, double sampleRate, Done done = {});
    void cancel (int id);
    /** Takes a finished job off the list. */
    void remove (int id);
    std::vector<Job> getJobs() const;
    bool getJob (int id, Job& out) const;
    bool isBusy() const;

    static juce::File toolsFolder();
    /** A helper program by name ("yt-dlp", "ffmpeg", "deno"): in the tools folder, or on a Mac also Homebrew's. */
    static juce::File tool (const juce::String& name);
    static juce::File ytDlp() { return tool ("yt-dlp"); }
    static juce::File ffmpeg() { return tool ("ffmpeg"); }
    static bool toolsInstalled() { return ytDlp().existsAsFile() && ffmpeg().existsAsFile(); }

    /** A Dropbox / OneDrive / SharePoint share link as a link to the file itself; "" if it isn't one. */
    static juce::String directLink (const juce::String& url);
    /** The http(s) links in some pasted text, in order, without repeats. */
    static juce::StringArray linksIn (const juce::String& text);

    /** Runs "yt-dlp -U" (sites change; a new yt-dlp usually fixes a link that stopped working). */
    void updateYtDlp (std::function<void (const juce::String& output)> done);

private:
    void run() override;
    bool process (Job job);
    bool fetchDirect (int id, const juce::String& link, const juce::File& tempFolder, juce::File& file, juce::String& title);
    bool fetchWithYtDlp (int id, const juce::String& url, const juce::File& tempFolder, juce::File& file, juce::String& title);
    bool convert (int id, const juce::File& in, const juce::File& out, double sampleRate, juce::String& error);
    /** Runs a program, handing each line it prints to onLine; false if cancelled. */
    bool runTool (int id, const juce::StringArray& command, std::function<void (const juce::String&)> onLine, int& exitCode);

    void update (int id, std::function<void (Job&)> change);
    bool isCancelled (int id) const;

    mutable juce::CriticalSection lock;
    std::vector<Job> jobs;
    std::map<int, Done> callbacks;
    std::set<int> cancelled;
    juce::ChildProcess* runningTool = nullptr;   // what the worker is running now (cancel kills it)
    int runningJob = 0;
    int lastId = 0;
    juce::WaitableEvent wake;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
