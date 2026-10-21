// Config creator for senseBox:home v2 (see index.html). The checks mirror
// lib/config/src/ConfigCodec.cpp so a config that passes here is accepted by the box; the box
// still validates everything itself.
"use strict";

// Phenomena per driver, in the order "All values of all sensors" adds them (Olpe order for PM).
const SENSOR_TYPES = {
  hdc1080: ["air_temperature", "relative_humidity"],
  am2315c: ["air_temperature", "relative_humidity"],
  sps30: ["pm2_5", "pm10", "pm1", "pm4"],
};
const PHENOMENA = ["air_temperature", "relative_humidity", "pm1", "pm2_5", "pm4", "pm10"];
const LPP_TYPES = ["temperature", "relative_humidity", "analog_in", "digital_in", "luminosity", "barometric_pressure"];
// LPP type per phenomenon; also the field prefix in TTN's CayenneLPP formatter.
const LPP_FOR = { air_temperature: "temperature", relative_humidity: "relative_humidity" };
const lppFor = (p) => LPP_FOR[p] || "analog_in";

const MAX_SENSORS = 8, MAX_CHANNELS = 16, NAME_MAX = 31, LINE_MAX = 2047;
const HEX = (n) => new RegExp("^[0-9A-Fa-f]{" + n + "}$");

const $ = (id) => document.getElementById(id);
const clone = (o) => JSON.parse(JSON.stringify(o));

let cfg = { schemaVersion: 1, station: {}, sensors: [] };
let savedLoRaWan = null;  // uplink kept while "none" is selected, so switching back restores it
let presetName = "config";
let formatterSource = null;

// ---------------------------------------------------------------------------- templates (templates.js)

function loadTemplates() {
  const sel = $("preset");
  sel.innerHTML = TEMPLATES.map((t) => `<option value="${t.id}">${t.label}</option>`).join("");
  applyTemplate(TEMPLATES[0].id);
}

function applyTemplate(id) {
  const t = TEMPLATES.find((x) => x.id === id);
  const name = cfg.station && cfg.station.name;  // keep what was typed when switching templates
  cfg = clone(t.config);
  cfg.station = cfg.station || {};
  if (name) cfg.station.name = name;
  cfg.sensors = cfg.sensors || [];
  savedLoRaWan = null;
  presetName = t.id;
  render();
}

// ---------------------------------------------------------------------------- form

const uplink = () => (cfg.uplinks && cfg.uplinks[0]) || null;

function options(list, value) {
  return list.map((v) => `<option${v === value ? " selected" : ""}>${v}</option>`).join("");
}

function render() {
  $("name").value = cfg.station.name || "";
  renderSensors();
  const u = uplink();
  $("uplink").value = u ? u.type : "none";
  $("interval").value = u ? u.intervalSec ?? 600 : "";
  $("devEui").value = u ? u.devEui || "" : "";
  $("joinEui").value = u ? u.joinEui || "" : "";
  renderChannels();
  update();
}

function renderSensors() {
  $("sensors").innerHTML = cfg.sensors.map((s, i) => `<tr>
    <td><input type="number" min="0" max="7" value="${s.slot}" data-i="${i}" data-k="slot" style="width:4em"></td>
    <td><select data-i="${i}" data-k="type">${options(Object.keys(SENSOR_TYPES), s.type)}</select></td>
    <td><input type="number" min="1" max="86400" value="${s.intervalSec ?? 60}" data-i="${i}" data-k="intervalSec"></td>
    <td>${s.type === "sps30" ? `<input type="number" min="0" step="any" value="${s.fanCleaningIntervalDays ?? 4}" data-i="${i}" data-k="fanCleaningIntervalDays">` : ""}</td>
    <td><button class="x" data-del="${i}" title="Remove">×</button></td></tr>`).join("");
}

