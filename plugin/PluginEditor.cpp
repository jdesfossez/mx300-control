#include "PluginEditor.h"

namespace
{
constexpr int kHeader = 60, kFooter = 28, kSidebar = 260, kRouting = 84;
const juce::String kNameChars = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_abcdefghijklmnopqrstuvwxyz{|}~";

juce::String slotLabel (uint8_t bank, uint8_t slot)
{
    return (bank == mx300::kBankFactory ? "F" : "U") + juce::String (slot + 1).paddedLeft ('0', 2);
}

juce::String stateText (mx300::DeviceSession::State s, DeviceHub::Source src)
{
    using S = mx300::DeviceSession::State;
    if (src == DeviceHub::Source::None)
        return "No MX300 found";
    switch (s)
    {
        case S::Offline: return "Offline";
        case S::Connecting: return "Connecting...";
        case S::Ready: return src == DeviceHub::Source::Demo ? "Demo unit" : "Connected";
        case S::UpdateMode: return "Unit in OS-update mode";
        case S::NoResponse: return "Unit not responding";
    }
    return {};
}

void drawSignalLine (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, float w = 1.6f)
{
    g.drawLine ({ a, b }, w);
}
} // namespace

// ============================================================================ ParamKnob

ParamKnob::ParamKnob (MX300Editor& e, uint16_t id, uint8_t p) : editor (e), paramId (id), pos (p)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setMouseDragSensitivity (220);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    if (auto* info = mx300::findParam (id))
    {
        slider.setRange (info->min, juce::jmax (info->min + 1, info->max), 1);
        slider.setTooltip (juce::String (info->name) + "  (drag, scroll, or use the arrow keys; raw " + juce::String (info->min)
                           + "-" + juce::String (info->max) + ")");
    }
    slider.onDragStart = [this] { editor.owner.beginGesture (paramId, pos); };
    slider.onDragEnd = [this] { editor.owner.endGesture (paramId, pos); };
    slider.onValueChange = [this]
    {
        if (! updating)
            editor.session().setParam (paramId, pos, (int) slider.getValue());
        repaint();
    };
    addAndMakeVisible (slider);
    refresh();
}

void ParamKnob::setAccent (juce::Colour c)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    repaint();
}

void ParamKnob::refresh()
{
    const juce::ScopedValueSetter<bool> s (updating, true);
    if (auto v = editor.session().value (paramId, pos))
        slider.setValue (*v, juce::dontSendNotification);
    slider.setEnabled (editor.connected() && ! followsHostTempo());
    repaint();
}

bool ParamKnob::followsHostTempo() const
{
    auto* info = mx300::findParam (paramId);
    return info != nullptr && info->display.tempo && editor.owner.tempoSync();
}

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (20);
    r.removeFromBottom (22);
    slider.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
}

void ParamKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.5f, true));
    g.drawFittedText (juce::String (mx300::paramName (paramId)).toUpperCase() + (followsHostTempo() ? " (DAW)" : ""),
                      r.removeFromTop (20), juce::Justification::centred, 1);

    auto engine = editor.session().engine (pos);
    const auto text = editor.session().value (paramId, pos)
                          ? juce::String (mx300::displayValue (paramId, (int) slider.getValue(), &engine))
                          : juce::String ("-");
    g.setColour (theme::text);
    g.setFont (theme::mono (14.0f));
    g.drawFittedText (text, r.removeFromBottom (22), juce::Justification::centred, 1);

    if (badge.isNotEmpty())
    {
        auto b = juce::Rectangle<float> (18.0f, 16.0f).withPosition ((float) getWidth() - 22.0f, 22.0f);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId).withAlpha (0.9f));
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (theme::background);
        g.setFont (theme::font (11.5f, true));
        g.drawText (badge, b, juce::Justification::centred);
    }
}

// ============================================================================ EnginePanel

EnginePanel::EnginePanel (MX300Editor& e, uint8_t p) : editor (e), pos (p)
{
    type.setTooltip ("Effect type. Changing it loads the type's default settings on the unit.");
    type.onChange = [this]
    {
        const int i = type.getSelectedId() - 1;
        if (i >= 0 && i < (int) typeIds.size() && editor.session().engine (pos).type != typeIds[(size_t) i])
            editor.session().setType (pos, typeIds[(size_t) i]);
    };
    addAndMakeVisible (type);

    bypass.setClickingTogglesState (false);
    bypass.setTooltip ("Bypass this processor");
    bypass.setColour (juce::TextButton::buttonOnColourId, theme::bad.withAlpha (0.35f));
    bypass.onClick = [this]
    {
        const int now = editor.session().value (mx300::kParamBypass, pos).value_or (0);
        editor.owner.beginGesture (mx300::kParamBypass, pos);
        editor.session().setParam (mx300::kParamBypass, pos, now ? 0 : 1);
        editor.owner.endGesture (mx300::kParamBypass, pos);
        refreshValues();
    };
    addAndMakeVisible (bypass);
    rebuild();
}

