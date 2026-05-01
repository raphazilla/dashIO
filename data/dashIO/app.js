// dashIO — SPA router + WebSocket client
'use strict';

const WS_URL       = `ws://${location.host}/ws`;
const API_BASE     = `http://${location.host}`;
const RECONNECT_MS = 2000;
const IMU_HISTORY  = 60;

// --- State ---
let ws = null;
let imuChart = null;
let imuData  = { ax: [], ay: [], az: [] };
const loraLog = [];
const MAX_LOG = 50;
let fmPath    = '/';
let chargingActive = false;

// Terminal state
let termHistory = [];
let termHistIdx = -1;
let termSeq     = 0;

// --- DOM ---
const $ = id => document.getElementById(id);
const fmt = (v, d = 2) => (typeof v === 'number') ? v.toFixed(d) : (v ?? '--');

function uptime(s) {
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
  return `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(sec).padStart(2,'0')}`;
}
function msToTs(ms) {
  const d = new Date(ms);
  return `${String(d.getMinutes()).padStart(2,'0')}:${String(d.getSeconds()).padStart(2,'0')}.${String(d.getMilliseconds()).padStart(3,'0')}`;
}

// ============================================================
// Theme Toggle
// ============================================================
function applyTheme(theme) {
  if (theme === 'light') {
    document.documentElement.classList.add('light');
    $('theme-toggle').textContent = '☀️';
  } else {
    document.documentElement.classList.remove('light');
    $('theme-toggle').textContent = '🌙';
  }
}

function toggleTheme() {
  const isLight = document.documentElement.classList.toggle('light');
  const theme = isLight ? 'light' : 'dark';
  localStorage.setItem('dashio-theme', theme);
  $('theme-toggle').textContent = isLight ? '☀️' : '🌙';
}

// ============================================================
// Router
// ============================================================
document.querySelectorAll('.nav-item').forEach(el => {
  el.addEventListener('click', () => {
    const page = el.dataset.page;
    document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));
    document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
    el.classList.add('active');
    $(`page-${page}`).classList.add('active');
    if (page === 'media')    fm_load(fmPath);
    if (page === 'admin')    admin_loadConfig();
    if (page === 'terminal') $('term-input') && $('term-input').focus();
  });
});

// ============================================================
// WebSocket
// ============================================================
function connect() {
  ws = new WebSocket(WS_URL);
  ws.onopen = () => {
    $('ws-dot').className = 'ws-dot connected';
    $('ws-label').textContent = 'Connected';
  };
  ws.onclose = () => {
    $('ws-dot').className = 'ws-dot';
    $('ws-label').textContent = 'Disconnected...';
    setTimeout(connect, RECONNECT_MS);
  };
  ws.onerror = () => ws.close();
  ws.onmessage = evt => {
    try {
      const msg = JSON.parse(evt.data);
      if      (msg.type === 'sensors')      handleSensors(msg);
      else if (msg.type === 'lora_rx')      handleLoRaRx(msg);
      else if (msg.type === 'terminal_out') handleTerminalOut(msg);
    } catch (_) {}
  };
}

function send(obj) {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
}

// ============================================================
// Sensors
// ============================================================
function handleSensors(d) {
  updateDashboard(d);
  updateBattery(d.battery);
  updateIMU(d.imu);
  updateAudio(d.audio);
  updateLoRaStatus(d.lora);
  updateGPS(d.gps);
}

