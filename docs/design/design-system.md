# Devotchi device design system (phase 2, v2 after review)

Applies to the 2.0" 320x240 ST7789 panel and its three buttons. The mockups in
`mockups/` (rendered in `renders/`, gallery in `index.html`) are built from the
tokens below by `tools/build.py`; a screen that is not mocked follows the same
rules. Mockups are not pixel-exact; this document is the rule set. Section 10
lists what changed after the two reviews and where the design disagrees.

## 1. Brand direction

A small, well-made object that sits next to a keyboard: closer to a Playdate or
a Teenage Engineering pocket device than to a debug display.

- **The pet is the product.** Home shows the pet and one line of text, nothing
  else. Chrome (bars, tabs, chips) appears only when it has a job.
- **Ink and cream.** One dark ink background, cream text, flat saturated colour
  used only with a meaning (risk, state). No gradients, no shadows, no glass.
  Depth comes from two surface steps.
- **The exact thing first.** A request shows the exact command or path first,
  in the code well. What Claude says about it comes second and is labelled as
  Claude's words. Risk and its reason come from the server, never from Claude.
- **You can only say yes to what the screen has shown you in full.** If the
  command or plan has more pages, the yes button waits until they were seen.
- **Says it in words.** Every coloured signal also has a word or a shape;
  nothing is colour-only. Every button tab names the outcome of the press.
- **Calm by default.** Motion is small and local. The device never flashes,
  and never animates the whole screen.
- **Green never surprises.** The right button grants only on a request card,
  and anything risky or wider than one call needs a hold.

**Lock-up:** the lower-case wordmark `devotchi` in Figtree Bold whose `i`
wears Devo's antenna (cream stem, mango tip) instead of a dot (`01-boot-splash`).
On the device it is one image asset (about 150x40 px, A4), also meant for the
dashboard and the case.

## 2. Colour

All colours are opaque. Where a tint is needed it is a pre-mixed solid, not an
alpha layer.

### 2.1 Core palette

| Token | Hex | Role |
|---|---|---|
| `ink` | `#0C0E14` | Screen background (day) |
| `surface` | `#161A22` | Code wells, review cards, pet ground shadow |
| `surface-2` | `#232936` | Button tabs, toasts, receipt strip, bar tracks |
| `line` | `#343C4B` | Outlines, inactive dots and bars, toast border |
| `cream` | `#F3EEE3` | Primary text; fill of the focused row (text on it is `ink`) |
| `fog` | `#A7AEBB` | Secondary text that carries information (8.3:1 on ink) |
| `slate` | `#7C8597` | Decoration and inactive things only: the `$` prompt, labels next to a value, inactive tabs (5.2:1 on ink, 4.7:1 on surface) |
| `sub-on-cream` | `#4B5160` | Description text inside a focused (cream or amber) row |

Any 14 px text a person needs (page marks, counts, time left, captions) is
`fog` or `cream`, never `slate`.

### 2.2 Semantic colours

| Token | Hex | Meaning | Always paired with |
|---|---|---|---|
| `mint` | `#5FD38D` | Allow, success, link up, low risk | check glyph / one bar / `LOW RISK` |
| `amber` | `#FFC53D` | Medium risk, attention, low battery, link connecting; the focused row of a wide grant | two bars / `MEDIUM RISK` / bang badge |
| `coral` | `#FF6B5E` | Deny, failure, high risk, destructive commit, link down | cross / three bars / warning triangle / `HIGH RISK` |
| `sky` | `#6CC4FF` | Questions, plans, names the user must match (SSID, host, BLE name) | `Question` / `Plan` chip |
| `violet` | `#B592FF` | Showcase (demo) only: 2 px frame and one tag (`DEMO` or `SAMPLE`) | the words |
| `mango` | `#FFB84D` | Brand colour (lock-up, the antenna tip of the wordmark) | |

Risk is said three ways on a request card: the chip colour, the **risk bars**
(three rising bars, one, two or three lit: a shape, readable without colour)
and the words `LOW RISK` / `MEDIUM RISK` / `HIGH RISK`. The chip keeps the
tool's icon. High risk adds a coral edge on the code well, a coral reason line
with the warning triangle, and turns the right tab into a hold.

### 2.3 Pet colours

Pet colour belongs to the pet, not to the UI (see `pet-animations.md` 2):

