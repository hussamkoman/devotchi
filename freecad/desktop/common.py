"""Desktop Devotchi — Shared SolidPython2 Primitives.

Building blocks used by all 6 case designs: ghost components,
mounting hardware, cutouts, and view helpers.
"""
from solid2 import cube, cylinder, sphere, hull, union, difference, intersection
from solid2 import translate, rotate, color, scale, mirror, linear_extrude, polygon
from solid2 import circle, square, offset, text
import math

from params import (
    SCREEN_MOD_W, SCREEN_MOD_H, SCREEN_MOD_D,
    SCREEN_ACT_W, SCREEN_ACT_H,
    PCB_W, PCB_H, PCB_D,
    ESP32_W, ESP32_H, ESP32_D,
    SPEAKER_DIA, SPEAKER_D,
    USBC_W, USBC_H, USBC_R,
    BTN_HOLE_DIA, BTN_CAP_H,
    STANDOFF_OD, STANDOFF_ID, STANDOFF_INSET,
    CLIP_OVERHANG, CLIP_THICKNESS,
    SCREW_BOSS_OD, SCREW_BOSS_ID,
    WALL, TOL,
)


# ── Basic Shapes ──

def rounded_box(w, h, d, r):
    """Box with rounded vertical edges (XY corners). r is corner radius."""
    r = min(r, w / 2 - 0.01, h / 2 - 0.01)
    if r < 0.5:
        return cube([w, h, d], center=True)
    # Hull of 4 cylinders at corners
    offset_x = w / 2 - r
    offset_y = h / 2 - r
    corners = []
    for sx in [-1, 1]:
        for sy in [-1, 1]:
            corners.append(
                translate([sx * offset_x, sy * offset_y, 0])(
                    cylinder(r=r, h=d, center=True, _fn=48)
                )
            )
    return hull()(*corners)


def rounded_box_xy(w, h, d, r):
    """Box with rounded XY corners, bottom at z=0."""
    return translate([0, 0, d / 2])(rounded_box(w, h, d, r))


def pill_2d(w, h):
    """2D pill/stadium shape (rounded rectangle), centered."""
    r = min(h / 2, w / 2)
    if w > h:
        return hull()(
            translate([-(w / 2 - r), 0])(circle(r=r, _fn=32)),
            translate([(w / 2 - r), 0])(circle(r=r, _fn=32)),
        )
    else:
        return hull()(
            translate([0, -(h / 2 - r)])(circle(r=r, _fn=32)),
            translate([0, (h / 2 - r)])(circle(r=r, _fn=32)),
        )


# ── Ghost Components (for visualization / exploded views) ──

def ghost_screen():
    """5" screen module — blue ghost for visualization."""
    module = cube([SCREEN_MOD_W, SCREEN_MOD_H, SCREEN_MOD_D], center=True)
    # Active area inset on front face
    active = translate([0, 0, SCREEN_MOD_D / 2 - 0.5])(
        cube([SCREEN_ACT_W, SCREEN_ACT_H, 1.0], center=True)
    )
    return color("DodgerBlue", 0.5)(module) + color("LightBlue", 0.7)(active)


def ghost_pcb():
    """Custom PCB — green ghost."""
    board = cube([PCB_W, PCB_H, PCB_D], center=True)
    # ESP32 module on front face
    esp = translate([PCB_W / 2 - 25, PCB_H / 2 - 15, PCB_D / 2 + ESP32_D / 2])(
        cube([ESP32_W, ESP32_H, ESP32_D], center=True)
    )
    return color("ForestGreen", 0.5)(board) + color("DarkGreen", 0.6)(esp)


def ghost_speaker():
    """28mm speaker — yellow ghost."""
    body = cylinder(d=SPEAKER_DIA, h=SPEAKER_D, center=True, _fn=48)
    return color("Gold", 0.5)(body)


def ghost_usbc():
    """USB-C connector — silver ghost."""
    conn = linear_extrude(height=7.0, center=True)(pill_2d(USBC_W, USBC_H))
    return color("Silver", 0.6)(conn)


# ── Cutouts ──

def screen_window_cutout(depth):
    """Rectangular cutout for the screen active area, with slight bevel."""
    # Main window
    window = cube([SCREEN_ACT_W + 1.0, SCREEN_ACT_H + 1.0, depth + 0.2], center=True)
    return window


def usbc_cutout(depth):
    """Pill-shaped USB-C port cutout through a wall."""
    return linear_extrude(height=depth + 0.2, center=True)(
        pill_2d(USBC_W + 0.4, USBC_H + 0.4)
    )


def button_hole(depth):
    """Cylindrical button hole through case wall."""
    return cylinder(d=BTN_HOLE_DIA, h=depth + 0.2, center=True, _fn=32)


def button_row(n, spacing, depth):
    """Row of n button holes spaced evenly, centered."""
    holes = []
    start_x = -(n - 1) * spacing / 2
    for i in range(n):
        holes.append(
            translate([start_x + i * spacing, 0, 0])(
                button_hole(depth)
            )
        )
    return union()(*holes)


