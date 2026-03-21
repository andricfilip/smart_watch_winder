#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <string.h>

// ===== NETWORK SETTINGS =====
const char* STA_SSID = "YOUR_WIFI_SSID";
const char* STA_PASSWORD = "YOUR_WIFI_PASSWORD";
const bool ENABLE_AP_FALLBACK = true;
const char* AP_SSID = "WatchWinder-Setup";
const char* AP_PASSWORD = "ChangeMe123!";
const unsigned long STA_CONNECT_TIMEOUT_MS = 15000;

// ===== PINOUT =====
const int IN1 = D5;
const int IN2 = D6;
const int IN3 = D7;
const int IN4 = D8;
const int motorPins[4] = {IN1, IN2, IN3, IN4};

// ===== ENGINE =====
const int STEPS_PER_REV = 2048;
const unsigned long DAY_MS = 86400000UL;
const unsigned long HOUR_MS = 3600000UL;
const unsigned long MIN_PAUSE_MS = 5UL * 60000UL;
const unsigned long MAX_PAUSE_MS = 10UL * 60000UL;
const unsigned long MIN_BURST_MS = 1UL * 60000UL;
const unsigned long MAX_BURST_MS = 3UL * 60000UL;
const unsigned long JAM_TIMEOUT_MS = 12000UL;
const unsigned long RUNTIME_PERSIST_MS = 15UL * 60000UL;
const int MIN_EFFECTIVE_STEP_DELAY_MS = 3;

enum DirectionMode { DIR_CW = 0, DIR_CCW = 1, DIR_BIDIR = 2 };
enum WinderMode { MODE_STANDARD = 0, MODE_SMART = 1 };
enum SystemState { SYS_IDLE = 0, SYS_RUNNING = 1, SYS_PAUSED = 2, SYS_ERROR = 3, SYS_TIMER_DONE = 4 };
enum ErrorCode { ERR_NONE = 0, ERR_JAM_DETECTED = 1, ERR_TIMER_DONE = 2, ERR_INVALID_CONFIG = 3 };

const char* stateToText(SystemState s) {
  switch (s) {
    case SYS_IDLE:
      return "IDLE";
    case SYS_RUNNING:
      return "RUNNING";
    case SYS_PAUSED:
      return "PAUSED";
    case SYS_ERROR:
      return "ERROR";
    case SYS_TIMER_DONE:
      return "TIMER_DONE";
    default:
      return "UNKNOWN";
  }
}

const char* errorToText(ErrorCode e) {
  switch (e) {
    case ERR_NONE:
      return "NONE";
    case ERR_JAM_DETECTED:
      return "JAM_DETECTED";
    case ERR_TIMER_DONE:
      return "TIMER_DONE";
    case ERR_INVALID_CONFIG:
      return "INVALID_CONFIG";
    default:
      return "UNKNOWN";
  }
}

const char* directionToText(DirectionMode d) {
  switch (d) {
    case DIR_CW:
      return "CW";
    case DIR_CCW:
      return "CCW";
    default:
      return "BIDIR";
  }
}

const char* modeToText(WinderMode m) {
  return m == MODE_SMART ? "SMART" : "STANDARD";
}

DirectionMode parseDirection(String value) {
  value.trim();
  value.toUpperCase();
  if (value == "CW") return DIR_CW;
  if (value == "CCW") return DIR_CCW;
  return DIR_BIDIR;
}

WinderMode parseMode(String value) {
  value.trim();
  value.toUpperCase();
  if (value == "SMART") return MODE_SMART;
  return MODE_STANDARD;
}

struct Config {
  int targetTPD;
  int direction;
  int speed;
  int mode;
  int activeStartHour;
  int activeEndHour;
  int smartSwitchRotations;
  bool quietMode;
  int quietStartHour;
  int quietEndHour;
  int nightSpeedCap;
  int runMinutes;
  int legacyDelay;
};

struct RuntimeSnapshot {
  uint32_t stepsToday;
  uint32_t totalSteps;
  uint32_t dayElapsedMs;
  uint8_t hourCursor;
  uint32_t hourBuckets[24];
  uint32_t cyclesCompleted;
  uint16_t smartRotAcc;
};