function updateDashboard(d) {
  const sys = d.system || {};
  $('sys-uptime').textContent  = uptime(sys.uptime ?? 0);
  $('sys-heap').textContent    = ((sys.heap ?? 0) / 1024).toFixed(1) + ' KB';
  $('dash-clients').textContent = sys.clients ?? 0;
  chipState('chip-wifi', sys.clients > 0 ? 'ok' : 'idle');

  // Show STA IP in dashboard if connected
  if (sys.sta_ip && sys.sta_ip.length > 0) {
    $('sta-ip-dash-row').style.display = '';
    $('sta-ip-dash').textContent = sys.sta_ip;
  } else {
    $('sta-ip-dash-row').style.display = 'none';
  }

  const lora = d.lora || {};
  $('dash-lora').textContent = lora.status ?? '--';
  chipState('chip-lora', lora.ok ? 'ok' : 'error');

  const imu = d.imu || {};
  $('dash-imu').textContent = imu.ok ? 'ok' : 'err';
  chipState('chip-imu', imu.ok ? 'ok' : 'error');

  const gps = d.gps || {};
  $('dash-gps').textContent = gps.fix ? 'fix' : 'searching';
  chipState('chip-gps', gps.fix ? 'ok' : 'warn');

  const bat = d.battery || {};
  $('bat-badge').textContent  = (bat.percent ?? '--') + '%';
  $('bat-badge').className    = 'card-badge ' + ((bat.percent ?? 100) > 20 ? 'ok' : 'error');
  $('bat-voltage').textContent = fmt(bat.voltage) + ' V';
  $('bat-percent').textContent = (bat.percent ?? '--') + '%';
  $('bat-percent').className   = 'stat-value ' + batteryColor(bat.percent);
  $('bat-status').textContent  = bat.charging ? 'Charging' : 'On battery';

  $('gps-badge-dash').textContent = gps.fix ? 'FIX' : 'NO FIX';
  $('gps-badge-dash').className   = 'card-badge ' + (gps.fix ? 'ok' : 'warn');
  $('dash-gps-lat').textContent   = gps.fix ? fmt(gps.lat, 6) + '°' : '--';
  $('dash-gps-lon').textContent   = gps.fix ? fmt(gps.lon, 6) + '°' : '--';
  $('dash-gps-sats').textContent  = gps.satellites ?? 0;
}

function chipState(id, state) {
  const el = $(id);
  if (el) el.className = `chip-dot ${state}`;
}

function batteryColor(pct) {
  if (pct == null) return '';
  return pct > 50 ? 'green' : pct > 20 ? 'yellow' : 'red';
}

function updateBattery(bat) {
  if (!bat) return;
  $('bat-badge2').textContent  = (bat.percent ?? '--') + '%';
  $('bat-badge2').className    = 'card-badge ' + ((bat.percent ?? 100) > 20 ? 'ok' : 'error');
  $('bat-voltage2').textContent = fmt(bat.voltage) + ' V';
  $('bat-percent2').textContent = (bat.percent ?? '--') + '%';
  $('bat-percent2').className   = 'stat-value ' + batteryColor(bat.percent);
  $('bat-status2').textContent  = bat.charging ? 'Charging' : 'On battery';
}

function updateIMU(imu) {
  if (!imu) return;
  $('imu-ax').textContent = fmt(imu.ax) + ' g';
  $('imu-ay').textContent = fmt(imu.ay) + ' g';
  $('imu-az').textContent = fmt(imu.az) + ' g';
  $('imu-gx').textContent = fmt(imu.gx) + ' °/s';
  $('imu-gy').textContent = fmt(imu.gy) + ' °/s';
  $('imu-gz').textContent = fmt(imu.gz) + ' °/s';
  ['ax','ay','az'].forEach(k => {
    imuData[k].push(parseFloat(imu[k]) || 0);
    if (imuData[k].length > IMU_HISTORY) imuData[k].shift();
  });
  if (imuChart) imuChart.update('none');
}

function updateAudio(audio) {
  if (!audio) return;
  const level = audio.level ?? 0;
  const peak  = audio.peak  ?? 0;
  const pctL  = Math.min(100, level / 32767 * 100);
  const pctP  = Math.min(100, peak  / 32767 * 100);
  $('audio-level').textContent = level;
  $('audio-peak').textContent  = peak;
  document.querySelectorAll('.meter-bar').forEach((bar, i) => {
    const factor = 0.4 + 0.6 * Math.abs(Math.sin(i * 0.7 + Date.now() * 0.003));
    bar.style.height = Math.max(2, pctL * factor) + '%';
  });
  const pl = $('audio-peak-line');
  if (pl) pl.style.marginBottom = pctP + '%';
}