- **Devo** (the ghost) has the PCB's look in every mood: a dark slate body
  `#2A3550` with a white `#F4F1EA` outline, traces, vias and chips. Only its
  antenna ball and chip light take one of eleven mood colours. None sits on
  a semantic hue: working is lime `#B4D455` (not mint), angry crimson
  `#E8457A` (not coral), focused cobalt `#5B82F0` (not sky), excited pink
  `#FF8DB4`, happy a pale gold `#FFD98C` (not amber), idle a ghost lilac
  `#B9AEFA`; asleep they go grey. Full table in `pet-moods.html`.
- **Mochi** has one hue, apricot `#F0C594`, that a mood only shifts.
- **Ember** has one hue, jade `#2FAE9C`, an ember-orange accent `#FF8A3D` and
  cream `#F6E7C1`; a mood only shifts the jade lighter, darker or paler.
- **Sprout** (catalog pet) has one hue, pink `#F2B8C6`, that a mood shifts,
  and a green leaf `#7DBE6A`.
- Shades are derived: `dark` = 28 % towards pet ink `#1A1D26`, `light` = 40 %
  towards white, `cheek` = 42 % towards coral.
- Semantic UI colour always wins: a pet never appears on a request card, and
  toasts carry their own icon and words.

### 2.4 Night palette (clock face, 22:00 to 07:00)

| Token | Hex | Role |
|---|---|---|
| `night-bg` | `#000000` | Background |
| `ember` | `#9A4A26` | Digits, pet outline (2 px), Zzz |
| `ember-dim` | `#6E3620` | Date, link dot |

Backlight 5 %. Nothing else is drawn: no status bar, no fills.

## 3. Typography

Two families, four sizes. Both families are SIL Open Font License 1.1; the
files and licence texts are in `fonts/`.

| Family | File | Licence | Use |
|---|---|---|---|
| Figtree (Erik Kennedy) | `Figtree-Medium.ttf`, `Figtree-Bold.ttf` | OFL 1.1 (`fonts/OFL-Figtree.txt`) | Everything a person reads |
| JetBrains Mono | `JetBrainsMono-Medium.ttf` | OFL 1.1 (`fonts/OFL-JetBrainsMono.txt`) | Everything a person types or compares: commands, paths, hosts, codes, rules, diagnostics |

Figtree was chosen for open apertures, a large x-height and unambiguous
`1 I l 0 O` at small sizes, and because it ships as static TTFs that
`lv_font_conv` takes directly. Both replace Montserrat 14/18/28 and the custom
mono 13 in `ui_theme.h` (remove those in the same change).

| Style | Font | Size / line | Use | Glyph range | Size at 4 bpp (estimate) |
|---|---|---|---|---|---|
| `display` | Figtree Bold | 64 / 64 | Clock, pairing code, value editor number | `0-9 :` and the 23 pairing letters (34 glyphs) | 31 kB (16 kB at 2 bpp) |
| `title` | Figtree Bold | 24 / 28 | Screen statements, pet name, `%` | ASCII (device-authored text only) | 11 kB |
| `body` | Figtree Medium | 18 / 22 | Questions, Claude's description, list rows, status line | ASCII + Latin-1 Supplement + `– — ‘ ’ “ ” • … ← ↑ → ↓ ✓ × −` | 15 kB |
| `small` | Figtree Medium | 14 / 18 | Captions, descriptions, button tabs, chips | same as body | 10 kB |
| `caps` | `small` + 9 % tracking, upper case | 14 / 18 | Status-bar titles, risk words, section labels | no extra font | 0 |
| `mono` | JetBrains Mono Medium | 14 / 18 | Commands, paths, codes, rules | same as body | 11 kB; 8.4 px advance, 34 columns in a code well |

Total about 78 kB. The sizes are **estimates measured by rasterising every
glyph with FreeType (Pillow) and summing the bounding boxes at 4 bpp plus 12 B
per glyph**; `lv_font_conv` was not available in this environment. Run it on
these exact ranges before implementation and record the real numbers (the
application is 1.597 MB against the 1.9 MB cap, architecture 4.11). If it gets
tight, drop `display` to 2 bpp first. Glyphs outside the ranges are replaced by
the server (transliterate to ASCII, else `?`) before a frame is sent; check
that the arrows exist in Figtree, otherwise take them from the mono font.

Rules:

- 14 px is the minimum. Nothing smaller, ever (at 200 ppi it is a 1.3 mm cap
  height, the limit at arm's length).
- A statement that must be understood (question, Claude's description, an
  option label) is `body`. The exact command is `mono` in `cream` inside the
  well. `small` is for supporting text.
- Two weights only: Medium for 14/18, Bold for 24/64. No italics.
- Digits are tabular so a countdown does not jitter.
- Truncation: labels one line with an ellipsis; hosts and paths are cut **in
  the middle**; the focused option of a question shows its description on up
  to three lines; a question longer than two lines gets a read page
  (`ux-flows.md` 4). A status line that does not fit scrolls once when it
  arrives, then rests with an ellipsis.

## 4. Layout

- Base unit 4 px. Outer margin 8 px for surfaces (cards, wells, rows), 12 px
  for bare text, so text inside a surface and text outside it share a left edge.
- Three bands: **status bar** y 0-19, **content** y 20-215, **button tabs**
  y 218-239. Full-bleed screens (splash, clock, battery empty) drop both bars.
- Radii: 4 (tags), 8 (rows, wells), 12 (toasts), 11 (chips: full pill),
  10 on the top corners of button tabs. Mochi's body is 46, Devo's dome 54.
- Row heights: list row 25 + 2 gap (settings 29 + 2); a focused row grows by
  its description (17 px per line, at most three). Lists show whole rows only.
- A code well is as tall as its content (one to three lines, plus a footer
  line when paged); the project line and the countdown are anchored to the
  bottom, so spare room sits between, not inside the well.
- One surface level inside the screen. Never a card on a card.

### Status bar (20 px, no background)

Left: the time (`small`) on home and on request cards; the page title in
`caps` on every other page (`SETTINGS`, `ABOUT 1/2`). Chips follow the time:
`DESK` (outlined). In the showcase one filled violet tag takes the left edge:
`DEMO` on showcase pages, `SAMPLE` on sample cards.
Right, fixed positions: link dot (mint registered, amber connecting, coral
offline), four Wi-Fi bars (struck through in coral when Wi-Fi is down), battery
(coral at 15 % and below, bolt when charging; the number lives in About).
The pet's antenna may reach into the empty middle of the bar; nothing may
touch the top edge.

Two things can take the bar's place for a moment: the **receipt strip** (the
first second of the next queued card) and the cream **REAL REQUEST** bar
(a real request during the showcase).

## 5. The three buttons on screen

Physical layout (from `hardware/devotchi.kicad_pcb` and
`case/exploration/fusion_parameters.csv`): the three switches sit in one row
under the display on the front face, 14 mm apart, the middle one on the
centre line. Seen from the front: **REJECT left (SW504), ACTION middle
(SW501), APPROVE right (SW502)**. The 40.8 mm window is 320 px wide, so the
buttons are under x = 50, 160 and 270 px (`renders/device-context.png`).

- Each active button has a **tab**: a 22 px label that grows out of the bottom
  edge of the screen directly above its button. A tab is centred on its button
  (50 / 160 / 270); when the labels are long the side tabs move towards the
  screen edge and the middle one gives way, but every tab still covers its
  button's x. A button without a tab does nothing on that screen.
- **A tab always names the outcome** (`Deny`, `Allow`, `Choose`, `Erase`),
  never the gesture alone.
- Glyph and colour are fixed per button: coral cross (left), cream dot
  (middle), mint check (right). One exception with a meaning: the APPROVE glyph
  and its hold fill turn **coral when the commit destroys something**
  (`Hold: Erase`).
- **Hold** is always written out. A tab whose only action is a hold shows the
  glyph inside a ring and `Hold: Outcome` (`Hold: Allow`, `Hold: Erase`,
  `Hold: More`). The middle tab can carry a click and a hold: `● Page  hold
  More`, the word `hold` in fog. There is no glyph-only hold cue.
- **While a hold is in progress** the other tabs disappear, the held tab widens
  to 136 px, says what is happening (`Allowing…`, `Erasing…`) and a 4 px bar
  fills along its top edge; release before it is full cancels.
- **A click on a hold-only tab** shakes the tab 4 px, ticks the motor and
  swaps its label to `Hold to allow` (amber ring) for 1.5 s. Nothing else
  happens (`23b-perm-high-clicked`).
- **Inactive tab:** slate glyph and word. Used during the arming delay and for
  a yes that waits for unread pages. A press shakes it.
- **Any-button screens** (sticky banner, going to sleep, identify, clock) show
  one centred tab, `Any button: OK`; all three buttons do the same.
- Home has no tabs. Any press on home shows them for 3 s (and performs its
  action); they are also shown for the first 10 s after boot.

### Grammar (the same on every screen)