juce::Colour EnginePanel::accent() const
{
    auto* t = mx300::findType (editor.session().engine (pos).type);
    return t ? theme::forClass (t->klass) : theme::textDim;
}

void EnginePanel::rebuild()
{
    auto engine = editor.session().engine (pos);
    type.clear (juce::dontSendNotification);
    typeIds.clear();
    std::optional<mx300::Klass> section;
    for (auto* t : mx300::typesFor (engine.routing))
    {
        if (! section || *section != t->klass)
        {
            section = t->klass;
            type.addSectionHeading (t->klass == mx300::Klass::Reverb ? "Reverb" : t->klass == mx300::Klass::Delay ? "Delay" : "Modulation & Dynamics");
        }
        typeIds.push_back (t->id);
        type.addItem (t->name, (int) typeIds.size());
    }
    for (size_t i = 0; i < typeIds.size(); ++i)
        if (typeIds[i] == engine.type)
            type.setSelectedId ((int) i + 1, juce::dontSendNotification);

    knobs.clear();
    if (auto* t = mx300::findType (engine.type))
        for (auto pid : t->params)
        {
            knobs.push_back (std::make_unique<ParamKnob> (editor, pid, pos));
            addAndMakeVisible (*knobs.back());
        }
    mix = std::make_unique<ParamKnob> (editor, mx300::kParamMix, pos);
    addAndMakeVisible (*mix);
    refreshValues();
    resized();
}

void EnginePanel::refreshValues()
{
    const auto c = accent();
    const bool bypassed = editor.session().value (mx300::kParamBypass, pos).value_or (0) != 0;
    bypass.setToggleState (bypassed, juce::dontSendNotification);
    bypass.setButtonText (bypassed ? "Bypassed" : "Bypass");
    for (auto& k : knobs)
    {
        k->setAccent (bypassed ? theme::textFaint : c);
        k->setBadge (editor.knobBadge (k->id(), pos));
        k->refresh();
    }
    if (mix)
    {
        mix->setAccent (bypassed ? theme::textFaint : c);
        mix->refresh();
    }
    type.setEnabled (editor.connected());
    bypass.setEnabled (editor.connected());
    repaint();
}

void EnginePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (r, 10.0f, 1.0f);
    g.setColour (accent());
    g.fillRoundedRectangle (r.removeFromTop (3.0f).reduced (10.0f, 0.0f), 1.5f);

    g.setColour (theme::textDim);
    g.setFont (theme::font (12.0f, true));
    const bool whole = mx300::isWholeRouting (editor.session().routing());
    g.drawText (whole ? "STEREO PROCESSOR" : "PROCESSOR " + juce::String (pos == mx300::kPosEngine1 ? 1 : 2),
                juce::Rectangle<int> (16, 12, 200, 16), juce::Justification::centredLeft);

    // divider before Mix
    if (mix)
    {
        g.setColour (theme::outline);
        const float x = (float) mix->getX() - 8.0f;
        g.drawLine (x, (float) mix->getY() + 10.0f, x, (float) mix->getBottom() - 10.0f, 1.0f);
    }
}

void EnginePanel::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (20);
    auto head = r.removeFromTop (34);
    type.setBounds (head.removeFromLeft (juce::jmin (240, head.getWidth() - 110)));
    head.removeFromLeft (8);
    bypass.setBounds (head.removeFromLeft (96));
    r.removeFromTop (8);

    auto mixColumn = r.removeFromRight (juce::jmin (130, r.getWidth() / 4));
    r.removeFromRight (16);
    const int n = juce::jmax (1, (int) knobs.size());
    const int rows = n > 6 ? 2 : 1;
    const int cols = (n + rows - 1) / rows;
    const int cw = r.getWidth() / cols;
    constexpr int labels = 46;   // name above + value below the knob
    const int knob = juce::jlimit (60, 150, juce::jmin (cw - 16, r.getHeight() / rows - labels - 30));
    const int rowGap = rows > 1 ? 18 : 0;
    const int ch = knob + labels + rowGap;
    // centre the block, a little above the middle
    const int top = r.getY() + juce::jmax (0, (r.getHeight() - rows * ch) / 3);
    for (int i = 0; i < (int) knobs.size(); ++i)
        knobs[(size_t) i]->setBounds (r.getX() + (i % cols) * cw, top + (i / cols) * ch, cw, ch - rowGap);
    if (mix)
        mix->setBounds (mixColumn.getX(), top + (rows - 1) * ch / 2, mixColumn.getWidth(), ch - rowGap);
}

// ============================================================================ RoutingBar

RoutingBar::RoutingBar (MX300Editor& e) : editor (e) {}

juce::Rectangle<float> RoutingBar::cell (int i) const
{
    const auto n = (float) mx300::routings().size();
    const float w = (float) getWidth() / n;
    return juce::Rectangle<float> (w * (float) i, 0, w, (float) getHeight()).reduced (4.0f, 6.0f);
}

