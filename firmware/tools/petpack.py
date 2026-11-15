#!/usr/bin/env python3
"""Pet pack tool: JSON source (pets/<id>.pet.json) <-> binary pack, format 1.

The byte layout is docs/architecture.md 7.2; the JSON schema is pets/README.md.
Python standard library only. The decoder applies every check of the
firmware's pet_pack_validate() (core/pet_pack.c), so `check` refuses what the
device would refuse.

  petpack.py encode <src.json> <out.pet>        write the pack, print id, size and CRC
  petpack.py decode <in.pet>                    print the pack as JSON source
  petpack.py c-array <src.json> <out.c>         write the compiled-in C array (core/pet_builtin_<id>.c)
  petpack.py check <src.json> [<golden.pet>]    encode, validate, and compare with the golden pack and its .crc32
"""
import json
import os
import struct
import sys
import zlib

FMT = 1
MAX_SIZE = 2048
MAX_PARTS, MAX_OVER, MAX_OSC = 28, 48, 32
FLAG_SHIFT = 0x01
LID_OFF = 1000
OSC_UNIT_MS = 50

MOODS = ["happy", "sad", "excited", "anxious", "sleeping", "angry", "proud", "idle", "focused", "working", "thinking"]
REACTS = ["pat", "poke", "dizzy", "wake", "attention"]
REACT_BASE = 16
CHANNELS = ["eye_open", "eye_size", "pupil_size", "pupil_dx", "pupil_dy", "brow_angle", "mouth_curve", "mouth_open",
            "bob_amp_px", "bob_period_ms", "blink_min_ms", "blink_max_ms", "jitter_px", "accessory", "glance", "lid",
            "brow_l", "brow_r", "brow_lift", "mouth_dx", "mouth_w"]
CHANNEL_RANGE = [(0, 100), (4, 48), (0, 100), (-100, 100), (-100, 100), (-60, 60), (-100, 100), (0, 100), (0, 12),
                 (0, 20000), (0, 30000), (0, 30000), (0, 6), (0, 6), (0, 2), (-60, 60), (-60, 60), (-60, 60),
                 (-20, 20), (-30, 30), (0, 60)]
ACCESSORIES = ["none", "zzz", "sweat", "tear", "sparkles", "dots", "tongue"]
GLANCES = ["off", "look", "scan"]
ROLES = ["body", "dark", "light", "cheek", "accent", "accent-dark", "cream", "ink", "white", "flame", "surface", "fog",
         "yellow"]
SHAPES = ["rrect", "line", "tri"]
LAYERS = ["behind", "body", "head", "front"]
OPS = ["move_x", "move_y", "show_nonzero", "show_eq"]
WAVES = ["sine", "bob", "scan", "jitter"]
EYES = ["round", "slit", "dot"]
MOUTHS = ["plain", "snout"]
WHOLE = 7

ROLE_NO_OUTLINE, ROLE_ANCHOR, ROLE_RING, ROLE_SQUASH = 0x10, 0x20, 0x40, 0x80


class PackError(ValueError):
    pass


def fail(msg):
    raise PackError(msg)


def rgb(text):
    t = text.lstrip("#")
    if len(t) != 6:
        fail(f"bad colour {text!r}")
    return bytes.fromhex(t)


def mood_number(name):
    if name in MOODS:
        return MOODS.index(name)
    if name in REACTS:
        return REACT_BASE + REACTS.index(name)
    fail(f"unknown mood or reaction {name!r}")


def mood_name(n):
    if n < len(MOODS):
        return MOODS[n]
    return REACTS[n - REACT_BASE]


def i8(v, what):
    if not -128 <= v <= 127:
        fail(f"{what} {v} does not fit int8")
    return v & 0xFF


def u8(v, what):
    if not 0 <= v <= 255:
        fail(f"{what} {v} does not fit uint8")
    return v


