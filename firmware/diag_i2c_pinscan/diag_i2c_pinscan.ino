/*
 * Diagnostic - find an MPU6050 on ANY GPIO pair.
 * Tries every (SDA, SCL) combination of candidate pins and probes 0x68 / 0x69.
 * Use when the MPU6050 is not found on GPIO21/22.
 */
#include <Wire.h>

const int PINS[] = {21, 22, 2, 5, 13, 14, 15, 16, 17, 18, 19, 23, 25, 26, 27, 32, 33};
const int N = sizeof(PINS) / sizeof(PINS[0]);

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== I2C pin-pair scan for MPU6050 ===");
  int hits = 0;
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      if (i == j) continue;
      int sda = PINS[i], scl = PINS[j];
      Wire.end();
      if (!Wire.begin(sda, scl, 100000)) continue;
      Wire.setTimeOut(20);
      for (uint8_t addr : {(uint8_t)0x68, (uint8_t)0x69}) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
          hits++;
          Serial.printf("FOUND device at 0x%02X with SDA=GPIO%d SCL=GPIO%d\n", addr, sda, scl);
        }
      }
    }
  }
  Wire.end();
  Serial.printf("=== done: %d hit(s) ===\n", hits);
}

void loop() {}
