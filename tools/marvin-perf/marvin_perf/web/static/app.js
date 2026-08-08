"use strict";

// ── Strip kind registry ─────────────────────────────────────────────────────
// Single source of truth for kind→panel mapping. Adding a new kind (MINIMAP,
// etc.) is one entry here plus the matching CSS slot.
//
// `id` must equal the record's `kind_name` (StripKind.<NAME>.lower() in
// records.py): it names the canvas element *and* the offline PNG route
// /api/capture/<id>/strip/<epoch>/<kind_name>.png, which a loaded capture uses
// because its records carry no inline pixels.

const STRIP_KIND_REGISTRY = {
  0: { id: "sensing",        label: "Sensing line",     color: "#6cb4ff", cadence: "60 Hz" },
  1: { id: "strike",         label: "Strike line",      color: "#4ade80", cadence: "60 Hz" },
  3: { id: "region",         label: "Score region",     color: "#f0abfc", cadence: "60 Hz" },
  5: { id: "sensing_2p",     label: "Sensing line (2P)", color: "#38bdf8", cadence: "60 Hz" },
  6: { id: "strike_2p",      label: "Strike line (2P)",  color: "#a3e635", cadence: "60 Hz" },
  7: { id: "score_2p_left",  label: "Score 2P left",     color: "#fbbf24", cadence: "60 Hz" },
  8: { id: "score_2p_right", label: "Score 2P right",    color: "#fb7185", cadence: "60 Hz" },
};

// Kinds gated by their own start/stop command rather than by the STRIP type
// mask (which gates the detector's band strips) — one per device region slot.
const COMMAND_GATED_KINDS = new Set([3, 7, 8]);

// Device region-stream slots, mirroring REGION_SLOTS in marvin_perf/records.py.
// Each is an independent enable + rect on the device and emits its own strip kind.
const REGION_SLOTS = [
  { slot: 0, label: "SCORE",   title: "Stream the 1-player scoring block as a REGION strip" },
  { slot: 1, label: "2P SC L", title: "Stream the 2-player left amp scoreboard" },
  { slot: 2, label: "2P SC R", title: "Stream the 2-player right amp scoreboard" },
];

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
  FBL_READ_COMPLETE:  "#a78bfa",
};
const STAGE_FALLBACK = "#8a93a0";

// ── Record types (mirrors records.RecordType for the mask checkboxes) ──────

const RECORD_TYPES = [
  { name: "SESSION",         typeName: "Session",        bit: 1,  alwaysOn: true,  defaultOn: true  },
  { name: "STAMP",           typeName: "Stamp",          bit: 2,  alwaysOn: false, defaultOn: true  },
  { name: "DETECTOR",        typeName: "Detector",       bit: 3,  alwaysOn: false, defaultOn: true  },
  { name: "TIMING",          typeName: "Timing",         bit: 4,  alwaysOn: false, defaultOn: true  },
  // STRIP carries ~10 KB/frame at 60 Hz × 2 kinds — opt-in to avoid
  // saturating the WS+JSON pipe before the user has asked for pixels.
  { name: "STRIP",           typeName: "Strip",          bit: 5,  alwaysOn: false, defaultOn: false },
  { name: "DROP",            typeName: "Drop",           bit: 6,  alwaysOn: false, defaultOn: true  },
  { name: "TASK_HIGHWATER",  typeName: "TaskHighwater",  bit: 7,  alwaysOn: false, defaultOn: true  },
  { name: "TASK_RUNTIME",    typeName: "TaskRuntime",    bit: 8,  alwaysOn: false, defaultOn: true  },
  // DETECTOR_CONFIG is sparse — emitted on attach + on change + every
  // 60 frames (≤1 Hz). Cheap and useful for STRIP overlay rendering.
  { name: "DETECTOR_CONFIG", typeName: "DetectorConfig", bit: 9,  alwaysOn: false, defaultOn: true  },
  // ACTUATOR fires per FretboardLink_Send (≈ timing-pipeline tick rate
  // when active, 0 when idle) — small record, default-on.
  { name: "ACTUATOR",        typeName: "Actuator",       bit: 10, alwaysOn: false, defaultOn: true  },
  // FRETBOARD_RAW is 240 Hz × 28 B = ~6.7 KB/s — opt-in like STRIP so
  // captures stay lean unless the user is collecting Edge-AI training data.
  { name: "FRETBOARD_RAW",   typeName: "FretboardRaw",   bit: 11, alwaysOn: false, defaultOn: false },
];
const TYPE_NAME_TO_BIT = Object.fromEntries(RECORD_TYPES.map((t) => [t.typeName, t.bit]));
const MASK_ALL = 0xFFFFFFFF >>> 0;
// SESSION + DROP — minimum useful mask; mirrors records.TYPE_MASK_MIN.
const MASK_MIN = ((1 << 1) | (1 << 6)) >>> 0;

// Sliding-window bounds for live mode. The 30-s window plus a 10-k absolute
// cap keeps Plotly redraws bounded — at 60 Hz × ~8 stamp stages we see ~14 k
// timeline points per 30 s, so the cap kicks in only on extreme bursts.
const LIVE_BUFFER_SECONDS = 30;
const LIVE_MAX_RECORDS = 10_000;
const LIVE_REDRAW_DEBOUNCE_MS = 250;
const MASK_DEBOUNCE_MS = 200;

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
  fsm: "idle",            // idle | loaded | playing | paused | live
  speed: 1.0,
  rafHandle: null,
  rafLastWall: 0,

  mode: "offline",        // offline | live
  // Live-mode bookkeeping.
  ws: null,
  liveStopRequested: false,
  reconnectAttempts: 0,
  lastSession: null,
  pendingRedrawHandle: null,
  pendingMaskHandle: null,
  liveMask: MASK_ALL,
  overlayEnabled: true,   // SENSING-strip target rings (device boots on)
  // Per-slot region streams, keyed by slot number (device boots all off).
  regionSlots: Object.fromEntries(REGION_SLOTS.map((s) => [s.slot, false])),
  recording: null,        // null | { capture_dir, started_at }
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

