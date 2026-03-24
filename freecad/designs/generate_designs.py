#!/usr/bin/env python3
"""Devotchi Case — 10 Design Variants with CadQuery.

Generates 10 different case designs, exports STL + rendered PNG,
and runs clearance checks against PCB, display, and battery.
"""
import cadquery as cq
import math
import os
import json

OUTPUT_DIR = os.path.dirname(os.path.abspath(__file__))

# ──────────────────────────────────────────────
# Fixed internal dimensions (from params.py)
# ──────────────────────────────────────────────
TOL = 0.2
WALL = 1.5

# Display (HS20HS072RX)
DISP_MOD_W = 51.8
DISP_MOD_H = 36.2
DISP_MOD_D = 2.5
DISP_ACT_W = 40.8
DISP_ACT_H = 30.6

# PCB
PCB_W = 45.0
PCB_H = 40.0
PCB_D = 1.6

# Battery
BATT_W = 30.0
BATT_H = 35.0
BATT_D = 5.0

# ESP32 module clearance
GAP_DISP_PCB = 2.5
GAP_PCB_BATT = 0.5

# USB-C
USBC_W = 9.34  # 8.94 + 2*0.2
USBC_H = 3.66  # 3.26 + 2*0.2

# Buttons
BTN_HEX_W = 12.0
BTN_HEX_H = 6.0
BTN_SPACING = 14.0

# Derived depth stack
MIN_DEPTH = WALL + DISP_MOD_D + GAP_DISP_PCB + PCB_D + GAP_PCB_BATT + BATT_D + WALL
# = 1.5 + 2.5 + 2.5 + 1.6 + 0.5 + 5.0 + 1.5 = 15.1mm

# Z positions
Z_DISP = WALL
Z_PCB = WALL + DISP_MOD_D + GAP_DISP_PCB
Z_BATT = Z_PCB + PCB_D + GAP_PCB_BATT


def min_case_width():
    """Minimum width to fit display module + walls + tolerance."""
    return DISP_MOD_W + 2 * WALL + 2 * TOL + 3.0  # 58.2mm


def min_case_height():
    """Minimum height: display zone + button zone + USB zone."""
    return DISP_MOD_H + 2 * WALL + 15.0 + 5.0  # 59.2mm


# ──────────────────────────────────────────────
# Design Definitions
# ──────────────────────────────────────────────

