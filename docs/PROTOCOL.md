# Lexicon MX300 — USB/MIDI SysEx protocol

Status: decoded statically from MX-Edit 2.2.0 (`MX300AU.component`, i386, symbols intact) and cross-checked against Leevi 0.1.1 captures (2011). **Items marked ⚠ have not yet been confirmed on hardware.** Items marked ✅ were confirmed on a real MX300 on 2026-10-29 (`probe` + `dump`, read-only; every checksum was valid).

## Transport
- The unit is a class-compliant **USB-MIDI** device (VID 0x1210, PID 0x0010). On macOS it appears in CoreMIDI, so plain SysEx works and you don't need libusb or a driver.
- The original software uses CoreMIDI `MIDISendSysex`. Leevi's "4-byte groups starting with 0x04" were simply USB-MIDI event packets.

## Frame
```
F0 06 <id0> <id1> <id2> <proc> <Super7 body …> <cs> F7
```
| Byte(s) | Meaning |
|---|---|
| `06` | Lexicon manufacturer ID |
| `id0 id1 id2` | `00 1A 05` for the MX300 (set by `SetIDs(0x00, 0x1A, 0x05)`). **WhoAmI** request uses `7F 1A 7F`; its reply comes back with `7F 7F 7F`. Bulk start/end (`19`/`1B`) come with `7E 1A 7F` ✅ |
| `proc` | message ID (raw byte; `GetProcID` = frame[5]) |
| body | **Super7** packed (below) |
| `cs` | XOR of every byte from frame[1] (`06`) up to the last body byte, `& 0x7F` |

**Super7 packing.** The body is split into groups of up to 7 bytes. Each group is preceded by one byte holding the groups' high bits: the 1st byte's bit 7 goes to `0x40`, the 2nd's to `0x20`, …, the 7th's to `0x01`. The data bytes themselves are sent `& 0x7F`.

**Body types.** `int16` is big-endian. Strings are NUL-terminated and at most 16 characters. **Mag7 value:** a value < 0x80 is one byte. Otherwise the first byte is `0x80|n`, followed by n big-endian bytes; the writer uses `0x82 hi lo` up to 0xFFFF and `0x83 b2 hi lo` above that.

## Banks
`0` = factory, `1` = user, `4` = **edit buffer** (slot 0). Slots are 0-based.

## Host → unit
| proc | Name | Body |
|---|---|---|
| 01 | WhoAmI | `7F 7F 7F` (header `7F 1A 7F`) |
| 08 | RequestConfig | — |
| 10 | RequestGlobalParameters ✅ | — (reply `11`) |
| 18 | RequestBulkDump | `00` |
| 20 | ReceiveUserPresetTable | `01 n <n bytes>` ⚠ |
| 21 | RequestPresetNames | `bank` |
| 29 | **SetPresetName (rename)** ✅ | `bank slot name\0` |
| 2A | RequestPreset | `bank slot` |
| 2B | PresetStart ✅ | `bank slot name\0 edited 02`. **Send `00 02`**: the MX300 NACKs MX-Edit's `00 02 00 02` with code 9 (the parameters still get applied). `edited` = 1 when the unit sends the edit buffer after it was modified (seen after a type change) |
| 2D | PresetParams | `int16 count`, then per parameter: `int16 id, byte pos, Mag7 value` |
| 2C | PresetEnd | — |
| 39 | **MovePreset** ✅ | `srcBank srcSlot dstBank dstSlot name\0-or-00 flag(1)` |
| 3A | RequestModifierLinkableList | `int16` ⚠ |
| 40 | RequestParam ✅ | `int16 id, byte pos` |
| 41 | **SetParam** ✅ (no ACK; confirm with RequestParam) | `int16 id, byte pos, Mag7 value` |
| 50 / 54 / 56 / 57 | Object names / get / move / delete | MX400 objects, probably unused on the MX300 ⚠ |
| 78 / 7A / 7B | Flash sector order / data / start OS download | **Firmware update. Do not use.** |
| 7E | ACK | — (the original never sends it during normal use) |

### High-level operations (as the original app does them)
- **Connect:** WhoAmI, then wait for `02`. If its 4th byte is ≠ 0 the unit is in OS-update/boot mode; stop.
  Then RequestConfig, wait for `09`, then **GUI mode on** = SetParam `0x300A` pos 0 value 1 (body `30 0A 00 01`).
  Then RequestPresetNames 0 and 1, then RequestPreset 4 0.
