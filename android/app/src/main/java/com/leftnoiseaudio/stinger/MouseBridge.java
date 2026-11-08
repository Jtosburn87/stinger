package com.leftnoiseaudio.stinger;

import android.content.Context;
import android.view.InputDevice;
import android.view.WindowManager;
import android.view.MotionEvent;
import android.view.View;

/** Samsung DeX: a real mouse for Stinger. JUCE on Android takes every click as a finger (left button only, no
    hover, no wheel), so mouse events on JUCE's windows are taken here first and handed to
    Stinger's C++ (DexBridge.cpp) as mouse events. Fingers still go to JUCE as before. */
final class MouseBridge
{
    private MouseBridge() {}

    /** Called once, from StingerActivity (JUCE's library is loaded by then). */
    static void init()
    {
        nativeInit (MouseBridge.class);
    }

    /** Called from C++ for every new JUCE window (peer is its ComponentPeer pointer). */
    static void attach (View view, long peer)
    {
        final int[] held = { 0 };   // the buttons pressed at ACTION_DOWN

        if (desktop)
        {
            // DeX: JUCE's window says it takes typing, so Android gives the keyboard app (SwiftKey...) a hardware key
            // first and it eats it. Kept away from the keyboard app, the keys reach Stinger (JUCE types into its own
            // text boxes). JUCE replaces the window's settings when it moves or resizes, so this is put back each time.
            keepKeyboardAppAway (view);
            view.addOnLayoutChangeListener ((v, l, t, r, b, ol, ot, or, ob) -> keepKeyboardAppAway (v));
        }

        view.setOnTouchListener ((v, e) ->
        {
            if (! isMouse (e))
                return false;   // a finger: JUCE's own handling
            final int action = e.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN)
            {
                held[0] = e.getButtonState() & 7;
                if (held[0] == 0)
                    held[0] = MotionEvent.BUTTON_PRIMARY;   // a touchpad tap (or an injected click) says no button: it's a click
            }
            else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL)
                held[0] = 0;
            else if (action != MotionEvent.ACTION_MOVE)
                return true;
            final int buttons = held[0];
            nativeMouse (peer, buttons, e.getRawX(), e.getRawY(), e.getEventTime(), e.getMetaState());
            return true;   // also keeps a right-click from turning into Back
        });

        view.setOnHoverListener ((v, e) ->
        {
            if (! isMouse (e))
                return false;
            if (e.getActionMasked() == MotionEvent.ACTION_HOVER_EXIT)
                nativeMouseExit (peer, e.getEventTime());
            else
                nativeMouse (peer, 0, e.getRawX(), e.getRawY(), e.getEventTime(), e.getMetaState());
            return true;
        });

        view.setOnGenericMotionListener ((v, e) ->
        {
            if (! isMouse (e) || e.getActionMasked() != MotionEvent.ACTION_SCROLL)
                return false;
            nativeWheel (peer, e.getRawX(), e.getRawY(), e.getAxisValue (MotionEvent.AXIS_HSCROLL),
                         e.getAxisValue (MotionEvent.AXIS_VSCROLL), e.getEventTime(), e.getMetaState());
            return true;
        });
    }

    private static void keepKeyboardAppAway (View view)
    {
        if (! (view.getLayoutParams() instanceof WindowManager.LayoutParams) || ! view.isAttachedToWindow())
            return;
        final WindowManager.LayoutParams params = (WindowManager.LayoutParams) view.getLayoutParams();
        if ((params.flags & WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM) != 0)
            return;
        params.flags |= WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM;
        try
        {
            ((WindowManager) view.getContext().getSystemService (Context.WINDOW_SERVICE)).updateViewLayout (view, params);
        }
        catch (Throwable ignored) {}   // the window is going
    }

    private static boolean isMouse (MotionEvent e)
    {
        return e.getToolType (0) == MotionEvent.TOOL_TYPE_MOUSE || e.isFromSource (InputDevice.SOURCE_MOUSE);
    }

    /** DeX on or off for Stinger's window (StingerActivity). */
    static void setDesktop (boolean on)
    {
        desktop = on;
        nativeSetDesktop (on);
    }

    static boolean isDesktop() { return desktop; }

    /** In DeX the keyboard goes to Stinger's activity window, not to JUCE's own windows: this is the JUCE window
        (the one with the keyboard focus) that should have it. Null if there's none. */
    static View keyTarget() { return nativeKeyTarget(); }

    /** A keyboard key, from whichever of Stinger's activities Android gave it to (in DeX that's often the deck on the
        phone's screen, the newest one): handed to Stinger. Back stays Android's. True if Stinger took it. */
    static boolean forwardKey (android.view.KeyEvent event)
    {
        if (! desktop || event.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK)
            return false;
        final View target = keyTarget();
        return target != null && target.dispatchKeyEvent (event);
    }

    private static boolean desktop = false;

    private static native void nativeInit (Class<?> bridge);
    private static native void nativeSetDesktop (boolean on);
    private static native void nativeMouse (long peer, int buttons, float rawX, float rawY, long time, int meta);
    private static native View nativeKeyTarget();
    /** The deck page (DexDeck.cpp), "" until Stinger's desktop screen has made it. */
    static String deckUrl() { return nativeDeckUrl(); }
    private static native String nativeDeckUrl();
    private static native void nativeMouseExit (long peer, long time);
    private static native void nativeWheel (long peer, float rawX, float rawY, float across, float down, long time, int meta);
}
