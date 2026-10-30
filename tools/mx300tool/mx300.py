#!/usr/bin/env python3
"""
mx300 - talk to a Lexicon MX300 over USB-MIDI (CoreMIDI) using the reversed protocol.

  pip3 install mido python-rtmidi
  python3 mx300.py ports
  python3 mx300.py probe              # read-only: who-am-i, config, names, edit buffer
  python3 mx300.py show               # read-only: edit buffer with parameter names and values
  python3 mx300.py monitor            # print everything the unit sends (turn knobs!)
  python3 mx300.py dump backup.syx    # bulk-dump all presets to a file (read-only)
  python3 mx300.py load 1 5 --yes     # recall user preset 6 into the edit buffer
  python3 mx300.py set 0x0789 2 40 --yes
  python3 mx300.py store 5 "My Hall" --yes
  python3 mx300.py rename 5 "Big Hall" --yes
  python3 mx300.py restore backup.syx --yes

Every message in and out is appended to mx300-traffic.log (send it back to Claude).
"""
import argparse
import sys
import time
from datetime import datetime

import mx300params as params
import mx300proto as p

try:
    import mido
except ImportError:
    sys.exit("Missing dependency: pip3 install mido python-rtmidi")

LOG = "mx300-traffic.log"


def hexs(b):
    return " ".join(f"{x:02X}" for x in b)


def log(direction, data):
    with open(LOG, "a") as f:
        f.write(f"{datetime.now().isoformat(timespec='milliseconds')} {direction} {hexs(data)}\n")


def find_port(names, hint):
    for n in names:
        if hint.lower() in n.lower():
            return n
    for n in names:
        if "mx300" in n.lower() or "lexicon" in n.lower():
            return n
    return None


class Unit:
    def __init__(self, hint=""):
        ins, outs = mido.get_input_names(), mido.get_output_names()
        i, o = find_port(ins, hint), find_port(outs, hint)
        if not i or not o:
            sys.exit(f"MX300 not found.\n inputs: {ins}\n outputs: {outs}\nUse --port <part of name>.")
        print(f"using in='{i}' out='{o}'")
        self.inp = mido.open_input(i)
        self.out = mido.open_output(o)

    def send(self, frame):
        log(">>", frame)
        self.out.send(mido.Message("sysex", data=list(frame[1:-1])))

    def recv(self, timeout=1.0, until=None):
        """Collect decoded replies until `until` proc arrives or timeout of silence."""
        got, deadline = [], time.time() + timeout
        while time.time() < deadline:
            msg = self.inp.poll()
            if msg is None:
                time.sleep(0.005)
                continue
            if msg.type != "sysex":
                continue
            raw = bytes([0xF0, *msg.data, 0xF7])
            log("<<", raw)
            try:
                d = p.decode(raw)
            except ValueError:
                d = {"name": "FOREIGN", "raw": list(raw)}
            got.append((raw, d))
            deadline = time.time() + timeout
            if until is not None and d.get("proc") == until:
                break
        return got

    def ask(self, frame, until, timeout=1.5):
        self.send(frame)
        return self.recv(timeout, until)

    def close(self):
        self.inp.close()
        self.out.close()


def show(replies):
    for raw, d in replies:
        flag = "" if d.get("checksum_ok", True) else "  [BAD CHECKSUM]"
        body = {k: v for k, v in d.items() if k not in ("proc", "name", "checksum_ok")}
        print(f"  <- {d['name']}{flag} {body}")


def cmd_probe(u, a):
    print("WhoAmI");      show(u.ask(p.req_whoami(), p.R_WHOAMI))
    print("Config");      show(u.ask(p.req_config(), p.R_CONFIG))
    print("GUI mode on"); u.send(p.gui_mode(True)); show(u.recv(0.5))
    for bank in (p.BANK_FACTORY, p.BANK_USER):
        print(f"Names bank {bank}"); show(u.ask(p.req_names(bank), p.R_NAMES, 2.0))
    print("Edit buffer");  show(u.ask(p.req_preset(p.BANK_EDIT, 0), p.R_PRESET_END, 2.0))
    u.send(p.gui_mode(False))


def read_edit_buffer(u):
    for raw, d in u.ask(p.req_preset(p.BANK_EDIT, 0), p.R_PRESET_END, 2.0):
        if d["name"] == "PRESET_START":
            print(f"edit buffer: {d['preset_name'].strip()!r}")
        if d["name"] == "PRESET_PARAMS":
            return d["params"]
    return []


