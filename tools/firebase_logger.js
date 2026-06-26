const fs = require("fs");
const os = require("os");
const path = require("path");

const DATABASE_URL = "https://pametan-sef-default-rtdb.europe-west1.firebasedatabase.app";
const POLL_INTERVAL_MS = 1000;
const RUN_ONCE = process.argv.includes("--once");

const logDir = "C:\\Users\\dsusi\\OneDrive\\Desktop\\pametni-sef";
const logFile = path.join(logDir, "PAMETAN_SEF_LOG.txt");

let lastStatus = null;
let lastAlarm = null;
let lastAlarmReason = null;
let firstRead = true;

function formatDateTime(date = new Date()) {
  return new Intl.DateTimeFormat("sr-RS", {
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false
  }).format(date);
}

function statusLabel(status) {
  return status === "unlocked" ? "OTKLJUCANO" : "ZAKLJUCANO";
}

function appendLog(message) {
  fs.mkdirSync(logDir, { recursive: true });
  const line = `[${formatDateTime()}] ${message}\n`;
  fs.appendFileSync(logFile, line, "utf8");
  process.stdout.write(line);
}

async function readSafeState() {
  const response = await fetch(`${DATABASE_URL}/safe.json`);

  if (!response.ok) {
    throw new Error(`Firebase HTTP ${response.status}: ${response.statusText}`);
  }

  return response.json();
}

function handleSafeState(data) {
  if (!data || typeof data !== "object") {
    return;
  }

  const status = data.status === "unlocked" ? "unlocked" : "locked";
  const tilt = Number(data.tilt);
  const alarmReason = typeof data.alarmReason === "string" ? data.alarmReason : "";
  const alarm = data.alarm === true || (Number.isFinite(tilt) && tilt >= 45);
  const tiltText = Number.isFinite(tilt) ? `, nagib=${tilt.toFixed(1)} deg` : "";

  if (firstRead) {
    appendLog(`POCETNO STANJE: ${statusLabel(status)}; alarm=${alarm ? "UKLJUCEN" : "ISKLJUCEN"}${tiltText}`);
    firstRead = false;
  } else if (status !== lastStatus) {
    appendLog(`STATUS: ${statusLabel(status)}${tiltText}`);
  }

  if (alarm !== lastAlarm || (alarm && alarmReason !== lastAlarmReason)) {
    if (alarm) {
      appendLog(`ALARM UKLJUCEN: ${alarmReason || "Nagib preko 45 stepeni"}${tiltText}`);
    } else {
      appendLog(`ALARM ISKLJUCEN${tiltText}`);
    }
  }

  lastStatus = status;
  lastAlarm = alarm;
  lastAlarmReason = alarmReason;
}

async function poll() {
  try {
    const data = await readSafeState();
    handleSafeState(data);
  } catch (error) {
    appendLog(`GRESKA PRI CITANJU FIREBASE-A: ${error.message}`);
  }
}

console.log(`Upisujem Firebase log u: ${logFile}`);
appendLog("LOGGER POKRENUT");
if (RUN_ONCE) {
  poll().then(() => {
    console.log("Jednokratna provera zavrsena.");
  });
} else {
  console.log("Pritisni Ctrl+C za zaustavljanje loggera.");
  poll();
  setInterval(poll, POLL_INTERVAL_MS);
}
