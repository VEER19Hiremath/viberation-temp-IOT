# Phase 1: Hardware Validation

Goal: show that every sensor is detected, reads physically sensible values and is calibrated before any data is collected. Nothing in Phase 2 counts until this checklist passes.

```
firmware/
  phase1_i2c_scanner/phase1_i2c_scanner.ino   <- step 1: bus check + I2C scan
  phase1_sensor_test/phase1_sensor_test.ino   <- step 2: all sensors + OLED
```

---

## 0. Arduino IDE setup

| Item | Value |
|---|---|
| Board package | **esp32 by Espressif Systems** (Boards Manager), v2.0.x or v3.x |
| Board | **ESP32 Dev Module** |
| Upload speed | 921600 (drop to 115200 if uploads fail) |
| Serial Monitor | **115200 baud**, line ending: "No line ending" |

Install these libraries from **Library Manager**:

| Library | Author | Used for |
|---|---|---|
| Adafruit SSD1306 | Adafruit | OLED driver |
| Adafruit GFX Library | Adafruit | OLED text/graphics (installed as a dependency) |
| OneWire | Paul Stoffregen | 1-Wire bus for DS18B20 |
| DallasTemperature | Miles Burton | DS18B20 driver |

`Wire` (I2C) is part of the ESP32 core.

**Why no MPU6050 library?** Many cheap GY-521 boards carry clone or variant chips whose WHO_AM_I value is 0x70, 0x72 or 0x98 instead of 0x68. Some popular libraries refuse to start on those chips. Reading the registers directly avoids that problem. It also gives exact control over the range, the digital low-pass filter and the sample rate, and Phase 2/3 vibration sampling depends on those settings.

---

## 1. Power-off checks (multimeter), before anything else

Do these with the ESP32 on USB, **the motor disconnected**, and the multimeter in DC volts.

| # | Measure | Between | Expect | Why |
|---|---|---|---|---|
| 1 | 3V3 rail | 3V3 pin ↔ GND | 3.2–3.4 V | MPU, OLED and DS18B20 supply |
| 2 | 5V rail | VIN/5V pin ↔ GND | ~4.6–5.1 V (USB) | **Write this down → `ACS_VCC_MEASURED_V`** |
| 3 | ACS712 OUT (before divider) | OUT ↔ GND | ≈ half of #2 | Zero-current output is Vcc/2 |
| 4 | GPIO34 (after divider) | GPIO34 ↔ GND | must be **< 3.1 V**; typically 1.5–1.8 V | ADC safety |
| 5 | **Divider ratio** | #4 ÷ #3 | e.g. ≈0.667 for 10k/20k | **Write this down → `DIVIDER_RATIO`** |

**Worst-case divider check:** the ACS712 output can swing up to about Vcc−0.5 V (≈4.5 V) at full-scale current. Multiply that by your ratio. The result must stay under ~3.1 V: the ESP32's safe input limit is 3.3 V, and the ADC is non-linear above ~3.1 V at 11 dB attenuation.
- 10k / 20k → 4.5 × 0.667 = **3.0 V** ✔
- 10k / 10k → 4.5 × 0.5 = 2.25 V ✔ (safer, but you lose some resolution)

**ACS712 variant:** read the chip marking on the module, `ACS712ELCTR-05B`, `-20A` or `-30A`, and set `ACS_SENS_MV_PER_A_AT_5V` to 185, 100 or 66 to match.

> ⚠ **Safety:** if your motor runs on **mains AC**, the ACS712's high-current terminals carry mains voltage. Use an enclosed load, keep the ESP32 side physically separated, and never touch the module while it is powered. For a first prototype, a **low-voltage DC motor (6–24 V)** or a small low-voltage AC fan is strongly recommended.

---

## 2. Step 1: I2C scanner

Upload `phase1_i2c_scanner.ino` and open Serial Monitor.

**Expected output** (format; your WHO_AM_I value may differ):
```
=== Phase 1a: I2C bus check and scanner ===
Idle line check: SDA(GPIO21)=HIGH (ok)  SCL(GPIO22)=HIGH (ok)
I2C started on SDA=21 SCL=22 at 100000 Hz

Scanning...
  Found 0x3C : SSD1306 OLED (default address)
  Found 0x68 : MPU6050 (AD0 = LOW / unconnected)
    WHO_AM_I = 0x68 -> genuine MPU6050 / MPU6000 register map
Scan done: 2 device(s) found.
  OLED   : DETECTED
  MPU6050: DETECTED
```

**Isolation test:** unplug the OLED's VCC and rescan; only 0x68 should remain. Reconnect it, unplug the MPU's VCC, and rescan; only 0x3C should remain. This proves each device answers on its own and that neither one is holding the bus.

---

## 3. Step 2: Full sensor test

