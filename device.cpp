#include <string.h>
#include "WString.h"
#include "esp32-hal.h"
#include "HardwareSerial.h"
#include <string>
#include <WiFi.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include <AES.h>
#include <Base69.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Wire.h>
#include "include.h"

// BLE Service UUID
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
// Characteristic UUID for SSID
#define SSID_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define STATUS_CHAR_UUID "aeb5483e-36e1-4688-b7f5-ea07361b26a9"

BLEServer* pServer;
BLEService* pService;
BLECharacteristic* pSSIDCharacteristic;
BLECharacteristic* pPasswordCharacteristic;
BLECharacteristic* pStatusCharacteristic;

bool deviceConnected = false;
volatile bool wifiCredentialsReceived = false;
bool bleActive = true;  // Track if BLE is active
String receivedSSID = "";

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("Device connected");
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("Device disconnected");
    // Don't restart advertising if we're shutting down BLE
    if (bleActive) {
      pServer->startAdvertising();
      Serial.println("Restarting advertising");
    }
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    String value = pCharacteristic->getValue().c_str();

    if (value.length() > 0) {
      if (pCharacteristic->getUUID().toString() == SSID_CHAR_UUID) {
        receivedSSID = value.c_str();
        Serial.print("Received SSID: ");
        Serial.println(receivedSSID);
      }

      if (!receivedSSID.isEmpty()) {
        wifiCredentialsReceived = true;
      }
    }
  }
};

