# viberation-temp-IOT

**Edge-AI-Based Predictive Maintenance for Industrial Machines.** This is a low-cost ESP32 research prototype. It monitors a motor's **vibration**, **temperature** and **current**, and the end goal is to detect abnormal operation **on the device itself**, with no cloud dependency.

> **Status: Phase 1 (hardware validation) is in progress.** See [Current status](#current-status).

---

## Hardware

| Component | Role | Interface |
|---|---|---|
| ESP32 DevKit V1 | Main controller; edge inference | — |
| MPU6050 | 3-axis accelerometer/gyroscope (vibration) | I2C `0x68` |
| DS18B20 (waterproof) | Motor housing temperature | 1-Wire |
| ACS712 (05B / 20A / 30A) | Motor current | Analog |
| SSD1306 OLED 128×64 | Local display | I2C `0x3C` |

### Wiring

| Component | Pin | ESP32 |
|---|---|---|
| MPU6050 | VCC / GND | 3V3 / GND |
| MPU6050 | SDA / SCL | GPIO 21 / GPIO 22 |
| OLED | VCC / GND | 3V3 / GND |
| OLED | SDA / SCL | GPIO 21 / GPIO 22 (shared I2C bus) |
| DS18B20 | VCC (red) / GND (black) | 3V3 / GND |
| DS18B20 | DATA (yellow) | GPIO 4, **plus a 4.7 kΩ pull-up resistor to 3V3** |
| ACS712 | VCC / GND | 5V (VIN) / GND |
| ACS712 | OUT | GPIO 34 **through a voltage divider** |

**ACS712 voltage divider (required).** The ACS712 runs from 5 V and its output can reach about 4.5 V. GPIO34 must stay below about 3.1 V.

```
ACS712 OUT ──[10 kΩ]──┬── GPIO34        ratio = 20/(10+20) = 0.667
                      └─[20 kΩ]── GND   4.5 V max → 3.0 V at the pin
```

> ⚠ If the motor runs on mains AC, the ACS712's high-current terminals carry mains voltage. For a first prototype, use a low-voltage DC motor or fan.

---

## Repository layout

```
firmware/
  phase1_i2c_scanner/    I2C bus check + device scan (WHO_AM_I identification)
  phase1_sensor_test/    All sensors + OLED, 1 s statistics, ACS712 zero calibration
  diag_onewire/          Finds DS18B20 on any pin, detects missing pull-up
  diag_i2c_pinscan/      Searches every GPIO pair for an MPU6050
docs/
  PHASE1_HARDWARE_VALIDATION.md   Full Phase 1 procedure, expected output, troubleshooting
setup.ps1                Toolchain install + build + upload + serial monitor (Windows)
```

---

## Setup

### Option A: script (Windows, PowerShell)

```powershell
git clone https://github.com/VEER19Hiremath/viberation-temp-IOT.git
cd viberation-temp-IOT

# Install arduino-cli, the ESP32 core and the libraries, then compile
.\setup.ps1

# Build, flash and open the Serial Monitor (port is auto-detected, or pass -Port COM3)
.\setup.ps1 -Upload -Monitor

# Another sketch
.\setup.ps1 -Sketch phase1_i2c_scanner -Upload -Monitor
```

If PowerShell blocks the script, run `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned` once.

### Option B: Arduino IDE

1. Boards Manager: install **esp32 by Espressif Systems** (tested with 3.3.12).
2. Board: **ESP32 Dev Module**. Serial Monitor: **115200 baud**.
3. Library Manager: install **Adafruit SSD1306**, **Adafruit GFX Library**, **OneWire** and **DallasTemperature**.
4. Open `firmware/phase1_i2c_scanner` first, then `firmware/phase1_sensor_test`.

| Dependency | Tested version |
|---|---|
| ESP32 Arduino core | 3.3.12 |
| Adafruit SSD1306 | 2.5.17 |
| Adafruit GFX Library | 1.13.0 |
| OneWire | 2.3.8 |
| DallasTemperature | 4.0.6 |

The MPU6050 is read through direct register access, not a library. Clone chips (WHO_AM_I 0x70, 0x72 or 0x98) are therefore supported, and the range, filter and sample rate are under full control.

---

## Calibration (before trusting current readings)

Edit section 3 of `firmware/phase1_sensor_test/phase1_sensor_test.ino` using **measured** values:

| Constant | How to obtain it |
|---|---|
| `ACS_SENS_MV_PER_A_AT_5V` | From the chip marking: 05B → 185, 20A → 100, 30A → 66 |
| `ACS_VCC_MEASURED_V` | Multimeter on the ESP32 VIN/5V pin |
| `DIVIDER_RATIO` | V(GPIO34) ÷ V(ACS712 OUT), measured with no current flowing |
| `ACS_GAIN_CORRECTION` | Reference current ÷ measured current, from a known-load test |

The zero-current offset is measured automatically at boot (motor **off**). To re-measure it, type `z` in the Serial Monitor.

---

## Current status

Results from the first hardware runs:

| Sensor | Status | Notes |
|---|---|---|
| OLED | ✅ Working | Detected at 0x3C |
| DS18B20 | ✅ Working (fallback) | No external 4.7 kΩ pull-up detected; the firmware uses the ESP32 internal pull-up. A 4.7 kΩ resistor still needs to be fitted. The reading (~41 °C) still needs checking against a reference |
| MPU6050 | ❌ Not detected | Not detected at 0x68/0x69 on any GPIO pair. Check power and header soldering, or replace the module |
| ACS712 | ⚠ Divider missing | GPIO34 reads ~2.53 V at zero current, which points to no divider. Do not apply load current until the divider is fitted |

---

## Roadmap

1. **Hardware validation** (in progress): detect, read and calibrate all sensors.
2. **Data collection:** labelled CSV recordings of the motor in normal and fault conditions, from independent sessions.
3. **Feature extraction:** sliding-window RMS, variance, peak, crest factor and FFT band energies; current and temperature features.
4. **Machine learning:** Decision Tree, Random Forest and Isolation Forest (scikit-learn), with session-wise splits to prevent data leakage.
5. **Edge deployment:** convert the selected model for on-device inference on the ESP32 (C code export or TFLite Micro).
6. **Evaluation:** threshold vs conventional ML vs edge model. Metrics: accuracy, precision, recall, F1, false-alarm rate, latency, RAM/CPU, energy and network traffic.

## Known limitations

- **ESP32 ADC:** noisy and non-linear near the ends of its range. An ADS1115 is the recommended upgrade.
- **ACS712 + 12-bit ADC:** coarse resolution for small motors. A measured zero-current noise of ≈0.075 A per sample (05B setting) is expected.
- **MPU6050:** usable vibration bandwidth is ≈200–400 Hz, which suits unbalance, misalignment and looseness but not high-frequency bearing faults. An ADXL345 or an IEPE accelerometer is the upgrade path.
- **DS18B20:** a slow thermal response that is good for trends, not transients.

See [docs/PHASE1_HARDWARE_VALIDATION.md](docs/PHASE1_HARDWARE_VALIDATION.md) for details.
