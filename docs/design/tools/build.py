#!/usr/bin/env python3
"""Generates the Devotchi phase-2 mockups (docs/design/mockups/*.html), the pet
sheet (pet-moods.html) and the gallery (index.html). Run from anywhere:
    python3 docs/design/tools/build.py
then render with tools/render-all.sh. Every mockup is a self-contained 320x240
page (inline CSS and SVG; fonts from ../fonts)."""
import html
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pet import DEVO_COLOR, EMBER_ACCENT, EMBER_CREAM, MOCHI_BASE, MOODS, REACT, pet_color, pet_svg  # noqa: E402
from pet import _source as pet_source  # noqa: E402
from PIL import ImageFont  # noqa: E402  (only to measure tab labels)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---- tokens (design-system.md) ------------------------------------------------
BG, SURF, SURF2, LINE = "#0C0E14", "#161A22", "#232936", "#343C4B"
TEXT, TEXT2, TEXT3 = "#F3EEE3", "#A7AEBB", "#7C8597"
MINT, AMBER, CORAL, SKY, VIOLET, MANGO = "#5FD38D", "#FFC53D", "#FF6B5E", "#6CC4FF", "#B592FF", "#FFB84D"
RISK = {"low": (MINT, "LOW RISK"), "med": (AMBER, "MEDIUM RISK"), "high": (CORAL, "HIGH RISK")}
NIGHT, NIGHT2 = "#9A4A26", "#6E3620"

CSS = f"""
@font-face{{font-family:Figtree;font-weight:500;src:url(FONTS/Figtree-Medium.ttf)}}
@font-face{{font-family:Figtree;font-weight:700;src:url(FONTS/Figtree-Bold.ttf)}}
@font-face{{font-family:JBMono;font-weight:500;src:url(FONTS/JetBrainsMono-Medium.ttf)}}
html,body{{margin:0;padding:0;background:#000;overflow:hidden}}
.scr{{position:relative;width:320px;height:240px;overflow:hidden;background:{BG};color:{TEXT};font:500 18px/22px Figtree;font-feature-settings:"tnum"}}
.scr *{{box-sizing:border-box}}
.a{{position:absolute}}
.t14{{font:500 14px/18px Figtree}} .t18{{font:500 18px/22px Figtree}} .t24{{font:700 24px/28px Figtree}} .t64{{font:700 64px/64px Figtree;letter-spacing:-1px}}
.mono{{font:500 14px/18px JBMono}}
.cap{{font:500 14px/18px Figtree;letter-spacing:.09em;text-transform:uppercase}}
.c{{text-align:center}} .r{{text-align:right}}
.f2{{color:{TEXT2}}} .f3{{color:{TEXT3}}}
.clamp1{{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}}
.clamp2{{display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden}}
.clamp3{{display:-webkit-box;-webkit-line-clamp:3;-webkit-box-orient:vertical;overflow:hidden}}
.row{{display:flex;align-items:center;gap:8px}}
.chip{{display:inline-flex;align-items:center;gap:5px;height:22px;padding:0 9px 0 7px;border-radius:11px;font:500 14px/18px Figtree;color:{BG};white-space:nowrap}}
.tag{{display:inline-flex;align-items:center;height:18px;padding:0 6px;border-radius:4px;font:500 14px/18px Figtree;letter-spacing:.06em}}
.well{{background:{SURF};border-radius:8px;padding:8px 10px;white-space:pre;overflow:hidden}}
.tab{{position:absolute;bottom:0;height:22px;border-radius:10px 10px 0 0;background:{SURF2};display:flex;align-items:center;justify-content:center;gap:4px;padding:0 7px;font:500 14px/18px Figtree;white-space:nowrap;transform:translateX(-50%)}}
.li{{display:flex;align-items:center;gap:8px;height:25px;padding:0 10px;border-radius:8px;margin-bottom:2px;white-space:nowrap}}
.li.sel{{background:{TEXT};color:{BG}}}
.li.sel2{{background:{TEXT};color:{BG};height:auto;padding:4px 10px 5px;display:block;white-space:normal}}
.li.grant{{background:{AMBER}}}
.li .v{{margin-left:auto;color:{TEXT2};font:500 14px/18px Figtree;display:flex;align-items:center;gap:4px}}
.li.sel .v{{color:#4B5160}}
.sub{{font:500 14px/17px Figtree;color:#4B5160;margin-top:1px}}
svg{{display:block;flex:none}}
"""

ICON = {
    "check": '<path d="M2.5 7.6 5.8 10.6 11.5 3.8"/>',
    "x": '<path d="M3.5 3.5 10.5 10.5M10.5 3.5 3.5 10.5"/>',
    "dot": '<circle cx="7" cy="7" r="3.6" fill="currentColor" stroke="none"/>',
    "terminal": '<path d="M2 4 5.5 7 2 10M7.5 10.5H12"/>',
    "chev": '<path d="M5 3 9.5 7 5 11"/>',
    "up": '<path d="M3 9 7 4.5 11 9"/>',
    "down": '<path d="M3 5 7 9.5 11 5"/>',
    "warn": '<path d="M7 2 12.8 12H1.2ZM7 6V8.4M7 10.3V10.4"/>',
    "q": '<path d="M4.5 5.3A2.5 2.5 0 1 1 8.2 7.4C7.4 7.9 7 8.4 7 9.3M7 11.6V11.7"/>',
    "shield": '<path d="M7 1.8 12 3.6V7C12 10 9.8 11.6 7 12.4 4.2 11.6 2 10 2 7V3.6Z"/>',
    "bell": '<path d="M3.5 10V6.5A3.5 3.5 0 0 1 10.5 6.5V10M2.3 10H11.7M6 12.2H8"/>',
    "sun": '<circle cx="7" cy="7" r="2.4"/><path d="M7 1.4V2.6M7 11.4V12.6M1.4 7H2.6M11.4 7H12.6M3 3 3.9 3.9M10.1 10.1 11 11M3 11 3.9 10.1M10.1 3.9 11 3"/>',
    "moon": '<path d="M11.5 8.6A5 5 0 1 1 5.4 2.5 4 4 0 0 0 11.5 8.6Z"/>',
    "pet": '<rect x="2" y="3" width="10" height="9" rx="3.5"/><path d="M5.3 7V7.6M8.7 7V7.6M7 3V1.6"/>',
    "play": '<path d="M4.5 3 11 7 4.5 11Z"/>',
    "clock": '<circle cx="7" cy="7" r="5.2"/><path d="M7 4.2V7L9 8.4"/>',
    "info": '<circle cx="7" cy="7" r="5.2"/><path d="M7 6.6V9.8M7 4.3V4.4"/>',
    "wifi": '<path d="M1.6 5.6A8 8 0 0 1 12.4 5.6M3.8 8.1A4.8 4.8 0 0 1 10.2 8.1M7 10.8V10.9"/>',
    "buzz": '<rect x="4.5" y="2.5" width="5" height="9" rx="1.5"/><path d="M1.8 5V9M12.2 5V9"/>',
    "stop": '<rect x="3.5" y="3.5" width="7" height="7" rx="1.5" fill="currentColor" stroke="none"/>',
    "bolt": '<path d="M8 1.5 3.5 8H6.8L6 12.5 10.5 6H7.2Z" fill="currentColor" stroke="none"/>',
    "pencil": '<path d="M2.5 11.5 3 9 9.5 2.5 11.5 4.5 5 11Z"/>',
    "globe": '<circle cx="7" cy="7" r="5.2"/><path d="M1.8 7H12.2M7 1.8C4.8 4.4 4.8 9.6 7 12.2 9.2 9.6 9.2 4.4 7 1.8"/>',
    "plug": '<path d="M5 1.8V4.5M9 1.8V4.5M3.5 4.5H10.5V7A3.5 3.5 0 0 1 3.5 7ZM7 10.5V12.5"/>',
    "list": '<path d="M2.5 4H3M5.5 4H11.5M2.5 7H3M5.5 7H11.5M2.5 10H3M5.5 10H11.5"/>',
    "radio0": '<circle cx="7" cy="7" r="5"/>',
    "radio1": '<circle cx="7" cy="7" r="5"/><circle cx="7" cy="7" r="2.3" fill="currentColor" stroke="none"/>',
    "box0": '<rect x="2" y="2" width="10" height="10" rx="2.5"/>',
    "box1": '<rect x="2" y="2" width="10" height="10" rx="2.5" fill="currentColor"/><path d="M4.3 7.2 6.2 9 9.8 4.9" stroke="#F3EEE3"/>',
    "box1d": '<rect x="2" y="2" width="10" height="10" rx="2.5" fill="currentColor"/><path d="M4.3 7.2 6.2 9 9.8 4.9" stroke="#0C0E14"/>',
    "desk": '<rect x="2" y="2.5" width="10" height="7" rx="1.5"/><path d="M5 12H9M7 9.5V12"/>',
    "speech": '<path d="M2 3H12V9.5H7.5L4.5 12V9.5H2Z"/>',
    "menu": '<path d="M2.5 4H11.5M2.5 7H11.5M2.5 10H11.5"/>',
    "reset": '<path d="M2.8 7A4.2 4.2 0 1 0 4.2 3.8M2.5 2V4.5H5"/>',
}


def ic(name, color="currentColor", size=14):
    return (f'<svg width="{size}" height="{size}" viewBox="0 0 14 14" fill="none" stroke="{color}" stroke-width="2" '
            f'stroke-linecap="round" stroke-linejoin="round" style="color:{color}">{ICON[name]}</svg>')


def hold(name, color):
    """Hold glyph: the click glyph inside a ring."""
    inner = ICON[name]
    return (f'<svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="{color}" stroke-linecap="round" stroke-linejoin="round" style="color:{color}">'
            f'<circle cx="8" cy="8" r="7" stroke-width="1.5"/><g transform="translate(3.8 3.8) scale(.6)" stroke-width="2.6">{inner}</g></svg>')


def a(x, y, w, h, inner="", cls="", style=""):
    hs = f"height:{h}px;" if h else ""
    ws = f"width:{w}px;" if w else ""
    return f'<div class="a {cls}" style="left:{x}px;top:{y}px;{ws}{hs}{style}">{inner}</div>'