void TurnOnBLE() {
  // Create the BLE Device
  BLEDevice::init("SmartDevice");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  pService = pServer->createService(SERVICE_UUID);

  // Create BLE Characteristics
  pSSIDCharacteristic = pService->createCharacteristic(
    SSID_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE);

  pStatusCharacteristic = pService->createCharacteristic(
    STATUS_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

  pStatusCharacteristic->setValue("Initial status");
  pSSIDCharacteristic->setCallbacks(new MyCallbacks());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("Waiting for a client connection to notify...");
}

void shutdownBLE() {
  if (!bleActive) return;

  Serial.println("Shutting down BLE...");
  BLEDevice::stopAdvertising();

  if (deviceConnected) {
    pServer->disconnect(pServer->getConnId());
  }

  if (pService != nullptr) {
    pService->stop();
    pServer->removeService(pService);
  }

  BLEDevice::deinit();
  bleActive = false;
  deviceConnected = false;
  Serial.println("BLE shut down complete");
}

Preferences preferences;
SemaphoreHandle_t mqttMutex;

// Network Clients
WiFiClient espClient;
PubSubClient mqtt(espClient);

/*MQTT Variables*/
const char* MQTT_SERVER = "mqtt.example.com";
int MQTT_PORT = 1883;
bool isValidContentType = false;

/*Device control variables*/
const char* DEVICE_TYPE = "Controller";
bool hasSecondStep = false;
bool hasThirdStep = false;
int step1Value = 0;
int step1Time = 0;
int step2Value = 0;
int step2Time = 0;
int step3Value = 0;
int step3Time = 0;
bool tryToConnect;

/*WIFI millis*/
static unsigned long connectingTimeOut = 30000;
static unsigned long connectingTimeOutDelay;
static unsigned long lastDotDelay;
static unsigned long lastTimeOutDelay;
static unsigned long OldlastTimeOutDelay;
static unsigned long lastRssiUpdate = 0;
static unsigned long lastdoneUpdate = 0;

struct WifiStructType {
  String ssid;
  String password;
  int state;
} wifiStruct;

static TaskHandle_t reconnectMQTTTaskHandle = NULL;

const char* ENCRYPTION_KEY = "32characterslongsecretkeymustbe1";
const char* ENCRYPTION_IV = "16charIV12345678";

/*Topics Variables*/
String deviceID;
String userID;
String TOPIC_PREFIX = "DEVICE/";
String slash = "/";
String MODE = "/newmode";
String newModeTopic;
String toUpdate = "/update/";
String url = "/url/";
String updateTopic;
String netInfo = "/networkinformation/";
String deviceinfo = "/deviceinfo/";
String request = "request";
String response = "response";
String _pause = "/pause";
String _timer = "/timer";
String _save = "/savemode";
String requestInfo;
String responseInfo;
String pauseTopic;
String timerTopic;
String devInfoTopic;
String deviceRequestTopic;
String saveModeTopic;

bool Updating = false;
bool Updated = false;
String updateURL;

void resetStepsFromWifiCMD() {
  hasSecondStep = false;
  hasThirdStep = false;
}

String decryptData(String encryptedBase69) {
  if (strlen(ENCRYPTION_KEY) != 32) {
    Serial.println("Invalid key length - must be 32 chars");
    return "";
  }
  if (strlen(ENCRYPTION_IV) != 16) {
    Serial.println("Invalid IV length - must be 16 chars");
    return "";
  }
  
  if (encryptedBase69.length() == 0) {
    Serial.println("Empty encrypted string");
    return "";
  }

  int inputLength = encryptedBase69.length();
  int decodedLength = Base69.decodedLength((char*)encryptedBase69.c_str(), inputLength);

  if (decodedLength <= 0 || decodedLength > 256) {
    Serial.println("Invalid Base69 data");
    return "";
  }

  char decodedData[decodedLength];
  int Base69Result = Base69.decode(decodedData, (char*)encryptedBase69.c_str(), inputLength);

  if (Base69Result == 0) {
    Serial.println("Base69 decoding failed");
    return "";
  }

  AES aes;
  byte key[32], iv[16];
  memcpy(key, ENCRYPTION_KEY, 32);
  memcpy(iv, ENCRYPTION_IV, 16);

  if (decodedLength % 16 != 0) {
    Serial.println("Invalid ciphertext length");
    return "";
  }

  byte ciphertext[decodedLength];
  memcpy(ciphertext, decodedData, decodedLength);
  byte decrypted[decodedLength];
  aes.set_key(key, 32);
  aes.cbc_decrypt(ciphertext, decrypted, decodedLength / 16, iv);

  int pad = decrypted[decodedLength - 1];
  if (pad > 0 && pad <= 16) {
    for (int i = decodedLength - pad; i < decodedLength; i++) {
      decrypted[i] = 0;
    }
    decrypted[decodedLength - pad] = '\0';
  }
  else {
    decrypted[decodedLength] = '\0';
  }

  return String((char*)decrypted);
}

String getBinName(String url) {
  url.replace("http://", "");
  url.replace("https://", "");

  int lastSlash = url.lastIndexOf('/');
  if (lastSlash >= 0) {
    return url.substring(lastSlash);
  }
  return "/" + url;
}

static String getHeaderValue(String header, String headerName) {
  String headerLower = header;
  headerLower.toLowerCase();
  String headerNameLower = headerName;
  headerNameLower.toLowerCase();

  if (headerLower.startsWith(headerNameLower)) {
    return header.substring(headerName.length());
  }
  return "";
}

String getHostName(String url) {
  url.replace("http://", "");
  url.replace("https://", "");

  int colonIndex = url.indexOf(':');
  if (colonIndex >= 0) {
    url = url.substring(0, colonIndex);
  }

  int slashIndex = url.indexOf('/');
  if (slashIndex >= 0) {
    url = url.substring(0, slashIndex);
  }

  return url;
}

void publishNetworkInfo() {
  if (WiFi.status() == WL_CONNECTED) {
    JsonDocument doc;
    doc["device_name"] = "SmartDevice";
    doc["device_type"] = DEVICE_TYPE;
    doc["mac"] = WiFi.macAddress();
    doc["ip"] = WiFi.localIP().toString();
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
    doc["bssid"] = WiFi.BSSIDstr();
    doc["channel"] = WiFi.channel();

    String output;
    serializeJson(doc, output);
    if (xSemaphoreTake(mqttMutex, (TickType_t)100) == pdTRUE) {
      mqtt.publish((char*)responseInfo.c_str(), output.c_str());
      xSemaphoreGive(mqttMutex);
    }
  }
}

void publishStartEnd(bool startEnd) {
  String output = startEnd ? "start" : "end";

  if (xSemaphoreTake(mqttMutex, (TickType_t)100) == pdTRUE) {
    mqtt.publish((char*)pauseTopic.c_str(), output.c_str());
    xSemaphoreGive(mqttMutex);
  }
}

void publishContinueSuspend(bool ContinueSuspend) {
  String output = ContinueSuspend ? "continue" : "suspend";

  if (xSemaphoreTake(mqttMutex, (TickType_t)100) == pdTRUE) {
    mqtt.publish((char*)pauseTopic.c_str(), output.c_str());
    xSemaphoreGive(mqttMutex);
  }
}

void publishDeviceInfo() {
  if (WiFi.status() == WL_CONNECTED) {
    JsonDocument doc;
    doc["pluggedIn"] = 0;
    doc["turnedOn"] = 0;
    doc["secondsOnCN"] = 0;
    doc["error"] = 0;
    doc["lastError"] = 0;
    doc["allTimeWS"] = 0;
    doc["sessionWS"] = 0;
    doc["igbtMax"] = 0;
    doc["panMax"] = 0;
    doc["volt"] = 0;
    doc["power_"] = 0;
    doc["pad"] = 0;
    doc["igbt"] = 0;

    String output;
    serializeJson(doc, output);
    mqtt.publish((char*)devInfoTopic.c_str(), output.c_str());
    Serial.println(output.c_str());
    Serial.println(devInfoTopic.c_str());
  }
}

void processDeviceCommand(JsonDocument& doc) {
  String currentMode = doc["mode"].as<String>();
  String nameOfMode = doc["name"].as<String>();
  JsonArray steps = doc["steps"];
  uint8_t newmodemsglen = nameOfMode.length();
  uint8_t DataOfMode[(newmodemsglen / 2) + 2];
  
  uint8_t ArbFlg;
  if ((nameOfMode[0] == 'D' || nameOfMode[0] == 'd') && (nameOfMode[1] == '8' || nameOfMode[1] == '9' || nameOfMode[1] == 'A' || nameOfMode[1] == 'B')) {
    ArbFlg = 1;
    DataOfMode[0] = 32;
  }
  else {
    ArbFlg = 0;
  }
  
  uint8_t PointCont;
  for (PointCont = 0; ((2 * PointCont) < newmodemsglen) && ((PointCont < 13 && ArbFlg == 0) || (PointCont < 25 && ArbFlg == 1)); PointCont++) {
    if (((uint8_t)nameOfMode[2 * PointCont] > 64) && ((uint8_t)nameOfMode[2 * PointCont] < 71)) {
      DataOfMode[PointCont + ArbFlg] = ((uint8_t)nameOfMode[2 * PointCont] - 55) * 16;
    }
    else {
      if ((uint8_t)nameOfMode[2 * PointCont] > 96) {
        DataOfMode[PointCont + ArbFlg] = ((uint8_t)nameOfMode[2 * PointCont] - 87) * 16;
      }
      else {
        DataOfMode[PointCont + ArbFlg] = ((uint8_t)nameOfMode[2 * PointCont] - 48) * 16;
      }
    }

    if (((uint8_t)nameOfMode[(2 * PointCont) + 1] > 64) && ((uint8_t)nameOfMode[(2 * PointCont) + 1] < 71)) {
      DataOfMode[PointCont + ArbFlg] += ((uint8_t)nameOfMode[(2 * PointCont) + 1] - 55);
    }
    else {
      if ((uint8_t)nameOfMode[(2 * PointCont) + 1] > 96) {
        DataOfMode[PointCont + ArbFlg] += ((uint8_t)nameOfMode[(2 * PointCont) + 1] - 87);
      }
      else {
        DataOfMode[PointCont + ArbFlg] += ((uint8_t)nameOfMode[(2 * PointCont) + 1] - 48);
      }
    }
  }
  DataOfMode[PointCont + ArbFlg] = 0;

  step1Value = 0;
  step2Time = 0;
  step2Value = 0;
  step2Time = 0;
  step3Value = 0;
  step3Time = 0;
  hasSecondStep = false;
  hasThirdStep = false;

  int stepIndex = 0;
  for (JsonObject step : steps) {
    int value = step["value"];
    int time = step["time"];

    Serial.printf("Processing step %d - Value: %d, Time: %d mins\n", stepIndex + 1, value, time);

    if (stepIndex == 0) {
      step1Value = value;
      step1Time = time;
    }
    else if (stepIndex == 1) {
      step2Value = value;
      step2Time = time;
      hasSecondStep = true;
    }
    else if (stepIndex == 2) {
      step3Value = value;
      step3Time = time;
      hasThirdStep = true;
    }

    if (stepIndex++ >= 2) break;
  }

  memcpy(UserDefinedtext, DataOfMode, 52);
  UserDefinedtext[51] = 0;

  if (currentMode == "power") {
    temperaturControl = 0;
    Serial.println("temperaturControl: power");
  }
  if (currentMode == "temperature") {
    temperaturControl = 1;
    Serial.println("temperaturControl: temperature");
  }

  // Device control functions would go here
  // These are specific to the hardware being controlled
  // Replaced with generic comments
  Serial.println("Executing device control commands");
}

void update(String url, int port) {
  int contentLength;
  String bin = getBinName(url);
  String host = getHostName(url);

  Serial.println("Connecting to host: " + host);
  Serial.println("Requesting bin: " + bin);
  Serial.print("Attempting connection to ");
  Serial.print(host);
  Serial.print(" on port ");
  Serial.println(port);

  Wire.end();

  if (!espClient.connect(host.c_str(), port)) {
    Serial.println("❌ Connection to " + host + " failed. Check network/server.");
    Wire.begin((uint8_t)I2C_DEV_ADDR);
    return;
  }

  espClient.print(String("GET ") + bin + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Cache-Control: no-cache\r\n" + "Connection: close\r\n\r\n");

  unsigned long timeout = millis();
  while (!espClient.available()) {
    if (millis() - timeout > 5000) {
      Serial.println("❌ HTTP timeout!");
      espClient.stop();
      Wire.begin((uint8_t)I2C_DEV_ADDR);
      return;
    }
  }

  contentLength = 0;
  isValidContentType = false;

  while (espClient.available()) {
    String line = espClient.readStringUntil('\n');
    line.trim();
    Serial.println("[DEBUG] Header: " + line);

    if (!line.length()) break;

    if (line.startsWith("HTTP/1.")) {
      if (!line.substring(9, 12).equals("200")) {
        Serial.println("❌ Non-200 response: " + line);
        espClient.stop();
        Wire.begin((uint8_t)I2C_DEV_ADDR);
        return;
      }
    }
    else if (line.substring(0, 16).equalsIgnoreCase("Content-Length: ")) {
      contentLength = atoi(getHeaderValue(line, "Content-Length: ").c_str());
      Serial.println("✅ Content-Length: " + String(contentLength));
    }
    else if (line.substring(0, 14).equalsIgnoreCase("Content-Type: ")) {
      String contentType = getHeaderValue(line, "Content-Type: ");
      Serial.println("✅ Content-Type: " + contentType);
      isValidContentType = contentType.equalsIgnoreCase("application/octet-stream") || contentType.startsWith("application/octet-stream");
      Serial.println("isValidContentType: " + String(isValidContentType));
    }
  }

  if (contentLength <= 0 || !isValidContentType) {
    Serial.println("❌ Invalid content from server. Aborting.");
    espClient.stop();
    Wire.begin((uint8_t)I2C_DEV_ADDR);
    return;
  }

  if (!Update.begin(contentLength)) {
    Serial.println("❌ Not enough space for OTA.");
    espClient.stop();
    Wire.begin((uint8_t)I2C_DEV_ADDR);
    return;
  }

  Serial.println("🔄 Starting OTA update...");
  size_t written = Update.writeStream(espClient);

  if (written == contentLength) {
    Serial.println("✅ Successfully written: " + String(written) + " bytes");
  }
  else {
    Serial.println("❌ Only written: " + String(written) + "/" + String(contentLength));
  }

  if (Update.end()) {
    Serial.println("🎉 OTA done.");
    if (Update.isFinished()) {
      Updated = true;
      Serial.println("✅ Update finished. Rebooting...");
    }
    else {
      Serial.println("⚠️ Update not marked finished.");
    }
  }
  else {
    Serial.println("❌ OTA Error #: " + String(Update.getError()));
  }
  
  Wire.begin((uint8_t)I2C_DEV_ADDR);
  espClient.stop();
}

void callback(char* topic, byte* payload, unsigned int length) {
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';

  if (strstr(topic, (char*)newModeTopic.c_str()) != NULL) {
    Serial.println("Processing device command");

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
      Serial.print("JSON parse failed: ");
      Serial.println(error.c_str());
      return;
    }

    processDeviceCommand(doc);
  }

  if (strstr(topic, (char*)saveModeTopic.c_str()) != NULL) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
      Serial.print("JSON parse failed: ");
      Serial.println(error.c_str());
      return;
    }

    processDeviceCommand(doc);

    Preferences modesPreferences;
    modesPreferences.begin("Modes", false);
    if (modesPreferences.getString("mode1", "") == "") modesPreferences.putString("mode1", message);
    else if (modesPreferences.getString("mode2", "") == "") modesPreferences.putString("mode2", message);
    else if (modesPreferences.getString("mode3", "") == "") modesPreferences.putString("mode3", message);
    else if (modesPreferences.getString("mode4", "") == "") modesPreferences.putString("mode4", message);
    else if (modesPreferences.getString("mode5", "") == "") modesPreferences.putString("mode5", message);
    modesPreferences.end();
  }

  if (strstr(topic, (char*)requestInfo.c_str()) != NULL) {
    publishNetworkInfo();
    Serial.println("Network info requested");
  }

  if (strstr(topic, (char*)deviceRequestTopic.c_str()) != NULL) {
    publishDeviceInfo();
    Serial.println("Device info requested");
  }

  String _topic = String(topic);
  if (_topic.equals((char*)updateTopic.c_str())) {
    if (String(message) == "done") {
      ESP.restart();
    }
    Serial.printf("message: ");
    Serial.println(message);
    updateURL = String(message);
    Updating = true;
  }

  if (strstr(topic, (char*)pauseTopic.c_str()) != NULL) {
    if (strcmp(message, "pause") == 0) {
      Serial.println("pause");
    }
    else if (strcmp(message, "resume") == 0) {
      Serial.println("resume");
    }
    return;
  }
}