def cmd_show(u, a):
    for line in params.describe(read_edit_buffer(u)):
        print("  " + line)


def read_globals(u):
    for raw, d in u.ask(p.req_globals(), p.R_GLOBALS, 2.0):
        if d["name"] == "GLOBALS":
            return {pid: v for pid, pos, v in d["params"]}
    return {}


def cmd_monitor(u, a):
    u.send(p.gui_mode(True))
    edit = {(pid, pos): v for pid, pos, v in read_edit_buffer(u)}
    globals_before = read_globals(u)
    print("Listening (Ctrl-C to stop). Turn knobs / change programs on the unit.")
    try:
        while True:
            for raw, d in u.recv(0.5):
                if d["name"] == "PARAM_VALUE":
                    edit[(d["pid"], d["pos"])] = d["value"]
                    eng = params.engines_from_params([(i, pos, v) for (i, pos), v in edit.items()])
                    print(f"  <- {params.name(d['pid'])} = {params.display(d['pid'], d['value'], eng.get(d['pos']))}"
                          f"   (0x{d['pid']:04X} pos {d['pos']} = {d['value']})")
                    continue
                show([(raw, d)])
                if d["name"] == "NOTIFY" and d.get("sub") == 3:  # program changed: reload context
                    edit = {(pid, pos): v for pid, pos, v in read_edit_buffer(u)}
                    show(u.ask(p.req_config(), p.R_CONFIG))
    except KeyboardInterrupt:
        pass
    finally:
        changed = {k: (globals_before.get(k), v) for k, v in read_globals(u).items() if globals_before.get(k) != v}
        print("global parameters changed during the session:" if changed else "no global parameter changed")
        for pid, (old, new) in changed.items():
            print(f"  0x{pid:04X}: {old} -> {new}")
        u.send(p.gui_mode(False))


def cmd_dump(u, a):
    u.send(p.req_bulk_dump())
    frames = [raw for raw, d in u.recv(5.0, p.R_BULK_END)]
    with open(a.file, "wb") as f:
        for fr in frames:
            f.write(fr)
    print(f"wrote {len(frames)} SysEx messages to {a.file}")


def need_yes(a):
    if not a.yes:
        sys.exit("This writes to the unit. Re-run with --yes (back up first with: dump backup.syx).")


def cmd_load(u, a):
    need_yes(a); show(u.ask(p.load_preset(a.bank, a.slot), p.R_ACK))
    time.sleep(0.01); show(u.ask(p.req_preset(p.BANK_EDIT, 0), p.R_PRESET_END, 2.0))


def cmd_set(u, a):
    need_yes(a)
    u.send(p.gui_mode(True)); u.recv(0.3)
    u.send(p.set_param(int(a.pid, 0), int(a.pos, 0), int(a.value, 0)))  # the unit doesn't ACK SetParam
    show(u.recv(0.1))
    show(u.ask(p.req_param(int(a.pid, 0), int(a.pos, 0)), p.R_PARAM))
    u.send(p.gui_mode(False))


def cmd_store(u, a):
    need_yes(a); show(u.ask(p.store_preset(a.slot, a.name), p.R_ACK))
    show(u.ask(p.req_names(p.BANK_USER), p.R_NAMES, 2.0))


def cmd_rename(u, a):
    need_yes(a); show(u.ask(p.rename_preset(a.slot, a.name), p.R_ACK))
    show(u.ask(p.req_names(p.BANK_USER), p.R_NAMES, 2.0))


def read_preset(u, bank, slot):
    name, prm = None, []
    for raw, d in u.ask(p.req_preset(bank, slot), p.R_PRESET_END, 2.0):
        if d["name"] == "PRESET_START":
            name = d["preset_name"].strip()
        if d["name"] == "PRESET_PARAMS":
            prm = d["params"]
    return name, prm


def cmd_write_edit(u, a):
    need_yes(a)
    name, prm = read_preset(u, a.bank, a.slot)
    if not prm:
        sys.exit(f"could not read bank {a.bank} slot {a.slot}")
    tail = (0, 2, 0, 2) if a.long_tail else (0, 2)
    print(f"writing {name!r} ({len(prm)} params) into the edit buffer, tail {tail}")
    for fr in p.preset_messages(name, prm, tail=tail):
        u.send(fr)
        show(u.recv(0.3))
    back_name, back = read_preset(u, p.BANK_EDIT, 0)
    same = sorted(back) == sorted(prm)
    print(f"read back {back_name!r}: {'IDENTICAL' if same else 'DIFFERENT'}")
    if not same:
        print("  missing:", sorted(set(prm) - set(back)))
        print("  extra:  ", sorted(set(back) - set(prm)))