function updateLoRaStatus(lora) {
  if (!lora) return;
  const ok = lora.ok ?? false;
  $('lora-badge').textContent = ok ? lora.status : 'error';
  $('lora-badge').className   = 'card-badge ' + (ok ? 'ok' : 'error');
  $('lora-freq').textContent  = fmt(lora.freq, 1) + ' MHz';
  $('lora-sf').textContent    = 'SF' + (lora.sf ?? '--');
  $('lora-bw').textContent    = fmt(lora.bw, 0) + ' kHz';
  $('lora-power').textContent = (lora.power ?? '--') + ' dBm';
  $('lora-rssi').textContent  = fmt(lora.rssi, 1) + ' dBm';
  $('lora-snr').textContent   = fmt(lora.snr, 1) + ' dB';
}

function updateGPS(gps) {
  if (!gps) return;
  const fix = gps.fix ?? false;
  $('gps-badge').textContent  = fix ? 'FIX' : 'NO FIX';
  $('gps-badge').className    = 'card-badge ' + (fix ? 'ok' : 'warn');
  $('gps-lat').textContent    = fix ? fmt(gps.lat, 6) + '°' : '--';
  $('gps-lon').textContent    = fix ? fmt(gps.lon, 6) + '°' : '--';
  $('gps-sats').textContent   = (gps.satellites ?? 0);
  $('gps-hdop').textContent   = 'HDOP ' + fmt(gps.hdop, 1);
}

// ============================================================
// LoRa
// ============================================================
function handleLoRaRx(pkt) {
  loraLog.unshift(pkt);
  if (loraLog.length > MAX_LOG) loraLog.pop();
  renderLoRaLog();
  $('lora-badge').textContent = 'RX';
  $('lora-badge').className   = 'card-badge ok';
}

function renderLoRaLog() {
  const log = $('pkt-log');
  if (!loraLog.length) return;
  log.innerHTML = loraLog.map(p => `
    <div class="pkt-entry">
      <span class="pkt-ts">${msToTs(p.ts)}</span>
      <span class="pkt-rssi">${fmt(p.rssi, 1)} dBm</span>
      <span class="pkt-snr">${fmt(p.snr, 1)} dB</span>
      <span class="pkt-data" title="${p.payload}">${p.text || p.payload}</span>
    </div>`).join('');
}

function sendLoRaTx() {
  const input = $('lora-tx-input');
  const payload = input.value.trim();
  if (!payload) return;
  send({ type: 'lora_tx', payload });
  input.value = '';
}

function sendLoRaConfig() {
  send({
    type:  'lora_config',
    sf:    parseInt($('cfg-sf').value),
    bw:    parseFloat($('cfg-bw').value),
    power: parseInt($('cfg-power').value)
  });
}

// ============================================================
// IMU Chart
// ============================================================
function initIMUChart() {
  const ctx = $('imu-chart').getContext('2d');
  const labels = Array.from({length: IMU_HISTORY}, (_, i) => i);
  imuChart = new Chart(ctx, {
    type: 'line',
    data: {
      labels,
      datasets: [
        { label: 'aX', data: imuData.ax, borderColor: '#f85149', borderWidth: 1, pointRadius: 0, tension: 0.3 },
        { label: 'aY', data: imuData.ay, borderColor: '#3fb950', borderWidth: 1, pointRadius: 0, tension: 0.3 },
        { label: 'aZ', data: imuData.az, borderColor: '#00e5ff', borderWidth: 1, pointRadius: 0, tension: 0.3 },
      ]
    },
    options: {
      animation: false,
      responsive: true,
      maintainAspectRatio: false,
      plugins: { legend: { labels: { color: '#8b949e', font: { size: 10 }, boxWidth: 12 } } },
      scales: {
        x: { display: false },
        y: { grid: { color: '#30363d' }, ticks: { color: '#8b949e', font: { size: 10 } } }
      }
    }
  });
}

// ============================================================
// Meter bars
// ============================================================
function initMeter() {
  const wrap = $('meter-bars');
  if (!wrap) return;
  for (let i = 0; i < 20; i++) {
    const bar = document.createElement('div');
    bar.className = 'meter-bar';
    bar.style.height = '2px';
    wrap.appendChild(bar);
  }
}

