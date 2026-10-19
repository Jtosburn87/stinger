#include "Downloader.h"

#if JUCE_WINDOWS
 static constexpr bool isWindows = true;
#else
 static constexpr bool isWindows = false;
#endif

namespace
{
    bool isAudioPath (const juce::String& path)
    {
        const auto p = path.upToFirstOccurrenceOf ("?", false, false).toLowerCase();
        for (auto ext : { ".mp3", ".wav", ".wave", ".m4a", ".aac", ".flac", ".ogg", ".opus", ".aif", ".aiff", ".wma", ".mp4" })
            if (p.endsWith (ext))
                return true;
        return false;
    }

    juce::String base64Url (const juce::String& text)
    {
        auto b64 = juce::Base64::toBase64 (text.toRawUTF8(), text.getNumBytesAsUTF8());
        return b64.replaceCharacter ('/', '_').replaceCharacter ('+', '-').trimCharactersAtEnd ("=");
    }

    juce::String withParameter (const juce::String& url, const juce::String& name, const juce::String& value)
    {
        const auto fragment = url.fromFirstOccurrenceOf ("#", true, false);
        auto u = url.upToFirstOccurrenceOf ("#", false, false);
        const auto query = u.fromFirstOccurrenceOf ("?", false, false);
        juce::StringArray params;
        if (query.isNotEmpty())
            params.addTokens (query, "&", {});
        for (int i = params.size(); --i >= 0;)
            if (params[i].upToFirstOccurrenceOf ("=", false, false) == name)
                params.remove (i);
        params.add (name + "=" + value);
        return u.upToFirstOccurrenceOf ("?", false, false) + "?" + params.joinIntoString ("&") + fragment;
    }

    /** "Duration: 00:03:21.45," -> seconds (-1 if not that line). */
    double durationIn (const juce::String& line)
    {
        const auto at = line.indexOf ("Duration: ");
        if (at < 0)
            return -1.0;
        const auto t = juce::StringArray::fromTokens (line.substring (at + 10).upToFirstOccurrenceOf (",", false, false), ":", {});
        if (t.size() != 3)
            return -1.0;
        return t[0].getIntValue() * 3600.0 + t[1].getIntValue() * 60.0 + t[2].getDoubleValue();
    }
}

Downloader::Downloader() : juce::Thread ("Downloader")
{
    startThread();
}

Downloader::~Downloader()
{
    alive->store (false);
    signalThreadShouldExit();
    {
        const juce::ScopedLock sl (lock);
        if (runningTool != nullptr)
            runningTool->kill();
    }
    wake.signal();
    stopThread (10000);
}

juce::File Downloader::toolsFolder()
{
    // installed: "tools" next to Stinger.exe (Mac: Stinger.app/Contents/Helpers). A developer build: tools\bin
    // of the source folder above it.
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
   #if JUCE_MAC
    const auto installed = exe.getParentDirectory().getSiblingFile ("Helpers");
   #else
    const auto installed = exe.getSiblingFile ("tools");
   #endif
    const juce::String ytDlpName = isWindows ? "yt-dlp.exe" : "yt-dlp";
    if (installed.getChildFile (ytDlpName).existsAsFile())
        return installed;
    for (auto dir = exe.getParentDirectory(); dir.getParentDirectory() != dir; dir = dir.getParentDirectory())
        if (auto dev = dir.getChildFile ("tools").getChildFile ("bin"); dev.getChildFile (ytDlpName).existsAsFile())
            return dev;
    return installed;
}

juce::File Downloader::tool (const juce::String& name)
{
    const auto file = toolsFolder().getChildFile (isWindows ? name + ".exe" : name);
   #if JUCE_MAC
    if (! file.existsAsFile())
        for (auto* dir : { "/opt/homebrew/bin", "/usr/local/bin" })
            if (auto brew = juce::File (dir).getChildFile (name); brew.existsAsFile())
                return brew;
   #endif
    return file;
}

