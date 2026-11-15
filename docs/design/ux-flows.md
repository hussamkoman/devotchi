# Devotchi device UX flows (phase 2, v2 after review)

Companion to `design-system.md`. Mockup file names refer to `mockups/` and
`renders/`. Buttons, seen from the front: **REJECT (left)**, **ACTION
(middle)**, **APPROVE (right)**. Click = release before 600 ms; hold = 600 ms
(ACTION, REJECT) or 1200 ms with a fill (APPROVE commit holds).

## 1. Page map

```
boot splash (01)
 ├─ no Wi-Fi credentials ─> Wi-Fi setup 1/2: QR (02) ─ ACTION ─> 2/2: by hand (02b)
 ├─ joining Wi-Fi (03) ─> finding server (04) ─┬─ no token ─> pairing (06)
 │                                             └─ offline / lost server (05)
 └─ HOME (10-16)
      ├─ ACTION click ─> home page 2: recent activity (not mocked; About row style) ─> home
      ├─ ACTION hold ──> QUICK MENU (16b): Settings, Showcase, Desk mode on/off
      │     └─ SETTINGS (50)
      │           ├─ Pet ─────────> pet picker (60-62)
      │           ├─ Display ─────> sub-list (51) ─> value editor (52)
      │           ├─ Clock ───────> sub-list: Show clock, Clock brightness, 12/24 h, Night dimming, Night hours
      │           ├─ Haptics and LED ─> sub-list: Haptics off/gentle/full, LED on/off
      │           ├─ Desk mode ───> toggles in place (toast 19)
      │           ├─ Network ─────> sub-list: Wi-Fi setup, Unpair, Factory reset ─> confirm (53)
      │           ├─ Showcase ────> SHOWCASE menu (80) ─> moods (81), reactions (82),
      │           │                  sample permission (83), sample question (84),
      │           │                  sample notification, clock face
      │           └─ About ───────> about 1/2 (70), diagnostics 2/2 (71)
      ├─ screen rests (dim stage) while idle ─> CLOCK (40-42) ─ any button or agent event ─> home
      └─ overlays, from any page that can show them:
            toast (17, 18, 19, 28, 38, 90)
            PERMISSION card (20-24) ─ ACTION hold ─> decision menu (25-27)
            PLAN card (29) ─ ACTION hold ─> plan menu (29b)
            QUESTION read page (33) ─> card (30-35) ─ ACTION hold ─> question menu (36); 2+ questions ─> review (37)
            going to sleep (91), battery empty (92), identify (93)
```

Priority when several things want the screen: battery empty > permission /
plan / question (first in, first out, one queue) > identify > going to sleep >
whatever page the user is on. A request takes over from home, the clock,
settings and showcase; when the queue empties the user returns to where they
were (settings returns to its top list; the showcase is left, see 8).

## 2. Buttons per screen

`-` = no action (and no tab). "any" = one centred `Any button` tab.