struct PersistRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t valid;
  uint32_t seq;
  Config cfg;
  RuntimeSnapshot snap;
  uint32_t crc;
};

const uint32_t CONFIG_MAGIC = 0x57574E44;
const uint16_t CONFIG_VERSION = 4;
const uint16_t RECORD_VALID = 0xA55A;

struct Runtime {
  bool burstActive;
  bool burstDirectionCw;
  bool nextBidirCw;
  unsigned long pauseUntilMs;
  unsigned long burstStartMs;
  unsigned long burstTargetSteps;
  unsigned long burstStepsDone;
  unsigned long stepsToday;
  unsigned long totalSteps;
  unsigned long nextStepAtMs;
  unsigned long lastStepMs;
  unsigned long cyclesCompleted;
  int phaseIndex;
  unsigned int smartRotationAccumulator;
};

ESP8266WebServer server(80);

Config config;
Config lastSavedConfig;
bool hasLastSavedConfig = false;
Runtime rt;

bool usingApFallback = false;
bool motorEnabled = true;
bool timerActive = false;
unsigned long runUntilMs = 0;
String lastStopReason = "boot";
SystemState systemState = SYS_IDLE;
ErrorCode errorCode = ERR_NONE;
unsigned long dayStartMs = 0;
unsigned long lastPersistMs = 0;
int hourCursor = 0;
uint32_t hourBuckets[24];
uint32_t persistSeq = 0;
int persistSlot = 0;

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
      crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
    }
  }
  return ~crc;
}

void setDefaultConfig() {
  config.targetTPD = 650;
  config.direction = DIR_BIDIR;
  config.speed = 3;
  config.mode = MODE_STANDARD;
  config.activeStartHour = 0;
  config.activeEndHour = 23;
  config.smartSwitchRotations = 10;
  config.quietMode = true;
  config.quietStartHour = 22;
  config.quietEndHour = 7;
  config.nightSpeedCap = 2;
  config.runMinutes = 0;
  config.legacyDelay = 10;
}

void sanitizeConfig() {
  config.targetTPD = clampInt(config.targetTPD, 500, 1000);
  config.direction = clampInt(config.direction, DIR_CW, DIR_BIDIR);
  config.speed = clampInt(config.speed, 1, 5);
  config.mode = clampInt(config.mode, MODE_STANDARD, MODE_SMART);
  config.activeStartHour = clampInt(config.activeStartHour, 0, 23);
  config.activeEndHour = clampInt(config.activeEndHour, 0, 23);
  config.smartSwitchRotations = clampInt(config.smartSwitchRotations, 2, 100);
  config.quietStartHour = clampInt(config.quietStartHour, 0, 23);
  config.quietEndHour = clampInt(config.quietEndHour, 0, 23);
  config.nightSpeedCap = clampInt(config.nightSpeedCap, 1, 5);
  config.runMinutes = clampInt(config.runMinutes, 0, 1440);
  config.legacyDelay = clampInt(config.legacyDelay, 1, 60);
}

unsigned int recordSize() { return (unsigned int)sizeof(PersistRecord); }
int slotAddress(int slot) { return slot * (int)recordSize(); }

void captureSnapshot(RuntimeSnapshot& snap, unsigned long now) {
  snap.stepsToday = rt.stepsToday;
  snap.totalSteps = rt.totalSteps;
  snap.dayElapsedMs = (uint32_t)(now - dayStartMs);
  snap.hourCursor = (uint8_t)hourCursor;
  for (int i = 0; i < 24; i++) snap.hourBuckets[i] = hourBuckets[i];
  snap.cyclesCompleted = rt.cyclesCompleted;
  snap.smartRotAcc = rt.smartRotationAccumulator;
}

void applySnapshot(const RuntimeSnapshot& snap, unsigned long now) {
  dayStartMs = now - min((unsigned long)snap.dayElapsedMs, DAY_MS - 1UL);
  hourCursor = snap.hourCursor % 24;
  for (int i = 0; i < 24; i++) hourBuckets[i] = snap.hourBuckets[i];
  rt.stepsToday = snap.stepsToday;
  rt.totalSteps = snap.totalSteps;
  rt.cyclesCompleted = snap.cyclesCompleted;
  rt.smartRotationAccumulator = snap.smartRotAcc;
}

