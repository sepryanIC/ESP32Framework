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
    .tabs button { padding: 8px 14px; margin-right: 8px; border:0; border-radius:8px; }
    .card { background:#1b2333; padding:16px; border-radius:12px; margin-top:12px; }
    .drop { border:2px dashed #5f88ff; padding:24px; text-align:center; border-radius:10px; }
    .progress { width:100%; height:18px; background:#2a3447; border-radius:10px; overflow:hidden; }
    .bar { width:0; height:100%; background:#43d17d; }
    .grid { display:grid; grid-template-columns: repeat(auto-fill,minmax(220px,1fr)); gap:8px; }
    .kpi { background:#131b2b; border-radius:8px; padding:8px; }
  </style>
</head>
<body>
  <h2>ESP32 Framework</h2>
  <div class="tabs">
    <button>OTA</button><button disabled>Web Terminal</button><button disabled>Credential</button>
    <button disabled>Mesh</button><button disabled>ESP-NOW</button><button disabled>URL</button>
  </div>
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

<script>
let selectedFile = null;
const $ = id => document.getElementById(id);
const msg = t => $('msg').textContent = t;

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
  xhr.open('POST', '/update');
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
setInterval(refreshStatus,2000); refreshStatus();
</script>
</body>
</html>
)rawliteral";
