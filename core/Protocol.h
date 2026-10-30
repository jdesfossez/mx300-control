#pragma once
// MX300 SysEx protocol: framing, Super7 packing, request builders, reply decoding.
// Spec: docs/PROTOCOL.md. Frames include the leading F0 and trailing F7.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mx300
{
using Bytes = std::vector<uint8_t>;

namespace proc
{
constexpr uint8_t WhoAmI = 0x01, WhoAmIReply = 0x02, RequestConfig = 0x08, Config = 0x09,
                  RequestGlobals = 0x10, Globals = 0x11, RequestBulkDump = 0x18, BulkStart = 0x19, BulkEnd = 0x1B,
                  RequestNames = 0x21, Names = 0x22, SetName = 0x29, RequestPreset = 0x2A, PresetStart = 0x2B,
                  PresetEnd = 0x2C, PresetParams = 0x2D, MovePreset = 0x39, RequestParam = 0x40, ParamValue = 0x41,
                  Notify = 0x70, Ack = 0x7E, Nack = 0x7F;
// 0x78 / 0x7A / 0x7B are firmware-update messages. This code never builds them.
} // namespace proc

constexpr uint8_t kBankFactory = 0, kBankUser = 1, kBankEdit = 4;
constexpr int kSlots = 99;
constexpr size_t kNameLength = 16;

struct ParamValue
{
    uint16_t id;
    uint8_t pos;
    int value;
    bool operator== (const ParamValue& o) const { return id == o.id && pos == o.pos && value == o.value; }
};

// ---- low level
Bytes super7Pack (const Bytes& data);
Bytes super7Unpack (const Bytes& data);
uint8_t checksum (const Bytes& frameWithoutChecksum);
Bytes buildFrame (uint8_t procId, const Bytes& body = {});
Bytes buildFrameWithIds (uint8_t procId, const Bytes& body, const Bytes& ids);   // e.g. 7E 1A 7F for bulk start/end

struct Message
{
    uint8_t procId = 0;
    bool checksumOk = false;
    Bytes body;   // unpacked
};
std::optional<Message> parseFrame (const Bytes& frame);

// ---- requests (host -> unit)
Bytes reqWhoAmI();
Bytes reqConfig();
Bytes reqGlobals();
Bytes reqNames (uint8_t bank);
Bytes reqPreset (uint8_t bank, uint8_t slot);
Bytes reqParam (uint16_t id, uint8_t pos);
Bytes reqBulkDump();
Bytes setParam (uint16_t id, uint8_t pos, int value);
Bytes guiMode (bool on);
Bytes movePreset (uint8_t srcBank, uint8_t srcSlot, uint8_t dstBank, uint8_t dstSlot, const std::string* name);
Bytes loadPreset (uint8_t bank, uint8_t slot);               // -> edit buffer
Bytes storePreset (uint8_t userSlot, const std::string& name); // edit buffer -> user slot
Bytes renamePreset (uint8_t userSlot, const std::string& name);
// Whole preset into bank/slot (default: edit buffer): PresetStart, PresetParams, PresetEnd.
std::vector<Bytes> presetMessages (const std::string& name, const std::vector<ParamValue>& params,
                                   uint8_t bank = kBankEdit, uint8_t slot = 0);

// ---- decoding (unit -> host)
struct WhoAmIInfo { uint8_t mode = 0; std::string model, deviceName; };
struct ConfigInfo { uint8_t bank = 0, slot = 0; };
struct NamesInfo { uint8_t bank = 0; std::vector<std::string> names; };
struct PresetStartInfo { uint8_t bank = 0, slot = 0; std::string name; bool edited = false; };
struct AckInfo { uint8_t forProc = 0, code = 0; };
struct NotifyInfo { uint8_t sub = 0; Bytes data; };

std::optional<WhoAmIInfo> decodeWhoAmI (const Message&);
std::optional<ConfigInfo> decodeConfig (const Message&);
std::optional<NamesInfo> decodeNames (const Message&);
std::optional<PresetStartInfo> decodePresetStart (const Message&);
std::optional<std::vector<ParamValue>> decodeParamList (const Message&);   // 2D and 11
std::optional<ParamValue> decodeParamValue (const Message&);               // 41
std::optional<AckInfo> decodeAck (const Message&);                         // 7E / 7F
std::optional<NotifyInfo> decodeNotify (const Message&);

std::string trimName (const std::string& padded);
std::vector<Bytes> splitSysex (const Bytes& stream);
} // namespace mx300
