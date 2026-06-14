/* ═══════════════════════════════════════════════════════════════════════
 * N1MM Rotor Server — Web UI  (app.js)
 * ═══════════════════════════════════════════════════════════════════════ */
'use strict';

/* ─── SVG icons (cross-browser safe) ────────────────────────────────── */
const ICO_STOP     = `<svg viewBox="0 0 18 18" width="14" height="14" fill="currentColor"><rect x="3" y="3" width="12" height="12" rx="2"/></svg>`;
const ICO_POWER    = `<svg viewBox="0 0 18 18" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M9 2v6"/><path d="M5.5 4.2A6 6 0 1 0 12.5 4.2"/></svg>`;
const ICO_TRASH    = `<svg viewBox="0 0 18 18" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><polyline points="3,5 15,5"/><path d="M7 5V3h4v2"/><path d="M5 5l1 10h6l1-10"/></svg>`;
const ICO_LOCK     = `<svg viewBox="0 0 18 18" width="13" height="13" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><rect x="4" y="8" width="10" height="8" rx="1.5"/><path d="M6 8V6a3 3 0 0 1 6 0v2"/></svg>`;
const ICO_UNLOCK   = `<svg viewBox="0 0 18 18" width="13" height="13" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><rect x="4" y="8" width="10" height="8" rx="1.5"/><path d="M6 8V6a3 3 0 0 1 6 0"/></svg>`;
const ICO_LOGIN    = `<svg viewBox="0 0 18 18" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><path d="M7 3H4a1 1 0 0 0-1 1v10a1 1 0 0 0 1 1h3"/><polyline points="11,5 15,9 11,13"/><line x1="15" y1="9" x2="6" y2="9"/></svg>`;
const ICO_LOGOUT   = `<svg viewBox="0 0 18 18" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><path d="M11 3h3a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1h-3"/><polyline points="7,13 3,9 7,5"/><line x1="3" y1="9" x2="12" y2="9"/></svg>`;
const ICO_REBOOT   = `<svg viewBox="0 0 18 18" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><path d="M13.5 4.5A7 7 0 1 1 5 5.5"/><polyline points="5,2 5,6 9,6"/></svg>`;
const ICO_SHUTDOWN = ICO_POWER;
const ICO_GEAR     = `<svg viewBox="0 0 18 18" width="15" height="15" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M13.5 2a3 3 0 0 0-2.1 5.1L3.1 15.4a1 1 0 0 0 1.4 1.4l8.3-8.3A3 3 0 1 0 13.5 2z"/><circle cx="13.5" cy="4.5" r=".8" fill="currentColor" stroke="none"/></svg>`;
const ICO_CLOSE    = `<svg viewBox="0 0 18 18" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><line x1="3" y1="3" x2="15" y2="15"/><line x1="15" y1="3" x2="3" y2="15"/></svg>`;
const ICO_WARN     = `<svg viewBox="0 0 18 18" width="13" height="13" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><path d="M9 2L1 16h16L9 2z"/><line x1="9" y1="8" x2="9" y2="11"/><circle cx="9" cy="13.5" r=".6" fill="currentColor" stroke="none"/></svg>`;
const ICO_STORM    = `<svg viewBox="0 0 18 18" width="13" height="13" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><polyline points="11,2 7,9 10,9 7,16"/><path d="M4 6a5 5 0 0 0 0 6"/><path d="M14 6a5 5 0 0 1 0 6"/></svg>`;
const ICO_ANTENNA  = `<svg viewBox="0 0 18 18" width="22" height="22" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><line x1="9" y1="9" x2="9" y2="16"/><path d="M5 13a6 6 0 0 1 0-8"/><path d="M13 13a6 6 0 0 0 0-8"/><path d="M3 15a9 9 0 0 1 0-12"/><path d="M15 15a9 9 0 0 0 0-12"/><circle cx="9" cy="9" r="1.2" fill="currentColor" stroke="none"/></svg>`;
const ICO_CHART    = `<svg viewBox="0 0 18 18" width="13" height="13" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><polyline points="2,14 5,9 8,11 12,5 16,8"/><line x1="2" y1="16" x2="16" y2="16"/></svg>`;
const ICO_TERMINAL = `<svg viewBox="0 0 18 18" width="15" height="15" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="3" width="14" height="12" rx="2"/><polyline points="5,7 7,9 5,11"/><line x1="9" y1="11" x2="13" y2="11"/></svg>`;
/* Day: sun with rays. Night: crescent moon. */
const ICO_SUN  = `<svg viewBox="0 0 18 18" width="15" height="15" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round">
  <circle cx="9" cy="9" r="3.2"/>
  <line x1="9" y1="1.5" x2="9" y2="3.2"/><line x1="9" y1="14.8" x2="9" y2="16.5"/>
  <line x1="1.5" y1="9" x2="3.2" y2="9"/><line x1="14.8" y1="9" x2="16.5" y2="9"/>
  <line x1="3.4" y1="3.4" x2="4.6" y2="4.6"/><line x1="13.4" y1="13.4" x2="14.6" y2="14.6"/>
  <line x1="14.6" y1="3.4" x2="13.4" y2="4.6"/><line x1="4.6" y1="13.4" x2="3.4" y2="14.6"/>
</svg>`;
const ICO_MOON = `<svg viewBox="0 0 18 18" width="15" height="15" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round">
  <path d="M13.5 11.5A6.5 6.5 0 0 1 6.5 4.5a6.5 6.5 0 1 0 7 7z"/>
  <circle cx="12" cy="4"  r=".5" fill="currentColor" stroke="none"/>
  <circle cx="15" cy="7"  r=".4" fill="currentColor" stroke="none"/>
  <circle cx="13" cy="8"  r=".3" fill="currentColor" stroke="none"/>
</svg>`;

/* ─── State ──────────────────────────────────────────────────────────── */
let state = { rotors: [], wind: null, hist: [] };

/* ─── Log system (server-side log file) ──────────────────────────────── */
const LOG_HARD_MAX = 20000;
const LOG_TYPES    = ['ROTOR','WIND','STORM','API','CONN','ERR'];
const LOG_COLORS   = { ROTOR:'#5b9bd5', WIND:'#4caf7d', STORM:'#e67e22',
                       API:'#b0b0b0',   CONN:'#777777', ERR:'#e05050' };

let logEntries      = [];
let logFilter       = new Set(LOG_TYPES);
let logPanelOpen    = false;
let logNewCount     = 0;
let logFetchedSince = 0;   /* ms timestamp of last successful fetch */

/* Classify a server log line by content */
function logLineType(text) {
  if (/GOTO|STOP|Bcast|stall|consecutive error|rotor.*reconnect/i.test(text)) return 'ROTOR';
  if (/[Ww]ind.*dir=|[Ww]ind.*bft|[Ww]ind.*fetch|Ecowitt|fallback/i.test(text))  return 'WIND';
  if (/[Ss]torm|correction/i.test(text))                                           return 'STORM';
  if (/WARNING|[Ee]rror|failed|Cannot|not found/i.test(text))                      return 'ERR';
  if (/start|Listening|reconnect|running|version/i.test(text))                     return 'CONN';
  return 'API';
}

/* Parse a raw log line: "[YYYY-MM-DD HH:MM:SS] text" */
function parseServerLogLine(raw) {
  const m = raw.match(/^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\]\s*(.*)/);
  const ts   = m ? new Date(m[1].replace(' ','T')).getTime() : Date.now();
  const text = m ? m[2] : raw;
  return { ts, type: logLineType(text), msg: text };
}

function logCutoff() { return Date.now() - 24 * 3600000; }

function logTrimOld() {
  const cutoff = logCutoff();
  let i = 0;
  while (i < logEntries.length && logEntries[i].ts < cutoff) i++;
  if (i > 0) logEntries.splice(0, i);
  if (logEntries.length > LOG_HARD_MAX)
    logEntries.splice(0, logEntries.length - LOG_HARD_MAX);
}

function logFiltered() { return logEntries.filter(e => logFilter.has(e.type)); }

/* Push one server log entry into the live panel (called from SSE handler) */
function logAppendLive(entry) {
  logEntries.push(entry);
  logTrimOld();
  updateLogBtnDot();
  if (!logPanelOpen) return;
  if (!logFilter.has(entry.type)) return;
  const body = document.getElementById('log-body');
  if (!body) return;
  const atBottom = logIsAtBottom(body);
  body.appendChild(makeLogLine(entry));
  updateLogCount();
  if (atBottom) {
    body.scrollTop = body.scrollHeight;
    logNewCount = 0;
  } else {
    logNewCount++;
    const badge = document.getElementById('log-new-badge');
    if (badge) { badge.textContent = `▼ ${logNewCount} new`; badge.style.display = ''; }
  }
}

/* Fetch last 24h from server and merge with in-memory entries.
   force=true skips the 30-second dedup cache. */
async function fetchLogHistory(force = false) {
  if (!force && logFetchedSince > 0 && (Date.now() - logFetchedSince) < 30000) {
    logTrimOld();
    return;
  }
  const sinceUnix = Math.floor(logCutoff() / 1000);
  let raw;
  try {
    const r = await fetch(`/api/log?since=${sinceUnix}`);
    if (!r.ok) { console.warn('fetchLogHistory: HTTP', r.status); return; }
    raw = await r.json();
  } catch(e) { console.warn('fetchLogHistory error:', e); return; }

  if (!Array.isArray(raw)) return;
  const cutoff  = logCutoff();
  const fetched = raw.map(parseServerLogLine).filter(e => e.ts >= cutoff);

  /* Merge: keep in-memory entries that are newer than the newest fetched entry
     (those are live SSE entries that arrived after the file was read), then
     deduplicate by timestamp+msg to avoid duplicates. */
  const maxFetchedTs = fetched.length ? fetched[fetched.length - 1].ts : 0;
  const liveOnly     = logEntries.filter(e => e.ts > maxFetchedTs);

  /* Build merged set, dedup by ts+msg */
  const seen   = new Set(fetched.map(e => e.ts + '|' + e.msg));
  const merged = [...fetched];
  for (const e of liveOnly) {
    const key = e.ts + '|' + e.msg;
    if (!seen.has(key)) { seen.add(key); merged.push(e); }
  }
  merged.sort((a, b) => a.ts - b.ts);

  logEntries      = merged;
  logFetchedSince = Date.now();
  logTrimOld();
}

function logIsAtBottom(el) {
  return el.scrollHeight - el.scrollTop - el.clientHeight < 40;
}

function logScrollToBottom() {
  const body = document.getElementById('log-body');
  if (body) body.scrollTop = body.scrollHeight;
  logNewCount = 0;
  const badge = document.getElementById('log-new-badge');
  if (badge) badge.style.display = 'none';
}

