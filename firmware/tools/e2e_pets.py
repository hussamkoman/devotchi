#!/usr/bin/env python3
"""End-to-end test of pet packs (protocol 14.8, architecture 7.2) on a real board against mock_server.py.

Installs, selects, replaces and removes packs over pet_cmd, checks every
failure result (crc, size, mismatch, http, format, unknown, builtin) and
that a failure changes nothing, and resets the board during an install.
The board's pairing, server URL, brightness, haptics and selected pet are
put back at the end; the test pets it stored are removed.

Run with the ESP-IDF Python:
  ~/.espressif/tools/python/v6.0/venv/bin/python -I firmware/tools/e2e_pets.py --out /tmp/e2e-pets
"""

import argparse
import json
import os
import struct
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from devotchi_serial import DEFAULT_PORT  # noqa: E402
from e2e_flow import Run, frame_of  # noqa: E402
from e2e_link import FORBIDDEN_PORTS, StepFailed, check, is_registered  # noqa: E402
import petpack  # noqa: E402

PETS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "pets")
TEST_IDS = ("mochi", "wrongid", "fmt2", "mochi3", "mochi4")


def pack_for(pet_id, rev=1, fmt=1, bad_trailer=False):
    """Devo's source renamed to pet_id; fmt and a broken trailer for the failure cases."""
    with open(os.path.join(PETS_DIR, "devo.pet.json"), encoding="utf-8") as fh:
        src = json.load(fh)
    src["id"], src["name"], src["rev"] = pet_id, pet_id.capitalize()[:12], rev
    data = bytearray(petpack.encode(src))
    if fmt != 1:
        data[4] = fmt
        data[-4:] = struct.pack("<I", zlib.crc32(bytes(data[:-4])))
    if bad_trailer:
        data[-1] ^= 0xFF
    return bytes(data)


def crc_of(data):
    return "%08x" % zlib.crc32(data[:-4])


