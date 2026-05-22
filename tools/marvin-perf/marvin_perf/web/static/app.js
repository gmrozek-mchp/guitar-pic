"use strict";

// ── Strip kind registry ─────────────────────────────────────────────────────
// Single source of truth for kind→panel mapping. Adding a new kind (SCORE,
// MINIMAP, etc.) is one entry here plus the matching CSS slot.

const STRIP_KIND_REGISTRY = {
  0: { id: "sensing", label: "Sensing line", color: "#6cb4ff", cadence: "60 Hz" },
  1: { id: "strike",  label: "Strike line",  color: "#4ade80", cadence: "60 Hz" },
};

// Display zoom for the shared frame-relative pane. Same factor for all kinds
// so spatial offsets between strips match the source frame.
const STRIP_PANE_ZOOM = 2;

// ── Stage colors for timeline scatter ───────────────────────────────────────

const STAGE_COLORS = {
  ISC_IRQ:            "#6cb4ff",
  FRAME_GUARD_DONE:   "#9c8cff",
  DETECTOR_BEGIN:     "#4ade80",
  DETECTOR_END:       "#22c55e",
  PATCH_PUBLISH:      "#facc15",
  VIDEO_PUBLISH:      "#fb923c",
  FBL_SEND:           "#f472b6",
  CDC_WRITE_COMPLETE: "#f87171",
};
const STAGE_FALLBACK = "#8a93a0";

// ── App state ───────────────────────────────────────────────────────────────

const state = {
  captureId: null,
  manifest: null,
  summary: null,
  health: null,
  rtos: null,
  records: [],            // all decoded records (Phase-1 captures fit in RAM)
  byKind: {},             // kind_id → array of records sorted by ts_counter
  timerFreqHz: 0,
  playheadTs: 0,          // ts_counter (raw ticks)
  ts0: 0,                 // ts_counter of first record (origin for ms display)
  fsm: "idle",            // idle | loaded | playing | paused
  speed: 1.0,
  rafHandle: null,
  rafLastWall: 0,
};

// ── DOM helpers ─────────────────────────────────────────────────────────────

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => Array.from(document.querySelectorAll(sel));

function setBanner(msg, kind = "info") {
  const el = $("#banner");
  if (!msg) { el.classList.add("hidden"); el.textContent = ""; return; }
  el.classList.remove("hidden", "error");
  if (kind === "error") el.classList.add("error");
  el.textContent = msg;
}

function setBadge(id, level, label) {
  const el = document.getElementById(id);
  el.classList.remove("green", "yellow", "red");
  if (level) el.classList.add(level);
  if (label) el.textContent = label;
}

function fmtMs(ticks) {
  if (!state.timerFreqHz) return `${ticks} t`;
  const ms = ((ticks - state.ts0) / state.timerFreqHz) * 1000.0;
  return `${ms.toFixed(3)} ms`;
}

function fillMeta(node, rows) {
  node.innerHTML = "";
  if (!rows || rows.length === 0) {
    node.classList.add("empty");
    return;
  }
  node.classList.remove("empty");
  for (const [k, v] of rows) {
    const ks = document.createElement("span"); ks.className = "k"; ks.textContent = k;
    const vs = document.createElement("span"); vs.className = "v"; vs.textContent = String(v);
    node.appendChild(ks); node.appendChild(vs);
  }
}

// ── API ─────────────────────────────────────────────────────────────────────

async function api(path, opts) {
  const res = await fetch(path, opts);
  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(`${res.status} ${res.statusText}: ${text}`);
  }
  return res.json();
}

async function openCapture(path) {
  const body = await api("/api/capture/open", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ path }),
  });
  await loadCapture(body.capture_id, body.manifest);
}

