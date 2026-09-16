# Pedal format

A pedal is one JSON file. It has three parts: metadata, the **knobs** a player sees, and a
**graph** of DSP blocks that makes the sound. Drop the file into your pedals folder and the
plugin loads it within a second; errors show up in the plugin's message area and from
`open-pedal-render --check my-pedal.json`.

```json
{
  "schema": 1,
  "id": "yourname.fuzz-face",
  "name": "Fuzz Face",
  "version": "1.0.0",
  "category": "drive",
  "author": "yourname",
  "description": "Two-knob germanium-style fuzz.",
  "oversampling": 4,
  "tail_seconds": 0,
  "knobs": [
    { "id": "fuzz",   "name": "Fuzz",   "type": "float", "min": 1,   "max": 200, "default": 40, "taper": "log" },
    { "id": "volume", "name": "Volume", "type": "float", "min": -30, "max": 6,   "default": -6, "unit": "dB" }
  ],
  "nodes": [
    { "id": "pre",  "type": "gain",       "gain": { "knob": "fuzz" } },
    { "id": "clip", "type": "waveshaper", "shape": "asymmetric", "bias": 0.15 },
    { "id": "dc",   "type": "dc_block" },
    { "id": "tone", "type": "filter",     "mode": "lowpass", "cutoff": 3500, "q": 0.6 },
    { "id": "out",  "type": "gain",       "gain_db": { "knob": "volume" } }
  ],
  "connections": [
    ["in", "pre"], ["pre", "clip"], ["clip", "dc"], ["dc", "tone"], ["tone", "out"], ["out", "output"]
  ]
}
```

## Metadata

| Field | Required | Meaning |
|---|---|---|
| `schema` | no | Format version, currently `1`. |
| `id` | **yes** | Stable identifier, `author.pedal-name`. Letters, digits, `.`, `-`, `_`. Boards reference this id, so never rename it; bump `version` instead. |
| `name` | no | Display name. Defaults to the id. |
| `version` | no | SemVer, default `1.0.0`. Boards ask for `1.x` style requirements. |
| `category` | no | `drive`, `modulation`, `delay`, `reverb`, `dynamics`, `filter`, `pitch`, `utility`. Sets the panel colour and menu grouping. |
| `author`, `description` | no | Free text. |
| `oversampling` | no | `1`, `2`, `4` or `8`. Runs the whole graph at that multiple of the sample rate. Use 2 or more for any pedal with a `waveshaper`. Adds a few samples of latency. |
| `tail_seconds` | no | How long the pedal rings after being switched off (delays, reverbs). The chain keeps running the pedal on silence for this long. |

## Knobs

Each knob is a host-automatable parameter and a control on the pedal panel.

| Field | Meaning |
|---|---|
| `id` | Stable identifier used by `{ "knob": ... }` bindings and by boards. |
| `name` | Label. Defaults to the id. |
| `type` | `float` (default), `int`, `bool`, or `enum`. |
| `min`, `max` | Required for `float` and `int`. Real units. |
| `default` | Defaults to `min`. For `enum`, a label or an index. For `bool`, `true`/`false`. |
| `unit` | Display suffix: `Hz`, `ms`, `dB`, `%`, or empty. |
| `taper` | `linear` (default), `log` (equal travel per octave, needs `min > 0`; use for Hz and ms), `audio` (more resolution near the bottom; use for mixes and linear gains). |
| `smoothing_ms` | Ramp time when the knob moves, default `20`. `0` snaps. Ignored for `int`, `bool`, `enum`. |
| `values` | `enum` only: list of labels, at least two. |
| `automatable` | Default `true`. |

The plugin exposes up to 8 knobs per pedal to the host. Extra knobs still work in the pedal file
but are not shown.

## Nodes

A node is `{ "id": ..., "type": ..., <params> }`. Every other key is a parameter of that block
type, and its value can be:

- a **number**, in the block's real units: `"cutoff": 720`
- a **label** for option parameters: `"mode": "highpass"`
- a **knob binding**: `"cutoff": { "knob": "tone" }`, optionally with
  `"scale"` and `"offset"` so `value = knob * scale + offset`, or for option parameters a
  `"map"` from knob labels to block options:
  `"shape": { "knob": "clip", "map": { "Smooth": "tanh", "Gritty": "asymmetric" } }`

Parameters you leave out take the block's default. Numbers outside the block's range are an
error, so the file tells you what the block accepts.

## Connections

`["from", "to"]` pairs. `from` is `in` (the pedal input) or a node id. `to` is a node id,
`node.port` for blocks with several inputs (`mix.a`, `mix.b`, `delay.mod`), or `output`.

- A node's output can feed any number of targets.
- Several connections into the same input **sum**. That is how you blend a clean path with a
  clipped one, or feed a delay both the input and its own feedback.
- **Feedback** is allowed only into a `delay` node. The loop is closed with one sample of delay,
  which is what a real feedback path does. Any other cycle is an error.
- `output` must receive at least one connection.

## Checking your pedal

