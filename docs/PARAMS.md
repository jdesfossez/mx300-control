# MX300 parameters

[`data/mx300params.json`](../data/mx300params.json) holds the MX300's effect types and parameters. Two readers turn raw values into display strings: `tools/mx300tool/mx300params.py`, and `core/ParamModel.cpp` through the generated `core/ParamData.cpp`. The facts (IDs, ranges, unit scales, the reverb-time constants) were taken from MX-Edit 2.2.0 and written in this project's own schema. They were then checked against all 99 user programs of a real unit (2026-10-29). Items marked ⚠ have not yet been compared with the unit's own display.

## Structure of a preset
A `2D` PresetParams message is a flat list of `(id, pos, value)`.

| pos | Meaning |
|---|---|
| 1 | Preset level: Routing `0x0C44` |
| 2 | Engine 1 |
| 3 | Engine 2 (only in the dual routings) |
| 0 | System/global (`0x30xx`, only in GlobalParameters `11`) |

Routing `0x0C44`: `0x0600` Dual Mono, `0x0601` Cascade, `0x0602` Parallel, `0x0603` Mono Split, `0x0604` Stereo.

- **Dual routings (`0x0600`–`0x0603`)** run two "half" engines (pos 2 and 3). Each has 3 parameters (4 for delays).
- **Stereo (`0x0604`)** runs one "whole" engine (pos 2) with up to 12 parameters, plus the A/B/C knob assignments `0x2142`/`0x2143`/`0x2144`. Each assignment value is `(pos << 16) | param id`; for example `0x020789` means knob → Pre-Delay on engine 1.

Each engine starts with Type `0x0C40` (value = effect-type ID from `types`), Bypass `0x0C41` (0 = effect active), and ends with Mix `0x0C42` (0–99). The type's parameters sit in between, in the order listed under `types[].params`; the unit sends them in that order.

## JSON schema
- `types[]`: `id`, `name`, `dna` (`half` = dual routings, `whole` = Stereo), `class` (reverb/delay/modulation), `params` (ordered IDs). In Stereo, Gated/Reverse Reverb and Spring use their half-DNA type IDs (`0x046A`–`0x046C`).
- `type_order.half` / `type_order.whole`: the type lists in the unit's menu order. The RT tables are indexed by position in these lists.
- `params{id}`: `name` (front-panel label), `min`, `max` (raw), `display` (below).
- `tables{}`: unit scales indexed by raw value (`unit` + `values`; `null` = "Off", or ∞ for the ratio table).
- `rt60{}`: reverb-time model constants.

## Display kinds
| kind | Display of raw value `v` |
|---|---|
| `int` | `v`, with optional `prefix`/`unit` (e.g. Pre-Delay `0x0789`: `v` ms) |
| `linear` | `v × scale + offset` `unit` (e.g. Threshold `v − 70` dB; Decay `200 + 10v` ms) |
| `choice` | `choices[v]` |
| `table` | `tables[table].values[v]` |
| `ms` | `v` milliseconds. For delay Tempo (`tempo`) the unit displays BPM = 60000 / `v` (✅ on hardware, 2026-10-29); decimals: none ≤ 60 ms, one ≤ 600 ms, two above (512 ms showed `117.2`) |
| `delay_time` | Fraction of the same engine's Tempo `T`: `v < 36` → `T·(0.25 + 0.75·v/36)` rounded; `v ≥ 36` → `T·(1 + (v−36)/36)` truncated (¼ beat … 1 beat at 36 … 2 beats at 72) |
| `feedback` | `v` (0–99); 100 = Repeat Hold |
| `lfo_hz` | `v < 95` → `0.05 + 0.01v` Hz, else `1 + 0.1(v − 95)` Hz (max 16 Hz). With `zero_is_stop`, 0 = Stop and the scale starts at 1 |
| `pan` | 99 = Center, below = Left `99 − v`, above = Right `v − 99` |
| `attack` | `v < 9` → `v/10` ms, else `v − 8` ms |
| `rt60` | Reverb time; see below |
| `type`, `routing` | Name lookup |

## Reverb time (`rt60`, Mid RT `0x078B` and Decay `0x07A4`)
The value `v` (0–99) moves a feedback gain `g` linearly between two endpoints; the displayed time follows from `g` and a loop length `k`:

```
x      = v / 99
g      = g_lo + (g_hi − g_lo) · x
per_m  = 637.5 / (−48000 · log10 g) · 0.9
n      = trunc(per_m · 64000)
RT [s] = trunc((n · k + 32) / 64) / 1000
```

- **Stereo (whole):** `rt60.whole[i] = [k, g_lo, g_hi]`, where `i` is the type's position in `type_order.whole`. If `k` is 0, `k` comes from `rt60.whole_size_m[i][Size]`, i.e. the reverb time scales with the Size parameter (the room size in metres).
- **Dual (half):** `rt60.half[i] = [k, rt_lo, rt_hi]` in seconds. The gain endpoints are `g = 10^(−637.5·k / (rt·48000))` for each `rt`; `k = 0` means the type has no RT (Gated and Reverse use a linear Decay instead).

Example: "Fan Tastic" (Stereo Small Hall, Size 4 = 14 m, Mid RT raw 76) → **1.369 s**. Both the whole and the half model matched the front panel ✅; the unit shows RT with millisecond resolution (e.g. `1.819s`, program 42 Vocal Plate raw 78).

## Known gaps
- ✅ Checked on the front panel (2026-10-29): Stereo Large Hall Pre Delay / Mid RT / Size; Cascade "EchoVerb" engine 1 Mono Delay Tempo (shown in BPM), Time, Feedback, and engine 2 Small Hall Pre Delay, Mid RT (half-DNA RT model), Liveliness.
- Dual routings: knobs A/B/C control parameters 1–3 of the engine selected on the front panel (a selector knob switches between processor 1 and 2). Switching the selector sends nothing over USB.
- Display strings are our own formatting. The values checked so far match the front panel; program 42 (Mono Split: Chorus + Vocal Plate), Compressor (Threshold, Ratio, Gain) and De-Esser (Ratio, Frequency, Bandwidth) also matched.
- The half-DNA tempo source for `delay_time` is assumed to be the engine's own Tempo parameter (`0x074C`/`0x0753`) ⚠.
- System parameters: Input Source `0x302E`, Clock Source `0x3028`, Digital Out `0x302C` and Unit Bypass `0x3034` (the global bypass button) were identified on the unit (only the values seen are named). The other GlobalParameters IDs (`0x301D`, `0x3021`, `0x3026`, `0x3027`, `0x302A`, `0x302F`, `0x3031`–`0x3033`, `0x3035`–`0x3037`) are still unknown.
- RequestParam on Type `0x0C40` returns the menu-list index, not the type ID (see PROTOCOL.md).
- Writing Pre-Delay `0x0789` to a Large Hall engine changed that engine's `0x0791` (see PROTOCOL.md) ⚠.
