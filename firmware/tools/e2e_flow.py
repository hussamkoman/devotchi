#!/usr/bin/env python3
"""End-to-end test of the permission flow and the pet on a real board against mock_server.py.

The mock only delivers frames and logs what the board sends; this script plays
the server's part of protocol v2 (it sends permission, dismiss, error, status,
notify, mood and identify and checks every frame the board sends back, with the
holding timing taken from the mock's log).

The board's pairing and server URL are backed up first (`pair-backup`, which
keeps an existing backup) and put back at the end (`pair-restore`), also when
a step fails. WiFi credentials are never touched.

Run with the ESP-IDF Python:
  ~/.espressif/tools/python/v6.0/venv/bin/python -I firmware/tools/e2e_flow.py
"""

import argparse
import json
import os
import secrets
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from devotchi_serial import DEFAULT_PORT, ConsoleError, ConsoleTimeout, save_png  # noqa: E402
from e2e_link import (FORBIDDEN_PORTS, Board, Mock, StepFailed, check, host_address_for,  # noqa: E402
                      is_registered)

CLAIM_CODE = "K7M2QX"
MOCK_OPTIONS = ("--ping", "10", "--code", CLAIM_CODE)
TEST_BRIGHTNESS = 25
# Without a battery the USB supply sags when the motor starts while WiFi transmits: the board was seen to
# power-cycle at a request buzz and to stall its USB console during the attention pattern. Feedback is
# still checked by pattern name; only the motor output is off during the run.
TEST_HAPTIC = 0
# Protocol 4.7 as the brief measures it: first holding 80..600 ms after the permission, then every 4 +/- 1 s.
FIRST_ACK_MIN_S = 0.08
FIRST_ACK_MAX_S = 0.6
HEARTBEAT_S = 4.0
HEARTBEAT_TOL_S = 1.0
CONFIRM_WINDOW_S = 1.5


def frame_of(event, kind):
    frame = event.get("frame")
    return frame if event.get("ev") == "rx" and isinstance(frame, dict) and frame.get("type") == kind else None