void RoutingBar::drawDiagram (juce::Graphics& g, juce::Rectangle<float> r, uint16_t routing, juce::Colour c)
{
    // tiny signal-flow pictures: inputs on the left, outputs on the right, boxes = processors
    g.setColour (c);
    const float h = r.getHeight(), cx = r.getCentreX(), top = r.getY() + h * 0.28f, bot = r.getY() + h * 0.72f;
    const float l = r.getX(), rr = r.getRight();
    auto box = [&] (float x, float y) { auto b = juce::Rectangle<float> (14.0f, 10.0f).withCentre ({ x, y }); g.fillRoundedRectangle (b, 2.0f); return b; };
    switch (routing)
    {
        case 0x0600:   // Dual Mono: L -> P1 -> L, R -> P2 -> R
            drawSignalLine (g, { l, top }, { rr, top });
            drawSignalLine (g, { l, bot }, { rr, bot });
            box (cx, top); box (cx, bot);
            break;
        case 0x0601:   // Cascade: P1 -> P2
            drawSignalLine (g, { l, (top + bot) / 2 }, { rr, (top + bot) / 2 });
            box (cx - 11, (top + bot) / 2); box (cx + 11, (top + bot) / 2);
            break;
        case 0x0602:   // Parallel: split -> P1 / P2 -> sum
            drawSignalLine (g, { l, (top + bot) / 2 }, { l + 8, (top + bot) / 2 });
            drawSignalLine (g, { l + 8, top }, { l + 8, bot });
            drawSignalLine (g, { rr - 8, top }, { rr - 8, bot });
            drawSignalLine (g, { rr - 8, (top + bot) / 2 }, { rr, (top + bot) / 2 });
            drawSignalLine (g, { l + 8, top }, { rr - 8, top });
            drawSignalLine (g, { l + 8, bot }, { rr - 8, bot });
            box (cx, top); box (cx, bot);
            break;
        case 0x0603:   // Mono Split: one input feeds both, two outputs
            drawSignalLine (g, { l, (top + bot) / 2 }, { l + 8, (top + bot) / 2 });
            drawSignalLine (g, { l + 8, top }, { l + 8, bot });
            drawSignalLine (g, { l + 8, top }, { rr, top });
            drawSignalLine (g, { l + 8, bot }, { rr, bot });
            box (cx, top); box (cx, bot);
            break;
        default:       // Stereo: one wide processor
            drawSignalLine (g, { l, top }, { rr, top });
            drawSignalLine (g, { l, bot }, { rr, bot });
            g.fillRoundedRectangle (juce::Rectangle<float> (16.0f, bot - top + 10.0f).withCentre ({ cx, (top + bot) / 2 }), 2.0f);
            break;
    }
}

void RoutingBar::paint (juce::Graphics& g)
{
    const auto current = editor.session().routing();
    const bool enabled = editor.connected();
    int i = 0;
    for (auto& [id, name] : mx300::routings())
    {
        auto c = cell (i);
        const bool sel = id == current && enabled;
        g.setColour (sel ? theme::panelRaised.brighter (0.08f) : (i == hover && enabled ? theme::panelRaised : theme::panel));
        g.fillRoundedRectangle (c, 8.0f);
        g.setColour (sel ? theme::reverb : theme::outline);
        g.drawRoundedRectangle (c, 8.0f, sel ? 1.5f : 1.0f);
        auto pic = c.reduced (18.0f, 8.0f).removeFromTop (c.getHeight() * 0.55f);
        drawDiagram (g, pic.withSizeKeepingCentre (juce::jmin (pic.getWidth(), 70.0f), pic.getHeight()), id,
                     sel ? theme::reverb : theme::textDim);
        g.setColour (sel ? theme::text : theme::textDim);
        g.setFont (theme::font (13.0f, sel));
        g.drawText (name, c.removeFromBottom (c.getHeight() * 0.4f), juce::Justification::centred);
        ++i;
    }
}

void RoutingBar::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < (int) mx300::routings().size(); ++i)
        if (cell (i).contains (e.position))
            h = i;
    if (h != hover)
    {
        hover = h;
        repaint();
    }
}

void RoutingBar::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < (int) mx300::routings().size(); ++i)
        if (cell (i).contains (e.position))
            editor.requestRouting (mx300::routings()[(size_t) i].first);
}

// ============================================================================ ProgramList

ProgramList::ProgramList (MX300Editor& e) : editor (e)
{
    for (auto* b : { &userTab, &factoryTab })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, theme::reverb.withAlpha (0.22f));
        addAndMakeVisible (*b);
    }
    userTab.onClick = [this] { selectBank (false); };
    factoryTab.onClick = [this] { selectBank (true); };
    userTab.setToggleState (true, juce::dontSendNotification);

    search.setTextToShowWhenEmpty ("Search programs", theme::textFaint);
    search.setFont (theme::font (14.0f));
    search.onTextChange = [this] { refresh(); };
    search.onEscapeKey = [this] { search.clear(); refresh(); };
    addAndMakeVisible (search);

    list.setRowHeight (30);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);
}

void ProgramList::selectBank (bool factory)
{
    showFactory = factory;
    userTab.setToggleState (! factory, juce::dontSendNotification);
    factoryTab.setToggleState (factory, juce::dontSendNotification);
    refresh();
}