void initializeMQTT() {
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(callback);
  mqtt.setBufferSize(2048);
  mqtt.setKeepAlive(10);
}

void reconnect(void* pvParameters) {
  Serial.print("Attempting MQTT connection...");
  String clientId = userID;

  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      esp_task_wdt_delete(reconnectMQTTTaskHandle);
      reconnectMQTTTaskHandle = NULL;
      vTaskDelete(reconnectMQTTTaskHandle);
    }

    if (mqtt.connect(clientId.c_str())) {
      Serial.println("connected");
      if (!mqtt.subscribe((char*)newModeTopic.c_str())) {
        Serial.println("Subscribe to newmode failed");
      }
      if (!mqtt.subscribe((char*)saveModeTopic.c_str())) {
        Serial.println("Subscribe to savemode failed");
      }
      if (!mqtt.subscribe((char*)requestInfo.c_str())) {
        Serial.println("Subscribe to network info failed");
      }
      if (!mqtt.subscribe((char*)updateTopic.c_str())) {
        Serial.println("Subscribe to update url failed");
      }
      if (!mqtt.subscribe((char*)pauseTopic.c_str())) {
        Serial.println("Subscribe to pause topic failed");
      }
      if (!mqtt.subscribe((char*)deviceRequestTopic.c_str())) {
        Serial.println("Subscribe to update url failed");
      }
      esp_task_wdt_delete(reconnectMQTTTaskHandle);
      reconnectMQTTTaskHandle = NULL;
      vTaskDelete(reconnectMQTTTaskHandle);
    }
    else {
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" try again in 5 seconds");
      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}

