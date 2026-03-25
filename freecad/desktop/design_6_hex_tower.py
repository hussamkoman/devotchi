"""Desktop Devotchi — Design 6: Hex Tower.

Hexagonal prism standing upright on one flat face. 130mm flat-to-flat, 160mm tall.
Geometric, sculptural desk art. Screen on front facet (portrait), buttons on adjacent
facet (60° away), speaker grille on opposite-buttons facet (120° from screen),
USB-C through the bottom face. Removable hex top cap.
"""
import math

from solid2 import (
    cube, cylinder, hull, union, difference, intersection,
    translate, rotate, color, linear_extrude, polygon,
)

from params import (
    SCREEN_MOD_W, SCREEN_MOD_H, SCREEN_MOD_D,
    SCREEN_ACT_W, SCREEN_ACT_H,
    PCB_W, PCB_H, PCB_D,
    ESP32_D,
    SPEAKER_DIA, SPEAKER_D,
    USBC_W, USBC_H,
    BTN_HOLE_DIA, BTN_SPACING,
    STANDOFF_OD, STANDOFF_ID, STANDOFF_INSET,
    SCREW_BOSS_OD, SCREW_BOSS_ID,
    WALL, TOL,
)
from common import (
    ghost_screen, ghost_pcb, ghost_speaker,
    screen_window_cutout, usbc_cutout, button_hole, button_column,
    speaker_grille, standoff, standoff_set, screw_boss, rubber_foot,
    camera_iso, camera_iso_back, camera_front, camera_back, camera_top, camera_custom,
    cross_section_cut, explode_parts,
)

# ── Hex Tower dimensions ──
HEX_FLAT   = 130.0          # Flat-to-flat diameter (mm)
HEX_H      = 160.0          # Total tower height (mm)
WALL_HEX   = 2.5            # Hex wall thickness

# Circumradius: distance from centre to corner
# For a regular hexagon: R = flat_to_flat / (2 * cos(30°))
# equivalently: R = flat_to_flat / sqrt(3)
HEX_R = HEX_FLAT / math.sqrt(3)          # ≈ 75.06 mm (corner-to-corner radius)
HEX_INNER_R = HEX_R - WALL_HEX           # Inner corner-to-corner radius

# Apothem = flat_to_flat / 2 (distance from centre to mid-face)
HEX_APO = HEX_FLAT / 2.0                 # 65.0 mm

# ── Facet layout (angles around Z axis, 0 = front facet faces +Y) ──
# The hex has 6 faces. We orient so one face's outward normal points exactly +Y.
# Facet normals are at angles: 90°, 30°, -30°, -90°, -150°, 150° from +X
# i.e. 90, 30, 330, 270, 210, 150  (in standard maths convention, CCW from +X)
# We choose:
#   Facet 0 (screen)   — normal at  +Y  → angle 90°   from +X  → face rotation 90°
#   Facet 1 (buttons)  — adjacent CCW → angle 30°    from +X  → face rotation 30°
#   Facet 2            — angle 330° (−30°) — unused
#   Facet 3 (speaker)  — opposite buttons → angle 210° from +X (= 30° + 180°)
#   Facet 4            — angle 270° (−90°)
#   Facet 5            — angle 150°

SCREEN_FACET_ANGLE  =  90.0   # degrees from +X (outward normal direction)
BUTTON_FACET_ANGLE  =  30.0   # 60° CCW from screen
SPEAKER_FACET_ANGLE = 210.0   # opposite of button facet (30° + 180°)

# ── Top cap ──
CAP_H       = 8.0             # Total cap height
CAP_RIM_H   = 5.0             # Rim insertion depth into body
CAP_RIM_TOL = 0.3             # Clearance between rim and body inner wall