def button_column(n, spacing, depth):
    """Column of n button holes spaced evenly, centered."""
    holes = []
    start_y = -(n - 1) * spacing / 2
    for i in range(n):
        holes.append(
            translate([0, start_y + i * spacing, 0])(
                button_hole(depth)
            )
        )
    return union()(*holes)


def speaker_grille(diameter, hole_dia=2.0, spacing=4.0):
    """Grid of small holes for speaker output."""
    r = diameter / 2 - 2.0
    holes = []
    rows = int(diameter / spacing)
    for iy in range(rows):
        y = -r + spacing / 2 + iy * spacing
        for ix in range(rows):
            x = -r + spacing / 2 + ix * spacing
            if x * x + y * y < r * r:
                holes.append(
                    translate([x, y, 0])(
                        cylinder(d=hole_dia, h=20, center=True, _fn=16)
                    )
                )
    if holes:
        return union()(*holes)
    return cylinder(d=1, h=1)  # fallback


def led_hole(depth):
    """Small hole for status LED."""
    return cylinder(d=2.0, h=depth + 0.2, center=True, _fn=16)


# ── Mounting Hardware ──

def standoff(height, od=None, screw_id=None):
    """PCB mounting standoff — cylinder with screw hole."""
    od = od or STANDOFF_OD
    screw_id = screw_id or STANDOFF_ID
    outer = cylinder(d=od, h=height, center=False, _fn=32)
    inner = translate([0, 0, -0.1])(
        cylinder(d=screw_id, h=height + 0.2, center=False, _fn=24)
    )
    return difference()(outer, inner)


def standoff_set(positions, height, od=None, screw_id=None):
    """Multiple standoffs at given (x, y) positions."""
    posts = []
    for x, y in positions:
        posts.append(translate([x, y, 0])(standoff(height, od, screw_id)))
    return union()(*posts)


def retention_clip(length, width=CLIP_THICKNESS, overhang=CLIP_OVERHANG):
    """Display retention clip — L-shaped lip."""
    vertical = cube([length, width, 3.0], center=True)
    horizontal = translate([0, -overhang / 2, 1.5])(
        cube([length, overhang, width], center=True)
    )
    return union()(vertical, horizontal)


def screw_boss(height, od=None, screw_id=None):
    """Screw boss for case assembly."""
    od = od or SCREW_BOSS_OD
    screw_id = screw_id or SCREW_BOSS_ID
    return standoff(height, od, screw_id)


def rubber_foot(diameter=8.0, height=1.5):
    """Anti-slip rubber foot pad."""
    return color("DimGray", 0.8)(
        cylinder(d=diameter, h=height, center=False, _fn=32)
    )


# ── View Helpers ──

def cross_section_cut(solid, axis="y", keep="back"):
    """Cut a solid in half for cross-section view.
    axis: 'x', 'y', or 'z'
    keep: 'front'/'back' (which half to keep)
    """
    big = 500
    if axis == "y":
        if keep == "back":
            cutter = translate([0, big / 2, 0])(cube([big * 2, big, big * 2], center=True))
        else:
            cutter = translate([0, -big / 2, 0])(cube([big * 2, big, big * 2], center=True))
    elif axis == "x":
        if keep == "back":
            cutter = translate([big / 2, 0, 0])(cube([big, big * 2, big * 2], center=True))
        else:
            cutter = translate([-big / 2, 0, 0])(cube([big, big * 2, big * 2], center=True))
    else:  # z
        if keep == "back":
            cutter = translate([0, 0, big / 2])(cube([big * 2, big * 2, big], center=True))
        else:
            cutter = translate([0, 0, -big / 2])(cube([big * 2, big * 2, big], center=True))
    return intersection()(solid, cutter)


def explode_parts(parts_with_offsets):
    """Translate parts apart for exploded view.
    parts_with_offsets: list of (solid, (dx, dy, dz)) tuples
    """
    exploded = []
    for solid, (dx, dy, dz) in parts_with_offsets:
        exploded.append(translate([dx, dy, dz])(solid))
    return union()(*exploded)


# ── Camera Presets ──

def camera_iso(dist=400):
    """Isometric camera: elevation 25, azimuth 325."""
    return f"0,0,0,55,0,325,{dist}"


def camera_front(dist=350):
    """Front view camera."""
    return f"0,0,0,0,0,0,{dist}"


def camera_back(dist=350):
    """Back view camera."""
    return f"0,0,0,0,0,180,{dist}"


def camera_top(dist=350):
    """Top view camera."""
    return f"0,0,0,90,0,0,{dist}"


def camera_custom(rot_x, rot_y, rot_z, dist=400, tx=0, ty=0, tz=0):
    """Custom camera position."""
    return f"{tx},{ty},{tz},{rot_x},{rot_y},{rot_z},{dist}"