async function loadCapture(captureId, manifest) {
  setBanner("Loading…");
  state.captureId = captureId;
  state.manifest = manifest;

  const enc = encodeURIComponent(captureId);
  const [summary, health, rtos, recs] = await Promise.all([
    api(`/api/capture/${enc}/summary`),
    api(`/api/capture/${enc}/health`),
    api(`/api/capture/${enc}/rtos`),
    api(`/api/capture/${enc}/records?limit=100000`),
  ]);
  state.summary = summary;
  state.health = health;
  state.rtos = rtos;
  state.records = recs.records;
  state.timerFreqHz = summary.timer_freq_hz || 0;
  state.ts0 = state.records.length ? state.records[0].ts_counter : 0;
  state.playheadTs = state.ts0;
  state.fsm = "loaded";

  indexByKind();
  renderManifestBanner();
  renderInspector();
  renderBadges();
  renderTimeline();
  renderRtosPanel();
  renderStripSlots();
  updatePlayheadDisplay();
  setBanner("");
}

function indexByKind() {
  state.byKind = {};
  for (const r of state.records) {
    if (typeof r.kind !== "number") continue;
    (state.byKind[r.kind] ||= []).push(r);
  }
  for (const k of Object.keys(state.byKind)) {
    state.byKind[k].sort((a, b) => a.ts_counter - b.ts_counter);
  }
}

// ── Manifest / banner ───────────────────────────────────────────────────────

function renderManifestBanner() {
  const m = state.manifest;
  const s = state.summary;
  const msgs = [];
  if (s && s.schema_ok === false) {
    msgs.push(`schema mismatch: ${s.schema_message}`);
  }
  if (m && m.producer_capabilities && !m.producer_capabilities.includes("Strip")) {
    msgs.push("v1 capture — no strip records yet");
  }
  if (msgs.length) setBanner(msgs.join("  ·  "));
}

// ── Inspector tabs ──────────────────────────────────────────────────────────

function setupTabs() {
  $$(".tab").forEach((btn) => {
    btn.addEventListener("click", () => {
      const which = btn.dataset.tab;
      $$(".tab").forEach((b) => b.classList.toggle("active", b === btn));
      $$(".tab-pane").forEach((p) => {
        p.classList.toggle("hidden", p.id !== `tab-${which}`);
      });
    });
  });
}

// ── Inspector content ───────────────────────────────────────────────────────

function nearestAtOrBefore(records, ts) {
  // Binary search on ts_counter ascending.
  let lo = 0, hi = records.length - 1, best = -1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (records[mid].ts_counter <= ts) { best = mid; lo = mid + 1; }
    else hi = mid - 1;
  }
  return best >= 0 ? records[best] : null;
}

function renderInspector() {
  const m = state.manifest || {};
  fillMeta($("#session-meta"), [
    ["schema", m.schema_version ?? "—"],
    ["timer_hz", m.timer_freq_hz ?? "—"],
    ["fw_git", m.fw_git_short ?? "—"],
    ["captured", m.captured_at ?? "—"],
    ["epoch first", m.frame_epoch_first ?? "—"],
    ["epoch last", m.frame_epoch_last ?? "—"],
    ["records", m.n_records ?? state.records.length],
    ["producers", (m.producer_capabilities || []).join(", ") || "—"],
  ]);

  const s = state.summary || {};
  const drops = s.drops || {};
  fillMeta($("#drops-meta"), [
    ["state", drops.since_session_state ?? 0],
    ["patch", drops.since_session_patch ?? 0],
    ["sink B", drops.since_session_sink_bytes ?? 0],
    ["max Δstate", drops.max_state_delta ?? 0],
    ["max Δpatch", drops.max_patch_delta ?? 0],
    ["max Δsink", drops.max_sink_bytes_delta ?? 0],
  ]);

  const warns = (s.warnings || []);
  const ul = $("#warnings-list");
  ul.innerHTML = "";
  if (warns.length === 0) {
    ul.classList.add("empty");
  } else {
    ul.classList.remove("empty");
    for (const w of warns) {
      const li = document.createElement("li");
      li.textContent = `${w.kind}: ${w.message}`;
      ul.appendChild(li);
    }
  }

  updatePlayheadInspector();
}

function updatePlayheadInspector() {
  const rec = nearestAtOrBefore(state.records, state.playheadTs);
  if (!rec) { fillMeta($("#event-meta"), []); return; }
  const rows = [["type", rec.type]];
  for (const [k, v] of Object.entries(rec)) {
    if (k === "type") continue;
    rows.push([k, typeof v === "object" ? JSON.stringify(v) : v]);
  }
  fillMeta($("#event-meta"), rows);
}

