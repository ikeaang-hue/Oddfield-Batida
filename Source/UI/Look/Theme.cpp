#include "Theme.h"

#include "BinaryData.h"

namespace theme
{
namespace
{
struct Typefaces
{
    juce::Typeface::Ptr regular, medium, bold, headMedium, headBold;

    Typefaces()
    {
        auto load = [] (const char* data, int size) { return juce::Typeface::createSystemTypefaceFor (data, (size_t) size); };
        regular = load (BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize);
        medium = load (BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize);
        bold = load (BinaryData::JetBrainsMonoBold_ttf, BinaryData::JetBrainsMonoBold_ttfSize);
        headMedium = load (BinaryData::MartianMonoStdMd_ttf, BinaryData::MartianMonoStdMd_ttfSize);
        headBold = load (BinaryData::MartianMonoStdxBd_ttf, BinaryData::MartianMonoStdxBd_ttfSize);
    }
};

const Typefaces& typefaces()
{
    static const Typefaces t;
    return t;
}

juce::Font fontFor (juce::Typeface::Ptr tf, float size)
{
    return juce::Font (juce::FontOptions {}.withTypeface (tf).withPointHeight (size));
}
} // namespace

float easeOut (float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

juce::Font mono (float size, Weight weight)
{
    const auto& t = typefaces();
    return fontFor (weight == Weight::Bold ? t.bold : weight == Weight::Medium ? t.medium : t.regular, size);
}

juce::Font head (float size, bool extraBold)
{
    const auto& t = typefaces();
    return fontFor (extraBold ? t.headBold : t.headMedium, size);
}

int tagWidth (const juce::String& text)
{
    return (int) std::ceil (juce::GlyphArrangement::getStringWidth (head (8.5f), text.toUpperCase())) + 10;
}

int drawTag (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, juce::Colour fill, juce::Colour textColour)
{
    const auto w = tagWidth (text);
    const auto r = area.withWidth (w).withSizeKeepingCentre (w, kTagHeight);
    g.setColour (fill);
    g.fillRect (r);
    g.setColour (textColour);
    g.setFont (head (8.5f));
    g.drawText (text.toUpperCase(), r.withTrimmedLeft (5), juce::Justification::centredLeft, false);
    return w;
}

void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Colour colour, juce::Justification just)
{
    g.setColour (colour);
    g.setFont (mono (9.5f));
    g.drawText (text.toUpperCase(), area, just, true);
}

// BatidaLook ----------------------------------------------------------------------

BatidaLook::BatidaLook()
{
    using namespace juce;
    setColour (ResizableWindow::backgroundColourId, bg);
    setColour (TextButton::buttonColourId, bg);
    setColour (TextButton::buttonOnColourId, ink);
    setColour (TextButton::textColourOffId, ink);
    setColour (TextButton::textColourOnId, bg);
    setColour (ToggleButton::textColourId, muted);
    setColour (ComboBox::backgroundColourId, bg);
    setColour (ComboBox::textColourId, ink);
    setColour (ComboBox::outlineColourId, line);
    setColour (ComboBox::arrowColourId, muted);
    setColour (PopupMenu::backgroundColourId, bg);
    setColour (PopupMenu::textColourId, ink);
    setColour (PopupMenu::highlightedBackgroundColourId, ink);
    setColour (PopupMenu::highlightedTextColourId, bg);
    setColour (PopupMenu::headerTextColourId, muted);
    setColour (TextEditor::backgroundColourId, bg);
    setColour (TextEditor::textColourId, ink);
    setColour (TextEditor::highlightColourId, ink.withAlpha (0.25f));
    setColour (TextEditor::highlightedTextColourId, ink);
    setColour (TextEditor::outlineColourId, line);
    setColour (TextEditor::focusedOutlineColourId, ink);
    setColour (CaretComponent::caretColourId, ink);
    setColour (Label::textColourId, ink);
    setColour (ListBox::backgroundColourId, bg);
    setColour (ListBox::outlineColourId, line);
    setColour (ScrollBar::thumbColourId, faint);
    setColour (TooltipWindow::backgroundColourId, bg);
    setColour (TooltipWindow::textColourId, ink);
    setColour (TooltipWindow::outlineColourId, ink);
    setColour (AlertWindow::backgroundColourId, bg);
    setColour (AlertWindow::textColourId, ink);
    setColour (AlertWindow::outlineColourId, ink);
    setColour (DirectoryContentsDisplayComponent::highlightColourId, ink);
    setColour (FileBrowserComponent::currentPathBoxBackgroundColourId, bg);

    auto scheme = getDarkColourScheme();
    scheme.setUIColour (ColourScheme::windowBackground, bg);
    scheme.setUIColour (ColourScheme::widgetBackground, bg);
    scheme.setUIColour (ColourScheme::menuBackground, bg);
    scheme.setUIColour (ColourScheme::outline, line);
    scheme.setUIColour (ColourScheme::defaultText, ink);
    scheme.setUIColour (ColourScheme::defaultFill, raised);
    scheme.setUIColour (ColourScheme::highlightedText, bg);
    scheme.setUIColour (ColourScheme::highlightedFill, ink);
    scheme.setUIColour (ColourScheme::menuText, ink);
    setColourScheme (scheme);

    // After the scheme (which resets them): selected text is inverted, white
    // block and black letters, like every other selection.
    setColour (TextEditor::backgroundColourId, bg);
    setColour (TextEditor::textColourId, ink);
    setColour (TextEditor::highlightColourId, ink);
    setColour (TextEditor::highlightedTextColourId, bg);
    setColour (TextEditor::outlineColourId, line);
    setColour (TextEditor::focusedOutlineColourId, ink);
    setColour (CaretComponent::caretColourId, ink);
}

juce::Typeface::Ptr BatidaLook::getTypefaceForFont (const juce::Font& f)
{
    // Every font without a typeface of its own is JetBrains Mono.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() || f.getTypefaceName() == juce::Font::getDefaultMonospacedFontName())
        return f.isBold() ? typefaces().bold : typefaces().regular;
    return LookAndFeel_V4::getTypefaceForFont (f);
}

void BatidaLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto on = b.getToggleState() || down;
    const auto fill = b.findColour (juce::TextButton::buttonColourId);
    if (on)
    {
        g.setColour (b.findColour (juce::TextButton::buttonOnColourId));
        g.fillRect (r);
    }
    else if (fill != bg && ! fill.isTransparent())
    {
        // A filled button (SAVE): its own colour.
        g.setColour (b.isEnabled() ? fill : faint);
        g.fillRect (r);
        return;
    }
    g.setColour (on ? b.findColour (juce::TextButton::buttonOnColourId) : (over && b.isEnabled() ? ink : line));
    g.drawRect (r, 1.0f);
}

void BatidaLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const auto on = b.getToggleState() || down;
    auto colour = b.findColour (on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    if (! b.isEnabled())
        colour = faint;
    g.setColour (colour);
    g.setFont (on ? mono (10.0f, Weight::Bold) : mono (10.0f));
    g.drawText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, true);
}

void BatidaLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    // A chip: [TEXT] off; lime with black text on.
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto on = b.getToggleState();
    if (on)
    {
        g.setColour (lime);
        g.fillRect (r);
    }
    g.setColour (on ? lime : (over ? ink : faint));
    g.drawRect (r, 1.0f);
    g.setColour (on ? bg : (b.isEnabled() ? muted : faint));
    g.setFont (mono (9.5f, Weight::Bold));
    const auto text = b.getButtonText().toUpperCase();
    g.drawText (on ? text : "[" + text + "]", b.getLocalBounds(), juce::Justification::centred, false);
}

void BatidaLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (bg);
    g.fillRect (r);
    g.setColour (box.isMouseOver (true) ? ink : line);
    g.drawRect (r, 1.0f);
    g.setColour (muted);
    g.setFont (mono (9.5f));
    g.drawText (juce::String::fromUTF8 ("\xe2\x96\xbe"), juce::Rectangle<int> (w - 16, 0, 12, h), juce::Justification::centred);
}

