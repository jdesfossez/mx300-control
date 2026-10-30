#include "ParamModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mx300::data
{
extern const std::vector<TypeInfo> kTypes;
extern const std::vector<uint16_t> kOrderHalf, kOrderWhole;
extern const std::vector<ParamInfo> kParams;
extern const std::vector<Table> kTables;
extern const std::vector<std::array<double, 3>> kRtHalf, kRtWhole;
extern const std::vector<std::vector<double>> kRtWholeSizes;
extern const double kRtK, kRtSampleRate, kRtScale;
extern const std::vector<std::pair<uint16_t, const char*>> kRoutings;
extern const std::vector<uint16_t> kWholeRoutings;
} // namespace mx300::data

namespace mx300
{
namespace
{
std::string format (const char* fmt, double v)
{
    char buf[64];
    std::snprintf (buf, sizeof (buf), fmt, v);
    return buf;
}

std::string trimNumber (double v)   // like %g: 3150 -> "3150", 31.5 -> "31.5"
{
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%g", v);
    return buf;
}

std::string hz (double v)
{
    if (std::isnan (v))
        return "Off";
    return v >= 1000 ? trimNumber (v / 1000) + "kHz" : trimNumber (v) + "Hz";
}

std::string msOrSeconds (double ms)
{
    return ms >= 1000 ? format ("%.2fs", ms / 1000) : trimNumber (ms) + "ms";
}

const std::vector<uint16_t>& order (bool whole) { return whole ? data::kOrderWhole : data::kOrderHalf; }
} // namespace

const std::vector<TypeInfo>& types() { return data::kTypes; }
const std::vector<ParamInfo>& params() { return data::kParams; }
const std::vector<Table>& tables() { return data::kTables; }
const std::vector<std::pair<uint16_t, const char*>>& routings() { return data::kRoutings; }

const TypeInfo* findType (uint16_t id)
{
    for (auto& t : data::kTypes)
        if (t.id == id)
            return &t;
    return nullptr;
}

const ParamInfo* findParam (uint16_t id)
{
    for (auto& p : data::kParams)
        if (p.id == id)
            return &p;
    return nullptr;
}

bool isWholeRouting (uint16_t routing)
{
    return std::find (data::kWholeRoutings.begin(), data::kWholeRoutings.end(), routing) != data::kWholeRoutings.end();
}

std::vector<const TypeInfo*> typesFor (uint16_t routing)
{
    std::vector<const TypeInfo*> out;
    for (auto id : order (isWholeRouting (routing)))
        if (auto* t = findType (id))
            out.push_back (t);
    return out;
}

std::optional<uint16_t> typeFromMenuIndex (uint16_t routing, int index)
{
    auto& o = order (isWholeRouting (routing));
    if (index < 0 || index >= (int) o.size())
        return std::nullopt;
    return o[(size_t) index];
}

std::string paramName (uint16_t id)
{
    for (size_t i = 0; i < kKnobAssign.size(); ++i)
        if (kKnobAssign[i] == id)
            return std::string ("Knob ") + char ('A' + i);
    if (auto* p = findParam (id))
        return p->name;
    return format ("0x%04X", id);
}

std::string routingName (uint16_t routing)
{
    for (auto& [id, name] : data::kRoutings)
        if (id == routing)
            return name;
    return "?";
}

std::optional<double> rt60Seconds (int value, const EngineState& e)
{
    auto* t = findType (e.type);
    const bool whole = isWholeRouting (e.routing);
    auto& o = order (whole);
    auto it = std::find (o.begin(), o.end(), e.type);
    if (t == nullptr || it == o.end())
        return std::nullopt;
    const auto idx = (size_t) (it - o.begin());
    auto& table = whole ? data::kRtWhole : data::kRtHalf;
    if (idx >= table.size())
        return std::nullopt;

    const double x = value / 99.0;
    double k = table[idx][0], gain;
    if (whole)
    {
        if (k == 0)   // loop length follows the Size parameter
        {
            auto& sizes = data::kRtWholeSizes[idx];
            const ParamInfo* sizeParam = nullptr;
            for (auto pid : t->params)
                if (auto* p = findParam (pid); p && p->display.table >= 0
                    && std::string (data::kTables[(size_t) p->display.table].name).rfind ("size_", 0) == 0)
                    sizeParam = p;
            if (sizes.empty() || sizeParam == nullptr)
                return std::nullopt;
            auto v = e.values.count (sizeParam->id) ? e.values.at (sizeParam->id) : 0;
            k = sizes[(size_t) std::clamp (v, 0, (int) sizes.size() - 1)];
        }
        gain = table[idx][1] + (table[idx][2] - table[idx][1]) * x;
    }
    else
    {
        if (k == 0)
            return std::nullopt;
        auto g = [&] (double rt) { return std::pow (10.0, -(k * data::kRtK) / (rt * data::kRtSampleRate)); };
        const double g0 = g (table[idx][1]), g1 = g (table[idx][2]);
        gain = g0 + (g1 - g0) * x;
    }
    if (! (gain > 0 && gain < 1))
        return std::nullopt;
    const double perM = data::kRtK / (-data::kRtSampleRate * std::log10 (gain)) * data::kRtScale;
    const auto n = (long long) (perM * 1000 * 64);
    return (double) (long long) ((double) (n * (long long) k + 32) / 64.0) / 1000.0;
}

int delayMs (int value, int tempoMs)
{
    if (value < 36)
        return (int) (tempoMs * (0.25 + 0.75 * value / 36.0) + 0.5);
    return (int) (tempoMs * (1 + (value - 36) / 36.0));
}

std::string bpm (int tempoMs)
{
    const double b = 60000.0 / tempoMs;
    return format (tempoMs <= 60 ? "%.0f" : tempoMs <= 600 ? "%.1f" : "%.2f", b);
}

const ParamInfo* tempoParam (uint16_t typeId)
{
    if (auto* t = findType (typeId))
        for (auto id : t->params)
            if (auto* p = findParam (id); p != nullptr && p->display.tempo)
                return p;
    return nullptr;
}

int tempoValueForBpm (double bpm, const ParamInfo& tempo)
{
    const int ms = bpm > 0 ? (int) std::lround (60000.0 / bpm) : tempo.max;
    return std::clamp (ms, std::max (1, tempo.min), tempo.max);
}

std::string displayValue (uint16_t id, int value, const EngineState* e)
{
    static const EngineState none;
    const EngineState& engine = e ? *e : none;

    if (id == kParamRouting)
        return routingName ((uint16_t) value);
    if (id == kParamType)
    {
        auto* t = findType ((uint16_t) value);
        return t ? t->name : format ("?%g", value);
    }
    for (auto k : kKnobAssign)
        if (k == id)
            return paramName ((uint16_t) (value & 0xFFFF)) + " (engine " + std::to_string ((value >> 16) - 1) + ")";

    auto* p = findParam (id);
    if (p == nullptr)
        return std::to_string (value);
    auto& d = p->display;
    switch (d.kind)
    {
        case DisplayKind::Int:
            return std::string (d.prefix) + std::to_string (value) + d.unit;
        case DisplayKind::Linear:
        {
            char fmt[16];
            std::snprintf (fmt, sizeof (fmt), "%%.%df", d.decimals);
            return format (fmt, value * d.scale + d.offset) + d.unit;
        }
        case DisplayKind::Choice:
            return value >= 0 && value < (int) d.choices.size() ? d.choices[(size_t) value] : std::to_string (value);
        case DisplayKind::Map:
            for (auto& [k, v] : d.map)
                if (k == value)
                    return v;
            return std::to_string (value);
        case DisplayKind::Table:
        {
            auto& t = data::kTables[(size_t) d.table];
            const std::string unit = t.unit;
            if (unit == "ratio")
                return value >= 0 && value < (int) t.ratios.size()
                           ? std::to_string (t.ratios[(size_t) value].first) + ":" + std::to_string (t.ratios[(size_t) value].second)
                           : "?";
            if (value < 0 || value >= (int) t.values.size())
                return "?";
            const double v = t.values[(size_t) value];
            if (unit == "Hz")
                return hz (v);
            if (unit == ":1")
                return std::isnan (v) ? "inf:1" : trimNumber (v) + ":1";
            if (unit == "oct")
                return "1/" + std::to_string ((int) std::lround (1 / v)) + "oct";
            if (unit == "x")
                return trimNumber (v) + "x";
            return trimNumber (v) + unit;
        }
        case DisplayKind::Ms:
            if (d.tempo && value > 0)
                return bpm (value) + " BPM";
            return msOrSeconds (value);
        case DisplayKind::Rt60:
        {
            auto s = rt60Seconds (value, engine);
            if (! s)
                return std::to_string (value);
            return *s >= 1 ? format ("%.3fs", *s) : std::to_string ((int) std::lround (*s * 1000)) + "ms";
        }
        case DisplayKind::DelayTime:
            for (auto& [pid, v] : engine.values)
                if (auto* tp = findParam (pid); tp && tp->display.kind == DisplayKind::Ms)
                    return msOrSeconds (delayMs (value, v));
            return std::to_string (value);
        case DisplayKind::Feedback:
            return value >= 100 ? "Repeat Hold" : std::to_string (value);
        case DisplayKind::LfoHz:
        {
            if (d.zeroIsStop)
            {
                if (value == 0)
                    return "Stop";
                --value;
            }
            const double f = value < 95 ? 0.05 + 0.01 * value : 1 + 0.1 * (value - 95);
            return format ("%.2fHz", f);
        }
        case DisplayKind::Pan:
            return value == 99 ? "Center" : value < 99 ? "L" + std::to_string (99 - value) : "R" + std::to_string (value - 99);
        case DisplayKind::Attack:
            return value < 9 ? format ("%.1fms", value / 10.0) : std::to_string (value - 8) + "ms";
        case DisplayKind::Type:
        case DisplayKind::Routing:
            break;
    }
    return std::to_string (value);
}
} // namespace mx300
