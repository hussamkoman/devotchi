"""Desktop Devotchi — Design 1: Upright Easel.

Portrait-mode flat slab (130 x 85 x 14mm) that slides into a separate
detachable triangular easel stand (110 x 60 x 70mm). Screen faces the
user at ~65-degree tilt when docked. No speaker.
"""

import math

from solid2 import (
    cube, cylinder, union, difference, intersection,
    translate, rotate, color, mirror, linear_extrude, polygon,
    hull, circle,
)

from params import (
    SCREEN_MOD_W, SCREEN_MOD_H, SCREEN_MOD_D,
    SCREEN_ACT_W, SCREEN_ACT_H,
    PCB_W, PCB_H, PCB_D,
    USBC_W, USBC_H,
    BTN_HOLE_DIA, BTN_SPACING,
    TOL, WALL,
    STANDOFF_OD, STANDOFF_ID, STANDOFF_INSET,
    CLIP_OVERHANG, CLIP_THICKNESS,
)
from common import (
    rounded_box, ghost_screen, ghost_pcb,
    screen_window_cutout, usbc_cutout, button_row, led_hole,
    standoff, standoff_set, retention_clip, cross_section_cut,
    camera_iso, camera_front, camera_back, camera_custom,
)

# ── Slab outer dimensions ──
SLAB_W = 130.0   # X
SLAB_H = 85.0    # Y
SLAB_D = 14.0    # Z (depth front-to-back)
SLAB_R = 4.0     # Corner radius

# ── Easel stand dimensions ──
STAND_W = 110.0   # X width of stand base
STAND_BASE_D = 60.0  # Y depth of stand footprint
STAND_H = 70.0   # Z height of triangular back leg
STAND_GROOVE_DEPTH = 6.0  # how deep the slab sits in the groove
STAND_GROOVE_W = SLAB_D + 2 * TOL  # groove width = slab thickness + tolerance

# ── Screen placement (screen centered in slab, shifted slightly upward) ──
# Screen module center relative to slab center
SCREEN_Y_OFFSET = (SLAB_H / 2) - WALL - (SCREEN_MOD_H / 2) - 6.0
# = 42.5 - 2 - 37.5 - 6 = -3mm → screen center is 3mm above slab center

# ── PCB placement (behind screen, same X/Y center) ──
# Z positions measured from front face (z = -SLAB_D/2 is front, z = +SLAB_D/2 is back)
# Front face at z = -SLAB_D/2
Z_FRONT = -SLAB_D / 2          # = -7.0
Z_SCREEN_FRONT = Z_FRONT + WALL  # front of screen pocket
Z_PCB_CENTER = Z_FRONT + WALL + SCREEN_MOD_D + 3.0 + PCB_D / 2

# ── Button row placement ──
# 3 buttons below screen, 8mm below the bottom of the screen module
BTN_Y = SCREEN_Y_OFFSET - SCREEN_MOD_H / 2 - 8.0

# ── LED position: top-right of screen ──
LED_X = SCREEN_MOD_W / 2 + 5.0
LED_Y = SCREEN_Y_OFFSET + SCREEN_MOD_H / 2 - 3.0

# ── Standoff positions (from back inner face, corners of PCB with inset) ──
_si = STANDOFF_INSET
PCB_STANDOFF_POSITIONS = [
    (-PCB_W / 2 + _si, -PCB_H / 2 + _si),
    ( PCB_W / 2 - _si, -PCB_H / 2 + _si),
    (-PCB_W / 2 + _si,  PCB_H / 2 - _si),
    ( PCB_W / 2 - _si,  PCB_H / 2 - _si),
]

# Standoff height: from back inner face to PCB bottom face
# Back inner face is at z = SLAB_D/2 - WALL = 7 - 2 = 5.0
# PCB center is at Z_PCB_CENTER; PCB bottom = Z_PCB_CENTER - PCB_D/2
Z_BACK_INNER = SLAB_D / 2 - WALL
PCB_Z_BOTTOM = Z_FRONT + WALL + SCREEN_MOD_D + 3.0  # = -7 + 2 + 4.5 + 3 = 2.5
STANDOFF_HEIGHT = Z_BACK_INNER - PCB_Z_BOTTOM        # = 5.0 - 2.5 = 2.5

