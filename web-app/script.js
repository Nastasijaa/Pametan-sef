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

const elements = {
  connectionStatus: document.getElementById("connectionStatus"),
  alarmBanner: document.getElementById("alarmBanner"),
  resetAlarmTopBtn: document.getElementById("resetAlarmTopBtn"),
  safeStatus: document.getElementById("safeStatus"),
  safeVisual: document.getElementById("safeVisual"),
  alarmState: document.getElementById("alarmState"),
  lastEvent: document.getElementById("lastEvent"),
  securityLevel: document.getElementById("securityLevel"),
  breakInCount: document.getElementById("breakInCount"),
  lastUnlock: document.getElementById("lastUnlock"),
  lockBtn: document.getElementById("lockBtn"),
  unlockBtn: document.getElementById("unlockBtn"),
  resetAlarmBtn: document.getElementById("resetAlarmBtn"),
  movementValue: document.getElementById("movementValue"),
  movementBar: document.getElementById("movementBar"),
  tiltValue: document.getElementById("tiltValue"),
  tiltBar: document.getElementById("tiltBar"),
  vibrationValue: document.getElementById("vibrationValue"),
  vibrationDot: document.getElementById("vibrationDot"),
  sensorUpdated: document.getElementById("sensorUpdated"),
  cameraUrlInput: document.getElementById("cameraUrlInput"),
  saveCameraBtn: document.getElementById("saveCameraBtn"),
  cameraLink: document.getElementById("cameraLink"),
  eventsList: document.getElementById("eventsList")
};

function formatValue(value, fallback = 0) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function clampPercent(value, max) {
  return `${Math.min(100, Math.max(0, (value / max) * 100))}%`;
}

function getSecurityLevel(movement, tilt, vibration) {
  if (vibration || movement >= 65 || tilt >= 35) {
    return "HIGH";
  }

  if (movement >= 30 || tilt >= 15) {
    return "MEDIUM";
  }

  return "LOW";
}

function updateStatus(status) {
  const normalized = status === "unlocked" ? "unlocked" : "locked";
  elements.safeStatus.textContent = normalized === "locked" ? "Zakljucan" : "Otkljucan";
  elements.safeStatus.classList.toggle("locked", normalized === "locked");
  elements.safeStatus.classList.toggle("unlocked", normalized === "unlocked");
}

function updateCameraLink(cameraUrl) {
  const url = typeof cameraUrl === "string" ? cameraUrl.trim() : "";
  elements.cameraUrlInput.value = url;

  if (!url) {
    elements.cameraLink.removeAttribute("href");
    elements.cameraLink.classList.add("disabled");
    return;
  }

  elements.cameraLink.href = url;
  elements.cameraLink.classList.remove("disabled");
}

function renderSafe(data = {}) {
  const movement = formatValue(data.movement);
  const tilt = formatValue(data.tilt);
  const vibration = data.vibration === true;
  const alarm = data.alarm === true;

  updateStatus(data.status);

  elements.alarmState.textContent = alarm ? "Pokusaj obijanja" : "Normalno";
  elements.alarmBanner.classList.toggle("hidden", !alarm);
  elements.safeVisual.classList.toggle("alarm", alarm);
  elements.lastEvent.textContent = data.lastEvent || "Nema dogadjaja";
  elements.movementValue.textContent = movement.toFixed(1);
  elements.tiltValue.textContent = `${tilt.toFixed(1)} deg`;
  elements.vibrationValue.textContent = vibration ? "Da" : "Ne";
  elements.vibrationDot.classList.toggle("active", vibration);
  elements.movementBar.style.width = clampPercent(movement, 100);
  elements.tiltBar.style.width = clampPercent(tilt, 60);
  elements.securityLevel.textContent = getSecurityLevel(movement, tilt, vibration);
  elements.sensorUpdated.textContent = new Date().toLocaleTimeString("sr-RS");

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
    elements.eventsList.innerHTML = '<div class="empty-state">Nema ucitanih dogadjaja.</div>';
    return;
  }

  elements.eventsList.innerHTML = events.slice(0, 30).map((event) => {
    const type = String(event.type || "event");
    const typeClass = isAlarmEvent(event) ? "alarm" : isUnlockEvent(event) ? "unlock" : "";
    const message = event.message || "Dogadjaj bez opisa";

    return `
      <article class="event-item">
        <span class="event-type ${typeClass}">${type}</span>
        <span class="event-message">${message}</span>
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

function resetAlarm() {
  if (!refs) {
    return Promise.resolve();
  }

  return refs.safe.update({
    alarm: false,
    lastEvent: "Alarm resetovan iz web aplikacije"
  });
}

elements.lockBtn.addEventListener("click", () => writeCommand("lock"));
elements.unlockBtn.addEventListener("click", () => writeCommand("unlock"));
elements.resetAlarmBtn.addEventListener("click", resetAlarm);
elements.resetAlarmTopBtn.addEventListener("click", resetAlarm);
elements.saveCameraBtn.addEventListener("click", () => {
  if (!refs) {
    return;
  }

  refs.safe.update({ cameraUrl: elements.cameraUrlInput.value.trim() });
});

renderSafe({
  status: "locked",
  alarm: false,
  movement: 0,
  tilt: 0,
  vibration: false,
  lastEvent: isFirebaseConfigured ? "Cekanje Firebase podataka" : "Unesi Firebase konfiguraciju u script.js"
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