juce::String Downloader::directLink (const juce::String& url)
{
    const auto lower = url.toLowerCase();
    const auto host = lower.fromFirstOccurrenceOf ("://", false, false).upToFirstOccurrenceOf ("/", false, false);
    if (host.endsWith ("dropbox.com"))
        return withParameter (url, "dl", "1");                     // dl=0 shows a page, dl=1 sends the file
    if (host == "1drv.ms" || host.endsWith ("onedrive.live.com"))
        return "https://api.onedrive.com/v1.0/shares/u!" + base64Url (url) + "/root/content";
    if (host.endsWith ("sharepoint.com"))
        return withParameter (url, "download", "1");
    if (host.endsWith ("drive.google.com"))
    {
        // .../file/d/<id>/view -> uc?export=download&id=<id>
        const auto id = url.fromFirstOccurrenceOf ("/d/", false, false).upToFirstOccurrenceOf ("/", false, false);
        if (id.isNotEmpty())
            return "https://drive.google.com/uc?export=download&id=" + id;
    }
    if (isAudioPath (url.fromFirstOccurrenceOf ("://", false, false).fromFirstOccurrenceOf ("/", true, false)))
        return url;
    return {};
}

juce::StringArray Downloader::linksIn (const juce::String& text)
{
    juce::StringArray links;
    for (auto token : juce::StringArray::fromTokens (text, " \t\r\n,;\"'<>", {}))
    {
        token = token.trim().trimCharactersAtEnd (".)]");
        if ((token.startsWithIgnoreCase ("https://") || token.startsWithIgnoreCase ("http://")) && token.length() > 10)
            links.addIfNotAlreadyThere (token);
    }
    return links;
}

int Downloader::add (const juce::String& url, const juce::File& folder, double sampleRate, Done done)
{
    Job job;
    {
        const juce::ScopedLock sl (lock);
        job.id = ++lastId;
        job.url = url.trim();
        job.title = job.url;
        job.folder = folder;
        job.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        job.status = "Waiting";
        jobs.push_back (job);
        if (done)
            callbacks[job.id] = std::move (done);
    }
    wake.signal();
    sendChangeMessage();
    return job.id;
}

void Downloader::cancel (int id)
{
    const juce::ScopedLock sl (lock);
    for (auto& j : jobs)
        if (j.id == id && (j.state == State::queued || j.state == State::running))
        {
            if (j.state == State::queued)
            {
                j.state = State::cancelled;
                j.status = "Cancelled";
            }
            else
                j.status = "Cancelling...";
        }
    cancelled.insert (id);
    if (runningJob == id && runningTool != nullptr)
        runningTool->kill();
    sendChangeMessage();
}

void Downloader::remove (int id)
{
    const juce::ScopedLock sl (lock);
    jobs.erase (std::remove_if (jobs.begin(), jobs.end(), [id] (auto& j)
                                { return j.id == id && j.state != State::running && j.state != State::queued; }),
                jobs.end());
    sendChangeMessage();
}

std::vector<Downloader::Job> Downloader::getJobs() const
{
    const juce::ScopedLock sl (lock);
    return jobs;
}

bool Downloader::getJob (int id, Job& out) const
{
    const juce::ScopedLock sl (lock);
    for (auto& j : jobs)
        if (j.id == id)
        {
            out = j;
            return true;
        }
    return false;
}

bool Downloader::isBusy() const
{
    const juce::ScopedLock sl (lock);
    for (auto& j : jobs)
        if (j.state == State::queued || j.state == State::running)
            return true;
    return false;
}

void Downloader::update (int id, std::function<void (Job&)> change)
{
    {
        const juce::ScopedLock sl (lock);
        for (auto& j : jobs)
            if (j.id == id)
                change (j);
    }
    sendChangeMessage();
}

bool Downloader::isCancelled (int id) const
{
    const juce::ScopedLock sl (lock);
    return cancelled.count (id) > 0 || threadShouldExit();
}

void Downloader::run()
{
    while (! threadShouldExit())
    {
        Job next;
        {
            const juce::ScopedLock sl (lock);
            for (auto& j : jobs)
                if (j.state == State::queued)
                {
                    j.state = State::running;
                    j.status = "Starting";
                    next = j;
                    break;
                }
        }
        if (next.id == 0)
        {
            wake.wait (500);
            continue;
        }
        sendChangeMessage();
        process (next);

        Job finished;
        Done done;
        {
            const juce::ScopedLock sl (lock);
            getJob (next.id, finished);
            if (auto it = callbacks.find (next.id); it != callbacks.end())
            {
                done = std::move (it->second);
                callbacks.erase (it);
            }
        }
        if (done)
            juce::MessageManager::callAsync ([alive = alive, done, finished] { if (alive->load()) done (finished); });
    }
}

