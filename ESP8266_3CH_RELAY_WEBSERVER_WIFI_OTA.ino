#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>       // https://github.com/tzapu/WiFiManager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "SinricPro.h"
#include "SinricProSwitch.h"
#include <map>

// ------------ Sinric Pro Credentials ------------
#define APP_KEY     "d0ff3d0f-cfa4-4458-940e-033091f5ae3d"
#define APP_SECRET  "dad2976a-1dd4-4d06-82b6-00e2e5fb0b6a-a286b2b4-b7b0-413c-96ab-e228121caf96"

// ------------ Device IDs ------------
#define device_ID_1   "63538953134b2df11cd2b1bc"
#define device_ID_2   "635388b7134b2df11cd2b0e5"
#define device_ID_3   "635f9c56b8a7fefbd62ca08a"
#define device_ID_4   "SWITCH_ID_NO_4_HERE"

// ------------ Relay Pins ------------
#define RelayPin1 5    // D1
#define RelayPin2 4    // D2
#define RelayPin3 14   // D5
#define RelayPin4 12   // D6

// ------------ Manual Switch Pins ------------
#define SwitchPin1 10   // SD3
#define SwitchPin2 0    // D3 
#define SwitchPin3 13   // D7
#define SwitchPin4 3    // RX

// ------------ WiFi + LED ------------
#define wifiLed   16   // D0

// ------------ Optional Reset Button ------------
#define resetPin 2     // D4 → GND press for 5 sec = Reset WiFi config

#define BAUD_RATE   9600
#define DEBOUNCE_TIME 250

// ------------ Device Map ------------
typedef struct {
  int relayPIN;
  int flipSwitchPIN;
} deviceConfig_t;

std::map<String, deviceConfig_t> devices = {
    {device_ID_1, { RelayPin1, SwitchPin1 }},
    {device_ID_2, { RelayPin2, SwitchPin2 }},
    {device_ID_3, { RelayPin3, SwitchPin3 }},
    {device_ID_4, { RelayPin4, SwitchPin4 }}     
};

typedef struct {
  String deviceId;
  bool lastFlipSwitchState;
  unsigned long lastFlipSwitchChange;
} flipSwitchConfig_t;

std::map<int, flipSwitchConfig_t> flipSwitches;

// ------------ Relay Setup ------------
void setupRelays() { 
  for (auto &device : devices) {
    pinMode(device.second.relayPIN, OUTPUT);
    digitalWrite(device.second.relayPIN, HIGH); // active LOW relay
  }
}

// ------------ Switch Setup ------------
void setupFlipSwitches() {
  for (auto &device : devices)  {
    flipSwitchConfig_t flipSwitchConfig;
    flipSwitchConfig.deviceId = device.first;
    flipSwitchConfig.lastFlipSwitchChange = 0;
    flipSwitchConfig.lastFlipSwitchState = true;
    int flipSwitchPIN = device.second.flipSwitchPIN;
    flipSwitches[flipSwitchPIN] = flipSwitchConfig;
    pinMode(flipSwitchPIN, INPUT_PULLUP);
  }
}

// ------------ SinricPro Callback ------------
bool onPowerState(String deviceId, bool &state) {
  int relayPIN = devices[deviceId].relayPIN;
  digitalWrite(relayPIN, !state);  // active LOW relay
  Serial.printf("Device %s => %s\n", deviceId.c_str(), state ? "ON" : "OFF");
  return true;
}

// ------------ Switch Handling ------------
void handleFlipSwitches() {
  unsigned long actualMillis = millis();
  for (auto &flipSwitch : flipSwitches) {
    unsigned long lastChange = flipSwitch.second.lastFlipSwitchChange;
    if (actualMillis - lastChange > DEBOUNCE_TIME) {
      int pin = flipSwitch.first;
      bool lastState = flipSwitch.second.lastFlipSwitchState;
      bool state = digitalRead(pin);
      if (state != lastState) {
        flipSwitch.second.lastFlipSwitchChange = actualMillis;
        String deviceId = flipSwitch.second.deviceId;
        int relayPIN = devices[deviceId].relayPIN;
        bool newRelayState = !digitalRead(relayPIN);
        digitalWrite(relayPIN, newRelayState);

        SinricProSwitch &mySwitch = SinricPro[deviceId];
        mySwitch.sendPowerStateEvent(!newRelayState);

        flipSwitch.second.lastFlipSwitchState = state;
      }
    }
  }
}

// ------------ WiFi Setup (Captive Portal + DNS) ------------
void setupWiFi() {
  WiFiManager wm;
  wm.setDebugOutput(true);

  pinMode(wifiLed, OUTPUT);
  digitalWrite(wifiLed, HIGH);

  // Captive Portal: AP SSID = ESP_Config, Password = 12345678
  if (!wm.autoConnect("ESP_Config", "12345678")) {
    Serial.println("⚠ Failed to connect. Restarting...");
    ESP.restart();
  }

  Serial.println("✅ WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(wifiLed, LOW);
}

// ------------ SinricPro Setup ------------
void setupSinricPro() {
  for (auto &device : devices) {
    SinricProSwitch &mySwitch = SinricPro[device.first.c_str()];
    mySwitch.onPowerState(onPowerState);
  }
  SinricPro.begin(APP_KEY, APP_SECRET);
  SinricPro.restoreDeviceStates(true);
}

// ------------ Setup ------------
void setup() {
  Serial.begin(BAUD_RATE);

  pinMode(resetPin, INPUT_PULLUP);  // Reset button

  setupRelays();
  setupFlipSwitches();
  setupWiFi();
  setupSinricPro();
}

// ------------ Loop ------------
void loop() {
  // Reset WiFi credentials if button pressed >5 sec
  if (digitalRead(resetPin) == LOW) {
    delay(5000);
    if (digitalRead(resetPin) == LOW) {
      WiFiManager wm;
      wm.resetSettings();
      Serial.println("⚠ WiFi credentials erased. Restarting...");
      ESP.restart();
    }
  }

  SinricPro.handle();
  handleFlipSwitches();
}