// ── RTOS panel ──────────────────────────────────────────────────────────────

function renderRtosPanel() {
  const r = state.rtos;
  const tbl = $("#rtos-table");
  tbl.innerHTML = "";
  if (!r || !r.tasks || r.tasks.length === 0) {
    tbl.innerHTML = "<tr><td>(no RTOS records)</td></tr>";
    Plotly.purge("rtos-hwm-chart");
    return;
  }
  const head = document.createElement("tr");
  for (const h of ["task", "id", "min words", "first", "last", "samples"]) {
    const th = document.createElement("th"); th.textContent = h; head.appendChild(th);
  }
  tbl.appendChild(head);
  for (const t of r.tasks) {
    const tr = document.createElement("tr");
    const cells = [
      t.task_name, t.task_id,
      t.hwm_words.min ?? "—",
      t.hwm_words.first ?? "—",
      t.hwm_words.last ?? "—",
      (t.hwm_words.samples || []).length,
    ];
    cells.forEach((c, i) => {
      const td = document.createElement("td"); td.textContent = String(c);
      if (i === 2 && typeof c === "number") {
        if (c < 32) td.classList.add("danger");
        else if (c < 64) td.classList.add("warn");
      }
      tr.appendChild(td);
    });
    tbl.appendChild(tr);
  }

  // HWM trend per task: Plotly line chart.
  const traces = r.tasks.map((t) => {
    const xs = (t.hwm_words.samples || []).map(([ts]) => tickToMs(ts));
    const ys = (t.hwm_words.samples || []).map(([, w]) => w);
    return { x: xs, y: ys, name: t.task_name, mode: "lines+markers", type: "scatter" };
  });
  Plotly.newPlot("rtos-hwm-chart", traces, {
    margin: { l: 36, r: 8, t: 4, b: 28 },
    paper_bgcolor: "#1d2229", plot_bgcolor: "#1d2229",
    font: { color: "#d4d8de", size: 10 },
    xaxis: { title: "ms", gridcolor: "#303843", zerolinecolor: "#303843" },
    yaxis: { title: "free words", gridcolor: "#303843", zerolinecolor: "#303843" },
    legend: { orientation: "h", y: -0.25 },
    showlegend: true,
  }, { displayModeBar: false, responsive: true });
}

function tickToMs(ts) {
  if (!state.timerFreqHz) return ts;
  return ((ts - state.ts0) / state.timerFreqHz) * 1000.0;
}

// ── Badges ──────────────────────────────────────────────────────────────────

function renderBadges() {
  const h = state.health || {};
  const f = h.framing || {};
  const d = h.drops_since_session || {};
  let level = "green", label = "link ●";
  if ((f.crc_mismatches || 0) > 0 || (f.bytes_resync_dropped || 0) > 0) {
    level = "red"; label = `link ✗ ${f.crc_mismatches || 0} crc`;
  } else if ((d.state_records || 0) + (d.patch_records || 0) + (d.sink_bytes || 0) > 0) {
    level = "yellow"; label = "link drops";
  }
  setBadge("badge-link", level, label);

  const r = state.rtos || {};
  const t = r.totals || {};
  let rl = "green", rt = "rtos ●";
  if (t.any_task_below_32_words) { rl = "red"; rt = "rtos <32w"; }
  else if (t.any_task_below_64_words) { rl = "yellow"; rt = "rtos <64w"; }
  setBadge("badge-rtos", rl, rt);
}

// ── Strip slots ─────────────────────────────────────────────────────────────