| Screen | REJECT click | ACTION click | APPROVE click | ACTION hold | Other holds |
|---|---|---|---|---|---|
| Boot splash | - | - | - | - | |
| Wi-Fi setup 1/2, QR (02) | - | No camera: step 2 | - | Settings | |
| Wi-Fi setup 2/2 (02b) | Back to the QR | - | - | Settings | |
| Joining Wi-Fi (03) | - | - | Start Wi-Fi setup (after 3 failures) | Settings | |
| Finding server (04) | - | - | - | Settings | |
| Offline (05) | - | - | Retry now | Settings | |
| Pairing (06) | - | New code | - | Settings | |
| Home (10-16) | Poke | Recent activity page | Pat | Quick menu | |
| Quick menu (16b) | Back | Next row | Open / toggle desk mode | - | |
| Toast, urgency 1-2 | (falls through to the page) | | | | |
| Sticky banner (18) | any: dismiss | | | | |
| Permission, low/med (20-22b) | Deny | Next page (only with 2+ pages) | **Allow once** (inactive until all pages seen) | Decision menu | |
| Permission, high (23-24) | Deny | Next page | Nudge: shake + "Hold to allow" | Decision menu | **APPROVE hold 1.2 s: allow once** |
| Real request in showcase (85) | Deny | Next page | Nudge | Decision menu | **APPROVE hold: allow once**, whatever the risk |
| Decision menu (25-27) | Back to the card | Next row (wraps) | Choose the focused row | - | APPROVE hold on "Allow for this session" |
| Plan card (29) | Revise (keep planning) | Next page | Approve, manual edit approval (inactive until all pages seen) | Plan menu | |
| Plan menu (29b) | Back | Next row | Choose | - | APPROVE hold on "Approve, accept all edits" |
| Question read page (33) | First question: to terminal. Later: back | Next page, then Options | - | Question menu | |
| Question, single (30-33b) | First question: to terminal. Later: back one question | Next option (wraps) | Choose the focused option, go on | Question menu | |
| Question, multi (34, 35) | as single | Next row (options, then Send) | Tick / untick; on the Send row: send | Question menu | |
| Question menu (36) | Back to the question | Next row | Choose | - | |
| Question review (37) | Back to the last question | - | Send all answers | Question menu | |
| Clock (40-42) | any: wake to home (no tab drawn; the press is a wake press) | | | | |
| Settings lists (50, 51) | Back (top list: exit to home) | Next row (wraps) | Open / change | - | |
| Value editor (52) | Cancel (restore the old value) | Next value (wraps, previewed live) | Save | - | |
| Destructive confirm (53) | Cancel | - | Nudge: shake + "Hold to erase" | - | **APPROVE hold 1.2 s: do it** (coral) |
| Pet picker (60-62) | Back | Next pet | Use this pet (not on the pet in use or "More pets") | - | |
| About (70, 71) | Back | Next page | - | - | |
| Showcase menu (80) | Back (leaves showcase) | Next row | Open | - | REJECT hold: leave showcase |
| Showcase moods / reactions (81, 82) | Back to the menu | Next mood / reaction | Pat / replay | - | REJECT hold: leave showcase |
| Sample permission / question (83, 84) | Exactly as the real card; nothing is sent | | | As the real card | REJECT hold: leave showcase |
| Going to sleep (91) | any: stay awake | | | | |
| Battery empty (92) | - | | | | |
| Identify (93) | any: dismiss | | | | |

Home page 2 ("recent activity": the last five status/notify lines with times)
is the one page added without a mockup; it uses the About row style.

## 3. Permission requests

### 3.1 The card (20-24)

Top to bottom:

1. Tool chip in the risk colour with the tool's icon, the risk bars and the
   risk words; queue position (`1/3`) when more are waiting.
2. **The code well: the exact command, path or arguments in mono.** It is the
   first thing in the card and always starts with the start of the command.
   One to three lines; when there is more, a footer shows what is left
   (`+2 more lines`) and the page (`1/2`). ACTION turns pages; text is never
   scrolled. Edit/Write: page 1 is the path and the line counts, later pages
   are the change.
