#include "ImportPanel.h"
#include "CueSettings.h"
#include "Look.h"

namespace
{
    juce::String buttonName (int id)
    {
        return juce::String::charToString ((juce::juce_wchar) ('A' + Show::bankOf (id))) + juce::String (Show::slotOf (id) + 1);
    }

    void styleNote (juce::Label& l)
    {
        l.setColour (juce::Label::textColourId, Look::textDim);
        l.setFont (Look::font (13.0f));
        l.setJustificationType (juce::Justification::topLeft);
    }
}

// ---------------------------------------------------------------------------------------------
// Files: choose several at once

class ImportPanel::FilesTab : public juce::Component
{
public:
    explicit FilesTab (ImportPanel& p) : panel (p)
    {
        styleNote (note);
        note.setText ("Choose audio files (WAV, MP3, AIFF, FLAC, OGG...) - as many as you like. They go onto the empty "
                      "buttons from button 1 of the bank on screen, in name order, skipping buttons in use and carrying on "
                      "into the next banks.\n\nTip: you can also drag several files (or a folder) from Explorer onto a button.",
                      juce::dontSendNotification);
        choose.onClick = [this] { chooseFiles(); };
        result.setColour (juce::Label::textColourId, Look::text);
        result.setFont (Look::font (14.0f));
        for (auto* c : std::initializer_list<juce::Component*> { &note, &choose, &result })
            addAndMakeVisible (c);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        note.setBounds (r.removeFromTop (90));
        r.removeFromTop (8);
        choose.setBounds (r.removeFromTop (34).removeFromLeft (200));
        r.removeFromTop (12);
        result.setBounds (r.removeFromTop (60));
    }

private:
    void chooseFiles()
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose audio files", juce::File(), panel.controller.getLoader().wildcards());
        juce::Component::SafePointer<FilesTab> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResults().isEmpty())
                return;
            auto files = fc.getResults();
            std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                       { return a.getFileName().compareNatural (b.getFileName()) < 0; });
            juce::StringArray where;
            int missed = 0;
            for (auto& f : files)
            {
                const int id = safe->panel.place (f, f.getFileNameWithoutExtension());
                if (id >= 0)
                    where.add (buttonName (id));
                else
                    ++missed;
            }
            juce::String text;
            if (! where.isEmpty())
                text << "Put " << where.size() << (where.size() == 1 ? " file" : " files") << " on "
                     << (where.size() == 1 ? where[0] : where[0] + juce::String::fromUTF8 ("\xe2\x80\x93") + where[where.size() - 1]) << ".";
            if (missed > 0)
                text << " " << missed << " didn't fit: every button is in use.";
            safe->result.setText (text, juce::dontSendNotification);
        });
    }

    ImportPanel& panel;
    juce::Label note, result;
    juce::TextButton choose { "Choose files..." };
    std::unique_ptr<juce::FileChooser> chooser;
};

// ---------------------------------------------------------------------------------------------
// Links: paste YouTube links, download each or all