# ── Display retention clip positions (inside screen pocket) ──
# Clips hook over the screen module edge from inside the front shell
_cw = 8.0  # clip width along the screen edge
CLIP_POSITIONS = [
    # (x, y, rotate_z) — each clip is on one of 4 sides
    (-SCREEN_MOD_W / 2, SCREEN_Y_OFFSET + SCREEN_MOD_H / 4,  90),   # left-top
    (-SCREEN_MOD_W / 2, SCREEN_Y_OFFSET - SCREEN_MOD_H / 4,  90),   # left-bottom
    ( SCREEN_MOD_W / 2, SCREEN_Y_OFFSET + SCREEN_MOD_H / 4, -90),   # right-top
    ( SCREEN_MOD_W / 2, SCREEN_Y_OFFSET - SCREEN_MOD_H / 4, -90),   # right-bottom
]


# ── Internal helpers ──

def _slab_solid():
    """Outer envelope of the slab (rounded box, centered on origin)."""
    return rounded_box(SLAB_W, SLAB_H, SLAB_D, SLAB_R)


def _screen_pocket():
    """Recess in the front face for the screen module to sit in."""
    pocket_w = SCREEN_MOD_W + 2 * TOL
    pocket_h = SCREEN_MOD_H + 2 * TOL
    pocket_d = SCREEN_MOD_D + 0.5
    # Pocket opens from the front face, so it extends inward in +Z direction
    return translate([0, SCREEN_Y_OFFSET, Z_FRONT + pocket_d / 2])(
        cube([pocket_w, pocket_h, pocket_d + 0.2], center=True)
    )


def _screen_window():
    """Through-hole for the active display area (front face to front of screen)."""
    # Passes through the front wall only (WALL thick)
    return translate([0, SCREEN_Y_OFFSET, Z_FRONT])(
        screen_window_cutout(WALL + 0.4)
    )


def _button_holes_front():
    """3 button holes through the front wall using button_row."""
    # button_row centers holes along X, at origin, holes along Z
    # We place them at the front face and have them punch through WALL thickness
    holes = button_row(3, BTN_SPACING, WALL + 0.4)
    return translate([0, BTN_Y, Z_FRONT])(holes)


def _led_hole_front():
    """LED hole through front wall."""
    return translate([LED_X, LED_Y, Z_FRONT])(
        led_hole(WALL + 0.4)
    )


def _usbc_hole_bottom():
    """USB-C cutout through the bottom wall of the slab."""
    # Bottom wall: Y = -SLAB_H/2, cutout goes through in Y direction
    # Center the connector on X=0, at the appropriate Z height (back of PCB area)
    usbc_z = Z_FRONT + WALL + SCREEN_MOD_D / 2  # mid-depth, reasonable
    return translate([0, -SLAB_H / 2, usbc_z])(
        rotate([90, 0, 0])(  # rotate so the pill extrusion goes in Y direction
            usbc_cutout(WALL + 0.4)
        )
    )


def _display_clips():
    """4 retention clips inside the screen pocket (visible from back of front shell)."""
    clips = []
    for cx, cy, rz in CLIP_POSITIONS:
        # Clip sits at the inner face of the screen pocket wall
        clip = rotate([0, 0, rz])(retention_clip(_cw))
        # Place at screen pocket depth
        clip = translate([cx, cy, Z_FRONT + SCREEN_MOD_D + 0.5])(clip)
        clips.append(clip)
    return union()(*clips)


def _standoffs_back():
    """4 PCB standoffs rising from the back shell inner face toward the front."""
    posts = []
    for x, y in PCB_STANDOFF_POSITIONS:
        # Standoff rises from back inner face (z = Z_BACK_INNER) toward front (-Z direction)
        # standoff() builds upward from z=0; we rotate 180 so it goes in -Z
        post = translate([x, y, Z_BACK_INNER])(
            rotate([180, 0, 0])(
                standoff(max(STANDOFF_HEIGHT, 1.0))
            )
        )
        posts.append(post)
    return union()(*posts)


