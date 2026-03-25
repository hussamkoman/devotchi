"""Desktop Devotchi — Design 3: Floating Orb.

Egg-shaped "head" (140 x 110 x 55mm ellipsoid) on a cylindrical pedestal
base (80mm diameter x 35mm tall). Screen in the front face of the egg.
Egg tilts forward 10 degrees. Speaker in the cylindrical base.
Most Tamagotchi-style design.

Assembly:
  - Front shell: front half of the egg (screen window cutout)
  - Back shell:  back half of the egg (PCB standoffs, peg socket)
  - Stand:       cylindrical base (buttons, USB-C, speaker cavity, side vents)
  - Peg:         short post connecting egg to base with wire channel
"""
import math

from solid2 import (
    cube, cylinder, sphere, hull, union, difference, intersection,
    translate, rotate, color, scale, mirror, linear_extrude,
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
    rounded_box,
    pill_2d,
    ghost_screen, ghost_pcb, ghost_speaker,
    usbc_cutout, button_hole,
    standoff, standoff_set,
    speaker_grille,
    camera_iso, camera_iso_back, camera_front, camera_back, camera_custom,
    cross_section_cut, explode_parts,
)

# ── Egg dimensions ──
# Unscaled sphere radius, scaled to produce 140 x 110 x 55mm ellipsoid:
#   X: 70 * 2.0 = 140mm (width,  scale=1.0 on r=70)
#   Y: 70 * 1.571 = 110mm → scale_y = 110/140 = 0.7857
#   Z: 70 * 0.786 = 55mm  → scale_z = 55/140  = 0.3929
EGG_R       = 70.0          # Base sphere radius (unscaled)
EGG_W       = 140.0         # Final width  (X)
EGG_H       = 110.0         # Final height (Y)
EGG_D       = 55.0          # Final depth  (Z, front-to-back)
EGG_SCALE_X = 1.0
EGG_SCALE_Y = EGG_H / (EGG_R * 2)   # 0.7857
EGG_SCALE_Z = EGG_D / (EGG_R * 2)   # 0.3929

EGG_TILT    = 10.0          # degrees forward tilt (rotation around X axis)
EGG_WALL    = 2.5           # Shell wall thickness for curved surfaces

# ── Base cylinder dimensions ──
BASE_DIA    = 80.0
BASE_R      = BASE_DIA / 2
BASE_H      = 35.0
BASE_WALL   = WALL          # 2.0mm

# ── Peg (egg-to-base connector) ──
PEG_OD      = 20.0
PEG_ID      = 8.0           # Hollow bore for wire routing
PEG_H       = 10.0          # Height of peg (above base top face)
PEG_SOCKET  = PEG_H + 2.0  # Depth of socket recess in egg bottom

# Egg bottom sits at Z = BASE_H + PEG_H (above world Z=0 = base bottom)
EGG_BASE_Z  = BASE_H + PEG_H

# ── Button layout on base front face ──
# 3 buttons in a row, centered, at 12mm from top of cylinder
BTN_N       = 3
BTN_Y_FROM_TOP = 12.0       # From top face of cylinder downward

# ── Speaker cavity in base ──
SPEAKER_CAVITY_R = SPEAKER_DIA / 2 + 1.0
VENT_SLOT_W  = 3.0
VENT_SLOT_H  = 8.0
VENT_N       = 5            # Side vent count per side arc

# ── Screen / PCB placement inside egg ──
# Egg front face (Z face of ellipsoid) is at Z = EGG_D/2 = 27.5mm from egg center.
# Front wall of egg shell at Z_egg = EGG_D/2 - EGG_WALL.
# Screen sits just inside the front wall.
SCREEN_Z_IN_EGG  = EGG_D / 2 - EGG_WALL - SCREEN_MOD_D / 2  # from egg center
PCB_Z_IN_EGG     = EGG_D / 2 - EGG_WALL - SCREEN_MOD_D - 1.0 - PCB_D / 2


