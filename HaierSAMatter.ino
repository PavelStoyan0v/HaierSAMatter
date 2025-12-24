// Copyright 2025 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Matter Manager
#include <Matter.h>
#if !CONFIG_ENABLE_CHIPOBLE
// if the device can be commissioned using BLE, WiFi is not used - save flash space
#include <WiFi.h>
#endif
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <MatterEndpoints/MatterTemperatureControlledCabinet.h>

// List of Matter Endpoints for this Node
// Color Light Endpoint
MatterColorLight ColorLight;

// CONFIG_ENABLE_CHIPOBLE is enabled when BLE is used to commission the Matter Network
#if !CONFIG_ENABLE_CHIPOBLE
// WiFi is manually set and started
const char *ssid = "YOUR_SSID";          // Change this to your WiFi SSID
const char *password = "YOUR_PASSWORD";  // Change this to your WiFi password
#endif

// it will keep last OnOff & HSV Color state stored, using Preferences
Preferences matterPref;
const char *onOffPrefKey = "OnOff";
const char *hsvColorPrefKey = "HSV";
const char *targetTempPrefKey = "TargetTemp";

// set your board RGB LED pin here
// ESP32-C6 onboard NeoPixel (WS2812) is wired to GPIO8
constexpr uint8_t neoPixelPin = 8;
constexpr uint8_t neoPixelCount = 1;
Adafruit_NeoPixel onboardPixel(neoPixelCount, neoPixelPin, NEO_GRB + NEO_KHZ800);

// Matter temperature endpoint (acts as a numeric control surfaced to Home Assistant)
class TargetTemperatureEndpoint : public MatterTemperatureControlledCabinet {
public:
  using TargetTempCB = std::function<void(double)>;

  void onTargetTemperatureChange(TargetTempCB cb) { onChangeCb = cb; }

  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    bool ret = MatterTemperatureControlledCabinet::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
    if (cluster_id == chip::app::Clusters::TemperatureControl::Id &&
        attribute_id == chip::app::Clusters::TemperatureControl::Attributes::TemperatureSetpoint::Id &&
        onChangeCb) {
      // TemperatureSetpoint is encoded as 1/100th of a degree Celsius
      onChangeCb(static_cast<double>(val->val.i16) / 100.0);
    }
    return ret;
  }

private:
  TargetTempCB onChangeCb = nullptr;
};

TargetTemperatureEndpoint TargetTempControl;
constexpr double targetTempMinC = 5.0;
constexpr double targetTempMaxC = 60.0;
constexpr double targetTempStepC = 0.5;
constexpr double targetTempDefaultC = 22.0;

// set your board USER BUTTON pin here
const uint8_t buttonPin = BOOT_PIN;  // Set your pin here. Using BOOT Button.

// Button control
uint32_t button_time_stamp = 0;                // debouncing control
bool button_state = false;                     // false = released | true = pressed
const uint32_t debouceTime = 250;              // button debouncing time (ms)
const uint32_t decommissioningTimeout = 5000;  // keep the button pressed for 5s, or longer, to decommission

// Set the RGB LED Light based on the current state of the Color Light
bool setLightState(bool state, espHsvColor_t colorHSV) {

  espRgbColor_t rgbColor = espHsvColorToRgbColor(colorHSV);
  if (state) {
    onboardPixel.setPixelColor(0, onboardPixel.Color(rgbColor.r, rgbColor.g, rgbColor.b));
  } else {
    onboardPixel.setPixelColor(0, 0);
  }
  onboardPixel.show();
  // store last HSV Color and OnOff state for when the Light is restarted / power goes off
  matterPref.putBool(onOffPrefKey, state);
  matterPref.putUInt(hsvColorPrefKey, colorHSV.h << 16 | colorHSV.s << 8 | colorHSV.v);
  // This callback must return the success state to Matter core
  return true;
}

