# Contributing

Thanks for helping! Bug reports, protocol captures from a real unit, and code are all welcome.

## Clean-room rules (please read)
This project re-implements the MX300 control protocol from its own documentation. To keep it free of vendor material:
- **Don't add vendor material.** That means code, decompiled code, graphics, preset data, manuals or other text from Lexicon/Harman software or documentation, including MX-Edit and its plugins.
- **Factual protocol data is fine.** Message layouts, parameter IDs and ranges, display formulas and observed behaviour are facts. Document them in your own words in [`docs/`](docs/), and put parameter facts in [`data/mx300params.json`](data/mx300params.json).
- **Keep presets out.** Don't commit `.syx` dumps or other preset data from a unit; `.gitignore` excludes `*.syx`. Tests use the simulated unit (`core/FakeUnit.*`), with short frames typed into the tests.
- **Don't put "Lexicon" in product names.** Name the hardware only to say what the software works with.

## Safety
- **Never add code that builds the firmware-update messages** (procs `78`, `7A`, `7B`; see [`docs/PROTOCOL.md`](docs/PROTOCOL.md)).
- **Back up before testing a write.** Anything new that writes to the unit should first be tried by hand with a backup made beforehand (`tools/mx300tool/mx300.py dump backup.syx`).

## Building and testing
See the [README](README.md#build-from-source). Before sending a change:
```sh
cmake --build build && ctest --test-dir build
python3 tools/mx300tool/test_mx300proto.py && python3 tools/mx300tool/test_mx300params.py
```
- **UI changes:** `build/plugin/MX300Snapshot_artefacts/Release/MX300Snapshot <dir>` renders the editor against the demo unit to PNG files.
- **Parameter data changes:** edit `data/mx300params.json`, then run `python3 tools/gen_params.py` and commit the regenerated `core/ParamData.cpp`.

## Reporting what your unit does
Captures from real hardware are the most useful contribution. Run `python3 tools/mx300tool/mx300.py monitor` (setup in [`tools/mx300tool/README.md`](tools/mx300tool/README.md)) while you use the unit. Send the printed lines, or the relevant part of `mx300-traffic.log`, and say what you did on the unit. Open questions are listed in [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md#open-questions).

## Code style
Match the surrounding code: JUCE style in `plugin/`, the same conventions in `core/` (which must not depend on JUCE). Keep comments short and about *why*.
