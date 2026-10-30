"""Offline checks: our encoder vs. byte strings independently captured by Leevi (2011)."""
import mx300proto as p


def hexs(b):
    return " ".join(f"{x:02X}" for x in b)


def test_load_preset_matches_leevi():
    # Leevi program select body: 39 00 <bank> <prog> 04 00 00 01, header F0 06 00 1A 05
    m = p.load_preset(1, 7)
    assert list(m[:6]) == [0xF0, 0x06, 0x00, 0x1A, 0x05, 0x39]
    assert list(m[6:13]) == [0x00, 0x01, 0x07, 0x04, 0x00, 0x00, 0x01]
    # Leevi checksum: XOR of 06 00 1A 05 + body bytes, & 0x7F
    cs = 0
    for b in [0x06, 0x00, 0x1A, 0x05, 0x39, 0x00, 0x01, 0x07, 0x04, 0x00, 0x00, 0x01]:
        cs ^= b
    assert m[-2] == cs & 0x7F and m[-1] == 0xF7


def test_param_matches_leevi_type_a():
    # Leevi type-A param: 41 20 <w> <x> <ch> <v>  -> pid = (w<<8)|(0x80|x)
    m = p.set_param(0x0789, 2, 0x40)
    assert list(m[5:11]) == [0x41, 0x20, 0x07, 0x09, 0x02, 0x40]


def test_super7_roundtrip():
    import random
    for n in range(0, 40):
        data = [random.randrange(256) for _ in range(n)]
        assert p.super7_unpack(p.super7_pack(data)) == data


def test_decode_roundtrip_preset():
    msgs = p.preset_messages("My Hall", [(0x0789, 2, 5), (0x0101, 3, 1000)])
    d = p.decode(msgs[1])
    assert d["checksum_ok"] and d["params"] == [(0x0789, 2, 5), (0x0101, 3, 1000)]
    d0 = p.decode(msgs[0])
    assert d0["preset_name"] == "My Hall" and d0["bank"] == 4


# Frames captured from a real MX300 on 2026-10-29.
def frame(hexstr):
    return bytes(int(x, 16) for x in hexstr.split())


def test_hw_whoami_reply():
    d = p.decode(frame("F0 06 7F 7F 7F 02 00 00 1A 05 00 02 4C 65 00 78 69 63 6F 6E 20 4D 00 78 33 30 30 00 4D 79 00 "
                       "20 4D 78 33 30 30 00 52 22 00 41 5F 6C 5F 0F 2C 4B 1A 4B 6E 35 08 6F 20 2C 24 4D 78 33 30 30 00 00 7E F7"))
    assert d["checksum_ok"] and d["bytes"][:4] == [0x00, 0x1A, 0x05, 0x00]
    assert d["model"] == "Lexicon Mx300" and d["device_name"] == "My Mx300" and d["short_model"] == "Mx300"


def test_hw_edit_buffer_start():
    d = p.decode(frame("F0 06 00 1A 05 2B 00 04 00 46 61 6E 20 54 00 61 73 74 69 63 20 20 00 20 20 20 20 00 00 02 65 F7"))
    assert d["checksum_ok"] and (d["bank"], d["slot"]) == (4, 0)
    assert d["preset_name"] == "Fan Tastic      " and d["extra"] == [0, 2]


def test_hw_globals():
    d = p.decode(frame("F0 06 00 1A 05 11 00 00 11 30 26 00 08 30 00 1D 00 01 30 27 00 01 00 30 28 00 00 30 2A 00 00 01 30 2C 00 00 30 2E 00 00 01 30 2F 00 "
                       "00 30 00 06 00 00 30 35 00 01 00 30 36 00 01 30 37 00 00 01 30 32 00 00 30 33 00 00 03 30 31 00 01 30 00 34 00 01 30 21 00 00 07 F7"))
    assert d["checksum_ok"] and len(d["params"]) == 17
    assert d["params"][0] == (0x3026, 0, 8) and d["params"][-1] == (0x3021, 0, 0)


def test_hw_program_knob_notification():
    # Turning PROGRAM from 6 back to 5 on the front panel.
    d = p.decode(frame("F0 06 00 1A 05 70 00 03 01 04 04 00 6B F7"))
    assert d["checksum_ok"] and d["sub"] == 3 and d["data"][:4] == [1, 4, 4, 0]


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
    print(hexs(p.req_whoami()))
    print(hexs(p.load_preset(1, 7)))
    print(hexs(p.store_preset(3, "Test Hall")))
