"""Desktop Devotchi — Design 5: Retro TV.

Miniature CRT television — 165 x 115 x 65mm. Thick bezels around a
rounded-corner screen window in landscape orientation. Speaker grille on
the left side panel. Knob-style button holes on the right side panel.
Two small feet splayed outward at the bottom. Decorative antenna stubs on top.
"""
import math

from solid2 import (
    cube, cylinder, hull, union, difference, intersection,
    translate, rotate, color, linear_extrude, mirror,
)

from params import (
    SCREEN_MOD_W, SCREEN_MOD_H, SCREEN_MOD_D,
    SCREEN_ACT_W, SCREEN_ACT_H,
    PCB_W, PCB_H, PCB_D,
    ESP32_D,
    USBC_W, USBC_H,
    BTN_HOLE_DIA,
    WALL, TOL,
    STANDOFF_OD, STANDOFF_ID, STANDOFF_INSET,
    SCREW_BOSS_OD, SCREW_BOSS_ID,
    SPEAKER_DIA, SPEAKER_D,
)
from common import (
    rounded_box, rounded_box_xy,
    pill_2d,
    ghost_screen, ghost_pcb, ghost_speaker,
    usbc_cutout, standoff_set, screw_boss,
    camera_iso, camera_front, camera_back, camera_top, camera_custom,
    cross_section_cut, explode_parts,
)

# ── TV body dimensions ──
TV_W    = 165.0     # Width  (landscape)
TV_H    = 115.0     # Height
TV_D    = 65.0      # Depth  (front to back)
TV_R    = 10.0      # Corner radius

WALL_TV = 2.5       # Beefier wall thickness for this design

# ── Front bezel dimensions ──
# Screen window opening: SCREEN_ACT_W + clearance, SCREEN_ACT_H + clearance
WIN_W   = SCREEN_ACT_W + 2.0   # 110.0mm
WIN_H   = SCREEN_ACT_H + 2.0   # 66.8mm
WIN_R   = 8.0                   # Screen window corner radius (rounded-CRT look)

# Bezel margins (space from body edge to screen window edge)
BEZEL_LR = (TV_W - WIN_W) / 2  # ~27.5mm each side (left panel has speaker, right has buttons)
BEZEL_TB = (TV_H - WIN_H) / 2  # ~24.1mm top/bottom

# ── Front/back split ──
# Body is split front+back. Front shell holds everything; back screws on.
# Front shell: from front face to split plane
SPLIT_Z = TV_D - WALL_TV * 2   # Split near back, back shell is just a cover plate

# Depth from front face to where screen sits
Z_SCREEN_FRONT  = WALL_TV                            # Inner front wall depth
Z_PCB_INNER     = Z_SCREEN_FRONT + SCREEN_MOD_D + 3.0  # PCB behind screen + 3mm gap

# ── Feet ──
FOOT_W          = 15.0   # Foot width
FOOT_D          = 20.0   # Foot depth (front-to-back)
FOOT_H          = 12.0   # Foot height
FOOT_SPLAY_DEG  = 10.0   # Outward splay angle
FOOT_INSET_X    = 35.0   # Distance of foot center from body center (each side)

# ── Side button holes (right panel, knob-style) ──
KNOB_DIA        = 10.0   # Larger diameter for knob-style buttons
KNOB_SPACING    = 15.0   # Center-to-center (vertical)
KNOB_FROM_FRONT = 10.0   # Distance from front face to button center

# ── Top button ──
TOP_BTN_DIA     = 8.0
TOP_BTN_FROM_FRONT = 10.0  # Distance from front face

# ── Speaker grille (left panel) ──
GRILLE_W        = 40.0   # Grille area width
GRILLE_H        = 30.0   # Grille area height
GRILLE_HOLE_DIA = 2.0
GRILLE_SPACING  = 4.0

