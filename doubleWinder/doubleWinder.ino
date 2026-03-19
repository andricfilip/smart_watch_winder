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

// ===== MOTOR STATE =====
enum State { MOVING_CW, PAUSE_SHORT, MOVING_CCW, PAUSE_LONG };

struct MotorState {
  State currentState = MOVING_CW;
  unsigned long lastStepTime = 0;
  unsigned long pauseStartTime = 0;
  unsigned long currentStep = 0;
  unsigned long stepsToDo = 0;
};

// ===== EEPROM LAYOUT =====
const uint32_t CONFIG_MAGIC = 0x57574E44;  // "WWND"
const uint16_t CONFIG_VERSION = 2;

struct Config {
  int numGiri1;
  int speed1;
  int numGiri2;
  int speed2;
  int globalDelay;
  int runMinutes;
};

struct StoredConfig {
  uint32_t magic;
  uint16_t version;
  Config cfg;
  uint32_t crc;
};

Config config;

// ===== PINS =====
const int IN1_A = D5;
const int IN2_A = D6;
const int IN3_A = D7;
const int IN4_A = D8;

const int IN1_B = D4;
const int IN2_B = D3;
const int IN3_B = D2;
const int IN4_B = D1;

const int motorPins1[4] = {IN1_A, IN2_A, IN3_A, IN4_A};
const int motorPins2[4] = {IN1_B, IN2_B, IN3_B, IN4_B};

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
MotorState motor1, motor2;
int activeMotor = 1;  // 1=motor1, 2=motor2, 3=global pause
unsigned long globalPauseStart = 0;
unsigned long cyclesCompleted = 0;
bool systemEnabled = true;
bool timerActive = false;
unsigned long runUntilMs = 0;
String lastStopReason = "boot";
bool usingApFallback = false;

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
  config.numGiri1 = 2;
  config.speed1 = 3;
  config.numGiri2 = 2;
  config.speed2 = 3;
  config.globalDelay = 3;
  config.runMinutes = 0;
}