function renderStripSlots() {
  const wrap = $("#strip-slots");
  wrap.innerHTML = "";
  const observedKinds = Object.keys(state.byKind).map(Number);
  const m = state.manifest || {};
  const hasStripProducer = (m.producer_capabilities || []).includes("Strip");

  if (observedKinds.length === 0 && !hasStripProducer) {
    $("#strip-hint").classList.remove("hidden");
    $("#strip-hint").textContent = "No STRIP records — Phase 2 wires producers.";
    return;
  }
  $("#strip-hint").classList.add("hidden");

  // Take the first record per kind as the canonical (x, y, w, h). All records
  // for a kind share the same crop window today (producer-side constants), so
  // bbox computed once is correct for every frame.
  const knownKinds = Object.keys(STRIP_KIND_REGISTRY).map(Number);
  const allKinds = Array.from(new Set([...knownKinds, ...observedKinds]));
  const positioned = [];
  const unpositioned = [];
  for (const kind of allKinds) {
    const cfg = STRIP_KIND_REGISTRY[kind];
    const id = cfg ? cfg.id : `kind-${kind}`;
    const label = cfg ? cfg.label : `Kind ${kind} (unknown)`;
    const color = cfg ? cfg.color : "#8a93a0";
    const recs = state.byKind[kind] || [];
    const sample = recs[0];
    if (sample) {
      positioned.push({ kind, id, label, color, recs,
                        x: sample.x, y: sample.y, w: sample.w, h: sample.h });
    } else {
      unpositioned.push({ kind, id, label, color });
    }
  }

  if (positioned.length > 0) {
    const minX = Math.min(...positioned.map((p) => p.x));
    const minY = Math.min(...positioned.map((p) => p.y));
    const maxX = Math.max(...positioned.map((p) => p.x + p.w));
    const maxY = Math.max(...positioned.map((p) => p.y + p.h));
    const z = STRIP_PANE_ZOOM;

    const legend = document.createElement("div");
    legend.className = "strip-legend";
    for (const p of positioned) {
      const li = document.createElement("span");
      li.className = "strip-legend-item";
      li.innerHTML = `<span class="dot" style="background:${p.color}"></span>` +
                     `${p.label} · ${p.w}×${p.h} @ (${p.x},${p.y}) · ${p.recs.length} samples`;
      legend.appendChild(li);
    }
    wrap.appendChild(legend);

    const pane = document.createElement("div");
    pane.className = "strip-pane";
    pane.style.width  = `${(maxX - minX) * z}px`;
    pane.style.height = `${(maxY - minY) * z}px`;
    pane.dataset.bboxOriginX = String(minX);
    pane.dataset.bboxOriginY = String(minY);

    for (const p of positioned) {
      const card = document.createElement("div");
      card.className = "strip-card positioned";
      card.dataset.kind = String(p.kind);
      card.style.left   = `${(p.x - minX) * z}px`;
      card.style.top    = `${(p.y - minY) * z}px`;
      card.style.width  = `${p.w * z}px`;
      card.style.height = `${p.h * z}px`;
      card.style.outlineColor = p.color;

      const canvas = document.createElement("canvas");
      canvas.id = `strip-canvas-${p.id}`;
      canvas.width  = p.w;
      canvas.height = p.h;
      canvas.style.width  = `${p.w * z}px`;
      canvas.style.height = `${p.h * z}px`;
      card.appendChild(canvas);
      pane.appendChild(card);
    }
    wrap.appendChild(pane);
  }

  if (unpositioned.length > 0) {
    const empty = document.createElement("div");
    empty.className = "strip-empty-list";
    for (const p of unpositioned) {
      const item = document.createElement("div");
      item.className = "strip-empty-item";
      item.innerHTML = `<span class="dot" style="background:${p.color}"></span>` +
                       `${p.label} · (no samples)`;
      empty.appendChild(item);
    }
    wrap.appendChild(empty);
  }
}

function updateStripsAtPlayhead() {
  const tolTicks = state.timerFreqHz ? state.timerFreqHz / 60 * 1.5 : Infinity;
  for (const kindStr of Object.keys(state.byKind)) {
    const kind = Number(kindStr);
    const recs = state.byKind[kind];
    const rec = nearestAtOrBefore(recs, state.playheadTs);
    if (!rec) continue;
    if (state.playheadTs - rec.ts_counter > tolTicks) continue;
    const cfg = STRIP_KIND_REGISTRY[kind];
    const id = cfg ? cfg.id : `kind-${kind}`;
    const canvas = document.getElementById(`strip-canvas-${id}`);
    if (!canvas) continue;
    const img = new Image();
    img.onload = () => {
      if (canvas.width !== img.width || canvas.height !== img.height) {
        canvas.width = img.width; canvas.height = img.height;
        canvas.style.width = `${img.width * STRIP_PANE_ZOOM}px`;
        canvas.style.height = `${img.height * STRIP_PANE_ZOOM}px`;
      }
      const ctx = canvas.getContext("2d");
      ctx.imageSmoothingEnabled = false;
      ctx.drawImage(img, 0, 0);
    };
    const enc = encodeURIComponent(state.captureId);
    img.src = `/api/capture/${enc}/strip/${rec.frame_epoch}/${cfg ? cfg.id : kind}.png`;
  }
}