function makeLogLine(entry) {
  const div = document.createElement('div');
  div.className = 'log-line';
  const d   = new Date(entry.ts);
  const pad = n => String(n).padStart(2, '0');
  const ts  = `${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
  const col = LOG_COLORS[entry.type] || '#ccc';
  div.innerHTML =
    `<span class="log-ts">${ts}</span>` +
    `<span class="log-type" style="color:${col}">[${entry.type}]</span>` +
    `<span class="log-msg">${logEsc(entry.msg)}</span>`;
  return div;
}

function logEsc(s) {
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function renderLogView() {
  const body = document.getElementById('log-body');
  if (!body) return;
  body.innerHTML = '';
  logFiltered().forEach(e => body.appendChild(makeLogLine(e)));
  body.scrollTop = body.scrollHeight;
  updateLogCount();
}

function rebuildLogBody() { renderLogView(); }

function updateLogCount() {
  const el = document.getElementById('log-count');
  if (el) el.textContent = `${logFiltered().length} regels`;
}

function updateLogBtnDot() {
  /* Show red dot when there are ERR entries from the server log */
  const btn = document.getElementById('btn-log');
  if (!btn) return;
  btn.classList.toggle('log-has-err', logEntries.some(e => e.type === 'ERR'));
}

function toggleLogPanel() {
  if (logPanelOpen) {
    const panel = document.getElementById('log-panel');
    if (panel) panel.remove();
    logPanelOpen = false;
    return;
  }
  logPanelOpen = true;
  logNewCount  = 0;

  const panel = document.createElement('div');
  panel.id = 'log-panel';
  panel.className = 'log-panel';

  const chips = LOG_TYPES.map(t =>
    `<button class="log-chip${logFilter.has(t) ? ' active' : ''}" data-type="${t}" style="--chip-col:${LOG_COLORS[t]}">${t}</button>`
  ).join('');

  panel.innerHTML = `
    <div class="log-header" id="log-drag-handle">
      <span class="log-title">${ICO_TERMINAL} Server Log — laatste 24 uur</span>
      <div class="log-chips">
        ${chips}
        <button class="log-chip-all" title="Toggle all">ALL</button>
      </div>
      <button class="log-close" title="Close">×</button>
    </div>
    <div class="log-body" id="log-body"></div>
    <div class="log-footer">
      <button class="log-new-badge" id="log-new-badge" style="display:none">▼ 0 new</button>
      <span class="log-count" id="log-count"></span>
    </div>`;

  document.body.appendChild(panel);

  /* Center and size the panel */
  const pw = Math.min(940, Math.round(window.innerWidth  * 0.90));
  const ph = Math.min(620, Math.round(window.innerHeight * 0.82));
  panel.style.width  = pw + 'px';
  panel.style.height = ph + 'px';
  panel.style.left   = Math.round((window.innerWidth  - pw) / 2) + 'px';
  panel.style.top    = Math.round((window.innerHeight - ph) / 2) + 'px';

  /* Filter chips */
  panel.querySelectorAll('.log-chip').forEach(btn => {
    btn.addEventListener('click', () => {
      const t = btn.dataset.type;
      if (logFilter.has(t)) logFilter.delete(t); else logFilter.add(t);
      btn.classList.toggle('active', logFilter.has(t));
      renderLogView();
    });
  });
  panel.querySelector('.log-chip-all').addEventListener('click', () => {
    const allOn = LOG_TYPES.every(t => logFilter.has(t));
    if (allOn) logFilter.clear(); else LOG_TYPES.forEach(t => logFilter.add(t));
    panel.querySelectorAll('.log-chip').forEach(b =>
      b.classList.toggle('active', logFilter.has(b.dataset.type)));
    renderLogView();
  });

  panel.querySelector('.log-close').addEventListener('click', () => {
    panel.remove();
    logPanelOpen = false;
  });
  document.getElementById('log-new-badge').addEventListener('click', logScrollToBottom);

  /* Auto-scroll tracking */
  document.getElementById('log-body').addEventListener('scroll', () => {
    if (logIsAtBottom(document.getElementById('log-body'))) {
      logNewCount = 0;
      const badge = document.getElementById('log-new-badge');
      if (badge) badge.style.display = 'none';
    }
  });

  /* Draggable panel */
  logMakeDraggable(panel, document.getElementById('log-drag-handle'));

  /* Show existing in-memory entries immediately */
  renderLogView();

  /* Fetch full 24h history from server and re-render when done */
  const count = document.getElementById('log-count');
  if (count) count.textContent = 'ophalen…';
  fetchLogHistory(true).then(() => { renderLogView(); });
}

let authToken       = sessionStorage.getItem('auth_token') || '';
let authRequired    = false;
let controlRequired = false;

/* ─── Log panel drag ─────────────────────────────────────────────────── */
function logMakeDraggable(panel, handle) {
  /* Start dragging: switch from bottom-anchored to top-anchored */
  let dragging = false, ox = 0, oy = 0;
  handle.style.cursor = 'grab';
  handle.addEventListener('mousedown', e => {
    if (e.target.tagName === 'BUTTON') return; /* don't drag on buttons */
    const rect = panel.getBoundingClientRect();
    /* Convert to top/left fixed positioning */
    panel.style.bottom = '';
    panel.style.right  = '';
    panel.style.top    = rect.top  + 'px';
    panel.style.left   = rect.left + 'px';
    ox = e.clientX - rect.left;
    oy = e.clientY - rect.top;
    dragging = true;
    handle.style.cursor = 'grabbing';
    e.preventDefault();
  });
  document.addEventListener('mousemove', e => {
    if (!dragging) return;
    panel.style.left = Math.max(0, e.clientX - ox) + 'px';
    panel.style.top  = Math.max(0, e.clientY - oy) + 'px';
  });
  document.addEventListener('mouseup', () => {
    dragging = false;
    handle.style.cursor = 'grab';
  });
}

/* ─── Compass size ───────────────────────────────────────────────────── */
const CW = 180;

/* ─── Tab switching ──────────────────────────────────────────────────── */
document.querySelectorAll('.tab').forEach(btn => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach(b => b.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    btn.classList.add('active');
    document.getElementById('tab-' + btn.dataset.tab).classList.add('active');
    if (btn.dataset.tab === 'meteo') renderMeteo();
  });
});

/* ─── Connection status — colours the ⏻ menu button ─────────────────── */
function setConnected(ok) {
  const btn = document.getElementById('btn-menu');
  if (!btn) return;
  btn.classList.toggle('conn-ok',  ok);
  btn.classList.toggle('conn-err', !ok);
  btn.title = ok ? 'Connected' : 'Disconnected';
}

/* ─── SSE ────────────────────────────────────────────────────────────── */
function connectSSE() {
  const tok = authToken || viewToken;
  const url = tok ? '/api/events?token=' + encodeURIComponent(tok) : '/api/events';
  const es = new EventSource(url);
  es.onopen = () => setConnected(true);
  es.addEventListener('rotors', e => {
    state.rotors = JSON.parse(e.data);
    renderRotors();
    setConnected(true);
  });
  es.addEventListener('wind', e => {
    state.wind = JSON.parse(e.data);
    renderMeteoCards();
    renderWindRose();
    setConnected(true);
  });
  es.addEventListener('hist', e => {
    state.hist = JSON.parse(e.data);
    /* hist is used by renderHistGraph() for the theme-redraw; no inline canvas to update */
  });
  es.addEventListener('log', e => {
    const data = JSON.parse(e.data);
    const entry = parseServerLogLine(data.line || '');
    if (entry.ts >= logCutoff()) logAppendLive(entry);
  });
  es.onerror = () => {
    setConnected(false);
    es.close();
    setTimeout(connectSSE, 3000);
  };
}

/* ─── Auth ───────────────────────────────────────────────────────────── */
let viewToken = sessionStorage.getItem('view_token') || '';

async function fetchStatus() {
  try {
    const r = await fetch('/api/status');
    const d = await r.json();
    authRequired    = d.auth_required    || false;
    controlRequired = d.control_required || false;
    if (d.version) {
      const info = document.getElementById('header-info');
      if (info) info.textContent = 'v' + d.version + ' · ' + (d.author || 'PD5DJ');
    }
    return true;
  } catch(e) { return false; }
}


/* showLogin(callback, reason, adminOnly, roleToast)
 * adminOnly=true  → control-password login is rejected with an error message.
 * roleToast=true  → after successful login show a toast: "Logged in as Admin/Controller" */
function showLogin(callback, reason, adminOnly, roleToast) {
  const dlg = document.getElementById('dlg-login');
  dlg.style.display = 'flex';
  /* Show reason above input if provided */
  let reasonEl = dlg.querySelector('.login-reason');
  if (!reasonEl) {
    reasonEl = document.createElement('div');
    reasonEl.className = 'login-reason';
    reasonEl.style.cssText = 'font-size:12px;color:var(--orange);margin-bottom:8px;display:none';
    dlg.querySelector('.dlg').insertBefore(reasonEl, dlg.querySelector('input'));
  }
  if (reason) { reasonEl.textContent = reason; reasonEl.style.display = ''; }
  else        { reasonEl.style.display = 'none'; }
  document.getElementById('pw-error').textContent = '';
  document.getElementById('pw-input').value = '';
  document.getElementById('pw-input').focus();
  document.getElementById('btn-login-cancel').onclick = () => { dlg.style.display = 'none'; };
  document.getElementById('btn-login-ok').onclick = async () => {
    const pw = document.getElementById('pw-input').value;
    const r = await fetch('/api/login', {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify({password: pw})
    });
    if (r.ok) {
      const d = await r.json();
      /* Always store viewToken (control access) */
      viewToken = d.token;
      sessionStorage.setItem('view_token', viewToken);
      /* Admin password also stores authToken (settings access) */
      if (d.operator) {
        authToken = d.token;
        sessionStorage.setItem('auth_token', authToken);
      }
      /* If admin-only action but user entered control password → block */
      if (adminOnly && !d.operator) {
        document.getElementById('pw-error').textContent = 'Admin password required';
        document.getElementById('pw-input').value = '';
        document.getElementById('pw-input').focus();
        return;
      }
      dlg.style.display = 'none';
      updateLogoutBtn();
      if (roleToast) showToast(d.operator ? 'Logged in as Admin' : 'Logged in as Controller');
      if (callback) callback();
    } else {
      document.getElementById('pw-error').textContent = 'Wrong password';
      document.getElementById('pw-input').select();
    }
  };
  document.getElementById('pw-input').onkeydown = e => {
    if (e.key === 'Enter') document.getElementById('btn-login-ok').click();
  };
}

function updateLogoutBtn() { /* no-op — menu is rebuilt dynamically */ }

async function sendGoto(name, az) {
  const r = await fetch('/api/goto', {
    method: 'POST',
    headers: {'Content-Type':'application/json', ...authHeader()},
    body: JSON.stringify({rotor: name, az: az})
  });
  if (r.status === 401) { showToast('Please login first', true); return false; }
  return r.ok;
}

async function sendStop(name) {
  const r = await fetch('/api/stop', {
    method: 'POST',
    headers: {'Content-Type':'application/json', ...authHeader()},
    body: JSON.stringify({rotor: name})
  });
  if (r.status === 401) { showToast('Please login first', true); return false; }
  return r.ok;
}

async function sendStormForce() {
  const r = await fetch('/api/storm', {
    method: 'POST',
    headers: {'Content-Type':'application/json', 'Authorization':'Bearer '+authToken},
    body: JSON.stringify({force: 1})
  });
  if (r.status === 401) showToast('Please login first', true);
}

async function sendStorm(on) {
  const r = await fetch('/api/storm', {
    method: 'POST',
    headers: {'Content-Type':'application/json', 'Authorization':'Bearer '+authToken},
    body: JSON.stringify({on: on ? 1 : 0})
  });
  if (r.status === 401) showToast('Please login first', true);
}

/* ─── Compass drawing ────────────────────────────────────────────────── */
function drawCompass(canvas, currentAz, targetAz, hasData, isMulti, offset) {
  const ctx  = canvas.getContext('2d');
  const W    = canvas.width, H = canvas.height;
  const cx   = W/2, cy = H/2;
  const r    = W/2 - 6;
  const dark = !isLight();
  ctx.clearRect(0,0,W,H);

  /* ── Background circle ── */
  ctx.beginPath(); ctx.arc(cx,cy,r,0,Math.PI*2);
  ctx.fillStyle = dark ? '#2a2a2a' : '#ffffff';
  ctx.fill();
  ctx.strokeStyle = dark ? '#666666' : '#c8d0dc';
  ctx.lineWidth = 2;
  ctx.stroke();

  if (!hasData) {
    /* Still draw target line if we have a pending target */
    if (!isMulti && targetAz >= 0) {
      const tr = (targetAz-90)*Math.PI/180;
      ctx.strokeStyle='rgba(52,152,219,.5)'; ctx.lineWidth=1.5;
      ctx.setLineDash([5,4]);
      ctx.beginPath(); ctx.moveTo(cx,cy);
      ctx.lineTo(cx+(r-8)*Math.cos(tr), cy+(r-8)*Math.sin(tr));
      ctx.stroke(); ctx.setLineDash([]);
    }
    ctx.fillStyle = dark ? 'rgba(255,255,255,.25)' : 'rgba(0,0,0,.25)';
    ctx.textAlign='center'; ctx.textBaseline='middle';
    ctx.font='bold 13px sans-serif';
    ctx.fillText('NO DATA', cx, cy);
    return;
  }

  /* ── Tick marks ── */
  for (let deg=0; deg<360; deg+=10) {
    const rad   = (deg-90)*Math.PI/180;
    const major = deg%90===0, mid = deg%30===0;
    const len   = major ? 12 : mid ? 8 : 4;
    ctx.strokeStyle = major
      ? (dark ? 'rgba(255,255,255,.85)' : 'rgba(0,0,0,.60)')
      : mid
        ? (dark ? 'rgba(255,255,255,.45)' : 'rgba(0,0,0,.30)')
        : (dark ? 'rgba(255,255,255,.22)' : 'rgba(0,0,0,.18)');
    ctx.lineWidth = major ? 2 : mid ? 1.2 : 0.8;
    ctx.beginPath();
    ctx.moveTo(cx + r*Math.cos(rad),       cy + r*Math.sin(rad));
    ctx.lineTo(cx + (r-len)*Math.cos(rad), cy + (r-len)*Math.sin(rad));
    ctx.stroke();
  }

  /* ── Cardinal labels ── */
  const cardinals = {0:'N', 90:'E', 180:'S', 270:'W'};
  const labelR = r - 18;
  ctx.textAlign='center'; ctx.textBaseline='middle'; ctx.font='bold 12px sans-serif';
  for (const [deg, lbl] of Object.entries(cardinals)) {
    const rad = (deg-90)*Math.PI/180;
    ctx.fillStyle = lbl==='N' ? '#e05050'
                  : (dark ? 'rgba(255,255,255,.90)' : 'rgba(0,0,0,.65)');
    ctx.fillText(lbl, cx + labelR*Math.cos(rad), cy + labelR*Math.sin(rad));
  }

  /* ── Target line (dashed blue) ── */
  if (targetAz >= 0) {
    const tr = (targetAz-90)*Math.PI/180;
    ctx.strokeStyle = dark ? '#5b9bd5' : '#2980b9'; ctx.lineWidth = 2;
    ctx.setLineDash([5,4]);
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(cx + (r-8)*Math.cos(tr), cy + (r-8)*Math.sin(tr));
    ctx.stroke(); ctx.setLineDash([]);
  }

  /* ── Current azimuth needle (orange) ── */
  if (!isMulti) {
    const ar   = (currentAz-90)*Math.PI/180;
    const tipX = cx + (r-8)*Math.cos(ar);
    const tipY = cy + (r-8)*Math.sin(ar);
    /* Shaft */
    ctx.strokeStyle = '#e67e22'; ctx.lineWidth = 3.5;
    ctx.lineCap = 'round';
    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(tipX, tipY); ctx.stroke();
    /* Arrowhead */
    const hs = 11;
    ctx.fillStyle = '#e67e22';
    ctx.beginPath();
    ctx.moveTo(tipX, tipY);
    ctx.lineTo(tipX - hs*Math.cos(ar-0.38), tipY - hs*Math.sin(ar-0.38));
    ctx.lineTo(tipX - hs*Math.cos(ar+0.38), tipY - hs*Math.sin(ar+0.38));
    ctx.closePath(); ctx.fill();
  }

  /* ── Centre dot ── */
  ctx.fillStyle = dark ? 'rgba(255,255,255,.55)' : 'rgba(0,0,0,.25)';
  ctx.beginPath(); ctx.arc(cx,cy,4,0,Math.PI*2); ctx.fill();

  /* ── Multi: show target azimuth label in lower half ── */
  if (isMulti && targetAz >= 0) {
    const bg = dark ? 'rgba(0,0,0,.48)' : 'rgba(255,255,255,.72)';
    const yTgt = cy + r * 0.36;
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.fillStyle = bg;
    ctx.beginPath(); ctx.roundRect(cx - 36, yTgt - 10, 72, 20, 5); ctx.fill();
    ctx.fillStyle = '#2980b9';
    ctx.font = '13px sans-serif';
    ctx.fillText(targetAz.toFixed(1) + '°', cx, yTgt);
  }

  /* ── Azimuth values: current above centre dot, target+offset below ── */
  if (!isMulti) {
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    const bg = dark ? 'rgba(0,0,0,.48)' : 'rgba(255,255,255,.72)';

    /* ── Current heading: between centre dot and N (upper half) ── */
    const yAz = cy - r * 0.36;
    ctx.fillStyle = bg;
    ctx.beginPath(); ctx.roundRect(cx - 36, yAz - 10, 72, 20, 5); ctx.fill();
    ctx.fillStyle = '#e67e22';
    ctx.font = 'bold 15px sans-serif';
    ctx.fillText(currentAz.toFixed(1) + '°', cx, yAz);

    /* ── Target + offset: between centre dot and S (lower half) ── */
    if (!isMulti && (targetAz >= 0 || (offset && offset !== 0))) {
      const hasOffset = offset && offset !== 0;
      const yTgt = cy + r * 0.36;
      const yOff = yTgt + 14;
      const boxH = hasOffset ? 34 : 20;
      ctx.fillStyle = bg;
      ctx.beginPath(); ctx.roundRect(cx - 38, yTgt - 10, 76, boxH, 5); ctx.fill();
      if (targetAz >= 0) {
        ctx.fillStyle = '#2980b9';
        ctx.font = '13px sans-serif';
        ctx.fillText(targetAz.toFixed(1) + '°', cx, yTgt);
      }
      if (hasOffset) {
        ctx.fillStyle = dark ? 'rgba(255,255,255,.50)' : 'rgba(0,0,0,.42)';
        ctx.font = '10px sans-serif';
        ctx.fillText('Offset: ' + (offset > 0 ? '+' : '') + offset + '°', cx, yOff);
      }
    }
  }
}

/* ─── Rotor cards ────────────────────────────────────────────────────── */
const rotorGrid = document.getElementById('rotor-grid');
const noRotors  = document.getElementById('no-rotors');

let multiTargetAz = -1;

function buildMultiCard() {
  const card = document.createElement('div');
  card.className = 'card multi-card';
  card.id = 'rcard-multi';
  card.innerHTML = `
    <div class="card-title">
      <span class="rcard-name">Multi Control</span>
      <div class="card-actions">
        <span class="badge" style="background:var(--orange);color:#fff;font-size:10px;padding:2px 6px">ALL</span>
      </div>
    </div>
    <div class="compass-wrap"><canvas class="compass" id="multi-canvas" width="${CW}" height="${CW}"></canvas></div>
    <div style="text-align:center;font-size:11px;color:var(--orange);margin-top:2px;height:16px;overflow:hidden">&#9888; Click sends GOTO to all rotors</div>
    <button class="btn-stop">${ICO_STOP} Stop all</button>`;
  const cvs = card.querySelector('canvas');
  cvs.addEventListener('click', e => onMultiCompassClick(e, cvs));
  cvs.addEventListener('dblclick', e => onMultiCompassDblClick(e, cvs));
  card.querySelector('.btn-stop').addEventListener('click', () => {
    requireControlAuth(() => {
      for (const r of state.rotors) sendStop(r.name);
      showToast('All rotors: Stop');
    });
  });
  return card;
}

function onMultiCompassClick(e, canvas) {
  const az = azFromClick(e, canvas);
  if (az === null) return;
  clearTimeout(clickTimers['__multi__']);
  clickTimers['__multi__'] = setTimeout(() => {
    delete clickTimers['__multi__'];
    requireControlAuth(() => {
      const dlg = document.getElementById('dlg-goto');
      document.getElementById('goto-rotor-name').textContent = 'ALL rotors';
      document.getElementById('goto-az').value = Math.round(az);
      dlg.style.display = 'flex';
      document.getElementById('btn-goto-cancel').onclick = () => dlg.style.display = 'none';
      document.getElementById('btn-goto-ok').onclick = () => {
        const goAz = parseFloat(document.getElementById('goto-az').value);
        if (isNaN(goAz)) return;
        dlg.style.display = 'none';
        multiTargetAz = goAz;
        for (const r of state.rotors) sendGoto(r.name, goAz);
        showToast('All rotors → ' + goAz + '°');
        updateMultiCanvas();
      };
    });
  }, 250);
}

function onMultiCompassDblClick(e, canvas) {
  e.preventDefault();
  clearTimeout(clickTimers['__multi__']);
  delete clickTimers['__multi__'];
  const az = azFromClick(e, canvas);
  if (az === null) return;
  requireControlAuth(async () => {
    multiTargetAz = az;
    const ok = await sendGoto(state.rotors[0]?.name, az);
    if (ok !== false) {
      for (const r of state.rotors.slice(1)) sendGoto(r.name, az);
      showToast('All rotors → ' + az.toFixed(1) + '°');
      updateMultiCanvas();
    }
  });
}

function updateMultiCanvas() {
  const cvs = document.getElementById('multi-canvas');
  if (!cvs) return;
  drawCompass(cvs, 0, multiTargetAz, true, true, 0);
}

function azFromClick(e, canvas) {
  const rect = canvas.getBoundingClientRect();
  const x = e.clientX - rect.left - CW/2;
  const y = e.clientY - rect.top  - CW/2;
  /* Only accept clicks inside the compass circle */
  if (Math.sqrt(x*x + y*y) > CW/2) return null;
  let az = Math.atan2(x, -y) * 180 / Math.PI;
  if (az < 0) az += 360;
  return Math.round(az * 10) / 10;
}

const clickTimers = {};   /* rotor name → setTimeout id */

function onCompassClick(e, canvas, rotorName) {
  const az = azFromClick(e, canvas);
  if (az === null) return;
  /* Delay single-click so a double-click can cancel it first */
  clearTimeout(clickTimers[rotorName]);
  clickTimers[rotorName] = setTimeout(() => {
    delete clickTimers[rotorName];
    requireControlAuth(() => {
      const dlg = document.getElementById('dlg-goto');
      document.getElementById('goto-rotor-name').textContent = rotorName;
      document.getElementById('goto-az').value = Math.round(az);
      dlg.style.display = 'flex';
      document.getElementById('btn-goto-cancel').onclick = () => dlg.style.display = 'none';
      document.getElementById('btn-goto-ok').onclick = () => {
        const goAz = parseFloat(document.getElementById('goto-az').value);
        if (isNaN(goAz)) return;
        dlg.style.display = 'none';
        sendGoto(rotorName, goAz);
      };
    });
  }, 250);
}

function onCompassDblClick(e, canvas, rotorName) {
  e.preventDefault();
  /* Cancel pending single-click popup */
  clearTimeout(clickTimers[rotorName]);
  delete clickTimers[rotorName];
  const az = azFromClick(e, canvas);
  if (az === null) return;
  requireControlAuth(async () => {
    const ok = await sendGoto(rotorName, az);
    if (ok !== false) showToast(rotorName + ' → ' + az.toFixed(1) + '°');
  });
}

/* ─── Meteo tab ──────────────────────────────────────────────────────── */
const meteoGrid = document.getElementById('meteo-grid');
const noMeteo   = document.getElementById('no-meteo');

/* Rendered once, updated in-place */
let meteoCardsBuilt = false;

function renderMeteo() {
  if (!state.wind) { noMeteo.style.display=''; meteoGrid.style.display='none'; return; }
  noMeteo.style.display='none'; meteoGrid.style.display='';
  if (!meteoCardsBuilt) buildMeteoCards();
  renderMeteoCards();
  renderWindRose();
}

function buildMeteoCards() {
  meteoCardsBuilt = true;
  meteoGrid.innerHTML = '';

  /* ── 1. Wind Monitor — always top-left (order:-1) ──────────────────── */
  const wmon = makeCard('mc-monitor', 'Wind Monitor', `
    <div class="wmon-status">
      <div class="storm-dot normal" id="mc-storm-dot"></div>
      <span id="mc-storm-lbl">Normal operation</span>
    </div>
    <div class="wmon-rows">
      <div class="wmon-row"><span class="wk">Source</span>    <span class="wv" id="mc-source">—</span></div>
      <div class="wmon-row"><span class="wk">Direction</span> <span class="wv" id="mc-dir-val">—</span></div>
      <div class="wmon-row"><span class="wk">Force</span>     <span class="wv" id="mc-bft">—</span></div>
      <div class="wmon-row"><span class="wk">Gust</span>      <span class="wv" id="mc-gust">—</span></div>
      <div class="wmon-row"><span class="wk">Threshold</span> <span class="wv" id="mc-thr">—</span></div>
      <div class="wmon-row"><span class="wk">Timer</span>     <span class="wv" id="mc-timer">—</span></div>
      <div class="wmon-row"><span class="wk">Last data</span> <span class="wv" id="mc-fetch">—</span></div>
    </div>
    <div style="display:flex;gap:6px;margin-top:8px">
      <button class="storm-btn off" id="mc-storm-btn" style="flex:2;font-size:12px;padding:9px 6px">Monitor OFF</button>
      <button class="storm-btn" id="mc-storm-force-btn" style="flex:1;font-size:12px;padding:9px 6px;background:var(--orange)" disabled>Activate</button>
    </div>
  `, 'wmon-card', 'wind');
  wmon.style.order = '-1';
  meteoGrid.appendChild(wmon);

  /* ── 2. Wind Direction ───────────────────────────────────────────────── */
  meteoGrid.appendChild(makeCard('mc-rose','Wind Direction',`
    <div class="wind-rose-wrap">
      <canvas id="mc-rose-canvas" width="180" height="180"></canvas>
    </div>
    <div style="text-align:center;font-size:11px;color:var(--text2);margin-top:4px;height:16px;overflow:hidden" id="mc-rose-lbl"></div>
  `));

  /* ── 4. Temperature ──────────────────────────────────────────────────── */
  meteoGrid.appendChild(makeCard('mc-temp','Temperature',`
    <div><span class="meteo-value" id="mc-temp-val">—</span><span class="meteo-unit">°C</span></div>
    <div class="meteo-row" style="margin-top:8px">
      <span class="key">Feels like</span><span class="val" id="mc-feels">—</span>
    </div>
  `,'mc-temp-card','temp'));

  /* ── 5. Humidity ─────────────────────────────────────────────────────── */
  meteoGrid.appendChild(makeCard('mc-hum','Humidity',`
    <div><span class="meteo-value" id="mc-hum-val">—</span><span class="meteo-unit">%RH</span></div>
  `,'mc-hum-card','hum'));

  /* ── 6. Barometer ────────────────────────────────────────────────────── */
  meteoGrid.appendChild(makeCard('mc-baro','Barometer',`
    <div><span class="meteo-value" id="mc-baro-val">—</span><span class="meteo-unit">hPa</span></div>
  `,'mc-baro-card','baro'));

  /* ── 7. Precipitation ────────────────────────────────────────────────── */
  meteoGrid.appendChild(makeCard('mc-precip','Precipitation',`
    <div><span class="meteo-value" id="mc-precip-val">—</span><span class="meteo-unit">mm/h</span></div>
  `,'mc-precip-card','precip'));

  /* ── 8. UV / Solar ───────────────────────────────────────────────────── */
  meteoGrid.appendChild(makeCard('mc-uv','UV / Solar',`
    <div class="meteo-row">
      <span class="key">UV index</span><span class="val" id="mc-uv-val">—</span>
    </div>
    <div class="meteo-row">
      <span class="key">Solar</span><span class="val" id="mc-solar-val">—</span>
    </div>
  `,'mc-uv-card','uvsolar'));

  /* Monitor ON/OFF button */
  document.getElementById('mc-storm-btn').addEventListener('click', () => {
    const btn = document.getElementById('mc-storm-btn');
    const on  = btn.classList.contains('off');
    requireAuth(() => sendStorm(on));
  });

  /* Force Activate button — bypass sustain, trigger immediately */
  document.getElementById('mc-storm-force-btn').addEventListener('click', () => {
    requireAuth(() => sendStormForce());
  });

  /* Chart button handlers (wind + meteo value cards) */
  meteoGrid.querySelectorAll('.hist-chart-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      if (btn.dataset.hist === 'wind') showWindHistChart();
      else showMeteoHistChart(btn.dataset.hist);
    });
  });
}

function makeCard(id, title, inner, extraClass, histKey) {
  const d = document.createElement('div');
  d.className = 'card' + (extraClass ? ' '+extraClass : '');
  d.id = id;
  const btn = histKey
    ? `<div class="card-actions"><button class="btn-icon hist-chart-btn" title="48h history" data-hist="${histKey}">${ICO_CHART}</button></div>`
    : '';
  d.innerHTML = `<div class="card-title">${title}${btn}</div>${inner}`;
  return d;
}

const BFT_COLORS = ['#27ae60','#2ecc71','#f1c40f','#f39c12','#e67e22','#e74c3c',
                    '#c0392b','#9b59b6','#8e44ad','#2980b9','#1abc9c','#16a085','#95a5a6'];
const COMPASS_DIRS = ['N','NNE','NE','ENE','E','ESE','SE','SSE','S','SSW','SW','WSW','W','WNW','NW','NNW'];
function degToDir(d) { return COMPASS_DIRS[Math.round(d/22.5)%16]; }
function bftToMs(bft) {
  const ms=[0,0.3,1.6,3.4,5.5,8.0,10.8,13.9,17.2,20.8,24.5,28.5,32.7];
  return bft <= 12 ? ms[bft] : 32.7;
}

function fmtFetch(ts) {
  if (!ts || ts === 0) return '—';
  const d = new Date(ts * 1000);
  const pad = n => String(n).padStart(2,'0');
  return pad(d.getDate())+' '+['Jan','Feb','Mar','Apr','May','Jun',
    'Jul','Aug','Sep','Oct','Nov','Dec'][d.getMonth()]+' '+
    pad(d.getHours())+':'+pad(d.getMinutes())+':'+pad(d.getSeconds());
}

function windSourceLabel(cfg) {
  if (!cfg) return '—';
  const src = cfg.wind_source || 'openmeteo';
  const map = { openmeteo:'Open-Meteo', yrno:'Yr.no', owm:'OpenWeatherMap',
                wapi:'WeatherAPI', ecowitt:'Ecowitt' };
  return map[src] || src;
}

function renderMeteoCards() {
  const w = state.wind;
  if (!w) return;

  /* ── Wind Monitor card ── */
  const corr = w.correcting;
  const dot  = document.getElementById('mc-storm-dot');
  const lbl  = document.getElementById('mc-storm-lbl');
  const btn  = document.getElementById('mc-storm-btn');
  if (dot) dot.className = 'storm-dot ' + (corr ? 'correcting' : 'normal');
  if (lbl) lbl.textContent = corr ? 'Storm correction active' : 'Normal operation';
  if (btn) { btn.className = 'storm-btn '+(w.storm?'on':'off');
             btn.textContent = w.storm ? 'Monitor ON' : 'Monitor OFF'; }
  const forceBtn = document.getElementById('mc-storm-force-btn');
  if (forceBtn) {
    forceBtn.disabled = !w.storm;
    forceBtn.style.opacity = w.storm ? '1' : '0.4';
  }

  /* ── Storm status in header ── */
  const hbadge = document.getElementById('storm-header-badge');
  if (hbadge) {
    if (corr) {
      hbadge.innerHTML = ICO_STORM + ' STORM';
      hbadge.className = 'storm-header-badge correcting';
      hbadge.style.display = '';
    } else if (w.storm) {
      hbadge.innerHTML = ICO_STORM + ' STORM';
      hbadge.className = 'storm-header-badge active';
      hbadge.style.display = '';
    } else {
      hbadge.style.display = 'none';
    }
  }

  const hasData = w.fetch > 0 || w.bft > 0 || w.dir > 0;
  /* Source label: show fallback source when active */
  const isFallback = w.fallback && cfgCache && cfgCache.wind_ecowitt_fallback;
  const srcLabel = isFallback
    ? windSourceLabel({wind_source: cfgCache.wind_ecowitt_fallback}) + ' (fallback)'
    : windSourceLabel(cfgCache);
  setText('mc-source', srcLabel);
  /* Fallback badge on status line */
  const statusEl = document.getElementById('mc-storm-lbl');
  const dotEl    = document.getElementById('mc-storm-dot');
  if (isFallback && !w.correcting) {
    if (dotEl) dotEl.className = 'storm-dot fallback';
    if (statusEl) statusEl.textContent = 'Ecowitt fallback active';
  }
  setText('mc-dir-val', hasData ? Math.round(w.dir)+'°  '+degToDir(w.dir) : '—');
  setText('mc-bft',     hasData ? w.bft+' Bft' : '—');
  setText('mc-gust',    hasData ? w.gust.toFixed(1)+' m/s' : '—');
  setText('mc-thr',     (cfgCache ? cfgCache.storm_threshold_bft : '—')+' Bft');
  setText('mc-fetch',   fmtFetch(w.fetch));

  /* Timer display */
  const t = w.timer || 0;
  if (t > 0)       setText('mc-timer', t+'s → active');
  else if (t < 0)  setText('mc-timer', Math.abs(t)+'s → release');
  else             setText('mc-timer', '—');

  /* ── Conditional meteo cards ── */
  showCard('mc-temp-card',   w.temp   !== undefined);
  showCard('mc-hum-card',    w.hum    !== undefined);
  showCard('mc-baro-card',   w.baro   !== undefined);
  showCard('mc-precip-card', w.precip !== undefined);
  showCard('mc-uv-card',     w.uv !== undefined || w.solar !== undefined);

  if (w.temp   !== undefined) setText('mc-temp-val',   w.temp.toFixed(1));
  if (w.feels  !== undefined) setText('mc-feels',      w.feels.toFixed(1)+'°C');
  else                        setText('mc-feels','—');
  if (w.hum    !== undefined) setText('mc-hum-val',    Math.round(w.hum));
  if (w.baro   !== undefined) setText('mc-baro-val',   w.baro.toFixed(1));
  if (w.precip !== undefined) setText('mc-precip-val', w.precip.toFixed(1));
  if (w.uv     !== undefined) setText('mc-uv-val',     Math.round(w.uv));
  else                        setText('mc-uv-val','—');
  if (w.solar  !== undefined) setText('mc-solar-val',  Math.round(w.solar)+' W/m²');
  else                        setText('mc-solar-val','—');
}

function setText(id, val) {
  const el = document.getElementById(id);
  if (el) el.textContent = val;
}
function showCard(id, show) {
  const el = document.getElementById(id);
  if (el) el.style.display = show ? '' : 'none';
}

/* ─── Theme helper ────────────────────────────────────────────────────── */
function isLight() { return document.body.classList.contains('light'); }

/* roundRect polyfill for browsers without native support */
if (!CanvasRenderingContext2D.prototype.roundRect) {
  CanvasRenderingContext2D.prototype.roundRect = function(x, y, w, h, r) {
    this.beginPath();
    this.moveTo(x+r, y);
    this.lineTo(x+w-r, y);  this.quadraticCurveTo(x+w, y,   x+w, y+r);
    this.lineTo(x+w, y+h-r); this.quadraticCurveTo(x+w, y+h, x+w-r, y+h);
    this.lineTo(x+r, y+h);  this.quadraticCurveTo(x,   y+h, x,   y+h-r);
    this.lineTo(x, y+r);    this.quadraticCurveTo(x,   y,   x+r, y);
    this.closePath();
  };
}

/* ─── Wind Rose ──────────────────────────────────────────────────────── */
function renderWindRose() {
  const canvas = document.getElementById('mc-rose-canvas');
  if (!canvas || !state.wind) return;
  const w = state.wind;
  const ctx = canvas.getContext('2d');
  const cw=canvas.width, ch=canvas.height, cx=cw/2, cy=ch/2, r=cx-10;
  const light = isLight();
  ctx.clearRect(0,0,cw,ch);

  /* Background */
  ctx.fillStyle = light ? '#dce8f8' : '#2a2a2a';
  ctx.beginPath(); ctx.arc(cx,cy,r,0,Math.PI*2); ctx.fill();
  ctx.strokeStyle = light ? 'rgba(0,0,0,.18)' : '#666666';
  ctx.lineWidth = light ? 1 : 2;
  ctx.beginPath(); ctx.arc(cx,cy,r,0,Math.PI*2); ctx.stroke();

  /* Cardinal rings */
  for (const rr of [r*.33,r*.66]) {
    ctx.strokeStyle = light ? 'rgba(0,0,0,.07)' : 'rgba(255,255,255,.12)';
    ctx.lineWidth = light ? .7 : 1;
    ctx.setLineDash([3,3]);
    ctx.beginPath(); ctx.arc(cx,cy,rr,0,Math.PI*2); ctx.stroke();
    ctx.setLineDash([]);
  }

  /* Tick marks */
  for (let deg=0; deg<360; deg+=10) {
    const trad = (deg-90)*Math.PI/180;
    const major = deg%90===0, mid = deg%30===0;
    const len = major ? 10 : mid ? 6 : 3;
    ctx.strokeStyle = major
      ? (light ? 'rgba(0,0,0,.55)' : 'rgba(255,255,255,.85)')
      : mid
        ? (light ? 'rgba(0,0,0,.28)' : 'rgba(255,255,255,.40)')
        : (light ? 'rgba(0,0,0,.15)' : 'rgba(255,255,255,.18)');
    ctx.lineWidth = major ? 1.8 : mid ? 1.1 : 0.7;
    ctx.beginPath();
    ctx.moveTo(cx+r*Math.cos(trad),       cy+r*Math.sin(trad));
    ctx.lineTo(cx+(r-len)*Math.cos(trad), cy+(r-len)*Math.sin(trad));
    ctx.stroke();
  }

  /* Cardinal labels */
  ctx.textAlign='center'; ctx.textBaseline='middle'; ctx.font='bold 11px sans-serif';
  const clbls={0:'N',90:'E',180:'S',270:'W'};
  for (const [d,l] of Object.entries(clbls)) {
    const lrad=(d-90)*Math.PI/180, rr=r-12;
    ctx.fillStyle = l==='N' ? '#e05050'
                  : (light ? 'rgba(0,0,0,.65)' : 'rgba(255,255,255,.90)');
    ctx.fillText(l, cx+rr*Math.cos(lrad), cy+rr*Math.sin(lrad));
  }

  if (!w) {
    ctx.fillStyle = light ? 'rgba(0,0,0,.3)' : 'rgba(255,255,255,.3)';
    ctx.textAlign='center'; ctx.font='12px sans-serif'; ctx.fillText('NO DATA',cx,cy); return;
  }

  /* Arrow: tail at wind source, tip at centre */
  const dir = w.dir;
  const rad = (dir-90)*Math.PI/180;
  const tx = cx+r*.70*Math.cos(rad), ty = cy+r*.70*Math.sin(rad); /* tail */
  const hx = cx-r*.42*Math.cos(rad), hy = cy-r*.42*Math.sin(rad); /* head (near centre) */

  /* Shaft */
  ctx.strokeStyle = light ? '#2980b9' : '#5b9bd5'; ctx.lineWidth=3.5; ctx.setLineDash([]);
  ctx.lineCap='round';
  ctx.beginPath(); ctx.moveTo(tx,ty); ctx.lineTo(hx,hy); ctx.stroke();

  /* Arrow head */
  const hs=13, a=Math.atan2(hy-ty,hx-tx);
  ctx.fillStyle = light ? '#2980b9' : '#5b9bd5';
  ctx.beginPath();
  ctx.moveTo(hx,hy);
  ctx.lineTo(hx-hs*Math.cos(a-0.4),hy-hs*Math.sin(a-0.4));
  ctx.lineTo(hx-hs*Math.cos(a+0.4),hy-hs*Math.sin(a+0.4));
  ctx.closePath(); ctx.fill();

  /* Label */
  const lbl = document.getElementById('mc-rose-lbl');
  if (lbl) lbl.textContent = Math.round(dir)+'° '+degToDir(dir)+'  |  '+w.bft+' Bft';
}

/* ─── Wind History Graph ─────────────────────────────────────────────── */
function renderHistGraph() {
  const canvas = document.getElementById('mc-hist-canvas');
  if (!canvas) return;
  /* Sync canvas resolution to CSS display size */
  const dpr = window.devicePixelRatio || 1;
  const cw = Math.round(canvas.clientWidth  * dpr) || 400;
  const ch = Math.round(canvas.clientHeight * dpr) || 120;
  if (canvas.width !== cw || canvas.height !== ch) {
    canvas.width  = cw;
    canvas.height = ch;
  }
  const hist = state.hist;
  const ctx  = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const lw = canvas.clientWidth  || (cw / dpr);
  const lh = canvas.clientHeight || (ch / dpr);
  ctx.clearRect(0, 0, lw, lh);

  const dark = !isLight();

  /* Margins */
  const ml=34, mr=32, mt=10, mb=20;
  const pw=lw-ml-mr, ph=lh-mt-mb;
  const dx0=ml, dy0=mt;

  /* Direction axis: fixed 0-360 */
  const dTicks = [0, 90, 180, 270, 360];
  const dyOf   = d => dy0 + ph * (1 - d / 360);

  /* Bft axis: dynamic — scale to actual data max */
  const maxBftData = hist.length ? Math.max(...hist.map(h => h.bft)) : 0;
  const bftMax = Math.max(maxBftData, 3);
  const byOf   = b => dy0 + ph * (1 - b / bftMax);
  /* Generate nice ticks for Bft */
  function bftNiceTicks(max) {
    const step = max <= 4 ? 1 : max <= 8 ? 2 : 3;
    const ticks = [];
    for (let v = 0; v <= max; v += step) ticks.push(v);
    if (ticks[ticks.length - 1] < max) ticks.push(max);
    return ticks;
  }
  const bftTicks = bftNiceTicks(bftMax);

  /* Direction grid lines */
  ctx.setLineDash([3,3]); ctx.lineWidth=.6;
  ctx.strokeStyle = dark ? 'rgba(255,255,255,.12)' : 'rgba(0,0,0,.1)';
  for (const d of dTicks) {
    const y = dyOf(d);
    ctx.beginPath(); ctx.moveTo(dx0, y); ctx.lineTo(dx0+pw, y); ctx.stroke();
  }
  ctx.setLineDash([]);

  /* Border */
  ctx.strokeStyle = dark ? 'rgba(255,255,255,.2)' : 'rgba(0,0,0,.18)';
  ctx.lineWidth=.8;
  ctx.strokeRect(dx0, dy0, pw, ph);

  /* Y labels: direction left, Bft right */
  ctx.font='9px sans-serif'; ctx.textBaseline='middle';
  ctx.fillStyle='#3498db'; ctx.textAlign='right';
  for (const d of dTicks) {
    ctx.fillText(d + '°', dx0-3, dyOf(d));
  }
  ctx.fillStyle='#e67e22'; ctx.textAlign='left';
  for (const b of bftTicks) {
    ctx.fillText(b, dx0+pw+3, byOf(b));
  }

  if (!hist.length) return;

  const n = hist.length;
  const xOf = i => dx0 + i / (Math.max(n-1, 1)) * pw;

  /* Direction line (blue) */
  ctx.strokeStyle='#3498db'; ctx.lineWidth=1.5; ctx.setLineDash([]);
  ctx.beginPath();
  monotonePath(ctx, hist.map((h, i) => ({ x: xOf(i), y: dyOf(h.dir) })));
  ctx.stroke();

  /* Bft line (orange) */
  ctx.strokeStyle='#e67e22'; ctx.lineWidth=1.5;
  ctx.beginPath();
  monotonePath(ctx, hist.map((h, i) => ({ x: xOf(i), y: byOf(h.bft) })));
  ctx.stroke();

  /* Time axis labels */
  ctx.fillStyle = dark ? 'rgba(255,255,255,.4)' : 'rgba(0,0,0,.4)';
  ctx.font='8px sans-serif'; ctx.textAlign='center';
  if (n > 1) {
    const first = new Date(hist[0].ts*1000), last = new Date(hist[n-1].ts*1000);
    ctx.fillText(timeStr(first), dx0,    dy0+ph+12);
    ctx.fillText(timeStr(last),  dx0+pw, dy0+ph+12);
  }
}

function timeStr(d) {
  return d.getHours().toString().padStart(2,'0')+':'+d.getMinutes().toString().padStart(2,'0');
}


/* ─── Zoom + scroll helpers ───────────────────────────────────────────── */
const ZOOM_LEVELS = [
  { label:'30m', hours: 0.5 },
  { label:'1h',  hours: 1   },
  { label:'3h',  hours: 3   },
  { label:'6h',  hours: 6   },
  { label:'12h', hours: 12  },
  { label:'24h', hours: 24  },
  { label:'48h', hours: 48  },
];

/* Returns the visible slice given zoom + scroll position (0=oldest, 1=newest) */
function getHistSlice(full, zoomHours, scrollFrac) {
  if (!full.length) return [];
  const tFirst  = full[0].ts;
  const tLast   = full[full.length - 1].ts;
  const zoomSec = zoomHours * 3600;
  if (zoomSec >= tLast - tFirst) return full;
  const maxStart = tLast - zoomSec;
  const startTs  = tFirst + scrollFrac * (maxStart - tFirst);
  return full.filter(h => h.ts >= startTs && h.ts <= startTs + zoomSec);
}

/* Zoom button bar — calls onZoom(hours) on click, snaps scroll to newest */
function createZoomBar(onZoom, defaultHours) {
  const bar = document.createElement('div');
  bar.style.cssText = 'display:flex;gap:4px;flex-wrap:wrap;margin-bottom:6px;';
  ZOOM_LEVELS.forEach(lv => {
    const btn = document.createElement('button');
    btn.textContent = lv.label;
    btn.style.cssText = 'padding:2px 8px;font-size:11px;border-radius:4px;cursor:pointer;' +
      'border:1px solid var(--border,rgba(255,255,255,.15));background:transparent;color:var(--text2);transition:.1s';
    if (lv.hours === defaultHours) {
      btn.style.background  = 'var(--accent,#3498db)';
      btn.style.color       = '#fff';
      btn.style.borderColor = 'var(--accent,#3498db)';
    }
    btn.addEventListener('click', () => {
      bar.querySelectorAll('button').forEach(b => {
        b.style.background  = 'transparent';
        b.style.color       = 'var(--text2)';
        b.style.borderColor = 'var(--border,rgba(255,255,255,.15))';
      });
      btn.style.background  = 'var(--accent,#3498db)';
      btn.style.color       = '#fff';
      btn.style.borderColor = 'var(--accent,#3498db)';
      onZoom(lv.hours);
    });
    bar.appendChild(btn);
  });
  return bar;
}

/* ─── Monotone cubic path (Fritsch-Carlson) ──────────────────────────────
 * Draws a smooth curve through pts=[{x,y},...] that never overshoots.
 * Falls back to lineTo for < 3 points. */
function monotonePath(ctx, pts) {
  const n = pts.length;
  if (n === 0) return;
  ctx.moveTo(pts[0].x, pts[0].y);
  if (n < 3) {
    for (let i = 1; i < n; i++) ctx.lineTo(pts[i].x, pts[i].y);
    return;
  }
  /* Compute inter-point slopes */
  const h = [], d = [];
  for (let i = 0; i < n - 1; i++) {
    h[i] = pts[i+1].x - pts[i].x || 1e-10;
    d[i] = (pts[i+1].y - pts[i].y) / h[i];
  }
  /* Initial tangents: average of neighbours */
  const m = new Array(n);
  m[0]   = d[0];
  m[n-1] = d[n-2];
  for (let i = 1; i < n - 1; i++) m[i] = (d[i-1] + d[i]) / 2;
  /* Fritsch-Carlson monotonicity fix */
  for (let i = 0; i < n - 1; i++) {
    if (Math.abs(d[i]) < 1e-10) { m[i] = m[i+1] = 0; continue; }
    const a = m[i] / d[i], b = m[i+1] / d[i];
    const s = a*a + b*b;
    if (s > 9) {
      const t = 3 / Math.sqrt(s);
      m[i]   = t * a * d[i];
      m[i+1] = t * b * d[i];
    }
  }
  /* Draw bezier segments */
  for (let i = 0; i < n - 1; i++) {
    ctx.bezierCurveTo(
      pts[i].x   + h[i]/3,       pts[i].y   + m[i]   * h[i]/3,
      pts[i+1].x - h[i]/3,       pts[i+1].y - m[i+1] * h[i]/3,
      pts[i+1].x, pts[i+1].y
    );
  }
}

/* Attach zoom + scroll behaviour to a chart popup.
 * canvas   — the <canvas> element
 * scrollEl — the <input type="range"> element (shown/hidden automatically)
 * fullHist — complete dataset
 * drawFn   — drawFn(canvas, slice)
 * Returns an object with redraw() */
function attachZoomScroll(canvas, scrollEl, fullHist, drawFn) {
  let zoomHours = 48;
  let scrollFrac = 1.0;  /* 1 = newest (rightmost) */

  function redraw() {
    const slice = getHistSlice(fullHist, zoomHours, scrollFrac);
    drawFn(canvas, slice);

    /* Show scrollbar only when data is wider than the zoom window */
    const tFirst = fullHist[0]?.ts || 0;
    const tLast  = fullHist[fullHist.length - 1]?.ts || 0;
    const needScroll = fullHist.length > 1 && zoomHours * 3600 < (tLast - tFirst);
    scrollEl.parentElement.style.display = needScroll ? '' : 'none';
    /* Sync scrollbar thumb to current position */
    scrollEl.value = Math.round(scrollFrac * 1000);
  }

  scrollEl.addEventListener('input', () => {
    scrollFrac = scrollEl.value / 1000;
    redraw();
  });

  return {
    setZoom(hours) { zoomHours = hours; scrollFrac = 1.0; redraw(); },
    redraw
  };
}

/* ─── Wind history chart (48 h) — direction + Bft dual axis ─────────── */
async function showWindHistChart() {
  const r = await fetch('/api/hist-meteo', { headers: authHeader() });
  if (!r.ok) { showToast('Could not load history', true); return; }
  const hist = await r.json();

  const overlay = document.createElement('div');
  overlay.className = 'dlg-overlay';
  overlay.style.zIndex = '200';
  overlay.innerHTML = `
    <div class="dlg" style="width:min(96vw,1400px);max-width:min(96vw,1400px);max-height:90vh;overflow-y:auto">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:6px">
        <h3 style="margin:0;font-size:15px">Wind history</h3>
        <button class="btn-icon" id="whchart-close">${ICO_CLOSE}</button>
      </div>
      <div id="whchart-zoombar"></div>
      <div class="graph-wrap"><canvas id="whchart-canvas" height="130"></canvas></div>
      <div style="padding:2px 0;display:none" id="whchart-scroll-wrap">
        <input type="range" id="whchart-scroll" min="0" max="1000" value="1000"
          style="width:100%;height:14px;cursor:pointer;accent-color:var(--accent,#3498db)">
      </div>
      <div style="display:flex;gap:12px;flex-wrap:wrap;font-size:11px;color:var(--text2);margin-top:4px;align-items:center">
        <span style="display:flex;align-items:center;gap:4px;color:#3498db"><svg width="18" height="4" viewBox="0 0 18 4"><line x1="0" y1="2" x2="18" y2="2" stroke="#3498db" stroke-width="2.5"/></svg> Direction (°)</span>
        <span style="display:flex;align-items:center;gap:4px;color:#e67e22"><svg width="18" height="4" viewBox="0 0 18 4"><line x1="0" y1="2" x2="18" y2="2" stroke="#e67e22" stroke-width="2.5"/></svg> Force (Bft)</span>
      </div>
    </div>`;
  document.body.appendChild(overlay);
  overlay.querySelector('#whchart-close').addEventListener('click', () => overlay.remove());
  overlay.addEventListener('click', e => { if (e.target === overlay) overlay.remove(); });

  const canvas    = overlay.querySelector('#whchart-canvas');
  const scrollEl  = overlay.querySelector('#whchart-scroll');
  const ctrl      = attachZoomScroll(canvas, scrollEl, hist,
                      (cvs, slice) => requestAnimationFrame(() => drawWindHistChart(cvs, slice)));

  const zoomBar = createZoomBar(h => ctrl.setZoom(h), 48);
  overlay.querySelector('#whchart-zoombar').appendChild(zoomBar);
  requestAnimationFrame(() => ctrl.redraw());
}

function drawWindHistChart(canvas, hist) {
  const dpr = window.devicePixelRatio || 1;
  const lw  = canvas.clientWidth  || 560;
  const lh  = canvas.clientHeight || 180;
  canvas.width  = Math.round(lw * dpr);
  canvas.height = Math.round(lh * dpr);

  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, lw, lh);

  const dark = !isLight();
  const ml=34, mr=32, mt=10, mb=28;
  const pw = lw-ml-mr, ph = lh-mt-mb;
  const dx0=ml, dy0=mt;

  /* Direction: fixed 0-360 left axis */
  const dTicks = [0, 90, 180, 270, 360];
  const dyOf   = d => dy0 + ph * (1 - d / 360);

  /* Bft: dynamic right axis */
  const maxBftData = hist.length ? Math.max(...hist.map(h => h.bft || 0)) : 0;
  const bftMax = Math.max(maxBftData, 3);
  const byOf   = b => dy0 + ph * (1 - b / bftMax);
  function bftNiceTicks(max) {
    const step = max <= 4 ? 1 : max <= 8 ? 2 : 3;
    const ticks = [];
    for (let v = 0; v <= max; v += step) ticks.push(v);
    if (ticks[ticks.length-1] < max) ticks.push(max);
    return ticks;
  }
  const bftTicks = bftNiceTicks(bftMax);

  /* Grid (direction ticks) */
  ctx.setLineDash([3,3]); ctx.lineWidth=.6;
  ctx.strokeStyle = dark ? 'rgba(255,255,255,.12)' : 'rgba(0,0,0,.1)';
  for (const d of dTicks) {
    const y = dyOf(d);
    ctx.beginPath(); ctx.moveTo(dx0, y); ctx.lineTo(dx0+pw, y); ctx.stroke();
  }
  ctx.setLineDash([]);

  /* Border */
  ctx.strokeStyle = dark ? 'rgba(255,255,255,.2)' : 'rgba(0,0,0,.18)';
  ctx.lineWidth=.8; ctx.strokeRect(dx0, dy0, pw, ph);

  /* Y labels: direction left, Bft right */
  ctx.font='9px sans-serif'; ctx.textBaseline='middle';
  ctx.fillStyle='#3498db'; ctx.textAlign='right';
  for (const d of dTicks) ctx.fillText(d+'°', dx0-3, dyOf(d));
  ctx.fillStyle='#e67e22'; ctx.textAlign='left';
  for (const b of bftTicks) ctx.fillText(b, dx0+pw+3, byOf(b));

  if (!hist.length) {
    ctx.fillStyle = dark ? 'rgba(255,255,255,.3)' : 'rgba(0,0,0,.3)';
    ctx.textAlign='center'; ctx.textBaseline='middle'; ctx.font='12px sans-serif';
    ctx.fillText('No data yet', lw/2, lh/2); return;
  }

  const n = hist.length;
  const tMin = hist[0].ts, tMax = hist[n-1].ts;
  const tRange = Math.max(tMax - tMin, 1);
  const xOf = t => dx0 + (t - tMin) / tRange * pw;

  /* Direction line */
  ctx.strokeStyle='#3498db'; ctx.lineWidth=1.8; ctx.setLineDash([]);
  ctx.beginPath();
  monotonePath(ctx, hist.map(h => ({ x: xOf(h.ts), y: dyOf(h.dir) })));
  ctx.stroke();

  /* Bft line */
  ctx.strokeStyle='#e67e22'; ctx.lineWidth=1.8;
  ctx.beginPath();
  monotonePath(ctx, hist.map(h => ({ x: xOf(h.ts), y: byOf(h.bft||0) })));
  ctx.stroke();

  /* X time labels */
  ctx.fillStyle = dark ? 'rgba(255,255,255,.4)' : 'rgba(0,0,0,.4)';
  ctx.font='8px sans-serif'; ctx.textAlign='center'; ctx.textBaseline='top';
  const nLbl = Math.min(6, n);
  for (let i = 0; i < nLbl; i++) {
    const idx = Math.round(i * (n-1) / Math.max(nLbl-1, 1));
    const h   = hist[idx];
    const d   = new Date(h.ts * 1000);
    ctx.fillText(`${d.getDate()}/${d.getMonth()+1} ${timeStr(d)}`, xOf(h.ts), dy0+ph+4);
  }
}

/* ─── Meteo history chart (48 h) ─────────────────────────────────────── */
const HIST_CHART_SPECS = {
  temp:    { title:'Temperature — 48h',  fields:['temp','feels'],   labels:['Temperature','Feels like'],  colors:['#e74c3c','#e67e22'], unit:'°C'   },
  hum:     { title:'Humidity — 48h',     fields:['hum'],            labels:['Humidity'],                  colors:['#3498db'],           unit:'%'    },
  baro:    { title:'Barometer — 48h',    fields:['baro'],           labels:['Pressure'],                  colors:['#9b59b6'],           unit:' hPa' },
  precip:  { title:'Precipitation — 48h',fields:['precip'],         labels:['Precipitation'],             colors:['#2980b9'],           unit:' mm/h'},
  uvsolar: { title:'UV / Solar — 48h',   fields:['uv','solar'],     labels:['UV index','Solar W/m²'],     colors:['#f1c40f','#e67e22'], unit:''     },
};

async function showMeteoHistChart(key) {
  const spec = HIST_CHART_SPECS[key];
  if (!spec) return;

  const r = await fetch('/api/hist-meteo', { headers: authHeader() });
  if (!r.ok) { showToast('Could not load history', true); return; }
  const hist = await r.json();

  const overlay = document.createElement('div');
  overlay.className = 'dlg-overlay';
  overlay.style.zIndex = '200';

  const legend = spec.fields.map((f, i) => `
    <span style="display:flex;align-items:center;gap:4px;color:${spec.colors[i]}">
      <svg width="18" height="4" viewBox="0 0 18 4"><line x1="0" y1="2" x2="18" y2="2" stroke="${spec.colors[i]}" stroke-width="2.5"/></svg>
      ${spec.labels[i]}
    </span>`).join('');

  overlay.innerHTML = `
    <div class="dlg" style="width:min(96vw,1400px);max-width:min(96vw,1400px);max-height:90vh;overflow-y:auto">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:6px">
        <h3 style="margin:0;font-size:15px">${spec.title.replace(' — 48h','')}</h3>
        <button class="btn-icon" id="hchart-close">${ICO_CLOSE}</button>
      </div>
      <div id="hchart-zoombar"></div>
      <div class="graph-wrap"><canvas id="hchart-canvas" height="130"></canvas></div>
      <div style="padding:2px 0;display:none" id="hchart-scroll-wrap">
        <input type="range" id="hchart-scroll" min="0" max="1000" value="1000"
          style="width:100%;height:14px;cursor:pointer;accent-color:var(--accent,#3498db)">
      </div>
      <div style="display:flex;gap:12px;flex-wrap:wrap;font-size:11px;color:var(--text2);margin-top:4px;align-items:center">
        ${legend}
      </div>
    </div>`;
  document.body.appendChild(overlay);

  overlay.querySelector('#hchart-close').addEventListener('click', () => overlay.remove());
  overlay.addEventListener('click', e => { if (e.target === overlay) overlay.remove(); });

  const canvas   = overlay.querySelector('#hchart-canvas');
  const scrollEl = overlay.querySelector('#hchart-scroll');
  const ctrl     = attachZoomScroll(canvas, scrollEl, hist,
                     (cvs, slice) => requestAnimationFrame(() => drawMeteoHistChart(cvs, slice, spec)));

  const zoomBar = createZoomBar(h => ctrl.setZoom(h), 48);
  overlay.querySelector('#hchart-zoombar').appendChild(zoomBar);
  requestAnimationFrame(() => ctrl.redraw());
}

function drawMeteoHistChart(canvas, hist, spec) {
  const dpr = window.devicePixelRatio || 1;
  const lw  = canvas.clientWidth  || 560;
  const lh  = canvas.clientHeight || 180;
  canvas.width  = Math.round(lw * dpr);
  canvas.height = Math.round(lh * dpr);

  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, lw, lh);

  const dark = !isLight();
  const ml=42, mr=16, mt=10, mb=28;
  const pw = lw-ml-mr, ph = lh-mt-mb;
  const dx0=ml, dy0=mt;

  /* Filter to entries that have at least one requested field */
  const data = hist.filter(h => spec.fields.some(f => h[f] !== undefined && h[f] !== null));

  if (data.length === 0) {
    ctx.fillStyle = dark ? 'rgba(255,255,255,.3)' : 'rgba(0,0,0,.3)';
    ctx.textAlign='center'; ctx.textBaseline='middle'; ctx.font='12px sans-serif';
    ctx.fillText('No data yet — history builds up over time', lw/2, lh/2);
    return;
  }

  /* Y range */
  let yMin = Infinity, yMax = -Infinity;
  for (const h of data) {
    for (const f of spec.fields) {
      if (h[f] !== undefined && h[f] !== null) {
        yMin = Math.min(yMin, h[f]); yMax = Math.max(yMax, h[f]);
      }
    }
  }
  if (yMin === yMax) { yMin -= 1; yMax += 1; }
  const yPad = (yMax - yMin) * 0.08 || 1;
  yMin -= yPad; yMax += yPad;

  /* X range */
  const tMin = data[0].ts, tMax = data[data.length-1].ts;
  const tRange = Math.max(tMax - tMin, 1);
  const xOf = t => dx0 + (t - tMin) / tRange * pw;
  const yOf = v => dy0 + ph * (1 - (v - yMin) / (yMax - yMin));

  /* Nice Y ticks */
  function niceTicks(min, max, n) {
    const range = max - min, rough = range / n;
    const mag   = Math.pow(10, Math.floor(Math.log10(Math.abs(rough) || 1)));
    const step  = [1,2,2.5,5,10].map(s => s*mag).find(s => s >= rough) || rough;
    const ticks = [];
    for (let v = Math.ceil(min/step)*step; v <= max + step*0.01; v += step)
      ticks.push(parseFloat(v.toFixed(10)));
    return ticks;
  }
  const yTicks = niceTicks(yMin, yMax, 4);

  /* Grid */
  ctx.setLineDash([3,3]); ctx.lineWidth=.6;
  ctx.strokeStyle = dark ? 'rgba(255,255,255,.12)' : 'rgba(0,0,0,.1)';
  for (const v of yTicks) {
    const y = yOf(v);
    if (y < dy0-2 || y > dy0+ph+2) continue;
    ctx.beginPath(); ctx.moveTo(dx0, y); ctx.lineTo(dx0+pw, y); ctx.stroke();
  }
  ctx.setLineDash([]);

  /* Border */
  ctx.strokeStyle = dark ? 'rgba(255,255,255,.2)' : 'rgba(0,0,0,.18)';
  ctx.lineWidth=.8; ctx.strokeRect(dx0, dy0, pw, ph);

  /* Y labels */
  ctx.font='9px sans-serif'; ctx.textBaseline='middle'; ctx.textAlign='right';
  ctx.fillStyle = spec.colors[0];
  for (const v of yTicks) {
    const y = yOf(v);
    if (y < dy0-2 || y > dy0+ph+2) continue;
    const label = spec.unit ? v + spec.unit : String(v);
    ctx.fillText(label, dx0-3, y);
  }

  /* Data lines — split on gaps (null/undefined), monotone smooth */
  for (let fi = 0; fi < spec.fields.length; fi++) {
    const f = spec.fields[fi];
    ctx.strokeStyle = spec.colors[fi]; ctx.lineWidth = 1.8; ctx.setLineDash([]);
    ctx.beginPath();
    let seg = [];
    for (const h of data) {
      if (h[f] === undefined || h[f] === null) {
        if (seg.length) { monotonePath(ctx, seg); seg = []; }
        continue;
      }
      seg.push({ x: xOf(h.ts), y: yOf(h[f]) });
    }
    if (seg.length) monotonePath(ctx, seg);
    ctx.stroke();
  }

  /* X time labels — show up to 6 evenly spaced */
  ctx.fillStyle = dark ? 'rgba(255,255,255,.4)' : 'rgba(0,0,0,.4)';
  ctx.font='8px sans-serif'; ctx.textAlign='center'; ctx.textBaseline='top';
  const nLbl = Math.min(6, data.length);
  for (let i = 0; i < nLbl; i++) {
    const idx = Math.round(i * (data.length-1) / Math.max(nLbl-1,1));
    const h   = data[idx];
    const d   = new Date(h.ts * 1000);
    const lbl = `${d.getDate()}/${d.getMonth()+1} ${timeStr(d)}`;
    ctx.fillText(lbl, xOf(h.ts), dy0+ph+4);
  }
}

/* ─── Utility ────────────────────────────────────────────────────────── */
function escHtml(s) {
  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

/* ═══════════════════════════════════════════════════════════════════════
 * Configuration UI
 * ═══════════════════════════════════════════════════════════════════════ */

let cfgCache = null;   /* last loaded config from /api/config */
let devCache  = null;  /* last loaded device list */

async function loadConfig() {
  const r = await fetch('/api/config', { headers: authHeader() });
  if (!r.ok) return null;
  cfgCache = await r.json();
  return cfgCache;
}

async function loadDevices() {
  const r = await fetch('/api/devices', {
    headers: authToken ? {'Authorization':'Bearer '+authToken} : {}
  });
  if (!r.ok) return [];
  devCache = await r.json();
  return devCache;
}

async function saveConfig(cfg, restartServer) {
  const body = Object.assign({}, cfg, { restart_server: restartServer ? 1 : 0 });
  const r = await fetch('/api/config', {
    method: 'POST',
    headers: {'Content-Type':'application/json', ...authHeader()},
    body: JSON.stringify(body)
  });
  return r;
}

/* ─── Rotor card gear + delete buttons ───────────────────────────────── */
function renderRotors() {
  if (!state.rotors.length) {
    noRotors.style.display=''; rotorGrid.style.display='none'; return;
  }
  noRotors.style.display='none'; rotorGrid.style.display='';

  /* Multi Control card: show when >1 rotor, always first */
  const wantMulti = state.rotors.length > 1;
  let multiCard = document.getElementById('rcard-multi');
  if (wantMulti && !multiCard) {
    multiCard = buildMultiCard();
    rotorGrid.insertBefore(multiCard, rotorGrid.firstChild);
  } else if (!wantMulti && multiCard) {
    multiCard.remove();
  }
  if (multiCard) updateMultiCanvas();

  state.rotors.forEach(r => {
    let card = document.getElementById('rcard-'+CSS.escape(r.name));
    if (!card) {
      card = document.createElement('div');
      card.className = 'card';
      card.id = 'rcard-'+CSS.escape(r.name);
      card.innerHTML = `
        <div class="card-title">
          <span class="rcard-name">${escHtml(r.name)}</span>
          <div class="card-actions">
            <span class="badge moving-badge" style="display:none"><span class="rotor-moving"></span></span>
            <span class="badge sim-badge" style="display:none;background:var(--orange);color:#fff">SIM</span>
            <span class="lock-icon" title="Locked by storm correction" style="display:none">${ICO_LOCK}</span>
            <button title="Edit rotor" data-rotor="${escHtml(r.name)}">${ICO_GEAR}</button>
            <button class="del" title="Delete rotor" data-del="${escHtml(r.name)}">${ICO_TRASH}</button>
          </div>
        </div>
        <div class="compass-wrap"><canvas class="compass" width="${CW}" height="${CW}"></canvas></div>
        <div class="rcard-status"></div>
        <button class="btn-stop" title="Stop rotor">${ICO_STOP} Stop</button>`;
      const cvs = card.querySelector('canvas');
      cvs.addEventListener('click',    e => onCompassClick(e,    cvs, r.name));
      cvs.addEventListener('dblclick', e => onCompassDblClick(e, cvs, r.name));
      card.querySelector('[data-rotor]').addEventListener('click', () => requireAuth(() => showRotorDialog(r.name)));
      card.querySelector('[data-del]').addEventListener('click', () => requireAuth(() => confirmDeleteRotor(r.name)));
      card.querySelector('.btn-stop').addEventListener('click', () => requireControlAuth(() => sendStop(r.name)));
      rotorGrid.appendChild(card);
    }
    const cvs      = card.querySelector('canvas');
    const badge    = card.querySelector('.moving-badge');
    const simBadge = card.querySelector('.sim-badge');
    const lockIcon = card.querySelector('.lock-icon');
    const stopBtn  = card.querySelector('.btn-stop');
    badge.style.display = r.moving ? '' : 'none';
    if (stopBtn) stopBtn.style.display = r.has_data ? '' : 'none';
    /* SIM badge + lock icon from config cache */
    const cfgR = cfgCache ? (cfgCache.rotors||[]).find(cr => cr.name === r.name) : null;
    if (simBadge) simBadge.style.display = (cfgR && cfgR.simulate) ? '' : 'none';
    if (lockIcon) {
      const correcting = state.wind && state.wind.correcting;
      const blocked = correcting && cfgR && cfgR.storm_enabled && !cfgR.always_controllable
                      && cfgCache && cfgCache.storm_block_manual;
      lockIcon.style.display = blocked ? '' : 'none';
    }
    const offset = cfgR ? (cfgR.offset || 0) : 0;
    const statusEl = card.querySelector('.rcard-status');
    if (statusEl) {
      if (!r.has_data) {
        statusEl.innerHTML = ICO_WARN + ' No connection';
        statusEl.className = 'rcard-status rcard-status-nodata';
      } else if (r.moving) {
        const tmMin = cfgR ? (cfgR.return_timeout_min || 0) : 0;
        const tmLg  = r.last_goto || 0;
        const tmCorr = !!(state.wind && state.wind.correcting);
        if (tmCorr && cfgR && cfgR.storm_enabled && cfgR.always_controllable && tmMin > 0 && tmLg > 0) {
          const remSec = tmMin * 60 - (Math.floor(Date.now() / 1000) - tmLg);
          if (remSec > 0) {
            const mm = Math.floor(remSec / 60), ss = remSec % 60;
            statusEl.textContent = `Moving… ↩ ${mm}:${String(ss).padStart(2,'0')}`;
            statusEl.className = 'rcard-status rcard-status-moving';
          } else {
            statusEl.textContent = 'Moving…';
            statusEl.className = 'rcard-status rcard-status-moving';
          }
        } else {
          statusEl.textContent = 'Moving…';
          statusEl.className = 'rcard-status rcard-status-moving';
        }
      } else {
        /* Return-to-storm countdown timer:
         * show when storm is correcting + rotor storm_enabled + always_controllable + timeout set + goto done */
        const timeoutMin   = cfgR ? (cfgR.return_timeout_min || 0) : 0;
        const lastGoto     = r.last_goto || 0;   /* unix seconds from SSE */
        const correcting   = !!(state.wind && state.wind.correcting);
        const stormEnabled = !!(cfgR && cfgR.storm_enabled);
        const alwaysCtrl   = !!(cfgR && cfgR.always_controllable);
        if (correcting && stormEnabled && alwaysCtrl && timeoutMin > 0 && lastGoto > 0) {
          const nowSec  = Math.floor(Date.now() / 1000);
          const elapsed = nowSec - lastGoto;
          const remSec  = timeoutMin * 60 - elapsed;
          if (remSec > 0) {
            const mm = Math.floor(remSec / 60);
            const ss = remSec % 60;
            statusEl.textContent = `↩ return in ${mm}:${String(ss).padStart(2,'0')}`;
            statusEl.className = 'rcard-status rcard-status-return';
          } else {
            statusEl.textContent = '';
            statusEl.className = 'rcard-status';
          }
        } else {
          statusEl.textContent = '';
          statusEl.className = 'rcard-status';
        }
      }
    }
    cvs.classList.toggle('no-data', !r.has_data);
    drawCompass(cvs, r.az, r.target, r.has_data, false, offset);
  });
  const names = state.rotors.map(r => 'rcard-'+CSS.escape(r.name));
  names.push('rcard-multi');   /* never remove the multi card here */
  rotorGrid.querySelectorAll('.card').forEach(c => { if (!names.includes(c.id)) c.remove(); });
}

/* ─── Settings modal ──────────────────────────────────────────────────── */
/* Gate: show login first if password is set and we're not yet authenticated */
function requireAuth(callback) {
  if (!authRequired || authToken) { callback(); return; }
  showToast(viewToken ? 'Login as Administrator' : 'Please login first', true);
}

/* Gate: require control (or admin) login to send GOTO/STOP.
 * Any configured password blocks unauthenticated rotor control. */
function requireControlAuth(callback) {
  if (viewToken || authToken) { callback(); return; }           /* already logged in */
  if (controlRequired || authRequired) {                        /* any password set  */
    showToast('Please login first', true);
    return;
  }
  callback();   /* no passwords configured — free access */
}

document.getElementById('btn-settings').addEventListener('click', () => requireAuth(() => showSettings()));

async function showSettings() {
  const cfg = await loadConfig();
  if (!cfg) { alert('Could not load config. Is the server running?'); return; }

  const needAuth = false; /* auth already done before opening */

  /* Build modal */
  const overlay = document.createElement('div');
  overlay.className = 'dlg-overlay';
  overlay.innerHTML = `
    <div class="sdlg">
      <div class="sdlg-header">
        <h2>${ICO_GEAR} Settings</h2>
        <button class="btn-icon" id="sdlg-close">${ICO_CLOSE}</button>
      </div>
      <div class="sdlg-tabs">
        <button class="sdlg-tab active" data-panel="rotors">Rotors</button>
        <button class="sdlg-tab" data-panel="server">Server</button>
        <button class="sdlg-tab" data-panel="wind">Meteo</button>
        <button class="sdlg-tab" data-panel="storm">Storm</button>
        <button class="sdlg-tab" data-panel="web">Web</button>
      </div>
      <div class="sdlg-body">
        <div class="sdlg-panel active" id="spanel-rotors"></div>
        <div class="sdlg-panel" id="spanel-server"></div>
        <div class="sdlg-panel" id="spanel-wind"></div>
        <div class="sdlg-panel" id="spanel-storm"></div>
        <div class="sdlg-panel" id="spanel-web"></div>
      </div>
      <div class="sdlg-footer">
        <button class="btn btn-cancel" id="sdlg-cancel">Close</button>
        <button class="btn" id="sdlg-save">Save &amp; restart server</button>
      </div>
    </div>`;
  document.body.appendChild(overlay);

  /* Tab switching */
  overlay.querySelectorAll('.sdlg-tab').forEach(tab => {
    tab.addEventListener('click', () => {
      overlay.querySelectorAll('.sdlg-tab').forEach(t => t.classList.remove('active'));
      overlay.querySelectorAll('.sdlg-panel').forEach(p => p.classList.remove('active'));
      tab.classList.add('active');
      document.getElementById('spanel-'+tab.dataset.panel).classList.add('active');
    });
  });

  /* Populate panels */
  buildRotorsPanel(document.getElementById('spanel-rotors'), cfg);
  buildServerPanel(document.getElementById('spanel-server'), cfg);
  buildWindPanel(document.getElementById('spanel-wind'), cfg);
  buildStormPanel(document.getElementById('spanel-storm'), cfg);
  buildWebPanel(document.getElementById('spanel-web'), cfg);

  /* Close */
  const close = () => overlay.remove();
  overlay.querySelector('#sdlg-close').addEventListener('click', close);
  overlay.querySelector('#sdlg-cancel').addEventListener('click', close);
  /* Save */
  overlay.querySelector('#sdlg-save').addEventListener('click', async () => {
    const newCfg = collectSettings(overlay, cfg);
    const webPortChanged = (newCfg.web_port !== cfg.web_port);
    const r = await saveConfig(newCfg, true);
    if (r.ok) {
      cfgCache = newCfg;
      close();
      if (webPortChanged) {
        showPowerConfirm('Reboot required',
          `Web port changed to ${newCfg.web_port}. A reboot is required for this to take effect. Reboot now?`,
          async () => {
            await fetch('/api/reboot', { method: 'POST', headers: authHeader() });
            showToast('Rebooting…');
          });
      } else {
        showToast('Config saved. Server restarting…');
        fetchStatus();
      }
    } else if (r.status === 401) {
      showToast('Please login first', true);
    } else {
      showToast('Error saving config: ' + r.status, true);
    }
  });
}

/* ─── Rotors panel ────────────────────────────────────────────────────── */
function buildRotorsPanel(el, cfg) {
  const rotors = cfg.rotors || [];
  let html = '';
  if (rotors.length === 0) {
    html = '<div class="empty-list">No rotors configured yet.</div>';
  } else {
    html = '<div class="rotor-list">';
    rotors.forEach((r, i) => {
      html += `<div class="rotor-list-item">
        <div><div class="rli-name">${escHtml(r.name)}</div>
          <div class="rli-sub">${escHtml(r.protocol||'YAESU')} · ${r.baud||9600} baud · offset ${r.offset||0}°${r.simulate ? ' · <span style="color:var(--orange)">simulate</span>' : ''}</div>
          <div class="rli-sub" style="font-size:10px;opacity:.6">${escHtml(r.by_path||'—')}${r.by_path ? '  ('+escHtml(r.name)+')' : ''}</div>
        </div>
        <div style="margin-left:auto;display:flex;gap:6px">
          <button class="btn" style="padding:4px 10px;font-size:12px" data-edit-rotor="${i}">Edit</button>
          <button class="btn btn-cancel" style="padding:4px 10px;font-size:12px;color:var(--red)" data-del-rotor="${i}">Delete</button>
        </div>
      </div>`;
    });
    html += '</div>';
  }
  html += '<div class="rotor-list-add"><button class="btn" id="btn-add-rotor">+ Add rotor</button></div>';
  el.innerHTML = html;

  el.querySelectorAll('[data-edit-rotor]').forEach(btn => {
    btn.addEventListener('click', () => {
      const idx = parseInt(btn.dataset.editRotor);
      showRotorEditDialog(cfg.rotors[idx], idx, cfg, (updated, i) => {
        if (i < 0) cfg.rotors.push(updated);
        else cfg.rotors[i] = updated;
        buildRotorsPanel(el, cfg);
      });
    });
  });
  el.querySelectorAll('[data-del-rotor]').forEach(btn => {
    btn.addEventListener('click', () => {
      const idx = parseInt(btn.dataset.delRotor);
      const name = cfg.rotors[idx].name;
      if (confirm(`Delete rotor "${name}"?`)) {
        cfg.rotors.splice(idx, 1);
        cfg.num_rotors = cfg.rotors.length;
        buildRotorsPanel(el, cfg);
      }
    });
  });
  el.querySelector('#btn-add-rotor').addEventListener('click', () => {
    showRotorEditDialog(null, -1, cfg, (updated) => {
      cfg.rotors.push(updated);
      cfg.num_rotors = cfg.rotors.length;
      buildRotorsPanel(el, cfg);
    });
  });
}

/* ─── Rotor edit dialog ───────────────────────────────────────────────── */
async function showRotorEditDialog(rotor, idx, cfg, onSave) {
  const devs = await loadDevices();
  const r = rotor || { name:'', by_path:'', protocol:'YAESU', baud:9600, offset:0,
                       storm_enabled:0, storm_offset:0, always_controllable:0, simulate:0,
                       return_timeout_min:0 };

  /* Build device options; mark devices already used by another rotor with [name] */
  let devList = [...devs];
  /* Add stored by_path if not in live list (e.g. device temporarily disconnected) */
  if (r.by_path && !devList.includes(r.by_path)) devList.unshift(r.by_path);
  /* Normalize usb/usbv2 variants so config-stored paths match deduped API names */
  const normDev = s => s.replace(/-usbv2-/g, '-usb-');
  const devOptions = devList.map(d => {
    const existing = (cfg.rotors||[]).find(cr => normDev(cr.by_path) === normDev(d));
    const label = existing ? `${d}  [${existing.name}]` : d;
    return `<option value="${escHtml(d)}" ${d===r.by_path?'selected':''}>${escHtml(label)}</option>`;
  }).join('');

  const overlay = document.createElement('div');
  overlay.className = 'dlg-overlay';
  overlay.style.zIndex = '300';
  overlay.innerHTML = `
    <div class="dlg" style="width:min(520px,92vw)">
      <h3>${idx<0?'Add rotor':'Edit rotor'}</h3>
      <div class="re-form">
        <div class="re-field">
          <label>Name</label>
          <input id="re-name" type="text" value="${escHtml(r.name)}" placeholder="e.g. 2m yagi" maxlength="63">
        </div>
        <div class="re-section">Mode</div>
        <div class="re-field re-check">
          <label><input id="re-simulate" type="checkbox" ${r.simulate ? 'checked' : ''}> Simulate</label>
        </div>
        <div id="re-serial-fields" style="${r.simulate ? 'display:none' : ''}">
          <div class="re-field">
            <label>Device</label>
            <select id="re-device">
              <option value="">— select device —</option>
              ${devOptions}
            </select>
          </div>
          <div class="re-field">
            <label>Protocol</label>
            <select id="re-proto">
              <option value="YAESU"     ${r.protocol==='YAESU'?'selected':''}>YAESU (GS-232A/B)</option>
              <option value="PROSISTEL" ${r.protocol==='PROSISTEL'?'selected':''}>PROSISTEL (CBOX)</option>
            </select>
          </div>
          <div class="re-field">
            <label>Baud rate</label>
            <select id="re-baud">
              <option value="4800"  ${r.baud==4800  ? 'selected' : ''}>4800</option>
              <option value="9600"  ${r.baud==9600  ? 'selected' : ''}>9600</option>
              <option value="19200" ${r.baud==19200 ? 'selected' : ''}>19200</option>
              <option value="38400" ${r.baud==38400 ? 'selected' : ''}>38400</option>
            </select>
          </div>
        </div>
        <div class="re-field">
          <label>Heading offset</label>
          <input id="re-offset" type="number" min="-180" max="180" value="${r.offset||0}">
        </div>
        <div class="re-section">Storm monitor</div>
        <div class="re-row">
          <div class="re-field re-check">
            <label><input id="re-storm-en" type="checkbox" ${r.storm_enabled?'checked':''}> Include in storm</label>
          </div>
          <div class="re-field re-check">
            <label><input id="re-always" type="checkbox" ${r.always_controllable?'checked':''}> Always controllable</label>
          </div>
        </div>
        <div class="re-row">
          <div class="re-field">
            <label>Storm offset (°)</label>
            <input id="re-storm-off" type="number" min="-180" max="180" value="${r.storm_offset||0}">
          </div>
          <div class="re-field">
            <label>Return timeout</label>
            <select id="re-return-timeout">
              <option value="0"   ${(r.return_timeout_min||0)===0  ?'selected':''}>Off</option>
              <option value="1"   ${r.return_timeout_min===1  ?'selected':''}>1 min</option>
              <option value="5"   ${r.return_timeout_min===5  ?'selected':''}>5 min</option>
              <option value="10"  ${r.return_timeout_min===10 ?'selected':''}>10 min</option>
              <option value="15"  ${r.return_timeout_min===15 ?'selected':''}>15 min</option>
              <option value="30"  ${r.return_timeout_min===30 ?'selected':''}>30 min</option>
              <option value="60"  ${r.return_timeout_min===60 ?'selected':''}>60 min</option>
              <option value="120" ${r.return_timeout_min===120?'selected':''}>120 min</option>
            </select>
          </div>
        </div>
        <div class="re-hint">Return to storm position after this idle time (requires Always controllable + active storm)</div>
      </div>
      <div class="dlg-btns" style="margin-top:16px">
        <button class="btn" id="re-save">${idx<0?'Add':'Save'}</button>
        <button class="btn btn-cancel" id="re-cancel">Cancel</button>
      </div>
    </div>`;
  document.body.appendChild(overlay);

  const sel = overlay.querySelector('#re-device');

  const simChk     = overlay.querySelector('#re-simulate');
  const serialFlds = overlay.querySelector('#re-serial-fields');
  if (simChk) simChk.addEventListener('change', () => {
    serialFlds.style.display = simChk.checked ? 'none' : '';
  });

  overlay.querySelector('#re-cancel').addEventListener('click', () => overlay.remove());
  overlay.querySelector('#re-save').addEventListener('click', () => {
    const simulate = overlay.querySelector('#re-simulate').checked ? 1 : 0;
    const byPath = simulate ? '' : sel.value;
    const updated = {
      name:               overlay.querySelector('#re-name').value.trim(),
      by_path:            byPath,
      protocol:           overlay.querySelector('#re-proto').value,
      baud:               parseInt(overlay.querySelector('#re-baud').value),
      offset:             parseInt(overlay.querySelector('#re-offset').value)||0,
      simulate:           simulate,
      storm_enabled:      overlay.querySelector('#re-storm-en').checked ? 1 : 0,
      storm_offset:       parseInt(overlay.querySelector('#re-storm-off').value)||0,
      always_controllable: overlay.querySelector('#re-always').checked ? 1 : 0,
      return_timeout_min:  parseInt(overlay.querySelector('#re-return-timeout').value) || 0,
      serial:             r.serial || ''
    };
    if (!updated.name) { alert('Name is required.'); return; }
    overlay.remove();
    onSave(updated, idx);
  });
}

async function showRotorDialog(name) {
  const cfg = await loadConfig();
  if (!cfg) return;
  const idx = (cfg.rotors||[]).findIndex(r => r.name === name);
  if (idx < 0) return;
  showRotorEditDialog(cfg.rotors[idx], idx, cfg, async (updated, i) => {
    cfg.rotors[i] = updated;
    cfg.num_rotors = cfg.rotors.length;
    const r = await saveConfig(cfg, true);
    if (r.ok) { showToast('Rotor saved. Server restarting…'); }
    else if (r.status===401) showToast('Please login first', true);
    else showToast('Save failed: ' + r.status, true);
  });
}

async function confirmDeleteRotor(name) {
  if (!confirm(`Delete rotor "${name}"? This cannot be undone.`)) return;
  const cfg = await loadConfig();
  if (!cfg) return;
  cfg.rotors = (cfg.rotors||[]).filter(r => r.name !== name);
  cfg.num_rotors = cfg.rotors.length;
  const r = await saveConfig(cfg, true);
  if (r.ok) { showToast('Rotor deleted. Server restarting…'); }
  else if (r.status===401) showToast('Please login first', true);
  else showToast('Delete failed: ' + r.status, true);
}

/* ─── Server panel ────────────────────────────────────────────────────── */
function buildServerPanel(el, cfg) {
  el.innerHTML = `<div class="sf-grid">
    <div class="sf-section">Network</div>
    <label>Command port</label>    <input id="sp-cmd"     type="number" value="${cfg.cmd_port||12040}">
    <label>Broadcast port 2</label><input id="sp-bcast2"  type="number" value="${cfg.bcast_port2||0}">
    <div class="sf-hint">Secondary broadcast port (0=off, 13011–13015)</div>
    <label>Broadcast address</label><input id="sp-baddr"  type="text"   value="${escHtml(cfg.bcast_addr||'255.255.255.255')}">
    <div class="sf-section">Timing</div>
    <label>Idle interval (ms)</label>  <input id="sp-idle"  type="number" value="${cfg.idle_ms||1000}">
    <label>Moving interval (ms)</label><input id="sp-mov"   type="number" value="${cfg.moving_ms||200}">
    <div class="sf-section">Log</div>
    <label>Log file</label><span class="sf-readonly">${escHtml(cfg.logfile||'—')}</span>
    <div class="sf-hint">Server bewaart automatisch de laatste 24 uur; ouder wordt elk uur opgeruimd</div>
    <label>Clear log</label>
    <button class="btn btn-cancel" id="sp-log-clear" style="justify-self:start">Clear history…</button>
    <div class="sf-hint">Removes all entries from the server log file permanently</div>
  </div>`;

  el.querySelector('#sp-log-clear').addEventListener('click', () => {
    requireAuth(() => {
      showPowerConfirm(
        'Clear server log',
        'All entries in the server log file will be permanently deleted from disk. This cannot be undone.',
        async () => {
          const r = await fetch('/api/log/clear', { method: 'POST', headers: authHeader() });
          if (r.ok) {
            logEntries = []; logFetchedSince = 0;
            if (logPanelOpen) { renderLogView(); }
            updateLogBtnDot();
            showToast('Server log cleared');
          } else if (r.status === 401) {
            showToast('Admin login required', true);
          } else {
            showToast('Failed to clear log: ' + r.status, true);
          }
        }
      );
    });
  });
}

/* ─── Wind panel ──────────────────────────────────────────────────────── */
function makeIntervalOpts(vals, cur) {
  return vals.map(function(v) {
    return '<option value="' + v + '"' + (cur == v ? ' selected' : '') + '>' + v + ' min</option>';
  }).join('');
}

function buildWindPanel(el, cfg) {
  const src      = cfg.wind_source || 'openmeteo';
  const fb       = cfg.wind_ecowitt_fallback || '';
  const intvOpts = makeIntervalOpts([5,10,15,30,60], cfg.wind_interval_min||15);

  const srcLabels = {
    openmeteo: 'Open-Meteo (internet, free, no key required)',
    yrno:      'Yr.no (internet, free, no key required)',
    owm:       'OpenWeatherMap (internet, API key required)',
    wapi:      'WeatherAPI (internet, API key required)',
    ecowitt:   'Ecowitt (local weather station, HTTP push)',
  };
  const srcOpts = Object.entries(srcLabels).map(([v,l]) =>
    `<option value="${v}"${src===v?' selected':''}>${l}</option>`).join('');

  const fbOpts = '<option value=""' + (!fb?' selected':'') + '>Off</option>'
    + ['openmeteo','yrno','owm','wapi'].map(v =>
        `<option value="${v}"${fb===v?' selected':''}>${srcLabels[v]}</option>`).join('');

  el.innerHTML = `<div class="sf-grid">
    <div class="sf-section">Wind source</div>
    <label>Enable wind monitor</label>
    <input id="wp-en" type="checkbox"${cfg.wind_enabled?' checked':''}>
    <label>Source</label><select id="wp-src">${srcOpts}</select>

    <div class="wp-group wp-geo">
      <label>Latitude</label>
      <input id="wp-lat" type="number" step="0.0001" value="${cfg.wind_lat||0}">
      <label>Longitude</label>
      <input id="wp-lon" type="number" step="0.0001" value="${cfg.wind_lon||0}">
      <label>Fetch interval</label>
      <select id="wp-intv">${intvOpts}</select>
    </div>

    <div class="wp-group wp-owm" style="display:none">
      <label>OWM API key</label>
      <input id="wp-owm" type="text" value="${escHtml(cfg.wind_owm_apikey||'')}">
    </div>

    <div class="wp-group wp-wapi" style="display:none">
      <label>WAPI API key</label>
      <input id="wp-wapi" type="text" value="${escHtml(cfg.wind_wapi_apikey||'')}">
    </div>

    <div class="wp-group wp-serial" style="display:none">
      <label>Serial device</label>
      <input id="wp-serial-dev" type="text" value="${escHtml(cfg.wind_serial_device||'')}">
      <label>Baud rate</label>
      <select id="wp-serial-baud">
        ${[1200,2400,4800,9600,19200,38400].map(b =>
          `<option value="${b}"${(cfg.wind_serial_baud||19200)==b?' selected':''}>${b}</option>`).join('')}
      </select>
    </div>

    <div class="wp-group wp-udp" style="display:none">
      <label>UDP listen port</label>
      <input id="wp-udp-port" type="number" value="${cfg.wind_udp_port||13030}">
    </div>

    <div class="wp-group wp-ecowitt" style="display:none">
      <div class="sf-section">Ecowitt</div>
      <label>Listen port</label>
      <input id="wp-eco-port" type="number" value="${cfg.wind_ecowitt_port||49199}">
      <label>PASSKEY</label>
      <input id="wp-eco-pk" type="text" value="${escHtml(cfg.wind_ecowitt_passkey||'')}">
      <div class="sf-hint">Leave empty to accept all incoming pushes</div>
      <label>Fallback source</label>
      <select id="wp-eco-fb">${fbOpts}</select>
      <label>Fallback after (min)</label>
      <input id="wp-eco-fbmin" type="number" value="${cfg.wind_ecowitt_fallback_min||5}">
      <label>Wind force correction (%)</label>
      <input id="wp-eco-corr" type="number" min="50" max="300" value="${cfg.wind_ecowitt_correction||100}">
      <div class="sf-hint">100 = no correction. Example: reads 3 Bft, actual is 5 Bft → enter 167</div>
      <div class="sf-section">Ecowitt forwarding</div>
      <label>Forward host</label>
      <input id="wp-fwd-host" type="text" value="${escHtml(cfg.wind_ecowitt_fwd_host||'')}">
      <label>Forward port</label>
      <input id="wp-fwd-port" type="number" value="${cfg.wind_ecowitt_fwd_port||0}">
      <label>Forward path</label>
      <input id="wp-fwd-path" type="text" value="${escHtml(cfg.wind_ecowitt_fwd_path||'/')}">
      <label>Forward PASSKEY</label>
      <input id="wp-fwd-pk" type="text" value="${escHtml(cfg.wind_ecowitt_fwd_passkey||'')}">
    </div>
  </div>`;

  function updateWindFields() {
    const s = el.querySelector('#wp-src').value;
    const geo     = ['openmeteo','yrno','owm','wapi'].includes(s);
    el.querySelector('.wp-geo').style.display     = geo           ? 'contents' : 'none';
    el.querySelector('.wp-owm').style.display     = s==='owm'     ? 'contents' : 'none';
    el.querySelector('.wp-wapi').style.display    = s==='wapi'    ? 'contents' : 'none';
    el.querySelector('.wp-ecowitt').style.display = s==='ecowitt' ? 'contents' : 'none';
  }

  el.querySelector('#wp-src').addEventListener('change', updateWindFields);
  updateWindFields();
}

/* ─── Storm panel ─────────────────────────────────────────────────────── */
function makeOpts(vals, cur) {
  return vals.map(function(v) {
    return '<option value="' + v + '"' + (cur == v ? ' selected' : '') + '>' + v + ' min</option>';
  }).join('');
}

function buildStormPanel(el, cfg) {
  el.innerHTML = '<div class="sf-grid">'
    + '<div class="sf-section">Thresholds</div>'
    + '<label>Wind force threshold</label>'
    + '<input id="stp-thr" type="number" min="0" max="12" value="' + (cfg.storm_threshold_bft||7) + '">'
    + '<div class="sf-hint">Beaufort (0–12)</div>'
    + '<label>Sustain before on</label>'
    + '<select id="stp-sus">' + makeOpts([1,2,5,10,15,30,60], cfg.storm_sustain_min||5)  + '</select>'
    + '<label>Sustain before off</label>'
    + '<select id="stp-rel">' + makeOpts([1,2,5,10,15,30,60], cfg.storm_release_min||10) + '</select>'
    + '<label>Correction interval</label>'
    + '<select id="stp-intv">' + makeOpts([5,10,15,30,60], cfg.storm_interval_min||15)    + '</select>'
    + '<div class="sf-section">GOTO blocking</div>'
    + '<label>Block manual GOTO</label>'
    + '<input id="stp-blk" type="checkbox"' + (cfg.storm_block_manual ? ' checked' : '') + '>'
    + '<div class="sf-hint">Blocks N1MM GOTO during storm correction (unless "always controllable" per rotor)</div>'
    + '</div>';
}

/* ─── Web panel ───────────────────────────────────────────────────────── */
async function savePassword(field, value) {
  const r = await fetch('/api/set-password', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', ...authHeader() },
    body: JSON.stringify({ field, value })
  });
  if (r.ok) {
    showToast(value === '__clear__' ? 'Password removed.' : 'Password saved.');
    loadConfig();
    fetchStatus();  /* refresh lock icon */
  } else if (r.status === 401) {
    showToast('Please login first', true);
  } else {
    showToast('Error saving password.', true);
  }
}

function buildPasswordEntry(el, id, field, isSet, label, hint) {
  const wrap = document.createElement('div');
  wrap.className = 'sf-grid';
  wrap.style.gridColumn = '1/-1';
  const ph = isSet ? '•••••••• (set — leave blank to keep unchanged)' : 'Leave empty to disable';
  wrap.innerHTML = `
    <label class="sf-label">${label}</label>
    <div class="pw-row">
      <input id="${id}" type="password" placeholder="${ph}" autocomplete="new-password">
      <button class="btn pw-save-btn" data-field="${field}">Save</button>
      ${isSet ? `<button class="btn-clear-pw" data-field="${field}" title="Remove password">${ICO_CLOSE}</button>` : ''}
    </div>
    <div class="sf-hint">${hint}</div>`;
  wrap.querySelector('.pw-save-btn').addEventListener('click', () => {
    const val = wrap.querySelector(`#${id}`).value;
    if (!val) { showToast('Enter a new password first.', true); return; }
    requireAuth(() => savePassword(field, val));
  });
  const clr = wrap.querySelector('.btn-clear-pw');
  if (clr) clr.addEventListener('click', () => {
    if (confirm('Remove this password?'))
      requireAuth(() => savePassword(field, '__clear__'));
  });
  el.appendChild(wrap);
}