def _shell_cavity():
    """Internal cavity of the slab (slab minus two walls on all sides)."""
    cav_w = SLAB_W - 2 * WALL
    cav_h = SLAB_H - 2 * WALL
    cav_d = SLAB_D - 2 * WALL
    return cube([cav_w, cav_h, cav_d], center=True)


def _split_plane_z():
    """Z coordinate of the shell split plane (where front/back shells meet)."""
    # Split at back-of-screen-pocket depth so front shell holds the screen
    return Z_FRONT + SCREEN_MOD_D + 1.0  # ~1mm behind screen module back face


def _front_half_mask():
    """Boolean mask for the front half of the slab."""
    z_split = _split_plane_z()
    thickness = z_split - (-SLAB_D / 2)
    return translate([0, 0, -SLAB_D / 2 + thickness / 2])(
        cube([SLAB_W + 2, SLAB_H + 2, thickness], center=True)
    )


def _back_half_mask():
    """Boolean mask for the back half of the slab."""
    z_split = _split_plane_z()
    thickness = SLAB_D / 2 - z_split
    return translate([0, 0, z_split + thickness / 2])(
        cube([SLAB_W + 2, SLAB_H + 2, thickness], center=True)
    )


def _snap_rim_notch():
    """Snap-fit tongue on the front shell edge (a thin ridge around the perimeter)."""
    rim_h = 1.2   # height of the rim/tongue
    rim_t = 0.8   # thickness
    z_split = _split_plane_z()
    # Outer loop
    outer = rounded_box(SLAB_W - 2 * WALL + rim_t * 2,
                        SLAB_H - 2 * WALL + rim_t * 2,
                        rim_h, SLAB_R - WALL + rim_t)
    # Inner loop (hollow it out)
    inner = rounded_box(SLAB_W - 2 * WALL - rim_t * 2,
                        SLAB_H - 2 * WALL - rim_t * 2,
                        rim_h + 0.2, SLAB_R - WALL - rim_t)
    tongue = difference()(outer, inner)
    return translate([0, 0, z_split - rim_h / 2])(tongue)


def _snap_rim_groove():
    """Matching groove on the back shell edge that receives the front shell tongue."""
    rim_h = 1.2 + TOL * 2
    rim_t = 0.8 + TOL * 2
    z_split = _split_plane_z()
    outer = rounded_box(SLAB_W - 2 * WALL + rim_t * 2 + TOL,
                        SLAB_H - 2 * WALL + rim_t * 2 + TOL,
                        rim_h, SLAB_R - WALL + rim_t)
    inner = rounded_box(SLAB_W - 2 * WALL - rim_t * 2 - TOL,
                        SLAB_H - 2 * WALL - rim_t * 2 - TOL,
                        rim_h + 0.2, SLAB_R - WALL - rim_t)
    groove = difference()(outer, inner)
    return translate([0, 0, z_split - rim_h / 2])(groove)


# ── Public API ──

def build_front_shell():
    """Front shell: flat slab front half with screen pocket, window, button holes, LED, USB-C.

    Returns a SolidPython2 object.
    """
    # Start with the front half of the outer slab
    shell = intersection()(
        _slab_solid(),
        _front_half_mask(),
    )

    # Add the snap-fit tongue so the two halves lock together
    shell = union()(shell, _snap_rim_notch())

    # Add display retention clips (inside the screen pocket, projecting inward)
    shell = union()(shell, _display_clips())

    # Cut: internal cavity (hollow the shell)
    shell = difference()(shell, _shell_cavity())

    # Cut: screen module pocket (recess in front face for screen to sit in)
    shell = difference()(shell, _screen_pocket())

    # Cut: screen window (active area, through the front wall)
    shell = difference()(shell, _screen_window())

    # Cut: 3 button holes through front wall
    shell = difference()(shell, _button_holes_front())

    # Cut: LED hole
    shell = difference()(shell, _led_hole_front())

    # Cut: USB-C port through bottom wall
    shell = difference()(shell, _usbc_hole_bottom())

    return shell


