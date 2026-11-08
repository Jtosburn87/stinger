package com.leftnoiseaudio.stinger;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.KeyEvent;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.window.OnBackInvokedDispatcher;

import java.io.InputStream;
import java.lang.ref.WeakReference;
import java.nio.charset.StandardCharsets;

/** The phone's own screen while Stinger is on the DeX monitor: the deck. It's the Wi-Fi remote's page (the same keys
    and layouts), served by Stinger on the phone itself (DexDeck.cpp). StingerActivity opens it when DeX starts and
    closes it when DeX ends. The screen stays on, and Back does nothing: a stray swipe mustn't close it mid-show. */
public class DeckActivity extends Activity
{
    static WeakReference<DeckActivity> current = new WeakReference<> (null);

    private WebView page;
    private final Handler handler = new Handler (Looper.getMainLooper());
    private String loaded = "";

    /** Stinger makes the page when its desktop screen first shows: until then, look again shortly. */
    private final Runnable load = new Runnable()
    {
        @Override public void run()
        {
            final String url = MouseBridge.deckUrl();
            if (url == null || url.isEmpty())
            {
                handler.postDelayed (this, 300);
                return;
            }
            if (! url.equals (loaded))
            {
                loaded = url;
                page.loadUrl (url);
            }
        }
    };

    @SuppressLint ("SetJavaScriptEnabled")
    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);
        current = new WeakReference<> (this);
        getWindow().addFlags (WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        page = new WebView (this);
        page.setBackgroundColor (Color.rgb (0x12, 0x14, 0x18));
        final WebSettings settings = page.getSettings();
        settings.setJavaScriptEnabled (true);      // the page's keys and live countdowns
        settings.setDomStorageEnabled (true);      // it remembers its layout choice (24 or 12 keys)
        settings.setSupportZoom (false);
        settings.setBuiltInZoomControls (false);
        page.setWebViewClient (new WebViewClient()   // links stay in the deck
        {
            @Override
            public void onPageFinished (WebView view, String url)
            {
                view.evaluateJavascript (layoutScript(), null);   // the deck's own layout (res/raw/deck_layout.js)
            }
        });
        page.setOverScrollMode (WebView.OVER_SCROLL_NEVER);
        setContentView (page);

        if (Build.VERSION.SDK_INT >= 33)
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback (OnBackInvokedDispatcher.PRIORITY_DEFAULT, () -> {});

        handler.post (load);
        Log.i (Dex.TAG, "deck opened");
    }

    private String layoutScript()
    {
        if (layout == null)
        {
            try (InputStream in = getResources().openRawResource (R.raw.deck_layout))
            {
                layout = new String (in.readAllBytes(), StandardCharsets.UTF_8);
            }
            catch (Exception e)
            {
                layout = "";
            }
        }
        return layout;
    }

    private String layout;

    @Override
    @SuppressWarnings ("deprecation")
    public void onBackPressed() {}   // Android 12 and earlier: Back does nothing here either

    @Override
    public void onWindowFocusChanged (boolean hasFocus)
    {
        super.onWindowFocusChanged (hasFocus);
        if (! hasFocus)
            return;
        final WindowInsetsController controller = getWindow().getInsetsController();
        if (controller != null)
        {
            controller.hide (WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
            controller.setSystemBarsBehavior (WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }
    }

    @Override
    protected void onResume()
    {
        super.onResume();
        page.onResume();
        handler.post (load);   // Stinger may have a new page (it was restarted)
    }

    @Override
    protected void onPause()
    {
        super.onPause();
        page.onPause();
    }

    /** In DeX a keyboard's keys often land here (the newest activity), not on the monitor: they belong to Stinger. */
    @Override
    public boolean dispatchKeyEvent (KeyEvent event)
    {
        return MouseBridge.forwardKey (event) || super.dispatchKeyEvent (event);
    }

    @Override
    protected void onDestroy()
    {
        handler.removeCallbacksAndMessages (null);
        page.destroy();
        Log.i (Dex.TAG, "deck closed");
        super.onDestroy();
    }
}
