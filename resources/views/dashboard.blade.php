@extends('layout')
@section('title', 'Dashboard')
@section('content')
<div class="page-heading">
  <div><p class="eyebrow">LIVE MONITORING</p><h1>System dashboard</h1><p class="mute">Environmental readings and device status at a glance.</p></div>
  <div class="updated"><span class="pulse-dot"></span>Updates every 3 seconds</div>
</div>
<div class="status-row">
  <span class="dot" id="dot"></span><span id="conn">Connecting…</span>
  <span class="mute" id="seen"></span>
</div>

<div class="grid">
  <div class="card metric-card"><div class="label">Temperature <span class="metric-icon warm">°</span></div><div class="big"><span id="temp">--</span><small>°C</small></div><div class="mute">DHT22 · GPIO 4</div></div>
  <div class="card metric-card"><div class="label">Humidity <span class="metric-icon cool">%</span></div><div class="big"><span id="hum">--</span><small>%</small></div><div class="mute">Relative humidity</div></div>
  <div class="card metric-card"><div class="label">Operating mode</div><div class="big sm" id="mode">--</div><div class="mute" id="modedesc"></div></div>
  <div class="card metric-card" id="alertcard"><div class="label">Temperature alert</div><div class="big sm" id="alert">--</div><div class="mute" id="alertinfo"></div></div>
</div>

<div class="card control-card">
  <div class="control-copy"><div class="section-title"><div><h2>Mode control</h2><p class="mute">Short press cycles modes; in Manual it toggles all outputs. Hold 3 seconds to enter or leave Manual.</p></div></div><div class="mute" id="controlstatus" aria-live="polite">Command: <strong id="requestedmode">No command yet</strong></div></div>
  <button type="button" class="press-button" id="modebutton" aria-label="Short press to cycle modes or toggle outputs in Manual. Hold for 3 seconds to enter or leave Manual."><span class="button-mark">●</span><span>PRESS TO CONTROL</span><small>Hold 3s: Manual / Idle</small></button>
</div>

<div class="card">
  <div class="section-title"><div><h2>Device outputs</h2><p class="mute">Outputs assigned to the active mode</p></div><span class="tag">ESP32</span></div>
  <div class="leds">
    <div class="led" id="led1"><i></i>LED 1 (G25)</div>
    <div class="led" id="led2"><i></i>LED 2 (G26)</div>
    <div class="led" id="led3"><i></i>LED 3 (G12)</div>
    <div class="led" id="buzzer"><i></i>Buzzer (G32)</div>
  </div>
  <div class="mute" style="margin-top:10px" id="thresholds"></div>
</div>

<div class="card">
  <div class="section-title"><div><h2>Sensor history</h2><p class="mute">Last 60 readings from the device</p></div><span class="tag">LIVE</span></div>
  <canvas id="chart" height="110"></canvas>
</div>
@endsection

@push('scripts')
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
<script>
const $ = id => document.getElementById(id);
const modeNames = { IDLE: 'Idle', MODE1: 'Mode 1', MODE2: 'Mode 2', MODE3: 'Mode 3', MANUAL: 'Manual' };
const chart = new Chart($('chart'), {
  type: 'line',
  data: { labels: [], datasets: [
    { label: 'Temperature °C', data: [], borderColor: '#d64545', yAxisID: 'y', tension: .3, pointRadius: 0 },
    { label: 'Humidity %',     data: [], borderColor: '#2f6fd6', yAxisID: 'y1', tension: .3, pointRadius: 0 }
  ]},
  options: { animation: false, interaction: { mode: 'index', intersect: false }, scales: {
    y:  { position: 'left',  title: { display: true, text: '°C' } },
    y1: { position: 'right', title: { display: true, text: '%' }, grid: { drawOnChartArea: false } }
  }}
});

