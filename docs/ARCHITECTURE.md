# Architecture

OpenPedal has three layers. The **core** knows nothing about plugins: it parses pedals and
boards, compiles pedal graphs, and processes mono float audio. The **plugin** wraps the core in
JUCE, maps knobs onto host parameters, and owns the editor. The **CLI** drives the core offline.

```
 pedal JSON ──parsePedal──▶ PedalDefinition ──GraphPedal::compile──▶ IPedal (GraphPedal)
                                                                         │
 board JSON ──boardFromJson──▶ Board ──buildChain(Board, PedalCollection)──▶ Chain ──process──▶ audio
                                                    │
                                          ResolutionReport (local / embedded / missing / failed)
```

## Core (`src/core`)

**Descriptors.** `ParamDescriptor` describes a knob: type, real-unit range, taper, smoothing,
enum labels. It owns the real ↔ normalised mapping used by the plugin's host parameters.
`PedalDescriptor` adds identity, category, oversampling, and tail.

**IPedal** is the single interface between the engine and any pedal backend: `prepare`,
`reset`, `setParam(id, realValue)`, `process(float*, n)`, `latencySamples`. Today the only
backend is `GraphPedal`; a Faust or native backend would implement the same five methods and
plug into `PedalCollection` unchanged.

**Blocks** (`graph/Block.h`, `graph/blocks/`). A `Block` is a per-sample DSP unit with named
input ports and indexed parameters; its `BlockSpec` carries defaults, ranges, enum labels, and
documentation. Blocks self-register into `BlockRegistry` at static-init time. The spec is the
source of truth for validation, error messages, and the generated block reference.

**GraphPedal::compile** turns a `PedalDefinition` into a runnable pedal:

1. Instantiate blocks; unknown types are errors.
2. Resolve node parameters: literals are range-checked, labels are looked up, knob bindings are
   type-checked (numeric vs enum) and enum labels mapped to block option indices.
3. Resolve connections to (node, port) pairs; `in` and `output` are pseudo-nodes.
4. Detect feedback: an edge into a `delay` whose source is reachable from that delay is marked
   as feedback and excluded from ordering. Kahn's algorithm orders the rest; a leftover cycle is
   reported with the nodes involved.
5. Flatten ports into contiguous source arrays for a tight per-sample loop.

At runtime each sample runs nodes in order; a port sums its sources' current outputs, and a
feedback source (later in the order) still holds the previous sample, which gives one-sample
delayed feedback for free. Knob values are linearly smoothed and pushed into bound parameters
every 16 samples. If `oversampling > 1`, the pedal wraps the loop in `juce::dsp::Oversampling`
and reports its latency.

**Chain** is a prepared series of `IPedal` slots, each with its own switch and an optional group
index; up to four group switches gate their members together. A slot is active when both are
on. Inactive pedals that declare `tail_seconds` keep running on silence and their output is added to the dry signal until
the tail expires, then they are reset. `process()` never allocates.

**PedalCollection** holds every known `PedalDefinition` with an origin (`bundled`, `user`,
`embedded`) and answers `find(id, versionRequirement)` with the newest match. It compiles each
definition once at load time so authors see graph errors immediately, and again per
`instantiate()` so every board slot has private state.

**buildChain** resolves each `PedalInstance` of a `Board` against the collection first, then the
board's embedded definitions, applies the instance's knob values (enum labels become indices),
and records what happened in a `ResolutionReport`. Unresolvable slots become null placeholders
that `Chain` skips.

## Plugin (`src/plugin`)

**Host parameters.** VST3 needs a fixed parameter list at construction, so the processor exposes
4 group switches plus 8 slots × (1 bypass + 8 knobs). Each `SlotParameter` holds a normalised value and a pointer to
the `ParamDescriptor` of whatever knob currently sits in that position. The descriptor supplies
name, units, text formatting, step count for enums, and the real-unit mapping. When the board
changes the pointers are re-targeted and the host is told parameter info changed.

**Threading.** The board is a message-thread model. Every edit goes: sync current parameter
values back into the board → mutate → `buildChain` → `prepare` → atomic pointer swap →
re-sync parameters from the new chain → notify the editor. The previous chain sits in a
graveyard for one second so an in-flight audio callback can finish with it. The audio thread
reads the active chain pointer, drains parameter change flags into `setParam`/`setEnabled`,
and processes channel 0 (a stereo input is summed), fanning the result to all outputs.

**Pedal folder.** Bundled example pedals are compiled into the binary (`juce_add_binary_data`)
and copied into the user folder on first run. A 500 ms timer hashes the folder's file names,
sizes, and newest modification time; on change the user origin is reloaded and the chain rebuilt.

**State.** Session state is `{ "board": ..., "ui": ... }` as UTF-8 JSON, with embedded pedal
definitions included so a session survives a missing pedal folder. Export strips `ui` and adds
or drops `pedals` according to the "include pedal definitions" choice.

**Editor.** `PedalPanel` builds its controls from the slot's live descriptors: rotary sliders
for floats and ints (attached to the host parameter so automation and the UI agree), combo
boxes for enums, toggles for bools, and a group selector. Dragging a panel's header reorders
the chain (`PedalStrip` is the drop target); removing a pedal asks for confirmation. Group
buttons in the top bar toggle their members and rename on right-click. Panels show resolution
problems inline and an **Install** button for embedded pedals.

## CLI (`src/cli/render.cpp`)

`open-pedal-render board.json in.wav out.wav` builds the same `Chain` the plugin would and
renders offline; exit code 3 signals unresolved slots. `--check` validates pedal files and
`--list-blocks` prints the block reference used in `docs/PEDAL_FORMAT.md`.

## Deliberate limits in this version

- Mono signal path; stereo inputs are summed, outputs duplicated.
- 8 slots and 8 host-visible knobs per pedal.
- Whole-chain rebuild on any board or pedal-file change (delay tails reset).
- Series routing only; no parallel paths between pedals.
- Pedal graphs are edited as text; there is no visual node editor.
