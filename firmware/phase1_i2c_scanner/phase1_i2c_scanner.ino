/*
 * Phase 1a - I2C bus check + scanner
 * Project: Edge-AI-Based Predictive Maintenance for Industrial Machines
 *
 * Board : ESP32 DevKit V1 ("ESP32 Dev Module" in Arduino IDE)
 * Bus   : SDA = GPIO21, SCL = GPIO22 (shared by MPU6050 and SSD1306 OLED)
 *
 * What it does:
 *   1. Before starting I2C, checks that SDA and SCL idle HIGH (pull-ups present,
 *      no short to GND, no device holding the bus).
 *   2. Scans addresses 0x01..0x7E every 5 s and names known devices.
 *   3. If an MPU-family chip answers at 0x68/0x69, reads WHO_AM_I to identify
 *      genuine MPU6050 vs. clone/variant chips.
 *
 * No external libraries needed (Wire is part of the ESP32 core).
 */

#include <Wire.h>

constexpr int PIN_SDA = 21;
constexpr int PIN_SCL = 22;
constexpr uint32_t I2C_CLOCK_HZ = 100000;  // 100 kHz for scanning (most tolerant)

void checkIdleLines() {
  // With the I2C peripheral not started, both lines should be pulled HIGH
  // by the pull-up resistors on the MPU6050 / OLED breakout boards.
  pinMode(PIN_SDA, INPUT);
  pinMode(PIN_SCL, INPUT);
  delay(5);
  int sda = digitalRead(PIN_SDA);
  int scl = digitalRead(PIN_SCL);
  Serial.printf("Idle line check: SDA(GPIO%d)=%s  SCL(GPIO%d)=%s\n",
                PIN_SDA, sda ? "HIGH (ok)" : "LOW (PROBLEM)",
                PIN_SCL, scl ? "HIGH (ok)" : "LOW (PROBLEM)");
  if (!sda || !scl) {
    Serial.println("  -> A LOW idle line means: missing pull-ups, line shorted to GND,");
    Serial.println("     a module not powered (unpowered chips can clamp the bus), or SDA/SCL swapped onto a wrong pin.");
  }
}

const char* nameFor(uint8_t addr) {
  switch (addr) {
    case 0x3C: return "SSD1306 OLED (default address)";
    case 0x3D: return "SSD1306 OLED (alternate address)";
    case 0x68: return "MPU6050 (AD0 = LOW / unconnected)";
    case 0x69: return "MPU6050 (AD0 = HIGH)";
    default:   return "unknown device";
  }
}

void identifyMpu(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(0x75);  // WHO_AM_I register
  if (Wire.endTransmission(false) != 0) {
    Serial.println("    WHO_AM_I: write failed");
    return;
  }
  if (Wire.requestFrom((int)addr, 1) != 1) {
    Serial.println("    WHO_AM_I: read failed");
    return;
  }
  uint8_t who = Wire.read();
  Serial.printf("    WHO_AM_I = 0x%02X -> ", who);
  switch (who) {
    case 0x68: Serial.println("genuine MPU6050 / MPU6000 register map"); break;
    case 0x70: Serial.println("MPU6500 (compatible accel/gyro registers)"); break;
    case 0x71: Serial.println("MPU9250 (compatible accel/gyro registers)"); break;
    case 0x72: Serial.println("clone reporting 0x72 (usually register-compatible)"); break;
    case 0x98: Serial.println("clone reporting 0x98 (usually register-compatible)"); break;
    default:   Serial.println("unexpected value - note it and report back"); break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("=== Phase 1a: I2C bus check and scanner ===");
  checkIdleLines();

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_CLOCK_HZ);
  Serial.printf("I2C started on SDA=%d SCL=%d at %lu Hz\n\n", PIN_SDA, PIN_SCL, (unsigned long)I2C_CLOCK_HZ);
}

void loop() {
  Serial.println("Scanning...");
  int found = 0;
  bool sawOled = false, sawMpu = false;

  for (uint8_t addr = 0x01; addr < 0x7F; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      found++;
      Serial.printf("  Found 0x%02X : %s\n", addr, nameFor(addr));
      if (addr == 0x3C || addr == 0x3D) sawOled = true;
      if (addr == 0x68 || addr == 0x69) { sawMpu = true; identifyMpu(addr); }
    } else if (err == 4) {
      Serial.printf("  Unknown bus error at 0x%02X\n", addr);
    }
  }

  Serial.printf("Scan done: %d device(s) found.\n", found);
  Serial.printf("  OLED   : %s\n", sawOled ? "DETECTED" : "NOT FOUND");
  Serial.printf("  MPU6050: %s\n", sawMpu ? "DETECTED" : "NOT FOUND");
  if (found == 0) {
    Serial.println("  -> Nothing answered. Check 3.3V/GND to both modules, SDA->21, SCL->22, and that the board is 'ESP32 Dev Module'.");
  }
  Serial.println("------------------------------------------------");
  delay(5000);
}