class ImportPanel::LinksTab : public juce::Component,
                              private juce::ListBoxModel,
                              private juce::Timer
{
public:
    explicit LinksTab (ImportPanel& p) : panel (p)
    {
        paste.setMultiLine (true, false);
        paste.setReturnKeyStartsNewLine (true);
        paste.setTextToShowWhenEmpty ("Paste YouTube, Dropbox or OneDrive links here (one per line), then Add",
                                      Look::textFaint);
        add.onClick = [this] { addLinks(); };
        all.onClick = [this] { downloadAll(); };
        clear.onClick = [this] { clearFinished(); };
        update.onClick = [this] { updateTools(); };
        all.setColour (juce::TextButton::buttonColourId, Look::green.darker (0.3f));
        update.setTooltip ("YouTube changes often: if links stop working, a new yt-dlp usually fixes it");
        clear.setTooltip ("Take the finished downloads off the list");
        list.setModel (this);
        list.setRowHeight (52);
        list.setColour (juce::ListBox::backgroundColourId, Look::ground);
        styleNote (note);
        if (! Downloader::toolsInstalled())
            note.setText ("The download tools (yt-dlp and ffmpeg) aren't installed. Install Stinger with its installer to use links.",
                          juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*> { &paste, &add, &all, &clear, &update, &list, &note })
            addAndMakeVisible (c);
        startTimerHz (8);
    }

    ~LinksTab() override { list.setModel (nullptr); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        auto top = r.removeFromTop (84);
        add.setBounds (top.removeFromRight (90).removeFromTop (34));
        top.removeFromRight (8);
        paste.setBounds (top);
        r.removeFromTop (10);
        auto buttons = r.removeFromBottom (34);
        all.setBounds (buttons.removeFromLeft (140));
        buttons.removeFromLeft (8);
        clear.setBounds (buttons.removeFromLeft (120));
        update.setBounds (buttons.removeFromRight (130));
        r.removeFromBottom (8);
        note.setBounds (r.removeFromBottom (note.getText().isEmpty() ? 0 : 36));
        list.setBounds (r);
    }

    /** A download finished: put it on a button. */
    void finished (int uid, const Downloader::Job& job)
    {
        for (auto& row : rows)
            if (row.uid == uid)
            {
                if (job.state == Downloader::State::done)
                {
                    row.placed = panel.place (job.result, job.title);
                    row.note = row.placed >= 0 ? "On button " + buttonName (row.placed)
                                               : "Saved, but every button is in use: " + job.result.getFileName();
                }
                row.title = job.title;
            }
        list.repaint();
    }

private:
    struct Row
    {
        int uid = 0;
        juce::String url, title;
        int job = 0;            // the download (0 = not started)
        int placed = -1;        // the button it went on
        juce::String note;
    };

    class RowComponent : public juce::Component
    {
    public:
        explicit RowComponent (LinksTab& t) : tab (t)
        {
            action.onClick = [this] { tab.rowAction (row); };
            remove.onClick = [this] { tab.removeRow (row); };
            remove.setTooltip ("Take this link off the list");
            action.setWantsKeyboardFocus (false);
            remove.setWantsKeyboardFocus (false);
            addAndMakeVisible (action);
            addAndMakeVisible (remove);
        }

        void update (int r)
        {
            row = r;
            if (auto* data = tab.rowAt (row))
            {
                Downloader::Job job;
                const bool has = data->job != 0 && tab.panel.downloader.getJob (data->job, job);
                const bool running = has && (job.state == Downloader::State::running || job.state == Downloader::State::queued);
                action.setButtonText (! has ? "Download" : running ? "Cancel"
                                      : job.state == Downloader::State::done ? "Again" : "Retry");
                action.setEnabled (Downloader::toolsInstalled());
                remove.setEnabled (! running);
            }
            repaint();
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 10);
            remove.setBounds (r.removeFromRight (30));
            r.removeFromRight (6);
            action.setBounds (r.removeFromRight (90));
        }

        void paint (juce::Graphics& g) override
        {
            auto* data = tab.rowAt (row);
            if (data == nullptr)
                return;
            g.setColour (Look::line);
            g.fillRect (0, getHeight() - 1, getWidth(), 1);
            Downloader::Job job;
            const bool has = data->job != 0 && tab.panel.downloader.getJob (data->job, job);
            auto r = getLocalBounds().reduced (10, 7).withTrimmedRight (140);
            g.setColour (Look::text);
            g.setFont (Look::font (14.0f, true));
            const auto title = has && job.title.isNotEmpty() ? job.title : data->title.isNotEmpty() ? data->title : data->url;
            g.drawText (title, r.removeFromTop (18), juce::Justification::centredLeft, true);
            auto status = r.removeFromTop (16);
            juce::String text = ! has ? "Ready" : data->note.isNotEmpty() ? data->note : job.status;
            const bool failed = has && job.state == Downloader::State::failed;
            g.setColour (failed ? Look::red : has && job.state == Downloader::State::done ? Look::green : Look::textDim);
            g.setFont (Look::font (12.0f));
            g.drawText (text, status, juce::Justification::centredLeft, true);
            auto bar = r.removeFromTop (6).withTrimmedTop (2).toFloat();
            g.setColour (juce::Colour (0xff23282f));
            g.fillRoundedRectangle (bar, 2.0f);
            if (has)
            {
                g.setColour (failed ? Look::red : job.state == Downloader::State::done ? Look::green : Look::blue);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, job.progress)), 2.0f);
            }
        }

    private:
        LinksTab& tab;
        int row = -1;
        juce::TextButton action { "Download" }, remove { juce::String::fromUTF8 ("\xc3\x97") };
    };

    Row* rowAt (int i) { return i >= 0 && i < (int) rows.size() ? &rows[(size_t) i] : nullptr; }

    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool, juce::Component* existing) override
    {
        auto* c = dynamic_cast<RowComponent*> (existing);
        if (c == nullptr)
        {
            delete existing;
            c = new RowComponent (*this);
        }
        c->update (row);
        return c;
    }

    void timerCallback() override
    {
        for (int i = 0; i < (int) rows.size(); ++i)
            if (auto* c = dynamic_cast<RowComponent*> (list.getComponentForRowNumber (i)))
                c->update (i);
    }

    void addLinks()
    {
        const auto links = Downloader::linksIn (paste.getText());
        if (links.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Links", "No links found: they start with https://");
            return;
        }
        for (auto& l : links)
        {
            Row row;
            row.uid = ++lastUid;
            row.url = l;
            rows.push_back (row);
        }
        paste.clear();
        list.updateContent();
        list.repaint();
    }

    void startRow (Row& row)
    {
        juce::Component::SafePointer<LinksTab> safe (this);
        const int uid = row.uid;
        row.note.clear();
        row.placed = -1;
        row.job = panel.downloader.add (row.url, CueSettings::downloadFolder (panel.controller),
                                        panel.controller.getEngine().getSampleRate(),
                                        [safe, uid] (const Downloader::Job& job) { if (safe != nullptr) safe->finished (uid, job); });
    }

    void rowAction (int i)
    {
        auto* row = rowAt (i);
        if (row == nullptr)
            return;
        Downloader::Job job;
        if (row->job != 0 && panel.downloader.getJob (row->job, job)
            && (job.state == Downloader::State::running || job.state == Downloader::State::queued))
            panel.downloader.cancel (row->job);
        else
            startRow (*row);
        list.repaint();
    }

    void removeRow (int i)
    {
        if (auto* row = rowAt (i))
        {
            if (row->job != 0)
                panel.downloader.remove (row->job);
            rows.erase (rows.begin() + i);
            list.updateContent();
            list.repaint();
        }
    }

    void downloadAll()
    {
        for (auto& row : rows)
        {
            Downloader::Job job;
            const bool has = row.job != 0 && panel.downloader.getJob (row.job, job);
            if (! has || job.state == Downloader::State::failed || job.state == Downloader::State::cancelled)
                startRow (row);   // in list order: they download (and land on buttons) in that order
        }
        list.repaint();
    }

    void clearFinished()
    {
        for (int i = (int) rows.size(); --i >= 0;)
        {
            Downloader::Job job;
            if (rows[(size_t) i].job != 0 && panel.downloader.getJob (rows[(size_t) i].job, job) && job.state == Downloader::State::done)
            {
                panel.downloader.remove (rows[(size_t) i].job);
                rows.erase (rows.begin() + i);
            }
        }
        list.updateContent();
        list.repaint();
    }

    void updateTools()
    {
        update.setEnabled (false);
        update.setButtonText ("Updating...");
        juce::Component::SafePointer<LinksTab> safe (this);
        panel.downloader.updateYtDlp ([safe] (const juce::String& output)
        {
            if (safe == nullptr)
                return;
            safe->update.setEnabled (true);
            safe->update.setButtonText ("Update yt-dlp");
            auto lines = juce::StringArray::fromLines (output.trim());
            lines.removeEmptyStrings();
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Update yt-dlp",
                                                    lines.isEmpty() ? juce::String ("Done.") : lines[lines.size() - 1]);
        });
    }

    ImportPanel& panel;
    juce::TextEditor paste;
    juce::TextButton add { "Add" }, all { "Download all" }, clear { "Clear finished" }, update { "Update yt-dlp" };
    juce::ListBox list;
    juce::Label note;
    std::vector<Row> rows;
    int lastUid = 0;
};

