const appConfig = window.APP_CONFIG || {};
const storageKey = "watch-winder-device";
const defaultDeviceUrl = appConfig.defaultDeviceUrl || "http://192.168.1.111";
const legacyDefaultUrl = "http://192.168.4.1";
const startupMode = appConfig.deviceMode === "double" ? "double" : "single";
const lockDeviceMode = Boolean(appConfig.lockDeviceMode);
const lockDeviceUrl = Boolean(appConfig.lockDeviceUrl);
const startupLanguage = appConfig.language === "en" ? "en" : "sr";
const lockLanguage = Boolean(appConfig.lockLanguage);

const el = {
  deviceUrl: document.getElementById("deviceUrl"),
  runMinutes: document.getElementById("runMinutes"),
  deviceUrlRow: document.getElementById("deviceUrlRow"),
  languageRow: document.getElementById("languageRow"),
  uiLanguage: document.getElementById("uiLanguage"),
  languagePicker: document.getElementById("languagePicker"),
  modePicker: document.getElementById("modePicker"),
  modeConfigRow: document.getElementById("modeConfigRow"),
  deviceMode: document.getElementById("deviceMode"),
  startBtn: document.getElementById("startBtn"),
  stopBtn: document.getElementById("stopBtn"),
  saveDeviceBtn: document.getElementById("saveDeviceBtn"),
  refreshBtn: document.getElementById("refreshBtn"),
  saveSingleBtn: document.getElementById("saveSingleBtn"),
  saveDoubleBtn: document.getElementById("saveDoubleBtn"),
  statusBadge: document.getElementById("statusBadge"),
  lastSync: document.getElementById("lastSync"),
  singlePanel: document.getElementById("singlePanel"),
  doublePanel: document.getElementById("doublePanel"),
  modeOut: document.getElementById("modeOut"),
  apOut: document.getElementById("apOut"),
  timerOut: document.getElementById("timerOut"),
  stateAOut: document.getElementById("stateAOut"),
  stateBOut: document.getElementById("stateBOut"),
  activeMotorOut: document.getElementById("activeMotorOut"),
  stepsOut: document.getElementById("stepsOut"),
  errorOut: document.getElementById("errorOut"),
  rotationsTodayOut: document.getElementById("rotationsTodayOut"),
  rotationsLastHourOut: document.getElementById("rotationsLastHourOut"),
  currentTpdOut: document.getElementById("currentTpdOut"),
  driftOut: document.getElementById("driftOut"),
  progressOut: document.getElementById("progressOut"),
  remainingOut: document.getElementById("remainingOut"),
  heapOut: document.getElementById("heapOut"),
  uptimeOut: document.getElementById("uptimeOut"),
  singleTargetTPD: document.getElementById("singleTargetTPD"),
  singleSpeed: document.getElementById("singleSpeed"),
  singleDirection: document.getElementById("singleDirection"),
  singleMode: document.getElementById("singleMode"),
  singleActiveStart: document.getElementById("singleActiveStart"),
  singleActiveEnd: document.getElementById("singleActiveEnd"),
  singleSmartSwitch: document.getElementById("singleSmartSwitch"),
  singleQuietMode: document.getElementById("singleQuietMode"),
  singleQuietStart: document.getElementById("singleQuietStart"),
  singleQuietEnd: document.getElementById("singleQuietEnd"),
  singleNightSpeedCap: document.getElementById("singleNightSpeedCap"),
  targetTPD1: document.getElementById("targetTPD1"),
  speed1: document.getElementById("speed1"),
  direction1: document.getElementById("direction1"),
  mode1: document.getElementById("mode1"),
  activeStartHour1: document.getElementById("activeStartHour1"),
  activeEndHour1: document.getElementById("activeEndHour1"),
  smartSwitchRotations1: document.getElementById("smartSwitchRotations1"),
  targetTPD2: document.getElementById("targetTPD2"),
  speed2: document.getElementById("speed2"),
  direction2: document.getElementById("direction2"),
  mode2: document.getElementById("mode2"),
  activeStartHour2: document.getElementById("activeStartHour2"),
  activeEndHour2: document.getElementById("activeEndHour2"),
  smartSwitchRotations2: document.getElementById("smartSwitchRotations2"),
  quietMode: document.getElementById("quietMode"),
  quietStartHour: document.getElementById("quietStartHour"),
  quietEndHour: document.getElementById("quietEndHour"),
  nightSpeedCap: document.getElementById("nightSpeedCap")
};

