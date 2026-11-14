#!/usr/bin/env python3
"""End-to-end check of the pet rig, pages, Settings and telemetry on a real board against mock_server.py.

Uses the record/claim/restore steps of e2e_flow.py: the board's pairing and
server URL are backed up first and put back at the end, also when a step
fails, and every setting the run changes is restored. WiFi credentials are
never touched. Factory reset and WiFi setup are taken only up to their
confirmation prompt; Unpair is executed once, against the mock pairing.

Screenshots of the eleven moods and of the other screens are saved under
--out, with a contact sheet of each set (moods.png, screens.png).

Run with the ESP-IDF Python:
  ~/.espressif/tools/python/v6.0/venv/bin/python -I firmware/tools/e2e_pet.py
"""

import argparse
import hashlib
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from devotchi_serial import DEFAULT_PORT  # noqa: E402
from e2e_flow import CLAIM_CODE, Run as FlowRun, frame_of  # noqa: E402
from e2e_link import FORBIDDEN_PORTS, StepFailed, check, is_registered  # noqa: E402

MOODS = ["happy", "sad", "excited", "anxious", "sleeping", "angry", "proud", "idle", "focused", "working",
         "thinking"]
SETTING_KEYS = ("brightness", "dimS", "deepMin", "haptic", "led", "batt")
SET_KEYS = {"brightness": "brightness", "dimS": "dim_s", "deepMin": "deep_min", "haptic": "haptic", "led": "led"}
TELEMETRY_MAX = 300
PERIOD_S = 60
CHANGE_MAX_S = 2.0
CONFIRM_S = 5.0
PAGE_IDLE_S = 20
FRAME_BUDGET_MS = 33


CHOICES = {"brightness": [25, 50, 75, 100], "dimS": [15, 30, 60, 120], "deepMin": [10, 20, 60, 0], "led": [0, 1]}


def next_choice(key, value):
    values = CHOICES[key]
    return values[(values.index(value) + 1) % len(values)] if value in values else values[0]


def telemetry_frames(events):
    return [(e["t"], e.get("bytes"), frame_of(e, "telemetry")) for e in events if frame_of(e, "telemetry")]


