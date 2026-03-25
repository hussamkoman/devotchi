"""Desktop Devotchi — Design 2: Wedge Console.

Low-profile landscape wedge: 155 x 100mm footprint, 40mm thick at back
tapering to 10mm at front. Screen embedded in the angled top surface
(~15 degrees), naturally tilted toward the user. Speaker fires downward
through grille holes in the bottom plate.
"""
import math

from solid2 import (
    cube, cylinder, hull, union, difference, intersection,
    translate, rotate, color, linear_extrude, polygon,
    circle,
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
    rounded_box, rounded_box_xy, pill_2d,
    ghost_screen, ghost_pcb, ghost_speaker,
    usbc_cutout, button_hole, speaker_grille,
    standoff, standoff_set, screw_boss, rubber_foot,
    camera_iso, camera_iso_back, camera_front, camera_back, camera_custom,
    cross_section_cut, explode_parts,
)

# ── Wedge overall dimensions ──
WEDGE_W      = 155.0   # Width  (X — landscape)
WEDGE_D      = 100.0   # Depth  (Y — front-to-back; negative Y = front/thin end)
BACK_H       = 40.0    # Height at the back edge (thick end), Z
FRONT_H      = 10.0    # Height at the front edge (thin end), Z
CORNER_R     = 5.0     # XY corner radius

# Taper angle of the top surface from horizontal
# arctan((BACK_H - FRONT_H) / WEDGE_D) — measured upward toward the back
TAPER_ANGLE  = math.degrees(math.atan2(BACK_H - FRONT_H, WEDGE_D))
# ≈ 16.7 degrees

# ── Coordinate convention ──
# Bottom face at Z = 0.
# Front face (thin end) at Y = -WEDGE_D/2.
# Back face  (thick end) at Y = +WEDGE_D/2.
# Centered in X.

# ── Wall thicknesses ──
WALL_BOTTOM = WALL   # Bottom cover plate thickness
WALL_SIDE   = WALL   # Left / right walls
WALL_FRONT  = WALL   # Front face (thin end) wall
WALL_BACK   = WALL   # Back face  (thick end) wall
WALL_TOP    = WALL   # Angled top surface thickness above the screen pocket

# ── Screen pocket on angled top surface ──
SCREEN_POCKET_D = SCREEN_MOD_D + 0.5   # Depth of recess for screen module (~5mm)

# ── Speaker cavity (in the thick back area, bottom floor) ──
SPEAKER_CAV_DIA   = 32.0   # Cavity diameter (larger than the 28mm speaker)
SPEAKER_CAV_DEPTH = 5.0    # Depth of cavity from bottom floor upward
SPEAKER_X         = 0.0    # Centered in X
SPEAKER_Y         = WEDGE_D / 2 - 22.0  # Near the back (thick) edge

# ── PCB standoffs ──
# PCB floats above the bottom floor at a height that clears the speaker cavity.
PCB_STANDOFF_H = max(SPEAKER_CAV_DEPTH + 2.0, 7.0)   # ≥ 7mm
# PCB is shifted slightly toward the back (positive Y) so it lands in the
# taller part of the wedge interior.
PCB_Y_OFFSET   = 8.0

# ── Screw boss layout — 4 corners for shell assembly ──
BOSS_INSET_X = 12.0
BOSS_INSET_Y = 10.0
BOSS_HEIGHT  = 5.0    # Boss height above bottom plate inner face
BOSS_POSITIONS = [
    ( WEDGE_W / 2 - BOSS_INSET_X,  WEDGE_D / 2 - BOSS_INSET_Y),
    (-WEDGE_W / 2 + BOSS_INSET_X,  WEDGE_D / 2 - BOSS_INSET_Y),
    ( WEDGE_W / 2 - BOSS_INSET_X, -WEDGE_D / 2 + BOSS_INSET_Y),
    (-WEDGE_W / 2 + BOSS_INSET_X, -WEDGE_D / 2 + BOSS_INSET_Y),
]

