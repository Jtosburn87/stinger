#include "DexStreamDeck.h"
#include "Look.h"
#include <jni.h>

namespace juce { extern JNIEnv* getEnv() noexcept; }   // JUCE's own (juce_JNIHelpers_android.h isn't in its public headers)

namespace
{
    jclass usbClass = nullptr;
    DexStreamDeck* instance = nullptr;

    struct Deck
    {
        bool connected = false;
        int productId = 0, keys = 0, columns = 0, pixels = 72;
    };
    Deck deck;   // message thread only

    juce::Colour colourFrom (const juce::var& v, juce::Colour fallback = juce::Colour (0xff5aa9f0))
    {
        const auto text = v.toString().trimCharactersAtStart ("#");
        return text.length() == 6 ? juce::Colour::fromString ("ff" + text) : fallback;
    }

    juce::String countdown (double seconds, bool tenths)
    {
        seconds = std::max (0.0, seconds);
        if (tenths && seconds < 10.0)
            return "-" + juce::String (seconds, 1);
        const int s = (int) std::ceil (seconds);
        return "-" + juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }

    const juce::Colour ground { 0xff121418 }, raised { 0xff1e2228 }, faint { 0xff3a414b }, dim { 0xff8a929c };

    /** The cue on button `slot` of the shown bank in the state message (null var if it's empty). */
    juce::var cueAt (const juce::var& state, int slot)
    {
        const int bank = (int) state.getProperty ("bank", 0);
        if (auto* cues = state.getProperty ("cues", {}).getArray())
            for (auto& c : *cues)
                if ((int) c.getProperty ("bank", -1) == bank && (int) c.getProperty ("slot", -1) == slot)
                    return c;
        return {};
    }
}

DexStreamDeck::DexStreamDeck (MainComponent& d) : desktop (d)
{
    instance = this;
    deckChanged();
    startTimerHz (10);
}

DexStreamDeck::~DexStreamDeck()
{
    stopTimer();
    if (instance == this)
        instance = nullptr;
}

void DexStreamDeck::deckChanged()
{
    lastState = {};
    shown.assign ((size_t) (deck.connected ? deck.keys : 0), {});
}

DexStreamDeck::Key DexStreamDeck::keyAt (int index) const
{
    Key k;
    auto button = [&k] (int slot) { k.kind = "button"; k.slot = slot; k.action = "slot" + juce::String (slot + 1); };
    auto command = [&k] (const char* id) { k.kind = id; k.action = id; };

    if (deck.keys == 15)   // 5 x 3: buttons 1-12, then GO, Stop all, Next bank
    {
        if (index < 12)
            button (index);
        else
            command (index == 12 ? "go" : index == 13 ? "stopAll" : "bankNext");
    }
    else if (deck.keys == 32)   // 8 x 4: Stinger's 6 x 4 where it is, then two columns
    {
        const int row = index / 8, column = index % 8;
        static const char* left[]  = { "go", "stopAll", "bankPrev", "countdown" };
        static const char* right[] = { "fadeNext", "fadeAll", "bankNext", "selfDestruct" };
        if (column < 6)
            button (row * 6 + column);
        else
            command (column == 6 ? left[row] : right[row]);
        if (k.kind == "countdown")
            k.action = {};   // shows, doesn't do
    }
    return k;
}

void DexStreamDeck::keyPressed (int key)
{
    if (! deck.connected || key < 0 || key >= deck.keys)
        return;
    const auto k = keyAt (key);
    if (k.action.isNotEmpty() && desktop.getStreamDeck().perform)
        desktop.getStreamDeck().perform (k.action);
}

void DexStreamDeck::timerCallback()
{
    if (! deck.connected || usbClass == nullptr)
        return;
    const auto text = desktop.getStreamDeck().stateJson();
    if (text == lastState)
        return;
    lastState = text;
    const auto state = juce::JSON::parse (text);

    for (int i = 0; i < deck.keys; ++i)
    {
        const auto k = keyAt (i);
        // what this key's picture depends on: a new picture (and USB traffic) only when that changes
        juce::String what = k.kind;
        if (k.kind == "button")
            what << (int) state.getProperty ("bank", 0) << juce::JSON::toString (cueAt (state, k.slot), true);
        else if (k.kind == "go")
            what << juce::JSON::toString (state.getProperty ("go", {}), true);
        else if (k.kind == "bankPrev" || k.kind == "bankNext")
            what << juce::JSON::toString (state.getProperty ("banks", {}), true) << (int) state.getProperty ("bank", 0);
        else if (k.kind == "countdown")
            what << juce::JSON::toString (state.getProperty ("now", {}), true);
        else if (k.kind == "selfDestruct")
            what << ((bool) state.getProperty ("selfDestruct", false) ? "on" : "off");
        if ((size_t) i < shown.size() && shown[(size_t) i] == what)
            continue;
        if ((size_t) i < shown.size())
            shown[(size_t) i] = what;
        send (i, draw (k, state, deck.pixels));
    }
}