void BatidaLook::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 0, box.getWidth() - 22, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, ink);
    label.setJustificationType (juce::Justification::centredLeft);
}

void BatidaLook::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (bg);
    g.setColour (ink);
    g.drawRect (0, 0, w, h, 1);
}

void BatidaLook::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                    bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                    const juce::String& shortcutKeyText, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (line);
        g.fillRect (area.withSizeKeepingCentre (area.getWidth(), 1));
        return;
    }
    const auto hot = isHighlighted && isActive;
    if (hot)
    {
        g.setColour (ink);
        g.fillRect (area);
    }
    const auto fg = hot ? bg : (isActive ? ink : faint);
    auto r = area.reduced (10, 0);
    g.setColour (fg);
    if (isTicked)
        g.fillRect (juce::Rectangle<int> (r.getX(), r.getCentreY() - 3, 6, 6));
    r.removeFromLeft (16);
    if (hasSubMenu)
    {
        g.setFont (mono (11.0f));
        g.drawText (juce::String::fromUTF8 ("\xe2\x80\xba"), r.removeFromRight (10), juce::Justification::centredRight);
    }
    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (hot ? bg : muted);
        g.setFont (mono (9.5f));
        g.drawText (shortcutKeyText, r.removeFromRight (100), juce::Justification::centredRight);
    }
    g.setColour (fg);
    g.setFont (getPopupMenuFont());
    g.drawText (text, r, juce::Justification::centredLeft, true);
}

void BatidaLook::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    g.setColour (muted);
    g.setFont (head (8.5f));
    g.drawText (name.toUpperCase(), area.reduced (10, 0), juce::Justification::bottomLeft, true);
}

void BatidaLook::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& w, int& h)
{
    if (isSeparator)
    {
        w = 50;
        h = 7;
        return;
    }
    w = (int) juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), text) + 56;
    h = 22;
}

void BatidaLook::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& ed)
{
    g.setColour (ed.findColour (juce::TextEditor::backgroundColourId));
    g.fillRect (0, 0, w, h);
}

void BatidaLook::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& ed)
{
    if (! ed.isEnabled())
        return;
    g.setColour (ed.hasKeyboardFocus (true) && ! ed.isReadOnly() ? ink : line);
    g.drawRect (0, 0, w, h, 1);
}

void BatidaLook::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                                int thumbSize, bool over, bool down)
{
    const auto thumb = vertical ? juce::Rectangle<int> (x + w - 4, thumbStart, 3, thumbSize)
                                : juce::Rectangle<int> (thumbStart, y + h - 4, thumbSize, 3);
    g.setColour (over || down ? muted : faint);
    g.fillRect (thumb);
}

juce::Rectangle<int> BatidaLook::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const auto w = std::min (360, (int) juce::GlyphArrangement::getStringWidth (mono (10.0f), text) + 16);
    const auto lines = 1 + (int) juce::GlyphArrangement::getStringWidth (mono (10.0f), text) / 344;
    const auto h = 10 + lines * 14;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 24,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h)
        .constrainedWithin (parent);
}

void BatidaLook::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (bg);
    g.setColour (ink);
    g.drawRect (0, 0, w, h, 1);
    g.setFont (mono (10.0f));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (8, 5), juce::Justification::centredLeft, 4);
}

void BatidaLook::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (label.isBeingEdited())
        return;
    if (const auto back = label.findColour (juce::Label::backgroundColourId); ! back.isTransparent())
        g.fillAll (back);
    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.5f));
    g.setFont (getLabelFont (label));
    g.drawFittedText (label.getText(), label.getBorderSize().subtractedFrom (label.getLocalBounds()), label.getJustificationType(),
                      std::max (1, (int) ((float) label.getHeight() / 13.0f)), label.getMinimumHorizontalScale());
}
} // namespace theme