# ── Button lip on front face ──
BTN_N        = 3
BTN_Z_CENTER = FRONT_H / 2   # Vertically centered on the front face

# ── Rubber feet ──
FOOT_DIA   = 8.0
FOOT_H     = 1.5
FOOT_INSET = 12.0


# ── Internal geometry builders ──

def _wedge_outer():
    """Solid wedge hull: thin front box + thick back box.

    Footprint centered at (0, 0).  Z = 0 is the bottom face.
    """
    # Front edge box (thin end): centered at Y = -WEDGE_D/2 + tiny_y_extent/2
    front_box = translate([0, -WEDGE_D / 2 + FRONT_H / 2, FRONT_H / 2])(
        rounded_box(WEDGE_W, FRONT_H, FRONT_H, CORNER_R)
    )
    # Back edge box (thick end)
    back_box = translate([0, WEDGE_D / 2 - BACK_H / 2, BACK_H / 2])(
        rounded_box(WEDGE_W, BACK_H, BACK_H, CORNER_R)
    )
    return hull()(front_box, back_box)


def _wedge_inner():
    """Inner void — same shape, inset by wall thicknesses.

    Opens downward (no bottom) so the bottom plate can be a removable cover.
    """
    inner_w       = WEDGE_W - WALL_SIDE * 2
    inner_front_h = max(FRONT_H - WALL_BOTTOM - WALL_TOP, 1.0)
    inner_back_h  = BACK_H  - WALL_BOTTOM - WALL_TOP

    # Inner front anchor: at the front inner face
    # Y range shrinks by WALL_FRONT at front and WALL_BACK at back
    front_box = translate([
        0,
        -WEDGE_D / 2 + WALL_FRONT + inner_front_h / 2,
        WALL_BOTTOM + inner_front_h / 2,
    ])(
        cube([inner_w, inner_front_h + 0.1, inner_front_h + 0.1], center=True)
    )
    back_box = translate([
        0,
        WEDGE_D / 2 - WALL_BACK - inner_back_h / 2,
        WALL_BOTTOM + inner_back_h / 2,
    ])(
        cube([inner_w, inner_back_h + 0.1, inner_back_h + 0.1], center=True)
    )
    return hull()(front_box, back_box)


def _top_surface_z(y):
    """Height of the top (angled) surface at a given Y position."""
    # Linear interpolation: at Y=-WEDGE_D/2 → FRONT_H, at Y=+WEDGE_D/2 → BACK_H
    t = (y + WEDGE_D / 2) / WEDGE_D
    return FRONT_H + t * (BACK_H - FRONT_H)


def _screen_pocket_cutter():
    """Cutter for the screen module recess on the angled top surface.

    The screen module pocket is cut perpendicular to the angled surface
    by rotating the cutter to match the taper angle.
    """
    pocket_w     = SCREEN_MOD_W + TOL * 2    # X extent
    pocket_h_xy  = SCREEN_MOD_H + TOL * 2   # Y extent (along depth direction)
    pocket_depth = SCREEN_POCKET_D           # depth into the surface

    # Extra height so the cutter punches cleanly through the top surface
    extra = 15.0

    # Pocket cutter: a tall box that will be rotated to match the surface angle.
    # Its local Z=0 sits at the outer surface; negative Z goes into the material.
    pocket_box = cube([pocket_w, pocket_h_xy, pocket_depth + extra], center=True)

    # Position the cutter so Z=0 of the (pre-rotation) box is at the surface.
    # After rotation, translate by surface_z at Y=0.
    # Shift up by (pocket_depth + extra)/2 so the bottom of the box is at z=0
    # in the rotated frame, then drop it so z=0 aligns with the surface.
    cutter = translate([0, 0, -(pocket_depth + extra) / 2])(pocket_box)
    cutter = rotate([-TAPER_ANGLE, 0, 0])(cutter)

    surface_z_at_center = _top_surface_z(0.0)   # 25mm
    return translate([0, 0.0, surface_z_at_center])(cutter)


