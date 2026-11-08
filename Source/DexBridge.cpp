#include "DexBridge.h"
#include <jni.h>
#include <set>

namespace juce { extern JNIEnv* getEnv() noexcept; }   // JUCE's own (juce_JNIHelpers_android.h isn't in its public headers)

namespace
{
    jclass bridgeClass = nullptr;
    bool desktop = false;
    juce::String deckUrl;
    std::function<void (bool)> onChange;
    std::set<std::pair<juce::ComponentPeer*, void*>> attached;   // peer + its view: a new peer can reuse an old address

    juce::ComponentPeer* validPeer (jlong p)
    {
        auto* peer = reinterpret_cast<juce::ComponentPeer*> (p);
        return juce::ComponentPeer::isValidPeer (peer) ? peer : nullptr;
    }

    /** Android hands over screen pixels; JUCE's windows work in points (pixels / the screen's density). */
    juce::Point<float> positionIn (juce::ComponentPeer& peer, float rawX, float rawY)
    {
        const auto scale = (float) juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale;
        return peer.globalToLocal (juce::Point<float> (rawX, rawY) / scale);
    }

    juce::ModifierKeys modifiers (int buttons, int meta)
    {
        int flags = 0;
        if (buttons & 1)      flags |= juce::ModifierKeys::leftButtonModifier;     // MotionEvent.BUTTON_PRIMARY
        if (buttons & 2)      flags |= juce::ModifierKeys::rightButtonModifier;    // BUTTON_SECONDARY
        if (buttons & 4)      flags |= juce::ModifierKeys::middleButtonModifier;   // BUTTON_TERTIARY
        if (meta & 0x1)       flags |= juce::ModifierKeys::shiftModifier;          // KeyEvent.META_SHIFT_ON
        if (meta & 0x2)       flags |= juce::ModifierKeys::altModifier;            // META_ALT_ON
        if (meta & 0x1000)    flags |= juce::ModifierKeys::ctrlModifier;           // META_CTRL_ON
        return juce::ModifierKeys (flags);
    }

    void attachNewWindows()
    {
        if (bridgeClass == nullptr)
            return;
        auto* env = juce::getEnv();
        static const jmethodID attach = env->GetStaticMethodID (bridgeClass, "attach", "(Landroid/view/View;J)V");
        if (attach == nullptr)
            return;

        for (auto it = attached.begin(); it != attached.end();)
            it = juce::ComponentPeer::isValidPeer (it->first) && it->first->getNativeHandle() == it->second ? std::next (it) : attached.erase (it);

        for (int i = 0; i < juce::ComponentPeer::getNumPeers(); ++i)
        {
            auto* peer = juce::ComponentPeer::getPeer (i);
            if (peer->getNativeHandle() != nullptr && attached.insert ({ peer, peer->getNativeHandle() }).second)
            {
                // JUCE makes its mouse source on the first mouse event but drops that event (getOrCreateMouseInputSource
                // returns nothing the first time): one harmless event off screen, so the first real click counts
                static bool primed = false;
                if (! std::exchange (primed, true))
                    peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, juce::MouseInputSource::offscreenMousePos,
                                            {}, juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                            juce::Time::getMillisecondCounter());

                env->CallStaticVoidMethod (bridgeClass, attach, (jobject) peer->getNativeHandle(), (jlong) peer);
            }
        }
    }

    struct Watcher : juce::Timer
    {
        void timerCallback() override { attachNewWindows(); }
    };
}

void DexBridge::start()
{
    static Watcher watcher;
    watcher.startTimerHz (20);   // a popup menu gets the mouse within 50 ms of opening
}

bool DexBridge::isDesktop() { return desktop; }

void DexBridge::setOnChange (std::function<void (bool)> callback) { onChange = std::move (callback); }

void DexBridge::setDeckUrl (const juce::String& url) { deckUrl = url; }

// ---- called from MouseBridge.java, on Android's main thread (JUCE's message thread) ----

extern "C" JNIEXPORT void JNICALL Java_com_leftnoiseaudio_stinger_MouseBridge_nativeInit (JNIEnv* env, jclass, jclass bridge)
{
    if (bridgeClass == nullptr)
        bridgeClass = (jclass) env->NewGlobalRef (bridge);
}

extern "C" JNIEXPORT jstring JNICALL Java_com_leftnoiseaudio_stinger_MouseBridge_nativeDeckUrl (JNIEnv* env, jclass)
{
    return env->NewStringUTF (deckUrl.toRawUTF8());
}

extern "C" JNIEXPORT void JNICALL Java_com_leftnoiseaudio_stinger_MouseBridge_nativeSetDesktop (JNIEnv*, jclass, jboolean on)
{
    if (desktop == (on != JNI_FALSE))
        return;
    desktop = on != JNI_FALSE;
    juce::MessageManager::callAsync ([]   // not inside Android's own callback: the window gets rebuilt
    {
        if (onChange)
            onChange (desktop);
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_leftnoiseaudio_stinger_MouseBridge_nativeMouse (JNIEnv*, jclass, jlong peerPointer, jint buttons,
                                                                                          jfloat x, jfloat y, jlong time, jint meta)
{
    if (auto* peer = validPeer (peerPointer))
    {
        const auto mods = modifiers (buttons, meta);
        juce::ModifierKeys::currentModifiers = mods;
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, positionIn (*peer, x, y), mods,
                                juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, (juce::int64) time);
    }
}

extern "C" JNIEXPORT jobject JNICALL Java_com_leftnoiseaudio_stinger_MouseBridge_nativeKeyTarget (JNIEnv* env, jclass)
{
    // the window holding the focused component, else the biggest one (the main window)
    juce::ComponentPeer* target = nullptr;
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        target = focused->getPeer();
    if (target == nullptr)
        for (int i = 0; i < juce::ComponentPeer::getNumPeers(); ++i)
            if (auto* peer = juce::ComponentPeer::getPeer (i); peer->getComponent().isShowing()
                    && (target == nullptr || peer->getBounds().getWidth() > target->getBounds().getWidth()))
                target = peer;
    return target != nullptr && target->getNativeHandle() != nullptr ? env->NewLocalRef ((jobject) target->getNativeHandle()) : nullptr;
}

extern "C" JNIEXPORT void JNICALL Java_com_leftnoiseaudio_stinger_MouseBridge_nativeMouseExit (JNIEnv*, jclass, jlong peerPointer, jlong time)
{
    if (auto* peer = validPeer (peerPointer))
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, juce::MouseInputSource::offscreenMousePos,
                                juce::ModifierKeys::currentModifiers.withoutMouseButtons(),
                                juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, (juce::int64) time);
}

extern "C" JNIEXPORT void JNICALL Java_com_leftnoiseaudio_stinger_MouseBridge_nativeWheel (JNIEnv*, jclass, jlong peerPointer, jfloat x, jfloat y,
                                                                                         jfloat across, jfloat down, jlong time, jint meta)
{
    if (auto* peer = validPeer (peerPointer))
    {
        juce::ModifierKeys::currentModifiers = modifiers (0, meta);
        juce::MouseWheelDetails wheel;
        wheel.deltaX = -across * 0.47f;   // one notch, as on Windows (120 / 256)
        wheel.deltaY = down * 0.47f;
        wheel.isReversed = false;
        wheel.isSmooth = false;
        wheel.isInertial = false;
        peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::mouse, positionIn (*peer, x, y), (juce::int64) time, wheel);
    }
}