def sb(left="14:32", chips=(), link="ok", wifi=3, batt=72, chg=False, left_color=TEXT2, demo=False, show_time=True):
    """Status bar, 20 px. left: time on home/cards, page title (caps) elsewhere."""
    o = []
    if demo:
        o.append(a(8, 1, 0, 18, f'<span class="tag" style="background:{VIOLET};color:{BG}">{demo if isinstance(demo, str) else "DEMO"}</span>'
                   + (f'<span class="cap" style="color:{TEXT2}">{left}</span>' if left else ""), "row", "gap:7px"))
    else:
        cls = "t14" if left[:1].isdigit() else "cap"
        inner = f'<span class="{cls}" style="color:{left_color}">{left}</span>'
        for c in chips:
            inner += f'<span class="tag" style="border:1.5px solid {TEXT3};color:{TEXT2};">{c}</span>'
        o.append(a(10, 1, 0, 18, inner, "row", "gap:7px"))
    # right cluster: link dot, wifi bars, battery
    lc = {"ok": MINT, "wait": AMBER, "off": CORAL}[link]
    s = f'<svg width="74" height="20" viewBox="0 0 74 20">'
    s += f'<circle cx="6" cy="10" r="3.5" fill="{lc}"/>'
    for i in range(4):
        hh = 3 + i * 2.5
        s += f'<rect x="{17 + i * 5}" y="{15 - hh}" width="3" height="{hh}" rx="1" fill="{TEXT2 if i < wifi else LINE}"/>'
    if wifi == 0:
        s += f'<path d="M16 4 L36 16" stroke="{CORAL}" stroke-width="2" stroke-linecap="round"/>'
    bc = CORAL if batt <= 15 else TEXT2
    s += f'<rect x="45.75" y="4.75" width="20.5" height="10.5" rx="3" fill="none" stroke="{bc}" stroke-width="1.5"/><rect x="67.5" y="8" width="2" height="4" rx="1" fill="{bc}"/>'
    s += f'<rect x="48" y="7" width="{max(2, 16 * batt / 100):.1f}" height="6" rx="1.5" fill="{MINT if chg else bc}"/>'
    if chg:
        s += f'<path d="M57 3 L52 10.5 H56 L55 17 L60 9.5 H56 Z" fill="{TEXT}" stroke="{BG}" stroke-width="1"/>'
    s += "</svg>"
    o.append(a(242, 0, 74, 20, s))
    return "".join(o)


GLY = {"x": ("x", CORAL), "dot": ("dot", TEXT), "check": ("check", MINT)}
_F14 = ImageFont.truetype(os.path.join(ROOT, "fonts", "Figtree-Medium.ttf"), 14)


def tw(s):
    return _F14.getlength(s)


def _tab(slot, items):
    """One button tab. items: (kind, label[, colour]).
    click  glyph + outcome                      'off'  the same, inactive (slate)
    hold   ring glyph + 'Hold: outcome' when it is the tab's only action;
           'hold outcome' (the word in fog) when it follows a click on the middle tab
    msg    ring glyph + label as given (the 'Hold to allow' nudge)"""
    base = {"l": "x", "m": "dot", "r": "check"}[slot]
    w, out = 12, ""
    for i, it in enumerate(items):
        kind, label = it[0], it[1]
        col = it[2] if len(it) > 2 else GLY[base][1]
        if kind == "off":
            out += f'{ic(base, TEXT3)}<span style="color:{TEXT3}">{label}</span>'
            w += 14 + 4 + tw(label)
        elif kind == "click":
            out += f'{ic(base, col)}<span>{label}</span>'
            w += 14 + 4 + tw(label)
        elif kind == "msg":
            out += f'{hold(base, col)}<span>{label}</span>'
            w += 16 + 4 + tw(label)
        elif i == 0:
            out += f'{hold(base, col)}<span>Hold: {label}</span>'
            w += 16 + 4 + tw("Hold: " + label)
        else:
            out += f'<span style="margin-left:5px"><span style="color:{TEXT2}">hold</span> {label}</span>'
            w += 4 + 5 + tw("hold " + label)
    return round(w + 1), out


def hints(l=None, m=None, r=None, holding=None):
    """Button tabs above REJECT (x=50), ACTION (160), APPROVE (270). A tab is centred on its
    button; a long one is pushed inwards from the screen edge but always covers its button.
    holding = (label, percent, colour): the held tab widens and fills."""
    if holding:
        label, pct, col = holding
        st = (f"left:auto;right:4px;transform:none;width:136px;height:26px;padding-top:4px;"
              f"background:linear-gradient(90deg,{col} {pct}%,{LINE} {pct}%) top/100% 4px no-repeat,{SURF2};")
        return f'<div class="tab" style="{st}">{hold("check", col)}<span>{label}</span></div>'
    box = {}
    if l:
        w, h = _tab("l", l)
        x0 = max(4, 50 - w / 2)
        box["l"] = [x0, w, h]
    if r:
        w, h = _tab("r", r)
        x1 = min(316, 270 + w / 2)
        box["r"] = [x1 - w, w, h]
    if m:
        w, h = _tab("m", m)
        x0 = 160 - w / 2
        if "l" in box:  # side tabs give way first, towards the screen edge
            box["l"][0] = max(4, min(box["l"][0], x0 - 3 - box["l"][1]))
        if "r" in box:
            box["r"][0] = min(316 - box["r"][1], max(box["r"][0], x0 + w + 3))
        if "r" in box and x0 + w + 3 > box["r"][0]:
            x0 = box["r"][0] - 3 - w
        if "l" in box and x0 < box["l"][0] + box["l"][1] + 3:
            x0 = box["l"][0] + box["l"][1] + 3
        if not (x0 <= 150 and x0 + w >= 170) or ("r" in box and x0 + w + 2 > box["r"][0]):
            print("  tab layout too tight:", l, m, r, file=sys.stderr)
        box["m"] = [x0, w, h]
    return "".join(f'<div class="tab" style="left:{x0:.0f}px;width:{w}px;padding:0;transform:none">{h}</div>' for x0, w, h in box.values())


ANY = lambda label="Any button": hints(None, [("click", label)], None)  # noqa: E731  one centred tab = all three buttons do the same


def pet(kind, mood, cx, cy, scale=1.0, **kw):
    w, h = 220, 190
    return a(round(cx - w * scale / 2), round(cy - (h / 2 + 12) * scale), 0, 0, pet_svg(kind, mood, scale=scale, **kw))


def toast(icon, color, title, sub=None, y=186):
    h = 46 if sub else 34
    inner = f'<div style="width:26px;height:26px;border-radius:13px;background:{color};display:flex;align-items:center;justify-content:center">{ic(icon, BG)}</div>'
    inner += f'<div><div class="t18 clamp1" style="max-width:236px">{title}</div>' + (f'<div class="t14 f2 clamp1" style="max-width:236px">{sub}</div>' if sub else "") + "</div>"
    return a(12, y, 296, h, inner, "row", f"background:{SURF2};border-radius:12px;padding:0 10px;gap:10px;border:1.5px solid {LINE}")


def strip(icon, color, text, right=""):
    """Receipt strip: takes the status bar's place for the first second of the next queued card."""
    inner = f'{ic(icon, color)}<span class="t14 clamp1" style="max-width:230px">{text}</span><span class="t14 f2" style="margin-left:auto">{right}</span>'
    return a(0, 0, 320, 21, inner, "row", f"background:{SURF2};padding:0 10px;gap:7px")


def countdown(frac, color=TEXT, y=208):
    return a(8, y, 304, 3, a(0, 0, round(304 * frac), 3, "", "", f"background:{color};border-radius:2px"), "", f"background:{SURF2};border-radius:2px")


def demo_frame():
    return a(0, 0, 320, 240, "", "", f"border:2px solid {VIOLET};pointer-events:none")


SCREENS = []  # (file, group, caption)


def screen(name, group, caption, body, bg=BG):
    doc = (f'<!doctype html><html><head><meta charset="utf-8"><title>Devotchi mockup: {name}</title>'
           f'<meta name="viewport" content="width=320"><style>{CSS.replace("FONTS", "../fonts")}</style></head>'
           f'<body><div class="scr" style="background:{bg}">{body}</div>'
           "<script>if(location.hash==='#2x')document.documentElement.style.zoom=2</script></body></html>")
    with open(os.path.join(ROOT, "mockups", name + ".html"), "w") as f:
        f.write(doc)
    SCREENS.append((name, group, caption))


def esc(s):
    return html.escape(s, quote=False)


def pips(n, col):
    """Risk glyph: three rising bars, n of them lit. Shape, not colour, carries the level."""
    s = '<svg width="15" height="14" viewBox="0 0 15 14">'
    for i in range(3):
        hh = 5 + i * 4
        s += f'<rect x="{i * 5.5}" y="{13.5 - hh}" width="4" height="{hh}" rx="1.2" fill="{col if i < n else LINE}"/>'
    return s + "</svg>"


# ---- screens ------------------------------------------------------------------
def home(mood, status, project=None, kind="devo", extra="", sbkw=None, react=None, look=(0, 0), cy=100):
    b = sb(**(sbkw or {}))
    b += pet(kind, mood, 160, cy, 0.92, react=react, look=look)
    if status is None:  # a toast replaces the status block while it shows
        pass
    elif project:
        b += a(0, 185, 320, 18, f'<span class="cap f3">{project}</span>', "c")
        b += a(16, 205, 288, 22, esc(status), "t18 c clamp1")
    else:
        b += a(16, 190, 288, 22, esc(status), "t18 c clamp1")
    return b + extra


HOME_TABS = lambda: hints([("click", "Poke")], [("click", "Recent"), ("hold", "Menu")], [("click", "Pat")])  # noqa: E731


