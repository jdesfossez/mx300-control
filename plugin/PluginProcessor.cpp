#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
juce::String slotId (int engine, int slot)
{
    const auto e = "e" + juce::String (engine + 1) + "_";
    if (slot == 0) return e + "bypass";
    if (slot == 1) return e + "mix";
    return e + "c" + juce::String (slot - 1);
}

juce::String slotName (int engine, int slot)
{
    const auto e = "Processor " + juce::String (engine + 1) + " ";
    if (slot == 0) return e + "Bypass";
    if (slot == 1) return e + "Mix";
    return e + "Control " + juce::String (slot - 1);
}

} // namespace

juce::AudioProcessor::BusesProperties MX300Processor::makeBuses()
{
    // The standalone app has no audio at all; in a host, pass audio through untouched.
    if (juce::JUCEApplicationBase::isStandaloneApp())
        return BusesProperties();
    return BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

MX300Processor::MX300Processor() : juce::AudioProcessor (makeBuses())
{
    for (auto& v : hostValues) v = 0;
    for (auto& d : hostDirty) d = false;

    for (int e = 0; e < kEngines; ++e)
        for (int s = 0; s < kSlotsPerEngine; ++s)
        {
            auto attrs = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [this, e, s] (float v, int) { return slotText (e, s, v); });
            auto* p = new SlotParameter (slotId (e, s), slotName (e, s), [this, e, s] { return slotDisplayName (e, s); }, attrs);
            p->addListener (this);
            addParameter (p);
            slots[(size_t) e][(size_t) s] = p;
        }

    unitBypass = new juce::AudioParameterBool (juce::ParameterID { "unit_bypass", 1 }, "Bypass (whole unit)", false);
    unitBypass->addListener (this);
    addParameter (unitBypass);   // last, so the slot parameters keep their indices

    session().addListener (this);
    rebuildSnapshot();
    pushModelToHost();
    startTimerHz (100);
}

MX300Processor::~MX300Processor()
{
    stopTimer();
    session().removeListener (this);
    for (auto& e : slots)
        for (auto* p : e)
            p->removeListener (this);
    unitBypass->removeListener (this);
}

// ---------------------------------------------------------------- slots <-> device

MX300Processor::SlotTarget MX300Processor::target (int engine, int slot) const
{
    const juce::SpinLock::ScopedLockType l (snapshotLock);
    return snapshot.targets[(size_t) engine][(size_t) slot];
}

void MX300Processor::rebuildSnapshot()
{
    Snapshot s;
    auto& ses = session();
    const auto positions = ses.enginePositions();
    for (int e = 0; e < kEngines; ++e)
    {
        const uint8_t pos = e == 0 ? mx300::kPosEngine1 : mx300::kPosEngine2;
        if (std::find (positions.begin(), positions.end(), pos) == positions.end())
            continue;
        s.engines[(size_t) e] = ses.engine (pos);
        auto& t = s.targets[(size_t) e];
        t[0] = { mx300::kParamBypass, pos, 0, 1 };
        t[1] = { mx300::kParamMix, pos, 0, 99 };
        if (auto* type = mx300::findType (s.engines[(size_t) e].type))
            for (size_t k = 0; k < type->params.size() && k < (size_t) kControls; ++k)
                if (auto* p = mx300::findParam (type->params[k]))
                    t[k + 2] = { p->id, pos, p->min, p->max };
    }
    s.editName = ses.editName();
    s.editBuffer = ses.editBuffer();
    const juce::SpinLock::ScopedLockType l (snapshotLock);
    snapshot = std::move (s);
}

juce::String MX300Processor::slotText (int engine, int slot, float v) const
{
    const juce::SpinLock::ScopedLockType l (snapshotLock);
    auto& t = snapshot.targets[(size_t) engine][(size_t) slot];
    if (t.id == 0)
        return "-";
    const int raw = juce::roundToInt ((float) t.min + v * (float) (t.max - t.min));
    return mx300::displayValue (t.id, raw, &snapshot.engines[(size_t) engine]);   // the name is the parameter's own
}