bool readRecordSlot(int slot, PersistRecord& out) {
  EEPROM.get(slotAddress(slot), out);
  if (out.magic != CONFIG_MAGIC || out.version != CONFIG_VERSION || out.valid != RECORD_VALID) return false;
  uint32_t calc = crc32(reinterpret_cast<const uint8_t*>(&out.cfg), sizeof(out.cfg) + sizeof(out.snap));
  return calc == out.crc;
}

void writeRecordSlot(int slot, const PersistRecord& rec) {
  EEPROM.put(slotAddress(slot), rec);
  EEPROM.commit();
}

bool configChanged() {
  if (!hasLastSavedConfig) return true;
  return memcmp(&lastSavedConfig, &config, sizeof(Config)) != 0;
}

void savePersistent(bool forceRuntime) {
  unsigned long now = millis();
  bool writeCfg = configChanged();
  bool writeRt = forceRuntime || (now - lastPersistMs >= RUNTIME_PERSIST_MS);
  if (!writeCfg && !writeRt) return;

  PersistRecord rec;
  rec.magic = CONFIG_MAGIC;
  rec.version = CONFIG_VERSION;
  rec.valid = RECORD_VALID;
  rec.seq = ++persistSeq;
  rec.cfg = config;
  captureSnapshot(rec.snap, now);
  rec.crc = crc32(reinterpret_cast<const uint8_t*>(&rec.cfg), sizeof(rec.cfg) + sizeof(rec.snap));

  persistSlot = 1 - persistSlot;
  writeRecordSlot(persistSlot, rec);
  lastSavedConfig = config;
  hasLastSavedConfig = true;
  lastPersistMs = now;
}

void loadPersistent() {
  EEPROM.begin(recordSize() * 2U);
  PersistRecord r0;
  PersistRecord r1;
  bool v0 = readRecordSlot(0, r0);
  bool v1 = readRecordSlot(1, r1);

  if (!v0 && !v1) {
    setDefaultConfig();
    sanitizeConfig();
    dayStartMs = millis();
    savePersistent(true);
    return;
  }

  PersistRecord chosen = r0;
  int chosenSlot = 0;
  if (!v0 || (v1 && r1.seq > r0.seq)) {
    chosen = r1;
    chosenSlot = 1;
  }

  config = chosen.cfg;
  sanitizeConfig();
  applySnapshot(chosen.snap, millis());
  persistSeq = chosen.seq;
  persistSlot = chosenSlot;
  lastSavedConfig = config;
  hasLastSavedConfig = true;
}

bool isHourInRange(int h, int start, int end) {
  if (start <= end) return h >= start && h <= end;
  return h >= start || h <= end;
}

int currentHour(unsigned long now) {
  return (int)(((now - dayStartMs) / HOUR_MS) % 24UL);
}

bool isQuietHour(unsigned long now) {
  if (!config.quietMode) return false;
  return isHourInRange(currentHour(now), config.quietStartHour, config.quietEndHour);
}

bool isActiveHour(unsigned long now) {
  return isHourInRange(currentHour(now), config.activeStartHour, config.activeEndHour);
}

void resetDailyProgress(unsigned long now) {
  dayStartMs = now;
  hourCursor = 0;
  for (int i = 0; i < 24; i++) hourBuckets[i] = 0;
  rt.stepsToday = 0;
  rt.cyclesCompleted = 0;
  rt.smartRotationAccumulator = 0;
}

void rollHourlyBuckets(unsigned long now) {
  if (dayStartMs == 0) dayStartMs = now;
  if (now - dayStartMs >= DAY_MS) {
    resetDailyProgress(now);
  }

  int target = currentHour(now);
  while (hourCursor != target) {
    hourCursor = (hourCursor + 1) % 24;
    hourBuckets[hourCursor] = 0;
  }
}

unsigned long targetStepsPerDay() { return (unsigned long)config.targetTPD * (unsigned long)STEPS_PER_REV; }

unsigned long speedToDelayMs(int speed) {
  switch (speed) {
    case 1:
      return 7;
    case 2:
      return 6;
    case 3:
      return 5;
    case 4:
      return 4;
    default:
      return 3;
  }
}