| Button | Click | Hold |
|---|---|---|
| REJECT (left, coral) | No / back / cancel / hand to the terminal. On a request card: deny | Leave showcase (showcase only) |
| ACTION (middle, cream) | Move: next row, next option, next page, next value | More: the menu of the screen (home: quick menu; start-up screens: settings) |
| APPROVE (right, mint) | Yes / choose / open / save. On a request card: allow | The committing version of yes, when a click would be too easy |

Lists are walked with ACTION alone (they wrap); REJECT is always the way out.
APPROVE never moves a cursor, so "green" keeps one meaning.

## 6. Interaction rules

- **Click** = release before 600 ms. **Hold (navigation)** = 600 ms on ACTION
  or REJECT, fires at the threshold with a haptic tick. **Hold (commit)** =
  1200 ms on APPROVE with a visible fill; fires at the end, cancelled by
  release.
- **Arming delay:** presses are ignored and the tabs are slate for 500 ms
  after a card, menu or confirmation appears; **1 s when a card replaces
  another in the queue** (with its own haptic and the receipt strip); 1.5 s
  for a real request during the showcase. Arming starts only when all buttons
  are released.
- **Wake swallow:** a press that wakes the screen (from dim, off or the clock)
  only wakes it. While a card or menu is visible the screen does not dim
  (`ux-flows.md` 7), so a press on a card is never swallowed.
- **What needs a commit hold:** allowing a high-risk request; allowing any
  real request during the showcase; anything that grants more than this one
  call (allow for this session, approve a plan with all edits accepted);
  anything destructive on the device (factory reset, unpair).
- **Not a hold:** starting Wi-Fi setup. It keeps the stored network until a
  new one has connected, so it destroys nothing.
- **What never needs a hold:** deny in any form, back, cancel, send to
  terminal. Saying no is always one click.
- **Read before yes:** a yes tab is inactive until every page of the command
  or plan has been shown. Commands longer than four pages and plans longer
  than three pages offer no yes on the device at all (deny or terminal).