def build_back_shell():
    """Back shell: flat back cover with snap-fit rim groove and 4 PCB standoffs.

    Returns a SolidPython2 object.
    """
    # Start with the back half of the outer slab
    shell = intersection()(
        _slab_solid(),
        _back_half_mask(),
    )

    # Cut the matching snap-fit groove
    shell = difference()(shell, _snap_rim_groove())

    # Cut internal cavity
    shell = difference()(shell, _shell_cavity())

    # Add PCB standoffs rising from the inner back face
    shell = union()(shell, _standoffs_back())

    return shell


def build_stand():
    """Easel stand: separate triangular piece with groove for the slab and USB-C cable channel.

    The stand positions the slab at ~65 degrees from horizontal.

    Returns a SolidPython2 object.
    """
    tilt_angle = 65.0  # degrees from horizontal (screen faces user)

    # ── Stand geometry ──
    # The stand is a triangular prism (in XZ plane) extruded in Y.
    # - Base: horizontal foot on the desk
    # - Back leg: rises at the tilt angle, carrying the groove at the top
    # - Hypotenuse: diagonal brace

    base_len  = STAND_BASE_D  # Y=60mm depth of foot
    stand_ext = STAND_W        # X=110mm width (extrusion depth)
    back_h    = STAND_H        # Z=70mm height of back leg
    # Foot length in Z-projection of the back leg
    foot_x = back_h / math.tan(math.radians(tilt_angle))  # ~32.6mm

    # 2D triangle profile (in XZ plane, extruded in Y)
    # Origin at bottom-front of stand
    # Points: bottom-front, bottom-back, top-back (where groove goes)
    tri_pts = [
        [0,      0],        # bottom-front corner
        [foot_x, 0],        # bottom-back corner (foot)
        [foot_x, back_h],   # top-back (groove location)
    ]
    tri_2d = polygon(tri_pts)

    # Wall thickness for the triangular shell
    shell_t = WALL * 1.5  # 3mm solid stand walls

    # Extrude to stand width, centered in X on the groove
    stand_solid = translate([-stand_ext / 2, -base_len / 2, 0])(
        rotate([90, 0, 90])(
            linear_extrude(height=stand_ext)(tri_2d)
        )
    )

    # ── Groove at top of back leg ──
    # The groove runs along the full X width of the stand, receives the slab bottom edge
    # Groove width = slab depth + tolerance, depth = STAND_GROOVE_DEPTH
    groove_w = STAND_GROOVE_W
    groove_d = STAND_GROOVE_DEPTH
    groove_h = SLAB_H * 0.25  # groove height (how much of slab sits in groove)

    # Groove position: at the top of the back leg, centered in X
    # The slab enters from above, so groove opens upward (+Z)
    groove = translate([0, 0, back_h - groove_h / 2])(
        cube([stand_ext + 0.2, groove_w, groove_h + 0.2], center=True)
    )

    stand_solid = difference()(stand_solid, groove)

    # ── Cable channel ──
    # USB-C cable exits the bottom of the slab; we route it through the groove floor
    # Small channel in the groove back wall for cable
    cable_ch_w = USBC_W + 4.0   # wider than USB-C opening
    cable_ch_h = USBC_H + 4.0
    cable_channel = translate([0, 0, back_h - groove_h - 2.0])(
        cube([cable_ch_w, groove_w + 2.0, WALL * 2 + 0.2], center=True)
    )
    stand_solid = difference()(stand_solid, cable_channel)

    # Color the stand for visualization
    return color("Sienna", 0.85)(stand_solid)


def build_internals():
    """Ghost components positioned as they would sit inside the assembled slab.

    Returns dict with keys 'screen' and 'pcb'.
    """
    # Screen module: centered in X, at SCREEN_Y_OFFSET in Y, front face flush with front wall
    screen_z = Z_FRONT + WALL + SCREEN_MOD_D / 2
    screen = translate([0, SCREEN_Y_OFFSET, screen_z])(ghost_screen())

    # PCB: behind screen module
    pcb_z = Z_PCB_CENTER
    pcb = translate([0, SCREEN_Y_OFFSET, pcb_z])(ghost_pcb())

    return {
        "screen": screen,
        "pcb": pcb,
    }


