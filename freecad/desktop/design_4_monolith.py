"""Desktop Devotchi — Design 4: Monolith Slab.

Thin vertical tablet (135 x 90 x 12mm) with integrated kickstand.
Like a Surface Pro but tiny. No speaker. The thinnest design.
"""
import math

from solid2 import (
    cube, cylinder, hull, union, difference, intersection,
    translate, rotate, color, linear_extrude,
)

from params import (
    SCREEN_MOD_W, SCREEN_MOD_H, SCREEN_MOD_D,
    SCREEN_ACT_W, SCREEN_ACT_H,
    PCB_W, PCB_H, PCB_D,
    ESP32_D,
    USBC_W, USBC_H,
    BTN_HOLE_DIA,
    WALL, TOL,
)
from common import (
    rounded_box, rounded_box_xy,
    pill_2d,
    ghost_screen, ghost_pcb,
    screen_window_cutout, usbc_cutout, button_hole,
    camera_iso, camera_front, camera_back, camera_top, camera_custom,
    cross_section_cut, explode_parts,
)

# ── Slab dimensions ──
# Screen rotated 90°: 75mm across the width, 120mm along the height
SLAB_W = 84.0       # Width  (screen 75mm + walls + tolerance)
SLAB_H = 135.0      # Height (screen 120mm + walls + tolerance)
SLAB_D = 15.0       # Depth  (thicker: front wall 2 + screen 4.5 + ESP32 gap 3 + PCB 1.6 + back 2 = 13.1 + margin)
SLAB_R = 3.0        # Corner radius

# Screen is rotated 90° in the pocket: what was W becomes H and vice versa
SCREEN_POCKET_W = SCREEN_MOD_H + TOL * 2   # 75mm module height → pocket width
SCREEN_POCKET_H = SCREEN_MOD_W + TOL * 2   # 120mm module width → pocket height
SCREEN_WIN_W = SCREEN_ACT_H + 1.0          # 64.8mm active height → window width
SCREEN_WIN_H = SCREEN_ACT_W + 1.0          # 108mm active width → window height

# ── Kickstand dimensions ──
KS_W = 50.0         # Kickstand width  (matches short dimension of back face)
KS_H = 120.0        # Kickstand height (most of the back face)
KS_D = 2.0          # Kickstand thickness (flat plate)
KS_R = 2.0          # Corner radius
KS_SLOT_CLEAR = 0.3 # Clearance around kickstand in slot

# ── Screen pocket ──
SCREEN_POCKET_D = SCREEN_MOD_D + 0.5   # 5.0mm
FRONT_WALL = WALL                        # 2.0mm

# ── Button layout on right edge ──
BTN_N = 3
BTN_SPACING = 12.0   # Center-to-center spacing (tighter than default 14)

# ── Derived Z positions (from front face inner surface = 0) ──
Z_SCREEN_INNER = 0.0
Z_PCB_INNER = SCREEN_MOD_D + (SLAB_D - FRONT_WALL - WALL - SCREEN_POCKET_D - PCB_D) / 2
# Remaining behind screen pocket: 12 - 2 - 5.0 = 5.0mm for PCB (1.6) + ESP32 (2.4) + ~1mm
Z_PCB = SCREEN_POCKET_D   # PCB sits right behind screen pocket — flush

# ── Hinge position ──
# Kickstand slot on back face, hinge at bottom of slot
HINGE_Y_FROM_BOTTOM = 8.0   # mm from bottom edge to hinge pivot


# ── Internal helpers ──

def _slab_body():
    """Solid slab body centered at origin."""
    return rounded_box(SLAB_W, SLAB_H, SLAB_D, SLAB_R)


def _screen_pocket():
    """Screen module recess on the front face interior (screen rotated 90°)."""
    return translate([0, 0, -SCREEN_POCKET_D / 2])(
        cube([SCREEN_POCKET_W, SCREEN_POCKET_H, SCREEN_POCKET_D + 0.1], center=True)
    )


def _screen_window():
    """Through-hole for the active screen area on the front face (rotated 90°)."""
    return cube([SCREEN_WIN_W, SCREEN_WIN_H, FRONT_WALL + 0.2], center=True)


