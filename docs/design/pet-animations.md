# Pets: rig, animations and the pet format (phase 2, v2 after review)

Pictures: `pet-moods.html` (render: `renders/pet-moods.png`), including a
greyscale check and a half-size check. `tools/pet.py` draws Devo and Ember
for these mockups only; it is hand-drawn and not read by anything else. The
source of truth for any pet is its `pets/<id>.pet.json`: `pnpm run pet-sheet
<id>` draws its preview sheet from the encoded pack (the dashboard's renderer,
`packages/shared/src/pet-render.ts`, which follows
`firmware/src/components/core/pet_pose.c` and `ui/pet_view.c`), and
`firmware/tools/petpack.py check` lints it against section 5
(`pets/README.md`).

## 1. What stays from the current rig

Everything the firmware animates today is kept, with the same constants:

- A **pose table** per mood (`pet_pose_t`): eye openness and size, pupil size
  and offset, brow angle, mouth curve and openness, bob amplitude and period,
  blink interval, jitter, accessory, glance mode.
- **Tween** between moods: every channel and the colour, 300 ms, ease-out;
  accessory and glance mode switch at the halfway point.
- **Bob** (sine, per-mood amplitude and period), **blink** (120 ms at a random
  interval), **glance** (a look to one side every 8-15 s for 900 ms) and
  **scan** (left, right, centre every 2-4 s: "reading").
- **Jitter** for anxious, **accessories** (Zzz, sweat, tear, sparkles, dots).
- **Reactions**: pat (`^` eyes, 1.5 s), poke (squish 12 px, 0.6 s), dizzy
  (three pokes in 2 s: spiral eyes, 2 s).
- The structure that keeps the frame cheap: a fixed group; the body bobs alone
  with its features as children.

The pose numbers of happy, sad, excited, anxious, sleeping, angry and proud
are the firmware's, unchanged. **Idle, focused, working and thinking are
re-tuned** (section 3): these are the faces people see most, and the old ones
(flat mouth, inward brows, grey idle) read as bored or annoyed.

Phase 2 adds: four small pose channels, parts around the face, four auxiliary
channels per pet, two reactions (wake-up, attention) and the pet format.

New pose channels (all optional, default 0):

| Channel | Meaning | Used by |
|---|---|---|
| `lid` | Upper lid drawn over the eye in the body colour with a dark edge; the value is its slant in degrees. The eye keeps its full size and `eye_open` says how much shows | Devo focused (flat lid); every Ember mood with half-open eyes |
| `brow_l`, `brow_r`, `brow_lift` | Brows per eye instead of one mirrored angle | Devo thinking (one brow up) |
| `mouth_dx`, `mouth_w` | Mouth offset and half-width | Devo thinking (small, off-centre), focused, working |
| accessory `tongue` | Tongue tip at the right mouth corner | Devo working |

## 2. A pet is data

```
pet
  id, name                         "devo", "Devo"
  palette                          11 mood colours, or 1 base colour + 11 shifts;
                                   accent, cream; derived dark / light / cheek
  face                             eye style (round | slit | dot), sclera colour, brows on/off,
                                   mouth set (plain | snout with fangs), anchors and sizes
  parts[]   (drawn back to front)  shape, geometry, colour role, layer, driver
  aux[4]    names                  what the four auxiliary channels mean
  poses[11] per mood               aux0..aux3 (int8) + optional face overrides
  reactions overrides              aux values for pat, poke, dizzy, wake, attention
  accessories                      which sprite set to use for zzz/sweat/tear/sparkles/dots
```

- **Palette.** A pet owns its colour. Either eleven mood colours (Devo) or one
  base colour with a shift per mood (Ember: `mix(base, target, t)`), plus an
  `accent` and a `cream`. `dark`, `light` and `cheek` are derived (design
  system 2.3). There is no global mood colour table any more: mood is carried
  by face, pose and aux first, colour second.
- **Face.** The shared rig still draws and animates eyes, pupils, glints,
  lids, brows, mouth and the closed / `^` / spiral eye variants, so blink,
  glance and scan work for every pet. A pet picks from a small built-in set:
  eye style `round` (white, round pupil), `slit` (vertical pupil whose width
  follows `pupil_size`) or `dot` (no sclera); sclera colour; brows on or off
  (off = the lid slant carries the brow angle); mouth set `plain` or `snout`
  (a lighter muzzle with nostrils, a wide mouth and two fangs).
- **Body.** Any number of body parts; there is no single "body shape". Devo
  is one rounded rect; Ember is a head on a smaller belly. An egg is two
  overlapping rounded rects.
- **Shapes:** `rrect` (x, y, w, h, radius; also used for circles and for
  pills in place of ellipses, which LVGL does not have), `line` (2-4 points,
  width, round caps), `tri` (3 points). Coordinates are int8/int16 in body
  units; the body centre is (0, 0).