// ── Timeline ────────────────────────────────────────────────────────────────

function renderTimeline() {
  const stamps = state.records.filter((r) => r.type === "Stamp");
  const drops = state.records.filter((r) => r.type === "Drop");

  const stampsByStage = {};
  for (const s of stamps) {
    (stampsByStage[s.stage] ||= []).push(s);
  }
  const stampTraces = Object.entries(stampsByStage).map(([stage, recs]) => ({
    x: recs.map((r) => tickToMs(r.ts_counter)),
    y: recs.map(() => stage),
    text: recs.map((r) => `epoch ${r.frame_epoch}`),
    name: stage,
    mode: "markers",
    type: "scatter",
    marker: { color: STAGE_COLORS[stage] || STAGE_FALLBACK, size: 8, symbol: "line-ns-open" },
  }));

  const dropTrace = {
    x: drops.map((r) => tickToMs(r.ts_counter)),
    y: drops.map(() => "DROP"),
    text: drops.map((r) => `state=${r.dropped_state} strip=${r.dropped_strip} sink=${r.dropped_sink}`),
    name: "Drop",
    mode: "markers",
    type: "scatter",
    marker: { color: "#f87171", size: 10, symbol: "triangle-down" },
  };

  const traces = [...stampTraces, dropTrace];

  const lastTs = state.records.length
    ? state.records[state.records.length - 1].ts_counter
    : state.ts0 + 1;
  const xMax = Math.max(1, tickToMs(lastTs));

  const layout = {
    margin: { l: 140, r: 12, t: 8, b: 32 },
    paper_bgcolor: "#1d2229",
    plot_bgcolor: "#1d2229",
    font: { color: "#d4d8de", size: 11 },
    xaxis: {
      title: "ms since session",
      range: [0, xMax],
      gridcolor: "#303843", zerolinecolor: "#303843",
    },
    yaxis: {
      type: "category",
      automargin: true,
      gridcolor: "#262d36",
    },
    showlegend: false,
    shapes: [playheadShape()],
  };

  Plotly.newPlot("timeline-chart", traces, layout, {
    displayModeBar: false, responsive: true,
  });

  $("#timeline-chart").on("plotly_click", (ev) => {
    if (!ev.points || !ev.points.length) return;
    const ms = ev.points[0].x;
    const ticks = state.ts0 + (ms / 1000.0) * state.timerFreqHz;
    seekTo(ticks);
  });
}

function playheadShape() {
  const xMs = tickToMs(state.playheadTs);
  return {
    type: "line",
    x0: xMs, x1: xMs, y0: 0, y1: 1,
    yref: "paper",
    line: { color: "#facc15", width: 1.5, dash: "dot" },
  };
}

function refreshPlayheadShape() {
  const node = document.getElementById("timeline-chart");
  if (!node || !node.layout) return;
  Plotly.relayout(node, { shapes: [playheadShape()] });
}

// ── Playhead / playback FSM ─────────────────────────────────────────────────

function updatePlayheadDisplay() {
  const rec = nearestAtOrBefore(state.records, state.playheadTs);
  $("#ph-epoch").textContent = rec ? rec.frame_epoch : "—";
  $("#ph-ts").textContent = state.records.length ? fmtMs(state.playheadTs) : "—";
  updatePlayheadInspector();
  updateStripsAtPlayhead();
  refreshPlayheadShape();
}

function seekTo(ticks) {
  if (!state.records.length) return;
  const first = state.records[0].ts_counter;
  const last = state.records[state.records.length - 1].ts_counter;
  state.playheadTs = Math.max(first, Math.min(last, ticks));
  updatePlayheadDisplay();
}