function renderChannels() {
  const u = uplink();
  const lw = !!u;
  document.querySelectorAll(".lw").forEach((el) => (el.style.display = lw ? "" : "none"));
  if (!lw) return;
  const cayenne = u.type === "lorawan-cayenne";
  document.querySelectorAll(".cay").forEach((el) => (el.style.display = cayenne ? "" : "none"));
  const slots = cfg.sensors.map((s) => s.slot);
  $("channels").innerHTML = (u.channels || []).map((c, i) => `<tr>
    <td>${i + 1}</td>
    <td><select data-i="${i}" data-k="slot">${slots.map((s) => `<option value="${s}"${s === c.slot ? " selected" : ""}>${s} (${sensorType(s) || "?"})</option>`).join("")}
      ${slots.includes(c.slot) ? "" : `<option selected value="${c.slot}">${c.slot} (missing)</option>`}</select></td>
    <td><select data-i="${i}" data-k="phenomenon">${options(PHENOMENA, c.phenomenon)}</select></td>
    ${cayenne ? `<td><input type="number" min="0" max="255" value="${c.ch ?? ""}" data-i="${i}" data-k="ch" style="width:5em"></td>
    <td><select data-i="${i}" data-k="lpp">${options(LPP_TYPES, c.lpp)}</select></td>` : ""}
    <td><button class="x" data-del="${i}" title="Remove">×</button></td></tr>`).join("");
}

function sensorType(slot) {
  const s = cfg.sensors.find((x) => x.slot === slot);
  return s && s.type;
}

// Rewrites the channel fields that differ between the two LoRaWAN formats.
function fitChannels(u) {
  (u.channels || []).forEach((c, i) => {
    if (u.type === "lorawan-compact") {
      delete c.ch;
      delete c.lpp;
    } else {
      c.ch = c.ch ?? i + 1;
      c.lpp = c.lpp || lppFor(c.phenomenon);
    }
  });
}

function allChannels() {
  const u = uplink();
  u.channels = [];
  for (const s of cfg.sensors) {
    for (const p of SENSOR_TYPES[s.type] || []) u.channels.push({ slot: s.slot, phenomenon: p });
  }
  fitChannels(u);
}

function bindForm() {
  $("preset").addEventListener("change", (e) => applyTemplate(e.target.value));
  $("name").addEventListener("input", (e) => ((cfg.station.name = e.target.value), update()));

  $("sensors").addEventListener("input", (e) => {
    const { i, k } = e.target.dataset;
    if (i === undefined) return;
    const s = cfg.sensors[+i];
    if (k === "type") {
      s.type = e.target.value;
      if (s.type === "sps30") s.fanCleaningIntervalDays = s.fanCleaningIntervalDays ?? 4;
      else delete s.fanCleaningIntervalDays;
      renderSensors();
      renderChannels();
    } else {
      s[k] = e.target.value === "" ? undefined : Number(e.target.value);
      if (k === "slot") renderChannels();
    }
    update();
  });
  $("sensors").addEventListener("click", (e) => {
    if (e.target.dataset.del === undefined) return;
    cfg.sensors.splice(+e.target.dataset.del, 1);
    renderSensors();
    renderChannels();
    update();
  });
  $("addSensor").addEventListener("click", () => {
    const used = cfg.sensors.map((s) => s.slot);
    const slot = [0, 1, 2, 3, 4, 5, 6, 7].find((n) => !used.includes(n));
    if (slot === undefined) return;
    cfg.sensors.push({ slot, type: "hdc1080", intervalSec: 60 });
    renderSensors();
    renderChannels();
    update();
  });

  $("uplink").addEventListener("change", (e) => {
    const type = e.target.value;
    const u = uplink();
    if (type === "none") {
      if (u) savedLoRaWan = u;
      delete cfg.uplinks;
    } else {
      const next = u || savedLoRaWan || { devEui: "", joinEui: "", intervalSec: 600, channels: [] };
      next.type = type;
      cfg.uplinks = [next];
      if (!next.channels.length) allChannels();
      fitChannels(next);
    }
    render();
  });
  $("interval").addEventListener("input", (e) => ((uplink().intervalSec = Number(e.target.value)), update()));
  for (const id of ["devEui", "joinEui"]) {
    $(id).addEventListener("input", (e) => ((uplink()[id] = e.target.value.trim().toUpperCase()), update()));
  }
  $("appKey").addEventListener("input", update);

  $("channels").addEventListener("input", (e) => {
    const { i, k } = e.target.dataset;
    if (i === undefined) return;
    const c = uplink().channels[+i];
    if (k === "slot" || k === "ch") c[k] = e.target.value === "" ? undefined : Number(e.target.value);
    else c[k] = e.target.value;
    if (k === "phenomenon" && uplink().type === "lorawan-cayenne") {
      c.lpp = lppFor(c.phenomenon);
      renderChannels();
    }
    update();
  });
  $("channels").addEventListener("click", (e) => {
    if (e.target.dataset.del === undefined) return;
    uplink().channels.splice(+e.target.dataset.del, 1);
    renderChannels();
    update();
  });
  $("addChannel").addEventListener("click", () => {
    const u = uplink();
    const s = cfg.sensors[0];
    u.channels.push({ slot: s ? s.slot : 0, phenomenon: s ? SENSOR_TYPES[s.type][0] : PHENOMENA[0] });
    fitChannels(u);
    renderChannels();
    update();
  });
  $("autoChannels").addEventListener("click", () => (allChannels(), renderChannels(), update()));
}