- **Colour roles:** `body`, `dark`, `light`, `cheek`, `accent`,
  `accent-dark`, `cream`, `ink`, `white`, `flame`.
- **Layers:** `behind` (does not bob: feet, shadow, tail), `body`, `head`
  (bobs; for a two-part pet the head takes squish and hop), `front`.
- **Drivers:** a part can be bound to one aux channel with a simple rule:
  move a point by `aux` px (optionally scaled), show when `aux != 0`, or pick
  a path variant.
- **Rig scale.** The firmware rig has fixed geometry today. Phase 2 gives it
  a scale factor n/16 that multiplies every part's geometry in integer maths
  when the pet is built for a screen: 15/16 on home, 12/16 in the picker,
  10/16 on connecting screens, 9/16 on the clock. No `transform_scale`, no
  off-screen layer.
- **Outline mode** (night clock): rounded rects become border-only objects
  (2 px `ember` border, black fill), lines keep their path at 2 px, triangles
  become closed 3-point `lv_line` polylines (`lv_draw_triangle` cannot
  stroke). Parts are still drawn back to front so nearer parts cover farther
  outlines. No accessories except Zzz.
- **Size:** Devo is 9 parts, Ember 24. With palette, poses and reaction
  overrides a pack is about 350 B (Devo) to 750 B (Ember). **Limit: 1.4 kB**,
  so that a pack fits one protocol frame as base64 (1 868 characters plus the
  envelope, under 2 048 bytes).

**Storage and loading (design decision):** every finished pet is compiled
into the firmware (each costs under 1 kB). On top of that the device has
**four dashboard slots**: the dashboard's Pets page lists further pets and
"send to device" writes one pack into a slot (one NVS blob each, 5.6 kB of
the 24 kB NVS partition at most; they survive reboot). Settings > Pet shows
the built-ins, the filled slots and a "More pets" page (`mockups/60..62`).
The transfer (one frame as above, or HTTPS fetch) is for the protocol plan.

## 3. Devo, the signature pet

A soft square (the device is a 60 mm square with rounded corners; Devo is its
shape with a face) and one antenna. Devo is the pet that **changes colour
with its mood**. The antenna is the signature: a second way to read the mood
from across the room, and its tip is the on-screen twin of the device's LED.

| Part | Shape | Colour | Layer | Notes |
|---|---|---|---|---|
| Ground shadow | pill 124x12 | `surface` | behind | Narrows by 4 px per px of hop |
| Feet (2) | rrect 30x16 r8 | `dark` | behind | Do not bob: the body squashes onto them |
| Antenna stem | line, 2-4 points, 4 px | `dark` | body | Root at top centre; tip at (aux0, -26 + aux1) |
| Antenna tip | circle r7 | `light` (amber when attention) | body | Follows the stem end; lags the bob by 80 ms |
| Body | rrect 140x116 r46 | `body` | body | Height -12 when poked, -10 asleep |
| Sheen | rrect 30x8 r4 | `light` | front | Top left; the only highlight |
| Cheeks (2) | pill 18x10 | `cheek` | front | Shown when aux3 = 1 |
| Face | shared rig, `round` eyes, brows, `plain` mouth | `white`, `ink` | front | Eyes at (±27, -8), mouth at (0, 30), half-width 18 |

Aux channels: `aux0` tip x, `aux1` tip y, `aux2` stem style (0 curve, 1 kinked,
2 hook, 3 straight), `aux3` cheeks.

Colours: focused `#5B82F0`, working `#B4D455`, thinking `#9A8AE0`, idle
`#F0C594`, happy `#FFB84D`, sad `#7E9BC8`, anxious `#C4A0D8`, excited
`#FF8DB4`, proud `#3CBFB4`, angry `#D4366B`, sleeping `#7377CF`.

### Moods

The "cue" column is what tells the mood apart in greyscale.