void ProgramList::refresh()
{
    rows.clear();
    auto& names = editor.session().names (bank());
    const auto q = search.getText().trim();
    for (int i = 0; i < (int) names.size(); ++i)
        if (q.isEmpty() || juce::String (names[(size_t) i]).containsIgnoreCase (q) || juce::String (i + 1) == q)
            rows.push_back (i);
    list.updateContent();
    list.repaint();
}

void ProgramList::paint (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds());
    g.setColour (theme::outline);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void ProgramList::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto tabs = r.removeFromTop (30);
    userTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2).reduced (2, 0));
    factoryTab.setBounds (tabs.reduced (2, 0));
    r.removeFromTop (8);
    search.setBounds (r.removeFromTop (30));
    r.removeFromTop (8);
    list.setBounds (r);
}

void ProgramList::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= (int) rows.size())
        return;
    const int slot = rows[(size_t) row];
    auto cur = editor.session().currentProgram();
    const bool current = cur && cur->first == bank() && cur->second == slot;
    auto r = juce::Rectangle<int> (0, 0, w, h).reduced (0, 1);
    if (current)
    {
        g.setColour (theme::reverb.withAlpha (0.16f));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (theme::reverb);
        g.fillRoundedRectangle (r.removeFromLeft (3).toFloat(), 1.5f);
    }
    else
        r.removeFromLeft (3);
    g.setColour (current ? theme::text : theme::textFaint);
    g.setFont (theme::mono (13.0f));
    g.drawText (juce::String (slot + 1).paddedLeft ('0', 2), r.removeFromLeft (34), juce::Justification::centred);
    g.setColour (current ? theme::text : theme::textDim);
    g.setFont (theme::font (14.5f, current));
    auto& names = editor.session().names (bank());
    g.drawText ((size_t) slot < names.size() ? juce::String (names[(size_t) slot]) : juce::String(), r, juce::Justification::centredLeft);
}

void ProgramList::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) rows.size())
        editor.requestLoad (bank(), (uint8_t) rows[(size_t) row]);
}

// ============================================================================ MX300Editor

MX300Editor::MX300Editor (MX300Processor& p) : juce::AudioProcessorEditor (p), owner (p)
{
    setLookAndFeel (&lnf);
    prevButton.setTooltip ("Previous program");
    nextButton.setTooltip ("Next program");
    storeButton.setTooltip ("Save the current sound to a user program on the unit");
    renameButton.setTooltip ("Rename the current user program");
    menuButton.setTooltip ("Backup, restore, connection and settings");
    bypassButton.setTooltip ("The unit's global bypass (same as its bypass button; the processors' settings are untouched). "
                             "Automatable as \"Bypass (whole unit)\" (Logic's own bypass button can't reach the unit)");
    bypassButton.setColour (juce::TextButton::buttonOnColourId, theme::warn.withAlpha (0.3f));
    bypassButton.onClick = [this]
    {
        auto& param = owner.unitBypassParameter();
        param.beginChangeGesture();
        param.setValueNotifyingHost (param.get() ? 0.0f : 1.0f);
        param.endChangeGesture();
        updateBypassButton();
    };
    prevButton.onClick = [this] { step (-1); };
    nextButton.onClick = [this] { step (1); };
    storeButton.onClick = [this] { showStoreDialog(); };
    renameButton.onClick = [this] { showRenameDialog(); };
    menuButton.onClick = [this] { showMenu(); };
    retryButton.onClick = [this] { owner.hub().reconnect(); };
    demoButton.onClick = [this] { owner.hub().setDemoMode (true); };
    for (auto* b : { &prevButton, &nextButton, &storeButton, &renameButton, &menuButton, &bypassButton, &retryButton, &demoButton })
        addAndMakeVisible (*b);
    addAndMakeVisible (programs);
    addAndMakeVisible (routingBar);

    session().addListener (this);
    setResizable (true, true);
    setResizeLimits (960, 600, 2000, 1400);
    setSize (1180, 720);
    rebuildEngines();
    programs.refresh();
    updateHeader();
    startTimerHz (30);
}

MX300Editor::~MX300Editor()
{
    session().removeListener (this);
    setLookAndFeel (nullptr);
}

juce::Rectangle<int> MX300Editor::mainArea() const
{
    return getLocalBounds().withTrimmedTop (kHeader).withTrimmedBottom (kFooter).withTrimmedLeft (kSidebar);
}