// Live mode computes the same shape /api/capture/{id}/rtos returns from the
// records buffer, so renderRtosPanel reads `state.rtos` uniformly.
function recomputeLiveRtos() {
  const hwm = {};       // task_id → { samples: [[ts, words], ...] }
  const runtimes = {};  // task_id → [TaskRuntime, ...] (last two kept)
  const taskNames = {}; // task_id → name
  for (const rec of state.records) {
    if (rec.type === "TaskHighwater") {
      const tid = rec.task_id;
      taskNames[tid] = rec.task_name || taskNames[tid] || `task_${tid}`;
      (hwm[tid] ||= { samples: [] }).samples.push([rec.ts_counter, rec.words]);
    } else if (rec.type === "TaskRuntime") {
      const tid = rec.task_id;
      taskNames[tid] = rec.task_name || taskNames[tid] || `task_${tid}`;
      const bucket = (runtimes[tid] ||= []);
      bucket.push(rec);
      if (bucket.length > 2) bucket.shift();
    }
  }

  // CPU snapshot: Δrun_time_counter / Σ across all tasks. uint32-wrap-safe.
  const deltas = {};
  for (const tid of Object.keys(runtimes)) {
    const b = runtimes[tid];
    if (b.length < 2) continue;
    deltas[tid] = (b[1].run_time_counter - b[0].run_time_counter) >>> 0;
  }
  const total = Object.values(deltas).reduce((a, b) => a + b, 0);
  const cpu = {};
  if (total > 0) {
    for (const tid of Object.keys(deltas)) {
      cpu[tid] = (100.0 * deltas[tid]) / total;
    }
  }

  const taskIds = Array.from(new Set([...Object.keys(hwm), ...Object.keys(cpu)]))
    .map((s) => Number(s))
    .sort((a, b) => a - b);

  const tasks = taskIds.map((tid) => {
    const series = hwm[tid];
    const samples = series ? series.samples : [];
    const wordsArr = samples.map(([, w]) => w);
    return {
      task_id: tid,
      task_name: taskNames[tid] || `task_${tid}`,
      hwm_words: {
        first: samples.length ? samples[0][1] : null,
        last: samples.length ? samples[samples.length - 1][1] : null,
        min: wordsArr.length ? Math.min(...wordsArr) : null,
        samples,
      },
      cpu_pct: cpu[tid] ?? null,
    };
  });
  // TaskId.IDLE = 6 (mirrors records.py).
  const cpuIdle = cpu[6] ?? null;
  const anyBelow64 = tasks.some(
    (t) => t.hwm_words.min !== null && t.hwm_words.min < 64
  );
  const anyBelow32 = tasks.some(
    (t) => t.hwm_words.min !== null && t.hwm_words.min < 32
  );
  state.rtos = {
    tasks,
    totals: {
      any_task_below_64_words: anyBelow64,
      any_task_below_32_words: anyBelow32,
      cpu_pct_idle: cpuIdle,
      cpu_pct_busy: cpuIdle === null ? null : 100.0 - cpuIdle,
    },
  };
}