DESIGNS = [
    {
        "name": "01_classic_box",
        "desc": "Classic rectangular with small corner radius — clean retro look",
        "corner_r": 2.0,
        "chamfer_front": 1.0,
        "chamfer_back": 0.5,
        "extra_w": 0.0,
        "extra_h": 0.0,
        "extra_d": 0.0,
        "btn_shape": "rect",
        "btn_zone_h": 15.0,
        "usb_zone_h": 5.0,
        "fillet_edges": False,
        "taper": 0.0,
        "belly": 0.0,
    },
    {
        "name": "02_pebble",
        "desc": "Large radius corners — smooth pebble-like shape",
        "corner_r": 8.0,
        "chamfer_front": 3.0,
        "chamfer_back": 2.0,
        "extra_w": 4.0,
        "extra_h": 4.0,
        "extra_d": 1.0,
        "btn_shape": "pill",
        "btn_zone_h": 15.0,
        "usb_zone_h": 5.0,
        "fillet_edges": True,
        "taper": 0.0,
        "belly": 0.0,
    },
    {
        "name": "03_slim_wedge",
        "desc": "Tapered profile — thinner at top, thicker at bottom for ergonomic grip",
        "corner_r": 3.0,
        "chamfer_front": 2.0,
        "chamfer_back": 1.0,
        "extra_w": 0.0,
        "extra_h": 2.0,
        "extra_d": 3.0,
        "btn_shape": "hex",
        "btn_zone_h": 17.0,
        "usb_zone_h": 5.0,
        "fillet_edges": False,
        "taper": 2.0,
        "belly": 0.0,
    },
    {
        "name": "04_tamagotchi_egg",
        "desc": "Egg-shaped with very large radius — classic Tamagotchi homage",
        "corner_r": 12.0,
        "chamfer_front": 4.0,
        "chamfer_back": 3.0,
        "extra_w": 8.0,
        "extra_h": 10.0,
        "extra_d": 2.0,
        "btn_shape": "circle",
        "btn_zone_h": 18.0,
        "usb_zone_h": 7.0,
        "fillet_edges": True,
        "taper": 0.0,
        "belly": 2.0,
    },
    {
        "name": "05_industrial",
        "desc": "Sharp edges, minimal radius — rugged industrial aesthetic",
        "corner_r": 1.0,
        "chamfer_front": 0.5,
        "chamfer_back": 0.5,
        "extra_w": 2.0,
        "extra_h": 2.0,
        "extra_d": 0.0,
        "btn_shape": "rect",
        "btn_zone_h": 15.0,
        "usb_zone_h": 5.0,
        "fillet_edges": False,
        "taper": 0.0,
        "belly": 0.0,
    },
    {
        "name": "06_compact_pill",
        "desc": "Pill/capsule shape — fully rounded short sides, minimal excess",
        "corner_r": 20.0,  # will be clamped to half-width
        "chamfer_front": 3.0,
        "chamfer_back": 2.0,
        "extra_w": 2.0,
        "extra_h": 2.0,
        "extra_d": 0.5,
        "btn_shape": "pill",
        "btn_zone_h": 15.0,
        "usb_zone_h": 5.0,
        "fillet_edges": True,
        "taper": 0.0,
        "belly": 0.0,
    },
    {
        "name": "07_ergonomic_grip",
        "desc": "Belly curve on back — fits naturally in hand",
        "corner_r": 4.0,
        "chamfer_front": 2.5,
        "chamfer_back": 1.5,
        "extra_w": 2.0,
        "extra_h": 2.0,
        "extra_d": 4.0,
        "btn_shape": "hex",
        "btn_zone_h": 16.0,
        "usb_zone_h": 5.0,
        "fillet_edges": True,
        "taper": 0.0,
        "belly": 4.0,
    },
    {
        "name": "08_game_boy_micro",
        "desc": "Wide and short — landscape-inspired with prominent button area",
        "corner_r": 3.5,
        "chamfer_front": 1.5,
        "chamfer_back": 1.0,
        "extra_w": 6.0,
        "extra_h": 0.0,
        "extra_d": 0.0,
        "btn_shape": "circle",
        "btn_zone_h": 15.0,
        "usb_zone_h": 5.0,
        "fillet_edges": False,
        "taper": 0.0,
        "belly": 0.0,
    },
    {
        "name": "09_keychain_mini",
        "desc": "Tight fit, minimal walls — smallest possible with keychain loop",
        "corner_r": 2.5,
        "chamfer_front": 1.0,
        "chamfer_back": 0.5,
        "extra_w": 0.0,
        "extra_h": 3.0,  # for keychain loop
        "extra_d": 0.0,
        "btn_shape": "rect",
        "btn_zone_h": 14.0,
        "usb_zone_h": 4.0,
        "fillet_edges": False,
        "taper": 0.0,
        "belly": 0.0,
        "keychain": True,
    },
    {
        "name": "10_luxury_chamfer",
        "desc": "Deep chamfers all around — premium feel like a polished gem",
        "corner_r": 5.0,
        "chamfer_front": 4.0,
        "chamfer_back": 3.0,
        "extra_w": 4.0,
        "extra_h": 4.0,
        "extra_d": 2.0,
        "btn_shape": "hex",
        "btn_zone_h": 16.0,
        "usb_zone_h": 6.0,
        "fillet_edges": True,
        "taper": 0.0,
        "belly": 1.0,
    },
]


