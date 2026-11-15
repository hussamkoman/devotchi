#!/usr/bin/env python3
"""Stand-in for the Devotchi server, for firmware tests without the TypeScript chain.

Python standard library only. Speaks the parts of docs/protocol.md the firmware
uses: GET /health, the pairing REST calls, and a WebSocket on /ws with a
hand-written RFC 6455 codec so that fragmentation, interleaved pings and close
codes can be produced on purpose. The register_ack lists the phase-2
capabilities of --caps (protocol 14.1); `decide`, `permission_response` and
`permission_defer` from the board are logged like every frame, and the test
that drives the mock answers them with `send` (dismiss with `choice`, error).

Every event is printed to stdout as one JSON object per line with a timestamp
("t", seconds since the epoch): received frames, upgrades, HTTP requests,
connections opening and closing.

Control, as lines on stdin or as the body of POST /_ctl:
  send <json>                      one text frame to every WebSocket
  send-fragmented <json> <n>       the same as a text frame (FIN=0) plus continuation frames, n frames in all
  send-fragmented-ping <json> <n>  like send-fragmented, with a ping frame between the fragments
  send-binary <hex>                one binary frame
  send-text <text>                 one text frame with any content (not checked to be JSON)
  send-continuation <text>         one continuation frame that no text frame started
  flood <count> <seconds>          count numbered status frames spread evenly over seconds
  close <code>                     close every WebSocket with this code (4003 also revokes its token)
  drop                             cut every WebSocket without a close frame
  confirm                          confirm every pending flow A code
  confirm-after <seconds|off>      confirm new flow A codes automatically after this delay
  expire                           expire every flow A code
  reject-token / accept-token      answer 401 to every upgrade / stop doing so
  stall / unstall                  accept connections but never answer / answer again
  caps <list|none>                 capabilities named in the next register_ack (comma-separated; default decide)
  send-burst <json array>          every frame of the array as its own text frame, written back to back in one go
  extend-reply <on|off>            answer `extend` with `deadline` 60000 (protocol 14.6; default on)
  pet-add <id> <hex>               serve this pack at GET /api/pets/<id>/pack (rev, size, CRC read from it)
  pet-cmd <op> <id> [select] [k=v] send pet_cmd for a served pack; k=v overrides a field (rev=2, crc32=..., size=..);
                                   the id "" (two quote characters) sends an empty id
  pet-mode <mode>                  how the pack route answers: normal, truncate (short body, then close),
                                   drop (half the body, then close), delay:<s> (100 bytes, wait, the rest),
                                   status:<code>, length:<n> (that Content-Length, the whole body)
  time [offset_min]                send `time` (protocol 14.9) with the offset, which also goes into later register_acks
  status                           counters and connection state as JSON
  tokens                           the tokens this server accepts (test tokens, for leak checks)
"""

import argparse
import base64
import hashlib
import json
import os
import secrets
import socket
import ssl
import struct
import sys
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

FORBIDDEN_PORTS = (8080, 3456)
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
PAIR_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"
DEFAULT_TOKEN = "0a1b2c3d-4e5f-4a6b-8c7d-9e0f1a2b3c4d"
ROOM_ID = "5e140e256d56"
SERVER_VERSION = "0.2.0-mock"
MAX_CLIENT_FRAME = 8192

OP_CONT, OP_TEXT, OP_BINARY, OP_CLOSE, OP_PING, OP_PONG = 0x0, 0x1, 0x2, 0x8, 0x9, 0xA

_print_lock = threading.Lock()


def log(event, **fields):
    record = {"t": round(time.time(), 3), "ev": event}
    record.update(fields)
    with _print_lock:
        sys.stdout.write(json.dumps(record) + "\n")
        sys.stdout.flush()


def encode_frame(opcode, payload, fin=True):
    """One unmasked server-to-client frame."""
    head = bytes([(0x80 if fin else 0) | opcode])
    n = len(payload)
    if n < 126:
        head += bytes([n])
    elif n < 65536:
        head += bytes([126]) + struct.pack(">H", n)
    else:
        head += bytes([127]) + struct.pack(">Q", n)
    return head + payload


