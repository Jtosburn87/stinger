#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "StreamDeckServer.h"

/** The Wi-Fi remote: phones and tablets on the same network scan a QR code and play Stinger from a web page.
    Stinger serves the page itself (no internet, nothing to install) on `port` of every network adapter;
    every request needs the random key in the link. The key layouts (24 = Stinger's buttons by default, or 12) are
    edited on the phone and kept in Stinger's settings, so every phone gets the same ones. */
class Remote : public juce::ChangeBroadcaster
{
public:
    static constexpr int port = 52818;

    Remote (ShowController& controller, juce::PropertiesFile& settings);
    ~Remote() override;

    /** Set these before turning it on (same as the Stream Deck server's). */
    std::function<void (const juce::String& action)> perform;
    std::function<int()> shownBank;
    std::function<juce::String (const Cue& cue)> colourOf;

    /** Turns it on if it was on when Stinger last closed. */
    void restore();
    bool isOn() const { return server != nullptr; }
    void setOn (bool on);
    /** A new key: old links stop working and connected phones are dropped. */
    void newKey();

    struct Address
    {
        juce::String ip, adapter;
    };
    /** This computer's IPv4 addresses, the likeliest one for phones first (hotspot, then Wi-Fi). */
    static std::vector<Address> addresses();
    /** The address in the QR code: the one picked, else the likeliest. */
    juce::String getAddress() const;
    void setAddress (const juce::String& ip);
    juce::String url() const;

    int numClients() const { return server != nullptr ? server->numClients() : 0; }
    bool failed() const { return server != nullptr && server->failed(); }

    /** Keeps a layout from the phone: device "grid" (24) / "phone" (12), keys = [{kind, mode, position, bank, playlist}]. */
    void setLayout (const juce::String& device, const juce::var& keys);
    juce::var getLayouts() const { return layouts; }

private:
    void start();

    ShowController& controller;
    juce::PropertiesFile& settings;
    std::unique_ptr<StreamDeckServer> server;
    juce::var layouts;
};

/** The Remote window: on/off, the network for the QR code, the QR code and link, connected phones. */
class RemotePanel : public juce::Component,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit RemotePanel (Remote& remote);
    ~RemotePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void refresh();
    void fillAddresses();

    Remote& remote;
    juce::ToggleButton on { "Wi-Fi remote on: phones and tablets on this network can play Stinger" };
    juce::Label networkLabel { {}, "Network" }, status, help;
    juce::ComboBox network;
    juce::TextButton rescan { "Rescan" }, copy { "Copy link" }, renew { "New link" }, hotspot { "Mobile hotspot settings..." };
    juce::TextEditor link;
    juce::Image qr;
    juce::Rectangle<int> qrArea;
    std::vector<Remote::Address> shownAddresses;
};

class RemoteWindow : public juce::DocumentWindow
{
public:
    explicit RemoteWindow (Remote& remote);
    void closeButtonPressed() override { setVisible (false); }
};