bool Downloader::process (Job job)
{
    const int id = job.id;
    auto fail = [this, id] (const juce::String& error)
    {
        update (id, [this, &error] (Job& j)
        {
            const bool wasCancelled = cancelled.count (j.id) > 0;
            j.state = wasCancelled ? State::cancelled : State::failed;
            j.status = wasCancelled ? juce::String ("Cancelled") : error;
        });
        return false;
    };
    if (! toolsInstalled())
        return fail ("The download tools (yt-dlp, ffmpeg) aren't installed: install Stinger with its installer.");

    auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Stinger download " + juce::String (id));
    temp.deleteRecursively();
    temp.createDirectory();
    struct Cleanup { juce::File dir; ~Cleanup() { dir.deleteRecursively(); } } cleanup { temp };

    juce::File downloaded;
    juce::String title;
    const auto direct = directLink (job.url);
    bool fetched = direct.isNotEmpty() ? fetchDirect (id, direct, temp, downloaded, title)
                                       : fetchWithYtDlp (id, job.url, temp, downloaded, title);
    if (! fetched && direct.isNotEmpty() && ! isCancelled (id))
    {
        // the direct fetch failed: yt-dlp knows Dropbox, OneDrive and odd servers. If it fails too, show the first error.
        Job now;
        getJob (id, now);
        const auto directError = now.status;
        for (auto& f : temp.findChildFiles (juce::File::findFiles, false))
            f.deleteFile();
        downloaded = juce::File();
        update (id, [] (Job& j) { j.progress = 0.0; j.status = "Trying yt-dlp"; });
        fetched = fetchWithYtDlp (id, job.url, temp, downloaded, title);
        if (! fetched && ! isCancelled (id))
            update (id, [&directError] (Job& j) { j.status = directError; });
    }
    if (! fetched)
    {
        Job now;
        getJob (id, now);
        return fail (now.status.startsWith ("Downloading") || now.status == "Starting" ? juce::String ("Download failed") : now.status);
    }
    if (isCancelled (id))
        return fail ("Cancelled");

    if (title.isEmpty())
        title = downloaded.getFileNameWithoutExtension();
    if (! job.folder.createDirectory())
        return fail ("Couldn't make the folder " + job.folder.getFullPathName());
    const auto out = job.folder.getChildFile (juce::File::createLegalFileName (title).trim() + ".wav").getNonexistentSibling (true);
    update (id, [&title] (Job& j) { j.title = title; j.status = "Converting"; j.progress = 0.85; });

    juce::String error;
    if (! convert (id, downloaded, out, job.sampleRate, error))
    {
        out.deleteFile();
        return fail (error);
    }
    update (id, [&out] (Job& j) { j.state = State::done; j.progress = 1.0; j.result = out; j.status = "Done"; });
    return true;
}

