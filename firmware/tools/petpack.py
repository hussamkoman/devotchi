#!/usr/bin/env python3
"""Pet pack tool: JSON source (pets/<id>.pet.json) <-> binary pack, format 1.

The byte layout is docs/architecture.md 7.2; the JSON schema is pets/README.md.
Python standard library only. The decoder applies every check of the
firmware's pet_pack_validate() (core/pet_pack.c), so `check` refuses what the
device would refuse.

  petpack.py encode <src.json> <out.pet>       write the pack, print id, size and CRC
  petpack.py golden <src.json>...              write pets/golden/<id>.pet and <id>.crc32 next to each source
  petpack.py decode <in.pet>                   print the pack as JSON source
  petpack.py check <src.json>...               encode, validate, compare with golden/<id>.pet if there is one,
                                               and lint (pet-animations.md 5) against the other pets beside it;
                                               exit 1 on an error, warnings only print
  petpack.py builtins <pets_dir> <out_dir>     write pet_builtins.h/.c with the packs of <pets_dir>/builtin.txt
  petpack.py poses <src.json>                  print the rest pose of every mood (shared table + overrides)
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
TAGLINE_MAX = 32

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
    tagline = src.get("tagline", "")
    if not isinstance(tagline, str) or len(tagline) > TAGLINE_MAX or not all(0x20 <= ord(c) <= 0x7E for c in tagline):
        fail(f"tagline {tagline!r} is not 0..{TAGLINE_MAX} printable ASCII")
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


# ---- lint (docs/design/pet-animations.md 5) ----------------------------------------
# A static, rest-pose model of the rig (core/pet_pose.c, ui/pet_view.c and
# packages/shared/src/pet-render.ts draw it in full). It finds the mistakes the
# rules name; the preview sheet is still the final check by eye.

INK, WHITE, CORAL = 0x1A1D26, 0xFFFFFF, 0xFF6B5E
FIXED_RGB = {"ink": INK, "white": WHITE, "flame": 0xFF8A3D, "surface": 0x161A22, "fog": 0xA7AEBB, "yellow": 0xFFC53D}
DARK_T, LIGHT_T, CHEEK_T, ACC_DARK_T = 71, 102, 107, 64
SEMANTIC = {"mint": 0x5FD38D, "amber": 0xFFC53D, "coral": 0xFF6B5E, "sky": 0x6CC4FF}
BOX = (-110, -107, 110, 83)
MAX_LINT_SIZE = 1400
EYE_SHUT_OPEN = 15

# The shared pose of each mood: eye_open, eye_size, pupil_size, pupil_dx, pupil_dy, brow_angle, mouth_curve,
# mouth_open, accessory, lid, brow_l, brow_r, brow_lift, mouth_dx, mouth_w (the table of core/pet_pose.c).
POSE_CHANNELS = ["eye_open", "eye_size", "pupil_size", "pupil_dx", "pupil_dy", "brow_angle", "mouth_curve",
                 "mouth_open", "accessory", "lid", "brow_l", "brow_r", "brow_lift", "mouth_dx", "mouth_w"]
SHARED_POSE = {
    "happy": [100, 26, 45, 0, 0, -5, 80, 0, 0, LID_OFF, 0, 0, 0, 0, 0],
    "sad": [65, 24, 45, 0, 40, -20, -70, 0, 3, LID_OFF, 0, 0, 0, 0, 0],
    "excited": [100, 32, 40, 0, 0, -12, 100, 60, 4, LID_OFF, 0, 0, 0, 0, 0],
    "anxious": [100, 28, 22, 0, 0, -18, -25, 15, 2, LID_OFF, 0, 0, 0, 0, 0],
    "sleeping": [0, 26, 45, 0, 0, 0, 15, 0, 1, LID_OFF, 0, 0, 0, 0, 0],
    "angry": [60, 26, 50, 0, 0, 28, -55, 0, 0, LID_OFF, 0, 0, 0, 0, 0],
    "proud": [65, 26, 45, 0, -45, -8, 65, 0, 4, LID_OFF, 0, 0, 0, 0, 0],
    "idle": [100, 25, 46, 0, 0, 0, 42, 0, 0, LID_OFF, 0, 0, 0, 0, 0],
    "focused": [62, 26, 52, 0, 20, 0, 16, 0, 0, 0, 0, 0, 0, 0, 11],
    "working": [88, 24, 46, 0, 45, 0, 34, 0, 6, LID_OFF, 0, 0, 0, 0, 13],
    "thinking": [92, 24, 45, 60, -60, 0, -4, 0, 5, LID_OFF, -4, -16, 5, 9, 8],
}
# How far a channel must move to count as one whole visible difference.
POSE_SPAN = {"eye_open": 100, "eye_size": 24, "pupil_size": 50, "pupil_dx": 100, "pupil_dy": 100, "brow_angle": 40,
             "mouth_curve": 100, "mouth_open": 50, "brow_l": 40, "brow_r": 40, "brow_lift": 20, "mouth_dx": 30,
             "mouth_w": 30}
MOOD_MIN_DIFF = 0.3
MOOD_WARN_DIFF = 0.5
SILHOUETTE_MAX_IOU = 0.9
SILHOUETTE_WARN_IOU = 0.8
HUE_MIN_DE = 10
HUE_WARN_DE = 20
HUE_MIN_SHARE = 0.15


def lint_input(src):
    """The decoded pack of a source, its parts labelled with the source's names."""
    dec = decode(encode(src))
    for i, part in enumerate(dec["parts"]):
        part["label"] = src["parts"][i].get("name", part["name"])
    return dec