# ────────────────────────────────────────────
# Internal helpers
# ────────────────────────────────────────────

def _egg_solid():
    """Solid ellipsoid for the egg shape, centered at origin."""
    return scale([EGG_SCALE_X, EGG_SCALE_Y, EGG_SCALE_Z])(
        sphere(r=EGG_R, _fn=64)
    )


def _egg_shell_solid():
    """Hollow egg shell: outer ellipsoid minus slightly smaller inner ellipsoid."""
    outer = _egg_solid()
    # Inner ellipsoid: reduce each axis by EGG_WALL
    inner_rx = EGG_R - EGG_WALL / EGG_SCALE_X
    inner_ry = EGG_R - EGG_WALL / EGG_SCALE_Y
    inner_rz = EGG_R - EGG_WALL / EGG_SCALE_Z
    # Scale inner sphere so the physical wall thickness is EGG_WALL on all axes
    scale_ix = (EGG_R * EGG_SCALE_X - EGG_WALL) / EGG_R
    scale_iy = (EGG_R * EGG_SCALE_Y - EGG_WALL) / EGG_R
    scale_iz = (EGG_R * EGG_SCALE_Z - EGG_WALL) / EGG_R
    inner = scale([scale_ix, scale_iy, scale_iz])(
        sphere(r=EGG_R, _fn=64)
    )
    return difference()(outer, inner)


def _split_plane_cutter(keep_front=True):
    """Large box cutter that keeps either the +Z (front) or -Z (back) half of the egg."""
    big = 500.0
    if keep_front:
        # Keep +Z half (front face of egg, toward viewer)
        return translate([0, 0, big / 2])(
            cube([big * 2, big * 2, big], center=True)
        )
    else:
        # Keep -Z half (back of egg)
        return translate([0, 0, -big / 2])(
            cube([big * 2, big * 2, big], center=True)
        )


def _bottom_flatten_cutter():
    """Cut away the very bottom of the egg so it has a flat seat for the peg socket."""
    # Remove everything below Y = -EGG_H/2 + 8mm (flatten ~8mm off bottom)
    flat_y = -EGG_H / 2 + 8.0
    big = 500.0
    return translate([0, -big / 2 + flat_y, 0])(
        cube([big * 2, big, big * 2], center=True)
    )


def _screen_window_cutter():
    """Rectangular cutter for the screen window through the egg front face.

    Oversized in Z to ensure full penetration through the curved wall.
    Centered at egg origin, cutting through +Z face.
    """
    win_w = SCREEN_ACT_W + 1.0 + TOL * 2
    win_h = SCREEN_ACT_H + 1.0 + TOL * 2
    win_d = EGG_WALL * 4  # deep enough to punch through curved surface
    # Position so it starts just inside front wall and cuts outward
    return translate([0, 0, EGG_D / 2 - EGG_WALL / 2])(
        cube([win_w, win_h, win_d + 0.2], center=True)
    )


def _peg_socket_cutter():
    """Cylindrical socket in egg bottom for the peg to insert into."""
    return translate([0, -EGG_H / 2 + 4.0, 0])(
        rotate([90, 0, 0])(
            cylinder(d=PEG_OD + TOL * 2, h=PEG_SOCKET + 0.1, center=False, _fn=48)
        )
    )


def _pcb_standoffs_inside_egg():
    """4 standoffs on the inside back face of the egg for the PCB."""
    # PCB is centered in XY. Standoffs at PCB corners inset by STANDOFF_INSET.
    inset = STANDOFF_INSET
    h = 4.0  # standoff height
    positions = [
        ( PCB_W / 2 - inset,  PCB_H / 2 - inset),
        (-PCB_W / 2 + inset,  PCB_H / 2 - inset),
        ( PCB_W / 2 - inset, -PCB_H / 2 + inset),
        (-PCB_W / 2 + inset, -PCB_H / 2 + inset),
    ]
    posts = []
    for x, y in positions:
        posts.append(
            translate([x, y, 0])(
                standoff(h, od=STANDOFF_OD, screw_id=STANDOFF_ID)
            )
        )
    return union()(*posts)


