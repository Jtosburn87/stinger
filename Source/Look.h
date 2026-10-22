#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Show.h"

/** Colours and fonts (the approved mockup's dark theme). */
namespace Look
{
    const juce::Colour ground     { 0xff0e1013 };
    const juce::Colour panel      { 0xff15181c };
    const juce::Colour sidePanel  { 0xff121418 };
    const juce::Colour raised     { 0xff1e2228 };
    const juce::Colour line       { 0xff262b32 };
    const juce::Colour edge       { 0xff2c323a };
    const juce::Colour text       { 0xffe8eaed };
    const juce::Colour textDim    { 0xffa7b0bb };
    const juce::Colour textFaint  { 0xff6e7885 };
    const juce::Colour amber      { 0xfff2a33a };
    const juce::Colour amberDark  { 0xff2a2418 };
    const juce::Colour blue       { 0xff5aa9f0 };
    const juce::Colour green      { 0xff4cc38a };
    const juce::Colour goGreen    { 0xff2f8f5b };
    const juce::Colour red        { 0xffc93a3f };
    const juce::Colour violet     { 0xffb48cf0 };

    /** Button colours (Cue::colour indexes this; the first 8 are the original ones, so old shows keep theirs). */
    constexpr int numCueColours = 16;
    juce::Colour cueColour (int index);
    juce::String cueColourName (int index);
    /** The palette in the order menus show it (by hue). */
    const std::vector<int>& paletteOrder();
    /** A button's colour as shown: black once Self Destruct has spent it, else its own colour. */
    juce::Colour colourOf (const Cue& cue);
    /** A button's own colour: its picked colour, else its palette colour. */
    juce::Colour baseColourOf (const Cue& cue);
    /** A dark shade of a button colour for backgrounds (text stays readable on it). */
    juce::Colour shade (juce::Colour c, float amount = 0.32f);
    /** Black or white, whichever reads better on c. */
    juce::Colour textOn (juce::Colour c);
    /** The colour menu (palette + "Custom colour..."); calls changed() after setting the cue's colour. */
    void showColourMenu (juce::Component& target, ShowController& controller, int cueId);
    /** The custom colour picker, next to screenArea. */
    void showColourPicker (juce::Rectangle<int> screenArea, ShowController& controller, int cueId);

    juce::Font font (float height, bool bold = false);
    juce::Font condensed (float height);   // headings
    juce::Font mono (float height, bool bold = false);
    float textWidth (const juce::Font& f, const juce::String& text);

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        juce::Font getTextButtonFont (juce::TextButton& b, int buttonHeight) override;   // property "big": large heading font
        juce::Font getLabelFont (juce::Label&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool highlighted, bool down) override;
    };
}
