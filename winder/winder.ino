#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

// ===== NETWORK SETTINGS =====
const char* STA_SSID = "YOUR_WIFI_SSID";
const char* STA_PASSWORD = "YOUR_WIFI_PASSWORD";
const bool ENABLE_AP_FALLBACK = true;
const char* AP_SSID = "WatchWinder-Setup";
const char* AP_PASSWORD = "ChangeMe123!";
const unsigned long STA_CONNECT_TIMEOUT_MS = 15000;

// ===== EEPROM LAYOUT =====
const uint32_t CONFIG_MAGIC = 0x57574E44;  // "WWND"
const uint16_t CONFIG_VERSION = 2;

struct Config {
  int numGiri;
  int turnDelay;
  int speed;
  int runMinutes;
};

struct StoredConfig {
  uint32_t magic;
  uint16_t version;
  Config cfg;
  uint32_t crc;
};

Config config;

// ===== MOTOR =====
enum State { MOVING_CW, PAUSE_SHORT, MOVING_CCW, PAUSE_LONG };
State currentState = MOVING_CW;

const int IN1 = D5;
const int IN2 = D6;
const int IN3 = D7;
const int IN4 = D8;

const int STEPS_PER_REV = 2048;
const int MIN_EFFECTIVE_STEP_DELAY_MS = 3;

const int seq[8][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1}
};

ESP8266WebServer server(80);

// ===== RUNTIME =====
bool usingApFallback = false;
bool motorEnabled = true;
bool timerActive = false;
unsigned long runUntilMs = 0;
unsigned long lastStepTime = 0;
unsigned long pauseStartTime = 0;
unsigned long currentStep = 0;
unsigned long stepsToDo = 0;
unsigned long cyclesCompleted = 0;
String lastStopReason = "boot";

int clampInt(int value, int minVal, int maxVal) {
  if (value < minVal) return minVal;
  if (value > maxVal) return maxVal;
  return value;
}

unsigned long crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xEDB88320;
      } else {
        crc >>= 1;
      }
    }
  }
  return ~crc;
}

void setDefaultConfig() {
  config.numGiri = 2;
  config.turnDelay = 3;
  config.speed = 3;
  config.runMinutes = 0;
}

void sanitizeConfig() {
  config.numGiri = clampInt(config.numGiri, 1, 10);
  config.turnDelay = clampInt(config.turnDelay, 1, 60);
  config.speed = clampInt(config.speed, 1, 5);
  config.runMinutes = clampInt(config.runMinutes, 0, 1440);
}

void saveConfig() {
  StoredConfig stored;
  stored.magic = CONFIG_MAGIC;
  stored.version = CONFIG_VERSION;
  stored.cfg = config;
  stored.crc = crc32(reinterpret_cast<const uint8_t*>(&stored.cfg), sizeof(stored.cfg));
  EEPROM.put(0, stored);
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.begin(sizeof(StoredConfig));

  StoredConfig stored;
  EEPROM.get(0, stored);

  if (stored.magic != CONFIG_MAGIC || stored.version != CONFIG_VERSION) {
    setDefaultConfig();
    saveConfig();
    return;
  }

  uint32_t expected = crc32(reinterpret_cast<const uint8_t*>(&stored.cfg), sizeof(stored.cfg));
  if (expected != stored.crc) {
    setDefaultConfig();
    saveConfig();
    return;
  }

  config = stored.cfg;
  sanitizeConfig();
}

const char* stateToText(State state) {
  switch (state) {
    case MOVING_CW:
      return "MOVING_CW";
    case PAUSE_SHORT:
      return "PAUSE_SHORT";
    case MOVING_CCW:
      return "MOVING_CCW";
    case PAUSE_LONG:
      return "PAUSE_LONG";
    default:
      return "UNKNOWN";
  }
}

void sendCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void stepMotor(int step) {
  digitalWrite(IN1, seq[step][0]);
  digitalWrite(IN2, seq[step][1]);
  digitalWrite(IN3, seq[step][2]);
  digitalWrite(IN4, seq[step][3]);
}

void releaseMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