def _screen_window_cutter():
    """Cutter for the active-area window through the angled top wall."""
    win_w    = SCREEN_ACT_W + 1.0
    win_h_xy = SCREEN_ACT_H + 1.0
    # Tall enough to cut through WALL_TOP and a bit more
    win_depth = WALL_TOP + 12.0

    window_box = cube([win_w, win_h_xy, win_depth], center=True)
    # Same approach: local Z=0 at surface, extends inward
    cutter = translate([0, 0, -win_depth / 2])(window_box)
    cutter = rotate([-TAPER_ANGLE, 0, 0])(cutter)

    surface_z_at_center = _top_surface_z(0.0)
    return translate([0, 0.0, surface_z_at_center])(cutter)


def _button_holes_front():
    """3 button holes through the front face, evenly spaced, centered in X."""
    holes = []
    start_x = -(BTN_N - 1) * BTN_SPACING / 2
    front_y  = -WEDGE_D / 2   # front face Y

    for i in range(BTN_N):
        x    = start_x + i * BTN_SPACING
        hole = translate([x, front_y, BTN_Z_CENTER])(
            rotate([90, 0, 0])(
                cylinder(d=BTN_HOLE_DIA, h=WALL_FRONT + 0.4, center=True, _fn=32)
            )
        )
        holes.append(hole)
    return union()(*holes)


def _usbc_cutout_back():
    """USB-C cutout through the back face, centered in X, centered in Z."""
    back_y   = WEDGE_D / 2
    usbc_z   = BACK_H / 2

    slot = linear_extrude(height=WALL_BACK + 0.4, center=True)(
        pill_2d(USBC_W + 0.4, USBC_H + 0.4)
    )
    return translate([0, back_y, usbc_z])(
        rotate([90, 0, 0])(slot)
    )


def _speaker_cavity_cutter():
    """Speaker cavity cut upward from the bottom floor interior."""
    # Cuts from just below the inner bottom surface into the interior
    return translate([SPEAKER_X, SPEAKER_Y, WALL_BOTTOM - 0.1])(
        cylinder(d=SPEAKER_CAV_DIA, h=SPEAKER_CAV_DEPTH + 0.2, center=False, _fn=48)
    )


def _screw_boss_holes_bottom():
    """M2.5 screw holes through the bottom plate at boss positions."""
    holes = []
    for (x, y) in BOSS_POSITIONS:
        holes.append(
            translate([x, y, -0.1])(
                cylinder(d=SCREW_BOSS_ID, h=WALL_BOTTOM + 0.2, center=False, _fn=24)
            )
        )
    return union()(*holes)


def _pcb_standoff_positions():
    """(x, y) positions of the 4 PCB mounting standoffs relative to wedge center."""
    hw = PCB_W / 2 - STANDOFF_INSET
    hh = PCB_H / 2 - STANDOFF_INSET
    return [
        ( hw + PCB_Y_OFFSET,  hh),
        (-hw + PCB_Y_OFFSET,  hh),
        ( hw + PCB_Y_OFFSET, -hh),
        (-hw + PCB_Y_OFFSET, -hh),
    ]


# ── Public build functions ──

def build_front_shell():
    """Main wedge body (top + sides + front + back walls), hollowed out.

    Includes: screen pocket + window on angled top, button holes on front
    face, USB-C cutout on back face, speaker cavity cut from below.
    The bottom is open — sealed by the removable back_shell plate.
    """
    outer = _wedge_outer()
    inner = _wedge_inner()
    shell = difference()(outer, inner)

    # Screen module pocket (recess into angled top surface)
    shell = difference()(shell, _screen_pocket_cutter())

    # Active area window (through-hole in the angled top wall)
    shell = difference()(shell, _screen_window_cutter())

    # 3 button holes on the front face
    shell = difference()(shell, _button_holes_front())

    # USB-C port cutout on the back face
    shell = difference()(shell, _usbc_cutout_back())

    # Speaker cavity cut from below into the interior floor
    shell = difference()(shell, _speaker_cavity_cutter())

    return color("LightSteelBlue", 0.9)(shell)


