"""Pet rig for the design mockups only (v2): it draws Devo and Ember by hand
for pet-moods.html and the screen renders, and nothing else reads it. The
preview of any pet from its pets/<id>.pet.json is `pnpm run pet-sheet`.
The shared pose table mirrors firmware/src/components/core/pet_pose.c; the four
everyday moods (focused, working, thinking, idle) are re-tuned for warmth.
See pet-animations.md."""
import math

INK = "#0C0E14"
PET_INK = "#1A1D26"
WHITE = "#FFFFFF"
MOODS = ["focused", "working", "thinking", "idle", "happy", "sad", "anxious", "excited", "proud", "angry", "sleeping"]


def mix(a, b, t):
    a, b = a.lstrip("#"), b.lstrip("#")
    return "#" + "".join("%02X" % round(int(a[i:i + 2], 16) * (1 - t) + int(b[i:i + 2], 16) * t) for i in (0, 2, 4))


# ---- palettes ----------------------------------------------------------------
# Devo owns eleven mood colours. None of them sits on a semantic UI hue
# (mint, amber, coral, sky): working is lime, angry crimson, focused cobalt, excited pink.
DEVO_COLOR = {
    "focused": "#5B82F0", "working": "#B4D455", "thinking": "#9A8AE0", "idle": "#F0C594",
    "happy": "#FFB84D", "sad": "#7E9BC8", "anxious": "#C4A0D8", "excited": "#FF8DB4",
    "proud": "#3CBFB4", "angry": "#D4366B", "sleeping": "#7377CF",
}
MOOD_COLOR = DEVO_COLOR  # kept for the builder

# Ember owns one hue (jade) and an ember-orange accent; a mood only shifts the jade.
JADE, EMBER_ACCENT, EMBER_CREAM = "#2FAE9C", "#FF8A3D", "#F6E7C1"
EMBER_SHIFT = {
    "focused": (PET_INK, .08), "working": (WHITE, 0), "thinking": ("#7377CF", .16), "idle": (WHITE, 0),
    "happy": (WHITE, .12), "sad": ("#8FA3A8", .42), "anxious": ("#C9D6D2", .34), "excited": (WHITE, .18),
    "proud": (WHITE, .07), "angry": (PET_INK, .14), "sleeping": ("#3F5A8C", .34),
}


def pet_color(kind, mood):
    if kind == "devo":
        return DEVO_COLOR[mood]
    to, t = EMBER_SHIFT[mood]
    return mix(JADE, to, t)


# ---- shared pose table --------------------------------------------------------
# eye_open, eye_size, pupil_size, pupil_dx, pupil_dy, brow, mouth_curve, mouth_open, accessory
# brow: one angle for both eyes, or (left, right, left lift px, right lift px).
POSE = {
    "happy": dict(eo=100, es=26, ps=45, dx=0, dy=0, brow=-5, curve=80, opened=0, acc=None),
    "sad": dict(eo=65, es=24, ps=45, dx=0, dy=40, brow=-20, curve=-70, opened=0, acc="tear"),
    "excited": dict(eo=100, es=32, ps=40, dx=0, dy=0, brow=-12, curve=100, opened=60, acc="sparkles"),
    "anxious": dict(eo=100, es=28, ps=22, dx=0, dy=0, brow=-18, curve=-25, opened=15, acc="sweat"),
    "sleeping": dict(eo=0, es=26, ps=45, dx=0, dy=0, brow=0, curve=15, opened=0, acc="zzz"),
    "angry": dict(eo=60, es=26, ps=50, dx=0, dy=0, brow=28, curve=-55, opened=0, acc=None),
    "proud": dict(eo=65, es=26, ps=45, dx=0, dy=-45, brow=-8, curve=65, opened=0, acc="sparkles"),
    # re-tuned in phase 2 (were: flat mouths and inward brows)
    "idle": dict(eo=100, es=25, ps=46, dx=0, dy=0, brow=0, curve=42, opened=0, acc=None),
    "focused": dict(eo=62, es=26, ps=52, dx=0, dy=20, brow=0, curve=16, opened=0, acc=None, lid=0, mw=11),
    "working": dict(eo=88, es=24, ps=46, dx=0, dy=45, brow=0, curve=34, opened=0, acc=None, mw=13, tongue=1),
    "thinking": dict(eo=92, es=24, ps=45, dx=60, dy=-60, brow=(-4, -16, 0, 5), curve=-4, opened=0, acc="dots", mw=8, mdx=9),
}

