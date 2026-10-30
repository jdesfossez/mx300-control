// Core tests: protocol vs. frames captured from a real MX300, the parameter model vs. values read off
// the unit's display, and DeviceSession against FakeUnit.

#include "../DeviceSession.h"
#include "../FakeUnit.h"
#include "../ParamModel.h"
#include "../Protocol.h"

#include <cstdio>
#include <deque>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace mx300;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        ++checks;                                                                         \
        if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)
#define CHECK_EQ(a, b)                                                                    \
    do {                                                                                  \
        ++checks;                                                                         \
        auto va = (a); auto vb = (b);                                                     \
        if (! (va == vb)) { ++failures; std::printf ("  FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); } \
    } while (0)

static Bytes hex (const std::string& s)
{
    Bytes b;
    std::istringstream in (s);
    std::string tok;
    while (in >> tok)
        b.push_back ((uint8_t) std::stoi (tok, nullptr, 16));
    return b;
}

// ------------------------------------------------------------------ protocol

static void testProtocol()
{
    CHECK_EQ (reqWhoAmI(), hex ("F0 06 7F 1A 7F 01 00 7F 7F 7F 62 F7"));
    // Leevi's independently captured "type A" param write: 41 20 07 09 02 40
    auto sp = setParam (0x0789, 2, 0x40);
    CHECK_EQ (Bytes (sp.begin() + 5, sp.begin() + 11), hex ("41 20 07 09 02 40"));
    // Leevi's program select = MovePreset into the edit buffer
    auto lp = loadPreset (1, 7);
    CHECK_EQ (Bytes (lp.begin() + 5, lp.begin() + 13), hex ("39 00 01 07 04 00 00 01"));

    for (size_t n = 0; n < 40; ++n)
    {
        Bytes d;
        for (size_t i = 0; i < n; ++i)
            d.push_back ((uint8_t) (i * 37 + n));
        CHECK_EQ (super7Unpack (super7Pack (d)), d);
    }

    auto who = parseFrame (hex ("F0 06 7F 7F 7F 02 00 00 1A 05 00 02 4C 65 00 78 69 63 6F 6E 20 4D 00 78 33 30 30 00 4D 79 00 "
                                "20 4D 78 33 30 30 00 52 22 00 41 5F 6C 5F 0F 2C 4B 1A 4B 6E 35 08 6F 20 2C 24 4D 78 33 30 30 00 00 7E F7"));
    CHECK (who && who->checksumOk);
    auto w = decodeWhoAmI (*who);
    CHECK (w && w->mode == 0 && w->model == "Lexicon Mx300" && w->deviceName == "My Mx300");

    auto cfg = parseFrame (hex ("F0 06 00 1A 05 09 00 00 11 00 01 02 01 04 00 00 07 F7"));
    CHECK (cfg && cfg->checksumOk);
    auto c = decodeConfig (*cfg);
    CHECK (c && c->bank == 1 && c->slot == 4);

    auto start = parseFrame (hex ("F0 06 00 1A 05 2B 00 04 00 46 61 6E 20 54 00 61 73 74 69 63 20 20 00 20 20 20 20 00 00 02 65 F7"));
    auto ps = decodePresetStart (*start);
    CHECK (ps && ps->bank == 4 && ps->name == "Fan Tastic" && ! ps->edited);

    auto note = parseFrame (hex ("F0 06 00 1A 05 70 00 03 01 04 04 00 6B F7"));
    auto n = decodeNotify (*note);
    CHECK (n && n->sub == 3 && n->data == hex ("01 04 04 00"));

    // our edit-buffer write ends PresetStart with 00 02 (the unit NACKs MX-Edit's 00 02 00 02)
    auto msgs = presetMessages ("Test", { { 0x0789, 2, 5 } });
    auto body = parseFrame (msgs[0])->body;
    CHECK_EQ (Bytes (body.end() - 2, body.end()), hex ("00 02"));
}

// ------------------------------------------------------------------ parameter model

// A Stereo Small Hall program in the layout the unit sends (made-up values, not a stored preset).
// Pre Delay 0, Size 4 and Mid RT 76 read 0ms / 14m / 1.369s on the unit's display.
static const std::vector<ParamValue> kTestHall = {
    { kParamRouting, kPosPreset, 0x0604 }, { kParamType, kPosEngine1, 0x046E }, { kParamBypass, kPosEngine1, 0 },
    { 0x0789, kPosEngine1, 0 }, { 0x078B, kPosEngine1, 76 }, { 0x079D, kPosEngine1, 4 }, { 0x0792, kPosEngine1, 0 },
    { 0x0793, kPosEngine1, 40 }, { 0x07A2, kPosEngine1, 10 }, { 0x0794, kPosEngine1, 20 }, { 0x0797, kPosEngine1, 3 },
    { 0x0796, kPosEngine1, 5 }, { 0x078E, kPosEngine1, 50 }, { 0x0798, kPosEngine1, 10 }, { 0x079A, kPosEngine1, 50 },
    { kParamMix, kPosEngine1, 60 }, { 0x2142, kPosEngine1, 0x020789 }, { 0x2143, kPosEngine1, 0x02078B },
    { 0x2144, kPosEngine1, 0x02079D },
};

static void testParamModel()
{
    auto m = parseFrame (presetMessages ("Test Hall", kTestHall)[1]);
    CHECK (m && m->checksumOk);
    auto params = decodeParamList (*m);
    CHECK (params && params->size() == 19);

    EngineState e;
    for (auto& p : *params)
    {
        if (p.id == kParamRouting) e.routing = (uint16_t) p.value;
        if (p.pos == kPosEngine1) e.values[p.id] = p.value;
        if (p.id == kParamType) e.type = (uint16_t) p.value;
    }
    CHECK_EQ (routingName (e.routing), std::string ("Stereo"));
    CHECK_EQ (displayValue (kParamType, e.type), std::string ("Small Hall"));
    CHECK_EQ (displayValue (0x0789, 0, &e), std::string ("0ms"));
    CHECK_EQ (displayValue (0x079D, 4, &e), std::string ("14m"));
    CHECK_EQ (displayValue (0x078B, 76, &e), std::string ("1.369s"));
    CHECK_EQ (displayValue (0x2142, 133001, &e), std::string ("Pre Delay (engine 1)"));

    // values read off the front panel, 2026-10-29
    EngineState vocalPlate { 0x0603, 0x0466, {} };   // program 42, Mono Split
    CHECK_EQ (displayValue (0x078B, 78, &vocalPlate), std::string ("1.819s"));
    CHECK_EQ (displayValue (0x074C, 512), std::string ("117.2 BPM"));
    {
        auto* monoDelay = tempoParam (0x0409);   // Mono Delay (half)
        CHECK (monoDelay != nullptr && monoDelay->id == 0x074C);
        CHECK (tempoParam (0x045D) == nullptr);   // Small Hall has no tempo
        auto* t = findParam (0x0753);
        CHECK (t != nullptr);
        if (t != nullptr)
        {
            CHECK_EQ (tempoValueForBpm (120.0, *t), 500);
            CHECK_EQ (tempoValueForBpm (128.0, *t), 469);
            CHECK_EQ (tempoValueForBpm (20.0, *t), 2500);   // clamped to the half-DNA maximum
            CHECK_EQ (tempoValueForBpm (0.0, *t), 2500);
        }
    }

    EngineState delay { 0x0600, 0x0408, { { 0x0753, 500 } } };
    CHECK_EQ (displayValue (0x074D, 36, &delay), std::string ("500ms"));
    CHECK_EQ (displayValue (0x074D, 0, &delay), std::string ("125ms"));
    CHECK_EQ (displayValue (0x074D, 72, &delay), std::string ("1.00s"));
    CHECK_EQ (displayValue (0x0747, 100), std::string ("Repeat Hold"));
    CHECK_EQ (displayValue (0x00CA, 0), std::string ("-70dB"));
    CHECK_EQ (displayValue (0x078C, 60), std::string ("800ms"));
    CHECK_EQ (displayValue (0x0794, 13), std::string ("20kHz"));
    CHECK_EQ (displayValue (0x302E, 2), std::string ("Analog Stereo"));

    EngineState hall { 0x0600, 0x045D, {} };
    auto lo = rt60Seconds (0, hall), hi = rt60Seconds (99, hall);
    CHECK (lo && *lo > 0.53 && *lo < 0.55);
    CHECK (hi && *hi > 1.79 && *hi < 1.81);

    CHECK_EQ (typesFor (kRoutingStereo).size(), (size_t) 31);
    CHECK_EQ (typesFor (0x0600).size(), (size_t) 32);
    CHECK (typeFromMenuIndex (0x0600, 31) == std::optional<uint16_t> (0x00C1));   // Compressor (seen on the unit)

    for (auto& t : types())
        for (auto pid : t.params)
            CHECK (findParam (pid) != nullptr);
}

// ------------------------------------------------------------------ session vs. fake unit

struct Rig : Transport, DeviceSession::Listener
{
    FakeUnit unit;
    DeviceSession session { *this };
    std::deque<Bytes> toHost;
    double now = 0;
    std::vector<ParamValue> fromUnit;
    std::vector<std::pair<std::string, bool>> ops;
    int editChanges = 0;

    Rig()
    {
        unit.toHost = [this] (const Bytes& f) { toHost.push_back (f); };
        session.addListener (this);
    }
    void sendSysex (const Bytes& f) override { unit.receive (f); }
    void paramChanged (const ParamValue& v, bool u) override { if (u) fromUnit.push_back (v); }
    void operationFinished (const std::string& w, bool ok) override { ops.push_back ({ w, ok }); }
    void editBufferChanged() override { ++editChanges; }

    void run (double ms)
    {
        for (double end = now + ms; now < end; now += 5)
        {
            while (! toHost.empty())
            {
                auto f = toHost.front();
                toHost.pop_front();
                session.handleFrame (f, now);
            }
            session.tick (now);
        }
    }
    bool lastOp (const std::string& what) const { return ! ops.empty() && ops.back().first == what && ops.back().second; }
};

static void testSession()
{
    {
        Rig r;
        r.session.connect (0);
        r.run (500);
        CHECK (r.session.state() == DeviceSession::State::Ready);
        CHECK (r.unit.guiMode());
        CHECK_EQ (r.session.names (kBankUser).size(), (size_t) 99);
        CHECK_EQ (r.session.names (kBankFactory).size(), (size_t) 99);
        CHECK (r.session.editBuffer() == r.unit.editBuffer().params);
        CHECK (r.session.currentProgram() == std::make_optional (std::make_pair (kBankUser, (uint8_t) 0)));

        // front-panel knob -> listener, model updated, edited flag
        const uint16_t pid = r.unit.editBuffer().params[3].id;
        r.unit.turnKnob (pid, kPosEngine1, 7);
        r.run (50);
        CHECK (! r.fromUnit.empty() && r.fromUnit.back().value == 7);
        CHECK (r.session.value (pid, kPosEngine1) == std::optional<int> (7));
        CHECK (r.session.edited());

        // global bypass (system parameter): both ways, never marks the program edited
        r.unit.selectProgram (1);
        r.run (100);
        CHECK (r.session.global (kParamUnitBypass) == std::optional<int> (0));
        r.session.setParam (kParamUnitBypass, kPosSystem, 1);
        r.run (50);
        CHECK_EQ (r.unit.global (kParamUnitBypass), 1);
        CHECK (! r.session.edited());
        CHECK (r.session.editBuffer() == r.unit.editBuffer().params);
        r.unit.turnKnob (kParamUnitBypass, kPosSystem, 0);   // the unit's own bypass button
        r.run (50);
        CHECK (r.session.global (kParamUnitBypass) == std::optional<int> (0));
        CHECK (! r.fromUnit.empty() && r.fromUnit.back().id == kParamUnitBypass && r.fromUnit.back().value == 0);
        CHECK (! r.session.edited());
        r.unit.turnKnob (pid, kPosEngine1, 7);
        r.run (50);

        // front-panel program change -> edit buffer reloaded
        r.unit.selectProgram (2);
        r.run (100);
        CHECK (r.session.currentProgram()->second == 2);
        CHECK (r.session.editBuffer() == r.unit.editBuffer().params);
        CHECK (! r.session.edited());

        // host parameter writes are coalesced: only the last value is sent
        const int before = r.unit.framesReceived;
        auto target = r.session.editBuffer()[4];
        for (int v = 0; v < 20; ++v)
            r.session.setParam (target.id, target.pos, v);
        r.run (50);
        CHECK (r.unit.framesReceived - before <= 2);
        CHECK (r.unit.editBuffer().params[4].value == 19);

        // type change: the unit fills in defaults, the session reloads
        r.session.setType (kPosEngine1, 0x0477);   // Stereo Ambience
        r.run (100);
        CHECK (r.session.engine (kPosEngine1).type == 0x0477);
        CHECK (r.session.editBuffer() == r.unit.editBuffer().params);

        // load / store / rename
        r.session.loadPreset (kBankFactory, 10);
        r.run (100);
        CHECK (r.lastOp ("load"));
        CHECK (r.session.editName() == r.unit.preset (kBankFactory, 10).name);
        r.session.store (50, "My Program");
        r.run (100);
        CHECK (r.lastOp ("store"));
        CHECK_EQ (r.session.names (kBankUser)[50], std::string ("My Program"));
        r.session.rename (50, "Renamed");
        r.run (100);
        CHECK (r.lastOp ("rename"));
        CHECK_EQ (r.session.names (kBankUser)[50], std::string ("Renamed"));

        // write a whole edit buffer (DAW session restore)
        auto saved = r.unit.preset (kBankUser, 7);
        r.session.writeEditBuffer (saved.name, saved.params);
        r.run (200);
        CHECK (r.lastOp ("write"));
        CHECK (r.unit.editBuffer().params == saved.params);
        CHECK (r.session.editBuffer() == saved.params);

        // backup and restore
        std::vector<Bytes> dump;
        r.session.backup ([&] (std::vector<Bytes> f) { dump = std::move (f); });
        r.run (200);
        CHECK (r.lastOp ("backup"));
        CHECK_EQ (dump.size(), (size_t) 300);
        r.session.rename (3, "Scratch");
        r.run (100);
        r.session.restore (dump);
        r.run (12000);
        CHECK (r.lastOp ("restore"));
        CHECK_EQ (r.session.names (kBankUser)[3], r.unit.preset (kBankUser, 3).name);
        CHECK (r.session.names (kBankUser)[3] != "Scratch");

        r.session.disconnect();
        CHECK (! r.unit.guiMode());
    }
    {
        Rig r;   // OS-update mode: stop after WhoAmI
        r.unit.whoAmIMode = 1;
        r.session.connect (0);
        r.run (200);
        CHECK (r.session.state() == DeviceSession::State::UpdateMode);
        CHECK_EQ (r.unit.framesReceived, 1);
    }
    {
        Rig r;   // nothing answers
        r.unit.silent = true;
        r.session.connect (0);
        r.run (3000);
        CHECK (r.session.state() == DeviceSession::State::NoResponse);
    }
}

int main()
{
    testProtocol();
    testParamModel();
    testSession();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