juce::Image DexStreamDeck::draw (const Key& k, const juce::var& state, int pixels) const
{
    juce::Image image (juce::Image::RGB, pixels, pixels, true);
    juce::Graphics g (image);
    // designed at 144 x 144 like the plugin's keys, and turned half a turn: these decks show their pictures upside down
    g.addTransform (juce::AffineTransform::scale ((float) pixels / 144.0f).rotated (juce::MathConstants<float>::pi,
                                                                                   (float) pixels * 0.5f, (float) pixels * 0.5f));
    const juce::Rectangle<float> all (0.0f, 0.0f, 144.0f, 144.0f);

    auto text = [&g] (const juce::String& t, juce::Rectangle<float> area, float size, juce::Colour colour, int lines = 3,
                      juce::Justification just = juce::Justification::centred)
    {
        g.setColour (colour);
        g.setFont (Look::condensed (size).boldened());
        g.drawFittedText (t, area.toNearestInt(), just, lines, 0.7f);
    };
    auto bar = [&g] (float progress, juce::Colour colour)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (8.0f, 128.0f, 128.0f, 8.0f);
        g.setColour (colour);
        g.fillRect (8.0f, 128.0f, 128.0f * juce::jlimit (0.0f, 1.0f, progress), 8.0f);
    };

    if (k.kind == "button")
    {
        const auto cue = cueAt (state, k.slot);
        if (! cue.isObject())
        {
            g.fillAll (ground);
            const auto bank = (int) state.getProperty ("bank", 0);
            text (juce::String::charToString ((juce::juce_wchar) ('A' + bank)) + juce::String (k.slot + 1), all, 34.0f, faint, 1);
            return image;
        }
        const auto colour = colourFrom (cue.getProperty ("colour", {}));
        const auto name = cue.getProperty ("name", {}).toString();
        if ((bool) cue.getProperty ("playing", false))
        {
            g.fillAll (colour);
            const auto on = Look::textOn (colour);
            text (name, all.withHeight (40.0f).reduced (8.0f, 4.0f), 20.0f, on, 2);
            const auto wait = cue.getProperty ("waitLeft", {});
            if (! wait.isVoid())
                text ("NEXT IN " + juce::String ((int) wait), juce::Rectangle<float> (0.0f, 44.0f, 144.0f, 56.0f), 26.0f, on, 1);
            else
                text (countdown ((double) cue.getProperty ("remaining", 0), false), juce::Rectangle<float> (0.0f, 42.0f, 144.0f, 60.0f), 46.0f, on, 1);
            if ((bool) cue.getProperty ("looping", false))
                text ("LOOP", juce::Rectangle<float> (0.0f, 100.0f, 144.0f, 24.0f), 18.0f, on, 1);
            bar ((float) (double) cue.getProperty ("progress", 0.0), (bool) cue.getProperty ("stopping", false) ? on.withAlpha (0.5f) : on);
            return image;
        }
        if ((bool) cue.getProperty ("spent", false))
        {
            g.fillAll (juce::Colours::black);
            text (name, all.reduced (10.0f), 24.0f, dim);
            return image;
        }
        g.fillAll (Look::shade (colour, 0.42f));
        g.setColour (colour);
        g.fillRect (0.0f, 0.0f, 144.0f, 14.0f);
        const bool next = (bool) cue.getProperty ("next", false);
        text (name, juce::Rectangle<float> (8.0f, 18.0f, 128.0f, next ? 92.0f : 118.0f), 26.0f, juce::Colours::white);
        if (next)
        {
            g.setColour (Look::goGreen);
            g.fillRoundedRectangle (70.0f, 114.0f, 66.0f, 24.0f, 5.0f);
            text ("NEXT", juce::Rectangle<float> (70.0f, 114.0f, 66.0f, 24.0f), 17.0f, juce::Colours::white, 1);
        }
        return image;
    }

    if (k.kind == "go")
    {
        const auto go = state.getProperty ("go", {});
        g.fillAll (Look::goGreen);
        text ("GO", juce::Rectangle<float> (0.0f, 14.0f, 144.0f, 70.0f), 64.0f, juce::Colours::white, 1);
        const auto next = go.getProperty ("next", {}).toString();
        text ((bool) go.getProperty ("looping", false) ? juce::String ("LOOPING") : next.isNotEmpty() ? next : juce::String ("(end of list)"),
              juce::Rectangle<float> (6.0f, 88.0f, 132.0f, 50.0f), 20.0f, juce::Colours::white.withAlpha (0.9f), 2);
        return image;
    }

    if (k.kind == "countdown")
    {
        const auto now = state.getProperty ("now", {});
        if (! now.isObject())
        {
            g.fillAll (ground);
            text ("--:--", all, 40.0f, faint, 1);
            return image;
        }
        const double left = (double) now.getProperty ("remaining", 0.0);
        const auto colour = left < 10.0 ? Look::red : colourFrom (now.getProperty ("colour", {}));
        g.fillAll (colour);
        const auto on = Look::textOn (colour);
        text (now.getProperty ("name", {}).toString(), all.withHeight (40.0f).reduced (8.0f, 4.0f), 20.0f, on, 2);
        text (countdown (left, true), juce::Rectangle<float> (0.0f, 42.0f, 144.0f, 60.0f), 46.0f, on, 1);
        bar ((float) (double) now.getProperty ("progress", 0.0), on);
        return image;
    }

    if (k.kind == "bankPrev" || k.kind == "bankNext")
    {
        g.fillAll (raised);
        text (k.kind == "bankPrev" ? juce::String::fromUTF8 ("\xe2\x97\x80 BANK") : juce::String::fromUTF8 ("BANK \xe2\x96\xb6"),
              juce::Rectangle<float> (0.0f, 20.0f, 144.0f, 44.0f), 28.0f, juce::Colours::white, 1);
        const int bank = (int) state.getProperty ("bank", 0);
        juce::String name;
        if (auto* banks = state.getProperty ("banks", {}).getArray(); banks != nullptr && bank < banks->size())
            name = (*banks)[bank].toString();
        text (name, juce::Rectangle<float> (6.0f, 70.0f, 132.0f, 60.0f), 22.0f, Look::amber, 2);
        return image;
    }

    if (k.kind == "selfDestruct")
    {
        const bool on = (bool) state.getProperty ("selfDestruct", false);
        g.fillAll (on ? Look::red : raised);
        text ("SELF\nDESTRUCT", all.reduced (8.0f), 26.0f, on ? juce::Colours::white : dim, 2);
        return image;
    }

    // Stop all, Fade all, Fade to next
    const bool stop = k.kind == "stopAll";
    g.fillAll (stop ? Look::red : raised);
    text (stop ? "STOP\nALL" : k.kind == "fadeAll" ? "FADE\nALL" : "FADE TO\nNEXT", all.reduced (8.0f), 32.0f, juce::Colours::white, 2);
    return image;
}