float driftPercent(unsigned long now) {
  unsigned long elapsed = max(1UL, now - dayStartMs);
  float expected = ((float)targetStepsPerDay() * (float)elapsed) / (float)DAY_MS;
  return ((float)rt.stepsToday - expected) * 100.0f / (float)targetStepsPerDay();
}

unsigned long computeBurstMs() {
  int minutes = clampInt(1 + (config.targetTPD - 500) / 250, 1, 3);
  return (unsigned long)minutes * 60000UL;
}

unsigned long computePauseMs(float driftPct) {
  int minutes = clampInt(10 - (config.targetTPD - 500) / 100, 5, 10);
  if (driftPct < -3.0f) minutes = max(5, minutes - 2);
  if (driftPct > 3.0f) minutes = min(10, minutes + 2);
  unsigned long result = (unsigned long)minutes * 60000UL;
  if (result < MIN_PAUSE_MS) result = MIN_PAUSE_MS;
  if (result > MAX_PAUSE_MS) result = MAX_PAUSE_MS;
  return result;
}

unsigned long computeBurstSteps(float driftPct) {
  float rotationsPerHour = (float)config.targetTPD / 24.0f;
  float baseRot = rotationsPerHour / 6.0f;
  float adjust = 1.0f;
  if (driftPct < -3.0f) adjust = 1.25f;
  if (driftPct > 3.0f) adjust = 0.8f;
  unsigned long steps = (unsigned long)(baseRot * adjust * (float)STEPS_PER_REV);
  unsigned long minSteps = (unsigned long)STEPS_PER_REV / 3UL;
  unsigned long maxSteps = (unsigned long)STEPS_PER_REV * 3UL;
  if (steps < minSteps) steps = minSteps;
  if (steps > maxSteps) steps = maxSteps;
  return steps;
}

void stepMotorPhase(int phase) {
  digitalWrite(IN1, seq[phase][0]);
  digitalWrite(IN2, seq[phase][1]);
  digitalWrite(IN3, seq[phase][2]);
  digitalWrite(IN4, seq[phase][3]);
}

void releaseMotor() {
  for (int i = 0; i < 4; i++) digitalWrite(motorPins[i], LOW);
}

void setError(ErrorCode code, const char* reason) {
  errorCode = code;
  systemState = SYS_ERROR;
  motorEnabled = false;
  timerActive = false;
  rt.burstActive = false;
  releaseMotor();
  lastStopReason = reason;
  savePersistent(true);
}

void initRuntime(unsigned long now) {
  rt.burstActive = false;
  rt.burstDirectionCw = true;
  rt.nextBidirCw = true;
  rt.pauseUntilMs = now;
  rt.burstStartMs = 0;
  rt.burstTargetSteps = 0;
  rt.burstStepsDone = 0;
  rt.nextStepAtMs = now;
  rt.lastStepMs = now;
  rt.phaseIndex = 0;
}

unsigned long getTimerRemainingMs(unsigned long now) {
  if (!timerActive) return 0;
  if (now >= runUntilMs) return 0;
  return runUntilMs - now;
}

void stopMotorSession(const char* reason) {
  motorEnabled = false;
  timerActive = false;
  systemState = SYS_IDLE;
  rt.burstActive = false;
  releaseMotor();
  lastStopReason = reason;
  savePersistent(true);
}

void startMotorSession(unsigned long now, int minutes) {
  errorCode = ERR_NONE;
  motorEnabled = true;
  systemState = SYS_RUNNING;
  initRuntime(now);
  if (minutes > 0) {
    timerActive = true;
    runUntilMs = now + (unsigned long)minutes * 60000UL;
  } else {
    timerActive = false;
    runUntilMs = 0;
  }
  lastStopReason = "running";
  savePersistent(true);
}

void sendCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

int findJsonKey(const String& body, const char* key) {
  String token = "\"" + String(key) + "\"";
  return body.indexOf(token);
}

bool parseJsonInt(const String& body, const char* key, int& outValue) {
  int keyPos = findJsonKey(body, key);
  if (keyPos < 0) return false;
  int colon = body.indexOf(':', keyPos);
  if (colon < 0) return false;
  int start = colon + 1;
  while (start < body.length() && (body[start] == ' ' || body[start] == '"')) start++;
  int end = start;
  if (end < body.length() && body[end] == '-') end++;
  while (end < body.length() && isDigit(body[end])) end++;
  if (end <= start) return false;
  outValue = body.substring(start, end).toInt();
  return true;
}

