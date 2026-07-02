// For Firebase JS SDK v7.20.0 and later, measurementId is optional
const firebaseConfig = {
  apiKey: "AIzaSyAalY6JMgPDSQB88GBPZUiAtMfelzE3puk",
  authDomain: "pametan-sef.firebaseapp.com",
  databaseURL: "https://pametan-sef-default-rtdb.europe-west1.firebasedatabase.app/",
  projectId: "pametan-sef",
  storageBucket: "pametan-sef.firebasestorage.app",
  messagingSenderId: "408365025227",
  appId: "1:408365025227:web:50f195029220248422d84e",
  measurementId: "G-WBPHKRQKN8"
};

const isFirebaseConfigured = Object.values(firebaseConfig).every((value) => {
  return typeof value === "string" && value.trim() !== "" && value.trim() !== "...";
});

let database = null;
let refs = null;
let pinOverlayVisible = false;

const ALARM_TILT_THRESHOLD_DEG = 30;
const ALARM_PIN = "000";

const elements = {
  connectionStatus: document.getElementById("connectionStatus"),
  alarmBanner: document.getElementById("alarmBanner"),
  safeStatus: document.getElementById("safeStatus"),
  safeVisual: document.getElementById("safeVisual"),
  alarmState: document.getElementById("alarmState"),
  lastEvent: document.getElementById("lastEvent"),
  securityLevel: document.getElementById("securityLevel"),
  breakInCount: document.getElementById("breakInCount"),
  lastUnlock: document.getElementById("lastUnlock"),
  lockBtn: document.getElementById("lockBtn"),
  unlockBtn: document.getElementById("unlockBtn"),
  movementValue: document.getElementById("movementValue"),
  movementBar: document.getElementById("movementBar"),
  tiltValue: document.getElementById("tiltValue"),
  tiltBar: document.getElementById("tiltBar"),
  sensorUpdated: document.getElementById("sensorUpdated"),
  cameraUrlInput: document.getElementById("cameraUrlInput"),
  saveCameraBtn: document.getElementById("saveCameraBtn"),
  cameraStream: document.getElementById("cameraStream"),
  cameraPlaceholder: document.getElementById("cameraPlaceholder"),
  cameraLink: document.getElementById("cameraLink"),
  eventsList: document.getElementById("eventsList"),
  pinOverlay: document.getElementById("pinOverlay"),
  pinForm: document.getElementById("pinForm"),
  pinInput: document.getElementById("pinInput"),
  pinError: document.getElementById("pinError")
};

function formatValue(value, fallback = 0) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function clampPercent(value, max) {
  return `${Math.min(100, Math.max(0, (value / max) * 100))}%`;
}

function getSecurityLevel(movement, tilt) {
  if (movement >= 65 || tilt >= 35) {
    return "LOW";
  }

  if (movement >= 30 || tilt >= 15) {
    return "MEDIUM";
  }

  return "HIGH";
}

function localizeDisplayText(value) {
  const text = String(value || "");
  const exact = {
    "Nema alarma": "Nema alarma",
    "Nagib preko 30 stepeni": "Nagib preko 30 stepeni",
    "Sef zakljucan komandom": "Sef zaključan komandom",
    "Sef otkljucan komandom": "Sef otključan komandom",
    "Sef je zakljucan iz web aplikacije": "Sef je zaključan iz web aplikacije",
    "Sef je otkljucan iz web aplikacije": "Sef je otključan iz web aplikacije",
    "Alarm utisan PIN kodom": "Alarm utišan PIN kodom",
    "Alarm je utisan ispravnim PIN kodom": "Alarm je utišan ispravnim PIN kodom",
    "ESP32 sistem pametnog sefa je pokrenut": "ESP32 sistem pametnog sefa je pokrenut",
    "Sistem pokrenut": "Sistem pokrenut",
    "alarm_silenced": "alarm utišan",
    "unlock": "otključavanje",
    "lock": "zaključavanje",
    "system": "sistem",
    "event": "događaj"
  };

  if (exact[text]) {
    return exact[text];
  }

  return text
    .replace(/zakljuc/gi, (match) => match[0] === match[0].toUpperCase() ? "Zaključ" : "zaključ")
    .replace(/otkljuc/gi, (match) => match[0] === match[0].toUpperCase() ? "Otključ" : "otključ")
    .replace(/dogadj/gi, (match) => match[0] === match[0].toUpperCase() ? "Događ" : "događ")
    .replace(/utisan/gi, (match) => match[0] === match[0].toUpperCase() ? "Utišan" : "utišan")
    .replace(/sacuvan/gi, (match) => match[0] === match[0].toUpperCase() ? "Sačuvan" : "sačuvan")
    .replace(/ucitan/gi, (match) => match[0] === match[0].toUpperCase() ? "Učitan" : "učitan")
    .replace(/pokusaj/gi, (match) => match[0] === match[0].toUpperCase() ? "Pokušaj" : "pokušaj")
    .replace(/sifra/gi, (match) => match[0] === match[0].toUpperCase() ? "Šifra" : "šifra");
}

