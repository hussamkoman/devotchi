#!/usr/bin/env python3
"""Host side of the Devotchi debug console (docs/architecture.md 4.10).

Standard library only; Pillow is needed for `screenshot`. Run it with the
ESP-IDF Python, which has Pillow:

    ~/.espressif/tools/python/v6.0/venv/bin/python firmware/tools/devotchi_serial.py state

The port is opened with os.open + termios and the modem lines are never
touched: the DTR-then-RTS sequence a serial library applies on open passes
through the state that resets the chip through the USB-Serial-JTAG bridge.
`--reset` pulses the lines on purpose.

Exit status: 0 success, 1 the device answered @@ERR or the data was wrong,
2 no answer in time or the port could not be opened.
"""

import argparse
import base64
import errno
import fcntl
import json
import os
import select
import struct
import sys
import termios
import time
import zlib

DEFAULT_PORT = "/dev/ttyACM0"
REOPEN_WINDOW_S = 15.0
SCREENSHOT_ATTEMPTS = 3

EXIT_OK = 0
EXIT_ERR = 1
EXIT_TIMEOUT = 2


class ConsoleError(Exception):
    """The device answered @@ERR."""

    def __init__(self, command, code, message):
        super().__init__(f"{command}: {code} {message}".rstrip())
        self.command = command
        self.code = code
        self.message = message


class ConsoleTimeout(Exception):
    """No terminator, event or port within the allowed time."""