def mix(a, b, t):
    return sum(((((a >> s) & 0xFF) * (255 - t) + ((b >> s) & 0xFF) * t + 127) // 255) << s for s in (0, 8, 16))


def hexrgb(text):
    return int(text.lstrip("#"), 16)


def body_rgb(dec, mood):
    pal = dec["palette"]
    e = pal["moods"][mood]
    if pal["mode"] == "shift":
        return mix(hexrgb(pal["base"]), hexrgb(e[0]), round(e[1] * 255))
    return hexrgb(e)


def role_rgb(dec, role, body):
    pal = dec["palette"]
    accent = hexrgb(pal["accent"])
    derived = {"body": body, "dark": mix(body, INK, DARK_T), "light": mix(body, WHITE, LIGHT_T),
               "cheek": mix(body, CORAL, CHEEK_T), "accent": accent, "accent-dark": mix(accent, INK, ACC_DARK_T),
               "cream": hexrgb(pal["cream"])}
    return derived[role] if role in derived else FIXED_RGB[role]


def luma(c):
    return (0.2126 * ((c >> 16) & 0xFF) + 0.7152 * ((c >> 8) & 0xFF) + 0.0722 * (c & 0xFF)) / 255


def lab(c):
    def lin(v):
        v /= 255
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4

    r, g, b = lin((c >> 16) & 0xFF), lin((c >> 8) & 0xFF), lin(c & 0xFF)
    xyz = ((0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047, 0.2126 * r + 0.7152 * g + 0.0722 * b,
           (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883)
    f = [v ** (1 / 3) if v > 0.008856 else 7.787 * v + 16 / 116 for v in xyz]
    return 116 * f[1] - 16, 500 * (f[0] - f[1]), 200 * (f[1] - f[2])


def delta_e(a, b):
    return sum((p - q) ** 2 for p, q in zip(lab(a), lab(b))) ** 0.5


def aux_row(dec, mood):
    return dec["aux"].get(mood, [0, 0, 0, 0])


def part_shown(part, aux):
    for d in part.get("drivers", []):
        if d["op"] == "show_nonzero" and aux[d["aux"]] == 0:
            return False
        if d["op"] == "show_eq" and aux[d["aux"]] != d["k"]:
            return False
    return True


def shapes(dec, mood, night=False):
    """The parts drawn at rest in `mood`, back to front: (shape, points, part); a mirrored part twice."""
    aux = aux_row(dec, mood)
    index = {p["name"]: i for i, p in enumerate(dec["parts"])}
    ends = {}
    out = []
    for i, part in enumerate(dec["parts"]):
        anchor = index.get(part.get("anchor"))
        for side in (0, 1) if part.get("mirror") else (0,):
            sx = -1 if side else 1
            pts = [[x * sx, y] for x, y in (part["points"] if "points" in part else [[part["x"], part["y"]]])]
            for d in part.get("drivers", []):
                if d["op"] in ("move_x", "move_y"):
                    k = 0 if d["op"] == "move_x" else 1
                    amount = aux[d["aux"]] * d.get("scale", 16) * (sx if k == 0 else 1) / 16
                    for j, pt in enumerate(pts):
                        if d.get("point", "whole") in ("whole", j):
                            pt[k] += amount
            if anchor is not None:
                a = dec["parts"][anchor]
                ax, ay = ends[(anchor, side if a.get("mirror") else 0)]
                if not a.get("mirror"):
                    ax *= sx
                for pt in pts:
                    pt[0] += ax
                    pt[1] += ay
            if part["shape"] == "line":
                ends[(i, side)] = tuple(pts[-1])
            if part_shown(part, aux) and not (night and part.get("outline") is False):
                out.append((part["shape"], pts, part))
    return out


def covers(shape, pts, part, x, y):
    if shape == "rrect":
        hw, hh = part["w"] / 2, part["h"] / 2
        dx, dy = abs(x - pts[0][0]), abs(y - pts[0][1])
        if dx > hw or dy > hh:
            return False
        r = min(part["r"], hw, hh)
        qx, qy = dx - (hw - r), dy - (hh - r)
        return qx <= 0 or qy <= 0 or qx * qx + qy * qy <= r * r
    if shape == "tri":
        (ax, ay), (bx, by), (cx, cy) = pts
        d1 = (x - bx) * (ay - by) - (ax - bx) * (y - by)
        d2 = (x - cx) * (by - cy) - (bx - cx) * (y - cy)
        d3 = (x - ax) * (cy - ay) - (cx - ax) * (y - ay)
        return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))
    half = max(2, part["width"]) / 2
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        vx, vy = bx - ax, by - ay
        ll = vx * vx + vy * vy
        t = 0 if ll == 0 else max(0, min(1, ((x - ax) * vx + (y - ay) * vy) / ll))
        if (x - ax - t * vx) ** 2 + (y - ay - t * vy) ** 2 <= half * half:
            return True
    return False


def bounds(shape, pts, part):
    if shape == "rrect":
        (x, y), = pts
        return x - part["w"] / 2, y - part["h"] / 2, x + part["w"] / 2, y + part["h"] / 2
    pad = max(2, part.get("width", 0)) / 2 if shape == "line" else 0
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return min(xs) - pad, min(ys) - pad, max(xs) + pad, max(ys) + pad


def raster(drawn, step):
    """Pixel centres every `step` body units in the mockup box -> index of the topmost shape covering it."""
    cells = {}
    for n, (shape, pts, part) in enumerate(drawn):
        x0, y0, x1, y1 = bounds(shape, pts, part)
        for i in range(max(0, int((x0 - BOX[0]) / step)), int((min(x1, BOX[2]) - BOX[0]) / step) + 1):
            x = BOX[0] + (i + 0.5) * step
            for j in range(max(0, int((y0 - BOX[1]) / step)), int((min(y1, BOX[3]) - BOX[1]) / step) + 1):
                if covers(shape, pts, part, x, BOX[1] + (j + 0.5) * step):
                    cells[(i, j)] = n
    return cells


def night_mask(dec):
    """The night outline's silhouette at clock scale (9/16): one cell per screen pixel."""
    return set(raster(shapes(dec, "idle", night=True), 16 / 9))


def mood_pose(dec, mood):
    pose = dict(zip(POSE_CHANNELS, SHARED_POSE[mood]))
    for ch, v in dec["overrides"].get(mood, {}).items():
        if ch in pose:
            pose[ch] = LID_OFF if v == "off" else v
    return pose


def mood_diff(dec, a, b):
    """How differently moods a and b read without colour: 1 is one clear cue."""
    pa, pb = mood_pose(dec, a), mood_pose(dec, b)
    face = dec["face"]
    shut_a, shut_b = pa["eye_open"] < EYE_SHUT_OPEN, pb["eye_open"] < EYE_SHUT_OPEN
    score = 1.0 if shut_a != shut_b else 0.0
    for ch, span in POSE_SPAN.items():
        if ch.startswith(("brow_l", "brow_r", "brow_lift")) and not face["brows"]:
            continue
        if (shut_a or shut_b) and ch in ("eye_open", "pupil_size", "pupil_dx", "pupil_dy", "brow_angle"):
            continue
        score += abs(pa[ch] - pb[ch]) / span
    if (pa["lid"] == LID_OFF) != (pb["lid"] == LID_OFF):
        score += 0.5
    elif pa["lid"] != LID_OFF:
        score += abs(pa["lid"] - pb["lid"]) / 60

    def cue(pose, mood):
        acc = pose["accessory"]
        own = 0 < acc < len(ACCESSORIES) and ACCESSORIES[acc] in face["replaces"]
        return 0 if own and not any(aux_row(dec, mood)) else acc

    if cue(pa, a) != cue(pb, b):
        score += 1
    xa, xb = aux_row(dec, a), aux_row(dec, b)
    for part in dec["parts"]:
        if part_shown(part, xa) != part_shown(part, xb):
            score += 0.5
        elif part_shown(part, xa):
            moves = [abs(xa[d["aux"]] - xb[d["aux"]]) * abs(d.get("scale", 16)) / 16
                     for d in part.get("drivers", []) if d["op"] in ("move_x", "move_y")]
            score += max(moves, default=0) / 24
    return score + abs(luma(body_rgb(dec, a)) - luma(body_rgb(dec, b))) * 4


def lint(dec, size, others):
    """(errors, warnings) for one decoded pack (see lint_input); `others` are the other pets'."""
    errors, warnings = [], []
    if size > MAX_LINT_SIZE:
        warnings.append(f"the pack is {size} bytes; keep a pet under {MAX_LINT_SIZE}")
    full = shapes(dec, "idle")
    outside = []
    for shape, pts, part in full:
        x0, y0, x1, y1 = bounds(shape, pts, part)
        if (x0 < BOX[0] or y0 < BOX[1] or x1 > BOX[2] or y1 > BOX[3]) and part["label"] not in outside:
            outside.append(part["label"])
    if outside:
        verb = "reaches" if len(outside) == 1 else "reach"
        warnings.append(f"{', '.join(outside)} {verb} outside the 220x190 mockup box")

    for i, a in enumerate(MOODS):
        for b in MOODS[i + 1:]:
            d = mood_diff(dec, a, b)
            if d < MOOD_MIN_DIFF:
                errors.append(f"{a} and {b} look the same without colour (difference {d:.2f})")
            elif d < MOOD_WARN_DIFF:
                warnings.append(f"{a} and {b} differ little without colour (difference {d:.2f})")

    cells = raster(full, 2)
    counts = {}
    for n in cells.values():
        counts[n] = counts.get(n, 0) + 1
    seen = set()
    for mood in MOODS:
        body = body_rgb(dec, mood)
        shares = {}
        for n, c in counts.items():
            role = full[n][2]["role"]
            shares[role] = shares.get(role, 0) + c / len(cells)
        for role, share in shares.items():
            if share < HUE_MIN_SHARE:
                continue
            c = role_rgb(dec, role, body)
            hue, de = min(((k, delta_e(c, v)) for k, v in SEMANTIC.items()), key=lambda kv: kv[1])
            key = (role, c)
            if de >= HUE_WARN_DE or key in seen:
                continue
            seen.add(key)
            text = f"{role} #{c:06X} ({mood}, {share:.0%} of the pet) is {de:.1f} from the UI's {hue}"
            (errors if de < HUE_MIN_DE else warnings).append(text)

    mask = night_mask(dec)
    for other in others:
        om = night_mask(other)
        iou = len(mask & om) / max(1, len(mask | om))
        if iou >= SILHOUETTE_MAX_IOU:
            errors.append(f"the night outline is {iou:.0%} the same as {other['id']}'s")
        elif iou >= SILHOUETTE_WARN_IOU:
            warnings.append(f"the night outline is {iou:.0%} the same as {other['id']}'s")
    return errors, warnings


# ---- built-ins -------------------------------------------------------------------

def pet_sources(pets_dir):
    """Every pets/<id>.pet.json; names starting with _ (the template) are not pets."""
    return sorted(os.path.join(pets_dir, f) for f in os.listdir(pets_dir)
                  if f.endswith(".pet.json") and not f.startswith("_"))


def builtin_ids(pets_dir):
    with open(os.path.join(pets_dir, "builtin.txt"), encoding="utf-8") as fh:
        ids = [line.split("#")[0].strip() for line in fh]
    ids = [i for i in ids if i]
    if not ids or len(set(ids)) != len(ids):
        fail("builtin.txt must list at least one id, each once")
    return ids


def load_source(path):
    src = load(path)
    want = os.path.basename(path)[:-len(".pet.json")]
    if src.get("id") != want:
        fail(f"{path}: id {src.get('id')!r} does not match the file name")
    return src


def builtins_c(pets_dir):
    """The generated header and source with every pack of pets/builtin.txt, the first the default."""
    ids = builtin_ids(pets_dir)
    head = "/* Generated by firmware/tools/petpack.py builtins from pets/builtin.txt; do not edit. */\n"
    h = head + "#pragma once\n\n#define PET_BUILTIN_COUNT    %d\n#define PET_BUILTIN_DEFAULT  \"%s\"\n" % (len(ids), ids[0])
    c = [head + '#include "pet_builtin.h"', ""]
    taglines = {}
    for pid in ids:
        src = load_source(os.path.join(pets_dir, pid + ".pet.json"))
        pack = encode(src)
        taglines[pid] = src.get("tagline", "").replace("\\", "\\\\").replace('"', '\\"')
        c.append("static const uint8_t PACK_%s[%d] = {" % (pid.upper(), len(pack)))
        c += ["    " + " ".join("0x%02x," % b for b in pack[i:i + 12]) for i in range(0, len(pack), 12)]
        c += ["};", ""]
    c.append("const pet_builtin_t pet_builtins[PET_BUILTIN_COUNT] = {")
    c += ['    {"%s", PACK_%s, sizeof(PACK_%s), "%s"},' % (pid, pid.upper(), pid.upper(), taglines[pid]) for pid in ids]
    c += ["};", ""]
    return h, "\n".join(c)


def write_if_changed(path, text):
    try:
        with open(path, encoding="utf-8") as fh:
            if fh.read() == text:
                return
    except FileNotFoundError:
        pass
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def check(path):
    """Encode, validate, compare with golden/<id>.pet when it exists, lint against the other pets."""
    src = load_source(path)
    pack = encode(src)
    dec = lint_input(src)
    crc = "%08x" % zlib.crc32(pack[:-4])
    pets_dir = os.path.dirname(os.path.abspath(path))
    golden = os.path.join(pets_dir, "golden", src["id"] + ".pet")
    if os.path.exists(golden):
        with open(golden, "rb") as fh:
            if fh.read() != pack:
                fail(f"{golden} differs from the encoded source (run petpack.py golden)")
        with open(golden[:-4] + ".crc32", encoding="ascii") as fh:
            if fh.read().strip() != crc:
                fail(f"{golden[:-4]}.crc32 is not {crc}")
    others = [lint_input(load(p)) for p in pet_sources(pets_dir)
              if os.path.abspath(p) != os.path.abspath(path)]
    errors, warnings = lint(dec, len(pack), [o for o in others if o["id"] != src["id"]])
    return {"id": src["id"], "size": len(pack), "crc32": crc, "golden": os.path.exists(golden),
            "ok": not errors, "errors": errors, "warnings": warnings}


# ---- commands ------------------------------------------------------------------

def load(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    cmd, args = argv[1], argv[2:]
    try:
        if cmd == "encode" and len(args) == 2:
            src = load(args[0])
            pack = encode(src)
            with open(args[1], "wb") as fh:
                fh.write(pack)
            print(json.dumps({"id": src["id"], "size": len(pack), "crc32": "%08x" % zlib.crc32(pack[:-4])}))
        elif cmd == "golden":
            for path in args:
                src = load_source(path)
                pack = encode(src)
                base = os.path.join(os.path.dirname(os.path.abspath(path)), "golden", src["id"])
                with open(base + ".pet", "wb") as fh:
                    fh.write(pack)
                with open(base + ".crc32", "w", encoding="ascii") as fh:
                    fh.write("%08x\n" % zlib.crc32(pack[:-4]))
                print(json.dumps({"id": src["id"], "size": len(pack), "crc32": "%08x" % zlib.crc32(pack[:-4])}))
        elif cmd == "decode" and len(args) == 1:
            with open(args[0], "rb") as fh:
                print(json.dumps(decode(fh.read()), indent=1))
        elif cmd == "builtins" and len(args) == 2:
            h, c = builtins_c(args[0])
            os.makedirs(args[1], exist_ok=True)
            write_if_changed(os.path.join(args[1], "pet_builtins.h"), h)
            write_if_changed(os.path.join(args[1], "pet_builtins.c"), c)
        elif cmd == "check":
            ok = True
            for path in args:
                result = check(path)
                ok = ok and result["ok"]
                print(json.dumps(result))
                for e in result["errors"]:
                    print(f"petpack: {path}: {e}", file=sys.stderr)
                for w in result["warnings"]:
                    print(f"petpack: {path}: warning: {w}", file=sys.stderr)
            return 0 if ok else 1
        elif cmd == "poses" and len(args) == 1:
            dec = decode(encode(load(args[0])))
            print(json.dumps({m: mood_pose(dec, m) for m in MOODS}))
        else:
            print(__doc__.strip(), file=sys.stderr)
            return 2
    except (ValueError, KeyError, IndexError, OSError) as e:
        print(f"petpack: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