def build_back_shell():
    """Bottom plate / cover — screws onto the front shell from below.

    Features: rubber feet at corners, speaker grille holes aligned with
    cavity, M2.5 screw holes at boss positions, PCB standoffs and screw
    bosses rising from the inner (top) face.
    """
    plate_w = WEDGE_W - TOL * 2
    plate_d = WEDGE_D - TOL * 2
    r       = max(CORNER_R - TOL, 0.5)

    # Flat bottom plate — bottom at Z = 0
    plate = rounded_box_xy(plate_w, plate_d, WALL_BOTTOM, r)

    # Speaker grille holes through the plate
    grille = translate([SPEAKER_X, SPEAKER_Y, -0.1])(
        speaker_grille(SPEAKER_CAV_DIA, hole_dia=2.0, spacing=4.0)
    )
    plate = difference()(plate, grille)

    # M2.5 screw holes at boss positions
    plate = difference()(plate, _screw_boss_holes_bottom())

    # PCB mounting standoffs rising from inner (top) face
    posts = translate([0, 0, WALL_BOTTOM])(
        standoff_set(
            _pcb_standoff_positions(),
            height=PCB_STANDOFF_H,
            od=STANDOFF_OD,
            screw_id=STANDOFF_ID,
        )
    )

    # Screw bosses for shell assembly (at same XY as screw holes)
    bosses = []
    for (x, y) in BOSS_POSITIONS:
        bosses.append(
            translate([x, y, WALL_BOTTOM])(
                screw_boss(height=BOSS_HEIGHT)
            )
        )

    # Rubber feet at corners (on outer / bottom face)
    feet_pos = [
        ( WEDGE_W / 2 - FOOT_INSET,  WEDGE_D / 2 - FOOT_INSET),
        (-WEDGE_W / 2 + FOOT_INSET,  WEDGE_D / 2 - FOOT_INSET),
        ( WEDGE_W / 2 - FOOT_INSET, -WEDGE_D / 2 + FOOT_INSET),
        (-WEDGE_W / 2 + FOOT_INSET, -WEDGE_D / 2 + FOOT_INSET),
    ]
    feet = [translate([fx, fy, 0])(rubber_foot(FOOT_DIA, FOOT_H)) for fx, fy in feet_pos]

    return color("SlateGray", 0.85)(
        union()(plate, posts, *bosses, *feet)
    )


def build_stand():
    """The wedge IS the stand — no separate stand component needed.

    Returns None.  The ~16.7-degree taper naturally angles the screen
    toward a seated user at desk height.
    """
    return None


def build_internals():
    """Ghost components: screen, PCB, and speaker positioned inside the wedge.

    Returns a dict: {'screen': solid, 'pcb': solid, 'speaker': solid}.

    Coordinate system matches the shell: Z=0 at bottom, Y+ toward back.
    """
    # ── Screen ──
    # The screen module sits in the pocket on the angled top surface.
    # Its back face (glass side facing up-toward-user) is at:
    #   outer_surface_z - WALL_TOP
    # and the module center is half its depth below that.
    surface_z     = _top_surface_z(0.0)                        # 25mm at Y=0
    screen_center_z = surface_z - WALL_TOP - SCREEN_MOD_D / 2

    screen = translate([0, 0.0, screen_center_z])(
        rotate([-TAPER_ANGLE, 0, 0])(
            ghost_screen()
        )
    )

    # ── PCB ──
    # PCB lies flat (horizontal), mounted on standoffs above the bottom floor.
    pcb_z = WALL_BOTTOM + PCB_STANDOFF_H + PCB_D / 2
    pcb = translate([PCB_Y_OFFSET, 0, pcb_z])(ghost_pcb())

    # ── Speaker ──
    # Speaker sits in its cavity just above the bottom floor.
    speaker_z = WALL_BOTTOM + SPEAKER_D / 2 + 0.5
    speaker = translate([SPEAKER_X, SPEAKER_Y, speaker_z])(ghost_speaker())

    return {"screen": screen, "pcb": pcb, "speaker": speaker}