1. Open `phase1_sensor_test.ino` and edit **section 3** with your measured `ACS_SENS_MV_PER_A_AT_5V`, `ACS_VCC_MEASURED_V` and `DIVIDER_RATIO`.
2. **Make sure no current flows through the ACS712** (motor off). The zero offset is measured at boot.
3. Upload it and open Serial Monitor at 115200.

To test one sensor at a time, set the other `ENABLE_*` switches at the top of the sketch to `0`.

### Expected Serial Monitor output

The **format** you should see is shown below. The numbers are only illustrative ranges, **not real measurements**. Your own measured values are what go into the research record.

```
=== Phase 1b: Sensor validation ===
[OLED] OK at 0x3C
[MPU] WHO_AM_I = 0x68 (MPU6050)
[MPU] OK: accel +/-4 g, gyro +/-500 dps, DLPF ~184 Hz, polled at 500 Hz
[DS18B20] devices found on GPIO4: 1
[DS18B20] ROM address: 28:xx:xx:xx:xx:xx:xx:xx
[DS18B20] parasite power: no (normal)
[DS18B20] OK: 12-bit, conversion time 750 ms
[ACS712] config: sens=185.0 mV/A @5V, Vcc=4.950 V, divider=0.6650, gain_corr=1.0000
[ACS712] Zero calibration: make sure NO current flows (motor off)...
[ACS712] GPIO34: mean=16xx.x mV  std=x.xx mV  min=... max=...  (n=4000)
[ACS712] Sensor-side zero = 24xx.x mV (expected ~Vcc/2 = 2475.0 mV)
[ACS712] Effective sensitivity = 183.15 mV/A, zero-current noise (1 sigma, per sample) = 0.0xx A

Summary: OLED=OK  MPU6050=OK  DS18B20=OK  ACS712=OK
Commands: 'z' = re-zero ACS712 (no current!), 'h' = help

========== t = 5 s ==========
MPU6050  rate=4xx Hz  read_errors=0
  accel mean [g]   x=+0.0xx  y=-0.0xx  z=+0.9xx   |a|=0.9xx..1.0xx
  accel std  [mg]  x=a few  y=a few  z=a few   (vibration / noise level)
  accel p-p  [mg]  x=...  y=...  z=...
  gyro mean  [dps] x=+/-few  y=+/-few  z=+/-few   die_temp=xx.x C
DS18B20  temp=2x.xxxx C  status=OK  errors=0
ACS712   rate=9xx Hz  GPIO34 mean=16xx.x mV (sensor-side 24xx.x mV)  min=... max=... mV
  I_dc(mean)=+0.0xx A   I_rms=0.0xx A   I_ac(std)=0.0xx A   zero_noise=0.0xx A
```

The OLED shows the same values in compact form, refreshed once per second.

Rates come out slightly below 500/1000 Hz because each OLED refresh blocks the loop for about 25 ms. That is acceptable here. Phase 2 will use a dedicated sampling task.

---

## 4. Validation tests (do all of these and record results)

### 4.1 MPU6050 accelerometer
| Test | Procedure | Pass criterion |
|---|---|---|
| Gravity magnitude | Board flat, still, 10 s | `|a|` ≈ 1.00 g (±0.05) |
| 6-face test | Rest the board on each of its 6 faces | The axis pointing up reads ≈ +1 g, pointing down ≈ −1 g, the others ≈ 0. Record all 6 → used later for offset/scale correction |
| Noise floor | Board still on a solid table | Record `accel std` for x/y/z (expected: a few mg) |
| Response | Tap the table near the sensor | `std` and `p-p` jump clearly above the noise floor |
| Gyro | Rotate the board slowly about each axis | The matching gyro axis changes sign with direction; record the at-rest bias |

### 4.2 DS18B20
| Test | Procedure | Pass criterion |
|---|---|---|
| Room temperature | Compare with a reference thermometer | Within ±0.5 °C (datasheet accuracy) |
| Response | Hold the probe in your hand | Rises smoothly toward ~30–34 °C |
| Ice-point (best reference) | Probe in crushed ice + water, stirred, 2 min | ≈ 0.0 °C ±0.5 → record the offset |
| Stability | 5 min untouched | `errors=0`, no −127 or 85 values |

### 4.3 ACS712
| Test | Procedure | Pass criterion |
|---|---|---|
| Zero | Motor off, reset the board | Sensor-side zero within 10% of Vcc/2; record `zero_noise` |
| Direction | DC load on, then swap the load wires | `I_dc` flips sign |
| **Gain calibration** | Put a multimeter in series (DC) or a clamp meter (AC) as reference. Run 2–3 load levels | `ACS_GAIN_CORRECTION = I_reference / I_measured` (average over levels), then re-test |
| Re-zero | Type `z` with the motor off | Zero is reproducible within the noise |