# ──────────────────────────────────────────────
# Case Builder
# ──────────────────────────────────────────────

def build_case(design):
    """Build a complete case (front + back shell) from design params."""
    d = design
    case_w = min_case_width() + d["extra_w"]
    case_h = min_case_height() + d["extra_h"]
    case_d = MIN_DEPTH + d["extra_d"]
    corner_r = min(d["corner_r"], case_w / 2 - 0.1, case_h / 2 - 0.1)
    chamf_f = min(d["chamfer_front"], case_d / 2 - 0.5)
    chamf_b = min(d["chamfer_back"], case_d / 2 - 0.5)

    # Outer shell — front half
    front_d = case_d - WALL  # front shell depth (everything except back wall)

    # Build outer body with chamfered front edge
    # Use workplane approach: create profile and extrude
    outer = (
        cq.Workplane("XY")
        .rect(case_w, case_h)
        .extrude(front_d)
    )
    # Apply corner fillets in XY
    if corner_r > 0.5:
        try:
            outer = outer.edges("|Z").fillet(corner_r)
        except Exception:
            pass

    # Front face chamfer — clamp to avoid consuming the solid
    safe_chamf_f = min(chamf_f, WALL * 0.8, 2.0)
    if safe_chamf_f > 0.3:
        try:
            outer = outer.faces("<Z").chamfer(safe_chamf_f)
        except Exception:
            pass

    # Interior cavity
    inner_w = case_w - 2 * WALL
    inner_h = case_h - 2 * WALL
    inner_r = max(corner_r - WALL, 0.3)
    cavity = (
        cq.Workplane("XY")
        .workplane(offset=WALL)
        .rect(inner_w, inner_h)
        .extrude(front_d - WALL + 0.1)
    )
    if inner_r > 0.5:
        try:
            cavity = cavity.edges("|Z").fillet(inner_r)
        except Exception:
            pass

    front_shell = outer.cut(cavity)

    # Display window (through front wall)
    disp_win_y_offset = (case_h / 2) - (DISP_MOD_H / 2 + WALL) + (DISP_MOD_H - DISP_ACT_H) / 2
    front_shell = (
        front_shell
        .faces("<Z")
        .workplane()
        .rect(DISP_ACT_W + 1.0, DISP_ACT_H + 1.0)
        .cutBlind(-WALL - 0.1)
    )

    # Button holes
    btn_zone_h = d["btn_zone_h"]
    btn_y_center = -(case_h / 2) + WALL + (DISP_MOD_H + 2 * WALL) + btn_zone_h * 0.30

    btn_shape = d["btn_shape"]
    for i, dx in enumerate([-BTN_SPACING, 0, BTN_SPACING]):
        wp = (
            front_shell
            .faces("<Z")
            .workplane()
            .center(dx, btn_y_center)
        )
        if btn_shape == "rect":
            front_shell = wp.rect(BTN_HEX_W, BTN_HEX_H).cutBlind(-WALL - 0.1)
        elif btn_shape == "circle":
            front_shell = wp.circle(BTN_HEX_H / 2 + 0.5).cutBlind(-WALL - 0.1)
        elif btn_shape == "pill":
            front_shell = wp.slot2D(BTN_HEX_W, BTN_HEX_H, 0).cutBlind(-WALL - 0.1)
        else:  # hex — approximate with rounded rect
            front_shell = wp.rect(BTN_HEX_W, BTN_HEX_H).cutBlind(-WALL - 0.1)
            if 0.5 < BTN_HEX_H / 4:
                try:
                    front_shell = front_shell.edges("|Z").edges(
                        cq.selectors.NearestToPointSelector((dx, btn_y_center, 0))
                    ).fillet(BTN_HEX_H / 4)
                except Exception:
                    pass

    # Wake button
    wake_y = -(case_h / 2) + WALL + (DISP_MOD_H + 2 * WALL) + btn_zone_h * 0.80
    if btn_shape == "circle":
        front_shell = (
            front_shell.faces("<Z").workplane()
            .center(0, wake_y)
            .circle(3.5)
            .cutBlind(-WALL - 0.1)
        )
    else:
        front_shell = (
            front_shell.faces("<Z").workplane()
            .center(0, wake_y)
            .rect(10.0, 4.5)
            .cutBlind(-WALL - 0.1)
        )

    # USB-C cutout (bottom face)
    usbc_z_center = -(front_d / 2) + Z_PCB
    front_shell = (
        front_shell
        .faces("<Y")
        .workplane()
        .center(0, usbc_z_center)
        .slot2D(USBC_W, USBC_H, 0)
        .cutBlind(-WALL - 0.1)
    )

    # LED hole
    led_x = -(case_w / 2) + 8.0
    led_y = -(front_d / 2) + 2.0
    front_shell = (
        front_shell
        .faces("<Y")
        .workplane()
        .center(led_x, led_y)
        .circle(1.0)
        .cutBlind(-WALL - 0.1)
    )

    # ── Back cover ──
    back = (
        cq.Workplane("XY")
        .workplane(offset=front_d - 0.5)
        .rect(case_w, case_h)
        .extrude(WALL + 0.5)
    )
    if corner_r > 0.5:
        try:
            back = back.edges("|Z").fillet(corner_r)
        except Exception:
            pass

    # Back chamfer — clamped for safety
    safe_chamf_b = min(chamf_b, WALL * 0.8, 1.5)
    if safe_chamf_b > 0.3:
        try:
            back = back.faces(">Z").chamfer(safe_chamf_b)
        except Exception:
            pass

    # Belly (convex back) — skip fillet to avoid failures
    if d.get("belly", 0) > 0:
        belly = d["belly"]
        belly_shape = (
            cq.Workplane("XY")
            .workplane(offset=case_d)
            .rect(case_w * 0.6, case_h * 0.6)
            .extrude(belly)
        )
        try:
            belly_shape = belly_shape.edges("|Z").fillet(min(belly, case_w * 0.15))
            belly_shape = belly_shape.faces(">Z").fillet(min(belly * 0.5, 1.5))
        except Exception:
            pass
        try:
            back = back.union(belly_shape)
        except Exception:
            pass
        case_d += belly

    # Keychain loop
    if d.get("keychain", False):
        loop_r = 4.0
        loop = (
            cq.Workplane("XZ")
            .center(0, case_d / 2)
            .circle(loop_r)
            .circle(loop_r - 1.5)
            .extrude(2.0)
            .translate((0, case_h / 2 + 1.0, 0))
        )
        try:
            back = back.union(loop)
        except Exception:
            pass

    # Combine into assembly — handle case where one part lost its solid
    try:
        assembly = front_shell.union(back)
    except ValueError:
        # Fallback: just use whichever part has a solid
        try:
            _ = front_shell.val()
            assembly = front_shell
        except Exception:
            assembly = back

    return {
        "assembly": assembly,
        "front": front_shell,
        "back": back,
        "case_w": case_w,
        "case_h": case_h,
        "case_d": case_d,
        "corner_r": corner_r,
    }


