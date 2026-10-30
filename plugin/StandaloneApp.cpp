// The standalone app: the same processor and editor as the plugin, in a plain window, with no audio.
// (JUCE's default standalone wrapper would open an audio device and show audio settings.)

#include "PluginProcessor.h"

#include <juce_audio_plugin_client/detail/juce_CreatePluginFilter.h>

#if JucePlugin_Build_Standalone

class MX300App : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "MX300 Control"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        processor = juce::createPluginFilterOfType (juce::AudioProcessor::wrapperType_Standalone);
        window = std::make_unique<Window> (*processor);
    }

    void shutdown() override
    {
        window.reset();
        processor.reset();   // last reference to the device hub: turns GUI mode off on the unit
    }

    void systemRequestedQuit() override { quit(); }

private:
    struct Window : juce::DocumentWindow
    {
        explicit Window (juce::AudioProcessor& p)
            : juce::DocumentWindow ("MX300 Control", juce::Colour (0xff121417), juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (p.createEditorAndMakeActive(), true);
            setResizable (true, false);
            if (auto* c = getContentComponent())
                if (auto* e = dynamic_cast<juce::AudioProcessorEditor*> (c))
                    if (auto* constrainer = e->getConstrainer())
                        setResizeLimits (constrainer->getMinimumWidth(), constrainer->getMinimumHeight(),
                                         constrainer->getMaximumWidth(), constrainer->getMaximumHeight());
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<juce::AudioProcessor> processor;
    std::unique_ptr<Window> window;
};

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new MX300App(); }

#endif
