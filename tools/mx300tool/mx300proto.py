"""
MX300 SysEx protocol (clean-room implementation from our own protocol notes).

Frame:  F0 06 <id0> <id1> <id2> <proc> <Super7-packed body...> <checksum> F7
  06              Lexicon manufacturer id
  id0 id1 id2     00 1A 05 for the MX300 (WhoAmI uses 7F 1A 7F)
  proc            command / reply id (raw, not packed)
  body            Super7 packed: each group of up to 7 data bytes is preceded
                  by one byte holding their high bits (0x40 = 1st ... 0x01 = 7th)
  checksum        XOR of every byte after F0 up to the last body byte, & 0x7F
Body conventions: int16 big-endian, strings NUL-terminated,
values "Mag7": v < 0x80 -> 1 byte, else 0x80|n followed by n big-endian bytes.
"""

MFR = 0x06
IDS = (0x00, 0x1A, 0x05)
WHOAMI_IDS = (0x7F, 0x1A, 0x7F)

# Requests (host -> unit)
P_WHOAMI = 0x01
P_REQ_CONFIG = 0x08
P_REQ_GLOBALS = 0x10
P_REQ_BULK = 0x18
P_USER_TABLE = 0x20
P_REQ_NAMES = 0x21
P_SET_NAME = 0x29
P_REQ_PRESET = 0x2A
P_PRESET_START = 0x2B
P_PRESET_END = 0x2C
P_PRESET_PARAMS = 0x2D
P_MOVE_PRESET = 0x39
P_REQ_MODLIST = 0x3A
P_REQ_PARAM = 0x40
P_PARAM = 0x41
P_ACK = 0x7E

# Replies (unit -> host)
R_WHOAMI = 0x02
R_CONFIG = 0x09
R_GLOBALS = 0x11
R_BULK_START = 0x19
R_BULK_END = 0x1B
R_NAMES = 0x22
R_PRESET_START = 0x2B
R_PRESET_END = 0x2C
R_PRESET_PARAMS = 0x2D
R_MODLIST = 0x3B
R_PARAM = 0x41
R_OBJ_NAMES = 0x51
R_NOTIFY = 0x70
R_ACK = 0x7E
R_NACK = 0x7F

PROC_NAMES = {
    R_WHOAMI: "WHOAMI", R_CONFIG: "CONFIG", R_GLOBALS: "GLOBALS", R_BULK_START: "BULK_START",
    R_BULK_END: "BULK_END", R_NAMES: "PRESET_NAMES", R_PRESET_START: "PRESET_START",
    R_PRESET_END: "PRESET_END", R_PRESET_PARAMS: "PRESET_PARAMS", R_MODLIST: "MODIFIER_LIST",
    R_PARAM: "PARAM_VALUE", R_OBJ_NAMES: "OBJECT_NAMES", R_NOTIFY: "NOTIFY", R_ACK: "ACK", R_NACK: "NACK",
    P_WHOAMI: "REQ_WHOAMI", P_REQ_CONFIG: "REQ_CONFIG", P_REQ_NAMES: "REQ_NAMES",
    P_REQ_PRESET: "REQ_PRESET", P_MOVE_PRESET: "MOVE_PRESET", P_SET_NAME: "SET_NAME",
    P_REQ_PARAM: "REQ_PARAM", P_REQ_BULK: "REQ_BULK",
}

BANK_FACTORY, BANK_USER, BANK_EDIT = 0, 1, 4
GUI_MODE_PARAM = 0x300A


def super7_pack(data):
    out = []
    for i in range(0, len(data), 7):
        grp = data[i:i + 7]
        hi = 0
        for k, b in enumerate(grp):
            if b & 0x80:
                hi |= 0x40 >> k
        out.append(hi)
        out.extend(b & 0x7F for b in grp)
    return out


def super7_unpack(data):
    out = []
    i = 0
    while i < len(data):
        hi = data[i]
        grp = data[i + 1:i + 8]
        for k, b in enumerate(grp):
            out.append(b | (0x80 if hi & (0x40 >> k) else 0))
        i += 1 + len(grp)
    return out


def checksum(frame_wo_cs):
    cs = 0
    for b in frame_wo_cs[1:]:
        cs ^= b
    return cs & 0x7F


def build(proc, body=(), ids=IDS):
    frame = [0xF0, MFR, *ids, proc] + super7_pack(list(body))
    return bytes(frame + [checksum(frame), 0xF7])


def parse(msg):
    """msg: full sysex bytes incl. F0/F7. Returns (proc, body, ok_checksum)."""
    m = list(msg)
    if len(m) < 8 or m[0] != 0xF0 or m[1] != MFR or m[-1] != 0xF7:
        raise ValueError("not an MX SysEx frame")
    ok = checksum(m[:-2]) == m[-2]
    return m[5], super7_unpack(m[6:-2]), ok


# ---- body helpers -------------------------------------------------------

def i16(v):
    v &= 0xFFFF
    return [v >> 8, v & 0xFF]


def mag7(v):
    if v < 0x80:
        return [v]
    if v < 0x10000:
        return [0x82, *i16(v)]
    return [0x83, (v >> 16) & 0xFF, *i16(v)]


def cstr(s, maxlen=16):
    return list(s.encode("ascii", "replace")[:maxlen]) + [0]


