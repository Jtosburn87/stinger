#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <winsock2.h>
 #include <ws2tcpip.h>
 #include <iphlpapi.h>
 #pragma comment (lib, "iphlpapi.lib")
#else
 #include <ifaddrs.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
 #include <net/if.h>
#endif

#include "Remote.h"
#include "Look.h"
#include "BinaryData.h"
#include "../third_party/qrcodegen/qrcodegen.hpp"

namespace
{
    juce::String makeKey()
    {
        return juce::Uuid().toDashedString().removeCharacters ("-").substring (0, 24);
    }

    bool isColour (const juce::String& s)
    {
        return s.length() == 7 && s[0] == '#' && s.substring (1).containsOnly ("0123456789abcdefABCDEF");
    }

    /** Only the fields the page uses, so a phone can't fill the settings with junk. */
    juce::var cleanKey (const juce::var& k)
    {
        static const juce::StringArray kinds { "button", "countdown", "nextcue", "hold", "schedgo", "rungo", "go", "fadenext", "stopall", "fadeall",
                                               "bank", "selfdestruct", "resetcolours", "empty" };
        const auto kind = k.getProperty ("kind", {}).toString();
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", kinds.contains (kind) ? kind : juce::String ("empty"));
        if (kind == "button")
        {
            o->setProperty ("mode", k.getProperty ("mode", {}).toString() == "fixed" ? "fixed" : "shown");
            o->setProperty ("position", juce::jlimit (1, Show::cuesPerBank, (int) k.getProperty ("position", 1)));
            o->setProperty ("bank", juce::jlimit (0, 7, (int) k.getProperty ("bank", 0)));
        }
        else if (kind == "hold")
        {
            const int minutes = (int) k.getProperty ("minutes", 5);
            o->setProperty ("minutes", minutes == 1 || minutes == 2 || minutes == 10 ? minutes : 5);
        }
        else if (kind == "go" || kind == "bank")
        {
            // "selected" / "next" / "previous" or a number
            const auto field = kind == "go" ? "playlist" : "bank";
            o->setProperty (field, k.getProperty (field, {}).toString().substring (0, 12).retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789"));
            if (kind == "go" && isColour (k.getProperty ("keyColour", {}).toString()))
                o->setProperty ("keyColour", k.getProperty ("keyColour", {}).toString());
        }
        // your own text on the key: up to 60 characters, size, top / middle / bottom, colour
        auto text = k.getProperty ("text", {}).toString().substring (0, 60);
        text = text.removeCharacters ("\r\t").replaceCharacters (juce::String::charToString (0), " ");
        if (kind != "empty" && text.trim().isNotEmpty())
        {
            o->setProperty ("text", text);
            o->setProperty ("size", juce::jlimit (8, 72, (int) k.getProperty ("size", 22)));
            const auto align = k.getProperty ("align", {}).toString();
            o->setProperty ("align", align == "top" || align == "bottom" ? align : juce::String ("middle"));
            if (isColour (k.getProperty ("textColour", {}).toString()))
                o->setProperty ("textColour", k.getProperty ("textColour", {}).toString());
        }
        return juce::var (o);
    }
}

Remote::Remote (ShowController& c, juce::PropertiesFile& s) : controller (c), settings (s)
{
    layouts = juce::JSON::parse (settings.getValue ("remoteLayouts"));
    if (! layouts.isObject())
        layouts = juce::var (new juce::DynamicObject());
    if (settings.getValue ("remoteKey").isEmpty())
        settings.setValue ("remoteKey", makeKey());
}

Remote::~Remote()
{
    server = nullptr;
}

void Remote::restore()
{
    if (settings.getBoolValue ("remoteOn", false))
        start();
}

void Remote::setOn (bool shouldBeOn)
{
    settings.setValue ("remoteOn", shouldBeOn);
    if (shouldBeOn && server == nullptr)
        start();
    else if (! shouldBeOn)
        server = nullptr;
    sendChangeMessage();
}

void Remote::start()
{
    StreamDeckServer::Options options;
    options.address = {};   // every adapter: Wi-Fi, hotspot, Ethernet
    options.key = settings.getValue ("remoteKey");
    options.page = juce::String::fromUTF8 (BinaryData::remote_html, BinaryData::remote_htmlSize);
    server = nullptr;   // a new key: the old server (and its phones) go first
    server = std::make_unique<StreamDeckServer> (controller, port, options);
    server->perform = [this] (const juce::String& action) { if (perform) perform (action); };
    server->shownBank = [this] { return shownBank ? shownBank() : 0; };
    server->colourOf = [this] (const Cue& cue) { return colourOf ? colourOf (cue) : juce::String ("#5aa9f0"); };
    server->addToState = [this] (juce::DynamicObject& state) { state.setProperty ("layouts", layouts); };
    server->onMessage = [this] (const juce::var& m)
    {
        if (m.getProperty ("type", {}).toString() == "layout")
            setLayout (m.getProperty ("device", {}).toString(), m.getProperty ("keys", {}));
    };
}

void Remote::newKey()
{
    settings.setValue ("remoteKey", makeKey());
    if (server != nullptr)
        start();
    sendChangeMessage();
}

void Remote::setLayout (const juce::String& device, const juce::var& keys)
{
    if (device != "phone" && device != "grid")
        return;
    juce::Array<juce::var> clean;
    if (auto* a = keys.getArray())
        for (int i = 0; i < juce::jmin (a->size(), 24); ++i)
            clean.add (cleanKey (a->getReference (i)));
    // a fresh object, so the server's copy of the old one isn't changed under it
    auto* o = new juce::DynamicObject();
    if (auto* old = layouts.getDynamicObject())
        for (auto& p : old->getProperties())
            o->setProperty (p.name, p.value);
    o->setProperty (device, clean);   // empty = the default layout
    layouts = juce::var (o);
    settings.setValue ("remoteLayouts", juce::JSON::toString (layouts, true));
    if (server != nullptr)
        server->resend();
}

std::vector<Remote::Address> Remote::addresses()
{
    std::vector<Address> found;
#ifdef _WIN32
    ULONG size = 16384;
    juce::HeapBlock<char> buffer;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        buffer.calloc (size);
        const auto r = GetAdaptersAddresses (AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                             nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*> (buffer.get()), &size);
        if (r == ERROR_BUFFER_OVERFLOW)
            continue;
        if (r != NO_ERROR)
            break;
        for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*> (buffer.get()); a != nullptr; a = a->Next)
        {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;
            for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next)
            {
                char text[INET_ADDRSTRLEN] = {};
                auto* in = reinterpret_cast<sockaddr_in*> (u->Address.lpSockaddr);
                if (inet_ntop (AF_INET, &in->sin_addr, text, sizeof (text)) != nullptr)
                    found.push_back ({ juce::String (text), juce::String (a->FriendlyName) });
            }
        }
        break;
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs (&list) == 0)
    {
        for (auto* a = list; a != nullptr; a = a->ifa_next)
        {
            if (a->ifa_addr == nullptr || a->ifa_addr->sa_family != AF_INET || (a->ifa_flags & IFF_LOOPBACK) != 0 || (a->ifa_flags & IFF_UP) == 0)
                continue;
            char text[INET_ADDRSTRLEN] = {};
            if (inet_ntop (AF_INET, &reinterpret_cast<sockaddr_in*> (a->ifa_addr)->sin_addr, text, sizeof (text)) != nullptr)
                found.push_back ({ juce::String (text), juce::String (a->ifa_name) });
        }
        freeifaddrs (list);
    }