class Run:
    def __init__(self, args):
        self.args = args
        self.board = Board(args.port)
        self.mock = Mock(args.mock_port, os.path.join(args.out, "mock.log"))
        self.results = []
        self.measurements = {"holding": {}, "screenshots": {}}
        # Certificate and key for a wss mock; the firmware must be built to trust the CA that signed them.
        self.tls = getattr(args, "tls", None)
        self.original = None
        self.brightness = None
        self.haptic = None
        self.device_id = None
        self.url = None
        self.prefix = secrets.token_hex(12)
        self.counter = 0
        # id -> mock-log time after which the board must never answer it (dismissed or expired).
        self.closed = {}

    # ---- helpers ----

    def mock_options(self):
        return MOCK_OPTIONS + (("--tls", *self.tls) if self.tls else ())

    def cmd(self, line, timeout=10.0):
        return self.board.command(line, timeout)

    def screen(self):
        return self.board.json_command("screen")

    def wait_screen(self, predicate, timeout, what):
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            last = self.screen()
            if predicate(last):
                return last
            time.sleep(0.2)
        raise StepFailed(f"{what}: not reached within {timeout:g} s; screen={json.dumps(last)[:400]}")

    def new_id(self):
        self.counter += 1
        return f"{self.prefix}{self.counter:08x}"

    def send(self, frame):
        reply = self.mock.ctl("send " + json.dumps(frame, separators=(",", ":")))
        check(reply.get("sent") == 1, f"mock sent the frame to {reply.get('sent')} sockets, expected 1")

    def last_ctl_time(self, mark):
        # The mock's log is read by a thread, so its line may arrive after the control reply.
        event = self.mock.wait_for(lambda e: e.get("ev") == "ctl" and e.get("command") == "send", 2, "send log", mark)
        return event["t"]

    def permission(self, rid=None, risk="low", timeout=30000, summary=None, details=None):
        rid = rid or self.new_id()
        frame = {"type": "permission", "id": rid, "tool": "Bash", "summary": summary or f"echo {rid[-4:]}",
                 "details": details if details is not None else "Run a harmless command\n$ echo ok",
                 "risk": risk, "sid": "3fa9c1", "project": "devotchi", "timeout": timeout}
        mark = self.mock.mark()
        self.send(frame)
        return rid, self.last_ctl_time(mark)

    def dismiss(self, rid, reason, by=None):
        frame = {"type": "dismiss", "id": rid, "reason": reason}
        if reason == "answered":
            frame["outcome"] = "allow"
            frame["by"] = by or self.device_id
        mark = self.mock.mark()
        self.send(frame)
        self.closed[rid] = self.last_ctl_time(mark)

    def holdings(self, mark):
        return [(e["t"], frame_of(e, "holding")["ids"]) for e in self.mock.snapshot()[mark:] if frame_of(e, "holding")]

    def wait_mock_frame(self, kind, predicate, timeout, mark, what):
        event = self.mock.wait_for(lambda e: frame_of(e, kind) is not None and predicate(frame_of(e, kind)),
                                   timeout, what, mark)
        return event["t"], frame_of(event, kind)

    def no_mock_frame(self, kinds, rid, seconds, mark):
        time.sleep(seconds)
        for event in self.mock.snapshot()[mark:]:
            for kind in kinds:
                frame = frame_of(event, kind)
                check(frame is None or frame.get("id") != rid, f"the board sent {kind} for {rid} unexpectedly")

    def wait_board(self, predicate, timeout, mark, what):
        try:
            return self.board.wait_event(predicate, timeout, mark)[1]
        except ConsoleTimeout:
            raise StepFailed(f"board: {what} not seen within {timeout:g} s")

    def wait_toast(self, text, mark, timeout=5):
        return self.wait_board(lambda e: e.get("evt") == "toast" and e.get("text") == text, timeout, mark,
                               f"toast '{text}'")

    def toasts(self, mark):
        return [e["text"] for _, e in self.board.events(mark) if e.get("evt") == "toast"]

    def wait_approval(self, rid, timeout=5):
        return self.wait_screen(lambda s: s["id"] == "approval" and (s.get("request") or {}).get("id") == rid,
                                timeout, f"approval card for {rid}")

    def wait_pet(self, timeout=5):
        return self.wait_screen(lambda s: s["id"] == "pet", timeout, "pet screen")

    def press(self, button, kind="press"):
        self.cmd(f"btn {button} {kind}")

    def screenshot(self, name):
        width, height, raw = self.board.screenshot()
        path = os.path.join(self.args.out, f"{name}.png")
        save_png(path, width, height, raw)
        self.measurements["screenshots"][name] = path
        return path

    def answer_and_confirm(self, rid, button, kind, granted):
        mark = self.mock.mark()
        self.press(button, kind)
        _, frame = self.wait_mock_frame("permission_response", lambda f: f.get("id") == rid, 5, mark,
                                        f"permission_response for {rid}")
        check(frame.get("granted") is granted, f"granted is {frame.get('granted')!r}, expected {granted}")
        screen = self.screen()
        check((screen.get("request") or {}).get("sent") is True and "Sent\u2026" in screen["texts"],
              f"card does not show Sent\u2026: {screen.get('texts')}")
        bmark = self.board.mark()
        self.dismiss(rid, "answered")
        self.wait_board(lambda e: e.get("evt") == "request" and e.get("op") == "remove" and e.get("id") == rid, 5,
                        bmark, "request removed")
        time.sleep(0.3)
        check(not any(t in ("Answered elsewhere", "Not delivered") for t in self.toasts(bmark)),
              f"own answer produced toasts {self.toasts(bmark)}")

    # ---- steps ----

    def step_record(self):
        state = self.board.wait_state(lambda s: s["link"]["wifi"] == "connected", 30, "board on WiFi")
        self.original = {key: state[key] for key in ("server", "serverSource", "paired", "wifiConfigured")}
        self.original["ssid"] = state["link"]["ssid"]
        self.brightness = state["settings"]["brightness"]
        self.haptic = state["settings"]["haptic"]
        self.device_id = state["deviceId"]
        host = self.args.host or host_address_for(state["link"]["ip"])
        self.url = f"{'wss' if self.tls else 'ws'}://{host}:{self.args.mock_port}"
        self.measurements["original"] = self.original
        self.cmd("pair-backup")
        check(self.board.state()["hasBackup"] or not self.original["paired"], "pair-backup left no backup")
        self.cmd(f"set brightness {TEST_BRIGHTNESS}")
        self.cmd(f"set haptic {TEST_HAPTIC}")
        return f"server={self.original['server']} source={self.original['serverSource']} paired={self.original['paired']}"

    def step_claim(self):
        self.mock.start(*self.mock_options())
        self.cmd(f"server {self.url}")
        self.cmd(f"pair {CLAIM_CODE}", timeout=30)
        self.board.wait_state(is_registered, 20, "registration after claim")
        self.wait_pet(10)
        return f"registered on {self.url}; pet screen up"

    def step_allow(self):
        bmark = self.board.mark()
        mark = self.mock.mark()
        rid, t0 = self.permission(risk="low", summary="Run the unit tests", details="$ pnpm test --filter server")
        self.wait_board(lambda e: e.get("evt") == "screen" and e.get("id") == "approval", 5, bmark, "approval EVT")
        screen = self.wait_approval(rid)
        req = screen["request"]
        check(req["count"] == 1 and req["position"] == 1 and req["risk"] == "low", f"request: {req}")
        check("1/1" not in screen["texts"], "position 1/1 is shown")
        check(screen["statusBar"]["visible"] and screen["statusBar"]["link"] == "registered",
              f"statusBar: {screen['statusBar']}")
        self.screenshot("approval-low")
        deadline = time.monotonic() + 3 * HEARTBEAT_S + 2
        while time.monotonic() < deadline and len([h for h in self.holdings(mark) if rid in h[1]]) < 3:
            time.sleep(0.2)
        held = [t for t, ids in self.holdings(mark) if rid in ids]
        check(len(held) >= 3, f"{len(held)} holding frames naming the request, expected 3")
        first = held[0] - t0
        gaps = [round(b - a, 3) for a, b in zip(held, held[1:])]
        self.measurements["holding"]["allow"] = {"firstMs": round(first * 1000), "gapsS": gaps}
        check(FIRST_ACK_MIN_S <= first <= FIRST_ACK_MAX_S, f"first holding after {first * 1000:.0f} ms")
        for gap in gaps:
            check(abs(gap - HEARTBEAT_S) <= HEARTBEAT_TOL_S, f"heartbeat gap {gap} s")
        self.answer_and_confirm(rid, "approve", "press", True)
        self.wait_pet()
        return f"first holding {first * 1000:.0f} ms, heartbeats {gaps} s, approve -> granted true -> pet"

    def step_deny(self):
        rid, _ = self.permission(risk="med")
        self.wait_approval(rid)
        self.answer_and_confirm(rid, "reject", "press", False)
        self.wait_pet()
        return "reject -> granted false -> dismiss -> pet"

    def step_high_risk(self):
        # Five lines that fit the well's 34 columns: two pages of three lines.
        details = "\n".join(f"line {i:02d} " + "x" * 20 for i in range(5))
        rid, _ = self.permission(risk="high", summary="rm -rf dist build node_modules/.cache", details=details)
        screen = self.wait_approval(rid)
        check(screen["request"]["pages"] == 2 and screen["request"]["page"] == 1, f"paging: {screen['request']}")
        approve_tab = (screen.get("tabs") or {}).get("approve") or {}
        check(approve_tab.get("text") == "Hold: Allow", f"approve tab: {screen.get('tabs')}")
        self.press("action")
        screen = self.wait_screen(lambda s: (s.get("request") or {}).get("page") == 2, 3, "details page 2")
        self.press("action")
        self.wait_screen(lambda s: (s.get("request") or {}).get("page") == 1, 3, "details page wraps to 1")
        mark = self.mock.mark()
        bmark = self.board.mark()
        self.press("approve")
        self.wait_board(lambda e: e.get("evt") == "nudge" and e.get("text") == "Hold to allow", 5, bmark,
                        "nudge 'Hold to allow'")
        self.screenshot("approval-high")
        self.no_mock_frame(["permission_response"], rid, CONFIRM_WINDOW_S, mark)
        self.answer_and_confirm(rid, "approve", "long", True)
        self.wait_pet()
        return "click -> nudge 'Hold to allow' and nothing sent; long -> granted true; details paged 1 -> 2 -> 1"

    def step_defer(self):
        rid, _ = self.permission(risk="med")
        self.wait_approval(rid)
        mark = self.mock.mark()
        self.press("action", "long")
        self.wait_mock_frame("permission_defer", lambda f: f.get("id") == rid, 5, mark, "permission_defer")
        bmark = self.board.mark()
        self.dismiss(rid, "released")
        self.wait_pet()
        time.sleep(0.3)
        check(not self.toasts(bmark), f"released produced toasts {self.toasts(bmark)}")
        return "action long -> permission_defer -> dismiss released -> pet, no toast"

    def step_burst(self):
        mark = self.mock.mark()
        ids = []
        times = []
        for _ in range(3):
            rid, t = self.permission(risk="low")
            ids.append(rid)
            times.append(t)
        spread = times[-1] - times[0]
        check(spread <= 0.05, f"the three permissions took {spread * 1000:.0f} ms to send")
        screen = self.wait_screen(lambda s: (s.get("request") or {}).get("count") == 3, 5, "queue of 3")
        check(screen["request"]["id"] == ids[0] and "1/3" in screen["texts"], f"head/position: {screen['texts']}")
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline and not self.holdings(mark):
            time.sleep(0.1)
        time.sleep(max(0.0, times[0] + 1.0 - time.time()))
        held = self.holdings(mark)
        check(held, "no holding frame")
        check(sorted(held[0][1]) == sorted(ids), f"first holding names {held[0][1]}")
        in_first_second = [h for h in held if h[0] - times[0] <= 1.0]
        check(len(in_first_second) <= 2, f"{len(in_first_second)} holding frames in the first second")
        self.measurements["holding"]["burst"] = {"spreadMs": round(spread * 1000),
                                                 "firstMs": round((held[0][0] - times[0]) * 1000),
                                                 "framesInFirstSecond": len(in_first_second)}
        for rid, (button, granted) in zip(ids, [("approve", True), ("reject", False), ("approve", True)]):
            self.wait_approval(rid)
            self.answer_and_confirm(rid, button, "press", granted)
        self.wait_pet()
        return (f"sent within {spread * 1000:.0f} ms, first holding after {(held[0][0] - times[0]) * 1000:.0f} ms "
                f"named all 3, {len(in_first_second)} frame(s) in the first second, answered FIFO")

    def step_overflow(self):
        ids = [self.permission()[0] for _ in range(4)]
        self.wait_screen(lambda s: (s.get("request") or {}).get("count") == 4, 5, "queue of 4")
        time.sleep(0.7)
        bmark = self.board.mark()
        mark = self.mock.mark()
        fifth, _ = self.permission()
        self.wait_board(lambda e: e.get("evt") == "request" and e.get("id") == fifth and e.get("reason") == "full",
                        3, bmark, "fifth refused as full")
        deadline = time.monotonic() + HEARTBEAT_S + 2
        while time.monotonic() < deadline and not self.holdings(mark):
            time.sleep(0.2)
        held = self.holdings(mark)
        check(held, "no heartbeat after the fifth request")
        check(fifth not in held[0][1] and sorted(held[0][1]) == sorted(ids), f"heartbeat names {held[0][1]}")
        bmark = self.board.mark()
        for rid in ids:
            self.dismiss(rid, "cancelled")
        self.dismiss(fifth, "cancelled")
        self.wait_pet()
        check(self.toasts(bmark).count("Handled in terminal") >= 1, f"toasts {self.toasts(bmark)}")
        return "fifth request refused (full) and not named; the heartbeat lists the four queued ids"

    def step_dismiss_reasons(self):
        expected = {"answered": "Answered elsewhere", "cancelled": "Handled in terminal",
                    "timeout": "Timed out - use terminal", "released": None}
        seen = []
        for reason, toast in expected.items():
            rid, _ = self.permission()
            self.wait_approval(rid)
            time.sleep(0.3)
            bmark = self.board.mark()
            mark = self.mock.mark()
            self.dismiss(rid, reason, by="dashboard")
            self.wait_pet()
            time.sleep(0.4)
            toasts = self.toasts(bmark)
            if toast is None:
                check(not toasts, f"{reason}: toasts {toasts}")
            else:
                check(toast in toasts, f"{reason}: toasts {toasts}")
            if reason == "answered":
                self.wait_board(lambda e: e.get("evt") == "fb" and e.get("pattern") == "tick", 2, bmark, "tick")
            self.no_mock_frame(["permission_response", "permission_defer"], rid, 0.2, mark)
            seen.append(f"{reason}: {toasts or 'no toast'}")
        return "; ".join(seen)

    def step_error_with_id(self):
        rid, _ = self.permission()
        self.wait_approval(rid)
        bmark = self.board.mark()
        self.send({"type": "error", "code": "UNKNOWN_REQUEST", "message": "not pending", "id": rid})
        self.closed[rid] = time.time()
        self.wait_toast("No longer pending", bmark)
        self.wait_pet()
        rid2, _ = self.permission()
        self.wait_approval(rid2)
        mark = self.mock.mark()
        self.press("approve")
        self.wait_mock_frame("permission_response", lambda f: f.get("id") == rid2, 5, mark, "response")
        bmark = self.board.mark()
        self.send({"type": "error", "code": "INVALID_MESSAGE", "message": "granted must be a boolean", "id": rid2})
        self.wait_toast("Not delivered", bmark)
        screen = self.wait_approval(rid2)
        check(screen["request"]["sent"] is False and ((screen.get("tabs") or {}).get("approve") or {}).get("text") == "Allow",
              f"buttons not re-enabled: {screen.get('tabs')}")
        mark = self.mock.mark()
        self.press("approve")
        sent_at, _ = self.wait_mock_frame("permission_response", lambda f: f.get("id") == rid2, 5, mark, "resend")
        bmark = self.board.mark()
        self.wait_toast("Not delivered", bmark, timeout=5)
        waited = time.time() - sent_at
        check(2.5 <= waited <= 4.5, f"'Not delivered' after {waited:.1f} s without a dismiss, expected about 3 s")
        screen = self.wait_approval(rid2)
        check(screen["request"]["sent"] is False, "buttons not re-enabled after the confirmation timeout")
        self.dismiss(rid2, "cancelled")
        self.wait_pet()
        return ("UNKNOWN_REQUEST removed the card with 'No longer pending'; INVALID_MESSAGE and a missing dismiss "
                f"({waited:.1f} s) re-enabled the buttons with 'Not delivered'")

    def step_local_expiry(self):
        bmark = self.board.mark()
        mark = self.mock.mark()
        rid, t0 = self.permission(timeout=5000)
        self.wait_approval(rid)
        event = self.wait_board(lambda e: e.get("evt") == "request" and e.get("id") == rid and
                                e.get("reason") == "expired", 8, bmark, "local expiry")
        self.closed[rid] = time.time()
        check(event["count"] == 0, f"queue count {event['count']} after expiry")
        self.wait_toast("Timed out - use terminal", bmark)
        self.wait_pet()
        self.no_mock_frame(["permission_response", "permission_defer"], rid, 1.0, mark)
        late = [t for t, ids in self.holdings(mark) if rid in ids and t > self.closed[rid] + 0.5]
        check(not late, "holding names the expired id")
        return "card removed after 5 s with 'Timed out - use terminal'; nothing sent for it"

    def step_link_loss(self):
        """MOCK-ONLY: the real server resolves a request whose sole holder vanished as holder_lost and never replays it."""
        mark = self.mock.mark()
        rid, _ = self.permission(timeout=30000)
        self.wait_approval(rid)
        self.mock.wait_for(lambda e: frame_of(e, "holding") is not None and rid in frame_of(e, "holding")["ids"],
                           3, "holding", mark)
        bmark = self.board.mark()
        self.mock.stop()
        self.wait_toast("Connection lost - answer in terminal", bmark, timeout=15)
        self.wait_board(lambda e: e.get("evt") == "request" and e.get("reason") == "link_lost", 2, bmark,
                        "link_lost removal")
        screen = self.wait_screen(lambda s: s["id"] == "pet" and (s.get("banner") or "").startswith("Server offline"),
                                  10, "pet with the offline banner")
        check(screen["statusBar"]["link"] in ("offline", "connecting"), f"link dot {screen['statusBar']['link']}")
        self.screenshot("banner")
        self.mock.start(*self.mock_options())
        self.board.wait_state(is_registered, 30, "re-registration")
        self.wait_screen(lambda s: s["id"] == "pet" and s.get("banner") is None, 5, "banner cleared")
        self.permission(rid=rid, timeout=12000)
        screen = self.wait_approval(rid)
        left = screen["request"]["remainingMs"]
        check(left <= 12000, f"replayed request shows {left} ms")
        self.dismiss(rid, "cancelled")
        self.wait_pet()
        return f"queue cleared with the toast, offline banner shown; replay shown again with {left} ms left (mock-only)"

    def step_status_mood(self):
        self.send({"type": "mood", "mood": "happy", "reason": "tests green"})
        self.wait_screen(lambda s: s["mood"] == "happy", 3, "mood happy")
        self.send({"type": "status", "msg": "Edit src/app.c", "project": "devotchi"})
        screen = self.wait_screen(lambda s: "Edit src/app.c" in s["texts"], 3, "status text")
        check(screen["mood"] == "happy", f"status without mood changed the mood to {screen['mood']}")
        # The project label is in the caps style: upper case on screen.
        check("DEVOTCHI" in screen["texts"], f"project header: {screen['texts']}")
        self.screenshot("pet")
        self.send({"type": "status", "msg": "Run tests", "mood": "sad"})
        self.wait_screen(lambda s: s["mood"] == "sad" and "Run tests" in s["texts"], 3, "status with mood")
        return "status without mood kept happy; status with mood sad changed it"

    def step_notify(self):
        notes = []
        bmark = self.board.mark()
        self.send({"type": "notify", "event": "tests_passed", "mood": "proud", "urgency": 0})
        self.wait_screen(lambda s: s["mood"] == "proud", 3, "urgency 0 mood")
        time.sleep(0.5)
        check(not self.toasts(bmark), f"urgency 0 made toasts {self.toasts(bmark)}")
        notes.append("u0 mood only")

        bmark = self.board.mark()
        self.send({"type": "notify", "event": "task_paused", "mood": "idle", "detail": "waiting", "urgency": 1})
        self.wait_toast("task paused: waiting", bmark)
        time.sleep(0.4)
        check(not [e for _, e in self.board.events(bmark) if e.get("evt") == "fb" and e.get("step") is not None
                   and e.get("pattern") not in ("pending",)], "urgency 1 played feedback")
        notes.append("u1 toast")

        bmark = self.board.mark()
        self.send({"type": "notify", "event": "build_failed", "mood": "sad", "detail": "2 errors", "urgency": 2,
                   "source": "ci"})
        self.wait_toast("ci: build failed: 2 errors", bmark)
        self.wait_board(lambda e: e.get("evt") == "fb" and e.get("pattern") == "negative", 2, bmark, "negative")
        bmark = self.board.mark()
        self.send({"type": "notify", "event": "deploy_complete", "mood": "happy", "urgency": 2})
        self.wait_board(lambda e: e.get("evt") == "fb" and e.get("pattern") == "positive", 2, bmark, "positive")
        notes.append("u2 toast + negative/positive")

        bmark = self.board.mark()
        mark = self.mock.mark()
        self.send({"type": "notify", "event": "needs_input", "mood": "anxious", "detail": "Pick a branch",
                   "urgency": 3})
        self.wait_screen(lambda s: s.get("banner") == "Pick a branch", 3, "urgency 3 banner")
        self.wait_board(lambda e: e.get("evt") == "fb" and e.get("pattern") == "attention", 2, bmark, "attention")
        time.sleep(2.5)
        screen = self.screen()
        check(screen.get("banner") == "Pick a branch", "the banner did not stay")
        self.screenshot("toast-banner")
        self.press("reject", "long")
        self.wait_screen(lambda s: s.get("banner") is None, 3, "banner cleared by a button")
        time.sleep(0.5)
        check(not [e for e in self.mock.snapshot()[mark:] if frame_of(e, "set_mode")],
              "the press that cleared the banner also toggled the mode")
        notes.append("u3 banner until a button, press consumed")
        return "; ".join(notes)

    def step_mode(self):
        self.wait_pet()
        for target, toast in (("desk", "At desk: on"), ("ask", "At desk: off")):
            bmark = self.board.mark()
            mark = self.mock.mark()
            self.press("reject", "long")
            self.wait_mock_frame("set_mode", lambda f: f.get("mode") == target, 3, mark, f"set_mode {target}")
            self.wait_toast(toast, bmark)
            self.wait_board(lambda e: e.get("evt") == "fb" and e.get("pattern") == "tick", 2, bmark, "tick")
            self.wait_screen(lambda s: s["statusBar"]["mode"] == target, 3, f"chip {target}")
        return "reject long toggled desk on and off through the mock's mode broadcast"

    def step_identify(self):
        bmark = self.board.mark()
        self.send({"type": "identify", "durationMs": 3000})
        screen = self.wait_screen(lambda s: s["id"] == "identify", 3, "identify screen")
        check(not screen["statusBar"]["visible"], "status bar visible on identify")
        check(any(t for t in screen["texts"]), "identify shows no name")
        self.wait_board(lambda e: e.get("evt") == "fb" and e.get("pattern") == "identify", 2, bmark, "identify fb")
        started = time.monotonic()
        self.wait_pet(6)
        took = time.monotonic() - started
        return f"identify showed {screen['texts']} and returned to pet after about {took + 0.5:.1f} s"

    def step_never_answered_closed(self):
        late = []
        for event in self.mock.snapshot():
            for kind in ("permission_response", "permission_defer"):
                frame = frame_of(event, kind)
                if frame and frame.get("id") in self.closed and event["t"] > self.closed[frame["id"]]:
                    late.append((kind, frame["id"]))
        check(not late, f"responses for closed ids: {late}")
        return f"no response for any of {len(self.closed)} dismissed or expired ids"

    def step_restore(self):
        self.mock.stop()
        if self.original is None:
            return "nothing was changed"
        if self.original["paired"]:
            self.cmd("pair-restore")
        else:
            self.cmd("reset-pairing")
        if self.brightness is not None:
            self.cmd(f"set brightness {self.brightness}")
        if self.haptic is not None:
            self.cmd(f"set haptic {self.haptic}")
        state = self.board.state()
        for key in ("server", "serverSource", "paired", "wifiConfigured"):
            check(state[key] == self.original[key], f"{key} is {state[key]!r}, was {self.original[key]!r}")
        check(state["settings"]["brightness"] == self.brightness and state["settings"]["haptic"] == self.haptic,
              f"settings not restored: {state['settings']}")
        check(state["link"]["ssid"] == self.original["ssid"], "the WiFi network changed")
        return f"server={state['server']} source={state['serverSource']} paired={state['paired']}"

    # ---- driver ----

    def run_step(self, name, function):
        started = time.monotonic()
        try:
            detail = function()
            ok = True
        except (StepFailed, ConsoleError, ConsoleTimeout) as e:
            detail = f"{type(e).__name__}: {e}"
            ok = False
        self.results.append({"step": name, "ok": ok, "seconds": round(time.monotonic() - started, 1), "detail": detail})
        print(f"{'PASS' if ok else 'FAIL'} {name} ({time.monotonic() - started:.1f} s): {detail}", flush=True)
        return ok

    def run(self):
        steps = [
            ("allow", self.step_allow),
            ("deny", self.step_deny),
            ("high-risk", self.step_high_risk),
            ("defer", self.step_defer),
            ("burst", self.step_burst),
            ("overflow", self.step_overflow),
            ("dismiss-reasons", self.step_dismiss_reasons),
            ("error-with-id", self.step_error_with_id),
            ("local-expiry", self.step_local_expiry),
            ("link-loss-mock-only", self.step_link_loss),
            ("status-mood", self.step_status_mood),
            ("notify", self.step_notify),
            ("mode", self.step_mode),
            ("identify", self.step_identify),
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
            ok = self.run_step("never-answered-closed", self.step_never_answered_closed) and ok
        finally:
            ok = self.run_step("restore", self.step_restore) and ok
            self.mock.stop()
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
    parser.add_argument("--mock-port", type=int, default=18092, help="TCP port for mock_server.py (18090..18099)")
    parser.add_argument("--host", help="address of this machine as the board sees it (default: derived)")
    parser.add_argument("--out", default="/tmp/devotchi-e2e-flow", help="directory for logs, screenshots, result.json")
    parser.add_argument("--steps", help="comma-separated subset of steps (record, claim and restore always run)")
    parser.add_argument("--tls", nargs=2, metavar=("CERT", "KEY"), help="serve wss/https; needs a firmware build "
                        "with CONFIG_DEVOTCHI_TLS_EXTRA_CA trusting the certificate's CA")
    args = parser.parse_args(argv)
    if args.mock_port in FORBIDDEN_PORTS:
        parser.error(f"port {args.mock_port} belongs to the real server and dashboard")
    os.makedirs(args.out, exist_ok=True)
    return Run(args).run()


if __name__ == "__main__":
    sys.exit(main())