function renderRtosPanel() {
  const r = state.rtos;
  const totalsNode = $("#rtos-cpu-totals");
  const tbl = $("#rtos-table");
  tbl.innerHTML = "";
  if (!r || !r.tasks || r.tasks.length === 0) {
    tbl.innerHTML = "<tr><td>(no RTOS records)</td></tr>";
    fillMeta(totalsNode, []);
    Plotly.purge("rtos-hwm-chart");
    return;
  }

  // TaskId.IDLE = 6, TaskId.OTHER = 16 — mirrored from records.py.
  const TID_IDLE = 6, TID_OTHER = 16;
  const t = r.totals || {};
  const idleTask = r.tasks.find((tk) => tk.task_id === TID_IDLE);
  const otherTask = r.tasks.find((tk) => tk.task_id === TID_OTHER);
  const totalsRows = [];
  if (t.cpu_pct_busy !== null && t.cpu_pct_busy !== undefined) {
    totalsRows.push(["busy", `${t.cpu_pct_busy.toFixed(1)} %`]);
    totalsRows.push(["idle", `${t.cpu_pct_idle.toFixed(1)} %`]);
    if (otherTask && otherTask.cpu_pct !== null && otherTask.cpu_pct !== undefined) {
      // OTHER = Σ all − Σ registered. Should idle near zero; nonzero means
      // a task running but not yet given a slot in s_task_handles[].
      totalsRows.push([
        "other",
        otherTask.cpu_pct >= 0.5
          ? `${otherTask.cpu_pct.toFixed(1)} %  (unnamed task)`
          : `${otherTask.cpu_pct.toFixed(1)} %`,
      ]);
    }
  } else {
    totalsRows.push(["cpu", "(waiting for 2 TASK_RUNTIME samples)"]);
  }
  fillMeta(totalsNode, totalsRows);

  const head = document.createElement("tr");
  for (const h of ["task", "id", "cpu %", "min words", "first", "last", "samples"]) {
    const th = document.createElement("th"); th.textContent = h; head.appendChild(th);
  }
  tbl.appendChild(head);
  // The totals header above gives the busy / idle / other snapshot, but the
  // per-task table includes IDLE and OTHER too — they're useful directly in
  // the workload row context (e.g. comparing a task's CPU% to IDLE's).
  // Sorted by CPU% desc, hottest first; tasks without CPU samples fall last.
  const sortedTasks = r.tasks
    .slice()
    .sort((a, b) => {
      const ac = a.cpu_pct ?? -1, bc = b.cpu_pct ?? -1;
      if (ac !== bc) return bc - ac;
      return a.task_id - b.task_id;
    });
  for (const tk of sortedTasks) {
    const tr = document.createElement("tr");
    const cpuStr = tk.cpu_pct === null || tk.cpu_pct === undefined
      ? "—"
      : tk.cpu_pct.toFixed(1);
    const cells = [
      tk.task_name, tk.task_id,
      cpuStr,
      tk.hwm_words.min ?? "—",
      tk.hwm_words.first ?? "—",
      tk.hwm_words.last ?? "—",
      (tk.hwm_words.samples || []).length,
    ];
    cells.forEach((c, i) => {
      const td = document.createElement("td"); td.textContent = String(c);
      // i==2 is cpu%, i==3 is min words.
      if (i === 3 && typeof c === "number") {
        if (c < 32) td.classList.add("danger");
        else if (c < 64) td.classList.add("warn");
      }
      tr.appendChild(td);
    });
    tbl.appendChild(tr);
  }

  // HWM trend per task: Plotly line chart.
  const traces = r.tasks.map((tk) => {
    const xs = (tk.hwm_words.samples || []).map(([ts]) => tickToMs(ts));
    const ys = (tk.hwm_words.samples || []).map(([, w]) => w);
    return { x: xs, y: ys, name: tk.task_name, mode: "lines+markers", type: "scatter" };
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
  if ((f.fcs_mismatches || 0) > 0 || (f.bytes_resync_dropped || 0) > 0) {
    level = "red"; label = `link ✗ ${f.fcs_mismatches || 0} fcs`;
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
    if (rec.bgr_b64) {
      paintBgrIntoCanvas(canvas, rec.bgr_b64, rec.w, rec.h);
      continue;
    }
    if (!state.captureId) continue;
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

function paintBgrIntoCanvas(canvas, b64, w, h) {
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w; canvas.height = h;
    canvas.style.width = `${w * STRIP_PANE_ZOOM}px`;
    canvas.style.height = `${h * STRIP_PANE_ZOOM}px`;
  }
  const bin = atob(b64);
  const ctx = canvas.getContext("2d");
  const img = ctx.createImageData(w, h);
  const data = img.data;
  // BGR (3 B/pixel) → RGBA (4 B/pixel).
  for (let i = 0, j = 0; i < bin.length; i += 3, j += 4) {
    data[j]     = bin.charCodeAt(i + 2);
    data[j + 1] = bin.charCodeAt(i + 1);
    data[j + 2] = bin.charCodeAt(i);
    data[j + 3] = 255;
  }
  ctx.imageSmoothingEnabled = false;
  ctx.putImageData(img, 0, 0);
}

// ── Timeline ────────────────────────────────────────────────────────────────

function renderTimeline() {
  const stamps = state.records.filter((r) => r.type === "Stamp");
  const drops = state.records.filter((r) => r.type === "Drop");

  // SVG scatter degrades sharply past ~5k points (long DOM rebuilds = the UI
  // freeze symptom). Switch to scattergl above that threshold; the trade-off
  // is a slightly heavier first-render and no per-stage marker symbols, but
  // it stays interactive at 30k+ points.
  const useGl = stamps.length > 5000;
  const stampType = useGl ? "scattergl" : "scatter";
  const stampSymbol = useGl ? "line-ns" : "line-ns-open";

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
    type: stampType,
    marker: { color: STAGE_COLORS[stage] || STAGE_FALLBACK, size: 8, symbol: stampSymbol },
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

  // `react` reuses the existing plot's DOM scaffolding, which is dramatically
  // cheaper than `newPlot`'s tear-down-and-rebuild — important for the live
  // mode's 1 Hz redraw with 10k+ points.
  const node = document.getElementById("timeline-chart");
  const isInitialized = node && node.data && node.layout;
  const fn = isInitialized ? Plotly.react : Plotly.newPlot;
  fn(node, traces, layout, { displayModeBar: false, responsive: true });

  if (!isInitialized) {
    node.on("plotly_click", (ev) => {
      if (!ev.points || !ev.points.length) return;
      const ms = ev.points[0].x;
      const ticks = state.ts0 + (ms / 1000.0) * state.timerFreqHz;
      seekTo(ticks);
    });
  }
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

// ── Mode toggle ─────────────────────────────────────────────────────────────

function setMode(mode) {
  if (state.mode === mode) return;
  state.mode = mode;
  $("#mode-offline").classList.toggle("active", mode === "offline");
  $("#mode-live").classList.toggle("active", mode === "live");
  $("#offline-controls").classList.toggle("hidden", mode !== "offline");
  $("#live-controls").classList.toggle("hidden", mode !== "live");
  $("#record-controls").classList.toggle("hidden", mode !== "live");
  $("#snapshot-controls").classList.toggle("hidden", mode !== "live");
  $("#types-panel").classList.toggle("hidden", mode !== "live");
  $("#badge-live").classList.toggle("hidden", mode !== "live");
  setLiveTransportEnabled(mode !== "live");

  // Reset capture-state when crossing the mode boundary so leftover offline
  // records don't bleed into a live session and vice versa.
  resetCaptureState();
  if (mode === "live") {
    buildTypesPanel();
    refreshSerialPorts();
    setBanner("Pick a port and click Start to stream from the device.");
  } else {
    closeWS({ userInitiated: true });
    setBanner("");
  }
}

function setLiveTransportEnabled(enabled) {
  for (const id of ["#step-prev", "#play-pause", "#step-next", "#speed"]) {
    const el = $(id);
    if (el) el.disabled = !enabled;
  }
}

function resetCaptureState() {
  pause();
  state.captureId = null;
  state.manifest = null;
  state.summary = null;
  state.health = null;
  state.rtos = null;
  state.records = [];
  state.byKind = {};
  state.timerFreqHz = 0;
  state.ts0 = 0;
  state.playheadTs = 0;
  state.fsm = "idle";
  state.lastSession = null;
  state.recording = null;
  setRecordingPill(null);
  $("#record-start").disabled = true;
  $("#record-stop").disabled = true;
  $("#record-out-dir").disabled = false;
  // Clear panels so the previous mode's contents don't linger.
  fillMeta($("#event-meta"), []);
  fillMeta($("#session-meta"), []);
  fillMeta($("#drops-meta"), []);
  $("#warnings-list").innerHTML = "";
  $("#warnings-list").classList.add("empty");
  $("#strip-slots").innerHTML = "";
  $("#strip-hint").classList.remove("hidden");
  $("#strip-hint").textContent = "(no records yet)";
  Plotly.purge("timeline-chart");
  Plotly.purge("rtos-hwm-chart");
  setBadge("badge-link", null, "link ●");
  setBadge("badge-rtos", null, "rtos ●");
  $("#ph-epoch").textContent = "—";
  $("#ph-ts").textContent = "—";
}

// ── Live: serial ports ──────────────────────────────────────────────────────

async function refreshSerialPorts() {
  try {
    const ports = await api("/api/serial/ports");
    const sel = $("#live-port");
    const prev = sel.value;
    sel.innerHTML = "";
    const placeholder = document.createElement("option");
    placeholder.value = ""; placeholder.textContent = "(select port)";
    sel.appendChild(placeholder);
    for (const p of ports) {
      const opt = document.createElement("option");
      opt.value = p.device;
      opt.textContent = p.description ? `${p.device} — ${p.description}` : p.device;
      sel.appendChild(opt);
    }
    if (prev && ports.find((p) => p.device === prev)) sel.value = prev;
  } catch (e) {
    setBanner(`Could not list serial ports: ${e.message}`, "error");
  }
}

// ── Live: start / stop / WS ─────────────────────────────────────────────────

async function liveStart() {
  const port = $("#live-port").value;
  if (!port) { setBanner("Pick a serial port first.", "error"); return; }
  resetCaptureState();
  state.fsm = "live";
  state.liveStopRequested = false;
  state.reconnectAttempts = 0;
  setBanner("Starting…");
  try {
    await api("/api/live/start", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ port }),
    });
  } catch (e) {
    setBanner(e.message, "error");
    state.fsm = "idle";
    return;
  }
  $("#live-start").disabled = true;
  $("#live-stop").disabled = false;
  $("#live-port").disabled = true;
  $("#record-start").disabled = false;
  $("#snapshot-btn").disabled = false;
  $("#overlay-toggle").disabled = false;
  setRegionCbDisabled(false);
  setBadge("badge-live", "yellow", "live …");
  suggestRecordOutDir();
  suggestSnapshotOut();
  // Push the initial mask before WS attach so the server's `hello` reflects
  // what the user actually wants (otherwise hello reports 0xffffffff and
  // clobbers the user's STRIP-off default).
  await applyMaskFromCheckboxes({ immediate: true });
  openWS();
}

async function liveStop() {
  state.liveStopRequested = true;
  closeWS({ userInitiated: true });
  try {
    // Server-side `live/stop` auto-finalizes any in-flight recording, so the
    // bin lands openable on disk without a separate POST from the browser.
    await api("/api/live/stop", { method: "POST" });
  } catch (e) {
    setBanner(e.message, "error");
  }
  $("#live-start").disabled = false;
  $("#live-stop").disabled = true;
  $("#live-port").disabled = false;
  $("#record-start").disabled = true;
  $("#record-stop").disabled = true;
  $("#snapshot-btn").disabled = true;
  $("#overlay-toggle").disabled = true;
  // Device stops streaming every slot on disconnect.
  for (const s of REGION_SLOTS) setRegionChecked(s.slot, false);
  setRegionCbDisabled(true);
  setRecordingPill(null);
  setBadge("badge-live", null, "live ●");
  state.fsm = "idle";
}

function openWS() {
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  const ws = new WebSocket(`${proto}//${location.host}/api/live/ws`);
  state.ws = ws;
  ws.addEventListener("open", () => {
    state.reconnectAttempts = 0;
    setBadge("badge-live", "green", "live ●");
    setBanner("");
  });
  ws.addEventListener("message", (ev) => {
    let msg;
    try { msg = JSON.parse(ev.data); } catch { return; }
    handleWSMessage(msg);
  });
  ws.addEventListener("close", (ev) => {
    state.ws = null;
    if (state.liveStopRequested) {
      setBadge("badge-live", null, "live ●");
      return;
    }
    setBadge("badge-live", "yellow", `live ✗ ${ev.code}`);
    if (ev.code === 4409) {
      setBanner("Another client is using the live session.", "error");
      return;
    }
    scheduleReconnect();
  });
  ws.addEventListener("error", () => {
    // 'close' will follow; reconnect handled there.
  });
}

function closeWS({ userInitiated }) {
  if (!state.ws) return;
  if (userInitiated) state.liveStopRequested = true;
  try { state.ws.close(); } catch {}
  state.ws = null;
}

function scheduleReconnect() {
  if (state.mode !== "live" || state.liveStopRequested) return;
  state.reconnectAttempts += 1;
  const delay = Math.min(5000, 1000 * state.reconnectAttempts);
  setBanner(`WS dropped — reconnecting in ${(delay / 1000).toFixed(1)}s…`);
  setTimeout(() => {
    if (state.mode === "live" && !state.liveStopRequested && !state.ws) openWS();
  }, delay);
}

function handleWSMessage(msg) {
  switch (msg.type) {
    case "hello":
      state.liveMask = parseInt(msg.session.mask, 16) >>> 0;
      $("#live-mask").textContent = `0x${state.liveMask.toString(16).padStart(8, "0")}`;
      applyMaskToCheckboxes(state.liveMask);
      // Re-attach mid-recording: reflect the server's view in the UI.
      applyRecordingState(msg.session.recording || null);
      if (typeof msg.session.overlay === "boolean") setOverlayButton(msg.session.overlay);
      applyRegionState(msg.session.region);
      $("#snapshot-btn").disabled = state.fsm !== "live";
      $("#overlay-toggle").disabled = state.fsm !== "live";
      setRegionCbDisabled(state.fsm !== "live");
      suggestSnapshotOut();
      break;
    case "session_replay":
    case "record":
      appendLiveRecord(msg.rec);
      break;
    case "mask":
      state.liveMask = parseInt(msg.value, 16) >>> 0;
      $("#live-mask").textContent = `0x${state.liveMask.toString(16).padStart(8, "0")}`;
      if (msg.source === "server") applyMaskToCheckboxes(state.liveMask);
      break;
    case "framing_stats":
      // (Phase-2: stash on state if we want to render a counter; for now,
      // the per-record append already keeps badges fresh enough.)
      break;
    case "recording":
      if (msg.state === "started") {
        applyRecordingState({
          capture_dir: msg.capture_dir,
          started_at: msg.started_at,
        });
      } else if (msg.state === "stopped") {
        applyRecordingState(null);
        const dir = msg.capture_dir || "(unknown)";
        const n = msg.n_frames ?? msg.manifest?.n_records ?? "?";
        setBanner(`Recorded ${n} frames → ${dir}`);
      }
      break;
    case "snapshot":
      handleSnapshotEvent(msg);
      break;
    case "overlay":
      setOverlayButton(!!msg.enabled);
      break;
    case "region":
      setRegionChecked(msg.slot ?? 0, !!msg.enabled);
      break;
    case "error":
      setBanner(`device error: ${msg.code}: ${msg.msg}`, "error");
      break;
  }
}

function handleSnapshotEvent(msg) {
  if (msg.state === "requested") {
    setBanner("Snapshot requested — waiting for frame…");
    return;
  }
  // Any terminal state re-enables the button (when still live).
  $("#snapshot-btn").disabled = state.fsm !== "live";
  if (msg.state === "timeout") {
    setBanner(
      "Snapshot timed out — no complete frame. HDMI locked and firmware schema current?",
      "error",
    );
    return;
  }
  if (msg.state === "error") {
    setBanner(`snapshot error: ${msg.msg}`, "error");
    return;
  }
  showSnapshotPreview(msg);  // state === "saved"
}

function showSnapshotPreview(msg) {
  // Cache-buster: the PNG endpoint always serves the latest snapshot, so the
  // URL is otherwise stable across captures.
  const url = `/api/live/snapshot.png?ts=${msg.frame_epoch}-${Date.now()}`;
  $("#snap-img").src = url;
  const dl = $("#snap-download");
  dl.href = url;
  dl.download = `snapshot-${msg.frame_epoch}.png`;
  const saved = msg.paths && msg.paths.length ? ` · saved → ${msg.paths.join(", ")}` : " · not saved";
  $("#snap-meta").textContent = `${msg.width}×${msg.height} · epoch ${msg.frame_epoch}${saved}`;
  $("#snapshot-modal").classList.remove("hidden");
  if (msg.save_error) {
    setBanner(`snapshot saved-to-disk failed: ${msg.save_error}`, "error");
  } else if (msg.paths && msg.paths.length) {
    setBanner(`Snapshot ${msg.width}×${msg.height} saved → ${shortDir(msg.paths[0])}`);
  } else {
    setBanner(`Snapshot ${msg.width}×${msg.height} captured.`);
  }
}

async function requestSnapshot() {
  const out = $("#snapshot-out").value.trim();
  $("#snapshot-btn").disabled = true;
  setBanner("Snapshot requested — waiting for frame…");
  try {
    await api("/api/live/snapshot", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ out: out || null }),
    });
    // Terminal UI flip happens on the WS `snapshot` event.
  } catch (e) {
    setBanner(`snapshot failed: ${e.message}`, "error");
    $("#snapshot-btn").disabled = state.fsm !== "live";
  }
}

