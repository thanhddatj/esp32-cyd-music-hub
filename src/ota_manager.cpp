#include "ota_manager.h"
#include <WiFi.h>
#include <WebServer.h>   // built-in synchronous – BLE-safe
#include <Update.h>

// ── State ─────────────────────────────────────────────────────
static OtaState   g_state     = OtaState::IDLE;
static uint8_t    g_progress  = 0;
static String     g_ip        = "192.168.4.1";
static char       g_errBuf[64]= "";
static bool       g_reboot    = false;
static WebServer* g_server    = nullptr;

// ── Embedded HTML ─────────────────────────────────────────────
static const char OTA_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="vi">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Music Hub OTA</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0d0d1a;color:#e0e0e0;font-family:system-ui,sans-serif;
     display:flex;align-items:center;justify-content:center;min-height:100vh;padding:16px}
.card{background:#1a1a2e;border:1px solid #2a2a4a;border-radius:16px;
      padding:28px 24px;width:100%;max-width:400px;text-align:center}
h1{color:#00d4ff;font-size:1.3rem;margin-bottom:4px}
.sub{color:#666;font-size:.82rem;margin-bottom:24px}
.drop{border:2px dashed #2a4a6a;border-radius:12px;padding:28px 16px;
      cursor:pointer;margin-bottom:16px;background:#0d1526;transition:.2s}
.drop:hover,.drop.drag{border-color:#00d4ff;background:#0d1f3c}
.drop input{display:none}
.ico{font-size:2rem;margin-bottom:6px}
.lbl{color:#888;font-size:.88rem}
.fname{color:#00d4ff;font-size:.82rem;margin-top:6px;word-break:break-all}
.btn{width:100%;padding:13px;background:#00d4ff;color:#0d0d1a;border:none;
     border-radius:10px;font-size:.95rem;font-weight:700;cursor:pointer;transition:.2s}
.btn:disabled{background:#1a3a4a;color:#446;cursor:not-allowed}
.btn:hover:not(:disabled){background:#33ddff}
.prog{display:none;margin-top:16px}
.bar{background:#0d1526;border-radius:8px;height:12px;overflow:hidden;border:1px solid #2a4a6a}
.fill{height:100%;background:linear-gradient(90deg,#0066ff,#00d4ff);width:0%;transition:width .3s;border-radius:8px}
.pct{color:#00d4ff;font-size:1rem;font-weight:700;margin-top:6px}
.st{margin-top:12px;font-size:.88rem;color:#888;min-height:18px}
.ok{color:#00e676}.err{color:#ff5252}.warn{color:#ffd740}
.done{display:none;background:#0d1f0d;border:1px solid #00e676;
      border-radius:12px;padding:16px;margin-top:14px}
.done h2{color:#00e676;margin-bottom:4px;font-size:1rem}
</style>
</head>
<body>
<div class="card">
  <h1>Music Hub OTA</h1>
  <p class="sub">Upload firmware .bin cho ESP32</p>
  <div class="drop" id="dz" onclick="document.getElementById('fi').click()"
       ondrop="onDrop(event)" ondragover="onOver(event)" ondragleave="onLeave()">
    <input type="file" id="fi" accept=".bin" onchange="onFile(this)">
    <div class="ico">[ .bin ]</div>
    <div class="lbl">Tap de chon file <b>.bin</b></div>
    <div class="fname" id="fn"></div>
  </div>
  <button class="btn" id="btn" onclick="doUpload()" disabled>Upload Firmware</button>
  <div class="prog" id="prog">
    <div class="bar"><div class="fill" id="fill"></div></div>
    <div class="pct" id="pct">0%</div>
  </div>
  <div class="st" id="st"></div>
  <div class="done" id="done">
    <h2>Flash xong!</h2>
    <p>Thiet bi dang reboot...<br>Co the dong trang nay.</p>
  </div>
</div>
<script>
var file=null;
function onFile(i){file=i.files[0];document.getElementById('fn').textContent=file?file.name:'';document.getElementById('btn').disabled=!file;st('');}
function onDrop(e){e.preventDefault();onLeave();var f=e.dataTransfer.files[0];if(f&&f.name.endsWith('.bin')){file=f;document.getElementById('fn').textContent=f.name;document.getElementById('btn').disabled=false;st('');}else st('Chi chap nhan .bin','warn');}
function onOver(e){e.preventDefault();document.getElementById('dz').classList.add('drag');}
function onLeave(){document.getElementById('dz').classList.remove('drag');}
function st(m,c){var e=document.getElementById('st');e.textContent=m;e.className='st '+(c||'');}
function doUpload(){
  if(!file)return;
  document.getElementById('btn').disabled=true;
  document.getElementById('prog').style.display='block';
  st('Dang upload...');
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/update',true);
  xhr.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);document.getElementById('fill').style.width=p+'%';document.getElementById('pct').textContent=p+'%';}};
  xhr.onload=function(){if(xhr.status===200){st('Thanh cong! Dang reboot...','ok');document.getElementById('done').style.display='block';}else{st('Loi: '+xhr.responseText,'err');document.getElementById('btn').disabled=false;}};
  xhr.onerror=function(){st('Mat ket noi - co the da reboot','warn');};
  var fd=new FormData();fd.append('firmware',file,file.name);xhr.send(fd);
}
</script>
</body></html>
)HTML";

// ── Handlers ──────────────────────────────────────────────────
static void handleRoot() {
  g_server->send_P(200, "text/html", OTA_HTML);
}

static void handleUpdateDone() {
  if (Update.hasError()) {
    g_server->send(500, "text/plain",
                   g_errBuf[0] ? g_errBuf : "Flash error");
    g_state = OtaState::ERROR;
  } else {
    g_server->send(200, "text/plain", "OK");
    g_state    = OtaState::SUCCESS;
    g_progress = 100;
    g_reboot   = true;
  }
}

static void handleUpload() {
  HTTPUpload& up = g_server->upload();

  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("[OTA] Upload start: %s\n", up.filename.c_str());
    g_state    = OtaState::UPLOADING;
    g_progress = 0;
    g_errBuf[0]= '\0';

    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
      strlcpy(g_errBuf, Update.errorString(), sizeof(g_errBuf));
      g_state = OtaState::ERROR;
    }

  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (g_state == OtaState::ERROR) return;

    size_t written = Update.write(up.buf, up.currentSize);
    if (written != up.currentSize) {
      Update.printError(Serial);
      strlcpy(g_errBuf, Update.errorString(), sizeof(g_errBuf));
      g_state = OtaState::ERROR;
      return;
    }

    // Progress 0-99 (100 set saukhi end())
    size_t prog = Update.progress();
    size_t total= up.totalSize > 0 ? up.totalSize : (prog + 1);
    g_progress  = (uint8_t)min(99UL, prog * 100UL / total);

  } else if (up.status == UPLOAD_FILE_END) {
    if (g_state == OtaState::ERROR) return;

    if (!Update.end(true)) {
      Update.printError(Serial);
      strlcpy(g_errBuf, Update.errorString(), sizeof(g_errBuf));
      g_state = OtaState::ERROR;
    } else {
      Serial.printf("[OTA] Flash OK: %u bytes\n", up.totalSize);
      // g_state / g_reboot set trong handleUpdateDone()
    }
  }
}

// ── Public ────────────────────────────────────────────────────
OtaState    ota_get_state()    { return g_state; }
uint8_t     ota_get_progress() { return g_progress; }
String      ota_get_ip()       { return g_ip; }
const char* ota_get_error()    { return g_errBuf; }

void ota_start(const char* ssid, const char* pass) {
  if (g_state != OtaState::IDLE) return;

  // Reset – g_reboot PHẢI false trước khi tick() chạy
  g_reboot    = false;
  g_progress  = 0;
  g_errBuf[0] = '\0';

  // WiFi AP
  WiFi.disconnect(true);
  delay(150);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, pass);
  delay(300);
  g_ip = WiFi.softAPIP().toString();
  Serial.printf("[OTA] AP '%s'  IP: %s\n", ssid, g_ip.c_str());

  // Web server
  if (g_server) { delete g_server; }
  g_server = new WebServer(80);

  g_server->on("/",       HTTP_GET,  handleRoot);
  g_server->on("/update", HTTP_POST, handleUpdateDone, handleUpload);
  g_server->onNotFound([]() { g_server->sendHeader("Location", "/"); g_server->send(302); });

  g_server->begin();
  g_state = OtaState::READY;
  Serial.println("[OTA] Server ready");
}

void ota_stop() {
  if (g_server) {
    g_server->stop();
    delete g_server;
    g_server = nullptr;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  g_state    = OtaState::IDLE;
  g_reboot   = false;
  g_progress = 0;
  Serial.println("[OTA] Stopped");
}

bool ota_tick() {
  if (!g_server || g_state == OtaState::IDLE) return false;

  g_server->handleClient();   // <-- sync, safe với BLE

  if (g_reboot) {
    Serial.println("[OTA] Rebooting in 1s...");
    delay(1000);
    ESP.restart();
  }

  return (g_state == OtaState::UPLOADING);
}