def _usbc_cutout_bottom():
    """USB-C cutout through the bottom edge, centered."""
    # Bottom edge center: Y = -SLAB_H/2, cutout runs through SLAB_D
    slot = linear_extrude(height=SLAB_D + 0.2, center=True)(
        pill_2d(USBC_W + 0.4, USBC_H + 0.4)
    )
    # Orient: extrude axis along Z (thickness), pill axis along X (width)
    # Need to cut through the bottom wall (Y axis), rotate into YZ plane
    return translate([0, -SLAB_H / 2, 0])(
        rotate([90, 0, 0])(slot)
    )


def _button_holes_right_edge():
    """3 button holes through the right edge, spaced vertically."""
    # Center button cluster on the screen vertical center (y=0)
    holes = []
    start_y = -(BTN_N - 1) * BTN_SPACING / 2
    for i in range(BTN_N):
        y = start_y + i * BTN_SPACING
        # Hole runs along X axis through the right wall
        hole = translate([SLAB_W / 2, y, 0])(
            rotate([0, 90, 0])(
                cylinder(d=BTN_HOLE_DIA, h=WALL + 0.2, center=True, _fn=32)
            )
        )
        holes.append(hole)
    return union()(*holes)


def _kickstand_slot():
    """Slot in the back face for the kickstand to recess into."""
    slot_w = KS_W + KS_SLOT_CLEAR * 2
    slot_h = KS_H + KS_SLOT_CLEAR * 2
    slot_d = KS_D + KS_SLOT_CLEAR
    # Slot on the back face: center in X, aligned toward bottom in Y,
    # hinge at bottom of slot
    # Back face inner surface at Z = -SLAB_D/2 + WALL
    slot_y_center = -SLAB_H / 2 + HINGE_Y_FROM_BOTTOM + slot_h / 2
    return translate([0, slot_y_center, SLAB_D / 2 - WALL - slot_d / 2])(
        cube([slot_w, slot_h, slot_d + 0.1], center=True)
    )


def _snap_clip_features():
    """Snap clips on back shell edges for assembly."""
    clips = []
    clip_h = 6.0
    clip_d = 1.2
    clip_w = 12.0
    # Two clips on left and right edges near top and bottom
    for side_x in [-SLAB_W / 2 + WALL / 2, SLAB_W / 2 - WALL / 2]:
        for clip_y in [-SLAB_H / 2 + 20, SLAB_H / 2 - 20]:
            clip = translate([side_x, clip_y, SLAB_D / 2 - WALL - clip_d / 2])(
                cube([clip_d, clip_h, clip_d + 0.1], center=True)
            )
            clips.append(clip)
    return union()(*clips)


# ── Public build functions ──

def build_front_shell():
    """Front face shell: screen window cutout + button holes on right edge.

    The front shell is the forward half of the slab — it holds the screen.
    Split plane is at Z=0 (mid-depth). Front shell is the -Z half (front-facing).
    """
    # Start with full slab body
    shell = _slab_body()

    # Hollow out interior — leave front wall, side walls, top/bottom walls
    inner_w = SLAB_W - WALL * 2
    inner_h = SLAB_H - WALL * 2
    inner_d = SLAB_D - WALL * 2 + 0.1
    interior = translate([0, 0, -WALL / 2 + 0.1])(
        cube([inner_w, inner_h, inner_d], center=True)
    )
    shell = difference()(shell, interior)

    # Screen pocket recess (from front face inward) — screen rotated 90°
    # Front face inner surface is at Z = -SLAB_D/2 + FRONT_WALL
    pocket_z = -SLAB_D / 2 + FRONT_WALL + SCREEN_POCKET_D / 2
    pocket = translate([0, 0, pocket_z])(
        cube([SCREEN_POCKET_W, SCREEN_POCKET_H, SCREEN_POCKET_D + 0.1], center=True)
    )
    shell = difference()(shell, pocket)

    # Screen window — through-hole in front wall for active area (rotated 90°)
    win_z = -SLAB_D / 2 + FRONT_WALL / 2
    window = translate([0, 0, win_z])(
        cube([SCREEN_WIN_W, SCREEN_WIN_H, FRONT_WALL + 0.2], center=True)
    )
    shell = difference()(shell, window)

    # Button holes on right edge
    shell = difference()(shell, _button_holes_right_edge())

    # USB-C cutout on bottom edge
    shell = difference()(shell, _usbc_cutout_bottom())

    return color("LightGray", 0.9)(shell)