def _base_outer():
    """Solid cylinder for the base, bottom at Z=0."""
    return cylinder(d=BASE_DIA, h=BASE_H, center=False, _fn=96)


def _base_hollow():
    """Interior cavity of the base cylinder."""
    inner_d = BASE_DIA - BASE_WALL * 2
    return translate([0, 0, BASE_WALL])(
        cylinder(d=inner_d, h=BASE_H - BASE_WALL + 0.1, center=False, _fn=96)
    )


def _base_top_peg():
    """Solid peg on top center of base — inserts into egg socket."""
    outer = cylinder(d=PEG_OD, h=PEG_H, center=False, _fn=48)
    inner = translate([0, 0, -0.1])(
        cylinder(d=PEG_ID, h=PEG_H + 0.2, center=False, _fn=32)
    )
    return translate([0, 0, BASE_H])(
        difference()(outer, inner)
    )


def _base_button_holes():
    """3 button holes in a row on the front face of the base cylinder.

    Buttons centered horizontally, positioned BTN_Y_FROM_TOP below the top face.
    The front face is at Y = +BASE_R (outermost point along Y axis).
    Holes run along Y axis through the cylinder wall.
    """
    btn_z = BASE_H - BTN_Y_FROM_TOP
    spacing = BTN_SPACING
    holes = []
    start_x = -(BTN_N - 1) * spacing / 2
    for i in range(BTN_N):
        x = start_x + i * spacing
        hole = translate([x, BASE_R, btn_z])(
            rotate([90, 0, 0])(
                cylinder(d=BTN_HOLE_DIA, h=BASE_WALL + 0.2, center=True, _fn=32)
            )
        )
        holes.append(hole)
    return union()(*holes)


def _base_usbc_cutout():
    """USB-C port cutout through the back wall of the base cylinder.

    Back face center is at Y = -BASE_R. Cutout at mid-height.
    """
    usbc_z = BASE_H / 2
    slot = linear_extrude(height=BASE_WALL + 0.2, center=True)(
        pill_2d(USBC_W + 0.4, USBC_H + 0.4)
    )
    return translate([0, -BASE_R, usbc_z])(
        rotate([90, 0, 0])(slot)
    )


def _base_speaker_cavity():
    """Cylindrical cavity inside base for the speaker module."""
    # Centered in XY, sits on top of the base wall floor
    cavity_z = BASE_WALL
    return translate([0, 0, cavity_z])(
        cylinder(d=SPEAKER_DIA + 2.0, h=SPEAKER_D + 2.0, center=False, _fn=48)
    )


def _base_side_vents():
    """Side vent slots on the left/right arc of the base cylinder for speaker output."""
    vents = []
    # Arc from 60..120 degrees (right side) and 60..120 on left (mirror)
    # Each vent is a slot box cutting through the cylinder wall
    slot_angles_deg = [-80, -60, -40, 40, 60, 80]  # degrees from Y axis
    for angle_deg in slot_angles_deg:
        angle_rad = math.radians(angle_deg)
        # Vent center on cylinder surface
        cx = BASE_R * math.sin(angle_rad)
        cy = BASE_R * math.cos(angle_rad)
        # Z center: in the lower half of the cylinder (speaker area)
        cz = BASE_WALL + SPEAKER_D / 2 + 1.0
        vent = translate([cx, cy, cz])(
            rotate([0, 0, angle_deg])(
                cube([VENT_SLOT_W, BASE_WALL * 2 + 0.2, VENT_SLOT_H], center=True)
            )
        )
        vents.append(vent)
    return union()(*vents)


def _base_wire_bore():
    """Vertical bore through center of base for wire routing (matches peg ID)."""
    return cylinder(d=PEG_ID, h=BASE_H + PEG_H + 0.2, center=False, _fn=32)