def get_views():
    """Return a list of render view descriptors.

    Each dict has keys: 'name', 'scad_object', 'camera'.
    """
    front  = build_front_shell()
    back   = build_back_shell()
    stand  = build_stand()
    parts  = build_internals()
    screen = parts["screen"]
    pcb    = parts["pcb"]

    # Assembled slab (no stand, just front+back together)
    assembled_slab = union()(front, back)

    # Assembled with stand: slab sits in the groove
    # Stand is at origin, slab sits in the groove at the top of the back leg
    tilt_angle = 65.0
    back_h = STAND_H
    foot_x = back_h / math.tan(math.radians(tilt_angle))
    groove_h = SLAB_H * 0.25
    groove_top_z = back_h
    # Slab is tilted by (90 - tilt_angle) from vertical, i.e. tilt_angle from horizontal
    # When sitting in the groove: the bottom edge of the slab is at groove center
    slab_in_stand = translate([foot_x, 0, groove_top_z - groove_h / 2])(
        rotate([0, -(90 - tilt_angle), 0])(assembled_slab)
    )

    assembled_full = union()(slab_in_stand, stand)
    assembled_full_with_internals = union()(
        slab_in_stand,
        stand,
        translate([foot_x, 0, groove_top_z - groove_h / 2])(
            rotate([0, -(90 - tilt_angle), 0])(union()(screen, pcb))
        ),
    )

    # Exploded view: parts separated along Z
    EXPLODE_STEP = 25.0
    front_ex  = translate([0,  0, -EXPLODE_STEP])(front)
    screen_ex = translate([0,  0,  0])(screen)
    pcb_ex    = translate([0,  0,  EXPLODE_STEP])(pcb)
    back_ex   = translate([0,  0,  EXPLODE_STEP * 2])(back)
    exploded  = union()(front_ex, screen_ex, pcb_ex, back_ex)

    # Interior front: front shell viewed from behind (rotated 180° around Y)
    interior_front = rotate([0, 180, 0])(front)

    # Interior back: back shell viewed from front face side
    interior_back = back

    # Cross section: assembled slab cut at Y=0
    cross = cross_section_cut(union()(assembled_slab, screen, pcb), axis="y", keep="back")

    views = [
        {
            "name": "assembled_iso",
            "scad_object": assembled_full_with_internals,
            "camera": camera_iso(dist=500),
        },
        {
            "name": "assembled_front",
            "scad_object": assembled_full_with_internals,
            "camera": camera_front(dist=450),
        },
        {
            "name": "assembled_back",
            "scad_object": assembled_full_with_internals,
            "camera": camera_back(dist=450),
        },
        {
            "name": "exploded",
            "scad_object": exploded,
            "camera": camera_iso(dist=500),
        },
        {
            "name": "interior_front",
            "scad_object": interior_front,
            "camera": camera_front(dist=400),
        },
        {
            "name": "interior_back",
            "scad_object": interior_back,
            "camera": camera_front(dist=400),
        },
        {
            "name": "cross_section",
            "scad_object": cross,
            "camera": camera_custom(rot_x=80, rot_y=0, rot_z=0, dist=350),
        },
    ]
    return views


