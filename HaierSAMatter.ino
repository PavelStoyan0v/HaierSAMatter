// Copyright 2025 Espressif Systems (Shanghai) PTE LTD
#include <Matter.h>
#include <Preferences.h>
#include <OneButton.h>

#include <MatterEndpoints/MatterTemperatureSensor.h>
#include <MatterEndpoints/MatterContactSensor.h>

#include "Config.h"
#include "Endpoints.h"
#include "StatusLED.h"

// Managed Instances
Preferences matterPref;
StatusIndicator statusLED(1, NEOPIXEL_PIN);
OneButton button(BUTTON_PIN, true);

// Matter Endpoints
MatterNumericEndpoint<double> targetTemp;
MatterNumericEndpoint<uint8_t> deviceState;
MatterNumericEndpoint<uint8_t> deviceMode;

// Sensors
MatterTemperatureSensor inletTemp;
MatterTemperatureSensor outletTemp;
MatterContactSensor pumpStatus;

void setup() {
  Serial.begin(115200);
  statusLED.begin();
  matterPref.begin(PREF_NAMESPACE, false);

  // --- Button & Decommissioning ---
  button.setPressMs(5000);
  button.attachLongPressStop([]() {
    Serial.println("Decommissioning device...");
    Matter.decommission();
  });

  // --- Target Temperature ---
  double storedTemp = (double)matterPref.getInt(KEY_TARGET_TEMP, (int)(TEMP_DEFAULT * 100)) / 100.0;
  if (storedTemp < TEMP_MIN || storedTemp > TEMP_MAX) storedTemp = TEMP_DEFAULT;
  
  targetTemp.begin(storedTemp, TEMP_MIN, TEMP_MAX, TEMP_STEP);
  targetTemp.onChange([](double tempC) {
    Serial.printf("Target Temperature: %.1fC\r\n", tempC);
    matterPref.putInt(KEY_TARGET_TEMP, (int)(tempC * 100));
  });

  // --- State (0:OFF, 1:HEAT, 2:COOL) ---
  uint8_t storedState = matterPref.getUChar(KEY_STATE, 0);
  deviceState.begin((double)(storedState > 2 ? 0 : storedState), 0.0, 2.0, 1.0);
  deviceState.onChange([](uint8_t state) {
    const char *names[] = {"OFF", "HEAT", "COOL"};
    Serial.printf("State: %d (%s)\r\n", state, state < 3 ? names[state] : "??");
    if (state < 3) matterPref.putUChar(KEY_STATE, state);
  });

  // --- Mode (0:QUIET, 1:ECO, 2:TURBO) ---
  uint8_t storedMode = matterPref.getUChar(KEY_MODE, 0);
  deviceMode.begin((double)(storedMode > 2 ? 0 : storedMode), 0.0, 2.0, 1.0);
  deviceMode.onChange([](uint8_t mode) {
    const char *names[] = {"QUIET", "ECO", "TURBO"};
    Serial.printf("Mode: %d (%s)\r\n", mode, mode < 3 ? names[mode] : "??");
    if (mode < 3) matterPref.putUChar(KEY_MODE, mode);
  });

  // --- Sensors ---
  inletTemp.begin(20.0);  // Initial dummy value
  outletTemp.begin(25.0); // Initial dummy value
  pumpStatus.begin(false); // Initial state: Off

  // --- Device Identification ---
  setDeviceIdentification(VENDOR_NAME, DEVICE_NAME);

  Matter.begin();
  if (Matter.isDeviceCommissioned()) {
    Serial.println("Matter Node Commissioned.");
  }
}

void loop() {
  button.tick();
  statusLED.update();

  if (!Matter.isDeviceCommissioned()) {
    static uint32_t lastLog = 0;
    if (millis() - lastLog > 5000) {
      Serial.printf("Waiting for commissioning... Pairing Code: %s\r\n", Matter.getManualPairingCode().c_str());
      lastLog = millis();
    }
  }
}