function stepEvent(dir) {
  if (!state.records.length) return;
  const cur = state.playheadTs;
  if (dir > 0) {
    const next = state.records.find((r) => r.ts_counter > cur);
    if (next) seekTo(next.ts_counter);
  } else {
    let prev = null;
    for (const r of state.records) {
      if (r.ts_counter < cur) prev = r; else break;
    }
    if (prev) seekTo(prev.ts_counter);
  }
}

function stepEpoch(dir) {
  if (!state.records.length) return;
  const curRec = nearestAtOrBefore(state.records, state.playheadTs);
  if (!curRec) return;
  const target = curRec.frame_epoch + (dir > 0 ? 1 : -1);
  let candidate = null;
  for (const r of state.records) {
    if (r.frame_epoch === target) {
      if (dir > 0) { candidate = r; break; }
      else { candidate = r; }
    }
  }
  if (candidate) seekTo(candidate.ts_counter);
}

function play() {
  if (state.fsm === "playing" || !state.records.length) return;
  state.fsm = "playing";
  $("#play-pause").textContent = "⏸";
  state.rafLastWall = performance.now();
  const tick = (now) => {
    if (state.fsm !== "playing") return;
    const dWall = (now - state.rafLastWall) / 1000.0;
    state.rafLastWall = now;
    const dTicks = dWall * state.speed * (state.timerFreqHz || 1);
    const last = state.records[state.records.length - 1].ts_counter;
    const newTs = state.playheadTs + dTicks;
    if (newTs >= last) { seekTo(last); pause(); return; }
    state.playheadTs = newTs;
    updatePlayheadDisplay();
    state.rafHandle = requestAnimationFrame(tick);
  };
  state.rafHandle = requestAnimationFrame(tick);
}

function pause() {
  if (state.fsm !== "playing") return;
  state.fsm = "paused";
  $("#play-pause").textContent = "▶";
  if (state.rafHandle) cancelAnimationFrame(state.rafHandle);
  state.rafHandle = null;
}

function togglePlay() {
  if (state.fsm === "playing") pause(); else play();
}

function changeSpeed(delta) {
  const choices = [0.25, 0.5, 1, 2, 4];
  const sel = $("#speed");
  let idx = choices.indexOf(parseFloat(sel.value));
  if (idx < 0) idx = 2;
  idx = Math.max(0, Math.min(choices.length - 1, idx + delta));
  sel.value = String(choices[idx]);
  state.speed = choices[idx];
}

// ── Wire-up ─────────────────────────────────────────────────────────────────

function setupControls() {
  $("#open-btn").addEventListener("click", async () => {
    const path = $("#capture-path").value.trim();
    if (!path) return;
    try { await openCapture(path); }
    catch (e) { setBanner(e.message, "error"); }
  });
  $("#capture-path").addEventListener("keydown", (e) => {
    if (e.key === "Enter") $("#open-btn").click();
  });
  $("#play-pause").addEventListener("click", togglePlay);
  $("#step-prev").addEventListener("click", () => stepEvent(-1));
  $("#step-next").addEventListener("click", () => stepEvent(1));
  $("#speed").addEventListener("change", (e) => {
    state.speed = parseFloat(e.target.value);
  });

  document.addEventListener("keydown", (e) => {
    const tag = (e.target && e.target.tagName) || "";
    if (tag === "INPUT" || tag === "SELECT" || tag === "TEXTAREA") return;
    switch (e.key) {
      case " ":          e.preventDefault(); togglePlay(); break;
      case "ArrowLeft":   e.shiftKey ? stepEpoch(-1) : stepEvent(-1); break;
      case "ArrowRight":  e.shiftKey ? stepEpoch(1)  : stepEvent(1);  break;
      case "[":           changeSpeed(-1); break;
      case "]":           changeSpeed(1);  break;
    }
  });
}

async function probePreloaded() {
  try {
    const body = await api("/api/preloaded");
    if (body.capture_id) {
      $("#capture-path").value = body.capture_id.split("@")[0];
      await loadCapture(body.capture_id, body.manifest);
    }
  } catch (e) {
    // Endpoint may not be available; fall through to manual open.
  }
}

document.addEventListener("DOMContentLoaded", async () => {
  setupTabs();
  setupControls();
  await probePreloaded();
});
