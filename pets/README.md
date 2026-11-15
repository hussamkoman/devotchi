# Pet sources

A pet is data (`docs/design/pet-animations.md` 2). Each `<id>.pet.json` here is
the editable source of one pet; `firmware/tools/petpack.py` turns it into the
binary pack of `docs/architecture.md` 7.2, which the firmware compiles in
(`core/pet_builtin_<id>.c`), stores and downloads, and which
`packages/shared` encodes byte for byte the same way. `golden/<id>.pet` and
`golden/<id>.crc32` are the committed encodings both test suites compare
against.

```bash
python3 -I firmware/tools/petpack.py check pets/devo.pet.json pets/golden/devo.pet
python3 -I firmware/tools/petpack.py decode pets/golden/ember.pet
```

Coordinates are body units at rig scale 16/16 (one unit is one pixel at full
size) with the body centre at 0,0, x to the right and y down; every
coordinate is an integer in -128..127.

## Fields

| Field | Type | Meaning |
|---|---|---|
| `id` | string | `[a-z0-9]{1,8}` |
| `name` | string | 1..12 printable ASCII characters |
| `rev` | integer | 1..255; a changed pack needs a higher `rev` |
| `palette` | object | see below |
| `face` | object | see below |
| `aux` | object | mood or reaction name -> 4 integers (-128..127); missing rows are 0 |
| `aux_names` | array | what the four channels mean (documentation only, not encoded) |
| `overrides` | object | mood or reaction name -> {channel: value} replacing the shared pose table for this pet |
| `oscillators` | array | motion added to an aux channel while a mood or reaction shows |
| `parts` | array | shapes, drawn back to front |

Mood names: `happy sad excited anxious sleeping angry proud idle focused
working thinking`. Reaction names (rows of `aux`, keys of `overrides`,
`mood` of an oscillator): `pat poke dizzy wake attention`.

### palette

| Field | Meaning |
|---|---|
| `mode` | `table` (eleven mood colours) or `shift` (the base mixed towards a colour per mood) |
| `base`, `accent`, `cream`, `sclera` | `#RRGGBB` |
| `moods` | mood name -> `#RRGGBB` (table) or `["#RRGGBB", t]` with t 0..1 (shift; stored as round(t * 255)) |

`dark`, `light` and `cheek` are derived from the body colour of the moment
(design-system 2.3); `accent-dark` is the accent 25 % towards pet ink.

### face

| Field | Meaning |
|---|---|
| `eyes` | `round`, `slit` or `dot` |
| `mouth` | `plain` or `snout` (fangs under the mouth line) |
| `brows` | true or false; without brows a half-open eye gets a lid at the brow angle |
| `eye_dx`, `eye_y` | half the eye spacing (4..60) and the eyes' y |
| `mouth_y`, `mouth_hw` | the mouth's y and half-width (4..60) |
| `sleep_drop` | units the pet sinks while asleep (0..30) |
| `replaces` | accessories the pet draws with its own parts instead of the shared ones (`zzz sweat tear sparkles dots tongue`) |
| `eye_sq`, `mouth_sq` | sixteenths of the sink and squash the eyes and the mouth follow (0..16) |
| `sweat_y` | y of the sweat drop (its x is `eye_dx` + 37) |

### overrides

Channel names (format-1 numbers 0..20): `eye_open eye_size pupil_size
pupil_dx pupil_dy brow_angle mouth_curve mouth_open bob_amp_px bob_period_ms
blink_min_ms blink_max_ms jitter_px accessory glance lid brow_l brow_r
brow_lift mouth_dx mouth_w`. `accessory` takes a name or 0..6, `glance` `off
look scan` or 0..2, `lid` a slant of -60..60 degrees or `"off"`. Values out of
the ranges of architecture 7.2 are refused.

### oscillators

