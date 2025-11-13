/*
 * This example shows how to read the energy data of a PZEM004Tv3
 *
 * The circuit:
 * - PZEM004Tv3 connected to Serial1 (RX=14, TX=27)
 *
 * Compile with -D MYCILA_JSON_SUPPORT to enable JSON support
 * Add ArduinoJson library to your project
 */
#include <Arduino.h>
#include <MycilaPZEM.h>

Mycila::PZEM pzem;
uint8_t address;

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;

  pzem.setCallback([](const Mycila::PZEM::EventType& event, const Mycila::PZEM::Data& data) {
    if (event == Mycila::PZEM::EventType::EVT_READ) {
      Serial.printf("PZEM Data Received: active power: %.2f W, voltage: %.2f V, current: %.2f A\n",
                    data.activePower,
                    data.voltage,
                    data.current);
    }
  });

  pzem.begin(Serial1, 14, 27);
  // pzem.begin(Serial1, 14, 27, 0x01);
  // pzem.begin(Serial1, 14, 27, 0x02);

  address = pzem.getDeviceAddress();
}

void loop() {
  if (!pzem.read()) {
    Serial.printf("Failed to read PZEM at address 0x%02X\n", address);
  }
  delay(1000);
}
