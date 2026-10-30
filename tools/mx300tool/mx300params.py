"""
MX300 parameter model: names, ranges and display strings for raw parameter values.

Data comes from data/mx300params.json (schema in docs/PARAMS.md). Display strings are our own formatting
of the physical value, so they may differ in style from the unit's display.
"""
import json
import math
import os

_DATA = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data", "mx300params.json")))

POS_PRESET, POS_ENGINE1, POS_ENGINE2 = 1, 2, 3
P_TYPE = int(_DATA["engine"]["type"], 16)
P_BYPASS = int(_DATA["engine"]["bypass"], 16)
P_MIX = int(_DATA["engine"]["mix"], 16)
P_ROUTING = int(_DATA["routing"]["param"], 16)
P_KNOBS = [int(x, 16) for x in _DATA["knob_assign"]["params"]]

ROUTINGS = {int(k, 16): v for k, v in _DATA["routing"]["values"].items()}
WHOLE_ROUTINGS = {int(x, 16) for x in _DATA["routing"]["whole"]}
TYPES = {}
for t in _DATA["types"]:
    TYPES[int(t["id"], 16)] = dict(t, id=int(t["id"], 16), params=[int(p, 16) for p in t["params"]])
PARAMS = {int(k, 16): v for k, v in _DATA["params"].items()}
ORDER = {k: [int(x, 16) for x in _DATA["type_order"][k]] for k in ("half", "whole")}
TABLES = {k: v["values"] for k, v in _DATA["tables"].items()}
RT = _DATA["rt60"]


class Engine:
    """What a value's display depends on: routing, the engine's type, and its other parameters."""

    def __init__(self, routing=None, type_id=None, values=None):
        self.routing = routing
        self.type_id = type_id
        self.values = values or {}

    @property
    def whole(self):
        return self.routing in WHOLE_ROUTINGS

    @property
    def type(self):
        return TYPES.get(self.type_id)


def engines_from_params(params):
    """params: [(id, pos, value)] as decoded from a 2D message -> {pos: Engine}."""
    routing = next((v for pid, pos, v in params if pid == P_ROUTING), None)
    out = {}
    for pid, pos, v in params:
        if pos in (POS_ENGINE1, POS_ENGINE2):
            e = out.setdefault(pos, Engine(routing))
            e.values[pid] = v
            if pid == P_TYPE:
                e.type_id = v
    return out


def name(pid):
    if pid in P_KNOBS:
        return "Knob " + "ABC"[P_KNOBS.index(pid)]
    return PARAMS.get(pid, {}).get("name", f"0x{pid:04X}")


def _hz(v):
    if v is None:
        return "Off"
    return f"{v / 1000:g}kHz" if v >= 1000 else f"{v:g}Hz"


def _ms(ms):
    return f"{ms / 1000:.2f}s" if ms >= 1000 else f"{ms:g}ms"


def rt60_seconds(value, engine):
    """Reverb time for a Mid RT / Decay value (0..99), or None if the model doesn't apply."""
    t = engine.type
    order = ORDER["whole" if engine.whole else "half"]
    if not t or engine.type_id not in order:
        return None
    idx = order.index(engine.type_id)
    table = RT["whole" if engine.whole else "half"]
    if idx >= len(table):
        return None
    x = value / 99
    if engine.whole:
        k, g_lo, g_hi = table[idx]
        if k == 0:  # loop length follows the Size parameter
            sizes = RT["whole_size_m"][idx]
            size_pid = next((p for p in t["params"] if PARAMS[p]["display"].get("table", "").startswith("size_")), None)
            if not sizes or size_pid is None:
                return None
            k = sizes[min(engine.values.get(size_pid, 0), len(sizes) - 1)]
        gain = g_lo + (g_hi - g_lo) * x
    else:
        k, rt_lo, rt_hi = table[idx]
        if k == 0:
            return None
        # interpolate in the gain domain between the RT endpoints
        g = [10 ** (-(k * RT["k"]) / (rt * RT["sample_rate"])) for rt in (rt_lo, rt_hi)]
        gain = g[0] + (g[1] - g[0]) * x
    if not 0 < gain < 1:
        return None
    per_m = RT["k"] / (-RT["sample_rate"] * math.log10(gain)) * RT["scale"]
    n = int(per_m * 1000 * 64)
    return int((n * k + 32) / 64) / 1000