#endif
    // self-assigned addresses (no network) are no use to a phone
    found.erase (std::remove_if (found.begin(), found.end(), [] (const Address& a) { return a.ip.startsWith ("169.254.") || a.ip.startsWith ("127."); }),
                 found.end());
    auto rank = [] (const Address& a)
    {
        if (a.ip.startsWith ("192.168.137."))   // Windows Mobile Hotspot
            return 0;
        const auto n = a.adapter.toLowerCase();
        if (n.contains ("wi-fi") || n.contains ("wifi") || n.contains ("wireless") || n.contains ("wlan") || n == "en0")
            return 1;
        if (n.contains ("local area connection*") || n.contains ("bridge"))
            return 2;
        if (n.contains ("dante") || n.contains ("soundgrid") || n.contains ("vethernet") || n.contains ("virtual") || n.contains ("vpn"))
            return 4;
        return 3;
    };
    std::stable_sort (found.begin(), found.end(), [&] (const Address& a, const Address& b) { return rank (a) < rank (b); });
    return found;
}

juce::String Remote::getAddress() const
{
    const auto all = addresses();
    const auto picked = settings.getValue ("remoteAddress");
    for (auto& a : all)
        if (a.ip == picked)
            return picked;
    return all.empty() ? juce::String ("127.0.0.1") : all.front().ip;
}