void MX300Editor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // header
    auto head = getLocalBounds().removeFromTop (kHeader);
    g.setColour (theme::panel);
    g.fillRect (head);
    g.setColour (theme::outline);
    g.drawHorizontalLine (kHeader - 1, 0.0f, (float) getWidth());
    auto brand = head.removeFromLeft (kSidebar).reduced (16, 0);
    g.setColour (theme::text);
    g.setFont (theme::font (19.0f, true));
    g.drawText ("MX300 Control", brand.removeFromTop (38).withTrimmedTop (10), juce::Justification::bottomLeft);
    g.setColour (theme::textFaint);
    g.setFont (theme::font (11.5f));
    g.drawText ("unofficial editor", brand, juce::Justification::topLeft);

    // current program
    auto prog = head.withTrimmedLeft (60).withTrimmedRight (380);
    if (auto cur = session().currentProgram(); cur && connected())
    {
        auto badge = prog.removeFromLeft (52).withSizeKeepingCentre (48, 26);
        g.setColour (theme::panelRaised);
        g.fillRoundedRectangle (badge.toFloat(), 5.0f);
        g.setColour (theme::reverb);
        g.setFont (theme::mono (15.0f));
        g.drawText (slotLabel (cur->first, cur->second), badge, juce::Justification::centred);
        prog.removeFromLeft (10);
        g.setColour (theme::text);
        g.setFont (theme::font (22.0f, true));
        const juce::String name = session().editName();
        const bool edited = session().edited();
        auto nameArea = edited ? prog.withTrimmedRight (70) : prog;   // keep room for the EDITED pill
        g.drawText (name, nameArea, juce::Justification::centredLeft);
        if (edited)
        {
            const int w = juce::jmin (nameArea.getWidth(), juce::GlyphArrangement::getStringWidthInt (theme::font (22.0f, true), name));
            auto pill = juce::Rectangle<int> (prog.getX() + w + 12, prog.getCentreY() - 10, 58, 20);
            g.setColour (theme::warn.withAlpha (0.18f));
            g.fillRoundedRectangle (pill.toFloat(), 10.0f);
            g.setColour (theme::warn);
            g.setFont (theme::font (11.5f, true));
            g.drawText ("EDITED", pill, juce::Justification::centred);
        }
    }

    // footer
    auto foot = getLocalBounds().removeFromBottom (kFooter);
    g.setColour (theme::panel);
    g.fillRect (foot);
    g.setColour (theme::outline);
    g.drawHorizontalLine (foot.getY(), 0.0f, (float) getWidth());
    auto f = foot.reduced (14, 0);
    using S = mx300::DeviceSession::State;
    const auto st = session().state();
    const auto src = owner.hub().source();
    const auto dot = st == S::Ready ? theme::good : (st == S::Connecting ? theme::warn : theme::bad);
    g.setColour (dot);
    g.fillEllipse (f.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat());
    f.removeFromLeft (8);
    g.setColour (theme::text);
    g.setFont (theme::font (12.5f, true));
    const auto stText = stateText (st, src);
    g.drawText (stText, f.removeFromLeft (170), juce::Justification::centredLeft);
    g.setColour (theme::textFaint);
    g.setFont (theme::font (12.0f));
    if (src == DeviceHub::Source::Hardware)
        g.drawText (owner.hub().portName(), f.removeFromLeft (300), juce::Justification::centredLeft);

    // MIDI activity
    const double now = juce::Time::getMillisecondCounterHiRes();
    auto leds = f.removeFromRight (90);
    for (auto [label, t] : { std::pair<const char*, double> { "IN", owner.hub().lastIncomingMs },
                             std::pair<const char*, double> { "OUT", owner.hub().lastOutgoingMs } })
    {
        auto cell = leds.removeFromLeft (45);
        const bool on = now - t < 120;
        g.setColour (on ? theme::reverb : theme::outline);
        g.fillEllipse (cell.removeFromLeft (10).withSizeKeepingCentre (7, 7).toFloat());
        g.setColour (theme::textFaint);
        g.setFont (theme::font (10.5f, true));
        g.drawText (label, cell.withTrimmedLeft (4), juce::Justification::centredLeft);
    }
    g.setColour (statusColour);
    g.setFont (theme::font (12.5f));
    g.drawText (status, f.withTrimmedRight (12), juce::Justification::centredRight);
}

