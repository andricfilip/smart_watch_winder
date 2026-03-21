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

// ===== MOTOR & SCHEDULER =====
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
const bool HAS_CURRENT_SENSOR = false;
const int CURRENT_SENSOR_PIN = A0;
const int CURRENT_JAM_THRESHOLD = 980;

enum DirectionMode { DIR_CW = 0, DIR_CCW = 1, DIR_BIDIR = 2 };
enum WinderMode { MODE_STANDARD = 0, MODE_SMART = 1 };
enum SystemState { SYS_IDLE = 0, SYS_RUNNING = 1, SYS_PAUSED = 2, SYS_ERROR = 3, SYS_TIMER_DONE = 4 };
enum ErrorCode {
  ERR_NONE = 0,
  ERR_JAM_DETECTED = 1,
  ERR_OVERCURRENT = 2,
  ERR_TIMER_DONE = 3,
  ERR_INVALID_CONFIG = 4
};
enum LogLevel { LOG_ERROR = 0, LOG_INFO = 1, LOG_DEBUG = 2 };

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
    case ERR_OVERCURRENT:
      return "OVERCURRENT";
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

struct MotorConfig {
  int targetTPD;
  int direction;
  int speed;
  int mode;
  int activeStartHour;
  int activeEndHour;
  int smartSwitchRotations;
};

struct Config {
  MotorConfig m1;
  MotorConfig m2;
  bool quietMode;
  int quietStartHour;
  int quietEndHour;
  int nightSpeedCap;
  int runMinutes;
  int logLevel;
  int legacyDelay;
};

