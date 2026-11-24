#include <juce_gui_extra/juce_gui_extra.h>
#include "MobilePages.h"
#include "MainComponent.h"
#include "DexBridge.h"
#include "DuoBridge.h"
#include "DexDeck.h"
#include "DexStreamDeck.h"
#include "Look.h"
#include "PluginHost.h"
#include "Autosave.h"

/** Stinger on Android: the same engine and show files as the desktop app, with pages made for touch. */
class StingerMobileApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Stinger";
        options.folderName = "Stinger";
        options.filenameSuffix = ".settings";
        properties.setStorageParameters (options);
        auto& settings = *properties.getUserSettings();

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        engine = std::make_unique<PlaybackEngine>();
        auto saved = settings.getXmlValue ("audio");
        engine->initialise (saved.get());
        controller = std::make_unique<ShowController> (*engine);
        // Android keeps an app's files in its own folder: shows live there rather than in Documents
        controller->setHomeFolderForTest (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Stinger"));
        pluginHost = std::make_unique<PluginHost> (settings);
        controller->setPluginHost (pluginHost.get());

        const auto recoveryFolder = settings.getFile().getSiblingFile ("Recovery");
        const auto found = Autosave::find (recoveryFolder);   // read before autosaving starts
        autosave = std::make_unique<Autosave> (recoveryFolder);
        controller->setAutosave (autosave.get());

        juce::String error;
        if (found.has_value())
            controller->openRecovery (found->file, error);   // Android stops apps without asking: pick up where it was
        else if (const juce::File show (settings.getValue ("lastShow")); show.existsAsFile())
            controller->openShow (show, error);

        // one MIDI / key listener for the phone pages and DeX alike: each MIDI device is opened once
        surface = std::make_unique<ControlSurface> (*controller, settings);
        root = std::make_unique<MobileRoot> (*controller, settings, *surface);
        window = std::make_unique<Window> (root.get(), surface.get());

        // the keys (the DeX deck page, a Stream Deck) work from the start: on the phone pages until DeX takes over
        deck = std::make_unique<DexDeck> (*controller);
        DexBridge::setDeckUrl (deck->url());
        streamDeck = std::make_unique<DexStreamDeck> (*deck);
        pointKeysAt (false);

        // Samsung DeX: the computer app's screen on the monitor, the touch pages on the phone
        DexBridge::setOnChange ([this] (bool on) { showDesktop (on); });
        DexBridge::start();
        if (DexBridge::isDesktop())
            showDesktop (true);

        // Surface Duo spanned over both screens: menus and editor on one, buttons on the other
        DuoBridge::setOnChange ([this] (DuoBridge::Hinge hinge)
        {
            if (root != nullptr)
                root->setHinge (root->getLocalArea (nullptr, hinge.bounds), hinge.across);
        });
    }

    /** The deck page's and the Stream Deck's keys act on the screen in charge. */
    void pointKeysAt (bool onDesktop)
    {
        if (onDesktop && desktop != nullptr)
        {
            auto& server = desktop->getStreamDeck();
            if (deck != nullptr)
                deck->setTarget (server.perform, server.shownBank);
            if (surface != nullptr)
            {
                surface->perform = desktopPerform;
                surface->shownBank = desktopBank;
            }
            return;
        }
        auto& page = root->getMainPage();
        juce::Component::SafePointer<MobileMainPage> safe (&page);
        auto perform = [safe] (const juce::String& action) { if (safe != nullptr) safe->perform (action); };
        auto bank = [safe] { return safe != nullptr ? safe->getBank() : 0; };
        if (deck != nullptr)
            deck->setTarget (perform, bank);
        if (surface != nullptr)
        {
            surface->perform = perform;
            surface->shownBank = bank;
        }
    }

    /** Swaps the window's content. The window is made again so it takes the new screen's size and density. */
    void showDesktop (bool on)
    {
        if (root == nullptr)
            return;
        if (on && desktop == nullptr)
        {
            desktop = std::make_unique<MainComponent> (*controller, *properties.getUserSettings(), surface.get());
            desktopPerform = surface->perform;   // the desktop pointed the shared listener at itself
            desktopBank = surface->shownBank;
            deck->setLayouts (&desktop->getRemote());
        }
        pointKeysAt (on);
        const_cast<juce::Displays&> (juce::Desktop::getInstance().getDisplays()).refresh();
        window = nullptr;
        window = std::make_unique<Window> (on ? static_cast<juce::Component*> (desktop.get()) : root.get(), on ? nullptr : surface.get());
        if (on)
        {
            juce::Component::SafePointer<juce::Component> safe (desktop.get());
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->isShowing()) safe->grabKeyboardFocus(); });
        }
    }

    void shutdown() override
    {
        saveAudioState();
        if (engine != nullptr)
            engine->shutdownAudio();
        window = nullptr;
        DexBridge::setDeckUrl ({});
        streamDeck = nullptr;
        deck = nullptr;   // before the screens its keys act on
        if (surface != nullptr)
        {
            surface->perform = nullptr;
            surface->shownBank = nullptr;
        }
        desktopPerform = nullptr;
        desktopBank = nullptr;
        desktop = nullptr;
        root = nullptr;
        surface = nullptr;   // after the pages that show it
        if (controller != nullptr)
            controller->setAutosave (nullptr);
        autosave = nullptr;
        controller = nullptr;
        pluginHost = nullptr;
        engine = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }
    void suspended() override           { saveAudioState(); }
    void anotherInstanceStarted (const juce::String&) override {}

    bool backButtonPressed() override
    {
        if (DexBridge::isDesktop())
            return true;   // a desktop has no Back: never let Android close Stinger for it
        return root != nullptr && root->goBack();
    }