void MX300Editor::paintOverChildren (juce::Graphics& g)
{
    using S = mx300::DeviceSession::State;
    const auto st = session().state();
    if (st == S::Ready)
        return;
    auto r = mainArea();
    g.setColour (theme::background.withAlpha (0.82f));
    g.fillRect (r);
    auto box = r.withSizeKeepingCentre (520, 180);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (box.toFloat(), 12.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (box.toFloat(), 12.0f, 1.0f);
    juce::String title, body;
    const auto src = owner.hub().source();
    if (st == S::UpdateMode)
    {
        title = "The unit is in OS-update mode";
        body = "This editor never sends firmware data. Power-cycle the MX300 to return to normal operation.";
    }
    else if (st == S::Connecting)
    {
        title = "Connecting...";
        body = "Reading the unit's programs and current settings.";
    }
    else if (src == DeviceHub::Source::None)
    {
        title = "No MX300 found";
        body = "Connect the MX300 over USB and power it on. It shows up as a USB MIDI device; this window connects automatically.";
    }
    else
    {
        title = "The MX300 is not responding";
        body = "The USB port is open but the unit does not answer. Check that it is powered on; retrying every few seconds.";
    }
    auto t = box.reduced (24, 20);
    g.setColour (theme::text);
    g.setFont (theme::font (18.0f, true));
    g.drawText (title, t.removeFromTop (28), juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::font (14.0f));
    g.drawFittedText (body, t.removeFromTop (60), juce::Justification::topLeft, 3);
}

void MX300Editor::resized()
{
    auto head = getLocalBounds().removeFromTop (kHeader).reduced (14, 13);
    menuButton.setBounds (head.removeFromRight (40));
    head.removeFromRight (8);
    renameButton.setBounds (head.removeFromRight (82));
    head.removeFromRight (8);
    storeButton.setBounds (head.removeFromRight (82));
    head.removeFromRight (8);
    bypassButton.setBounds (head.removeFromRight (82));
    // program display spans [kSidebar + 60, width - 380]; arrows sit at both ends
    prevButton.setBounds (kSidebar + 14, 13, 34, kHeader - 26);
    nextButton.setBounds (getWidth() - 372, 13, 34, kHeader - 26);

    programs.setBounds (getLocalBounds().withTrimmedTop (kHeader).withTrimmedBottom (kFooter).removeFromLeft (kSidebar));
    auto main = mainArea().reduced (16);
    routingBar.setBounds (main.removeFromTop (kRouting));
    main.removeFromTop (12);
    if (engines.size() == 1)
        engines[0]->setBounds (main);
    else if (engines.size() == 2)
    {
        auto left = main.removeFromLeft ((main.getWidth() - 28) / 2);
        engines[0]->setBounds (left);
        main.removeFromLeft (28);
        engines[1]->setBounds (main);
    }

    auto overlay = mainArea().withSizeKeepingCentre (520, 180).reduced (24, 20);
    auto buttons = overlay.removeFromBottom (32);
    retryButton.setBounds (buttons.removeFromLeft (100));
    buttons.removeFromLeft (10);
    demoButton.setBounds (buttons.removeFromLeft (140));
}

void MX300Editor::rebuildEngines()
{
    const auto routing = session().routing();
    std::vector<uint16_t> typesNow;
    const auto positions = session().enginePositions();
    for (auto pos : positions)
        typesNow.push_back (session().engine (pos).type);

    if (routing != builtRouting || typesNow != builtTypes || engines.size() != positions.size())
    {
        engines.clear();
        for (auto pos : positions)
        {
            engines.push_back (std::make_unique<EnginePanel> (*this, pos));
            addAndMakeVisible (*engines.back());
        }
        builtRouting = routing;
        builtTypes = typesNow;
        resized();
    }
    else
        for (auto& e : engines)
            e->refreshValues();
    routingBar.repaint();
}

void MX300Editor::updateHeader()
{
    using S = mx300::DeviceSession::State;
    const bool ready = session().state() == S::Ready;
    const auto cur = session().currentProgram();
    prevButton.setEnabled (ready && cur.has_value());
    nextButton.setEnabled (ready && cur.has_value());
    storeButton.setEnabled (ready);
    bypassButton.setEnabled (ready);
    updateBypassButton();
    renameButton.setEnabled (ready && cur && cur->first == mx300::kBankUser);
    const bool showOverlayButtons = ! ready && session().state() != S::Connecting;
    retryButton.setVisible (showOverlayButtons && session().state() != S::UpdateMode);
    demoButton.setVisible (showOverlayButtons && owner.hub().source() != DeviceHub::Source::Demo);
    repaint();
}

juce::String MX300Editor::knobBadge (uint16_t id, uint8_t pos)
{
    const auto routing = session().routing();
    if (mx300::isWholeRouting (routing))
    {
        for (size_t k = 0; k < mx300::kKnobAssign.size(); ++k)
            if (auto v = session().value (mx300::kKnobAssign[k], mx300::kPosEngine1))
                if ((*v & 0xFFFF) == id && (*v >> 16) == pos)
                    return juce::String::charToString ((juce::juce_wchar) ('A' + k));
        return {};
    }
    // dual routings: A/B/C control the first three parameters of the processor selected on the unit
    if (auto* t = mx300::findType (session().engine (pos).type))
        for (size_t k = 0; k < 3 && k < t->params.size(); ++k)
            if (t->params[k] == id)
                return juce::String::charToString ((juce::juce_wchar) ('A' + k));
    return {};
}

void MX300Editor::requestLoad (uint8_t bank, uint8_t slot)
{
    if (! connected())
        return;
    auto load = [this, bank, slot] { session().loadPreset (bank, slot); setStatus ("Loading " + slotLabel (bank, slot) + "..."); };
    if (! session().edited())
    {
        load();
        return;
    }
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Discard changes?")
                                      .withMessage ("\"" + juce::String (session().editName()) + "\" has unsaved changes. Load "
                                                    + slotLabel (bank, slot) + " anyway?")
                                      .withButton ("Load")
                                      .withButton ("Cancel")
                                      .withAssociatedComponent (this),
                                  [load] (int r) { if (r == 1) load(); });
}

void MX300Editor::requestRouting (uint16_t routing)
{
    if (connected() && routing != session().routing())
    {
        session().setRouting (routing);
        setStatus ("Routing: " + juce::String (mx300::routingName (routing)));
    }
}

