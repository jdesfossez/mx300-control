#pragma once
// One connection to the MX300, shared by every plugin instance in the process (and by the app).
// Lives on the message thread. Owns the CoreMIDI port (or the demo unit) and the DeviceSession.

#include "DeviceSession.h"
#include "FakeUnit.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <mutex>

class DeviceHub : private juce::Timer,
                  private juce::MidiInputCallback,
                  private mx300::Transport
{
public:
    DeviceHub();
    ~DeviceHub() override;

    mx300::DeviceSession& session() { return deviceSession; }

    enum class Source { None, Hardware, Demo };
    Source source() const { return currentSource; }
    juce::String portName() const { return currentPortName; }

    // Port selection: empty identifier = automatic (first port named like the MX300).
    juce::Array<juce::MidiDeviceInfo> availablePorts() const;
    juce::String preferredPort() const;
    void setPreferredPort (const juce::String& identifier);
    bool demoMode() const;
    void setDemoMode (bool);
    void reconnect();

    juce::PropertiesFile& settings() { return *props; }

    // Activity for the UI's MIDI LEDs (milliseconds timestamps).
    double lastIncomingMs = 0, lastOutgoingMs = 0;

private:
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void sendSysex (const mx300::Bytes& frame) override;
    void openBestSource();
    void closeSource();
    double nowMs() const { return juce::Time::getMillisecondCounterHiRes(); }

    std::unique_ptr<juce::PropertiesFile> props;
    mx300::DeviceSession deviceSession { *this };
    Source currentSource = Source::None;
    juce::String currentPortName;
    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    std::unique_ptr<mx300::FakeUnit> demo;
    juce::MidiDeviceListConnection deviceListConnection;

    std::mutex incomingLock;
    std::vector<mx300::Bytes> incoming;   // filled on the MIDI thread
    double lastConnectAttemptMs = 0;
};
