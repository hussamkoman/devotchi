#!/usr/bin/env python3
"""Desktop Devotchi — Generate All 6 Case Designs.

Entry point: imports all designs, renders views, runs clearance checks,
and outputs a summary report.

Handles different return formats from design modules gracefully.
"""
import json
import os
import sys
import time

# Add this directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from render import render_design, setup_xvfb, print_summary, OUTPUT_DIR

# Design modules — each exports the same interface
DESIGNS = [
    ("design_1_easel", "01_easel"),
    ("design_2_wedge", "02_wedge"),
    ("design_3_orb", "03_orb"),
    ("design_4_monolith", "04_monolith"),
    ("design_5_retro_tv", "05_retro_tv"),
    ("design_6_hex_tower", "06_hex_tower"),
]


def normalize_views(raw_views):
    """Convert various view formats to the standard list-of-dicts format.

    Handles:
    - dict {name: (solid, camera)} — Design 4 style
    - list of dicts [{name, scad_object, camera}] — standard format
    - list of tuples [(name, solid, camera)] — alternative
    """
    if isinstance(raw_views, dict):
        # {name: (solid, camera_str)}
        return [
            {"name": name, "scad_object": solid, "camera": cam}
            for name, (solid, cam) in raw_views.items()
        ]
    elif isinstance(raw_views, list):
        if not raw_views:
            return []
        first = raw_views[0]
        if isinstance(first, dict):
            # Already in standard format — ensure keys exist
            normalized = []
            for v in raw_views:
                normalized.append({
                    "name": v.get("name", "unnamed"),
                    "scad_object": v.get("scad_object", v.get("solid")),
                    "camera": v.get("camera", "0,0,0,55,0,325,400"),
                })
            return normalized
        elif isinstance(first, (list, tuple)):
            # [(name, solid, camera)]
            return [
                {"name": t[0], "scad_object": t[1], "camera": t[2]}
                for t in raw_views
            ]
    return raw_views


def normalize_clearance(raw, design_name):
    """Convert various clearance formats to standard format.

    Standard: {"design": str, "pass": bool, "checks": [{name, pass, detail}]}
    Design 4 style: {check_name: (value, limit, passed)}
    """
    if isinstance(raw, dict):
        # Check if it already has the standard format
        if "checks" in raw:
            raw.setdefault("design", design_name)
            return raw

        # Design 4 style: {check_name: (value, limit, passed)}
        checks = []
        all_pass = True
        for name, val in raw.items():
            if name in ("design", "pass", "checks", "error"):
                continue
            if isinstance(val, (list, tuple)) and len(val) == 3:
                value, limit, passed = val
                checks.append({
                    "name": name,
                    "pass": bool(passed),
                    "detail": f"{value} vs limit {limit}",
                })
                if not passed:
                    all_pass = False
            else:
                checks.append({"name": name, "pass": True, "detail": str(val)})

        return {
            "design": design_name,
            "pass": all_pass,
            "checks": checks,
        }
    return {"design": design_name, "pass": False, "checks": [], "error": "Unknown format"}


def main():
    start_time = time.time()
    print("=" * 60)
    print("Desktop Devotchi — Case Design Generator")
    print("=" * 60)

    # Setup headless display
    print("\nSetting up display...")
    setup_xvfb()

    # Import and process each design
    all_render_results = []
    all_clearance_results = []

    for module_name, output_prefix in DESIGNS:
        print(f"\n{'=' * 60}")
        print(f"Design: {module_name}")
        print(f"{'=' * 60}")

        # Import the design module
        try:
            mod = __import__(module_name)
        except ImportError as e:
            print(f"  SKIP: Could not import {module_name}: {e}")
            continue
        except Exception as e:
            print(f"  ERROR importing {module_name}: {e}")
            import traceback
            traceback.print_exc()
            continue

        # Get views — normalize format
        try:
            raw_views = mod.get_views()
            views = normalize_views(raw_views)
            print(f"  Views: {len(views)}")
        except Exception as e:
            print(f"  ERROR getting views: {e}")
            import traceback
            traceback.print_exc()
            continue

        # Render all views
        render_result = render_design(output_prefix, views)
        all_render_results.append(render_result)

        # Run clearance checks — normalize format
        try:
            raw_clearance = mod.check_clearances()
            clearance = normalize_clearance(raw_clearance, module_name)
            all_clearance_results.append(clearance)
            status = "PASS" if clearance.get("pass", False) else "FAIL"
            print(f"\n  Clearance: {status}")
            for check in clearance.get("checks", []):
                name = check.get("name", "?")
                ok = check.get("pass", False)
                marker = "OK" if ok else "!!"
                print(f"    [{marker}] {name}: {check.get('detail', '')}")
        except Exception as e:
            print(f"  ERROR in clearance check: {e}")
            all_clearance_results.append({
                "design": module_name,
                "pass": False,
                "error": str(e),
            })

    # Write clearance report
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    report_path = os.path.join(OUTPUT_DIR, "clearance_report.json")
    with open(report_path, "w") as f:
        json.dump(all_clearance_results, f, indent=2)
    print(f"\nClearance report: {report_path}")

    # Print render summary
    print_summary(all_render_results)

    elapsed = time.time() - start_time
    print(f"\nTotal time: {elapsed:.1f}s")

    # Count total files
    png_count = len([f for f in os.listdir(OUTPUT_DIR) if f.endswith(".png")])
    scad_count = len([f for f in os.listdir(OUTPUT_DIR) if f.endswith(".scad")])
    print(f"Generated: {png_count} PNGs, {scad_count} SCAD files")


if __name__ == "__main__":
    main()