# ── Screen window position on screen facet ──
# Portrait: 120mm vertical (along Z / tower height), 75mm horizontal (across facet)
# Facet width (chord at apothem) = flat_to_flat = 130mm → 75mm fits fine
# Centre window vertically at mid-height, with some bottom offset for a bezel feel
SCREEN_WIN_W   = SCREEN_ACT_W + 1.0     # 109 mm (horizontal across facet)
SCREEN_WIN_H   = SCREEN_ACT_H + 1.0     #  65.8 mm (vertical along tower)
SCREEN_WIN_Z   = 0.0                     # Vertically centred at tower mid-height

# ── Button column on button facet ──
BTN_N        = 3
BTN_SPACING_LOCAL = 15.0      # Centre-to-centre vertical spacing (mm)
BTN_Z_CENTRE = 0.0            # Centred at tower mid-height

# ── Speaker grille on speaker facet ──
SPEAKER_GRILLE_DIA = 40.0     # Diameter of perforated area

# ── Rubber feet ──
FOOT_DIA    = 8.0
FOOT_H      = 1.5
# Feet at three alternating hex corners on the bottom face
# Hex corners are at radius HEX_R, at 30°, 90°, 150°, 210°, 270°, 330° from +X
# Use alternating corners: 30°, 150°, 270°  (every other corner)
FOOT_ANGLES = [30.0, 150.0, 270.0]
FOOT_INSET  = 5.0             # Inward from corner along apothem direction

# ── PCB standoffs ──
STANDOFF_H  = 10.0            # Height of PCB standoffs from bottom inner floor


# ── Internal geometry helpers ──

def _hex_prism(flat, h, center=True):
    """Solid hexagonal prism, flat-to-flat = flat, height = h.

    A regular hexagon.  The cylinder _fn=6 in OpenSCAD/SolidPython2
    produces a hexagonal prism whose 'diameter' argument is the
    corner-to-corner (circumscribed circle) diameter.
    Without rotation, vertices at 0°,60°,…,300° and face normals at
    30°,90°,150°,210°,270°,330° — matching our feature placement angles.
    """
    R = flat / math.sqrt(3)   # circumradius
    return cylinder(d=R * 2, h=h, center=center, _fn=6)


def _hex_body_solid():
    """Full solid hex prism, centred at origin, standing along Z."""
    return _hex_prism(HEX_FLAT, HEX_H, center=True)


def _hex_body_hollow():
    """Hollow hex shell (outer minus inner), open top, closed bottom.

    The bottom is at Z = -HEX_H/2.  The top is open.
    """
    outer = _hex_prism(HEX_FLAT, HEX_H + 0.02, center=True)

    # Inner void: slightly shorter so bottom wall remains intact.
    # Bottom wall = WALL_HEX, so inner starts at -HEX_H/2 + WALL_HEX
    inner_h = HEX_H - WALL_HEX             # leaves bottom wall
    inner_flat = HEX_FLAT - WALL_HEX * 2   # inset by wall thickness (flat-to-flat)
    inner = translate([0, 0, WALL_HEX / 2])(  # shift up so bottom is at -HEX_H/2+WALL_HEX
        _hex_prism(inner_flat, inner_h + 0.02, center=True)
    )
    return difference()(outer, inner)


def _facet_center_y(angle_deg, apo=HEX_APO):
    """Return the (x, y) midpoint of a facet whose outward normal is at angle_deg."""
    rad = math.radians(angle_deg)
    return (apo * math.cos(rad), apo * math.sin(rad))


def _cutout_on_facet(angle_deg, cutout_solid):
    """Place a cutout at the facet surface and rotate it to face outward.

    The cutout is defined in "facet-local" coordinates:
      - X: horizontal across the facet
      - Z: vertical (same as world Z, i.e. tower height axis)
      - Y: depth into the wall (normal to facet, pointing inward)

    The facet outward normal points at angle_deg from +X.
    The cutout centre is placed at the facet mid-surface and then the
    difference() call in the caller removes material.
    """
    # Rotate about Z so the facet normal aligns with +Y, then translate to surface
    return rotate([0, 0, angle_deg - 90])(
        translate([0, HEX_APO, 0])(
            rotate([90, 0, 0])(          # tip the cutout so it points into the wall
                cutout_solid
            )
        )
    )