def _assembled_no_internals():
    """Front shell + back shell joined."""
    return union()(build_front_shell(), build_back_shell())


def get_views():
    """Return list of 7 view dicts, each with keys 'name', 'solid', 'camera'."""
    internals   = build_internals()
    front_shell = build_front_shell()
    back_shell  = build_back_shell()
    screen      = internals["screen"]
    pcb         = internals["pcb"]
    speaker     = internals["speaker"]
    assembled   = union()(front_shell, back_shell)

    views = []

    # 1. assembled_iso — isometric showing wedge shape with screen ghost
    #    Wedge screen is on angled TOP surface — use iso_back (from above) to see it
    views.append({
        "name":   "assembled_iso",
        "solid":  union()(assembled, screen),
        "camera": camera_iso_back(dist=450),
    })

    # 2. assembled_front — front face, slightly elevated to see the button lip
    views.append({
        "name":   "assembled_front",
        "solid":  assembled,
        "camera": camera_custom(15, 0, 0, dist=400),
    })

    # 3. assembled_back — back face showing USB-C cutout
    views.append({
        "name":   "assembled_back",
        "solid":  assembled,
        "camera": camera_custom(15, 0, 180, dist=400),
    })

    # 4. exploded — parts separated along Z
    exploded = explode_parts([
        (front_shell, (0,  0,  55)),
        (screen,      (0,  0,  30)),
        (pcb,         (0,  0,  15)),
        (speaker,     (0, 25,  10)),
        (back_shell,  (0,  0, -15)),
    ])
    views.append({
        "name":   "exploded",
        "solid":  exploded,
        "camera": camera_iso_back(dist=550),
    })

    # 5. interior_front — top shell viewed from below (screen pocket visible)
    views.append({
        "name":   "interior_front",
        "solid":  front_shell,
        "camera": camera_custom(180, 0, 0, dist=450),
    })

    # 6. interior_back — bottom plate viewed from above (standoffs, speaker pocket)
    views.append({
        "name":   "interior_back",
        "solid":  back_shell,
        "camera": camera_custom(0, 0, 0, dist=400),
    })

    # 7. cross_section — side cross-section (X cut) showing taper and depth stack
    full_with_internals = union()(front_shell, back_shell, screen, pcb, speaker)
    cross = cross_section_cut(full_with_internals, axis="x", keep="back")
    views.append({
        "name":   "cross_section",
        "solid":  cross,
        # Cut at x=0, keep x>0 — look from -X into cut face
        # rot_z=270 puts -X toward camera, rot_x=80 adds elevation
        "camera": camera_custom(80, 0, 270, dist=350),
    })

    return views