`{"mood": "idle", "aux": 0, "wave": "sine", "amp": 3, "period_ms": 3500}`.
`wave` is `sine`, `bob` (follows the body bob), `scan` (follows the eyes'
scan) or `jitter` (a new random offset every period). `period_ms` is a
multiple of 50 up to 12750; `sine` and `jitter` need one, `bob` and `scan`
ignore it.

### parts

| Field | Meaning |
|---|---|
| `name` | for `anchor` references (not encoded) |
| `shape` | `rrect` (`x`, `y` centre, `w`, `h`, `r` with r <= half the short side), `line` (`points` 2..4, `width` 1..31), `tri` (`points` 3) |
| `layer` | `behind` (does not bob), `body`, `head`, `front` (head and front follow the sink) |
| `role` | colour: `body dark light cheek accent accent-dark cream ink white flame surface fog yellow` |
| `mirror` | also drawn at -x with x-moves negated |
| `outline` | false hides the part in outline mode (the night clock) |
| `ring` | rrect drawn as a ring instead of filled |
| `squash` | rrect whose top edge follows the sink and squash while its bottom stays put |
| `anchor` | name of an earlier line part; this part's coordinates are relative to that line's last point |
| `drivers` | up to 4: `{"op": "move_x" or "move_y", "aux": n, "point": index or "whole", "scale": sixteenths per aux unit (default 16)}`, `{"op": "show_nonzero", "aux": n}`, `{"op": "show_eq", "aux": n, "k": 0..7}` |

A `surface` rrect in the `behind` layer is the ground shadow: it narrows by
4 units per unit of hop.

## Format 1: what the binary layout adds to architecture 7.2

The table in architecture 7.2 is the layout; these are the details the
firmware (`core/pet_pack.c`) and `petpack.py` implement on top of it. A
codec must produce and accept exactly this.

- **Face block, bytes 9-11** (7.2 lists them as reserved): byte 9 `eye_sq`
  (u8 0..16), byte 10 `mouth_sq` (u8 0..16), byte 11 `sweat_y` (i8). There
  are no reserved bytes left in the face block.
- **Part `role` byte, bits 6 and 7** (reserved in 7.2): bit 6 `ring`, bit 7
  `squash`; both only on a rrect (set on a line or tri: invalid).
- **Lid channel**: the value 1000 means "no lid" (the shared default);
  otherwise -60..60.
- **Value ranges** a face override must respect (else the pack is invalid):
  eye_open 0..100, eye_size 4..48, pupil_size 0..100, pupil_dx/dy -100..100,
  brow_angle -60..60, mouth_curve -100..100, mouth_open 0..100, bob_amp_px
  0..12, bob_period_ms 0..20000, blink_min_ms/blink_max_ms 0..30000,
  jitter_px 0..6, accessory 0..6, glance 0..2, lid -60..60 or 1000, brow_l/r
  -60..60, brow_lift -20..20, mouth_dx -30..30, mouth_w 0..60. Override and
  oscillator `mood` is 0..10 or 16..20.
- **Face ranges**: eye_style 0..2, mouth_set 0..1, brows 0..1, eye_dx 4..60,
  mouth_hw 4..60, sleep_drop 0..30; `acc_mask` bits 0 and 7 are reserved.
- **Oscillators**: bits 4-7 of `aux_wave` are reserved; `period` must be
  non-zero for sine (wave 0) and jitter (wave 3).
- **Drivers**: a move on a rrect must use point index 7 (the whole part); a
  move on a line or tri uses a point index below the point count or 7. A
  show driver has `scale` 0; show-when-nonzero has index bits 0.
- **Table palette**: the fourth byte of each mood entry is 0.
- **Counts**: n_parts 1..28, n_over 0..48, n_osc 0..32; `size` equals the
  length; nothing may follow the last part. Name and id: the text, then only
  NUL bytes; at least one character.
- **Order of checks** (`pet_pack_validate`): length 175..2048, CRC (`crc`),
  magic, `fmt` = 1, rev 1..255, `size` = length, then structure (all `format`).
