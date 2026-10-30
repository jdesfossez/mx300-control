# mx300 test tool

A Python implementation of the MX300 SysEx protocol (see [`docs/PROTOCOL.md`](../../docs/PROTOCOL.md)). It was used to check the protocol on real hardware before the app was written. It is still the quickest way to see what the unit sends (`monitor`) or to try a message by hand.

## Setup (macOS, Apple Silicon)
```sh
cd tools/mx300tool
python3.12 -m venv .venv && source .venv/bin/activate
pip install mido python-rtmidi
python3 test_mx300proto.py        # offline self-test
python3 test_mx300params.py
python3 mx300.py ports            # the MX300 should be listed
```

## Commands
Start with the read-only ones, and make a backup before anything that writes:
1. `python3 mx300.py probe`: read-only. It identifies the unit and reads the program names and the edit buffer.
2. `python3 mx300.py show`: read-only. Prints the edit buffer with parameter names and display values (see [`docs/PARAMS.md`](../../docs/PARAMS.md)).
3. `python3 mx300.py dump backup.syx`: a **full backup** before anything that writes.
4. `python3 mx300.py monitor`: turn a few knobs and change programs on the unit, then press Ctrl-C.
5. Optional writes (each needs `--yes`): `load 1 0`, `set <id> <pos> <value>`, `store <slot> "<name>"`, `rename …`, `restore backup.syx`.

Each run appends the raw traffic to `mx300-traffic.log`. That file is useful when reporting what your unit does (see [CONTRIBUTING.md](../../CONTRIBUTING.md)). The tool never sends the firmware-update messages.

Unofficial and not affiliated with Lexicon/Harman. It contains no vendor code.
