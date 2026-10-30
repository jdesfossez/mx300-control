#include "Theme.h"

namespace theme
{
juce::Colour forClass (mx300::Klass k)
{
    switch (k)
    {
        case mx300::Klass::Reverb: return reverb;
        case mx300::Klass::Delay: return delay;
        case mx300::Klass::Modulation: return modulation;
    }
    return reverb;
}

juce::Font font (float size, bool bold)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font mono (float size)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
}
} // namespace theme

MX300LookAndFeel::MX300LookAndFeel()
{
    using namespace theme;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::backgroundColourId, background);
    setColour (juce::TextButton::buttonColourId, panelRaised);
    setColour (juce::TextButton::buttonOnColourId, reverb.withAlpha (0.25f));
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, text);
    setColour (juce::ComboBox::backgroundColourId, panelRaised);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panelRaised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, reverb.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ListBox::outlineColourId, outline);
    setColour (juce::TextEditor::backgroundColourId, panelRaised);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::focusedOutlineColourId, reverb);
    setColour (juce::TextEditor::highlightColourId, reverb.withAlpha (0.3f));
    setColour (juce::CaretComponent::caretColourId, reverb);
    setColour (juce::Label::textColourId, text);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, outline);
    setColour (juce::ScrollBar::thumbColourId, outline);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::TooltipWindow::backgroundColourId, panelRaised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, outline);
}

void MX300LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float track = juce::jmax (3.0f, radius * 0.12f);
    const float angle = start + pos * (end - start);
    const bool enabled = s.isEnabled();

    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0, start, end, true);
    g.setColour (theme::outline);
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (enabled)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0, start, angle, true);
        g.setColour (accent);
        g.strokePath (arc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // knob body
    const float body = radius - track * 2.4f;
    g.setGradientFill (juce::ColourGradient (theme::panelRaised.brighter (0.15f), centre.x, centre.y - body,
                                             theme::panelRaised.darker (0.3f), centre.x, centre.y + body, false));
    g.fillEllipse (centre.x - body, centre.y - body, body * 2, body * 2);
    g.setColour (theme::outline);
    g.drawEllipse (centre.x - body, centre.y - body, body * 2, body * 2, 1.0f);

    // pointer
    juce::Path p;
    p.addRoundedRectangle (-1.5f, -body + 3.0f, 3.0f, body * 0.45f, 1.5f);
    g.setColour (enabled ? theme::text : theme::textFaint);
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (centre));
}

void MX300LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : base;
    if (down) c = c.brighter (0.15f);
    else if (over) c = c.brighter (0.07f);
    if (! b.isEnabled()) c = c.withMultipliedAlpha (0.5f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId).withAlpha (1.0f) : theme::outline);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void MX300LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (juce::TextButton::textColourOffId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1);
}

void MX300LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 18.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.5f, ay - 2.5f, ax + 4.5f, ay - 2.5f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.fillPath (arrow);
}

void MX300LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}