# Devo aux channels: antenna tip dx, tip dy (0 = upright), stem style, cheeks
DEVO_AUX = {
    "happy": (12, 0, "curve", 1), "sad": (30, 26, "curve", 0), "excited": (0, -6, "curve", 1),
    "anxious": (6, 4, "zig", 0), "sleeping": (26, 20, "curve", 1), "angry": (-10, 2, "zig", 0),
    "proud": (4, -6, "curve", 1), "idle": (10, 3, "curve", 1), "focused": (0, -4, "straight", 0),
    "working": (-14, 2, "curve", 0), "thinking": (14, 0, "hook", 0),
}
# Ember aux channels: wing lift (px), tail curl (px), nostril smoke, flame
EMBER_AUX = {
    "happy": (10, 6, 0, 0), "sad": (-20, -12, 0, 0), "excited": (24, 12, 0, 1), "anxious": (-8, -4, 0, 0),
    "sleeping": (-22, -16, 1, 0), "angry": (22, 10, 2, 0), "proud": (16, 10, 0, 0), "idle": (0, 0, 0, 0),
    "focused": (-6, 0, 0, 0), "working": (4, 4, 0, 0), "thinking": (0, -8, 0, 0),
}
# Ember face overrides: lid slant replaces brows; a dragon squints where Devo smiles
EMBER_POSE = {
    "idle": dict(curve=30), "focused": dict(eo=50, lid=10, dy=0), "working": dict(eo=85, lid=0, dy=40),
    "thinking": dict(eo=80, lid=-8), "happy": dict(eo=100, curve=70), "sad": dict(eo=60, lid=-22),
    "anxious": dict(eo=100, ps=20), "excited": dict(eo=100, ps=62, opened=70), "proud": dict(eo=55, lid=-6),
    "angry": dict(eo=58, lid=30), "sleeping": dict(),
}

REACT = {
    # pose override, eye style, accessory, body squish px, hop px
    "pat": (dict(curve=90, opened=0, brow=0, tongue=0, mdx=0, mw=None), "arc", "hearts", 0, 0),
    "poke": (dict(eo=25, curve=-20, opened=0, brow=-14, lid=None, tongue=0, mdx=0), None, "impact", 12, 0),
    "dizzy": (dict(curve=-10, opened=20, brow=0, tongue=0, mdx=0), "spiral", None, 0, 0),
    "wake": (dict(eo=38, curve=0, opened=70, brow=-16, es=26, lid=None), None, "zzz1", -8, 0),
    "attention": (dict(eo=100, es=30, ps=34, dy=-30, dx=0, curve=0, opened=22, brow=-14, lid=None, tongue=0, mdx=0), None, "bang", 0, 6),
}