void Remote::setAddress (const juce::String& ip)
{
    settings.setValue ("remoteAddress", ip);
    sendChangeMessage();
}

juce::String Remote::url() const
{
    return "http://" + getAddress() + ":" + juce::String (port) + "/?k=" + settings.getValue ("remoteKey");
}

// ---------------------------------------------------------------------------------------------

RemotePanel::RemotePanel (Remote& r) : remote (r)
{
    on.onClick = [this] { remote.setOn (on.getToggleState()); };
    addAndMakeVisible (on);

    networkLabel.setColour (juce::Label::textColourId, Look::textDim);
    addAndMakeVisible (networkLabel);
    network.setTooltip ("The network the phones are on (the address in the QR code)");
    network.onChange = [this]
    {
        const int i = network.getSelectedItemIndex();
        if (i >= 0 && i < (int) shownAddresses.size())
            remote.setAddress (shownAddresses[(size_t) i].ip);
    };
    addAndMakeVisible (network);
    rescan.setTooltip ("Look for networks again (after joining a Wi-Fi or turning the hotspot on)");
    rescan.onClick = [this] { fillAddresses(); refresh(); };
    addAndMakeVisible (rescan);

    link.setReadOnly (true);
    link.setFont (Look::mono (13.0f));
    addAndMakeVisible (link);
    copy.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (remote.url()); };
    addAndMakeVisible (copy);
    renew.setTooltip ("Makes a new link and QR code: phones with the old one are disconnected and must scan again");
    renew.onClick = [this]
    {
        juce::Component::SafePointer<RemotePanel> safe (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "New link",
                                            "Phones and tablets with the old link are disconnected and must scan the new QR code.",
                                            "New link", "Cancel", this,
                                            juce::ModalCallbackFunction::create ([safe] (int result)
        {
            if (safe != nullptr && result == 1)
                safe->remote.newKey();
        }));
    };
    addAndMakeVisible (renew);

    status.setFont (Look::font (15.0f, true));
    addAndMakeVisible (status);

    help.setColour (juce::Label::textColourId, Look::textDim);
    help.setJustificationType (juce::Justification::topLeft);
    help.setText (juce::String::fromUTF8 (
        "1. Turn the remote on and scan the QR code with the phone's camera (or type the link into its browser).\n"
        "2. The phone must be on the same network as this computer. No internet is needed.\n"
        "3. The phone shows Stinger's 24 buttons with the bank tabs on top. Edit on the phone changes any key (the size button switches to 12 keys). "
        "Layouts are kept in Stinger, so every phone gets the same ones.\n\n"
        "Venue Wi-Fi often blocks phones from reaching other devices. Then turn on the computer's Mobile Hotspot, "
        "join it from the phone, pick the hotspot network above and scan again.\n"
        "The first time, Windows asks whether Stinger may use the network: allow it (Private, and Public if the venue Wi-Fi is Public).\n"
        "Set the phone's screen timeout longer for a show; the page reconnects by itself after the phone sleeps."),
        juce::dontSendNotification);
    addAndMakeVisible (help);

    hotspot.setTooltip ("Windows settings for the Mobile Hotspot (your own Wi-Fi from this computer)");
    hotspot.onClick = [] { juce::URL ("ms-settings:network-mobilehotspot").launchInDefaultBrowser(); };
#ifdef _WIN32
    addAndMakeVisible (hotspot);
#endif

    remote.addChangeListener (this);
    fillAddresses();
    refresh();
    startTimerHz (2);
    setSize (640, 540);
}

