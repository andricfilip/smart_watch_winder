import React, { useEffect, useMemo, useState } from "react";

const appConfig = window.APP_CONFIG || {};
const defaultUrl = appConfig.defaultDeviceUrl || "http://192.168.1.111";
const startupMode = appConfig.deviceMode === "double" ? "double" : "single";
const lockDeviceUrl = Boolean(appConfig.lockDeviceUrl);
const lockDeviceMode = Boolean(appConfig.lockDeviceMode);
const lockLanguage = Boolean(appConfig.lockLanguage);
const startupLanguage = appConfig.language === "en" ? "en" : "sr";

const dict = {
  sr: {
    title: "Modern panel za watch winder",
    subtitle: "Stabilna kontrola za single ili double firmware",
    browserTitle: "Watch Winder Panel - SR",
    language: "Jezik",
    deviceUrl: "URL uredjaja",
    deviceType: "Tip uredjaja",
    timer: "Tajmer (min, 0 bez limita)",
    saveDevice: "Sacuvaj",
    refresh: "Osvezi",
    start: "Start",
    stop: "Stop",
    single: "Single",
    double: "Double",
    saveSettings: "Sacuvaj podesavanja",
    targetTpd: "Target TPD",
    direction: "Smer",
    mode: "Mode",
    speed: "Brzina",
    activeHours: "Aktivni sati",
    smartSwitch: "Smart promena smera (rot)",
    quietMode: "Quiet mode",
    quietHours: "Quiet sati",
    nightCap: "Night speed cap",
    status: "Status",
    connected: "Povezano",
    offline: "Van mreze",
    state: "Stanje",
    error: "Greska",
    network: "Mreza",
    rotToday: "Rot danas",
    rotHour: "Rot sat",
    drift: "Drift %",
    progress: "Progres",
    remaining: "Preostalo",
    heap: "Memorija",
    uptime: "Uptime",
    startHour: "od",
    endHour: "do",
    timerOff: "iskljucen",
    fetchFailed: "Uredjaj nije dostupan",
    httpError: "Server greska"
  },
  en: {
    title: "Modern watch winder panel",
    subtitle: "Stable controller for single or double firmware",
    browserTitle: "Watch Winder Dashboard",
    language: "Language",
    deviceUrl: "Device URL",
    deviceType: "Device type",
    timer: "Timer (min, 0 unlimited)",
    saveDevice: "Save",
    refresh: "Refresh",
    start: "Start",
    stop: "Stop",
    single: "Single",
    double: "Double",
    saveSettings: "Save settings",
    targetTpd: "Target TPD",
    direction: "Direction",
    mode: "Mode",
    speed: "Speed",
    activeHours: "Active hours",
    smartSwitch: "Smart direction switch (rot)",
    quietMode: "Quiet mode",
    quietHours: "Quiet hours",
    nightCap: "Night speed cap",
    status: "Status",
    connected: "Connected",
    offline: "Offline",
    state: "State",
    error: "Error",
    network: "Network",
    rotToday: "Rot today",
    rotHour: "Rot hour",
    drift: "Drift %",
    progress: "Progress",
    remaining: "Remaining",
    heap: "Heap",
    uptime: "Uptime",
    startHour: "start",
    endHour: "end",
    timerOff: "off",
    fetchFailed: "Device is unreachable",
    httpError: "Server error"
  }
};

function localizeStatusError(message, t) {
  const raw = String(message || "").trim();
  if (!raw) return "";

  if (/failed to fetch|networkerror|network request failed|load failed/i.test(raw)) {
    return t.fetchFailed;
  }

  const httpMatch = raw.match(/^HTTP\s+(\d{3})$/i);
  if (httpMatch) {
    return `${t.httpError} ${httpMatch[1]}`;
  }

  return raw;
}

