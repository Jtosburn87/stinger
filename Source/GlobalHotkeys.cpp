#include "GlobalHotkeys.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>

namespace
{
    constexpr UINT applyMessage = WM_APP + 1;

    /** JUCE key -> Windows virtual key + modifiers; false if it has no virtual key. */
    bool toVirtualKey (const juce::KeyPress& key, UINT& vk, UINT& mods)
    {
        const int code = key.getKeyCode();
        mods = MOD_NOREPEAT;
        const auto m = key.getModifiers();
        if (m.isCtrlDown())  mods |= MOD_CONTROL;
        if (m.isShiftDown()) mods |= MOD_SHIFT;
        if (m.isAltDown())   mods |= MOD_ALT;

        if ((code & 0x10000) != 0)                          // JUCE marks extended keys with this bit on Windows
            vk = (UINT) (code & 0xffff);                    // F-keys, arrows, numpad, Home/End...
        else if (code >= 'a' && code <= 'z')
            vk = (UINT) (code - 'a' + 'A');
        else if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')
                 || code == VK_SPACE || code == VK_RETURN || code == VK_ESCAPE || code == VK_TAB || code == VK_BACK)
            vk = (UINT) code;
        else
        {
            const SHORT scan = VkKeyScanW ((WCHAR) code);    // punctuation: depends on the keyboard layout
            if (scan == -1)
                return false;
            vk = (UINT) (scan & 0xff);
        }
        return vk != 0;
    }
}

GlobalHotkeys::GlobalHotkeys() : juce::Thread ("Global hotkeys")
{
    startThread();
    for (int i = 0; i < 100 && threadId.load() == 0; ++i)
        juce::Thread::sleep (5);
}

GlobalHotkeys::~GlobalHotkeys()
{
    alive->store (false);
    if (const auto id = threadId.load())
        PostThreadMessageW ((DWORD) id, WM_QUIT, 0, 0);
    stopThread (2000);
}

juce::Array<juce::KeyPress> GlobalHotkeys::setKeys (const juce::Array<juce::KeyPress>& keys)
{
    {
        const juce::ScopedLock sl (lock);
        if (keys == wanted)
            return refused;
        wanted = keys;
    }
    applied.reset();
    if (const auto id = threadId.load())
        if (PostThreadMessageW ((DWORD) id, applyMessage, 0, 0))
            applied.wait (1000);
    const juce::ScopedLock sl (lock);
    return refused;
}

void GlobalHotkeys::run()
{
    MSG msg;
    PeekMessageW (&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);   // makes this thread's message queue
    threadId.store (GetCurrentThreadId());

    while (GetMessageW (&msg, nullptr, 0, 0) > 0)
    {
        if (msg.message == applyMessage)
        {
            const juce::ScopedLock sl (lock);
            for (int i = 0; i < registered.size(); ++i)
                UnregisterHotKey (nullptr, i + 1);
            registered.clear();
            refused.clear();
            for (auto& key : wanted)
            {
                UINT vk = 0, mods = 0;
                if (toVirtualKey (key, vk, mods) && RegisterHotKey (nullptr, registered.size() + 1, mods, vk))
                    registered.add (key);
                else
                {
                    registered.add (juce::KeyPress());   // keeps ids in step with the list
                    refused.add (key);
                }
            }
            applied.signal();
        }
        else if (msg.message == WM_HOTKEY)
        {
            juce::KeyPress key;
            {
                const juce::ScopedLock sl (lock);
                const int index = (int) msg.wParam - 1;
                if (index >= 0 && index < registered.size())
                    key = registered[index];
            }
            if (key.isValid())
                juce::MessageManager::callAsync ([this, key, alive = alive]
                {
                    if (alive->load() && onKey)
                        onKey (key);
                });
        }
    }

    const juce::ScopedLock sl (lock);
    for (int i = 0; i < registered.size(); ++i)
        UnregisterHotKey (nullptr, i + 1);
    registered.clear();
}

#elif JUCE_MAC
 #include <Carbon/Carbon.h>