class Reader:
    def __init__(self, body):
        self.b, self.i = body, 0

    def byte(self):
        v = self.b[self.i]
        self.i += 1
        return v

    def int16(self):
        v = (self.b[self.i] << 8) | self.b[self.i + 1]
        self.i += 2
        return v - 0x10000 if v & 0x8000 else v

    def uint16(self):
        return self.int16() & 0xFFFF

    def mag7(self):
        v = self.byte()
        if v < 0x80:
            return v
        n, r = v & 0x7F, 0
        for _ in range(n):
            r = (r << 8) | self.byte()
        return r

    def string(self):
        s = []
        while self.i < len(self.b) and self.b[self.i] != 0:
            s.append(self.b[self.i])
            self.i += 1
        self.i += 1
        return bytes(s).decode("ascii", "replace")

    def left(self):
        return len(self.b) - self.i


# ---- requests -----------------------------------------------------------

def req_whoami():
    return build(P_WHOAMI, [0x7F, 0x7F, 0x7F], ids=WHOAMI_IDS)


def req_config():
    return build(P_REQ_CONFIG)


def req_names(bank):
    return build(P_REQ_NAMES, [bank])


def req_preset(bank, slot):
    return build(P_REQ_PRESET, [bank, slot])


def req_param(pid, pos):
    return build(P_REQ_PARAM, [*i16(pid), pos])


def set_param(pid, pos, value):
    return build(P_PARAM, [*i16(pid), pos, *mag7(value)])


def gui_mode(on):
    return set_param(GUI_MODE_PARAM, 0, 1 if on else 0)


def move_preset(src_bank, src_slot, dst_bank, dst_slot, name=None, flag=1):
    body = [src_bank, src_slot, dst_bank, dst_slot] + (cstr(name) if name else [0]) + [flag]
    return build(P_MOVE_PRESET, body)


def load_preset(bank, slot):
    """Recall a stored preset into the edit buffer (what the front panel does)."""
    return move_preset(bank, slot, BANK_EDIT, 0)


def store_preset(user_slot, name):
    """Save the edit buffer into user bank slot `user_slot` (0-based) with `name`."""
    return move_preset(BANK_EDIT, 0, BANK_USER, user_slot, name)


def rename_preset(user_slot, name):
    return build(P_SET_NAME, [BANK_USER, user_slot] + cstr(name))


def req_globals():
    return build(P_REQ_GLOBALS)


def req_bulk_dump():
    return build(P_REQ_BULK, [0])


def preset_messages(name, params, bank=BANK_EDIT, slot=0, tail=(0, 2)):
    """Write a full preset (list of (pid, pos, value)) into bank/slot (default edit buffer).
    tail: bytes after the name. The MX300 NACKs (code 9) MX-Edit's 00 02 00 02; use 00 02."""
    start = build(P_PRESET_START, [bank, slot] + cstr(name) + list(tail))
    body = i16(len(params))
    for pid, pos, val in params:
        body += [*i16(pid), pos, *mag7(val)]
    return [start, build(P_PRESET_PARAMS, body), build(P_PRESET_END)]


# ---- reply decoding -----------------------------------------------------

def decode(msg):
    proc, body, ok = parse(msg)
    r = Reader(body)
    name = PROC_NAMES.get(proc, f"0x{proc:02X}")
    d = {"proc": proc, "name": name, "checksum_ok": ok}
    try:
        if proc == R_WHOAMI:
            d.update(bytes=[r.byte() for _ in range(5)], model=r.string(), device_name=r.string(),
                     guid=bytes(r.byte() for _ in range(16)).hex(), short_model=r.string())
        elif proc == R_CONFIG:
            d.update(bank=r.int16(), preset=r.int16(), extra=[r.byte() for _ in range(r.left())])
        elif proc == R_NAMES:
            bank, n = r.byte(), r.byte()
            d.update(bank=bank, names=[r.string() for _ in range(n) if r.left() > 0])
        elif proc == R_PRESET_START:
            d.update(bank=r.byte(), slot=r.byte(), preset_name=r.string(),
                     extra=[r.byte() for _ in range(r.left())])
        elif proc in (R_PRESET_PARAMS, R_GLOBALS):
            n = r.uint16()
            d.update(params=[(r.uint16(), r.byte(), r.mag7()) for _ in range(n)])
        elif proc == R_PARAM:
            d.update(pid=r.uint16(), pos=r.byte(), value=r.mag7())
        elif proc in (R_ACK, R_NACK):
            d.update(for_proc=r.byte(), code=r.byte())
        elif proc == R_NOTIFY:
            sub = r.byte()
            d.update(sub=sub, data=[r.byte() for _ in range(r.left())])
            if sub == 0 and len(d["data"]) >= 2:
                d["event"] = f"preset loaded on unit: bank {d['data'][0]} slot {d['data'][1]}"
            elif sub == 3 and len(d["data"]) >= 4:
                src_bank, src_slot, dst_bank, dst_slot = d["data"][:4]
                d["event"] = f"preset moved on unit: bank {src_bank} slot {src_slot} -> bank {dst_bank} slot {dst_slot}"
        else:
            d.update(raw=body)
    except IndexError:
        d.update(truncated=True, raw=body)
    return d