void publishTimerUpdate(uint32_t value) {
  char timerStr[10];
  snprintf(timerStr, sizeof(timerStr), "%u", value);
  if (xSemaphoreTake(mqttMutex, (TickType_t)100) == pdTRUE) {
    if (mqtt.publish(timerTopic.c_str(), timerStr)) {
      Serial.print("Published timer: ");
      Serial.println(value);
    }
    else {
      Serial.println("Failed to publish timer");
    }
    xSemaphoreGive(mqttMutex);
  }
}

void createTopics() {
  newModeTopic = TOPIC_PREFIX + userID + slash + deviceID + MODE;
  saveModeTopic = TOPIC_PREFIX + userID + slash + deviceID + _save;
  updateTopic = toUpdate + userID + slash + deviceID + url;
  requestInfo = TOPIC_PREFIX + userID + slash + deviceID + netInfo + request;
  responseInfo = TOPIC_PREFIX + userID + slash + deviceID + netInfo + response;
  pauseTopic = TOPIC_PREFIX + userID + slash + deviceID + _pause;
  timerTopic = TOPIC_PREFIX + userID + slash + deviceID + _timer;
  deviceRequestTopic = TOPIC_PREFIX + userID + slash + deviceID + deviceinfo + request;
  devInfoTopic = TOPIC_PREFIX + userID + slash + deviceID + deviceinfo;
}

