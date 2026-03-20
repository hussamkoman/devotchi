# Hardware Design

## Device Variants

### 1. Portable WiFi+BLE (build first)
- ESP32-S3-MINI-1 with integrated PCB antenna
- HS20HS072RX 2.0" color TFT LCD (ST7789, 320x240, IPS)
- LiPo battery, USB-C charging
- 4 buttons (3 front + 1 side)
- Status LED, vibration motor
- Connects to broker via WiFi, or to phone via BLE

### 2. Portable Cellular (build second)
- Same form factor as WiFi+BLE version
- Additional cellular modem (MCU TBD — may use same ESP32-S3 + SIM module, or different MCU)
- SIM/eSIM support (provider TBD)
- Works standalone anywhere with cell coverage

### 3. Desktop (build third)
- USB-powered (no battery needed)
- Potentially larger display
- Same ESP32-S3-MINI-1, connects via USB serial or WiFi
- PCB designed alongside portable version for combined ordering

---

## Component Bill of Materials (Portable WiFi+BLE)

### MCU: ESP32-S3-MINI-1

| Spec | Value |
|---|---|
| Manufacturer | Espressif |
| Part number | ESP32-S3-MINI-1-N8 (8MB flash, no PSRAM) |
| CPU | Dual-core Xtensa LX7 @ 240 MHz |
| SRAM | 512 KB |
| WiFi | 802.11 b/g/n, 2.4 GHz |
| Bluetooth | BLE 5.0 |
| Antenna | Integrated PCB antenna |
| Module dimensions | 15.4 x 20.5 x 2.4 mm |
| Operating voltage | 3.0 - 3.6V |
| Deep sleep current | ~8 uA (module level) |
| Interface highlights | SPI, I2C, I2S, UART, USB OTG, PWM, ADC |

**Datasheet:** [ESP32-S3-MINI-1 datasheet](../docs/mcu/ESP32-S3-MINI-1_datasheet.pdf)
**SoC datasheet:** [ESP32-S3 SoC datasheet](../docs/mcu/ESP32-S3_SoC_datasheet.pdf)

**Why this part:**
- Integrated PCB antenna — no external antenna or custom antenna design needed
- Small module footprint (15.4 x 20.5 mm)
- Dual-core allows running WiFi/BLE stack on one core and application on the other
- I2S peripheral enables future voice command features (microphone input)
- Massive community and library ecosystem
- USB OTG for the desktop variant (serial communication without UART bridge)

### Display: HS20HS072RX 2.0" Color TFT LCD

| Spec | Value |
|---|---|
| Manufacturer | HS (Shenzhen Hansheng) |
| Part number | HS20HS072RX |
| LCSC | C5329582 |
| Driver IC | ST7789T3 |
| Size | 2.0 inches diagonal |
| Resolution | 320 x 240 pixels (QVGA) |
| Colors | 262K (RGB666) |
| Pixel pitch | 0.151 x 0.153 mm |
| Active area | 40.80 x 30.60 mm |
| Module size | 51.80 x 36.20 mm |
| Interface | SPI (4-wire) |
| Operating voltage | IOVCC 1.65-3.3V, VCI 2.4-3.3V |
| Backlight | 4 white LEDs in parallel, 80mA max |
| Backlight forward voltage | 3.0V typical (2.8-3.2V) |
| Sleep current | ~30 uA (SLPIN command) |
| Viewing angle | ALL directions (IPS) |
| Contrast ratio | 300:1 typical |
| Connector | 12-pin FPC, 0.5mm pitch |
| Connection type | COG + FPC |

**Datasheet:** [HS20HS072RX datasheet](../docs/display/HS20HS072RX_datasheet.pdf)