const modeButtons = Array.from(document.querySelectorAll(".mode-btn"));
const languageButtons = Array.from(document.querySelectorAll("#languagePicker .mode-btn"));
const optionButtons = Array.from(document.querySelectorAll(".option-picker .option-btn"));

const i18n = {
  sr: {
    heroKicker: "Watch winder kontrola",
    heroTitle: "Kontrola navijanja automatskog sata",
    heroSub: "Pouzdan panel za pracenje rada i precizno podesavanje watch winder uredjaja.",
    connectionTitle: "Konekcija",
    deviceUrlLabel: "URL uredjaja",
    languageLabel: "Jezik",
    timerLabel: "Tajmer rada (min, 0 = bez limita)",
    startBtn: "Start",
    stopBtn: "Stop",
    deviceTypeLabel: "Tip uredjaja",
    saveDeviceBtn: "Sacuvaj uredjaj",
    refreshBtn: "Osvezi status",
    singleTitle: "Podesavanja za jedan sat",
    doubleTitle: "Podesavanja za dva sata",
    directionLabel: "Smer",
    modeLabel: "Mode",
    saveSettingsBtn: "Sacuvaj podesavanja",
    motor1Direction: "Motor 1 smer",
    motor1Mode: "Motor 1 mode",
    motor2Direction: "Motor 2 smer",
    motor2Mode: "Motor 2 mode"
  },
  en: {
    heroKicker: "Watch winder control",
    heroTitle: "Automatic Watch Winder Control",
    heroSub: "Reliable panel for monitoring and precise watch winder tuning.",
    connectionTitle: "Connection",
    deviceUrlLabel: "Device URL",
    languageLabel: "Language",
    timerLabel: "Run timer (min, 0 = unlimited)",
    startBtn: "Start",
    stopBtn: "Stop",
    deviceTypeLabel: "Device type",
    saveDeviceBtn: "Save device",
    refreshBtn: "Refresh status",
    singleTitle: "Single winder settings",
    doubleTitle: "Double winder settings",
    directionLabel: "Direction",
    modeLabel: "Mode",
    saveSettingsBtn: "Save settings",
    motor1Direction: "Motor 1 direction",
    motor1Mode: "Motor 1 mode",
    motor2Direction: "Motor 2 direction",
    motor2Mode: "Motor 2 mode"
  }
};

function getLanguage() {
  const raw = localStorage.getItem("watch-winder-language");
  if (lockLanguage) return startupLanguage;
  return raw === "en" ? "en" : raw === "sr" ? "sr" : startupLanguage;
}

function setLanguage(lang) {
  localStorage.setItem("watch-winder-language", lang);
}

function applyTranslations(lang) {
  const dict = i18n[lang] || i18n.sr;
  document.querySelectorAll("[data-i18n]").forEach((node) => {
    const key = node.getAttribute("data-i18n");
    if (dict[key]) node.textContent = dict[key];
  });
}

function renderLanguage(lang) {
  const effective = lockLanguage ? startupLanguage : lang;
  if (el.uiLanguage) el.uiLanguage.value = effective;
  if (el.languageRow) el.languageRow.style.display = lockLanguage ? "none" : "block";
  languageButtons.forEach((button) => {
    const active = button.dataset.lang === effective;
    button.classList.toggle("is-active", active);
    button.disabled = lockLanguage;
  });
  applyTranslations(effective);
}