def perm(risk, tool, icon, lines, claude=None, queue=None, secs=24, total=30, project="devotchi", page=None, more=None,
         reason=None, holding=None, banner=None, state="ready", receipt=None, prompt=True):
    """Permission card. The exact command or path is the first thing in the card; what Claude says
    about it comes second and is labelled as Claude's. page=(i, n); state: ready | arming | unread | nudge."""
    col, word = RISK[risk]
    n = {"low": 1, "med": 2, "high": 3}[risk]
    if receipt:
        b = strip(*receipt)
    elif banner == "sample":
        b = sb(demo="SAMPLE", left="")
    elif banner == "real":
        b = a(0, 0, 320, 21, f'{ic("bell", BG)}<span class="cap">Real request</span><span class="t14" style="margin-left:auto">not a sample</span>', "row", f"background:{TEXT};color:{BG};padding:0 10px;gap:7px")
    else:
        b = sb()
    head = f'<span class="chip" style="background:{col}">{ic(icon, BG)}{tool}</span>'
    head += f'{pips(n, col)}<span class="cap" style="color:{col};margin-left:-2px">{word}</span>'
    head += f'<span style="margin-left:auto" class="t14 f2">{queue or ""}</span>'
    b += a(8, 25, 304, 22, head, "row")
    code = esc("\n".join(lines))
    if tool == "Bash" and prompt:
        code = f'<span style="color:{TEXT3}">$ </span>' + code
    foot = ""
    if page:
        foot = (f'<div class="row t14" style="justify-content:space-between;color:{TEXT2};font-family:Figtree;margin-top:2px">'
                f'<span>{more or ""}</span><span class="mono">{page[0]}/{page[1]}</span></div>')
    hh = 16 + 18 * len(lines) + (20 if page else 0)
    b += a(8, 51, 304, hh, f"<div>{code}</div>{foot}", "well mono", f"border-left:3px solid {col};" if risk == "high" else "")
    y = 51 + hh + 6
    if reason:
        b += a(12, y, 296, 18, f'{ic("warn", col)}<span class="t14 clamp1" style="color:{col}">{reason}</span>', "row", "gap:6px")
        y += 20
    if claude:
        b += a(12, y + 2, 296, 0, f'<span class="f3">Claude:</span> “{esc(claude)}”', f"t18 f2 clamp{2 if y + 48 <= 184 else 1}")
    b += a(12, 185, 200, 18, f'{project} <span class="f3">· 3fa9c1</span>', "t14 f2 clamp1")
    b += a(240, 185, 68, 18, f"{secs} s", "t14 f2 r")
    b += countdown(secs / total, col)
    paged = page and page[1] > 1
    mid = ([("click", "Page")] if paged else []) + [("hold", "More")]
    if holding is not None:
        b += hints(holding=("Allowing…", holding, MINT))
    elif state == "arming":
        b += hints([("off", "Deny")], [("off", "Page")] if paged else None, [("off", "Allow")])
    elif state == "nudge":
        b += hints([("click", "Deny")], mid, [("msg", "Hold to allow", AMBER)])
    elif state == "unread":
        b += hints([("click", "Deny")], mid, [("off", "Allow")])
    elif risk == "high" or banner == "real":
        b += hints([("click", "Deny")], mid, [("hold", "Allow")])
    else:
        b += hints([("click", "Deny")], mid, [("click", "Allow")])
    if banner == "sample":
        b += demo_frame()
    return b


def menu_rows(rows, sel, top=50, x=8, w=304):
    """rows: (icon, label, description html, right tag, grant). The focused row expands to explain itself;
    a focused row that grants more than one call is amber instead of cream."""
    o = ""
    for i, r in enumerate(rows):
        icon, label, desc = r[0], r[1], r[2]
        tagx = r[3] if len(r) > 3 else ""
        grant = len(r) > 4 and r[4]
        if i == sel:
            tg = ""  # the right tab already says "Hold: ..." for the focused row
            o += (f'<div class="li sel2{" grant" if grant else ""}"><div class="row">{ic(icon, BG)}<span>{label}</span>{tg}</div>'
                  + (f'<div class="sub" style="padding-left:22px">{desc}</div>' if desc else "") + "</div>")
        else:
            tg = f'<span class="v">{tagx}</span>' if tagx else ""
            o += f'<div class="li">{ic(icon, TEXT2)}<span>{label}</span>{tg}</div>'
    return a(x, top, w, 0, o)


def menu(chip_col, icon, tool, context, rows, sel, right, secs=None, total=30, bar=None, mono=True, demo=False):
    b = sb(demo=True, left="") if demo else sb()
    head = f'<span class="chip" style="background:{chip_col}">{ic(icon, BG)}{tool}</span>'
    head += f'<span class="{"mono" if mono else "t14"} f2 clamp1" style="flex:1;min-width:0">{esc(context)}</span>'
    if secs is not None:
        head += f'<span class="t14 f2">{secs} s</span>'
    b += a(8, 25, 304, 22, head, "row")
    b += menu_rows(rows, sel)
    if secs is not None:
        b += countdown(secs / total, bar or chip_col)
    b += hints([("click", "Back")], [("click", "Next")], right)
    return b


def qhead(header, pos, secs, banner=None):
    b = sb(demo="SAMPLE", left="") if banner == "sample" else sb()
    head = f'<span class="chip" style="background:{SKY}">{ic("q", BG)}Question</span><span class="cap clamp1" style="color:{SKY};flex:1;min-width:0">{header}</span>'
    rt = (pos + " · " if pos else "") + f"{secs} s"
    head += f'<span class="t14 f2">{rt}</span>'
    return b + a(8, 25, 304, 22, head, "row")


def question(header, pos, text, opts, sel, multi=False, checked=(), qlines=2, secs=26, total=60, first=0, count=None,
             extra_row=None, left="Terminal", banner=None, chosen=None):
    """opts: (label, description). The focused option shows its description on up to three lines.
    The list shows whole rows only: rows first..first+count-1."""
    b = qhead(header, pos, secs, banner)
    b += a(12, 48, 296, 22 * qlines, esc(text), f"t18 clamp{qlines}")
    top = 50 + 22 * qlines
    rows = []
    for i, (label, desc) in enumerate(opts):
        on = i in checked
        if i == sel:
            g = ic("box1" if on else "box0", BG) if multi else ""
            pad = 22 if multi else 0
            rows.append(f'<div class="li sel2"><div class="row" style="align-items:flex-start">{g and f"<span style=margin-top:4px>{g}</span>"}<span class="clamp2">{esc(label)}</span></div>'
                        f'<div class="sub clamp3" style="padding-left:{pad}px">{esc(desc)}</div></div>')
        else:
            g = ic("box1d" if on else "box0", MINT if on else TEXT2) if multi else ""
            mark = f'<span class="v">{ic("check", MINT)}</span>' if chosen == i else ""
            rows.append(f'<div class="li">{g}<span class="clamp1">{esc(label)}</span>{mark}</div>')
    send_focused = extra_row is not None and sel == len(opts)
    if extra_row:
        if send_focused:
            rows.append(f'<div class="li sel" style="justify-content:center">{ic("check", BG)}<span>{extra_row}</span></div>')
        else:
            rows.append(f'<div class="li" style="justify-content:center;background:{SURF}">{ic("check", TEXT2)}<span>{extra_row}</span></div>')
    n = len(rows)
    count = n - first if count is None else count
    scrolls = count < n
    listh = 207 - top
    b += a(8, top, 296 if scrolls else 304, listh, "".join(rows[first:first + count]), "", "overflow:hidden")
    if scrolls:
        b += a(309, top, 3, listh, a(0, round(listh * first / n), 3, round(listh * count / n), "", "", f"background:{TEXT2};border-radius:2px"), "", f"background:{SURF2};border-radius:2px")
    b += countdown(secs / total, SKY, 211)
    if multi:
        r = [("click", "Send")] if send_focused else [("click", "Untick" if sel in checked else "Tick")]
    else:
        r = [("click", "Choose")]
    b += hints([("click", left)], [("click", "Next"), ("hold", "More")], r)
    if banner == "sample":
        b += demo_frame()
    return b


def settings(title, rows, sel, scroll=0, thumb=(0, .7), demo=False):
    b = sb(left=title, demo=demo) if demo else sb(left=title)
    o = ""
    for i, (icon, label, val) in enumerate(rows):
        v = f'<span class="v">{val}</span>' if val is not None else ""
        if val == ">":
            v = f'<span class="v">{ic("chev", "#4B5160" if i == sel else TEXT3)}</span>'
        o += f'<div class="li {"sel" if i == sel else ""}" style="height:29px">{ic(icon, BG if i == sel else TEXT2)}<span>{label}</span>{v}</div>'
    b += a(8, 24, 296, 186, f'<div style="position:absolute;top:{-scroll}px;width:296px">{o}</div>', "", "overflow:hidden")
    if thumb[1] < 1:
        b += a(309, 24, 3, 186, a(0, round(186 * thumb[0]), 3, round(186 * thumb[1]), "", "", f"background:{TEXT2};border-radius:2px"), "", f"background:{SURF2};border-radius:2px")
    return b


def qr_mock(px=4):
    """A version-4 style code (33 modules + 4 quiet modules each side) at exactly px per module."""
    import random
    rnd = random.Random(11)
    n = 33
    cells = [[rnd.random() < 0.47 for _ in range(n)] for _ in range(n)]

    def finder(r0, c0):
        for r in range(-1, 8):
            for c in range(-1, 8):
                if 0 <= r0 + r < n and 0 <= c0 + c < n:
                    inside = 0 <= r < 7 and 0 <= c < 7
                    cells[r0 + r][c0 + c] = inside and (r in (0, 6) or c in (0, 6) or (2 <= r <= 4 and 2 <= c <= 4))
    finder(0, 0), finder(0, n - 7), finder(n - 7, 0)
    for i in range(8, n - 8):
        cells[6][i] = cells[i][6] = i % 2 == 0
    for r in range(-2, 3):
        for c in range(-2, 3):
            cells[26 + r][26 + c] = max(abs(r), abs(c)) != 1
    size = (n + 8) * px
    s = f'<svg width="{size}" height="{size}" viewBox="0 0 {size} {size}" shape-rendering="crispEdges"><rect width="{size}" height="{size}" fill="{TEXT}"/>'
    for r in range(n):
        for c in range(n):
            if cells[r][c]:
                s += f'<rect x="{(c + 4) * px}" y="{(r + 4) * px}" width="{px}" height="{px}" fill="{BG}"/>'
    return s + "</svg>"


