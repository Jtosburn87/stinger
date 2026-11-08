package com.leftnoiseaudio.stinger;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.util.Log;

import java.util.Arrays;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** An Elgato Stream Deck plugged into the phone (through the DeX dock): Stinger drives it itself over USB, as Elgato's
    own software doesn't run on Android. Key presses go to DexStreamDeck.cpp, which sends back the key pictures.
    The 15-key decks (original v2, MK.2) and the XL (32 keys) use the same protocol: 1024-byte image reports
    (JPEG, sent in pages), a 512-byte key report, and feature reports for reset and brightness. */
final class StreamDeckUsb
{
    private StreamDeckUsb() {}

    static final String TAG = "StingerDeckUsb";
    private static final int ELGATO = 0x0fd9;
    private static final String PERMISSION = "com.leftnoiseaudio.stinger.STREAM_DECK_PERMISSION";
    private static final int REPORT = 1024, HEADER = 8, BRIGHTNESS = 80;

    /** keys, columns, key size in pixels; null = a deck Stinger doesn't drive (yet). */
    static int[] modelOf (int productId)
    {
        switch (productId)
        {
            case 0x006d:   // Stream Deck (original, v2)
            case 0x0080:   // Stream Deck MK.2
            case 0x00a5:   // Stream Deck MK.2 (scissor keys)
                return new int[] { 15, 5, 72 };
            case 0x006c:   // Stream Deck XL
            case 0x008f:   // Stream Deck XL (v2)
                return new int[] { 32, 8, 96 };
            default:
                return null;
        }
    }

    private static Context context;
    private static UsbManager manager;
    private static boolean registered = false;

    private static UsbDevice device;
    private static UsbDeviceConnection connection;
    private static UsbInterface usbInterface;
    private static UsbEndpoint in, out;
    private static ExecutorService writer;
    private static Thread reader;
    private static volatile boolean open = false;