unsigned long getTimerRemainingMs(unsigned long now) {
  if (!timerActive) return 0;
  if (now >= runUntilMs) return 0;
  return runUntilMs - now;
}

void stopMotorSession(const char* reason) {
  motorEnabled = false;
  timerActive = false;
  stepsToDo = 0;
  releaseMotor();
  lastStopReason = reason;
}

void startMotorSession(unsigned long now, int minutes) {
  motorEnabled = true;
  if (minutes > 0) {
    timerActive = true;
    runUntilMs = now + (unsigned long)minutes * 60000UL;
  } else {
    timerActive = false;
    runUntilMs = 0;
  }
  if (currentState == PAUSE_LONG) {
    currentState = MOVING_CW;
  }
  lastStopReason = "running";
}

void applyConfigFromRequest() {
  if (server.hasArg("giri")) {
    config.numGiri = clampInt(server.arg("giri").toInt(), 1, 10);
  }
  if (server.hasArg("delay")) {
    config.turnDelay = clampInt(server.arg("delay").toInt(), 1, 60);
  }
  if (server.hasArg("speed")) {
    config.speed = clampInt(server.arg("speed").toInt(), 1, 5);
  }
  if (server.hasArg("runMinutes")) {
    config.runMinutes = clampInt(server.arg("runMinutes").toInt(), 0, 1440);
  }
}

String activeNetworkMode() {
  return usingApFallback ? "AP" : "STA";
}

String activeIpString() {
  if (usingApFallback) {
    return WiFi.softAPIP().toString();
  }
  return WiFi.localIP().toString();
}

void handleRoot() {
  String msg = "{";
  msg += "\"name\":\"watch-winder-single\",";
  msg += "\"message\":\"Use /api/status and /api/config\"";
  msg += "}";
  sendCorsHeaders();
  server.send(200, "application/json", msg);
}

void handleApiStatus() {
  unsigned long now = millis();
  String json = "{";
  json += "\"mode\":\"single\",";
  json += "\"config\":{";
  json += "\"giri\":" + String(config.numGiri) + ",";
  json += "\"delay\":" + String(config.turnDelay) + ",";
  json += "\"speed\":" + String(config.speed) + ",";
  json += "\"runMinutes\":" + String(config.runMinutes);
  json += "},";
  json += "\"runtime\":{";
  json += "\"motorEnabled\":" + String(motorEnabled ? "true" : "false") + ",";
  json += "\"timerActive\":" + String(timerActive ? "true" : "false") + ",";
  json += "\"timerRemainingMs\":" + String(getTimerRemainingMs(now)) + ",";
  json += "\"state\":\"" + String(stateToText(currentState)) + "\",";
  json += "\"stepsRemaining\":" + String(stepsToDo) + ",";
  json += "\"cyclesCompleted\":" + String(cyclesCompleted) + ",";
  json += "\"lastStopReason\":\"" + lastStopReason + "\",";
  json += "\"uptimeMs\":" + String(now);
  json += "},";
  json += "\"network\":{";
  json += "\"mode\":\"" + activeNetworkMode() + "\",";
  json += "\"ip\":\"" + activeIpString() + "\",";
  json += "\"ssid\":\"" + String(usingApFallback ? AP_SSID : STA_SSID) + "\",";
  json += "\"rssi\":" + String(usingApFallback ? 0 : WiFi.RSSI());
  json += "}";
  json += "}";

  sendCorsHeaders();
  server.send(200, "application/json", json);
}

void handleApiConfig() {
  if (server.method() != HTTP_POST) {
    sendCorsHeaders();
    server.send(405, "application/json", "{\"error\":\"POST required\"}");
    return;
  }

  applyConfigFromRequest();
  sanitizeConfig();
  saveConfig();
  handleApiStatus();
}

void handleApiStart() {
  if (server.method() != HTTP_POST) {
    sendCorsHeaders();
    server.send(405, "application/json", "{\"error\":\"POST required\"}");
    return;
  }

  int minutes = config.runMinutes;
  if (server.hasArg("runMinutes")) {
    minutes = clampInt(server.arg("runMinutes").toInt(), 0, 1440);
  }
  startMotorSession(millis(), minutes);
  handleApiStatus();
}

