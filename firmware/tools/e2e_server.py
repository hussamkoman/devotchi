#!/usr/bin/env python3
"""End-to-end test of a real board against the real Devotchi server (packages/server) and CLI.

Starts a temporary server on a test port with its own database and a temporary
HOME and config directory, pairs the board with it and plays Claude Code's part
by posting hook events to /hook/event. The script needs the built TypeScript
packages (`pnpm run build`) and `node` on PATH.

The board's pairing and server URL are backed up first (`pair-backup`, which
keeps an existing backup) and put back at the end (`pair-restore`), also when a
step fails. WiFi credentials are never touched. Neither ~/.devotchi nor the
real server's ports are used.

Run with the ESP-IDF Python:
  ~/.espressif/tools/python/v6.0/venv/bin/python -I firmware/tools/e2e_server.py
"""

import argparse
import http.client
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from devotchi_serial import DEFAULT_PORT  # noqa: E402
from e2e_flow import Run  # noqa: E402
from e2e_link import FORBIDDEN_PORTS, StepFailed, check, is_registered  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SERVER_JS = os.path.join(REPO, "packages", "server", "dist", "index.js")
CLI_JS = os.path.join(REPO, "packages", "client", "dist", "cli.js")
SESSION = "e2e-session-1"
ALLOW = {"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": {"behavior": "allow"}}}
# The server notices a rebooted board by its missing heartbeat (ALIVE, 12 s) or by its new socket.
HOLDER_LOST_MAX_S = 15
CODE_ON_SCREEN = re.compile(r"([A-Z2-9]{3}) ([A-Z2-9]{3})")


def shown_code(screen):
    """The pairing code on the screen (shown as two groups, protocol 9.1), without the space; None when absent."""
    for text in screen.get("texts", []):
        match = CODE_ON_SCREEN.fullmatch(text)
        if match:
            return match.group(1) + match.group(2)
    return None


class Hook(threading.Thread):
    """One POST /hook/event held open until the server answers."""

    def __init__(self, run, event):
        super().__init__(daemon=True)
        self.run_ref = run
        self.event = event
        self.reply = None
        self.error = None
        self.seconds = None
        self.start()

    def run(self):
        started = time.monotonic()
        try:
            self.reply = self.run_ref.api("POST", "/hook/event", self.event, timeout=120,
                                          headers={"X-Devotchi-Hook": "1"})
        except (OSError, ValueError, StepFailed) as e:
            self.error = e
        self.seconds = time.monotonic() - started

    def result(self, timeout):
        self.join(timeout)
        check(not self.is_alive(), f"the hook got no reply within {timeout:g} s")
        check(self.error is None, f"hook request failed: {self.error}")
        return self.reply