void sanitizeConfig() {
  config.numGiri1 = clampInt(config.numGiri1, 0, 10);
  config.speed1 = clampInt(config.speed1, 1, 5);
  config.numGiri2 = clampInt(config.numGiri2, 0, 10);
  config.speed2 = clampInt(config.speed2, 1, 5);
  config.globalDelay = clampInt(config.globalDelay, 1, 60);
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

void stepMotor(int step, const int pins[4]) {
  digitalWrite(pins[0], seq[step][0]);
  digitalWrite(pins[1], seq[step][1]);
  digitalWrite(pins[2], seq[step][2]);
  digitalWrite(pins[3], seq[step][3]);
}

void releaseMotor(const int pins[4]) {
  for (int i = 0; i < 4; i++) {
    digitalWrite(pins[i], LOW);
  }
}

void releaseAllMotors() {
  releaseMotor(motorPins1);
  releaseMotor(motorPins2);
}

void resetMotorState(MotorState& m) {
  m.currentState = MOVING_CW;
  m.lastStepTime = 0;
  m.pauseStartTime = 0;
  m.currentStep = 0;
  m.stepsToDo = 0;
}

unsigned long getTimerRemainingMs(unsigned long now) {
  if (!timerActive) return 0;
  if (now >= runUntilMs) return 0;
  return runUntilMs - now;
}

void stopSystem(const char* reason) {
  systemEnabled = false;
  timerActive = false;
  resetMotorState(motor1);
  resetMotorState(motor2);
  activeMotor = 1;
  releaseAllMotors();
  lastStopReason = reason;
}

void startSystem(unsigned long now, int minutes) {
  systemEnabled = true;
  resetMotorState(motor1);
  resetMotorState(motor2);
  activeMotor = 1;

  if (minutes > 0) {
    timerActive = true;
    runUntilMs = now + (unsigned long)minutes * 60000UL;
  } else {
    timerActive = false;
    runUntilMs = 0;
  }

  lastStopReason = "running";
}

void applyConfigFromRequest() {
  if (server.hasArg("giri1")) {
    config.numGiri1 = clampInt(server.arg("giri1").toInt(), 0, 10);
  }
  if (server.hasArg("speed1")) {
    config.speed1 = clampInt(server.arg("speed1").toInt(), 1, 5);
  }
  if (server.hasArg("giri2")) {
    config.numGiri2 = clampInt(server.arg("giri2").toInt(), 0, 10);
  }
  if (server.hasArg("speed2")) {
    config.speed2 = clampInt(server.arg("speed2").toInt(), 1, 5);
  }
  if (server.hasArg("gdelay")) {
    config.globalDelay = clampInt(server.arg("gdelay").toInt(), 1, 60);
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
  msg += "\"name\":\"watch-winder-double\",";
  msg += "\"message\":\"Use /api/status and /api/config\"";
  msg += "}";
  sendCorsHeaders();
  server.send(200, "application/json", msg);
}

void handleApiStatus() {
  unsigned long now = millis();
  String json = "{";
  json += "\"mode\":\"double\",";
  json += "\"config\":{";
  json += "\"giri1\":" + String(config.numGiri1) + ",";
  json += "\"speed1\":" + String(config.speed1) + ",";
  json += "\"giri2\":" + String(config.numGiri2) + ",";
  json += "\"speed2\":" + String(config.speed2) + ",";
  json += "\"gdelay\":" + String(config.globalDelay) + ",";
  json += "\"runMinutes\":" + String(config.runMinutes);
  json += "},";
  json += "\"runtime\":{";
  json += "\"systemEnabled\":" + String(systemEnabled ? "true" : "false") + ",";
  json += "\"timerActive\":" + String(timerActive ? "true" : "false") + ",";
  json += "\"timerRemainingMs\":" + String(getTimerRemainingMs(now)) + ",";
  json += "\"activeMotor\":" + String(activeMotor) + ",";
  json += "\"motor1State\":\"" + String(stateToText(motor1.currentState)) + "\",";
  json += "\"motor2State\":\"" + String(stateToText(motor2.currentState)) + "\",";
  json += "\"motor1StepsRemaining\":" + String(motor1.stepsToDo) + ",";
  json += "\"motor2StepsRemaining\":" + String(motor2.stepsToDo) + ",";
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
  startSystem(millis(), minutes);
  handleApiStatus();
}

void handleApiStop() {
  if (server.method() != HTTP_POST) {
    sendCorsHeaders();
    server.send(405, "application/json", "{\"error\":\"POST required\"}");
    return;
  }

  stopSystem("api_stop");
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

void runMotor(unsigned long now, const int pins[4], int numGiri, int speed, MotorState& m) {
  const unsigned long effectiveStepDelay = (unsigned long)max(speed, MIN_EFFECTIVE_STEP_DELAY_MS);

  switch (m.currentState) {
    case MOVING_CW:
      if (m.stepsToDo == 0) {
        m.stepsToDo = (unsigned long)STEPS_PER_REV * (unsigned long)numGiri * 2UL;
        m.currentStep = 0;
      }
      if (m.stepsToDo > 0 && now - m.lastStepTime >= effectiveStepDelay) {
        int budget = 0;
        while (m.stepsToDo > 0 && now - m.lastStepTime >= effectiveStepDelay && budget < 4) {
          m.lastStepTime += effectiveStepDelay;
          stepMotor(m.currentStep % 8, pins);
          m.currentStep++;
          m.stepsToDo--;
          budget++;
        }
        if (m.stepsToDo == 0) {
          releaseMotor(pins);
          m.currentState = PAUSE_SHORT;
          m.pauseStartTime = now;
        }
      }
      break;

    case PAUSE_SHORT:
      if (now - m.pauseStartTime >= 500) {
        m.currentState = MOVING_CCW;
        m.stepsToDo = 0;
      }
      break;

    case MOVING_CCW:
      if (m.stepsToDo == 0) {
        m.stepsToDo = (unsigned long)STEPS_PER_REV * (unsigned long)numGiri * 2UL;
        m.currentStep = 0;
      }
      if (m.stepsToDo > 0 && now - m.lastStepTime >= effectiveStepDelay) {
        int budget = 0;
        while (m.stepsToDo > 0 && now - m.lastStepTime >= effectiveStepDelay && budget < 4) {
          m.lastStepTime += effectiveStepDelay;
          stepMotor((8 - (m.currentStep % 8)) % 8, pins);
          m.currentStep++;
          m.stepsToDo--;
          budget++;
        }
        if (m.stepsToDo == 0) {
          releaseMotor(pins);
          m.currentState = PAUSE_LONG;
          m.pauseStartTime = now;
        }
      }
      break;

    case PAUSE_LONG:
      break;
  }
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(motorPins1[i], OUTPUT);
    pinMode(motorPins2[i], OUTPUT);
  }

  loadConfig();
  startNetwork();
  startServer();

  if (config.runMinutes > 0) {
    startSystem(millis(), config.runMinutes);
  }
}

void loop() {
  unsigned long now = millis();

  server.handleClient();

  if (!systemEnabled) {
    return;
  }

  if (timerActive && now >= runUntilMs) {
    stopSystem("timer_elapsed");
    return;
  }

  if (activeMotor == 1) {
    runMotor(now, motorPins1, config.numGiri1, config.speed1, motor1);
    if (motor1.currentState == PAUSE_LONG) {
      motor1.currentState = MOVING_CW;
      activeMotor = 2;
    }
  } else if (activeMotor == 2) {
    runMotor(now, motorPins2, config.numGiri2, config.speed2, motor2);
    if (motor2.currentState == PAUSE_LONG) {
      motor2.currentState = MOVING_CW;
      activeMotor = 3;
      globalPauseStart = now;
      cyclesCompleted++;
    }
  } else {
    if (now - globalPauseStart >= (unsigned long)config.globalDelay * 60000UL) {
      activeMotor = 1;
    }
  }
}