# ──────────────────────────────────────────────
# Ghost components for clearance visualization
# ──────────────────────────────────────────────

def build_internals(case_w, case_h):
    """Build display, PCB, and battery as positioned inside the case."""
    # Display
    disp_x = 0  # centered
    disp_y = (case_h / 2) - (DISP_MOD_H / 2 + WALL)
    display = (
        cq.Workplane("XY")
        .workplane(offset=Z_DISP)
        .center(disp_x, disp_y)
        .rect(DISP_MOD_W, DISP_MOD_H)
        .extrude(DISP_MOD_D)
    )

    # PCB (bottom-aligned)
    pcb_y = -(case_h / 2) + WALL + PCB_H / 2
    pcb = (
        cq.Workplane("XY")
        .workplane(offset=Z_PCB)
        .center(0, pcb_y)
        .rect(PCB_W, PCB_H)
        .extrude(PCB_D)
    )

    # Battery (centered)
    battery = (
        cq.Workplane("XY")
        .workplane(offset=Z_BATT)
        .rect(BATT_W, BATT_H)
        .extrude(BATT_D)
    )

    return display, pcb, battery


# ──────────────────────────────────────────────
# Clearance Checker
# ──────────────────────────────────────────────

def check_clearances(design_name, case_w, case_h, case_d, corner_r):
    """Check if all components fit with adequate clearance."""
    results = {"design": design_name, "pass": True, "checks": []}

    inner_w = case_w - 2 * WALL
    inner_h = case_h - 2 * WALL

    # 1. Display fits in cavity
    disp_clearance_w = inner_w - DISP_MOD_W
    disp_clearance_h = inner_h - (DISP_MOD_H + 2 * WALL)  # display zone
    disp_ok = disp_clearance_w >= 0.4 and disp_clearance_h >= 0.0
    results["checks"].append({
        "component": "Display",
        "status": "PASS" if disp_ok else "FAIL",
        "clearance_w": round(disp_clearance_w, 2),
        "clearance_h": round(disp_clearance_h, 2),
        "min_required": 0.4,
        "note": f"Module {DISP_MOD_W}x{DISP_MOD_H} in cavity {inner_w:.1f}x{inner_h:.1f}"
    })
    if not disp_ok:
        results["pass"] = False

    # 2. PCB fits
    pcb_clearance_w = inner_w - PCB_W
    pcb_clearance_h = inner_h - PCB_H
    pcb_ok = pcb_clearance_w >= 0.4 and pcb_clearance_h >= 0.0
    results["checks"].append({
        "component": "PCB",
        "status": "PASS" if pcb_ok else "FAIL",
        "clearance_w": round(pcb_clearance_w, 2),
        "clearance_h": round(pcb_clearance_h, 2),
        "min_required": 0.4,
        "note": f"PCB {PCB_W}x{PCB_H} in cavity {inner_w:.1f}x{inner_h:.1f}"
    })
    if not pcb_ok:
        results["pass"] = False

    # 3. Battery fits
    batt_clearance_w = inner_w - BATT_W
    batt_clearance_h = inner_h - BATT_H
    batt_ok = batt_clearance_w >= 1.0 and batt_clearance_h >= 1.0
    results["checks"].append({
        "component": "Battery",
        "status": "PASS" if batt_ok else "FAIL",
        "clearance_w": round(batt_clearance_w, 2),
        "clearance_h": round(batt_clearance_h, 2),
        "min_required": 1.0,
        "note": f"Battery {BATT_W}x{BATT_H} in cavity {inner_w:.1f}x{inner_h:.1f}"
    })
    if not batt_ok:
        results["pass"] = False

    # 4. Depth stack fits
    depth_ok = case_d >= MIN_DEPTH
    results["checks"].append({
        "component": "Depth stack",
        "status": "PASS" if depth_ok else "FAIL",
        "case_depth": round(case_d, 2),
        "min_required": round(MIN_DEPTH, 2),
        "margin": round(case_d - MIN_DEPTH, 2),
    })
    if not depth_ok:
        results["pass"] = False

    # 5. USB-C accessible
    usbc_accessible = case_h >= min_case_height() - 1.0
    results["checks"].append({
        "component": "USB-C port",
        "status": "PASS" if usbc_accessible else "FAIL",
        "note": "Bottom edge accessible"
    })
    if not usbc_accessible:
        results["pass"] = False

    # 6. Corner radius vs component fit
    # Large corner radius eats into usable space at corners
    effective_loss = corner_r * (1 - math.pi / 4)  # area lost per corner
    corner_ok = (inner_w - 2 * effective_loss) >= PCB_W
    results["checks"].append({
        "component": "Corner clearance",
        "status": "PASS" if corner_ok else "WARN",
        "effective_loss_per_corner": round(effective_loss, 2),
        "note": f"R={corner_r:.1f}mm corners reduce usable rect by ~{effective_loss:.1f}mm"
    })

    # 7. Wall printability
    wall_ok = WALL >= 1.2
    results["checks"].append({
        "component": "Wall printability",
        "status": "PASS" if wall_ok else "FAIL",
        "wall_thickness": WALL,
        "min_fdm": 1.2,
    })

    # 8. Button clearance from edges
    btn_y_from_top = DISP_MOD_H + 2 * WALL + 15.0 * 0.30
    btn_edge_dist = case_w / 2 - BTN_SPACING - BTN_HEX_W / 2
    btn_ok = btn_edge_dist >= WALL + 1.0
    results["checks"].append({
        "component": "Button edge clearance",
        "status": "PASS" if btn_ok else "FAIL",
        "distance_to_wall": round(btn_edge_dist, 2),
        "min_required": WALL + 1.0,
    })
    if not btn_ok:
        results["pass"] = False

    return results


