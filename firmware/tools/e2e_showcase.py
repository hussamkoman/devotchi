#!/usr/bin/env python3
"""End-to-end test of the showcase (protocol 14.7, ux-flows 8) on a real board against mock_server.py.

Enters the showcase from the quick menu, answers the sample permission and the
sample question in every way and proves from the mock's log that no
permission_response, decide, question_response or permission_defer ever left
the board while it was in the showcase. A real request during the showcase
ends it and needs the hold whatever its risk; the same guard holds for 10 s
after leaving. With a real request queued the showcase cannot be entered from
the console, and can be entered again once it was answered. The leave paths
(REJECT hold, REJECT on the menu, the last row) and the quick menu and Settings
rows are covered too.

The board's pairing, server URL, brightness and haptics are put back at the end.

Run with the ESP-IDF Python:
  ~/.espressif/tools/python/v6.0/venv/bin/python -I firmware/tools/e2e_showcase.py --out /tmp/e2e-showcase
"""

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from devotchi_serial import DEFAULT_PORT, ConsoleError  # noqa: E402
from e2e_flow import Run, frame_of  # noqa: E402
from e2e_link import FORBIDDEN_PORTS, StepFailed, check  # noqa: E402

ANSWER_KINDS = ("permission_response", "decide", "question_response", "permission_defer")
GUARD_S = 10
REAL_ARM_S = 1.5


