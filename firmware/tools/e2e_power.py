#!/usr/bin/env python3
"""Board test of the power stages (architecture 4.8) on USB power against mock_server.py.

Covers what the bench allows: stage timings on external power and, with an
injected battery, on battery; panel sleep and redraw; the CPU lock; wake by an
incoming permission; the swallowed wake press; the pending-request cap; low and
critical battery and the battery-wake decision in dry-run; the sleep guards
with a USB host attached; an unreachable server that must not keep the screen
on; optionally one forced deep sleep with a timer wake.

Deep-sleep decisions are only reported (dry-run), except for `forced-sleep`.
The steps `reachability` and `guards` expect a build with
CONFIG_DEVOTCHI_PWR_TEST_SECONDS=y (the deep-sleep setting counts seconds).

Pairing, settings, battery injection and the pet freeze are restored at the
end, also when a step fails.

Run with the ESP-IDF Python:
  ~/.espressif/tools/python/v6.0/venv/bin/python -I firmware/tools/e2e_power.py [--steps a,b]
"""

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from devotchi_serial import DEFAULT_PORT, ConsoleError, ConsoleTimeout, save_png  # noqa: E402
from e2e_flow import FIRST_ACK_MAX_S, TEST_BRIGHTNESS, Run as FlowRun  # noqa: E402
from e2e_link import FORBIDDEN_PORTS, StepFailed, check, is_registered  # noqa: E402

DIM_S = 15
# Device-reported idle seconds at a stage change may be one tick late.
TIMING_TOL_S = 2
UNREACHABLE_PORT = 1
TEST_DEEP_MIN = 10


def power_evt(stage=None, action=None):
    def predicate(e):
        if e.get("evt") != "power":
            return False
        if stage is not None and e.get("stage") != stage:
            return False
        return action is None or e.get("action") == action
    return predicate