void MX300Editor::step (int delta)
{
    auto cur = session().currentProgram();
    if (! cur)
        return;
    const int slot = (cur->second + delta + mx300::kSlots) % mx300::kSlots;
    requestLoad (cur->first, (uint8_t) slot);
}

void MX300Editor::setStatus (const juce::String& s, juce::Colour c)
{
    status = s;
    statusColour = c;
    repaint (getLocalBounds().removeFromBottom (kFooter));
}

void MX300Editor::showMenu()
{
    auto& hub = owner.hub();
    juce::PopupMenu m;
    const bool ready = connected();
    m.addItem ("Back up user programs to file...", ready && hub.source() == DeviceHub::Source::Hardware, false, [this] { backupToFile(); });
    m.addItem ("Restore user programs from file...", ready && hub.source() == DeviceHub::Source::Hardware, false, [this] { restoreFromFile(); });
    m.addItem ("Reload from unit", ready, false, [this] { session().refreshEditBuffer(); });
    m.addSeparator();

    juce::PopupMenu ports;
    ports.addItem ("Automatic (find the MX300)", true, hub.preferredPort().isEmpty() && ! hub.demoMode(), [&hub] { hub.setPreferredPort ({}); });
    for (auto& d : hub.availablePorts())
    {
        const auto id = d.identifier;
        ports.addItem (d.name, true, hub.preferredPort() == id && ! hub.demoMode(), [&hub, id] { hub.setPreferredPort (id); });
    }
    m.addSubMenu ("MIDI port", ports);
    m.addItem ("Demo unit (no hardware)", true, hub.demoMode(), [&hub] { hub.setDemoMode (! hub.demoMode()); });
    if (! juce::JUCEApplicationBase::isStandaloneApp())
        m.addItem ("Restore the unit's sound when a project opens", true, owner.restoreOnLoad(),
                   [this] { owner.setRestoreOnLoad (! owner.restoreOnLoad()); });
    if (! juce::JUCEApplicationBase::isStandaloneApp())
    {
        const auto bpm = owner.hostBpm();
        m.addItem ("Sync delay tempo to the DAW" + (bpm > 0 ? juce::String (" (") + juce::String (bpm, 1) + " BPM)" : juce::String()),
                   true, owner.tempoSync(), [this]
                   {
                       owner.setTempoSync (! owner.tempoSync());
                       for (auto& e : engines)
                           e->refreshValues();
                   });
    }
    m.addSeparator();
    m.addItem ("About MX300 Control", [this]
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::InfoIcon)
                                          .withTitle ("MX300 Control " JucePlugin_VersionString)
                                          .withMessage ("An unofficial, open-source editor for the Lexicon MX300 over USB.\n"
                                                        "Not affiliated with Lexicon or Harman. Licensed under the AGPLv3.")
                                          .withButton ("OK")
                                          .withAssociatedComponent (this),
                                      nullptr);
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton));
}

void MX300Editor::showStoreDialog()
{
    dialog = std::make_unique<juce::AlertWindow> ("Store program", "Save the current sound to a user program on the unit.\nThe program in the chosen slot is replaced.",
                                                  juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel (&lnf);
    dialog->addTextEditor ("name", session().editName(), "Name");
    dialog->getTextEditor ("name")->setInputRestrictions ((int) mx300::kNameLength, kNameChars);
    juce::StringArray items;
    auto& names = session().names (mx300::kBankUser);
    for (size_t i = 0; i < names.size(); ++i)
        items.add (juce::String ((int) i + 1).paddedLeft ('0', 2) + "   " + names[i]);
    dialog->addComboBox ("slot", items, "Slot");
    auto cur = session().currentProgram();
    dialog->getComboBoxComponent ("slot")->setSelectedItemIndex (cur && cur->first == mx300::kBankUser ? cur->second : 0);
    dialog->addButton ("Store", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r)
    {
        if (r == 1 && dialog)
        {
            auto name = dialog->getTextEditorContents ("name").trim();
            const int slot = dialog->getComboBoxComponent ("slot")->getSelectedItemIndex();
            if (name.isEmpty())
                name = "Untitled";
            if (slot >= 0)
            {
                session().store ((uint8_t) slot, name.toStdString());
                setStatus ("Storing to U" + juce::String (slot + 1).paddedLeft ('0', 2) + "...");
            }
        }
        juce::MessageManager::callAsync ([this] { dialog.reset(); });
    }), false);
}

void MX300Editor::showRenameDialog()
{
    auto cur = session().currentProgram();
    if (! cur || cur->first != mx300::kBankUser)
        return;
    const uint8_t slot = cur->second;
    dialog = std::make_unique<juce::AlertWindow> ("Rename program", "New name for U" + juce::String (slot + 1).paddedLeft ('0', 2) + ":",
                                                  juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel (&lnf);
    const auto& names = session().names (mx300::kBankUser);
    dialog->addTextEditor ("name", slot < names.size() ? juce::String (names[slot]) : juce::String(), "Name");
    dialog->getTextEditor ("name")->setInputRestrictions ((int) mx300::kNameLength, kNameChars);
    dialog->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, slot] (int r)
    {
        if (r == 1 && dialog)
        {
            const auto name = dialog->getTextEditorContents ("name").trim();
            if (name.isNotEmpty())
            {
                session().rename (slot, name.toStdString());
                setStatus ("Renaming...");
            }
        }
        juce::MessageManager::callAsync ([this] { dialog.reset(); });
    }), false);
}

