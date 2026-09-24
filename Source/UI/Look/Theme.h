#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Direction C, "Signal": black ground, white for what you set, one lime accent
// for what Batida moves (XY, macros, modulators, step locks). Mono type, 1 px
// lines, hard edges. Everything is drawn from here, so a tweak happens once.
namespace theme
{
// Colours
inline const juce::Colour bg { 0xff000000 };
inline const juce::Colour surface { 0xff0a0a0a };
inline const juce::Colour raised { 0xff121212 };
inline const juce::Colour line { 0xff262626 };
inline const juce::Colour ink { 0xfff2f2f2 };      // text and set values
inline const juce::Colour muted { 0xff8c8c8c };
inline const juce::Colour faint { 0xff4f4f4f };
inline const juce::Colour track { 0xff1c1c1c };
inline const juce::Colour trackHover { 0xff2c2c2c };
inline const juce::Colour lime { 0xffd4ff3a };     // moved by Batida
inline const juce::Colour limeSoft = lime.withAlpha (0.30f);
inline const juce::Colour limeBand = lime.withAlpha (0.12f);
inline const juce::Colour grid { 0xff161616 };
inline const juce::Colour cellA { 0xff0b0b0b };
inline const juce::Colour cellB { 0xff141414 };

// Timings (ms)
constexpr double kFast = 80.0, kBase = 120.0, kDecay = 180.0, kSlide = 90.0, kWipe = 100.0;
float easeOut (float t); // cubic ease-out, 0..1

// Whether glides and slides animate: on screen, and macOS Reduce Motion off.
// Otherwise they jump straight to the end (offscreen renders too). Hit
// lights still decay.
bool reduceMotion();
inline bool canAnimate (const juce::Component& c) { return c.getPeer() != nullptr && ! reduceMotion(); }

// Visible with all its parents (on screen or rendered offscreen): hidden pages skip their work.
inline bool isVisibleInTree (const juce::Component& c)
{
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (! p->isVisible())
            return false;
    return true;
}

// Fonts: JetBrains Mono for everything, Martian Mono for headings and names.
enum class Weight { Regular, Medium, Bold };
juce::Font mono (float size, Weight weight = Weight::Regular);
juce::Font head (float size, bool extraBold = false);

// Metrics
constexpr int kRowHeight = 20;   // one value bar
constexpr int kTagHeight = 16;
constexpr int kPad = 8;

// A numbered, inverted section tag ("01 DYNAMICS"); returns its width.
int drawTag (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text,
             juce::Colour fill = ink, juce::Colour textColour = bg);
int tagWidth (const juce::String& text);

// Small uppercase label, as used next to bars.
void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                juce::Colour colour = muted, juce::Justification just = juce::Justification::centredLeft);

// The whole plugin's look for stock JUCE widgets (buttons, menus, text
// fields, tooltips, scrollbars).
class BatidaLook final : public juce::LookAndFeel_V4
{
public:
    BatidaLook();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return mono (10.0f); }
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return mono (10.5f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& name) override;
    juce::Font getPopupMenuFont() override { return mono (10.5f); }
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight, int& w, int& h) override;
    int getPopupMenuBorderSize() override { return 4; }

    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                        int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 8; }

    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;
};

// One shared look for every Batida window (menus opened without a target use
// it too, while an editor is open).
struct SharedLook
{
    BatidaLook look;
};
} // namespace theme