# ---- encode ------------------------------------------------------------------
def encode(src):
    pid, name = src["id"], src["name"]
    if not (1 <= len(pid) <= 8 and all(c in "abcdefghijklmnopqrstuvwxyz0123456789" for c in pid)):
        fail(f"id {pid!r} is not [a-z0-9]{{1,8}}")
    if not (1 <= len(name) <= 12 and all(0x20 <= ord(c) <= 0x7E for c in name)):
        fail(f"name {name!r} is not 1..12 printable ASCII")
    rev = src.get("rev", 1)
    pal = src["palette"]
    shift = pal["mode"] == "shift"
    palette = rgb(pal["base"]) + rgb(pal["accent"]) + rgb(pal["cream"]) + rgb(pal["sclera"])
    for m in MOODS:
        e = pal["moods"][m]
        if shift:
            target, t = e
            palette += rgb(target) + bytes([round(t * 255)])
        else:
            palette += rgb(e) + b"\0"

    f = src["face"]
    acc_mask = 0
    for a in f.get("replaces", []):
        acc_mask |= 1 << ACCESSORIES.index(a)
    face = bytes([EYES.index(f["eyes"]), MOUTHS.index(f["mouth"]), 1 if f["brows"] else 0, u8(f["eye_dx"], "eye_dx"),
                  i8(f["eye_y"], "eye_y"), i8(f["mouth_y"], "mouth_y"), u8(f["mouth_hw"], "mouth_hw"),
                  u8(f.get("sleep_drop", 0), "sleep_drop"), acc_mask, u8(f.get("eye_sq", 16), "eye_sq"),
                  u8(f.get("mouth_sq", 16), "mouth_sq"), i8(f.get("sweat_y", -46), "sweat_y")])

    aux = b""
    for m in MOODS + REACTS:
        row = src.get("aux", {}).get(m, [0, 0, 0, 0])
        if len(row) != 4:
            fail(f"aux row {m} needs 4 values")
        aux += bytes(i8(v, f"aux {m}") for v in row)

    over = b""
    n_over = 0
    for m, chans in src.get("overrides", {}).items():
        for ch, v in chans.items():
            if ch == "lid" and v == "off":
                v = LID_OFF
            elif ch == "accessory" and isinstance(v, str):
                v = ACCESSORIES.index(v)
            elif ch == "glance" and isinstance(v, str):
                v = GLANCES.index(v)
            over += struct.pack("<BBh", mood_number(m), CHANNELS.index(ch), v)
            n_over += 1

    osc = b""
    for o in src.get("oscillators", []):
        period = o.get("period_ms", 0)
        if period % OSC_UNIT_MS:
            fail(f"oscillator period {period} is not a multiple of {OSC_UNIT_MS} ms")
        osc += bytes([mood_number(o["mood"]), o["aux"] | (WAVES.index(o["wave"]) << 2), i8(o["amp"], "amp"),
                      u8(period // OSC_UNIT_MS, "period")])

    names = {}
    parts = b""
    for i, p in enumerate(src["parts"]):
        shape = SHAPES.index(p["shape"])
        drivers = p.get("drivers", [])
        kind = shape | (LAYERS.index(p["layer"]) << 2) | (len(drivers) << 4) | (0x80 if p.get("mirror") else 0)
        role = ROLES.index(p["role"])
        if p.get("outline") is False:
            role |= ROLE_NO_OUTLINE
        if "anchor" in p:
            role |= ROLE_ANCHOR
        if p.get("ring"):
            role |= ROLE_RING
        if p.get("squash"):
            role |= ROLE_SQUASH
        b = bytes([kind, role])
        if "anchor" in p:
            b += bytes([names[p["anchor"]]])
        if shape == 0:
            b += bytes([i8(p["x"], "x"), i8(p["y"], "y"), u8(p["w"], "w"), u8(p["h"], "h"), u8(p["r"], "r")])
        else:
            pts = p["points"]
            if shape == 1:
                b += bytes([len(pts) | (p["width"] << 3)])
            for x, y in pts:
                b += bytes([i8(x, "x"), i8(y, "y")])
        for d in drivers:
            op = OPS.index(d["op"])
            if op <= 1:
                arg = WHOLE if d.get("point", "whole") == "whole" else d["point"]
                scale = d.get("scale", 16)
            else:
                arg = d.get("k", 0)
                scale = 0
            b += bytes([d["aux"] | (op << 2) | (arg << 4), i8(scale, "scale")])
        if "name" in p:
            names[p["name"]] = i
        parts += b

    n_parts = len(src["parts"])
    size = 32 + len(palette) + len(face) + len(aux) + len(over) + len(osc) + len(parts) + 4
    header = b"DPET" + struct.pack("<BBH", FMT, rev, size) + pid.encode().ljust(8, b"\0") + \
        name.encode().ljust(12, b"\0") + bytes([FLAG_SHIFT if shift else 0, n_parts, n_over, len(osc) // 4])
    body = header + palette + face + aux + over + osc + parts
    pack = body + struct.pack("<I", zlib.crc32(body))
    decode(pack)
    return pack


# ---- decode (the checks of pet_pack_validate) ----------------------------------
def text_field(raw, is_id):
    n = raw.find(b"\0")
    n = len(raw) if n < 0 else n
    if n == 0 or any(raw[n:]):
        fail("text field padding")
    for c in raw[:n]:
        ok = (97 <= c <= 122 or 48 <= c <= 57) if is_id else 0x20 <= c <= 0x7E
        if not ok:
            fail("text field character")
    return raw[:n].decode()


def decode(pack):
    if len(pack) < 164 + 2 + 5 + 4 or len(pack) > MAX_SIZE:
        fail("length")
    body = pack[:-4]
    if zlib.crc32(body) != struct.unpack("<I", pack[-4:])[0]:
        fail("crc")
    if pack[:4] != b"DPET":
        fail("magic")
    if pack[4] != FMT:
        fail("fmt")
    rev, size = pack[5], struct.unpack("<H", pack[6:8])[0]
    if rev == 0:
        fail("rev")
    if size != len(pack):
        fail("length")
    out = {"id": text_field(pack[8:16], True), "name": text_field(pack[16:28], False), "rev": rev}
    flags, n_parts, n_over, n_osc = pack[28:32]
    if flags & ~FLAG_SHIFT:
        fail("reserved flags")
    if not 1 <= n_parts <= MAX_PARTS or n_over > MAX_OVER or n_osc > MAX_OSC:
        fail("count")
    shift = bool(flags & FLAG_SHIFT)
    pal = pack[32:88]
    col = lambda b: "#" + b.hex().upper()
    moods = {}
    for i, m in enumerate(MOODS):
        e = pal[12 + 4 * i:16 + 4 * i]
        if shift:
            moods[m] = [col(e[:3]), round(e[3] / 255, 3)]
        elif e[3]:
            fail("reserved palette byte")
        else:
            moods[m] = col(e[:3])
    out["palette"] = {"mode": "shift" if shift else "table", "base": col(pal[0:3]), "accent": col(pal[3:6]),
                      "cream": col(pal[6:9]), "sclera": col(pal[9:12]), "moods": moods}
    f = pack[88:100]
    s8 = lambda v: v - 256 if v > 127 else v
    if f[0] > 2 or f[1] > 1 or f[2] > 1 or not 4 <= f[3] <= 60 or not 4 <= f[6] <= 60 or f[7] > 30 or f[9] > 16 \
            or f[10] > 16:
        fail("face range")
    if f[8] & ~0x7E:
        fail("reserved acc_mask bits")
    out["face"] = {"eyes": EYES[f[0]], "mouth": MOUTHS[f[1]], "brows": bool(f[2]), "eye_dx": f[3], "eye_y": s8(f[4]),
                   "mouth_y": s8(f[5]), "mouth_hw": f[6], "sleep_drop": f[7],
                   "replaces": [a for i, a in enumerate(ACCESSORIES) if f[8] & (1 << i)], "eye_sq": f[9],
                   "mouth_sq": f[10], "sweat_y": s8(f[11])}
    out["aux"] = {m: [s8(v) for v in pack[100 + 4 * i:104 + 4 * i]] for i, m in enumerate(MOODS + REACTS)}
    p = 164
    end = len(pack) - 4

    def take(n):
        nonlocal p
        if end - p < n:
            fail("truncated")
        b = pack[p:p + n]
        p += n
        return b

    overrides = {}
    for _ in range(n_over):
        m, ch, v = struct.unpack("<BBh", take(4))
        if not (m < 11 or 16 <= m <= 20) or ch >= len(CHANNELS):
            fail("override range")
        lo, hi = CHANNEL_RANGE[ch]
        if not (lo <= v <= hi or (CHANNELS[ch] == "lid" and v == LID_OFF)):
            fail("override value")
        if CHANNELS[ch] == "lid" and v == LID_OFF:
            v = "off"
        overrides.setdefault(mood_name(m), {})[CHANNELS[ch]] = v
    out["overrides"] = overrides
    oscs = []
    for _ in range(n_osc):
        m, aw, amp, period = take(4)
        if not (m < 11 or 16 <= m <= 20):
            fail("oscillator mood")
        if aw & 0xF0:
            fail("reserved oscillator bits")
        wave = (aw >> 2) & 3
        if wave in (0, 3) and period == 0:
            fail("oscillator period")
        oscs.append({"mood": mood_name(m), "aux": aw & 3, "wave": WAVES[wave], "amp": s8(amp),
                     "period_ms": period * OSC_UNIT_MS})
    out["oscillators"] = oscs
    parts, kinds = [], []
    for i in range(n_parts):
        kind, role = take(2)
        kinds.append(kind)
        shape, layer, n_drv = kind & 3, (kind >> 2) & 3, (kind >> 4) & 7
        if shape > 2 or n_drv > 4:
            fail("part kind")
        if (role & 0x0F) >= len(ROLES):
            fail("role")
        if role & (ROLE_RING | ROLE_SQUASH) and shape != 0:
            fail("reserved role bits")
        part = {"name": f"p{i}", "shape": SHAPES[shape], "layer": LAYERS[layer], "role": ROLES[role & 0x0F]}
        if kind & 0x80:
            part["mirror"] = True
        if role & ROLE_NO_OUTLINE:
            part["outline"] = False
        if role & ROLE_RING:
            part["ring"] = True
        if role & ROLE_SQUASH:
            part["squash"] = True
        if role & ROLE_ANCHOR:
            a = take(1)[0]
            if a >= i or kinds[a] & 3 != 1:
                fail("anchor")
            part["anchor"] = f"p{a}"
        if shape == 0:
            x, y, w, h, r = take(5)
            if w == 0 or h == 0 or r > min(w, h) // 2:
                fail("rrect size")
            part.update(x=s8(x), y=s8(y), w=w, h=h, r=r)
            n_pts = 1
        else:
            n_pts = 3
            if shape == 1:
                lb = take(1)[0]
                n_pts, part["width"] = lb & 7, lb >> 3
                if not 2 <= n_pts <= 4 or part["width"] == 0:
                    fail("line")
            raw = take(2 * n_pts)
            part["points"] = [[s8(raw[2 * k]), s8(raw[2 * k + 1])] for k in range(n_pts)]
        drivers = []
        for _ in range(n_drv):
            ob, scale = take(2)
            aux, op, arg = ob & 3, (ob >> 2) & 3, (ob >> 4) & 7
            if op <= 1:
                if arg != WHOLE and (shape == 0 or arg >= n_pts):
                    fail("driver point")
                drivers.append({"op": OPS[op], "aux": aux, "point": "whole" if arg == WHOLE else arg,
                                "scale": s8(scale)})
            else:
                if scale or (op == 2 and arg):
                    fail("reserved driver bits")
                d = {"op": OPS[op], "aux": aux}
                if op == 3:
                    d["k"] = arg
                drivers.append(d)
        if drivers:
            part["drivers"] = drivers
        parts.append(part)
    if p != end:
        fail("trailing")
    out["parts"] = parts
    return out


# ---- commands ------------------------------------------------------------------
def load(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def c_array(pack, pid):
    lines = ["/* Generated by firmware/tools/petpack.py c-array from pets/%s.pet.json; do not edit. */" % pid,
             '#include "pet_builtin.h"', "",
             "const uint8_t pet_builtin_%s[%d] = {" % (pid, len(pack))]
    for i in range(0, len(pack), 12):
        lines.append("    " + " ".join("0x%02x," % b for b in pack[i:i + 12]))
    lines += ["};", "const size_t pet_builtin_%s_len = sizeof(pet_builtin_%s);" % (pid, pid), ""]
    return "\n".join(lines)


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    cmd = argv[1]
    try:
        if cmd == "encode" and len(argv) == 4:
            pack = encode(load(argv[2]))
            with open(argv[3], "wb") as fh:
                fh.write(pack)
            print(json.dumps({"id": load(argv[2])["id"], "size": len(pack), "crc32": "%08x" % zlib.crc32(pack[:-4])}))
        elif cmd == "decode" and len(argv) == 3:
            with open(argv[2], "rb") as fh:
                print(json.dumps(decode(fh.read()), indent=1))
        elif cmd == "c-array" and len(argv) == 4:
            src = load(argv[2])
            with open(argv[3], "w", encoding="utf-8") as fh:
                fh.write(c_array(encode(src), src["id"]))
        elif cmd == "check" and len(argv) in (3, 4):
            src = load(argv[2])
            pack = encode(src)
            crc = "%08x" % zlib.crc32(pack[:-4])
            if len(argv) == 4:
                with open(argv[3], "rb") as fh:
                    if fh.read() != pack:
                        fail(f"{argv[3]} differs from the encoded source")
                crc_path = os.path.splitext(argv[3])[0] + ".crc32"
                with open(crc_path, encoding="ascii") as fh:
                    if fh.read().strip() != crc:
                        fail(f"{crc_path} is not {crc}")
            print(json.dumps({"id": src["id"], "size": len(pack), "crc32": crc, "ok": True}))
        else:
            print(__doc__.strip(), file=sys.stderr)
            return 2
    except (ValueError, KeyError, IndexError) as e:
        print(f"petpack: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
