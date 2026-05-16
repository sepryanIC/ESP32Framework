#pragma once

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>ESP32 Framework</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 16px; background:#101622; color:#eaf0ff; }
    .tabs { display:flex; gap:8px; flex-wrap:wrap; }
    .tabs button { padding: 8px 14px; border:0; border-radius:8px; cursor:pointer; }
    .tabs button.active { background:#5f88ff; color:#fff; }
    .tabs button:disabled { opacity:0.5; cursor:not-allowed; }
    .card { background:#1b2333; padding:16px; border-radius:12px; margin-top:12px; }
    .drop { border:2px dashed #5f88ff; padding:24px; text-align:center; border-radius:10px; }
    .progress { width:100%; height:18px; background:#2a3447; border-radius:10px; overflow:hidden; }
    .bar { width:0; height:100%; background:#43d17d; }
    .grid { display:grid; grid-template-columns: repeat(auto-fill,minmax(220px,1fr)); gap:8px; }
    .kpi { background:#131b2b; border-radius:8px; padding:8px; }
    .panel { display:none; }
    .panel.active { display:block; }
    #termView { width:100%; min-height:260px; background:#060a12; color:#49f27f; border-radius:8px; padding:10px; box-sizing:border-box; font-family:monospace; white-space:pre-wrap; overflow:auto; }
    #termInput { width:100%; box-sizing:border-box; margin-top:8px; border-radius:8px; border:1px solid #334; background:#0f1522; color:#fff; padding:8px; resize:vertical; min-height:96px; font-family:monospace; }
  </style>
</head>
<body>
  <h2>ESP32 Framework</h2>
  <div class="tabs">
    <button id="tab-ota" class="active">OTA</button>
    <button id="tab-term">Web Terminal</button>
    <button disabled>Credential</button>
    <button disabled>Mesh</button>
    <button disabled>ESP-NOW</button>
    <button disabled>URL</button>
  </div>

  <div id="panel-ota" class="panel active">
    <div class="card">
      <h3>OTA Firmware Update</h3>
      <div id="drop" class="drop">Drag & drop firmware <b>.bin</b> or choose file<br><input id="fw" type="file" accept=".bin" /></div>
      <p><button id="btnUpload">Update Firmware</button> <button id="btnRollback">Rollback</button> <button id="btnReboot">Reboot</button></p>
      <div class="progress"><div id="uploadBar" class="bar"></div></div>
      <small id="msg">Idle</small>
    </div>
    <div class="card">
      <h3>Device Info</h3>
      <div id="info" class="grid"></div>
      <div>Heap: <div class="progress"><div id="heapBar" class="bar"></div></div></div>
      <div>Flash: <div class="progress"><div id="flashBar" class="bar"></div></div></div>
    </div>
  </div>

  <div id="panel-term" class="panel">
    <div class="card">
      <h3>Web Terminal</h3>
      <div id="termView"></div>
      <div style="display:flex;gap:8px;flex-wrap:wrap;margin-top:8px;">
        <button id="btnTermClear" type="button">Clear</button>
        <button id="btnTermDownload" type="button">Download</button>
      </div>
      <textarea id="termInput" rows="4" placeholder="ketik command (Enter kirim, Shift+Enter baris baru, max 4092 byte)"></textarea>
      <small id="termMeta">TX/RX max 4092 byte</small>
    </div>
  </div>

<script>
let selectedFile = null;
let activeTab = 'ota';
let statusTimer = null;
let termTimer = null;
const $ = id => document.getElementById(id);
const msg = t => $('msg').textContent = t;

function setTab(tab){
  activeTab = tab;
  $('tab-ota').classList.toggle('active', tab==='ota');
  $('tab-term').classList.toggle('active', tab==='term');
  $('panel-ota').classList.toggle('active', tab==='ota');
  $('panel-term').classList.toggle('active', tab==='term');
  startServicesForTab();
}
$('tab-ota').addEventListener('click', ()=>setTab('ota'));
$('tab-term').addEventListener('click', ()=>setTab('term'));

function stopAllServices(){
  if (statusTimer) { clearInterval(statusTimer); statusTimer = null; }
  if (termTimer) { clearInterval(termTimer); termTimer = null; }
}
function startServicesForTab(){
  stopAllServices();
  if (activeTab === 'ota') {
    refreshStatus();
    statusTimer = setInterval(refreshStatus,2000);
  } else if (activeTab === 'term') {
    pollTerminal();
    termTimer = setInterval(pollTerminal,300);
  }
}

function ensureBin(file){ return file && file.name.toLowerCase().endsWith('.bin'); }
$('fw').addEventListener('change', e => {
  selectedFile = e.target.files[0];
  msg(ensureBin(selectedFile) ? `Ready: ${selectedFile.name}` : 'File must be .bin');
});
$('drop').addEventListener('dragover', e => e.preventDefault());
$('drop').addEventListener('drop', e => {
  e.preventDefault();
  selectedFile = e.dataTransfer.files[0];
  msg(ensureBin(selectedFile) ? `Ready: ${selectedFile.name}` : 'File must be .bin');
});

$('btnUpload').addEventListener('click', () => {
  if (!ensureBin(selectedFile)) return msg('Please select valid .bin file');
  const fd = new FormData(); fd.append('update', selectedFile);
  const xhr = new XMLHttpRequest();
  xhr.upload.onprogress = e => {
    const p = e.lengthComputable ? Math.round((e.loaded/e.total)*100) : 0;
    $('uploadBar').style.width = `${p}%`;
    msg(`Uploading ${p}%`);
  };
  xhr.onload = () => msg(xhr.status === 200 ? 'Upload done, device may reboot' : `Upload failed ${xhr.status}`);
  xhr.open('POST', '/api/update');
  xhr.send(fd);
});

async function post(url){
  const r = await fetch(url,{method:'POST'});
  const j = await r.json();
  msg(j.message || 'done');
}
$('btnRollback').addEventListener('click',()=>post('/api/rollback'));
$('btnReboot').addEventListener('click',()=>post('/api/reboot'));

function dash(v){ return (v===undefined||v===null||v==='')?'-':v; }
function bar(id, used, total){
  const p = total>0 ? Math.min(100,Math.round((used/total)*100)) : 0;
  $(id).style.width = `${p}%`;
}
async function refreshStatus(){
  const r = await fetch('/api/status');
  const s = await r.json();
  const fields = [
    ['Firmware', s.firmwareVersion],['Framework', s.frameworkVersion],['Hardware', s.hardwareVersion],
    ['Device ID', s.deviceId],['Runtime (min)', s.runTimeMinutes],['Power On', s.powerOn],
    ['Suhu', s.temperature],['Tegangan', s.voltage]
  ];
  $('info').innerHTML = fields.map(([k,v])=>`<div class='kpi'><b>${k}</b><br>${dash(v)}</div>`).join('');
  bar('heapBar', s.heapUsed||0, s.heapTotal||0);
  bar('flashBar', s.flashUsed||0, s.flashTotal||0);
}

function appendTerm(text){
  if (!text) return;
  const view = $('termView');
  view.textContent += text;
  if (view.textContent.length > 40000) view.textContent = view.textContent.slice(-40000);
  view.scrollTop = view.scrollHeight;
}

async function pollTerminal(){
  const r = await fetch('/api/webterm/poll');
  const j = await r.json();
  appendTerm(j.data || '');
}

$('btnTermClear').addEventListener('click', () => {
  $('termView').textContent = '';
});

$('btnTermDownload').addEventListener('click', () => {
  const content = $('termView').textContent || '';
  const blob = new Blob([content], {type:'text/plain;charset=utf-8'});
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `webterm-${Date.now()}.log`;
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
});

$('termInput').addEventListener('keydown', async (e) => {
  if (e.key !== 'Enter') return;
  if (e.shiftKey) return;
  e.preventDefault();
  const data = e.target.value;
  e.target.value = '';
  if (!data) return;
  const enc = new TextEncoder().encode(data);
  if (enc.length > 4092) return appendTerm('\n[web] input > 4092 byte ditolak\n');
  await fetch('/api/webterm/write', {method:'POST', headers:{'Content-Type':'text/plain'}, body:data});
});

startServicesForTab();
</script>
</body>
</html>
)rawliteral";