def bpm(tempo_ms):
    """Tempo as the unit displays it (BPM); decimals depend on the range."""
    b = 60000 / tempo_ms
    return f"{b:.0f}" if tempo_ms <= 60 else f"{b:.1f}" if tempo_ms <= 600 else f"{b:.2f}"


def delay_ms(value, tempo_ms):
    """Delay 'Time' (0..72) is a fraction of the tempo: 1/4 .. 1 beat .. 2 beats."""
    if value < 36:
        return int(tempo_ms * (0.25 + 0.75 * value / 36) + 0.5)
    return int(tempo_ms * (1 + (value - 36) / 36))


def display(pid, value, engine=None):
    """Human-readable string for a raw value."""
    engine = engine or Engine()
    if pid == P_ROUTING:
        return ROUTINGS.get(value, f"?{value:#x}")
    if pid == P_TYPE:
        return TYPES[value]["name"] if value in TYPES else f"?{value:#x}"
    if pid in P_KNOBS:
        target = value & 0xFFFF
        return f"{name(target)} (engine {(value >> 16) - 1})"
    p = PARAMS.get(pid)
    if not p:
        return str(value)
    d = p["display"]
    kind = d["kind"]
    if kind == "int":
        return f"{d.get('prefix', '')}{value}{d.get('unit', '')}"
    if kind == "linear":
        v = value * d["scale"] + d["offset"]
        return f"{v:.{d.get('decimals', 0)}f}{d['unit']}"
    if kind == "map":
        return d["values"].get(str(value), str(value))
    if kind == "choice":
        c = d["choices"]
        return c[value] if 0 <= value < len(c) else str(value)
    if kind == "table":
        vals, unit = TABLES[d["table"]], _DATA["tables"][d["table"]]["unit"]
        if not 0 <= value < len(vals):
            return f"?{value}"
        v = vals[value]
        if unit == "Hz":
            return _hz(v)
        if unit == ":1":
            return "inf:1" if v is None else f"{v:g}:1"
        if unit == "ratio":
            return f"{v[0]}:{v[1]}"
        if unit == "oct":
            return f"1/{round(1 / v)}oct"
        if unit == "x":
            return f"{v:g}x"
        return f"{v:g}{unit}"
    if kind == "ms":
        return f"{bpm(value)} BPM ({_ms(value)})" if d.get("tempo") and value > 0 else _ms(value)
    if kind == "rt60":
        s = rt60_seconds(value, engine)
        if s is None:
            return str(value)
        return f"{s:.3f}s" if s >= 1 else f"{round(s * 1000)}ms"  # the unit shows ms resolution
    if kind == "delay_time":
        tempo = next((engine.values[t] for t in engine.values if PARAMS.get(t, {}).get("display", {}).get("kind") == "ms"), None)
        return str(value) if tempo is None else _ms(delay_ms(value, tempo))
    if kind == "feedback":
        return "Repeat Hold" if value >= 100 else str(value)
    if kind == "lfo_hz":
        if d.get("zero_is_stop"):
            if value == 0:
                return "Stop"
            value -= 1
        hz = 0.05 + 0.01 * value if value < 95 else 1 + 0.1 * (value - 95)
        return f"{hz:.2f}Hz"
    if kind == "pan":
        return "Center" if value == 99 else f"L{99 - value}" if value < 99 else f"R{value - 99}"
    if kind == "attack":
        return f"{value / 10:.1f}ms" if value < 9 else f"{value - 8}ms"
    return str(value)


def describe(params):
    """[(id, pos, value)] -> list of display lines, grouped per engine."""
    engines = engines_from_params(params)
    lines = []
    for pid, pos, v in params:
        e = engines.get(pos)
        where = {POS_PRESET: "preset", POS_ENGINE1: "eng1", POS_ENGINE2: "eng2"}.get(pos, f"pos{pos}")
        lines.append(f"{where:6s} {name(pid):12s} {display(pid, v, e):>14s}   (0x{pid:04X} = {v})")
    return lines
