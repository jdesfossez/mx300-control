#include "Protocol.h"

#include <algorithm>

namespace mx300
{
namespace
{
constexpr uint8_t kManufacturer = 0x06;
const Bytes kIds = { 0x00, 0x1A, 0x05 };
const Bytes kWhoAmIIds = { 0x7F, 0x1A, 0x7F };

void appendI16 (Bytes& b, int v)
{
    b.push_back ((uint8_t) ((v >> 8) & 0xFF));
    b.push_back ((uint8_t) (v & 0xFF));
}

void appendMag7 (Bytes& b, int v)
{
    if (v >= 0 && v < 0x80)
        b.push_back ((uint8_t) v);
    else if (v >= 0 && v <= 0xFFFF)
        b.insert (b.end(), { 0x82, (uint8_t) (v >> 8), (uint8_t) v });
    else
        b.insert (b.end(), { 0x83, (uint8_t) (v >> 16), (uint8_t) (v >> 8), (uint8_t) v });
}

void appendName (Bytes& b, const std::string& s)
{
    for (size_t i = 0; i < s.size() && i < kNameLength; ++i)
    {
        const auto c = (uint8_t) s[i];
        b.push_back (c >= 0x20 && c < 0x7F ? c : '?');
    }
    b.push_back (0);
}

Bytes build (uint8_t procId, const Bytes& body, const Bytes& ids)
{
    Bytes f = { 0xF0, kManufacturer };
    f.insert (f.end(), ids.begin(), ids.end());
    f.push_back (procId);
    auto packed = super7Pack (body);
    f.insert (f.end(), packed.begin(), packed.end());
    f.push_back (checksum (f));
    f.push_back (0xF7);
    return f;
}

struct Reader
{
    const Bytes& b;
    size_t i = 0;
    bool ok = true;