bool parseJsonString(const String& body, const char* key, String& outValue) {
  int keyPos = findJsonKey(body, key);
  if (keyPos < 0) return false;
  int colon = body.indexOf(':', keyPos);
  if (colon < 0) return false;
  int q1 = body.indexOf('"', colon + 1);
  if (q1 < 0) return false;
  int q2 = body.indexOf('"', q1 + 1);
  if (q2 < 0) return false;
  outValue = body.substring(q1 + 1, q2);
  return true;
}

bool parseJsonActiveHours(const String& body, int& startHour, int& endHour) {
  int keyPos = findJsonKey(body, "activeHours");
  if (keyPos < 0) return false;
  int b1 = body.indexOf('[', keyPos);
  int b2 = body.indexOf(']', keyPos);
  if (b1 < 0 || b2 < 0 || b2 <= b1) return false;
  String c = body.substring(b1 + 1, b2);
  int comma = c.indexOf(',');
  if (comma < 0) return false;
  startHour = c.substring(0, comma).toInt();
  endHour = c.substring(comma + 1).toInt();
  return true;
}

unsigned long legacyTpdFromGiriDelay(int giri, int delayMin) {
  int fg = clampInt(giri, 1, 10);
  int fd = clampInt(delayMin, 1, 60);
  float tpd = (float)(fg * 2) * (1440.0f / (float)fd);
  return (unsigned long)clampInt((int)(tpd + 0.5f), 500, 1000);
}

void applyConfigFromRequest() {
  String body = server.hasArg("plain") ? server.arg("plain") : "";
  int v = 0;
  String s;

  if (server.hasArg("targetTPD")) config.targetTPD = clampInt(server.arg("targetTPD").toInt(), 500, 1000);
  else if (parseJsonInt(body, "targetTPD", v)) config.targetTPD = clampInt(v, 500, 1000);

  if (server.hasArg("direction")) config.direction = parseDirection(server.arg("direction"));
  else if (parseJsonString(body, "direction", s)) config.direction = parseDirection(s);

  if (server.hasArg("speed")) config.speed = clampInt(server.arg("speed").toInt(), 1, 5);
  else if (parseJsonInt(body, "speed", v)) config.speed = clampInt(v, 1, 5);

  if (server.hasArg("mode")) config.mode = parseMode(server.arg("mode"));
  else if (parseJsonString(body, "mode", s)) config.mode = parseMode(s);

  if (server.hasArg("runMinutes")) config.runMinutes = clampInt(server.arg("runMinutes").toInt(), 0, 1440);
  else if (parseJsonInt(body, "runMinutes", v)) config.runMinutes = clampInt(v, 0, 1440);

  if (server.hasArg("quietMode")) config.quietMode = server.arg("quietMode") != "0";
  else if (parseJsonInt(body, "quietMode", v)) config.quietMode = v != 0;

  if (server.hasArg("quietStartHour")) config.quietStartHour = clampInt(server.arg("quietStartHour").toInt(), 0, 23);
  else if (parseJsonInt(body, "quietStartHour", v)) config.quietStartHour = clampInt(v, 0, 23);

  if (server.hasArg("quietEndHour")) config.quietEndHour = clampInt(server.arg("quietEndHour").toInt(), 0, 23);
  else if (parseJsonInt(body, "quietEndHour", v)) config.quietEndHour = clampInt(v, 0, 23);

  if (server.hasArg("nightSpeedCap")) config.nightSpeedCap = clampInt(server.arg("nightSpeedCap").toInt(), 1, 5);
  else if (parseJsonInt(body, "nightSpeedCap", v)) config.nightSpeedCap = clampInt(v, 1, 5);

  if (server.hasArg("smartSwitchRotations")) config.smartSwitchRotations = clampInt(server.arg("smartSwitchRotations").toInt(), 2, 100);
  else if (parseJsonInt(body, "smartSwitchRotations", v)) config.smartSwitchRotations = clampInt(v, 2, 100);

  if (server.hasArg("activeStartHour")) config.activeStartHour = clampInt(server.arg("activeStartHour").toInt(), 0, 23);
  else if (parseJsonInt(body, "activeStartHour", v)) config.activeStartHour = clampInt(v, 0, 23);

  if (server.hasArg("activeEndHour")) config.activeEndHour = clampInt(server.arg("activeEndHour").toInt(), 0, 23);
  else if (parseJsonInt(body, "activeEndHour", v)) config.activeEndHour = clampInt(v, 0, 23);

  int ahStart = 0;
  int ahEnd = 23;
  if (parseJsonActiveHours(body, ahStart, ahEnd)) {
    config.activeStartHour = clampInt(ahStart, 0, 23);
    config.activeEndHour = clampInt(ahEnd, 0, 23);
  }

  bool hasLegacyDelay = false;
  int legacyDelay = config.legacyDelay;
  if (server.hasArg("delay")) {
    hasLegacyDelay = true;
    legacyDelay = server.arg("delay").toInt();
  } else if (parseJsonInt(body, "delay", v)) {
    hasLegacyDelay = true;
    legacyDelay = v;
  }
  if (hasLegacyDelay) config.legacyDelay = clampInt(legacyDelay, 1, 60);

  bool hasGiri = false;
  int giri = 2;
  if (server.hasArg("giri")) {
    hasGiri = true;
    giri = server.arg("giri").toInt();
  } else if (parseJsonInt(body, "giri", v)) {
    hasGiri = true;
    giri = v;
  }

  if (hasGiri || hasLegacyDelay) {
    config.targetTPD = legacyTpdFromGiriDelay(hasGiri ? giri : 2, config.legacyDelay);
  }

  sanitizeConfig();
}

