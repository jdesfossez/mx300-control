#pragma once

#include "ParamModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
const juce::Colour background { 0xff121417 };
const juce::Colour panel { 0xff1b1e23 };
const juce::Colour panelRaised { 0xff23272e };
const juce::Colour outline { 0xff2e333b };
const juce::Colour text { 0xffe6e8eb };
const juce::Colour textDim { 0xff8a919b };
const juce::Colour textFaint { 0xff5a616b };
const juce::Colour reverb { 0xff3dd6c4 };
const juce::Colour delay { 0xfff2a33a };
const juce::Colour modulation { 0xffa78bfa };
const juce::Colour good { 0xff4ade80 };
const juce::Colour warn { 0xfffbbf24 };
const juce::Colour bad { 0xfff87171 };

juce::Colour forClass (mx300::Klass);
juce::Font font (float size, bool bold = false);
juce::Font mono (float size);
} // namespace theme

class MX300LookAndFeel : public juce::LookAndFeel_V4
{
public:
    MX300LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::font (14.0f, true); }
    juce::Font getPopupMenuFont() override { return theme::font (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return theme::font (13.0f, true); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
};