function buildWebPanel(el, cfg) {
  el.innerHTML = `<div class="sf-grid">
    <div class="sf-section">Web interface</div>
    <label>Port</label><input id="webp-port" type="number" value="${cfg.web_port||80}">
    <label>Session timeout</label>
    <div style="display:flex;align-items:center;gap:6px">
      <input id="webp-timeout" type="number" min="5" max="480" step="5" value="${cfg.session_timeout_min||60}" style="width:70px">
      <span style="font-size:12px;color:var(--text2)">minutes (5–480)</span>
    </div>
    <div class="sf-section">Control password</div>
  </div>`;
  const grid = el.querySelector('.sf-grid');
  buildPasswordEntry(grid, 'webp-vpw', 'web_view_password',
    cfg.web_view_password_set, 'Control password',
    'Required to send GOTO/STOP commands. Without this anyone can control rotors.');
  const sec2 = document.createElement('div');
  sec2.className = 'sf-section'; sec2.style.gridColumn = '1/-1';
  sec2.textContent = 'Admin password';
  grid.appendChild(sec2);
  buildPasswordEntry(grid, 'webp-pw', 'web_password',
    cfg.web_password_set, 'Admin password',
    'Required for settings and storm control. Save with the button on the right.');
}

/* ─── Collect all settings from the modal ────────────────────────────── */
function collectSettings(overlay, baseCfg) {
  const g  = id => { const el = overlay.querySelector('#'+id); return el ? el.value : ''; };
  const gi = id => parseInt(g(id)) || 0;
  const gf = id => parseFloat(g(id)) || 0;
  const gc = id => { const el = overlay.querySelector('#'+id); return el && el.checked ? 1 : 0; };

  return Object.assign({}, baseCfg, {
    /* Server */
    cmd_port:     gi('sp-cmd')   || 12040,
    bcast_port2:  gi('sp-bcast2'),
    bcast_addr:   g('sp-baddr')  || '255.255.255.255',
    idle_ms:      gi('sp-idle')  || 1000,
    moving_ms:    gi('sp-mov')   || 200,
    logfile:      baseCfg.logfile || '',
    /* Wind */
    wind_enabled:             gc('wp-en'),
    wind_source:              g('wp-src')        || 'openmeteo',
    wind_lat:                 gf('wp-lat'),
    wind_lon:                 gf('wp-lon'),
    wind_interval_min:        gi('wp-intv')       || 15,
    wind_owm_apikey:          g('wp-owm'),
    wind_wapi_apikey:         g('wp-wapi'),
    wind_serial_device:       g('wp-serial-dev'),
    wind_serial_baud:         gi('wp-serial-baud') || 19200,
    wind_udp_port:            gi('wp-udp-port'),
    wind_ecowitt_port:        gi('wp-eco-port')   || 49199,
    wind_ecowitt_passkey:     g('wp-eco-pk'),
    wind_ecowitt_fallback:    g('wp-eco-fb'),
    wind_ecowitt_fallback_min:gi('wp-eco-fbmin')  || 5,
    wind_ecowitt_correction:  gi('wp-eco-corr')   || 100,
    wind_ecowitt_fwd_host:    g('wp-fwd-host'),
    wind_ecowitt_fwd_port:    gi('wp-fwd-port'),
    wind_ecowitt_fwd_path:    g('wp-fwd-path')    || '/',
    wind_ecowitt_fwd_passkey: g('wp-fwd-pk'),
    /* Storm */
    storm_threshold_bft: gi('stp-thr')  || 7,
    storm_sustain_min:   gi('stp-sus')  || 5,
    storm_release_min:   gi('stp-rel')  || 10,
    storm_interval_min:  gi('stp-intv') || 15,
    storm_block_manual:  gc('stp-blk'),
    /* Web */
    web_port:            gi('webp-port')    || 80,
    session_timeout_min: gi('webp-timeout') || 60,
    /* passwords are saved separately via their own Save buttons */
    /* Rotors — managed in rotors panel, already in baseCfg */
    num_rotors: baseCfg.rotors ? baseCfg.rotors.length : baseCfg.num_rotors
  });
}

