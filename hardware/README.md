# Hardware Design

## Device Variants

### 1. Portable WiFi+BLE (build first)
- ESP32-S3-MINI-1 with integrated PCB antenna
- SSD1351 1.5" color OLED (or SSD1327 grayscale alternative)
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

### Display: SSD1351 1.5" Color OLED (primary)

| Spec | Value |
|---|---|
| Driver IC | SSD1351 (Solomon Systech) |
| Size | 1.5 inches diagonal |
| Resolution | 128 x 128 pixels |
| Colors | 65K (RGB565, 16-bit) |
| Interface | SPI (4-wire) |
| Operating voltage | 3.3V logic, 7-15V OLED drive (onboard charge pump) |
| Active current | ~30 mA typical (content dependent) |
| Sleep current | ~1 uA (display off command) |
| Viewing angle | >160 degrees |

**Datasheet:** [SSD1351 datasheet](../docs/display/SSD1351_datasheet.pdf)

**Why this part:**
- Full color makes the Tamagotchi character expressive (red angry, green happy, etc.)
- 128x128 square resolution is ideal for character art
- OLED = true blacks, high contrast, no backlight power waste
- Compatible SPI pinout with SSD1327 modules — one PCB can support both (verify specific module breakout boards match before finalizing footprint)

### Display: SSD1327 1.5" Grayscale OLED (alternative)

| Spec | Value |
|---|---|
| Driver IC | SSD1327 (Solomon Systech) |
| Size | 1.5 inches diagonal |
| Resolution | 128 x 128 pixels |
| Colors | 16 grayscale levels |
| Interface | SPI or I2C |
| Active current | ~10 mA typical |
| Sleep current | ~1 uA |

**Datasheet:** [SSD1327 datasheet](../docs/display/SSD1327_datasheet.pdf)

**Why as alternative:**
- Same physical size and resolution as SSD1351
- ~20 mA less power draw — better battery life
- Cheaper ($4-6 vs $8-12)
- Firmware abstracts the display driver — same pixel art assets, auto-converted to grayscale

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
- Powers both the ESP32-S3 and the OLED display logic

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
| OLED SCLK | GPIO 12 | SPI clock |
| OLED MOSI | GPIO 11 | SPI data |
| OLED CS | GPIO 10 | Chip select |
| OLED DC | GPIO 9 | Data/command select |
| OLED RST | GPIO 8 | Reset |
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
| **Active** (WiFi on, display on) | ~80 mA | ~30 mA | ~110 mA | ~4.5 hours |
| **Idle** (WiFi connected, display dim) | ~20 mA | ~5 mA | ~25 mA | ~20 hours |
| **Light sleep** (WiFi off, display on) | ~2 mA | ~10 mA | ~12 mA | ~41 hours |
| **Deep sleep** (everything off) | ~8 uA | ~1 uA | ~10 uA | ~5.7 years |

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
- Display connects via FPC cable or pin header
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
