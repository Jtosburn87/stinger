package com.leftnoiseaudio.stinger;

import android.app.Activity;
import android.content.Context;
import android.content.res.Configuration;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.util.Log;
import android.view.Display;

/** Samsung DeX: is Stinger on the DeX desktop, and what screens are there. (DeX probe: everything is logged
    under the tag "StingerDex", read with: adb logcat -s StingerDex) */
final class Dex
{
    static final String TAG = "StingerDex";

    private Dex() {}

    /** Samsung's own check (their developer guide): a field Samsung adds to Configuration. Not on other phones. */
    static String samsungDesktopMode (Configuration config)
    {
        try
        {
            final Class<?> type = config.getClass();
            final int enabled = type.getField ("SEM_DESKTOP_MODE_ENABLED").getInt (type);
            final int mode = type.getField ("semDesktopModeEnabled").getInt (config);
            return mode == enabled ? "on" : "off";
        }
        catch (Throwable t)
        {
            return "unknown (" + t.getClass().getSimpleName() + ")";
        }
    }

    /** On the DeX desktop: Samsung says so, or the window isn't on the phone's own screen. (One UI 9 on the Fold 7
        still has Samsung's field but it reads "off" in DeX, so the screen decides.) */
    static boolean isOn (Activity activity)
    {
        if (samsungDesktopMode (activity.getResources().getConfiguration()).equals ("on"))
            return true;
        final Display display = displayOf (activity);
        return display != null && display.getDisplayId() != Display.DEFAULT_DISPLAY;
    }

    static Display displayOf (Activity activity)
    {
        return Build.VERSION.SDK_INT >= 30 ? activity.getDisplay() : activity.getWindowManager().getDefaultDisplay();
    }

    static String describe (Activity activity, String event)
    {
        final Configuration config = activity.getResources().getConfiguration();
        final StringBuilder s = new StringBuilder();
        s.append (event).append (": ").append (activity.getClass().getSimpleName());
        s.append ("\n  Samsung desktop mode: ").append (samsungDesktopMode (config));
        s.append ("\n  DeX (Stinger's answer): ").append (isOn (activity) ? "yes" : "no");
        final Display mine = displayOf (activity);
        s.append ("\n  this window's screen: ").append (mine == null ? "?" : mine.getDisplayId() + " \"" + mine.getName() + "\"");
        s.append ("\n  window ").append (config.screenWidthDp).append (" x ").append (config.screenHeightDp).append (" dp, density ")
         .append (config.densityDpi).append (" dpi, ui mode type ").append (config.uiMode & Configuration.UI_MODE_TYPE_MASK)
         .append (", multi-window ").append (activity.isInMultiWindowMode());
        final DisplayManager dm = (DisplayManager) activity.getSystemService (Context.DISPLAY_SERVICE);
        for (Display d : dm.getDisplays())
            s.append ("\n  screen ").append (d.getDisplayId()).append (" \"").append (d.getName()).append ("\" ")
             .append (d.getMode().getPhysicalWidth()).append (" x ").append (d.getMode().getPhysicalHeight())
             .append (d.getState() == Display.STATE_ON ? " on" : " state " + d.getState());
        return s.toString();
    }

    static void log (Activity activity, String event)
    {
        for (String line : describe (activity, event).split ("\n"))
            Log.i (TAG, line);
    }
}