function setOverlayButton(enabled) {
  state.overlayEnabled = enabled;
  const btn = $("#overlay-toggle");
  if (btn) btn.classList.toggle("active", enabled);
}

async function toggleOverlay() {
  const next = !state.overlayEnabled;
  $("#overlay-toggle").disabled = true;
  try {
    const body = await api("/api/live/overlay", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ enabled: next }),
    });
    setOverlayButton(!!body.overlay);  // WS 'overlay' echo also lands
  } catch (e) {
    setBanner(`overlay toggle failed: ${e.message}`, "error");
  } finally {
    $("#overlay-toggle").disabled = state.fsm !== "live";
  }
}

function setRegionChecked(slot, enabled) {
  state.regionSlots[slot] = enabled;
  const cb = $(`#region-cb-${slot}`);
  if (cb) cb.checked = enabled;
}

function setRegionCbDisabled(disabled) {
  for (const s of REGION_SLOTS) {
    const cb = $(`#region-cb-${s.slot}`);
    if (cb) cb.disabled = disabled;
  }
}

// Restore every slot's checkbox from a /api/live/status or WS `hello` region
// object. `slots` is keyed by slot id; the flattened enabled/rect describe slot 0.
function applyRegionState(region) {
  if (!region) return;
  if (region.slots) {
    for (const info of Object.values(region.slots)) {
      setRegionChecked(info.slot, !!info.enabled);
    }
    return;
  }
  setRegionChecked(0, !!region.enabled);
}