function syncOptionPickers() {
  optionButtons.forEach((button) => {
    const picker = button.closest(".option-picker");
    if (!picker) return;
    const targetId = picker.getAttribute("data-target");
    if (!targetId) return;
    const hidden = document.getElementById(targetId);
    if (!hidden) return;
    button.classList.toggle("is-active", String(hidden.value) === String(button.dataset.value));
  });
}

function getDevice() {
  const raw = localStorage.getItem(storageKey);
  if (!raw) {
    return { url: defaultDeviceUrl, mode: startupMode };
  }
  try {
    const parsed = JSON.parse(raw);

    if (!parsed.url || parsed.url === legacyDefaultUrl) {
      parsed.url = defaultDeviceUrl;
      setDevice(parsed);
    }

    if (!parsed.mode) {
      parsed.mode = startupMode;
      setDevice(parsed);
    }

    if (lockDeviceMode) {
      parsed.mode = startupMode;
      setDevice(parsed);
    }

    return parsed;
  } catch {
    return { url: defaultDeviceUrl, mode: startupMode };
  }
}

function setDevice(device) {
  localStorage.setItem(storageKey, JSON.stringify(device));
}

function normalizeBaseUrl(url) {
  const trimmed = url.trim();
  if (!trimmed) return defaultDeviceUrl;
  const withProtocol = /^https?:\/\//i.test(trimmed) ? trimmed : `http://${trimmed}`;
  return withProtocol.replace(/\/$/, "");
}

function setConnectionState(connected, text) {
  el.statusBadge.textContent = text;
  el.statusBadge.classList.toggle("ok", connected);
}

function renderMode(mode) {
  const effectiveMode = lockDeviceMode ? startupMode : mode;
  const isSingle = effectiveMode === "single";

  if (el.modeConfigRow) {
    el.modeConfigRow.style.display = lockDeviceMode ? "none" : "block";
  }
  if (el.deviceUrlRow) {
    el.deviceUrlRow.style.display = lockDeviceUrl ? "none" : "block";
  }
  if (el.saveDeviceBtn) {
    el.saveDeviceBtn.style.display = (lockDeviceMode && lockDeviceUrl) ? "none" : "inline-block";
  }

  el.singlePanel.style.display = isSingle ? "block" : "none";
  el.doublePanel.style.display = isSingle ? "none" : "block";

  modeButtons.forEach((button) => {
    const active = button.dataset.mode === effectiveMode;
    button.classList.toggle("is-active", active);
    button.setAttribute("aria-selected", active ? "true" : "false");
    button.disabled = lockDeviceMode;
  });

  el.deviceMode.value = effectiveMode;
}

async function apiFetch(path, options = {}) {
  const device = getDevice();
  const base = normalizeBaseUrl(device.url);
  const response = await fetch(`${base}${path}`, options);
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  return response.json();
}

function fillSingle(data) {
  el.singleTargetTPD.value = data.config.targetTPD ?? 650;
  el.singleSpeed.value = data.config.speed ?? 3;
  el.singleDirection.value = data.config.direction ?? "BIDIR";
  el.singleMode.value = data.config.mode ?? "STANDARD";
  const hours = data.config.activeHours ?? [0, 23];
  el.singleActiveStart.value = hours[0] ?? 0;
  el.singleActiveEnd.value = hours[1] ?? 23;
  el.singleSmartSwitch.value = data.config.smartSwitchRotations ?? 10;
  el.singleQuietMode.checked = Boolean(data.config.quietMode ?? true);
  el.singleQuietStart.value = data.config.quietStartHour ?? 22;
  el.singleQuietEnd.value = data.config.quietEndHour ?? 7;
  el.singleNightSpeedCap.value = data.config.nightSpeedCap ?? 2;
  el.runMinutes.value = data.config.runMinutes ?? 0;
  el.stateAOut.textContent = data.runtime.motorState ?? data.runtime.state ?? "-";
  el.stateBOut.textContent = "-";
  el.activeMotorOut.textContent = "1";
  el.stepsOut.textContent = data.runtime.stepsRemaining ?? data.runtime.remainingRotations ?? "-";
  syncOptionPickers();
}

