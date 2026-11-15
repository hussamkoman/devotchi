#!/usr/bin/env python3
"""Robustness run on a real board against mock_server.py: malformed frames, a flood, a stuck link, a soak.

Steps (all run by default; --steps selects a subset):
  heap-online        internal heap in the steady ONLINE state
  malformed          invalid JSON, wrong types, an oversized frame, a binary frame and a stray continuation
  flood              200 status frames in 2 s, then a permission that must still be acknowledged and answered
  stuck-link         button events while the client connects to, and is stopped on, a black-holed address
  soak               --soak-minutes of a status every 2 s, a permission every 30 s answered alternately with
                     the approve and reject buttons, and a server restart every 5 minutes
  stacks             stack high-water mark of every task after the steps above
  provisioning-heap  heap while BLE provisioning runs (reboots the board twice; WiFi credentials stay)
  secrets            no token the mock issued or accepted appears anywhere in the captured console output

The board's pairing and server URL are backed up first and put back at the end, also when a step fails.

Run with the ESP-IDF Python:
  ~/.espressif/tools/python/v6.0/venv/bin/python -I firmware/tools/e2e_soak.py --soak-minutes 30
"""

import argparse
import json
import os
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from devotchi_serial import DEFAULT_PORT  # noqa: E402
from e2e_flow import Run  # noqa: E402
from e2e_link import BLACK_HOLE, FORBIDDEN_PORTS, StepFailed, check, is_registered  # noqa: E402

STEADY_HEAP_MIN = 60000
STACK_FREE_MIN = 0.25
BUTTON_LATENCY_MAX_MS = 200
FLOOD_FRAMES = 200
FLOOD_SECONDS = 2
STATUS_PERIOD_S = 2
PERMISSION_PERIOD_S = 30
RESTART_PERIOD_S = 300
SAMPLE_PERIOD_S = 30
# The low-water mark is compared after the first server restart, so one reconnect is already in it.
SOAK_BASELINE_S = 330
HEAP_MIN_DECLINE_MAX = 2048