class PetsRun(Run):
    def __init__(self, args):
        super().__init__(args)
        self.original_pet = None

    def mock_options(self):
        return super().mock_options() + ("--caps", "decide,ask,plan,pets")

    def pets_state(self):
        s = self.board.state()
        return s["pet"], s["pets"]

    def pet_cmd(self, op, pet_id, *extra, expect, request=None, timeout=25):
        """Sends pet_cmd, waits for the board's `pets` with a result; checks error and whether a GET was made."""
        mark = self.mock.mark()
        reply = self.mock.ctl(" ".join(("pet-cmd", op, pet_id if pet_id else '""') + extra))
        check(reply.get("sent", 0) >= 1, f"pet_cmd reached {reply.get('sent')} sockets")
        ev = self.mock.wait_for(lambda e: (frame_of(e, "pets") or {}).get("result") is not None, timeout,
                                f"pets result for {op} {pet_id!r}", mark)
        result = frame_of(ev, "pets")["result"]
        got = "ok" if result.get("ok") else result.get("error")
        check(got == expect, f"{op} {pet_id!r}: result {result}, expected {expect}")
        gets = [e for e in self.mock.since(mark, "http") if e.get("path", "").startswith("/api/pets/")]
        if request is not None:
            check(bool(gets) == request, f"{op} {pet_id!r}: pack requests {gets}, expected {'one' if request else 'none'}")
        return frame_of(ev, "pets"), gets

    def unchanged(self, before, what):
        after = self.pets_state()
        check(after[0]["selected"] == before[0]["selected"], f"{what}: selection changed {before[0]} -> {after[0]}")
        check(after[1]["stored"] == before[1]["stored"], f"{what}: stored pets changed {before[1]} -> {after[1]}")

    def reboot(self):
        self.board.reset()
        self.board.wait_ready(40)
        state = self.board.wait_state(is_registered, 40, "registration after the reset")
        check(state["paired"] and state["wifiConfigured"], "pairing or WiFi lost over the reset")
        return state

    # ---- steps ----

    def step_record_pet(self):
        pet, pets = self.pets_state()
        self.original_pet = pet["selected"]
        check(pets["partition"] in ("ok", "erased") and pets["slots"] == 8, f"pets partition: {pets}")
        for pid in TEST_IDS:
            if any(e["id"] == pid for e in pets["stored"]):
                self.cmd(f"pet rm {pid}")
        return f"selected {self.original_pet}; partition {pets['partition']}, {pets['slots']} slots"

    def step_pets_after_register(self):
        ev = self.mock.wait_for(lambda e: frame_of(e, "pets") is not None, 10, "pets after register_ack")
        frame = frame_of(ev, "pets")
        check(frame["slots"] == 8 and any(p["id"] == "devo" and p.get("builtin") for p in frame["installed"]),
              f"pets frame {frame}")
        check(len(json.dumps(frame, separators=(",", ":"))) <= 700, "pets frame over 700 bytes")
        return json.dumps(frame, separators=(",", ":"))[:160]

    def step_install_select_reboot(self):
        data = pack_for("mochi")
        self.mock.ctl(f"pet-add mochi {data.hex()}")
        self.pet_cmd("install", "mochi", "select", expect="ok", request=True)
        pet, pets = self.pets_state()
        check(pet == {"selected": "mochi", "shown": "mochi", "source": "stored"}, f"after install: {pet}")
        check(pets["stored"] == [{"id": "mochi", "rev": 1}], f"stored: {pets['stored']}")
        self.reboot()
        pet, _ = self.pets_state()
        check(pet == {"selected": "mochi", "shown": "mochi", "source": "stored"}, f"after the reset: {pet}")
        pose = self.board.json_command("pet pose")
        check(pose["pet"] == "mochi", f"drawn pet {pose['pet']}")
        return "mochi rev 1 installed, selected, still selected and drawn after a reset"

    def step_failures_change_nothing(self):
        before = self.pets_state()
        data = pack_for("mochi")
        self.mock.ctl(f"pet-add mochi {data.hex()}")
        self.pet_cmd("install", "mochi", "crc32=00000000", expect="crc", request=True)
        self.unchanged(before, "crc")
        self.pet_cmd("install", "mochi", f"size={len(data) + 1}", expect="size", request=True)
        self.unchanged(before, "size")
        self.pet_cmd("install", "mochi", "rev=2", expect="mismatch", request=True)
        self.unchanged(before, "rev mismatch")
        self.mock.ctl(f"pet-add wrongid {data.hex()}")
        self.pet_cmd("install", "wrongid", expect="mismatch", request=True)
        self.unchanged(before, "id mismatch")
        self.pet_cmd("install", "nope", "rev=1", f"size={len(data)}", "crc32=12345678", expect="http", request=True)
        self.unchanged(before, "404")
        fmt2 = pack_for("fmt2", fmt=2)
        self.mock.ctl(f"pet-add fmt2 {fmt2.hex()}")
        self.pet_cmd("install", "fmt2", expect="format", request=True)
        self.unchanged(before, "fmt 2")
        for mode in ("truncate", "drop"):
            self.mock.ctl(f"pet-mode {mode}")
            self.pet_cmd("install", "mochi", expect="http", request=True)
            self.unchanged(before, mode)
        self.mock.ctl("pet-mode normal")
        return "crc, size, mismatch (rev, id), http (404, short body, dropped connection), format: nothing changed"

    def step_malformed_and_builtin(self):
        before = self.pets_state()
        for bad in ("../x", "Mochi", "abcdefghi", ""):
            frame, _ = self.pet_cmd("install", bad, "rev=1", "size=344", "crc32=1a2b3c4d", expect="unknown",
                                    request=False)
            check(frame["result"]["pet"] == "", f"malformed id echoed back: {frame['result']}")
        self.pet_cmd("install", "mochi", "rev=0", expect="unknown", request=False)
        self.pet_cmd("install", "devo", expect="builtin", request=False)
        self.pet_cmd("remove", "ember", expect="builtin", request=False)
        self.pet_cmd("select", "nothere", expect="unknown", request=False)
        self.unchanged(before, "malformed")
        return "../x, Mochi, 9 characters, empty: unknown without a request; devo install, ember remove: builtin"

    def step_replace_bad_crc(self):
        good = pack_for("mochi", rev=1)
        self.mock.ctl(f"pet-add mochi {good.hex()}")
        self.pet_cmd("install", "mochi", "select", expect="ok")
        bad = pack_for("mochi", rev=2, bad_trailer=True)
        self.mock.ctl(f"pet-add mochi {bad.hex()}")
        # The command carries the CRC of the body, so the broken trailer is what fails.
        self.pet_cmd("install", "mochi", "select", f"crc32={crc_of(bad)}", expect="crc", request=True)
        self.reboot()
        pet, pets = self.pets_state()
        check(pet == {"selected": "mochi", "shown": "mochi", "source": "stored"}, f"after the reset: {pet}")
        check({"id": "mochi", "rev": 1} in pets["stored"], f"rev 1 not kept: {pets['stored']}")
        return "rev 2 with a bad CRC refused; rev 1 still installed, selected and drawn after a reset"

    def check_boot_consistent(self, old, new_id):
        pet, pets = self.pets_state()
        check(pet["source"] != "fallback", f"selected pet does not load: {pet}")
        check(pet["selected"] in (old, new_id), f"selection is neither old nor new: {pet}")
        if pet["selected"] == new_id:
            check(any(e["id"] == new_id for e in pets["stored"]), f"{new_id} selected but not stored")
        return pet

    def step_reset_during_install(self):
        old = self.pets_state()[0]["selected"]
        data = pack_for("mochi3")
        self.mock.ctl(f"pet-add mochi3 {data.hex()}")
        self.mock.ctl("pet-mode delay:8")
        mark = self.mock.mark()
        self.mock.ctl("pet-cmd install mochi3 select")
        self.mock.wait_for(lambda e: e.get("ev") == "http" and e.get("path") == "/api/pets/mochi3/pack", 15,
                           "pack request", mark)
        self.reboot()
        first = self.check_boot_consistent(old, "mochi3")
        self.mock.ctl("pet-mode normal")
        data = pack_for("mochi4")
        self.mock.ctl(f"pet-add mochi4 {data.hex()}")
        mark = self.mock.mark()
        self.mock.ctl("pet-cmd install mochi4 select")
        self.mock.wait_for(lambda e: (frame_of(e, "pets") or {}).get("result") is not None, 25, "pets result", mark)
        self.reboot()
        second = self.check_boot_consistent(first["selected"], "mochi4")
        return f"reset mid-body: {first['selected']}; reset after the result: {second['selected']}"

    def leave_settings(self):
        for _ in range(3):
            screen = self.screen().get("id")
            if screen == "picker":
                self.cmd("btn reject press")
            elif screen == "settings":
                self.cmd("btn action long")
            else:
                return
            time.sleep(0.5)

    def step_picker(self):
        try:
            return self.picker()
        finally:
            self.leave_settings()

    def picker(self):
        """Settings > Pet: page through the built-ins and "More pets", Use selects, Back returns."""
        self.cmd("pet use devo")
        self.wait_pet(10)
        self.cmd("btn action long")
        self.wait_screen(lambda s: (s.get("settings") or {}).get("list") == "quick", 5, "quick menu open")
        self.cmd("btn approve press")
        scr = self.wait_screen(lambda s: (s.get("settings") or {}).get("row") == "pet", 5, "cursor on Pet")
        self.cmd("btn approve press")
        scr = self.wait_screen(lambda s: s.get("id") == "picker", 5, "picker open")
        check("Devo" in scr["texts"] and "IN USE" in scr["texts"], f"page 1 texts {scr['texts']}")
        check((scr.get("tabs") or {}).get("approve") is None, f"Use on the pet in use: {scr.get('tabs')}")
        self.cmd("btn action press")
        scr = self.wait_screen(lambda s: "Ember" in s.get("texts", []), 5, "Ember page")
        check("IN USE" not in scr["texts"], f"Ember marked in use: {scr['texts']}")
        check(((scr.get("tabs") or {}).get("approve") or {}).get("text") == "Use", f"no Use tab: {scr.get('tabs')}")
        self.cmd("btn approve press")
        self.board.wait_state(lambda s: s["pet"]["selected"] == "ember" and s["pet"]["shown"] == "ember", 5,
                              "Use selects Ember")
        scr = self.wait_screen(lambda s: "IN USE" in s.get("texts", []), 5, "Ember now in use")
        stored = []
        for _ in range(10):
            self.cmd("btn action press")
            time.sleep(0.3)
            scr = self.screen()
            if "More pets" in scr.get("texts", []):
                break
            stored.append(scr.get("texts", [""])[0])
        check("More pets" in scr.get("texts", []), f"More pets page not reached: {scr.get('texts')}")
        check((scr.get("tabs") or {}).get("approve") is None, f"Use on More pets: {scr.get('tabs')}")
        self.cmd("btn reject press")
        self.wait_screen(lambda s: s.get("id") == "settings", 5, "back to settings")
        self.cmd("btn reject press")
        self.wait_pet(10)
        pose = self.board.json_command("pet pose")
        check(pose["pet"] == "ember", f"home draws {pose['pet']}")
        return (f"Devo (in use, no Use tab) -> Ember -> Use -> In use -> stored pages {stored} -> More pets -> Back; "
                "home draws Ember")

    def step_cleanup(self):
        if self.original_pet is not None:
            self.cmd(f"pet use {self.original_pet}")
        pets = self.pets_state()[1]
        for entry in pets["stored"]:
            if entry["id"] in TEST_IDS:
                self.cmd(f"pet rm {entry['id']}")
        pet, pets = self.pets_state()
        check(self.original_pet is None or pet["selected"] == self.original_pet, f"selection not restored: {pet}")
        check(not any(e["id"] in TEST_IDS for e in pets["stored"]), f"test pets left: {pets['stored']}")
        return f"selected {pet['selected']}; stored {pets['stored']}"

    def run(self):
        steps = [
            ("record-pet", self.step_record_pet),
            ("pets-after-register", self.step_pets_after_register),
            ("install-select-reboot", self.step_install_select_reboot),
            ("failures-change-nothing", self.step_failures_change_nothing),
            ("malformed-and-builtin", self.step_malformed_and_builtin),
            ("replace-bad-crc", self.step_replace_bad_crc),
            ("reset-during-install", self.step_reset_during_install),
            ("picker", self.step_picker),
        ]
        selected = self.args.steps.split(",") if self.args.steps else None
        ok = True
        try:
            self.board.open()
            self.board.wait_ready()
            ok = self.run_step("record", self.step_record) and self.run_step("claim", self.step_claim)
            for name, function in steps if ok else []:
                if selected is not None and name not in selected and name != "record-pet":
                    continue
                ok = self.run_step(name, function) and ok
        finally:
            ok = self.run_step("cleanup", self.step_cleanup) and ok
            ok = self.run_step("restore", self.step_restore) and ok
            self.mock.stop()
            self.board.drain()
            with open(os.path.join(self.args.out, "board.log"), "w", encoding="utf-8") as f:
                f.write("\n".join(text for _, text in self.board.lines) + "\n")
            self.board.close()
        with open(os.path.join(self.args.out, "result.json"), "w", encoding="utf-8") as f:
            json.dump({"ok": ok, "steps": self.results}, f, indent=2)
        print(f"{'OK' if ok else 'FAILED'}: results in {self.args.out}", flush=True)
        return 0 if ok else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", default=DEFAULT_PORT, help="serial console of the board")
    parser.add_argument("--mock-port", type=int, default=18093, help="TCP port for mock_server.py (18090..18099)")
    parser.add_argument("--host", help="address of this machine as the board sees it (default: derived)")
    parser.add_argument("--out", default="/tmp/devotchi-e2e-pets", help="directory for logs and result.json")
    parser.add_argument("--steps", help="comma-separated subset of steps (record, claim, cleanup, restore always run)")
    args = parser.parse_args(argv)
    if args.mock_port in FORBIDDEN_PORTS:
        parser.error(f"port {args.mock_port} belongs to the real server and dashboard")
    args.tls = None
    os.makedirs(args.out, exist_ok=True)
    return PetsRun(args).run()


if __name__ == "__main__":
    sys.exit(main())