# ── Antenna stubs ──
ANT_DIA         = 4.0
ANT_H           = 15.0
ANT_SPACING_X   = 30.0  # Center-to-center along X
ANT_FROM_FRONT  = TV_D / 2  # Roughly center top

# ── Ventilation slots (back shell, near top) ──
VENT_W          = 30.0
VENT_H          = 2.5
VENT_N          = 4
VENT_SPACING    = 5.5   # Center-to-center

# ── Screw boss positions (corners of back panel interior) ──
BOSS_INSET = 8.0  # From body edge to boss center


# ── Internal helpers ──

def _tv_outer():
    """Solid TV outer body — rounded box centered at origin."""
    return rounded_box(TV_W, TV_H, TV_D, TV_R)


def _interior_cavity():
    """Hollow interior cavity, leaving WALL_TV on all sides."""
    inner_w = TV_W - WALL_TV * 2
    inner_h = TV_H - WALL_TV * 2
    inner_d = TV_D - WALL_TV * 2
    # Offset toward back so the front wall stays solid
    # Front wall at Z = -TV_D/2 + WALL_TV (inner surface)
    # Back wall at Z = TV_D/2 - WALL_TV (inner surface)
    return translate([0, 0, 0])(
        cube([inner_w, inner_h, inner_d + 0.1], center=True)
    )


def _screen_window_cutout():
    """Rounded-corner screen window through the front face."""
    r = WIN_R
    ox = WIN_W / 2 - r
    oy = WIN_H / 2 - r
    corners = [
        translate([sx * ox, sy * oy, 0])(
            cylinder(r=r, h=WALL_TV + 0.4, center=True, _fn=48)
        )
        for sx in [-1, 1] for sy in [-1, 1]
    ]
    return hull()(*corners)


def _screen_pocket():
    """Screen module recess just inside the front wall."""
    pocket_w = SCREEN_MOD_W + TOL * 2
    pocket_h = SCREEN_MOD_H + TOL * 2
    pocket_d = SCREEN_MOD_D + 1.0
    # Pocket sits at front inner surface
    front_inner_z = -TV_D / 2 + WALL_TV
    return translate([0, 0, front_inner_z + pocket_d / 2])(
        cube([pocket_w, pocket_h, pocket_d + 0.1], center=True)
    )


def _knob_holes_right():
    """Two knob-style button holes through the right side panel."""
    # Right panel: X = +TV_W/2
    # Buttons are centered vertically on the body, offset from front face
    # Front face is at Z = -TV_D/2; button center at Z = -TV_D/2 + KNOB_FROM_FRONT
    btn_z = -TV_D / 2 + KNOB_FROM_FRONT
    offsets_y = [KNOB_SPACING / 2, -KNOB_SPACING / 2]
    holes = []
    for dy in offsets_y:
        hole = translate([TV_W / 2, dy, btn_z])(
            rotate([0, 90, 0])(
                cylinder(d=KNOB_DIA, h=WALL_TV + 0.4, center=True, _fn=32)
            )
        )
        holes.append(hole)
    return union()(*holes)


def _top_button_hole():
    """Single button hole through the top panel."""
    # Top panel: Y = +TV_H/2
    # Button centered in X, offset from front face
    btn_z = -TV_D / 2 + TOP_BTN_FROM_FRONT
    return translate([0, TV_H / 2, btn_z])(
        rotate([90, 0, 0])(
            cylinder(d=TOP_BTN_DIA, h=WALL_TV + 0.4, center=True, _fn=32)
        )
    )