def split_evenly(data, parts):
    parts = max(1, min(parts, len(data))) if data else 1
    size, extra = divmod(len(data), parts)
    out, pos = [], 0
    for i in range(parts):
        step = size + (1 if i < extra else 0)
        out.append(data[pos:pos + step])
        pos += step
    return out


class Connection:
    """One upgraded WebSocket; reading happens on the HTTP handler's thread."""

    def __init__(self, state, sock, number, token):
        self.state = state
        self.sock = sock
        self.number = number
        self.token = token
        self.registered = False
        self.open = True
        self.write_lock = threading.Lock()

    def send_raw(self, data):
        with self.write_lock:
            if not self.open:
                return False
            try:
                self.sock.sendall(data)
                return True
            except OSError:
                self.open = False
                return False

    def send_json(self, obj):
        return self.send_raw(encode_frame(OP_TEXT, json.dumps(obj, separators=(",", ":")).encode()))

    def send_fragmented(self, payload, parts, with_ping):
        chunks = split_evenly(payload, parts)
        frames = []
        for i, chunk in enumerate(chunks):
            frames.append(encode_frame(OP_TEXT if i == 0 else OP_CONT, chunk, fin=i == len(chunks) - 1))
            if with_ping and i < len(chunks) - 1:
                frames.append(encode_frame(OP_PING, b"mid"))
        # Separate writes with a pause so the fragments also arrive in separate TCP segments.
        for frame in frames:
            if not self.send_raw(frame):
                return False
            time.sleep(0.02)
        return True

    def close(self, code):
        self.send_raw(encode_frame(OP_CLOSE, struct.pack(">H", code)))
        # The client answers the close frame and the reader loop ends; this is the fallback.
        threading.Timer(1.0, self.drop).start()

    def drop(self):
        self.open = False
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass

    def _read_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("closed")
            data += chunk
        return data

    def _read_frame(self):
        b0, b1 = self._read_exact(2)
        fin, opcode = bool(b0 & 0x80), b0 & 0x0F
        masked, length = bool(b1 & 0x80), b1 & 0x7F
        if length == 126:
            (length,) = struct.unpack(">H", self._read_exact(2))
        elif length == 127:
            (length,) = struct.unpack(">Q", self._read_exact(8))
        if length > MAX_CLIENT_FRAME:
            raise ConnectionError("frame too large")
        mask = self._read_exact(4) if masked else None
        payload = self._read_exact(length)
        if mask:
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        return fin, opcode, masked, payload

    def _on_text(self, text):
        try:
            msg = json.loads(text)
        except ValueError:
            msg = None
        log("rx", conn=self.number, bytes=len(text.encode()), frame=msg if msg is not None else text)
        if not isinstance(msg, dict):
            self.send_json({"type": "error", "code": "INVALID_MESSAGE", "message": "not a JSON object"})
            return
        kind = msg.get("type")
        if not self.registered:
            self._on_register(msg)
        elif kind == "ping":
            self.send_json({"type": "pong"})
        elif kind == "extend" and isinstance(msg.get("id"), str):
            self.state.count("extend")
            with self.state.lock:
                reply = self.state.extend_reply
            if reply:
                self.send_json({"type": "deadline", "id": msg["id"], "timeout": 60000})
        elif kind == "set_mode" and msg.get("mode") in ("ask", "desk"):
            self.state.mode = msg["mode"]
            self.state.broadcast({"type": "mode", "mode": self.state.mode})

    def _on_register(self, msg):
        client = msg.get("client")
        if msg.get("type") != "register" or not isinstance(client, dict):
            self.send_json({"type": "error", "code": "INVALID_MESSAGE", "message": "register expected"})
            self.close(1008)
            return
        if msg.get("proto") != 2:
            self.send_json({"type": "error", "code": "UNSUPPORTED_PROTO", "message": "protocol 2 required"})
            self.close(4002)
            return
        if not isinstance(client.get("version"), str) or not isinstance(client.get("deviceId"), str):
            self.send_json({"type": "error", "code": "INVALID_MESSAGE", "message": "client.version and deviceId"})
            self.close(1008)
            return
        self.registered = True
        self.state.count("registered")
        ack = {
            "type": "register_ack", "role": "consumer", "roomId": ROOM_ID, "proto": 2,
            "serverVersion": SERVER_VERSION, "serverTime": int(time.time() * 1000), "mode": self.state.mode,
            "utcOffsetMin": self.state.utc_offset,
            "peers": {"producers": 0, "consumers": 1, "devices": 1},
            "device": {"id": client["deviceId"], "name": self.state.device_name},
        }
        with self.state.lock:
            caps = list(self.state.caps)
        if caps:
            ack["caps"] = caps
        self.send_json(ack)
        log("registered", conn=self.number, deviceId=client["deviceId"], version=client["version"],
            caps=client.get("caps"))

    def serve(self):
        message, message_op = b"", None
        # Protocol 3.4: a socket that sent nothing for this long is dead.
        self.sock.settimeout(self.state.args.dead_after)
        try:
            while self.open:
                fin, opcode, masked, payload = self._read_frame()
                if not masked:
                    log("protocol_error", conn=self.number, detail="client frame not masked")
                if opcode == OP_PING:
                    self.send_raw(encode_frame(OP_PONG, payload))
                elif opcode == OP_PONG:
                    pass
                elif opcode == OP_CLOSE:
                    code = struct.unpack(">H", payload[:2])[0] if len(payload) >= 2 else None
                    log("rx_close", conn=self.number, code=code)
                    self.send_raw(encode_frame(OP_CLOSE, payload[:2]))
                    break
                elif opcode in (OP_TEXT, OP_BINARY, OP_CONT):
                    if opcode != OP_CONT:
                        message, message_op = b"", opcode
                    message += payload
                    if fin and message_op == OP_TEXT:
                        self._on_text(message.decode("utf-8", "replace"))
                    elif fin:
                        log("rx_binary", conn=self.number, bytes=len(message))
        except socket.timeout:
            log("silent", conn=self.number, seconds=self.state.args.dead_after)
        except (OSError, ConnectionError):
            pass
        finally:
            self.open = False


