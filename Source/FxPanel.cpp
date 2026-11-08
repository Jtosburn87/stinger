#include "FxPanel.h"
#include "PluginHost.h"
#include "Look.h"
#if JUCE_ANDROID
 #include "StockFxEditor.h"
#endif

/** A plugin's own window (or JUCE's generic sliders if it has none). Stays in front of Stinger. */
class FxPanel::EditorWindow : public juce::DocumentWindow
{
public:
    EditorWindow (const juce::String& title, juce::AudioProcessor& p, std::function<void()> onClose)
        : DocumentWindow (title, Look::panel, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          processor (&p), closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        juce::AudioProcessorEditor* editor = p.hasEditor() ? p.createEditorIfNeeded() : nullptr;
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (p);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setAlwaysOnTop (true);
        setVisible (true);
    }

    ~EditorWindow() override
    {
        clearContentComponent();   // the editor goes before its plugin can
    }

    void closeButtonPressed() override
    {
        if (closed)
            juce::MessageManager::callAsync (closed);
    }

    juce::AudioProcessor* processor;

private:
    std::function<void()> closed;
};

// ---------------------------------------------------------------------------------------------

FxPanel::FxPanel (ShowController& c) : controller (c)
{
    for (int i = 0; i < CueFx::numSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        s.number.setText (juce::String (i + 1), juce::dontSendNotification);
        s.number.setFont (Look::mono (12.0f));
        s.number.setColour (juce::Label::textColourId, juce::Colour (0xffa99fd0));
        s.choose.setTooltip ("Choose the plugin for this slot");
        s.choose.onClick = [this, i] { showPluginMenu (i); };
        s.edit.setTooltip ("Open the plugin's window");
        s.edit.onClick = [this, i] { openEditor (i); };
        s.bypass.setClickingTogglesState (true);
        s.bypass.setTooltip ("Skip this plugin (keeps its settings)");
        s.bypass.onClick = [this, i] { controller.setPluginBypassed (cueId, i, slots[(size_t) i].bypass.getToggleState()); };
        for (auto* comp : std::initializer_list<juce::Component*> { &s.number, &s.choose, &s.edit, &s.bypass })
        {
            comp->setWantsKeyboardFocus (false);
            addAndMakeVisible (comp);
        }
    }
    pluginsButton.setWantsKeyboardFocus (false);
    pluginsButton.setTooltip ("Scan for VST3 plugins, see which ones failed");
    pluginsButton.onClick = [this] { showPluginList(); };
   #if ! JUCE_ANDROID   // Samsung DeX: no VST3 on Android, nothing to scan for
    addAndMakeVisible (pluginsButton);
   #endif

    controller.onPluginGoing = [this] (juce::AudioProcessor* p) { closeEditorFor (p); };
    controller.addChangeListener (this);
}

FxPanel::~FxPanel()
{
    controller.removeChangeListener (this);
    controller.onPluginGoing = nullptr;
    editors.clear();
}

void FxPanel::setCue (int id)
{
    cueId = id;
    refresh();
}

void FxPanel::refresh()
{
    auto& show = controller.getShow();
    const bool has = show.isValid (cueId) && ! show.cue (cueId).isEmpty();
    auto* fx = controller.getFx (cueId);
    for (int i = 0; i < CueFx::numSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        auto* plugin = fx != nullptr ? fx->getPlugin (i) : nullptr;
        juce::String text = "+ Add plugin";
        if (plugin != nullptr)
            text = fx->getDescription (i).name;
        else if (has && ! show.cue (cueId).plugins[(size_t) i].isEmpty())
        {
            juce::PluginDescription d;
            if (auto xml = juce::parseXML (show.cue (cueId).plugins[(size_t) i].description); xml != nullptr && d.loadFromXml (*xml))
                text = d.name + " (not found)";
        }
        s.choose.setButtonText (text);
        s.choose.setEnabled (has);
        s.choose.setColour (juce::TextButton::buttonColourId, plugin != nullptr ? juce::Colour (0xff221e30) : Look::raised);
        s.choose.setColour (juce::TextButton::textColourOffId, plugin != nullptr ? juce::Colour (0xffe2dafb) : Look::textDim);
        s.edit.setEnabled (plugin != nullptr);
        s.bypass.setEnabled (plugin != nullptr);
        s.bypass.setToggleState (plugin != nullptr && fx->isBypassed (i), juce::dontSendNotification);
    }
    repaint();
}