    size_t left() const { return i < b.size() ? b.size() - i : 0; }
    int byte()
    {
        if (i >= b.size()) { ok = false; return 0; }
        return b[i++];
    }
    int u16() { const int hi = byte(); return (hi << 8) | byte(); }
    int mag7()
    {
        const int v = byte();
        if (v < 0x80)
            return v;
        int r = 0;
        for (int n = v & 0x7F; n > 0; --n)
            r = (r << 8) | byte();
        return r;
    }
    std::string str()
    {
        std::string s;
        while (i < b.size() && b[i] != 0)
            s.push_back ((char) b[i++]);
        if (i < b.size())
            ++i;
        else
            ok = false;
        return s;
    }
};
} // namespace

Bytes super7Pack (const Bytes& data)
{
    Bytes out;
    for (size_t i = 0; i < data.size(); i += 7)
    {
        uint8_t hi = 0;
        const size_t n = std::min<size_t> (7, data.size() - i);
        for (size_t k = 0; k < n; ++k)
            if (data[i + k] & 0x80)
                hi |= (uint8_t) (0x40 >> k);
        out.push_back (hi);
        for (size_t k = 0; k < n; ++k)
            out.push_back (data[i + k] & 0x7F);
    }
    return out;
}

Bytes super7Unpack (const Bytes& data)
{
    Bytes out;
    size_t i = 0;
    while (i < data.size())
    {
        const uint8_t hi = data[i];
        const size_t n = std::min<size_t> (7, data.size() - i - 1);
        for (size_t k = 0; k < n; ++k)
            out.push_back ((uint8_t) (data[i + 1 + k] | ((hi & (0x40 >> k)) ? 0x80 : 0)));
        i += 1 + n;
    }
    return out;
}

uint8_t checksum (const Bytes& f)
{
    uint8_t cs = 0;
    for (size_t i = 1; i < f.size(); ++i)
        cs ^= f[i];
    return cs & 0x7F;
}

Bytes buildFrame (uint8_t procId, const Bytes& body) { return build (procId, body, kIds); }
Bytes buildFrameWithIds (uint8_t procId, const Bytes& body, const Bytes& ids) { return build (procId, body, ids); }

std::optional<Message> parseFrame (const Bytes& f)
{
    if (f.size() < 8 || f.front() != 0xF0 || f.back() != 0xF7 || f[1] != kManufacturer)
        return std::nullopt;
    Message m;
    m.procId = f[5];
    Bytes head (f.begin(), f.end() - 2);
    m.checksumOk = checksum (head) == f[f.size() - 2];
    m.body = super7Unpack (Bytes (f.begin() + 6, f.end() - 2));
    return m;
}

Bytes reqWhoAmI() { return build (proc::WhoAmI, { 0x7F, 0x7F, 0x7F }, kWhoAmIIds); }
Bytes reqConfig() { return buildFrame (proc::RequestConfig); }
Bytes reqGlobals() { return buildFrame (proc::RequestGlobals); }
Bytes reqNames (uint8_t bank) { return buildFrame (proc::RequestNames, { bank }); }
Bytes reqPreset (uint8_t bank, uint8_t slot) { return buildFrame (proc::RequestPreset, { bank, slot }); }
Bytes reqBulkDump() { return buildFrame (proc::RequestBulkDump, { 0 }); }

Bytes reqParam (uint16_t id, uint8_t pos)
{
    Bytes b;
    appendI16 (b, id);
    b.push_back (pos);
    return buildFrame (proc::RequestParam, b);
}

Bytes setParam (uint16_t id, uint8_t pos, int value)
{
    Bytes b;
    appendI16 (b, id);
    b.push_back (pos);
    appendMag7 (b, value);
    return buildFrame (proc::ParamValue, b);
}

Bytes guiMode (bool on) { return setParam (0x300A, 0, on ? 1 : 0); }

Bytes movePreset (uint8_t srcBank, uint8_t srcSlot, uint8_t dstBank, uint8_t dstSlot, const std::string* name)
{
    Bytes b = { srcBank, srcSlot, dstBank, dstSlot };
    if (name)
        appendName (b, *name);
    else
        b.push_back (0);
    b.push_back (1);
    return buildFrame (proc::MovePreset, b);
}

Bytes loadPreset (uint8_t bank, uint8_t slot) { return movePreset (bank, slot, kBankEdit, 0, nullptr); }
Bytes storePreset (uint8_t userSlot, const std::string& name) { return movePreset (kBankEdit, 0, kBankUser, userSlot, &name); }

Bytes renamePreset (uint8_t userSlot, const std::string& name)
{
    Bytes b = { kBankUser, userSlot };
    appendName (b, name);
    return buildFrame (proc::SetName, b);
}

std::vector<Bytes> presetMessages (const std::string& name, const std::vector<ParamValue>& params, uint8_t bank, uint8_t slot)
{
    Bytes start = { bank, slot };
    appendName (start, name);
    start.insert (start.end(), { 0x00, 0x02 });   // the MX300 NACKs MX-Edit's 00 02 00 02
    Bytes body;
    appendI16 (body, (int) params.size());
    for (auto& p : params)
    {
        appendI16 (body, p.id);
        body.push_back (p.pos);
        appendMag7 (body, p.value);
    }
    return { buildFrame (proc::PresetStart, start), buildFrame (proc::PresetParams, body), buildFrame (proc::PresetEnd) };
}

std::optional<WhoAmIInfo> decodeWhoAmI (const Message& m)
{
    if (m.procId != proc::WhoAmIReply || m.body.size() < 5)
        return std::nullopt;
    Reader r { m.body };
    WhoAmIInfo w;
    for (int i = 0; i < 5; ++i)
    {
        const int b = r.byte();
        if (i == 3)
            w.mode = (uint8_t) b;
    }
    w.model = r.str();
    w.deviceName = r.str();
    return w;
}

std::optional<ConfigInfo> decodeConfig (const Message& m)
{
    if (m.procId != proc::Config || m.body.size() < 7)
        return std::nullopt;
    return ConfigInfo { m.body[5], m.body[6] };
}

std::optional<NamesInfo> decodeNames (const Message& m)
{
    if (m.procId != proc::Names)
        return std::nullopt;
    Reader r { m.body };
    NamesInfo n;
    n.bank = (uint8_t) r.byte();
    const int count = r.byte();
    for (int i = 0; i < count && r.left() > 0; ++i)
        n.names.push_back (trimName (r.str()));
    if (! r.ok && n.names.empty())
        return std::nullopt;
    return n;
}

std::optional<PresetStartInfo> decodePresetStart (const Message& m)
{
    if (m.procId != proc::PresetStart)
        return std::nullopt;
    Reader r { m.body };
    PresetStartInfo p;
    p.bank = (uint8_t) r.byte();
    p.slot = (uint8_t) r.byte();
    p.name = trimName (r.str());
    p.edited = r.left() > 0 && r.byte() != 0;
    if (! r.ok)
        return std::nullopt;
    return p;
}

std::optional<std::vector<ParamValue>> decodeParamList (const Message& m)
{
    if (m.procId != proc::PresetParams && m.procId != proc::Globals)
        return std::nullopt;
    Reader r { m.body };
    const int n = r.u16();
    std::vector<ParamValue> out;
    for (int i = 0; i < n && r.ok; ++i)
    {
        ParamValue p;
        p.id = (uint16_t) r.u16();
        p.pos = (uint8_t) r.byte();
        p.value = r.mag7();
        if (r.ok)
            out.push_back (p);
    }
    if (! r.ok)
        return std::nullopt;
    return out;
}

std::optional<ParamValue> decodeParamValue (const Message& m)
{
    if (m.procId != proc::ParamValue)
        return std::nullopt;
    Reader r { m.body };
    ParamValue p;
    p.id = (uint16_t) r.u16();
    p.pos = (uint8_t) r.byte();
    p.value = r.mag7();
    if (! r.ok)
        return std::nullopt;
    return p;
}

std::optional<AckInfo> decodeAck (const Message& m)
{
    if ((m.procId != proc::Ack && m.procId != proc::Nack) || m.body.size() < 2)
        return std::nullopt;
    return AckInfo { m.body[0], m.body[1] };
}

std::optional<NotifyInfo> decodeNotify (const Message& m)
{
    if (m.procId != proc::Notify || m.body.empty())
        return std::nullopt;
    return NotifyInfo { m.body[0], Bytes (m.body.begin() + 1, m.body.end()) };
}

std::string trimName (const std::string& s)
{
    auto end = s.find_last_not_of (' ');
    return end == std::string::npos ? std::string() : s.substr (0, end + 1);
}

std::vector<Bytes> splitSysex (const Bytes& stream)
{
    std::vector<Bytes> out;
    Bytes cur;
    for (auto b : stream)
    {
        if (b == 0xF0)
            cur.clear();
        cur.push_back (b);
        if (b == 0xF7)
        {
            if (cur.front() == 0xF0)
                out.push_back (cur);
            cur.clear();
        }
    }
    return out;
}
} // namespace mx300