| Mood | Face | Antenna (aux0, aux1, style) | Body motion | Cue without colour |
|---|---|---|---|---|
| focused | flat upper lids at 62 %, pupils slightly down, small smile (curve 16, narrow), no brows | (0, -4) straight, perfectly still | bob 1 px / 4.0 s | level half lids + rigid antenna |
| working | eyes 88 %, pupils on the work (down), scan; smile 34 with the tongue tip out | (-14, 2), swings to (+14, 2) with each scan leg | bob 2 px / 2.0 s | tongue + antenna leaning |
| thinking | pupils up-right, right brow raised high, left brow level, small flat mouth off-centre | (14, 0) hook: curls into a question mark | bob 1 px / 3.0 s | one brow + hook + three rising dots (400 ms apart) |
| idle | fully open, soft smile (curve 42), slow blinks, glances | (10, 3) relaxed curve, sways ±3 px / 3.5 s | bob 2 px / 3.5 s | open face, cheeks, nothing else |
| happy | smile 80, brows -5° | (12, 0) perky, bounces with the bob | bob 3 px / 2.4 s | wide smile, cheeks |
| sad | eyes 65 %, pupils down, frown | (30, 26) droops to the side | bob 1 px / 4.0 s | tear (falls 14 px every 2.5 s), drooped antenna |
| anxious | pin pupils (22 %), brows -18°, small "o" | (6, 4) kinked, trembles with the jitter | jitter 1 px / 100 ms | sweat drop, pin pupils |
| excited | big eyes (32), open smile | (0, -6), whips ±6 px at double bob rate | bob 5 px / 0.8 s | open mouth, sparkles |
| proud | eyes 65 %, pupils up, smile 65 | (4, -6) tall | bob 2 px / 3.0 s | sparkles, lifted gaze |
| angry | eyes 60 %, brows +28°, frown | (-10, 2) kinked back | bob 1 px / 1.2 s (a huff) | slanted brows, frown |
| sleeping | lids closed, small smile, no blink | (26, 20) droops forward | bob 3 px / 5.0 s (breathing), body 10 px lower | closed eyes, Zzz |

### Key animations

Times in ms from the start; every step eases out.

