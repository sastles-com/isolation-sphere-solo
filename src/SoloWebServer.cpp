/**
 * @file SoloWebServer.cpp
 * @brief SoloWebServer実装
 */

#include "SoloWebServer.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <string.h>

namespace sastle {

namespace {

constexpr size_t kRxBufSize = 4096;
/// LittleFS のメタデータ/ウェアレベリング余裕。空き容量からこれを引いた分だけ受け付ける。
constexpr size_t kFsMargin = 64 * 1024;
constexpr const char* kTmpPath = "/video.tmp";
constexpr int kRecvTimeoutRetries = 5;

// ---------------------------------------------------------------------------
// 埋め込み Web UI (iPhone Safari 向け最小構成。外部リソース依存なし)
// ---------------------------------------------------------------------------
const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html><html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Isolation Sphere solo</title>
<style>
:root{color-scheme:dark}
body{font-family:-apple-system,system-ui,sans-serif;margin:0 auto;padding:16px;max-width:520px;background:#111;color:#eee}
h1{font-size:20px;margin:8px 0 16px}h1 small{color:#888;font-weight:normal}
.card{background:#1d1d1f;border-radius:12px;padding:14px;margin-bottom:14px}
.row{display:flex;justify-content:space-between;align-items:center;gap:8px;margin:6px 0}
.k{color:#999;font-size:13px}.v{font-variant-numeric:tabular-nums;text-align:right}
button{font-size:16px;padding:12px 16px;border:0;border-radius:10px;background:#2c7be5;color:#fff;flex:1}
button.warn{background:#c0392b}button.gray{background:#444}button:disabled{opacity:.4}
input[type=range]{width:100%}input[type=file]{width:100%;font-size:14px}
progress{width:100%;height:14px}
.st{display:inline-block;padding:2px 10px;border-radius:999px;font-size:13px;background:#333}
.st.playing{background:#1e8449}.st.error{background:#c0392b}.st.uploading{background:#d68910}
small{color:#888}#msg{min-height:1.2em;color:#f5b041;font-size:14px;word-break:break-all}
</style></head><body>
<h1>Isolation Sphere <small>solo</small></h1>
<div id="cna" class="card" hidden style="background:#3b2f00"><b>接続できました。</b><br><small>この「ログイン」画面ではファイル選択ができない場合があります。動画をアップロードするときは、この画面を閉じてから Safari で <b id="cnaUrl">http://192.168.4.1/</b> を開いてください。再生・停止・明るさはここから操作できます。</small></div>
<div class="card">
 <div class="row"><span class="k">状態</span><span id="state" class="st">-</span></div>
 <div class="row"><span class="k">動画</span><span id="video" class="v">-</span></div>
 <div class="row"><span class="k">再生</span><span id="stats" class="v">-</span></div>
 <div class="row"><span class="k">空き容量</span><span id="fs" class="v">-</span></div>
 <div id="msg"></div>
 <div class="row"><button id="play">再生</button><button id="stop" class="gray">停止</button></div>
</div>
<div class="card">
 <div class="row"><span class="k">明るさ</span><span id="bval" class="v">-</span></div>
 <input id="bri" type="range" min="0" max="100" step="1">
</div>
<div class="card">
 <div class="k">動画アップロード (320×160 / 10fps / raw MJPEG)</div>
 <p><small>アップロード中は再生が止まり、成功すると新しい動画を先頭から再生します。空き容量が足りない場合は現在の動画を先に削除してから受信します (失敗すると「動画なし」になり再アップロードできます)。</small></p>
 <input id="file" type="file" accept=".mjpg,.mjpeg,video/x-motion-jpeg,application/octet-stream">
 <div class="row"><button id="up">アップロード</button><button id="del" class="warn">削除</button></div>
 <progress id="prog" value="0" max="100" hidden></progress>
</div>
<div class="card">
 <div class="row"><span class="k">デバイス</span><span id="dev" class="v">-</span></div>
 <div class="row"><button id="reboot" class="gray">再起動</button></div>
</div>
<script>
const $=id=>document.getElementById(id);
const fmt=n=>n>=1048576?(n/1048576).toFixed(2)+' MB':n>=1024?(n/1024).toFixed(1)+' KB':n+' B';
let busy=false,briTimer=null;
async function api(p,body){const r=await fetch(p,{method:'POST',headers:{'Content-Type':'application/json'},body:body?JSON.stringify(body):'{}'});const j=await r.json().catch(()=>({}));if(!r.ok)throw new Error(j.error||('HTTP '+r.status));return j}
const say=t=>{$('msg').textContent=t};
async function refresh(){if(busy)return;try{const r=await fetch('/api/status',{cache:'no-store'});const s=await r.json();
 const st=$('state');st.textContent=s.state+(s.error?' - '+s.error:'');st.className='st '+s.state;
 $('video').textContent=s.video.present?`${s.video.frames}f / ${s.video.duration_s.toFixed(1)}s / ${fmt(s.video.bytes)}`:'なし';
 $('stats').textContent=`${s.stats.fps.toFixed(1)} fps / miss ${s.stats.deadline_misses} / err ${s.stats.decode_errors}`;
 $('fs').textContent=`${fmt(s.fs.free)} (最大 ${fmt(s.fs.max_upload)})`;
 if(document.activeElement!==$('bri')){$('bri').value=s.brightness;$('bval').textContent=s.brightness+'%'}
 $('dev').textContent=`${s.device} / AP ${s.ap.ssid} (${s.ap.clients}) / up ${s.uptime_s}s`;
 $('play').disabled=s.state!=='stopped';$('stop').disabled=s.state!=='playing';
 $('up').disabled=s.state==='uploading';$('del').disabled=!s.video.present||s.state==='uploading';
}catch(e){$('state').textContent='接続エラー';$('state').className='st error'}}
$('play').onclick=()=>api('/api/play').then(refresh).catch(e=>say(e.message));
$('stop').onclick=()=>api('/api/stop').then(refresh).catch(e=>say(e.message));
$('bri').oninput=e=>{$('bval').textContent=e.target.value+'%';clearTimeout(briTimer);briTimer=setTimeout(()=>api('/api/brightness',{value:+e.target.value}).catch(e=>say(e.message)),150)};
$('del').onclick=()=>{if(confirm('保存済み動画を削除しますか？'))api('/api/video/delete').then(refresh).catch(e=>say(e.message))};
$('reboot').onclick=()=>{if(confirm('再起動しますか？'))api('/api/reboot').then(()=>say('再起動中…')).catch(e=>say(e.message))};
$('up').onclick=()=>{const f=$('file').files[0];if(!f){say('ファイルを選択してください');return}
 busy=true;$('up').disabled=true;$('prog').hidden=false;$('prog').value=0;say(`送信中 ${fmt(f.size)}`);
 const x=new XMLHttpRequest();x.open('POST','/api/video');x.setRequestHeader('Content-Type','application/octet-stream');x.timeout=600000;
 x.upload.onprogress=e=>{if(e.lengthComputable)$('prog').value=e.loaded/e.total*100};
 x.onload=()=>{busy=false;$('prog').hidden=true;let j={};try{j=JSON.parse(x.responseText)}catch(_){}
  say(x.status===200?`完了: ${j.frames} フレーム / ${(j.duration_s||0).toFixed(1)} 秒`:`失敗: ${j.error||('HTTP '+x.status)}`);refresh()};
 x.onerror=x.ontimeout=()=>{busy=false;$('prog').hidden=true;say('送信エラー (接続が切れました)');refresh()};
 x.send(f)};
if(/[?&]cna=1/.test(location.search)){$('cna').hidden=false;$('cnaUrl').textContent=location.origin+'/'}
refresh();setInterval(refresh,2000);
</script></body></html>)HTML";

}  // namespace

// ---------------------------------------------------------------------------

SoloWebServer::SoloWebServer()
    : _server(nullptr),
      _config(nullptr),
      _player(nullptr),
      _led(nullptr),
      _rxBuf(nullptr),
      _brightnessPct(50),
      _rebootAtMs(0),
      _uploads(0),
      _uploadFailures(0) {
    _jsonBuf[0] = '\0';
}

SoloWebServer::~SoloWebServer() {
    end();
    if (_rxBuf) {
        free(_rxBuf);
        _rxBuf = nullptr;
    }
}

bool SoloWebServer::begin(ConfigManager& config, SoloPlayer& player, LEDManager& led, uint16_t port) {
    if (_server) {
        return true;
    }
    _config = &config;
    _player = &player;
    _led = &led;
    _brightnessPct = config.getParamBrightness();

    if (!_rxBuf) {
        _rxBuf = (uint8_t*)malloc(kRxBufSize);
        if (!_rxBuf) {
            Serial.println("[SoloWeb] ERROR: rx buffer allocation failed");
            return false;
        }
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = port;
    cfg.core_id = 0;               // WiFi/lwIP・再生タスクと同じ Core0。描画(Core1)を汚さない
    cfg.task_priority = 2;         // 既定(5)は高すぎるので描画タスクと同等まで下げる
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 12;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 30;
    cfg.send_wait_timeout = 30;

    if (httpd_start(&_server, &cfg) != ESP_OK) {
        Serial.println("[SoloWeb] ERROR: httpd_start failed");
        _server = nullptr;
        return false;
    }

    const httpd_uri_t routes[] = {
        {"/",                 HTTP_GET,  onRoot,       this, false, false, nullptr},
        {"/api/status",       HTTP_GET,  onStatus,     this, false, false, nullptr},
        {"/api/play",         HTTP_POST, onPlay,       this, false, false, nullptr},
        {"/api/stop",         HTTP_POST, onStop,       this, false, false, nullptr},
        {"/api/brightness",   HTTP_POST, onBrightness, this, false, false, nullptr},
        {"/api/video",        HTTP_POST, onUpload,     this, false, false, nullptr},
        {"/api/video/delete", HTTP_POST, onDelete,     this, false, false, nullptr},
        {"/api/reboot",       HTTP_POST, onReboot,     this, false, false, nullptr},
    };
    for (const auto& r : routes) {
        if (httpd_register_uri_handler(_server, &r) != ESP_OK) {
            Serial.printf("[SoloWeb] ERROR: failed to register %s\n", r.uri);
        }
    }
    httpd_register_err_handler(_server, HTTPD_404_NOT_FOUND, onNotFound);

    // キャプティブポータル用 DNS (全ホスト名 → AP の IP)
    _dns.setTTL(60);
    _dns.setErrorReplyCode(DNSReplyCode::NoError);
    _dnsStarted = _dns.start(53, "*", WiFi.softAPIP());
    if (!_dnsStarted) {
        Serial.println("[SoloWeb] WARN: captive DNS failed to start (UI still reachable by IP)");
    }

    Serial.printf("[SoloWeb] HTTP server listening on port %u (captive portal %s)\n",
                  (unsigned)port, _dnsStarted ? "on" : "off");
    return true;
}

void SoloWebServer::end() {
    if (_dnsStarted) {
        _dns.stop();
        _dnsStarted = false;
    }
    if (_server) {
        httpd_stop(_server);
        _server = nullptr;
    }
}

void SoloWebServer::loop() {
    if (_dnsStarted) {
        _dns.processNextRequest();
    }
    if (_rebootAtMs != 0 && (int32_t)(millis() - _rebootAtMs) >= 0) {
        Serial.println("[SoloWeb] Restarting as requested via HTTP");
        Serial.flush();
        delay(50);
        ESP.restart();
    }
}

// ---------------------------------------------------------------------------
// 共通ヘルパ
// ---------------------------------------------------------------------------

esp_err_t SoloWebServer::sendJson(httpd_req_t* req, const char* status, const char* json) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

esp_err_t SoloWebServer::sendError(httpd_req_t* req, const char* status, const char* message) {
    snprintf(_jsonBuf, sizeof(_jsonBuf), "{\"ok\":false,\"error\":\"%s\"}", message ? message : "error");
    return sendJson(req, status, _jsonBuf);
}

bool SoloWebServer::readBody(httpd_req_t* req, char* out, size_t cap, size_t& len) {
    len = 0;
    if (req->content_len >= cap) {
        return false;
    }
    while (len < req->content_len) {
        int r = httpd_req_recv(req, out + len, req->content_len - len);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            return false;
        }
        len += (size_t)r;
    }
    out[len] = '\0';
    return true;
}

void SoloWebServer::applyBrightness(uint8_t percent) {
    if (percent > 100) percent = 100;
    _brightnessPct = percent;
    if (_led) {
        _led->setBrightness((uint8_t)map(percent, 0, 100, 0, 255));
    }
}

void SoloWebServer::scheduleReboot(uint32_t delayMs) {
    _rebootAtMs = millis() + delayMs;
    if (_rebootAtMs == 0) _rebootAtMs = 1;
}

size_t SoloWebServer::maxUploadBytes(size_t& freeOut, size_t& existingOut) const {
    const size_t total = LittleFS.totalBytes();
    const size_t used = LittleFS.usedBytes();
    freeOut = (total > used) ? total - used : 0;
    existingOut = 0;
    if (_player) {
        fs::File f = LittleFS.open(_player->videoPath().c_str(), "r");
        if (f) {
            existingOut = f.size();
            f.close();
        }
    }
    // 既存動画は置換時に先に消せるので上限に含める
    const size_t avail = freeOut + existingOut;
    return (avail > kFsMargin) ? avail - kFsMargin : 0;
}

// ---------------------------------------------------------------------------
// ハンドラ
// ---------------------------------------------------------------------------

esp_err_t SoloWebServer::onRoot(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kIndexHtml, sizeof(kIndexHtml) - 1);
}

esp_err_t SoloWebServer::onStatus(httpd_req_t* req) {
    auto* self = static_cast<SoloWebServer*>(req->user_ctx);
    SoloPlayer& p = *self->_player;
    const SoloPlayer::Stats st = p.stats();

    size_t fsFree = 0, existing = 0;
    const size_t maxUpload = self->maxUploadBytes(fsFree, existing);
    const char* err = p.lastError();

    // error は静的ASCII文字列のみ (エスケープ不要)
    int n = snprintf(self->_jsonBuf, sizeof(self->_jsonBuf),
        "{\"device\":\"%s\",\"state\":\"%s\",\"error\":%s%s%s,"
        "\"video\":{\"present\":%s,\"path\":\"%s\",\"bytes\":%u,\"frames\":%u,\"duration_s\":%.1f,"
        "\"width\":%u,\"height\":%u,\"max_frame_bytes\":%u},"
        "\"brightness\":%u,\"fps_target\":%u,"
        "\"stats\":{\"fps\":%.2f,\"frames\":%u,\"loops\":%u,\"deadline_misses\":%u,\"decode_errors\":%u,"
        "\"last_frame_bytes\":%u,\"read_us\":%u,\"tick_us\":%u},"
        "\"fs\":{\"total\":%u,\"used\":%u,\"free\":%u,\"max_upload\":%u},"
        "\"ap\":{\"ssid\":\"%s\",\"ip\":\"%s\",\"clients\":%u},"
        "\"uploads\":%u,\"upload_failures\":%u,"
        "\"uptime_s\":%lu,\"heap_free\":%u,\"psram_free\":%u}",
        self->_config->getSphereID().c_str(), p.stateName(),
        err ? "\"" : "null", err ? err : "", err ? "\"" : "",
        p.hasVideo() ? "true" : "false", p.videoPath().c_str(), (unsigned)p.videoBytes(),
        (unsigned)p.videoFrames(), (float)p.videoFrames() / (float)kSoloFps,
        (unsigned)p.width(), (unsigned)p.height(), (unsigned)p.maxFrameBytes(),
        (unsigned)self->_brightnessPct, (unsigned)kSoloFps,
        st.fps, (unsigned)st.frames, (unsigned)st.loops, (unsigned)st.deadlineMisses,
        (unsigned)st.decodeErrors, (unsigned)st.lastFrameBytes, (unsigned)st.lastReadUs,
        (unsigned)st.lastTickUs,
        (unsigned)LittleFS.totalBytes(), (unsigned)LittleFS.usedBytes(), (unsigned)fsFree,
        (unsigned)maxUpload,
        WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str(),
        (unsigned)WiFi.softAPgetStationNum(),
        (unsigned)self->_uploads, (unsigned)self->_uploadFailures,
        (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
    if (n < 0 || (size_t)n >= sizeof(self->_jsonBuf)) {
        return self->sendError(req, "500 Internal Server Error", "status too large");
    }
    return self->sendJson(req, "200 OK", self->_jsonBuf);
}

esp_err_t SoloWebServer::onPlay(httpd_req_t* req) {
    auto* self = static_cast<SoloWebServer*>(req->user_ctx);
    if (self->_player->state() == SoloPlayer::State::Uploading) {
        return self->sendError(req, "409 Conflict", "upload in progress");
    }
    if (!self->_player->hasVideo()) {
        return self->sendError(req, "409 Conflict", "no video");
    }
    self->_player->play();
    snprintf(self->_jsonBuf, sizeof(self->_jsonBuf), "{\"ok\":true,\"state\":\"%s\"}", self->_player->stateName());
    return self->sendJson(req, "200 OK", self->_jsonBuf);
}

esp_err_t SoloWebServer::onStop(httpd_req_t* req) {
    auto* self = static_cast<SoloWebServer*>(req->user_ctx);
    if (self->_player->state() == SoloPlayer::State::Uploading) {
        return self->sendError(req, "409 Conflict", "upload in progress");
    }
    self->_player->stop();
    snprintf(self->_jsonBuf, sizeof(self->_jsonBuf), "{\"ok\":true,\"state\":\"%s\"}", self->_player->stateName());
    return self->sendJson(req, "200 OK", self->_jsonBuf);
}

esp_err_t SoloWebServer::onBrightness(httpd_req_t* req) {
    auto* self = static_cast<SoloWebServer*>(req->user_ctx);
    char body[128];
    size_t len = 0;
    if (!self->readBody(req, body, sizeof(body), len)) {
        return self->sendError(req, "400 Bad Request", "invalid body");
    }
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, body, len) != DeserializationError::Ok || !doc.containsKey("value")) {
        return self->sendError(req, "400 Bad Request", "expected {\\\"value\\\":0-100}");
    }
    int v = doc["value"] | -1;
    if (v < 0 || v > 100) {
        return self->sendError(req, "400 Bad Request", "value out of range (0-100)");
    }
    self->applyBrightness((uint8_t)v);
    snprintf(self->_jsonBuf, sizeof(self->_jsonBuf), "{\"ok\":true,\"brightness\":%d}", v);
    return self->sendJson(req, "200 OK", self->_jsonBuf);
}

esp_err_t SoloWebServer::onUpload(httpd_req_t* req) {
    auto* self = static_cast<SoloWebServer*>(req->user_ctx);
    return self->doUpload(req);
}

esp_err_t SoloWebServer::doUpload(httpd_req_t* req) {
    const size_t len = req->content_len;
    if (len == 0) {
        return sendError(req, "400 Bad Request", "empty body");
    }

    size_t fsFree = 0, existing = 0;
    const size_t maxUpload = maxUploadBytes(fsFree, existing);
    if (len > maxUpload) {
        snprintf(_jsonBuf, sizeof(_jsonBuf),
                 "{\"ok\":false,\"error\":\"insufficient storage\",\"content_length\":%u,\"max_upload\":%u,\"free\":%u}",
                 (unsigned)len, (unsigned)maxUpload, (unsigned)fsFree);
        return sendJson(req, "507 Insufficient Storage", _jsonBuf);
    }
    // 一時ファイル + 既存動画 の二重保持ができない場合だけ、先に既存動画を消す
    const bool deleteFirst = (len + kFsMargin > fsFree);

    if (!_player->beginUpload()) {
        return sendError(req, "409 Conflict", "upload already in progress");
    }

    const String videoPath = _player->videoPath();
    if (deleteFirst && LittleFS.exists(videoPath.c_str())) {
        Serial.println("[SoloWeb] Removing current video to make room for upload");
        LittleFS.remove(videoPath.c_str());
    }
    if (LittleFS.exists(kTmpPath)) {
        LittleFS.remove(kTmpPath);
    }

    const char* err = nullptr;
    size_t received = 0;
    {
        fs::File f = LittleFS.open(kTmpPath, "w");
        if (!f) {
            err = "failed to create temp file";
        } else {
            size_t remaining = len;
            int timeouts = 0;
            while (remaining > 0) {
                const size_t want = remaining < kRxBufSize ? remaining : kRxBufSize;
                const int r = httpd_req_recv(req, (char*)_rxBuf, want);
                if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                    if (++timeouts > kRecvTimeoutRetries) {
                        err = "receive timeout";
                        break;
                    }
                    continue;
                }
                if (r <= 0) {
                    err = "connection lost during upload";
                    break;
                }
                timeouts = 0;
                if (f.write(_rxBuf, (size_t)r) != (size_t)r) {
                    err = "write failed (storage full?)";
                    break;
                }
                remaining -= (size_t)r;
                received += (size_t)r;
            }
            f.close();
        }
    }

    MjpegInfo info;
    if (!err) {
        const char* verr = nullptr;
        if (!_player->validateFile(kTmpPath, info, &verr)) {
            err = verr ? verr : "invalid video";
        }
    }
    if (!err) {
        if (LittleFS.exists(videoPath.c_str())) {
            LittleFS.remove(videoPath.c_str());
        }
        if (!LittleFS.rename(kTmpPath, videoPath.c_str())) {
            err = "failed to install video";
        }
    }

    if (err) {
        LittleFS.remove(kTmpPath);
        _uploadFailures++;
        _player->endUpload();  // 旧動画が残っていればそれを再生、無ければ no_video
        Serial.printf("[SoloWeb] Upload failed: %s (received %u/%u bytes)\n", err, (unsigned)received, (unsigned)len);
        snprintf(_jsonBuf, sizeof(_jsonBuf),
                 "{\"ok\":false,\"error\":\"%s\",\"received\":%u,\"expected\":%u,\"replaced_before_upload\":%s,"
                 "\"got_width\":%u,\"got_height\":%u,\"state\":\"%s\"}",
                 err, (unsigned)received, (unsigned)len, deleteFirst ? "true" : "false",
                 (unsigned)info.width, (unsigned)info.height, _player->stateName());
        sendJson(req, "400 Bad Request", _jsonBuf);
        // 受信途中で切れた場合は本文が残っているので接続を閉じさせる
        return (received == len) ? ESP_OK : ESP_FAIL;
    }

    _uploads++;
    _player->endUpload();  // 新しい動画を先頭から再生
    Serial.printf("[SoloWeb] Upload ok: %u frames, %u bytes, max frame %u\n",
                  (unsigned)info.frames, (unsigned)info.fileBytes, (unsigned)info.maxFrameBytes);
    snprintf(_jsonBuf, sizeof(_jsonBuf),
             "{\"ok\":true,\"frames\":%u,\"bytes\":%u,\"max_frame_bytes\":%u,\"duration_s\":%.1f,"
             "\"replaced_before_upload\":%s,\"state\":\"%s\"}",
             (unsigned)info.frames, (unsigned)info.fileBytes, (unsigned)info.maxFrameBytes,
             (float)info.frames / (float)kSoloFps, deleteFirst ? "true" : "false", _player->stateName());
    return sendJson(req, "200 OK", _jsonBuf);
}

esp_err_t SoloWebServer::onDelete(httpd_req_t* req) {
    auto* self = static_cast<SoloWebServer*>(req->user_ctx);
    if (!self->_player->beginUpload()) {
        return self->sendError(req, "409 Conflict", "upload in progress");
    }
    const String path = self->_player->videoPath();
    bool removed = false;
    if (LittleFS.exists(path.c_str())) {
        removed = LittleFS.remove(path.c_str());
    }
    self->_player->endUpload();
    snprintf(self->_jsonBuf, sizeof(self->_jsonBuf), "{\"ok\":true,\"removed\":%s,\"state\":\"%s\"}",
             removed ? "true" : "false", self->_player->stateName());
    return self->sendJson(req, "200 OK", self->_jsonBuf);
}

esp_err_t SoloWebServer::onReboot(httpd_req_t* req) {
    auto* self = static_cast<SoloWebServer*>(req->user_ctx);
    self->_player->stop();
    self->scheduleReboot(500);
    return self->sendJson(req, "200 OK", "{\"ok\":true,\"reboot_in_ms\":500}");
}

esp_err_t SoloWebServer::onNotFound(httpd_req_t* req, httpd_err_code_t) {
    if (strncmp(req->uri, "/api/", 5) == 0) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"ok\":false,\"error\":\"not found\"}", HTTPD_RESP_USE_STRLEN);
    }
    // キャプティブポータル検出 (iOS: /hotspot-detect.html, Android: /generate_204,
    // Windows: /connecttest.txt など) を含む未知パスは UI へリダイレクト。
    // 期待される "Success" 応答が返らないため OS が「ネットワークにログイン」画面を開く。
    char location[64];
    snprintf(location, sizeof(location), "http://%s/?cna=1", WiFi.softAPIP().toString().c_str());
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "redirect", HTTPD_RESP_USE_STRLEN);
}

}  // namespace sastle
