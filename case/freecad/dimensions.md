# Devotchi Case — Dimensions Reference

All dimensions in mm. Use this as a reference when recreating the design in Onshape/Fusion/etc.

## Case Outer Dimensions

| Dimension | Value | Notes |
|---|---|---|
| Width | 58.2 | Display (51.8) + walls (3.0) + tolerance (0.4) + clearance (3.0) |
| Height | 59.2 | Display zone (39.2) + button zone (15.0) + USB zone (5.0) |
| Depth | 15.1 | See depth stack below |
| Corner radius | 3.0 | XY plane, all 4 corners |
| Front chamfer | 2.5 | 45-degree slope on front face edges only |
| Back cover chamfer | 1.0 | 45-degree slope on back face edges |
| Wall thickness | 1.5 | All walls |

## Depth Stack (front to back, Z axis)

```
Front wall ──────── 1.5mm   Z = 0 to 1.5
Display ─────────── 2.5mm   Z = 1.5 to 4.0
Gap (ESP32) ─────── 2.5mm   Z = 4.0 to 6.5
PCB ─────────────── 1.6mm   Z = 6.5 to 8.1
Gap ─────────────── 0.5mm   Z = 8.1 to 8.6
Battery ─────────── 5.0mm   Z = 8.6 to 13.6
Back wall ────────── 1.5mm   Z = 13.6 to 15.1
```

## Front View Layout (Y axis, top to bottom)

```
┌──────────────────────────────────────┐ Y = 0
│           CASE TOP WALL (1.5)        │
│  ┌────────────────────────────────┐  │ Y = 1.5
│  │                                │  │
│  │   Display window (40.8 x 30.6)│  │
│  │   Centered in display zone     │  │
│  │                                │  │
│  └────────────────────────────────┘  │
│          Display zone: 39.2mm        │ Y = 39.2
│                                      │
│  <approve> <action>  <reject>        │ Y = 43.7 (btn centers)
│                                      │
│           <wake>                     │ Y = 51.2 (wake center)
│         Button zone: 15.0mm         │ Y = 54.2
│     (LED)     ═══USB═══             │
│          USB zone: 5.0mm             │ Y = 59.2
└──────────────────────────────────────┘
```

## Display

| Dimension | Value | Notes |
|---|---|---|
| Module size | 51.8 x 36.2 x 2.5 | HS20HS072RX |
| Active area | 40.8 x 30.6 | Window cutout size |
| FPC tail | 6.5 wide, 12.1 long | Exits LEFT edge, ~15.3mm from top |
| Pocket size | 55.2 x 36.2 | Full inner cavity width/height |
| Pocket depth | 3.0 | From Z=1.5 to Z=4.5 |
| Window bezel slope | 1.5mm margin, 0.8mm deep | Sloped surround on front face |

### Display Retention Clips (5 total)

Thin lips (1.5mm overhang, 1.0mm thick) at Z = 3.0 (back of display).

| Clip | Position | Size |
|---|---|---|
| Left top | X = pocket edge, Y = 1mm from top | 1.5 x 5.0 |
| Left bottom | X = pocket edge, Y = 20.3mm from top | 1.5 x 5.0 |
| Right top | X = pocket far edge, Y = 1mm from top | 1.5 x 5.0 |
| Right bottom | X = pocket far edge, Y = 20.3mm from top | 1.5 x 5.0 |
| Top | Centered on top edge | 8.0 x 1.5 |

Left clips split to avoid FPC exit zone (15.3mm from top, ~5mm clearance).

## PCB

| Dimension | Value | Notes |
|---|---|---|
| Size | 45.0 x 40.0 x 1.6 | Smaller than display |
| Position X | 6.6 | Centered: (58.2 - 45.0) / 2 |
| Position Y | 17.7 | Bottom-aligned: 59.2 - 1.5 - 40.0 |
| Position Z | 6.5 | Components face front (toward display) |

## Battery