3. For medium and high risk, optionally the **server's reason** in the risk
   colour with the warning triangle ("Force push: rewrites the remote
   branch"). It comes from the rule that set the risk, never from Claude
   (proposed new optional field, at most 40 characters).
4. **Claude's own description**, in fog, quoted and prefixed `Claude:`. It is
   shown because it helps, and marked because Claude wrote it: a mistaken or
   manipulated agent can describe a harmful command as harmless.
5. Project and session id; seconds left and a countdown bar in the risk colour.

- APPROVE allows this one call. It is inactive (slate) until every page of a
  Bash command, URL or tool argument has been shown; a command longer than
  four pages has no Allow on the device. For high risk Allow is a 1.2 s hold.
- REJECT denies this one call. The message to Claude is "Denied on Devotchi.
  Do not retry this call; try another way or ask the user."
- ACTION hold opens the decision menu.
- When time runs out the card closes with the receipt "Time ran out · sent to
  terminal"; the reply is neutral. If that happens during a hold, the hold is
  cancelled.
- A request answered or cancelled elsewhere (`dismiss`) closes the card with a
  receipt that says where ("Answered in terminal", "Answered on dashboard").

### 3.2 The richer decision menu (25-27): proposal

At most four rows. The focused row explains itself. Focus opens on **Send to
terminal**. The card's countdown and seconds stay in view.

| Row | Shown when | Needs | Hook reply (see hooks-facts) |
|---|---|---|---|
| Send to terminal | always | click | neutral `{}` (`permission_defer`) |
| Allow for this session | risk low or medium, tool is not Edit/Write, **and** the server can express the grant as one narrow rule | APPROVE **hold** | allow + `updatedPermissions`: one `addRules` entry, `behavior: allow`, `destination: "session"` |
| Deny, ask why | always | click | deny + message: explain what this call is for and why it is needed, then you may ask again |
| Deny and stop | always | click | deny + `interrupt: true` |

**The session rule is written by the server and shown word for word.** The
server takes the request's own `permission_suggestions` entry (or builds the
same shape), changes only the destination to `session`, and sends the device
the text of the rule. The device prints it in mono and says how it matches:
`pnpm test …` / "Starts like this, until Claude exits." for a prefix rule,
"Exactly this command" for an exact one. The server **withholds the row** when
the rule is broad: a whole-tool rule or a wildcard at the front, an
interpreter or shell (`sh`, `bash`, `node`, `python`, `npx`, `eval`, `sudo`,
`env`), a rule shorter than two words, a path outside the project, more than
one rule, or anything that is not `addRules`/`allow`.

Why these rows and not "always allow / deny with reason / deny and stop":

- **A pocket device should be generous with "no" and stingy with "yes".**
  Each kind of deny saves a trip to the keyboard: the card's plain Deny tells
  Claude to try another way, "ask why" makes Claude explain itself, "stop"
  ends the turn.
- **One bounded "yes more".** It is the rule the row shows, it lives in
  memory and dies with the session, it takes a hold, and its row is amber.
- **Nothing persistent from three buttons.** "Always allow" (a rule written to
  `settings.local.json`) is offered by the dashboard and the terminal only,
  where the rule can be read in full and edited. The device never writes
  settings, never offers bypass, auto or accept-edits mode on a permission
  card, never edits a command.
- **High risk has no granting row at all.** The only way to allow a high-risk
  call from the device is the hold on its own card, once.
- Free-text deny reasons are impossible with three buttons.

