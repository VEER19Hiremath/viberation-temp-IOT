/*
 * Phase 1b - Complete sensor validation sketch
 * Project: Edge-AI-Based Predictive Maintenance for Industrial Machines
 *
 * Board : ESP32 DevKit V1  (Arduino IDE board: "ESP32 Dev Module")
 *
 * Wiring:
 *   MPU6050  SDA->GPIO21  SCL->GPIO22  VCC->3V3  GND->GND   (I2C 0x68)
 *   SSD1306  SDA->GPIO21  SCL->GPIO22  VCC->3V3  GND->GND   (I2C 0x3C/0x3D)
 *   DS18B20  DATA->GPIO4 (4.7k pull-up to 3V3)  VCC->3V3  GND->GND
 *   ACS712   OUT->divider->GPIO34   VCC->5V   GND->GND
 *
 * Libraries (Library Manager):
 *   Adafruit SSD1306, Adafruit GFX Library, OneWire, DallasTemperature
 *   MPU6050 is accessed by direct register reads (no library) - see README.
 *
 * Serial Monitor: 115200 baud. Commands:
 *   z  -> re-measure ACS712 zero offset (MOTOR MUST BE OFF / no current)
 *   h  -> help
 *
 * Every 1 s the sketch prints per-sensor statistics computed over that second,
 * so you can validate both the values AND the achievable sampling rates.
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>

// ============================================================================
// 1. Component enable switches (set to 0 to test a subset independently)
// ============================================================================
#define ENABLE_OLED     1
#define ENABLE_MPU      1
#define ENABLE_DS18B20  1
#define ENABLE_ACS712   1

// ============================================================================
// 2. Pins
// ============================================================================
constexpr int PIN_SDA     = 21;
constexpr int PIN_SCL     = 22;
constexpr int PIN_ONEWIRE = 4;
constexpr int PIN_ACS     = 34;   // ADC1_CH6, input-only, works with Wi-Fi on

// ============================================================================
// 3. ACS712 calibration constants  --- YOU MUST SET THESE FROM YOUR HARDWARE ---
// ============================================================================
// (a) Sensitivity from the chip marking on your module (datasheet, at Vcc = 5.000 V):
//       ACS712ELCTR-05B -> 185 mV/A
//       ACS712ELCTR-20A -> 100 mV/A
//       ACS712ELCTR-30A ->  66 mV/A
constexpr float ACS_SENS_MV_PER_A_AT_5V = 185.0f;   // <-- CHANGE to match your chip

// (b) The ACS712 is ratiometric: zero point and sensitivity scale with its supply.
//     Measure the ESP32 "5V"/VIN pin with a multimeter and enter it here (volts).
constexpr float ACS_VCC_MEASURED_V = 5.00f;         // <-- CHANGE to your measured value

// (c) Divider ratio = V(GPIO34) / V(ACS712 OUT), measured with a multimeter
//     with no current flowing. Example: R_top=10k, R_bottom=20k -> 20/(10+20) = 0.6667.
//     Use the MEASURED ratio, not the nominal one (resistor tolerance + ADC load).
constexpr float DIVIDER_RATIO = 0.6667f;            // <-- CHANGE to your measured value

// (d) Optional gain correction after a known-current test (see README). Start at 1.0.
constexpr float ACS_GAIN_CORRECTION = 1.0f;

// Sampling
constexpr uint32_t ACS_PERIOD_US       = 1000;  // 1 kHz ADC sampling (20 samples per 50 Hz cycle)
constexpr int      ACS_ZERO_SAMPLES    = 4000;  // samples averaged for zero calibration

// ============================================================================
// 4. MPU6050 configuration (direct register access)
// ============================================================================
constexpr uint8_t MPU_ADDR          = 0x68;   // 0x69 if AD0 is tied HIGH
constexpr uint8_t REG_SMPLRT_DIV    = 0x19;
constexpr uint8_t REG_CONFIG        = 0x1A;
constexpr uint8_t REG_GYRO_CONFIG   = 0x1B;
constexpr uint8_t REG_ACCEL_CONFIG  = 0x1C;
constexpr uint8_t REG_ACCEL_XOUT_H  = 0x3B;
constexpr uint8_t REG_PWR_MGMT_1    = 0x6B;
constexpr uint8_t REG_WHO_AM_I      = 0x75;

// Accel full-scale +/-4 g  (ACCEL_CONFIG = 0x08) -> 8192 LSB/g
// Gyro  full-scale +/-500 dps (GYRO_CONFIG = 0x08) -> 65.5 LSB/(deg/s)
// DLPF_CFG = 1 -> accel bandwidth ~184 Hz, internal sample rate 1 kHz
constexpr uint8_t ACCEL_CFG_VAL     = 0x08;
constexpr uint8_t GYRO_CFG_VAL      = 0x08;
constexpr uint8_t DLPF_CFG_VAL      = 0x01;
constexpr float   ACCEL_LSB_PER_G   = 8192.0f;
constexpr float   GYRO_LSB_PER_DPS  = 65.5f;
constexpr uint32_t MPU_PERIOD_US    = 2000;   // 500 Hz polling for this validation test

// ============================================================================
// 5. Other settings
// ============================================================================
constexpr uint32_t I2C_CLOCK_HZ     = 400000;
constexpr uint32_t REPORT_PERIOD_MS = 1000;
constexpr uint8_t  DS_RESOLUTION    = 12;     // 0.0625 C steps, ~750 ms conversion

// ============================================================================
// Globals
// ============================================================================
// Keep the I2C clock at 400 kHz before AND after OLED transfers (library default
// drops it to 100 kHz after every display(), which would slow MPU reads).
Adafruit_SSD1306 oled(128, 64, &Wire, -1, I2C_CLOCK_HZ, I2C_CLOCK_HZ);
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature ds(&oneWire);
DeviceAddress dsAddr;

bool oledOk = false, mpuOk = false, dsOk = false, acsOk = false;
uint8_t oledAddr = 0, mpuWho = 0;

// Running statistics over one report window
struct Stats {
  double sum = 0, sumSq = 0;
  float minV = INFINITY, maxV = -INFINITY;
  uint32_t n = 0;
  void add(float v) {
    sum += v; sumSq += (double)v * v; n++;
    if (v < minV) minV = v;
    if (v > maxV) maxV = v;
  }
  float mean() const { return n ? (float)(sum / n) : NAN; }
  float stdev() const {
    if (n < 2) return NAN;
    double m = sum / n, var = sumSq / n - m * m;
    return var > 0 ? (float)sqrt(var) : 0.0f;
  }
  float rms() const { return n ? (float)sqrt(sumSq / n) : NAN; }
  void reset() { *this = Stats(); }
};

Stats sAx, sAy, sAz, sAmag, sGx, sGy, sGz, sMpuT;
Stats sAdcMv, sCurrent;

uint32_t tMpuUs = 0, tAcsUs = 0, tReportMs = 0, windowStartMs = 0;
uint32_t mpuReadErrors = 0;

float acsZeroSensorMv = NAN;  // ACS712 OUT voltage at zero current (sensor side, mV)
float acsZeroNoiseA   = NAN;  // 1-sigma noise at zero current, in amps
float acsSensMvPerA   = 0;    // effective sensitivity after Vcc scaling

bool     dsPending = false;
uint32_t dsRequestMs = 0, dsConvMs = 750;
float    dsLastC = NAN;
bool     dsFirstReading = true;
uint32_t dsErrors = 0;
const char* dsStatus = "init";

// ============================================================================
// I2C helpers
// ============================================================================
bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool i2cReadRegs(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// ============================================================================
// OLED
// ============================================================================
bool oledInit() {
  if (i2cPresent(0x3C)) oledAddr = 0x3C;
  else if (i2cPresent(0x3D)) oledAddr = 0x3D;
  else { Serial.println("[OLED] not found at 0x3C or 0x3D"); return false; }

  // periphBegin=false: Wire is already started on our pins
  if (!oled.begin(SSD1306_SWITCHCAPVCC, oledAddr, true, false)) {
    Serial.println("[OLED] begin() failed (buffer allocation)");
    return false;
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println("PdM Phase 1");
  oled.println("Sensor validation");
  oled.println("Booting...");
  oled.display();
  Serial.printf("[OLED] OK at 0x%02X\n", oledAddr);
  return true;
}

// ============================================================================
// MPU6050
// ============================================================================
bool mpuInit() {
  if (!i2cReadRegs(MPU_ADDR, REG_WHO_AM_I, &mpuWho, 1)) {
    Serial.printf("[MPU] no response at 0x%02X\n", MPU_ADDR);
    return false;
  }
  Serial.printf("[MPU] WHO_AM_I = 0x%02X %s\n", mpuWho,
                mpuWho == 0x68 ? "(MPU6050)" : "(variant/clone - continuing, verify readings)");

  i2cWriteReg(MPU_ADDR, REG_PWR_MGMT_1, 0x80);  // device reset
  delay(100);
  bool ok = true;
  ok &= i2cWriteReg(MPU_ADDR, REG_PWR_MGMT_1, 0x01);  // wake, clock = PLL (X gyro)
  delay(10);
  ok &= i2cWriteReg(MPU_ADDR, REG_SMPLRT_DIV, 0x00);  // 1 kHz output rate
  ok &= i2cWriteReg(MPU_ADDR, REG_CONFIG, DLPF_CFG_VAL);
  ok &= i2cWriteReg(MPU_ADDR, REG_GYRO_CONFIG, GYRO_CFG_VAL);
  ok &= i2cWriteReg(MPU_ADDR, REG_ACCEL_CONFIG, ACCEL_CFG_VAL);
  delay(50);

  uint8_t rb = 0;
  if (!ok || !i2cReadRegs(MPU_ADDR, REG_ACCEL_CONFIG, &rb, 1) || (rb & 0x18) != (ACCEL_CFG_VAL & 0x18)) {
    Serial.printf("[MPU] configuration write/readback failed (ACCEL_CONFIG=0x%02X)\n", rb);
    return false;
  }
  Serial.println("[MPU] OK: accel +/-4 g, gyro +/-500 dps, DLPF ~184 Hz, polled at 500 Hz");
  return true;
}

bool mpuRead(float& ax, float& ay, float& az, float& gx, float& gy, float& gz, float& tC) {
  uint8_t b[14];
  if (!i2cReadRegs(MPU_ADDR, REG_ACCEL_XOUT_H, b, 14)) return false;
  auto s16 = [&](int i) { return (int16_t)((b[i] << 8) | b[i + 1]); };
  ax = s16(0)  / ACCEL_LSB_PER_G;
  ay = s16(2)  / ACCEL_LSB_PER_G;
  az = s16(4)  / ACCEL_LSB_PER_G;
  tC = s16(6)  / 340.0f + 36.53f;   // MPU6050 die temperature (not machine temp)
  gx = s16(8)  / GYRO_LSB_PER_DPS;
  gy = s16(10) / GYRO_LSB_PER_DPS;
  gz = s16(12) / GYRO_LSB_PER_DPS;
  return true;
}

// ============================================================================
// DS18B20
// ============================================================================
bool dsInit() {
  // Check for the external 4.7k pull-up: with the weak internal pull-DOWN enabled,
  // the line only stays HIGH if a strong external pull-up is present.
  pinMode(PIN_ONEWIRE, INPUT_PULLDOWN);
  delay(5);
  bool extPullup = digitalRead(PIN_ONEWIRE) == HIGH;
  if (extPullup) {
    pinMode(PIN_ONEWIRE, INPUT);
    Serial.println("[DS18B20] external pull-up detected on DATA (good)");
  } else {
    // Fallback: ESP32 internal pull-up (~45k). Works for one sensor on a short cable,
    // but is out of spec for 1-Wire - fit a 4.7k resistor DATA<->3V3 for reliability.
    pinMode(PIN_ONEWIRE, INPUT_PULLUP);
    Serial.println("[DS18B20] WARNING: no external 4.7k pull-up found - using ESP32 internal pull-up (fallback)");
  }
  delay(5);
  ds.begin();
  int count = ds.getDeviceCount();
  Serial.printf("[DS18B20] devices found on GPIO%d: %d\n", PIN_ONEWIRE, count);
  if (count == 0 || !ds.getAddress(dsAddr, 0)) {
    Serial.println("[DS18B20] no sensor found - check DATA wire and 4.7k pull-up to 3V3");
    return false;
  }
  Serial.print("[DS18B20] ROM address: ");
  for (int i = 0; i < 8; i++) Serial.printf("%02X%s", dsAddr[i], i < 7 ? ":" : "\n");
  if (dsAddr[0] != 0x28) Serial.printf("[DS18B20] WARNING family code 0x%02X (genuine DS18B20 = 0x28)\n", dsAddr[0]);
  if (OneWire::crc8(dsAddr, 7) != dsAddr[7]) Serial.println("[DS18B20] WARNING ROM CRC mismatch - noisy bus");
  Serial.printf("[DS18B20] parasite power: %s\n", ds.isParasitePowerMode() ? "YES (VCC not connected?)" : "no (normal)");

  ds.setResolution(dsAddr, DS_RESOLUTION);
  ds.setWaitForConversion(false);  // non-blocking: don't stall vibration sampling
  dsConvMs = ds.millisToWaitForConversion(DS_RESOLUTION);
  Serial.printf("[DS18B20] OK: %d-bit, conversion time %lu ms\n", DS_RESOLUTION, (unsigned long)dsConvMs);
  return true;
}

void dsService() {
  if (!dsPending) {
    ds.requestTemperaturesByAddress(dsAddr);
    dsRequestMs = millis();
    dsPending = true;
    return;
  }
  if (millis() - dsRequestMs < dsConvMs + 10) return;
  dsPending = false;

  float t = ds.getTempC(dsAddr);
  if (t == DEVICE_DISCONNECTED_C) {
    dsErrors++;
    dsStatus = "DISCONNECTED";
    dsLastC = NAN;
  } else if (t == 85.0f && dsFirstReading) {
    // 85.0 C is the power-on reset value: conversion did not complete
    dsStatus = "85C reset value";
    dsErrors++;
  } else {
    dsLastC = t;
    dsStatus = "OK";
    dsFirstReading = false;
  }
}

// ============================================================================
// ACS712
// ============================================================================
float acsReadGpioMv() {
  // analogReadMilliVolts() applies the ESP32's factory (eFuse) ADC calibration,
  // which is far more accurate than converting raw counts with 3.3/4095.
  return (float)analogReadMilliVolts(PIN_ACS);
}

bool acsCalibrateZero() {
  Serial.println("[ACS712] Zero calibration: make sure NO current flows (motor off)...");
  Stats s;
  for (int i = 0; i < ACS_ZERO_SAMPLES; i++) {
    s.add(acsReadGpioMv());
    delayMicroseconds(250);
  }
  float gpioMean = s.mean(), gpioStd = s.stdev();
  acsZeroSensorMv = gpioMean / DIVIDER_RATIO;
  acsZeroNoiseA   = (gpioStd / DIVIDER_RATIO) / acsSensMvPerA;
  float expectedZeroMv = ACS_VCC_MEASURED_V * 1000.0f / 2.0f;

  Serial.printf("[ACS712] GPIO34: mean=%.1f mV  std=%.2f mV  min=%.0f  max=%.0f  (n=%lu)\n",
                gpioMean, gpioStd, s.minV, s.maxV, (unsigned long)s.n);
  Serial.printf("[ACS712] Sensor-side zero = %.1f mV (expected ~Vcc/2 = %.1f mV)\n",
                acsZeroSensorMv, expectedZeroMv);
  Serial.printf("[ACS712] Effective sensitivity = %.2f mV/A, zero-current noise (1 sigma, per sample) = %.3f A\n",
                acsSensMvPerA, acsZeroNoiseA);

  if (gpioMean < 100.0f) {
    Serial.println("[ACS712] ERROR: ~0 V on GPIO34. Check OUT wire, divider, ACS712 power (5V) and GND.");
    return false;
  }
  if (s.maxV > 3100.0f) {
    Serial.println("[ACS712] DANGER: GPIO34 above 3.1 V. Divider ratio too high - fix before applying load current!");
    return false;
  }
  if (fabsf(acsZeroSensorMv - expectedZeroMv) > 0.10f * expectedZeroMv) {
    Serial.println("[ACS712] WARNING: zero point >10% away from Vcc/2. Check DIVIDER_RATIO and ACS_VCC_MEASURED_V,");
    Serial.println("         and make sure no current was flowing during calibration.");
  }
  return true;
}

void acsSample() {
  float gpioMv   = acsReadGpioMv();
  float sensorMv = gpioMv / DIVIDER_RATIO;
  float amps     = (sensorMv - acsZeroSensorMv) / acsSensMvPerA * ACS_GAIN_CORRECTION;
  sAdcMv.add(gpioMv);
  sCurrent.add(amps);
}

// ============================================================================
// Reporting
// ============================================================================
void printHelp() {
  Serial.println("Commands: 'z' = re-zero ACS712 (no current!), 'h' = help");
}

void report() {
  float winS = (millis() - windowStartMs) / 1000.0f;
  uint32_t upS = millis() / 1000;

  Serial.printf("\n========== t = %lu s ==========\n", (unsigned long)upS);

  // ---- MPU6050 ----
  if (mpuOk) {
    Serial.printf("MPU6050  rate=%.0f Hz  read_errors=%lu\n", sAx.n / winS, (unsigned long)mpuReadErrors);
    Serial.printf("  accel mean [g]   x=%+.3f  y=%+.3f  z=%+.3f   |a|=%.3f\n",
                  sAx.mean(), sAy.mean(), sAz.mean(), sAmag.mean());
    Serial.printf("  accel std  [mg]  x=%.2f  y=%.2f  z=%.2f   (vibration / noise level)\n",
                  sAx.stdev() * 1000, sAy.stdev() * 1000, sAz.stdev() * 1000);
    Serial.printf("  accel p-p  [mg]  x=%.1f  y=%.1f  z=%.1f\n",
                  (sAx.maxV - sAx.minV) * 1000, (sAy.maxV - sAy.minV) * 1000, (sAz.maxV - sAz.minV) * 1000);
    Serial.printf("  gyro mean  [dps] x=%+.2f  y=%+.2f  z=%+.2f   die_temp=%.1f C\n",
                  sGx.mean(), sGy.mean(), sGz.mean(), sMpuT.mean());
  } else {
    Serial.println("MPU6050  DISABLED / NOT DETECTED");
  }

  // ---- DS18B20 ----
  if (dsOk) {
    Serial.printf("DS18B20  temp=%.4f C  status=%s  errors=%lu\n", dsLastC, dsStatus, (unsigned long)dsErrors);
  } else {
    Serial.println("DS18B20  DISABLED / NOT DETECTED");
  }

  // ---- ACS712 ----
  if (acsOk) {
    float iMean = sCurrent.mean();
    float iRms  = sCurrent.rms();     // true RMS about the calibrated zero (use for AC motors)
    float iAc   = sCurrent.stdev();   // AC component only (ripple)
    Serial.printf("ACS712   rate=%.0f Hz  GPIO34 mean=%.1f mV (sensor-side %.1f mV)  min=%.0f max=%.0f mV\n",
                  sAdcMv.n / winS, sAdcMv.mean(), sAdcMv.mean() / DIVIDER_RATIO, sAdcMv.minV, sAdcMv.maxV);
    Serial.printf("  I_dc(mean)=%+.3f A   I_rms=%.3f A   I_ac(std)=%.3f A   zero_noise=%.3f A\n",
                  iMean, iRms, iAc, acsZeroNoiseA);
    if (sAdcMv.maxV > 3100.0f) Serial.println("  WARNING: GPIO34 > 3.1 V (ADC non-linear / unsafe region)");
  } else {
    Serial.println("ACS712   DISABLED / CALIBRATION FAILED");
  }

  // ---- OLED ----
  if (oledOk) {
    oled.clearDisplay();
    oled.setCursor(0, 0);
    oled.printf("PdM P1        %5lus\n", (unsigned long)upS);
    if (mpuOk) {
      oled.printf("Ax%+.2f Ay%+.2f\n", sAx.mean(), sAy.mean());
      oled.printf("Az%+.2f |a|%.2f g\n", sAz.mean(), sAmag.mean());
      oled.printf("Vib std %.1f mg\n", sAmag.stdev() * 1000);
    } else {
      oled.println("MPU: --");
      oled.println();
      oled.println();
    }
    if (dsOk && !isnan(dsLastC)) oled.printf("Temp %.2f C\n", dsLastC);
    else oled.println("Temp: --");
    if (acsOk) oled.printf("Irms %.2fA dc%+.2f\n", sCurrent.rms(), sCurrent.mean());
    else oled.println("Current: --");
    oled.printf("M:%s D:%s A:%s", mpuOk ? "ok" : "--", dsOk ? "ok" : "--", acsOk ? "ok" : "--");
    oled.display();
  }

  // reset window
  sAx.reset(); sAy.reset(); sAz.reset(); sAmag.reset();
  sGx.reset(); sGy.reset(); sGz.reset(); sMpuT.reset();
  sAdcMv.reset(); sCurrent.reset();
  windowStartMs = millis();
}

// ============================================================================
// Setup / loop
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("=== Phase 1b: Sensor validation ===");

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_CLOCK_HZ);

#if ENABLE_OLED
  oledOk = oledInit();
#endif
#if ENABLE_MPU
  mpuOk = mpuInit();
#endif
#if ENABLE_DS18B20
  dsOk = dsInit();
#endif
#if ENABLE_ACS712
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_ACS, ADC_11db);  // ~0.15-2.45 V linear, usable to ~3.1 V
  acsSensMvPerA = ACS_SENS_MV_PER_A_AT_5V * (ACS_VCC_MEASURED_V / 5.0f);
  Serial.printf("[ACS712] config: sens=%.1f mV/A @5V, Vcc=%.3f V, divider=%.4f, gain_corr=%.4f\n",
                ACS_SENS_MV_PER_A_AT_5V, ACS_VCC_MEASURED_V, DIVIDER_RATIO, ACS_GAIN_CORRECTION);
  acsOk = acsCalibrateZero();
#endif

  Serial.printf("\nSummary: OLED=%s  MPU6050=%s  DS18B20=%s  ACS712=%s\n",
                oledOk ? "OK" : "FAIL/OFF", mpuOk ? "OK" : "FAIL/OFF",
                dsOk ? "OK" : "FAIL/OFF", acsOk ? "OK" : "FAIL/OFF");
  printHelp();

  tMpuUs = tAcsUs = micros();
  tReportMs = windowStartMs = millis();
}

void loop() {
  uint32_t nowUs = micros();

  // ---- MPU6050 @ 500 Hz ----
  if (mpuOk && (uint32_t)(nowUs - tMpuUs) >= MPU_PERIOD_US) {
    tMpuUs += MPU_PERIOD_US;
    if ((uint32_t)(nowUs - tMpuUs) > MPU_PERIOD_US) tMpuUs = nowUs;  // resync after a stall, no burst catch-up
    float ax, ay, az, gx, gy, gz, tC;
    if (mpuRead(ax, ay, az, gx, gy, gz, tC)) {
      sAx.add(ax); sAy.add(ay); sAz.add(az);
      sAmag.add(sqrtf(ax * ax + ay * ay + az * az));
      sGx.add(gx); sGy.add(gy); sGz.add(gz);
      sMpuT.add(tC);
    } else {
      mpuReadErrors++;
    }
  }

  // ---- ACS712 @ 1 kHz ----
  if (acsOk && (uint32_t)(nowUs - tAcsUs) >= ACS_PERIOD_US) {
    tAcsUs += ACS_PERIOD_US;
    if ((uint32_t)(nowUs - tAcsUs) > ACS_PERIOD_US) tAcsUs = nowUs;
    acsSample();
  }

  // ---- DS18B20 (non-blocking) ----
  if (dsOk) dsService();

  // ---- Report every second ----
  if (millis() - tReportMs >= REPORT_PERIOD_MS) {
    tReportMs = millis();
    report();
  }

  // ---- Serial commands ----
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'z' || c == 'Z') {
#if ENABLE_ACS712
      acsOk = acsCalibrateZero();
      sAdcMv.reset(); sCurrent.reset();
#endif
    } else if (c == 'h' || c == 'H') {
      printHelp();
    }
  }
}
