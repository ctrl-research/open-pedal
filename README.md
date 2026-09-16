# OpenPedal

Build your own guitar pedals as small JSON files, chain them into pedalboards inside your DAW,
and share both with other players.

OpenPedal is a VST3 plugin plus a command-line renderer. It ships a handful of DSP building
blocks (filters, waveshapers, delay lines, LFOs, ...) and a few example pedals. The pedals
themselves are meant to be written by you: a pedal is a graph of blocks with the knobs you want
to expose, tuned however you like.

```
pedals/examples/ts-drive.json     a pedal: knobs + a graph of DSP blocks
boards/examples/starter.json      a board: which pedals, in what order, with what settings
```

## Status

Early. VST3 on macOS is the tested target; Linux builds in CI. AU and CLAP are planned.

## Quick start

Requirements: a C++20 compiler (Xcode Command Line Tools on macOS), and
[mise](https://mise.jdx.dev/) to install the pinned cmake and ninja from `.tool-versions`.

```sh
mise install
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The default preset copies `OpenPedal.vst3` into `~/Library/Audio/Plug-Ins/VST3` on macOS.
Rescan plugins in your DAW and add OpenPedal to a guitar track.

## Writing a pedal

Drop a `.json` file into your pedals folder and the plugin picks it up within a second:

- macOS: `~/Music/OpenPedal/pedals`
- Linux: `~/.local/share/open-pedal/pedals`
- Any platform: set `OPENPEDAL_PEDALS_DIR`

The folder is seeded with the example pedals on first run. Copy one, change its `id`, and edit.
The full format and the reference for every block live in [docs/PEDAL_FORMAT.md](docs/PEDAL_FORMAT.md).

Check a pedal from the terminal without opening a DAW:

```sh
open-pedal-render --check ~/Music/OpenPedal/pedals/my-fuzz.json
open-pedal-render boards/examples/starter.json guitar.wav out.wav
```

Errors name the offending node and list the valid options, for example:

```
node 'clipper' param 'shape' has unknown option "warm" (options: tanh, soft, hard, asymmetric, fold, diode)
```

## Sharing

**Export** in the plugin writes the board as JSON, either to a file or to the clipboard. Tick
"include pedal definitions" and the file also carries every pedal it uses, so a friend can
**Import** it with nothing installed and press **Install** on any pedal they want to keep.
Boards without embedded pedals stay tiny and resolve against the recipient's own pedals folder.

See [docs/BOARD_FORMAT.md](docs/BOARD_FORMAT.md) for the board schema and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the plugin is put together.

## Repository layout

```
src/core/       engine: parameters, board model, pedal graph compiler, DSP blocks (no plugin code)
src/plugin/     JUCE VST3: host parameters, chain swapping, editor UI
src/cli/        open-pedal-render: offline rendering, pedal validation, block reference
pedals/         example pedal definitions (bundled into the plugin)
boards/         example boards
tests/          Catch2 unit tests
docs/           formats and architecture
```

## License

MIT. JUCE is used under its GPLv3 / AGPLv3 open-source license terms.