def _speaker_grille_holes():
    """Grid of holes in the left side panel for the speaker."""
    # Left panel: X = -TV_W/2
    # Grille centered vertically, slightly toward front half in Z
    grille_z_center = -TV_D / 2 + TV_D * 0.35  # ~35% from front
    holes = []
    cols = int(GRILLE_W / GRILLE_SPACING)
    rows = int(GRILLE_H / GRILLE_SPACING)
    start_z = grille_z_center - (cols - 1) * GRILLE_SPACING / 2
    start_y = -(rows - 1) * GRILLE_SPACING / 2
    for ic in range(cols):
        gz = start_z + ic * GRILLE_SPACING
        for ir in range(rows):
            gy = start_y + ir * GRILLE_SPACING
            hole = translate([-TV_W / 2, gy, gz])(
                rotate([0, 90, 0])(
                    cylinder(d=GRILLE_HOLE_DIA, h=WALL_TV + 0.4, center=True, _fn=16)
                )
            )
            holes.append(hole)
    return union()(*holes)


def _antenna_stubs():
    """Two cylindrical antenna stubs on the top face."""
    # Top face: Y = +TV_H/2
    # Stubs positioned toward rear, spaced ANT_SPACING_X apart
    ant_z = TV_D / 2 - TV_D * 0.25  # ~75% from front, near rear
    stubs = []
    for dx in [-ANT_SPACING_X / 2, ANT_SPACING_X / 2]:
        stub = translate([dx, TV_H / 2, ant_z])(
            rotate([90, 0, 0])(
                cylinder(d=ANT_DIA, h=-(ANT_H + WALL_TV), center=False, _fn=32)
            )
        )
        stubs.append(stub)
    return union()(*stubs)


def _antenna_stubs_solid():
    """Solid antenna cylinder bodies (added to shell, not subtracted)."""
    ant_z = TV_D / 2 - TV_D * 0.25
    stubs = []
    for dx in [-ANT_SPACING_X / 2, ANT_SPACING_X / 2]:
        # Cylinder: sits on top of the TV top face, oriented upward
        # Top face at Y = TV_H/2; stub extends upward in +Y direction
        stub = translate([dx, TV_H / 2 + ANT_H / 2, ant_z])(
            rotate([90, 0, 0])(
                cylinder(d=ANT_DIA, h=ANT_H, center=True, _fn=32)
            )
        )
        stubs.append(stub)
    return union()(*stubs)


def _feet():
    """Two small feet splayed outward from the bottom."""
    # Bottom face: Y = -TV_H/2
    # Feet are roughly box-shaped, angled outward
    # Foot is a rounded box; translate to bottom face, rotate outward

    def single_foot():
        r = 2.0
        return rounded_box(FOOT_W, FOOT_H, FOOT_D, r)

    feet = []
    for side in [-1, 1]:
        # Build foot centered at origin, then splay and position
        foot = single_foot()
        # Splay outward: rotate around Z axis so foot tilts sideways
        foot = rotate([0, 0, side * FOOT_SPLAY_DEG])(foot)
        # Position: center of foot at bottom edge of TV, inset from each side
        foot_x = side * FOOT_INSET_X
        foot_y = -TV_H / 2 - FOOT_H / 2  # Hanging below bottom edge
        foot_z = -TV_D / 2 + FOOT_D / 2  # Aligned with front region
        foot = translate([foot_x, foot_y, foot_z])(foot)
        feet.append(foot)
    return union()(*feet)


def _vent_slots():
    """Ventilation slots on the back panel near the top."""
    # Back face: Z = +TV_D/2
    # Slots are horizontal, centered in X, near the top
    vent_y_top = TV_H / 2 - BOSS_INSET - 8.0
    slots = []
    for i in range(VENT_N):
        vy = vent_y_top - i * VENT_SPACING
        slot = translate([0, vy, TV_D / 2])(
            rotate([0, 0, 0])(
                cube([VENT_W, VENT_H, WALL_TV + 0.4], center=True)
            )
        )
        slots.append(slot)
    return union()(*slots)


def _usbc_cutout_back():
    """USB-C cutout through the back panel, centered horizontally, at bottom."""
    # Back face: Z = +TV_D/2; cutout runs through back wall along Z
    usbc_y = -TV_H / 2 + 12.0  # Near the bottom of back face
    slot = usbc_cutout(WALL_TV)
    # usbc_cutout extrudes along Z (pill shape in XY), centered
    # We need it on the back face — rotate so it punches through Z wall
    return translate([0, usbc_y, TV_D / 2])(slot)


