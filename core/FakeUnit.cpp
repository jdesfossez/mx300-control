#include "FakeUnit.h"

#include "ParamModel.h"

#include <algorithm>

namespace mx300
{
namespace
{
const Bytes kBulkIds = { 0x7E, 0x1A, 0x7F };

Bytes nameBytes (const std::string& n)
{
    std::string s = n.substr (0, kNameLength);
    s.resize (kNameLength, ' ');   // the unit space-pads names to 16 characters
    Bytes b (s.begin(), s.end());
    b.push_back (0);
    return b;
}

Bytes paramList (const std::vector<ParamValue>& ps)
{
    Bytes b = { (uint8_t) (ps.size() >> 8), (uint8_t) ps.size() };
    for (auto& p : ps)
    {
        b.insert (b.end(), { (uint8_t) (p.id >> 8), (uint8_t) p.id, p.pos });
        if (p.value < 0x80)
            b.push_back ((uint8_t) p.value);
        else if (p.value <= 0xFFFF)
            b.insert (b.end(), { 0x82, (uint8_t) (p.value >> 8), (uint8_t) p.value });
        else
            b.insert (b.end(), { 0x83, (uint8_t) (p.value >> 16), (uint8_t) (p.value >> 8), (uint8_t) p.value });
    }
    return b;
}

int defaultValue (const ParamInfo& p)
{
    if (p.display.kind == DisplayKind::Ms)
        return std::min (p.max, 500);   // tempo: 120 BPM
    return p.min + (p.max - p.min) / 2;
}
} // namespace

std::vector<ParamValue> FakeUnit::defaultsFor (uint8_t pos, uint16_t type)
{
    std::vector<ParamValue> out = { { kParamType, pos, type }, { kParamBypass, pos, 0 } };
    if (auto* t = findType (type))
        for (auto pid : t->params)
            if (auto* p = findParam (pid))
                out.push_back ({ pid, pos, defaultValue (*p) });
    out.push_back ({ kParamMix, pos, 99 });
    return out;
}

FakeUnit::Preset FakeUnit::makePreset (const std::string& name, uint16_t routing, uint16_t type1, uint16_t type2)
{
    Preset p { name, { { kParamRouting, kPosPreset, routing } } };
    auto e1 = defaultsFor (kPosEngine1, type1);
    p.params.insert (p.params.end(), e1.begin(), e1.end());
    if (isWholeRouting (routing))
    {
        // knob assignments: the type's first three parameters
        auto* t = findType (type1);
        for (size_t k = 0; k < kKnobAssign.size(); ++k)
            if (t && k < t->params.size())
                p.params.push_back ({ kKnobAssign[k], kPosEngine1, (kPosEngine1 << 16) | t->params[k] });
    }
    else
    {
        auto e2 = defaultsFor (kPosEngine2, type2);
        p.params.insert (p.params.end(), e2.begin(), e2.end());
    }
    return p;
}

FakeUnit::FakeUnit()
{
    // Generated demo content: walk through the routings and types so every editor layout shows up.
    auto whole = typesFor (kRoutingStereo);
    auto half = typesFor (0x0600);
    for (int bank = 0; bank < 2; ++bank)
        for (int i = 0; i < kSlots; ++i)
        {
            const uint16_t routing = (uint16_t) (i % 3 == 2 ? 0x0600 + (i / 3) % 4 : kRoutingStereo);
            Preset p;
            if (isWholeRouting (routing))
            {
                auto* t = whole[(size_t) (i + bank * 7) % whole.size()];
                p = makePreset (std::string (t->name) + " " + std::to_string (i + 1), routing, t->id, 0);
            }
            else
            {
                auto* a = half[(size_t) (i + bank * 5) % half.size()];
                auto* b = half[(size_t) (i * 7 + 3) % half.size()];
                p = makePreset (std::string (a->name).substr (0, 7) + "+" + std::string (b->name).substr (0, 7), routing, a->id, b->id);
            }
            banks[(size_t) bank][(size_t) i] = p;
        }
    edit = banks[1][0];
    globals = { { 0x3006, 0 }, { 0x3028, 0 }, { 0x302C, 0 }, { 0x302E, 1 }, { kParamUnitBypass, 0 } };
}

void FakeUnit::ack (uint8_t forProc, uint8_t code)
{
    reply (buildFrame (code == 0 ? proc::Ack : proc::Nack, { forProc, code }));
}

void FakeUnit::sendPreset (uint8_t bank, uint8_t slot, const Preset& p)
{
    Bytes start = { bank, slot };
    auto n = nameBytes (p.name);
    start.insert (start.end(), n.begin(), n.end());
    start.insert (start.end(), { (uint8_t) (bank == kBankEdit && editEdited ? 1 : 0), 0x02 });
    reply (buildFrame (proc::PresetStart, start));
    reply (buildFrame (proc::PresetParams, paramList (p.params)));
    reply (buildFrame (proc::PresetEnd));
}

void FakeUnit::setValue (Preset& p, uint16_t id, uint8_t pos, int value)
{
    if (id == kParamType)
    {
        // the unit replaces the engine's parameters with the new type's defaults
        auto first = std::find_if (p.params.begin(), p.params.end(), [&] (auto& v) { return v.pos == pos && v.id == kParamType; });
        if (first == p.params.end())
            return;
        const auto at = first - p.params.begin();
        p.params.erase (std::remove_if (p.params.begin(), p.params.end(),
                                        [&] (auto& v) { return v.pos == pos && std::find (kKnobAssign.begin(), kKnobAssign.end(), v.id) == kKnobAssign.end(); }),
                        p.params.end());
        auto d = defaultsFor (pos, (uint16_t) value);
        p.params.insert (p.params.begin() + std::min<long> (at, (long) p.params.size()), d.begin(), d.end());
        return;
    }
    if (id == kParamRouting)
    {
        auto old = std::find_if (p.params.begin(), p.params.end(), [] (auto& v) { return v.id == kParamRouting; });
        const bool wasWhole = old != p.params.end() && isWholeRouting ((uint16_t) old->value);
        if (wasWhole == isWholeRouting ((uint16_t) value))
        {
            if (old != p.params.end())
                old->value = value;
            return;
        }
        auto list = typesFor ((uint16_t) value);
        p = makePreset (p.name, (uint16_t) value, list[0]->id, list.size() > 16 ? list[16]->id : list[0]->id);
        return;
    }
    for (auto& v : p.params)
        if (v.id == id && v.pos == pos)
        {
            v.value = value;
            return;
        }
}

void FakeUnit::receive (const Bytes& frame)
{
    ++framesReceived;
    if (silent)
        return;
    auto m = parseFrame (frame);
    if (! m || ! m->checksumOk)
        return;
    auto& b = m->body;
    switch (m->procId)
    {
        case proc::WhoAmI:
        {
            Bytes body = { 0x00, 0x1A, 0x05, whoAmIMode, 0x02 };
            for (auto* s : { "Simulated Mx300", "Demo Unit" })
            {
                body.insert (body.end(), s, s + std::char_traits<char>::length (s));
                body.push_back (0);
            }
            reply (buildFrameWithIds (proc::WhoAmIReply, body, { 0x7F, 0x7F, 0x7F }));
            break;
        }
        case proc::RequestConfig:
            reply (buildFrame (proc::Config, { 0x00, 0x11, 0x00, 0x01, 0x02, curBank, curSlot, 0x00 }));
            break;
        case proc::RequestGlobals:
        {
            std::vector<ParamValue> g;
            for (auto& [id, v] : globals)
                g.push_back ({ id, kPosSystem, v });
            reply (buildFrame (proc::Globals, paramList (g)));
            break;
        }
        case proc::RequestNames:
        {
            if (b.empty())
                break;
            const uint8_t bank = b[0];
            Bytes body = { bank, (uint8_t) kSlots };
            for (auto& p : banks[bank == 0 ? 0 : 1])
            {
                auto n = nameBytes (p.name);
                body.insert (body.end(), n.begin(), n.end());
            }
            reply (buildFrame (proc::Names, body));
            break;
        }
        case proc::RequestPreset:
            if (b.size() >= 2)
                sendPreset (b[0], b[1], b[0] == kBankEdit ? edit : banks[b[0] == 0 ? 0 : 1][b[1] % kSlots]);
            break;
        case proc::ParamValue:   // SetParam: applied silently (the unit does not echo host writes)
            if (auto v = decodeParamValue (*m))
            {
                if (v->pos == kPosSystem)
                {
                    if (v->id == kParamGuiMode)
                        gui = v->value != 0;
                    else
                        globals[v->id] = v->value;
                }
                else
                {
                    setValue (edit, v->id, v->pos, v->value);
                    editEdited = true;
                }
            }
            break;
        case proc::RequestParam:
            if (b.size() >= 3)
            {
                const uint16_t id = (uint16_t) ((b[0] << 8) | b[1]);
                for (auto& v : edit.params)
                    if (v.id == id && v.pos == b[2])
                    {
                        int value = v.value;
                        if (id == kParamType)   // the unit answers with the menu index
                        {
                            auto list = typesFor ((uint16_t) [&] { for (auto& r : edit.params) if (r.id == kParamRouting) return r.value; return 0; }());
                            for (size_t k = 0; k < list.size(); ++k)
                                if (list[k]->id == v.value)
                                    value = (int) k;
                        }
                        reply (setParam (id, v.pos, value));
                    }
            }
            break;
        case proc::MovePreset:
            if (b.size() >= 6)
            {
                const uint8_t sb = b[0], ss = b[1], db = b[2], ds = b[3];
                if (db == kBankEdit)
                {
                    edit = banks[sb == 0 ? 0 : 1][ss % kSlots];
                    editEdited = false;
                    curBank = sb;
                    curSlot = ss;
                }
                else if (db == kBankUser && sb == kBankEdit)
                {
                    std::string name;
                    for (size_t i = 4; i < b.size() && b[i] != 0; ++i)
                        name.push_back ((char) b[i]);
                    edit.name = name.empty() ? edit.name : name;
                    banks[1][ds % kSlots] = edit;
                    editEdited = false;
                    curBank = kBankUser;
                    curSlot = ds;
                }
                ack (proc::MovePreset);
            }
            break;
        case proc::SetName:
            if (b.size() >= 3)
            {
                std::string name;
                for (size_t i = 2; i < b.size() && b[i] != 0; ++i)
                    name.push_back ((char) b[i]);
                banks[b[0] == 0 ? 0 : 1][b[1] % kSlots].name = name;
                ack (proc::SetName);
            }
            break;
        case proc::PresetStart:
            if (auto s = decodePresetStart (*m))
            {
                // the MX300 NACKs MX-Edit's 4-byte tail (00 02 00 02) but still takes the data
                size_t nul = 2;
                while (nul < b.size() && b[nul] != 0)
                    ++nul;
                const size_t tail = b.size() - (nul + 1);
                incoming = { s->name, {} };
                incomingBank = s->bank;
                incomingSlot = s->slot;
                tail == 2 ? ack (proc::PresetStart) : ack (proc::PresetStart, 9);
            }
            break;
        case proc::PresetParams:
            if (auto ps = decodeParamList (*m))
            {
                incoming.params = *ps;
                ack (proc::PresetParams);
            }
            break;
        case proc::PresetEnd:
            if (incomingBank == kBankEdit)
            {
                edit = incoming;
                editEdited = false;
            }
            else
            {
                banks[incomingBank == 0 ? 0 : 1][incomingSlot % kSlots] = incoming;
            }
            ack (proc::PresetEnd);
            break;
        case proc::RequestBulkDump:
        {
            reply (buildFrameWithIds (proc::BulkStart, { 0x01, 0x2B, 0x00, 0x00, 0x28, 0xF2, 0x63 }, kBulkIds));
            for (uint8_t s = 0; s < kSlots; ++s)
                sendPreset (kBankUser, s, banks[1][s]);
            std::vector<ParamValue> g;
            for (auto& [id, v] : globals)
                g.push_back ({ id, kPosSystem, v });
            reply (buildFrame (proc::Globals, paramList (g)));
            reply (buildFrameWithIds (proc::BulkEnd, {}, kBulkIds));
            break;
        }
        case proc::BulkStart:
            reply (buildFrameWithIds (proc::Ack, { proc::BulkStart, 0 }, kBulkIds));
            break;
        case proc::BulkEnd:
            reply (buildFrameWithIds (proc::Ack, { proc::BulkEnd, 0 }, kBulkIds));
            break;
        case proc::Globals:
            if (auto g = decodeParamList (*m))
                for (auto& v : *g)
                    globals[v.id] = v.value;
            ack (proc::Globals);
            break;
        default:
            break;   // firmware messages and unknown procs are ignored
    }
}

void FakeUnit::turnKnob (uint16_t id, uint8_t pos, int value)
{
    if (pos == kPosSystem)
        globals[id] = value;   // system menu / global bypass: not part of the program
    else
    {
        setValue (edit, id, pos, value);
        editEdited = true;
    }
    if (gui)
        reply (setParam (id, pos, value));
}

void FakeUnit::selectProgram (uint8_t slot)
{
    edit = banks[1][slot % kSlots];
    editEdited = false;
    curBank = kBankUser;
    curSlot = slot;
    if (gui)
        reply (buildFrame (proc::Notify, { 0x03, kBankUser, slot, kBankEdit, 0x00 }));
}
} // namespace mx300