function updateStatus(status) {
  const normalized = status === "unlocked" ? "unlocked" : "locked";
  elements.safeStatus.textContent = normalized === "locked" ? "Zaključan" : "Otključan";
  elements.safeStatus.classList.toggle("locked", normalized === "locked");
  elements.safeStatus.classList.toggle("unlocked", normalized === "unlocked");
}

function updateCameraLink(cameraUrl, forceReload = false) {
  const url = typeof cameraUrl === "string" ? cameraUrl.trim() : "";
  elements.cameraUrlInput.value = url;

  if (!url) {
    elements.cameraStream.removeAttribute("src");
    elements.cameraStream.classList.add("hidden");
    elements.cameraPlaceholder.textContent = "Nema sačuvanog stream URL-a";
    elements.cameraPlaceholder.classList.remove("hidden");
    elements.cameraLink.removeAttribute("href");
    elements.cameraLink.classList.add("disabled");
    return;
  }

  if (forceReload || elements.cameraStream.src !== url) {
    elements.cameraPlaceholder.textContent = "Učitavanje stream-a...";
    elements.cameraPlaceholder.classList.remove("hidden");
    const separator = url.includes("?") ? "&" : "?";
    elements.cameraStream.src = forceReload ? `${url}${separator}t=${Date.now()}` : url;
  }

  elements.cameraStream.classList.remove("hidden");
  elements.cameraLink.href = url;
  elements.cameraLink.classList.remove("disabled");
}

function setPinOverlayVisible(visible) {
  if (pinOverlayVisible === visible) {
    return;
  }

  pinOverlayVisible = visible;
  elements.pinOverlay.classList.toggle("hidden", !visible);

  if (visible) {
    requestAnimationFrame(() => elements.pinInput.focus());
  } else {
    elements.pinInput.value = "";
    elements.pinError.classList.add("hidden");
  }
}

function renderSafe(data = {}) {
  const movement = formatValue(data.movement);
  const tilt = formatValue(data.tilt);
  const alarm = data.alarm === true || tilt >= ALARM_TILT_THRESHOLD_DEG;
  const alarmSilenced = data.alarmSilenced === true;

  updateStatus(data.status);

  elements.alarmState.textContent = alarm
    ? alarmSilenced ? "Alarm utišan" : "Nagib preko 30 deg"
    : "Normalno";
  elements.alarmBanner.classList.toggle("hidden", !alarm);
  elements.safeVisual.classList.toggle("alarm", alarm);
  elements.lastEvent.textContent = localizeDisplayText(data.lastEvent || "Nema događaja");
  elements.movementValue.textContent = movement.toFixed(1);
  elements.tiltValue.textContent = `${tilt.toFixed(1)} deg`;
  elements.movementBar.style.width = clampPercent(movement, 100);
  elements.tiltBar.style.width = clampPercent(tilt, 60);
  elements.securityLevel.textContent = getSecurityLevel(movement, tilt);
  elements.sensorUpdated.textContent = new Date().toLocaleTimeString("sr-RS");

  setPinOverlayVisible(alarm && !alarmSilenced);
  updateCameraLink(data.cameraUrl);
}

function normalizeTimestamp(timestamp) {
  if (typeof timestamp === "number") {
    return timestamp;
  }

  if (typeof timestamp === "string") {
    const parsed = Date.parse(timestamp);
    return Number.isNaN(parsed) ? 0 : parsed;
  }

  return 0;
}

function formatTimestamp(timestamp) {
  const normalized = normalizeTimestamp(timestamp);
  if (!normalized) {
    return "-";
  }

  return new Date(normalized).toLocaleString("sr-RS");
}