# ---- face ---------------------------------------------------------------------
def _eye(x, y, p, look, style, night, sx, face, col, dark):
    eo, w, ps = p["eo"], p["es"], p["ps"]
    ink = night or PET_INK
    if style == "arc":
        return f'<path d="M{x - w / 2},{y + 4} Q{x},{y - 12} {x + w / 2},{y + 4}" fill="none" stroke="{ink}" stroke-width="4" stroke-linecap="round"/>'
    if style == "spiral":
        pts = []
        for i in range(40):
            ang = i / 39 * 4.2 * math.pi
            r = 1 + i / 39 * (w / 2 - 1)
            pts.append(f"{x + r * math.cos(ang):.1f},{y + r * math.sin(ang):.1f}")
        return f'<polyline points="{" ".join(pts)}" fill="none" stroke="{ink}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>'
    if eo < 15:
        return f'<path d="M{x - w / 2},{y} Q{x},{y + 9} {x + w / 2},{y}" fill="none" stroke="{ink}" stroke-width="{2 if night else 4}" stroke-linecap="round"/>'
    lid = p.get("lid")
    hf = w * 1.2
    h = hf if lid is not None else max(4, hf * eo / 100)
    rx = min(w, h) / 2
    if night:
        hh = max(4, hf * eo / 100)
        return f'<rect x="{x - w / 2}" y="{y + hf / 2 - hh:.1f}" width="{w}" height="{hh:.1f}" rx="{min(w, hh) / 2:.1f}" fill="none" stroke="{night}" stroke-width="2"/>'
    cid = f"c{abs(hash((x, y, eo, w, ps, lid))) % 999999}"
    s = f'<clipPath id="{cid}"><rect x="{x - w / 2}" y="{y - h / 2:.1f}" width="{w}" height="{h:.1f}" rx="{rx:.1f}"/></clipPath>'
    s += f'<rect x="{x - w / 2}" y="{y - h / 2:.1f}" width="{w}" height="{h:.1f}" rx="{rx:.1f}" fill="{face["sclera"]}"/>'
    vis_top = y - h / 2 if lid is None else y + hf / 2 - hf * eo / 100  # where the visible part starts
    vis_cy = (vis_top + y + h / 2) / 2
    vis_h = y + h / 2 - vis_top
    if face["eye"] == "slit":
        pw = max(3.5, w * ps / 100 * 0.46)
        ph = min(vis_h - 4, w * 0.86)
        tx = (w / 2 - pw / 2 - 2) * (p["dx"] + look[0]) / 100
        ty = max(0, (vis_h - ph) / 2 - 1) * (p["dy"] + look[1]) / 100
        s += (f'<g clip-path="url(#{cid})"><rect x="{x + tx - pw / 2:.1f}" y="{vis_cy + ty - ph / 2:.1f}" width="{pw:.1f}" height="{ph:.1f}" rx="{pw / 2:.1f}" fill="{PET_INK}"/>'
              f'<circle cx="{x + tx + pw * .2 + 2:.1f}" cy="{vis_cy + ty - ph * .26:.1f}" r="1.6" fill="{WHITE}"/></g>')
    else:
        pr = w * ps / 100 / 2
        tx = (w / 2 - pr - 2) * (p["dx"] + look[0]) / 100
        ty = max(0, (vis_h / 2 - pr - 2)) * (p["dy"] + look[1]) / 100
        s += (f'<g clip-path="url(#{cid})"><circle cx="{x + tx:.1f}" cy="{vis_cy + ty:.1f}" r="{pr:.1f}" fill="{PET_INK}"/>'
              f'<circle cx="{x + tx + pr * .35:.1f}" cy="{vis_cy + ty - pr * .35:.1f}" r="{max(1.5, pr * .28):.1f}" fill="{WHITE}"/></g>')
    if lid is not None:  # a lid in the body colour with a dark edge; slant = lid angle
        k = math.tan(math.radians(lid)) * w / 2
        xi, xo = x - sx * (w / 2 + 1), x + sx * (w / 2 + 1)  # inner (towards the nose), outer
        yi, yo = vis_top + k, vis_top - k
        topy = y - hf / 2 - 3
        s += f'<polygon points="{xi},{topy} {xo},{topy} {xo},{yo:.1f} {xi},{yi:.1f}" fill="{col}"/>'
        s += f'<line x1="{xi}" y1="{yi:.1f}" x2="{xo}" y2="{yo:.1f}" stroke="{dark}" stroke-width="4" stroke-linecap="round"/>'
    return s


def _brow(x, y, es, angle, lift, side, ink):
    if abs(angle) < 4:
        return ""
    ang = math.radians(angle) * side
    by = y - es * 0.6 - 9 - lift
    dx, dy = 11 * math.cos(ang), 11 * math.sin(ang)
    return f'<line x1="{x - dx:.1f}" y1="{by - dy:.1f}" x2="{x + dx:.1f}" y2="{by + dy:.1f}" stroke="{ink}" stroke-width="4" stroke-linecap="round"/>'