void FxPanel::showPluginMenu (int slot)
{
    auto* host = controller.getPluginHost();
    if (host == nullptr)
        return;
   #if JUCE_ANDROID
    // Samsung DeX: Stinger's own effects are the ones there are
    const juce::Array<juce::PluginDescription> effects { StockFx::describe (StockFx::Kind::eq),
                                                         StockFx::describe (StockFx::Kind::compressor),
                                                         StockFx::describe (StockFx::Kind::limiter) };
    juce::PopupMenu m;
   #else
    const auto effects = host->effects();
    juce::PopupMenu m;
    if (effects.isEmpty())
        m.addItem (-1, "No plugins yet: Plugins... scans for them", false);
   #endif
    juce::String maker;
    juce::PopupMenu sub;
    auto flush = [&]
    {
        if (sub.getNumItems() > 0)
            m.addSubMenu (maker.isEmpty() ? juce::String ("(no maker)") : maker, sub);
        sub = {};
    };
    for (int i = 0; i < effects.size(); ++i)
    {
        if (effects[i].manufacturerName != maker)
        {
            flush();
            maker = effects[i].manufacturerName;
        }
        sub.addItem (1000 + i, effects[i].name);
    }
    flush();
    m.addSeparator();
    auto* fx = controller.getFx (cueId);
    m.addItem (1, "Remove", fx != nullptr && fx->getPlugin (slot) != nullptr
                            || ! controller.getShow().cue (cueId).plugins[(size_t) slot].isEmpty());
   #if ! JUCE_ANDROID
    m.addItem (2, "Plugins (scan)...");
   #endif

    juce::Component::SafePointer<FxPanel> safe (this);
    const int id = cueId;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slots[(size_t) slot].choose), [safe, id, slot, effects] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& c = safe->controller;
        if (result == 1)
            c.removePlugin (id, slot);
        else if (result == 2)
            safe->showPluginList();
        else if (result >= 1000 && result - 1000 < effects.size())
        {
            juce::String error;
            if (! c.setPlugin (id, slot, effects[result - 1000], error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Plugin",
                                                        "Couldn't load " + effects[result - 1000].name + (error.isNotEmpty() ? ": " + error : juce::String()));
            else
                safe->openEditor (slot);
        }
    });
}

void FxPanel::openEditor (int slot)
{
    auto* fx = controller.getFx (cueId);
    auto* plugin = fx != nullptr ? fx->getPlugin (slot) : nullptr;
    if (plugin == nullptr)
        return;
    for (auto& w : editors)
        if (w->processor == plugin)
        {
            w->toFront (true);
            return;
        }
    const auto title = fx->getDescription (slot).name + juce::String::fromUTF8 (" \xe2\x80\x94 ") + controller.getShow().cue (cueId).name;
    juce::Component::SafePointer<FxPanel> safe (this);
    editors.push_back (std::make_unique<EditorWindow> (title, *plugin, [safe, plugin] { if (safe != nullptr) safe->closeEditorFor (plugin); }));
   #if JUCE_ANDROID
    // Samsung DeX: Stinger's own effects have no window of their own; JUCE's generic one would be empty
    if (auto* stock = dynamic_cast<StockFx*> (plugin))
    {
        auto& window = *editors.back();
        const int id = cueId;
        window.setContentOwned (new StockFxEditor (*stock, [safe, id] { if (safe != nullptr) safe->controller.cueChanged (id); }), true);
        window.centreWithSize (window.getWidth(), window.getHeight());
    }
   #endif
}

void FxPanel::closeEditorFor (juce::AudioProcessor* p)
{
    editors.erase (std::remove_if (editors.begin(), editors.end(), [p] (auto& w) { return w->processor == p; }), editors.end());
}

void FxPanel::showPluginList()
{
    auto* host = controller.getPluginHost();
    if (host == nullptr)
        return;
    auto* list = new juce::PluginListComponent (host->formats, host->known, host->deadMansPedalFile(), &host->getSettings(), true);
    list->setSize (700, 520);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (list);
    o.dialogTitle = "Plugins (Options > Scan for new or updated VST3 plugins)";
    o.dialogBackgroundColour = Look::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void FxPanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xffc9d0d8));
    g.setFont (Look::condensed (15.0f));
   #if JUCE_ANDROID
    g.drawText ("EFFECTS", getLocalBounds().removeFromTop (24), juce::Justification::centredLeft);
   #else
    g.drawText ("EFFECTS (VST3)", getLocalBounds().removeFromTop (24), juce::Justification::centredLeft);
   #endif
}

void FxPanel::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (24);
    pluginsButton.setBounds (head.removeFromRight (84).reduced (0, 1));
    r.removeFromTop (6);
    for (auto& s : slots)
    {
        auto top = r.removeFromTop (28);
        s.number.setBounds (top.removeFromLeft (16));
        s.choose.setBounds (top);
        r.removeFromTop (4);
        auto buttons = r.removeFromTop (24).withTrimmedLeft (16);
        s.edit.setBounds (buttons.removeFromLeft ((buttons.getWidth() - 6) / 2));
        buttons.removeFromLeft (6);
        s.bypass.setBounds (buttons);
        r.removeFromTop (12);
    }
}