def wordmark(size=30, color=TEXT):
    """Lock-up: lower-case wordmark whose i carries Devo's antenna instead of a dot. One image asset on the device."""
    k = size / 30
    ant = (f'<svg width="{20 * k:.0f}" height="{22 * k:.0f}" viewBox="0 0 20 22" style="position:absolute;left:{-1.4 * k:.1f}px;top:{-8.5 * k:.1f}px">'
           f'<path d="M5.4 21 Q5.4 10 12.5 6.5" fill="none" stroke="{color}" stroke-width="3.2" stroke-linecap="round"/><circle cx="14" cy="5.4" r="4.6" fill="{MANGO}"/></svg>')
    return (f'<span style="font:700 {size}px/{size + 6}px Figtree;letter-spacing:.01em;color:{color};display:inline-block">devotch'
            f'<span style="position:relative;display:inline-block">ı{ant}</span></span>')


def dots(n, idx, m=3):
    return "".join(f'<span style="display:inline-block;width:6px;height:6px;border-radius:3px;margin:0 {m}px;background:{TEXT if i == idx else LINE}"></span>' for i in range(n))


def build():
    md = os.path.join(ROOT, "mockups")
    os.makedirs(md, exist_ok=True)
    for f in os.listdir(md):
        if f.endswith(".html"):
            os.remove(os.path.join(md, f))
    H = hints
    SET = [("hold", "Settings")]

    # --- start-up ---
    b = pet("devo", "happy", 160, 90, 0.72)
    b += a(0, 152, 320, 36, wordmark(30), "c")
    b += a(0, 190, 320, 18, "your agent's desk pet", "t14 c f2")
    b += a(0, 216, 320, 18, "v2.1.0", "mono c f3")
    screen("01-boot-splash", "Start-up", "Boot splash: the signature pet and the lock-up (the i wears Devo's antenna). No status bar, about 1.5 s.", b)

    b = sb(left="Wi-Fi setup 1/2", link="off", wifi=0)
    b += a(8, 26, 164, 164, qr_mock(4))
    b += a(184, 30, 128, 66, "Scan with your phone", "t24")
    b += a(184, 96, 128, 54, "The camera opens Wi-Fi setup for this device.", "t14 f2")
    b += a(184, 154, 128, 36, "Bluetooth is on while this shows.", "t14 f3")
    b += H(None, [("click", "No camera"), ("hold", "Settings")], None)
    screen("02-wifi-provisioning", "Start-up", "Wi-Fi setup, step 1: nothing but the code and one instruction. 4 px per module (41 modules = 164 px incl. quiet zone), drawn with lv_qrcode at an exact scale.", b)

    b = sb(left="Wi-Fi setup 2/2", link="off", wifi=0)
    b += a(12, 28, 296, 22, "Or set it up by hand", "t18")
    b += a(12, 54, 296, 18, "In the setup app, pick this device", "t14 f2")
    b += a(8, 74, 304, 34, "DEVOTCHI-3F2A", "well mono", f"color:{SKY}")
    b += a(12, 116, 296, 18, "and type this code when it asks", "t14 f2")
    b += a(8, 136, 304, 34, "7K2M 9QXD", "well mono", "letter-spacing:.14em")
    b += a(12, 178, 296, 18, "Your old network stays until a new one works.", "t14 f3")
    b += H([("click", "Back")], SET, None)
    screen("02b-wifi-provisioning-code", "Start-up", "Wi-Fi setup, step 2 (ACTION on step 1): device name and proof code in mono, for phones that cannot scan.", b)

    b = sb(left="Wi-Fi", link="off", wifi=1)
    b += pet("devo", "thinking", 160, 82, 0.6)
    b += a(0, 138, 320, 28, "Joining Wi-Fi", "t24 c")
    b += a(0, 168, 320, 18, "HomeNet-5G", "mono c", f"color:{SKY}")
    b += a(0, 190, 320, 18, "Attempt 3 · wrong password?", "t14 c f2")
    b += H(None, SET, [("click", "Set up")])
    screen("03-wifi-connecting", "Start-up", "Joining Wi-Fi, with the failure reason. After three failures APPROVE offers Wi-Fi setup (a click: the old network is kept until a new one works).", b)

    b = sb(left="Server", link="wait", wifi=3)
    b += pet("devo", "thinking", 160, 82, 0.6, look=(-120, 60))
    b += a(0, 138, 320, 28, "Finding your server", "t24 c")
    b += a(0, 168, 320, 18, "devotchi.local", "mono c", f"color:{SKY}")
    b += a(0, 194, 320, 10, dots(3, 1, 4), "c")
    b += H(None, SET, None)
    screen("04-connecting-server", "Start-up", "Connecting to the server (first connect and reconnects).", b)

    b = sb(link="off", wifi=3)
    b += pet("devo", "sad", 160, 82, 0.6)
    b += a(0, 138, 320, 28, "Server unreachable", "t24 c")
    b += a(0, 168, 320, 18, '<span class="mono">devotchi.local</span> · retry in 8 s', "t14 c f2")
    b += a(0, 190, 320, 18, "Requests go to your terminal meanwhile", "t14 c f2")
    b += H(None, SET, [("click", "Retry")])
    screen("05-offline", "Start-up", "Lost server: says what happens to requests meanwhile; retry countdown.", b)

    b = sb(left="Pairing", link="wait")
    b += a(14, 22, 190, 64, "K7M", "t64", "letter-spacing:3px")
    b += a(14, 84, 190, 64, "2QX", "t64", "letter-spacing:3px")
    b += a(214, 34, 96, 18, "Expires", "cap f3")
    b += a(214, 52, 96, 28, "4:32", "t24")
    b += a(8, 153, 304, 34, f'<span style="color:{TEXT3}">$ </span>devotchi pair K7M2QX', "well mono")
    b += a(12, 190, 296, 18, '<span class="t14 f3">Server </span>devotchi.local', "mono f2 clamp1")
    b += countdown(0.76, TEXT, 212)
    b += H(None, [("click", "New code"), ("hold", "Settings")], None)
    screen("06-pairing", "Start-up", "Pairing code in two groups of three at display size, the exact command to type, expiry, and the server host on one mono line (default port hidden, long hosts cut in the middle).", b)

    # --- home ---
    screen("10-home-idle", "Home", "Home, idle, no project label. Devo, the dark PCB ghost with its white outline and traces, idles with a smile, ball and chip light lilac. No button tabs: the pet owns the screen.", home("idle", "Waiting for an agent"))
    screen("11-home-working", "Home", "Home, working, with project label: pupils on the work, tongue out, antenna sways with the scan.", home("working", "Edit src/routes/hooks.ts", "devotchi", look=(-55, 0)))
    screen("12-home-thinking", "Home", "Home, thinking: one brow up, mouth off-centre, dots rising, chip light pulsing.", home("thinking", "Planning the settings menu", "devotchi"))
    screen("13-home-happy", "Home", "Home, happy, after a positive notification.", home("happy", "47 tests passed", "devotchi"))
    screen("14-home-angry-long", "Home", "Home, angry (crimson, not the coral of a denial), with a status line that is too long (ellipsis; it scrolls once on arrival).", home("angry", "pnpm test failed again in packages/server/test/ws.test.ts", "api-gateway"))
    screen("15-home-attention", "Home", "needs_input: the antenna ball lights amber, a badge appears, LED and haptics fire. The antenna stays inside the screen.",
           home("idle", "Claude is waiting for you", "devotchi", react="attention"))
    screen("16-home-hints-pat", "Home", "Any press on home shows the button tabs for 3 s; here APPROVE patted the pet. ACTION hold opens the quick menu.",
           home("idle", "Waiting for an agent", extra=HOME_TABS(), react="pat"))
    b = sb(left="Menu")
    qrows = [("menu", "Settings", ">"), ("play", "Showcase", ">"), ("desk", "Desk mode", "Off")]
    o = ""
    for i, (icon, label, val) in enumerate(qrows):
        v = f'<span class="v">{ic("chev", "#4B5160" if i == 0 else TEXT3) if val == ">" else val}</span>'
        o += f'<div class="li {"sel" if i == 0 else ""}" style="height:29px">{ic(icon, BG if i == 0 else TEXT2)}<span>{label}</span>{v}</div>'
    b += a(8, 24, 304, 0, o)
    b += a(12, 124, 296, 54, "Desk mode sends every request straight to your terminal. Showcase is a demo for visitors.", "t14 f2")
    b += H([("click", "Back")], [("click", "Next")], [("click", "Open")])
    screen("16b-home-quick-menu", "Home", "Quick menu (ACTION hold on home): Settings, Showcase and the desk-mode switch, two presses from anywhere.", b)
    b = home("happy", None, "devotchi", extra=toast("check", MINT, "Tests passed", "47 of 47 · packages/server"))
    screen("17-toast-positive", "Home", "Notification toast (urgency 1-2): slides up from the bottom edge, 4 s; the pet reacts above it.", b)
    b = home("sad", None, "devotchi", cy=96, extra=toast("x", CORAL, "Build failed", "tsc: 3 errors in src/ws.ts", y=170) + ANY("Any button: OK"))
    screen("18-toast-sticky", "Home", "Sticky banner (urgency 3): stays until any button is pressed (one centred tab = all three buttons). The pet steps up 4 px so the banner never covers its hem.", b)
    b = home("idle", None, sbkw={"chips": ["DESK"]}, extra=toast("desk", TEXT2, "Desk mode on", "Requests go to your terminal"))
    screen("19-desk-mode", "Home", "Desk mode: DESK chip in the status bar for as long as it is on; toast when it is switched in the quick menu.", b)

    # --- permission ---
    screen("20-perm-low", "Permission request", "Low risk. The exact command comes first, in the well; Claude's own description is second and labelled as Claude's. One page, so no Page tab.",
           perm("low", "Bash", "terminal", ["pnpm test --filter server"], "Run the server test suite"))
    screen("21-perm-med-queue", "Permission request", "Medium risk, first of three queued requests. Risk is said three ways: colour, the rising bars, the words.",
           perm("med", "Edit", "pencil", ["packages/server/src/routes/", "  hooks.ts"], "Widen the decision reply type", queue="1/3", secs=17, page=(1, 2), more="+18 −4 lines"))
    screen("22-perm-med-details", "Permission request", "Page 2/2 after an ACTION click: the change itself, paged, never scrolled.",
           perm("med", "Edit", "pencil", ['- decision: {behavior:"allow"}', "+ decision: AllowDecision", "+   | DenyDecision"], None, queue="1/3", secs=12, page=(2, 2), more="hooks.ts"))
    screen("22b-perm-long-command", "Permission request", "A command longer than one page: the first page always shows its start, and Allow stays inactive until every page has been seen.",
           perm("med", "Bash", "terminal", ["docker compose -f deploy/compose", "  .yml run --rm migrate --target", "  0042 && docker compose restart"], "Run the migration and restart the API", secs=21, page=(1, 2), more="+2 more lines", state="unread"))
    screen("23-perm-high", "Permission request", "High risk: the tab says what a hold does (Hold: Allow). The reason comes from the server's risk rule, not from Claude. Deny stays one click.",
           perm("high", "Bash", "terminal", ["git push --force origin master"], "Update the remote branch", reason="Force push: rewrites the remote branch", secs=27))
    screen("23b-perm-high-clicked", "Permission request", "A click on a hold-only tab: the tab shakes, the motor ticks, and for 1.5 s the label spells the gesture out.",
           perm("high", "Bash", "terminal", ["git push --force origin master"], "Update the remote branch", reason="Force push: rewrites the remote branch", secs=25, state="nudge"))
    screen("24-perm-high-holding", "Permission request", "Holding APPROVE: the tab names the outcome and fills over 1.2 s; releasing early cancels.",
           perm("high", "Bash", "terminal", ["git push --force origin master"], "Update the remote branch", reason="Force push: rewrites the remote branch", secs=22, holding=62))

    cmd = "pnpm test --filter server"
    rule = f'<span class="mono" style="color:{BG}">pnpm test …</span><br>Starts like this, until Claude exits.'
    rows_full = [
        ("terminal", "Send to terminal", "Decide at the keyboard instead."),
        ("shield", "Allow for this session", rule, "HOLD", True),
        ("speech", "Deny, ask why", "Claude explains what this is for, then may ask again."),
        ("stop", "Deny and stop", "Ends Claude's turn. Nothing else runs."),
    ]
    screen("25-decision-menu", "Richer decisions", "ACTION hold opens More. Focus starts on the neutral row; the focused row explains itself; the card's time keeps running in view.",
           menu(MINT, "terminal", "Bash", cmd, rows_full, 0, [("click", "Choose")], secs=19))
    screen("26-decision-menu-session", "Richer decisions", "The only granting row. The server writes the rule it will send (here a prefix rule) and the device shows it word for word in mono; the row is amber and needs a hold.",
           menu(MINT, "terminal", "Bash", cmd, rows_full, 1, [("hold", "Allow")], secs=16))
    rows_high = [rows_full[0], rows_full[2], rows_full[3]]
    screen("27-decision-menu-high", "Richer decisions", "High risk, or a rule the server finds too wide (wildcards, interpreters, paths outside the project): no granting row at all.",
           menu(CORAL, "terminal", "Bash", "git push --force origin master", rows_high, 2, [("click", "Choose")], secs=14))
    b = home("working", None, "devotchi", extra=toast("x", CORAL, "Denied", "Claude was told to try another way"))
    screen("28-decision-receipt", "Richer decisions", "Receipt toast after any decision: what was sent, in words. (The working pet is lime now, so it no longer looks like 'allowed'.)", b)
    screen("28b-queue-next", "Richer decisions", "When the queue moves on, the receipt is a strip at the top and the next card arms for 1 s (slate tabs, distinct haptic), so a double tap cannot answer it.",
           perm("low", "Bash", "terminal", ["pnpm run lint"], "Check the code style", queue="2/3", secs=30, state="arming", receipt=("x", CORAL, "Denied · next request", "")))

    # plan
    b = sb()
    head = f'<span class="chip" style="background:{SKY}">{ic("list", BG)}Plan</span><span class="cap" style="color:{SKY}">Ready to build</span><span style="margin-left:auto" class="t14 f2">1/2</span>'
    b += a(8, 25, 304, 22, head, "row")
    b += a(12, 51, 296, 22, "Add question cards to the device", "t18 clamp1")
    steps = "".join(f'<div class="row" style="align-items:flex-start;gap:8px;margin-bottom:3px"><span class="mono f3">{i}</span><span class="t14 clamp1" style="max-width:262px">{t}</span></div>'
                    for i, t in enumerate(["Widen the hook reply type in shared", "Send questions one at a time", "New question screen in firmware", "Dashboard mirror and e2e tests"], 1))
    b += a(8, 77, 304, 100, steps, "well", "white-space:normal")
    b += a(12, 185, 200, 18, 'devotchi <span class="f3">· 7 steps</span>', "t14 f2")
    b += a(240, 185, 68, 18, "48 s", "t14 f2 r")
    b += countdown(0.8, SKY)
    b += H([("click", "Revise")], [("click", "Page"), ("hold", "More")], [("off", "Approve")])
    screen("29-plan-approval", "Richer decisions", "Plan approval (ExitPlanMode). Approve keeps manual edit approval and is inactive until both pages were seen. Time starts at the room hold and grows with every press.", b)
    prow = [
        ("terminal", "Send to terminal", "Read the whole plan at the keyboard."),
        ("warn", "Approve, accept all edits", "Claude may change <b>any file</b> in this project without asking, until it exits.", "HOLD", True),
        ("stop", "Deny and stop", "Ends Claude's turn."),
    ]
    screen("29b-plan-menu", "Richer decisions", "Plan menu. The wide grant is named for what it is, drawn amber, and needs a hold. (Open question: keep it on the device at all?)",
           menu(SKY, "list", "Plan", "Add question cards to the device", prow, 1, [("hold", "Approve")], secs=41, total=60, mono=False))

    # --- questions (real-length strings) ---
    q2 = [("Vitest (Recommended)", "Already used by the server and client packages, so config and CI stay the same."),
          ("Node test runner", "Built into Node 22, no dependency, but no watch UI and weaker mocking.")]
    screen("30-question-2", "Questions", "Single choice, two options. No radio marks: the cream bar is the focus, APPROVE commits. The focused description wraps to three lines.",
           question("Test runner", None, "Which test runner should the new package use?", q2, 0))
    q3 = [("SQLite (Recommended)", "One file next to the server, no setup. Matches how rooms and tokens are stored today."),
          ("Postgres", "Scales to a hosted multi-user server, but local runs need a container and a connection string."),
          ("In memory", "Fastest to build. Every device has to pair again after a restart.")]
    screen("31-question-3", "Questions", "Three options, the second focused; first of two questions (1/2). Rows that do not fit scroll in whole rows (thumb on the right).",
           question("Storage", "1/2", "Where should paired devices be stored?", q3, 1, count=2, secs=41))
    q4 = [("Bearer token (Recommended)", "Reuses the CLI token the hooks already send. Simplest, but the token sits in localStorage."),
          ("Signed session cookie", "HttpOnly cookie set by the dashboard proxy. Safer in the browser, needs CSRF protection."),
          ("Mutual TLS", "Strongest. Every browser needs a client certificate."), ("None on the LAN", "Trust the local network.")]
    screen("32-question-4", "Questions", "Four options, second question of two: REJECT is Back (the earlier answer is kept and shown with a mint check when you return).",
           question("Auth method", "2/2", "How should the dashboard authenticate to the server?", q4, 0, left="Back", count=2, secs=52))
    longq = "The devices table gained a room_id column, so the schema changed. How should existing rooms and their paired devices be migrated when the server starts?"
    b = qhead("Migration", None, 30)
    b += a(12, 48, 296, 132, esc(longq), "t18")
    b += a(12, 186, 296, 18, "4 options follow", "t14 f2")
    b += countdown(30 / 30, SKY, 211)
    b += H([("click", "Terminal")], [("click", "Options"), ("hold", "More")], None)
    screen("33-question-read", "Questions", "A question longer than two lines opens on a read page with the whole text (paged if it is longer still). There is nothing to choose yet, so APPROVE has no tab.", b)
    qm4 = [("Migrate in place", "Run ALTER TABLE on start and backfill room_id from the token table. Keeps every pairing."),
           ("Copy to a new file", "Build devices_v2.db next to the old one. Slower, but the old file stays as a backup."),
           ("Start fresh", "Drop the table. Everyone pairs again."), ("Ask me again later", "Keep the old schema for now and skip this change.")]
    screen("33b-question-scroll", "Questions", "After the read page the question shrinks to one line and the options get the room: here rows 2-4 of 4, the focused row always complete.",
           question("Migration", None, longq, qm4, 1, qlines=1, first=1, count=3, secs=47))
    qm = [("Unit tests", "Per module, run by pnpm test. Adds about 20 cases."), ("End-to-end tests", "Server, client and the sim device together."),
          ("Docs", "protocol.md and the README."), ("Changelog", "One entry per package.")]
    screen("34-question-multi", "Questions", "Multi-select: APPROVE ticks or unticks, ACTION moves on. Ticked rows keep a mint box.",
           question("Deliver", None, "What should this change include?", qm, 1, multi=True, checked=(0, 1), qlines=1, extra_row="Send 2 selected", count=3, secs=38))
    screen("35-question-multi-send", "Questions", "The list ends in a Send row; with focus there APPROVE sends the ticked labels. With nothing ticked the row reads 'None of these'.",
           question("Deliver", None, "What should this change include?", qm, 4, multi=True, checked=(0, 1), qlines=1, extra_row="Send 2 selected", first=0, count=5, secs=33))
    qrows = [
        ("terminal", "Answer in terminal", "Moves to your keyboard. You can type your own answer there."),
        ("q", "Read the question again", "Back to the full text."),
        ("pet", "Let Claude decide", "Sends “no preference, use your judgment”."),
        ("x", "Decline to answer", "Claude is told you did not answer."),
    ]
    screen("36-question-more", "Questions", "ACTION hold on a question: the terminal path, the full text again, and two canned replies. The time left stays in view.",
           menu(SKY, "q", "Question", "Where should paired devices be stored?", qrows, 0, [("click", "Choose")], secs=44, total=60, mono=False))
    b = sb()
    b += a(8, 25, 304, 22, f'<span class="chip" style="background:{SKY}">{ic("q", BG)}Question</span><span class="cap" style="color:{SKY};flex:1">Send 2 answers?</span><span class="t14 f2">57 s</span>', "row")
    rv = ""
    for hd, ans in (("Storage", "Postgres"), ("Auth method", "Bearer token (Recommended)")):
        rv += f'<div style="background:{SURF};border-radius:8px;padding:7px 10px;margin-bottom:6px"><div class="cap f2">{hd}</div><div class="t18 clamp1">{ans}</div></div>'
    b += a(8, 53, 304, 0, rv)
    b += a(12, 180, 296, 18, "Answers cannot be changed once sent.", "t14 f2")
    b += countdown(0.95, SKY, 211)
    b += H([("click", "Back")], [("hold", "More")], [("click", "Send")])
    screen("37-question-review", "Questions", "Two or more questions end in a review card; nothing is sent before it. The middle tab says its only action is a hold.", b)
    b = home("working", None, "devotchi", extra=toast("check", SKY, "Answers sent", "Storage: Postgres · Auth: Bearer token"))
    screen("38-question-receipt", "Questions", "Receipt after sending answers (same data as the review card). 'Terminal' gives the receipt 'Moved to terminal'.", b)

    # --- clock ---
    s = f'<svg width="40" height="20" viewBox="0 0 40 20"><circle cx="6" cy="10" r="3" fill="{MINT}"/><rect x="15.75" y="4.75" width="18.5" height="10.5" rx="3" fill="none" stroke="{TEXT3}" stroke-width="1.5"/><rect x="18" y="7" width="10" height="6" rx="1.5" fill="{TEXT3}"/></svg>'
    b = a(276, 2, 40, 20, s)
    b += a(0, 22, 320, 64, "14:32", "t64 c")
    b += a(0, 92, 320, 18, "Sun 15 Nov", "cap c f2")
    b += pet("devo", "sleeping", 160, 172, 0.56)
    screen("40-clock-day", "Clock", "Clock face (day): what the screen shows in its resting (dimmed) stage whenever the agent is idle and nothing is pending. Any button or agent event returns home.", b)
    b = a(0, 22, 320, 64, "02:07", "t64 c", f"color:{NIGHT}")
    b += a(0, 92, 320, 18, "Mon 16 Nov", "cap c", f"color:{NIGHT2}")
    b += pet("devo", "sleeping", 160, 172, 0.56, night=NIGHT)
    b += a(303, 8, 6, 6, "", "", f"background:{NIGHT2};border-radius:3px")
    screen("41-clock-night", "Clock", "Clock face (night, 22:00-07:00): pure black, ember ink, outline pet, backlight 5 %.", b, bg="#000000")
    b = a(0, 22, 320, 64, "09:15", "t64 c")
    b += a(0, 92, 320, 18, "Mon 16 Nov", "cap c f2")
    b += pet("ember", "sleeping", 160, 170, 0.56)
    b += a(276, 2, 40, 20, s)
    screen("42-clock-ember", "Clock", "The same clock with the other pet: every screen takes whichever pet is selected, with its own shape and colours.", b)

    # --- settings ---
    rows = [("pet", "Pet", "Devo"), ("sun", "Display", ">"), ("clock", "Clock", ">"), ("buzz", "Haptics and LED", ">"),
            ("desk", "Desk mode", "Off"), ("wifi", "Network", ">"), ("play", "Showcase", ">"), ("info", "About", ">")]
    screen("50-settings", "Settings", "Settings: eight groups, six visible, scroll thumb. Values on the right; chevrons open a sub-list.",
           settings("Settings", rows, 1, 0, (0, .75)) + H([("click", "Back")], [("click", "Next")], [("click", "Open")]))
    rows2 = [("sun", "Brightness", "75 %"), ("clock", "Screen timeout", "30 s"), ("moon", "Deep sleep", "20 min"), ("moon", "Night dimming", "On")]
    screen("51-settings-display", "Settings", "A sub-list (Display). Rows with a value open the value editor.",
           settings("Display", rows2, 0, 0, (0, 1)) + H([("click", "Back")], [("click", "Next")], [("click", "Change")]))
    b = sb(left="Brightness")
    b += a(0, 44, 320, 64, '75<span class="t24" style="margin-left:4px">%</span>', "t64 c")
    seg = "".join(f'<span style="display:inline-block;width:62px;height:10px;border-radius:5px;margin:0 3px;background:{TEXT if i < 3 else SURF2}"></span>' for i in range(4))
    b += a(0, 126, 320, 10, seg, "c")
    b += a(0, 150, 320, 18, "25 · 50 · 75 · 100", "t14 c f2")
    b += a(0, 178, 320, 18, "The screen shows each value as you step.", "t14 c f2")
    b += H([("click", "Cancel")], [("click", "Step")], [("click", "Save")])
    screen("52-value-editor", "Settings", "Value editor: big value, stepped bar, live preview. ACTION steps, APPROVE saves, REJECT cancels.", b)
    b = sb(left="Network")
    b += a(127, 34, 66, 66, f'<div style="width:66px;height:66px;border-radius:33px;background:{CORAL};display:flex;align-items:center;justify-content:center">{ic("warn", BG, 34)}</div>')
    b += a(0, 108, 320, 28, "Factory reset?", "t24 c")
    b += a(24, 140, 272, 54, "Erases Wi-Fi, pairing and settings, then restarts into setup.", "t14 c f2")
    b += H([("click", "Cancel")], None, [("hold", "Erase", CORAL)])
    screen("53-confirm-destructive", "Settings", "Destructive confirmation (factory reset, unpair): cancel is one click; the commit is 'Hold: Erase', and its glyph and fill turn coral because it destroys.", b)

    # --- pet picker ---
    def picker(kind, name, sub, idx, current, pages=4):
        b = sb(left="Pet")
        b += pet(kind, "happy", 160, 96, 0.72)
        nm = f'<span class="t24">{name}</span>' + (f'<span class="tag" style="background:{MINT};color:{BG}">IN USE</span>' if current else "")
        b += a(0, 158, 320, 28, nm, "row", "justify-content:center;gap:8px")
        b += a(0, 186, 320, 18, sub, "t14 c f2")
        b += a(0, 205, 320, 8, dots(pages, idx), "c", "line-height:8px")
        b += H([("click", "Back")], [("click", "Next")], None if current else [("click", "Use")])
        return b
    screen("60-pet-picker", "Pets", "Pet picker: a live, animated preview per pet; the one in use is tagged. Page dots only: ACTION is the one way to move.", picker("devo", "Devo", "Built in · the Devotchi ghost", 0, True))
    screen("60b-pet-picker-mochi", "Pets", "The next built-in pet: Mochi, the soft square with an antenna. APPROVE switches.", picker("mochi", "Mochi", "Built in · a soft square with an antenna", 1, False))
    screen("61-pet-picker-ember", "Pets", "A pet with its own body, face and colours. APPROVE switches; the change shows everywhere at once.", picker("ember", "Ember", "Built in · a small jade dragon", 2, False))
    b = sb(left="Pet")
    b += a(110, 30, 100, 84, f'<div style="width:100px;height:84px;border-radius:30px;border:2px dashed {LINE};display:flex;align-items:center;justify-content:center;font:700 40px/40px Figtree;color:{TEXT3}">+</div>')
    b += a(0, 122, 320, 28, "More pets", "t24 c")
    b += a(20, 152, 280, 36, "Pick up to four more in the dashboard, under Pets. They are sent here.", "t14 c f2")
    b += a(0, 205, 320, 8, dots(4, 3), "c", "line-height:8px")
    b += H([("click", "Back")], [("click", "Next")], None)
    screen("62-pet-picker-more", "Pets", "Last page of the picker: every finished pet ships in the firmware; four more slots are filled from the web dashboard.", b)

    # --- about ---
    def about(page, kv):
        b = sb(left=f"About {page}/2")
        o = "".join(f'<div class="row" style="height:21px;gap:0"><span class="t14 f2" style="width:86px">{k}</span><span class="mono clamp1" style="max-width:210px;color:{c}">{v}</span></div>' for k, v, c in kv)
        b += a(12, 28, 296, 0, o)
        b += H([("click", "Back")], [("click", "Page")], None)
        return b
    screen("70-about", "About", "About, page 1: identity and network. Mono values, one fact per row.",
           about(1, [("Firmware", "2.1.0 · a1b2c3d", TEXT), ("Device", "devotchi-3F2A", TEXT), ("Pet", "Devo (built in)", TEXT), ("Wi-Fi", "HomeNet-5G  -58 dBm", TEXT),
                     ("IP", "192.168.1.42", TEXT), ("Server", "devotchi.local:8080", TEXT), ("Link", "online  2 h 14 min", MINT), ("Room", "desk · mode ask", TEXT)]))
    screen("71-about-diagnostics", "About", "About, page 2: diagnostics, with the last error in coral.",
           about(2, [("Battery", "3.92 V  72 %  charging", TEXT), ("Uptime", "5 h 02 min", TEXT), ("Reset", "power-on", TEXT), ("Heap", "148 kB free · min 96", TEXT),
                     ("Frames", "30 fps · 11 ms draw", TEXT), ("Requests", "42 held · 37 answered", TEXT), ("Last err", "ws close 1006, 14:02", CORAL), ("Fonts", "Figtree, JB Mono (OFL)", TEXT2)]))

    # --- demo ---
    drows = [("pet", "Moods", "11"), ("play", "Reactions", "5"), ("shield", "Sample permission", None), ("q", "Sample question", None),
             ("bell", "Sample notification", None), ("clock", "Clock face", None), ("x", "Leave showcase", None)]
    screen("80-demo-menu", "Showcase (demo)", "Showcase menu: everything a friend should see, without an agent. Violet frame and one violet tag on every demo screen.",
           settings("Showcase", drows, 0, 0, (0, .86), demo=True) + H([("click", "Back")], [("click", "Next")], [("click", "Open")]) + demo_frame())
    b = sb(left="Moods 9/11", demo=True)
    b += pet("devo", "proud", 160, 104, 0.78)
    b += a(0, 170, 320, 28, "Proud", "t24 c")
    b += a(0, 197, 320, 18, "All green after a big refactor", "t14 c f2")
    b += H([("click", "Back")], [("click", "Next")], [("click", "Pat")]) + demo_frame()
    screen("81-demo-moods", "Showcase (demo)", "Mood preview: ACTION steps through the eleven moods, APPROVE pats, REJECT goes back.", b)
    b = sb(left="Reactions", demo=True)
    b += pet("devo", "idle", 160, 104, 0.78, react="dizzy")
    b += a(0, 170, 320, 28, "Dizzy", "t24 c")
    b += a(0, 197, 320, 18, "Three pokes within two seconds", "t14 c f2")
    b += H([("click", "Back")], [("click", "Next")], [("click", "Replay")]) + demo_frame()
    screen("82-demo-reactions", "Showcase (demo)", "Reaction preview: pat, poke, dizzy, wake-up, attention.", b)
    screen("83-demo-permission", "Showcase (demo)", "Sample permission request: exactly the real high-risk card, with SAMPLE where the clock is; the answer goes nowhere.",
           perm("high", "Bash", "terminal", ["rm -rf dist node_modules"], "Clean the build output", reason="Recursive delete in the project", secs=30, banner="sample"))
    screen("84-demo-question", "Showcase (demo)", "Sample question: exactly the real card (REJECT is Terminal, as on a real first question).",
           question("Lunch", None, "What should the team order for lunch today?",
                    [("Pizza (Recommended)", "Classic. Nobody complains, and the leftovers survive until the evening deploy."), ("Sushi", "Fancy."), ("Tacos", "Messy keyboards.")], 0, banner="sample", secs=30, total=30, count=2))
    screen("85-demo-real-request", "Showcase (demo)", "A real request during the showcase (and for 10 s after): cream REAL REQUEST bar, two pulses, 1.5 s arming, and Allow is a hold whatever the risk.",
           perm("low", "Bash", "terminal", ["pnpm test --filter server"], "Run the server test suite", secs=29, banner="real"))

    # --- power ---
    b = home("idle", None, sbkw={"batt": 12}, extra=toast("bolt", AMBER, "Battery low · 12 %", "Plug in USB-C soon"))
    screen("90-low-battery", "Power", "Low battery warning at 15 %: one toast, then a coral battery glyph in the status bar.", b)
    b = sb(batt=41)
    b += pet("devo", "sleeping", 160, 84, 0.66, react="wake")
    b += a(0, 142, 320, 28, "Going to sleep", "t24 c")
    b += a(0, 172, 320, 18, "in 6 seconds", "t14 c f2")
    b += countdown(0.55, TEXT2, 200)
    b += ANY("Any button: stay awake")
    screen("91-going-to-sleep", "Power", "Ten-second warning before deep sleep (the pet yawns). One centred tab: every button means 'stay awake'.", b)
    bat = (f'<svg width="120" height="60" viewBox="0 0 120 60"><rect x="3" y="3" width="100" height="54" rx="14" fill="none" stroke="{CORAL}" stroke-width="5"/>'
           f'<rect x="108" y="20" width="9" height="20" rx="4" fill="{CORAL}"/><rect x="13" y="13" width="12" height="34" rx="5" fill="{CORAL}"/></svg>')
    b = a(100, 46, 120, 60, bat)
    b += a(0, 124, 320, 28, "Battery empty", "t24 c")
    b += a(0, 156, 320, 18, "Plug in USB-C to wake me up", "t14 c f2")
    screen("92-battery-empty", "Power", "Battery empty: full screen, no status bar, shown for 5 s before power-down.", b)
    b = pet("devo", "idle", 160, 88, 0.7, react="attention")
    b += a(0, 150, 320, 28, "devotchi-3F2A", "t24 c")
    b += a(0, 182, 320, 18, "This is the device you picked", "t14 c f2")
    b += ANY("Any button: OK")
    screen("93-identify", "Power", "Identify (from the dashboard or `devotchi devices identify`): name full screen, LED and haptics.", b)