function fillDouble(data) {
  el.targetTPD1.value = data.config.targetTPD1 ?? data.config.targetTPD ?? 650;
  el.speed1.value = data.config.speed1 ?? 3;
  el.direction1.value = data.config.direction1 ?? "BIDIR";
  el.mode1.value = data.config.mode1 ?? "STANDARD";
  el.activeStartHour1.value = data.config.activeHours1?.[0] ?? 0;
  el.activeEndHour1.value = data.config.activeHours1?.[1] ?? 23;
  el.smartSwitchRotations1.value = data.config.smartSwitchRotations1 ?? 10;

  el.targetTPD2.value = data.config.targetTPD2 ?? data.config.targetTPD ?? 650;
  el.speed2.value = data.config.speed2 ?? 3;
  el.direction2.value = data.config.direction2 ?? "BIDIR";
  el.mode2.value = data.config.mode2 ?? "STANDARD";
  el.activeStartHour2.value = data.config.activeHours2?.[0] ?? 0;
  el.activeEndHour2.value = data.config.activeHours2?.[1] ?? 23;
  el.smartSwitchRotations2.value = data.config.smartSwitchRotations2 ?? 10;

  el.quietMode.checked = Boolean(data.config.quietMode ?? true);
  el.quietStartHour.value = data.config.quietStartHour ?? 22;
  el.quietEndHour.value = data.config.quietEndHour ?? 7;
  el.nightSpeedCap.value = data.config.nightSpeedCap ?? 2;
  el.runMinutes.value = data.config.runMinutes ?? 0;
  el.stateAOut.textContent = data.runtime.motor1State ?? "-";
  el.stateBOut.textContent = data.runtime.motor2State ?? "-";
  el.activeMotorOut.textContent = data.runtime.activeMotor ?? "-";
  const m1 = data.runtime.motor1StepsRemaining ?? "-";
  const m2 = data.runtime.motor2StepsRemaining ?? "-";
  el.stepsOut.textContent = `M1:${m1} / M2:${m2}`;
  syncOptionPickers();
}

function fillMetricsFromStatus(data) {
  const rt = data.runtime ?? {};
  el.errorOut.textContent = rt.errorCode ?? "NONE";
  el.rotationsTodayOut.textContent =
    rt.rotationsToday ??
    (rt.rotationsToday1 !== undefined && rt.rotationsToday2 !== undefined
      ? `${rt.rotationsToday1} / ${rt.rotationsToday2}`
      : "-");
  el.rotationsLastHourOut.textContent =
    rt.rotationsLastHour ??
    (rt.rotationsLastHour1 !== undefined && rt.rotationsLastHour2 !== undefined
      ? `${rt.rotationsLastHour1} / ${rt.rotationsLastHour2}`
      : "-");
  el.currentTpdOut.textContent =
    rt.currentTPD ??
    (rt.currentTPD1 !== undefined && rt.currentTPD2 !== undefined
      ? `${rt.currentTPD1} / ${rt.currentTPD2}`
      : "-");
  el.driftOut.textContent =
    rt.driftPercent ??
    (rt.driftPercent1 !== undefined && rt.driftPercent2 !== undefined
      ? `${rt.driftPercent1} / ${rt.driftPercent2}`
      : "-");
  el.progressOut.textContent = rt.currentTPDProgress ?? "-";
  el.remainingOut.textContent =
    rt.remainingRotations ??
    (rt.remainingRotations1 !== undefined && rt.remainingRotations2 !== undefined
      ? `${rt.remainingRotations1} / ${rt.remainingRotations2}`
      : "-");
  el.uptimeOut.textContent = rt.uptime ?? "-";
}