def _screw_boss_positions():
    """(x, y) positions for screw bosses in the front shell interior."""
    inset = BOSS_INSET + WALL_TV
    x_range = TV_W / 2 - inset
    y_range = TV_H / 2 - inset
    return [
        (-x_range, -y_range),
        ( x_range, -y_range),
        (-x_range,  y_range),
        ( x_range,  y_range),
    ]


def _pcb_standoffs():
    """PCB mounting standoffs rising from the bottom interior floor."""
    # PCB is centered in XY, standoffs at corners inset from PCB edges
    standoff_h = Z_PCB_INNER - (-TV_D / 2 + WALL_TV)  # height from inner floor to PCB bottom
    sx = PCB_W / 2 - STANDOFF_INSET
    sy = PCB_H / 2 - STANDOFF_INSET
    positions = [
        (-sx, -sy),
        ( sx, -sy),
        (-sx,  sy),
        ( sx,  sy),
    ]
    posts = []
    floor_z = -TV_D / 2 + WALL_TV  # inner bottom of front shell (front face side)
    for (px, py) in positions:
        post = translate([px, py, floor_z])(
            standoff_set([(0, 0)], standoff_h)
        )
        posts.append(post)
    return union()(*posts)


# ── Public build functions ──

def build_front_shell():
    """TV body front half: screen window, side button holes, speaker grille, antenna stubs.

    The body is centered at origin. Front face is at Z = -TV_D/2.
    Split plane at approximately Z = TV_D/2 - WALL_TV*2 (back shell screws on).
    """
    # Start with solid outer body
    shell = _tv_outer()

    # Hollow out interior
    shell = difference()(shell, _interior_cavity())

    # Screen window on front face
    win_z = -TV_D / 2 + WALL_TV / 2
    window = translate([0, 0, win_z])(_screen_window_cutout())
    shell = difference()(shell, window)

    # Screen module pocket (recess inside front wall)
    shell = difference()(shell, _screen_pocket())

    # Knob-style button holes on right side panel
    shell = difference()(shell, _knob_holes_right())

    # Top button hole
    shell = difference()(shell, _top_button_hole())

    # Speaker grille holes on left side panel
    shell = difference()(shell, _speaker_grille_holes())

    # Add antenna stubs on top (solid additions)
    stubs = _antenna_stubs_solid()
    shell = union()(shell, stubs)

    # Add PCB standoffs
    shell = union()(shell, _pcb_standoffs())

    # Add feet
    feet = _feet()
    shell = union()(shell, feet)

    return color("Ivory", 0.95)(shell)


def build_back_shell():
    """Back panel: screws onto front shell. Has USB-C cutout and vent slots."""
    # Back shell: a thin plate covering the open back of the TV body
    # It is a rounded box, same width/height, WALL_TV*2 thick
    back_d = WALL_TV * 2.5
    back = rounded_box(TV_W - TOL * 2, TV_H - TOL * 2, back_d, TV_R - 0.5)
    # Position: back face of TV body at Z = TV_D/2; back shell centered at Z = TV_D/2 - back_d/2
    back_z = TV_D / 2 - back_d / 2
    back = translate([0, 0, back_z])(back)

    # USB-C cutout — punches through back panel, near bottom
    usbc_y = -TV_H / 2 + 12.0
    # Cutout runs along Z through the back panel — pill shape in XY
    usbc = translate([0, usbc_y, back_z])(
        linear_extrude(height=back_d + 0.4, center=True)(
            pill_2d(USBC_W + 0.4, USBC_H + 0.4)
        )
    )
    back = difference()(back, usbc)

    # Ventilation slots near top — horizontal slots on back face
    vent_y_start = TV_H / 2 - 18.0
    for i in range(VENT_N):
        vy = vent_y_start - i * VENT_SPACING
        slot = translate([0, vy, back_z])(
            cube([VENT_W, VENT_H, back_d + 0.4], center=True)
        )
        back = difference()(back, slot)

    # Screw holes at corners matching front shell bosses
    inset = BOSS_INSET + WALL_TV
    x_range = TV_W / 2 - inset
    y_range = TV_H / 2 - inset
    for bx, by in [(-x_range, -y_range), (x_range, -y_range),
                   (-x_range,  y_range), (x_range,  y_range)]:
        boss_hole = translate([bx, by, back_z])(
            cylinder(d=SCREW_BOSS_ID, h=back_d + 0.4, center=True, _fn=24)
        )
        back = difference()(back, boss_hole)

    return color("WhiteSmoke", 0.9)(back)


