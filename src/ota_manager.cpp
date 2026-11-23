#include "ota_manager.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>

// ── State ─────────────────────────────────────────────────────
static OtaState       g_state    = OtaState::IDLE;
static uint8_t        g_progress = 0;
static bool           g_reboot   = false;
static AsyncWebServer g_server(80);
static String         g_ip       = "192.168.4.1";

// ── Embedded HTML page ────────────────────────────────────────
// Trang web upload firmware, hiển thị progress bar realtime
static const char OTA_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Music Hub OTA</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#0d0d1a;color:#e0e0e0;font-family:system-ui,sans-serif;
       display:flex;align-items:center;justify-content:center;min-height:100vh;padding:16px}
  .card{background:#1a1a2e;border:1px solid #2a2a4a;border-radius:16px;
        padding:32px 28px;width:100%;max-width:420px;text-align:center}
  h1{color:#00d4ff;font-size:1.4rem;margin-bottom:4px}
  .sub{color:#666;font-size:.85rem;margin-bottom:28px}
  .drop{border:2px dashed #2a4a6a;border-radius:12px;padding:32px 20px;
        cursor:pointer;transition:.2s;margin-bottom:20px;background:#0d1526}
  .drop:hover,.drop.drag{border-color:#00d4ff;background:#0d1f3c}
  .drop input{display:none}
  .drop-icon{font-size:2.4rem;margin-bottom:8px}
  .drop-label{color:#888;font-size:.9rem}
  .file-name{color:#00d4ff;font-size:.85rem;margin-top:8px;word-break:break-all}
  .btn{width:100%;padding:14px;background:#00d4ff;color:#0d0d1a;border:none;
       border-radius:10px;font-size:1rem;font-weight:700;cursor:pointer;transition:.2s}
  .btn:disabled{background:#1a3a4a;color:#446;cursor:not-allowed}
  .btn:hover:not(:disabled){background:#33ddff}
  .progress-wrap{display:none;margin-top:20px}
  .progress-bar{background:#0d1526;border-radius:8px;height:12px;overflow:hidden;
                border:1px solid #2a4a6a}
  .progress-fill{height:100%;background:linear-gradient(90deg,#0066ff,#00d4ff);
                 width:0%;transition:width .3s;border-radius:8px}
  .progress-pct{color:#00d4ff;font-size:1.1rem;font-weight:700;margin-top:8px}
  .status{margin-top:16px;font-size:.9rem;color:#888;min-height:20px}
  .ok{color:#00e676}.err{color:#ff5252}.warn{color:#ffd740}
  .reboot{display:none;background:#0d1f0d;border:1px solid #00e676;
          border-radius:12px;padding:20px;margin-top:16px}
  .reboot h2{color:#00e676;margin-bottom:6px}
</style>
</head>
<body>
<div class="card">
  <h1>🎵 Music Hub OTA</h1>
  <p class="sub">Upload firmware mới cho ESP32</p>

  <div class="drop" id="dropZone" onclick="document.getElementById('fileIn').click()"
       ondrop="handleDrop(event)" ondragover="handleDragOver(event)"
       ondragleave="handleDragLeave(event)">
    <input type="file" id="fileIn" accept=".bin" onchange="fileSelected(this)">
    <div class="drop-icon">📦</div>
    <div class="drop-label">Kéo thả hoặc click để chọn file <strong>.bin</strong></div>
    <div class="file-name" id="fileName"></div>
  </div>

  <button class="btn" id="uploadBtn" onclick="startUpload()" disabled>
    Upload Firmware
  </button>

  <div class="progress-wrap" id="progressWrap">
    <div class="progress-bar"><div class="progress-fill" id="progressFill"></div></div>
    <div class="progress-pct" id="progressPct">0%</div>
  </div>

  <div class="status" id="status"></div>

  <div class="reboot" id="rebootBox">
    <h2>✅ Flash thành công!</h2>
    <p>Thiết bị đang khởi động lại...<br>Trang này sẽ tự đóng sau 5 giây.</p>
  </div>
</div>

<script>
var selectedFile = null;

function fileSelected(input) {
  selectedFile = input.files[0];
  document.getElementById('fileName').textContent = selectedFile ? selectedFile.name : '';
  document.getElementById('uploadBtn').disabled = !selectedFile;
  setStatus('');
}

function handleDrop(e) {
  e.preventDefault();
  document.getElementById('dropZone').classList.remove('drag');
  var f = e.dataTransfer.files[0];
  if (f && f.name.endsWith('.bin')) {
    selectedFile = f;
    document.getElementById('fileName').textContent = f.name;
    document.getElementById('uploadBtn').disabled = false;
    setStatus('');
  } else {
    setStatus('⚠ Chỉ chấp nhận file .bin', 'warn');
  }
}
function handleDragOver(e) { e.preventDefault(); document.getElementById('dropZone').classList.add('drag'); }
function handleDragLeave()  { document.getElementById('dropZone').classList.remove('drag'); }

function setStatus(msg, cls) {
  var el = document.getElementById('status');
  el.textContent = msg;
  el.className = 'status ' + (cls || '');
}

function startUpload() {
  if (!selectedFile) return;
  document.getElementById('uploadBtn').disabled = true;
  document.getElementById('progressWrap').style.display = 'block';
  setStatus('Đang upload...', '');

  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/update', true);

  xhr.upload.onprogress = function(e) {
    if (e.lengthComputable) {
      var pct = Math.round(e.loaded / e.total * 100);
      document.getElementById('progressFill').style.width = pct + '%';
      document.getElementById('progressPct').textContent  = pct + '%';
    }
  };

  xhr.onload = function() {
    if (xhr.status === 200) {
      setStatus('Flash xong! Đang reboot...', 'ok');
      document.getElementById('rebootBox').style.display = 'block';
      setTimeout(function(){ window.close(); }, 5000);
    } else {
      setStatus('Lỗi: ' + xhr.responseText, 'err');
      document.getElementById('uploadBtn').disabled = false;
    }
  };

  xhr.onerror = function() {
    setStatus('Mất kết nối (ESP32 đang reboot?)', 'warn');
  };

  var form = new FormData();
  form.append('firmware', selectedFile);
  xhr.send(form);
}
</script>
</body>
</html>
)rawhtml";

// ── Helpers ───────────────────────────────────────────────────
OtaState ota_get_state()    { return g_state; }
uint8_t  ota_get_progress() { return g_progress; }
String   ota_get_ip()       { return g_ip; }

// ── Start ─────────────────────────────────────────────────────
void ota_start(const char* ssid, const char* pass) {
  if (g_state != OtaState::IDLE) return;
  g_state    = OtaState::STARTING;
  g_progress = 0;
  g_reboot   = false;

  // Bật WiFi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, pass);
  g_ip = WiFi.softAPIP().toString();
  Serial.printf("[OTA] AP: %s  IP: %s\n", ssid, g_ip.c_str());

  // ── Route: trang chủ ──────────────────────────────────────
  g_server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", OTA_HTML);
  });

  // ── Route: nhận file .bin ─────────────────────────────────
  g_server.on("/update", HTTP_POST,
    // onRequest – gửi response khi upload xong
    [](AsyncWebServerRequest* req) {
      bool ok = !Update.hasError();
      req->send(ok ? 200 : 500, "text/plain",
                ok ? "OK" : Update.errorString());
      if (ok) {
        g_state  = OtaState::SUCCESS;
        g_reboot = true;
      } else {
        g_state = OtaState::ERROR;
      }
    },
    // onUpload – nhận từng chunk
    [](AsyncWebServerRequest* req, String filename,
       size_t index, uint8_t* data, size_t len, bool final) {

      if (index == 0) {
        Serial.printf("[OTA] Begin: %s  size: %u\n",
                      filename.c_str(), req->contentLength());
        g_state    = OtaState::UPLOADING;
        g_progress = 0;
        if (!Update.begin(req->contentLength())) {
          Serial.printf("[OTA] begin error: %s\n", Update.errorString());
        }
      }

      if (Update.isRunning()) {
        Update.write(data, len);
        // Tính progress
        size_t written = Update.progress();
        size_t total   = req->contentLength();
        g_progress = total ? (uint8_t)(written * 100UL / total) : 0;
        g_state = OtaState::FLASHING;
      }

      if (final) {
        if (Update.end(true)) {
          Serial.println("[OTA] Flash OK");
          g_progress = 100;
        } else {
          Serial.printf("[OTA] end error: %s\n", Update.errorString());
          g_state = OtaState::ERROR;
        }
      }
    }
  );

  // ── 404 ───────────────────────────────────────────────────
  g_server.onNotFound([](AsyncWebServerRequest* req) {
    req->redirect("/");
  });

  g_server.begin();
  g_state = OtaState::READY;
  Serial.println("[OTA] Server ready");
}

// ── Stop ──────────────────────────────────────────────────────
void ota_stop() {
  g_server.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  g_state = OtaState::IDLE;
  Serial.println("[OTA] Stopped");
}

// ── Loop tick – reboot sau khi flash ─────────────────────────
void ota_update() {
  if (g_reboot) {
    delay(1200);  // chờ response gửi xong
    ESP.restart();
  }
}