function fillMetricsFromStats(stats) {
  if (!stats) return;
  if (stats.rotationsToday !== undefined) el.rotationsTodayOut.textContent = stats.rotationsToday;
  if (stats.rotationsLastHour !== undefined) el.rotationsLastHourOut.textContent = stats.rotationsLastHour;
  if (stats.currentTPD !== undefined) el.currentTpdOut.textContent = stats.currentTPD;
  if (stats.driftPercent !== undefined) el.driftOut.textContent = stats.driftPercent;
  if (stats.errorCode) el.errorOut.textContent = stats.errorCode;
  if (stats.uptime !== undefined) el.uptimeOut.textContent = stats.uptime;
}

function fillMetricsFromHealth(health) {
  if (!health) return;
  if (health.errorCode) el.errorOut.textContent = health.errorCode;
  if (health.heapFree !== undefined) el.heapOut.textContent = health.heapFree;
  if (health.uptime !== undefined) el.uptimeOut.textContent = health.uptime;
}

async function refreshStatus() {
  try {
    const data = await apiFetch("/api/status");
    setConnectionState(true, "Povezano");
    el.lastSync.textContent = `Poslednja provera: ${new Date().toLocaleTimeString()}`;
    el.modeOut.textContent = data.mode;
    const networkMode = data.network?.mode ?? (data.runtime?.apActive ? "AP" : "STA");
    const networkIp = data.network?.ip ?? "-";
    el.apOut.textContent = `${networkMode} (${networkIp})`;

    const timerMs = data.runtime?.timerRemainingMs ?? 0;
    const timerMin = Math.ceil(timerMs / 60000);
    const timerActive = data.runtime?.timerActive;
    el.timerOut.textContent = timerActive ? `${timerMin} min` : "iskljucen";

    fillMetricsFromStatus(data);

    if (data.mode === "single") {
      fillSingle(data);
      renderMode("single");
    } else {
      fillDouble(data);
      renderMode("double");
    }

    const [stats, health] = await Promise.all([
      apiFetch("/api/stats").catch(() => null),
      apiFetch("/api/health").catch(() => null)
    ]);
    fillMetricsFromStats(stats);
    fillMetricsFromHealth(health);
  } catch (error) {
    setConnectionState(false, "Van mreze");
    el.lastSync.textContent = `Uredjaj nije dostupan (${error.message})`;
  }
}

async function postConfig(params) {
  const data = await apiFetch("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(params)
  });
  return data;
}

async function saveSingleConfig() {
  const data = await postConfig({
    targetTPD: el.singleTargetTPD.value,
    direction: el.singleDirection.value,
    mode: el.singleMode.value,
    speed: el.singleSpeed.value,
    activeHours: [Number(el.singleActiveStart.value), Number(el.singleActiveEnd.value)],
    smartSwitchRotations: el.singleSmartSwitch.value,
    quietMode: el.singleQuietMode.checked ? 1 : 0,
    quietStartHour: el.singleQuietStart.value,
    quietEndHour: el.singleQuietEnd.value,
    nightSpeedCap: el.singleNightSpeedCap.value,
    runMinutes: el.runMinutes.value
  });
  fillSingle(data);
  fillMetricsFromStatus(data);
  setConnectionState(true, "Azurirano");
}

async function saveDoubleConfig() {
  const data = await postConfig({
    targetTPD1: el.targetTPD1.value,
    direction1: el.direction1.value,
    mode1: el.mode1.value,
    speed1: el.speed1.value,
    activeStartHour1: el.activeStartHour1.value,
    activeEndHour1: el.activeEndHour1.value,
    smartSwitchRotations1: el.smartSwitchRotations1.value,
    targetTPD2: el.targetTPD2.value,
    direction2: el.direction2.value,
    mode2: el.mode2.value,
    speed2: el.speed2.value,
    activeStartHour2: el.activeStartHour2.value,
    activeEndHour2: el.activeEndHour2.value,
    smartSwitchRotations2: el.smartSwitchRotations2.value,
    quietMode: el.quietMode.checked ? 1 : 0,
    quietStartHour: el.quietStartHour.value,
    quietEndHour: el.quietEndHour.value,
    nightSpeedCap: el.nightSpeedCap.value,
    runMinutes: el.runMinutes.value
  });
  fillDouble(data);
  fillMetricsFromStatus(data);
  setConnectionState(true, "Azurirano");
}

