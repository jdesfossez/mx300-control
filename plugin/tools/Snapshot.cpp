// Renders the editor against the demo unit into PNG files: MX300Snapshot <output dir>
#include "../PluginProcessor.h"

#include <cstdlib>

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

int main (int argc, char** argv)
{
    setenv ("MX300_DEMO", "1", 1);
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out (argc > 1 ? argv[1] : ".");
    out.createDirectory();

    auto processor = std::make_unique<MX300Processor>();
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());
    editor->setSize (1180, 720);
    editor->addToDesktop (0);   // offscreen peer, needed for fonts/layout
    editor->setVisible (false);

    auto shoot = [&] (const juce::String& name)
    {
        pump (400);
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream os (out.getChildFile (name + ".png"));
        os.setPosition (0);
        os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("wrote %s (state %d, routing %04x)\n", name.toRawUTF8(), (int) processor->session().state(), processor->session().routing());
        // what a host sees: parameter name and value text
        for (auto* p : processor->getParameters())
            if (p->getName (64).contains ("unused") == false)
                std::printf ("    %-28s %s\n", p->getName (64).toRawUTF8(), p->getCurrentValueAsText().toRawUTF8());
    };

    pump (1500);
    shoot ("1-stereo");
    processor->session().loadPreset (mx300::kBankUser, 2);   // a dual routing in the demo bank
    shoot ("2-dual");
    processor->session().loadPreset (mx300::kBankUser, 5);
    shoot ("3-dual-other");
    processor->session().setParam (mx300::kParamBypass, mx300::kPosEngine1, 1);
    shoot ("4-edited-bypassed");

    // whole-unit bypass = the unit's global bypass (0x3034); the processors' own Bypass is untouched
    auto bypasses = [&] (const char* when)
    {
        auto& ses = processor->session();
        std::printf ("%-24s unit bypass %d (param %d), engine1 bypass %d, engine2 bypass %d, edited %d\n", when,
                     ses.global (mx300::kParamUnitBypass).value_or (-1), (int) processor->unitBypassParameter().get(),
                     ses.value (mx300::kParamBypass, mx300::kPosEngine1).value_or (-1),
                     ses.value (mx300::kParamBypass, mx300::kPosEngine2).value_or (-1), (int) ses.edited());
    };
    bypasses ("before:");
    processor->getBypassParameter()->setValueNotifyingHost (1.0f);
    pump (300);
    bypasses ("host bypass on:");
    processor->getBypassParameter()->setValueNotifyingHost (0.0f);
    pump (300);
    bypasses ("host bypass off:");
    processor->getBypassParameter()->setValueNotifyingHost (1.0f);
    shoot ("6-unit-bypassed");
    processor->getBypassParameter()->setValueNotifyingHost (0.0f);
    pump (300);

    editor->setSize (960, 600);
    shoot ("5-minimum-size");
    editor->removeFromDesktop();
    editor.reset();
    processor.reset();
    return 0;
}
