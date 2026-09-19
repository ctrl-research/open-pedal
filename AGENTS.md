# CLAUDE.md

## Purpose

OpenPedal is a VST3 guitar-pedalboard plugin where pedals are user-authored JSON graphs of DSP
blocks and boards are shareable JSON. This file is the operating manual for humans and AI agents
working in the repo.

## Tech stack

- **C++20**, **CMake >= 3.28** with presets, **Ninja**. Tool versions are pinned in `.tool-versions`
  (install with `mise install`). Never assume a globally installed cmake.
- **JUCE 9** (VST3 wrapper, `juce::dsp` oversampling, GUI), **nlohmann/json**, **Catch2 v3**, all
  fetched by CMake `FetchContent` with exact tags in `cmake/Dependencies.cmake`.
- **pluginval** validates the built plugin in CI on macOS.

## Build, test, run

```sh
mise install                       # cmake + ninja from .tool-versions
cmake --preset default             # RelWithDebInfo, copies the VST3 to the user plugin folder
cmake --build --preset default
ctest --preset default             # 49 Catch2 test cases
build/default/src/cli/open-pedal-render_artefacts/RelWithDebInfo/open-pedal-render --check pedals/examples/*.json
```

Presets: `default`, `debug`, `ci` (Release, no plugin copy). Build dirs live under `build/<preset>`.
Binaries: `build/<preset>/src/plugin/OpenPedal_artefacts/<Config>/VST3/OpenPedal.vst3`,
`build/<preset>/src/cli/open-pedal-render_artefacts/<Config>/open-pedal-render`,
`build/<preset>/tests/openpedal_tests_artefacts/<Config>/openpedal_tests`.

## Structure

```
src/core/            engine, no JUCE plugin dependencies (only juce_audio_basics + juce_dsp)
  ParamDescriptor.*  knob metadata, tapers, real<->normalised
  PedalDescriptor.h  pedal metadata + knobs
  Pedal.h            IPedal: the one interface every pedal backend implements
  PedalDefinition.*  parsed pedal JSON (knobs + nodes + connections), parser with collected errors
  graph/Block.*      per-sample DSP block interface + BlockRegistry
  graph/blocks/      one file per block family; self-registering via BlockRegistrar
  graph/GraphPedal.* compiles a PedalDefinition into a runnable IPedal (validation, topo sort,
                     delay feedback, oversampling, knob smoothing)
  PedalCollection.*  all known pedal definitions (bundled, user folder, embedded), semver lookup
  Board.*            board model + JSON round-trip
  BoardLoader.*      Board + PedalCollection -> Chain, with a ResolutionReport
  Chain.*            realtime series chain: bypass, ring-out tails, latency
  Paths.*            user pedal folder location
src/plugin/          JUCE plugin: SlotParameter (host params), PluginProcessor, PluginEditor
src/cli/render.cpp   offline renderer, --check, --list-blocks
pedals/examples/     bundled example pedals (also seeded into the user folder on first run)
boards/examples/     example boards
tests/               Catch2; TestHelpers.h has compile/signal helpers
docs/                ARCHITECTURE.md, PEDAL_FORMAT.md (user-facing block reference), BOARD_FORMAT.md
```

## Conventions

- **Realtime rules.** Nothing on the audio path (`Chain::process`, `GraphPedal::process`,
  `Block::tick`) may allocate, lock, or block. Board edits build a new `Chain` on the message
  thread and swap it in atomically (`PluginProcessor::rebuildChain`).
- **Real units everywhere.** Knob values in boards, `IPedal::setParam`, and block params are Hz,
  ms, dB, or unit-less; only host parameters are normalised 0..1, converted through the descriptor.
- **Errors are for authors.** Parsing and compiling a pedal collects every problem and names the
  node, param, and valid options. Keep that standard when adding blocks or checks.
- **Adding a block:** implement `Block`, give it a `BlockSpec` with `doc` text for every param,
  register with `BlockRegistrar`, add a `link<Name>()` stub and call it from
  `ensureBuiltinBlocksRegistered()`, add a `BlockTests.cpp` case, and regenerate the reference
  table in `docs/PEDAL_FORMAT.md` with `open-pedal-render --list-blocks`.
- **Schema changes** to pedal or board JSON bump `kPedalSchemaVersion` / `kBoardSchemaVersion` and
  must keep loading older files.
- **Warnings.** First-party sources compile with strict warnings (`cmake/Warnings.cmake`); keep
  the build warning-free. JUCE sources are excluded on purpose.
- **Releases are automatic.** Merging to `main` runs `auto-release.yml`, which bumps from the
  latest tag according to the PR label (`major`/`minor`/`patch`, default patch), tags, and calls
  `release.yml`. The binary's version comes from `-DOPENPEDAL_VERSION`; local builds report 0.0.0.
  Never merge or tag on the maintainer's behalf.
- Versioning is SemVer as bare `X.Y.Z`. Branches follow `feat|bug|hotfix|release|chore/short-name`,
  commits follow Conventional Commits. Never push directly to `main`; all changes via PR.
- When adding a language or tool, pin it in `.tool-versions` first.