def build_back_shell():
    """Back cover with kickstand hinge slot.

    Flat back that snaps onto the front shell.
    """
    back_d = WALL + KS_D + KS_SLOT_CLEAR + WALL  # ~5.7mm — the back portion
    # Build as a solid slab, then hollow and cut slot
    back_w = SLAB_W
    back_h = SLAB_H

    # Outer shell — same width/height, back_d thick, centered in XY, back half in Z
    outer = rounded_box(back_w, back_h, back_d, SLAB_R)
    # Shift to align with back face of slab
    outer = translate([0, 0, SLAB_D / 2 - back_d / 2])(outer)

    # Hollow out (thin shell)
    inner_w = back_w - WALL * 2
    inner_h = back_h - WALL * 2
    inner_d = back_d - WALL + 0.1
    hollow = translate([0, 0, SLAB_D / 2 - inner_d / 2 - WALL + 0.05])(
        cube([inner_w, inner_h, inner_d], center=True)
    )
    back = difference()(outer, hollow)

    # Kickstand slot — recess on back face
    slot_w = KS_W + KS_SLOT_CLEAR * 2
    slot_h = KS_H + KS_SLOT_CLEAR * 2
    slot_depth = KS_D + KS_SLOT_CLEAR
    slot_y_center = -SLAB_H / 2 + HINGE_Y_FROM_BOTTOM + slot_h / 2
    slot = translate([0, slot_y_center, SLAB_D / 2 - slot_depth / 2])(
        cube([slot_w, slot_h, slot_depth + 0.1], center=True)
    )
    back = difference()(back, slot)

    return color("DarkGray", 0.85)(back)


def build_stand():
    """Kickstand — flat rounded rectangle, shown deployed at 45 degrees.

    In folded position: flush against the back face.
    Here modeled in deployed position: rotated -45 degrees around hinge axis
    at the bottom of the kickstand slot.
    """
    # Build flat kickstand plate centered at its own origin
    ks_w = KS_W
    ks_h = KS_H
    ks_d = KS_D

    # Rounded rectangle: hull of 4 cylinders
    r = KS_R
    ox = ks_w / 2 - r
    oy = ks_h / 2 - r
    corners = [
        translate([sx * ox, sy * oy, 0])(cylinder(r=r, h=ks_d, center=True, _fn=32))
        for sx in [-1, 1] for sy in [-1, 1]
    ]
    ks_plate = hull()(*corners)

    # Hinge pivot point: bottom edge of kickstand = -ks_h/2 in local Y
    # In world space: bottom of slot on back face
    # hinge_y (world) = -SLAB_H/2 + HINGE_Y_FROM_BOTTOM
    hinge_world_y = -SLAB_H / 2 + HINGE_Y_FROM_BOTTOM
    hinge_world_z = SLAB_D / 2  # back face of slab

    # Step 1: shift plate so its bottom edge is at local origin (pivot)
    ks_pivoted = translate([0, ks_h / 2, 0])(ks_plate)

    # Step 2: rotate -45 deg around X axis (hinge) to deploy the stand
    ks_deployed = rotate([-45, 0, 0])(ks_pivoted)

    # Step 3: move hinge to world position
    ks_world = translate([0, hinge_world_y, hinge_world_z])(ks_deployed)

    return color("Gray", 0.75)(ks_world)


def build_internals():
    """Ghost components: screen and PCB positioned inside the slab.

    Returns a dict with keys 'screen' and 'pcb'.
    """
    # Screen: front face of screen module flush with inner front wall
    # Inner front wall at Z = -SLAB_D/2 + FRONT_WALL
    # Screen module depth: SCREEN_MOD_D → back face at Z_inner + SCREEN_MOD_D
    screen_z = -SLAB_D / 2 + FRONT_WALL + SCREEN_MOD_D / 2
    screen = translate([0, 0, screen_z])(
        rotate([0, 0, 90])(ghost_screen())  # Rotated 90° to fit portrait slab
    )

    # PCB: directly behind the screen pocket
    pcb_z = -SLAB_D / 2 + FRONT_WALL + SCREEN_POCKET_D + PCB_D / 2
    pcb = translate([0, 0, pcb_z])(ghost_pcb())

    return {"screen": screen, "pcb": pcb}


def _all_parts_assembled():
    """Union of all parts in assembled position."""
    internals = build_internals()
    return union()(
        build_front_shell(),
        build_back_shell(),
        build_stand(),
        internals["screen"],
        internals["pcb"],
    )