def _mouth(y, p, ink, hw, sagk, night=False):
    curve, opened = p["curve"], p["opened"]
    hw = p.get("mw") or hw
    cx = p.get("mdx", 0) or 0
    sw = 2 if night else 4
    sag = curve * sagk / 100
    y0 = y - sag / 2
    if opened > 5:
        if curve > 40:
            hh = 8 + opened * 0.16
            if night:
                return f'<path d="M{-hw},{y0 - 2} Q0,{y0 + 2 * hh} {hw},{y0 - 2} Z" fill="none" stroke="{ink}" stroke-width="2" stroke-linejoin="round"/>'
            return (f'<path d="M{-hw},{y0 - 2} Q0,{y0 + 2 * hh} {hw},{y0 - 2} Z" fill="{ink}" stroke="{ink}" stroke-width="4" stroke-linejoin="round"/>'
                    f'<ellipse cx="0" cy="{y0 + hh - 1}" rx="7" ry="3.5" fill="#FF8A80"/>')
        mh = 6 + opened * 0.2
        return f'<rect x="{-(7 + opened * .05)}" y="{y - mh / 2:.1f}" width="{14 + opened * .1}" height="{mh:.1f}" rx="{min(7, mh / 2):.1f}" fill="{ink}"/>'
    s = f'<path d="M{cx - hw},{y0:.1f} Q{cx},{y0 + 2 * sag:.1f} {cx + hw},{y0:.1f}" fill="none" stroke="{ink}" stroke-width="{sw}" stroke-linecap="round"/>'
    if p.get("tongue") and not night:  # tip of the tongue at the right corner: "concentrating"
        s += f'<rect x="{cx + hw - 9}" y="{y0 + sag * .9 - 1:.1f}" width="10" height="9" rx="4.5" fill="#FF8A80"/>'
        s += f'<path d="M{cx},{y0 + sag:.1f} Q{cx + hw * .6},{y0 + sag * .95:.1f} {cx + hw},{y0:.1f}" fill="none" stroke="{ink}" stroke-width="4" stroke-linecap="round"/>'
    return s


def _star(x, y, r, c):
    k = r * 0.28
    return f'<path d="M{x},{y - r} L{x + k},{y - k} L{x + r},{y} L{x + k},{y + k} L{x},{y + r} L{x - k},{y + k} L{x - r},{y} L{x - k},{y - k} Z" fill="{c}"/>'


def _drop(x, y, s, c):
    return f'<path d="M{x},{y - s} C{x + s * .9},{y} {x + s * .7},{y + s} {x},{y + s} C{x - s * .7},{y + s} {x - s * .9},{y} {x},{y - s} Z" fill="{c}"/>'


def _zzz(c, sw=3):
    return "".join(
        f'<path d="M{x},{y} h{s} l{-s},{s} h{s}" fill="none" stroke="{c}" stroke-width="{sw}" stroke-linecap="round" stroke-linejoin="round"/>'
        for x, y, s in ((62, -52, 8), (76, -70, 10), (90, -92, 13)))


def _accessory(acc, kind, eye_y, ex):
    if acc == "zzz":
        if kind == "ember":  # smoke rings instead of letters
            return "".join(f'<circle cx="{58 + i * 13}" cy="{-40 - i * 19}" r="{4 + i * 2.5}" fill="none" stroke="#A7AEBB" stroke-width="2.5"/>' for i in range(3))
        return _zzz("#A7AEBB")
    if acc == "sweat":
        return _drop(64 if kind == "devo" else 62, -46 if kind == "devo" else -64, 8, "#B3E5FC")
    if acc == "tear":
        return _drop(-ex, eye_y + 26, 6, "#B3E5FC")
    if acc == "sparkles":
        c = "#FFE9A8" if kind == "devo" else "#FFB454"
        return _star(-88, -44, 9, c) + _star(88, -58, 11, c) + _star(96, 0, 6, c)
    if acc == "dots":
        return "".join(f'<circle cx="{64 + i * 13}" cy="{-66 - i * 9}" r="{3 + i}" fill="#F3EEE3"/>' for i in range(3))
    if acc == "bang":
        return ('<circle cx="80" cy="-62" r="15" fill="#FFC53D"/>'
                f'<rect x="77.5" y="-72" width="5" height="13" rx="2.5" fill="{INK}"/><circle cx="80" cy="-54" r="3" fill="{INK}"/>')
    if acc == "hearts":
        h = lambda x, y, s: f'<path d="M{x},{y + s} C{x - s * 1.6},{y - s * .2} {x - s * .6},{y - s * 1.3} {x},{y - s * .3} C{x + s * .6},{y - s * 1.3} {x + s * 1.6},{y - s * .2} {x},{y + s} Z" fill="#FF8FA3"/>'
        return h(78, -62, 8) + h(96, -38, 5)
    if acc == "impact":
        return "".join(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#F3EEE3" stroke-width="3" stroke-linecap="round"/>'
                       for x1, y1, x2, y2 in ((-84, -34, -96, -44), (-90, -14, -104, -14), (84, -34, 96, -44), (90, -14, 104, -14)))
    return ""