class State:
    def __init__(self, args):
        self.args = args
        self.lock = threading.Lock()
        self.tokens = {args.token}
        self.reject_token = False
        self.stalled = False
        self.mode = "ask"
        self.device_name = "desk"
        self.confirm_after = args.confirm_after
        self.connections = []
        self.next_conn = 1
        self.pairings = {}
        self.counters = {}
        self.caps = [c for c in args.caps.split(",") if c] if args.caps != "none" else []
        self.utc_offset = args.utc_offset
        self.extend_reply = True
        self.pets = {}
        self.pet_mode = "truncate" if args.pet_truncate else "normal"
        if os.path.isdir(args.pets):
            for name in sorted(os.listdir(args.pets)):
                if name.endswith(".pet"):
                    with open(os.path.join(args.pets, name), "rb") as fh:
                        self.pets[name[:-4]] = fh.read()

    def count(self, name):
        with self.lock:
            self.counters[name] = self.counters.get(name, 0) + 1

    def live(self):
        with self.lock:
            self.connections = [c for c in self.connections if c.open]
            return list(self.connections)

    def broadcast(self, obj):
        return sum(1 for c in self.live() if c.send_json(obj))

    def flood(self, count, seconds):
        """Sends count numbered status frames spread evenly over seconds; returns how many were written."""
        started = time.monotonic()
        sent = 0
        for i in range(count):
            delay = started + seconds * i / count - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            sent += self.broadcast({"type": "status", "msg": f"flood {i:03d}"})
        return sent

    def status(self):
        live = self.live()
        with self.lock:
            return {
                "ok": True,
                "connections": len(live),
                "registered": sum(1 for c in live if c.registered),
                "rejectToken": self.reject_token,
                "stalled": self.stalled,
                "proto": self.args.proto,
                "pendingCodes": [p["code"] for p in self.pairings.values() if not p["confirmed"]],
                "counters": dict(self.counters),
            }

    def control(self, line):
        """Runs one control command and returns its JSON-serialisable result."""
        line = line.strip()
        if not line:
            return {"ok": True}
        command, _, rest = line.partition(" ")
        rest = rest.strip()
        log("ctl", command=command)
        try:
            if command == "time":
                if rest:
                    self.utc_offset = int(rest)
                msg = {"type": "time", "serverTime": int(time.time() * 1000), "utcOffsetMin": self.utc_offset}
                data = encode_frame(OP_TEXT, json.dumps(msg, separators=(",", ":")).encode())
                return {"ok": True, "sent": sum(1 for c in self.live() if c.send_raw(data)), "utcOffsetMin": self.utc_offset}
            if command == "send":
                json.loads(rest)
                data = encode_frame(OP_TEXT, rest.encode())
                return {"ok": True, "sent": sum(1 for c in self.live() if c.send_raw(data)), "bytes": len(rest.encode())}
            if command in ("send-fragmented", "send-fragmented-ping"):
                text, _, parts = rest.rpartition(" ")
                json.loads(text)
                payload = text.encode()
                with_ping = command.endswith("-ping")
                sent = sum(1 for c in self.live() if c.send_fragmented(payload, int(parts), with_ping))
                return {"ok": True, "sent": sent, "bytes": len(payload), "frames": int(parts)}
            if command == "send-burst":
                frames = json.loads(rest)
                if not isinstance(frames, list) or not all(isinstance(f, dict) for f in frames):
                    raise ValueError("a JSON array of objects expected")
                data = b"".join(encode_frame(OP_TEXT, json.dumps(f, separators=(",", ":")).encode()) for f in frames)
                return {"ok": True, "sent": sum(1 for c in self.live() if c.send_raw(data)), "frames": len(frames)}
            if command == "extend-reply":
                if rest not in ("on", "off"):
                    raise ValueError("on or off expected")
                with self.lock:
                    self.extend_reply = rest == "on"
                return {"ok": True, "extendReply": rest == "on"}
            if command == "send-binary":
                data = encode_frame(OP_BINARY, bytes.fromhex(rest))
                return {"ok": True, "sent": sum(1 for c in self.live() if c.send_raw(data))}
            if command == "send-text":
                data = encode_frame(OP_TEXT, rest.encode())
                return {"ok": True, "sent": sum(1 for c in self.live() if c.send_raw(data)), "bytes": len(rest.encode())}
            if command == "send-continuation":
                data = encode_frame(OP_CONT, rest.encode())
                return {"ok": True, "sent": sum(1 for c in self.live() if c.send_raw(data))}
            if command == "flood":
                count, seconds = rest.split()
                return {"ok": True, "sent": self.flood(int(count), float(seconds)), "frames": int(count)}
            if command == "close":
                code = int(rest)
                closed = self.live()
                for c in closed:
                    if code == 4003:
                        with self.lock:
                            self.tokens.discard(c.token)
                    c.close(code)
                return {"ok": True, "closed": len(closed)}
            if command == "drop":
                dropped = self.live()
                for c in dropped:
                    c.drop()
                return {"ok": True, "dropped": len(dropped)}
            if command == "confirm":
                with self.lock:
                    pending = [p for p in self.pairings.values() if not p["confirmed"]]
                    for p in pending:
                        p["confirmed"] = True
                return {"ok": True, "confirmed": len(pending)}
            if command == "confirm-after":
                with self.lock:
                    self.confirm_after = None if rest == "off" else float(rest)
                return {"ok": True}
            if command == "expire":
                with self.lock:
                    for p in self.pairings.values():
                        p["expires"] = 0
                return {"ok": True}
            if command in ("reject-token", "accept-token"):
                with self.lock:
                    self.reject_token = command == "reject-token"
                return {"ok": True}
            if command in ("stall", "unstall"):
                with self.lock:
                    self.stalled = command == "stall"
                return {"ok": True}
            if command == "caps":
                with self.lock:
                    self.caps = [] if rest == "none" else [c for c in rest.split(",") if c]
                return {"ok": True, "caps": self.caps}
            if command == "pet-add":
                pid, _, hexdata = rest.partition(" ")
                data = bytes.fromhex(hexdata.strip())
                with self.lock:
                    self.pets[pid] = data
                return {"ok": True, "pet": pid, "size": len(data), "rev": data[5] if len(data) > 5 else 0,
                        "crc32": data[-4:][::-1].hex() if len(data) >= 4 else ""}
            if command == "pet-cmd":
                parts = rest.split()
                op, pid = parts[0], parts[1] if len(parts) > 1 else ""
                pid = "" if pid == '""' else pid
                cmd = {"type": "pet_cmd", "op": op, "pet": pid}
                with self.lock:
                    data = self.pets.get(pid)
                if op == "install" and data is not None:
                    cmd.update(rev=data[5], size=len(data), crc32=data[-4:][::-1].hex())
                for extra in parts[2:]:
                    if extra == "select":
                        cmd["select"] = True
                    elif "=" in extra:
                        k, _, v = extra.partition("=")
                        cmd[k] = int(v) if v.lstrip("-").isdigit() and k != "crc32" else v
                text = json.dumps(cmd, separators=(",", ":"))
                frame = encode_frame(OP_TEXT, text.encode())
                return {"ok": True, "sent": sum(1 for c in self.live() if c.send_raw(frame)), "cmd": cmd}
            if command == "pet-mode":
                with self.lock:
                    self.pet_mode = rest or "normal"
                return {"ok": True, "mode": self.pet_mode}
            if command == "status":
                return self.status()
            if command == "tokens":
                with self.lock:
                    return {"ok": True, "tokens": sorted(self.tokens)}
        except (ValueError, TypeError) as e:
            return {"ok": False, "error": f"{command}: {e}"}
        return {"ok": False, "error": f"unknown command {command}"}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "devotchi-mock"

    def log_message(self, fmt, *args):
        pass

    @property
    def state(self):
        return self.server.state

    def _json(self, status, obj, extra_headers=()):
        body = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        for name, value in extra_headers:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def _error(self, status, code, message):
        self._json(status, {"error": message, "code": code})

    def _body(self):
        length = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(length) if length > 0 else b""

    def _body_json(self):
        try:
            obj = json.loads(self._body() or b"{}")
        except ValueError:
            return None
        return obj if isinstance(obj, dict) else None

    def _stall(self):
        """Holds the connection open without answering, like a black-holed server."""
        self.state.count("stalled")
        log("stalled", path=self.path.split("?")[0])
        self.connection.settimeout(1.0)
        while self.state.stalled:
            try:
                if not self.connection.recv(1024):
                    break
            except socket.timeout:
                continue
            except OSError:
                break
        self.close_connection = True

    def _bearer(self):
        value = self.headers.get("Authorization") or ""
        return value[7:].strip() if value.startswith("Bearer ") else None

    def do_GET(self):
        path, _, query = self.path.partition("?")
        if self.state.stalled:
            self._stall()
            return
        if (self.headers.get("Upgrade") or "").lower() == "websocket":
            self._upgrade(path, query)
            return
        if path in ("/health", "/"):
            if self.state.args.proto != 2:
                # A v1 server has no /health route and does not report a protocol version.
                self._error(404, "not_found", "Not found")
                log("http", method="GET", path=path, status=404)
                return
            self._json(200, {"status": "ok", "version": SERVER_VERSION, "proto": 2, "mode": "single-user",
                             "uptimeS": int(time.time() - self.server.started)})
            self.state.count("health")
            log("http", method="GET", path=path, status=200)
            return
        if path.startswith("/api/pets/") and path.endswith("/pack"):
            self._pet_pack(path[len("/api/pets/"):-len("/pack")])
            return
        self._error(404, "not_found", "Not found")
        log("http", method="GET", path=path, status=404)

    def _pet_pack(self, pid):
        """GET /api/pets/:id/pack (protocol 14.8), with the failure modes of pet-mode."""
        state = self.state
        token = self._bearer()
        with state.lock:
            known = token in state.tokens
            data = state.pets.get(pid)
            mode = state.pet_mode
        path = f"/api/pets/{pid}/pack"
        if not known:
            self._error(401, "unauthorized", "Token required")
            log("http", method="GET", path=path, status=401)
            return
        if data is None:
            self._error(404, "not_found", "Pet not found")
            log("http", method="GET", path=path, status=404)
            return
        if mode.startswith("status:"):
            code = int(mode.split(":", 1)[1])
            self._error(code, "not_found" if code == 404 else "internal", "Pet pack refused")
            log("http", method="GET", path=path, status=code, mode=mode)
            return
        length = int(mode.split(":", 1)[1]) if mode.startswith("length:") else len(data)
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(length))
        self.send_header("ETag", '"%s"' % data[-4:][::-1].hex())
        self.end_headers()
        log("http", method="GET", path=path, status=200, mode=mode, size=len(data))
        try:
            if mode == "truncate":
                self.wfile.write(data[:-20])
            elif mode == "drop":
                self.wfile.write(data[: len(data) // 2])
            elif mode.startswith("delay:"):
                self.wfile.write(data[:100])
                self.wfile.flush()
                time.sleep(float(mode.split(":", 1)[1]))
                self.wfile.write(data[100:])
            else:
                self.wfile.write(data[:length] if length < len(data) else data)
            self.wfile.flush()
        except OSError:
            pass
        if mode in ("truncate", "drop") or length != len(data):
            self.close_connection = True
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    def do_POST(self):
        path = self.path.partition("?")[0]
        if path == "/_ctl":
            result = self.state.control(self._body().decode("utf-8", "replace"))
            self._json(200 if result.get("ok") else 400, result)
            return
        if self.state.stalled:
            self._stall()
            return
        routes = {
            "/api/pair/claim": self._claim,
            "/api/device/pair/start": self._pair_start,
            "/api/device/pair/poll": self._pair_poll,
        }
        route = routes.get(path)
        if route is None or self.state.args.proto != 2:
            self._body()
            self._error(404, "not_found", "Not found")
            log("http", method="POST", path=path, status=404)
            return
        body = self._body_json()
        if body is None or "application/json" not in (self.headers.get("Content-Type") or ""):
            self._error(400, "invalid_request", "JSON body with Content-Type application/json required")
            log("http", method="POST", path=path, status=400)
            return
        status = route(body)
        log("http", method="POST", path=path, status=status, body=_redact(body))

    def _claim(self, body):
        code = body.get("code")
        if not isinstance(code, str):
            self._error(400, "invalid_request", "code required")
            return 400
        if code.upper() != self.state.args.code.upper():
            self._error(404, "not_found", "Unknown code")
            return 404
        with self.state.lock:
            self.state.tokens.add(self.state.args.token)
        self._json(201, {"token": self.state.args.token, "roomId": ROOM_ID,
                         "deviceId": body.get("deviceId") or "dv-000000", "name": "device"})
        self.state.count("claimed")
        return 201

    def _pair_start(self, body):
        if not isinstance(body.get("deviceId"), str):
            self._error(400, "invalid_request", "deviceId required")
            return 400
        code = "".join(secrets.choice(PAIR_ALPHABET) for _ in range(6))
        secret = secrets.token_hex(16)
        now = time.time()
        with self.state.lock:
            self.state.pairings[secret] = {
                "code": code, "deviceId": body["deviceId"], "created": now,
                "expires": now + self.state.args.code_life, "confirmed": False, "token": None,
            }
        self._json(201, {"code": code, "deviceSecret": secret, "expiresInS": self.state.args.code_life,
                         "pollIntervalMs": self.state.args.poll_ms})
        self.state.count("pairStart")
        log("pair_code", code=code)
        return 201

    def _pair_poll(self, body):
        now = time.time()
        with self.state.lock:
            pairing = self.state.pairings.get(body.get("deviceSecret"))
            if pairing is None:
                reply = (404, {"error": "Unknown secret", "code": "not_found"})
            elif now >= pairing["expires"] and pairing["token"] is None:
                reply = (410, {"error": "Code expired", "code": "expired"})
            else:
                auto = self.state.confirm_after
                if not pairing["confirmed"] and auto is not None and now - pairing["created"] >= auto:
                    pairing["confirmed"] = True
                if pairing["confirmed"]:
                    if pairing["token"] is None:
                        pairing["token"] = str(uuid.uuid4())
                        self.state.tokens.add(pairing["token"])
                    reply = (200, {"token": pairing["token"], "roomId": ROOM_ID, "deviceId": pairing["deviceId"],
                                   "name": self.state.device_name})
                else:
                    reply = (202, {"status": "pending", "expiresInS": int(pairing["expires"] - now)})
        self._json(*reply)
        if reply[0] == 200:
            self.state.count("pairDelivered")
        return reply[0]

    def _upgrade(self, path, query):
        bearer = self._bearer()
        with self.state.lock:
            accepted = bearer in self.state.tokens and not self.state.reject_token
        key = self.headers.get("Sec-WebSocket-Key")
        info = {"path": path, "auth": "bearer" if bearer is not None else "none",
                "tokenInQuery": "token=" in query, "query": bool(query)}
        if not accepted or key is None:
            self._error(401, "unauthorized", "Invalid token")
            self.close_connection = True
            self.state.count("upgrade401")
            log("upgrade", status=401, **info)
            return
        accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
        self.send_response(101, "Switching Protocols")
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        self.wfile.flush()
        self.close_connection = True
        # Like the ws library behind the real server: without this, Nagle holds back the second of several
        # frames sent together until the device's delayed ACK, which spreads a burst over 100+ ms.
        self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        with self.state.lock:
            conn = Connection(self.state, self.connection, self.state.next_conn, bearer)
            self.state.next_conn += 1
            self.state.connections.append(conn)
        self.state.count("upgrade101")
        log("upgrade", status=101, conn=conn.number, **info)

        stop_ping = threading.Event()

        def ping_loop():
            while not stop_ping.wait(self.state.args.ping):
                if conn.registered and not conn.send_json({"type": "ping"}):
                    break

        threading.Thread(target=ping_loop, daemon=True).start()
        try:
            conn.serve()
        finally:
            stop_ping.set()
            log("closed", conn=conn.number)


def _redact(body):
    return {k: ("<redacted>" if k in ("deviceSecret", "token") else v) for k, v in body.items()}


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=18090)
    parser.add_argument("--tls", nargs=2, metavar=("CERT", "KEY"), help="serve wss/https with this certificate")
    parser.add_argument("--proto", type=int, default=2, choices=(1, 2),
                        help="1 answers like a v1 server: no /health, no pairing routes")
    parser.add_argument("--caps", default="decide",
                        help="capabilities in register_ack, comma-separated, or 'none' for a phase-1 server")
    parser.add_argument("--code", default="K7M2QX", help="flow B code accepted by POST /api/pair/claim")
    parser.add_argument("--token", default=DEFAULT_TOKEN, help="token handed out by a claim and accepted on /ws")
    parser.add_argument("--confirm-after", type=float, default=None, metavar="SECONDS",
                        help="confirm flow A codes automatically this long after they were issued")
    parser.add_argument("--code-life", type=int, default=300, metavar="SECONDS")
    parser.add_argument("--poll-ms", type=int, default=2000)
    parser.add_argument("--utc-offset", type=int, default=-time.altzone // 60 if time.daylight and time.localtime().tm_isdst
                        else -time.timezone // 60, metavar="MINUTES", help="utcOffsetMin of register_ack and time")
    parser.add_argument("--ping", type=float, default=30.0, metavar="SECONDS", help="protocol ping interval")
    parser.add_argument("--pets", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "pets",
                                                       "golden"),
                        help="directory whose <id>.pet files are served at /api/pets/<id>/pack")
    parser.add_argument("--pet-truncate", action="store_true", help="start in pet-mode truncate")
    parser.add_argument("--dead-after", type=float, default=70.0, metavar="SECONDS",
                        help="drop a WebSocket that sent no frame for this long")
    args = parser.parse_args(argv)
    if args.port in FORBIDDEN_PORTS:
        parser.error(f"port {args.port} belongs to the real server and dashboard; use 18080..18099")
    return args


def main(argv=None):
    args = parse_args(argv)
    server = Server((args.host, args.port), Handler)
    server.state = State(args)
    server.started = time.time()
    if args.tls:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(args.tls[0], args.tls[1])
        # The handshake runs on the connection's own thread, so a slow client does not block accept().
        server.socket = context.wrap_socket(server.socket, server_side=True, do_handshake_on_connect=False)
    log("listening", host=args.host, port=args.port, tls=bool(args.tls), proto=args.proto, pid=os.getpid())

    def stdin_loop():
        for line in sys.stdin:
            result = server.state.control(line)
            log("ctl_result", **result)

    threading.Thread(target=stdin_loop, daemon=True).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