class ServerRun(Run):
    def __init__(self, args):
        super().__init__(args)
        self.measurements = {"screenshots": {}}
        self.work = args.work
        self.base = f"http://127.0.0.1:{args.server_port}"
        self.server = None
        self.token = None
        self.commands = 0

    # ---- server, CLI and hooks ----

    def env(self):
        env = dict(os.environ)
        env.update({"HOME": os.path.join(self.work, "home"), "DEVOTCHI_CONFIG_DIR": os.path.join(self.work, "cfg"),
                    "DEVOTCHI_HOST": "0.0.0.0", "DEVOTCHI_PORT": str(self.args.server_port),
                    "DEVOTCHI_DB_PATH": os.path.join(self.work, "db.sqlite"), "DEVOTCHI_SERVER_URL": self.base})
        return env

    def cli(self, *arguments, expect=0):
        done = subprocess.run(["node", CLI_JS, *arguments], env=self.env(), capture_output=True, text=True, timeout=60)
        check(done.returncode == expect, f"devotchi {' '.join(arguments)} exited {done.returncode}: {done.stderr[-300:]}")
        return done.stdout

    def api(self, method, path, body=None, timeout=10, headers=None, expect=(200, 202, 204)):
        connection = http.client.HTTPConnection("127.0.0.1", self.args.server_port, timeout=timeout)
        try:
            all_headers = {"Content-Type": "application/json", **(headers or {})}
            if self.token:
                all_headers["Authorization"] = f"Bearer {self.token}"
            connection.request(method, path, None if body is None else json.dumps(body), all_headers)
            reply = connection.getresponse()
            text = reply.read().decode()
            check(reply.status in expect, f"{method} {path} answered {reply.status}: {text[:200]}")
            return json.loads(text) if text else None
        finally:
            connection.close()

    def room(self):
        return self.api("GET", "/api/room")

    def wait_last_request(self, outcome, detail, timeout=5):
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            last = self.room()["lastRequest"]
            if last and last.get("outcome") == outcome and last.get("detail") == detail:
                return last
            time.sleep(0.2)
        raise StepFailed(f"lastRequest is {last}, expected {outcome}/{detail}")

    def permission_event(self, command=None, session=SESSION):
        self.commands += 1
        return {"hook_event_name": "PermissionRequest", "session_id": session, "cwd": "/tmp/devotchi",
                "tool_name": "Bash",
                "tool_input": {"command": command or f"echo step-{self.commands}", "description": "End-to-end test"}}

    def held_hook(self, command=None, session=SESSION):
        """Posts a PermissionRequest and waits for its card; returns the hook, its event and the screen."""
        event = self.permission_event(command, session)
        hook = Hook(self, event)
        screen = self.wait_screen(lambda s: s["id"] == "approval" and s.get("request"), 5, "approval card")
        return hook, event, screen

    def wait_registered(self, timeout=40):
        return self.board.wait_state(is_registered, timeout, "registration with the test server")

    # ---- steps ----

    def step_server(self):
        check(os.path.exists(SERVER_JS) and os.path.exists(CLI_JS), "packages are not built (pnpm run build)")
        check(not os.path.exists(self.work), f"{self.work} exists; remove it or pass --work")
        for name in ("home", "cfg"):
            os.makedirs(os.path.join(self.work, name))
        log = open(os.path.join(self.args.out, "server.log"), "w", encoding="utf-8")
        self.server = subprocess.Popen(["node", SERVER_JS], env=self.env(), stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 10
        health = None
        while time.monotonic() < deadline and health is None:
            try:
                health = self.api("GET", "/health", timeout=2)
            except OSError:
                time.sleep(0.2)
        check(health is not None and health.get("proto") == 2, f"/health: {health}")
        return f"server pid {self.server.pid} on port {self.args.server_port}: {health}"

    def step_pair_flow_b(self):
        code = json.loads(self.cli("pair", "--new", "--json"))["code"]
        with open(os.path.join(self.work, "cfg", "config.json"), encoding="utf-8") as f:
            self.token = json.load(f)["servers"][self.base]["token"]
        self.url = f"ws://{self.url.split('//')[1].split(':')[0]}:{self.args.server_port}"
        self.cmd(f"server {self.url}")
        self.cmd(f"pair {code}", timeout=30)
        self.wait_registered()
        self.wait_pet(10)
        return f"claimed the code from `devotchi pair --new` on {self.url}"

    def step_devices(self):
        deadline = time.monotonic() + 10
        while True:
            devices = self.api("GET", "/api/devices")["devices"]
            if devices and devices[0].get("telemetry") or time.monotonic() > deadline:
                break
            time.sleep(0.5)
        check(len(devices) == 1 and devices[0]["id"] == self.device_id, f"devices: {devices}")
        device = devices[0]
        check(device["online"] is True and device.get("telemetry"), f"device: {device}")
        status = json.loads(self.cli("status", "--json"))
        check(status["peers"]["devices"] == 1 and status["devices"][0]["id"] == self.device_id, f"status: {status}")
        self.measurements["device"] = device
        return f"{device['id']} online, fw {device.get('fw')}, telemetry {device['telemetry']}"

    def step_allow(self):
        hook, _, screen = self.held_hook()
        check(self.room()["pending"][0]["holders"] == [self.device_id], f"holders: {self.room()['pending']}")
        self.screenshot("server-approval")
        self.press("approve")
        reply = hook.result(5)
        check(reply == ALLOW, f"hook reply {reply}")
        last = self.wait_last_request("allow", "answered")
        check(last.get("by") == self.device_id, f"lastRequest: {last}")
        self.wait_pet()
        return f"card {screen['request']['id'][:8]}.. -> approve -> {reply['hookSpecificOutput']['decision']}; {last}"

    def step_deny(self):
        hook, _, _ = self.held_hook()
        self.press("reject")
        reply = hook.result(5)
        decision = (reply.get("hookSpecificOutput") or {}).get("decision") or {}
        check(decision.get("behavior") == "deny", f"hook reply {reply}")
        last = self.wait_last_request("deny", "answered")
        check(last.get("by") == self.device_id, f"lastRequest: {last}")
        self.wait_pet()
        return f"reject -> {decision}; {last}"

    def step_high_risk(self):
        hook, _, screen = self.held_hook("rm -rf /tmp/devotchi-e2e-scratch")
        check(screen["request"]["risk"] == "high" and "HOLD TO APPROVE" in screen["texts"], f"card: {screen}")
        bmark = self.board.mark()
        self.press("approve")
        self.wait_toast("Hold to approve", bmark)
        time.sleep(1.5)
        check(hook.is_alive(), f"a short press answered a high-risk request: {hook.reply}")
        self.press("approve", "long")
        reply = hook.result(5)
        check(reply == ALLOW, f"hook reply {reply}")
        self.wait_pet()
        return "risk high: short press -> 'Hold to approve' and the hook stayed held; long press -> allow"

    def step_defer(self):
        hook, _, _ = self.held_hook()
        self.press("action", "long")
        reply = hook.result(5)
        check(reply == {}, f"hook reply {reply}")
        last = self.wait_last_request("neutral", "deferred")
        self.wait_pet()
        return f"action long -> {{}}; {last}"

    def step_hold_timeout(self):
        self.api("PUT", "/api/room/settings", {"holdMs": 10000})
        try:
            bmark = self.board.mark()
            hook, _, _ = self.held_hook()
            reply = hook.result(15)
            check(reply == {}, f"hook reply {reply}")
            check(9 <= hook.seconds <= 12, f"the hook was held {hook.seconds:.1f} s with holdMs 10000")
            self.wait_toast("Timed out - use terminal", bmark)
            last = self.wait_last_request("neutral", "timeout")
        finally:
            self.api("PUT", "/api/room/settings", {"holdMs": 30000})
        self.wait_pet()
        return f"{{}} after {hook.seconds:.1f} s with the timeout toast; {last}"

    def step_three_concurrent(self):
        hooks = [Hook(self, self.permission_event()) for _ in range(3)]
        screen = self.wait_screen(lambda s: s["id"] == "approval" and (s.get("request") or {}).get("count") == 3, 5,
                                  "three queued requests")
        check("1/3" in screen["texts"], f"position: {screen['texts']}")
        time.sleep(3.5)
        pending = self.room()["pending"]
        check(len(pending) == 3 and all(p["holders"] == [self.device_id] for p in pending),
              f"not all three are held after the ack time: {pending}")
        check(all(h.is_alive() for h in hooks), "a hook was answered before any button press (no_ack?)")
        for button in ("approve", "reject", "approve"):
            self.press(button)
            time.sleep(0.8)
        replies = [h.result(5) for h in hooks]
        behaviors = sorted(((r.get("hookSpecificOutput") or {}).get("decision") or {}).get("behavior", "none")
                           for r in replies)
        check(behaviors == ["allow", "allow", "deny"], f"replies: {replies}")
        self.wait_pet()
        return "three hooks held by the device (card 1/3), answered approve, reject, approve -> 2 allow, 1 deny"

    def step_no_consumer(self):
        host = self.url.split("//")[1].split(":")[0]
        self.cmd(f"server ws://{host}:1")
        deadline = time.monotonic() + 15
        while self.room()["peers"]["devices"] != 0:
            check(time.monotonic() < deadline, "the server still counts the device after it left")
            time.sleep(0.3)
        hook = Hook(self, self.permission_event())
        reply = hook.result(3)
        check(reply == {} and hook.seconds < 1, f"reply {reply} after {hook.seconds:.2f} s")
        last = self.wait_last_request("neutral", "no_consumer")
        self.cmd(f"server {self.url}")
        self.wait_registered()
        return f"device pointed at port 1: {{}} after {hook.seconds * 1000:.0f} ms; {last}"

    def step_holder_lost(self):
        hook, _, screen = self.held_hook()
        rid = screen["request"]["id"]
        self.cmd("reboot")
        rebooted = time.monotonic()
        reply = hook.result(HOLDER_LOST_MAX_S + 5)
        took = time.monotonic() - rebooted
        check(reply == {}, f"hook reply {reply}")
        check(took <= HOLDER_LOST_MAX_S, f"the hook returned {took:.1f} s after the reboot")
        last = self.wait_last_request("neutral", "holder_lost")
        time.sleep(1)
        self.board.reopen()
        self.board.wait_ready(40)
        mark = self.board.mark()
        self.wait_registered(60)
        time.sleep(5)
        self.board.drain()
        replayed = [e for _, e in self.board.events(mark) if e.get("evt") == "rx" and e.get("type") == "permission"]
        screen = self.screen()
        check(not replayed and screen["id"] == "pet" and not screen.get("request"),
              f"request {rid} came back after the reboot: {replayed} {screen['id']}")
        check(self.room()["pending"] == [], "the server still lists a pending request")
        return f"reboot mid-request: {{}} after {took:.1f} s; {last}; nothing replayed after re-registration"

    def step_hook_aborted(self):
        body = json.dumps(self.permission_event()).encode()
        request = (f"POST /hook/event HTTP/1.1\r\nHost: 127.0.0.1:{self.args.server_port}\r\n"
                   f"Authorization: Bearer {self.token}\r\nContent-Type: application/json\r\n"
                   f"Content-Length: {len(body)}\r\n\r\n").encode() + body
        with socket.create_connection(("127.0.0.1", self.args.server_port), timeout=5) as sock:
            sock.sendall(request)
            screen = self.wait_screen(lambda s: s["id"] == "approval" and s.get("request"), 5, "approval card")
            rid = screen["request"]["id"]
            bmark = self.board.mark()
        self.wait_board(lambda e: e.get("evt") == "request" and e.get("op") == "remove" and e.get("id") == rid, 5,
                        bmark, "request removed after the hook client went away")
        last = self.wait_last_request("neutral", "cancelled")
        self.wait_pet()
        return f"hook connection closed -> dismissed on the device; toasts {self.toasts(bmark)}; {last}"

    def cancelled_by(self, event_for):
        hook, event, screen = self.held_hook()
        rid = screen["request"]["id"]
        bmark = self.board.mark()
        check(self.api("POST", "/hook/event", event_for(event)) == {}, "the cancelling hook did not answer {}")
        reply = hook.result(5)
        check(reply == {}, f"hook reply {reply}")
        self.wait_board(lambda e: e.get("evt") == "request" and e.get("op") == "remove" and e.get("id") == rid, 5,
                        bmark, "request removed")
        last = self.wait_last_request("neutral", "cancelled")
        self.wait_pet()
        return last

    def step_cancelling_events(self):
        tool_done = self.cancelled_by(lambda e: {**e, "hook_event_name": "PostToolUse", "tool_response": {}})
        time.sleep(0.5)
        stop = self.cancelled_by(lambda e: {"hook_event_name": "Stop", "session_id": e["session_id"],
                                            "cwd": e["cwd"]})
        return f"PostToolUse with the same tool and input -> {tool_done}; Stop -> {stop}"

    def step_notify(self):
        bmark = self.board.mark()
        self.cli("notify", "tests_failed", "3 of 47 failing", "--urgent")
        cli_toast = self.wait_board(lambda e: e.get("evt") == "toast" and "3 of 47 failing" in e.get("text", ""), 5,
                                    bmark, "toast for devotchi notify")
        time.sleep(3)
        bmark = self.board.mark()
        reply = self.api("POST", "/api/notify", {"event": "deploy_complete", "detail": "v1.2 is live", "source": "ci"})
        check(reply.get("delivered") == 1, f"/api/notify: {reply}")
        rest_toast = self.wait_board(lambda e: e.get("evt") == "toast" and "v1.2 is live" in e.get("text", ""), 5,
                                     bmark, "toast for POST /api/notify")
        return f"devotchi notify -> toast '{cli_toast['text']}'; POST /api/notify -> toast '{rest_toast['text']}'"

    def step_status_keeps_mood(self):
        self.api("POST", "/api/notify", {"event": "tests_passed", "mood": "happy", "urgency": 0})
        self.wait_screen(lambda s: s.get("mood") == "happy", 5, "mood happy")
        mark = self.board.mark()
        self.api("POST", "/hook/event", {"hook_event_name": "PostToolUse", "session_id": SESSION,
                                         "cwd": "/tmp/devotchi", "tool_name": "Read",
                                         "tool_input": {"file_path": "/tmp/devotchi/src/app.c"}, "tool_response": {}})
        self.wait_board(lambda e: e.get("evt") == "rx" and e.get("type") == "status", 5, mark, "status frame")
        time.sleep(0.5)
        screen = self.screen()
        check(screen["id"] == "pet" and screen["mood"] == "happy", f"mood after the status: {screen.get('mood')}")
        return f"status from a PostToolUse hook (no mood) kept the pet happy; screen texts {screen['texts']}"

    def step_identify(self):
        self.cli("devices", "identify", self.device_id)
        screen = self.wait_screen(lambda s: s["id"] == "identify", 5, "identify screen")
        self.wait_pet(12)
        return f"devotchi devices identify -> identify screen {screen['texts']} -> pet"

    def step_flow_a(self):
        self.cmd("reset-pairing")
        screen = self.wait_screen(lambda s: s["id"] == "pairing" and shown_code(s), 20, "pairing code on screen")
        self.cli("pair", shown_code(screen))
        self.wait_registered(30)
        devices = self.api("GET", "/api/devices")["devices"]
        check(any(d["id"] == self.device_id and d["online"] for d in devices), f"devices after flow A: {devices}")
        return f"reset-pairing -> code on screen -> devotchi pair -> registered; {len(devices)} device row(s)"

    def step_revoke(self):
        bmark = self.board.mark()
        self.cli("devices", "rm", self.device_id, "--yes")
        state = self.board.wait_state(lambda s: s["app"] == "pairing" and not s["paired"], 15, "pairing after rm")
        screen = self.wait_screen(lambda s: s["id"] == "pairing", 10, "pairing screen")
        self.board.drain()
        closes = [text for _, text in self.board.lines[bmark:] if "close 4003" in text]
        check(closes, "the board log shows no close 4003")
        check(state["wifiConfigured"] and state["server"] == self.url, "revocation touched WiFi or the server URL")
        return f"devotchi devices rm -> close 4003 -> pairing screen {screen['texts']}"

    def step_stop_server(self):
        if self.server is not None:
            self.server.send_signal(signal.SIGTERM)
            try:
                self.server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.server.kill()
                self.server.wait()
            self.server = None
            shutil.rmtree(self.work, ignore_errors=True)
        return f"server stopped, {self.work} removed"

    def run(self):
        steps = [
            ("devices", self.step_devices),
            ("allow", self.step_allow),
            ("deny", self.step_deny),
            ("high-risk", self.step_high_risk),
            ("defer", self.step_defer),
            ("hold-timeout", self.step_hold_timeout),
            ("three-concurrent", self.step_three_concurrent),
            ("no-consumer", self.step_no_consumer),
            ("holder-lost", self.step_holder_lost),
            ("hook-aborted", self.step_hook_aborted),
            ("cancelling-events", self.step_cancelling_events),
            ("notify", self.step_notify),
            ("status-keeps-mood", self.step_status_keeps_mood),
            ("identify", self.step_identify),
            ("flow-a", self.step_flow_a),
            ("revoke", self.step_revoke),
        ]
        selected = self.args.steps.split(",") if self.args.steps else None
        ok = True
        try:
            self.board.open()
            self.board.wait_ready()
            ok = (self.run_step("record", self.step_record) and self.run_step("server", self.step_server)
                  and self.run_step("pair-flow-b", self.step_pair_flow_b))
            for name, function in steps if ok else []:
                if selected is not None and name not in selected:
                    continue
                try:
                    ok = self.run_step(name, function) and ok
                except (KeyError, IndexError, TypeError, OSError, ValueError) as e:
                    ok = False
                    print(f"FAIL {name}: {type(e).__name__}: {e}", flush=True)
        finally:
            ok = self.run_step("restore", self.step_restore) and ok
            ok = self.run_step("stop-server", self.step_stop_server) and ok
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
    parser.add_argument("--server-port", type=int, default=18080, help="TCP port for the temporary server")
    parser.add_argument("--host", help="address of this machine as the board sees it (default: derived)")
    parser.add_argument("--work", default="/tmp/dv-e2e", help="directory for the server's database, HOME and config; "
                        "must not exist, removed at the end")
    parser.add_argument("--out", default="/tmp/devotchi-e2e-server", help="directory for logs and result.json")
    parser.add_argument("--steps", help="comma-separated subset of steps (pairing and restore always run)")
    args = parser.parse_args(argv)
    args.mock_port = args.server_port
    if args.server_port in FORBIDDEN_PORTS:
        parser.error(f"port {args.server_port} belongs to the real server and dashboard")
    os.makedirs(args.out, exist_ok=True)
    return ServerRun(args).run()


if __name__ == "__main__":
    sys.exit(main())
