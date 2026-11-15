# Phase 2: manual checks on a real Claude Code session

Questions, plans and the richer decisions rely on Claude Code behaviour that
the hook documentation implies but does not state (protocol 14.11). The
automated tests pin what Devotchi sends; these checks confirm what Claude
Code does with it. Run them once per Claude Code release you rely on, and
before a public release.

Every check names the server switch that turns the feature off if Claude Code
behaves differently. A switch that is off sends the request to the terminal
prompt as before, so nothing is ever allowed by mistake.

## 0. Setup (10 minutes)

1. Build and link the CLI: `pnpm install && pnpm run build`, then use
   `node packages/client/dist/cli.js` wherever this guide says `devotchi`
   (or `npm link` in `packages/client`).
2. Make a scratch project, so no real settings are touched:

   ```bash
   mkdir -p /tmp/devotchi-p2 && cd /tmp/devotchi-p2 && git init -q
   printf '{"name":"p2","private":true,"scripts":{"test":"echo tests ok","lint":"echo lint ok"}}\n' > package.json
   ```

3. In a second terminal start the server and the dashboard:
   `devotchi start --local`. To run a check with a feature switched off,
   stop it and start it again with the switch in front, for example
   `DEVOTCHI_SESSION_GRANTS=0 devotchi start --local` (the same variables
   work for `pnpm run server` and Docker, see `docs/deploy.md`).
4. In the scratch project: `devotchi setup`, then `devotchi doctor`. Expect
   `PASS hooks`, `INFO features questions on, plans on, session grants on`
   and, with the dashboard tab open, `PASS questions 1 consumer(s) online
   can answer questions, 1 can approve plans`. A device with phase-2
   firmware counts as well.
5. Set a hold time that leaves room to read: `devotchi hold 120`.
6. Start Claude Code in the scratch project: `claude`. Keep the dashboard
   tab visible (a hidden tab does not hold requests).

Write down the Claude Code version (`claude --version`) next to the results.

## 1. "Allow for this session" is accepted

Assumption: an `addRules` entry with `destination: "session"`, built from
Claude Code's own suggestion, is accepted.

1. Type: `Run pnpm test.`
2. Expect a permission card with `pnpm test` in the command well and, under
   **More**, the row **Allow for this session** showing `pnpm test …` and
   "Starts like this, until Claude exits." (or an exact rule with "Exactly
   this command"). No row at all means Claude Code sent no usable
   suggestion; note the suggestion and try `pnpm lint`.
3. Click the row twice (the first click arms it).
4. Expect: the command runs, the receipt says "allowed for this session",
   and `devotchi status` shows `Last request: Bash: allow (answered,
   allowed for this session) by dashboard`.
5. Type: `Run pnpm test again.` Expect no card and no terminal prompt.
6. Type `/permissions` in Claude Code. Expect the session rule
   (`Bash(pnpm test:*)` or the exact command) and **no** new entry in
   `.claude/settings.local.json` of the scratch project.

If after step 3 the terminal prompt appears instead, or the rule is written
to a settings file: set `DEVOTCHI_SESSION_GRANTS=0` and report it.

## 2. A session prefix rule does not match a compound command

Assumption: Claude Code splits commands on shell operators, so the rule
`pnpm test:*` does not allow `pnpm test && echo x`.

Directly after check 1, in the same Claude Code session, type each line on
its own and wait for the result:

1. `Run exactly this command: pnpm test && echo x`
2. `Run exactly this command: pnpm test; echo x`
3. `Run exactly this command: pnpm test || echo x`
4. `Run exactly this command: pnpm test | tee out.txt`
5. `Run exactly this command: pnpm test $(echo x)`

Expect for every line a new permission request (a card, or the terminal
prompt in desk mode). Deny each one on the card.

If any of them runs **without** a request: set `DEVOTCHI_SESSION_GRANTS=0`
at once and report which line it was.

## 3. "Deny and stop" ends the turn

Assumption: `interrupt: true` stops Claude like Esc.

1. Type: `Create a file called a.txt with the text hello, then list the directory.`
2. On the card open **More** and choose **Deny and stop**.
3. Expect: Claude stops at once, makes no further tool call and waits for
   you to type. The receipt says "denied, Claude stopped".

If Claude carries on and tries another way, "Deny and stop" behaves like a
plain deny. That is harmless; no switch is needed, but report it.

Also try **Deny, ask why** on the same prompt: expect Claude to explain what
the call is for and then ask again.

## 4. Plan approval leaves plan mode in the chosen mode

Assumption: `setMode default` / `acceptEdits` together with `updatedInput`
leaves plan mode in that mode.

1. Restart Claude Code in plan mode: `claude --permission-mode plan`.
2. Type: `Plan how to add a CONTRIBUTING.md with three sections, then do it.`
3. Expect a plan card with the whole Markdown plan, as text.
4. Click **Approve**. Expect: Claude leaves plan mode (the mode line shows
   no plan mode and no "accept edits"), and its first file write shows a
   permission card.
5. Repeat steps 1-3 with a new file name and choose **Approve and accept
   edits** (two clicks). Expect the mode line to show accept-edits and the
   files to be written without a card.
6. Repeat once more and choose **Revise**. Expect Claude to stay in plan
   mode and ask what to change. **Stop** should end the turn as in check 3.

If the mode after 4 or 5 is wrong (for example still plan mode, or
accept-edits after **Approve**): set `DEVOTCHI_PLANS=0` and report it.

## 5. The terminal dialog while a question or plan is pending

Assumption: Claude Code does not show its own dialog while the hook holds the
request. If it does, both can answer and the first answer wins.

1. Type: `Use AskUserQuestion to ask me which colour I like, with three options.`
2. While the question card is shown, look at the terminal.
3. Note whether the terminal shows the question. If it does, answer it
   there: expect the dashboard card to disappear with "handled in terminal".

Either result is acceptable; report which one you saw. `devotchi doctor`
counts how often Claude Code showed its prompt during a pending request.

## 6. Answers reach Claude as labels

Assumption: the `answers` object of protocol 14.4 is read as the person's
answer, and every question needs a key (Devotchi always sends all).

1. Type: `Use AskUserQuestion to ask me two questions at once: my favourite
   fruit (one choice) and which pets I have (several choices).`
2. Expect one card with both questions. Choose one fruit, tick two pets,
   click **Send**.
3. Ask: `What exactly did I answer?` Expect the labels word for word, the
   pets joined as `A, B`.
4. Repeat and choose **No preference** for one question and **None of
   these** for the other. Expect Claude to treat them as "No preference,
   use your judgment" and "None of these".
5. Repeat and click **Decline**: expect Claude to continue on its own
   judgment. **Answer in terminal** must hand the dialog to the terminal.

If Claude reports no answer, or asks the same question again after Send:
set `DEVOTCHI_QUESTIONS=0` and report it.

## 7. Reading keeps a question or plan open

1. `devotchi hold 30`, then ask for a question as in check 5.
2. Click an option every 20 seconds for two minutes without sending.
3. Expect the countdown to jump back to about 60 s after a click once less
   than 55 s are left, and the card to stay open past 30 s, but never past
   5 minutes in total. Untouched, it goes to the terminal after 30 s.

## 8. Clean up

`devotchi setup --remove` in `/tmp/devotchi-p2`, then delete the directory.
Restore your hold time with `devotchi hold <seconds>`.

## Reporting

For each check note: Claude Code version, pass or fail, what you saw, and
the switch you set. After a failed check 1, 2, 4 or 6 run the server with
that switch at `0` until Devotchi is changed, and add the finding to
`TODO.md`.