async function setRegionEnabled(slot, enabled) {
  // Independent of the STRIP type mask — the device gates each region slot on
  // this command alone.
  const cb = $(`#region-cb-${slot}`);
  const label = REGION_SLOTS.find((s) => s.slot === slot)?.label ?? `slot ${slot}`;
  if (cb) cb.disabled = true;
  try {
    const body = await api("/api/live/region", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ enabled, slot }),
    });
    setRegionChecked(slot, !!body.enabled);  // WS 'region' echo also lands
    if (enabled) setBanner(`${label} streaming — hit Record to capture it`, "info");
  } catch (e) {
    setBanner(`${label} toggle failed: ${e.message}`, "error");
    setRegionChecked(slot, !enabled);  // revert the checkbox on failure
  } finally {
    if (cb) cb.disabled = state.fsm !== "live";
  }
}

function suggestSnapshotOut() {
  const inp = $("#snapshot-out");
  if (!inp || inp.value.trim()) return;
  const d = new Date();
  const stamp = d.getFullYear().toString() +
    String(d.getMonth() + 1).padStart(2, "0") +
    String(d.getDate()).padStart(2, "0") + "-" +
    String(d.getHours()).padStart(2, "0") +
    String(d.getMinutes()).padStart(2, "0") +
    String(d.getSeconds()).padStart(2, "0");
  inp.value = `snapshots/web-${stamp}`;
}