/* ─── Toast notification ──────────────────────────────────────────────── */
function showToast(msg, isError) {
  const t = document.createElement('div');
  t.textContent = msg;
  t.style.cssText = `position:fixed;bottom:20px;left:50%;transform:translateX(-50%);
    background:${isError?'var(--red)':'var(--green)'};color:#fff;padding:10px 20px;
    border-radius:6px;font-size:14px;z-index:999;pointer-events:none;
    box-shadow:0 2px 8px rgba(0,0,0,.4)`;
  document.body.appendChild(t);
  setTimeout(() => t.remove(), 3000);
}

/* ─── Theme toggle ───────────────────────────────────────────────────── */
function applyTheme(light) {
  document.body.classList.toggle('light', light);
  const btn = document.getElementById('btn-theme');
  if (btn) btn.innerHTML = light ? ICO_MOON : ICO_SUN;
}

(function initTheme() {
  const stored = localStorage.getItem('theme');
  const preferLight = stored ? stored === 'light'
                             : window.matchMedia('(prefers-color-scheme: light)').matches;
  applyTheme(preferLight);
})();

document.getElementById('btn-log').addEventListener('click', () => toggleLogPanel());

document.getElementById('btn-theme').addEventListener('click', () => {
  const light = !document.body.classList.contains('light');
  applyTheme(light);
  localStorage.setItem('theme', light ? 'light' : 'dark');
  /* Redraw canvases with new theme colours */
  renderRotors();
  renderWindRose();
});