function render(d) {
  $('dot').className = 'dot ' + (d.online ? 'on' : 'off');
  $('conn').textContent = d.online ? 'Device online' : 'Device offline';
  $('seen').textContent = d.last_seen ? '· last data ' + d.last_seen : '· no data yet';

  const has = Number.isFinite(Number(d.temperature)) && d.temperature !== null;
  $('temp').textContent = has ? Number(d.temperature).toFixed(1) : '--';
  $('hum').textContent  = has && Number.isFinite(Number(d.humidity)) ? Number(d.humidity).toFixed(1) : '--';
  $('mode').textContent = d.mode_label || '--';
  $('modedesc').textContent = d.mode_desc || '';
  $('requestedmode').textContent = d.requested_mode ? modeNames[d.requested_mode] : 'No command yet';

  const alarm = d.online && d.alert;
  $('alert').textContent = alarm ? 'ALERT' : (d.online ? 'Normal' : 'Unknown');
  $('alertcard').classList.toggle('alarm', alarm);
  $('alertinfo').textContent = d.last_alert
    ? 'Last alert: ' + d.last_alert.time + ' (' + d.last_alert.temperature + ' °C)'
    : 'No alerts logged yet';
  $('thresholds').textContent = 'Alarm starts at ' + d.alert_on + ' °C and stops at ' + d.alert_clear + ' °C (Idle and Manual modes).';

  [1, 2, 3].forEach(n => {
    const el = $('led' + n);
    el.classList.toggle('lit', d.online && !!d.leds[n - 1]);
  });
  $('buzzer').classList.toggle('lit', d.online && !!d.leds?.some(Boolean));

  chart.data.labels = d.history.map(p => p.t);
  chart.data.datasets[0].data = d.history.map(p => p.temp);
  chart.data.datasets[1].data = d.history.map(p => p.hum);
  chart.update();
}

async function refresh() {
  try {
    const r = await fetch('{{ route('dashboard.data') }}', { headers: { 'Accept': 'application/json' } });
    if (r.status === 401) { location.href = '{{ route('login') }}'; return; }
    render(await r.json());
  } catch (e) {
    $('conn').textContent = 'Cannot reach server';
    $('dot').className = 'dot off';
  }
}
const modeButton = $('modebutton');
let holdTimer = null;
let heldForManual = false;
let sendingMode = false;

async function sendMode(action) {
  if (sendingMode) return;
  sendingMode = true;
  modeButton.disabled = true;
    $('controlstatus').textContent = action === 'long' ? 'Sending long-press command…' : 'Sending short-press command…';
  try {
    const response = await fetch('{{ route('dashboard.mode') }}', {
      method: 'POST',
      headers: { 'Accept': 'application/json', 'Content-Type': 'application/json', 'X-CSRF-TOKEN': document.querySelector('meta[name="csrf-token"]').content },
    body: JSON.stringify({ action })
    });
    if (response.status === 401) { location.href = '{{ route('login') }}'; return; }
    if (!response.ok) throw new Error('Command failed');
    const result = await response.json();
    $('requestedmode').textContent = modeNames[result.mode];
    $('controlstatus').textContent = result.message + (result.mode === 'MANUAL' ? (result.outputs_on ? ' Outputs on.' : ' Outputs off.') : '');
  } catch (error) {
    $('controlstatus').textContent = 'Could not send the mode command. Check the server connection.';
  } finally {
    sendingMode = false;
    modeButton.disabled = false;
  }
}

modeButton.addEventListener('pointerdown', event => {
  if (sendingMode) return;
  event.preventDefault();
  heldForManual = false;
  modeButton.classList.add('holding');
  modeButton.setPointerCapture(event.pointerId);
  holdTimer = setTimeout(() => {
    heldForManual = true;
    sendMode('long');
  }, 3000);
});
function finishPress() {
  clearTimeout(holdTimer);
  modeButton.classList.remove('holding');
  if (!heldForManual) sendMode('short');
}
modeButton.addEventListener('pointerup', finishPress);
modeButton.addEventListener('pointercancel', () => { clearTimeout(holdTimer); modeButton.classList.remove('holding'); });
modeButton.addEventListener('click', event => { if (event.detail === 0) sendMode('short'); });

refresh();
setInterval(refresh, 3000);
</script>
@endpush