juce::String MX300Processor::slotDisplayName (int engine, int slot) const
{
    const juce::SpinLock::ScopedLockType l (snapshotLock);
    auto& t = snapshot.targets[(size_t) engine][(size_t) slot];
    const auto& ownEngine = snapshot.engines[(size_t) engine];
    if (t.id == 0)
    {
        // slot not used by the current type (or this processor is absent in Stereo)
        const auto what = slot == 0 ? juce::String ("Bypass") : slot == 1 ? juce::String ("Mix") : "Control " + juce::String (slot - 1);
        return "P" + juce::String (engine + 1) + " " + what + " (unused)";
    }
    const juce::String name = slot == 0 ? "Bypass" : mx300::paramName (t.id);
    return mx300::isWholeRouting (ownEngine.routing) ? name : juce::String (engine + 1) + " " + name;
}

juce::RangedAudioParameter* MX300Processor::hostParameter (uint16_t id, uint8_t pos)
{
    for (int e = 0; e < kEngines; ++e)
        for (int s = 0; s < kSlotsPerEngine; ++s)
        {
            auto t = target (e, s);
            if (t.id == id && t.pos == pos)
                return slots[(size_t) e][(size_t) s];
        }
    return nullptr;
}

void MX300Processor::beginGesture (uint16_t id, uint8_t pos)
{
    if (auto* p = hostParameter (id, pos))
        p->beginChangeGesture();
}

void MX300Processor::endGesture (uint16_t id, uint8_t pos)
{
    if (auto* p = hostParameter (id, pos))
        p->endChangeGesture();
}

void MX300Processor::pushModelToHost()
{
    // set every slot to the device's value without sending it back to the device
    updatingFromModel = true;
    for (int e = 0; e < kEngines; ++e)
        for (int s = 0; s < kSlotsPerEngine; ++s)
        {
            auto t = target (e, s);
            float norm = 0;
            if (t.id != 0 && t.max > t.min)
                if (auto v = session().value (t.id, t.pos))
                    norm = (float) (*v - t.min) / (float) (t.max - t.min);
            auto* p = slots[(size_t) e][(size_t) s];
            if (std::abs (p->get() - norm) > 1.0e-6f)
                p->setValueNotifyingHost (norm);
        }
    updatingFromModel = false;
    if (auto b = session().global (mx300::kParamUnitBypass))
        syncUnitBypass (*b != 0);
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
}

void MX300Processor::syncUnitBypass (bool on)
{
    if (unitBypass->get() == on)
        return;
    updatingFromModel = true;
    unitBypass->setValueNotifyingHost (on ? 1.0f : 0.0f);   // hosts that show their bypass follow the unit
    updatingFromModel = false;
}

void MX300Processor::parameterValueChanged (int index, float v)
{
    if (index == unitBypass->getParameterIndex())
    {
        if (! updatingFromModel)
            unitBypassDirty = true;   // may be the audio thread: sent by the timer
        return;
    }
    if (updatingFromModel || index < 0 || index >= (int) hostValues.size())
        return;
    hostValues[(size_t) index] = v;   // may be the audio thread: just record it
    hostDirty[(size_t) index] = true;
}

void MX300Processor::timerCallback()
{
    if (session().state() != mx300::DeviceSession::State::Ready)
        return;

    for (int e = 0; e < kEngines; ++e)
        for (int s = 0; s < kSlotsPerEngine; ++s)
        {
            const auto i = (size_t) (e * kSlotsPerEngine + s);
            if (! hostDirty[i].exchange (false))
                continue;
            auto t = target (e, s);
            if (t.id == 0)
                continue;
            const int raw = juce::roundToInt ((float) t.min + hostValues[i].load() * (float) (t.max - t.min));
            session().setParam (t.id, t.pos, raw);
        }

    applyHostTempo();

    // a DAW project wants its saved edit buffer back on the unit
    std::optional<std::pair<juce::String, std::vector<mx300::ParamValue>>> restore;
    {
        const juce::ScopedLock l (pendingLock);
        restore.swap (pendingRestore);
    }
    if (restore && ! restore->second.empty())
    {
        auto current = session().editBuffer();
        auto wanted = restore->second;
        auto key = [] (auto& a, auto& b) { return std::tie (a.pos, a.id, a.value) < std::tie (b.pos, b.id, b.value); };
        std::sort (current.begin(), current.end(), key);
        std::sort (wanted.begin(), wanted.end(), key);
        if (current != wanted)
            session().writeEditBuffer (restore->first.toStdString(), restore->second);
    }

    if (unitBypassDirty.exchange (false))
        session().setParam (mx300::kParamUnitBypass, mx300::kPosSystem, unitBypass->get() ? 1 : 0);
}