void setup() {
  // Initialize the USER BUTTON (Boot button) GPIO that will act as a toggle switch
  pinMode(buttonPin, INPUT_PULLUP);
  // Initialize the onboard NeoPixel and Matter End Point
  onboardPixel.begin();
  onboardPixel.clear();
  onboardPixel.show();

  Serial.begin(115200);

// CONFIG_ENABLE_CHIPOBLE is enabled when BLE is used to commission the Matter Network
#if !CONFIG_ENABLE_CHIPOBLE
  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  // Manually connect to WiFi
  WiFi.begin(ssid, password);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\r\nWiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
#endif

  // Initialize Matter EndPoint
  matterPref.begin("MatterPrefs", false);
  // default OnOff state is ON if not stored before
  bool lastOnOffState = matterPref.getBool(onOffPrefKey, true);
  // default HSV color is blue HSV(169, 254, 254)
  uint32_t prefHsvColor = matterPref.getUInt(hsvColorPrefKey, 169 << 16 | 254 << 8 | 254);
  espHsvColor_t lastHsvColor = {uint8_t(prefHsvColor >> 16), uint8_t(prefHsvColor >> 8), uint8_t(prefHsvColor)};
  ColorLight.begin(lastOnOffState, lastHsvColor);
  // set the callback function to handle the Light state change
  ColorLight.onChange(setLightState);

  // Initialize Target Temperature endpoint (visible in Home Assistant as a Number)
  double storedTargetTemp = (double)matterPref.getInt(targetTempPrefKey, (int)(targetTempDefaultC * 100)) / 100.0;
  // clamp stored value within allowed range
  if (storedTargetTemp < targetTempMinC || storedTargetTemp > targetTempMaxC) {
    storedTargetTemp = targetTempDefaultC;
  }
  TargetTempControl.begin(storedTargetTemp, targetTempMinC, targetTempMaxC, targetTempStepC);
  TargetTempControl.onTargetTemperatureChange([](double tempC) {
    Serial.printf("Target Temperature changed to %.1fC\r\n", tempC);
    matterPref.putInt(targetTempPrefKey, (int)(tempC * 100));  // persist in 1/100 C
    // TODO: react to new target temperature here (drive heaters/relays/etc.)
  });

  // lambda functions are used to set the attribute change callbacks
  ColorLight.onChangeOnOff([](bool state) {
    Serial.printf("Light OnOff changed to %s\r\n", state ? "ON" : "OFF");
    return true;
  });
  ColorLight.onChangeColorHSV([](HsvColor_t hsvColor) {
    Serial.printf("Light HSV Color changed to (%d,%d,%d)\r\n", hsvColor.h, hsvColor.s, hsvColor.v);
    return true;
  });

  // Matter beginning - Last step, after all EndPoints are initialized
  Matter.begin();
  // This may be a restart of a already commissioned Matter accessory
  if (Matter.isDeviceCommissioned()) {
    Serial.println("Matter Node is commissioned and connected to the network. Ready for use.");
    Serial.printf(
      "Initial state: %s | RGB Color: (%d,%d,%d) \r\n", ColorLight ? "ON" : "OFF", ColorLight.getColorRGB().r, ColorLight.getColorRGB().g,
      ColorLight.getColorRGB().b
    );
    // configure the Light based on initial on-off state and its color
    ColorLight.updateAccessory();
  }
}

void loop() {
  // Check Matter Light Commissioning state, which may change during execution of loop()
  if (!Matter.isDeviceCommissioned()) {
    Serial.println("");
    Serial.println("Matter Node is not commissioned yet.");
    Serial.println("Initiate the device discovery in your Matter environment.");
    Serial.println("Commission it to your Matter hub with the manual pairing code or QR code");
    Serial.printf("Manual pairing code: %s\r\n", Matter.getManualPairingCode().c_str());
    Serial.printf("QR code URL: %s\r\n", Matter.getOnboardingQRCodeUrl().c_str());
    // waits for Matter Light Commissioning.
    uint32_t timeCount = 0;
    while (!Matter.isDeviceCommissioned()) {
      delay(100);
      if ((timeCount++ % 50) == 0) {  // 50*100ms = 5 sec
        Serial.println("Matter Node not commissioned yet. Waiting for commissioning.");
      }
    }
    Serial.printf(
      "Initial state: %s | RGB Color: (%d,%d,%d) \r\n", ColorLight ? "ON" : "OFF", ColorLight.getColorRGB().r, ColorLight.getColorRGB().g,
      ColorLight.getColorRGB().b
    );
    // configure the Light based on initial on-off state and its color
    ColorLight.updateAccessory();
    Serial.println("Matter Node is commissioned and connected to the network. Ready for use.");
  }

  // A button is also used to control the light
  // Check if the button has been pressed
  if (digitalRead(buttonPin) == LOW && !button_state) {
    // deals with button debouncing
    button_time_stamp = millis();  // record the time while the button is pressed.
    button_state = true;           // pressed.
  }

  // Onboard User Button is used as a Light toggle switch or to decommission it
  uint32_t time_diff = millis() - button_time_stamp;
  if (digitalRead(buttonPin) == HIGH && button_state && time_diff > debouceTime) {
    // Toggle button is released - toggle the light
    Serial.println("User button released. Toggling Light!");
    ColorLight.toggle();   // Matter Controller also can see the change
    button_state = false;  // released
  }

  // Onboard User Button is kept pressed for longer than 5 seconds in order to decommission matter node
  if (button_state && time_diff > decommissioningTimeout) {
    Serial.println("Decommissioning the Light Matter Accessory. It shall be commissioned again.");
    ColorLight = false;  // turn the light off
    Matter.decommission();
    button_time_stamp = millis();  // avoid running decommissining again, reboot takes a second or so
  }
}
