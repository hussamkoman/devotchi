#!/usr/bin/env python3
"""End-to-end test of the network link on a real board against mock_server.py.

Covers pairing by claim (flow B) and by code on screen (flow A), registration,
message reception including fragmented frames, reconnecting, the app task
staying responsive while the WebSocket client stops, revocation by close code
4003, the HTTP 401 rule and a v1-style server.

The board's pairing and server URL are backed up first (`pair-backup`) and put
back at the end (`pair-restore`), also when a step fails. WiFi credentials are
never changed: the WiFi step tries a wrong passphrase, which the firmware
drops again by itself.

Run with the ESP-IDF Python:
  ~/.espressif/tools/python/v6.0/venv/bin/python firmware/tools/e2e_link.py
"""

import argparse
import json
import os
import re
import signal
import socket
import ssl
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from devotchi_serial import DEFAULT_PORT, ConsoleError, ConsoleTimeout, DevotchiSerial, save_png  # noqa: E402

MOCK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mock_server.py")
FORBIDDEN_PORTS = (8080, 3456)
CLAIM_CODE = "K7M2QX"
REQUEST_ID = "9f2c4e6a8b0d1f3a5c7e9b1d3f5a7c9e"
# A short ping interval and silence limit so that the mock notices a dead socket within the test's patience.
MOCK_OPTIONS = ("--ping", "5", "--dead-after", "12", "--code", CLAIM_CODE)
TRIAL_BRIGHTNESS = 25
# TEST-NET-1 (RFC 5737): never routed, so a connect to it gets no answer.
BLACK_HOLE = "192.0.2.1"


class StepFailed(Exception):
    pass


def check(condition, message):
    if not condition:
        raise StepFailed(message)


class Board(DevotchiSerial):
    """Console connection that keeps every line it reads, with the time it was read."""

    def __init__(self, port):
        super().__init__(port)
        self.lines = []

    def read_line(self, deadline):
        text = super().read_line(deadline)
        if text is not None:
            self.lines.append((time.monotonic(), text))
        return text

    def drain(self):
        # The base class discards pending input; here it belongs to the transcript.
        while self.read_line(time.monotonic() + 0.05) is not None:
            pass

    def mark(self):
        self.drain()
        return len(self.lines)

    def events(self, since=0):
        out = []
        for stamp, text in self.lines[since:]:
            if text.startswith("@@EVT "):
                try:
                    out.append((stamp, json.loads(text[6:])))
                except ValueError:
                    pass
        return out

    def wait_event(self, predicate, timeout, since):
        """Returns the first event after `since` (a mark) that satisfies predicate."""
        deadline = time.monotonic() + timeout
        seen = since
        while True:
            for index in range(seen, len(self.lines)):
                stamp, text = self.lines[index]
                if text.startswith("@@EVT "):
                    try:
                        event = json.loads(text[6:])
                    except ValueError:
                        continue
                    if predicate(event):
                        return stamp, event
            seen = len(self.lines)
            if self.read_line(deadline) is None:
                raise ConsoleTimeout(f"expected event did not arrive within {timeout:g} s")

    def state(self):
        return self.json_command("state")

    def wait_state(self, predicate, timeout, what):
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            last = self.state()
            if predicate(last):
                return last
            time.sleep(0.5)
        raise StepFailed(f"{what}: not reached within {timeout:g} s; app={last.get('app')} link={last.get('link')}")

    def texts(self):
        return self.json_command("screen").get("texts", [])