float rotationsToday() { return (float)rt.stepsToday / (float)STEPS_PER_REV; }
float rotationsLastHour() {
  uint32_t total = 0;
  for (int i = 0; i < 24; i++) total += hourBuckets[i];
  return (float)total / (float)STEPS_PER_REV;
}

float currentTPD(unsigned long now) {
  unsigned long elapsed = max(1UL, now - dayStartMs);
  return rotationsToday() * ((float)DAY_MS / (float)elapsed);
}

void handleRoot() {
  String msg = "{";
  msg += "\"name\":\"watch-winder-single\",";
  msg += "\"message\":\"Use /api/status, /api/stats, /api/health\"";
  msg += "}";
  sendCorsHeaders();
  server.send(200, "application/json", msg);
}

void handleApiStatus() {
  unsigned long now = millis();
  rollHourlyBuckets(now);

  float drift = driftPercent(now);
  float progress = min(100.0f, rotationsToday() * 100.0f / (float)config.targetTPD);

  String json = "{";
  json += "\"mode\":\"single\",";
  json += "\"config\":{";
  json += "\"targetTPD\":" + String(config.targetTPD) + ",";
  json += "\"direction\":\"" + String(directionToText((DirectionMode)config.direction)) + "\",";
  json += "\"mode\":\"" + String(modeToText((WinderMode)config.mode)) + "\",";
  json += "\"speed\":" + String(config.speed) + ",";
  json += "\"activeHours\":[" + String(config.activeStartHour) + "," + String(config.activeEndHour) + "],";
  json += "\"smartSwitchRotations\":" + String(config.smartSwitchRotations) + ",";
  json += "\"quietMode\":" + String(config.quietMode ? "true" : "false") + ",";
  json += "\"quietStartHour\":" + String(config.quietStartHour) + ",";
  json += "\"quietEndHour\":" + String(config.quietEndHour) + ",";
  json += "\"nightSpeedCap\":" + String(config.nightSpeedCap) + ",";
  json += "\"runMinutes\":" + String(config.runMinutes);
  json += "},";

  json += "\"runtime\":{";
  json += "\"motorEnabled\":" + String(motorEnabled ? "true" : "false") + ",";
  json += "\"timerActive\":" + String(timerActive ? "true" : "false") + ",";
  json += "\"timerRemainingMs\":" + String(getTimerRemainingMs(now)) + ",";
  json += "\"motorState\":\"" + String(stateToText(systemState)) + "\",";
  json += "\"errorCode\":\"" + String(errorToText(errorCode)) + "\",";
  json += "\"rotationsToday\":" + String(rotationsToday(), 2) + ",";
  json += "\"rotationsLastHour\":" + String(rotationsLastHour(), 2) + ",";
  json += "\"currentTPD\":" + String(currentTPD(now), 2) + ",";
  json += "\"driftPercent\":" + String(drift, 2) + ",";
  json += "\"currentTPDProgress\":" + String(progress, 2) + ",";
  json += "\"remainingRotations\":" + String(max(0.0f, (float)config.targetTPD - rotationsToday()), 2) + ",";
  json += "\"cyclesCompleted\":" + String(rt.cyclesCompleted) + ",";
  json += "\"uptime\":" + String(now);
  json += "},";

  json += "\"network\":{";
  json += "\"mode\":\"" + String(usingApFallback ? "AP" : "STA") + "\",";
  json += "\"ip\":\"" + (usingApFallback ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\",";
  json += "\"ssid\":\"" + String(usingApFallback ? AP_SSID : STA_SSID) + "\",";
  json += "\"rssi\":" + String(usingApFallback ? 0 : WiFi.RSSI());
  json += "}";

  json += "}";
  sendCorsHeaders();
  server.send(200, "application/json", json);
}

void handleApiStats() {
  unsigned long now = millis();
  String json = "{";
  json += "\"rotationsToday\":" + String(rotationsToday(), 2) + ",";
  json += "\"rotationsLastHour\":" + String(rotationsLastHour(), 2) + ",";
  json += "\"currentTPD\":" + String(currentTPD(now), 2) + ",";
  json += "\"driftPercent\":" + String(driftPercent(now), 2) + ",";
  json += "\"motorState\":\"" + String(stateToText(systemState)) + "\",";
  json += "\"errorCode\":\"" + String(errorToText(errorCode)) + "\",";
  json += "\"uptime\":" + String(now);
  json += "}";
  sendCorsHeaders();
  server.send(200, "application/json", json);
}

void handleApiHealth() {
  unsigned long now = millis();
  String json = "{";
  json += "\"state\":\"" + String(stateToText(systemState)) + "\",";
  json += "\"errorCode\":\"" + String(errorToText(errorCode)) + "\",";
  json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"heapFree\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"uptime\":" + String(now);
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
  savePersistent(false);
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
  } else if (server.hasArg("plain")) {
    int parsed = 0;
    if (parseJsonInt(server.arg("plain"), "runMinutes", parsed)) {
      minutes = clampInt(parsed, 0, 1440);
    }
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
  server.on("/api/stats", HTTP_GET, handleApiStats);
  server.on("/api/health", HTTP_GET, handleApiHealth);
  server.on("/api/config", HTTP_POST, handleApiConfig);
  server.on("/api/start", HTTP_POST, handleApiStart);
  server.on("/api/stop", HTTP_POST, handleApiStop);

  server.on("/api/status", HTTP_OPTIONS, handleOptions);
  server.on("/api/stats", HTTP_OPTIONS, handleOptions);
  server.on("/api/health", HTTP_OPTIONS, handleOptions);
  server.on("/api/config", HTTP_OPTIONS, handleOptions);
  server.on("/api/start", HTTP_OPTIONS, handleOptions);
  server.on("/api/stop", HTTP_OPTIONS, handleOptions);

  server.onNotFound(handleRoot);
  server.begin();
}

void chooseBurstDirection() {
  if ((DirectionMode)config.direction == DIR_CW) {
    rt.burstDirectionCw = true;
    return;
  }
  if ((DirectionMode)config.direction == DIR_CCW) {
    rt.burstDirectionCw = false;
    return;
  }

  if ((WinderMode)config.mode == MODE_SMART) {
    if (rt.smartRotationAccumulator >= (unsigned int)config.smartSwitchRotations) {
      rt.burstDirectionCw = !rt.burstDirectionCw;
      rt.smartRotationAccumulator = 0;
    }
    return;
  }

  rt.burstDirectionCw = rt.nextBidirCw;
  rt.nextBidirCw = !rt.nextBidirCw;
}

bool runScheduler(unsigned long now) {
  if (!isActiveHour(now)) return false;
  if (rt.stepsToday >= targetStepsPerDay()) return false;

  float drift = driftPercent(now);

  if (!rt.burstActive) {
    if (now < rt.pauseUntilMs) return false;

    rt.burstActive = true;
    rt.burstStartMs = now;
    rt.burstStepsDone = 0;
    unsigned long planned = computeBurstSteps(drift);
    unsigned long remaining = targetStepsPerDay() - rt.stepsToday;
    rt.burstTargetSteps = min(planned, remaining);
    chooseBurstDirection();
    rt.nextStepAtMs = now;
    rt.lastStepMs = now;
  }

  int effectiveSpeed = config.speed;
  if (isQuietHour(now)) {
    effectiveSpeed = min(effectiveSpeed, config.nightSpeedCap);
  }

  unsigned long baseDelay = max((unsigned long)MIN_EFFECTIVE_STEP_DELAY_MS, speedToDelayMs(effectiveSpeed));

  int budget = 0;
  while (rt.burstActive && rt.burstStepsDone < rt.burstTargetSteps && (long)(now - rt.nextStepAtMs) >= 0 && budget < 6) {
    float progress = rt.burstTargetSteps == 0 ? 1.0f : (float)rt.burstStepsDone / (float)rt.burstTargetSteps;
    unsigned long rampAdd = (progress < 0.15f || progress > 0.85f) ? 2UL : 0UL;

    if (rt.burstDirectionCw) rt.phaseIndex = (rt.phaseIndex + 1) % 8;
    else rt.phaseIndex = (rt.phaseIndex + 7) % 8;

    stepMotorPhase(rt.phaseIndex);
    rt.burstStepsDone++;
    rt.stepsToday++;
    rt.totalSteps++;
    hourBuckets[hourCursor]++;
    rt.lastStepMs = now;
    rt.nextStepAtMs += baseDelay + rampAdd;
    budget++;
  }

  if (rt.burstActive && now - rt.lastStepMs > JAM_TIMEOUT_MS) {
    setError(ERR_JAM_DETECTED, "jam_timeout");
    return false;
  }

  unsigned long burstMs = computeBurstMs();
  if (burstMs < MIN_BURST_MS) burstMs = MIN_BURST_MS;
  if (burstMs > MAX_BURST_MS) burstMs = MAX_BURST_MS;

  bool finished = rt.burstStepsDone >= rt.burstTargetSteps;
  bool timedOut = now - rt.burstStartMs >= burstMs;
  bool reached = rt.stepsToday >= targetStepsPerDay();

  if (rt.burstActive && (finished || timedOut || reached)) {
    rt.burstActive = false;
    rt.cyclesCompleted++;
    releaseMotor();
    rt.smartRotationAccumulator += (unsigned int)(rt.burstStepsDone / (unsigned long)STEPS_PER_REV);
    rt.pauseUntilMs = now + computePauseMs(drift);
    return false;
  }

  return rt.burstActive;
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) pinMode(motorPins[i], OUTPUT);

  loadPersistent();
  if (dayStartMs == 0) dayStartMs = millis();
  initRuntime(millis());

  startNetwork();
  startServer();

  if (config.runMinutes > 0) {
    startMotorSession(millis(), config.runMinutes);
  } else {
    systemState = SYS_IDLE;
  }
}

void loop() {
  unsigned long now = millis();

  server.handleClient();
  rollHourlyBuckets(now);

  if (timerActive && now >= runUntilMs) {
    errorCode = ERR_TIMER_DONE;
    systemState = SYS_TIMER_DONE;
    stopMotorSession("timer_elapsed");
    return;
  }

  if (!motorEnabled) {
    if (systemState != SYS_ERROR && systemState != SYS_TIMER_DONE) {
      systemState = SYS_IDLE;
    }
    savePersistent(false);
    return;
  }

  if (errorCode != ERR_NONE) {
    systemState = SYS_ERROR;
    savePersistent(true);
    return;
  }

  bool needs = isActiveHour(now) && rt.stepsToday < targetStepsPerDay();
  if (!needs) {
    systemState = SYS_PAUSED;
    releaseMotor();
    savePersistent(false);
    return;
  }

  systemState = SYS_RUNNING;
  runScheduler(now);
  savePersistent(false);
}
