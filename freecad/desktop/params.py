"""Desktop Devotchi — Hardware Parameters.

All dimensions in millimeters. Single source of truth for all 6 case designs.
"""

# ── Screen: 5.0" TFT Touchscreen (SSD1963, 800x480) ──
SCREEN_MOD_W = 120.0    # Module width
SCREEN_MOD_H = 75.0     # Module height
SCREEN_MOD_D = 4.5      # Module depth (PCB + glass)
SCREEN_ACT_W = 108.0    # Active area width
SCREEN_ACT_H = 64.8     # Active area height
SCREEN_BEZEL_L = (SCREEN_MOD_W - SCREEN_ACT_W) / 2  # ~6mm per side
SCREEN_BEZEL_T = (SCREEN_MOD_H - SCREEN_ACT_H) / 2  # ~5.1mm per side

# ── MCU: ESP32-S3-MINI-1-N4R2 ──
ESP32_W = 15.4
ESP32_H = 20.5
ESP32_D = 2.4

# ── Custom PCB (sized to match 5" screen) ──
PCB_W = 110.0
PCB_H = 65.0
PCB_D = 1.6

# ── Speaker (optional — 28mm coin speaker) ──
SPEAKER_DIA = 28.0
SPEAKER_D = 3.5

# ── USB-C Connector ──
USBC_W = 9.34   # 8.94 + 2*0.2 tolerance
USBC_H = 3.66   # 3.26 + 2*0.2 tolerance
USBC_R = 0.8    # Corner rounding

# ── Buttons: 3x tactile switches ──
BTN_DIA = 6.0       # Tactile switch footprint diameter
BTN_HOLE_DIA = 8.0  # Hole in case (with clearance)
BTN_SPACING = 14.0  # Center-to-center spacing
BTN_CAP_H = 2.0     # Button cap height above surface

# ── Print Tolerances ──
TOL = 0.2        # Tolerance per side for FDM
WALL = 2.0       # Wall thickness (desktop = sturdier)
WALL_THIN = 1.5  # Thinner walls where needed

# ── Mounting Hardware ──
STANDOFF_OD = 5.0    # Standoff outer diameter
STANDOFF_ID = 2.2    # M2 screw hole
STANDOFF_INSET = 4.0 # Distance from PCB edge to standoff center

CLIP_OVERHANG = 1.5  # Display retention clip overhang
CLIP_THICKNESS = 1.0 # Clip material thickness

SCREW_BOSS_OD = 6.0  # Screw boss outer diameter
SCREW_BOSS_ID = 2.5  # M2.5 screw hole

# ── Derived: Depth stack (minimum, front-to-back) ──
# Front wall + screen + gap (ESP32) + PCB + back wall
GAP_SCREEN_PCB = 3.0  # ESP32 module (2.4mm) + clearance
GAP_PCB_BACK = 1.0    # Clearance behind PCB
MIN_DEPTH = WALL + SCREEN_MOD_D + GAP_SCREEN_PCB + PCB_D + GAP_PCB_BACK + WALL
# = 2.0 + 4.5 + 3.0 + 1.6 + 1.0 + 2.0 = 14.1mm

# Z positions (from front face = 0)
Z_SCREEN = WALL
Z_PCB = WALL + SCREEN_MOD_D + GAP_SCREEN_PCB
Z_BEHIND_PCB = Z_PCB + PCB_D