- **Disconnect:** GUI mode off (SetParam `0x300A`, 0, 0).
- **Load a preset:** MovePreset `bank slot → 04 00`, no name, flag 1; wait about 2 ms; then RequestPreset `04 00`.
- **Store the edit buffer:** MovePreset `04 00 → 01 slot` with the name and flag 1, then RequestPresetNames 1.
- **Rename:** SetPresetName `01 slot name`, then RequestPresetNames 1.
- **Edit a parameter live:** SetParam id/pos/value. This only takes effect when MIDI is enabled and the app isn't loading a preset.
- **Write a whole preset into the edit buffer** ✅ (e.g. from a DAW session): PresetStart `04 00 name 00 02`, then PresetParams, then PresetEnd. Each is ACKed (`2B` immediately, `2D` after ~350 ms, `2C` after ~600 ms); the read-back is identical and the display shows the new name.
- **Backup:** RequestBulkDump ✅. The unit streams `19`, then 99 × (`2B 01 slot` / `2D` / `2C`) for the **user bank only**, then `11` GlobalParameters, then `1B`: 300 messages, ~13 KB. The original saves the raw SysEx bytes to disk.
- **Restore** ✅: send that raw byte stream back unchanged (25 ms between messages worked), then refresh the names and RequestPreset 4 0.

## Unit → host
| proc | Name | Body |
|---|---|---|
| 02 | WhoAmI reply ✅ | `00 1A 05 mode 02`, `"Lexicon Mx300\0"`, device name `"My Mx300\0"`, 16 bytes (looks like a GUID), `"Mx300\0"`. `mode` (4th byte) ≠ 0 means OS-update mode; it was `00` on a running unit |
| 09 | Config ✅ | 8 bytes. Observed `00 11 00 01 02 01 04 00` while the edit buffer held factory #5 "Fan Tastic", so the bytes are **not** `int16 bank, int16 preset` as first assumed (bank would be 17). Bytes 5–6 are the **current bank and slot** ✅ (seen as `01 04`, `01 29`, `01 2C` for programs 5, 42, 45). The other bytes stayed `00 11 00 01 02 … 00` in every reading; `0x11` = 17 is also the global-parameter count ⚠ |
| 11 | GlobalParameters ✅ | Same layout as `2D`: `int16 count` (17), then `int16 id, byte pos, Mag7 value`. All IDs are `0x30xx` system parameters (`3026 3021 3027 3028 302A 302C 302E 302F 3006 3035 3036 3037 3032 3033 3031 3034 301D`) |
| 19 / 1B | Bulk start / end ✅ | Header `7E 1A 7F`. `19` body `01 2B 00 00 28 F2 63` (meaning open); `1B` has no body |
| 22 | PresetNames ✅ | `bank count`, then `count` × `name\0`. 99 names per bank, space-padded to 16 characters. Takes ~0.9 s to arrive |
| 2B / 2D / 2C | Preset start / params / end ✅ | same layout as host → unit; names come back space-padded to 16 characters |
| 3B | ModifierList | ⚠ |
| 41 | ParamValue ✅ | `int16 id, byte pos, Mag7 value`. In GUI mode the unit sends one per knob detent, about every 40 ms while turning. Values are raw integers |
| 51 | ObjectNames | ⚠ |
| 70 | DeviceNotification | `sub …`. `03 srcBank srcSlot dstBank dstSlot` ✅ = a preset was moved; turning the front-panel PROGRAM knob sends `03 01 slot 04 00` (user preset → edit buffer). `00 bank slot` (preset loaded) was **not** seen on a program change ⚠ |
| 7E | ACK ✅ | `procID code`, code `00` = OK. Sent for MovePreset `39`, SetPresetName `29`, and for **every** message of a restore (`19`, each `2B`/`2D`/`2C`, `11`, `1B`; the `19`/`1B` ACKs use header `7E 1A 7F`). **Not** sent for SetParam `41` |
| 7F | NACK | `procID err` |

## Parameter IDs
See **[`PARAMS.md`](PARAMS.md)** and [`data/mx300params.json`](../data/mx300params.json): 59 effect types, 126 parameters, ranges, and how raw values map to display units (including the reverb-time model).
- Positions: `1` = preset level (routing `0x0C44`), `2` = engine 1, `3` = engine 2, `0` = system/global.
- Each engine has Type `0x0C40` (value = effect-type ID), Bypass `0x0C41`, Mix `0x0C42`, then the parameters of its type, in a fixed order.
- In Stereo routing (`0x0604`) the preset also carries the A/B/C knob assignments `0x2142`–`0x2144` = `(pos << 16) | param id`.

## Notes vs. Leevi
- Leevi's "program select" was really MovePreset into the edit buffer. Its "unknown" `41 00 0C …` commands are SetParam on system-level parameter IDs.
- Leevi framed packets incorrectly (it padded with CIN 4 instead of using CIN 5/6/7 end packets). That's a likely cause of the unit "hanging". CoreMIDI does this framing correctly.
- The original app sends no keep-alive or polling. It relies on GUI mode and the unit's notifications.