# ── Tilt transform for the egg assembly ──

def _tilt_egg(solid):
    """Tilt egg forward 10 degrees (rotation around X axis, + = tilt top toward viewer)."""
    return rotate([EGG_TILT, 0, 0])(solid)


def _position_egg(solid):
    """Tilt and lift egg to sit on top of the base peg."""
    # Egg center in Y: with tilt, the very bottom of the egg touches the peg top.
    # The bottom of the untilted egg is at Y = -EGG_H/2 from egg center.
    # After tilt, this is approximate; keep it simple: center at EGG_BASE_Z in Z.
    tilted = _tilt_egg(solid)
    # Translate egg center to sit above base top + peg height
    # World Z of egg center = BASE_H + PEG_H + EGG_H/2 * sin(EGG_TILT) + small gap
    egg_center_z = BASE_H + PEG_H + EGG_H / 2 + 2.0
    return translate([0, 0, egg_center_z])(tilted)


# ────────────────────────────────────────────
# Public build functions
# ────────────────────────────────────────────

def build_front_shell():
    """Front half of the egg with screen window cutout.

    The split is along the Z=0 plane of the egg (mid front-to-back).
    The front shell is the +Z half (the face pointing toward the viewer).
    Tilted forward 10 degrees and lifted above the base.
    """
    shell = _egg_shell_solid()

    # Cut screen window through the front curved face
    shell = difference()(shell, _screen_window_cutter())

    # Keep only the front (+Z) half
    shell = intersection()(shell, _split_plane_cutter(keep_front=True))

    # Apply tilt and position above base
    shell = _position_egg(shell)

    return color("Ivory", 0.95)(shell)


def build_back_shell():
    """Back half of the egg with PCB standoffs and peg socket.

    The split is along the Z=0 plane of the egg (mid front-to-back).
    The back shell is the -Z half (the face pointing away from the viewer).
    Tilted forward 10 degrees and lifted above the base.
    """
    shell = _egg_shell_solid()

    # Flatten the bottom so it seats on the peg cleanly
    shell = difference()(shell, _bottom_flatten_cutter())

    # Peg socket in the bottom of the egg
    shell = difference()(shell, _peg_socket_cutter())

    # Keep only the back (-Z) half
    shell = intersection()(shell, _split_plane_cutter(keep_front=False))

    # Add PCB standoffs on inner back wall
    # Standoffs sit on the inner back face: Z_egg ~ -(EGG_D/2 - EGG_WALL)
    standoff_z = -(EGG_D / 2 - EGG_WALL)
    posts = translate([0, 0, standoff_z])(_pcb_standoffs_inside_egg())
    shell = union()(shell, posts)

    # Apply tilt and position above base
    shell = _position_egg(shell)

    return color("WhiteSmoke", 0.9)(shell)


def build_stand():
    """Cylindrical pedestal base with buttons, USB-C, speaker cavity, and side vents.

    This is a separate printable piece. The peg on top connects to the egg.
    """
    # Hollow cylinder shell
    base = difference()(
        _base_outer(),
        _base_hollow(),
    )

    # Speaker cavity inside (centered, lower portion)
    base = difference()(base, _base_speaker_cavity())

    # Top plate with wire bore (hollow peg channel continues through)
    # The top WALL acts as speaker mounting shelf — leave it intact except for bore
    base = difference()(base, _base_wire_bore())

    # Peg on top center
    base = union()(base, _base_top_peg())

    # Button holes on front face
    base = difference()(base, _base_button_holes())

    # USB-C on back face
    base = difference()(base, _base_usbc_cutout())

    # Side vents for speaker output
    base = difference()(base, _base_side_vents())

    return color("DimGray", 0.85)(base)