class ShowcaseRun(Run):
    # ---- helpers ----

    def showcase_on(self):
        self.cmd("showcase on")
        return self.wait_screen(lambda s: s.get("showcase") and s.get("mark") == "demo", 3, "showcase menu")

    def showcase_off(self):
        self.cmd("showcase off")
        self.wait_screen(lambda s: not s.get("showcase"), 3, "showcase left")

    def menu_cursor_to(self, index):
        """Moves the showcase menu's cursor to row index (0 Moods .. 6 Leave showcase)."""
        mark = self.board.mark()
        self.cmd("showcase menu")
        for _ in range(index):
            self.press("action")
        self.board.drain()
        moves = [e for _, e in self.board.events(mark) if e.get("evt") == "showcase" and e.get("op") == "move"]
        check(len(moves) == index, f"{len(moves)} cursor moves, expected {index}")

    def no_answers(self, mark, what):
        sent = [(k, frame_of(e, k)) for e in self.mock.snapshot()[mark:] for k in ANSWER_KINDS if frame_of(e, k)]
        check(not sent, f"{what}: the board sent {sent}")

    def real_request(self, risk="low"):
        rid, _ = self.permission(risk=risk)
        screen = self.wait_approval(rid)
        self.shown_at = time.monotonic()
        return rid, screen

    # ---- steps ----

    def step_quick_menu(self):
        self.wait_pet(5)
        self.press("action", "long")
        screen = self.wait_screen(lambda s: s["id"] == "settings" and s["settings"]["list"] == "quick", 3,
                                  "quick menu")
        check(all(t in screen["texts"] for t in ("Settings", "Showcase", "Desk mode")),
              f"quick menu texts {screen['texts']}")
        self.press("action")
        check(self.screen()["settings"]["row"] == "showcase", "second row is Showcase")
        self.press("approve")
        screen = self.wait_screen(lambda s: s.get("showcase") and s.get("mark") == "demo", 3, "showcase from menu")
        check("Leave showcase" in screen["texts"], f"showcase menu texts {screen['texts']}")
        self.press("reject")
        self.wait_screen(lambda s: not s.get("showcase") and s["id"] == "pet", 3, "REJECT on the menu leaves")
        return "home ACTION hold, Next, Open reached the showcase; REJECT on its menu left it"

    def step_settings_row(self):
        self.cmd("settings open top")
        for _ in range(6):
            self.press("action")
        check(self.screen()["settings"]["row"] == "showcase", "Settings row 7 is Showcase")
        self.press("approve")
        self.wait_screen(lambda s: s.get("showcase"), 3, "showcase from Settings")
        self.showcase_off()
        return "Settings > Showcase entered the showcase"

    def step_sample_permission(self):
        mark = self.mock.mark()
        self.showcase_on()
        self.menu_cursor_to(2)
        self.press("approve")
        screen = self.wait_screen(lambda s: s["id"] == "approval" and s.get("mark") == "sample", 3, "sample card")
        check(screen["request"]["id"] == "sample" and screen["request"]["risk"] == "high",
              f"sample request {screen['request']}")
        bmark = self.board.mark()
        self.press("approve", "long")
        self.wait_receipt("Sample: allowed", bmark)
        self.wait_screen(lambda s: s["id"] == "settings" and s.get("showcase"), 3, "back on the showcase menu")
        # Deny, and the decision menu's send-to-terminal row.
        self.press("approve")
        self.wait_screen(lambda s: s.get("mark") == "sample", 3, "sample card again")
        bmark = self.board.mark()
        self.press("reject")
        self.wait_receipt("Sample: denied", bmark)
        self.press("approve")
        self.wait_screen(lambda s: s.get("mark") == "sample", 3, "sample card a third time")
        self.open_menu()
        self.menu_to("terminal")
        bmark = self.board.mark()
        self.press("approve")
        self.wait_receipt("Sample: sent to terminal", bmark)
        time.sleep(1)
        self.no_answers(mark, "sample permission")
        self.showcase_off()
        return "allowed (hold), denied, sent to terminal: receipts only, no frame"

    def step_sample_question(self):
        mark = self.mock.mark()
        self.showcase_on()
        self.cmd("showcase question")
        screen = self.wait_screen(lambda s: s["id"] == "question" and s.get("mark") == "sample", 3,
                                  "sample question")
        bmark = self.board.mark()
        self.press("approve")
        self.wait_receipt("Sample: answered", bmark)
        time.sleep(1)
        self.no_answers(mark, "sample question")
        self.showcase_off()
        return f"answered with a receipt only; texts {screen['texts'][:3]}"

    def step_pages(self):
        self.showcase_on()
        self.cmd("showcase moods")
        moods = self.wait_screen(lambda s: s["id"] == "pet" and s.get("mark") == "demo", 3, "moods page")
        self.press("action")
        self.cmd("showcase reactions")
        self.wait_screen(lambda s: any(t == "Pat" for t in s["texts"]), 3, "reactions page")
        self.cmd("showcase clock")
        clock = self.wait_screen(lambda s: s["id"] == "clock", 3, "clock face page")
        self.press("action")
        self.wait_screen(lambda s: (s.get("clock") or {}).get("night"), 3, "night face")
        self.cmd("showcase notify")
        bmark = self.board.mark()
        self.wait_screen(lambda s: "Sample notification" in s["texts"], 3, "notification page")
        self.press("approve")
        self.wait_toast("Tests passed", bmark)
        self.press("reject", "long")
        self.wait_screen(lambda s: not s.get("showcase") and s["id"] == "pet", 3, "REJECT hold leaves")
        return f"moods {moods['texts'][:2]}, clock {clock.get('clock')}, notification, REJECT hold left"

    def step_last_row(self):
        self.showcase_on()
        self.menu_cursor_to(6)
        self.press("approve")
        self.wait_screen(lambda s: not s.get("showcase"), 3, "last row leaves")
        return "Leave showcase row left the showcase"

    def step_real_request(self):
        self.showcase_on()
        mark = self.mock.mark()
        rid, screen = self.real_request(risk="low")
        check(not screen.get("showcase") and screen.get("mark") == "real", f"real card marks {screen.get('mark')}")
        self.wait_armed(4)
        armed_after = time.monotonic() - self.shown_at
        check(armed_after >= REAL_ARM_S - 0.4, f"armed after {armed_after:.2f} s, expected about {REAL_ARM_S} s")
        tabs = self.board.json_command("ui tabs")["tabs"]
        check(tabs["approve"]["state"] == "hold_only", f"low-risk real request in the showcase: {tabs['approve']}")
        self.cmd("btn approve press", timeout=15.0)
        time.sleep(1)
        check(not self.frames(mark, ("permission_response",), rid), "a click allowed a guarded real request")
        self.answer_and_confirm(rid, "approve", "long", True)
        check(not self.screen().get("showcase"), "the user is on home afterwards, not in the showcase")
        return "real request ended the showcase, REAL bar, click refused, hold allowed"

    def step_guard_window(self):
        self.showcase_on()
        self.showcase_off()
        rid, screen = self.real_request()
        check(screen.get("mark") == "real", f"inside the guard: mark {screen.get('mark')}")
        self.answer_and_confirm(rid, "reject", "press", False)
        time.sleep(GUARD_S + 1)
        rid, screen = self.real_request()
        check(screen.get("mark") == "none", f"after the guard: mark {screen.get('mark')}")
        self.wait_armed(4)
        tabs = self.board.json_command("ui tabs")["tabs"]
        check(tabs["approve"]["state"] != "hold_only", f"after the guard the low-risk card is a click: {tabs}")
        self.answer_and_confirm(rid, "approve", "press", True)
        return "guarded for 10 s after leaving, normal afterwards"

    def step_refused_with_request(self):
        rid, _ = self.permission(risk="low", timeout=60000)
        self.wait_approval(rid)
        try:
            self.cmd("showcase on")
            raise StepFailed("showcase on succeeded with a request queued")
        except ConsoleError as e:
            check("A request is waiting" in str(e), f"refusal message {e}")
        check(not self.screen().get("showcase"), "showcase must not start")
        self.answer_and_confirm(rid, "reject", "press", False)
        self.showcase_on()
        self.showcase_off()
        return "refused while queued ('A request is waiting'), entered once answered"

    def step_telemetry(self):
        mark = self.mock.mark()
        self.showcase_on()
        event = self.mock.wait_for(lambda e: (frame_of(e, "telemetry") or {}).get("screen") == "showcase", 75,
                                   "telemetry screen showcase", mark)
        self.showcase_off()
        return f"telemetry screen=showcase at {event['t']}"

    # ---- driver ----

    def run(self):
        steps = [
            ("quick-menu", self.step_quick_menu),
            ("settings-row", self.step_settings_row),
            ("sample-permission", self.step_sample_permission),
            ("sample-question", self.step_sample_question),
            ("pages", self.step_pages),
            ("last-row", self.step_last_row),
            ("real-request", self.step_real_request),
            ("guard-window", self.step_guard_window),
            ("refused-with-request", self.step_refused_with_request),
            ("telemetry", self.step_telemetry),
        ]
        selected = self.args.steps.split(",") if self.args.steps else None
        ok = True
        try:
            self.board.open()
            self.board.wait_ready()
            ok = self.run_step("record", self.step_record) and self.run_step("claim", self.step_claim)
            for name, function in steps if ok else []:
                if selected is None or name in selected:
                    ok = self.run_step(name, function) and ok
        finally:
            try:
                self.cmd("showcase off")
            except ConsoleError:
                pass
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
    parser.add_argument("--mock-port", type=int, default=18095, help="TCP port for mock_server.py (18090..18099)")
    parser.add_argument("--host", help="address of this machine as the board sees it (default: derived)")
    parser.add_argument("--out", default="/tmp/devotchi-e2e-showcase", help="directory for logs and result.json")
    parser.add_argument("--steps", help="comma-separated subset of steps (record, claim and restore always run)")
    args = parser.parse_args(argv)
    if args.mock_port in FORBIDDEN_PORTS:
        parser.error(f"port {args.mock_port} belongs to the real server and dashboard")
    args.tls = None
    os.makedirs(args.out, exist_ok=True)
    return ShowcaseRun(args).run()


if __name__ == "__main__":
    sys.exit(main())