private:
    void saveAudioState()
    {
        if (engine != nullptr)
            if (auto xml = engine->createStateXml())
                properties.getUserSettings()->setValue ("audio", xml.get());
        properties.saveIfNeeded();
    }

    class Window : public juce::DocumentWindow
    {
    public:
        /** keys: the phone pages' MIDI / key listener (keys nothing on the page used); DeX's screen handles its own. */
        Window (juce::Component* content, ControlSurface* keys)
            : DocumentWindow ("Stinger", Look::ground, 0), keySurface (keys)
        {
            setUsingNativeTitleBar (true);
            setTitleBarHeight (0);   // the app's own window: Android (or DeX) frames it, JUCE draws no title bar
            setContentNonOwned (content, false);
            setFullScreen (true);
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

        /** Samsung DeX: with nothing focused a key stops here, at the window. The page under it decides (Space for GO...). */
        bool keyPressed (const juce::KeyPress& key) override
        {
            auto* content = getContentComponent();
            if (content != nullptr && content->keyPressed (key))
                return true;
            return keySurface != nullptr && keySurface->handleKey (key);
        }

    private:
        ControlSurface* keySurface;
    };

    Look::LookAndFeel lookAndFeel;
    juce::ApplicationProperties properties;
    std::unique_ptr<PlaybackEngine> engine;
    std::unique_ptr<Autosave> autosave;
    std::unique_ptr<ShowController> controller;
    std::unique_ptr<PluginHost> pluginHost;
    std::unique_ptr<ControlSurface> surface;   // MIDI and keys: phone pages or DeX, whichever is showing
    std::function<void (const juce::String&)> desktopPerform;
    std::function<int()> desktopBank;
    std::unique_ptr<MobileRoot> root;
    std::unique_ptr<MainComponent> desktop;   // made the first time DeX shows it, then kept
    std::unique_ptr<DexDeck> deck;            // the key server: the DeX deck page and a Stream Deck, from the start
    std::unique_ptr<DexStreamDeck> streamDeck;   // a real Stream Deck plugged into the phone, phone pages or DeX
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (StingerMobileApplication)