def build_internals():
    """Ghost components for visualization: screen, PCB, and speaker.

    Returns a dict with keys 'screen', 'pcb', 'speaker'.
    Screen and PCB are positioned inside the egg (tilted + lifted).
    Speaker is inside the base cylinder.
    """
    # Screen: centered in egg XY, front face near front wall
    screen_local = translate([0, 0, SCREEN_Z_IN_EGG])(
        rotate([0, 90, 0])(ghost_screen())  # screen face in +Z direction
    )
    # Actually screen face should face +Z (toward viewer), module centered:
    # ghost_screen() uses cube centered, front face at +SCREEN_MOD_D/2
    # We want it in XY plane with screen face pointing toward +Z of the egg
    screen_local = translate([0, 0, SCREEN_Z_IN_EGG])(ghost_screen())
    screen = _position_egg(screen_local)

    # PCB: behind screen
    pcb_local = translate([0, 0, PCB_Z_IN_EGG])(ghost_pcb())
    pcb = _position_egg(pcb_local)

    # Speaker: inside base cylinder, centered, sitting on base floor
    speaker_z = BASE_WALL + SPEAKER_D / 2
    speaker = translate([0, 0, speaker_z])(ghost_speaker())

    return {"screen": screen, "pcb": pcb, "speaker": speaker}


def _all_parts_assembled():
    """Union of all structural parts in assembled position."""
    return union()(
        build_front_shell(),
        build_back_shell(),
        build_stand(),
    )


def get_views():
    """Return 7 view definitions as a dict of {name: (solid, camera_string)}."""
    internals = build_internals()
    front_shell = build_front_shell()
    back_shell  = build_back_shell()
    stand       = build_stand()
    screen      = internals["screen"]
    pcb         = internals["pcb"]
    speaker     = internals["speaker"]

    assembled        = union()(front_shell, back_shell, stand)
    assembled_full   = union()(front_shell, back_shell, stand, screen, pcb, speaker)

    views = {}

    # Assembly center height for camera framing
    assy_cz = (BASE_H + PEG_H + EGG_H / 2 + 2.0) / 2  # ~51mm — midpoint of full assembly

    # 1. Assembled ISO — orb screen is on +Z face, so use iso_back (from above) to see it
    #    Include internals so screen/PCB are visible through shell
    views["assembled_iso"] = (
        assembled_full,
        camera_custom(rot_x=55, rot_y=0, rot_z=325, dist=500, tz=assy_cz),
    )

    # 2. Assembled front — egg screen is on +Z face; use rot_x=0 (sees +Z)
    views["assembled_front"] = (
        assembled_full,
        camera_custom(rot_x=0, rot_y=0, rot_z=0, dist=450, tz=assy_cz),
    )

    # 3. Assembled back — back of egg is -Z; use rot_x=180 (sees -Z)
    views["assembled_back"] = (
        assembled_full,
        camera_custom(rot_x=180, rot_y=0, rot_z=0, dist=450, tz=assy_cz),
    )

    # 4. Exploded — egg halves apart, base below
    views["exploded"] = (
        explode_parts([
            (front_shell, (0,   0,  60)),   # front half up/forward
            (back_shell,  (0,   0, -20)),   # back half slightly back
            (stand,       (0,   0, -80)),   # base pulled down
            (screen,      (0,   0,  40)),
            (pcb,         (0,   0,  20)),
            (speaker,     (0,   0, -60)),
        ]),
        camera_custom(rot_x=55, rot_y=0, rot_z=325, dist=600, tz=assy_cz),
    )

    # 5. Interior front — front half with ghost internals (screen on +Z, view from +Z)
    egg_cz = BASE_H + PEG_H + EGG_H / 2 + 2.0  # egg center Z
    views["interior_front"] = (
        union()(front_shell, screen, pcb),
        camera_custom(rot_x=0, rot_y=0, rot_z=0, dist=400, tz=egg_cz),
    )

    # 6. Interior back — back half with standoffs (view from -Z side)
    views["interior_back"] = (
        union()(back_shell, pcb),
        camera_custom(rot_x=180, rot_y=0, rot_z=0, dist=400, tz=egg_cz),
    )

    # 7. Cross-section through assembled model (cut along X axis, keep front half)
    views["cross_section"] = (
        cross_section_cut(assembled_full, axis="x", keep="back"),
        camera_custom(rot_x=55, rot_y=0, rot_z=325, dist=500, tz=assy_cz),
    )

    return views