// ============================================================
// Brightness
// ============================================================
$('brightness-slider') && $('brightness-slider').addEventListener('input', function() {
  $('brightness-val').textContent = this.value;
  send({ type: 'display_brightness', value: parseInt(this.value) });
});

// ============================================================
// File Manager
// ============================================================
function fm_load(path) {
  fmPath = path;
  $('fm-path').textContent = path;
  $('fm-list').innerHTML = '<div style="padding:16px;text-align:center;color:var(--text-muted)">Loading...</div>';

  fetch(`${API_BASE}/api/files?path=${encodeURIComponent(path)}`)
    .then(r => r.json())
    .then(data => fm_render(data.files || []))
    .catch(() => {
      $('fm-list').innerHTML = '<div style="padding:16px;text-align:center;color:var(--red)">Failed to load</div>';
    });
}

function fm_render(files) {
  if (!files.length) {
    $('fm-list').innerHTML = '<div style="padding:16px;text-align:center;color:var(--text-muted)">Empty folder</div>';
    return;
  }
  files.sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name));
  $('fm-list').innerHTML = files.map(f => {
    const fpath = (fmPath.replace(/\/$/, '') + '/' + f.name).replace(/\/\//g, '/');
    return `
    <div class="file-item" onclick="${f.dir ? `fm_load('${fpath}')` : `fm_download('${fpath}')`}">
      <span class="file-icon">${f.dir ? '📁' : '📄'}</span>
      <span class="file-name">${f.name}</span>
      <span class="file-size">${f.dir ? '' : fmSize(f.size)}</span>
      <div class="file-actions" onclick="event.stopPropagation()">
        ${!f.dir ? `<button class="btn btn-ghost btn-sm" title="Download" onclick="fm_download('${fpath}')">↓</button>` : ''}
        <button class="btn btn-danger btn-sm" title="Delete" onclick="fm_delete('${fpath}', ${f.dir})">✕</button>
      </div>
    </div>`;
  }).join('');
}

function fmSize(bytes) {
  if (bytes < 1024)        return bytes + ' B';
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
  return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

function fm_up() {
  const parts = fmPath.replace(/\/$/, '').split('/');
  parts.pop();
  fm_load(parts.join('/') || '/');
}

function fm_refresh() { fm_load(fmPath); }

function fm_download(path) {
  const a = document.createElement('a');
  a.href = `${API_BASE}/api/file?path=${encodeURIComponent(path)}`;
  a.download = path.split('/').pop();
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
}

function fm_delete(path, isDir) {
  if (!confirm(`Delete ${path}?`)) return;
  fetch(`${API_BASE}/api/file?path=${encodeURIComponent(path)}`, { method: 'DELETE' })
    .then(() => fm_refresh())
    .catch(e => alert('Delete failed: ' + e));
}

function fm_mkdir() {
  const name = prompt('Folder name:');
  if (!name) return;
  const path = (fmPath.replace(/\/$/, '') + '/' + name).replace(/\/\//g, '/');
  fetch(`${API_BASE}/api/mkdir`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: `path=${encodeURIComponent(path)}`
  }).then(() => fm_refresh()).catch(e => alert('Failed: ' + e));
}

function fm_upload(input) {
  const files = input.files;
  if (!files.length) return;
  const uploads = Array.from(files).map(file => {
    const fd = new FormData();
    fd.append('file', file, file.name);
    return fetch(`${API_BASE}/api/upload?path=${encodeURIComponent(fmPath)}`, {
      method: 'POST',
      body: fd
    });
  });
  Promise.all(uploads)
    .then(() => { input.value = ''; fm_refresh(); })
    .catch(e => alert('Upload failed: ' + e));
}

// ============================================================
// Admin
// ============================================================
function admin_loadConfig() {
  fetch(`${API_BASE}/api/config`)
    .then(r => r.json())
    .then(d => {
      $('admin-ssid').value = d.ap_ssid     || 'dashIO';
      $('admin-pass').value = d.ap_password || '';
      $('sta-ssid').value   = d.sta_ssid    || '';
      const slider = $('brightness-slider');
      if (slider) {
        slider.value = d.brightness || 200;
        $('brightness-val').textContent = slider.value;
      }
    })
    .catch(() => {});

  wifi_loadStatus();
}

function admin_saveConfig() {
  const body = JSON.stringify({
    ap_ssid:     $('admin-ssid').value,
    ap_password: $('admin-pass').value,
    brightness:  parseInt($('brightness-slider').value)
  });
  fetch(`${API_BASE}/api/config`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body
  }).then(r => r.json()).then(d => {
    if (d.ok) alert('Saved! AP will restart with new settings.');
  }).catch(e => alert('Failed: ' + e));
}