def pet_sheet():
    def tile(svg, name, note="", bg=SURF, w=180):
        return f'<figure style="background:{bg};width:{w}px">{svg}<figcaption><b>{name}</b><span>{note}</span></figcaption></figure>'
    dn = {
        "focused": "level half lids, antenna still", "working": "eyes on the work, tongue out, antenna sways with the scan",
        "thinking": "one brow up, mouth off-centre, dots, chip light pulses", "idle": "open eyes, soft smile, hem ripples",
        "happy": "big smile, perky antenna, hem ripples", "sad": "low gaze, tear, antenna droops, chip light off",
        "anxious": "pin pupils, sweat, chip light flickers", "excited": "open smile, fast bob, sparkles, chip light pulses",
        "proud": "eyes half shut looking up, sparkles", "angry": "brows down, frown, antenna bent back",
        "sleeping": "lids closed, body settles, lights out, Zzz",
    }
    notes = {
        "focused": "flat half lids, small smile, antenna straight and still", "working": "eyes on the work, tongue out, antenna wags",
        "thinking": "one brow up, mouth off-centre, antenna hooks, dots", "idle": "open eyes, soft smile, cheeks, slow blinks",
        "happy": "big smile, cheeks, perky antenna", "sad": "low gaze, tear, antenna droops",
        "anxious": "pin pupils, jitter, sweat, kinked antenna", "excited": "open smile, fast bob, sparkles",
        "proud": "eyes half shut looking up, sparkles", "angry": "brows down, frown, kinked antenna",
        "sleeping": "lids closed, body settles, Zzz",
    }
    rn = {"pat": "pat (APPROVE): ^ eyes, hearts, 1.5 s", "poke": "poke (REJECT): squish, 0.6 s", "dizzy": "dizzy: 3 pokes in 2 s, 2 s",
          "wake": "wake-up: yawn and stretch, 1.4 s", "attention": "attention: needs input"}
    en = {
        "focused": "lids low and level, wings tucked", "working": "eyes down, small flaps", "thinking": "one-sided squint, tail curls down",
        "idle": "round eyes, fangs over a smile", "happy": "wings lift, tail wags", "sad": "lids droop outwards, wings and tail hang, paler",
        "anxious": "needle pupils, wings tight, paler", "excited": "wide pupils, wings spread, breathes fire", "proud": "smug lids, wings high",
        "angry": "lids slant in, fins flare, smoke, hotter accent", "sleeping": "wings folded, head down, smoke rings",
    }
    er = {"pat": "wings and tail lift", "poke": "head squashes, wings flatten", "dizzy": "", "wake": "", "attention": "wings and fins snap up"}
    dr = dict(rn, attention="attention: needs input; antenna tip turns amber")
    devo = "".join(tile(pet_svg("devo", m, scale=.8), m, dn[m]) for m in MOODS)
    react = "".join(tile(pet_svg("devo", "idle", react=r, scale=.8), r, dr[r]) for r in REACT)
    mochi = "".join(tile(pet_svg("mochi", m, scale=.8), m, notes[m]) for m in MOODS)
    mreact = "".join(tile(pet_svg("mochi", "sleeping" if r == "wake" else "idle", react=r, scale=.8), r, rn[r]) for r in REACT)
    ember = "".join(tile(pet_svg("ember", m, scale=.8), m, en[m]) for m in MOODS)
    ereact = "".join(tile(pet_svg("ember", "sleeping" if r == "wake" else "idle", react=r, scale=.8), r, er[r]) for r in REACT)
    sprout = "".join(tile(pet_svg("sprout", m, scale=.8), m, "") for m in MOODS)
    sreact = "".join(tile(pet_svg("sprout", "idle", react=r, scale=.8), r, "") for r in REACT)
    night = (tile(pet_svg("devo", "sleeping", night=NIGHT, scale=.8), "Devo", "black fill, ember outline", "#000")
             + tile(pet_svg("mochi", "sleeping", night=NIGHT, scale=.8), "Mochi", "", "#000") + tile(pet_svg("ember", "sleeping", night=NIGHT, scale=.8), "Ember", "", "#000")
             + tile(pet_svg("sprout", "sleeping", night=NIGHT, scale=.8), "Sprout", "", "#000"))
    gray = "".join(tile(pet_svg(k, m, scale=.45, gray=True), m, "", SURF, 100) for k in ("devo", "mochi", "ember", "sprout") for m in MOODS)
    onex = "".join(tile(pet_svg(k, m, scale=.46), m, "", BG, 100) for k in ("devo", "mochi", "ember", "sprout") for m in ("focused", "working", "thinking", "idle"))
    devo_body = pet_source("devo")["palette"]["base"]
    devo_line = pet_source("devo")["palette"]["cream"]
    sw = f'<span class="sw" style="background:{devo_body};color:{devo_line}">body<br>{devo_body}</span>'
    sw += f'<span class="sw" style="background:{devo_line}">lines<br>{devo_line}</span>'
    sw += "".join(f'<span class="sw" style="background:{DEVO_COLOR[m]}">{m}<br>{DEVO_COLOR[m]}</span>' for m in MOODS)
    sw1 = "".join(f'<span class="sw" style="background:{pet_color("mochi", m)}">{m}<br>{pet_color("mochi", m)}</span>' for m in MOODS)
    sw2 = "".join(f'<span class="sw" style="background:{pet_color("ember", m)}">{m}<br>{pet_color("ember", m)}</span>' for m in MOODS)
    sw3 = "".join(f'<span class="sw" style="background:{pet_color("sprout", m)}">{m}<br>{pet_color("sprout", m)}</span>' for m in MOODS)
    sw2 += f'<span class="sw" style="background:{EMBER_ACCENT}">accent<br>{EMBER_ACCENT}</span><span class="sw" style="background:{EMBER_CREAM}">cream<br>{EMBER_CREAM}</span>'
    sem = "".join(f'<span class="sw" style="background:{c}">{n}<br>{c}</span>' for n, c in (("mint", MINT), ("amber", AMBER), ("coral", CORAL), ("sky", SKY), ("violet", VIOLET)))
    doc = f"""<!doctype html><html><head><meta charset="utf-8"><title>Devotchi pets</title><style>
{CSS.replace("FONTS", "fonts").replace("overflow:hidden}", "}", 1)}
body{{background:{BG};color:{TEXT};font:500 16px/22px Figtree;margin:0;padding:24px 28px;width:1144px}}
h1{{font:700 28px/34px Figtree;margin:0 0 4px}} h2{{font:700 20px/26px Figtree;margin:26px 0 2px}} p{{color:{TEXT2};margin:0 0 10px;max-width:900px}}
.grid{{display:flex;flex-wrap:wrap;gap:10px}}
figure{{margin:0;width:180px;background:{SURF};border-radius:14px;padding:6px 0 10px;text-align:center}}
figure svg{{margin:0 auto}} figcaption b{{display:block;font:700 16px/20px Figtree}} figcaption span{{display:block;font:500 12px/15px Figtree;color:{TEXT2};padding:0 8px;min-height:30px}}
.sw{{display:inline-block;width:94px;padding:8px 0;margin:0 6px 6px 0;border-radius:8px;color:{BG};font:500 12px/15px JBMono;text-align:center}}
</style></head><body>
<h1>Devotchi pets</h1>
<p>A pet is data: its own palette, body parts, face style and four auxiliary channels, posed by the shared channel table of the firmware rig (pet_pose.c). Everything is rounded rectangles, circles, short polylines and a few triangles. See pet-animations.md.</p>
<h2>Devo, the signature pet: 11 moods</h2><p>The ghost from the PCB silkscreen: a dome, straight sides and a three-bump hem, with the antenna and ball of the small logo. It has the board's look: a dark body with a white outline and white circuit traces, vias and chips, round eyes with a glint and a small smile. The body and the traces never change: the mood colour shows on the antenna ball and on the light in the big chip (grey asleep), with the face and the antenna. It floats over a soft shadow while its hem ripples; the light in the big chip pulses while it thinks, flickers when anxious and is off when sad or asleep, and the antenna ball is the on-screen twin of the device LED. Drawn here from its pack by the dashboard's renderer.</p>
<div class="grid">{devo}</div>
<h2>Devo: reactions</h2><div class="grid">{react}</div>
<h2>Mochi: 11 moods</h2><p>A soft square with an antenna, in one apricot hue that a mood only shifts lighter, darker or paler. The antenna is its second face: it wags, droops, kinks and curls.</p>
<div class="grid">{mochi}</div>
<h2>Mochi: reactions</h2><div class="grid">{mreact}</div>
<h2>Ember, the alternative pet: 11 moods</h2><p>A small jade dragon: big head on a small body, snout with nostrils and fangs, slit pupils, lids instead of brows, orange wings, ear fins and tail spade. Ember keeps its jade in every mood (a mood only shifts it lighter, darker or paler); wings, tail, smoke and fire carry the feeling.</p>
<div class="grid">{ember}</div>
<h2>Ember: reactions</h2><div class="grid">{ereact}</div>
<h2>Sprout, a catalog pet: 11 moods</h2><p>An egg with a sprouting leaf, sent to a device from the dashboard rather than compiled in. One pink hue shifted by mood; the leaf tilts and droops with the feeling. Drawn from its pack by the dashboard's renderer.</p>
<div class="grid">{sprout}</div>
<h2>Sprout: reactions</h2><div class="grid">{sreact}</div>
<h2>Night outline (clock face at night)</h2><div class="grid">{night}</div>
<h2>Check 1: greyscale (every mood must be told apart without colour)</h2><div class="grid">{gray}</div>
<h2>Check 2: the everyday moods at the size they have on the clock and going-to-sleep screens (about half scale)</h2><div class="grid">{onex}</div>
<h2>Devo palette (dark body, white lines; eleven colours for the ball and the chip light)</h2><div>{sw}</div>
<h2>Mochi palette (one hue {MOCHI_BASE}, shifted by mood)</h2><div>{sw1}</div>
<h2>Ember palette (one hue, shifted by mood, plus accent and cream)</h2><div>{sw2}</div>
<h2>Sprout palette (one hue, shifted by mood)</h2><div>{sw3}</div>
<h2>Semantic UI colours, for comparison (no pet colour sits on these)</h2><div>{sem}</div>
</body></html>"""
    with open(os.path.join(ROOT, "pet-moods.html"), "w") as f:
        f.write(doc)


