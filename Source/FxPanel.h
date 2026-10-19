#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Show.h"

/** The selected button's 3 plugin slots: pick a VST3 effect (menu by maker), Edit opens its window,
    Bypass, and Plugins... scans for installed plugins. */
class FxPanel : public juce::Component,
                private juce::ChangeListener
{
public:
    explicit FxPanel (ShowController& controller);
    ~FxPanel() override;

    void setCue (int id);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Slot
    {
        juce::Label number;
        juce::TextButton choose, edit { "Edit" }, bypass { "Bypass" };
    };

    void refresh();
    void showPluginMenu (int slot);
    void openEditor (int slot);
    void closeEditorFor (juce::AudioProcessor* p);
    void showPluginList();
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    class EditorWindow;

    ShowController& controller;
    int cueId = -1;
    std::array<Slot, CueFx::numSlots> slots;
    juce::TextButton pluginsButton { "Plugins..." };
    std::vector<std::unique_ptr<EditorWindow>> editors;
};