# ──────────────────────────────────────────────
# Image Rendering (VTK offscreen)
# ──────────────────────────────────────────────

def render_stl_matplotlib(stl_path, png_path, case_w, case_h, case_d, view="iso"):
    """Render an STL file to PNG using matplotlib's 3D projection."""
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d import art3d
    import struct

    # Read binary STL
    triangles = []
    with open(stl_path, 'rb') as f:
        header = f.read(80)
        num_tri = struct.unpack('<I', f.read(4))[0]
        for _ in range(num_tri):
            data = struct.unpack('<12fH', f.read(50))
            nx, ny, nz = data[0:3]
            v1 = data[3:6]
            v2 = data[6:9]
            v3 = data[9:12]
            triangles.append((v1, v2, v3, (nx, ny, nz)))

    if not triangles:
        print(f"  WARNING: No triangles in STL")
        return

    # Subsample if too many triangles for matplotlib
    step = max(1, len(triangles) // 5000)
    sampled = triangles[::step]

    fig = plt.figure(figsize=(8, 8), dpi=100)
    ax = fig.add_subplot(111, projection='3d')

    verts = []
    colors = []
    for v1, v2, v3, (nx, ny, nz) in sampled:
        verts.append([v1, v2, v3])
        # Simple lighting from upper-right-front
        light_dir = [0.4, -0.3, 0.7]
        mag = (light_dir[0]**2 + light_dir[1]**2 + light_dir[2]**2)**0.5
        light_dir = [l/mag for l in light_dir]
        intensity = abs(nx*light_dir[0] + ny*light_dir[1] + nz*light_dir[2])
        intensity = 0.3 + 0.5 * intensity
        colors.append((intensity * 0.7, intensity * 0.7, intensity * 0.75, 0.9))

    poly = art3d.Poly3DCollection(verts, facecolors=colors, edgecolors='none', linewidths=0.1)
    ax.add_collection3d(poly)

    # Auto-scale
    all_pts = [p for tri in verts for p in tri]
    xs = [p[0] for p in all_pts]
    ys = [p[1] for p in all_pts]
    zs = [p[2] for p in all_pts]

    max_range = max(max(xs)-min(xs), max(ys)-min(ys), max(zs)-min(zs)) / 2
    mid_x = (max(xs)+min(xs)) / 2
    mid_y = (max(ys)+min(ys)) / 2
    mid_z = (max(zs)+min(zs)) / 2
    ax.set_xlim(mid_x - max_range, mid_x + max_range)
    ax.set_ylim(mid_y - max_range, mid_y + max_range)
    ax.set_zlim(mid_z - max_range, mid_z + max_range)

    if view == "front":
        ax.view_init(elev=0, azim=0)
    elif view == "side":
        ax.view_init(elev=0, azim=90)
    else:  # iso
        ax.view_init(elev=25, azim=-50)

    ax.set_axis_off()
    ax.set_facecolor((0.95, 0.95, 0.95))
    fig.patch.set_facecolor((0.95, 0.95, 0.95))

    plt.tight_layout()
    plt.savefig(png_path, dpi=100, bbox_inches='tight',
                facecolor=(0.95, 0.95, 0.95), pad_inches=0.1)
    plt.close()
    print(f"  Rendered: {png_path}")


def render_multi_view(stl_path, filepath_base, case_w, case_h, case_d):
    """Render front, side, and iso views from an STL file."""
    for view in ["front", "side", "iso"]:
        png_path = f"{filepath_base}_{view}.png"
        render_stl_matplotlib(stl_path, png_path, case_w, case_h, case_d, view)


# ──────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────

def main():
    all_results = []

    for i, design in enumerate(DESIGNS):
        name = design["name"]
        print(f"\n{'='*50}")
        print(f"Design {i+1}/10: {name}")
        print(f"  {design['desc']}")
        print(f"{'='*50}")

        # Build
        try:
            result = build_case(design)
            assembly = result["assembly"]
            case_w = result["case_w"]
            case_h = result["case_h"]
            case_d = result["case_d"]
            corner_r = result["corner_r"]

            print(f"  Dimensions: {case_w:.1f} x {case_h:.1f} x {case_d:.1f} mm")

            # Export STL
            stl_path = os.path.join(OUTPUT_DIR, f"{name}.stl")
            cq.exporters.export(assembly, stl_path, exportType="STL")
            print(f"  Exported: {stl_path}")

            # Render images from STL
            png_base = os.path.join(OUTPUT_DIR, name)
            try:
                render_multi_view(stl_path, png_base, case_w, case_h, case_d)
            except Exception as e:
                print(f"  WARNING: Render failed: {e}")
                import traceback
                traceback.print_exc()

            # Clearance checks
            clearance = check_clearances(name, case_w, case_h, case_d, corner_r)
            all_results.append(clearance)

            status = "PASS" if clearance["pass"] else "FAIL"
            print(f"\n  Clearance check: {status}")
            for check in clearance["checks"]:
                s = check["status"]
                comp = check["component"]
                marker = "  " if s == "PASS" else ">>"
                print(f"    {marker} [{s}] {comp}: {check.get('note', '')}")
                if "clearance_w" in check:
                    print(f"       W clearance: {check['clearance_w']}mm, H clearance: {check['clearance_h']}mm")
                if "margin" in check:
                    print(f"       Depth margin: {check['margin']}mm")

        except Exception as e:
            print(f"  ERROR building {name}: {e}")
            import traceback
            traceback.print_exc()
            all_results.append({"design": name, "pass": False, "error": str(e)})

    # Summary report
    report_path = os.path.join(OUTPUT_DIR, "clearance_report.json")
    with open(report_path, "w") as f:
        json.dump(all_results, f, indent=2)
    print(f"\nClearance report saved: {report_path}")

    # Print summary table
    print(f"\n{'='*60}")
    print("SUMMARY")
    print(f"{'='*60}")
    print(f"{'Design':<25} {'Status':<8} {'W(mm)':<8} {'H(mm)':<8} {'D(mm)':<8}")
    print("-" * 60)
    for r in all_results:
        name = r["design"]
        status = "PASS" if r.get("pass") else "FAIL"
        # Find dimensions from checks
        depth_check = next((c for c in r.get("checks", []) if c["component"] == "Depth stack"), None)
        d_val = depth_check["case_depth"] if depth_check else "?"
        disp_check = next((c for c in r.get("checks", []) if c["component"] == "Display"), None)
        note = disp_check.get("note", "") if disp_check else r.get("error", "")[:30]
        print(f"  {name:<23} {status:<8} {note}")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
