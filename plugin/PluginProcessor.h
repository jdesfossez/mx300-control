#pragma once
// The plugin processes no audio: it is a remote control for the MX300.
// Host parameters are generic slots per engine (Bypass, Mix, Control 1..12) that follow the loaded
// effect type, so automation keeps working when the type changes.

#include "DeviceHub.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

// A host parameter whose name follows what it controls (e.g. "Mid RT", or "2 Tempo" in dual routings).
// Hosts are told to refresh names when the effect type or routing changes.
class SlotParameter : public juce::AudioParameterFloat
{
public:
    SlotParameter (const juce::String& id, const juce::String& fallbackName, std::function<juce::String()> nameFn,
                   const juce::AudioParameterFloatAttributes& attrs)
        : juce::AudioParameterFloat (juce::ParameterID { id, 1 }, fallbackName, juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, attrs),
          currentName (std::move (nameFn)) {}

    juce::String getName (int maximumStringLength) const override
    {
        auto n = currentName ? currentName() : juce::String();
        if (n.isEmpty())
            n = juce::AudioParameterFloat::getName (1024);
        return maximumStringLength > 0 ? n.substring (0, maximumStringLength) : n;
    }

private:
    std::function<juce::String()> currentName;
};

class MX300Processor : public juce::AudioProcessor,
                       private mx300::DeviceSession::Listener,
                       private juce::AudioProcessorParameter::Listener,
                       private juce::Timer
{
public:
    static constexpr int kEngines = 2, kControls = 12, kSlotsPerEngine = kControls + 2;   // bypass, mix, controls

    MX300Processor();
    ~MX300Processor() override;

    DeviceHub& hub() { return *sharedHub; }
    mx300::DeviceSession& session() { return sharedHub->session(); }

    // Which device parameter a host slot drives right now (id 0 = unused).
    struct SlotTarget { uint16_t id = 0; uint8_t pos = 0; int min = 0, max = 0; };
    SlotTarget target (int engine, int slot) const;
    juce::RangedAudioParameter* hostParameter (uint16_t id, uint8_t pos);
    // Called by the editor around a drag so hosts record automation gestures.
    void beginGesture (uint16_t id, uint8_t pos);
    void endGesture (uint16_t id, uint8_t pos);

    bool restoreOnLoad() const;
    void setRestoreOnLoad (bool);
    // "Bypass (whole unit)": mirrors the unit's global bypass; the editor's main Bypass button.
    juce::AudioParameterBool& unitBypassParameter() { return *unitBypass; }

    // Delay Tempo follows the host's tempo (saved with the project). hostBpm() is 0 when the host gives none.
    bool tempoSync() const { return syncTempo; }
    void setTempoSync (bool on) { syncTempo = on; }
    double hostBpm() const { return bpmFromHost; }

    // ---- juce::AudioProcessor
    const juce::String getName() const override { return "MX300 Control"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    // The unit's global bypass (0x3034, its own bypass button), also the host's bypass in hosts that tell
    // the plugin (Logic doesn't, it just stops running a bypassed plugin).
    juce::AudioProcessorParameter* getBypassParameter() const override { return unitBypass; }
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    struct Snapshot   // what host-facing threads may read
    {
        std::array<std::array<SlotTarget, kSlotsPerEngine>, kEngines> targets;
        std::array<mx300::EngineState, kEngines> engines;
        juce::String editName;
        std::vector<mx300::ParamValue> editBuffer;
    };

    // mx300::DeviceSession::Listener
    void stateChanged() override;
    void editBufferChanged() override;
    void paramChanged (const mx300::ParamValue&, bool fromUnit) override;
    void programChanged() override;
    // juce::AudioProcessorParameter::Listener
    void parameterValueChanged (int index, float newValue) override;
    void parameterGestureChanged (int, bool) override {}
    void timerCallback() override;

    static BusesProperties makeBuses();
    void rebuildSnapshot();
    void pushModelToHost();
    juce::String slotText (int engine, int slot, float normalised) const;
    juce::String slotDisplayName (int engine, int slot) const;
    static int engineIndex (uint8_t pos) { return pos == mx300::kPosEngine2 ? 1 : 0; }

    juce::SharedResourcePointer<DeviceHub> sharedHub;
    std::array<std::array<SlotParameter*, kSlotsPerEngine>, kEngines> slots {};

    mutable juce::SpinLock snapshotLock;
    Snapshot snapshot;

    // host -> device: written on any thread, drained on the message thread
    std::array<std::atomic<float>, kEngines * kSlotsPerEngine> hostValues;
    std::array<std::atomic<bool>, kEngines * kSlotsPerEngine> hostDirty;
    std::atomic<bool> updatingFromModel { false };

    juce::AudioParameterBool* unitBypass = nullptr;
    std::atomic<bool> unitBypassDirty { false };
    void syncUnitBypass (bool on);   // unit -> parameter, without sending it back

    std::atomic<bool> syncTempo { false };
    std::atomic<double> bpmFromHost { 0.0 };
    void applyHostTempo();

    // session restore from a DAW project
    juce::CriticalSection pendingLock;
    std::optional<std::pair<juce::String, std::vector<mx300::ParamValue>>> pendingRestore;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MX300Processor)
};