async function postStart() {
  return apiFetch("/api/start", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ runMinutes: Number(el.runMinutes.value) })
  });
}

async function postStop() {
  return apiFetch("/api/stop", {
    method: "POST"
  });
}

function bindEvents() {
  el.saveDeviceBtn.addEventListener("click", () => {
    const device = {
      url: lockDeviceUrl ? defaultDeviceUrl : normalizeBaseUrl(el.deviceUrl.value),
      mode: lockDeviceMode ? startupMode : el.deviceMode.value
    };
    setDevice(device);
    renderMode(device.mode);
    refreshStatus();
  });

  el.refreshBtn.addEventListener("click", refreshStatus);

  el.startBtn.addEventListener("click", async () => {
    try {
      await postStart();
      await refreshStatus();
      el.lastSync.textContent = "Uredjaj je pokrenut";
    } catch (error) {
      setConnectionState(false, "Greska start");
      el.lastSync.textContent = error.message;
    }
  });

  el.stopBtn.addEventListener("click", async () => {
    try {
      await postStop();
      await refreshStatus();
      el.lastSync.textContent = "Uredjaj je zaustavljen";
    } catch (error) {
      setConnectionState(false, "Greska stop");
      el.lastSync.textContent = error.message;
    }
  });

  el.saveSingleBtn.addEventListener("click", async () => {
    try {
      await saveSingleConfig();
      el.lastSync.textContent = "Podesavanja za single uredjaj su uspesno sacuvana";
    } catch (error) {
      setConnectionState(false, "Greska cuvanja");
      el.lastSync.textContent = error.message;
    }
  });

  el.saveDoubleBtn.addEventListener("click", async () => {
    try {
      await saveDoubleConfig();
      el.lastSync.textContent = "Podesavanja za double uredjaj su uspesno sacuvana";
    } catch (error) {
      setConnectionState(false, "Greska cuvanja");
      el.lastSync.textContent = error.message;
    }
  });

  modeButtons.forEach((button) => {
    button.addEventListener("click", () => {
      if (lockDeviceMode) return;
      const selectedMode = button.dataset.mode;
      const device = getDevice();
      device.mode = selectedMode;
      setDevice(device);
      renderMode(selectedMode);
    });
  });

  optionButtons.forEach((button) => {
    button.addEventListener("click", () => {
      const picker = button.closest(".option-picker");
      if (!picker) return;
      const targetId = picker.getAttribute("data-target");
      if (!targetId) return;
      const hidden = document.getElementById(targetId);
      if (!hidden) return;
      hidden.value = button.dataset.value;
      syncOptionPickers();
    });
  });

  languageButtons.forEach((button) => {
    button.addEventListener("click", () => {
      if (lockLanguage) return;
      const lang = button.dataset.lang === "en" ? "en" : "sr";
      setLanguage(lang);
      renderLanguage(lang);
    });
  });
}

function bootstrap() {
  const device = getDevice();
  if (lockDeviceMode) {
    device.mode = startupMode;
    setDevice(device);
  }
  if (lockDeviceUrl) {
    device.url = defaultDeviceUrl;
    setDevice(device);
  }
  el.deviceUrl.value = lockDeviceUrl ? defaultDeviceUrl : device.url;
  const lang = getLanguage();
  renderLanguage(lang);
  renderMode(device.mode);
  syncOptionPickers();
  bindEvents();
  refreshStatus();
  setInterval(refreshStatus, 5000);
}

bootstrap();
