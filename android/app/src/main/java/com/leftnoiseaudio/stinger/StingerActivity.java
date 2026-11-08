package com.leftnoiseaudio.stinger;

import android.app.Activity;
import android.app.ActivityOptions;
import android.content.Intent;
import android.content.Context;
import android.content.res.Configuration;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.Looper;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;
import android.view.KeyEvent;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/** Stinger's window: JUCE (started by com.rmsl.juce.JuceApp) puts the app in it. Full screen, and the screen
    stays on: a show must not go dark in the middle of a countdown.
    In Samsung DeX it opens the deck on the phone's own screen (DeckActivity) and closes it again when DeX ends. */
public class StingerActivity extends Activity
{
    private final Handler handler = new Handler (Looper.getMainLooper());

    /** Unplugging the monitor doesn't tell a paused Stinger anything (it sits behind the deck), so the screens are watched here. */
    private final DisplayManager.DisplayListener displays = new DisplayManager.DisplayListener()
    {
        @Override public void onDisplayAdded (int id)   { Log.i (Dex.TAG, "screen added: " + id); }
        @Override public void onDisplayChanged (int id) {}
        @Override public void onDisplayRemoved (int id)
        {
            Log.i (Dex.TAG, "screen removed: " + id);
            handler.postDelayed (StingerActivity.this::afterScreenRemoved, 500);   // let Android move the windows first
        }
    };

    /** The monitor went: if no other screen is left, the deck closes and Stinger comes to the front on the phone. */
    private void afterScreenRemoved()
    {
        final DisplayManager dm = (DisplayManager) getSystemService (Context.DISPLAY_SERVICE);
        for (Display d : dm.getDisplays())
            if (d.getDisplayId() != Display.DEFAULT_DISPLAY)
                return;
        final DeckActivity deck = DeckActivity.current.get();
        if (deck == null || deck.isFinishing() || deck.isDestroyed())
            return;
        Log.i (Dex.TAG, "monitor gone: Stinger to the front, deck closed");
        try
        {
            startActivity (new Intent (this, StingerActivity.class).addFlags (Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
        }
        catch (Throwable t)
        {
            Log.e (Dex.TAG, "couldn't bring Stinger to the front: " + t);
        }
        deck.finish();
    }

    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);
        getWindow().getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        getWindow().addFlags (WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        Dex.log (this, "created");
        MouseBridge.init();
        StreamDeckUsb.scan (this);   // a Stream Deck already plugged in
        ((DisplayManager) getSystemService (Context.DISPLAY_SERVICE)).registerDisplayListener (displays, handler);
    }

    /** A Stream Deck plugged in while Stinger runs (Android offers it to Stinger: res/xml/stream_deck_filter.xml). */
    @Override
    protected void onNewIntent (Intent intent)
    {
        super.onNewIntent (intent);
        if (android.hardware.usb.UsbManager.ACTION_USB_DEVICE_ATTACHED.equals (intent.getAction()))
            StreamDeckUsb.scan (this);
    }

    @Override
    protected void onResume()
    {
        super.onResume();
        Dex.log (this, "resumed");
        updateDeck();
    }

    @Override
    public void onConfigurationChanged (Configuration config)
    {
        super.onConfigurationChanged (config);
        Dex.log (this, "configuration changed");
        updateDeck();
    }

    @Override
    protected void onDestroy()
    {
        Log.i (Dex.TAG, "Stinger window closed");
        ((DisplayManager) getSystemService (Context.DISPLAY_SERVICE)).unregisterDisplayListener (displays);
        handler.removeCallbacksAndMessages (null);
        super.onDestroy();
    }

    /** DeX with Stinger off the phone's screen: the deck goes on the phone's screen. Otherwise no deck. */
    private void updateDeck()
    {
        final DeckActivity deck = DeckActivity.current.get();
        final Display mine = Dex.displayOf (this);
        final boolean wanted = Dex.isOn (this) && mine != null && mine.getDisplayId() != Display.DEFAULT_DISPLAY;
        MouseBridge.setDesktop (wanted);   // the desktop screen on the monitor, the touch pages on the phone
        if (! wanted)
        {
            if (deck != null && ! deck.isFinishing())
            {
                Log.i (Dex.TAG, "DeX off: closing the deck");
                deck.finish();
            }
            return;
        }
        if (deck != null && ! deck.isFinishing() && ! deck.isDestroyed())
            return;
        try
        {
            final Intent intent = new Intent (this, DeckActivity.class);
            intent.addFlags (Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_MULTIPLE_TASK);
            final ActivityOptions options = ActivityOptions.makeBasic();
            options.setLaunchDisplayId (Display.DEFAULT_DISPLAY);
            startActivity (intent, options.toBundle());
            Log.i (Dex.TAG, "deck asked for on screen " + Display.DEFAULT_DISPLAY);
        }
        catch (Throwable t)
        {
            Log.e (Dex.TAG, "couldn't open the deck on the phone's screen: " + t);
        }
    }

    /** Samsung DeX: a keyboard's keys reach this activity's window, not JUCE's: they're handed on to JUCE
        (Space for GO, the button keys...). Back stays Android's. */
    @Override
    public boolean dispatchKeyEvent (KeyEvent event)
    {
        return MouseBridge.forwardKey (event) || super.dispatchKeyEvent (event);
    }

    @Override
    public void onWindowFocusChanged (boolean hasFocus)
    {
        super.onWindowFocusChanged (hasFocus);
        if (hasFocus && ! Dex.isOn (this))   // a DeX window keeps its bars: it's a desktop
        {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null)
            {
                controller.hide (WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior (WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
    }
}
