# BLE-MQTT-OTA-ESP32
A secure, OTA-capable ESP32 firmware using BLE for WiFi provisioning and MQTT for real-time control.

This project is an ESP32-based firmware that provides:

- BLE setup for WiFi credentials
- MQTT integration
- OTA firmware updates
- AES encrypted communication
- Device mode configuration via MQTT
- Persistent storage with Preferences API

## 🚀 Features

- BLE setup for headless configuration
- AES encryption with Base69 encoding for secure data
- Auto-reconnect WiFi & MQTT logic
- JSON-based command parsing and device control
- OTA update support over HTTP
- Real-time MQTT publishing of network and device info

## 📦 Dependencies

This project uses the following libraries:

- [WiFi.h](https://www.arduino.cc/en/Reference/WiFi)
- [PubSubClient](https://github.com/knolleary/pubsubclient)
- [ArduinoJson](https://arduinojson.org/)
- [Update.h](https://github.com/espressif/arduino-esp32)
- [AESLib / AES](https://github.com/DavyLandman/AESLib)
- [`mbedtls/base64`](https://github.com/espressif/esp-idf/blob/master/components/mbedtls/mbedtls/library/base64.c)
- [Preferences](https://github.com/espressif/arduino-esp32/tree/master/libraries/Preferences)
- [BLEDevice / BLEUtils / BLEServer](https://github.com/nkolban/ESP32_BLE_Arduino)

## 🔧 Setup

1. Copy files into your Arduino sketch folder

2. Install required libraries via Library Manager

3. Select ESP32 board & upload

### 🔐 Encryption Format
- Uses AES-256 CBC encryption with:
    - Key: 32characterslongsecretkeymustbe1
    - IV: 16charIV12345678

- Encrypted data is encoded in Base64 format (ensure this library is provided)

### 📡 MQTT Topics Structure

DEVICE/{userID}/{deviceID}/newmode
DEVICE/{userID}/{deviceID}/savemode
DEVICE/{userID}/{deviceID}/networkinformation/request
DEVICE/{userID}/{deviceID}/networkinformation/response
DEVICE/{userID}/{deviceID}/pause
DEVICE/{userID}/{deviceID}/timer
DEVICE/{userID}/{deviceID}/deviceinfo
/update/{userID}/{deviceID}/url

### 🛠️ Customization

const char* MQTT_SERVER = "mqtt.example.com";
int MQTT_PORT = 1883;
const char* ENCRYPTION_KEY = "32characterslongsecretkeymustbe1";
const char* ENCRYPTION_IV = "16charIV12345678";

### ⚠️ Notes

- BLE is automatically disabled after credentials are received.

- OTA works with .bin files served over HTTP.

- Base69 and AES libraries must support streaming decode and CBC decryption.