void MX300Editor::backupToFile()
{
    const auto file = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                          .getChildFile ("MX300 backup " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d") + ".syx");
    chooser = std::make_unique<juce::FileChooser> ("Save MX300 backup", file, "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        const auto target = fc.getResult();
        if (target == juce::File())
            return;
        setStatus ("Backing up...");
        session().backup ([this, target] (std::vector<mx300::Bytes> frames)
        {
            if (frames.empty())
            {
                setStatus ("Backup failed: no answer from the unit", theme::bad);
                return;
            }
            juce::MemoryBlock data;
            for (auto& f : frames)
                data.append (f.data(), f.size());
            if (target.replaceWithData (data.getData(), data.getSize()))
                setStatus ("Backup saved: " + target.getFileName() + " (" + juce::String ((int) frames.size()) + " messages)", theme::good);
            else
                setStatus ("Could not write " + target.getFullPathName(), theme::bad);
        });
    });
}

void MX300Editor::restoreFromFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Restore MX300 backup",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;
        juce::MemoryBlock data;
        file.loadFileAsData (data);
        const auto* p = static_cast<const uint8_t*> (data.getData());
        auto frames = mx300::splitSysex (mx300::Bytes (p, p + data.getSize()));

        // Only send back what a bulk dump contains. Never firmware (78/7A/7B) or anything unexpected.
        int presets = 0;
        bool valid = ! frames.empty();
        for (auto& f : frames)
        {
            auto m = mx300::parseFrame (f);
            using namespace mx300::proc;
            const bool allowed = m && m->checksumOk
                                 && (m->procId == BulkStart || m->procId == BulkEnd || m->procId == PresetStart
                                     || m->procId == PresetParams || m->procId == PresetEnd || m->procId == Globals);
            valid = valid && allowed;
            if (m && m->procId == PresetStart)
                ++presets;
        }
        if (! valid || presets == 0)
        {
            setStatus ("Not an MX300 backup: " + file.getFileName(), theme::bad);
            return;
        }
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Restore user programs?")
                                          .withMessage ("This replaces the user programs on the unit with the " + juce::String (presets)
                                                        + " programs in " + file.getFileName() + ". Back up first if unsure.")
                                          .withButton ("Restore")
                                          .withButton ("Cancel")
                                          .withAssociatedComponent (this),
                                      [this, frames] (int r)
        {
            if (r == 1)
            {
                session().restore (frames);
                setStatus ("Restoring... (about " + juce::String ((int) frames.size() * 30 / 1000 + 1) + " s)");
            }
        });
    });
}

// ---------------------------------------------------------------- session events

void MX300Editor::stateChanged()
{
    rebuildEngines();
    programs.refresh();
    updateHeader();
    if (session().state() == mx300::DeviceSession::State::Ready)
        setStatus ({});
}

void MX300Editor::namesChanged (uint8_t) { programs.refresh(); updateHeader(); }
void MX300Editor::editBufferChanged() { rebuildEngines(); updateHeader(); }
void MX300Editor::programChanged() { programs.refresh(); updateHeader(); }

void MX300Editor::paramChanged (const mx300::ParamValue& v, bool)
{
    if (v.pos == mx300::kPosSystem)
        return;
    for (auto& e : engines)
        e->refreshValues();
}

void MX300Editor::operationFinished (const std::string& what, bool ok)
{
    static const std::map<std::string, std::pair<const char*, const char*>> text = {
        { "load", { "Program loaded", "Load failed" } },
        { "store", { "Program stored", "Store failed" } },
        { "rename", { "Program renamed", "Rename failed" } },
        { "write", { "Sound restored from the project", "Could not restore the project's sound" } },
        { "backup", { "", "Backup failed" } },
        { "restore", { "User programs restored", "Restore finished with errors" } },
    };
    auto it = text.find (what);
    if (it == text.end())
        return;
    const auto* msg = ok ? it->second.first : it->second.second;
    if (*msg)
        setStatus (msg, ok ? theme::good : theme::bad);
}

void MX300Editor::updateBypassButton()
{
    const bool on = owner.unitBypassParameter().get();   // also changed by automation or the host
    if (bypassButton.getToggleState() != on)
    {
        bypassButton.setToggleState (on, juce::dontSendNotification);
        bypassButton.setButtonText (on ? "Bypassed" : "Bypass");
    }
}

void MX300Editor::timerCallback()
{
    updateBypassButton();
    repaint (getLocalBounds().removeFromBottom (kFooter).removeFromRight (120));
    static DeviceHub::Source lastSource = DeviceHub::Source::None;
    if (owner.hub().source() != lastSource)
    {
        lastSource = owner.hub().source();
        updateHeader();
    }
}
