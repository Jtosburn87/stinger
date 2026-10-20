#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#include "ControlsWindow.h"
#include "Look.h"
#include "PluginHost.h"
#include "ImportPanel.h"

class StingerApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override
    {
        return getCommandLineParameters().contains ("--snapshot") || getCommandLineParameters().contains ("--scan-plugin");
    }

    void initialise (const juce::String& commandLine) override
    {
        if (getCommandLineParameterArray().contains ("--scan-plugin"))   // plugin scanner child process (see PluginHost)
        {
            setApplicationReturnValue (PluginHost::runScanner (getCommandLineParameterArray()));
            quit();
            return;
        }

        juce::PropertiesFile::Options options;
        options.applicationName = "Stinger";
        options.folderName = "Stinger";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);
        auto& settings = *properties.getUserSettings();

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        engine = std::make_unique<PlaybackEngine>();
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        if (const int i = args.indexOf ("--snapshot"); i >= 0 && i + 2 < args.size())
        {
            writeSnapshot (juce::File (args[i + 1].unquoted()), juce::File (args[i + 2].unquoted()));
            return;
        }
        auto saved = settings.getXmlValue ("audio");
        engine->initialise (saved.get());
        controller = std::make_unique<ShowController> (*engine);
        pluginHost = std::make_unique<PluginHost> (settings);
        controller->setPluginHost (pluginHost.get());

        auto* content = new MainComponent (*controller, settings);
        window = std::make_unique<MainWindow> (content->windowTitle(), content);
        content->onTitleChanged = [this, content] { if (window != nullptr) window->setName (content->windowTitle()); };

        // a show given on the command line (double-clicked .stinger file), else the last one
        juce::File show;
        for (auto& arg : juce::StringArray::fromTokens (commandLine, true))
            if (auto f = juce::File::getCurrentWorkingDirectory().getChildFile (arg.unquoted()); f.hasFileExtension ("stinger"))
                show = f;
        if (! show.existsAsFile())
            show = juce::File (settings.getValue ("lastShow"));
        if (show.existsAsFile())
            content->openShow (show);

        showDownloadWarningOnce (settings);
    }

    void shutdown() override
    {
        saveAudioState();
        if (engine != nullptr)
            engine->shutdownAudio();   // no more audio callbacks before plugins and the show go
        snapshotTimer = nullptr;
        snapshotContent = nullptr;
        window = nullptr;
        controller = nullptr;
        pluginHost = nullptr;
        engine = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    /** A .stinger file opened while Stinger runs (on a Mac, also the one double-clicked to start it). */
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        auto* content = window != nullptr ? dynamic_cast<MainComponent*> (window->getContentComponent()) : nullptr;
        if (content == nullptr)
            return;
        for (auto& arg : juce::StringArray::fromTokens (commandLine, true))
            if (auto f = juce::File::getCurrentWorkingDirectory().getChildFile (arg.unquoted()); f.hasFileExtension ("stinger") && f.existsAsFile())
            {
                juce::Component::SafePointer<MainComponent> safe (content);
                content->askToSave ([safe, f] (bool go) { if (go && safe != nullptr) safe->openShow (f); });
                return;
            }
    }

    void systemRequestedQuit() override
    {
        auto* content = window != nullptr ? dynamic_cast<MainComponent*> (window->getContentComponent()) : nullptr;
        if (content == nullptr)
        {
            quit();
            return;
        }
        content->askToSave ([] (bool go) { if (go) juce::JUCEApplication::quit(); });
    }

private:
    /** First start only: a reminder about the download feature (Pull WAV / Import links). */
    void showDownloadWarningOnce (juce::PropertiesFile& settings)
    {
        if (settings.getBoolValue ("downloadWarningShown"))
            return;
        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::WarningIcon)
                           .withTitle ("Stinger")
                           .withMessage ("The download feature is only for content you have the right to use!")
                           .withButton ("OK")
                           .withAssociatedComponent (window.get());
        juce::AlertWindow::showAsync (options, [&settings] (int)
        {
            settings.setValue ("downloadWarningShown", true);
            settings.saveIfNeeded();
        });
    }

    /** Developer option: Stinger.exe --snapshot show.stinger out.png draws the main window with that show
        into an image and quits, without opening an audio device or showing a window. */
    void writeSnapshot (const juce::File& showFile, const juce::File& image)
    {
        engine->prepareForTest (48000.0, 8);
        controller = std::make_unique<ShowController> (*engine);
        snapshotContent = std::make_unique<MainComponent> (*controller, snapshotSettings());
        snapshotContent->openShow (showFile);
        snapshotTimer = std::make_unique<SnapshotTimer> (*this, image);
    }

    juce::PropertiesFile& snapshotSettings()
    {
        static juce::PropertiesFile scratch (juce::File::createTempFile (".settings"), {});
        return scratch;
    }

    struct SnapshotTimer : juce::Timer
    {
        SnapshotTimer (StingerApplication& a, juce::File f) : app (a), file (std::move (f)) { startTimer (100); }
        void timerCallback() override
        {
            if (app.controller->getLoader().pending() > 0)
                return;
            stopTimer();
            juce::Image image;
            if (file.getFileName().containsIgnoreCase ("controls"))   // out file named *controls*: the Controls window
            {
                ControlsPanel panel (app.snapshotContent->getSurface(), &app.snapshotContent->getStreamDeck());
                image = panel.createComponentSnapshot (panel.getLocalBounds());
            }
            else if (file.getFileName().containsIgnoreCase ("import"))   // *import*: the Import window (*links*: its Links tab)
            {
                Downloader downloader;
                ImportPanel panel (*app.controller, downloader, [] { return 0; });
                panel.showLinksTab (file.getFileName().containsIgnoreCase ("links"));
                image = panel.createComponentSnapshot (panel.getLocalBounds());
            }
            else
                image = app.snapshotContent->createComponentSnapshot (app.snapshotContent->getLocalBounds());
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (image, out);
            juce::JUCEApplication::quit();
        }
        StingerApplication& app;
        juce::File file;
    };

    void saveAudioState()
    {
        if (snapshotContent != nullptr)
            return;
        if (engine != nullptr)
            if (auto xml = engine->createStateXml())
                properties.getUserSettings()->setValue ("audio", xml.get());
        properties.saveIfNeeded();
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, juce::Component* content)
            : DocumentWindow (name, Look::ground, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content, true);
            setResizable (true, false);
            setResizeLimits (1280, 800, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            content->grabKeyboardFocus();
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    Look::LookAndFeel lookAndFeel;
    juce::ApplicationProperties properties;
    std::unique_ptr<PlaybackEngine> engine;
    std::unique_ptr<ShowController> controller;
    std::unique_ptr<PluginHost> pluginHost;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<MainComponent> snapshotContent;
    std::unique_ptr<SnapshotTimer> snapshotTimer;
};

START_JUCE_APPLICATION (StingerApplication)
