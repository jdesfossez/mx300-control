#pragma once
// MX300 parameter model: effect types, parameters, and raw value -> display string.
// Data: ParamData.cpp (generated from data/mx300params.json). See docs/PARAMS.md.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mx300
{
enum class Dna { Half, Whole };
enum class Klass { Reverb, Delay, Modulation };
enum class DisplayKind { Int, Linear, Choice, Map, Table, Ms, Rt60, DelayTime, Feedback, LfoHz, Pan, Attack, Type, Routing };

struct TypeInfo
{
    uint16_t id;
    const char* name;
    Dna dna;
    Klass klass;
    std::vector<uint16_t> params;   // in the order the unit sends them
};

struct DisplaySpec
{
    DisplayKind kind;
    const char* unit;
    const char* prefix;
    double scale, offset;
    int decimals;
    int table;                       // index into tables(), or -1
    std::vector<const char*> choices;
    std::vector<std::pair<int, const char*>> map;
    bool tempo, zeroIsStop;
};

struct ParamInfo
{
    uint16_t id;
    const char* name;
    int min, max;
    DisplaySpec display;
};

struct Table
{
    const char* name;
    const char* unit;
    std::vector<double> values;               // NaN = "Off" (filters) or infinity (ratio)
    std::vector<std::pair<int, int>> ratios;  // only for "tap_ratio"
};

// Well-known parameter IDs and positions.
constexpr uint16_t kParamType = 0x0C40, kParamBypass = 0x0C41, kParamMix = 0x0C42, kParamRouting = 0x0C44;
constexpr uint16_t kParamGuiMode = 0x300A;
constexpr uint16_t kParamUnitBypass = 0x3034;   // system (pos 0): the unit's global bypass button, 1 = bypassed
constexpr std::array<uint16_t, 3> kKnobAssign = { 0x2142, 0x2143, 0x2144 };
constexpr uint8_t kPosSystem = 0, kPosPreset = 1, kPosEngine1 = 2, kPosEngine2 = 3;
constexpr uint16_t kRoutingStereo = 0x0604;

// Everything a value's display depends on: routing, the engine's type, its other values.
struct EngineState
{
    uint16_t routing = 0;
    uint16_t type = 0;
    std::map<uint16_t, int> values;
};

const std::vector<TypeInfo>& types();
const std::vector<ParamInfo>& params();
const std::vector<Table>& tables();
const std::vector<std::pair<uint16_t, const char*>>& routings();

const TypeInfo* findType (uint16_t id);
const ParamInfo* findParam (uint16_t id);
bool isWholeRouting (uint16_t routing);
// Types available in a routing, in the unit's menu order.
std::vector<const TypeInfo*> typesFor (uint16_t routing);
// Type ID for a menu index (RequestParam on 0x0C40 answers with the index).
std::optional<uint16_t> typeFromMenuIndex (uint16_t routing, int index);

std::string paramName (uint16_t id);
std::string routingName (uint16_t routing);
std::string displayValue (uint16_t id, int value, const EngineState* engine = nullptr);

std::optional<double> rt60Seconds (int value, const EngineState& engine);
int delayMs (int value, int tempoMs);
std::string bpm (int tempoMs);
// The Tempo parameter of a type (delays), or nullptr. Its value is one beat in ms.
const ParamInfo* tempoParam (uint16_t typeId);
// Tempo value for a BPM, rounded to the unit's 1 ms steps and clamped to the parameter's range.
int tempoValueForBpm (double bpm, const ParamInfo& tempo);
} // namespace mx300