def check_clearances():
    """Validate key dimensional constraints.

    Returns a dict of {check_name: (value, limit, passed)}.
    """
    results = {}

    # Egg interior depth (half-depth minus two walls for front-to-back interior)
    egg_interior_depth = EGG_D - EGG_WALL * 2
    depth_stack = SCREEN_MOD_D + 1.0 + PCB_D + ESP32_D
    results["egg_depth_stack"] = (
        depth_stack, egg_interior_depth,
        depth_stack <= egg_interior_depth,
    )

    # Screen module width fits within egg interior width (at screen Z position)
    # Egg width at Z = EGG_D/2 - EGG_WALL: approximate with full width minus walls
    egg_interior_w = EGG_W - EGG_WALL * 2
    results["screen_width_in_egg"] = (
        SCREEN_MOD_W, egg_interior_w,
        SCREEN_MOD_W <= egg_interior_w,
    )

    # Screen module height fits within egg interior height
    egg_interior_h = EGG_H - EGG_WALL * 2
    results["screen_height_in_egg"] = (
        SCREEN_MOD_H, egg_interior_h,
        SCREEN_MOD_H <= egg_interior_h,
    )

    # PCB fits within egg interior width
    results["pcb_width_in_egg"] = (
        PCB_W, egg_interior_w,
        PCB_W <= egg_interior_w,
    )

    # PCB fits within egg interior height
    results["pcb_height_in_egg"] = (
        PCB_H, egg_interior_h,
        PCB_H <= egg_interior_h,
    )

    # Base interior height accommodates speaker
    base_interior_h = BASE_H - BASE_WALL
    results["speaker_fits_base_height"] = (
        SPEAKER_D + 2.0, base_interior_h,
        SPEAKER_D + 2.0 <= base_interior_h,
    )

    # Speaker diameter fits inside base interior
    base_interior_dia = BASE_DIA - BASE_WALL * 2
    results["speaker_fits_base_dia"] = (
        SPEAKER_DIA + 2.0, base_interior_dia,
        SPEAKER_DIA + 2.0 <= base_interior_dia,
    )

    # Button row fits within base diameter
    btn_span = (BTN_N - 1) * BTN_SPACING
    results["button_row_span"] = (
        btn_span, BASE_DIA - BASE_WALL * 4,
        btn_span <= BASE_DIA - BASE_WALL * 4,
    )

    # Peg OD vs peg ID (structural: must have wall)
    results["peg_wall_thickness"] = (
        (PEG_OD - PEG_ID) / 2, 2.0,
        (PEG_OD - PEG_ID) / 2 >= 2.0,
    )

    return results


# ────────────────────────────────────────────
# Main: render default assembled view
# ────────────────────────────────────────────

if __name__ == "__main__":
    import os
    from solid2 import scad_render_to_file

    out_dir = os.path.join(os.path.dirname(__file__), "output")
    os.makedirs(out_dir, exist_ok=True)

    # Print clearance report
    print("=== Floating Orb — Clearance Check ===")
    for name, (value, limit, passed) in check_clearances().items():
        status = "OK" if passed else "FAIL"
        print(f"  [{status}] {name}: {value:.2f} vs limit {limit:.2f}")

    # Render all views
    views = get_views()
    for view_name, (solid, camera) in views.items():
        out_path = os.path.join(out_dir, f"design_3_orb_{view_name}.scad")
        scad_render_to_file(solid, out_path)
        print(f"  Wrote {out_path}")

    print("Done.")