def build_stand():
    """Two feet extending from the bottom of the TV, slightly splayed outward."""
    return color("Ivory", 0.9)(_feet())


def build_internals():
    """Ghost components: screen, PCB, and speaker positioned inside the TV.

    Returns a dict with keys 'screen', 'pcb', 'speaker'.
    """
    # Screen: front face flush with inner front wall
    # Inner front wall at Z = -TV_D/2 + WALL_TV
    # Screen module extends from there inward
    screen_z = -TV_D / 2 + WALL_TV + SCREEN_MOD_D / 2
    screen = translate([0, 0, screen_z])(ghost_screen())

    # PCB: behind the screen module + gap
    pcb_z = -TV_D / 2 + Z_PCB_INNER + PCB_D / 2
    pcb = translate([0, 0, pcb_z])(ghost_pcb())

    # Speaker: mounted on left interior wall
    # Left wall at X = -TV_W/2 + WALL_TV; speaker faces inward
    spk_x = -TV_W / 2 + WALL_TV + SPEAKER_D / 2
    spk_z = -TV_D / 2 + TV_D * 0.35
    speaker = translate([spk_x, 0, spk_z])(
        rotate([0, 90, 0])(ghost_speaker())
    )

    return {"screen": screen, "pcb": pcb, "speaker": speaker}


def _all_parts_assembled():
    """Union of all parts in assembled position (no internals)."""
    return union()(
        build_front_shell(),
        build_back_shell(),
    )


def get_views():
    """Return 7 view definitions as a dict of {name: (solid, camera_string)}."""
    internals = build_internals()
    front  = build_front_shell()
    back   = build_back_shell()
    screen = internals["screen"]
    pcb    = internals["pcb"]
    speaker = internals["speaker"]
    assembled = union()(front, back)

    views = {}

    # 1. ISO assembled — 3/4 front view showing front face, right side, top
    #    rot_x=155 is close to 180 (-Z = screen face) with slight top visibility
    views["iso_assembled"] = (
        assembled,
        camera_custom(155, 0, 25, dist=500),
    )

    # 2. Front face — looking straight at screen bezel
    views["front"] = (
        front,
        camera_front(dist=400),
    )

    # 3. Back panel — showing USB-C and vent slots
    views["back"] = (
        back,
        camera_back(dist=400),
    )

    # 4. Right side — knob button holes
    #    rot_x=5 adds slight elevation; dist=500 for 165mm wide TV
    views["right_side"] = (
        assembled,
        camera_custom(5, 0, 90, dist=500),
    )

    # 5. Left side — speaker grille
    views["left_side"] = (
        assembled,
        camera_custom(5, 0, 270, dist=500),
    )

    # 6. Cross-section along X axis — shows depth stack
    #    Side view with slight elevation to see cut interior
    full_with_internals = union()(front, back, screen, pcb, speaker)
    views["cross_section"] = (
        cross_section_cut(full_with_internals, axis="x", keep="back"),
        camera_custom(85, 0, 90, dist=450),
    )

    # 7. Exploded — parts pulled apart along Z
    views["exploded"] = (
        explode_parts([
            (front,   (0,  0, -20)),
            (back,    (0,  0,  30)),
            (screen,  (0,  0,  -8)),
            (pcb,     (0,  0,   5)),
            (speaker, (0,  0,   0)),
        ]),
        camera_custom(155, 0, 25, dist=600),
    )

    return views


