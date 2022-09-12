#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>

// Preferences
Preferences preferences;
struct PreferencesStruct {
  char deviceID[17] = "";
};
PreferencesStruct preferencesStorage;

// Neon Timing Connection
const int NT_PROTOCOL_DISCONNECTED = 0;
const int NT_PROTOCOL_SERIAL = 1;
int neonTimingConnectionProtocol = NT_PROTOCOL_DISCONNECTED;
bool rncAllowedEventsLog = false;
unsigned long connectionLastHeartbeatTime = millis();
unsigned long connectionLastHeartbeatPingTime = millis();
unsigned long connectionHeartbeatTimeout = 10000;
unsigned long connectionHeartbeatInterval = connectionHeartbeatTimeout / 3;

// Neon Timing Connection Light
const int connectionLightToggleDelay = 1000;
bool connectionLightEnabled = false;
int connectionLightLastToggleTime = millis();

// Serial
const int SERIAL_BUFFER_MAX = 201;
char serialBuffer[SERIAL_BUFFER_MAX];

// Pixel Setup
const int PIXEL_COUNT = 8;
const int PIXEL_PIN = 18;
Adafruit_NeoPixel pixelStrip = Adafruit_NeoPixel(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Light show
const int LS_NONE = 0;
const int LS_COUNTDOWN_STARTED_ANIMATION = 1;
const int LS_RACE_COMPLETED_ANIMATION = 2;
int activeLightShow = LS_NONE;
int lightShowStartTime = 0;
int lightShowStateStartTime = 0;
int lightShowState = -1;

void setupPreferences();
void setupPixels();
void toggleConnectionLight(int toggle = -1);
void startLightShow(int newLightShow);
void stopLightShow();
void onMessage(int communicationProtocol, JsonVariant messageDoc);
void writeLog(const char* message);
void serialMessageLoop();
void sendHandshakeMessage(const char* type, int sendWithProtocol);
void setAllPixels(byte red, byte green, byte blue);
void setPixel(int pixel, byte red, byte green, byte blue);
void lightShowLoop();
void sendCommand(JsonDocument& doc, int sendWithProtocol = -1);
void updateNeonTimingConnectionState(int newConnectionState);

void setup() {

  Serial.begin(115200);
  Serial.println();

  setupPreferences();
  setupPixels();
}

void loop() {
  serialMessageLoop();
  lightShowLoop();
}

void setupPreferences() {

  preferences.begin("neon-timing", false);
  preferences.getString("device_id", preferencesStorage.deviceID, 17);
  // If there isn't a device id saved into memory then store one
  if (strcmp(preferencesStorage.deviceID, "") == 0) {
    char did[10];
    snprintf(did, 10, "%d", random(100000000, 999999999));
    strncpy(preferencesStorage.deviceID, did, sizeof(did));
    // Store the newly generated device id into storage, even on reboot this doesn't reset
    preferences.putString("device_id", preferencesStorage.deviceID);
    writeLog("Saved new Device ID");
  }
}

void setupPixels() {

  pixelStrip.begin();

  // Initialize all pixels to 'off'
  pixelStrip.show();
}

void toggleConnectionLight(int toggle) {

  connectionLightLastToggleTime = millis();
  if (toggle == 0) {
    connectionLightEnabled = false;
  }
  else if (toggle > 0) {
    connectionLightEnabled = true;
  }
  else {
    connectionLightEnabled = !connectionLightEnabled;
  }

  if (connectionLightEnabled) {
    setAllPixels(0, 0, 200); // Blue
    pixelStrip.show();
  } else {
    setAllPixels(0, 0, 0); // Off
    pixelStrip.show();
  }
}

void startLightShow(int newLightShow) {

  activeLightShow = newLightShow;
  lightShowStartTime = millis();
  lightShowState = -1;
  lightShowStateStartTime = 0;

  lightShowLoop();
  return;
}

void stopLightShow() {
  startLightShow(LS_NONE);
}

void onMessage(int communicationProtocol, JsonVariant messageDoc) {

  if (!messageDoc.is<JsonObject>()) {
    writeLog("Error processing message: message must be an object");
    return;
  }

  if (!messageDoc["cmd"].is<const char*>()) {
    writeLog("Error processing message: cmd must be a string");
    return;
  }
  const char* command = messageDoc["cmd"];

  if (neonTimingConnectionProtocol == NT_PROTOCOL_DISCONNECTED && strcmp(command, "handshake_init") != 0 && strcmp(command, "handshake_ack") != 0) {
    writeLog("Error processing message: device not initialized");
    return;
  }

  if (strcmp(command, "event") == 0) {
    if (!messageDoc["evt"].is<const char*>()) {
      writeLog("Error processing message: evt must be a string");
      return;
    }
    if (!messageDoc["type"].is<const char*>()) {
      writeLog("Error processing message: type must be a string");
      return;
    }
    if (strcmp(messageDoc["evt"], "race") != 0 && strcmp(messageDoc["evt"], "flag") != 0) {
      writeLog("Error processing message: evt value is not supported");
      return;
    }
    const char* eventType = messageDoc["type"];
    const char* evt = messageDoc["evt"];

    if (strcmp(evt, "race") == 0) {
      if (strcmp(eventType, "race_staging") == 0) {
        stopLightShow();
        setAllPixels(200, 0, 0); // Red
      }
      else if (strcmp(eventType, "countdown_started") == 0) {
        startLightShow(LS_COUNTDOWN_STARTED_ANIMATION);
      }
      else if (strcmp(eventType, "countdown_end_delay_started") == 0) {
        stopLightShow();
        setAllPixels(0, 0, 0); // Off
      }
      else if (strcmp(eventType, "race_started") == 0) {
        stopLightShow();
        setAllPixels(0, 100, 0); // Green
      }
      else if (strcmp(eventType, "race_completed") == 0) {
        startLightShow(LS_RACE_COMPLETED_ANIMATION);
      }
    }
  }
  else if (strcmp(command, "handshake_init") == 0) {
    if (strcmp(messageDoc["protocol"].as<const char*>(), "NT1") != 0) {
      writeLog("Error processing message: protocol must be NT1");
      return;
    }
    if (!messageDoc["events"].is<JsonArray>()) {
      writeLog("Error processing message: events must be an array");
      return;
    }

    sendHandshakeMessage("handshake_ack", communicationProtocol);

    for (JsonVariant value : messageDoc["events"].as<JsonArray>()) {
      if (strcmp(value.as<const char*>(), "log") == 0) {
        rncAllowedEventsLog = true;
      }
      else if (strcmp(value.as<const char*>(), "*") == 0) {
        rncAllowedEventsLog = true;
      }
    }

    if (neonTimingConnectionProtocol != communicationProtocol) {
      updateNeonTimingConnectionState(communicationProtocol);
      toggleConnectionLight(0);
    }
  }
  else if (strcmp(command, "handshake_ack") == 0) {
    if (strcmp(messageDoc["protocol"].as<const char*>(), "NT1") != 0) {
        writeLog("Error processing message: protocol must be NT1");
      return;
    }
  }
  else {
    StaticJsonDocument<200> cmd;
    cmd["cmd"] = "event";
    cmd["evt"] = "log";
    cmd["message"] = "Invalid command";
    cmd["data"]["command"] = command;
    sendCommand(cmd);
  }
}

void writeLog(const char* message) {

  StaticJsonDocument<200> cmd;
  cmd["cmd"] = "event";
  cmd["evt"] = "log";
  cmd["message"] = message;
  sendCommand(cmd);
}

void serialMessageLoop() {
  if (neonTimingConnectionProtocol == NT_PROTOCOL_DISCONNECTED && millis() - connectionLightLastToggleTime > connectionLightToggleDelay) {
    toggleConnectionLight();
  }

  if (neonTimingConnectionProtocol == NT_PROTOCOL_SERIAL) {
    // If heartbeat timeout occurs then disconnection
    if (millis() - connectionLastHeartbeatTime > connectionHeartbeatTimeout) {
      updateNeonTimingConnectionState(NT_PROTOCOL_DISCONNECTED);
    }
    // If heartbeat timeout 
    else if (millis() - connectionLastHeartbeatTime > connectionHeartbeatInterval && millis() - connectionLastHeartbeatPingTime > connectionHeartbeatInterval) {
      sendHandshakeMessage("handshake_init", NT_PROTOCOL_SERIAL);
      connectionLastHeartbeatPingTime = millis();
    }
  }

  if (Serial.available() <= 0) {
    return;
  }

  connectionLastHeartbeatTime = millis();

  Serial.readBytesUntil('\n', serialBuffer, SERIAL_BUFFER_MAX - 1);

  StaticJsonDocument<512> messageDoc;
  DeserializationError error = deserializeJson(messageDoc, serialBuffer);
  if (error) {
    writeLog("Error processing message: could not deserialize json");
    return;
  }

  onMessage(NT_PROTOCOL_SERIAL, messageDoc);

  // Reset buffer
  memset(&serialBuffer, 0, SERIAL_BUFFER_MAX);
}

void sendHandshakeMessage(const char* type, int sendWithProtocol) {

    StaticJsonDocument<200> cmd;
    cmd["cmd"] = type;
    cmd["device"] = "Race Lights";
    JsonArray events = cmd.createNestedArray("events");
    events.add("race");
    sendCommand(cmd, sendWithProtocol);
}

void setAllPixels(byte red, byte green, byte blue) {

  for(int i = 0; i < PIXEL_COUNT; i++) {
    pixelStrip.setPixelColor(i, pixelStrip.Color(red, green, blue));
  }
  pixelStrip.show();
}

void setPixel(int pixel, byte red, byte green, byte blue) {

  pixelStrip.setPixelColor(pixel, pixelStrip.Color(red, green, blue));
  pixelStrip.show();
}

void lightShowLoop() {

  if (activeLightShow == LS_NONE) {
    return;
  }

  if (lightShowState < 0) {
    lightShowState = 0;
  }

  if (activeLightShow == LS_COUNTDOWN_STARTED_ANIMATION) {
    if (millis() - lightShowStateStartTime >= 500) {
      if (lightShowState == 1) {
        setAllPixels(0, 0, 0);
        lightShowState = 0;
      }
      else {
        setAllPixels(200, 0, 0);
        lightShowState = 1;
      }
      lightShowStateStartTime = millis();
    }
  }
  else if (activeLightShow == LS_RACE_COMPLETED_ANIMATION) {
    if (millis() - lightShowStartTime >= 10000) {
      setAllPixels(100, 0, 0); // Red
      stopLightShow();
      return;
    }

    if (millis() - lightShowStateStartTime >= 30) {
      int pixel = random(PIXEL_COUNT);
      setAllPixels(0, 0, 0);
      setPixel(pixel, 255, 255, 255);
      lightShowStateStartTime = millis();
    }
  }
}

void sendCommand(JsonDocument& doc, int sendWithProtocol) {
  if (neonTimingConnectionProtocol != NT_PROTOCOL_DISCONNECTED && doc["evt"] == "log" && !rncAllowedEventsLog) {
    return;
  }

  doc["protocol"] = "NT1";
  doc["time"] = millis();
  doc["did"] = preferencesStorage.deviceID;

  serializeJson(doc, Serial);
  Serial.println();
}

void updateNeonTimingConnectionState(int newConnectionState) {
  neonTimingConnectionProtocol = newConnectionState;
  if (newConnectionState == NT_PROTOCOL_DISCONNECTED) {
    rncAllowedEventsLog = false;
    startLightShow(LS_NONE);
  }
}