def device_context():
    """The front of the case with a mockup in the window, to show that the tabs sit above the buttons."""
    k = 7.843  # px per mm (320 px across the 40.8 mm window)
    cw = 60 * k
    sx, sy = (60 - 40.8) / 2 * k, 5.2 * k
    by = 43.9 * k
    btn = ""
    for i, (name, col) in enumerate((("REJECT", CORAL), ("ACTION", TEXT), ("APPROVE", MINT))):
        cx = (30 + (i - 1) * 14) * k
        btn += (f'<div style="position:absolute;left:{cx - 6 * k:.0f}px;top:{by:.0f}px;width:{12 * k:.0f}px;height:{6 * k:.0f}px;border-radius:{1.2 * k:.0f}px;background:#2A2F3A;border:2px solid #3A4150"></div>'
                f'<div style="position:absolute;left:{cx - 50:.0f}px;top:{by + 6 * k + 8:.0f}px;width:100px;text-align:center;font:500 13px/16px Figtree;letter-spacing:.09em;color:{col}">{name}</div>'
                f'<div style="position:absolute;left:{cx:.0f}px;top:{sy + 240:.0f}px;width:0;height:{by - sy - 240:.0f}px;border-left:1.5px dashed {col}"></div>')
    doc = f"""<!doctype html><html><head><meta charset="utf-8"><title>Device context</title><style>
@font-face{{font-family:Figtree;font-weight:500;src:url(fonts/Figtree-Medium.ttf)}}
body{{margin:0;background:{BG};width:{cw + 80:.0f}px;height:{cw + 80:.0f}px;position:relative}}
</style></head><body>
<div style="position:absolute;left:40px;top:40px;width:{cw:.0f}px;height:{cw:.0f}px;border-radius:{3 * k:.0f}px;background:#1B1F28;border:2px solid #343C4B">
<img src="renders/23-perm-high.png" style="position:absolute;left:{sx:.0f}px;top:{sy:.0f}px;width:320px;height:240px;border-radius:4px">
{btn}</div></body></html>"""
    with open(os.path.join(ROOT, "device-context.html"), "w") as f:
        f.write(doc)