def get_views():
    """Return 7 view definitions as a dict of {name: (solid, camera_string)}."""
    internals = build_internals()
    front = build_front_shell()
    back = build_back_shell()
    stand = build_stand()
    screen = internals["screen"]
    pcb = internals["pcb"]
    assembled = union()(front, back, stand)

    views = {}

    # 1. ISO assembled — 3/4 front view showing screen face + side + top
    #    rot_x=200 = 20° past 180 (screen at -Z), rot_z=340 orbits right
    views["iso_assembled"] = (
        assembled,
        camera_custom(200, 0, 340, dist=500),
    )

    # 2. Front face — looking straight at screen, adequate distance for 135x90 slab
    views["front"] = (
        front,
        camera_front(dist=400),
    )

    # 3. Back face — looking at kickstand slot
    views["back"] = (
        back,
        camera_back(dist=400),
    )

    # 4. Right edge — button holes
    views["right_edge"] = (
        assembled,
        camera_custom(5, 0, 90, dist=400),
    )

    # 5. Bottom edge — USB-C port
    views["bottom_edge"] = (
        assembled,
        camera_custom(90, 0, 0, dist=400),
    )

    # 6. Cross-section — cut at y=0, keep y>0, look from -Y into cut face
    #    Thin slab (15mm Z) so use steep elevation to see depth stack
    full_with_internals = union()(front, back, screen, pcb)
    views["cross_section"] = (
        cross_section_cut(full_with_internals, axis="y", keep="back"),
        camera_custom(55, 0, 350, dist=350),
    )

    # 7. Exploded — parts pulled apart along Z, front-ISO angle
    views["exploded"] = (
        explode_parts([
            (front,   (0,  0, -15)),
            (back,    (0,  0,  15)),
            (stand,   (0, 30,  25)),
            (screen,  (0,  0,  -5)),
            (pcb,     (0,  0,   5)),
        ]),
        camera_custom(200, 0, 340, dist=550),
    )

    return views


def check_clearances():
    """Validate key dimensional constraints.

    Returns a dict of {check_name: (value, limit, passed)}.
    """
    results = {}

    # Depth stack check: front wall + screen pocket + PCB + back wall <= SLAB_D
    depth_used = FRONT_WALL + SCREEN_POCKET_D + PCB_D + WALL
    depth_limit = SLAB_D
    results["depth_stack"] = (depth_used, depth_limit, depth_used <= depth_limit)

    # Remaining space behind PCB front face for ESP32
    remaining = SLAB_D - FRONT_WALL - SCREEN_POCKET_D - PCB_D - WALL
    results["esp32_clearance"] = (remaining, ESP32_D, remaining >= ESP32_D)

    # Screen pocket fits (rotated 90°: 75mm across width, 120mm along height)
    results["screen_pocket_width"] = (SCREEN_POCKET_W, SLAB_W - WALL * 2, SCREEN_POCKET_W <= SLAB_W - WALL * 2)
    results["screen_pocket_height"] = (SCREEN_POCKET_H, SLAB_H - WALL * 2, SCREEN_POCKET_H <= SLAB_H - WALL * 2)

    # Button column fits within slab height
    btn_span = (BTN_N - 1) * BTN_SPACING
    results["button_column_span"] = (btn_span, SLAB_H - WALL * 4, btn_span <= SLAB_H - WALL * 4)

    # Kickstand slot fits within back face
    slot_h = KS_H + KS_SLOT_CLEAR * 2
    results["kickstand_slot_height"] = (slot_h, SLAB_H - WALL * 2, slot_h <= SLAB_H - WALL * 2)

    # Kickstand slot width fits
    slot_w = KS_W + KS_SLOT_CLEAR * 2
    results["kickstand_slot_width"] = (slot_w, SLAB_W - WALL * 2, slot_w <= SLAB_W - WALL * 2)

    return results


# ── Main: render default assembled view ──

if __name__ == "__main__":
    import os
    from solid2 import scad_render_to_file

    out_dir = os.path.join(os.path.dirname(__file__), "output")
    os.makedirs(out_dir, exist_ok=True)

    # Print clearance report
    print("=== Monolith Slab — Clearance Check ===")
    for name, (value, limit, passed) in check_clearances().items():
        status = "OK" if passed else "FAIL"
        print(f"  [{status}] {name}: {value:.2f} vs limit {limit:.2f}")

    # Render all views
    views = get_views()
    for view_name, (solid, camera) in views.items():
        out_path = os.path.join(out_dir, f"design_4_monolith_{view_name}.scad")
        scad_render_to_file(solid, out_path)
        print(f"  Wrote {out_path}")

    print("Done.")