void handleApiStop() {
  if (server.method() != HTTP_POST) {
    sendCorsHeaders();
    server.send(405, "application/json", "{\"error\":\"POST required\"}");
    return;
  }

  stopMotorSession("api_stop");
  handleApiStatus();
}

void handleOptions() {
  sendCorsHeaders();
  server.send(204);
}

bool connectSta() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(STA_SSID, STA_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < STA_CONNECT_TIMEOUT_MS) {
    delay(250);
  }

  return WiFi.status() == WL_CONNECTED;
}

void startApFallback() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  usingApFallback = true;
  Serial.print("AP fallback IP: ");
  Serial.println(WiFi.softAPIP());
}

void startNetwork() {
  if (connectSta()) {
    usingApFallback = false;
    Serial.print("STA connected. IP: ");
    Serial.println(WiFi.localIP());
    return;
  }

  if (ENABLE_AP_FALLBACK) {
    startApFallback();
  } else {
    usingApFallback = false;
    Serial.println("STA connection failed and AP fallback disabled");
  }
}

void startServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/config", HTTP_POST, handleApiConfig);
  server.on("/api/start", HTTP_POST, handleApiStart);
  server.on("/api/stop", HTTP_POST, handleApiStop);
  server.on("/api/status", HTTP_OPTIONS, handleOptions);
  server.on("/api/config", HTTP_OPTIONS, handleOptions);
  server.on("/api/start", HTTP_OPTIONS, handleOptions);
  server.on("/api/stop", HTTP_OPTIONS, handleOptions);
  server.onNotFound(handleRoot);
  server.begin();
}

void setup() {
  Serial.begin(115200);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  loadConfig();
  startNetwork();
  startServer();

  if (config.runMinutes > 0) {
    startMotorSession(millis(), config.runMinutes);
  }
}

void loop() {
  unsigned long now = millis();

  server.handleClient();

  if (!motorEnabled) {
    return;
  }

  if (timerActive && now >= runUntilMs) {
    stopMotorSession("timer_elapsed");
    return;
  }

  const unsigned long effectiveStepDelay = (unsigned long)max(config.speed, MIN_EFFECTIVE_STEP_DELAY_MS);

  switch (currentState) {
    case MOVING_CW:
      if (stepsToDo == 0) {
        stepsToDo = (unsigned long)STEPS_PER_REV * (unsigned long)config.numGiri * 2UL;
        currentStep = 0;
      }
      if (stepsToDo > 0 && now - lastStepTime >= effectiveStepDelay) {
        int budget = 0;
        while (stepsToDo > 0 && now - lastStepTime >= effectiveStepDelay && budget < 4) {
          lastStepTime += effectiveStepDelay;
          stepMotor(currentStep % 8);
          currentStep++;
          stepsToDo--;
          budget++;
        }
        if (stepsToDo == 0) {
          releaseMotor();
          currentState = PAUSE_SHORT;
          pauseStartTime = now;
        }
      }
      break;

    case PAUSE_SHORT:
      if (now - pauseStartTime >= 500) {
        currentState = MOVING_CCW;
        stepsToDo = 0;
      }
      break;

    case MOVING_CCW:
      if (stepsToDo == 0) {
        stepsToDo = (unsigned long)STEPS_PER_REV * (unsigned long)config.numGiri * 2UL;
        currentStep = 0;
      }
      if (stepsToDo > 0 && now - lastStepTime >= effectiveStepDelay) {
        int budget = 0;
        while (stepsToDo > 0 && now - lastStepTime >= effectiveStepDelay && budget < 4) {
          lastStepTime += effectiveStepDelay;
          stepMotor((8 - (currentStep % 8)) % 8);
          currentStep++;
          stepsToDo--;
          budget++;
        }
        if (stepsToDo == 0) {
          releaseMotor();
          currentState = PAUSE_LONG;
          pauseStartTime = now;
          cyclesCompleted++;
        }
      }
      break;

    case PAUSE_LONG:
      if (now - pauseStartTime >= (unsigned long)config.turnDelay * 60000UL) {
        currentState = MOVING_CW;
        stepsToDo = 0;
      }
      break;
  }
}