def gallery():
    groups = []
    for name, group, cap in SCREENS:
        if not groups or groups[-1][0] != group:
            groups.append((group, []))
        groups[-1][1].append((name, cap))
    sec = ""
    for g, items in groups:
        figs = "".join(f'<figure><a href="mockups/{n}.html"><img src="renders/{n}.png" width="320" height="240" alt="{n}"></a><figcaption><b>{n}</b>{html.escape(c)}</figcaption></figure>' for n, c in items)
        sec += f"<h2>{g}</h2><div class=grid>{figs}</div>"
    changes = [
        "Pets own their identity: Devo is the ghost of the PCB silkscreen, dark with white traces, the mood on its antenna ball and chip light; Ember is a jade dragon with its own body, face and palette.",
        "Request cards put the exact command first and Claude's description second; a long command must be paged through before Allow works.",
        "Every hold says what it does (Hold: Allow, Hold: Erase), and shows the outcome while held (Allowing…).",
        "The session grant shows the rule the server will send, word for word; wide grants are amber or absent.",
        "Questions use real-length text: three-line descriptions, a read page for long questions, time that grows as you press.",
        "The clock is the resting face of the dimmed screen; the quick menu (ACTION hold on home) holds desk mode and the showcase.",
        "A real request during the showcase is unmistakable and needs a hold.",
        "Wi-Fi setup is two calm steps with a QR code at an exact 4 px per module.",
    ]
    doc = f"""<!doctype html><html><head><meta charset="utf-8"><title>Devotchi phase 2 design</title><style>
@font-face{{font-family:Figtree;font-weight:500;src:url(fonts/Figtree-Medium.ttf)}}
@font-face{{font-family:Figtree;font-weight:700;src:url(fonts/Figtree-Bold.ttf)}}
body{{background:#07080C;color:{TEXT};font:500 15px/21px Figtree,sans-serif;margin:0;padding:28px 36px 60px}}
h1{{font:700 30px/36px Figtree;margin:0}} h2{{font:700 21px/26px Figtree;margin:34px 0 12px;padding-top:14px;border-top:1px solid {SURF2}}}
p,li{{color:{TEXT2};max-width:820px}} a{{color:{SKY}}}
.grid{{display:flex;flex-wrap:wrap;gap:18px}}
figure{{margin:0;width:320px}} figure img{{display:block;border-radius:10px;border:1px solid {LINE}}}
figcaption{{color:{TEXT2};line-height:17px;margin-top:6px}} figcaption b{{display:block;color:{TEXT};font:500 12px/16px monospace}}
.wide img{{max-width:100%;height:auto;border-radius:12px;border:1px solid {LINE}}}
</style></head><body>
<h1>Devotchi phase 2: device UI design (v2, after review)</h1>
<p>Every mockup is a 320x240 page (click an image to open it); the images are 2x renders. Read with
<a href="design-system.md">design-system.md</a>, <a href="ux-flows.md">ux-flows.md</a> and <a href="pet-animations.md">pet-animations.md</a>.
Mockups define look, type, pages and button behaviour; they are not pixel-exact. {len(SCREENS)} screens.</p>
<h2>What changed after the two reviews</h2><ul>{"".join(f"<li>{c}</li>" for c in changes)}</ul>
<p>The full list, with the points where the design disagrees with a reviewer, is in design-system.md under "Review decisions".</p>
<h2>The device</h2><p>The three buttons sit in a row under the screen: REJECT left, ACTION middle, APPROVE right, 14 mm apart. The on-screen tabs sit straight above them (x = 50, 160 and 270 px).</p>
<div class="wide"><img src="renders/device-context.png" width="550" alt="device"></div>
<h2>Pets</h2><p>The signature pet Devo (the ghost from the PCB silkscreen), Mochi and Ember (built in) and Sprout (from the catalog), in all moods and reactions, with the greyscale and small-size checks (<a href="pet-moods.html">pet-moods.html</a>).</p>
<div class="wide"><img src="renders/pet-moods.png" alt="pets"></div>
{sec}
</body></html>"""
    with open(os.path.join(ROOT, "index.html"), "w") as f:
        f.write(doc)


if __name__ == "__main__":
    build()
    pet_sheet()
    device_context()
    gallery()
    print(len(SCREENS), "mockups")