void DexStreamDeck::send (int index, const juce::Image& picture)
{
    juce::MemoryOutputStream jpeg;
    juce::JPEGImageFormat format;
    format.setQuality (0.92f);
    if (! format.writeImageToStream (picture, jpeg) || usbClass == nullptr)
        return;

    auto* env = juce::getEnv();
    static const jmethodID sendImage = env->GetStaticMethodID (usbClass, "sendImage", "(I[B)V");
    if (sendImage == nullptr)
        return;
    const auto size = (jsize) jpeg.getDataSize();
    jbyteArray bytes = env->NewByteArray (size);
    env->SetByteArrayRegion (bytes, 0, size, (const jbyte*) jpeg.getData());
    env->CallStaticVoidMethod (usbClass, sendImage, (jint) index, bytes);
    env->DeleteLocalRef (bytes);
}

// ---- called from StreamDeckUsb.java ----

extern "C" JNIEXPORT void JNICALL Java_com_leftnoiseaudio_stinger_StreamDeckUsb_nativeInit (JNIEnv* env, jclass, jclass bridge)
{
    if (usbClass == nullptr)
        usbClass = (jclass) env->NewGlobalRef (bridge);
}

extern "C" JNIEXPORT void JNICALL Java_com_leftnoiseaudio_stinger_StreamDeckUsb_nativeConnected (JNIEnv*, jclass, jint productId, jint keys,
                                                                                                jint columns, jint pixels)
{
    juce::MessageManager::callAsync ([productId, keys, columns, pixels]
    {
        deck = { true, (int) productId, (int) keys, (int) columns, (int) pixels };
        if (instance != nullptr)
            instance->deckChanged();
    });
}

extern "C" JNIEXPORT void JNICALL Java_com_leftnoiseaudio_stinger_StreamDeckUsb_nativeKey (JNIEnv*, jclass, jint key, jboolean down)
{
    if (down == JNI_FALSE)
        return;   // keys act when pressed, as on the Stream Deck plugin
    juce::MessageManager::callAsync ([key] { if (instance != nullptr) instance->keyPressed ((int) key); });
}

extern "C" JNIEXPORT void JNICALL Java_com_leftnoiseaudio_stinger_StreamDeckUsb_nativeDisconnected (JNIEnv*, jclass)
{
    juce::MessageManager::callAsync ([]
    {
        deck = {};
        if (instance != nullptr)
            instance->deckChanged();
    });
}
