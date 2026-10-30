# MX300 Control

An open-source editor for the **Lexicon MX300** reverb on macOS (Apple Silicon). It runs as a standalone app and as an **AU / VST3 plugin**, built from the same code.

The plugin doesn't process audio. It controls the unit over USB:
- it shows every parameter of the loaded program in real units and follows changes made on the unit;
- the DAW can automate the unit's parameters;
- your DAW project saves the unit's sound.

The official MX-Edit software is 32-bit only and doesn't run on a modern Mac. This project re-implements the control protocol from our own documentation ([`docs/PROTOCOL.md`](docs/PROTOCOL.md)). It contains no vendor code, graphics or preset data.

> Unofficial. Not affiliated with, endorsed by or supported by Lexicon or Harman. "Lexicon" and "MX300" are used only to say which hardware this works with.

## Features
- **Programs:** browse the User and Factory banks with search. You can load, store to any user slot, rename, and step through programs. Before loading, you're asked whether to keep unsaved edits.
- **Routing and processors:** set the routing (Dual Mono, Cascade, Parallel, Mono Split, Stereo). Each processor shows its effect type, Bypass, Mix and every parameter of the type, displayed as on the unit (ms, s, BPM, Hz, dB…). Badges show which parameters the unit's A/B/C knobs control.
- **Whole-unit Bypass:** the same as the unit's own bypass button.
- **Live sync:** knobs, program changes and bypass on the unit show up immediately. Unplugging and replugging the unit is handled.
- **Backup / restore** of the user bank to a `.syx` file.
- **In a DAW:**
  - **Automation:** 29 automatable parameters, each named after what it currently controls, such as "Mid RT" or "2 Tempo".
  - **Project recall:** the project saves the unit's sound and writes it back when the project is reopened.
  - **Tempo sync:** an option makes delay tempos follow the DAW tempo.
  - **Multiple instances:** several instances share one connection.
- **Demo unit:** a simulated MX300 for trying the editor without hardware.

Tested with a real MX300 on macOS (Apple Silicon), in Logic Pro (AU) and as a standalone app.

## Install
Download the zip from the [latest release](https://github.com/jdesfossez/mx300-control/releases/latest) and follow its `INSTALL.txt`. The builds aren't signed by Apple, so after copying the files, clear the quarantine flag macOS puts on downloads:
```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"MX300 Control.component" \
  ~/Library/Audio/Plug-Ins/VST3/"MX300 Control.vst3" /Applications/"MX300 Control.app"
```

## Build from source
Requirements: macOS 12 or later on Apple Silicon, the Xcode command-line tools (`xcode-select --install`), CMake and Ninja (`brew install cmake ninja`).

```sh
git clone https://github.com/jdesfossez/mx300-control.git
cd mx300-control
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # downloads JUCE 9.0.4 on first run
cmake --build build
ctest --test-dir build                                      # core tests, no hardware needed
```

Install the plugins for your user:
```sh
R=build/plugin/MX300Control_artefacts/Release
cp -R "$R/AU/MX300 Control.component" ~/Library/Audio/Plug-Ins/Components/
cp -R "$R/VST3/MX300 Control.vst3" ~/Library/Audio/Plug-Ins/VST3/
open "$R/Standalone/MX300 Control.app"                      # or copy it to /Applications
```
These builds are not signed. They run on the Mac that built them.

## Using it
- Plug in the MX300 over USB. The app finds it automatically. To pick another port, use the `...` menu → MIDI port.
- **Header:** current program, previous/next, **Bypass**, **Store**, **Rename**, and the `...` menu (backup/restore, MIDI port, demo unit, plugin options).
- **Processors:** drag or scroll a knob, or use the arrow keys. Changing the effect type loads that type's defaults on the unit.
- **Demo unit:** in the `...` menu. You can also set `MX300_DEMO=1` in the environment to use it without changing your saved settings.

### In a DAW
- Insert the plugin on any track. Audio passes through untouched.
- **Automation:** Bypass, Mix and 12 controls per processor, plus "Bypass (whole unit)". Each parameter follows the loaded effect type. Its name tells you what it controls: "Mid RT" in Stereo, "1 Pre Delay" in dual routings, or "P2 Control 4 (unused)" for slots the type doesn't use. Some hosts only show new names after you reopen the plugin window.
- **Project recall:** the project saves the unit's current sound and writes it back when the project opens. You can turn this off in the `...` menu.
- **Sync delay tempo to the DAW** (`...` menu, saved per project): every delay's Tempo follows the host tempo, rounded to the unit's 1 ms steps. Delay Time is a fraction of the Tempo, so repeats stay on the beat.
- **Bypass:** the header button is the unit's global bypass. In hosts that tell the plugin when it is bypassed, the host's bypass drives it too. **Logic Pro doesn't**: it just stops running a bypassed plugin. In Logic, use the plugin's Bypass button or automate "Bypass (whole unit)".

### Known quirks of the unit
- When the app sets the global bypass, the unit bypasses, but its bypass LED doesn't light.
- After a routing change from the app, the routing icon on the unit's display only refreshes at the next change made on the unit.

## Repository layout
| Path | |
|---|---|
| `core/` | C++ library without JUCE: protocol, parameter model, device session, simulated unit, tests |
| `plugin/` | JUCE app and plugins: MIDI connection, host parameters, UI |
| `data/mx300params.json` | Effect types, parameters and display models (single source of truth) |
| `tools/gen_params.py` | Generates `core/ParamData.cpp` from the JSON |
| `tools/mx300tool/` | Python command-line tool used to work out and check the protocol on hardware |
| `docs/` | [Protocol](docs/PROTOCOL.md), [parameters](docs/PARAMS.md), [development notes](docs/DEVELOPMENT.md) |

## Contributing
Bug reports, captures from your unit and pull requests are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) first, especially the clean-room rules.

## License
GNU AGPL v3 or later (see [LICENSE](LICENSE)). This follows the open-source licence of JUCE, which the app and plugins are built with.