function CustomSelect({ value, options, onChange }) {
  const [open, setOpen] = useState(false);
  const current = options.find((o) => o.value === value) || options[0];

  return (
    <div className="custom-select" tabIndex={0} onBlur={() => setOpen(false)}>
      <button type="button" className="select-trigger" onClick={() => setOpen((v) => !v)}>
        <span>{current.label}</span>
        <span className="caret">▾</span>
      </button>
      {open && (
        <div className="select-menu">
          {options.map((opt) => (
            <button
              type="button"
              key={opt.value}
              className={`select-option ${opt.value === value ? "active" : ""}`}
              onMouseDown={(e) => {
                e.preventDefault();
                onChange(opt.value);
                setOpen(false);
              }}
            >
              {opt.label}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}

function ModeButtons({ value, onChange, disabled, labels }) {
  return (
    <div className="pill-group">
      <button type="button" className={value === "single" ? "pill active" : "pill"} disabled={disabled} onClick={() => onChange("single")}>
        {labels.single}
      </button>
      <button type="button" className={value === "double" ? "pill active" : "pill"} disabled={disabled} onClick={() => onChange("double")}>
        {labels.double}
      </button>
    </div>
  );
}

function ToggleSwitch({ checked, onChange }) {
  return (
    <button
      type="button"
      className={checked ? "switch on" : "switch"}
      onClick={() => onChange(!checked)}
      aria-pressed={checked}
    >
      <span className="switch-knob" />
    </button>
  );
}

function normalizeUrl(url) {
  const trimmed = String(url || "").trim();
  if (!trimmed) return defaultUrl;
  const prefixed = /^https?:\/\//i.test(trimmed) ? trimmed : `http://${trimmed}`;
  return prefixed.replace(/\/$/, "");
}

export default function App() {
  const [language, setLanguage] = useState(startupLanguage);
  const t = useMemo(() => dict[language] || dict.sr, [language]);

  const saved = useMemo(() => {
    try {
      return JSON.parse(localStorage.getItem("modern-device") || "{}");
    } catch {
      return {};
    }
  }, []);

  const [deviceUrl, setDeviceUrl] = useState(lockDeviceUrl ? defaultUrl : normalizeUrl(saved.url || defaultUrl));
  const [mode, setMode] = useState(lockDeviceMode ? startupMode : saved.mode || startupMode);
  const [runMinutes, setRunMinutes] = useState(0);
  const [statusDetail, setStatusDetail] = useState("");
  const [connected, setConnected] = useState(false);

  const [single, setSingle] = useState({
    targetTPD: 650,
    direction: "BIDIR",
    mode: "STANDARD",
    speed: 3,
    activeStart: 0,
    activeEnd: 23,
    smartSwitch: 10,
    quietMode: true,
    quietStart: 22,
    quietEnd: 7,
    nightCap: 2
  });

  const [doubleCfg, setDoubleCfg] = useState({
    targetTPD1: 650,
    direction1: "BIDIR",
    mode1: "STANDARD",
    speed1: 3,
    activeStart1: 0,
    activeEnd1: 23,
    smartSwitch1: 10,
    targetTPD2: 650,
    direction2: "BIDIR",
    mode2: "STANDARD",
    speed2: 3,
    activeStart2: 0,
    activeEnd2: 23,
    smartSwitch2: 10,
    quietMode: true,
    quietStart: 22,
    quietEnd: 7,
    nightCap: 2
  });

  const [metrics, setMetrics] = useState({
    state: "-",
    errorCode: "-",
    timer: "-",
    rotationsToday: "-",
    rotationsLastHour: "-",
    currentTPD: "-",
    driftPercent: "-",
    progress: "-",
    remaining: "-",
    heap: "-",
    uptime: "-",
    network: "-"
  });

  useEffect(() => {
    setLanguage(startupLanguage);
    localStorage.setItem("modern-lang", startupLanguage);
  }, []);

  useEffect(() => {
    localStorage.setItem("modern-lang", language);
  }, [language]);

  useEffect(() => {
    document.title = t.browserTitle;
  }, [t]);

  useEffect(() => {
    localStorage.setItem("modern-device", JSON.stringify({
      url: deviceUrl,
      mode
    }));
  }, [deviceUrl, mode]);

  async function apiFetch(path, options = {}) {
    const base = normalizeUrl(lockDeviceUrl ? defaultUrl : deviceUrl);
    const res = await fetch(`${base}${path}`, options);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.json();
  }

  function applyStatus(data) {
    const runtime = data.runtime || {};
    setMetrics((m) => ({
      ...m,
      state: runtime.motorState || `${runtime.motor1State || "-"} / ${runtime.motor2State || "-"}`,
      errorCode: runtime.errorCode || "NONE",
      timer: runtime.timerActive ? `${Math.ceil((runtime.timerRemainingMs || 0) / 60000)} min` : "off",
      rotationsToday: runtime.rotationsToday ?? `${runtime.rotationsToday1 ?? "-"} / ${runtime.rotationsToday2 ?? "-"}`,
      rotationsLastHour: runtime.rotationsLastHour ?? `${runtime.rotationsLastHour1 ?? "-"} / ${runtime.rotationsLastHour2 ?? "-"}`,
      currentTPD: runtime.currentTPD ?? `${runtime.currentTPD1 ?? "-"} / ${runtime.currentTPD2 ?? "-"}`,
      driftPercent: runtime.driftPercent ?? `${runtime.driftPercent1 ?? "-"} / ${runtime.driftPercent2 ?? "-"}`,
      progress: runtime.currentTPDProgress ?? "-",
      remaining: runtime.remainingRotations ?? `${runtime.remainingRotations1 ?? "-"} / ${runtime.remainingRotations2 ?? "-"}`,
      uptime: runtime.uptime || runtime.uptimeMs || "-",
      network: `${data.network?.mode || "-"} (${data.network?.ip || "-"})`
    }));

    if (data.mode === "single") {
      setSingle((s) => ({
        ...s,
        targetTPD: data.config?.targetTPD ?? s.targetTPD,
        direction: data.config?.direction ?? s.direction,
        mode: data.config?.mode ?? s.mode,
        speed: data.config?.speed ?? s.speed,
        activeStart: data.config?.activeHours?.[0] ?? s.activeStart,
        activeEnd: data.config?.activeHours?.[1] ?? s.activeEnd,
        smartSwitch: data.config?.smartSwitchRotations ?? s.smartSwitch,
        quietMode: Boolean(data.config?.quietMode ?? s.quietMode),
        quietStart: data.config?.quietStartHour ?? s.quietStart,
        quietEnd: data.config?.quietEndHour ?? s.quietEnd,
        nightCap: data.config?.nightSpeedCap ?? s.nightCap
      }));
    } else {
      setDoubleCfg((d) => ({
        ...d,
        targetTPD1: data.config?.targetTPD1 ?? d.targetTPD1,
        direction1: data.config?.direction1 ?? d.direction1,
        mode1: data.config?.mode1 ?? d.mode1,
        speed1: data.config?.speed1 ?? d.speed1,
        activeStart1: data.config?.activeHours1?.[0] ?? d.activeStart1,
        activeEnd1: data.config?.activeHours1?.[1] ?? d.activeEnd1,
        smartSwitch1: data.config?.smartSwitchRotations1 ?? d.smartSwitch1,
        targetTPD2: data.config?.targetTPD2 ?? d.targetTPD2,
        direction2: data.config?.direction2 ?? d.direction2,
        mode2: data.config?.mode2 ?? d.mode2,
        speed2: data.config?.speed2 ?? d.speed2,
        activeStart2: data.config?.activeHours2?.[0] ?? d.activeStart2,
        activeEnd2: data.config?.activeHours2?.[1] ?? d.activeEnd2,
        smartSwitch2: data.config?.smartSwitchRotations2 ?? d.smartSwitch2,
        quietMode: Boolean(data.config?.quietMode ?? d.quietMode),
        quietStart: data.config?.quietStartHour ?? d.quietStart,
        quietEnd: data.config?.quietEndHour ?? d.quietEnd,
        nightCap: data.config?.nightSpeedCap ?? d.nightCap
      }));
    }

    setRunMinutes(data.config?.runMinutes ?? 0);
    setMode(lockDeviceMode ? startupMode : data.mode || mode);
  }

  async function refreshAll() {
    try {
      const status = await apiFetch("/api/status");
      applyStatus(status);
      const [stats, health] = await Promise.all([
        apiFetch("/api/stats").catch(() => null),
        apiFetch("/api/health").catch(() => null)
      ]);
      setMetrics((m) => ({
        ...m,
        rotationsToday: stats?.rotationsToday ?? m.rotationsToday,
        rotationsLastHour: stats?.rotationsLastHour ?? m.rotationsLastHour,
        currentTPD: stats?.currentTPD ?? m.currentTPD,
        driftPercent: stats?.driftPercent ?? m.driftPercent,
        errorCode: health?.errorCode || m.errorCode,
        heap: health?.heapFree ?? m.heap
      }));
      setConnected(true);
      setStatusDetail(new Date().toLocaleTimeString());
    } catch (err) {
      setConnected(false);
      setStatusDetail(err.message || "");
    }
  }
  const statusText = connected
    ? `${t.connected}${statusDetail ? ` ${statusDetail}` : ""}`
    : `${t.offline}${statusDetail ? ` (${localizeStatusError(statusDetail, t)})` : ""}`;


  useEffect(() => {
    refreshAll();
    const timer = setInterval(refreshAll, 5000);
    return () => clearInterval(timer);
  }, [deviceUrl, mode, language]);

  async function saveConfig() {
    const payload = mode === "single"
      ? {
          targetTPD: Number(single.targetTPD),
          direction: single.direction,
          mode: single.mode,
          speed: Number(single.speed),
          activeHours: [Number(single.activeStart), Number(single.activeEnd)],
          smartSwitchRotations: Number(single.smartSwitch),
          quietMode: single.quietMode ? 1 : 0,
          quietStartHour: Number(single.quietStart),
          quietEndHour: Number(single.quietEnd),
          nightSpeedCap: Number(single.nightCap),
          runMinutes: Number(runMinutes)
        }
      : {
          targetTPD1: Number(doubleCfg.targetTPD1),
          direction1: doubleCfg.direction1,
          mode1: doubleCfg.mode1,
          speed1: Number(doubleCfg.speed1),
          activeStartHour1: Number(doubleCfg.activeStart1),
          activeEndHour1: Number(doubleCfg.activeEnd1),
          smartSwitchRotations1: Number(doubleCfg.smartSwitch1),
          targetTPD2: Number(doubleCfg.targetTPD2),
          direction2: doubleCfg.direction2,
          mode2: doubleCfg.mode2,
          speed2: Number(doubleCfg.speed2),
          activeStartHour2: Number(doubleCfg.activeStart2),
          activeEndHour2: Number(doubleCfg.activeEnd2),
          smartSwitchRotations2: Number(doubleCfg.smartSwitch2),
          quietMode: doubleCfg.quietMode ? 1 : 0,
          quietStartHour: Number(doubleCfg.quietStart),
          quietEndHour: Number(doubleCfg.quietEnd),
          nightSpeedCap: Number(doubleCfg.nightCap),
          runMinutes: Number(runMinutes)
        };

    await apiFetch("/api/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    await refreshAll();
  }

  async function start() {
    await apiFetch("/api/start", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ runMinutes: Number(runMinutes) })
    });
    await refreshAll();
  }

  async function stop() {
    await apiFetch("/api/stop", { method: "POST" });
    await refreshAll();
  }

  const selectDirection = [
    { value: "CW", label: "CW" },
    { value: "CCW", label: "CCW" },
    { value: "BIDIR", label: "BIDIR" }
  ];

  const selectMode = [
    { value: "STANDARD", label: "STANDARD" },
    { value: "SMART", label: "SMART" }
  ];

  return (
    <main className="shell">
      <header className="hero modern-hero">
        <div>
          <h1>{t.title}</h1>
          <p>{t.subtitle}</p>
        </div>
        <div className="hero-controls">
          {!lockLanguage && (
            <label className="hero-lang">
              {t.language}
              <CustomSelect
                value={language}
                options={[
                  { value: "sr", label: "Srpski" },
                  { value: "en", label: "English" }
                ]}
                onChange={(v) => setLanguage(v)}
              />
            </label>
          )}
          <div className="signature-wrap">
            <img src="/filip-signature.svg" alt="Filip Andrić signature" className="signature-svg" />
          </div>
        </div>
      </header>

      <section className="panel">
        <div className="grid two">
          {!lockDeviceUrl && (
            <label>
              {t.deviceUrl}
              <input value={deviceUrl} onChange={(e) => setDeviceUrl(e.target.value)} />
            </label>
          )}

          {!lockLanguage && <div />}
        </div>

        <div className="grid two actions-row">
          <label>
            {t.timer}
            <input type="number" min="0" max="1440" value={runMinutes} onChange={(e) => setRunMinutes(e.target.value)} />
          </label>
          <div className="actions">
            {!lockDeviceUrl && <button className="btn secondary" onClick={() => setDeviceUrl(normalizeUrl(deviceUrl))}>{t.saveDevice}</button>}
            <button className="btn" onClick={refreshAll}>{t.refresh}</button>
            <button className="btn" onClick={start}>{t.start}</button>
            <button className="btn danger" onClick={stop}>{t.stop}</button>
          </div>
        </div>

        {!lockDeviceMode && (
          <label>
            {t.deviceType}
            <ModeButtons value={mode} onChange={setMode} disabled={false} labels={{ single: t.single, double: t.double }} />
          </label>
        )}

        <p className={connected ? "state ok" : "state"}>{statusText}</p>
      </section>

      <section className="grid two">
        {mode === "single" ? (
          <article className="panel">
            <h2>{t.single}</h2>
            <div className="grid two">
              <label>{t.targetTpd}<input type="number" min="500" max="1000" value={single.targetTPD} onChange={(e) => setSingle({ ...single, targetTPD: e.target.value })} /></label>
              <label>{t.speed}<input type="number" min="1" max="5" value={single.speed} onChange={(e) => setSingle({ ...single, speed: e.target.value })} /></label>
              <label>{t.direction}<CustomSelect value={single.direction} options={selectDirection} onChange={(v) => setSingle({ ...single, direction: v })} /></label>
              <label>{t.mode}<CustomSelect value={single.mode} options={selectMode} onChange={(v) => setSingle({ ...single, mode: v })} /></label>
              <label>{t.activeHours} {t.startHour}<input type="number" min="0" max="23" value={single.activeStart} onChange={(e) => setSingle({ ...single, activeStart: e.target.value })} /></label>
              <label>{t.activeHours} {t.endHour}<input type="number" min="0" max="23" value={single.activeEnd} onChange={(e) => setSingle({ ...single, activeEnd: e.target.value })} /></label>
              <label>{t.smartSwitch}<input type="number" min="2" max="100" value={single.smartSwitch} onChange={(e) => setSingle({ ...single, smartSwitch: e.target.value })} /></label>
              <label className="check">
                {t.quietMode}
                <ToggleSwitch checked={single.quietMode} onChange={(v) => setSingle({ ...single, quietMode: v })} />
              </label>
              <label>{t.quietHours} {t.startHour}<input type="number" min="0" max="23" value={single.quietStart} onChange={(e) => setSingle({ ...single, quietStart: e.target.value })} /></label>
              <label>{t.quietHours} {t.endHour}<input type="number" min="0" max="23" value={single.quietEnd} onChange={(e) => setSingle({ ...single, quietEnd: e.target.value })} /></label>
              <label>{t.nightCap}<input type="number" min="1" max="5" value={single.nightCap} onChange={(e) => setSingle({ ...single, nightCap: e.target.value })} /></label>
            </div>
            <button className="btn save-btn" onClick={saveConfig}>{t.saveSettings}</button>
          </article>
        ) : (
          <article className="panel">
            <h2>{t.double}</h2>
            <div className="grid two">
              <label>M1 {t.targetTpd}<input type="number" min="500" max="1000" value={doubleCfg.targetTPD1} onChange={(e) => setDoubleCfg({ ...doubleCfg, targetTPD1: e.target.value })} /></label>
              <label>M1 {t.speed}<input type="number" min="1" max="5" value={doubleCfg.speed1} onChange={(e) => setDoubleCfg({ ...doubleCfg, speed1: e.target.value })} /></label>
              <label>M1 {t.direction}<CustomSelect value={doubleCfg.direction1} options={selectDirection} onChange={(v) => setDoubleCfg({ ...doubleCfg, direction1: v })} /></label>
              <label>M1 {t.mode}<CustomSelect value={doubleCfg.mode1} options={selectMode} onChange={(v) => setDoubleCfg({ ...doubleCfg, mode1: v })} /></label>
              <label>M2 {t.targetTpd}<input type="number" min="500" max="1000" value={doubleCfg.targetTPD2} onChange={(e) => setDoubleCfg({ ...doubleCfg, targetTPD2: e.target.value })} /></label>
              <label>M2 {t.speed}<input type="number" min="1" max="5" value={doubleCfg.speed2} onChange={(e) => setDoubleCfg({ ...doubleCfg, speed2: e.target.value })} /></label>
              <label>M2 {t.direction}<CustomSelect value={doubleCfg.direction2} options={selectDirection} onChange={(v) => setDoubleCfg({ ...doubleCfg, direction2: v })} /></label>
              <label>M2 {t.mode}<CustomSelect value={doubleCfg.mode2} options={selectMode} onChange={(v) => setDoubleCfg({ ...doubleCfg, mode2: v })} /></label>
              <label className="check">
                {t.quietMode}
                <ToggleSwitch checked={doubleCfg.quietMode} onChange={(v) => setDoubleCfg({ ...doubleCfg, quietMode: v })} />
              </label>
              <label>{t.nightCap}<input type="number" min="1" max="5" value={doubleCfg.nightCap} onChange={(e) => setDoubleCfg({ ...doubleCfg, nightCap: e.target.value })} /></label>
            </div>
            <button className="btn save-btn" onClick={saveConfig}>{t.saveSettings}</button>
          </article>
        )}

        <article className="panel">
          <h2>{t.status}</h2>
          <div className="stats">
            <div><span>{t.state}</span><b>{metrics.state}</b></div>
            <div><span>{t.error}</span><b>{metrics.errorCode}</b></div>
            <div><span>{t.timer}</span><b>{metrics.timer === "off" ? t.timerOff : metrics.timer}</b></div>
            <div><span>{t.network}</span><b>{metrics.network}</b></div>
            <div><span>{t.rotToday}</span><b>{metrics.rotationsToday}</b></div>
            <div><span>{t.rotHour}</span><b>{metrics.rotationsLastHour}</b></div>
            <div><span>{t.targetTpd}</span><b>{metrics.currentTPD}</b></div>
            <div><span>{t.drift}</span><b>{metrics.driftPercent}</b></div>
            <div><span>{t.progress}</span><b>{metrics.progress}</b></div>
            <div><span>{t.remaining}</span><b>{metrics.remaining}</b></div>
            <div><span>{t.heap}</span><b>{metrics.heap}</b></div>
            <div><span>{t.uptime}</span><b>{metrics.uptime}</b></div>
          </div>
        </article>
      </section>
    </main>
  );
}