// ── Live: record append + sliding window ────────────────────────────────────

function appendLiveRecord(rec) {
  // Client-side mask filter. The device-side mask change has serial RTT
  // latency plus already-buffered frames in flight, so for ~hundreds of ms
  // after the user unticks a box, records of that type still arrive over
  // the WS. Drop them here so the UI reacts immediately. Session is always
  // accepted so timer_freq_hz / replay still binds on reconnect.
  if (rec.type !== "Session") {
    // Region strips are command-gated (independent of the STRIP type mask),
    // so don't drop them when the STRIP box is unticked.
    const isRegion = rec.type === "Strip" && COMMAND_GATED_KINDS.has(rec.kind);
    const bit = TYPE_NAME_TO_BIT[rec.type];
    if (!isRegion && bit !== undefined && (state.liveMask & (1 << bit)) === 0) return;
  }
  if (!state.records.length) {
    state.ts0 = rec.ts_counter;
  }
  if (rec.type === "Session") {
    state.timerFreqHz = rec.timer_freq_hz || state.timerFreqHz;
    state.lastSession = rec;
    renderLiveSessionMeta(rec);
  }
  state.records.push(rec);
  pruneLiveBuffer();
  state.playheadTs = rec.ts_counter;

  // Strip records: ensure a slot exists, then paint immediately.
  if (rec.type === "Strip" && rec.bgr_b64) {
    if (!(rec.kind in state.byKind) || state.byKind[rec.kind].length === 0) {
      // First strip of this kind in the buffer — re-index + re-render slots.
      indexByKind();
      renderStripSlots();
    } else {
      (state.byKind[rec.kind] ||= []).push(rec);
    }
    const cfg = STRIP_KIND_REGISTRY[rec.kind];
    const id = cfg ? cfg.id : `kind-${rec.kind}`;
    const canvas = document.getElementById(`strip-canvas-${id}`);
    if (canvas) paintBgrIntoCanvas(canvas, rec.bgr_b64, rec.w, rec.h);
  } else if (typeof rec.kind === "number") {
    (state.byKind[rec.kind] ||= []).push(rec);
  }

  $("#ph-epoch").textContent = rec.frame_epoch;
  $("#ph-ts").textContent = state.timerFreqHz ? fmtMs(rec.ts_counter) : `${rec.ts_counter} t`;
  scheduleLiveRedraw();
}

function pruneLiveBuffer() {
  // Drop records older than LIVE_BUFFER_SECONDS; cap absolute count.
  if (state.timerFreqHz > 0 && state.records.length > 1) {
    const cutoffTicks = state.records[state.records.length - 1].ts_counter
                      - LIVE_BUFFER_SECONDS * state.timerFreqHz;
    let drop = 0;
    while (drop < state.records.length
           && state.records[drop].ts_counter < cutoffTicks) {
      drop += 1;
    }
    if (drop > 0) state.records.splice(0, drop);
  }
  if (state.records.length > LIVE_MAX_RECORDS) {
    state.records.splice(0, state.records.length - LIVE_MAX_RECORDS);
  }
  if (state.records.length) {
    state.ts0 = state.records[0].ts_counter;
  }
}