def cmd_burst(u, a):
    need_yes(a)
    pid, pos = int(a.pid, 0), int(a.pos, 0)
    lo, hi = int(a.start, 0), int(a.end, 0)
    step = 1 if hi >= lo else -1
    u.send(p.gui_mode(True)); u.recv(0.3)
    t0 = time.time()
    for v in range(lo, hi + step, step):
        u.send(p.set_param(pid, pos, v))
        time.sleep(a.ms / 1000)
    sent = abs(hi - lo) + 1
    print(f"sent {sent} SetParam in {time.time() - t0:.2f}s ({a.ms} ms apart)")
    replies = u.recv(0.5)
    show([r for r in replies if r[1]["name"] != "PARAM_VALUE"])
    echoes = sum(1 for r in replies if r[1]["name"] == "PARAM_VALUE")
    print(f"unit sent {echoes} ParamValue messages back during/after the burst")
    show(u.ask(p.req_param(pid, pos), p.R_PARAM))
    u.send(p.gui_mode(False))


def cmd_restore(u, a):
    need_yes(a)
    data = open(a.file, "rb").read()
    frames, cur = [], []
    for b in data:
        cur.append(b)
        if b == 0xF7:
            frames.append(bytes(cur)); cur = []
    # Only send back what a bulk dump contains: never firmware (78/7A/7B) or anything unexpected.
    allowed = (p.R_BULK_START, p.R_BULK_END, p.R_PRESET_START, p.R_PRESET_PARAMS, p.R_PRESET_END, p.R_GLOBALS)
    for fr in frames:
        d = p.decode(fr)
        if not d.get("checksum_ok") or len(fr) < 6 or fr[5] not in allowed:
            sys.exit(f"{a.file}: not an MX300 bulk dump (message {fr[:8].hex(' ')}...), nothing sent")
    for fr in frames:
        u.send(fr); time.sleep(0.02)
    print(f"sent {len(frames)} messages"); show(u.recv(2.0))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="", help="substring of the MIDI port name")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("ports")
    sub.add_parser("probe")
    sub.add_parser("show")
    sub.add_parser("monitor")
    s = sub.add_parser("dump"); s.add_argument("file")
    s = sub.add_parser("write-edit", help="copy a stored preset into the edit buffer via 2B/2D/2C"); s.add_argument("bank", type=int); s.add_argument("slot", type=int); s.add_argument("--long-tail", action="store_true", help="end PresetStart with MX-Edit's 00 02 00 02 (NACKed by the MX300)"); s.add_argument("--yes", action="store_true")
    s = sub.add_parser("burst", help="sweep a parameter quickly, like automation"); s.add_argument("pid"); s.add_argument("pos"); s.add_argument("start"); s.add_argument("end"); s.add_argument("--ms", type=float, default=5); s.add_argument("--yes", action="store_true")
    s = sub.add_parser("restore"); s.add_argument("file"); s.add_argument("--yes", action="store_true")
    s = sub.add_parser("load"); s.add_argument("bank", type=int, help="0=factory 1=user"); s.add_argument("slot", type=int, help="0-based"); s.add_argument("--yes", action="store_true")
    s = sub.add_parser("set"); s.add_argument("pid"); s.add_argument("pos"); s.add_argument("value"); s.add_argument("--yes", action="store_true")
    s = sub.add_parser("store"); s.add_argument("slot", type=int, help="user slot, 0-based"); s.add_argument("name"); s.add_argument("--yes", action="store_true")
    s = sub.add_parser("rename"); s.add_argument("slot", type=int); s.add_argument("name"); s.add_argument("--yes", action="store_true")
    a = ap.parse_args()

    if a.cmd == "ports":
        print("inputs: ", mido.get_input_names()); print("outputs:", mido.get_output_names()); return
    u = Unit(a.port)
    try:
        globals()["cmd_" + a.cmd.replace("-", "_")](u, a)
    finally:
        u.close()


if __name__ == "__main__":
    main()