def check_clearances():
    """Validate key dimensional constraints.

    Returns a dict of {check_name: (value, limit, passed)}.
    """
    results = {}

    # Depth stack: front wall + screen module + gap + PCB + back wall <= TV_D
    depth_stack = WALL_TV + SCREEN_MOD_D + 3.0 + PCB_D + WALL_TV
    results["depth_stack"] = (depth_stack, TV_D, depth_stack <= TV_D)

    # Clearance behind PCB for ESP32 and other components
    space_behind_pcb = TV_D - depth_stack
    results["space_behind_pcb"] = (space_behind_pcb, ESP32_D, space_behind_pcb >= ESP32_D)

    # Screen pocket fits screen module width within interior
    pocket_w = SCREEN_MOD_W + TOL * 2
    interior_w = TV_W - WALL_TV * 2
    results["screen_pocket_width"] = (pocket_w, interior_w, pocket_w <= interior_w)

    # Screen pocket fits screen module height within interior
    pocket_h = SCREEN_MOD_H + TOL * 2
    interior_h = TV_H - WALL_TV * 2
    results["screen_pocket_height"] = (pocket_h, interior_h, pocket_h <= interior_h)

    # Screen window does not exceed front face interior width
    results["screen_window_width"] = (WIN_W, interior_w, WIN_W <= interior_w)

    # Screen window does not exceed front face interior height
    results["screen_window_height"] = (WIN_H, interior_h, WIN_H <= interior_h)

    # PCB fits in interior width
    results["pcb_width"] = (PCB_W + TOL * 2, interior_w, PCB_W + TOL * 2 <= interior_w)

    # PCB fits in interior height
    results["pcb_height"] = (PCB_H + TOL * 2, interior_h, PCB_H + TOL * 2 <= interior_h)

    # Knob button hole positions: buttons at z = -TV_D/2 + KNOB_FROM_FRONT
    # must be within the body depth (not in front wall itself)
    knob_z_from_inner = KNOB_FROM_FRONT - WALL_TV
    results["knob_clears_front_wall"] = (knob_z_from_inner, 0.0, knob_z_from_inner > 0.0)

    # Speaker grille: 2mm holes through WALL_TV=2.5mm — confirm wall is thick enough
    results["grille_wall_thickness"] = (WALL_TV, GRILLE_HOLE_DIA, WALL_TV >= GRILLE_HOLE_DIA)

    # Vent slots fit within back panel width
    results["vent_slot_width"] = (VENT_W, TV_W - WALL_TV * 4, VENT_W <= TV_W - WALL_TV * 4)

    # Foot height clears the WALL_TV bottom: feet protrude below bottom face
    foot_protrusion = FOOT_H  # feet hang below bottom face
    results["foot_height_positive"] = (foot_protrusion, 0.0, foot_protrusion > 0.0)

    return results


# ── Main: render default assembled view ──

if __name__ == "__main__":
    import os
    from solid2 import scad_render_to_file

    out_dir = os.path.join(os.path.dirname(__file__), "output")
    os.makedirs(out_dir, exist_ok=True)

    # Print clearance report
    print("=== Retro TV — Clearance Check ===")
    for name, (value, limit, passed) in check_clearances().items():
        status = "OK" if passed else "FAIL"
        print(f"  [{status}] {name}: {value:.2f} vs limit {limit:.2f}")

    # Render all views
    views = get_views()
    for view_name, (solid, camera) in views.items():
        out_path = os.path.join(out_dir, f"design_5_retro_tv_{view_name}.scad")
        scad_render_to_file(solid, out_path)
        print(f"  Wrote {out_path}")

    print("Done.")