// Mac: Carbon RegisterEventHotKey (needs no Accessibility permission). Its events arrive on the main thread,
// so the thread isn't used here.
namespace
{
    /** JUCE key -> Mac virtual key code + Carbon modifiers; false if it has none. Positions are ANSI (US) keys. */
    bool toMacKey (const juce::KeyPress& key, UInt32& vk, UInt32& mods)
    {
        mods = 0;
        const auto m = key.getModifiers();
        if (m.isCommandDown()) mods |= cmdKey;
        if (m.isCtrlDown())    mods |= controlKey;
        if (m.isShiftDown())   mods |= shiftKey;
        if (m.isAltDown())     mods |= optionKey;

        static const std::pair<int, int> table[] = {
            { 'a', kVK_ANSI_A }, { 'b', kVK_ANSI_B }, { 'c', kVK_ANSI_C }, { 'd', kVK_ANSI_D }, { 'e', kVK_ANSI_E },
            { 'f', kVK_ANSI_F }, { 'g', kVK_ANSI_G }, { 'h', kVK_ANSI_H }, { 'i', kVK_ANSI_I }, { 'j', kVK_ANSI_J },
            { 'k', kVK_ANSI_K }, { 'l', kVK_ANSI_L }, { 'm', kVK_ANSI_M }, { 'n', kVK_ANSI_N }, { 'o', kVK_ANSI_O },
            { 'p', kVK_ANSI_P }, { 'q', kVK_ANSI_Q }, { 'r', kVK_ANSI_R }, { 's', kVK_ANSI_S }, { 't', kVK_ANSI_T },
            { 'u', kVK_ANSI_U }, { 'v', kVK_ANSI_V }, { 'w', kVK_ANSI_W }, { 'x', kVK_ANSI_X }, { 'y', kVK_ANSI_Y },
            { 'z', kVK_ANSI_Z },
            { '0', kVK_ANSI_0 }, { '1', kVK_ANSI_1 }, { '2', kVK_ANSI_2 }, { '3', kVK_ANSI_3 }, { '4', kVK_ANSI_4 },
            { '5', kVK_ANSI_5 }, { '6', kVK_ANSI_6 }, { '7', kVK_ANSI_7 }, { '8', kVK_ANSI_8 }, { '9', kVK_ANSI_9 },
            { '-', kVK_ANSI_Minus }, { '=', kVK_ANSI_Equal }, { '[', kVK_ANSI_LeftBracket }, { ']', kVK_ANSI_RightBracket },
            { ';', kVK_ANSI_Semicolon }, { '\'', kVK_ANSI_Quote }, { ',', kVK_ANSI_Comma }, { '.', kVK_ANSI_Period },
            { '/', kVK_ANSI_Slash }, { '\\', kVK_ANSI_Backslash }, { '`', kVK_ANSI_Grave },
            { juce::KeyPress::spaceKey, kVK_Space }, { juce::KeyPress::returnKey, kVK_Return },
            { juce::KeyPress::escapeKey, kVK_Escape }, { juce::KeyPress::tabKey, kVK_Tab },
            { juce::KeyPress::backspaceKey, kVK_Delete }, { juce::KeyPress::deleteKey, kVK_ForwardDelete },
            { juce::KeyPress::upKey, kVK_UpArrow }, { juce::KeyPress::downKey, kVK_DownArrow },
            { juce::KeyPress::leftKey, kVK_LeftArrow }, { juce::KeyPress::rightKey, kVK_RightArrow },
            { juce::KeyPress::homeKey, kVK_Home }, { juce::KeyPress::endKey, kVK_End },
            { juce::KeyPress::pageUpKey, kVK_PageUp }, { juce::KeyPress::pageDownKey, kVK_PageDown },
            { juce::KeyPress::F1Key, kVK_F1 }, { juce::KeyPress::F2Key, kVK_F2 }, { juce::KeyPress::F3Key, kVK_F3 },
            { juce::KeyPress::F4Key, kVK_F4 }, { juce::KeyPress::F5Key, kVK_F5 }, { juce::KeyPress::F6Key, kVK_F6 },
            { juce::KeyPress::F7Key, kVK_F7 }, { juce::KeyPress::F8Key, kVK_F8 }, { juce::KeyPress::F9Key, kVK_F9 },
            { juce::KeyPress::F10Key, kVK_F10 }, { juce::KeyPress::F11Key, kVK_F11 }, { juce::KeyPress::F12Key, kVK_F12 },
            { juce::KeyPress::F13Key, kVK_F13 }, { juce::KeyPress::F14Key, kVK_F14 }, { juce::KeyPress::F15Key, kVK_F15 },
            { juce::KeyPress::F16Key, kVK_F16 }, { juce::KeyPress::F17Key, kVK_F17 }, { juce::KeyPress::F18Key, kVK_F18 },
            { juce::KeyPress::F19Key, kVK_F19 }, { juce::KeyPress::F20Key, kVK_F20 },
        };
        int code = key.getKeyCode();
        if (code >= 'A' && code <= 'Z')
            code += 'a' - 'A';
        for (auto& [k, v] : table)
            if (k == code)
            {
                vk = (UInt32) v;
                return true;
            }
        return false;
    }