- **Focus starts on the safe row.** Menus open on the neutral entry ("Send to
  terminal", "Answer in terminal", "Cancel"). A focused row that grants more
  than one call is amber instead of cream.
- **Every decision gets a receipt** in words ("Denied · Claude was told to try
  another way", "Moved to terminal").
- **Timeouts never decide.** A card that runs out of time goes to the
  terminal (neutral), with a receipt. Time left is shown as a bar and in
  seconds on every card and every menu.

## 7. Iconography

14x14 grid, 2 px stroke, round caps and joins, no fills except dots and the
stop square. All are short polylines, arcs and circles, so each can be an
`lv_line`/`lv_arc` set or an A4 bitmap of at most 14x14 (49 bytes); the whole
set fits in a 3 kB symbol font. Set (see `ICON` in `tools/build.py`):

`check` `x` `dot` (button glyphs), `chev` `up` `down`, `terminal` (Bash, send
to terminal), `pencil` (Edit/Write), `globe` (web), `plug` (MCP tool), `warn`
(risk reason, destructive, wide grant), `shield` (session grant), `q`
(question), `speech` (deny, ask why), `list` (plan), `menu`, `stop`, `bell`,
`bolt`, `desk`, `sun`, `moon`, `clock`, `buzz`, `wifi`, `pet`, `play`, `info`,
`box0/1` (multi-select). Single-choice lists have no radio marks: the focus
bar is the cursor and nothing is selected until APPROVE.

Hold glyph: the button glyph at 60 % inside a 16 px ring (1.5 px stroke).
Risk bars: three 4 px bars, 5 / 9 / 13 px tall, lit in the risk colour, the
rest `line`.

## 8. Motion

One curve: quadratic ease-out (`pet_ease_out`). Three durations.

| Token | ms | Use |
|---|---|---|
| `quick` | 120 | Blink, focus row change, tab shake, tick |
| `move` | 200 | Tabs rise 22 px, toast slides up 46 px, list scroll by one row, card content swap, pet steps up 4 px for a sticky banner |
| `mood` | 300 | Pose tween between moods (existing `PET_TWEEN_MS`), body colour tween |

- Screen changes are **cuts**. The new screen is drawn once; only its tabs
  rise (`move`). No slides, no fades, no zoom of full screens.
- A new request: cut to the card, one haptic pulse (two for high risk and for
  a real request during the showcase), tabs become active after the arming
  delay.
- Toast: slides up from the bottom edge over the status block (never over the
  pet), stays 4 s, slides out. Urgency 3 stays until a press.
- Countdown bars shrink in steps of 1 px (30 s over 304 px is one step per
  100 ms; no per-frame redraw).
- The pet runs at 30 fps (33 ms frame, as today) and is the only thing that
  animates continuously. On the clock face it drops to 10 fps.
- Haptics mirror motion: tick 15 ms (focus wrap, hold threshold, tab shake),
  pulse 60 ms (request, toast urgency 2), double pulse (high risk, attention),
  short-long (the queue moved on).

## 9. LVGL 9 feasibility

Cheap (use freely): solid fills, rounded rectangles, borders, `lv_line`
polylines with round ends, arcs, circles, A4 glyphs, opaque colour changes,
object moves inside a small parent.

Avoid: blur and drop shadows; alpha layers larger than an icon (every
"transparent" colour in the palette is pre-mixed); `transform_scale` /
`transform_rotation` on anything bigger than an icon (it forces an off-screen
layer); image assets for UI chrome (the lock-up is the one exception);
full-screen redraws per frame.

**Frame budget.** A full 320x240 RGB565 frame is 153.6 kB, about 31 ms at
40 MHz SPI before any drawing. The display driver has one 40-line partial
buffer today (`display.c`), so rendering and the SPI flush run one after the
other. For phase 2:

- use two DMA draw buffers so LVGL renders the next strip during the flush;
- invalidate per moving part (antenna, eyes, mouth, one wing), not the whole
  pet box. The pet box is 220x190 (54 % of the screen) and must never be
  invalidated as a whole at 30 fps;
- budget: at most **170x170 px invalidated per frame at 30 fps** (the body
  bobbing with its face). Measure Devo and Ember on hardware (About 2/2 shows
  fps and draw time) before sign-off; if Ember misses the budget its wing flap
  runs at 15 fps;
- the hop of the attention reaction is the largest area; it lasts 300 ms and
  may drop frames.

Per element:

- **Focused row:** a cream (or amber) rounded rect that moves; text colour
  swap. Two small invalidated areas (old row, new row).
- **Button tabs:** three objects on the top layer, created once; label, glyph,
  x and width are set per screen from the measured label width. Top corners
  rounded with a rect that extends 10 px below the screen edge.
- **Hold fill:** a 4 px bar whose width grows; redraws 136x4 px per step.
- **Risk bars, page dots, countdown:** plain rects.
- **QR code:** `lv_qrcode` at exactly 4 px per module (version 4 = 164 px with
  the quiet zone, version 5 = 180 px; both fit the 198 px content band).
- **Dashed outline** (More pets) is the only dashed line.
- **Pet:** a fixed group; the body is a child that bobs with its features.
  There is no ellipse primitive: shadow, cheeks and belly are pill-shaped
  rounded rects. Triangles are `lv_draw_triangle` in the body's draw event.
  Size comes from a **rig scale** (all geometry multiplied by n/16 in integer
  maths), never from `transform_scale`. See `pet-animations.md` 2.
- **Night clock:** black screen, 5 glyphs; rounded rects become border-only
  objects and triangles and lines become closed `lv_line` polylines (2 px).
  With the backlight at 5 % this is the lowest-power lit state.
- **Buttons:** `btn_fsm` has one long-press threshold (1000 ms) and emits
  CLICK or LONG. Phase 2 needs: a threshold chosen per press by the current
  screen (600 or 1200 ms), a HOLD_PROGRESS tick from the down timestamp (for
  the fill), a CANCELLED outcome when a hold-only button is released early
  (for the shake and the nudge), and "armed only after all buttons are up"
  (`btn_fsm_arm_swallow` already does this). All of it is host-testable.

## 10. Review decisions

Both reviews said "revise". Every major issue is addressed; the table lists
what was done and the points where the design differs from the suggestion.
Later decisions replace two of the rows: pet packs may be 2048 bytes and
travel over HTTP (`docs/architecture.md` 7.2), and the device has eight
stored-pet slots in its own `pets` partition instead of four NVS slots.

| Review point | Decision |
|---|---|
| Hold affordance is inconsistent and has no verb (visual, ux) | Done. One rule (section 5): `Hold: Outcome` with the ring, `Allowing…` while held, a 1.5 s `Hold to allow` nudge after a click, coral for destructive commits. The ux reviewer's `Keep holding` is not used: a held tab names the outcome too. |
| Middle tab: dot versus ring is not learnable | Done. The hold is spelled `hold More` (fog word) on a shared tab and `Hold: More` on its own. A caps micro-label above the tab was rejected: it would need text below 14 px. |
| Ember does not prove the pet system (visual) | Done. A pet owns its palette, face style and body parts (`pet-animations.md` 2). Ember is redrawn: head on a small body, snout, slit pupils, lids instead of brows, jade in every mood, attached tail spade. |
| Devo is least charming in its everyday moods (visual) | Done. Idle is apricot with a smile and cheeks; focused, working and thinking lost the frowning brows and each has a cue that survives greyscale. Checks are in `pet-moods.html`. |
| Wi-Fi provisioning looks like a dev board (visual) | Done. Two steps; QR at 4 px per module; no Espressif wording. Which phone app or page the QR opens is an open question. |
| Mood colours collide with semantic colours (visual) | Done (2.3). |
| Risk icon missing on low/medium (visual) | Done: risk bars next to the words; the chip keeps the tool icon. |
| Slate too faint (visual) | Done: `#7C8597`, and information text is fog. |
| Focus looks like selection on questions (visual) | Done: no radio marks. Sample data is consistent across 31, 37, 38. |
| REJECT "Terminal" looks like a refusal; rename to "Skip" (visual) | **Partly.** The tab stays `Terminal`: "Skip" would read as "skip this question and let Claude decide", which is a different reply. The receipt says `Moved to terminal`, as the reviewer allowed. "Deny, explain first" is now `Deny, ask why` with a speech icon. |
| Picker chevrons (both) | Removed; page dots only. |
| Attention ring clipped; toast covers feet (both) | Done: the ring is capped inside the screen; the pet steps up 4 px for a sticky banner. The antenna may enter the empty middle of the status bar; that is intended. |
| Wi-Fi "Set up" should be a hold or leave the destructive list (visual) | Left the list: setup keeps the old network until a new one works (section 6). |
| Layout polish: pairing host, code well height (visual) | Done. |
| Review card and home had tabs that hide actions (both) | Done: `Hold: More` on the review card; home shows `Recent  hold Menu`; desk mode moved from a hidden REJECT hold into the quick menu. |
| Double violet tagging (visual) | Done: one tag. |
| Lock-up (visual) | Done as a proposal; open question for the owner. |
| Budgets not measured (visual, ux) | Font sizes are now measured by rasterising (section 3) but not with `lv_font_conv`; pet packs are capped at 1.4 kB so one fits a protocol frame as base64. |
| 300 s hold locks the keyboard (ux) | Done: questions and plans start with the room hold and each press extends it (`ux-flows.md` 4, 6). Needs a protocol message. |
| Clock never visible with default timeouts (ux) | Done: the clock is the face of the dim stage (`ux-flows.md` 5). |
| Real request during the showcase (ux) | Done: REAL REQUEST bar, two pulses, 1.5 s arming, hold to allow, also for 10 s after leaving. The "send to terminal automatically" setting is not added: one more setting, and the hold already makes an accident very unlikely. |
| Claude's description above the command; unread pages (ux) | Done: command first, `Claude: "…"` second, read-before-yes. For Edit/Write only the path page is required reading, not every diff page (open question). |
| Session grant described dishonestly (ux) | Done: the server renders the rule text, the device shows it in mono; broad rules get no row; `acceptEdits` is gone from permission cards. It remains only in the plan menu, labelled "Approve, accept all edits" (open question whether to keep it). |
| Question text truncated (ux) | Done: three-line descriptions, read page, real-length strings in 30-36. |
| Menus without countdown (ux) | Done: bar and seconds on every menu. |
| Queue advance too fast (ux) | Done: 1 s arming, receipt strip at the top. |
| Showcase is hard to reach (ux) | Done: quick menu. |
| Page tab on a one-page card (ux) | Done. |
| Pet box versus the quarter-screen rule; rig scale; ellipses; outlines (ux) | Done (section 9, `pet-animations.md` 2). |
| Font ranges (ux) | Done (section 3). `title` stays ASCII: it only shows device-authored text. |
| Button FSM thresholds (ux) | Recorded in section 9. |
| Card dims while being read (ux) | Done (`ux-flows.md` 7). |
| Plain Deny duplicates "try another way" (ux) | Done: the card's Deny carries that message; the menu has four rows. |
| Pet storage too narrow (ux) | Done: every finished pet is compiled in; four dashboard slots. |
| Skip reply, frame size, answers kept on Back (ux) | Done (`ux-flows.md` 4). |
