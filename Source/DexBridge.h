#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** Samsung DeX on Android (android/.../MouseBridge.java): whether Stinger's window is on the DeX monitor, and a real
    mouse for JUCE's windows (right-click, hover, wheel), which JUCE on Android doesn't have. (Pointer shapes: android/juce-patches.)
    Android only; the computer apps never build it. */
namespace DexBridge
{
    /** Starts watching for new JUCE windows (to give them the mouse). Message thread. */
    void start();

    bool isDesktop();
    /** Called (on the message thread, after the event) when Stinger's window moves onto or off the DeX monitor. */
    void setOnChange (std::function<void (bool desktop)> callback);

    /** The deck page for the phone's screen (DexDeck), empty while there's none. */
    void setDeckUrl (const juce::String& url);
}