function isAlarmEvent(event) {
  const type = String(event.type || "").toLowerCase();
  const message = String(event.message || "").toLowerCase();
  return type.includes("alarm") || type.includes("break") || message.includes("obij");
}

function isUnlockEvent(event) {
  const type = String(event.type || "").toLowerCase();
  const message = String(event.message || "").toLowerCase();
  return type.includes("unlock") || message.includes("otklj");
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#039;");
}

function renderEvents(snapshot) {
  const events = [];

  snapshot.forEach((child) => {
    events.push({
      id: child.key,
      ...child.val()
    });
  });

  events.sort((a, b) => normalizeTimestamp(b.timestamp) - normalizeTimestamp(a.timestamp));

  const alarmEvents = events.filter(isAlarmEvent);
  const lastUnlockEvent = events.find(isUnlockEvent);
  elements.breakInCount.textContent = alarmEvents.length;
  elements.lastUnlock.textContent = lastUnlockEvent ? formatTimestamp(lastUnlockEvent.timestamp) : "-";

  if (!events.length) {
    elements.eventsList.innerHTML = '<div class="empty-state">Nema učitanih događaja.</div>';
    return;
  }

  elements.eventsList.innerHTML = events.slice(0, 30).map((event) => {
    const type = localizeDisplayText(event.type || "event");
    const typeClass = isAlarmEvent(event) ? "alarm" : isUnlockEvent(event) ? "unlock" : "";
    const message = localizeDisplayText(event.message || "Događaj bez opisa");

    return `
      <article class="event-item">
        <span class="event-type ${typeClass}">${escapeHtml(type)}</span>
        <span class="event-message">${escapeHtml(message)}</span>
        <span class="event-time">${formatTimestamp(event.timestamp)}</span>
      </article>
    `;
  }).join("");
}

function setConnectionState(isOnline) {
  elements.connectionStatus.classList.toggle("online", isOnline);
  elements.connectionStatus.querySelector("span:last-child").textContent = isOnline ? "Online" : "Offline";
}

function writeCommand(command) {
  if (!refs) {
    return Promise.resolve();
  }

  const payload = command === "lock"
    ? { lock: true, unlock: false }
    : { unlock: true, lock: false };

  return refs.commands.update(payload);
}

function silenceAlarm() {
  if (!refs) {
    return Promise.resolve();
  }

  return refs.commands.update({ silenceAlarm: true });
}

elements.lockBtn.addEventListener("click", () => writeCommand("lock"));
elements.unlockBtn.addEventListener("click", () => writeCommand("unlock"));
elements.pinForm.addEventListener("submit", (event) => {
  event.preventDefault();

  if (elements.pinInput.value.trim() !== ALARM_PIN) {
    elements.pinError.classList.remove("hidden");
    elements.pinInput.select();
    return;
  }

  elements.pinError.classList.add("hidden");
  silenceAlarm();
});
elements.cameraStream.addEventListener("load", () => {
  elements.cameraPlaceholder.classList.add("hidden");
});
elements.cameraStream.addEventListener("error", () => {
  elements.cameraStream.classList.add("hidden");
  elements.cameraPlaceholder.textContent = "Stream nije dostupan";
  elements.cameraPlaceholder.classList.remove("hidden");
});
elements.saveCameraBtn.addEventListener("click", () => {
  if (!refs) {
    return;
  }

  const cameraUrl = elements.cameraUrlInput.value.trim();
  updateCameraLink(cameraUrl, true);
  refs.safe.update({ cameraUrl });
});

renderSafe({
  status: "locked",
  alarm: false,
  movement: 0,
  tilt: 0,
  lastEvent: isFirebaseConfigured ? "Čekanje Firebase podataka" : "Unesi Firebase konfiguraciju u script.js"
});

if (isFirebaseConfigured) {
  firebase.initializeApp(firebaseConfig);
  database = firebase.database();

  refs = {
    safe: database.ref("safe"),
    commands: database.ref("commands"),
    events: database.ref("events")
  };

  database.ref(".info/connected").on("value", (snapshot) => {
    setConnectionState(snapshot.val() === true);
  });

  refs.safe.on("value", (snapshot) => {
    renderSafe(snapshot.val() || {});
  });

  refs.events.on("value", renderEvents);
} else {
  setConnectionState(false);
}