# ── Screen window cutout ──

def _screen_window_cutout_3d():
    """Box cutout for the screen active area through the screen facet wall."""
    depth = WALL_HEX + 2.0   # punch all the way through with margin
    win = cube([SCREEN_WIN_W, SCREEN_WIN_H, depth + 0.2], center=True)
    return win


def _screen_window_on_body():
    """Screen window positioned on the screen facet, centred vertically."""
    cutout = _screen_window_cutout_3d()
    return translate([0, 0, SCREEN_WIN_Z])(
        _cutout_on_facet(SCREEN_FACET_ANGLE, cutout)
    )


# ── Button holes ──

def _button_holes_on_body():
    """3 button holes in a vertical column on the button facet."""
    depth = WALL_HEX + 2.0
    holes = []
    start_z = -(BTN_N - 1) * BTN_SPACING_LOCAL / 2
    for i in range(BTN_N):
        z = start_z + i * BTN_SPACING_LOCAL
        hole = translate([0, 0, z + BTN_Z_CENTRE])(
            _cutout_on_facet(
                BUTTON_FACET_ANGLE,
                cylinder(d=BTN_HOLE_DIA, h=depth + 0.2, center=True, _fn=32)
            )
        )
        holes.append(hole)
    return union()(*holes)


# ── Speaker grille ──

def _speaker_grille_on_body():
    """Hex grid of speaker holes on the speaker facet."""
    depth = WALL_HEX + 2.0
    grille = speaker_grille(SPEAKER_GRILLE_DIA, hole_dia=2.0, spacing=4.0)
    return _cutout_on_facet(
        SPEAKER_FACET_ANGLE,
        translate([0, 0, 0])(grille)
    )


# ── USB-C cutout through the bottom face ──

def _usbc_cutout_bottom():
    """Pill-shaped USB-C cutout centred in the bottom hex face, pointing up through it."""
    depth = WALL_HEX + 1.0
    from common import pill_2d
    slot = linear_extrude(height=depth + 0.2, center=True)(
        pill_2d(USBC_W + 0.4, USBC_H + 0.4)
    )
    # Bottom face is at Z = -HEX_H/2; we want to cut straight up through the floor
    return translate([0, 0, -HEX_H / 2 + (depth + 0.2) / 2])(slot)


# ── Screw bosses for cap on inner top rim ──

def _cap_screw_bosses():
    """6 screw bosses equally spaced around the inner top rim for cap attachment."""
    bosses = []
    boss_h = CAP_RIM_H + 2.0
    inner_flat = HEX_FLAT - WALL_HEX * 2
    inner_apo = inner_flat / 2.0 - SCREW_BOSS_OD / 2 - 1.0
    for i in range(6):
        angle = math.radians(i * 60)
        x = inner_apo * math.cos(angle)
        y = inner_apo * math.sin(angle)
        z_base = HEX_H / 2 - WALL_HEX - boss_h
        bosses.append(
            translate([x, y, z_base])(
                screw_boss(boss_h)
            )
        )
    return union()(*bosses)


# ── Rubber feet on the bottom ──

def _rubber_feet():
    """3 rubber feet at alternating hex corners on the bottom face."""
    feet = []
    for angle_deg in FOOT_ANGLES:
        rad = math.radians(angle_deg)
        # Corner is at HEX_R from centre; move inward by FOOT_INSET
        r = HEX_R - FOOT_INSET
        x = r * math.cos(rad)
        y = r * math.sin(rad)
        feet.append(
            translate([x, y, -HEX_H / 2])(
                rubber_foot(FOOT_DIA, FOOT_H)
            )
        )
    return union()(*feet)


# ── PCB standoffs inside the tower ──