function admin_restart() {
  if (!confirm('Restart the Cardputer?')) return;
  fetch(`${API_BASE}/api/restart`, { method: 'POST' })
    .then(() => alert('Restarting...'))
    .catch(() => {});
}

function admin_enableUsbMsc() {
  if (!confirm('Enable USB Drive mode?\n\nThe device will restart when you eject the drive.')) return;
  fetch(`${API_BASE}/api/usb/msc`, { method: 'POST' })
    .then(r => r.json())
    .then(d => { if (d.ok) alert('USB Drive mode active. Safely eject to resume normal operation.'); })
    .catch(e => alert('Failed: ' + e));
}

function admin_toggleCharging() {
  chargingActive = !chargingActive;
  const btn = $('charging-btn');
  fetch(`${API_BASE}/api/charging`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ enabled: chargingActive })
  }).then(r => r.json()).then(d => {
    if (d.ok) {
      btn.textContent = chargingActive ? 'Disable Charging Mode' : 'Enable Charging Mode';
      btn.className   = chargingActive ? 'btn btn-primary btn-sm' : 'btn btn-warning btn-sm';
    } else {
      chargingActive = !chargingActive;
    }
  }).catch(() => { chargingActive = !chargingActive; });
}

// ============================================================
// WiFi STA
// ============================================================
function wifi_loadStatus() {
  fetch(`${API_BASE}/api/wifi/status`)
    .then(r => r.json())
    .then(d => {
      const connected = d.sta_connected && d.sta_ip && d.sta_ip.length > 0;
      $('wifi-mode-val').textContent = connected ? 'AP + Local Network' : 'AP Only';
      $('sta-badge').textContent  = connected ? 'Connected' : 'AP Only';
      $('sta-badge').className    = 'card-badge ' + (connected ? 'ok' : '');
      if (connected) {
        $('sta-ip-row').style.display = '';
        $('sta-ip-val').textContent   = d.sta_ip;
        $('sta-ssid').value           = d.sta_ssid || '';
      } else {
        $('sta-ip-row').style.display = 'none';
      }
    })
    .catch(() => {});
}

function wifi_scan() {
  const list = $('wifi-scan-list');
  list.style.display = '';
  list.innerHTML = '<div class="wifi-item"><span class="wifi-ssid" style="color:var(--text-muted)">Scanning...</span></div>';

  fetch(`${API_BASE}/api/wifi/scan`)
    .then(r => r.json())
    .then(networks => {
      if (!networks.length) {
        list.innerHTML = '<div class="wifi-item"><span class="wifi-ssid" style="color:var(--text-muted)">No networks found</span></div>';
        return;
      }
      networks.sort((a, b) => b.rssi - a.rssi);
      list.innerHTML = networks.map(n => `
        <div class="wifi-item" onclick="wifi_select('${n.ssid.replace(/'/g,"\\'")}')">
          <span class="wifi-ssid">${n.ssid}</span>
          <span>
            ${n.secure ? '<span class="wifi-lock">🔒</span>' : ''}
            <span class="wifi-rssi">${n.rssi} dBm</span>
          </span>
        </div>`).join('');
    })
    .catch(() => {
      list.innerHTML = '<div class="wifi-item"><span style="color:var(--red)">Scan failed</span></div>';
    });
}

function wifi_select(ssid) {
  $('sta-ssid').value = ssid;
  $('sta-password').focus();
}