After any choice the card closes and a receipt states the outcome ("Denied ·
Claude was told to try another way", "Allowed · pnpm test … for this
session", "Denied · Claude stopped"). If another request is waiting, the
receipt is a **strip at the top** of the next card for 1 s while that card
arms (`28b`); otherwise it is a toast on home (`28`).

### 3.3 Plan approval (29, 29b)

`ExitPlanMode` arrives as a card with a sky `Plan` chip: title (the plan's
first heading), numbered steps in a well, four per page, paged by ACTION.
APPROVE approves and keeps manual approval of edits (`setMode default`,
session); it is inactive until every page was shown, and a plan longer than
three pages (12 steps) can only be revised or sent to the terminal from the
device. REJECT is "Revise" (deny with a keep-planning message). The plan menu:
Send to terminal (focus), **Approve, accept all edits** (amber row, hold; says
"Claude may change any file in this project without asking, until it exits"),
Deny and stop. Timing as for questions (4).

### 3.4 Safety rules

1. Only a button press on a card or a menu row decides. Timeouts, sleep, lost
   links, the showcase and errors all end in the neutral reply.
2. Arming delay on every new card (500 ms; 1 s when it replaces another card;
   1.5 s for a real request during the showcase). It starts when all buttons
   are released. The press that woke the screen is swallowed.
3. The exact command comes first; Claude's description is labelled. Risk and
   its reason are computed by the server from the command only.
4. Yes waits until everything was shown (read before yes).
5. High risk: allow is a 1.2 s hold with visible progress and the word
   `Allowing…`. A click only nudges.
6. Anything that grants beyond one call is a hold on an amber row, is never
   available for high risk, and shows the rule text the server will send.
7. Deny, back and terminal are always single clicks and never held.
8. The cursor never rests on a granting row when a menu opens.
9. APPROVE never moves a cursor, so a habit of "tap right to scroll" cannot
   form.
10. Desk mode and "no live device" mean the card is never shown; the device
    says so (DESK chip, offline screen).
11. Showcase: sample cards are framed in violet and tagged `SAMPLE`. A real
    request during the showcase, and for 10 s after it ends, opens under a
    cream `REAL REQUEST` bar with two haptic pulses, arms for 1.5 s, and can
    only be allowed with the hold (`85`). Deny and terminal stay one click.
12. Every outcome is confirmed in words by a receipt.

## 4. Questions (`AskUserQuestion`)

- **One question per card**, sent by the server one per frame (a call with
  four questions and their descriptions does not fit one 2 048-byte frame).
  Header chip `Question`, the question's `header` in caps, position `1/2` when
  the call carries several, and the seconds left.
- **Reading.** A question of up to two lines is shown in full above its
  options. A longer one opens on a **read page** (33) with the whole text
  (six lines per page, ACTION pages, then `Options`); on the options page it
  is then one line. "Read the question again" is in the question menu.
- **Options** are a list. The focused option shows its full label (up to two
  lines) and its description on **up to three lines**; the others show one
  line. Rows that do not fit scroll by whole rows with a thumb; the focused
  row is always complete. Text beyond that is on the dashboard.
- **Single choice:** no radio marks. ACTION moves the cream bar, APPROVE
  chooses and goes to the next question. With one question the answer is sent
  at once; with two or more the last choice leads to the review card (37) and
  nothing is sent before APPROVE there.
- **Back** (REJECT on a later question, or on the review card) keeps every
  answer: the earlier choice is focused and marked with a mint check, ticks
  stay ticked.
- **Multi-select:** APPROVE ticks or unticks the focused option; the list ends
  with a `Send n selected` row. With nothing ticked the row reads `None of
  these` and sends exactly that text as the answer.
- **Answer in terminal:** REJECT on the first question (tab `Terminal`,
  receipt "Moved to terminal"), or the first row of the question menu. The
  reply is neutral and the terminal dialog takes over; that is also the only
  way to type a free-text answer.
- **Question menu (36):** Answer in terminal (focus), Read the question
  again, Let Claude decide (allow; every question not yet answered gets "no
  preference, use your judgment"), Decline to answer (deny with a message).
- **Time.** The terminal dialog is probably unusable while the hook is
  pending (protocol 8.4), so a long fixed hold would lock the person at the
  keyboard out. A question or plan therefore **starts with the room hold
  (default 30 s). Every press on its card extends the hold to 60 s after that
  press, up to 300 s in total.** Untouched, it goes to the terminal after the
  room hold like any request; while someone is reading and pressing, it stays.
  The bar and the seconds show the current deadline. This needs a new
  device-to-server message (extend hold) and a server-side cap; the installed
  hook timeout (310 s) already covers it.
- Receipt: "Answers sent" with the chosen labels (38).

## 5. Home, toasts, clock

- **Home:** status bar, pet, optional project label (caps), status line
  (`body`, one line). No tabs until a button is pressed; then `Poke`,
  `Recent  hold Menu`, `Pat` for 3 s.
- **Quick menu (16b):** Settings, Showcase, Desk mode. Two presses reach the
  showcase; desk mode is a visible switch, not a hidden hold.
- **Toasts** slide up over the status block: icon in a filled circle, title,
  one detail line. 4 s; urgency 3 stays with an `Any button: OK` tab, and the
  pet steps up 4 px so the banner clears its feet.
- **Desk mode:** quick menu or Settings. `DESK` chip in the status bar for as
  long as it is on, a toast when it changes.
- **Clock (40-42): the resting face.** The power stages stay as in
  architecture 4.8; what changes is what the dim stage shows:

  | Stage | Agent idle or sleeping, nothing pending | Agent busy |
  |---|---|---|
  | `active` | Home. With Screen timeout set to Never: the clock after 2 min | Home |
  | `dim` | **Clock.** Battery: 10 % backlight (day), 5 % (night). External power: Clock brightness setting, default 30 % (day), 5 % (night) | Home at 10 % |
  | `off` | Dark. With Show clock = "Always on USB power": the clock stays, at the same levels | Dark |

  With the default 30 s timeout the clock is therefore visible from 30 s to
  60 s on battery and from 60 s to 5 min on external power, and all night on a
  desk with "Always on USB power". Time in `display`, date in caps, the
  sleeping pet at about half scale, 10 fps. Night palette (black, ember,
  outline pet) between 22:00 and 07:00 when Night dimming is on.
  Settings > Clock > Show clock: When resting (default) / Always on USB power
  / Never.
- Leaving the clock: any button (the press is swallowed) or any agent event;
  the pet plays the wake-up animation on home.

## 6. Timeouts

| What | Time | Then |
|---|---|---|
| Boot splash | 1.5 s | next start-up screen |
| Arming delay | 500 ms; 1 s after a queue advance; 1.5 s real request in showcase | tabs become active |
| Permission card | the room hold (10-300 s, default 30 s) | neutral, receipt "Time ran out · sent to terminal" |
| Question / plan card | the room hold; each press extends to 60 s after it; cap 300 s | neutral, receipt |
| Decision / question / plan menu | none of its own; shares its card's deadline, which it shows | closes with the card |
| "Hold to allow" nudge | 1.5 s | label returns |
| Toast | 4 s (urgency 3: until a press) | slides out |
| Receipt toast / strip | 2.5 s / 1 s | slides out / status bar returns |
| Home tabs after a press | 3 s | hidden |
| Quick menu, Settings, About, pet picker without a press | 60 s | home |
| Value editor without a press | 30 s | cancel, back to the list |
| Destructive confirmation | 10 s | cancel |
| Showcase without a press | 5 min | leave showcase |
| Real-request guard after leaving the showcase | 10 s | normal cards again |
| Screen dim / off / deep sleep | as today (architecture 4.8) | |
| Going-to-sleep warning | 10 s before deep sleep | deep sleep |
| Low-battery toast | once at 15 % and once at 8 % | |
| Battery empty screen | 5 s | power down |

## 7. What wakes the screen, and what keeps it awake

Wakes (unchanged from architecture 4.8, plus questions and plans):

- Any button (the waking press is swallowed).
- An incoming permission request, question or plan (with haptics).
- A `notify` with urgency 1 or more; `identify`.
- A change of external power; first ONLINE after boot; ONLINE to OFFLINE.
- Agent traffic (`status`, `mood`, urgency 0) does not wake a dark screen, but
  it turns the clock back into home when the screen is lit.

Keeps awake (change to `power_fsm`): **while a card or one of its menus is
visible the stage is held at `active`** (today it is only capped at `dim`), so
a card never dims while it is read and the next press is never swallowed as a
wake press. The protocol hold ends the card; the normal timers restart then.

## 8. Showcase (demo) mode

- **Enter:** home, ACTION hold, Showcase (also Settings > Showcase).
- **Marked:** a 2 px violet frame around the whole screen and one violet tag
  at the left of the status bar: `DEMO` on showcase pages, `SAMPLE` on sample
  cards.
- **Menu (80):** Moods (ACTION steps through the eleven, with name and a
  one-line meaning), Reactions (pat, poke, dizzy, wake-up, attention),
  Sample permission (a high-risk card, so the hold can be shown), Sample
  question, Sample notification (a toast and the pet's reaction), Clock face
  (day; ACTION switches to night), Leave showcase.
- **Sample cards are the real cards**, with the same tabs, menus, hold and
  receipts, but send nothing; receipts read "Sample: allowed".
- **Leave:** REJECT hold anywhere in the showcase, REJECT on its menu, the
  last menu row, 5 min without a press, or sleep. Nothing is stored.
- **Real events win, and are unmistakable:** a real permission request,
  question or plan interrupts the showcase at once, without the violet frame,
  under the cream `REAL REQUEST` bar, with two pulses and a 1.5 s arming
  delay; allowing needs the hold whatever the risk (85). The same guard
  applies for 10 s after the showcase ends. Afterwards the user is on home,
  not in the showcase. Real toasts appear over the showcase unframed. The
  device stays registered and holding throughout.
