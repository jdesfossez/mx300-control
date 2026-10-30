#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

class MX300Editor;

// One parameter: label, rotary knob, value text, optional front-panel knob badge (A/B/C).
class ParamKnob : public juce::Component
{
public:
    ParamKnob (MX300Editor&, uint16_t id, uint8_t pos);
    void refresh();
    void setAccent (juce::Colour);
    void setBadge (juce::String b) { badge = b; repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    uint16_t id() const { return paramId; }

private:
    bool followsHostTempo() const;
    MX300Editor& editor;
    uint16_t paramId;
    uint8_t pos;
    juce::Slider slider;
    juce::String badge;
    bool updating = false;
};

// A processor: type, bypass, mix and its parameters.
class EnginePanel : public juce::Component
{
public:
    EnginePanel (MX300Editor&, uint8_t pos);
    void rebuild();          // type or routing changed
    void refreshValues();    // values changed
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Colour accent() const;
    MX300Editor& editor;
    uint8_t pos;
    juce::ComboBox type;
    juce::TextButton bypass { "Bypass" };
    std::unique_ptr<ParamKnob> mix;
    std::vector<std::unique_ptr<ParamKnob>> knobs;
    std::vector<uint16_t> typeIds;
};

class RoutingBar : public juce::Component
{
public:
    explicit RoutingBar (MX300Editor&);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

private:
    juce::Rectangle<float> cell (int i) const;
    static void drawDiagram (juce::Graphics&, juce::Rectangle<float>, uint16_t routing, juce::Colour);
    MX300Editor& editor;
    int hover = -1;
};

class ProgramList : public juce::Component, private juce::ListBoxModel
{
public:
    explicit ProgramList (MX300Editor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    uint8_t bank() const { return showFactory ? mx300::kBankFactory : mx300::kBankUser; }

private:
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void selectBank (bool factory);

    MX300Editor& editor;
    juce::TextButton userTab { "User" }, factoryTab { "Factory" };
    juce::TextEditor search;
    juce::ListBox list { "programs", this };
    std::vector<int> rows;   // slots matching the search
    bool showFactory = false;
};

class MX300Editor : public juce::AudioProcessorEditor,
                    private mx300::DeviceSession::Listener,
                    private juce::Timer
{
public:
    explicit MX300Editor (MX300Processor&);
    ~MX300Editor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    MX300Processor& owner;
    mx300::DeviceSession& session() { return owner.session(); }
    bool connected() { return session().state() == mx300::DeviceSession::State::Ready; }

    void requestLoad (uint8_t bank, uint8_t slot);
    void requestRouting (uint16_t routing);
    juce::String knobBadge (uint16_t id, uint8_t pos);

private:
    void stateChanged() override;
    void namesChanged (uint8_t) override;
    void editBufferChanged() override;
    void paramChanged (const mx300::ParamValue&, bool) override;
    void programChanged() override;
    void operationFinished (const std::string&, bool) override;
    void timerCallback() override;

    void rebuildEngines();
    void updateHeader();
    void showMenu();
    void showStoreDialog();
    void showRenameDialog();
    void backupToFile();
    void restoreFromFile();
    void step (int delta);
    void updateBypassButton();
    void setStatus (const juce::String& s, juce::Colour c = theme::textDim);
    juce::Rectangle<int> mainArea() const;

    MX300LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    juce::TextButton prevButton { "<" }, nextButton { ">" }, storeButton { "Store" }, renameButton { "Rename" },
        menuButton { "..." }, bypassButton { "Bypass" }, retryButton { "Retry" }, demoButton { "Use demo unit" };
    ProgramList programs { *this };
    RoutingBar routingBar { *this };
    std::vector<std::unique_ptr<EnginePanel>> engines;
    uint16_t builtRouting = 0;
    std::vector<uint16_t> builtTypes;
    juce::String status;
    juce::Colour statusColour = theme::textDim;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> dialog;
};