RemotePanel::~RemotePanel()
{
    remote.removeChangeListener (this);
}

void RemotePanel::fillAddresses()
{
    shownAddresses = Remote::addresses();
    network.clear (juce::dontSendNotification);
    for (int i = 0; i < (int) shownAddresses.size(); ++i)
        network.addItem (shownAddresses[(size_t) i].adapter + juce::String::fromUTF8 ("  \xe2\x80\x94  ") + shownAddresses[(size_t) i].ip, i + 1);
    if (shownAddresses.empty())
        network.setTextWhenNothingSelected ("No network: join a Wi-Fi or turn on the hotspot");
}

void RemotePanel::refresh()
{
    on.setToggleState (remote.isOn(), juce::dontSendNotification);
    const auto address = remote.getAddress();
    for (int i = 0; i < (int) shownAddresses.size(); ++i)
        if (shownAddresses[(size_t) i].ip == address)
            network.setSelectedItemIndex (i, juce::dontSendNotification);

    const auto url = remote.url();
    link.setText (url, false);
    link.setCaretPosition (0);
    qr = {};
    try
    {
        const auto code = qrcodegen::QrCode::encodeText (url.toRawUTF8(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int n = code.getSize(), border = 3, scale = 8;
        qr = juce::Image (juce::Image::RGB, (n + border * 2) * scale, (n + border * 2) * scale, true);
        juce::Graphics g (qr);
        g.fillAll (juce::Colours::white);
        g.setColour (juce::Colours::black);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (code.getModule (x, y))
                    g.fillRect ((x + border) * scale, (y + border) * scale, scale, scale);
    }
    catch (...) {}
    timerCallback();
    repaint();
}

void RemotePanel::timerCallback()
{
    juce::String text;
    juce::Colour colour = Look::textDim;
    if (! remote.isOn())
        text = "Remote is off";
    else if (remote.failed())
    {
        text = "Can't start: port " + juce::String (Remote::port) + " is in use (another Stinger?)";
        colour = Look::red;
    }
    else if (const int n = remote.numClients(); n > 0)
    {
        text = juce::String (n) + (n == 1 ? " phone / tablet connected" : " phones / tablets connected");
        colour = Look::green;
    }
    else
    {
        text = "Waiting for a phone: scan the QR code";
        colour = Look::amber;
    }
    status.setText (text, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, colour);
}

void RemotePanel::paint (juce::Graphics& g)
{
    g.fillAll (Look::panel);
    if (qr.isValid())
    {
        g.setOpacity (remote.isOn() ? 1.0f : 0.15f);
        g.drawImage (qr, qrArea.toFloat(), juce::RectanglePlacement::centred);
        g.setOpacity (1.0f);
    }
    if (! remote.isOn())
    {
        g.setColour (Look::textDim);
        g.setFont (Look::font (15.0f, true));
        g.drawFittedText ("Turn the remote on\nto use the QR code", qrArea, juce::Justification::centred, 2);
    }
}

void RemotePanel::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    on.setBounds (r.removeFromTop (26));
    r.removeFromTop (8);
    auto row = r.removeFromTop (28);
    networkLabel.setBounds (row.removeFromLeft (70));
    rescan.setBounds (row.removeFromRight (80));
    row.removeFromRight (6);
    network.setBounds (row);
    r.removeFromTop (12);

    auto top = r.removeFromTop (240);
    qrArea = top.removeFromLeft (240);
    top.removeFromLeft (16);
    status.setBounds (top.removeFromTop (28));
    top.removeFromTop (8);
    link.setBounds (top.removeFromTop (28));
    top.removeFromTop (8);
    auto buttons = top.removeFromTop (28);
    copy.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (8);
    renew.setBounds (buttons.removeFromLeft (110));
    top.removeFromTop (8);
    hotspot.setBounds (top.removeFromTop (28).removeFromLeft (228));

    r.removeFromTop (12);
    help.setBounds (r);
}

RemoteWindow::RemoteWindow (Remote& remote)
    : DocumentWindow ("Wi-Fi Remote", Look::panel, DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new RemotePanel (remote), true);
    setResizable (false, false);
    centreWithSize (getWidth(), getHeight());
}