class DevotchiSerial:
    """One open console port. Survives the port disappearing (reset, deep sleep)."""

    def __init__(self, port=DEFAULT_PORT, verbose=False):
        self.port = port
        self.verbose = verbose
        self.fd = -1
        self.pending = b""

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *exc):
        self.close()

    def open(self, window_s=REOPEN_WINDOW_S):
        deadline = time.monotonic() + window_s
        last_error = None
        while True:
            try:
                self._open_once()
                return
            except OSError as e:
                last_error = e
                self.close()
            if time.monotonic() >= deadline:
                raise ConsoleTimeout(f"cannot open {self.port}: {last_error}")
            time.sleep(0.2)

    def _open_once(self):
        fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.fd = fd
        # One tool per port at a time; a second invocation waits in open().
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        attrs = termios.tcgetattr(fd)
        iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs
        iflag &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP | termios.INLCR
                   | termios.IGNCR | termios.ICRNL | termios.IXON | termios.IXOFF | termios.IXANY)
        oflag &= ~termios.OPOST
        lflag &= ~(termios.ECHO | termios.ECHOE | termios.ECHOK | termios.ECHONL | termios.ICANON
                   | termios.ISIG | termios.IEXTEN)
        cflag &= ~(termios.CSIZE | termios.PARENB | termios.HUPCL)
        cflag |= termios.CS8 | termios.CLOCAL | termios.CREAD
        cc[termios.VMIN] = 0
        cc[termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, [iflag, oflag, cflag, lflag, ispeed, ospeed, cc])
        self.pending = b""

    def close(self):
        if self.fd >= 0:
            try:
                os.close(self.fd)
            except OSError:
                pass
            self.fd = -1

    def reopen(self):
        self.close()
        self.open()

    def reset(self):
        """Resets the chip the way esptool's hard reset does: RTS (EN) low with DTR released."""
        dtr = struct.pack("I", termios.TIOCM_DTR)
        rts = struct.pack("I", termios.TIOCM_RTS)
        fcntl.ioctl(self.fd, termios.TIOCMBIC, dtr)
        fcntl.ioctl(self.fd, termios.TIOCMBIS, rts)
        time.sleep(0.2)
        try:
            fcntl.ioctl(self.fd, termios.TIOCMBIC, rts)
        except OSError:
            pass
        # The USB device re-enumerates; give the old node time to go away.
        time.sleep(0.5)
        self.reopen()

    def drain(self):
        """Discards input that arrived before a command is sent."""
        self.pending = b""
        try:
            while True:
                readable, _, _ = select.select([self.fd], [], [], 0)
                if not readable or not os.read(self.fd, 4096):
                    break
        except OSError:
            self.reopen()

    def send(self, line):
        # The leading newline ends any partial line the device may have buffered.
        data = b"\n" + line.encode("utf-8") + b"\n"
        deadline = time.monotonic() + REOPEN_WINDOW_S
        while data:
            try:
                _, writable, _ = select.select([], [self.fd], [], 1.0)
                if writable:
                    data = data[os.write(self.fd, data):]
            except BlockingIOError:
                pass
            except OSError:
                self.reopen()
            if time.monotonic() >= deadline:
                raise ConsoleTimeout(f"cannot write to {self.port}")

    def read_line(self, deadline):
        """Returns the next line without its newline, or None at the deadline."""
        while True:
            nl = self.pending.find(b"\n")
            if nl >= 0:
                line = self.pending[:nl].rstrip(b"\r")
                self.pending = self.pending[nl + 1:]
                text = line.decode("utf-8", errors="replace")
                if self.verbose:
                    print(f"< {text}", file=sys.stderr)
                return text
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            try:
                readable, _, _ = select.select([self.fd], [], [], min(remaining, 0.5))
                if not readable:
                    continue
                chunk = os.read(self.fd, 65536)
            except BlockingIOError:
                continue
            except OSError as e:
                if e.errno not in (errno.EIO, errno.ENODEV, errno.ENXIO, errno.EBADF, errno.ENOENT):
                    raise
                chunk = b""
            if not chunk:
                # Hang-up: the device reset or went to deep sleep.
                self.reopen()
                continue
            self.pending += chunk

    def command(self, line, timeout=10.0, on_line=None):
        """Sends one command and returns its payload lines; raises ConsoleError on @@ERR."""
        name = line.split()[0]
        self.drain()
        self.send(line)
        deadline = time.monotonic() + timeout
        payload = []
        while True:
            text = self.read_line(deadline)
            if text is None:
                raise ConsoleTimeout(f"no answer to '{name}' within {timeout:g} s")
            if text == f"@@OK {name}":
                return payload
            if text.startswith(f"@@ERR {name} "):
                parts = text.split(" ", 3)
                raise ConsoleError(name, parts[2], parts[3] if len(parts) > 3 else "")
            if text.startswith("@@") and not text.startswith(("@@EVT ", "@@READY ", "@@OK ", "@@ERR ")):
                payload.append(text)
            elif on_line is not None:
                on_line(text)

    def json_command(self, line, timeout=10.0):
        """Sends a command whose payload is one @@JSON line and returns the object."""
        for text in self.command(line, timeout):
            if text.startswith("@@JSON "):
                return json.loads(text[len("@@JSON "):])
        raise ConsoleError(line.split()[0], "failed", "no @@JSON payload")

    def wait_ready(self, timeout=30.0):
        """Polls `version` until the device answers; never relies on @@READY."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                return self.json_command("version", timeout=0.5)
            except (ConsoleTimeout, ConsoleError, ValueError):
                continue
        raise ConsoleTimeout(f"device not ready within {timeout:g} s")

    def expect_event(self, conditions, timeout=10.0):
        """Waits for an @@EVT object whose fields equal all conditions (compared as text)."""
        deadline = time.monotonic() + timeout
        while True:
            text = self.read_line(deadline)
            if text is None:
                raise ConsoleTimeout(f"no event matching {conditions} within {timeout:g} s")
            if not text.startswith("@@EVT "):
                continue
            try:
                event = json.loads(text[len("@@EVT "):])
            except ValueError:
                continue
            if all(_as_text(event.get(key)) == value for key, value in conditions.items()):
                return event

    def screenshot(self, timeout=20.0):
        """Returns (width, height, RGB565LE bytes) after checking byte count and CRC-32."""
        last_error = None
        for _ in range(SCREENSHOT_ATTEMPTS):
            try:
                return _parse_screenshot(self.command("screenshot", timeout))
            except (ConsoleError, ConsoleTimeout, ValueError) as e:
                last_error = e
                time.sleep(0.3)
        raise ConsoleError("screenshot", "failed", f"{SCREENSHOT_ATTEMPTS} attempts: {last_error}")


def _as_text(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    return None if value is None else str(value)


def _parse_screenshot(payload):
    begin = next((l for l in payload if l.startswith("@@SCREENSHOT BEGIN ")), None)
    end = next((l for l in payload if l.startswith("@@SCREENSHOT END ")), None)
    if begin is None or end is None:
        raise ValueError("screenshot block incomplete")
    fields = dict(item.split("=", 1) for item in begin.split()[2:])
    if fields.get("fmt") != "RGB565LE" or fields.get("enc") != "base64":
        raise ValueError(f"unsupported screenshot format: {begin}")
    width, height, size = int(fields["w"]), int(fields["h"]), int(fields["bytes"])
    data_lines = [l[4:] for l in payload if l.startswith("@@D ")]
    expected_lines = int(dict(item.split("=", 1) for item in end.split()[2:])["lines"])
    if len(data_lines) != expected_lines:
        raise ValueError(f"got {len(data_lines)} data lines, device sent {expected_lines}")
    raw = b"".join(base64.b64decode(l, validate=True) for l in data_lines)
    if len(raw) != size or size != width * height * 2:
        raise ValueError(f"got {len(raw)} bytes, expected {size}")
    crc = zlib.crc32(raw) & 0xFFFFFFFF
    if crc != int(fields["crc32"], 16):
        raise ValueError(f"crc32 {crc:08x} does not match {fields['crc32']}")
    return width, height, raw


def rgb565le_to_rgb888(raw):
    out = bytearray(len(raw) // 2 * 3)
    for i in range(len(raw) // 2):
        pixel = raw[2 * i] | (raw[2 * i + 1] << 8)
        r, g, b = pixel >> 11, (pixel >> 5) & 0x3F, pixel & 0x1F
        out[3 * i] = (r << 3) | (r >> 2)
        out[3 * i + 1] = (g << 2) | (g >> 4)
        out[3 * i + 2] = (b << 3) | (b >> 2)
    return bytes(out)


def save_png(path, width, height, raw):
    from PIL import Image

    Image.frombytes("RGB", (width, height), rgb565le_to_rgb888(raw)).save(path)


def _cmd_cmd(dev, args):
    for text in dev.command(args.line, args.timeout):
        print(text)
    return EXIT_OK


def _cmd_state(dev, args):
    print(json.dumps(dev.json_command("state", args.timeout), indent=2))
    return EXIT_OK


def _cmd_screenshot(dev, args):
    width, height, raw = dev.screenshot(max(args.timeout, 20.0))
    save_png(args.out, width, height, raw)
    print(f"{args.out} {width}x{height} crc32={zlib.crc32(raw) & 0xFFFFFFFF:08x}")
    return EXIT_OK


def _cmd_wait_ready(dev, args):
    version = dev.wait_ready(args.timeout)
    print(f"ready fw={version.get('fw')} reset={version.get('reset')} uptimeS={version.get('uptimeS')}")
    return EXIT_OK


def _cmd_expect_evt(dev, args):
    conditions = {}
    for item in args.conditions.split():
        key, sep, value = item.partition("=")
        if not sep:
            raise SystemExit(f"expect-evt: '{item}' is not key=value")
        conditions[key] = value
    if args.cmd:
        # The event can precede the terminator, so the command's own output is searched too.
        dev.drain()
        dev.send(args.cmd)
    print(json.dumps(dev.expect_event(conditions, args.timeout)))
    return EXIT_OK


def _cmd_monitor(dev, args):
    deadline = time.monotonic() + args.seconds if args.seconds else float("inf")
    try:
        while True:
            text = dev.read_line(min(deadline, time.monotonic() + 1.0))
            if text is not None:
                print(text, flush=True)
            elif time.monotonic() >= deadline:
                return EXIT_OK
    except KeyboardInterrupt:
        return EXIT_OK


def main(argv=None):
    parser = argparse.ArgumentParser(description="Devotchi debug console client")
    parser.add_argument("--port", default=os.environ.get("DEVOTCHI_PORT", DEFAULT_PORT))
    parser.add_argument("--timeout", type=float, default=10.0, help="seconds to wait for the answer")
    # --timeout is also accepted after the subcommand; SUPPRESS keeps the value given before it.
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--timeout", type=float, default=argparse.SUPPRESS, help=argparse.SUPPRESS)
    parser.add_argument("--reset", action="store_true", help="reset the chip first, then wait until it answers")
    parser.add_argument("--verbose", action="store_true", help="copy every received line to stderr")
    sub = parser.add_subparsers(dest="subcommand", required=True)

    p = sub.add_parser("cmd", parents=[common], help="send one command line and print its payload lines")
    p.add_argument("line")
    p.set_defaults(func=_cmd_cmd)

    p = sub.add_parser("state", parents=[common], help="print the state object as indented JSON")
    p.set_defaults(func=_cmd_state)

    p = sub.add_parser("screenshot", parents=[common], help="save the panel content as a PNG")
    p.add_argument("out")
    p.set_defaults(func=_cmd_screenshot)

    p = sub.add_parser("wait-ready", parents=[common], help="poll `version` until the device answers")
    p.set_defaults(func=_cmd_wait_ready)

    p = sub.add_parser("expect-evt", parents=[common], help="wait for an @@EVT line, e.g. 'evt=screen id=pet'")
    p.add_argument("conditions")
    p.add_argument("--cmd", help="command line to send before waiting")
    p.set_defaults(func=_cmd_expect_evt)

    p = sub.add_parser("monitor", parents=[common], help="print everything the device writes")
    p.add_argument("--seconds", type=float, help="stop after this many seconds (default: until Ctrl-C)")
    p.set_defaults(func=_cmd_monitor)

    args = parser.parse_args(argv)
    try:
        with DevotchiSerial(args.port, args.verbose) as dev:
            if args.reset:
                dev.reset()
                dev.wait_ready(max(args.timeout, 30.0))
            return args.func(dev, args)
    except ConsoleError as e:
        print(f"@@ERR {e.command} {e.code} {e.message}".rstrip(), file=sys.stderr)
        return EXIT_ERR
    except ConsoleTimeout as e:
        print(f"timeout: {e}", file=sys.stderr)
        return EXIT_TIMEOUT


if __name__ == "__main__":
    sys.exit(main())