def _pcb_standoffs():
    """4 standoffs for PCB mounted vertically inside the tower.

    The PCB (110 x 65mm) is mounted on the XZ plane at Y = -HEX_APO + WALL_HEX + SCREEN_MOD_D.
    Standoffs rise from the bottom floor.
    """
    pcb_y = -(HEX_APO - WALL_HEX - SCREEN_MOD_D - PCB_D / 2)
    # Standoffs are at PCB corners inset by STANDOFF_INSET
    sx = PCB_W / 2 - STANDOFF_INSET
    sz = PCB_H / 2 - STANDOFF_INSET
    z_base = -HEX_H / 2 + WALL_HEX
    posts = []
    for sign_x in [-1, 1]:
        for sign_z in [-1, 1]:
            posts.append(
                translate([sign_x * sx, pcb_y, z_base + sign_z * sz + PCB_H / 2])(
                    rotate([90, 0, 0])(
                        standoff(STANDOFF_H)
                    )
                )
            )
    return union()(*posts)


# ── Public build functions ──

def build_front_shell():
    """Main hex tower body: hollow prism, open top, with all cutouts.

    Includes screen window, button holes, speaker grille, USB-C on bottom,
    and screw bosses for cap attachment.
    """
    body = _hex_body_hollow()

    # Screen window on front facet
    body = difference()(body, _screen_window_on_body())

    # Button holes on button facet
    body = difference()(body, _button_holes_on_body())

    # Speaker grille on speaker facet
    body = difference()(body, _speaker_grille_on_body())

    # USB-C cutout on bottom face
    body = difference()(body, _usbc_cutout_bottom())

    # Add screw bosses for cap (positive addition)
    body = union()(body, _cap_screw_bosses())

    return color("SlateGray", 0.9)(body)


def build_back_shell():
    """Removable hexagonal top cap.

    8mm tall outer hex cap with a 5mm deep rim that inserts into the body.
    6 screw holes in the rim for M2.5 screws into the bosses.
    """
    # Outer cap: same flat-to-flat as tower, CAP_H tall
    outer_cap = _hex_prism(HEX_FLAT, CAP_H, center=False)

    # Rim: slightly undersize hex that inserts 5mm into the body
    rim_flat = HEX_FLAT - WALL_HEX * 2 - CAP_RIM_TOL * 2
    rim = translate([0, 0, -CAP_RIM_H])(
        _hex_prism(rim_flat, CAP_RIM_H + 0.02, center=False)
    )
    cap_solid = union()(outer_cap, rim)

    # Hollow out inside of outer cap (leave top wall = WALL_HEX)
    inner_hollow_flat = HEX_FLAT - WALL_HEX * 2
    inner_hollow = translate([0, 0, WALL_HEX])(
        _hex_prism(inner_hollow_flat, CAP_H - WALL_HEX + 0.02, center=False)
    )
    cap = difference()(cap_solid, inner_hollow)

    # Screw holes through the rim
    inner_apo_boss = (HEX_FLAT - WALL_HEX * 2) / 2.0 - SCREW_BOSS_OD / 2 - 1.0
    for i in range(6):
        angle = math.radians(i * 60)
        x = inner_apo_boss * math.cos(angle)
        y = inner_apo_boss * math.sin(angle)
        screw_hole = translate([x, y, -CAP_RIM_H - 0.1])(
            cylinder(d=SCREW_BOSS_ID, h=CAP_RIM_H + CAP_H + 0.2, center=False, _fn=24)
        )
        cap = difference()(cap, screw_hole)

    # Shift cap to sit on top of tower
    cap = translate([0, 0, HEX_H / 2])(cap)

    return color("DimGray", 0.85)(cap)


def build_stand():
    """No separate stand — the tower sits directly on its flat bottom face."""
    return None


