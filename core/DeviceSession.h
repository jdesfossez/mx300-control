#pragma once
// Connection state machine and device model for one MX300.
// Pure C++: the owner provides the transport (send SysEx), feeds received frames into handleFrame(),
// and calls tick() regularly with a monotonic clock. All calls must come from one thread.

#include "ParamModel.h"
#include "Protocol.h"

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mx300
{
class Transport
{
public:
    virtual ~Transport() = default;
    virtual void sendSysex (const Bytes& frame) = 0;
};

class DeviceSession
{
public:
    enum class State { Offline, Connecting, Ready, UpdateMode, NoResponse };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void stateChanged() {}
        virtual void namesChanged (uint8_t /*bank*/) {}
        virtual void editBufferChanged() {}                                  // whole edit buffer replaced
        virtual void paramChanged (const ParamValue&, bool /*fromUnit*/) {}  // one value changed
        virtual void programChanged() {}                                     // current bank/slot/name/edited
        virtual void operationFinished (const std::string& /*what*/, bool /*ok*/) {}
        virtual void trafficSeen (bool /*outgoing*/) {}
    };

    explicit DeviceSession (Transport&);

    void addListener (Listener* l) { listeners.push_back (l); }
    void removeListener (Listener* l);

    // ---- connection
    void connect (double nowMs);
    void disconnect();   // turns GUI mode off if connected
    void handleFrame (const Bytes& frame, double nowMs);
    void tick (double nowMs);
    State state() const { return currentState; }
    const WhoAmIInfo& unitInfo() const { return info; }
    bool busy() const { return ! queue.empty(); }

    // ---- model
    const std::vector<std::string>& names (uint8_t bank) const { return bankNames[bank == kBankFactory ? 0 : 1]; }
    std::optional<std::pair<uint8_t, uint8_t>> currentProgram() const { return current; }
    const std::string& editName() const { return editBufferName; }
    bool edited() const { return editBufferEdited; }
    const std::vector<ParamValue>& editBuffer() const { return edit; }
    std::optional<int> value (uint16_t id, uint8_t pos) const;
    uint16_t routing() const;
    EngineState engine (uint8_t pos) const;
    // Engines present in the current routing (pos 2, plus pos 3 for dual routings).
    std::vector<uint8_t> enginePositions() const;
    const std::map<uint16_t, int>& globals() const { return globalParams; }
    std::optional<int> global (uint16_t id) const { auto it = globalParams.find (id); if (it == globalParams.end()) return std::nullopt; return it->second; }

    // ---- operations (asynchronous; listeners get operationFinished)
    void setParam (uint16_t id, uint8_t pos, int value);   // coalesced and paced
    void setType (uint8_t pos, uint16_t typeId);
    void setRouting (uint16_t routing);
    void loadPreset (uint8_t bank, uint8_t slot);
    void store (uint8_t userSlot, const std::string& name);
    void rename (uint8_t userSlot, const std::string& name);
    void writeEditBuffer (const std::string& name, const std::vector<ParamValue>& params);
    void refreshEditBuffer();
    void backup (std::function<void (std::vector<Bytes>)> done);   // bulk dump of the user bank
    void restore (const std::vector<Bytes>& frames);

private:
    struct Step
    {
        Bytes frame;
        uint8_t expect = 0;          // reply proc that completes the step (0 = none)
        uint8_t ackFor = 0;          // complete on ACK/NACK for this proc
        double timeoutMs = 1500;
        double delayAfterMs = 0;     // pacing for fire-and-forget steps
        std::function<void (bool)> done;
        std::function<bool()> waitUntil;   // poll condition instead of a reply
    };

    void enqueue (Step s);
    void startNext (double nowMs);
    void completeStep (bool ok, double nowMs);
    void setState (State s);
    void send (const Bytes& f);
    void flushParams (double nowMs);
    void finished (const std::string& what, bool ok);
    void applyEditBuffer();
    void requestEditBuffer();
    template <typename F> void each (F f) { for (auto* l : std::vector<Listener*> (listeners)) f (*l); }

    Transport& transport;
    std::vector<Listener*> listeners;
    State currentState = State::Offline;
    WhoAmIInfo info;

    std::deque<Step> queue;
    bool stepActive = false;
    double stepStartedMs = 0, lastSendMs = -1e9, now = 0;

    std::array<std::vector<std::string>, 2> bankNames;
    std::optional<std::pair<uint8_t, uint8_t>> current;
    std::string editBufferName;
    bool editBufferEdited = false;
    std::vector<ParamValue> edit;
    std::map<uint16_t, int> globalParams;

    // incoming preset being assembled
    std::optional<PresetStartInfo> incomingStart;
    std::vector<ParamValue> incomingParams;

    // pending SetParam writes, coalesced per (id, pos)
    std::map<std::pair<uint16_t, uint8_t>, int> pendingParams;
    std::deque<std::pair<uint16_t, uint8_t>> pendingOrder;

    // bulk dump capture
    bool capturingDump = false;
    std::vector<Bytes> dumpFrames;
    int acksSeen = 0, nacksSeen = 0;
};
} // namespace mx300