class Soak(Run):
    def __init__(self, args):
        super().__init__(args)
        self.measurements = {"screenshots": {}}
        self.tokens = set()

    def link(self):
        return self.board.state()["link"]

    def button_latency_ms(self):
        mark = self.board.mark()
        sent = time.monotonic()
        self.cmd("btn action press")
        stamp, _ = self.board.wait_event(lambda e: e.get("evt") == "btn" and e.get("id") == "action", 2, mark)
        return (stamp - sent) * 1000

    def answered_permission(self, approve):
        """Sends a permission, answers it with a button and returns the ack latency in ms."""
        mark = self.mock.mark()
        rid, t0 = self.permission(risk="low")
        t_ack, _ = self.wait_mock_frame("holding", lambda f: rid in f.get("ids", []), 5, mark, f"holding for {rid}")
        self.wait_approval(rid)
        self.press("approve" if approve else "reject")
        _, frame = self.wait_mock_frame("permission_response", lambda f: f.get("id") == rid, 5, mark,
                                        f"permission_response for {rid}")
        check(frame.get("granted") is approve, f"granted is {frame.get('granted')!r}, expected {approve}")
        bmark = self.board.mark()
        self.dismiss(rid, "answered")
        self.wait_board(lambda e: e.get("evt") == "request" and e.get("op") == "remove" and e.get("id") == rid, 5,
                        bmark, "request removed")
        return (t_ack - t0) * 1000

    def max_sockets(self):
        """Largest number of WebSocket connections the mock had open at once, from its log."""
        live = set()
        most = 0
        for event in self.mock.snapshot():
            kind = event.get("ev")
            if kind == "listening":
                live.clear()
            elif kind == "upgrade" and event.get("status") == 101:
                live.add(event["conn"])
                most = max(most, len(live))
            elif kind == "closed":
                live.discard(event["conn"])
        return most

    # ---- steps ----

    def step_claim(self):
        detail = super().step_claim()
        self.tokens.update(self.mock.ctl("tokens")["tokens"])
        return detail

    def step_heap_online(self):
        time.sleep(10)
        state = self.board.state()
        check(is_registered(state), "not registered")
        self.measurements["heapOnline"] = state["heap"]
        check(state["heap"]["internalFree"] >= STEADY_HEAP_MIN,
              f"internal heap free {state['heap']['internalFree']} is under {STEADY_HEAP_MIN}")
        return f"heap {state['heap']}"

    def step_malformed(self):
        before = self.link()
        invalid = ["not json at all", '{"type":"status","msg":7}', '{"type":"permission","id":42,"tool":"Bash"}',
                   '{"msg":"no type"}', "[1,2,3]"]
        for text in invalid:
            check(self.mock.ctl("send-text " + text).get("sent") == 1, "frame not sent")
        oversized = json.dumps({"type": "status", "msg": "x" * 4200}, separators=(",", ":"))
        check(self.mock.ctl("send " + oversized).get("sent") == 1, "oversized frame not sent")
        check(self.mock.ctl("send-binary 0001ff7b7d").get("sent") == 1, "binary frame not sent")
        check(self.mock.ctl('send-continuation {"type":"status","msg":"stray"}').get("sent") == 1,
              "continuation frame not sent")
        text = json.dumps({"type": "status", "msg": "after malformed"}, separators=(",", ":"))
        mark = self.board.mark()
        self.send(json.loads(text))
        event = self.wait_board(lambda e: e.get("evt") == "rx" and e.get("type") == "status", 5, mark, "valid status")
        check(event["crc32"] == f"{zlib.crc32(text.encode()):08x}", "the status after the malformed frames differs")
        state = self.board.state()
        after = state["link"]
        check(is_registered(state), f"link is {after['ws']} after the malformed frames")
        check(after["rxInvalid"] - before["rxInvalid"] == len(invalid),
              f"rxInvalid rose by {after['rxInvalid'] - before['rxInvalid']}, expected {len(invalid)}")
        check(after["rxDropped"] - before["rxDropped"] == 2,
              f"rxDropped rose by {after['rxDropped'] - before['rxDropped']}, expected 2 (oversized and binary)")
        check(state["screen"] == "pet", f"screen is {state['screen']}")
        return (f"{len(invalid)} invalid frames counted, {len(oversized)}-byte and binary frames dropped and counted, "
                "stray continuation ignored, link stayed registered and the next status arrived intact")

    def step_flood(self):
        before = self.board.state()
        mark = self.board.mark()
        reply = self.mock.ctl(f"flood {FLOOD_FRAMES} {FLOOD_SECONDS}")
        check(reply.get("sent") == FLOOD_FRAMES, f"mock wrote {reply.get('sent')} of {FLOOD_FRAMES} frames")
        time.sleep(2)
        state = self.board.wait_state(is_registered, 40, "registered after the flood")
        check(state["uptimeS"] >= before["uptimeS"] and state["reset"] == before["reset"], "the board reset")
        self.board.drain()
        seen = sum(1 for _, e in self.board.events(mark) if e.get("evt") == "rx" and e.get("type") == "status")
        dropped = state["link"]["rxDropped"] - before["link"]["rxDropped"]
        events_dropped = state["eventsDropped"] - before["eventsDropped"]
        # rx EVT lines the console could not write while the host lagged; those frames were still handled.
        evt_dropped = state["evtDropped"] - before["evtDropped"]
        check(seen + dropped + evt_dropped >= FLOOD_FRAMES,
              f"{FLOOD_FRAMES - seen - dropped - evt_dropped} frames neither handled nor counted as dropped")
        ack_ms = self.answered_permission(True)
        self.wait_pet()
        result = {"frames": FLOOD_FRAMES, "rxEvents": seen, "rxDropped": dropped, "evtDropped": evt_dropped,
                  "eventsDropped": events_dropped, "heap": state["heap"], "ackAfterMs": round(ack_ms)}
        self.measurements["flood"] = result
        return f"{result}"

    def step_stuck_link(self):
        self.cmd(f"server ws://{BLACK_HOLE}:{self.args.mock_port}")
        self.board.wait_state(lambda s: s["link"]["wsPhase"] == "running" and s["link"]["ws"] == "connecting", 20,
                              "connect attempt to the black hole")
        connecting = []
        for _ in range(5):
            if self.link()["ws"] != "connecting":
                break
            connecting.append(self.button_latency_ms())
            time.sleep(0.3)
        check(connecting, "no button press landed while the client was connecting")
        self.board.wait_state(lambda s: s["link"]["wsPhase"] == "running" and s["link"]["ws"] == "connecting", 70,
                              "second connect attempt to the black hole")
        time.sleep(0.5)
        self.cmd(f"server {self.url}")
        stopping = []
        for _ in range(5):
            if self.link()["wsPhase"] != "stopping":
                break
            stopping.append(self.button_latency_ms())
            time.sleep(0.3)
        self.board.wait_state(is_registered, 60, "registration after the stuck link")
        result = {"connectingMs": [round(v) for v in connecting], "stoppingMs": [round(v) for v in stopping]}
        self.measurements["stuckLink"] = result
        for value in connecting + stopping:
            check(value < BUTTON_LATENCY_MAX_MS, f"button event took {value:.0f} ms: {result}")
        if not stopping:
            return f"{result}; the stop finished before a press could land in it"
        return f"{result}"

    def sample(self, elapsed):
        state = self.board.state()
        return {"t": round(elapsed), "uptimeS": state["uptimeS"], "reset": state["reset"],
                "internalFree": state["heap"]["internalFree"], "internalMin": state["heap"]["internalMin"],
                "psramFree": state["heap"]["psramFree"], "rxDropped": state["link"]["rxDropped"],
                "rxInvalid": state["link"]["rxInvalid"], "eventsDropped": state["eventsDropped"],
                "ws": state["link"]["ws"], "stage": state["power"]["stage"]}

    def step_soak(self):
        duration = self.args.soak_minutes * 60
        start = self.sample(0)
        samples = [start]
        acks = []
        statuses = permissions = restarts = 0
        started = time.monotonic()
        next_status = STATUS_PERIOD_S
        next_permission = 10
        next_sample = SAMPLE_PERIOD_S - 5
        next_restart = RESTART_PERIOD_S
        while True:
            elapsed = time.monotonic() - started
            if elapsed >= duration:
                break
            if elapsed >= next_restart:
                next_restart += RESTART_PERIOD_S
                self.mock.stop()
                time.sleep(3)
                self.mock.start(*self.mock_options())
                self.board.wait_state(is_registered, 90, f"registration after server restart {restarts + 1}")
                restarts += 1
                continue
            if elapsed >= next_permission:
                next_permission += PERMISSION_PERIOD_S
                acks.append(self.answered_permission(permissions % 2 == 0))
                permissions += 1
                continue
            if elapsed >= next_sample:
                next_sample += SAMPLE_PERIOD_S
                samples.append(self.sample(elapsed))
                continue
            if elapsed >= next_status:
                next_status += STATUS_PERIOD_S
                frame = {"type": "status", "msg": f"soak step {statuses}", "sid": "3fa9c1", "project": "devotchi"}
                if statuses % 10 == 0:
                    frame["mood"] = ("working", "thinking", "happy")[(statuses // 10) % 3]
                self.send(frame)
                statuses += 1
                continue
            self.board.drain()
            time.sleep(0.1)
        end = self.sample(time.monotonic() - started)
        samples.append(end)
        baseline = next((s for s in samples if s["t"] >= SOAK_BASELINE_S), start)
        sockets = self.max_sockets()
        result = {"minutes": self.args.soak_minutes, "statuses": statuses, "permissions": permissions,
                  "restarts": restarts, "ackMsMax": round(max(acks)) if acks else None, "maxSockets": sockets,
                  "baseline": baseline, "end": end,
                  "internalFreeMin": min(s["internalFree"] for s in samples)}
        self.measurements["soak"] = result
        self.measurements["soakSamples"] = samples
        for previous, current in zip(samples, samples[1:]):
            check(current["uptimeS"] >= previous["uptimeS"] and current["reset"] == start["reset"],
                  f"the board reset between {previous['t']} s and {current['t']} s")
        check(end["rxDropped"] == start["rxDropped"], f"rxDropped rose from {start['rxDropped']} to {end['rxDropped']}")
        check(end["eventsDropped"] == start["eventsDropped"], "the app event queue dropped events")
        check(sockets <= 1, f"{sockets} WebSocket connections were open at once")
        check(baseline["internalMin"] - end["internalMin"] <= HEAP_MIN_DECLINE_MAX,
              f"internalMin fell from {baseline['internalMin']} at {baseline['t']} s to {end['internalMin']}")
        return (f"{statuses} statuses, {permissions} permissions (ack max {result['ackMsMax']} ms), {restarts} "
                f"restarts, max sockets {sockets}, internalMin {baseline['internalMin']} at {baseline['t']} s -> "
                f"{end['internalMin']} at {end['t']} s, internalFree {baseline['internalFree']} -> "
                f"{end['internalFree']}, rxDropped +0")

    def step_stacks(self):
        tasks = self.board.json_command("tasks")["tasks"]
        self.measurements["tasks"] = tasks
        tight = [t for t in tasks if "stack" in t and t["free"] < STACK_FREE_MIN * t["stack"]]
        check(not tight, f"stacks with less than 25 % free: {tight}")
        return ", ".join(f"{t['name']} {t['free']}/{t['stack']}" if "stack" in t else f"{t['name']} {t['free']} free"
                         for t in tasks)

    def reboot_and_wait(self, command):
        self.cmd(command)
        time.sleep(3)
        self.board.reopen()
        self.board.wait_ready(40)

    def step_provisioning_heap(self):
        self.reboot_and_wait("wifi-setup")
        try:
            self.board.wait_state(lambda s: s["screen"] == "provisioning", 40, "provisioning screen")
            time.sleep(5)
            state = self.board.state()
            self.measurements["heapProvisioning"] = state["heap"]
        finally:
            self.reboot_and_wait("reboot")
        after = self.board.wait_state(lambda s: s["link"]["wifi"] == "connected" and s["app"] != "provisioning", 60,
                                      "WiFi with the stored credentials after provisioning was abandoned")
        check(after["wifiConfigured"] and after["link"]["ssid"] == self.original["ssid"], "WiFi credentials changed")
        return f"heap while provisioning {state['heap']}; rebooted back onto {after['link']['ssid']!r}"

    def step_secrets(self):
        check(self.tokens, "the mock reported no tokens")
        self.board.drain()
        text = "\n".join(line for _, line in self.board.lines)
        leaked = [t for t in self.tokens if t in text]
        check(not leaked, f"{len(leaked)} token(s) appear in the console output")
        return f"{len(self.tokens)} token(s) absent from {len(self.board.lines)} captured console lines"

    def run(self):
        steps = [
            ("heap-online", self.step_heap_online),
            ("malformed", self.step_malformed),
            ("flood", self.step_flood),
            ("stuck-link", self.step_stuck_link),
            ("soak", self.step_soak),
            ("stacks", self.step_stacks),
            ("provisioning-heap", self.step_provisioning_heap),
            ("secrets", self.step_secrets),
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
                try:
                    ok = self.run_step(name, function) and ok
                except (KeyError, OSError) as e:
                    ok = False
                    print(f"FAIL {name}: {type(e).__name__}: {e}", flush=True)
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
    parser.add_argument("--mock-port", type=int, default=18095, help="TCP port for mock_server.py (18090..18099)")
    parser.add_argument("--host", help="address of this machine as the board sees it (default: derived)")
    parser.add_argument("--out", default="/tmp/devotchi-e2e-soak", help="directory for logs and result.json")
    parser.add_argument("--steps", help="comma-separated subset of steps (record, claim and restore always run)")
    parser.add_argument("--soak-minutes", type=int, default=30, help="length of the soak step")
    parser.add_argument("--tls", nargs=2, metavar=("CERT", "KEY"), help="serve wss/https; needs a firmware build "
                        "with CONFIG_DEVOTCHI_TLS_EXTRA_CA trusting the certificate's CA")
    args = parser.parse_args(argv)
    if args.mock_port in FORBIDDEN_PORTS:
        parser.error(f"port {args.mock_port} belongs to the real server and dashboard")
    os.makedirs(args.out, exist_ok=True)
    return Soak(args).run()


if __name__ == "__main__":
    sys.exit(main())
