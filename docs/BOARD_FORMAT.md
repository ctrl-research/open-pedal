# Board format

A board is the ordered list of pedals on your pedalboard with their knob settings. It is also
exactly what the plugin stores in your DAW session, so **Export** and session save produce the
same data.

```json
{
  "schema": 1,
  "name": "Starter Board",
  "author": "OpenPedal",
  "input_gain_db": 0,
  "chain": [
    { "pedal": "openpedal.clean-boost", "version": "1.x", "enabled": true,
      "params": { "boost": 4, "low_cut": 60 } },
    { "pedal": "openpedal.ts-drive", "version": "1.x", "enabled": true,
      "params": { "drive": 25, "tone": 2000, "level": -6, "clip": "Gritty" } }
  ],
  "pedals": {
    "openpedal.ts-drive": { "...": "full pedal definition, only present when exported with pedals" }
  }
}
```

| Field | Meaning |
|---|---|
| `schema` | Format version, currently `1`. Newer files are refused with a clear message; unknown keys in older builds are ignored. |
| `name`, `author` | Free text. |
| `input_gain_db` | Gain applied before the first pedal. |
| `chain` | Pedals in signal order, first is closest to the guitar. At most 8 in the current plugin. |
| `chain[].pedal` | Pedal id, e.g. `yourname.fuzz-face`. |
| `chain[].version` | Requirement: `*`, `1.x`, `1.2.x`, or exact `1.2.3`. Default `*`. The newest installed pedal that satisfies it is used. |
| `chain[].enabled` | Footswitch state. |
| `chain[].params` | Knob id to value. Numbers are in the knob's real units; `enum` knobs use their label; `bool` knobs use `true`/`false`. Unknown knobs are ignored with a warning. |
| `pedals` | Optional. Pedal id to full pedal definition, added by "Export (include pedal definitions)". |

## How pedals are resolved on import

For each entry in `chain`, in order:

1. If a pedal with that id and a matching version is installed locally, it is used. Local always
   wins, even when the board also embeds a definition.
2. Otherwise, if the board embeds the pedal under `pedals`, that definition is loaded in memory.
   The panel offers **Install pedal**, which writes it into your pedals folder.
3. Otherwise the slot loads as a bypass placeholder and the message area says which pedal is
   missing. The rest of the board works normally.

A board therefore always loads. Sharing without embedded pedals keeps files small for people who
already have the same pedals; sharing with them makes the file self-contained.

## What is not in the file

DAW automation, window size, and MIDI mappings belong to the DAW session, not the board.
The plugin's session state is `{ "board": <board>, "ui": { ... } }`; export writes only `board`.
