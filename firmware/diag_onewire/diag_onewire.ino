/*
 * Diagnostic - DS18B20 / 1-Wire bus finder
 *
 * For each candidate GPIO:
 *   1. Pull-up test: enable the ESP32's weak internal pull-DOWN (~45k). If the pin
 *      still reads HIGH, a strong external pull-up (e.g. 4.7k to 3V3) is present.
 *   2. 1-Wire reset: checks for a device "presence pulse" and searches ROM codes.
 * Finally retries GPIO4 using the ESP32 internal pull-up (~45k) as a fallback.
 */
#include <OneWire.h>

const int PINS[] = {4, 2, 5, 13, 14, 15, 16, 17, 18, 19, 23, 25, 26, 27, 32, 33};

bool externalPullup(int pin) {
  pinMode(pin, INPUT_PULLDOWN);
  delay(5);
  int a = digitalRead(pin);
  pinMode(pin, INPUT);
  return a == HIGH;
}

int searchPin(int pin, bool internalPullup) {
  OneWire ow(pin);
  if (internalPullup) pinMode(pin, INPUT_PULLUP);
  delay(5);
  uint8_t present = ow.reset();
  int count = 0;
  uint8_t addr[8];
  ow.reset_search();
  while (ow.search(addr)) {
    count++;
    Serial.printf("    ROM: ");
    for (int i = 0; i < 8; i++) Serial.printf("%02X%s", addr[i], i < 7 ? ":" : "");
    Serial.printf("  family=0x%02X%s  crc=%s\n", addr[0], addr[0] == 0x28 ? " (DS18B20)" : "",
                  OneWire::crc8(addr, 7) == addr[7] ? "ok" : "BAD");
  }
  Serial.printf("    presence pulse=%s, devices=%d\n", present ? "YES" : "no", count);
  return count;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== 1-Wire diagnostic ===");
  for (int pin : PINS) {
    bool pu = externalPullup(pin);
    Serial.printf("GPIO%-2d external pull-up: %s\n", pin, pu ? "YES" : "no");
    if (pu || pin == 4) searchPin(pin, false);
  }
  Serial.println("\nRetry GPIO4 with ESP32 internal pull-up (~45k):");
  searchPin(4, true);
  Serial.println("=== done ===");
}

void loop() {}