bool Downloader::runTool (int id, const juce::StringArray& command, std::function<void (const juce::String&)> onLine, int& exitCode)
{
    juce::ChildProcess process;
    if (! process.start (command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        onLine ("ERROR: couldn't start " + juce::File (command[0]).getFileName());
        exitCode = -1;
        return true;
    }
    {
        const juce::ScopedLock sl (lock);
        runningTool = &process;
        runningJob = id;
    }
    juce::MemoryOutputStream line;
    char c = 0;
    // one byte at a time: readProcessOutput waits until it has as many as asked for
    while (process.readProcessOutput (&c, 1) == 1)
    {
        if (c == '\n' || c == '\r')
        {
            if (line.getDataSize() > 0)
                onLine (line.toUTF8());
            line.reset();
        }
        else
            line.writeByte (c);
    }
    if (line.getDataSize() > 0)
        onLine (line.toUTF8());
    process.waitForProcessToFinish (5000);
    exitCode = (int) process.getExitCode();
    {
        const juce::ScopedLock sl (lock);
        runningTool = nullptr;
        runningJob = 0;
    }
    return ! isCancelled (id);
}

bool Downloader::fetchWithYtDlp (int id, const juce::String& url, const juce::File& tempFolder, juce::File& file, juce::String& title)
{
    juce::StringArray command { ytDlp().getFullPathName(),
                                "--no-playlist", "--newline", "--progress", "--no-simulate", "--no-colors", "--encoding", "utf-8",
                                "-f", "bestaudio/best",
                                "--ffmpeg-location", ffmpeg().getParentDirectory().getFullPathName(),
                                "-o", tempFolder.getChildFile ("audio.%(ext)s").getFullPathName(),
                                "--print", "before_dl:TITLE %(title)s",
                                "--print", "after_move:FILE %(filepath)s",
                                "--progress-template", "download:PROGRESS %(progress.downloaded_bytes)s %(progress.total_bytes,progress.total_bytes_estimate)s" };
    if (auto deno = tool ("deno"); deno.existsAsFile())
        command.addArray ({ "--js-runtimes", "deno:" + deno.getFullPathName() });   // YouTube needs it for all its formats
    command.add (url);
    juce::String error;
    int exitCode = 0;
    update (id, [] (Job& j) { j.status = "Finding the audio"; });
    const bool ok = runTool (id, command, [&] (const juce::String& l)
    {
        if (l.startsWith ("TITLE "))
        {
            title = l.substring (6).trim();
            update (id, [&] (Job& j) { j.title = title; });
        }
        else if (l.startsWith ("FILE "))
            file = juce::File (l.substring (5).trim());
        else if (l.startsWith ("PROGRESS "))
        {
            const auto parts = juce::StringArray::fromTokens (l.substring (9), " ", {});
            const double got = parts[0].getDoubleValue(), total = parts[1].getDoubleValue();
            if (total > 0.0)
                update (id, [&] (Job& j)
                {
                    j.progress = 0.85 * juce::jlimit (0.0, 1.0, got / total);
                    j.status = "Downloading " + juce::String (juce::roundToInt (100.0 * got / total)) + "%";
                });
        }
        else if (l.startsWith ("ERROR:"))
            error = l.substring (6).trim();
    }, exitCode);
    if (! ok)
        return false;
    if (! file.existsAsFile())
    {
        // the file may not have been printed (an older yt-dlp): take what's in the folder
        for (auto& f : tempFolder.findChildFiles (juce::File::findFiles, false, "audio.*"))
            if (! f.hasFileExtension ("part;ytdl"))
                file = f;
    }
    if (exitCode != 0 || ! file.existsAsFile())
    {
        if (error.containsIgnoreCase ("Unsupported URL"))
            error = "That link isn't a song yt-dlp can fetch (for Dropbox / OneDrive, use the share link).";
        else if (error.isEmpty())
            error = "yt-dlp couldn't fetch it (exit code " + juce::String (exitCode) + ")";
        update (id, [&] (Job& j) { j.status = error; });
        return false;
    }
    return true;
}

bool Downloader::fetchDirect (int id, const juce::String& link, const juce::File& tempFolder, juce::File& file, juce::String& title)
{
    update (id, [] (Job& j) { j.status = "Connecting"; });
    juce::StringPairArray headers;
    int status = 0;
    std::unique_ptr<juce::InputStream> stream;
    auto header = [&headers] (const juce::String& name)
    {
        for (auto& key : headers.getAllKeys())
            if (key.equalsIgnoreCase (name))
                return headers[key];
        return juce::String();
    };
    // redirects followed here, not by JUCE: Dropbox sends the file from a second server whose address ends in
    // "#", and passed on as it is that server answers 400
    juce::String current = link;
    for (int hop = 0; hop < 10; ++hop)
    {
        headers.clear();
        status = 0;
        stream = juce::URL (current).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                            .withConnectionTimeoutMs (20000)
                                                            .withNumRedirectsToFollow (0)
                                                            .withResponseHeaders (&headers)
                                                            .withStatusCode (&status));
        auto location = header ("Location").upToFirstOccurrenceOf ("#", false, false).trim();
        if (status < 300 || status >= 400 || location.isEmpty())
            break;
        if (location.startsWith ("/"))   // relative: same server
            location = current.upToFirstOccurrenceOf ("://", true, false)
                     + current.fromFirstOccurrenceOf ("://", false, false).upToFirstOccurrenceOf ("/", false, false) + location;
        current = location;
        stream = nullptr;
    }
    auto failWith = [&] (const juce::String& error) { update (id, [&] (Job& j) { j.status = error; }); return false; };
    if (stream == nullptr || status >= 400)
        return failWith (status == 401 || status == 403 || status == 404
                           ? "The link didn't let Stinger in (" + juce::String (status) + "): share it as \"Anyone with the link\"."
                           : "Couldn't open the link" + (status > 0 ? " (" + juce::String (status) + ")" : juce::String()));

    // the file's name: from the server if it says, else from the link
    juce::String name;
    for (auto& key : headers.getAllKeys())
        if (key.equalsIgnoreCase ("Content-Disposition"))
        {
            const auto v = headers[key];
            if (v.contains ("filename*="))
                name = juce::URL::removeEscapeChars (v.fromFirstOccurrenceOf ("filename*=", false, false)
                                                         .fromFirstOccurrenceOf ("''", false, false).upToFirstOccurrenceOf (";", false, false));
            else if (v.contains ("filename="))
                name = v.fromFirstOccurrenceOf ("filename=", false, false).upToFirstOccurrenceOf (";", false, false).unquoted();
        }
        else if (key.equalsIgnoreCase ("Content-Type") && headers[key].startsWithIgnoreCase ("text/html"))
            return failWith ("That link opens a web page, not a file (a folder link?). Use the link of the song itself.");
    if (name.isEmpty() || name.equalsIgnoreCase ("unspecified"))   // (Dropbox says "unspecified")
        name = juce::URL::removeEscapeChars (link.upToFirstOccurrenceOf ("?", false, false).fromLastOccurrenceOf ("/", false, false));
    name = name.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);   // a server may send a path
    title = juce::File::createLegalFileName (name).upToLastOccurrenceOf (".", false, false);
    if (title.isEmpty())
        title = juce::File::createLegalFileName (name);
    update (id, [&] (Job& j) { j.title = title; });

    file = tempFolder.getChildFile ("download" + juce::File (name).getFileExtension());
    juce::FileOutputStream out (file);
    if (! out.openedOk())
        return failWith ("Couldn't write " + file.getFullPathName());
    const auto total = stream->getTotalLength();
    juce::HeapBlock<char> buffer (65536);
    juce::int64 got = 0;
    int lastPercent = -1;
    while (! stream->isExhausted())
    {
        if (isCancelled (id))
            return false;
        const int n = stream->read (buffer, 65536);
        if (n <= 0)
            break;
        out.write (buffer, (size_t) n);
        got += n;
        const int percent = total > 0 ? (int) (100 * got / total) : -1;
        if (percent != lastPercent)
        {
            lastPercent = percent;
            update (id, [&] (Job& j)
            {
                j.progress = total > 0 ? 0.85 * got / (double) total : 0.0;
                j.status = total > 0 ? "Downloading " + juce::String (percent) + "%" : "Downloading " + juce::String (got / 1048576) + " MB";
            });
        }
    }
    out.flush();
    if (got == 0)
        return failWith ("The link sent nothing.");
    return true;
}