FACE = {
    "devo": dict(eye="round", sclera=WHITE, brows=True, ex=27, eye_y=-8, mouth_y=30, hw=18, sagk=11),
    "ember": dict(eye="slit", sclera="#FFF3C4", brows=False, ex=27, eye_y=-43, mouth_y=-7, hw=15, sagk=7),
}


def pet_svg(kind="devo", mood="idle", react=None, scale=1.0, look=(0, 0), night=None, shadow=True, w=220, h=190, gray=False):
    """Returns an <svg> of w*scale x h*scale; the body centre is at the svg centre + 12 px down."""
    face = FACE[kind]
    p = dict(POSE[mood])
    if kind == "ember":
        p.pop("tongue", None), p.pop("mdx", None), p.pop("mw", None)
        p["lid"] = None
        p.update(EMBER_POSE[mood])
        if p.get("lid") is None and p["eo"] < 100 and p["eo"] >= 15:
            p["lid"] = 0
    style, squish, hop, acc = None, 0, 0, p["acc"]
    if react:
        o, style, acc, squish, hop = REACT[react]
        p.update(o)
        if kind == "ember" and p.get("lid") is None and 15 <= p["eo"] < 100:
            p["lid"] = -6
    col = pet_color(kind, mood)
    if react == "attention" and mood in ("idle", "sleeping"):
        col = pet_color(kind, "happy")
    dark, light = mix(col, PET_INK, 0.28), mix(col, WHITE, 0.4)
    stroke_only = night is not None
    fill = (lambda c: f'fill="#000000" stroke="{night}" stroke-width="2" stroke-linejoin="round"') if stroke_only else (lambda c: f'fill="{c}"')
    out = []
    if mood == "sleeping" and not react:
        squish = 10 if kind == "devo" else 7

    if kind == "devo":
        bw, bh = 140, 116
        top = -bh / 2 + squish - hop
        eye_y = face["eye_y"] + squish * 0.6 - hop
        mouth_y = face["mouth_y"] + squish * 0.3 - hop
        if shadow and not stroke_only:
            out.append(f'<rect x="{-62 + hop * 2}" y="{bh / 2 + 4}" width="{124 - hop * 4}" height="12" rx="6" fill="#161A22"/>')
        adx, ady, astyle, cheeks = DEVO_AUX[mood]
        if react == "attention":
            adx, ady, astyle = 0, -2, "straight"
        if react == "poke":
            adx, ady, astyle = -18, 6, "zig"
        if react == "dizzy":
            adx, ady, astyle = 20, 8, "zig"
        if react == "pat":
            adx, ady, astyle, cheeks = 14, 2, "curve", 1
        if react == "wake":
            adx, ady, astyle = 6, -4, "curve"
        for sx in (-1, 1):
            out.append(f'<rect x="{sx * 32 - 15}" y="{bh / 2 - 9}" width="30" height="16" rx="8" {fill(dark)}/>')
        bx, by = 0, top + 3
        tx, ty = adx, top - 26 + ady
        ac = night or dark
        if astyle == "zig":
            d = f"M{bx},{by} L{bx + 7},{by - 9} L{bx - 5},{by - 17} L{tx},{ty}"
        elif astyle == "hook":
            d = f"M{bx},{by} C{bx},{by - 16} {tx + 12},{ty + 16} {tx + 10},{ty + 2} C{tx + 8},{ty - 10} {tx - 8},{ty - 8} {tx - 6},{ty + 4}"
            tx, ty = tx - 6, ty + 4
        elif astyle == "straight":
            d = f"M{bx},{by} L{tx},{ty}"
        else:
            d = f"M{bx},{by} Q{bx},{(by + ty) / 2 - 6} {tx},{ty}"
        out.append(f'<path d="{d}" fill="none" stroke="{ac}" stroke-width="{2 if stroke_only else 4}" stroke-linecap="round" stroke-linejoin="round"/>')
        tip = "#FFC53D" if react == "attention" else light
        if react == "attention":
            out.append(f'<circle cx="{tx}" cy="{ty}" r="11.5" fill="none" stroke="#FFC53D" stroke-width="2"/>')
        out.append(f'<circle cx="{tx}" cy="{ty}" r="7" {fill(tip)}/>')
        out.append(f'<rect x="{-bw / 2}" y="{top}" width="{bw}" height="{bh - squish}" rx="46" {fill(col)}/>')
        if not stroke_only:
            out.append(f'<rect x="{-bw / 2 + 22}" y="{top + 12}" width="30" height="8" rx="4" fill="{light}"/>')
            if cheeks:
                ck = mix(col, "#FF6B5E", 0.42)
                for sx in (-1, 1):
                    out.append(f'<rect x="{sx * 48 - 9}" y="{eye_y + 19}" width="18" height="10" rx="5" fill="{ck}"/>')
    else:
        wing, tail, smoke, flame = EMBER_AUX[mood]
        if react == "attention":
            wing, tail = 22, 10
        if react == "poke":
            wing, tail = -16, -6
        if react == "pat":
            wing, tail = 12, 12
        if react == "wake":
            wing, tail = 8, -4
        acc_col = "#FF6A33" if mood == "angry" and not react else EMBER_ACCENT
        acc_dark = mix(acc_col, PET_INK, 0.25)
        cream = EMBER_CREAM
        hy = squish - hop  # head shift
        byy = -hop
        eye_y = face["eye_y"] + hy
        mouth_y = face["mouth_y"] + hy
        if shadow and not stroke_only:
            out.append(f'<rect x="{-56 + hop * 2}" y="60" width="{112 - hop * 4}" height="12" rx="6" fill="#161A22"/>')
        # tail with its spade attached to the end of the line
        ex_, ey_ = 99, 40 - tail * .8 + byy
        out.append(f'<path d="M26,{48 + byy} C58,{76 + byy} 106,{78 + byy} {ex_},{ey_ + 8}" fill="none" stroke="{night or dark}" stroke-width="{2 if stroke_only else 10}" stroke-linecap="round"/>')
        out.append(f'<path d="M{ex_ - 12},{ey_ + 8} L{ex_ - 1},{ey_ - 16} L{ex_ + 12},{ey_ + 8} Z" {fill(acc_col)} stroke-linejoin="round"/>')
        # wings (behind the body): orange membrane, dark arm
        for sx in (-1, 1):
            pts = [(38, 2), (80, -16 - wing * .8), (77, 0 - wing * .4), (88, 8 - wing * .25), (74, 14), (78, 26), (40, 22)]
            out.append(f'<polygon points="{" ".join(f"{sx * a_},{b_ + byy:.1f}" for a_, b_ in pts)}" {fill(acc_col)} stroke-linejoin="round"/>')
            if not stroke_only:
                out.append(f'<line x1="{sx * 38}" y1="{2 + byy}" x2="{sx * 80}" y2="{-16 - wing * .8 + byy}" stroke="{acc_dark}" stroke-width="5" stroke-linecap="round"/>')
        for sx in (-1, 1):
            out.append(f'<rect x="{sx * 24 - 14}" y="50" width="28" height="15" rx="7.5" {fill(dark)}/>')
        # belly body
        out.append(f'<rect x="-42" y="{-6 + byy}" width="84" height="64" rx="30" {fill(col)}/>')
        if not stroke_only:
            out.append(f'<rect x="-24" y="{14 + byy}" width="48" height="42" rx="20" fill="{cream}"/>')
            for yy in (30, 42):
                out.append(f'<line x1="-14" y1="{yy + byy}" x2="14" y2="{yy + byy}" stroke="{mix(cream, dark, .3)}" stroke-width="3" stroke-linecap="round"/>')
        # horns and ear fins (behind the head)
        for sx in (-1, 1):
            out.append(f'<path d="M{sx * 24},{-62 + hy} L{sx * 42},{-93 + hy} L{sx * 50},{-56 + hy} Z" {fill(cream)} stroke-linejoin="round"/>')
            fl = wing * .25
            out.append(f'<path d="M{sx * 52},{-48 + hy} L{sx * 74},{-54 + hy - fl} L{sx * 55},{-26 + hy} Z" {fill(acc_col)} stroke-linejoin="round"/>')
        # head
        out.append(f'<rect x="-58" y="{-72 + hy}" width="116" height="{78 - (squish if react == "poke" else 0)}" rx="35" {fill(col)}/>')
        if not stroke_only:
            out.append(f'<rect x="-38" y="{-62 + hy}" width="24" height="7" rx="3.5" fill="{light}"/>')
        # snout
        out.append(f'<rect x="-31" y="{-25 + hy}" width="62" height="29" rx="14.5" {fill(light)}/>')
        if not stroke_only:
            for sx in (-1, 1):
                out.append(f'<circle cx="{sx * 8}" cy="{-17 + hy}" r="2.3" fill="{dark}"/>')
        if smoke and not react and not stroke_only:
            if smoke == 2:  # angry: a puff from each nostril
                for sx in (-1, 1):
                    out.append(f'<circle cx="{sx * 44}" cy="{-12 + hy}" r="5" fill="none" stroke="#A7AEBB" stroke-width="2.5"/>'
                               f'<circle cx="{sx * 58}" cy="{-20 + hy}" r="7.5" fill="none" stroke="#A7AEBB" stroke-width="2.5"/>')

    # face
    ex = face["ex"]
    brow = p["brow"]
    bl, br, ll, lr = brow if isinstance(brow, tuple) else (brow, brow, 0, 0)
    for sx, ang, lift in ((-1, bl, ll), (1, br, lr)):
        out.append(_eye(sx * ex, eye_y, p, look, style, night, sx, face, col, dark))
        if face["brows"] and not style and not stroke_only:
            out.append(_brow(sx * ex, eye_y, p["es"], ang, lift, -sx, PET_INK))
    out.append(_mouth(mouth_y, p, night or PET_INK, face["hw"], face["sagk"], stroke_only))
    if kind == "ember" and not stroke_only:
        if p["opened"] <= 5:  # two fangs over the lip
            sag = p["curve"] * face["sagk"] / 100
            for sx in (-1, 1):
                fy = mouth_y - sag / 2 + sag * .45
                out.append(f'<path d="M{sx * 9 - 3.5},{fy:.1f} l3.5,7 l3.5,-7 Z" fill="{WHITE}"/>')
        if EMBER_AUX[mood][3] and not react:
            fy = mouth_y + 8
            out.append(f'<path d="M-9,{fy} C-18,{fy + 16} -7,{fy + 22} 0,{fy + 38} C7,{fy + 22} 18,{fy + 16} 9,{fy} Z" fill="#FF8A3D"/>')
            out.append(f'<path d="M-4,{fy + 3} C-8,{fy + 13} -3,{fy + 16} 0,{fy + 26} C3,{fy + 16} 8,{fy + 13} 4,{fy + 3} Z" fill="#FFE27A"/>')
    if acc == "zzz1":
        out.append('<path d="M66,-56 h9 l-9,9 h9" fill="none" stroke="#7C8597" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>')
    elif acc == "zzz" and stroke_only:
        out.append(_zzz(night, 2))
    elif acc and not stroke_only:
        out.append(_accessory(acc, kind, eye_y, ex))
    flt = ' style="filter:grayscale(1)"' if gray else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w * scale:.0f}" height="{h * scale:.0f}" '
            f'viewBox="{-w / 2} {-h / 2 - 12} {w} {h}"{flt}>{"".join(out)}</svg>')