// ---------------------------------------------------------------------------------------------

ImportPanel::ImportPanel (ShowController& c, Downloader& d, std::function<int()> bank)
    : controller (c), downloader (d), shownBank (std::move (bank))
{
    filesTab = std::make_unique<FilesTab> (*this);
    linksTab = std::make_unique<LinksTab> (*this);
    tabs.addTab ("Files", Look::panel, filesTab.get(), false);
    tabs.addTab ("YouTube / links", Look::panel, linksTab.get(), false);
    tabs.setTabBarDepth (32);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);
    downloader.addChangeListener (this);
    setSize (640, 480);
}

ImportPanel::~ImportPanel()
{
    downloader.removeChangeListener (this);
}

void ImportPanel::showLinksTab (bool links)
{
    tabs.setCurrentTabIndex (links ? 1 : 0);
}

void ImportPanel::resized()
{
    tabs.setBounds (getLocalBounds());
}

void ImportPanel::paint (juce::Graphics& g)
{
    g.fillAll (Look::panel);
}

void ImportPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (linksTab != nullptr)
        linksTab->repaint();
}

int ImportPanel::place (const juce::File& file, const juce::String& name)
{
    const int start = Show::cueId (shownBank ? shownBank() : 0, 0);
    int id = controller.nextEmptyCue (start);
    if (id < 0)
    {
        if (controller.assignFiles ({ file }, start) == 0)   // adds a bank if there's room for one
            return -1;
        id = controller.getShow().banks.empty() ? -1 : Show::cueId ((int) controller.getShow().banks.size() - 1, 0);
    }
    else
        controller.assignFile (id, file);
    if (id >= 0 && controller.getShow().isValid (id) && name.isNotEmpty())
    {
        controller.getShow().cue (id).name = name;
        controller.cueChanged (id);
    }
    return id;
}