## Hardware observations (2026-10-29)
- The CoreMIDI port is named `Lexicon MX300 Lexicon Win USB 1-2 In/Out`.
- The edit buffer `2D` of factory "Fan Tastic" holds 19 parameters. They include `0x0C44`/`0x0C40`/`0x0C41`/`0x0C42` (pos 1–2, probably routing/effect selection), effect parameters in Leevi's type-A range (`0x0789`, `0x078B`, `0x079D` …, pos 2), and `0x2142`–`0x2144`, whose values are `0x02_0789`, `0x02_078B`, `0x02_079D`, i.e. `(pos << 16) | param id`. ✅ These are the **A/B/C knob assignments**: on "Fan Tastic" knob A sent `0x0789` (Pre-Delay), B sent `0x078B` (Mid RT) and C sent `0x079D` (Size), matching `0x2142`/`0x2143`/`0x2144`.
- The user bank on this unit still holds copies of the factory programs. The unit plays from the **user bank**: program 5 on the display = bank 1, slot 4.
- No ACK (`7E`) came back for GUI mode on/off (SetParam `0x300A`).
- Edit-buffer values match the knobs: Pre-Delay `0x0789` = 0, Mid RT `0x078B` = 76, Size `0x079D` = 4 (raw). Mapping raw values to display units is still open.
- python-rtmidi has no wheel for Python 3.13/3.14 on macOS arm64, and the source build fails (`sem_timedwait`). Use Python 3.12.

### Write tests (2026-10-29, all OK)
- `load 1 5`: MovePreset `01 05 → 04 00` was ACKed after ~240 ms; the edit buffer then read back as "Wide Load" (program 6).
- `set 0x0789 2 10`: no ACK; RequestParam then returned value 10. The loaded program was Wide Load (Large Hall), whose Pre-Delay is `0x0791`, not `0x0789`. Afterwards the edit buffer showed `0x0791` = 10 (it was 0 in the backup), so the unit seems to apply a Pre-Delay write to the engine's own Pre-Delay parameter ⚠. The plugin should still write the ID from the current type's list.
- `store 98 "Test Store"`: MovePreset `04 00 → 01 62` + name was ACKed after ~60 ms; the names list showed it at slot 98.
- `rename 98 "Test Rename"`: ACKed after ~25 ms.
- `restore backup.syx`: all 300 messages ACKed with code 0; slot 98 is back to "Wayback Machine".
- The restore does **not** touch the edit buffer: it still held the test state (named "Test Rename") afterwards.

### Front-panel and edit tests (2026-10-29)
- **Bypass** button: the unit sends ParamValue `0x0C41` pos 2 = 1 (on) / 0 (off).
- **Tap** button (on a delay): one ParamValue with the new Tempo, e.g. `0x074C` = 734 (ms).
- **System menu**: each change is sent live as ParamValue at pos 0: Input Source `0x302E` (1 = Analog Mono, 2 = Analog Stereo), Clock Source `0x3028` (0 = Internal 48 kHz, 1 = Digital), Digital Out `0x302C` (0 = Processed, 1 = Dry).
- **Bypass buttons** (two on the panel): one is processor 1's Bypass (`0x0C41` pos 2); the other is the unit's global bypass, a system parameter: ParamValue `0x3034` pos 0 = 1 (bypassed) / 0. It leaves the processors' own Bypass alone and is not part of the program (✅ 2026-10-29). The app writes it with SetParam `0x3034` pos 0: the unit bypasses and stays in sync, but its bypass LED does not light (unlike SetParam on a processor's `0x0C41`, which updates its LED) ✅ 2026-10-29. Untried: writing it through a GlobalParameters (`11`) message instead.
- **Routing change** by SetParam `0x0C44` pos 1 works (the unit reconfigures; the app reloads the edit buffer), but the routing icon on the unit's display only refreshes at the next change made on the unit ✅ 2026-10-29.
- **Effect type change** (SetParam `0x0C40` = type ID): accepted. The edit buffer then holds the new type with default parameter values. RequestParam `0x0C40` answers with the type's **index in the menu list** (Compressor = 31, De-Esser = 30 in the half-DNA list), not its ID; the `2D` dump still has the ID.
- **Load a factory preset** (MovePreset `00 slot → 04 00`): ACKed.
- The processor-select knob sends nothing.
- **SetParam burst** (`burst 0x0C42 2 0 99`): 100 messages at 5 ms (~155/s) and at 1 ms (~680/s, 0.15 s total) were all accepted: no NACK, and the final RequestParam returned the last value. The unit sends no ParamValue echo for host writes, so a plugin won't see its own automation come back. Whether every intermediate value was applied can't be observed.