Use `I_dc` for DC motors and `I_rms` for AC motors.

### 4.4 OLED
Every line updates once per second, the status line reads `M:ok D:ok A:ok`, and the text has no corruption.

---

## 5. Troubleshooting

| Symptom | Likely cause → fix |
|---|---|
| Scanner finds **nothing** | Check VCC/GND to both modules; check that SDA→21 and SCL→22 are not swapped; check the board selection |
| Idle line check reports **LOW** | Missing pull-ups, a short, or an **unpowered** module clamping the bus. Disconnect modules one at a time |
| Only one I2C device found | Loose jumper on that module; verify with the isolation test |
| OLED at 0x3D | Handled automatically. Some modules use 0x3D |
| OLED detected but blank | Some 1.3" "OLED" modules use the **SH1106** controller, not SSD1306. Check the listing, and if so tell me and I'll switch the driver |
| MPU WHO_AM_I ≠ 0x68 | Clone/variant chip. Continue, but the 6-face test is mandatory |
| `|a|` far from 1 g / random values | Wrong full-scale range or a bad module; also try powering the GY-521 VCC from **5V** (it has an on-board 3.3 V regulator, and 3.3 V input leaves it little headroom) |
| MPU `read_errors` increasing | Long or loose I2C wires. Keep wires < 20 cm and away from motor power leads, or lower `I2C_CLOCK_HZ` to 100000 |
| DS18B20 count = 0 | Missing 4.7 kΩ pull-up (DATA↔3V3) or DATA on the wrong pin |
| DS18B20 −127 °C | Intermittent connection or no pull-up |
| DS18B20 stuck at 85 °C | Conversion not completing: power problem or parasite mode → connect the red wire to 3V3 |
| ACS712 GPIO34 ≈ 0 mV | OUT not connected, divider bottom resistor shorted, or ACS712 unpowered |
| ACS712 zero far from Vcc/2 | Wrong `DIVIDER_RATIO` / `ACS_VCC_MEASURED_V`, or current was flowing during boot |
| Current noisy at zero load | Normal for the ACS712 + ESP32 ADC; see limitations below. Twist the OUT/GND wires, add a 0.1 µF capacitor from GPIO34 to GND |
| Readings jump when motor starts | Motor EMI or ground bounce. Keep the motor supply separate, use a common GND only at one point, and add a flyback diode across a DC motor |

---

## 6. Known hardware limitations (to cite in the paper)

1. **ESP32 ADC:** non-linear at the ends of its range, a few mV of noise per sample, and unit-to-unit gain error. `analogReadMilliVolts()` applies the factory eFuse calibration, but the gain-calibration test is still needed. An ADS1115 (16-bit I2C ADC) is the standard low-cost upgrade.
2. **ACS712 resolution:** with a 30 A module (66 mV/A) and a 2/3 divider, 1 A ≈ 44 mV at the ADC. Small motors drawing < 1 A will sit close to the noise floor. Use the **05B (185 mV/A)** variant for small motors, or a non-invasive clamp CT (SCT-013) for AC.
3. **MPU6050 for vibration:** it is a consumer MEMS sensor with an accel output rate of up to 1 kHz and a DLPF bandwidth of ≤ 260 Hz, so it captures vibration content up to roughly 200–400 Hz. That is enough for unbalance, misalignment and looseness at motor-speed harmonics, but not for high-frequency bearing defects. An ADXL345 at 3.2 kHz or an industrial IEPE accelerometer is the upgrade path. Mounting matters as much as the sensor: **bolt or epoxy it rigidly to the motor housing**, never tape it.
4. **DS18B20:** slow (750 ms conversion, plus a thermal time constant of seconds in the steel probe). Fine for temperature trends, useless for transients. Clamp it to the motor housing with thermal paste.
5. **Shared I2C bus:** OLED refreshes block MPU sampling for ~25 ms. In Phase 2 sampling moves to a FreeRTOS task and the display updates less often.

---

## 7. Report back (Phase 1 results)

Please send:

1. The full I2C scanner output (including the WHO_AM_I value).
2. Your ACS712 variant and the measured 5V, ACS712 OUT, GPIO34 and divider-ratio values.
3. The full boot log of the sensor test, plus 3–4 consecutive report blocks at rest.
4. MPU6050 6-face test: x/y/z means for each face.
5. DS18B20: room-temperature vs reference reading, and the ice-point reading if done.
6. ACS712: zero noise, the sign-flip result, and reference vs measured current at 2–3 load levels.
7. The motor you will monitor: AC or DC, rated voltage/current/speed, and how you plan to create fault conditions.

Item 7 decides the Phase 2 sampling rates and the fault-labelling plan.
