#include "Look.h"
#include <juce_gui_extra/juce_gui_extra.h>   // ColourSelector

#if JUCE_MAC
 static constexpr bool isMac = true;
#else
 static constexpr bool isMac = false;
#endif

namespace Look
{
    juce::Colour cueColour (int index)
    {
        static const juce::uint32 colours[numCueColours] = {
            0xff5aa9f0, 0xfff2a33a, 0xff4cc38a, 0xffb48cf0, 0xffe5484d, 0xffe8eaed, 0xff3fc6c6, 0xffe879b9,
            0xffff7a2e, 0xfff5d63d, 0xffa3d93b, 0xff38d0f0, 0xff6f6cf6, 0xffd65bf0, 0xffa8794f, 0xff8c96a3 };
        return juce::Colour (colours[(size_t) juce::jlimit (0, numCueColours - 1, index)]);
    }

    juce::String cueColourName (int index)
    {
        static const char* names[numCueColours] = { "Blue", "Amber", "Green", "Violet", "Red", "White", "Teal", "Pink",
                                                    "Orange", "Yellow", "Lime", "Cyan", "Indigo", "Magenta", "Brown", "Grey" };
        return names[(size_t) juce::jlimit (0, numCueColours - 1, index)];
    }

    const std::vector<int>& paletteOrder()
    {
        // red -> orange -> ... -> pink, then the neutrals
        static const std::vector<int> order { 4, 8, 1, 9, 10, 2, 6, 11, 0, 12, 3, 13, 7, 14, 15, 5 };
        return order;
    }

    juce::Colour colourOf (const Cue& cue)
    {
        return cue.customColour != 0 ? juce::Colour (cue.customColour).withAlpha (1.0f) : cueColour (cue.colour);
    }

    juce::Colour shade (juce::Colour c, float amount)
    {
        return ground.interpolatedWith (c, amount);
    }

    juce::Colour textOn (juce::Colour c)
    {
        return c.getPerceivedBrightness() > 0.62f ? juce::Colour (0xff0e1013) : juce::Colours::white;
    }

    namespace
    {
        /** "Custom colour...": a colour picker that sets the button's colour as it moves. */
        class ColourPicker : public juce::ColourSelector, private juce::ChangeListener
        {
        public:
            ColourPicker (ShowController& c, int id)
                : juce::ColourSelector (juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders), controller (c), cueId (id)
            {
                setCurrentColour (colourOf (controller.getShow().cue (cueId)));
                setColour (juce::ColourSelector::backgroundColourId, panel);
                addChangeListener (this);
                setSize (300, 320);
            }

        private:
            void changeListenerCallback (juce::ChangeBroadcaster*) override
            {
                if (! controller.getShow().isValid (cueId))
                    return;
                controller.getShow().cue (cueId).customColour = getCurrentColour().withAlpha (1.0f).getARGB();
                controller.cueChanged (cueId);
            }

            ShowController& controller;
            int cueId;
        };
    }

    void showColourMenu (juce::Component& target, ShowController& controller, int cueId)
    {
        if (! controller.getShow().isValid (cueId))
            return;
        const auto& cue = controller.getShow().cue (cueId);
        juce::PopupMenu m;
        for (int i : paletteOrder())
            m.addColouredItem (100 + i, cueColourName (i), cueColour (i), true, cue.customColour == 0 && cue.colour == i);
        m.addSeparator();
        m.addItem (1, "Custom colour...", true, cue.customColour != 0);
        juce::Component::SafePointer<juce::Component> safe (&target);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [safe, &controller, cueId] (int result)
        {
            if (safe == nullptr || result == 0 || ! controller.getShow().isValid (cueId))
                return;
            auto& c = controller.getShow().cue (cueId);
            if (result >= 100)
            {
                c.colour = result - 100;
                c.customColour = 0;
                controller.cueChanged (cueId);
            }
            else if (result == 1)
                showColourPicker (safe->getScreenBounds(), controller, cueId);
        });
    }

    void showColourPicker (juce::Rectangle<int> screenArea, ShowController& controller, int cueId)
    {
        if (controller.getShow().isValid (cueId))
            juce::CallOutBox::launchAsynchronously (std::make_unique<ColourPicker> (controller, cueId), screenArea, nullptr);
    }

    juce::Font font (float height, bool bold)
    {
        return juce::Font (juce::FontOptions (isMac ? "Helvetica Neue" : "Segoe UI", height, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font condensed (float height)
    {
        return juce::Font (juce::FontOptions (isMac ? "Avenir Next Condensed" : "Bahnschrift", height, juce::Font::bold)).withExtraKerningFactor (0.08f);
    }

    float textWidth (const juce::Font& f, const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidth (f, s);
    }

    juce::Font mono (float height, bool bold)
    {
        return juce::Font (juce::FontOptions (isMac ? "Menlo" : "Consolas", height, bold ? juce::Font::bold : juce::Font::plain));
    }

    LookAndFeel::LookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, ground);
        setColour (juce::TextButton::buttonColourId, raised);
        setColour (juce::TextButton::buttonOnColourId, amberDark);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colour (0xfff5c77e));
        setColour (juce::ComboBox::backgroundColourId, raised);
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a414b));
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, textDim);
        setColour (juce::PopupMenu::backgroundColourId, raised);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2f3a48));
        setColour (juce::PopupMenu::highlightedTextColourId, text);
        setColour (juce::Label::textColourId, text);
        setColour (juce::Slider::thumbColourId, amber);
        setColour (juce::Slider::trackColourId, amber.withAlpha (0.6f));
        setColour (juce::Slider::backgroundColourId, edge);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxBackgroundColourId, raised);
        setColour (juce::Slider::textBoxOutlineColourId, edge);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, amber);
        setColour (juce::ToggleButton::tickDisabledColourId, textDim);
        setColour (juce::TextEditor::backgroundColourId, raised);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::outlineColourId, edge);
        setColour (juce::TextEditor::focusedOutlineColourId, blue);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::ListBox::backgroundColourId, raised);
        setColour (juce::ScrollBar::thumbColourId, edge);
        setColour (juce::DocumentWindow::backgroundColourId, ground);
    }

    juce::Font LookAndFeel::getTextButtonFont (juce::TextButton& b, int buttonHeight)
    {
        if (b.getProperties().contains ("big"))
            return condensed (juce::jmin (26.0f, (float) buttonHeight * 0.5f));
        return font (juce::jmin (15.0f, (float) buttonHeight * 0.5f), true);
    }

    juce::Font LookAndFeel::getLabelFont (juce::Label& l)
    {
        return l.getFont().getTypefaceName() == juce::Font::getDefaultSansSerifFontName() ? font (14.0f) : l.getFont();
    }

    juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (14.0f); }
    juce::Font LookAndFeel::getPopupMenuFont() { return font (15.0f); }

    void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background,
                                            bool highlighted, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : background;
        if (down)
            fill = fill.brighter (0.15f);
        else if (highlighted)
            fill = fill.brighter (0.07f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (b.getToggleState() ? amber : edge.brighter (0.15f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }
}