void MX300Processor::applyHostTempo()
{
    const double bpm = bpmFromHost;
    if (! syncTempo || bpm <= 0)
        return;
    for (auto pos : session().enginePositions())
    {
        if (auto* tempo = mx300::tempoParam (session().engine (pos).type))
        {
            const int v = mx300::tempoValueForBpm (bpm, *tempo);
            if (session().value (tempo->id, pos) != v)
                session().setParam (tempo->id, pos, v);   // also a delay loaded later, or a tap on the unit
        }
    }
}

// ---------------------------------------------------------------- session listener (message thread)

void MX300Processor::stateChanged()
{
    rebuildSnapshot();
    pushModelToHost();
}

void MX300Processor::editBufferChanged()
{
    rebuildSnapshot();
    pushModelToHost();
}

void MX300Processor::programChanged() { rebuildSnapshot(); }

void MX300Processor::paramChanged (const mx300::ParamValue& v, bool)
{
    if (v.pos == mx300::kPosSystem)
    {
        if (v.id == mx300::kParamUnitBypass)
            syncUnitBypass (v.value != 0);   // the unit's bypass button, or the value read on connect
        return;
    }
    rebuildSnapshot();
    if (auto* p = hostParameter (v.id, v.pos))
    {
        auto range = p->getNormalisableRange();
        juce::ignoreUnused (range);
        for (int e = 0; e < kEngines; ++e)
            for (int s = 0; s < kSlotsPerEngine; ++s)
                if (slots[(size_t) e][(size_t) s] == p)
                {
                    auto t = target (e, s);
                    if (t.max > t.min)
                    {
                        updatingFromModel = true;
                        p->setValueNotifyingHost ((float) (v.value - t.min) / (float) (t.max - t.min));
                        updatingFromModel = false;
                    }
                }
    }
}

// ---------------------------------------------------------------- audio (none)

bool MX300Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet().isDisabled() && layouts.getMainOutputChannelSet().isDisabled())
        return true;
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
}

void MX300Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
            if (auto bpm = position->getBpm())
                bpmFromHost = *bpm;

    // audio passes through untouched; clear any outputs without a matching input
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* MX300Processor::createEditor() { return new MX300Editor (*this); }

// ---------------------------------------------------------------- state

bool MX300Processor::restoreOnLoad() const { return sharedHub->settings().getBoolValue ("restoreOnLoad", true); }
void MX300Processor::setRestoreOnLoad (bool on) { sharedHub->settings().setValue ("restoreOnLoad", on); }

void MX300Processor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree state ("MX300Control");
    state.setProperty ("version", 1, nullptr);
    state.setProperty ("tempoSync", syncTempo.load(), nullptr);
    {
        const juce::SpinLock::ScopedLockType l (snapshotLock);
        state.setProperty ("name", snapshot.editName, nullptr);
        juce::ValueTree params ("EditBuffer");
        for (auto& p : snapshot.editBuffer)
        {
            juce::ValueTree v ("P");
            v.setProperty ("id", (int) p.id, nullptr);
            v.setProperty ("pos", (int) p.pos, nullptr);
            v.setProperty ("value", p.value, nullptr);
            params.appendChild (v, nullptr);
        }
        state.appendChild (params, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void MX300Processor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType ("MX300Control"))
        return;
    syncTempo = (bool) state.getProperty ("tempoSync", false);
    std::vector<mx300::ParamValue> params;
    for (auto v : state.getChildWithName ("EditBuffer"))
        params.push_back ({ (uint16_t) (int) v["id"], (uint8_t) (int) v["pos"], (int) v["value"] });
    if (! restoreOnLoad() || params.empty())
        return;
    const juce::ScopedLock l (pendingLock);
    pendingRestore = std::make_pair (state["name"].toString(), params);   // applied once the unit is ready
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MX300Processor(); }