**Mood change (300):** all channels tween as today; the antenna tip moves on
an arc (aux0 and aux1 tween, the stem's middle control point follows), so it
swings rather than slides. Style, lid and tongue change at 150.

**Wake-up (1400), new.** Plays when the room leaves `sleeping`/the clock for
a working mood, and on a button wake.
| t | Key |
|---|---|
| 0 | Sleeping pose; Zzz shrinks to one small z |
| 0-300 | Eyes open to 38 %, brows lift (-16°), antenna rises to (6, -4) |
| 300-800 | Yawn: mouth opens to 70 (round), body stretches 8 px taller |
| 800-1000 | Mouth closes, body back; one slow blink (200) |
| 1000-1400 | Tween to the target mood |

**Attention / needs input (loop until answered), new.** For `needs_input`
and while a request waits unanswered on another screen.
| t | Key |
|---|---|
| 0-150 | Hop 6 px (shadow narrows), eyes wide (size 30), pupils small and up, mouth "o" |
| 150-300 | Land; antenna snaps straight to (0, -2), tip turns amber |
| 300-1500 | Tip ring pulses r 8 to 11.5, twice; bang badge pops in at the top right (scale by 2 px steps) |
| 1500-4000 | Hold; blink; then repeat from 0 every 4 s. LED pulses with the ring |

The ring radius and the hop are capped so that, at home scale, the ring stays
5 px inside the top edge of the screen.

**Pat (1500), kept.** APPROVE on home. Eyes become `^` arcs, smile 90,
cheeks on, two hearts rise 12 px and fade by shrinking; antenna wags ±8 px
three times. Ends with the tween back.

**Poke (600), kept.** REJECT on home. Body squashes 12 px in 80 ms and springs
back by 400; eyes 25 % open; antenna kinks back; four impact ticks at the
sides for 200 ms.

**Dizzy (2000), kept.** Third poke within 2 s. Spiral eyes rotate (redraw the
polyline with a phase, 8 steps per turn), mouth small "o", antenna tip circles
a 6 px radius, body sways ±2 px.

Reactions keep the colour of the current mood (on home at rest that is the
apricot of idle).

**Positive / negative / complete reactions (existing classes).** Positive: one
5 px hop, sparkles for 1.2 s, then the target mood. Negative: 3 px sink for
300 ms, antenna drops, then the target mood. Complete: proud pose for 2 s with
sparkles, then the target mood.

**Sticky banner:** the whole pet steps up 4 px (200 ms) while the banner is
shown and back when it leaves.

**Going to sleep (10 s warning screen):** the wake-up animation run backwards
and slower: blink slows, yawn at 2 s and 6 s, antenna sinks.

**Night clock:** outline only; breathing bob 2 px / 6 s at 10 fps; one Zzz
cycle every 6 s.

## 4. Ember, the alternative pet (proof of the format)

A small jade dragon. It shares nothing visible with Devo except the rig
underneath: a big head on a small belly body, a snout, slit pupils, lids
instead of brows, wings, ear fins and a spade tail in ember orange. Ember
**stays jade in every mood**; wings, tail, smoke and fire carry the feeling.

Palette: base `#2FAE9C`; accent `#FF8A3D` (hotter `#FF6A33` when angry);
cream `#F6E7C1`. Shifts: happy +12 % white, excited +18 % white, proud +7 %
white, focused 8 % and angry 14 % towards ink, thinking 16 % towards indigo,
sad 42 % towards grey-green `#8FA3A8`, anxious 34 % towards pale `#C9D6D2`,
sleeping 34 % towards night blue `#3F5A8C`; idle and working unshifted.

Face: `slit` eyes with a warm sclera `#FFF3C4` at (±27, -43), no brows (the
lid slant takes the brow angle), `snout` mouth at (0, -7), half-width 15.

| Part | Shape | Colour | Layer | Driver |
|---|---|---|---|---|
| Ground shadow | pill 112x12 | `surface` | behind | |
| Tail | line, 4 points, 10 px | `dark` | behind | end point y moves by 0.8 x aux1 (curl) |
| Tail spade | tri, base centred on the tail's end point | `accent` | behind | follows the tail end, so it is always attached |
| Wings (2) | 7-point polygon (5 tris) + arm line 5 px | `accent`, `accent-dark` | behind | tip y moves by 0.8 x aux0 (lift) |
| Feet (2) | rrect 28x15 | `dark` | behind | |
| Belly body | rrect 84x64 r30 | `body` | body | |
| Belly plate | rrect 48x42 r20 + 2 lines | `cream` | body | |
| Horns (2) | tri | `cream` | head | |
| Ear fins (2) | tri | `accent` | head | tip lifts by 0.25 x aux0 |
| Head | rrect 116x78 r35 | `body` | head | squashes when poked, sinks 7 px asleep |
| Sheen | rrect 24x7 | `light` | head | |
| Snout | rrect 62x29 r14 + 2 nostril dots | `light`, `dark` | front | |
| Fangs (2) | tri 7x7 | `white` | front | follow the mouth line; hidden when the mouth is open |
| Smoke | stroked circles | `fog` | front | aux2 = 1: three rings rise (asleep, instead of Zzz); aux2 = 2: a puff at each side of the snout (angry) |
| Flame | 2 nested drops | `flame`, yellow | front | shown when aux3 = 1; flickers ±3 px / 150 ms |

Aux channels: `aux0` wing lift, `aux1` tail curl, `aux2` smoke, `aux3` flame.

| Mood | Face | Wings (aux0) | Tail (aux1) | Extras / cue without colour |
|---|---|---|---|---|
| focused | lids level at 50 % | -6, tucked, still | 0 | level lids |
| working | lids at 85 %, pupils down, scan | +4, small flap with each scan leg | +4 | downward gaze |
| thinking | lids at 80 % slanting outwards, pupils up-right | 0 | -8, tip taps every 1.5 s | dots |
| idle | round open eyes, smile 30 with fangs | 0, slow fold/unfold ±3 / 3.5 s | 0, sways | open face |
| happy | open eyes, smile 70 | +10, flap with the bob | +6, wags | lifted wings |
| sad | lids at 60 % drooping outwards, frown | -20, hanging | -12 | tear, hanging wings, paler |
| anxious | needle pupils, small "o" | -8, tight, trembling | -4 | sweat, paler |
| excited | wide pupils, mouth open | +24, spread, fast flap | +12 | breathes fire, sparks |
| proud | smug lids at 55 %, pupils up | +16, held high | +10 | sparks |
| angry | lids slant sharply inwards, frown | +22, flared; fins flare | +10, lashes | smoke puffs, hotter accent |
| sleeping | lids closed | -22, folded | -16, curled | head down, smoke rings |

Reactions reuse the shared ones (pat, poke, dizzy, wake-up, attention) through
the face rig; the wings and fins take the role of the antenna (snap up on
attention, flatten on poke, lift on pat).

Budget note: Ember moves more area than Devo (two wings and a tail outside the
body). Its parts are invalidated one by one; if it still misses the frame
budget of design-system 9 on hardware, its wing flap runs at 15 fps.

## 5. Rules for designing another pet

1. Everything within 220x190 body units (the mockup box) so the status line
   and toasts stay clear; the parts that bob within 150x130.
2. At most 28 parts, 4 aux channels, shapes from the list in section 2,
   pack size at most 1.4 kB.
3. The face is animated by the shared rig, but a pet chooses its eye style,
   sclera, brows or lids, mouth set, anchors and sizes.
4. A pet owns its palette. Keep clear of the semantic UI hues (mint, amber,
   coral, sky) for anything the pet shows often; give it one colour or one
   behaviour that is its own (Devo: changes colour, antenna; Ember: stays
   jade, wings and fire).
5. Its silhouette must differ from every other pet's: check the night
   outline first.
6. It must read at 9/16 scale (clock) and 10/16 (connecting screens).
7. All 11 moods and 5 reactions must be distinguishable without colour:
   check the sheet in greyscale (both checks are on the pet's preview sheet,
   `pnpm run pet-sheet <id>`, and for Devo and Ember also in
   `pet-moods.html`). `petpack.py check` lints rules 1, 2, 4, 5 and 7.