function scheduleLiveRedraw() {
  if (state.pendingRedrawHandle != null) return;
  // Throttle harder when the buffer is large — a 50 k-record Plotly redraw
  // on every tick is what makes the UI jumpy.
  const debounceMs = state.records.length > 5000
    ? LIVE_REDRAW_DEBOUNCE_MS * 4
    : LIVE_REDRAW_DEBOUNCE_MS;
  state.pendingRedrawHandle = setTimeout(() => {
    state.pendingRedrawHandle = null;
    indexByKind();
    renderTimeline();
    updatePlayheadInspector();
    recomputeLiveRtos();
    renderRtosPanel();
    renderBadges();
  }, debounceMs);
}

function renderLiveSessionMeta(rec) {
  fillMeta($("#session-meta"), [
    ["schema", rec.schema_version ?? "—"],
    ["timer_hz", rec.timer_freq_hz ?? "—"],
    ["fw_git", rec.fw_git_short ?? "—"],
    ["mode", "live"],
  ]);
}

// ── Live: types panel + mask ────────────────────────────────────────────────

function buildTypesPanel() {
  const wrap = $("#types-checkboxes");
  wrap.innerHTML = "";
  for (const t of RECORD_TYPES) {
    const lbl = document.createElement("label");
    lbl.className = "type-cb";
    const cb = document.createElement("input");
    cb.type = "checkbox";
    cb.dataset.bit = String(t.bit);
    cb.checked = t.defaultOn;
    if (t.alwaysOn) cb.disabled = true;
    cb.addEventListener("change", () => applyMaskFromCheckboxes());
    const span = document.createElement("span");
    span.textContent = t.name;
    lbl.appendChild(cb);
    lbl.appendChild(span);
    wrap.appendChild(lbl);
  }

  // Region captures: not record-type mask bits but device commands (start/stop
  // one region slot each). Grouped with the type checkboxes for consistency; the
  // strips render in the pane like sensing/strike, and Record captures them into
  // the .bin like any other record.
  for (const s of REGION_SLOTS) {
    const rlbl = document.createElement("label");
    rlbl.className = "type-cb";
    rlbl.title = `${s.title} (records into the .bin like any other record)`;
    const rcb = document.createElement("input");
    rcb.type = "checkbox";
    rcb.id = `region-cb-${s.slot}`;
    rcb.checked = !!state.regionSlots[s.slot];
    rcb.disabled = state.fsm !== "live";
    rcb.addEventListener("change", () => setRegionEnabled(s.slot, rcb.checked));
    const rspan = document.createElement("span");
    rspan.textContent = s.label;
    rlbl.appendChild(rcb);
    rlbl.appendChild(rspan);
    wrap.appendChild(rlbl);
  }
}

function applyMaskToCheckboxes(mask) {
  $$("#types-checkboxes input[data-bit]").forEach((cb) => {
    const bit = Number(cb.dataset.bit);
    cb.checked = (mask & (1 << bit)) !== 0;
  });
}

function applyMaskFromCheckboxes({ immediate } = {}) {
  if (state.pendingMaskHandle != null) {
    clearTimeout(state.pendingMaskHandle);
    state.pendingMaskHandle = null;
  }
  let mask = 0;
  $$("#types-checkboxes input[data-bit]").forEach((cb) => {
    const bit = Number(cb.dataset.bit);
    if (cb.checked) mask |= (1 << bit);
  });
  mask = mask >>> 0;
  // Apply locally up front so appendLiveRecord drops disabled types as soon
  // as the click registers, without waiting for the device-side ack.
  state.liveMask = mask;
  $("#live-mask").textContent = `0x${mask.toString(16).padStart(8, "0")}`;
  state.records = [];
  state.byKind = {};
  if (state.pendingRedrawHandle != null) {
    clearTimeout(state.pendingRedrawHandle);
    state.pendingRedrawHandle = null;
  }
  Plotly.purge("timeline-chart");
  $("#strip-slots").innerHTML = "";

  const fire = async () => {
    state.pendingMaskHandle = null;
    try {
      await api("/api/live/set-mask", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ mask }),
      });
    } catch (e) {
      setBanner(`set-mask failed: ${e.message}`, "error");
    }
  };
  if (immediate) return fire();
  state.pendingMaskHandle = setTimeout(fire, MASK_DEBOUNCE_MS);
  return Promise.resolve();
}

function applyMaskPreset(mask) {
  $$("#types-checkboxes input[data-bit]").forEach((cb) => {
    if (cb.disabled) return;
    const bit = Number(cb.dataset.bit);
    cb.checked = (mask & (1 << bit)) !== 0;
  });
  applyMaskFromCheckboxes({ immediate: true });
}

// ── Live: recording ─────────────────────────────────────────────────────────

function suggestRecordOutDir() {
  const inp = $("#record-out-dir");
  if (!inp || inp.value.trim()) return;
  const d = new Date();
  const stamp = d.getFullYear().toString() +
    String(d.getMonth() + 1).padStart(2, "0") +
    String(d.getDate()).padStart(2, "0") + "-" +
    String(d.getHours()).padStart(2, "0") +
    String(d.getMinutes()).padStart(2, "0") +
    String(d.getSeconds()).padStart(2, "0");
  inp.value = `captures/web-${stamp}`;
}

