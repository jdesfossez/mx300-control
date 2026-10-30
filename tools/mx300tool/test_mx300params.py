"""Offline checks for the parameter model (mx300params.json + mx300params.py)."""
import mx300params as m
import mx300proto as p

# A Stereo Small Hall program in the layout the unit sends (made-up values, not a stored preset).
# Pre Delay 0, Size 4 and Mid RT 76 read 0ms / 14m / 1.369s on the unit's display.
TEST_HALL_PARAMS = [
    (0x0C44, 1, 0x0604), (0x0C40, 2, 0x046E), (0x0C41, 2, 0), (0x0789, 2, 0), (0x078B, 2, 76), (0x079D, 2, 4),
    (0x0792, 2, 0), (0x0793, 2, 40), (0x07A2, 2, 10), (0x0794, 2, 20), (0x0797, 2, 3), (0x0796, 2, 5),
    (0x078E, 2, 50), (0x0798, 2, 10), (0x079A, 2, 50), (0x0C42, 2, 60),
    (0x2142, 2, 0x020789), (0x2143, 2, 0x02078B), (0x2144, 2, 0x02079D),
]
TEST_HALL = p.preset_messages("Test Hall", TEST_HALL_PARAMS)[1]


def test_program_decodes():
    params = p.decode(TEST_HALL)["params"]
    eng = m.engines_from_params(params)[m.POS_ENGINE1]
    assert m.display(m.P_ROUTING, 0x0604) == "Stereo"
    assert eng.type["name"] == "Small Hall" and eng.whole
    shown = {m.name(pid): m.display(pid, v, eng) for pid, pos, v in params}
    assert shown["Pre Delay"] == "0ms"
    assert shown["Size"] == "14m"
    assert shown["Mid RT"] == "1.369s"
    assert shown["Knob A"] == "Pre Delay (engine 1)"


def test_engine_params_follow_type_order():
    params = p.decode(TEST_HALL)["params"]
    eng = m.engines_from_params(params)[m.POS_ENGINE1]
    body = [pid for pid in eng.values if pid not in (m.P_TYPE, m.P_BYPASS, m.P_MIX, *m.P_KNOBS)]
    assert body == eng.type["params"]


def test_data_is_consistent():
    for t in m.TYPES.values():
        for pid in t["params"]:
            assert pid in m.PARAMS, (t["name"], hex(pid))
    for pid, info in m.PARAMS.items():
        d = info["display"]
        if d["kind"] == "table":
            assert len(m.TABLES[d["table"]]) == info["max"] + 1 or d["table"] in ("pitch_hpf", "ratio"), hex(pid)


def test_rt60_scales_with_size():
    eng = m.Engine(0x0604, 0x046E, {0x079D: 0})   # Stereo Small Hall, 8 m
    small = m.rt60_seconds(76, eng)
    eng.values[0x079D] = 7                        # 20 m
    assert m.rt60_seconds(76, eng) > small * 2


def test_half_rt60_endpoints():
    # Dual-mono Small Hall: the RT range in the data is 0.6 s .. 2.0 s (times the 0.9 scale).
    eng = m.Engine(0x0600, 0x045D)
    assert abs(m.rt60_seconds(0, eng) - 0.54) < 0.01
    assert abs(m.rt60_seconds(99, eng) - 1.8) < 0.01


def test_value_models():
    assert m.display(0x074D, 36, m.Engine(0x0600, 0x0408, {0x0753: 500})) == "500ms"   # 1 beat
    assert m.display(0x074D, 0, m.Engine(0x0600, 0x0408, {0x0753: 500})) == "125ms"    # 1/4 beat
    assert m.display(0x074D, 72, m.Engine(0x0600, 0x0408, {0x0753: 500})) == "1.00s"   # 2 beats
    assert m.display(0x074C, 512) == "117.2 BPM (512ms)"   # unit shows BPM for Tempo
    assert m.display(0x078B, 78, m.Engine(0x0603, 0x0466)) == "1.819s"  # Mono Split Vocal Plate, seen on the unit
    assert m.display(0x0747, 100) == "Repeat Hold"
    assert m.display(0x00CA, 0) == "-70dB"
    assert m.display(0x078C, 60) == "800ms"
    assert m.display(0x0794, 13) == "20kHz"
    assert m.display(0x0345, 10) == "10"


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
