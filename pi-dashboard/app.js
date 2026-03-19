const storageKey = "watch-winder-device";
const defaultDeviceUrl = "http://192.168.1.111";
const legacyDefaultUrl = "http://192.168.4.1";

const el = {
  deviceUrl: document.getElementById("deviceUrl"),
  runMinutes: document.getElementById("runMinutes"),
  modePicker: document.getElementById("modePicker"),
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
  singleGiri: document.getElementById("singleGiri"),
  singleDelay: document.getElementById("singleDelay"),
  singleSpeed: document.getElementById("singleSpeed"),
  giri1: document.getElementById("giri1"),
  speed1: document.getElementById("speed1"),
  giri2: document.getElementById("giri2"),
  speed2: document.getElementById("speed2"),
  gdelay: document.getElementById("gdelay")
};

const modeButtons = Array.from(document.querySelectorAll(".mode-btn"));

function getDevice() {
  const raw = localStorage.getItem(storageKey);
  if (!raw) {
    return { url: defaultDeviceUrl, mode: "single" };
  }
  try {
    const parsed = JSON.parse(raw);

    if (!parsed.url || parsed.url === legacyDefaultUrl) {
      parsed.url = defaultDeviceUrl;
      setDevice(parsed);
    }

    if (!parsed.mode) {
      parsed.mode = "single";
      setDevice(parsed);
    }

    return parsed;
  } catch {
    return { url: defaultDeviceUrl, mode: "single" };
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
  const isSingle = mode === "single";
  el.singlePanel.style.display = isSingle ? "block" : "none";
  el.doublePanel.style.display = isSingle ? "none" : "block";

  modeButtons.forEach((button) => {
    const active = button.dataset.mode === mode;
    button.classList.toggle("is-active", active);
    button.setAttribute("aria-selected", active ? "true" : "false");
  });

  el.deviceMode.value = mode;
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
  el.singleGiri.value = data.config.giri;
  el.singleDelay.value = data.config.delay;
  el.singleSpeed.value = data.config.speed;
  el.runMinutes.value = data.config.runMinutes ?? 0;
  el.stateAOut.textContent = data.runtime.state;
  el.stateBOut.textContent = "-";
  el.activeMotorOut.textContent = "1";
  el.stepsOut.textContent = data.runtime.stepsRemaining ?? "-";
}

function fillDouble(data) {
  el.giri1.value = data.config.giri1;
  el.speed1.value = data.config.speed1;
  el.giri2.value = data.config.giri2;
  el.speed2.value = data.config.speed2;
  el.gdelay.value = data.config.gdelay;
  el.runMinutes.value = data.config.runMinutes ?? 0;
  el.stateAOut.textContent = data.runtime.motor1State;
  el.stateBOut.textContent = data.runtime.motor2State;
  el.activeMotorOut.textContent = data.runtime.activeMotor ?? "-";
  const m1 = data.runtime.motor1StepsRemaining ?? "-";
  const m2 = data.runtime.motor2StepsRemaining ?? "-";
  el.stepsOut.textContent = `M1:${m1} / M2:${m2}`;
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

    if (data.mode === "single") {
      fillSingle(data);
      renderMode("single");
    } else {
      fillDouble(data);
      renderMode("double");
    }
  } catch (error) {
    setConnectionState(false, "Van mreze");
    el.lastSync.textContent = `Uredjaj nije dostupan (${error.message})`;
  }
}

async function postConfig(params) {
  const body = new URLSearchParams(params);
  const data = await apiFetch("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body
  });
  return data;
}

async function saveSingleConfig() {
  const data = await postConfig({
    giri: el.singleGiri.value,
    delay: el.singleDelay.value,
    speed: el.singleSpeed.value,
    runMinutes: el.runMinutes.value
  });
  fillSingle(data);
  setConnectionState(true, "Azurirano");
}

async function saveDoubleConfig() {
  const data = await postConfig({
    giri1: el.giri1.value,
    speed1: el.speed1.value,
    giri2: el.giri2.value,
    speed2: el.speed2.value,
    gdelay: el.gdelay.value,
    runMinutes: el.runMinutes.value
  });
  fillDouble(data);
  setConnectionState(true, "Azurirano");
}

async function postStart() {
  return apiFetch("/api/start", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({ runMinutes: el.runMinutes.value })
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
      url: normalizeBaseUrl(el.deviceUrl.value),
      mode: el.deviceMode.value
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
      const selectedMode = button.dataset.mode;
      const device = getDevice();
      device.mode = selectedMode;
      setDevice(device);
      renderMode(selectedMode);
    });
  });
}

function bootstrap() {
  const device = getDevice();
  el.deviceUrl.value = device.url;
  renderMode(device.mode);
  bindEvents();
  refreshStatus();
  setInterval(refreshStatus, 5000);
}

bootstrap();