    OSStatus hotKeyPressed (EventHandlerCallRef, EventRef event, void* user)
    {
        EventHotKeyID id {};
        if (GetEventParameter (event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof (id), nullptr, &id) == noErr)
            if (auto* f = static_cast<std::function<void (int)>*> (user))
                (*f) ((int) id.id - 1);
        return noErr;
    }
}

GlobalHotkeys::GlobalHotkeys() : juce::Thread ("Global hotkeys")
{
    pressed = std::make_unique<std::function<void (int)>> ([this, alive = alive] (int index)
    {
        if (! alive->load() || index < 0 || index >= registered.size() || ! registered[index].isValid())
            return;
        const auto key = registered[index];
        juce::MessageManager::callAsync ([this, key, alive]
        {
            if (alive->load() && onKey)
                onKey (key);
        });
    });
    EventTypeSpec spec { kEventClassKeyboard, kEventHotKeyPressed };
    EventHandlerRef handler = nullptr;
    if (InstallApplicationEventHandler (&hotKeyPressed, 1, &spec, pressed.get(), &handler) == noErr)
        eventHandler = handler;
}

GlobalHotkeys::~GlobalHotkeys()
{
    alive->store (false);
    setKeys ({});
    if (eventHandler != nullptr)
        RemoveEventHandler ((EventHandlerRef) eventHandler);
}

juce::Array<juce::KeyPress> GlobalHotkeys::setKeys (const juce::Array<juce::KeyPress>& keys)
{
    if (keys == wanted)
        return refused;
    wanted = keys;
    for (auto* ref : hotKeyRefs)
        if (ref != nullptr)
            UnregisterEventHotKey ((EventHotKeyRef) ref);
    hotKeyRefs.clear();
    registered.clear();
    refused.clear();
    for (auto& key : keys)
    {
        UInt32 vk = 0, mods = 0;
        EventHotKeyRef ref = nullptr;
        const EventHotKeyID id { 0x53746e67 /* 'Stng' */, (UInt32) registered.size() + 1 };
        if (eventHandler != nullptr && toMacKey (key, vk, mods)
            && RegisterEventHotKey (vk, mods, id, GetApplicationEventTarget(), 0, &ref) == noErr)
            registered.add (key);
        else
        {
            registered.add (juce::KeyPress());   // keeps ids in step with the list
            refused.add (key);
        }
        hotKeyRefs.push_back (ref);
    }
    return refused;
}

void GlobalHotkeys::run() {}

#else

GlobalHotkeys::GlobalHotkeys() : juce::Thread ("Global hotkeys") {}
GlobalHotkeys::~GlobalHotkeys() {}
juce::Array<juce::KeyPress> GlobalHotkeys::setKeys (const juce::Array<juce::KeyPress>& keys) { return keys; }   // not on this OS yet
void GlobalHotkeys::run() {}

#endif