class Mock:
    """mock_server.py as a child process; its stdout lines are collected as events."""

    def __init__(self, port, log_path):
        self.port = port
        self.log_path = log_path
        self.proc = None
        self.tls = False
        self.events = []
        self._lock = threading.Lock()

    def start(self, *options):
        check(self.proc is None, "mock server already running")
        self.tls = "--tls" in options
        mark = self.mark()
        self.proc = subprocess.Popen(
            [sys.executable, "-I", MOCK, "--host", "0.0.0.0", "--port", str(self.port), *options],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        threading.Thread(target=self._reader, args=(self.proc,), daemon=True).start()
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if self.since(mark, "listening"):
                return
            check(self.proc.poll() is None, f"mock server exited: see {self.log_path}")
            time.sleep(0.05)
        raise StepFailed("mock server did not start listening")

    def _reader(self, proc):
        with open(self.log_path, "a", encoding="utf-8") as log_file:
            for line in proc.stdout:
                log_file.write(line)
                log_file.flush()
                try:
                    event = json.loads(line)
                except ValueError:
                    continue
                with self._lock:
                    self.events.append(event)

    def stop(self):
        if self.proc is None:
            return
        self.proc.send_signal(signal.SIGTERM)
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self.proc = None

    def snapshot(self):
        with self._lock:
            return list(self.events)

    def mark(self):
        with self._lock:
            return len(self.events)

    def since(self, mark, kind=None):
        return [e for e in self.snapshot()[mark:] if kind is None or e.get("ev") == kind]

    def ctl(self, command):
        scheme = "https" if self.tls else "http"
        request = urllib.request.Request(f"{scheme}://127.0.0.1:{self.port}/_ctl", data=command.encode(), method="POST")
        # The control channel is this script talking to its own child over loopback with a throwaway certificate.
        context = ssl._create_unverified_context() if self.tls else None
        try:
            with urllib.request.urlopen(request, timeout=5, context=context) as reply:
                return json.load(reply)
        except urllib.error.HTTPError as e:
            raise StepFailed(f"mock control '{command.split()[0]}' failed: {e.read().decode()}")
        except OSError as e:
            raise StepFailed(f"mock control '{command.split()[0]}': {e}")

    def wait_for(self, predicate, timeout, what, mark=0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for event in self.snapshot()[mark:]:
                if predicate(event):
                    return event
            time.sleep(0.1)
        raise StepFailed(f"mock server: {what} not seen within {timeout:g} s")


def host_address_for(board_ip):
    """The address of this machine on the network the board is on."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect((board_ip, 9))
        return probe.getsockname()[0]
    finally:
        probe.close()


def is_registered(state):
    return state["app"] == "online" and state["link"]["ws"] == "registered"


class Run:
    def __init__(self, args):
        self.args = args
        self.board = Board(args.port)
        self.mock = Mock(args.mock_port, os.path.join(args.out, "mock.log"))
        self.results = []
        self.measurements = {}
        self.original = None
        self.host = None
        self.url = None
        self.secrets = set()

    # ---- helpers ----

    def cmd(self, line, timeout=10.0):
        return self.board.command(line, timeout)

    def wait_registered(self, timeout, what):
        return self.board.wait_state(is_registered, timeout, what)

    def send_and_expect(self, control, payload_text, expected_type):
        """Sends a frame through the mock and returns the board's rx event for it."""
        mark = self.board.mark()
        reply = self.mock.ctl(f"{control} {payload_text}" if control == "send" else control)
        check(reply.get("sent") == 1, f"mock sent the frame to {reply.get('sent')} sockets, expected 1")
        _, event = self.board.wait_event(lambda e: e.get("evt") == "rx" and e.get("type") == expected_type, 10, mark)
        return event

    def remember_tokens(self):
        self.secrets.update(self.mock.ctl("tokens").get("tokens", []))

    def screenshot(self, name):
        width, height, raw = self.board.screenshot()
        path = os.path.join(self.args.out, f"{name}.png")
        save_png(path, width, height, raw)
        return path

    # ---- steps ----

    def step_record(self):
        state = self.board.wait_state(lambda s: s["link"]["wifi"] == "connected", 30, "board on WiFi")
        self.original = {key: state[key] for key in ("server", "serverSource", "paired", "wifiConfigured")}
        self.original["ssid"] = state["link"]["ssid"]
        self.device_id = state["deviceId"]
        self.host = self.args.host or host_address_for(state["link"]["ip"])
        self.url = f"ws://{self.host}:{self.args.mock_port}"
        self.measurements["original"] = self.original
        self.cmd("pair-backup")
        check(self.board.state()["hasBackup"] or not self.original["paired"], "pair-backup left no backup")
        return f"server={self.original['server']} source={self.original['serverSource']} paired={self.original['paired']}"

    def step_claim(self):
        self.mock.start(*MOCK_OPTIONS)
        self.cmd(f"server {self.url}")
        try:
            self.cmd(f"pair {CLAIM_CODE.lower()}", timeout=30)
        except ConsoleError as e:
            if "unreachable" in e.message.lower():
                raise StepFailed(f"the board cannot reach {self.url}: {e.message}. If a firewall blocks the port, "
                                 "open it or pass --mock-port; this test does not change system settings")
            raise
        state = self.wait_registered(20, "registration after claim")
        check(state["paired"] and state["serverSource"] == "nvs", "claim did not store the pairing and server URL")

        upgrade = self.mock.wait_for(lambda e: e.get("ev") == "upgrade" and e.get("status") == 101, 5, "upgrade")
        check(upgrade["path"] == "/ws", f"upgrade path is {upgrade['path']}, expected /ws")
        check(upgrade["auth"] == "bearer", "upgrade carried no Authorization: Bearer header")
        for event in self.mock.since(0, "upgrade"):
            check(not event["tokenInQuery"] and not event["query"], "the device put a query string in its URL")
        register = self.mock.wait_for(lambda e: e.get("ev") == "rx" and isinstance(e.get("frame"), dict)
                                      and e["frame"].get("type") == "register", 5, "register frame")["frame"]
        client = register.get("client", {})
        check(register.get("proto") == 2 and register.get("role") == "consumer", f"bad register: {register}")
        check(client.get("kind") == "device" and client.get("deviceId") == self.device_id, f"bad client: {client}")
        check(isinstance(client.get("version"), str) and client["version"], "register has no client.version")
        check("fw" not in client, "register carries the removed field client.fw")
        claim = self.mock.wait_for(lambda e: e.get("ev") == "http" and e.get("path") == "/api/pair/claim", 5, "claim")
        check(claim["body"].get("deviceId") == self.device_id and claim["body"].get("fw") == client["version"],
              f"claim body: {claim['body']}")
        self.remember_tokens()
        return f"registered as {client['deviceId']} version {client['version']} with a Bearer header on /ws"

    def step_messages(self):
        frames = [
            ("status", {"type": "status", "msg": "Edit src/app.c", "mood": "working", "sid": "3fa9c1"}),
            ("status", {"type": "status", "msg": "ping"}),
            ("notify", {"type": "notify", "event": "tests_failed", "mood": "sad", "detail": "3 of 47 failing",
                        "urgency": 2}),
            ("mood", {"type": "mood", "mood": "anxious", "reason": "Long build running"}),
            ("unknown", {"type": "hologram", "data": [1, 2, 3]}),
        ]
        for kind, frame in frames:
            text = json.dumps(frame, separators=(",", ":"))
            event = self.send_and_expect("send", text, kind)
            check(event["crc32"] == f"{zlib.crc32(text.encode()):08x}", f"{kind}: content differs")
        self.measurements["heapOnline"] = self.board.state()["heap"]
        mark = self.mock.mark()
        before = self.board.mark()
        self.board.wait_event(lambda e: e.get("evt") == "rx" and e.get("type") == "ping", 12, before)
        self.mock.wait_for(lambda e: e.get("ev") == "rx" and e.get("frame") == {"type": "pong"}, 5, "pong", mark)
        return "status, status with the text ping, notify, mood and an unknown type parsed; ping answered with pong"

    def step_large_frames(self):
        details = ("0123456789abcdef" * 200)[:1900]
        frame = {"type": "permission", "id": REQUEST_ID, "tool": "Bash", "summary": "run the build", "risk": "low",
                 "timeout": 30000, "details": details}
        text = json.dumps(frame, separators=(",", ":"))
        pad = 1900 - len(text.encode())
        frame["details"] = details[:len(details) + pad]
        text = json.dumps(frame, separators=(",", ":"))
        size = len(text.encode())
        check(size == 1900, f"test frame is {size} bytes")
        expected = f"{zlib.crc32(text.encode()):08x}"

        results = {}
        for name, control in (("single", "send"), ("fragmented", f"send-fragmented {text} 5"),
                              ("fragmented+ping", f"send-fragmented-ping {text} 5")):
            event = self.send_and_expect(control, text, "permission")
            results[name] = event
            check(event["bytes"] == size and event["crc32"] == expected,
                  f"{name}: board parsed {event['bytes']} bytes crc {event['crc32']}, sent {size} bytes crc {expected}")
        link = self.board.state()["link"]
        check(link["rxDropped"] == 0 and link["rxInvalid"] == 0,
              f"rxDropped={link['rxDropped']} rxInvalid={link['rxInvalid']}")
        return f"{size}-byte permission identical in one frame, 5 fragments, and 5 fragments with pings (crc {expected})"

    def press_while_stopping(self, label):
        """Measures a button event while the client is stopping; None when it was not stopping any more."""
        self.board.wait_state(lambda s: s["link"]["wsPhase"] == "stopping", 5, f"{label}: wsPhase stopping")
        mark = self.board.mark()
        sent = time.monotonic()
        self.cmd("btn action press")
        stamp, _ = self.board.wait_event(lambda e: e.get("evt") == "btn" and e.get("id") == "action", 2, mark)
        still = self.board.state()["link"]["wsPhase"]
        if still != "stopping":
            return None
        return (stamp - sent) * 1000

    def step_responsive_while_stopping(self):
        # A server that accepts the TCP connection and never answers the upgrade.
        self.mock.ctl("stall")
        self.mock.ctl("drop")
        self.board.wait_state(lambda s: s["link"]["ws"] == "connecting" and s["link"]["wsPhase"] == "running", 15,
                              "connect attempt against the stalled server")
        time.sleep(0.5)
        self.cmd(f"server {self.url}")
        stalled_ms = self.press_while_stopping("stalled server")
        check(stalled_ms is not None, "the client stopped before the button could be pressed")
        check(stalled_ms < 200, f"button event took {stalled_ms:.0f} ms while the WebSocket was stopping")
        self.measurements["btnWhileStoppingStalledMs"] = round(stalled_ms)
        self.mock.ctl("unstall")

        # The same against an address nothing answers from.
        self.cmd(f"server ws://{BLACK_HOLE}:{self.args.mock_port}")
        self.board.wait_state(lambda s: s["link"]["wsPhase"] == "running" and s["screen"] == "connecting", 20,
                              "connect attempt to the black hole")
        self.measurements["screenshots"]["connecting"] = self.screenshot("connecting")
        self.cmd(f"server {self.url}")
        try:
            hole_ms = self.press_while_stopping("black hole")
        except StepFailed:
            hole_ms = None
        self.measurements["btnWhileStoppingBlackHoleMs"] = None if hole_ms is None else round(hole_ms)
        if hole_ms is not None:
            check(hole_ms < 200, f"button event took {hole_ms:.0f} ms while stopping a connect to {BLACK_HOLE}")
        self.wait_registered(40, "registration after the stalled attempts")
        hole = "not observed (the network refused the address at once)" if hole_ms is None else f"{hole_ms:.0f} ms"
        return f"btn event {stalled_ms:.0f} ms after the command with wsPhase=stopping (stalled server); black hole: {hole}"

    def step_server_restart(self):
        mark = self.board.mark()
        self.mock.stop()
        self.board.wait_state(lambda s: s["app"] == "offline", 15, "offline after the server died")
        # Four attempts with the delay doubling from 1 s take about 15 s.
        time.sleep(18)
        self.board.drain()
        delays = [int(m.group(1)) for _, text in self.board.lines[mark:]
                  for m in [re.search(r"retry in (\d+) ms", text)] if m]
        check(len(delays) >= 4, f"expected at least 4 retries in 18 s, saw delays {delays}")
        for previous, current in zip(delays, delays[1:]):
            check(current > previous * 1.2 or current >= 48000, f"retry delays do not back off: {delays}")
        check(800 <= delays[0] <= 1200, f"first retry delay {delays[0]} ms is not 1 s +/- 20 %")
        state = self.board.state()
        check(state["paired"], "the pairing was lost while the server was down")
        texts = " | ".join(self.board.texts())
        self.mock.start(*MOCK_OPTIONS)
        self.wait_registered(70, "registration after the server came back")
        self.measurements["retryDelaysMs"] = delays
        return f"retry delays {delays} ms, pairing kept, registered again after restart (screen while down: {texts})"

    def step_wifi_reconnect(self):
        mark = self.board.mark()
        mock_mark = self.mock.mark()
        # Measured on USB power without a battery: with the backlight at 75 % the supply does not carry the
        # radio through repeated failed handshakes and the reconnect after them, and the chip resets with a
        # brownout. The backlight stays dim until the board is registered again.
        brightness = self.board.state()["settings"]["brightness"]
        self.cmd(f"set brightness {TRIAL_BRIGHTNESS}")
        try:
            try:
                self.cmd(f'wifi "{self.original["ssid"]}" "wrong-passphrase-e2e"', timeout=40)
                raise StepFailed("the board accepted a wrong WiFi passphrase")
            except ConsoleError as e:
                check("previous credentials restored" in e.message,
                      f"unexpected answer to the WiFi trial: {e.message}")
            self.wait_registered(60, "registration after the WiFi reconnect")
        finally:
            self.cmd(f"set brightness {brightness}")
        state = self.board.state()
        check(state["wifiConfigured"] and state["link"]["ssid"] == self.original["ssid"],
              "WiFi credentials changed by a failed trial")
        events = [e for _, e in self.board.events(mark) if e.get("evt") == "link"]
        check(any(e["wifi"] != "connected" for e in events), "WiFi never went down during the trial")
        # The socket from before the drop cannot say goodbye; the mock reaps it after its silence limit.
        deadline = time.monotonic() + 20
        while self.mock.ctl("status")["connections"] != 1 and time.monotonic() < deadline:
            time.sleep(1)
        check(self.mock.ctl("status")["connections"] == 1, "more than one WebSocket is open after the reconnect")
        open_now = set()
        opened = 0
        for event in self.mock.since(mock_mark):
            if event.get("ev") == "upgrade" and event.get("status") == 101:
                opened += 1
                open_now.add(event["conn"])
                check(len(open_now) <= 1, "a second WebSocket was opened while the first new one was still open")
            elif event.get("ev") == "closed":
                open_now.discard(event["conn"])
        check(opened >= 1, "no new WebSocket after the WiFi reconnect")
        return f"WiFi dropped and returned with the stored credentials; {opened} new socket, one open"

    def step_revocation(self):
        self.mock.ctl("confirm-after off")
        mark = self.board.mark()
        closed_at = time.monotonic()
        self.mock.ctl("close 4003")
        stamp, _ = self.board.wait_event(lambda e: e.get("evt") == "app" and e.get("state") == "pairing", 5, mark)
        state = self.board.wait_state(lambda s: s["screen"] == "pairing", 5, "pairing screen after 4003")
        check(not state["paired"], "the token survived close code 4003")
        check(state["serverSource"] == "nvs" and state["server"] == self.url, "4003 changed the server URL")
        check(state["wifiConfigured"], "4003 erased the WiFi credentials")
        code_event = self.mock.wait_for(lambda e: e.get("ev") == "pair_code", 15, "flow A start")
        code = code_event["code"]
        shown = f"{code[:3]} {code[3:]}"
        deadline = time.monotonic() + 10
        texts = []
        while time.monotonic() < deadline:
            texts = self.board.texts()
            if shown in texts:
                break
            time.sleep(0.5)
        check(shown in texts, f"code {shown} is not on screen: {texts}")
        check("Pairing revoked" in texts, f"'Pairing revoked' is not on screen: {texts}")
        check(any(code in t and "devotchi pair" in t for t in texts), f"no 'devotchi pair {code}' hint: {texts}")
        self.measurements["screenshots"]["pairing"] = self.screenshot("pairing")
        self.mock.ctl("confirm")
        state = self.wait_registered(20, "registration after flow A")
        check(state["paired"], "flow A did not store a pairing")
        self.remember_tokens()
        return f"4003 -> pairing {1000 * (stamp - closed_at):.0f} ms later, code {shown} shown, confirmed, registered"

    def step_rejected_token(self):
        self.mock.ctl("confirm-after 3")
        mock_mark = self.mock.mark()
        self.mock.ctl("reject-token")
        self.mock.ctl("drop")
        first = self.mock.wait_for(lambda e: e.get("ev") == "upgrade" and e.get("status") == 401, 20,
                                   "first 401", mock_mark)
        erased_at = None
        seen_message = False
        deadline = time.monotonic() + 150
        while time.monotonic() < deadline:
            state = self.board.state()
            rejected = [e for e in self.mock.since(mock_mark, "upgrade") if e.get("status") == 401]
            elapsed = time.time() - first["t"]
            if not state["paired"]:
                erased_at = elapsed
                check(len(rejected) >= 3, f"token erased after only {len(rejected)} rejected upgrades")
                check(rejected[-1]["t"] - rejected[0]["t"] >= 30,
                      f"token erased {rejected[-1]['t'] - rejected[0]['t']:.1f} s after the first 401")
                break
            if state["link"]["lastError"] == "Server rejected token, retrying":
                seen_message = True
            time.sleep(1)
        check(erased_at is not None, "the token was still there 150 s after the first 401")
        check(seen_message, "'Server rejected token, retrying' was never reported")
        health = len([e for e in self.mock.since(mock_mark, "http") if e.get("path") == "/health"])
        check(health >= len(rejected) - 1, f"{len(rejected)} rejections but only {health} health checks")
        state = self.board.wait_state(lambda s: s["app"] == "pairing", 5, "pairing after the token was given up")
        check(state["serverSource"] == "nvs", "the server URL was lost with the token")
        self.mock.ctl("accept-token")
        self.wait_registered(40, "registration after pairing again")
        self.remember_tokens()
        self.measurements["rejected401"] = {"count": len(rejected), "erasedAfterS": round(erased_at, 1)}
        return f"token kept through {len(rejected)} rejections over {erased_at:.0f} s, then pairing and flow A again"

    def step_v1_server(self):
        self.mock.stop()
        mock_mark = self.mock.mark()
        self.mock.start("--proto", "1")
        self.mock.ctl("reject-token")
        started = time.monotonic()
        self.board.wait_state(lambda s: s["link"]["lastError"] == "Incompatible server", 90,
                              "'Incompatible server' against a v1-style server")
        while time.monotonic() - started < 95:
            state = self.board.state()
            check(state["paired"], f"token erased {time.monotonic() - started:.0f} s into a v1-style server")
            check(state["link"]["rejected401"] == 0, "a 401 from a server without protocol 2 was counted")
            time.sleep(5)
        attempts = [e["t"] for e in self.mock.since(mock_mark, "upgrade") if e.get("status") == 401]
        gaps = [round(b - a, 1) for a, b in zip(attempts, attempts[1:])]
        check(len(attempts) >= 2, f"expected a second attempt within 95 s, saw {len(attempts)}")
        check(all(gap >= 58 for gap in gaps), f"retries against an incompatible server are too fast: {gaps}")
        self.measurements["v1RetryGapsS"] = gaps
        return f"paired stayed true for 95 s, 'Incompatible server' shown, {len(attempts)} attempts, gaps {gaps} s"

    def step_restore(self):
        self.mock.stop()
        if self.original is None:
            return "nothing was changed"
        if self.original["paired"]:
            self.cmd("pair-restore")
        else:
            self.cmd("reset-pairing")
        state = self.board.state()
        for key in ("server", "serverSource", "paired", "wifiConfigured"):
            check(state[key] == self.original[key], f"{key} is {state[key]!r}, was {self.original[key]!r} at the start")
        check(state["link"]["ssid"] == self.original["ssid"], "the WiFi network changed")
        return f"server={state['server']} source={state['serverSource']} paired={state['paired']}"

    def step_logs(self):
        self.board.drain()
        transcript = "\n".join(text for _, text in self.board.lines)
        with open(os.path.join(self.args.out, "board.log"), "w", encoding="utf-8") as f:
            f.write(transcript + "\n")
        check("Brownout" not in transcript and "ESP-ROM:" not in transcript, "the board reset during the test")
        check(self.secrets, "no test token was collected")
        for secret in self.secrets:
            check(secret not in transcript, "a token appears in the board's output")
        for word in ("Bearer", "Authorization", "wrong-passphrase-e2e"):
            check(word not in transcript, f"'{word}' appears in the board's output")
        # Every token is a UUID, including the owner's, which this test never learns.
        check(re.search(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", transcript) is None,
              "a UUID-shaped value appears in the board's output")

        phases = [e["wsPhase"] for _, e in self.board.events() if e.get("evt") == "link"]
        order = {"idle": "running", "running": "stopping", "stopping": "idle"}
        previous = None
        for phase in phases:
            check(previous is None or phase == previous or order.get(previous) == phase,
                  f"WebSocket lifecycle went {previous} -> {phase}")
            previous = phase
        return (f"{len(self.board.lines)} lines captured, {len(self.secrets)} tokens and the Authorization header "
                f"absent; lifecycle only idle -> running -> stopping -> idle over {len(phases)} link events")

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
        self.measurements["screenshots"] = {}
        steps = [
            ("record", self.step_record),
            ("claim", self.step_claim),
            ("messages", self.step_messages),
            ("large-frames", self.step_large_frames),
            ("responsive-while-stopping", self.step_responsive_while_stopping),
            ("server-restart", self.step_server_restart),
            ("wifi-reconnect", self.step_wifi_reconnect),
            ("revocation-4003", self.step_revocation),
            ("rejected-token-401", self.step_rejected_token),
            ("v1-server", self.step_v1_server),
        ]
        selected = self.args.steps.split(",") if self.args.steps else None
        ok = True
        try:
            self.board.open()
            self.board.wait_ready()
            for name, function in steps:
                if selected is not None and name not in selected and name not in ("record", "claim"):
                    continue
                if not self.run_step(name, function):
                    ok = False
                    break
        finally:
            ok = self.run_step("restore", self.step_restore) and ok
            ok = self.run_step("logs", self.step_logs) and ok
            self.mock.stop()
            self.board.close()
        with open(os.path.join(self.args.out, "result.json"), "w", encoding="utf-8") as f:
            json.dump({"ok": ok, "steps": self.results, "measurements": self.measurements}, f, indent=2)
        print(f"{'OK' if ok else 'FAILED'}: results in {self.args.out}", flush=True)
        return 0 if ok else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", default=DEFAULT_PORT, help="serial console of the board")
    parser.add_argument("--mock-port", type=int, default=18090, help="TCP port for mock_server.py (18090..18099)")
    parser.add_argument("--host", help="address of this machine as the board sees it (default: derived)")
    parser.add_argument("--out", default="/tmp/devotchi-e2e-link", help="directory for logs, screenshots, result.json")
    parser.add_argument("--steps", help="comma-separated subset of steps (record and claim always run)")
    args = parser.parse_args(argv)
    if args.mock_port in FORBIDDEN_PORTS:
        parser.error(f"port {args.mock_port} belongs to the real server and dashboard")
    os.makedirs(args.out, exist_ok=True)
    return Run(args).run()


if __name__ == "__main__":
    sys.exit(main())
