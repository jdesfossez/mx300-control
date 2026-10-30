#include "DeviceSession.h"

#include <algorithm>
#include <memory>

namespace mx300
{
namespace
{
constexpr double kParamIntervalMs = 2.0;   // the unit accepted ~680 SetParam/s; stay well below
constexpr int kMaxParamsPerTick = 8;
constexpr double kRestoreSpacingMs = 25.0; // proven pacing for a bulk restore
} // namespace

DeviceSession::DeviceSession (Transport& t) : transport (t) {}

void DeviceSession::removeListener (Listener* l)
{
    listeners.erase (std::remove (listeners.begin(), listeners.end(), l), listeners.end());
}

// ---------------------------------------------------------------- connection

void DeviceSession::connect (double nowMs)
{
    now = nowMs;
    queue.clear();
    stepActive = false;
    pendingParams.clear();
    pendingOrder.clear();
    setState (State::Connecting);

    Step who { reqWhoAmI(), proc::WhoAmIReply };
    who.done = [this] (bool ok)
    {
        if (! ok)
        {
            queue.clear();
            setState (State::NoResponse);
        }
        else if (info.mode != 0)
        {
            queue.clear();   // OS-update / boot mode: never talk to it
            setState (State::UpdateMode);
        }
    };
    enqueue (who);
    enqueue ({ reqConfig(), proc::Config });
    Step gui { guiMode (true) };
    gui.delayAfterMs = 20;
    enqueue (gui);
    enqueue ({ reqNames (kBankUser), proc::Names, 0, 3000 });
    enqueue ({ reqNames (kBankFactory), proc::Names, 0, 3000 });
    enqueue ({ reqGlobals(), proc::Globals });
    Step preset { reqPreset (kBankEdit, 0), proc::PresetEnd, 0, 3000 };
    preset.done = [this] (bool ok) { setState (ok ? State::Ready : State::NoResponse); };
    enqueue (preset);
    startNext (nowMs);
}

void DeviceSession::disconnect()
{
    queue.clear();
    stepActive = false;
    if (currentState == State::Ready || currentState == State::Connecting)
        send (guiMode (false));
    setState (State::Offline);
}

void DeviceSession::setState (State s)
{
    if (s == currentState)
        return;
    currentState = s;
    each ([] (Listener& l) { l.stateChanged(); });
}

void DeviceSession::send (const Bytes& f)
{
    transport.sendSysex (f);
    lastSendMs = now;
    each ([] (Listener& l) { l.trafficSeen (true); });
}

// ---------------------------------------------------------------- command queue

void DeviceSession::enqueue (Step s)
{
    queue.push_back (std::move (s));
    if (! stepActive)
        startNext (now);
}

void DeviceSession::startNext (double nowMs)
{
    if (stepActive || queue.empty())
        return;
    stepActive = true;
    stepStartedMs = nowMs;
    auto& s = queue.front();
    if (! s.frame.empty())
        send (s.frame);
    if (s.expect == 0 && s.ackFor == 0 && ! s.waitUntil && s.delayAfterMs <= 0)
        completeStep (true, nowMs);
}

void DeviceSession::completeStep (bool ok, double nowMs)
{
    if (! stepActive || queue.empty())
        return;
    auto done = std::move (queue.front().done);
    queue.pop_front();
    stepActive = false;
    if (done)
        done (ok);
    startNext (nowMs);
}

void DeviceSession::tick (double nowMs)
{
    now = nowMs;
    if (stepActive && ! queue.empty())
    {
        auto& s = queue.front();
        const double elapsed = nowMs - stepStartedMs;
        if (s.waitUntil)
        {
            if (s.waitUntil())
                completeStep (true, nowMs);
            else if (elapsed > s.timeoutMs)
                completeStep (false, nowMs);
        }
        else if (s.expect == 0 && s.ackFor == 0)
        {
            if (elapsed >= s.delayAfterMs)
                completeStep (true, nowMs);
        }
        else if (elapsed > s.timeoutMs)
        {
            completeStep (false, nowMs);
        }
    }
    flushParams (nowMs);
}

// ---------------------------------------------------------------- incoming

void DeviceSession::handleFrame (const Bytes& frame, double nowMs)
{
    now = nowMs;
    auto msg = parseFrame (frame);
    if (! msg || ! msg->checksumOk)
        return;
    each ([] (Listener& l) { l.trafficSeen (false); });
    const auto p = msg->procId;

    if (capturingDump)
        dumpFrames.push_back (frame);

    switch (p)
    {
        case proc::WhoAmIReply:
            if (auto w = decodeWhoAmI (*msg))
                info = *w;
            break;
        case proc::Config:
            if (auto c = decodeConfig (*msg))
            {
                current = std::make_pair (c->bank, c->slot);
                each ([] (Listener& l) { l.programChanged(); });
            }
            break;
        case proc::Names:
            if (auto n = decodeNames (*msg))
            {
                bankNames[n->bank == kBankFactory ? 0 : 1] = n->names;
                const auto bank = n->bank;
                each ([bank] (Listener& l) { l.namesChanged (bank); });
            }
            break;
        case proc::Globals:
            if (auto g = decodeParamList (*msg))
                for (auto& v : *g)
                {
                    globalParams[v.id] = v.value;
                    each ([&] (Listener& l) { l.paramChanged (v, true); });
                }
            break;
        case proc::PresetStart:
            incomingStart = decodePresetStart (*msg);
            incomingParams.clear();
            break;
        case proc::PresetParams:
            if (auto ps = decodeParamList (*msg))
                incomingParams = *ps;
            break;
        case proc::PresetEnd:
            if (incomingStart && incomingStart->bank == kBankEdit && ! capturingDump)
                applyEditBuffer();
            incomingStart.reset();
            break;
        case proc::ParamValue:
            if (auto v = decodeParamValue (*msg))
            {
                if (v->pos == kPosSystem)
                {
                    globalParams[v->id] = v->value;
                    each ([&] (Listener& l) { l.paramChanged (*v, true); });
                    break;
                }
                auto it = std::find_if (edit.begin(), edit.end(), [&] (auto& e) { return e.id == v->id && e.pos == v->pos; });
                if (it != edit.end())
                    it->value = v->value;
                else
                    edit.push_back (*v);
                if (v->id == kParamType || v->id == kParamRouting)
                    requestEditBuffer();   // the parameter set changes with the type
                if (! editBufferEdited)
                {
                    editBufferEdited = true;
                    each ([] (Listener& l) { l.programChanged(); });
                }
                each ([&] (Listener& l) { l.paramChanged (*v, true); });
            }
            break;
        case proc::Notify:
            if (auto n = decodeNotify (*msg))
            {
                // 03 srcBank srcSlot dstBank dstSlot: a preset was moved (front-panel program change or store)
                // 00 bank slot: a preset was loaded (documented by MX-Edit, not seen on the MX300 yet)
                if (n->sub == 3 && n->data.size() >= 4)
                {
                    if (n->data[2] == kBankEdit)
                    {
                        current = std::make_pair (n->data[0], n->data[1]);
                        requestEditBuffer();
                    }
                    else if (n->data[2] == kBankUser)
                    {
                        enqueue ({ reqNames (kBankUser), proc::Names, 0, 3000 });
                    }
                }
                else if (n->sub == 0 && n->data.size() >= 2)
                {
                    current = std::make_pair (n->data[0], n->data[1]);
                    requestEditBuffer();
                }
            }
            break;
        case proc::Ack:
        case proc::Nack:
            p == proc::Ack ? ++acksSeen : ++nacksSeen;
            break;
        default:
            break;
    }

    // does this complete the active step?
    if (stepActive && ! queue.empty())
    {
        auto& s = queue.front();
        if (s.expect != 0 && p == s.expect)
            completeStep (true, nowMs);
        else if (s.ackFor != 0 && (p == proc::Ack || p == proc::Nack))
        {
            if (auto a = decodeAck (*msg); a && a->forProc == s.ackFor)
                completeStep (p == proc::Ack && a->code == 0, nowMs);
        }
    }
}

void DeviceSession::applyEditBuffer()
{
    edit = incomingParams;
    editBufferName = incomingStart->name;
    editBufferEdited = incomingStart->edited;
    each ([] (Listener& l) { l.editBufferChanged(); });
    each ([] (Listener& l) { l.programChanged(); });
}

void DeviceSession::requestEditBuffer()
{
    // avoid stacking several reloads (e.g. fast program knob turns)
    for (auto& s : queue)
        if (s.expect == proc::PresetEnd && s.frame == reqPreset (kBankEdit, 0))
            return;
    enqueue ({ reqPreset (kBankEdit, 0), proc::PresetEnd, 0, 3000 });
}

// ---------------------------------------------------------------- model

std::optional<int> DeviceSession::value (uint16_t id, uint8_t pos) const
{
    for (auto& e : edit)
        if (e.id == id && e.pos == pos)
            return e.value;
    return std::nullopt;
}

uint16_t DeviceSession::routing() const { return (uint16_t) value (kParamRouting, kPosPreset).value_or (kRoutingStereo); }

EngineState DeviceSession::engine (uint8_t pos) const
{
    EngineState e;
    e.routing = routing();
    for (auto& v : edit)
        if (v.pos == pos)
        {
            e.values[v.id] = v.value;
            if (v.id == kParamType)
                e.type = (uint16_t) v.value;
        }
    return e;
}

std::vector<uint8_t> DeviceSession::enginePositions() const
{
    std::vector<uint8_t> out;
    for (auto pos : { kPosEngine1, kPosEngine2 })
        if (value (kParamType, pos))
            out.push_back (pos);
    return out;
}

// ---------------------------------------------------------------- operations

void DeviceSession::finished (const std::string& what, bool ok)
{
    each ([&] (Listener& l) { l.operationFinished (what, ok); });
}

void DeviceSession::setParam (uint16_t id, uint8_t pos, int v)
{
    if (pos == kPosSystem)
    {
        // system parameters are not part of the program: no edited flag
        if (auto g = globalParams.find (id); g != globalParams.end() && g->second == v)
            return;
        globalParams[id] = v;
        const auto key = std::make_pair (id, pos);
        if (! pendingParams.count (key))
            pendingOrder.push_back (key);
        pendingParams[key] = v;
        each ([&] (Listener& l) { l.paramChanged ({ id, pos, v }, false); });
        return;
    }
    auto it = std::find_if (edit.begin(), edit.end(), [&] (auto& e) { return e.id == id && e.pos == pos; });
    if (it != edit.end())
    {
        if (it->value == v)
            return;
        it->value = v;
    }
    const auto key = std::make_pair (id, pos);
    if (! pendingParams.count (key))
        pendingOrder.push_back (key);
    pendingParams[key] = v;
    if (! editBufferEdited)
    {
        editBufferEdited = true;
        each ([] (Listener& l) { l.programChanged(); });
    }
    each ([&] (Listener& l) { l.paramChanged ({ id, pos, v }, false); });
}

void DeviceSession::flushParams (double nowMs)
{
    if (currentState != State::Ready)
        return;
    for (int n = 0; n < kMaxParamsPerTick && ! pendingOrder.empty(); ++n)
    {
        if (nowMs - lastSendMs < kParamIntervalMs && n == 0 && lastSendMs > 0)
            break;
        const auto key = pendingOrder.front();
        pendingOrder.pop_front();
        auto it = pendingParams.find (key);
        if (it == pendingParams.end())
            continue;
        send (mx300::setParam (key.first, key.second, it->second));
        pendingParams.erase (it);
    }
}

void DeviceSession::setType (uint8_t pos, uint16_t typeId)
{
    send (mx300::setParam (kParamType, pos, typeId));
    editBufferEdited = true;
    requestEditBuffer();   // the unit fills in the new type's defaults
}

void DeviceSession::setRouting (uint16_t r)
{
    send (mx300::setParam (kParamRouting, kPosPreset, r));
    editBufferEdited = true;
    requestEditBuffer();
}

void DeviceSession::loadPreset (uint8_t bank, uint8_t slot)
{
    pendingParams.clear();
    pendingOrder.clear();
    Step s;
    s.frame = mx300::loadPreset (bank, slot);
    s.ackFor = proc::MovePreset;
    s.done = [this, bank, slot] (bool ok)
    {
        if (ok)
        {
            current = std::make_pair (bank, slot);
            requestEditBuffer();
        }
        finished ("load", ok);
    };
    enqueue (s);
}

void DeviceSession::store (uint8_t userSlot, const std::string& name)
{
    Step s;
    s.frame = storePreset (userSlot, name);
    s.ackFor = proc::MovePreset;
    s.done = [this, userSlot, name] (bool ok)
    {
        if (ok)
        {
            current = std::make_pair (kBankUser, userSlot);
            editBufferName = name.substr (0, kNameLength);
            editBufferEdited = false;
            each ([] (Listener& l) { l.programChanged(); });
            enqueue ({ reqNames (kBankUser), proc::Names, 0, 3000 });
        }
        finished ("store", ok);
    };
    enqueue (s);
}

void DeviceSession::rename (uint8_t userSlot, const std::string& name)
{
    Step s;
    s.frame = renamePreset (userSlot, name);
    s.ackFor = proc::SetName;
    s.done = [this, userSlot, name] (bool ok)
    {
        if (ok && current && current->first == kBankUser && current->second == userSlot)
        {
            editBufferName = name.substr (0, kNameLength);
            each ([] (Listener& l) { l.programChanged(); });
        }
        if (ok)
            enqueue ({ reqNames (kBankUser), proc::Names, 0, 3000 });
        finished ("rename", ok);
    };
    enqueue (s);
}

void DeviceSession::writeEditBuffer (const std::string& name, const std::vector<ParamValue>& params)
{
    pendingParams.clear();
    pendingOrder.clear();
    auto frames = presetMessages (name, params);
    auto ok = std::make_shared<bool> (true);
    const uint8_t procs[] = { proc::PresetStart, proc::PresetParams, proc::PresetEnd };
    for (size_t i = 0; i < frames.size(); ++i)
    {
        Step s;
        s.frame = frames[i];
        s.ackFor = procs[i];
        s.timeoutMs = 3000;
        const bool last = i + 1 == frames.size();
        s.done = [this, ok, last] (bool stepOk)
        {
            *ok = *ok && stepOk;
            if (last)
            {
                requestEditBuffer();
                finished ("write", *ok);
            }
        };
        enqueue (s);
    }
}

void DeviceSession::refreshEditBuffer() { requestEditBuffer(); }

void DeviceSession::backup (std::function<void (std::vector<Bytes>)> done)
{
    Step s;
    s.frame = reqBulkDump();
    s.expect = proc::BulkEnd;
    s.timeoutMs = 15000;
    capturingDump = true;
    dumpFrames.clear();
    s.done = [this, done] (bool ok)
    {
        capturingDump = false;
        auto frames = std::move (dumpFrames);
        dumpFrames.clear();
        if (! ok)
            frames.clear();
        finished ("backup", ok);
        if (done)
            done (frames);
    };
    enqueue (s);
}

void DeviceSession::restore (const std::vector<Bytes>& frames)
{
    pendingParams.clear();
    pendingOrder.clear();
    auto acksBefore = std::make_shared<int> (0);
    Step first;
    first.waitUntil = [] { return true; };
    first.done = [this, acksBefore] (bool) { *acksBefore = acksSeen + nacksSeen; };
    enqueue (first);
    for (auto& f : frames)
    {
        Step s;
        s.frame = f;
        s.delayAfterMs = kRestoreSpacingMs;
        enqueue (s);
    }
    const int expected = (int) frames.size();
    Step wait;
    wait.timeoutMs = 10000;
    wait.waitUntil = [this, acksBefore, expected] { return acksSeen + nacksSeen - *acksBefore >= expected; };
    auto nacksBefore = std::make_shared<int> (nacksSeen);
    wait.done = [this, nacksBefore] (bool ok)
    {
        enqueue ({ reqNames (kBankUser), proc::Names, 0, 3000 });
        requestEditBuffer();
        finished ("restore", ok && nacksSeen == *nacksBefore);
    };
    enqueue (wait);
}
} // namespace mx300