void wifiSetup() {
  wifiStruct.state = START_LOGIN;
  mqttMutex = xSemaphoreCreateMutex();
  initializeMQTT();
}

void wifiLoop() {
  switch (wifiStruct.state) {
    case START_LOGIN:
      preferences.begin("wifi-creds", false);
      wifiStruct.ssid = preferences.getString("ssid", "");
      wifiStruct.password = preferences.getString("password", "");
      deviceID = preferences.getString("deviceID", "");
      userID = preferences.getString("userID", "");
      preferences.end();
      if (wifiStruct.ssid.isEmpty()) {
        TurnOnBLE();
        Serial.println("No WiFi credentials found.");
        Serial.println("Bluetooth started");
        wifiStruct.state = OPEN_BLUETOOTH;
      }
      else {
        Serial.println("OLD_CONNECTING");
        WiFi.begin(wifiStruct.ssid.c_str(), wifiStruct.password.c_str());
        wifiStruct.state = OLD_CONNECTING;
        connectingTimeOutDelay = millis();
      }
      break;

    case DISCONNECTED:
      WiFi.begin(wifiStruct.ssid.c_str(), wifiStruct.password.c_str());
      wifiStruct.state = OLD_CONNECTING;
      break;

    case RESETED:
      Serial.println("Button pressed. Clearing WiFi credentials.");
      preferences.begin("wifi-creds", false);
      preferences.remove("ssid");
      preferences.remove("password");
      preferences.remove("deviceID");
      preferences.remove("userID");
      preferences.end();
      WiFi.disconnect(true);
      Serial.println("Bluetooth started");
      TurnOnBLE();
      wifiStruct.state = OPEN_BLUETOOTH;
      break;

    case OPEN_BLUETOOTH:
      if (wifiCredentialsReceived) {
        wifiCredentialsReceived = false;
        receivedSSID.trim();

        String decryptedData = decryptData(receivedSSID);

        if (decryptedData.length() > 0) {
          Serial.print("Decrypted JSON: ");
          Serial.println(decryptedData);

          JsonDocument doc;
          DeserializationError error = deserializeJson(doc, decryptedData);

          if (error) {
            Serial.print("JSON parse failed: ");
            Serial.println(error.c_str());
            break;
          }

          const char* newSSID = doc["ssid"];
          const char* newPassword = doc["password"];
          const char* newDeviceID = doc["deviceID"];
          const char* newUserID = doc["userID"];

          if (newSSID && newPassword && newDeviceID && newUserID) {
            Serial.println("Parsed values:");
            Serial.printf("SSID: %s\n", newSSID);
            Serial.printf("Password: %s\n", newPassword);
            Serial.printf("DeviceID: %s\n", newDeviceID);
            Serial.printf("UserID: %s\n", newUserID);
            wifiStruct.ssid = newSSID;
            wifiStruct.password = newPassword;
            deviceID = newDeviceID;
            userID = newUserID;
            WiFi.begin(wifiStruct.ssid.c_str(), wifiStruct.password.c_str());
            Serial.println("Connecting to WiFi...");
            wifiStruct.state = NEW_CONNECTING;
            connectingTimeOutDelay = millis();
          }
        }
        else {
          Serial.println("Decryption failed - invalid data");
        }
      }
      break;

    case NEW_CONNECTING:
      if (WiFi.status() != WL_CONNECTED) {
        if (millis() - lastDotDelay > 500) {
          lastDotDelay = millis();
          Serial.print(".");

          if (millis() - connectingTimeOutDelay > connectingTimeOut) {
            connectingTimeOutDelay = millis();
            Serial.println("Time Out");
            wifiStruct.state = OPEN_BLUETOOTH;
          }
        }
      }
      else {
        wifiStruct.state = NEW_CONNECTED;
      }
      break;

    case NEW_CONNECTED:
      preferences.begin("wifi-creds", false);
      preferences.putString("ssid", wifiStruct.ssid);
      preferences.putString("password", wifiStruct.password);
      preferences.putString("deviceID", deviceID);
      preferences.putString("userID", userID);
      preferences.end();

      if (deviceConnected) {
        pStatusCharacteristic->setValue(0);
        pStatusCharacteristic->notify();
        delay(100);
      }

      shutdownBLE();
      Serial.println("\nWiFi Connected!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      createTopics();
      wifiStruct.state = NOTHING;
      break;

    case OLD_CONNECTING:
      if (WiFi.status() != WL_CONNECTED) {
        if (millis() - lastDotDelay > 500) {
          lastDotDelay = millis();
          Serial.print(".");
          if (millis() - connectingTimeOutDelay > connectingTimeOut) {
            connectingTimeOutDelay = millis();
            Serial.println("Time Out");
            wifiStruct.state = OLD_TIME_OUT;
            OldlastTimeOutDelay = millis();
          }
        }
      }
      else {
        wifiStruct.state = OLD_CONNECTED;
      }
      break;

    case OLD_CONNECTED:
      Serial.println("\nWiFi Connected!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      createTopics();
      wifiStruct.state = NOTHING;
      break;

    case OLD_TIME_OUT:
      if (millis() - OldlastTimeOutDelay > connectingTimeOut) {
        OldlastTimeOutDelay = millis();
        wifiStruct.state = OLD_CONNECTING;
        connectingTimeOutDelay = millis();
      }
      break;

    case NOTHING:
      if (WiFi.status() == WL_DISCONNECTED) {
        wifiStruct.state = DISCONNECTED;
      }
      if (millis() - lastRssiUpdate > 10000) {
        lastRssiUpdate = millis();
        if (mqtt.connected()) {
          publishNetworkInfo();
        }
      }
      
      if (!mqtt.connected()) {
        if (reconnectMQTTTaskHandle == NULL) {
          xTaskCreatePinnedToCore(
            reconnect,
            "RECONNECT",
            5000,
            NULL,
            1,
            &reconnectMQTTTaskHandle,
            1
          );
        }
      }
      if (mqtt.connected()) {
        mqtt.loop();
      }

      if (millis() - lastdoneUpdate > 1000) {
        lastdoneUpdate = millis();
        if (Updated && mqtt.connected() && WiFi.status() == WL_CONNECTED) {
          mqtt.publish((char*)updateTopic.c_str(), "done");
        }
      }
      break;
  }
}