**Source:** [LCSC/JLCPCB C5329582](https://www.lcsc.com/product-detail/lcd-screen_hs-hs20hs072rx_C5329582.html) — $3.70 (1+), $2.49 (100+), 1687 in stock.

**Why this part:**
- Full RGB color — expressive Tamagotchi character (red angry, green happy, etc.)
- 320x240 resolution — excellent detail for pixel art and text
- IPS viewing angle — looks good from any direction (critical for handheld)
- Runs entirely on 3.3V — no boost converter needed (unlike OLED panels needing 12V VCC)
- ST7789 has first-class ESP-IDF support via built-in `esp_lcd` driver
- $2.49 at volume with 1600+ stock — cheap and available
- 12-pin FPC is simple to wire (vs 30-pin for OLED panels)
- Backlight can be PWM-dimmed via MOSFET for power saving

**FPC pinout:**

| Pin | Symbol | Description |
|---|---|---|
| 1 | GND | Power ground |
| 2 | CS | Chip select (active low) |
| 3 | RS (DC) | Data/command select |
| 4 | SCL | SPI clock |
| 5 | SDA | SPI data (MOSI) |
| 6 | RST | Reset (active low) |
| 7 | NC | No connect |
| 8 | IOVCC | I/O logic supply (3.3V) |
| 9 | VCC | Panel power supply (3.3V) |
| 10 | A | Backlight LED anode |
| 11 | K | Backlight LED cathode |
| 12 | GND | Power ground |

### FPC Connector: AFC01-S12FCA-00

| Spec | Value |
|---|---|
| Manufacturer | JUSHUO |
| Part number | AFC01-S12FCA-00 |
| LCSC | C262661 |
| Pins | 12 |
| Pitch | 0.5mm |
| Contact type | Bottom contact |
| Locking | Hinged lid (flip-lock) |
| Height | 2mm above board |
| FPC thickness | 0.3mm |
| Price | $0.09 (1+), $0.07 (50+) |
| Stock | 76,000+ |

### Backlight Dimming Circuit

The display has no built-in backlight controller. Backlight is driven from battery voltage (VBAT, 3.7-4.2V) through a current-limiting resistor and switched by an N-channel MOSFET controlled by ESP32 PWM.

**Why VBAT instead of 3.3V:** The backlight LEDs have Vf = 3.0V typical. Running from 3.3V gives only 0.3V headroom — poor current regulation. Running from VBAT (~3.7-4.2V) gives 0.7-1.2V headroom for reliable regulation.

**Components:**

| Ref | Part | Value | Purpose |
|---|---|---|---|
| Q_BL | AO3400A (C20917) | N-ch MOSFET, SOT-23 | Switches backlight cathode, PWM dimming |
| R_BL | Resistor | 12Ω, 0603 | Current limiting (~80mA at VBAT=3.96V) |

**MOSFET: AO3400A**

| Spec | Value |
|---|---|
| LCSC | C20917 |
| Library | Basic (no assembly fee) |
| Vgs(th) | 1.45V (fully on at 3.3V GPIO) |
| Vds | 30V |
| Id | 5.7A |
| Rds(on) | 48mΩ @ Vgs=2.5V |
| Package | SOT-23 |
| Price | $0.08 |
| Stock | 2,000,000+ |

**Circuit:**
```
VBAT ─── R_BL (12Ω) ─── FPC Pin 10 (LED Anode)
                          FPC Pin 11 (LED Cathode) ─── Q_BL Drain
                                                       Q_BL Source ─── GND
                                                       Q_BL Gate ─── ESP32 PWM GPIO
```

**Resistor calculation:**
- VBAT nominal = 3.7V, Vf = 3.0V, I = 80mA
- R = (3.7 - 3.0) / 0.08 = 8.75Ω → use 12Ω for safety margin
- At 4.2V (full charge): I = (4.2 - 3.0) / 12 = 100mA (within 80mA abs max only briefly, PWM duty < 80% at full charge)
- At 3.7V (nominal): I = (3.7 - 3.0) / 12 = 58mA (comfortable)
- At 3.4V (low battery): I = (3.4 - 3.0) / 12 = 33mA (dimmer but still visible)

**Firmware notes:**
- PWM frequency: 1kHz+ (no visible flicker)
- Sleep: send ST7789 SLPIN command (0x10), then set PWM duty to 0
- Wake: set PWM duty > 0, then send SLPOUT command (0x11), wait 120ms

### Firmware Graphics Library

**Recommended: ESP-IDF native `esp_lcd` + LovyanGFX**

- `esp_lcd` provides the built-in ST7789 panel driver for SPI initialization
- LovyanGFX provides sprite-based graphics, text rendering, and animations
- LovyanGFX supports ESP-IDF natively (not Arduino-only)
- Supports DMA transfers for background rendering
- ST7789 sleep commands (SLPIN/SLPOUT) supported for power management
- Alternative: LVGL if more complex UI widgets are needed later

### Battery Protection: DW01A + FS8205A

| Spec | Value |
|---|---|
| Protection IC | DW01A (PUOLOP / Fortune Semiconductor) |
| Package (DW01A) | SOT-23-6 |
| MOSFET | FS8205A dual N-channel MOSFET |
| Package (FS8205A) | SOT-23-6 |
| Over-discharge cutoff | ~2.4V |
| Over-charge cutoff | ~4.3V |
| Short circuit protection | Yes, via CS pin current sense |
| Rds(on) (FS8205A) | ~25 mΩ per MOSFET |

**Datasheets:**
- [DW01A datasheet](../docs/power/DW01A_datasheet.pdf)
- [FS8205A datasheet](../docs/power/FS8205A_datasheet.pdf)

**Why these parts:**
- Standard LiPo protection pair — used on virtually every small battery board
- DW01A monitors voltage and current, FS8205A switches the ground path
- Protects against over-discharge (battery drains to 0V = dead cell), over-charge, and short circuit
- Very low cost (~$0.10 total) and tiny footprint (two SOT-23-6 packages)

**Design notes:**
- DW01A VCC connects to battery positive via 100Ω resistor (inrush limiting)
- 100nF cap between VCC and GND
- FS8205A sits in the battery ground path — DW01A controls its gates (OD/OC pins)
- CS pin connects to FS8205A source via 1kΩ resistor (current sense for short circuit detection)
- In normal operation, MOSFETs are ON and battery GND = system GND
- During fault, MOSFETs turn OFF, disconnecting the battery

### Charger: TP4056

| Spec | Value |
|---|---|
| Type | Linear single-cell LiPo charger |
| Package | SOP-8 / ESOP-8 |
| Input | USB-C, 4V - 8V |
| Charge current | Up to 1A (configurable via resistor) |
| Charge voltage | 4.2V (constant voltage) |
| Protection | DW01A + 8205A MOSFETs on module (over-charge, over-discharge, short circuit) |
| Charge termination | Current drops below threshold |
| Status outputs | 2 pins (charging / standby) |

**Datasheet:** [TP4056 datasheet](../docs/power/TP4056_datasheet.pdf)

**Design notes:**
- Set charge current via PROG resistor (R_prog). For a small LiPo (300-500mAh), use 2.4k-4k ohm for 250-500mA charge rate.
- Connect STDBY and CHRG pins to ESP32 GPIOs for software battery status reading.
- USB-C connector wired with CC1/CC2 pull-down resistors (5.1k each) for proper USB-C detection.

### Regulator: ME6211 3.3V LDO

| Spec | Value |
|---|---|
| Type | CMOS LDO voltage regulator |
| Package | SOT-23-5 |
| Output voltage | 3.3V (fixed) |
| Output current | 500 mA max |
| Dropout voltage | 100 mV @ 100mA |
| Quiescent current | 40 uA typical |
| Input voltage | 2.0 - 6.0V |

**Datasheet:** [ME6211 datasheet](../docs/power/ME6211_datasheet.pdf)

**Design notes:**
- Input from LiPo battery (3.0 - 4.2V) → output 3.3V
- 1uF input cap + 1uF output cap minimum
- Powers both the ESP32-S3 and the TFT display logic (IOVCC + VCC)

**Known concern — dropout at low battery:**
The ME6211 has 100mV dropout, meaning it needs ~3.4V input to regulate 3.3V. LiPo cells drop to 3.0-3.2V before cutoff. Between 3.0-3.4V the output will sag below 3.3V. The ESP32-S3 operates down to 3.0V, so it will still function but may be out of spec. Options to address this:
- Accept it — the device will work slightly below spec at very low battery, and the DW01A cuts off at ~2.4V anyway
- Use a buck-boost converter instead (e.g., TPS63001) — converts 1.8-5.5V to 3.3V but more complex and expensive
- For MVP, the LDO is fine — flag low battery in firmware and prompt charging before regulation drops

### Buttons

| Button | Position | Function |
|---|---|---|
| Approve (green) | Front left | Accept approval/permission requests |
| Reject (red) | Front right | Reject approval/permission requests |
| Action | Front center | Context-dependent: scroll, open menu, poke pet |
| Wake/Power | Side | Wake from deep sleep, long-press for power off |

All buttons are standard 6x6mm tactile switches with 10k pull-up resistors (or use ESP32 internal pull-ups). Buttons connect to ground when pressed.

### Feedback

| Component | Purpose | GPIO |
|---|---|---|
| Status LED (RGB or single color) | Connection status, charging, notifications | 1 PWM pin (or 3 for RGB) |
| Vibration motor (coin type, 3V) | Haptic alert for approval requests | 1 GPIO via MOSFET |

---

## Pin Assignment (preliminary)

These are preliminary and will be finalized during schematic design.

**Strapping pins to avoid:** GPIO0 (boot mode), GPIO3 (JTAG source, floating), GPIO45 (VDD_SPI), GPIO46 (boot mode). These are sampled at reset and should not have external loads that could affect their default state during boot.

| Function | ESP32-S3 GPIO | Notes |
|---|---|---|
| **SPI Display** | | |
| LCD SCLK | GPIO 12 | SPI clock |
| LCD MOSI | GPIO 11 | SPI data |
| LCD CS | GPIO 10 | Chip select |
| LCD DC | GPIO 9 | Data/command select |
| LCD RST | GPIO 8 | Reset |
| LCD Backlight | GPIO 13 | PWM via MOSFET to backlight cathode |
| **Buttons** | | |
| Approve | GPIO 4 | Pull-up, active low |
| Reject | GPIO 5 | Pull-up, active low |
| Action | GPIO 6 | Pull-up, active low |
| Wake/Power | GPIO 7 | RTC-capable for deep sleep wake |
| **Feedback** | | |
| LED | GPIO 38 | PWM for brightness control |
| Vibration motor | GPIO 37 | Via N-channel MOSFET |
| **Power status** | | |
| TP4056 CHRG | GPIO 18 | Low = charging |
| TP4056 STDBY | GPIO 2 | Low = charge complete |
| Battery ADC | GPIO 1 | Via voltage divider for battery level |
| **Future** | | |
| I2S BCLK | GPIO 15 | For microphone (voice commands) |
| I2S WS | GPIO 16 | For microphone |
| I2S DIN | GPIO 17 | For microphone |

---

## Power Budget (estimated)

| State | ESP32-S3 | Display | Total | Battery life (500mAh) |
|---|---|---|---|---|
| **Active** (WiFi on, display on, backlight 100%) | ~80 mA | ~60 mA | ~140 mA | ~3.6 hours |
| **Idle** (WiFi connected, backlight 25%) | ~20 mA | ~15 mA | ~35 mA | ~14 hours |
| **Light sleep** (WiFi off, backlight off, LCD sleep) | ~2 mA | ~0.03 mA | ~2 mA | ~250 hours |
| **Deep sleep** (everything off) | ~8 uA | ~30 uA | ~38 uA | ~1.5 years |

Real-world usage will be a mix — display on for a few seconds when events arrive, then back to sleep. Expected practical battery life: **2-5 days** with typical AI coding session activity.

---

## Power Management Strategy

1. **Normal operation**: WiFi connected, display off. ESP32 in light sleep between events.
2. **Event arrives** (via WiFi): Wake up, turn on display, show status/approval, vibrate if needed.
3. **Approval request**: Display stays on with countdown timer until user responds or timeout.
4. **No activity for N minutes**: Enter deep sleep. Wake on button press (side button is RTC-wake capable).
5. **Deep sleep**: Everything off. Battery lasts weeks/months.
6. **Display auto-off**: Display turns off after 30 seconds of no new events (configurable).

---

## PCB Design Notes

- **Single PCB** — power management and logic on one board
- Keep power section (TP4056, ME6211, USB-C, battery connector) on one side
- Keep logic section (ESP32, display connector, buttons) on the other side
- Ground plane between sections
- Follow ESP32-S3-MINI-1 datasheet antenna keepout zone (no copper/ground under antenna area)
- Display connects via 12-pin 0.5mm FPC cable (AFC01-S12FCA-00 connector)
- USB-C connector: use 16-pin mid-mount type with data lines (D+/D-) connected to ESP32-S3 USB OTG pins for firmware updates and desktop serial communication
- Battery connector: JST-PH 2-pin (standard for small LiPo)
- Test points for: 3.3V, VBAT, GND, UART TX/RX

---

## Enclosure

- 3D printed (material TBD: PLA, ABS, or PETG)
- Size determined after PCB layout and LiPo selection
- Rough estimate: ~50 x 40 x 20mm (similar to original Tamagotchi)
- Cutouts for: display window, 4 buttons, USB-C port, LED light pipe
- Optional clip or lanyard hole for portability
