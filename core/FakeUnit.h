#pragma once
// A simulated MX300 that answers the protocol the way the real unit was observed to (2026-10-29).
// Used by the tests and by the app's demo mode. Its presets are generated, not copied from a unit.

#include "Protocol.h"

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mx300
{
class FakeUnit
{
public:
    struct Preset
    {
        std::string name;
        std::vector<ParamValue> params;
    };

    FakeUnit();

    // Host -> unit. Replies go to toHost (synchronously).
    void receive (const Bytes& frame);
    std::function<void (const Bytes&)> toHost;

    // Front-panel actions (unit -> host notifications, as the real unit sends them in GUI mode).
    void turnKnob (uint16_t id, uint8_t pos, int value);
    void selectProgram (uint8_t slot);   // user bank, like the PROGRAM knob

    // Inspection for tests.
    const Preset& editBuffer() const { return edit; }
    const Preset& preset (uint8_t bank, uint8_t slot) const { return banks[bank == 0 ? 0 : 1][slot]; }
    bool guiMode() const { return gui; }
    int global (uint16_t id) const { auto it = globals.find (id); return it != globals.end() ? it->second : -1; }
    int framesReceived = 0;
    uint8_t whoAmIMode = 0;   // set != 0 to simulate OS-update mode
    bool silent = false;      // simulate an unplugged / unresponsive unit

    static Preset makePreset (const std::string& name, uint16_t routing, uint16_t type1, uint16_t type2);
    static std::vector<ParamValue> defaultsFor (uint8_t pos, uint16_t type);

private:
    void reply (const Bytes& f) { if (toHost) toHost (f); }
    void ack (uint8_t forProc, uint8_t code = 0);
    void sendPreset (uint8_t bank, uint8_t slot, const Preset& p);
    void setValue (Preset& p, uint16_t id, uint8_t pos, int value);

    std::array<std::array<Preset, kSlots>, 2> banks;
    Preset edit;
    bool editEdited = false;
    uint8_t curBank = kBankUser, curSlot = 0;
    bool gui = false;
    std::map<uint16_t, int> globals;
    // edit-buffer write in progress
    Preset incoming;
    uint8_t incomingBank = 0, incomingSlot = 0;
};
} // namespace mx300