def build_internals():
    """Ghost components positioned inside the tower.

    Returns a dict with keys 'screen', 'pcb', 'speaker'.

    The screen module is mounted on the inner surface of the screen facet.
    The PCB is parallel to and behind the screen module.
    The speaker sits near the speaker facet.
    """
    # ── Screen ──
    # Screen facet inner surface at Y = +(HEX_APO - WALL_HEX) in world coords
    # (screen facet normal points +Y; inner surface is at HEX_APO - WALL_HEX from centre)
    # Screen module: width along X, height along Z, depth along Y
    # Front face of screen module flush with inner facet surface
    screen_y = HEX_APO - WALL_HEX - SCREEN_MOD_D / 2
    screen_inner = translate([0, screen_y, SCREEN_WIN_Z])(
        # Ghost screen: by default it's 120x75x4.5, XY plane
        # Rotate so it lies on the YZ plane (face pointing +Y = outward)
        rotate([90, 0, 0])(
            ghost_screen()
        )
    )

    # ── PCB ──
    # PCB behind screen module, parallel to screen facet
    # PCB is 110x65mm (XZ plane), depth along Y
    pcb_y = HEX_APO - WALL_HEX - SCREEN_MOD_D - 3.0 - PCB_D / 2
    pcb_inner = translate([0, pcb_y, 0])(
        rotate([90, 0, 0])(
            ghost_pcb()
        )
    )

    # ── Speaker ──
    # Speaker facet inner surface at Y = -(HEX_APO - WALL_HEX) (pointing -Y)
    # Speaker disc faces the speaker facet
    speaker_y = -(HEX_APO - WALL_HEX - SPEAKER_D / 2)
    speaker_inner = translate([0, speaker_y, 0])(
        rotate([90, 0, 0])(
            ghost_speaker()
        )
    )

    return {
        "screen":  screen_inner,
        "pcb":     pcb_inner,
        "speaker": speaker_inner,
    }


def _all_parts_assembled():
    """Union of all shell parts in assembled position."""
    front = build_front_shell()
    back  = build_back_shell()
    internals = build_internals()
    parts = [front, back, internals["screen"], internals["pcb"], internals["speaker"]]
    return union()(*parts)


def get_views():
    """Return 7 view definitions as a dict of {name: (solid, camera_string)}."""
    front     = build_front_shell()
    back      = build_back_shell()
    internals = build_internals()
    screen    = internals["screen"]
    pcb       = internals["pcb"]
    speaker   = internals["speaker"]
    assembled = union()(front, back)

    views = {}

    # Face normals (no 30° rotation): 30°, 90°, 150°, 210°, 270°, 330°
    # To see facet at angle θ straight-on: rot_z = 90° - θ, rot_x = 90°
    # For ISO with 15° elevation: rot_x = 75°, add ±10° to rot_z for 3/4 view

    # 1. ISO assembled — screen facet (90°) with adjacent button facet visible
    #    rot_z≈235 shows screen window centered with button facet to the right
    views["iso_assembled"] = (
        assembled,
        camera_custom(65, 0, 235, dist=550),
    )

    # 2. Screen facet — face normal at 90° (+Y).
    #    rot_z rotates scene; to bring 90° face to camera: rot_z = θ - 90 = 0
    #    Empirically verified: rot_z≈245 shows the screen window on the front face
    views["screen_facet"] = (
        front,
        camera_custom(75, 0, 245, dist=500),
    )

    # 3. Button facet — face normal at 30°. rot_z=65 confirmed shows buttons
    views["button_facet"] = (
        assembled,
        camera_custom(75, 0, 65, dist=500),
    )

    # 4. Speaker facet — face normal at 210°. rot_z=5 confirmed shows speaker
    views["speaker_facet"] = (
        assembled,
        camera_custom(75, 0, 5, dist=500),
    )

    # 5. Top view — looking straight down at hex cap (rot_x=0 sees +Z = top)
    views["top"] = (
        assembled,
        camera_custom(0, 0, 0, dist=400),
    )

    # 6. Bottom — USB-C and rubber feet (rot_x=180 sees -Z = bottom)
    views["bottom"] = (
        union()(front, _rubber_feet()),
        camera_custom(180, 0, 0, dist=400),
    )

    # 7. Cross-section — cut along X axis, view from -X into cut
    #    rot_z=270 + rot_x=80 looks from -X direction with slight elevation
    full_with_internals = union()(front, back, screen, pcb, speaker)
    views["cross_section"] = (
        cross_section_cut(full_with_internals, axis="x", keep="back"),
        camera_custom(80, 0, 275, dist=550),
    )

    return views