// ---------------------------------------------------------------------------- checks + output

function check() {
  const errors = [], warnings = [];
  const int = (v, min, max) => Number.isInteger(v) && v >= min && v <= max;

  if ((cfg.station.name || "").length > NAME_MAX) errors.push(`Station name: at most ${NAME_MAX} characters`);
  if (cfg.sensors.length > MAX_SENSORS) errors.push(`At most ${MAX_SENSORS} sensors`);
  const slots = new Set();
  cfg.sensors.forEach((s, i) => {
    if (!int(s.slot, 0, 7)) errors.push(`Sensor ${i + 1}: slot must be 0–7`);
    else if (slots.has(s.slot)) errors.push(`Sensor ${i + 1}: slot ${s.slot} is used twice`);
    slots.add(s.slot);
    if (!int(s.intervalSec, 1, 86400)) errors.push(`Sensor ${i + 1}: interval must be 1–86400 s`);
  });

  const u = uplink();
  const appKey = $("appKey").value.trim();
  if (u) {
    if (!int(u.intervalSec, 60, 86400)) errors.push("Upload interval must be 60–86400 s");
    for (const k of ["devEui", "joinEui"]) {
      if (u[k] && !HEX(16).test(u[k])) errors.push(`${k}: 16 hex digits`);
    }
    if (appKey && !HEX(32).test(appKey)) errors.push("AppKey: 32 hex digits");
    if (!u.devEui || !u.joinEui || !appKey) {
      warnings.push("LoRaWAN stays off until DevEUI, JoinEUI and AppKey are set (they can also be set later in the serial monitor).");
    }
    const chans = u.channels || [];
    if (!chans.length) errors.push("Add at least one channel");
    if (chans.length > MAX_CHANNELS) errors.push(`At most ${MAX_CHANNELS} channels`);
    const seen = new Set(), seenCh = new Set();
    chans.forEach((c, i) => {
      const n = `Channel ${i + 1}`;
      const type = sensorType(c.slot);
      if (!type) errors.push(`${n}: sensor slot ${c.slot} is not configured`);
      else if (!SENSOR_TYPES[type].includes(c.phenomenon)) errors.push(`${n}: ${type} doesn't measure ${c.phenomenon}`);
      const key = c.slot + "/" + c.phenomenon;
      if (seen.has(key)) errors.push(`${n}: slot ${c.slot} ${c.phenomenon} is sent twice`);
      seen.add(key);
      if (u.type === "lorawan-cayenne") {
        if (!int(c.ch, 0, 255)) errors.push(`${n}: LPP channel must be 0–255`);
        else if (seenCh.has(c.ch)) errors.push(`${n}: LPP channel ${c.ch} is used twice`);
        seenCh.add(c.ch);
      }
    });
  }
  ["devEui", "joinEui"].forEach((id) => $(id).classList.toggle("bad", !!u && !!u[id] && !HEX(16).test(u[id])));
  $("appKey").classList.toggle("bad", !!appKey && !HEX(32).test(appKey));

  const line = "config-set " + JSON.stringify(cfg);
  if (line.length > LINE_MAX) errors.push(`Config is ${line.length} characters, the serial monitor line takes at most ${LINE_MAX}`);
  return { errors, warnings, line, appKey };
}