bool Downloader::convert (int id, const juce::File& in, const juce::File& out, double sampleRate, juce::String& error)
{
    juce::StringArray command { ffmpeg().getFullPathName(), "-hide_banner", "-nostdin", "-y", "-i", in.getFullPathName(),
                                "-vn", "-map", "0:a:0", "-c:a", "pcm_s24le", "-ar", juce::String (juce::roundToInt (sampleRate)),
                                "-progress", "pipe:1", "-nostats", out.getFullPathName() };
    double duration = -1.0;
    juce::StringArray lastLines;
    int exitCode = 0;
    const bool ok = runTool (id, command, [&] (const juce::String& l)
    {
        if (const double d = durationIn (l); d > 0.0)
            duration = d;
        else if (l.startsWith ("out_time_us=") && duration > 0.0)
        {
            const double done = l.substring (12).getLargeIntValue() / 1.0e6;
            update (id, [&] (Job& j)
            {
                j.progress = 0.85 + 0.15 * juce::jlimit (0.0, 1.0, done / duration);
                j.status = "Converting " + juce::String (juce::roundToInt (100.0 * juce::jlimit (0.0, 1.0, done / duration))) + "%";
            });
        }
        else if (! l.contains ("="))
        {
            lastLines.add (l);
            if (lastLines.size() > 3)
                lastLines.remove (0);
        }
    }, exitCode);
    if (! ok)
        return false;
    if (exitCode != 0 || ! out.existsAsFile())
    {
        error = "Couldn't convert it to WAV: " + lastLines.joinIntoString (" ").trim().substring (0, 200);
        return false;
    }
    return true;
}

void Downloader::updateYtDlp (std::function<void (const juce::String&)> done)
{
    juce::Thread::launch ([alive = alive, done]
    {
        juce::ChildProcess p;
        juce::String output = "yt-dlp isn't installed.";
        if (ytDlp().existsAsFile() && p.start (juce::StringArray { ytDlp().getFullPathName(), "-U" }))
        {
            output = p.readAllProcessOutput();
            p.waitForProcessToFinish (60000);
        }
        juce::MessageManager::callAsync ([alive, done, output] { if (alive->load() && done) done (output); });
    });
}
