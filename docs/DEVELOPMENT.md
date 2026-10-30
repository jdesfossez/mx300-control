# Development notes

## How the protocol was worked out
1. **The original editor.** The protocol was first documented by examining MX-Edit 2.2.0 (2015), the vendor's last release. The message layouts, request/reply pairs, parameter IDs, ranges and display formulas were written down as facts in [`PROTOCOL.md`](PROTOCOL.md) and [`data/mx300params.json`](../data/mx300params.json).
2. **The Leevi project.** Its message captures (Linux, 2011) were used as an independent cross-check.
3. **A real unit.** Everything was then confirmed with the Python tool in [`tools/mx300tool/`](../tools/mx300tool/): read-only checks first, then each kind of write after a backup. Items marked ✅ in the docs were seen on a unit. Items marked ⚠ are not confirmed.

The app was then written from those documents only.

## Architecture
| Code | |
|---|---|
| `core/Protocol.*` | SysEx framing, Super7 packing, message builders and decoders |
| `core/ParamModel.*`, `core/ParamData.cpp` | Effect types and parameters; raw value → display text. `ParamData.cpp` is generated from `data/mx300params.json` by `tools/gen_params.py` |
| `core/DeviceSession.*` | Connection state machine and device model (names, edit buffer, current program, system parameters). Operations wait for ACKs; parameter writes are coalesced and paced. No JUCE: the transport and clock are injected |
| `core/FakeUnit.*` | Simulated MX300 that behaves like the real one. Used by the tests and the demo unit |
| `core/tests/` | Protocol against frames captured from a real unit, display values against the unit's display, session against the simulated unit |
| `plugin/DeviceHub.*` | The shared CoreMIDI connection (or demo unit), port detection, hot-plug. Runs the session on the message thread |
| `plugin/PluginProcessor.*` | Host parameters ↔ unit, project state, tempo sync, whole-unit bypass |
| `plugin/PluginEditor.*`, `plugin/Theme.*` | The UI |
| `plugin/StandaloneApp.cpp` | The standalone app's window |
| `plugin/tools/Snapshot.cpp` | Renders the editor against the demo unit to PNG files and prints what a host sees |

Notes:
- **Connection sequence:**
  1. WhoAmI. If the unit reports OS-update mode, stop.
  2. Config.
  3. GUI mode on (the unit then reports front-panel changes).
  4. User and factory program names.
  5. System parameters.
  6. The edit buffer.

  GUI mode is turned off on close.
- **Host parameters:** these are fixed slots per processor: Bypass, Mix and Control 1–12. They map to whatever the loaded effect type uses, so automation survives type changes. Their names are reported dynamically.
- **Thread safety:** host → unit changes go through atomics and are drained by a 100 Hz timer on the message thread. Unit → host changes are pushed with a guard, so they aren't sent back to the unit.
- **Project restore:** writes the saved edit buffer back to the unit (`2B`/`2D`/`2C`) once the unit is ready, and only if it differs.
- **Whole-unit bypass:** this is the system parameter `0x3034`. It is not part of a program, so it never marks the program as edited.
- **Safety:** the firmware-update messages (`78`/`7A`/`7B`) are never built. Restore only sends files in which every message is a valid bulk-dump message.

## Parameter data
`data/mx300params.json` holds:
- 59 effect types and their parameter lists per routing;
- parameter ranges;
- the display model of each parameter: reverb time model, tempo/BPM, delay as a fraction of tempo, LFO rates, tables.

Its schema and formulas are in [`PARAMS.md`](PARAMS.md). The data was checked against every program of a unit and against the front-panel display for reverb, delay, modulation, compressor and de-esser types.

After editing the JSON, run `python3 tools/gen_params.py` to regenerate `core/ParamData.cpp`.

## Hardware findings worth knowing
These are all in `PROTOCOL.md`; the ones that shaped the code are:
- **PresetStart tail:** must end with `00 02`. The longer `00 02 00 02` is NACKed (code 9), although the parameters are still applied.
- **SetParam:** has no ACK and no echo. Bursts of about 680 messages per second were accepted. MovePreset, SetName, restore messages and edit-buffer writes are ACKed.
- **RequestParam on Type (`0x0C40`):** returns the type's menu index, not its ID. Changing the type fills in that type's defaults.
- **Config (`09`):** bytes 5–6 are the current bank and slot. The edited flag is the first trailing byte of `2B`.
- **Front-panel controls in dual routings:** knobs A/B/C control parameters 1–3 of the processor selected on the unit. One bypass button is processor 1's Bypass (`0x0C41` pos 2). The other is the global bypass `0x3034` pos 0.
- **Display quirks:** the unit's bypass LED doesn't follow a SetParam on `0x3034`. Its routing icon doesn't refresh after a routing change made from the app.

## Open questions
- The meaning of `70` notification sub-codes 1, 2, 4, 5 and 6.
- The other bytes of the Config reply (`09`).
- Most system parameter IDs (`0x30xx`); see `PARAMS.md`.
- Whether procs `3B` and `51` are used by the MX300 at all.
- Whether writing `0x3034` through a GlobalParameters (`11`) message would also update the bypass LED.

## Prior art
**Leevi** (Linux, libusb, 2011; http://leevi.sourceforge.net) was an earlier attempt that could read settings. Its message captures were useful for cross-checking.