/* ─── Combined menu (⏻ button) ───────────────────────────────────────── */
document.getElementById('btn-menu').addEventListener('click', () => {
  const existing = document.getElementById('main-menu-popup');
  if (existing) { existing.remove(); return; }
  showMainMenu();
});

function showMainMenu() {
  const popup = document.createElement('div');
  popup.id = 'main-menu-popup';
  popup.className = 'power-popup';

  const isControl = !!(viewToken || authToken);
  const isAdmin   = !!authToken;
  const hasAnyPw  = controlRequired || authRequired;

  /* Build menu items based on state */
  let items = '';
  if (hasAnyPw && !isControl)
    items += `<button class="pp-btn pp-login"  id="mm-login">${ICO_LOGIN} Login</button>`;
  if (isControl)
    items += `<button class="pp-btn pp-logout" id="mm-logout">${ICO_LOGOUT} Logout</button>`;
  items += `<div class="pp-divider"></div>`;
  items += `<button class="pp-btn pp-reboot"   id="mm-reboot">${ICO_REBOOT} Reboot</button>`;
  items += `<button class="pp-btn pp-shutdown" id="mm-shutdown">${ICO_SHUTDOWN} Shutdown</button>`;

  popup.innerHTML = items;
  document.body.appendChild(popup);

  const btn = document.getElementById('btn-menu');
  const rect = btn.getBoundingClientRect();
  popup.style.top   = (rect.bottom + 6) + 'px';
  popup.style.right = (window.innerWidth - rect.right) + 'px';

  const close = () => popup.remove();

  popup.querySelector('#mm-login')?.addEventListener('click', () => { close(); showLogin(null, '', false, true); });
  popup.querySelector('#mm-logout')?.addEventListener('click', () => {
    close();
    authToken = ''; viewToken = '';
    sessionStorage.removeItem('auth_token');
    sessionStorage.removeItem('view_token');
    location.reload();
  });
  popup.querySelector('#mm-reboot').addEventListener('click', () => {
    close();
    requireAuth(() => showPowerConfirm('Reboot', 'Reboot the system?', async () => {
      await fetch('/api/reboot', { method:'POST', headers: authHeader() });
      showToast('Rebooting…');
    }));
  });
  popup.querySelector('#mm-shutdown').addEventListener('click', () => {
    close();
    requireAuth(() => showPowerConfirm('Shutdown', 'Shut down the system?', async () => {
      await fetch('/api/shutdown', { method:'POST', headers: authHeader() });
      showToast('Shutting down…');
    }));
  });

  setTimeout(() => document.addEventListener('click', function h(e) {
    if (!popup.contains(e.target) && e.target.id !== 'btn-menu') { close(); }
    document.removeEventListener('click', h);
  }), 10);
}