class PowerRun(FlowRun):
    def __init__(self, args):
        super().__init__(args)
        self.saved = None
        self.measurements.update({"stages": {}, "frame": {}, "latency": {}})

    # ---- helpers ----

    def press(self, button, kind="press"):
        super().press(button, kind)
        time.sleep(0.15)

    def power(self):
        return self.board.json_command("power")

    def wait_power_evt(self, predicate, timeout, mark, what):
        return self.wait_board(predicate, timeout, mark, what)

    def force(self, stage):
        mark = self.board.mark()
        self.cmd(f"power stage {stage}")
        if self.power()["stage"] != stage:
            self.wait_power_evt(power_evt(stage=stage), 3, mark, f"stage {stage}")

    def to_active(self):
        if self.power()["stage"] != "active":
            mark = self.board.mark()
            self.cmd("wake")
            self.wait_power_evt(power_evt(stage="active"), 3, mark, "stage active")

    def battery(self, mv, chrg, stdby, present="yes"):
        self.cmd(f"batt present {present}")
        self.cmd(f"batt inject {mv} {chrg} {stdby}")
        ext = chrg == 1 or stdby == 1
        self.board.wait_state(lambda s: s["battery"]["extPower"] == ext and s["battery"]["injected"], 10,
                              f"battery extPower={ext}")
        # The debounced change is user activity; start every timing from a fresh wake.
        time.sleep(0.5)
        self.cmd("wake")

    def after_boot_grace(self):
        grace = self.power()["timeouts"]["bootGraceS"]
        up = self.board.json_command("version")["uptimeS"]
        if up < grace + 2:
            time.sleep(grace + 2 - up)

    def shot(self, name):
        width, height, raw = self.board.screenshot()
        path = os.path.join(self.args.out, f"{name}.png")
        save_png(path, width, height, raw)
        self.measurements["screenshots"][name] = path
        return width, raw

    @staticmethod
    def diff_box(width, a, b):
        xs, ys = [], []
        for i in range(0, min(len(a), len(b)), 2):
            if a[i:i + 2] != b[i:i + 2]:
                p = i // 2
                xs.append(p % width)
                ys.append(p // width)
        if not xs:
            return None
        return {"pixels": len(xs), "x": [min(xs), max(xs)], "y": [min(ys), max(ys)]}

    # ---- steps ----

    def step_save(self):
        state = self.board.state()
        self.saved = {"dim_s": state["settings"]["dimS"], "deep_min": state["settings"]["deepMin"],
                      "batt": state["settings"]["batt"]}
        self.cmd(f"set dim_s {DIM_S}")
        power = self.power()
        check(power["dryRun"] is True, f"dry-run is not on with a USB host: {power}")
        check(power["pmConfigured"] and power["cpuLock"] and power["cpuMhz"] == 160,
              f"DFS/lock not as expected in active: {power}")
        return f"saved {self.saved}; dryRun on, cpuLock on at {power['cpuMhz']} MHz"

    def step_frame_time(self):
        self.to_active()
        self.cmd("pet freeze off")
        self.send({"type": "mood", "mood": "excited"})
        time.sleep(6)
        state = self.board.state()
        power = self.power()
        check(power["cpuLock"] and power["cpuMhz"] == 160, f"lock not held while animating: {power}")
        self.measurements["frame"] = dict(state["ui"], cpuMhz=power["cpuMhz"])
        return f"frameMsAvg {state['ui']['frameMsAvg']} max {state['ui']['frameMsMax']} at {power['cpuMhz']} MHz"

    def step_usb_timings(self):
        self.cmd("batt present no")
        self.cmd("batt inject off")
        self.cmd("pet freeze on")
        self.send({"type": "mood", "mood": "happy"})
        time.sleep(1)
        mark = self.board.mark()
        self.cmd("wake")
        width, before = self.shot("before-off")
        dim = self.wait_power_evt(power_evt(stage="dim"), 2 * DIM_S + 10, mark, "stage dim")
        check(abs(dim["idleS"] - 2 * DIM_S) <= TIMING_TOL_S, f"dim at idle {dim['idleS']} s, expected {2 * DIM_S}")
        p = self.power()
        check(p["source"] == "external" and p["cpuLock"] and not p["panelAsleep"], f"dim state: {p}")
        off = self.wait_power_evt(power_evt(stage="off"), 8 * DIM_S + 10, mark, "stage off")
        check(abs(off["idleS"] - 10 * DIM_S) <= TIMING_TOL_S, f"off at idle {off['idleS']} s, expected {10 * DIM_S}")
        time.sleep(0.5)
        p = self.power()
        check(p["panelAsleep"] and p["uiParked"] and not p["cpuLock"], f"off state: {p}")
        check(p["wifiPs"] == "min" and not p["lightSleepAllowed"], f"external power off state: {p}")
        st = self.board.state()["power"]
        check(st["panelAsleep"] and st["uiParked"], f"state.power while off: {st}")
        self.measurements["stages"]["usb"] = {"dimIdleS": dim["idleS"], "offIdleS": off["idleS"],
                                              "offCpuMhz": p["cpuMhz"]}
        _, during = self.shot("during-off")
        p = self.power()
        check(p["panelAsleep"] and p["backlightPct"] == 0, f"the screenshot woke the panel: {p}")
        mark = self.board.mark()
        self.cmd("wake")
        self.wait_power_evt(power_evt(stage="active"), 3, mark, "stage active after wake")
        time.sleep(0.3)
        p = self.power()
        check(not p["panelAsleep"] and not p["uiParked"] and p["cpuLock"] and p["cpuMhz"] == 160, f"after wake: {p}")
        check(p["backlightPct"] == TEST_BRIGHTNESS, f"backlight {p['backlightPct']} % after the wake redraw")
        _, after = self.shot("after-wake")
        box_off = self.diff_box(width, before, during)
        box_wake = self.diff_box(width, before, after)
        self.measurements["stages"]["redraw"] = {"beforeVsOff": box_off, "beforeVsAfter": box_wake}
        # Only the status bar's link/battery glyphs may legitimately change between the shots.
        for box in (box_off, box_wake):
            check(box is None or box["y"][1] < 20, f"screen content changed across panel sleep: {box}")
        return (f"dim at {dim['idleS']} s, off at {off['idleS']} s (off cpu {p['cpuMhz']} MHz); "
                f"diff before/off {box_off}, before/after {box_wake}")

    def step_wake_permission(self):
        self.cmd("batt present no")
        self.force("off")
        time.sleep(0.5)
        mark = self.board.mark()
        mock_mark = self.mock.mark()
        rid, sent_t = self.permission()
        self.wait_power_evt(power_evt(stage="active"), 3, mark, "wake on permission")
        self.wait_board(lambda e: e.get("evt") == "fb" and e.get("pattern") == "request", 3, mark,
                        "request feedback")
        t, _ = self.wait_mock_frame("holding", lambda f: rid in f.get("ids", []), 3, mock_mark, "first holding")
        latency = t - sent_t
        self.measurements["latency"]["firstHoldingFromOffS"] = round(latency, 3)
        check(latency <= FIRST_ACK_MAX_S, f"first holding after {latency:.3f} s")
        self.wait_approval(rid)
        self.answer_and_confirm(rid, "approve", "press", True)
        return f"woke from off, fb request, first holding after {latency * 1000:.0f} ms"

    def step_swallow(self):
        self.to_active()
        rid, _ = self.permission()
        self.wait_approval(rid)
        self.force("dim")
        mark = self.board.mark()
        mock_mark = self.mock.mark()
        self.press("approve")
        btn = self.wait_board(lambda e: e.get("evt") == "btn", 2, mark, "btn event")
        check(btn["swallowed"] is True, f"wake press not swallowed: {btn}")
        self.wait_power_evt(power_evt(stage="active"), 2, mark, "active after the wake press")
        self.no_mock_frame(("permission_response",), rid, 1.0, mock_mark)
        check(not any(e.get("evt") == "tx" and e.get("type") == "permission_response"
                      for _, e in self.board.events(mark)), "the wake press sent an answer")
        self.answer_and_confirm(rid, "approve", "press", True)
        return "dim press swallowed without tx; second press answered"

    def step_pending_cap(self):
        self.battery(3900, 0, 0)
        p = self.power()
        check(p["source"] == "battery" and p["timeouts"]["dimS"] == DIM_S and p["timeouts"]["offS"] == 2 * DIM_S,
              f"battery timeouts: {p}")
        mark = self.board.mark()
        rid, _ = self.permission(timeout=120000)
        self.wait_approval(rid)
        self.wait_power_evt(power_evt(stage="dim"), DIM_S + 5, mark, "dim with a pending request")
        time.sleep(2 * DIM_S + 5 - DIM_S)
        p = self.power()
        check(p["stage"] == "dim" and "pending" in p["guards"] and p["idleS"] > 2 * DIM_S,
              f"pending request did not cap at dim: {p}")
        check(not any(e.get("evt") == "power" and e.get("stage") == "off" for _, e in self.board.events(mark)),
              "stage went off with a request pending")
        mark = self.board.mark()
        self.dismiss(rid, "cancelled")
        self.wait_power_evt(power_evt(stage="off"), 3, mark, "off once the queue is empty")
        self.to_active()
        return f"dim held at idle {p['idleS']} s (off timeout {2 * DIM_S} s); off right after the dismiss"

    def step_battery_timings(self):
        self.battery(3900, 0, 0)
        mark = self.board.mark()
        self.cmd("wake")
        dim = self.wait_power_evt(power_evt(stage="dim"), DIM_S + 5, mark, "battery dim")
        off = self.wait_power_evt(power_evt(stage="off"), DIM_S + 5, mark, "battery off")
        check(abs(dim["idleS"] - DIM_S) <= TIMING_TOL_S and abs(off["idleS"] - 2 * DIM_S) <= TIMING_TOL_S,
              f"battery timings dim {dim['idleS']} off {off['idleS']}")
        time.sleep(0.5)
        p = self.power()
        check(p["wifiPs"] == "max" and not p["lightSleepAllowed"] and "usb_host" not in p["guards"],
              f"battery off with a host: {p}")
        self.measurements["stages"]["battery"] = {"dimIdleS": dim["idleS"], "offIdleS": off["idleS"]}
        self.to_active()
        return f"dim at {dim['idleS']} s, off at {off['idleS']} s; wifi max, light sleep refused (USB host)"

    def step_critical(self):
        mark = self.board.mark()
        mock_mark = self.mock.mark()
        self.battery(3200, 0, 0)
        self.wait_toast("Battery low", mark, timeout=5)
        self.wait_screen(lambda s: s["id"] == "lowbatt", 45, "lowbatt screen")
        texts = self.board.texts()
        check("Battery empty" in texts and any(t.startswith("Plug in USB-C") for t in texts), f"lowbatt texts {texts}")
        shown = time.monotonic()
        evt = self.wait_power_evt(power_evt(action="deep_sleep"), 10, mark, "deep-sleep decision")
        check(evt.get("reason") == "battery" and evt.get("dryRun") is True, f"decision {evt}")
        delay = time.monotonic() - shown
        telemetry = [f for f in (e.get("frame") for e in self.mock.snapshot()[mock_mark:] if e.get("ev") == "rx")
                     if isinstance(f, dict) and f.get("type") == "telemetry"]
        check(any(f.get("alert") == "battery_critical" for f in telemetry), f"no battery_critical telemetry")
        toasts = [t for t in self.toasts(mark) if t == "Battery low"]
        check(len(toasts) == 1, f"low toasts {toasts}")
        self.cmd("batt inject 3900 0 0")
        # Idle on battery for longer than 2T, the screen goes off once the card ends; a wake shows the pet.
        self.board.wait_state(lambda s: not s["battery"]["critical"], 10, "critical cleared")
        time.sleep(0.5)
        self.cmd("wake")
        self.wait_screen(lambda s: s["id"] != "lowbatt", 5, "lowbatt gone after recovery")
        return f"toast once, lowbatt, dry-run battery decision {delay:.1f} s after the card appeared"

    def step_wake_test(self):
        self.battery(3200, 0, 0)
        resleep = self.board.json_command("power wake-test battery")
        check(resleep["decision"] == "resleep" and resleep["lowbattMs"] == 3000, f"3200 0 0: {resleep}")
        self.battery(3200, 1, 0)
        boot = self.board.json_command("power wake-test battery")
        check(boot["decision"] == "boot" and boot["extPower"] is True, f"3200 1 0: {boot}")
        self.cmd("batt inject off")
        return f"3200 0 0 -> {resleep['decision']}, 3200 1 0 -> {boot['decision']}"

    def step_guards(self):
        try:
            self.cmd("power sleep deep 10")
            raise StepFailed("power sleep deep without --force was accepted")
        except ConsoleError as e:
            refused = e.message
        self.cmd(f"set deep_min {TEST_DEEP_MIN}")
        self.after_boot_grace()
        self.battery(3900, 0, 0)
        self.cmd("power dry-run off")
        try:
            self.force("off")
            mark = self.board.mark()
            time.sleep(TEST_DEEP_MIN + 3)
            p = self.power()
            check(p["guards"] == ["usb_host"] and not p["deepSleepDue"] and p["usbHost"] and not p["dryRun"],
                  f"guards with a USB host: {p}")
            check(not any(e.get("action") == "deep_sleep" for _, e in self.board.events(mark)),
                  "a deep-sleep decision was taken with a USB host attached")
        finally:
            self.cmd("power dry-run on")
        self.to_active()
        return f"without --force: '{refused}'; dry-run off + host: guards {p['guards']}, no sleep"

    def step_reachability(self):
        self.cmd(f"set deep_min {TEST_DEEP_MIN}")
        host = self.url.rsplit(":", 1)[0]
        self.cmd(f"server {host}:{UNREACHABLE_PORT}")
        self.after_boot_grace()
        self.battery(3900, 0, 0)
        mark = self.board.mark()
        self.cmd("wake")
        dim = self.wait_power_evt(power_evt(stage="dim"), DIM_S + 5, mark, "dim while retrying")
        off = self.wait_power_evt(power_evt(stage="off"), DIM_S + 5, mark, "off while retrying")
        evt = self.wait_power_evt(power_evt(action="deep_sleep"), TEST_DEEP_MIN + 15, mark, "deep-sleep decision")
        check(evt.get("reason") == "idle" and evt.get("dryRun") is True, f"decision {evt}")
        retries = [e for _, e in self.board.events(mark) if e.get("evt") == "link" and e.get("wsPhase") == "running"]
        check(len(retries) >= 2, f"only {len(retries)} connection attempts during the test")
        self.cmd(f"server {self.url}")
        self.board.wait_state(is_registered, 30, "back on the mock")
        self.to_active()
        return (f"dim {dim['idleS']} s, off {off['idleS']} s, dry-run idle sleep after {len(retries)} "
                f"connection attempts")

    def step_forced_sleep(self):
        self.cmd("batt inject off")
        self.cmd("batt present auto")
        self.cmd("power sleep deep 10 --force")
        gone = time.monotonic()
        self.board.close()
        time.sleep(3)
        self.board.open(window_s=60)
        self.board.wait_ready(timeout=60)
        back = time.monotonic() - gone
        state = self.board.wait_state(lambda s: s["link"]["wifi"] == "connected", 30, "WiFi after the wake")
        wifi_s = time.monotonic() - gone
        check(state["reset"] == "deepsleep", f"reset reason {state['reset']}")
        p = self.power()
        check(p["deepSleepWake"] and p["wakeTimer"] and not p["sleptForBattery"], f"wake info {p}")
        self.board.wait_state(is_registered, 30, "registered after the wake")
        registered_s = time.monotonic() - gone
        self.measurements["forcedSleep"] = {"consoleBackS": round(back, 1), "wifiS": round(wifi_s, 1),
                                           "registeredS": round(registered_s, 1)}
        return (f"console back {back:.1f} s after the command, WiFi {wifi_s:.1f} s, registered "
                f"{registered_s:.1f} s; reset=deepsleep, timer wake")

    def step_restore(self):
        try:
            self.cmd("batt inject off")
            self.cmd("pet freeze off")
            if self.saved is not None:
                self.cmd(f"set dim_s {self.saved['dim_s']}")
                self.cmd(f"set deep_min {self.saved['deep_min']}")
                self.cmd(f"set batt {self.saved['batt']}")
        except (ConsoleError, ConsoleTimeout) as e:
            print(f"restore of power settings failed: {e}", flush=True)
        return super().step_restore()

    # ---- driver ----

    def run(self):
        steps = [
            ("frame-time", self.step_frame_time),
            ("usb-timings", self.step_usb_timings),
            ("wake-permission", self.step_wake_permission),
            ("swallow", self.step_swallow),
            ("pending-cap", self.step_pending_cap),
            ("battery-timings", self.step_battery_timings),
            ("critical", self.step_critical),
            ("wake-test", self.step_wake_test),
            ("guards", self.step_guards),
            ("reachability", self.step_reachability),
            ("forced-sleep", self.step_forced_sleep),
        ]
        selected = self.args.steps.split(",") if self.args.steps else [n for n, _ in steps if n not in
                                                                       ("guards", "reachability", "forced-sleep")]
        ok = True
        try:
            self.board.open()
            self.board.wait_ready()
            ok = (self.run_step("record", self.step_record) and self.run_step("claim", self.step_claim)
                  and self.run_step("save", self.step_save))
            for name, function in steps if ok else []:
                if name in selected:
                    ok = self.run_step(name, function) and ok
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
    parser.add_argument("--mock-port", type=int, default=18094, help="TCP port for mock_server.py (18090..18099)")
    parser.add_argument("--host", help="address of this machine as the board sees it (default: derived)")
    parser.add_argument("--out", default="/tmp/devotchi-e2e-power", help="directory for logs, screenshots, result.json")
    parser.add_argument("--steps", help="comma-separated steps (default: all but guards, reachability, forced-sleep)")
    args = parser.parse_args(argv)
    if args.mock_port in FORBIDDEN_PORTS:
        parser.error(f"port {args.mock_port} belongs to the real server and dashboard")
    os.makedirs(args.out, exist_ok=True)
    return PowerRun(args).run()


if __name__ == "__main__":
    sys.exit(main())