class PetRun(FlowRun):
    def __init__(self, args):
        super().__init__(args)
        self.settings = None
        self.registered_t = None
        self.measurements.update({"moods": {}, "telemetry": [], "frames": {}})

    # ---- helpers ----

    def press(self, button, kind="press"):
        # `btn` returns once the press is queued; give the app task time to act on it before the next read.
        super().press(button, kind)
        time.sleep(0.15)

    def pose(self):
        return self.board.json_command("pet pose")

    def contact_sheet(self, names, path, columns):
        from PIL import Image

        shots = [Image.open(self.measurements["screenshots"][n]) for n in names]
        width, height = shots[0].size
        rows = (len(shots) + columns - 1) // columns
        sheet = Image.new("RGB", (columns * (width + 4), rows * (height + 4)), (60, 60, 60))
        for i, shot in enumerate(shots):
            sheet.paste(shot, ((i % columns) * (width + 4), (i // columns) * (height + 4)))
        sheet.save(path)
        self.measurements["screenshots"][os.path.basename(path)[:-4]] = path

    def settings_screen(self):
        screen = self.screen()
        check(screen["id"] == "settings", f"settings not on screen: {screen['id']}")
        return screen

    def go_to_row(self, key):
        for _ in range(12):
            if self.settings_screen()["settings"]["row"] == key:
                return
            self.press("approve")
        raise StepFailed(f"row {key} not reached")

    # ---- steps ----

    def step_record(self):
        self.settings = {k: self.board.state()["settings"][k] for k in SETTING_KEYS}
        self.measurements["settings"] = self.settings
        return super().step_record()

    def step_claim(self):
        super().step_claim()
        event = self.mock.wait_for(lambda e: e.get("ev") == "registered", 5, "registration")
        self.registered_t = event["t"]

    def step_telemetry_register(self):
        event = self.mock.wait_for(lambda e: frame_of(e, "telemetry") is not None and e["t"] >= self.registered_t,
                                   5, "telemetry after register_ack")
        frame = frame_of(event, "telemetry")
        delay = event["t"] - self.registered_t
        check(delay < 2.0, f"first telemetry {delay:.2f} s after registration")
        check(event["bytes"] <= TELEMETRY_MAX, f"telemetry is {event['bytes']} bytes")
        for key in ("battery", "power", "rssi", "uptimeS", "heapFree", "heapMin", "resetReason", "fw"):
            check(key in frame, f"telemetry lacks {key}: {frame}")
        check(frame.get("stage") == "active" and frame.get("screen"), f"screen/stage missing: {frame}")
        self.measurements["telemetry"].append({"reason": "register", "bytes": event["bytes"], "frame": frame})
        return f"{event['bytes']} bytes, {delay * 1000:.0f} ms after registration: {json.dumps(frame)}"

    def step_moods(self):
        self.wait_pet()
        self.cmd("pet freeze on")
        hashes = {}
        for mood in MOODS:
            self.cmd(f"pet mood {mood}")
            pose = self.pose()
            check(pose["mood"] == mood and pose["tweenPermille"] == 1000 and pose["frozen"], f"pose {pose}")
            screen = self.screen()
            check(screen["id"] == "pet" and screen["mood"] == mood, f"screen {screen['id']} mood {screen['mood']}")
            path = self.screenshot(f"mood-{mood}")
            with open(path, "rb") as f:
                hashes[mood] = hashlib.sha256(f.read()).hexdigest()
            self.measurements["moods"][mood] = {"pose": pose["pose"], "png": path}
        check(len(set(hashes.values())) == len(MOODS), "two moods produced identical screenshots")
        self.contact_sheet([f"mood-{m}" for m in MOODS], os.path.join(self.args.out, "moods.png"), 4)
        return f"11 distinct screenshots, sheet {os.path.join(self.args.out, 'moods.png')}"

    def step_animating_screenshot(self):
        self.cmd("pet freeze off")
        self.cmd("pet mood excited")
        time.sleep(0.5)
        check(self.pose()["animating"], "the rig is not animating")
        self.screenshot("animating-excited")
        return "screenshot of the bobbing rig passed its CRC"

    def step_tween(self):
        self.cmd("pet mood happy")
        time.sleep(0.6)
        start = time.monotonic()
        self.cmd("pet mood sad")
        samples = []
        while time.monotonic() - start < 0.8:
            p = self.pose()
            samples.append((round(time.monotonic() - start, 3), p["tweenPermille"], p["pose"]["mouthCurve"],
                            p["pose"]["bodyColor"]))
        mids = [s for s in samples if 0 < s[1] < 1000]
        check(mids, f"no intermediate pose seen: {samples}")
        check(samples[-1][1] == 1000 and samples[-1][2] == -70, f"tween did not end on sad: {samples[-1]}")
        done = next(s[0] for s in samples if s[1] == 1000)
        self.measurements["tween"] = samples
        return f"{len(mids)} intermediate samples, at target after <= {done:.2f} s: {samples[:6]}"

    def frame_numbers(self, mood, seconds=6):
        self.cmd(f"pet mood {mood}")
        time.sleep(seconds)
        ui = self.board.state()["ui"]
        self.measurements["frames"][mood] = ui
        return ui

    def step_frame_time(self):
        self.cmd("pet freeze off")
        results = {m: self.frame_numbers(m) for m in ("excited", "anxious", "working", "idle")}
        worst = max(r["frameMsAvg"] for r in results.values())
        check(worst <= FRAME_BUDGET_MS, f"frameMsAvg {worst} above {FRAME_BUDGET_MS}: {results}")
        return "; ".join(f"{m} avg {r['frameMsAvg']} max {r['frameMsMax']} ms" for m, r in results.items())

    def step_pat_poke(self):
        self.cmd("pet mood idle")
        mark = self.board.mark()
        self.press("approve")
        self.wait_board(lambda e: e.get("evt") == "pet" and e.get("action") == "pat", 3, mark, "pat")
        self.wait_board(lambda e: e.get("evt") == "fb" and e.get("pattern") == "tick", 3, mark, "tick pattern")
        check(self.pose()["reaction"] == "pat", "pat not shown")
        time.sleep(1.7)
        check(self.pose()["reaction"] == "none", "pat did not end after 1.5 s")
        mark = self.board.mark()
        self.press("reject")
        self.wait_board(lambda e: e.get("evt") == "pet" and e.get("action") == "poke", 3, mark, "poke")
        time.sleep(0.8)
        check(self.pose()["reaction"] == "none", "poke did not end after 600 ms")
        time.sleep(2.1)
        mark = self.board.mark()
        for _ in range(3):
            self.press("reject")
        actions = [e["action"] for _, e in self.board.events(mark) if e.get("evt") == "pet"]
        if len(actions) < 3:
            self.wait_board(lambda e: e.get("evt") == "pet" and e.get("action") == "dizzy", 3, mark, "dizzy")
            actions = [e["action"] for _, e in self.board.events(mark) if e.get("evt") == "pet"]
        check(actions == ["poke", "poke", "dizzy"], f"three pokes gave {actions}")
        check(self.pose()["reaction"] == "dizzy", "dizzy not shown")
        time.sleep(2.2)
        check(self.pose()["reaction"] == "none", "dizzy did not end after 2 s")
        return "pat (tick pattern), poke, three pokes -> dizzy; each ended on time"

    def step_pages(self):
        self.cmd("pet freeze on")
        self.cmd("pet mood happy")
        self.press("action")
        screen = self.wait_screen(lambda s: s["page"] == "about", 3, "about page")
        texts = screen["texts"]
        for prefix in ("Name: ", "Firmware: ", "Device: dv-", "Uptime: ", "Reset: ", "WiFi: ", "IP: ", "RSSI: ",
                       "Server: ws://", "Link: online", "Error: ", "NVS: ok"):
            check(any(t.startswith(prefix) for t in texts), f"About lacks '{prefix}': {texts}")
        check(not self.pose()["animating"], "the face animates behind the About page")
        self.screenshot("about")
        self.press("action")
        self.wait_screen(lambda s: s["page"] == "pet", 3, "back to the pet page")
        check(self.pose()["animating"] or self.pose()["frozen"], "face not running again")
        mark = self.board.mark()
        self.press("action")
        self.wait_screen(lambda s: s["page"] == "about", 3, "about page again")
        t0 = time.monotonic()
        self.wait_board(lambda e: e.get("evt") == "page" and e.get("page") == "pet", PAGE_IDLE_S + 3, mark,
                        "return to the pet page")
        idle = time.monotonic() - t0
        check(PAGE_IDLE_S - 1 <= idle <= PAGE_IDLE_S + 2, f"About returned after {idle:.1f} s")
        return f"ACTION cycles pet/about; About has every row; idle return after {idle:.1f} s"

    def step_settings(self):
        self.cmd("pet freeze on")
        mark = self.board.mark()
        self.press("action", "long")
        screen = self.wait_screen(lambda s: s["id"] == "settings", 3, "settings screen")
        check(screen["page"] == "main" and screen["settings"]["row"] == "brightness", f"settings {screen}")
        labels = screen["texts"]
        for label in ("Brightness", "Screen timeout", "Deep sleep", "Haptics", "LED", "Mode", "WiFi setup", "Unpair",
                      "Factory reset", "About"):
            check(label in labels, f"row {label} missing: {labels}")
        self.press("approve")
        check(self.settings_screen()["settings"]["row"] == "screen_timeout", "APPROVE is not down")
        self.press("reject")
        check(self.settings_screen()["settings"]["row"] == "brightness", "REJECT is not up")
        self.press("reject")
        check(self.settings_screen()["settings"]["row"] == "about", "up from the first row does not wrap")
        self.press("approve")

        current = self.board.state()["settings"]
        changes = [(row, key, next_choice(key, current[key])) for row, key in
                   (("brightness", "brightness"), ("screen_timeout", "dimS"), ("deep_sleep", "deepMin"), ("led", "led"))]
        for row, key, expected in changes:
            self.go_to_row(row)
            self.press("action")
            value = self.board.state()["settings"][key]
            check(value == expected, f"{row}: setting {key} is {value}, expected {expected}")
        self.go_to_row("mode")
        mmark = self.mock.mark()
        self.press("action")
        self.wait_mock_frame("set_mode", lambda f: f.get("mode") == "desk", 3, mmark, "set_mode desk")
        self.wait_screen(lambda s: "Desk" in s["texts"] and s["statusBar"]["mode"] == "desk", 3, "mode Desk")
        self.screenshot("settings")
        mmark = self.mock.mark()
        self.press("action")
        self.wait_mock_frame("set_mode", lambda f: f.get("mode") == "ask", 3, mmark, "set_mode ask")
        self.wait_screen(lambda s: "Ask" in s["texts"], 3, "mode Ask")

        # Holding ACTION exits and never also activates the row under the cursor.
        before = self.board.state()["settings"]
        self.press("action", "long")
        self.wait_pet()
        check(self.board.state()["settings"] == before, "the exit press changed a setting")
        events = [e for _, e in self.board.events(mark) if e.get("evt") == "settings"]
        check(events[-1]["op"] == "exit", f"last settings event {events[-1]}")

        self.cmd("reboot")
        time.sleep(2)
        self.board.wait_ready()
        state = self.board.wait_state(is_registered, 40, "registration after reboot")
        persisted = {key: state["settings"][key] for _, key, _ in changes}
        check(persisted == {key: v for _, key, v in changes}, f"not persisted across reboot: {persisted}")
        for key, set_key in SET_KEYS.items():
            if key != "haptic":
                self.cmd(f"set {set_key} {self.settings[key]}")
        self.cmd("set brightness 25")
        return f"navigation, value cycling, mode via set_mode, exit; persisted across reboot: {persisted}"

    def step_confirm_rows(self):
        self.wait_pet(10)
        self.press("action", "long")
        self.wait_screen(lambda s: s["id"] == "settings", 3, "settings screen")
        results = []
        for row in ("wifi_setup", "factory_reset", "unpair"):
            self.go_to_row(row)
            mark = self.board.mark()
            self.press("action")
            asked = time.monotonic()
            screen = self.settings_screen()
            check(screen["settings"]["confirming"] and "Press ACTION again to confirm" in screen["texts"],
                  f"{row}: no confirmation prompt: {screen['texts']}")
            if row == "factory_reset":
                self.screenshot("settings-confirm")
            stamp, _ = self.board.wait_event(lambda e: e.get("evt") == "settings" and e.get("op") == "cancel",
                                             CONFIRM_S + 2, mark)
            waited = stamp - asked
            check(CONFIRM_S - 0.5 <= waited <= CONFIRM_S + 1.0, f"{row}: prompt ended after {waited:.1f} s")
            check(not self.settings_screen()["settings"]["confirming"], f"{row} still confirming")
            results.append(f"{row} prompt timed out after ~{waited:.1f} s")
        # Moving the cursor cancels a prompt at once.
        self.press("action")
        check(self.settings_screen()["settings"]["confirming"], "unpair prompt missing")
        self.press("reject")
        screen = self.settings_screen()
        check(not screen["settings"]["confirming"] and screen["settings"]["row"] == "wifi_setup",
              "moving the cursor did not cancel")
        self.press("action", "long")
        self.wait_pet()
        return "; ".join(results) + "; cursor move cancels"

    def tx_telemetry(self):
        return [(stamp, e) for stamp, e in self.board.events() if e.get("evt") == "tx" and e.get("type") == "telemetry"]

    def step_telemetry_period(self):
        registers = [stamp for stamp, e in self.tx_telemetry() if e.get("reason") == "register"]
        check(registers, "no register telemetry in the board log")
        start = registers[-1]
        while time.monotonic() < start + PERIOD_S + 5:
            self.board.mark()
            if [s for s, e in self.tx_telemetry() if e.get("reason") == "period" and s > start]:
                break
            time.sleep(1)
        sent = [(round(s - start, 1), e["reason"], e["bytes"], e["sent"]) for s, e in self.tx_telemetry() if s >= start]
        periods = [t for t, reason, _, _ in sent if reason == "period"]
        check(periods, f"no periodic telemetry within {PERIOD_S + 5} s: {sent}")
        # The app checks the period on its 1 Hz tick.
        check(PERIOD_S - 0.5 <= periods[0] <= PERIOD_S + 2.0, f"first periodic sample at {periods[0]} s")
        check(all(ok for _, _, _, ok in sent), f"a sample was not sent: {sent}")
        times = [t for t, _, _, _ in sent]
        check(all(b - a >= 1.0 for a, b in zip(times, times[1:])), f"telemetry faster than 1/s: {sent}")
        frames = telemetry_frames(self.mock.snapshot())
        check(all(b <= TELEMETRY_MAX for _, b, _ in frames), "a telemetry frame is over 300 bytes")
        return f"since the last registration: {sent}; mock received {len(frames)}, max {max(b for _, b, _ in frames)} bytes"

    def wait_telemetry(self, mark, bmark, predicate, what):
        # Protocol 4.12 counts from the change as the device sees it: its power EVT (the
        # charger pins are debounced for 2 s), not from the console command.
        event = self.mock.wait_for(lambda e: frame_of(e, "telemetry") is not None and predicate(frame_of(e, "telemetry")),
                                   CHANGE_MAX_S + 4, what, mark)
        frame = frame_of(event, "telemetry")
        self.board.drain()
        events = self.board.events(bmark)
        sent = [s for s, e in events if e.get("evt") == "tx" and e.get("type") == "telemetry"]
        check(sent, f"{what}: no tx telemetry EVT")
        changes = [s for s, e in events if e.get("evt") == "power" and s <= sent[-1]]
        check(changes, f"{what}: no power EVT before the telemetry")
        delay = sent[-1] - changes[-1]
        self.measurements["telemetry"].append({"reason": what, "bytes": event["bytes"], "frame": frame,
                                               "delayS": round(delay, 2)})
        check(event["bytes"] <= TELEMETRY_MAX, f"{what}: {event['bytes']} bytes")
        return frame, delay

    def step_telemetry_battery(self):
        out = []
        try:
            cases = [
                ("batt inject 3900 0 0", lambda f: f["battery"]["state"] == "discharging" and f["power"] == "battery",
                 "discharging"),
                ("batt inject 3700 0 0", lambda f: f["battery"]["state"] == "discharging" and
                 f["battery"]["pct"] is not None, "5 % step"),
                ("batt inject 3450 0 0", lambda f: f.get("alert") == "battery_low", "alert battery_low"),
                ("batt inject 4000 1 0", lambda f: f["battery"]["state"] == "charging" and "alert" not in f,
                 "charging, alert cleared"),
                ("batt present no", lambda f: f["battery"]["state"] == "absent" and f["battery"]["pct"] is None and
                 "alert" not in f and f["power"] == "usb", "batt present no"),
            ]
            pct = None
            for command, predicate, what in cases:
                time.sleep(1.6)
                mark = self.mock.mark()
                bmark = self.board.mark()
                self.cmd(command)
                frame, delay = self.wait_telemetry(mark, bmark, predicate, what)
                check(delay <= CHANGE_MAX_S + 0.3, f"{what}: telemetry {delay:.2f} s after the change")
                if what == "5 % step" and pct is not None:
                    check(abs(frame["battery"]["pct"] - pct) >= 5, f"pct {pct} -> {frame['battery']['pct']}")
                pct = frame["battery"].get("pct")
                out.append(f"{what} after {delay:.2f} s: {json.dumps(frame['battery'])}"
                           f"{' alert ' + frame['alert'] if 'alert' in frame else ''}")
        finally:
            self.cmd("batt inject off")
            self.cmd(f"set batt {self.settings['batt']}")
        state = self.board.state()["battery"]
        check(not state["injected"], "injection still on")
        return "; ".join(out)

    def step_unpair(self):
        self.wait_pet(10)
        check(self.board.state()["hasBackup"], "no backup: unpair is only run with a backup in place")
        self.press("action", "long")
        self.wait_screen(lambda s: s["id"] == "settings", 3, "settings screen")
        self.go_to_row("unpair")
        self.press("action")
        mark = self.board.mark()
        self.press("action")
        self.wait_board(lambda e: e.get("evt") == "settings" and e.get("op") == "execute" and e.get("row") == "unpair",
                        3, mark, "unpair executed")
        state = self.board.wait_state(lambda s: s["app"] == "pairing" and not s["paired"], 10, "pairing after unpair")
        check(state["hasBackup"], "the backup is gone")
        check(state["link"]["ws"] == "down", f"socket still {state['link']['ws']}")
        self.wait_screen(lambda s: s["id"] == "pairing", 10, "pairing screen")
        # Settings opens from the pairing screen as well.
        self.press("action", "long")
        self.wait_screen(lambda s: s["id"] == "settings", 3, "settings from pairing")
        self.press("action", "long")
        self.wait_screen(lambda s: s["id"] == "pairing", 3, "back to pairing")
        self.cmd(f"pair {CLAIM_CODE}", timeout=30)
        self.board.wait_state(is_registered, 20, "registration after re-pairing")
        return "Unpair erased the mock pairing (backup kept), showed pairing; Settings opens from pairing"

    def step_preview_settings(self):
        self.cmd("preview settings")
        screen = self.wait_screen(lambda s: s["id"] == "settings" and s["preview"], 3, "settings preview")
        self.press("action", "long")
        check(self.screen()["id"] == "settings", "buttons act on a preview")
        self.cmd("preview off")
        self.wait_pet()
        return f"preview shows {len(screen['texts'])} texts"

    def step_restore(self):
        try:
            self.cmd("pet freeze off")
        finally:
            detail = super().step_restore()
        for key, set_key in SET_KEYS.items():
            if key not in ("brightness", "haptic"):
                self.cmd(f"set {set_key} {self.settings[key]}")
        self.cmd(f"set batt {self.settings['batt']}")
        state = self.board.state()
        current = {k: state["settings"][k] for k in SETTING_KEYS}
        check(current == self.settings, f"settings {current}, were {self.settings}")
        # Settings also opens from the connecting screen the restored pairing leads to.
        screen = self.wait_screen(lambda s: s["id"] in ("connecting", "pet", "wifi"), 15, "a link screen")
        if screen["id"] in ("connecting", "wifi"):
            self.press("action", "long")
            self.wait_screen(lambda s: s["id"] == "settings", 3, "settings from " + screen["id"])
            self.press("action", "long")
            self.wait_screen(lambda s: s["id"] == screen["id"], 3, "back to " + screen["id"])
        return f"{detail}; settings {current}"

    def run(self):
        steps = [
            ("telemetry-register", self.step_telemetry_register),
            ("moods", self.step_moods),
            ("animating-screenshot", self.step_animating_screenshot),
            ("tween", self.step_tween),
            ("frame-time", self.step_frame_time),
            ("pat-poke", self.step_pat_poke),
            ("pages", self.step_pages),
            ("settings", self.step_settings),
            ("confirm-rows", self.step_confirm_rows),
            ("preview-settings", self.step_preview_settings),
            ("telemetry-period", self.step_telemetry_period),
            ("telemetry-battery", self.step_telemetry_battery),
            ("unpair", self.step_unpair),
        ]
        selected = self.args.steps.split(",") if self.args.steps else None
        ok = True
        try:
            self.board.open()
            self.board.wait_ready()
            ok = self.run_step("record", self.step_record) and self.run_step("claim", self.step_claim)
            for name, function in steps if ok else []:
                if selected is not None and name not in selected:
                    continue
                ok = self.run_step(name, function) and ok
        finally:
            ok = self.run_step("restore", self.step_restore) and ok
            self.mock.stop()
            shots = self.measurements["screenshots"]
            others = [n for n in ("about", "settings", "settings-confirm", "animating-excited") if n in shots]
            if others:
                self.contact_sheet(others, os.path.join(self.args.out, "screens.png"), 2)
            self.board.drain()
            with open(os.path.join(self.args.out, "board.log"), "w", encoding="utf-8") as f:
                f.write("\n".join(text for _, text in self.board.lines) + "\n")
            self.board.close()
        with open(os.path.join(self.args.out, "result.json"), "w", encoding="utf-8") as f:
            json.dump({"ok": ok, "steps": self.results, "measurements": self.measurements}, f, indent=2)
        print(f"{'OK' if ok else 'FAILED'}: results in {self.args.out}", flush=True)
        return 0 if ok else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", default=DEFAULT_PORT, help="serial console of the board")
    parser.add_argument("--mock-port", type=int, default=18093, help="TCP port for mock_server.py (18090..18099)")
    parser.add_argument("--host", help="address of this machine as the board sees it (default: derived)")
    parser.add_argument("--out", default="/tmp/devotchi-e2e-pet", help="directory for logs, screenshots, result.json")
    parser.add_argument("--steps", help="comma-separated subset of steps (record, claim and restore always run)")
    args = parser.parse_args(argv)
    if args.mock_port in FORBIDDEN_PORTS:
        parser.error(f"port {args.mock_port} belongs to the real server and dashboard")
    os.makedirs(args.out, exist_ok=True)
    return PetRun(args).run()


if __name__ == "__main__":
    sys.exit(main())