function update() {
  const { errors, warnings, line, appKey } = check();
  $("errors").innerHTML = errors.map((e) => `<li>${e}</li>`).join("") +
    warnings.map((w) => `<li class="warn">${w}</li>`).join("");
  const ok = errors.length === 0;
  $("out").style.opacity = ok ? "" : ".5";
  $("download").disabled = !ok;
  renderCommands(ok ? commands(line, appKey) : []);
  renderFormatter(ok);
}

// The commands for the serial monitor, one per line, with the answer the box gives.
function commands(line, appKey) {
  const cmds = [{ title: "Store the config", line, answer: "ok: config stored, reboot to apply" }];
  if (appKey && uplink()) {
    cmds.push({ title: "Store the AppKey", line: "appkey " + appKey.toUpperCase(), answer: "ok: AppKey stored, reboot to apply" });
  }
  cmds.push({ title: "Restart the box", line: "reboot", answer: "the box restarts with the new config" });
  return cmds;
}

function renderCommands(cmds) {
  const el = $("cmds");
  el.innerHTML = "";
  cmds.forEach((c, i) => {
    const div = document.createElement("div");
    div.className = "cmd";
    div.innerHTML = `<div class="head"><span>${i + 1}. ${c.title}</span><button class="sec">Copy</button></div>
      <pre></pre><div class="sub">Answer: <code></code></div>`;
    div.querySelector("pre").textContent = c.line;
    div.querySelector("code").textContent = c.answer;
    const button = div.querySelector("button");
    button.addEventListener("click", () => copy(c.line, button));
    el.appendChild(div);
  });
}

async function renderFormatter(ok) {
  const u = uplink();
  const el = $("fmt");
  if (!ok || !u) {
    el.innerHTML = "";
    return;
  }
  if (u.type === "lorawan-cayenne") {
    el.innerHTML = `<h3>TTN payload formatter</h3><p>Use TTN's built-in <b>CayenneLPP</b> formatter. Decoded field names
      (for the openSenseMap <code>decodeOptions</code>):</p><pre>${u.channels.map((c) => `${c.lpp}_${c.ch}  ←  slot ${c.slot} ${c.phenomenon}`).join("\n")}</pre>`;
    return;
  }
  if (formatterSource === null) {
    try {
      formatterSource = await (await fetch("formatters/ttn-uplink-formatter.js")).text();
    } catch (e) {
      formatterSource = "";
    }
  }
  const layout = "var LAYOUT = [\n" + u.channels.map((c, i) =>
    `  { phenomenon: "${c.phenomenon}", field: "${lppFor(c.phenomenon)}_${i + 1}" }`).join(",\n") + "\n];";
  const code = formatterSource ? formatterSource.replace(/var LAYOUT = \[[\s\S]*?\];/, layout) : layout;
  const intro = formatterSource
    ? `Paste this into TTN → Payload formatters → Uplink → <i>Custom Javascript formatter</i>. The <code>LAYOUT</code> matches the channels above.`
    : `Take <code>tools/ttn-uplink-formatter.js</code> from the repository and replace its <code>LAYOUT</code> with this one, which matches the channels above.`;
  el.innerHTML = `<h3>TTN payload formatter</h3><p>${intro}</p><textarea id="fmtCode" readonly></textarea>
    <div class="row"><button class="sec" id="copyFmt">Copy formatter</button></div>`;
  $("fmtCode").value = code;
  $("copyFmt").onclick = () => copy(code, $("copyFmt"));
}

async function copy(text, button) {
  await navigator.clipboard.writeText(text);
  const label = button.textContent;
  button.textContent = "Copied";
  setTimeout(() => (button.textContent = label), 1500);
}

function bindOutput() {
  $("download").addEventListener("click", () => {
    const blob = new Blob([JSON.stringify(cfg, null, 2) + "\n"], { type: "application/json" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = (cfg.station.name || presetName).replace(/[^\w.-]+/g, "_") + ".json";
    a.click();
    URL.revokeObjectURL(a.href);
  });
}

// ---------------------------------------------------------------------------- start

bindForm();
bindOutput();
loadTemplates();
