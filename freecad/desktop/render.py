"""Desktop Devotchi — OpenSCAD Rendering Pipeline.

Generates .scad files from SolidPython2 objects, then invokes
OpenSCAD CLI to render PNG images. Handles camera positioning,
timeouts, and error reporting.
"""
import os
import subprocess
import sys
import time
from solid2 import scad_render

OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "output")
RENDER_TIMEOUT = 120  # seconds per render
IMG_SIZE = "1200,900"
COLOR_SCHEME = "Tomorrow"


def ensure_output_dir():
    os.makedirs(OUTPUT_DIR, exist_ok=True)


def write_scad(scad_object, filename):
    """Write a SolidPython2 object to a .scad file. Returns the file path."""
    ensure_output_dir()
    scad_code = scad_render(scad_object)
    path = os.path.join(OUTPUT_DIR, filename)
    with open(path, "w") as f:
        f.write(scad_code)
    return path


def render_png(scad_path, png_path, camera_str, img_size=None):
    """Invoke OpenSCAD CLI to render a .scad file to PNG.

    camera_str: 'tx,ty,tz,rot_x,rot_y,rot_z,dist' (7 numbers)
    Returns True on success, False on failure.
    """
    img_size = img_size or IMG_SIZE
    cmd = [
        "openscad",
        "--render",
        f"--camera={camera_str}",
        f"--imgsize={img_size}",
        f"--colorscheme={COLOR_SCHEME}",
        "-o", png_path,
        scad_path,
    ]

    print(f"  Rendering: {os.path.basename(png_path)} ... ", end="", flush=True)
    start = time.time()

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=RENDER_TIMEOUT,
            env={**os.environ, "DISPLAY": ":99"},  # Xvfb if available
        )
        elapsed = time.time() - start

        if result.returncode != 0:
            print(f"FAILED ({elapsed:.1f}s)")
            print(f"    stderr: {result.stderr[:300]}")
            return False

        if os.path.exists(png_path) and os.path.getsize(png_path) > 0:
            print(f"OK ({elapsed:.1f}s)")
            return True
        else:
            print(f"EMPTY ({elapsed:.1f}s)")
            return False

    except subprocess.TimeoutExpired:
        print(f"TIMEOUT (>{RENDER_TIMEOUT}s)")
        return False
    except FileNotFoundError:
        print("ERROR: openscad not found in PATH")
        return False


def render_design(design_name, views):
    """Render all views for a single design.

    views: list of dicts with keys:
        - name: str (e.g., 'assembled_iso')
        - scad_object: SolidPython2 object
        - camera: camera string for openscad

    Returns dict with results.
    """
    ensure_output_dir()
    results = {"design": design_name, "views": [], "success": 0, "failed": 0}

    for view in views:
        view_name = view["name"]
        scad_obj = view["scad_object"]
        camera = view["camera"]

        scad_filename = f"{design_name}_{view_name}.scad"
        png_filename = f"{design_name}_{view_name}.png"
        png_path = os.path.join(OUTPUT_DIR, png_filename)

        # Write .scad file
        scad_path = write_scad(scad_obj, scad_filename)

        # Render to PNG
        ok = render_png(scad_path, png_path, camera)

        results["views"].append({
            "name": view_name,
            "scad": scad_path,
            "png": png_path,
            "success": ok,
        })

        if ok:
            results["success"] += 1
        else:
            results["failed"] += 1

    return results


def setup_xvfb():
    """Start Xvfb for headless OpenSCAD rendering."""
    try:
        # Check if DISPLAY is already set and working
        result = subprocess.run(
            ["xdpyinfo"],
            capture_output=True,
            timeout=5,
            env={**os.environ, "DISPLAY": ":99"},
        )
        if result.returncode == 0:
            os.environ["DISPLAY"] = ":99"
            return True
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass

    # Try to start Xvfb
    try:
        subprocess.Popen(
            ["Xvfb", ":99", "-screen", "0", "1280x1024x24"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(1)
        os.environ["DISPLAY"] = ":99"
        print("  Xvfb started on :99")
        return True
    except FileNotFoundError:
        print("  WARNING: Xvfb not found, trying with existing DISPLAY")
        return False


def print_summary(all_results):
    """Print a summary table of all render results."""
    print(f"\n{'=' * 60}")
    print("RENDER SUMMARY")
    print(f"{'=' * 60}")
    print(f"{'Design':<25} {'OK':<6} {'Failed':<8} {'Total':<6}")
    print("-" * 60)

    total_ok = 0
    total_fail = 0
    for r in all_results:
        print(f"  {r['design']:<23} {r['success']:<6} {r['failed']:<8} {r['success'] + r['failed']:<6}")
        total_ok += r["success"]
        total_fail += r["failed"]

    print("-" * 60)
    print(f"  {'TOTAL':<23} {total_ok:<6} {total_fail:<8} {total_ok + total_fail:<6}")
    print(f"{'=' * 60}")