    private static final BroadcastReceiver receiver = new BroadcastReceiver()
    {
        @Override
        @SuppressWarnings ("deprecation")   // the typed getParcelableExtra is Android 13+; Stinger runs from 10
        public void onReceive (Context c, Intent intent)
        {
            final UsbDevice d = intent.getParcelableExtra (UsbManager.EXTRA_DEVICE);
            if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals (intent.getAction()))
            {
                if (d != null && device != null && d.getDeviceName().equals (device.getDeviceName()))
                    close();
            }
            else if (PERMISSION.equals (intent.getAction()))
            {
                if (d != null && intent.getBooleanExtra (UsbManager.EXTRA_PERMISSION_GRANTED, false))
                    openDevice (d);
                else
                    Log.i (TAG, "permission refused");
            }
        }
    };

    /** From StingerActivity: when it starts, and when Android says a deck was plugged in. */
    static void scan (Context c)
    {
        if (context == null)
        {
            context = c.getApplicationContext();
            manager = (UsbManager) context.getSystemService (Context.USB_SERVICE);
            nativeInit (StreamDeckUsb.class);
        }
        if (! registered)
        {
            final IntentFilter filter = new IntentFilter();
            filter.addAction (PERMISSION);
            filter.addAction (UsbManager.ACTION_USB_DEVICE_DETACHED);
            if (Build.VERSION.SDK_INT >= 33)
                context.registerReceiver (receiver, filter, Context.RECEIVER_NOT_EXPORTED);
            else
                context.registerReceiver (receiver, filter);
            registered = true;
        }
        if (open || manager == null)
            return;
        for (UsbDevice d : manager.getDeviceList().values())
        {
            if (d.getVendorId() != ELGATO || modelOf (d.getProductId()) == null)
                continue;
            if (manager.hasPermission (d))
                openDevice (d);
            else
            {
                final Intent intent = new Intent (PERMISSION).setPackage (context.getPackageName());
                manager.requestPermission (d, PendingIntent.getBroadcast (context, 0, intent, PendingIntent.FLAG_MUTABLE));
            }
            return;   // one deck at a time
        }
    }

    private static synchronized void openDevice (UsbDevice d)
    {
        if (open)
            return;
        final int[] model = modelOf (d.getProductId());
        if (model == null)
            return;
        final UsbDeviceConnection c = manager.openDevice (d);
        if (c == null)
            return;
        final UsbInterface face = d.getInterface (0);
        if (! c.claimInterface (face, true))
        {
            c.close();
            return;
        }
        UsbEndpoint i = null, o = null;
        for (int e = 0; e < face.getEndpointCount(); ++e)
        {
            final UsbEndpoint ep = face.getEndpoint (e);
            if (ep.getDirection() == UsbConstants.USB_DIR_IN) i = ep; else o = ep;
        }
        if (i == null || o == null)
        {
            c.releaseInterface (face);
            c.close();
            return;
        }
        device = d;
        connection = c;
        usbInterface = face;
        in = i;
        out = o;
        open = true;
        writer = Executors.newSingleThreadExecutor();

        feature (new byte[] { 0x03, 0x02 });                       // reset: clears the keys
        feature (new byte[] { 0x03, 0x08, (byte) BRIGHTNESS });   // brightness, percent

        final int keys = model[0];
        reader = new Thread (() -> readKeys (keys), "Stream Deck keys");
        reader.start();
        Log.i (TAG, "opened " + d.getProductName() + " (" + Integer.toHexString (d.getProductId()) + "), " + keys + " keys");
        nativeConnected (d.getProductId(), model[0], model[1], model[2]);
    }

    private static void readKeys (int keys)
    {
        final byte[] buffer = new byte[512];
        final boolean[] down = new boolean[keys];
        while (open)
        {
            final UsbDeviceConnection c = connection;
            if (c == null)
                break;
            final int n = c.bulkTransfer (in, buffer, buffer.length, 250);
            if (n < 4 + keys || buffer[0] != 0x01)
                continue;   // a timeout, or not a key report
            for (int k = 0; k < keys; ++k)
            {
                final boolean now = buffer[4 + k] != 0;
                if (now != down[k])
                {
                    down[k] = now;
                    nativeKey (k, now);
                }
            }
        }
    }

    private static void feature (byte[] data)
    {
        final byte[] report = Arrays.copyOf (data, 32);
        // HID SET_REPORT (feature) on the deck's interface
        connection.controlTransfer (0x21, 0x09, 0x0300 | (report[0] & 0xff), usbInterface.getId(), report, report.length, 1000);
    }

    /** From DexStreamDeck.cpp: one key's picture (a JPEG already turned the way the deck wants it). */
    static void sendImage (int key, byte[] jpeg)
    {
        final ExecutorService w = writer;
        if (! open || w == null)
            return;
        w.execute (() ->
        {
            final UsbDeviceConnection c = connection;
            if (! open || c == null)
                return;
            final byte[] report = new byte[REPORT];
            int sent = 0, page = 0;
            while (sent < jpeg.length || page == 0)
            {
                final int length = Math.min (REPORT - HEADER, jpeg.length - sent);
                final boolean last = sent + length >= jpeg.length;
                Arrays.fill (report, (byte) 0);
                report[0] = 0x02;
                report[1] = 0x07;
                report[2] = (byte) key;
                report[3] = (byte) (last ? 1 : 0);
                report[4] = (byte) (length & 0xff);
                report[5] = (byte) (length >> 8);
                report[6] = (byte) (page & 0xff);
                report[7] = (byte) (page >> 8);
                System.arraycopy (jpeg, sent, report, HEADER, length);
                if (c.bulkTransfer (out, report, REPORT, 1000) < 0)
                    return;   // unplugged: the detach message tidies up
                sent += length;
                ++page;
                if (last)
                    break;
            }
        });
    }

    static synchronized void close()
    {
        if (! open)
            return;
        open = false;
        try
        {
            if (writer != null)
                writer.shutdownNow();
            if (reader != null)
                reader.join (500);
        }
        catch (InterruptedException ignored) {}
        try
        {
            feature (new byte[] { 0x03, 0x02 });   // back to Elgato's own picture if still plugged in
        }
        catch (Throwable ignored) {}
        connection.releaseInterface (usbInterface);
        connection.close();
        connection = null;
        device = null;
        writer = null;
        reader = null;
        Log.i (TAG, "closed");
        nativeDisconnected();
    }

    private static native void nativeInit (Class<?> bridge);
    private static native void nativeConnected (int productId, int keys, int columns, int pixels);
    private static native void nativeKey (int key, boolean down);
    private static native void nativeDisconnected();
}
