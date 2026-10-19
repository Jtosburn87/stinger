#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** System-wide hotkeys (Windows RegisterHotKey): the keys reach Stinger even when another app is in
    front, and that app doesn't get them while they're registered. Runs its own thread with a message loop. */
class GlobalHotkeys : private juce::Thread
{
public:
    GlobalHotkeys();
    ~GlobalHotkeys() override;

    /** Called on the message thread when a registered key is pressed. */
    std::function<void (const juce::KeyPress&)> onKey;

    /** Registers exactly these keys (an empty list unregisters everything).
        Returns the keys Windows refused (taken by another app, or not a key it can register). */
    juce::Array<juce::KeyPress> setKeys (const juce::Array<juce::KeyPress>& keys);

private:
    void run() override;

    juce::CriticalSection lock;
    juce::Array<juce::KeyPress> wanted, registered, refused;
    juce::WaitableEvent applied;
    std::atomic<unsigned long> threadId { 0 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    // Mac (Carbon hot keys): one EventHotKeyRef per wanted key (nullptr if refused), the event handler
    std::vector<void*> hotKeyRefs;
    void* eventHandler = nullptr;
    std::unique_ptr<std::function<void (int)>> pressed;
};