struct RuntimeSnapshot {
  uint32_t stepsToday1;
  uint32_t stepsToday2;
  uint32_t totalSteps1;
  uint32_t totalSteps2;
  uint32_t dayElapsedMs;
  uint8_t hourCursor;
  uint32_t hourBuckets1[24];
  uint32_t hourBuckets2[24];
  uint32_t cycles1;
  uint32_t cycles2;
  uint16_t smartRotAcc1;
  uint16_t smartRotAcc2;
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

const uint32_t CONFIG_MAGIC = 0x57574E44;  // WWND
const uint16_t CONFIG_VERSION = 4;
const uint16_t RECORD_VALID = 0xA55A;

struct MotorRuntime {
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

MotorRuntime motor1, motor2;
bool usingApFallback = false;
bool systemEnabled = true;
bool timerActive = false;
unsigned long runUntilMs = 0;
String lastStopReason = "boot";
SystemState systemState = SYS_IDLE;
ErrorCode errorCode = ERR_NONE;
unsigned long dayStartMs = 0;
unsigned long lastPersistMs = 0;
unsigned long lastHourCheckMs = 0;
int hourCursor = 0;
uint32_t hourBuckets1[24];
uint32_t hourBuckets2[24];
int activeMotor = 1;
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

void logMessage(LogLevel level, const String& msg) {
  if (level <= (LogLevel)config.logLevel) {
    Serial.println(msg);
  }
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

void setDefaultMotorConfig(MotorConfig& mc) {
  mc.targetTPD = 650;
  mc.direction = DIR_BIDIR;
  mc.speed = 3;
  mc.mode = MODE_STANDARD;
  mc.activeStartHour = 0;
  mc.activeEndHour = 23;
  mc.smartSwitchRotations = 10;
}

void setDefaultConfig() {
  setDefaultMotorConfig(config.m1);
  setDefaultMotorConfig(config.m2);
  config.quietMode = true;
  config.quietStartHour = 22;
  config.quietEndHour = 7;
  config.nightSpeedCap = 2;
  config.runMinutes = 0;
  config.logLevel = LOG_INFO;
  config.legacyDelay = 10;
}

void sanitizeMotorConfig(MotorConfig& mc) {
  mc.targetTPD = clampInt(mc.targetTPD, 500, 1000);
  mc.direction = clampInt(mc.direction, DIR_CW, DIR_BIDIR);
  mc.speed = clampInt(mc.speed, 1, 5);
  mc.mode = clampInt(mc.mode, MODE_STANDARD, MODE_SMART);
  mc.activeStartHour = clampInt(mc.activeStartHour, 0, 23);
  mc.activeEndHour = clampInt(mc.activeEndHour, 0, 23);
  mc.smartSwitchRotations = clampInt(mc.smartSwitchRotations, 2, 100);
}

void sanitizeConfig() {
  sanitizeMotorConfig(config.m1);
  sanitizeMotorConfig(config.m2);
  config.quietStartHour = clampInt(config.quietStartHour, 0, 23);
  config.quietEndHour = clampInt(config.quietEndHour, 0, 23);
  config.nightSpeedCap = clampInt(config.nightSpeedCap, 1, 5);
  config.runMinutes = clampInt(config.runMinutes, 0, 1440);
  config.logLevel = clampInt(config.logLevel, LOG_ERROR, LOG_DEBUG);
  config.legacyDelay = clampInt(config.legacyDelay, 1, 60);
}

unsigned int recordSize() {
  return (unsigned int)sizeof(PersistRecord);
}

int slotAddress(int slot) {
  return slot * (int)recordSize();
}

void captureSnapshot(RuntimeSnapshot& snap, unsigned long now) {
  snap.stepsToday1 = motor1.stepsToday;
  snap.stepsToday2 = motor2.stepsToday;
  snap.totalSteps1 = motor1.totalSteps;
  snap.totalSteps2 = motor2.totalSteps;
  snap.dayElapsedMs = (uint32_t)(now - dayStartMs);
  snap.hourCursor = (uint8_t)hourCursor;
  for (int i = 0; i < 24; i++) {
    snap.hourBuckets1[i] = hourBuckets1[i];
    snap.hourBuckets2[i] = hourBuckets2[i];
  }
  snap.cycles1 = motor1.cyclesCompleted;
  snap.cycles2 = motor2.cyclesCompleted;
  snap.smartRotAcc1 = motor1.smartRotationAccumulator;
  snap.smartRotAcc2 = motor2.smartRotationAccumulator;
}

void applySnapshot(const RuntimeSnapshot& snap, unsigned long now) {
  dayStartMs = now - min((unsigned long)snap.dayElapsedMs, DAY_MS - 1UL);
  hourCursor = snap.hourCursor % 24;
  for (int i = 0; i < 24; i++) {
    hourBuckets1[i] = snap.hourBuckets1[i];
    hourBuckets2[i] = snap.hourBuckets2[i];
  }
  motor1.stepsToday = snap.stepsToday1;
  motor2.stepsToday = snap.stepsToday2;
  motor1.totalSteps = snap.totalSteps1;
  motor2.totalSteps = snap.totalSteps2;
  motor1.cyclesCompleted = snap.cycles1;
  motor2.cyclesCompleted = snap.cycles2;
  motor1.smartRotationAccumulator = snap.smartRotAcc1;
  motor2.smartRotationAccumulator = snap.smartRotAcc2;
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
  bool shouldWriteConfig = configChanged();
  bool shouldWriteRuntime = forceRuntime || (now - lastPersistMs >= RUNTIME_PERSIST_MS);
  if (!shouldWriteConfig && !shouldWriteRuntime) {
    return;
  }

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
  if (dayStartMs == 0) dayStartMs = millis();
}

bool isHourInRange(int h, int start, int end) {
  if (start <= end) return h >= start && h <= end;
  return h >= start || h <= end;
}

int currentHour(unsigned long now) {
  unsigned long elapsed = now - dayStartMs;
  return (int)((elapsed / HOUR_MS) % 24UL);
}

void resetDailyProgress(unsigned long now) {
  dayStartMs = now;
  hourCursor = 0;
  for (int i = 0; i < 24; i++) {
    hourBuckets1[i] = 0;
    hourBuckets2[i] = 0;
  }
  motor1.stepsToday = 0;
  motor2.stepsToday = 0;
  motor1.cyclesCompleted = 0;
  motor2.cyclesCompleted = 0;
  motor1.smartRotationAccumulator = 0;
  motor2.smartRotationAccumulator = 0;
}

void rollHourlyBuckets(unsigned long now) {
  if (dayStartMs == 0) dayStartMs = now;
  if (now - dayStartMs >= DAY_MS) {
    resetDailyProgress(now);
  }

  int targetHour = currentHour(now);
  while (hourCursor != targetHour) {
    hourCursor = (hourCursor + 1) % 24;
    hourBuckets1[hourCursor] = 0;
    hourBuckets2[hourCursor] = 0;
  }
}

unsigned long targetStepsPerDay(int targetTPD) {
  return (unsigned long)targetTPD * (unsigned long)STEPS_PER_REV;
}

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

bool isQuietHour(unsigned long now) {
  if (!config.quietMode) return false;
  int h = currentHour(now);
  return isHourInRange(h, config.quietStartHour, config.quietEndHour);
}

float driftPercentFor(const MotorRuntime& m, int targetTPD, unsigned long now) {
  unsigned long elapsed = now - dayStartMs;
  if (elapsed == 0) return 0.0f;
  unsigned long targetSteps = targetStepsPerDay(targetTPD);
  float expected = ((float)targetSteps * (float)elapsed) / (float)DAY_MS;
  return ((float)m.stepsToday - expected) * 100.0f / (float)targetSteps;
}

unsigned long computeBurstMs(int targetTPD) {
  int minutes = clampInt(1 + (targetTPD - 500) / 250, 1, 3);
  return (unsigned long)minutes * 60000UL;
}

unsigned long computePauseMs(int targetTPD, float driftPercent) {
  int minutes = clampInt(10 - (targetTPD - 500) / 100, 5, 10);
  if (driftPercent < -3.0f) minutes = max(5, minutes - 2);
  if (driftPercent > 3.0f) minutes = min(10, minutes + 2);
  unsigned long result = (unsigned long)minutes * 60000UL;
  if (result < MIN_PAUSE_MS) result = MIN_PAUSE_MS;
  if (result > MAX_PAUSE_MS) result = MAX_PAUSE_MS;
  return result;
}

unsigned long computeBurstSteps(int targetTPD, float driftPercent) {
  float rotationsPerHour = (float)targetTPD / 24.0f;
  float baseRot = rotationsPerHour / 6.0f;  // roughly every 10 minutes
  float adjust = 1.0f;
  if (driftPercent < -3.0f) adjust = 1.25f;
  if (driftPercent > 3.0f) adjust = 0.8f;
  float rotations = baseRot * adjust;
  unsigned long steps = (unsigned long)(rotations * (float)STEPS_PER_REV);
  unsigned long minSteps = (unsigned long)STEPS_PER_REV / 3UL;
  unsigned long maxSteps = (unsigned long)STEPS_PER_REV * 3UL;
  if (steps < minSteps) steps = minSteps;
  if (steps > maxSteps) steps = maxSteps;
  return steps;
}

bool isMotorActiveHour(const MotorConfig& mc, unsigned long now) {
  return isHourInRange(currentHour(now), mc.activeStartHour, mc.activeEndHour);
}

void stepMotorPhase(int phase, const int pins[4]) {
  digitalWrite(pins[0], seq[phase][0]);
  digitalWrite(pins[1], seq[phase][1]);
  digitalWrite(pins[2], seq[phase][2]);
  digitalWrite(pins[3], seq[phase][3]);
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

void setError(ErrorCode code, const char* reason) {
  errorCode = code;
  systemState = SYS_ERROR;
  systemEnabled = false;
  timerActive = false;
  motor1.burstActive = false;
  motor2.burstActive = false;
  releaseAllMotors();
  lastStopReason = reason;
  savePersistent(true);
}

void resetMotorRuntime(MotorRuntime& m, unsigned long now) {
  m.burstActive = false;
  m.burstDirectionCw = true;
  m.nextBidirCw = true;
  m.pauseUntilMs = now;
  m.burstStartMs = 0;
  m.burstTargetSteps = 0;
  m.burstStepsDone = 0;
  m.nextStepAtMs = now;
  m.lastStepMs = now;
  m.phaseIndex = 0;
}

void initRuntimes(unsigned long now) {
  resetMotorRuntime(motor1, now);
  resetMotorRuntime(motor2, now);
}

bool isLagging(float driftPercent) {
  return driftPercent < -3.0f;
}

bool isAhead(float driftPercent) {
  return driftPercent > 3.0f;
}

unsigned long getTimerRemainingMs(unsigned long now) {
  if (!timerActive) return 0;
  if (now >= runUntilMs) return 0;
  return runUntilMs - now;
}

void stopSystem(const char* reason) {
  systemEnabled = false;
  timerActive = false;
  systemState = SYS_IDLE;
  motor1.burstActive = false;
  motor2.burstActive = false;
  releaseAllMotors();
  lastStopReason = reason;
  savePersistent(true);
}

void startSystem(unsigned long now, int minutes) {
  if (errorCode != ERR_NONE) {
    errorCode = ERR_NONE;
  }
  systemEnabled = true;
  systemState = SYS_RUNNING;
  initRuntimes(now);

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

String activeNetworkMode() {
  return usingApFallback ? "AP" : "STA";
}

String activeIpString() {
  return usingApFallback ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
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
  String content = body.substring(b1 + 1, b2);
  int comma = content.indexOf(',');
  if (comma < 0) return false;
  startHour = content.substring(0, comma).toInt();
  endHour = content.substring(comma + 1).toInt();
  return true;
}

unsigned long legacyTpdFromGiriDelay(int giri, int delayMin) {
  int fg = clampInt(giri, 1, 10);
  int fd = clampInt(delayMin, 1, 60);
  float tpd = (float)(fg * 2) * (1440.0f / (float)fd);
  return (unsigned long)clampInt((int)(tpd + 0.5f), 500, 1000);
}

void applyMotorConfigFromRequest(MotorConfig& mc, const String& body, const char* suffix) {
  String targetKey = String("targetTPD") + suffix;
  String directionKey = String("direction") + suffix;
  String speedKey = String("speed") + suffix;
  String modeKey = String("mode") + suffix;
  String activeStartKey = String("activeStartHour") + suffix;
  String activeEndKey = String("activeEndHour") + suffix;
  String smartKey = String("smartSwitchRotations") + suffix;

  int v = 0;
  String s;
  if (server.hasArg(targetKey)) mc.targetTPD = clampInt(server.arg(targetKey).toInt(), 500, 1000);
  else if (parseJsonInt(body, targetKey.c_str(), v)) mc.targetTPD = clampInt(v, 500, 1000);

  if (server.hasArg(directionKey)) mc.direction = parseDirection(server.arg(directionKey));
  else if (parseJsonString(body, directionKey.c_str(), s)) mc.direction = parseDirection(s);

  if (server.hasArg(speedKey)) mc.speed = clampInt(server.arg(speedKey).toInt(), 1, 5);
  else if (parseJsonInt(body, speedKey.c_str(), v)) mc.speed = clampInt(v, 1, 5);

  if (server.hasArg(modeKey)) mc.mode = parseMode(server.arg(modeKey));
  else if (parseJsonString(body, modeKey.c_str(), s)) mc.mode = parseMode(s);

  if (server.hasArg(activeStartKey)) mc.activeStartHour = clampInt(server.arg(activeStartKey).toInt(), 0, 23);
  else if (parseJsonInt(body, activeStartKey.c_str(), v)) mc.activeStartHour = clampInt(v, 0, 23);

  if (server.hasArg(activeEndKey)) mc.activeEndHour = clampInt(server.arg(activeEndKey).toInt(), 0, 23);
  else if (parseJsonInt(body, activeEndKey.c_str(), v)) mc.activeEndHour = clampInt(v, 0, 23);

  if (server.hasArg(smartKey)) mc.smartSwitchRotations = clampInt(server.arg(smartKey).toInt(), 2, 100);
  else if (parseJsonInt(body, smartKey.c_str(), v)) mc.smartSwitchRotations = clampInt(v, 2, 100);
}

void applyConfigFromRequest() {
  String body = server.hasArg("plain") ? server.arg("plain") : "";
  int v = 0;
  String s;

  // Shared keys apply to both motors.
  if (server.hasArg("targetTPD")) {
    int t = clampInt(server.arg("targetTPD").toInt(), 500, 1000);
    config.m1.targetTPD = t;
    config.m2.targetTPD = t;
  } else if (parseJsonInt(body, "targetTPD", v)) {
    int t = clampInt(v, 500, 1000);
    config.m1.targetTPD = t;
    config.m2.targetTPD = t;
  }

  if (server.hasArg("direction")) {
    int d = parseDirection(server.arg("direction"));
    config.m1.direction = d;
    config.m2.direction = d;
  } else if (parseJsonString(body, "direction", s)) {
    int d = parseDirection(s);
    config.m1.direction = d;
    config.m2.direction = d;
  }

  if (server.hasArg("speed")) {
    int sp = clampInt(server.arg("speed").toInt(), 1, 5);
    config.m1.speed = sp;
    config.m2.speed = sp;
  } else if (parseJsonInt(body, "speed", v)) {
    int sp = clampInt(v, 1, 5);
    config.m1.speed = sp;
    config.m2.speed = sp;
  }

  if (server.hasArg("mode")) {
    int m = parseMode(server.arg("mode"));
    config.m1.mode = m;
    config.m2.mode = m;
  } else if (parseJsonString(body, "mode", s)) {
    int m = parseMode(s);
    config.m1.mode = m;
    config.m2.mode = m;
  }

  int ahStart = 0;
  int ahEnd = 23;
  if (parseJsonActiveHours(body, ahStart, ahEnd)) {
    config.m1.activeStartHour = clampInt(ahStart, 0, 23);
    config.m1.activeEndHour = clampInt(ahEnd, 0, 23);
    config.m2.activeStartHour = clampInt(ahStart, 0, 23);
    config.m2.activeEndHour = clampInt(ahEnd, 0, 23);
  }

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

  if (server.hasArg("logLevel")) config.logLevel = clampInt(server.arg("logLevel").toInt(), LOG_ERROR, LOG_DEBUG);
  else if (parseJsonInt(body, "logLevel", v)) config.logLevel = clampInt(v, LOG_ERROR, LOG_DEBUG);

  applyMotorConfigFromRequest(config.m1, body, "1");
  applyMotorConfigFromRequest(config.m2, body, "2");

  // Backward compatibility with giri/gdelay.
  bool hasLegacyDelay = false;
  int legacyDelay = config.legacyDelay;
  if (server.hasArg("gdelay")) {
    hasLegacyDelay = true;
    legacyDelay = server.arg("gdelay").toInt();
  } else if (parseJsonInt(body, "gdelay", v)) {
    hasLegacyDelay = true;
    legacyDelay = v;
  }
  if (hasLegacyDelay) {
    config.legacyDelay = clampInt(legacyDelay, 1, 60);
  }

  int giri1 = 2;
  int giri2 = 2;
  bool hasGiri1 = false;
  bool hasGiri2 = false;
  if (server.hasArg("giri1")) {
    hasGiri1 = true;
    giri1 = server.arg("giri1").toInt();
  } else if (parseJsonInt(body, "giri1", v)) {
    hasGiri1 = true;
    giri1 = v;
  }

  if (server.hasArg("giri2")) {
    hasGiri2 = true;
    giri2 = server.arg("giri2").toInt();
  } else if (parseJsonInt(body, "giri2", v)) {
    hasGiri2 = true;
    giri2 = v;
  }

  if (hasGiri1 || hasLegacyDelay) {
    config.m1.targetTPD = legacyTpdFromGiriDelay(hasGiri1 ? giri1 : 2, config.legacyDelay);
  }
  if (hasGiri2 || hasLegacyDelay) {
    config.m2.targetTPD = legacyTpdFromGiriDelay(hasGiri2 ? giri2 : 2, config.legacyDelay);
  }

  sanitizeConfig();
}

float rotationsToday(const MotorRuntime& m) {
  return (float)m.stepsToday / (float)STEPS_PER_REV;
}

float rotationsLastHour(bool firstMotor) {
  uint32_t total = 0;
  if (firstMotor) {
    for (int i = 0; i < 24; i++) total += hourBuckets1[i];
  } else {
    for (int i = 0; i < 24; i++) total += hourBuckets2[i];
  }
  return (float)total / (float)STEPS_PER_REV;
}

float currentTPD(const MotorRuntime& m, unsigned long now) {
  unsigned long elapsed = max(1UL, now - dayStartMs);
  float dayRatio = (float)DAY_MS / (float)elapsed;
  return rotationsToday(m) * dayRatio;
}

void handleRoot() {
  String msg = "{";
  msg += "\"name\":\"watch-winder-double\",";
  msg += "\"message\":\"Use /api/status, /api/stats, /api/health, /api/config\"";
  msg += "}";
  sendCorsHeaders();
  server.send(200, "application/json", msg);
}

void appendCommonConfigJson(String& json) {
  json += "\"quietMode\":" + String(config.quietMode ? "true" : "false") + ",";
  json += "\"quietStartHour\":" + String(config.quietStartHour) + ",";
  json += "\"quietEndHour\":" + String(config.quietEndHour) + ",";
  json += "\"nightSpeedCap\":" + String(config.nightSpeedCap) + ",";
  json += "\"runMinutes\":" + String(config.runMinutes) + ",";
  json += "\"logLevel\":" + String(config.logLevel);
}

void appendMotorConfigJson(String& json, const MotorConfig& mc, const char* suffix) {
  json += "\"targetTPD" + String(suffix) + "\":" + String(mc.targetTPD) + ",";
  json += "\"direction" + String(suffix) + "\":\"" + String(directionToText((DirectionMode)mc.direction)) + "\",";
  json += "\"speed" + String(suffix) + "\":" + String(mc.speed) + ",";
  json += "\"mode" + String(suffix) + "\":\"" + String(modeToText((WinderMode)mc.mode)) + "\",";
  json += "\"activeHours" + String(suffix) + "\":[" + String(mc.activeStartHour) + "," + String(mc.activeEndHour) + "],";
  json += "\"smartSwitchRotations" + String(suffix) + "\":" + String(mc.smartSwitchRotations) + ",";
}

void appendMotorStatsJson(String& json,
                          const MotorRuntime& m,
                          const MotorConfig& mc,
                          bool firstMotor,
                          unsigned long now,
                          const char* suffix) {
  float drift = driftPercentFor(m, mc.targetTPD, now);
  json += "\"rotationsToday" + String(suffix) + "\":" + String(rotationsToday(m), 2) + ",";
  json += "\"rotationsLastHour" + String(suffix) + "\":" + String(rotationsLastHour(firstMotor), 2) + ",";
  json += "\"currentTPD" + String(suffix) + "\":" + String(currentTPD(m, now), 2) + ",";
  json += "\"driftPercent" + String(suffix) + "\":" + String(drift, 2) + ",";
  json += "\"remainingRotations" + String(suffix) + "\":" + String(max(0.0f, (float)mc.targetTPD - rotationsToday(m)), 2) + ",";
}

void handleApiStatus() {
  unsigned long now = millis();
  rollHourlyBuckets(now);

  String json = "{";
  json += "\"mode\":\"double\",";
  json += "\"config\":{";
  appendMotorConfigJson(json, config.m1, "1");
  appendMotorConfigJson(json, config.m2, "2");
  appendCommonConfigJson(json);
  json += "},";

  json += "\"runtime\":{";
  json += "\"systemEnabled\":" + String(systemEnabled ? "true" : "false") + ",";
  json += "\"timerActive\":" + String(timerActive ? "true" : "false") + ",";
  json += "\"timerRemainingMs\":" + String(getTimerRemainingMs(now)) + ",";
  json += "\"motorState\":\"" + String(stateToText(systemState)) + "\",";
  json += "\"errorCode\":\"" + String(errorToText(errorCode)) + "\",";
  json += "\"activeMotor\":" + String(activeMotor) + ",";
  json += "\"cyclesCompleted\":" + String(motor1.cyclesCompleted + motor2.cyclesCompleted) + ",";
  appendMotorStatsJson(json, motor1, config.m1, true, now, "1");
  appendMotorStatsJson(json, motor2, config.m2, false, now, "2");
  float progress1 = min(100.0f, (rotationsToday(motor1) * 100.0f) / (float)config.m1.targetTPD);
  float progress2 = min(100.0f, (rotationsToday(motor2) * 100.0f) / (float)config.m2.targetTPD);
  json += "\"currentTPDProgress\":" + String((progress1 + progress2) * 0.5f, 2) + ",";
  json += "\"uptime\":" + String(now);
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

void handleApiStats() {
  unsigned long now = millis();
  rollHourlyBuckets(now);

  String json = "{";
  json += "\"rotationsToday\":" + String(rotationsToday(motor1) + rotationsToday(motor2), 2) + ",";
  json += "\"rotationsLastHour\":" + String(rotationsLastHour(true) + rotationsLastHour(false), 2) + ",";
  json += "\"currentTPD\":" + String((currentTPD(motor1, now) + currentTPD(motor2, now)) * 0.5f, 2) + ",";
  json += "\"driftPercent\":" + String((driftPercentFor(motor1, config.m1.targetTPD, now) + driftPercentFor(motor2, config.m2.targetTPD, now)) * 0.5f, 2) + ",";
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

bool shouldStepDirection(const MotorConfig& mc, MotorRuntime& rt) {
  if ((DirectionMode)mc.direction == DIR_CW) {
    rt.burstDirectionCw = true;
    return true;
  }
  if ((DirectionMode)mc.direction == DIR_CCW) {
    rt.burstDirectionCw = false;
    return true;
  }

  if ((WinderMode)mc.mode == MODE_SMART) {
    if (rt.smartRotationAccumulator >= (unsigned int)mc.smartSwitchRotations) {
      rt.burstDirectionCw = !rt.burstDirectionCw;
      rt.smartRotationAccumulator = 0;
    }
    return true;
  }

  rt.burstDirectionCw = rt.nextBidirCw;
  rt.nextBidirCw = !rt.nextBidirCw;
  return true;
}

bool runMotorScheduler(unsigned long now,
                      const int pins[4],
                      const MotorConfig& mc,
                      MotorRuntime& rt,
                      bool canStep,
                      bool firstMotor) {
  if (!isMotorActiveHour(mc, now)) {
    return false;
  }

  unsigned long targetSteps = targetStepsPerDay(mc.targetTPD);
  if (rt.stepsToday >= targetSteps) {
    return false;
  }

  float driftPct = driftPercentFor(rt, mc.targetTPD, now);

  if (!rt.burstActive) {
    if (now < rt.pauseUntilMs) {
      return false;
    }

    rt.burstActive = true;
    rt.burstStartMs = now;
    rt.burstStepsDone = 0;

    unsigned long planned = computeBurstSteps(mc.targetTPD, driftPct);
    unsigned long remaining = targetSteps - rt.stepsToday;
    rt.burstTargetSteps = min(planned, remaining);
    shouldStepDirection(mc, rt);
    rt.nextStepAtMs = now;
    rt.lastStepMs = now;
  }

  if (!canStep) return rt.burstActive;

  int effectiveSpeed = mc.speed;
  if (isQuietHour(now)) {
    effectiveSpeed = min(effectiveSpeed, config.nightSpeedCap);
  }

  unsigned long baseDelay = max((unsigned long)MIN_EFFECTIVE_STEP_DELAY_MS, speedToDelayMs(effectiveSpeed));

  int budget = 0;
  while (rt.burstActive && rt.burstStepsDone < rt.burstTargetSteps && (long)(now - rt.nextStepAtMs) >= 0 && budget < 6) {
    float progress = rt.burstTargetSteps == 0 ? 1.0f : (float)rt.burstStepsDone / (float)rt.burstTargetSteps;
    unsigned long rampAdd = 0;
    if (progress < 0.15f || progress > 0.85f) {
      rampAdd = 2;
    }

    if (rt.burstDirectionCw) {
      rt.phaseIndex = (rt.phaseIndex + 1) % 8;
    } else {
      rt.phaseIndex = (rt.phaseIndex + 7) % 8;
    }

    stepMotorPhase(rt.phaseIndex, pins);
    rt.burstStepsDone++;
    rt.stepsToday++;
    rt.totalSteps++;
    if (firstMotor) hourBuckets1[hourCursor]++;
    else hourBuckets2[hourCursor]++;

    rt.lastStepMs = now;
    rt.nextStepAtMs += baseDelay + rampAdd;
    budget++;
  }

  if (HAS_CURRENT_SENSOR) {
    int current = analogRead(CURRENT_SENSOR_PIN);
    if (current >= CURRENT_JAM_THRESHOLD) {
      setError(ERR_OVERCURRENT, "overcurrent");
      return false;
    }
  }

  if (rt.burstActive && now - rt.lastStepMs > JAM_TIMEOUT_MS) {
    setError(ERR_JAM_DETECTED, "jam_timeout");
    return false;
  }

  unsigned long burstMs = computeBurstMs(mc.targetTPD);
  if (burstMs < MIN_BURST_MS) burstMs = MIN_BURST_MS;
  if (burstMs > MAX_BURST_MS) burstMs = MAX_BURST_MS;

  bool finished = rt.burstStepsDone >= rt.burstTargetSteps;
  bool timedOut = now - rt.burstStartMs >= burstMs;
  bool reached = rt.stepsToday >= targetSteps;

  if (rt.burstActive && (finished || timedOut || reached)) {
    rt.burstActive = false;
    rt.cyclesCompleted++;
    releaseMotor(pins);

    unsigned int burstRot = (unsigned int)(rt.burstStepsDone / (unsigned long)STEPS_PER_REV);
    rt.smartRotationAccumulator += burstRot;

    rt.pauseUntilMs = now + computePauseMs(mc.targetTPD, driftPct);
    return false;
  }

  return rt.burstActive;
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(motorPins1[i], OUTPUT);
    pinMode(motorPins2[i], OUTPUT);
  }

  loadPersistent();
  if (dayStartMs == 0) dayStartMs = millis();
  initRuntimes(millis());

  startNetwork();
  startServer();

  if (config.runMinutes > 0) {
    startSystem(millis(), config.runMinutes);
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
    stopSystem("timer_elapsed");
    return;
  }

  if (!systemEnabled) {
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

  bool needs1 = isMotorActiveHour(config.m1, now) && motor1.stepsToday < targetStepsPerDay(config.m1.targetTPD);
  bool needs2 = isMotorActiveHour(config.m2, now) && motor2.stepsToday < targetStepsPerDay(config.m2.targetTPD);

  if (!needs1 && !needs2) {
    systemState = SYS_PAUSED;
    releaseAllMotors();
    savePersistent(false);
    return;
  }

  systemState = SYS_RUNNING;

  if (activeMotor == 1 && !needs1) activeMotor = 2;
  if (activeMotor == 2 && !needs2) activeMotor = 1;

  bool run1 = activeMotor == 1;
  bool run2 = activeMotor == 2;

  bool active1 = runMotorScheduler(now, motorPins1, config.m1, motor1, run1, true);
  bool active2 = runMotorScheduler(now, motorPins2, config.m2, motor2, run2, false);

  if (activeMotor == 1 && !active1 && needs2) activeMotor = 2;
  if (activeMotor == 2 && !active2 && needs1) activeMotor = 1;

  savePersistent(false);
}