def check_clearances():
    """Validate key dimensional constraints.

    Returns a dict of {check_name: (value, limit, passed)}.
    """
    results = {}

    # Inner height at back (thick end): must fit PCB stack
    inner_back_h = BACK_H - WALL_BOTTOM - WALL_TOP
    pcb_stack    = PCB_STANDOFF_H + PCB_D + ESP32_D
    results["pcb_stack_vs_inner_back"] = (
        pcb_stack, inner_back_h, pcb_stack <= inner_back_h
    )

    # Inner height at front (thin end): must be positive
    inner_front_h = FRONT_H - WALL_BOTTOM - WALL_TOP
    results["inner_front_positive"] = (
        inner_front_h, 0.0, inner_front_h > 0.0
    )

    # Screen module width fits in inner width
    inner_w = WEDGE_W - WALL_SIDE * 2
    results["screen_mod_width"] = (
        SCREEN_MOD_W + TOL * 2, inner_w,
        SCREEN_MOD_W + TOL * 2 <= inner_w
    )

    # Screen module Y extent fits in inner depth
    inner_d = WEDGE_D - WALL_FRONT - WALL_BACK
    results["screen_mod_depth"] = (
        SCREEN_MOD_H + TOL * 2, inner_d,
        SCREEN_MOD_H + TOL * 2 <= inner_d
    )

    # PCB fits horizontally inside the inner cavity
    results["pcb_width_fit"] = (
        PCB_W, inner_w, PCB_W <= inner_w
    )
    results["pcb_depth_fit"] = (
        PCB_H, inner_d, PCB_H <= inner_d
    )

    # Taper angle should be between 10 and 25 degrees
    results["taper_angle_range"] = (
        TAPER_ANGLE, 25.0, 10.0 <= TAPER_ANGLE <= 25.0
    )

    # Speaker cavity front edge stays in back half (Y > 0)
    speaker_front_edge_y = SPEAKER_Y - SPEAKER_CAV_DIA / 2
    results["speaker_in_back_half"] = (
        speaker_front_edge_y, 0.0, speaker_front_edge_y > 0.0
    )

    # Standoff height clears the speaker cavity depth
    results["standoff_clears_speaker_cavity"] = (
        PCB_STANDOFF_H, SPEAKER_CAV_DEPTH + 1.0,
        PCB_STANDOFF_H >= SPEAKER_CAV_DEPTH + 1.0
    )

    # Button row span fits across the front face
    btn_span = (BTN_N - 1) * BTN_SPACING
    results["button_span_fit"] = (
        btn_span, WEDGE_W - WALL_SIDE * 4,
        btn_span <= WEDGE_W - WALL_SIDE * 4
    )

    # Screw boss positions clear the edge (boss OD must fit)
    boss_max_x  = WEDGE_W / 2 - BOSS_INSET_X
    boss_limit_x = WEDGE_W / 2 - SCREW_BOSS_OD / 2 - 1.0
    results["boss_inside_footprint"] = (
        boss_max_x, boss_limit_x, boss_max_x <= boss_limit_x
    )

    return results


# ── Main: render all views ──

if __name__ == "__main__":
    import os
    from solid2 import scad_render_to_file

    out_dir = os.path.join(os.path.dirname(__file__), "output")
    os.makedirs(out_dir, exist_ok=True)

    print("=== Wedge Console — Design Parameters ===")
    print(f"  Footprint   : {WEDGE_W} x {WEDGE_D} mm")
    print(f"  Height      : {FRONT_H}mm (front) → {BACK_H}mm (back)")
    print(f"  Taper angle : {TAPER_ANGLE:.1f} degrees")
    print(f"  Inner back H: {BACK_H - WALL_BOTTOM - WALL_TOP:.1f}mm")
    print(f"  Inner front H: {FRONT_H - WALL_BOTTOM - WALL_TOP:.1f}mm")
    print()

    print("=== Clearance Check ===")
    all_pass = True
    for name, (value, limit, passed) in check_clearances().items():
        status    = "OK  " if passed else "FAIL"
        all_pass  = all_pass and passed
        print(f"  [{status}] {name}: {value:.2f}  (limit {limit:.2f})")
    print()
    print(f"  Overall: {'PASS' if all_pass else 'FAIL'}")
    print()

    views = get_views()
    for view in views:
        name     = view["name"]
        solid    = view["solid"]
        out_path = os.path.join(out_dir, f"design_2_wedge_{name}.scad")
        scad_render_to_file(solid, out_path)
        print(f"  Wrote {out_path}")

    print("Done.")