| Dimension | Value | Notes |
|---|---|---|
| Size | 30.0 x 35.0 x 5.0 | Placeholder, 5mm thick LiPo |
| Position | Centered XY | Behind PCB |
| Position Z | 8.6 | Between PCB and back wall |

## Buttons (Front Face)

Elongated hexagon shape (`<=>`) with rounded corners.

### Main 3 Buttons (Approve, Action, Reject)

| Dimension | Value |
|---|---|
| Hex width (tip to tip) | 12.0 |
| Hex height (flat to flat) | 6.0 |
| Corner rounding | 1.2 |
| Spacing (center to center) | 14.0 |
| Y position (center) | 43.7 |
| X positions | 15.1, 29.1, 43.1 (centered) |
| Bezel slope margin | 0.5 |
| Bezel slope depth | 0.3 |

### Wake Button

| Dimension | Value |
|---|---|
| Hex width | 10.0 |
| Hex height | 4.5 |
| Corner rounding | 1.2 |
| Y position (center) | 51.2 |
| X position | 29.1 (centered) |

### Button Caps

| Dimension | Value | Notes |
|---|---|---|
| Cap clearance | 0.3 per side | Gap between cap and hole |
| Cap top thickness | 1.8 | Visible part |
| Cap protrusion | 0.8 | Above case surface |
| Retention lip | +0.8 per side | Wider than hole to retain |
| Lip thickness | 0.8 | Behind front wall |
| Actuator post | 3.0 dia, 2.0 tall | Presses tactile switch |

## USB-C Cutout (Bottom Edge)

| Dimension | Value | Notes |
|---|---|---|
| Connector | HRO TYPE-C-31-M-12 | |
| Hole width | 9.34 | 8.94 + 2 x 0.2 tolerance |
| Hole height | 3.66 | 3.26 + 2 x 0.2 tolerance |
| Shape | Pill (rounded rectangle) | |
| Position | Centered X, bottom wall | |
| Z center | 4.67 | Connector extends from PCB toward front |

## LED Hole

| Dimension | Value |
|---|---|
| Diameter | 2.0 |
| Position | X = 8.0, Y = 54.2 (bottom-left area) |

## Back Cover

| Dimension | Value | Notes |
|---|---|---|
| Plate thickness | 1.5 | Matches wall thickness |
| Outer chamfer | 1.0 | Rounded outer edge |
| Rim height | 2.0 | Inserts into front shell |
| Rim clearance | 0.3 | Gap for snap fit |
| Rim wall thickness | 1.0 | |

### Snap-Fit Clips (4 total on rim)

| Dimension | Value |
|---|---|
| Clip width | 8.0 |
| Clip height | 2.0 |
| Hook overhang | 0.5 |
| Positions | Left, Right (Y centered), Top, Bottom (X centered) |

### PCB Mounting Standoffs (4 on back cover)

| Dimension | Value | Notes |
|---|---|---|
| Outer diameter | 4.0 | |
| Screw hole | 2.2 (M2) | |
| Inset from PCB edge | 3.0 | |
| Height | 5.5 | From back inner wall to PCB solder side |
| Positions | 4 corners of PCB | |

### Battery Guide Rails (on back cover)

| Dimension | Value | Notes |
|---|---|---|
| Guide wall thickness | 1.2 | |
| Positions | Left, Right, Top, Bottom (split) | |
| Bottom split gap | 8.0 | For JST cable |
| Height | Back inner wall to Z_BATT | Connects to back wall |

### Motor Pocket (on back cover inner face)

| Dimension | Value |
|---|---|
| Diameter | 6.4 (motor 6.0 + tolerance) |
| Depth | 1.0 |
| Position | X = 25%, Y = 20% of case |

## FDM Print Notes

- Front shell prints face-down (chamfered front on bed)
- Back cover prints outer-face-down (flat back on bed, features up)
- Button caps print top-face-down (visible surface gets bed finish)
- Tolerance: 0.2mm per side for FDM
- No overhangs > 45 degrees
- Display clips are 1.5mm bridges (acceptable for FDM)