def check_clearances():
    """Validate key dimensional constraints.

    Returns a dict of {check_name: (value, limit, passed)}.
    """
    results = {}

    # Screen window width fits within facet width (flat-to-flat dimension)
    results["screen_win_width_vs_facet"] = (
        SCREEN_WIN_W, HEX_FLAT - WALL_HEX * 2,
        SCREEN_WIN_W <= HEX_FLAT - WALL_HEX * 2,
    )

    # Screen window height fits within tower height (with top/bottom margins)
    margin_z = 10.0
    results["screen_win_height_vs_tower"] = (
        SCREEN_WIN_H, HEX_H - margin_z * 2,
        SCREEN_WIN_H <= HEX_H - margin_z * 2,
    )

    # Screen module depth + PCB stack fits inside apothem
    inner_apo = HEX_APO - WALL_HEX
    depth_stack = SCREEN_MOD_D + 3.0 + PCB_D + 3.0   # screen + gap + pcb + rear clearance
    results["screen_pcb_depth_vs_inner_apo"] = (
        depth_stack, inner_apo,
        depth_stack <= inner_apo,
    )

    # Speaker grille diameter fits on its facet
    results["speaker_grille_vs_facet"] = (
        SPEAKER_GRILLE_DIA, HEX_FLAT - WALL_HEX * 4,
        SPEAKER_GRILLE_DIA <= HEX_FLAT - WALL_HEX * 4,
    )

    # Button column span fits within tower height (with margins)
    btn_span = (BTN_N - 1) * BTN_SPACING_LOCAL
    results["button_span_vs_tower"] = (
        btn_span, HEX_H - WALL_HEX * 4,
        btn_span <= HEX_H - WALL_HEX * 4,
    )

    # Cap rim insertion depth does not exceed inner height clearance at top
    results["cap_rim_insertion"] = (
        CAP_RIM_H, HEX_H / 2,
        CAP_RIM_H <= HEX_H / 2,
    )

    # Hex inner flat-to-flat large enough for screen module width
    inner_flat = HEX_FLAT - WALL_HEX * 2
    results["inner_flat_vs_screen_mod_w"] = (
        SCREEN_MOD_W, inner_flat,
        SCREEN_MOD_W <= inner_flat,
    )

    # Hex inner flat-to-flat large enough for PCB width
    results["inner_flat_vs_pcb_w"] = (
        PCB_W, inner_flat,
        PCB_W <= inner_flat,
    )

    return results


# ── Main: render default assembled view ──

if __name__ == "__main__":
    import os
    from solid2 import scad_render_to_file

    out_dir = os.path.join(os.path.dirname(__file__), "output")
    os.makedirs(out_dir, exist_ok=True)

    # Print clearance report
    print("=== Hex Tower — Clearance Check ===")
    for name, (value, limit, passed) in check_clearances().items():
        status = "OK" if passed else "FAIL"
        print(f"  [{status}] {name}: {value:.2f} vs limit {limit:.2f}")

    # Render all views
    views = get_views()
    for view_name, (solid, camera) in views.items():
        out_path = os.path.join(out_dir, f"design_6_hex_tower_{view_name}.scad")
        scad_render_to_file(solid, out_path)
        print(f"  Wrote {out_path}")

    print("Done.")