function showPowerConfirm(title, message, onConfirm) {
  const dlg = document.createElement('div');
  dlg.className = 'dlg-overlay';
  dlg.innerHTML = `
    <div class="dlg">
      <h3>${title}</h3>
      <p style="margin:12px 0;color:var(--text2);font-size:13px">${message}</p>
      <div class="dlg-btns">
        <button class="btn pp-confirm-ok">${title}</button>
        <button class="btn btn-cancel pp-confirm-cancel">Cancel</button>
      </div>
    </div>`;
  document.body.appendChild(dlg);
  dlg.querySelector('.pp-confirm-cancel').addEventListener('click', () => dlg.remove());
  dlg.querySelector('.pp-confirm-ok').addEventListener('click', () => { dlg.remove(); onConfirm(); });
}

function authHeader() {
  const tok = authToken || viewToken;
  return tok ? { 'Authorization': 'Bearer ' + tok } : {};
}

/* ─── Session heartbeat ──────────────────────────────────────────────── */
/* Sends a lightweight request every 10 minutes to keep the token alive.
 * On 401 (idle timeout expired): clear tokens and show login screen. */
function sessionExpired() {
  authToken = ''; viewToken = '';
  sessionStorage.removeItem('auth_token');
  sessionStorage.removeItem('view_token');
  showToast('Session expired — please log in again', true);
  setTimeout(() => location.reload(), 2000);
}

async function heartbeat() {
  if (!authToken && !viewToken) return;
  try {
    const r = await fetch('/api/heartbeat', { method: 'POST', headers: authHeader() });
    if (r.status === 401) sessionExpired();
  } catch(e) { /* network error — ignore, SSE will handle disconnect */ }
}

setInterval(heartbeat, 10 * 60 * 1000); /* every 10 minutes */

/* ─── Boot ───────────────────────────────────────────────────────────── */
async function boot() {
  await fetchStatus();
  document.body.classList.remove('not-ready');
  connectSSE();
  loadConfig();
}
boot();