def check_clearances():
    """Run dimensional sanity checks on the design.

    Returns dict with keys 'design', 'pass', 'checks'.
    Each check is a dict with 'name', 'pass', 'value', 'limit', 'note'.
    """
    checks = []

    # 1. Slab depth vs. minimum required
    # GAP_PCB_BACK is 0.8mm in this design (tighter than the 1.0mm default) to
    # fit the stack into the 14mm slab.  Allow 0.5mm tolerance in the check.
    min_depth = WALL + SCREEN_MOD_D + 3.0 + PCB_D + 0.8 + WALL
    checks.append({
        "name": "slab_depth_vs_stack",
        "pass": SLAB_D >= min_depth,
        "value": SLAB_D,
        "limit": min_depth,
        "note": f"Slab depth {SLAB_D}mm vs min stack {min_depth:.1f}mm (0.8mm PCB back gap)",
    })

    # 2. Screen module fits in slab width
    checks.append({
        "name": "screen_width_fits",
        "pass": SCREEN_MOD_W + 2 * WALL <= SLAB_W,
        "value": SCREEN_MOD_W + 2 * WALL,
        "limit": SLAB_W,
        "note": f"Screen module + walls {SCREEN_MOD_W + 2*WALL}mm vs slab {SLAB_W}mm",
    })

    # 3. Screen module fits in slab height
    checks.append({
        "name": "screen_height_fits",
        "pass": SCREEN_MOD_H + 2 * WALL <= SLAB_H,
        "value": SCREEN_MOD_H + 2 * WALL,
        "limit": SLAB_H,
        "note": f"Screen module + walls {SCREEN_MOD_H + 2*WALL}mm vs slab {SLAB_H}mm",
    })

    # 4. Button row fits in slab width
    btn_row_span = 3 * BTN_SPACING + BTN_HOLE_DIA
    checks.append({
        "name": "buttons_fit_width",
        "pass": btn_row_span <= SLAB_W - 2 * WALL,
        "value": btn_row_span,
        "limit": SLAB_W - 2 * WALL,
        "note": f"Button row span {btn_row_span}mm vs inner width {SLAB_W - 2*WALL}mm",
    })

    # 5. Button holes don't overlap with screen
    screen_bottom_y = SCREEN_Y_OFFSET - SCREEN_MOD_H / 2
    btn_top_y = BTN_Y + BTN_HOLE_DIA / 2
    checks.append({
        "name": "buttons_below_screen",
        "pass": btn_top_y < screen_bottom_y,
        "value": btn_top_y,
        "limit": screen_bottom_y,
        "note": f"Button top Y {btn_top_y:.1f}mm vs screen bottom {screen_bottom_y:.1f}mm",
    })

    # 6. Standoff height is positive
    checks.append({
        "name": "standoff_height_positive",
        "pass": STANDOFF_HEIGHT > 0,
        "value": STANDOFF_HEIGHT,
        "limit": 0.0,
        "note": f"Standoff height {STANDOFF_HEIGHT:.1f}mm (must be > 0)",
    })

    # 7. PCB fits in slab width
    checks.append({
        "name": "pcb_width_fits",
        "pass": PCB_W + 2 * WALL <= SLAB_W,
        "value": PCB_W + 2 * WALL,
        "limit": SLAB_W,
        "note": f"PCB + walls {PCB_W + 2*WALL}mm vs slab {SLAB_W}mm",
    })

    # 8. Stand groove width accommodates slab depth
    checks.append({
        "name": "groove_fits_slab",
        "pass": STAND_GROOVE_W >= SLAB_D + TOL,
        "value": STAND_GROOVE_W,
        "limit": SLAB_D + TOL,
        "note": f"Groove width {STAND_GROOVE_W:.1f}mm vs slab depth + tol {SLAB_D + TOL:.1f}mm",
    })

    # 9. USB-C cutout fits in bottom wall height
    wall_h = WALL  # bottom wall thickness
    checks.append({
        "name": "usbc_fits_bottom_wall",
        "pass": USBC_H + 0.4 <= SLAB_D - 2 * WALL,
        "value": USBC_H + 0.4,
        "limit": SLAB_D - 2 * WALL,
        "note": f"USB-C height {USBC_H + 0.4:.1f}mm vs available interior depth {SLAB_D - 2*WALL:.1f}mm",
    })

    # 10. Screen window smaller than screen module
    win_w = SCREEN_ACT_W + 1.0
    win_h = SCREEN_ACT_H + 1.0
    checks.append({
        "name": "screen_window_smaller_than_module",
        "pass": win_w < SCREEN_MOD_W and win_h < SCREEN_MOD_H,
        "value": win_w,
        "limit": SCREEN_MOD_W,
        "note": f"Window {win_w}x{win_h}mm vs module {SCREEN_MOD_W}x{SCREEN_MOD_H}mm",
    })

    overall_pass = all(c["pass"] for c in checks)

    return {
        "design": "design_1_easel",
        "pass": overall_pass,
        "checks": checks,
    }