```sh
open-pedal-render --check ~/Music/OpenPedal/pedals/fuzz-face.json
open-pedal-render my-board.json dry-guitar.wav wet.wav
```

The first command prints `ok` with the knob count or every problem found. The second renders a
board so you can listen without a DAW.

## Block reference

Generated with `open-pedal-render --list-blocks`. Ranges are in real units.

### `clamp`

Hard-limits the signal between min and max.

Inputs: `in`

| Parameter | Default | Range | Description |
|---|---|---|---|
| `min` | -1 | -100 .. 100 | Lower bound. |
| `max` | 1 | -100 .. 100 | Upper bound. |

### `dc_block`

One-pole high-pass filter that removes DC offset, e.g. after asymmetric clipping.

Inputs: `in`

| Parameter | Default | Range | Description |
|---|---|---|---|
| `cutoff` | 10 | 0.1 .. 200 | Corner frequency in Hz. |

### `delay`

Delay line with smooth, interpolated time changes. Connect an `lfo` to `mod` for modulation effects; connect a later node back into `in` for feedback.

Inputs: `in`, `mod`

| Parameter | Default | Range | Description |
|---|---|---|---|
| `time_ms` | 250 | 0 .. 10000 | Base delay time in ms. |
| `max_ms` | 2000 | 1 .. 10000 | Buffer length; time + mod is clamped to this. |
| `interpolation` | `cubic` | `linear`, `cubic` | Cubic is smoother for modulated delays. |

### `envelope`

Peak envelope follower. Output is the smoothed absolute level of the input, a control signal for `multiply` or for driving other blocks.

Inputs: `in`

| Parameter | Default | Range | Description |
|---|---|---|---|
| `attack_ms` | 10 | 0.01 .. 5000 | Rise time. |
| `release_ms` | 100 | 0.01 .. 10000 | Fall time. |

### `filter`

Second-order (biquad) filter. `gain_db` only applies to peak and shelf modes.

Inputs: `in`

| Parameter | Default | Range | Description |
|---|---|---|---|
| `mode` | `lowpass` | `lowpass`, `highpass`, `bandpass`, `notch`, `peak`, `lowshelf`, `highshelf` | Filter response. |
| `cutoff` | 1000 | 1 .. 40000 | Cutoff or centre frequency in Hz. |
| `q` | 0.7071 | 0.05 .. 30 | Resonance / bandwidth. |
| `gain_db` | 0 | -40 .. 40 | Boost or cut for peak/shelf modes. |

### `gain`

Scales the signal. Effective gain is gain * 10^(gain_db/20), so use either the linear `gain` or the `gain_db` parameter (or both).

Inputs: `in`

| Parameter | Default | Range | Description |
|---|---|---|---|
| `gain` | 1 | 0 .. 1000 | Linear gain multiplier. |
| `gain_db` | 0 | -120 .. 60 | Gain in decibels. |

### `lfo`

Low-frequency oscillator. Output is offset + depth * wave, where wave swings -1..1. Feed it into a `delay.mod`, a `multiply`, or a filter cutoff via a knob-less node param binding.

Inputs: none (source block)

| Parameter | Default | Range | Description |
|---|---|---|---|
| `shape` | `sine` | `sine`, `triangle`, `square`, `saw` | Waveform. |
| `rate_hz` | 1 | 0.01 .. 50 | Frequency in Hz. |
| `depth` | 1 | 0 .. 1000 | Amplitude multiplier. |
| `offset` | 0 | -1000 .. 1000 | Added to the output. |
| `phase` | 0 | 0 .. 1 | Starting phase in cycles. |

### `mix`

Crossfade: out = a * (1 - mix) + b * mix. Typical use: a = dry, b = wet.

Inputs: `a`, `b`

| Parameter | Default | Range | Description |
|---|---|---|---|
| `mix` | 0.5 | 0 .. 1 | 0 = only a, 1 = only b. |

### `multiply`

out = a * b. Use with an `lfo` for tremolo or with an `envelope` for dynamics.

Inputs: `a`, `b`

No parameters.

### `tilt`

One-knob tone control: a low shelf and a high shelf pivoting around `center`. Positive `tilt_db` brightens, negative darkens.

Inputs: `in`

| Parameter | Default | Range | Description |
|---|---|---|---|
| `center` | 1000 | 20 .. 20000 | Pivot frequency in Hz. |
| `tilt_db` | 0 | -24 .. 24 | High-shelf gain; the low shelf gets the opposite. |

### `waveshaper`

Static nonlinearity: out = f(drive * in + bias) - f(bias). Set the pedal's `oversampling` to 2 or more when using this block.

Inputs: `in`

| Parameter | Default | Range | Description |
|---|---|---|---|
| `shape` | `tanh` | `tanh`, `soft`, `hard`, `asymmetric`, `fold`, `diode` | Transfer curve. |
| `drive` | 1 | 0 .. 1000 | Input gain before the curve. |
| `bias` | 0 | -2 .. 2 | DC offset into the curve; makes even harmonics. |