async function recordStart() {
  const dir = $("#record-out-dir").value.trim();
  if (!dir) { setBanner("Pick an output dir for the recording.", "error"); return; }
  $("#record-start").disabled = true;
  try {
    await api("/api/live/record/start", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ out_dir: dir }),
    });
    // The actual UI flip happens when the WS broadcasts {state:"started"}.
  } catch (e) {
    setBanner(`record start failed: ${e.message}`, "error");
    $("#record-start").disabled = false;
  }
}

async function recordStop() {
  $("#record-stop").disabled = true;
  try {
    await api("/api/live/record/stop", { method: "POST" });
    // UI flip on the WS broadcast; if WS already closed the REST result has
    // the manifest summary, but we don't render it here.
  } catch (e) {
    setBanner(`record stop failed: ${e.message}`, "error");
  }
}

function applyRecordingState(rec) {
  state.recording = rec;
  if (rec) {
    setRecordingPill("active", `REC · ${shortDir(rec.capture_dir)}`);
    $("#record-start").disabled = true;
    $("#record-stop").disabled = false;
    $("#record-out-dir").disabled = true;
  } else {
    setRecordingPill(null);
    $("#record-start").disabled = state.fsm !== "live";
    $("#record-stop").disabled = true;
    $("#record-out-dir").disabled = false;
  }
}

function setRecordingPill(level, label) {
  const el = $("#record-pill");
  if (!el) return;
  el.classList.remove("rec-idle", "rec-active", "rec-stopped");
  if (!level) {
    el.classList.add("rec-idle");
    el.textContent = "idle";
    return;
  }
  el.classList.add(`rec-${level}`);
  el.textContent = label || level;
}

function shortDir(path) {
  if (!path) return "?";
  const segs = path.replace(/\\/g, "/").split("/").filter(Boolean);
  return segs.length > 1 ? `…/${segs[segs.length - 1]}` : path;
}

// ── Wire-up ─────────────────────────────────────────────────────────────────

function setupControls() {
  $("#mode-offline").addEventListener("click", () => setMode("offline"));
  $("#mode-live").addEventListener("click", () => setMode("live"));

  $("#open-btn").addEventListener("click", async () => {
    const path = $("#capture-path").value.trim();
    if (!path) return;
    try { await openCapture(path); }
    catch (e) { setBanner(e.message, "error"); }
  });
  $("#capture-path").addEventListener("keydown", (e) => {
    if (e.key === "Enter") $("#open-btn").click();
  });

  $("#live-port-refresh").addEventListener("click", refreshSerialPorts);
  $("#live-start").addEventListener("click", liveStart);
  $("#live-stop").addEventListener("click", liveStop);
  $("#record-start").addEventListener("click", recordStart);
  $("#record-stop").addEventListener("click", recordStop);
  $("#snapshot-btn").addEventListener("click", requestSnapshot);
  $("#overlay-toggle").addEventListener("click", toggleOverlay);
  $("#snap-close").addEventListener("click", () => $("#snapshot-modal").classList.add("hidden"));
  $("#snapshot-modal").addEventListener("click", (e) => {
    if (e.target === $("#snapshot-modal")) $("#snapshot-modal").classList.add("hidden");
  });
  $("#types-all").addEventListener("click", () => applyMaskPreset(MASK_ALL));
  $("#types-min").addEventListener("click", () => applyMaskPreset(MASK_MIN));

  $("#play-pause").addEventListener("click", togglePlay);
  $("#step-prev").addEventListener("click", () => stepEvent(-1));
  $("#step-next").addEventListener("click", () => stepEvent(1));
  $("#speed").addEventListener("change", (e) => {
    state.speed = parseFloat(e.target.value);
  });

  document.addEventListener("keydown", (e) => {
    const tag = (e.target && e.target.tagName) || "";
    if (tag === "INPUT" || tag === "SELECT" || tag === "TEXTAREA") return;
    if (e.key === "Escape" && !$("#snapshot-modal").classList.contains("hidden")) {
      $("#snapshot-modal").classList.add("hidden");
      return;
    }
    if (state.mode === "live") return;  // transport is offline-only
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

async function probeLiveSession() {
  // If the server already has an active live session (e.g. page reload
  // during streaming), switch to live mode and re-attach the WS.
  try {
    const s = await api("/api/live/status");
    if (s.active) {
      setMode("live");
      $("#live-port").value = s.port || "";
      $("#live-port").disabled = true;
      $("#live-start").disabled = true;
      $("#live-stop").disabled = false;
      $("#record-start").disabled = !!s.recording;
      $("#snapshot-btn").disabled = false;
      $("#overlay-toggle").disabled = false;
      setRegionCbDisabled(false);
      if (typeof s.overlay === "boolean") setOverlayButton(s.overlay);
      applyRegionState(s.region);
      state.fsm = "live";
      state.liveMask = parseInt(s.mask, 16) >>> 0;
      $("#live-mask").textContent = s.mask;
      applyMaskToCheckboxes(state.liveMask);
      applyRecordingState(s.recording || null);
      suggestRecordOutDir();
      suggestSnapshotOut();
      openWS();
    }
  } catch {
    // Pre-Phase-2 backend or transient — fall through.
  }
}

document.addEventListener("DOMContentLoaded", async () => {
  setupTabs();
  setupControls();
  await probePreloaded();
  await probeLiveSession();
});