function wifi_connect() {
  const ssid = $('sta-ssid').value.trim();
  const pass  = $('sta-password').value;
  if (!ssid) { alert('Enter a network SSID'); return; }

  $('sta-badge').textContent = 'Connecting...';
  $('sta-badge').className   = 'card-badge warn';

  fetch(`${API_BASE}/api/wifi/connect`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid, password: pass })
  }).then(r => r.json()).then(d => {
    if (d.ok) {
      $('sta-ip-row').style.display = '';
      $('sta-ip-val').textContent   = d.ip || '--';
      $('wifi-mode-val').textContent = 'AP + Local Network';
      $('sta-badge').textContent = 'Connected';
      $('sta-badge').className   = 'card-badge ok';
      $('wifi-scan-list').style.display = 'none';
    } else {
      $('sta-badge').textContent = 'Failed';
      $('sta-badge').className   = 'card-badge error';
      alert(d.error || 'Connection failed');
    }
  }).catch(e => {
    $('sta-badge').textContent = 'AP Only';
    $('sta-badge').className   = 'card-badge';
    alert('Error: ' + e);
  });
}

function wifi_disconnect() {
  fetch(`${API_BASE}/api/wifi/disconnect`, { method: 'POST' })
    .then(r => r.json())
    .then(() => {
      $('sta-ip-row').style.display  = 'none';
      $('wifi-mode-val').textContent = 'AP Only';
      $('sta-badge').textContent     = 'AP Only';
      $('sta-badge').className       = 'card-badge';
    })
    .catch(() => {});
}

// ============================================================
// Terminal
// ============================================================
function termPrint(text, cls) {
  const out  = $('term-output');
  const line = document.createElement('div');
  line.className = 'terminal-line' + (cls ? ' ' + cls : '');
  line.innerHTML = text;
  out.appendChild(line);
  out.scrollTop = out.scrollHeight;
}

function termSend() {
  const input = $('term-input');
  const cmd   = input.value.trim();
  if (!cmd) return;

  // echo
  termPrint(`<span class="t-cyan">${$('term-prompt').textContent}</span> ${escHtml(cmd)}`, 'cmd');

  // history
  termHistory.unshift(cmd);
  if (termHistory.length > 50) termHistory.pop();
  termHistIdx = -1;

  input.value = '';

  // send via WebSocket
  send({ type: 'terminal', cmd, seq: ++termSeq });
}

function handleTerminalOut(msg) {
  if (!msg.output) return;
  const lines = msg.output.split('\n');
  lines.forEach(line => {
    if (line === '') return;
    termPrint(escHtml(line), msg.ok ? '' : 'err');
  });
  // Update prompt with new CWD if provided
  if (msg.cwd) $('term-prompt').textContent = `dashIO:${msg.cwd}>`;
}

function escHtml(s) {
  return String(s)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

// ============================================================
// Boot
// ============================================================
document.addEventListener('DOMContentLoaded', () => {
  // Apply saved theme
  const saved = localStorage.getItem('dashio-theme') || 'dark';
  applyTheme(saved);

  initMeter();
  initIMUChart();
  connect();

  // Theme toggle button
  $('theme-toggle').addEventListener('click', toggleTheme);

  // LoRa TX on Enter
  $('lora-tx-input') && $('lora-tx-input').addEventListener('keydown', e => {
    if (e.key === 'Enter') sendLoRaTx();
  });

  // Terminal keyboard shortcuts
  const termInput = $('term-input');
  if (termInput) {
    termInput.addEventListener('keydown', e => {
      if (e.key === 'Enter') {
        e.preventDefault();
        termSend();
      } else if (e.key === 'ArrowUp') {
        e.preventDefault();
        if (termHistIdx < termHistory.length - 1) {
          termHistIdx++;
          termInput.value = termHistory[termHistIdx];
        }
      } else if (e.key === 'ArrowDown') {
        e.preventDefault();
        if (termHistIdx > 0) {
          termHistIdx--;
          termInput.value = termHistory[termHistIdx];
        } else {
          termHistIdx = -1;
          termInput.value = '';
        }
      }
    });
  }
});
