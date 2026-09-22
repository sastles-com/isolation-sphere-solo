/**
 * @file main.cpp
 * @brief isolation-sphere solo: 外部 server 無しで球体単独再生するファームウェア
 *
 * 起動すると SoftAP を立て、LittleFS 上の raw MJPEG (320x160, 10fps) を自動ループ再生する。
 * 操作は本体 LCD の QR から iPhone を接続し、Web UI (http://192.168.4.1/) で行う。
 *
 * タスク構成:
 *   Core0: WiFi/lwIP, solo_play (再生: ファイル読み+JPEGデコード), httpd (Web UI/アップロード)
 *   Core1: LED_Render (IMU姿勢で毎パス再マッピング + RMT出力), loopTask (本ファイルの loop)
 */

#include <Arduino.h>
#include <WiFi.h>
#include "common.h"
#include "FileManager.h"
#include "ConfigManager.h"
#include "NetworkManager.h"
#include "IMUManager.h"
#include "GestureManager.h"
#include "SoundManager.h"
#include "ImageManager.h"
#include "LEDManager.h"
#include "LCDManager.h"
#include "OtaManager.h"
#include "Settings.h"

#include "SoloPlayer.h"
#include "SoloWebServer.h"

using namespace sastle;

// グローバルデバッグフラグ (common.hでextern宣言)
bool g_debugEnabled = false;

ConfigManager config;
NetworkManager network;
IMUManager imuSensor;
GestureManager gesture;
SoundManager sound;
ImageManager imageManager;
LEDManager ledManager;
LCDManager lcdManager;
OtaManager ota;
SoloPlayer soloPlayer;
SoloWebServer soloWeb;

// LCD に出す Wi-Fi 接続 QR ("WIFI:T:WPA;S:..;P:..;;") と表示用文字列
static String g_wifiQrText;
static String g_apSsid;
static String g_uiUrl;

unsigned long lastIMULog = 0;
const unsigned long IMU_LOG_INTERVAL = 3000; // 3秒に1回ログ出力
unsigned long lastPerfLog = 0;
const unsigned long PERF_LOG_INTERVAL = 2000; // 2秒に1回 性能計測ログ

// Grove I2C バスを走査して検出アドレスを出す (診断用)。
// BNO055 は ADR ピンにより 0x28 (L) / 0x29 (H) に応答する。
static void scanI2cBus(uint8_t sda, uint8_t scl) {
    Wire.begin(sda, scl, 400000);
    delay(50);

    sastle::Log.printf("\n[I2C] Scanning bus (SDA=GPIO%u, SCL=GPIO%u)\n", sda, scl);

    int found = 0;
    bool bnoFound = false;
    for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            ++found;
            const bool isBno = (addr == 0x28 || addr == 0x29);
            if (isBno) {
                bnoFound = true;
            }
            sastle::Log.printf("[I2C]   found 0x%02X%s\n", addr, isBno ? "  <- BNO055" : "");
            delay(2);
        }
    }

    sastle::Log.printf("[I2C] Scan done: %d device(s), BNO055(0x28/0x29)=%s\n",
                       found, bnoFound ? "PRESENT" : "ABSENT");
}

void setup() {
    // 最優先: LED即時消灯。WS2812は起動中のデータ線ノイズでランダム点灯
    // (白=フル電流もあり得る) し、電源が引き倒されて起動に失敗する事象があるため、
    // 何よりも先に全ストリップへ黒を送信する。
    LEDManager::earlyBlank();

    // シリアル初期化
    Serial.begin(115200);

    // Sound初期化を最優先で行い、起動音を即再生する。
    // 理由: WiFi/LCD初期化が終わるまでLED表示は数秒かかるため、
    // 電源スイッチが正しくONになったかをすぐ確認できるよう、
    // 他の初期化より前に音でフィードバックする。
    bool soundReady = sound.begin(config);
    if (soundReady) {
        sound.playEffect(SoundEffect::STARTUP);
    }

    // シリアルモニタ接続待ち。USB ホストが繋がっているときだけ短く待つ (起動ログの先頭を
    // 取りこぼさないため)。電池駆動 (ホスト無し) では待たない。
    if (Serial.isConnected()) {
        delay(300);
    }

    if (!soundReady) {
        sastle::Log.println("Sound initialization failed (continuing without sound)");
    }

    sastle::Log.println("\n\n=== Isolation Sphere (solo) ===");

    // 直前のリセット理由を記録 (勝手な再起動の原因切り分け用)
    // 1=POWERON 3=SW 4=PANIC 5/6=WDT 7=TASK_WDT 9=BROWNOUT (esp_reset_reason_t)
    {
        esp_reset_reason_t rr = esp_reset_reason();
        const char* rrName;
        switch (rr) {
            case ESP_RST_POWERON:  rrName = "POWERON"; break;
            case ESP_RST_SW:       rrName = "SW_RESTART"; break;
            case ESP_RST_PANIC:    rrName = "PANIC"; break;
            case ESP_RST_INT_WDT:  rrName = "INT_WDT"; break;
            case ESP_RST_TASK_WDT: rrName = "TASK_WDT"; break;
            case ESP_RST_WDT:      rrName = "WDT"; break;
            case ESP_RST_BROWNOUT: rrName = "BROWNOUT"; break;
            case ESP_RST_DEEPSLEEP:rrName = "DEEPSLEEP"; break;
            default:               rrName = "OTHER"; break;
        }
        sastle::Log.printf("[BOOT] Reset reason: %d (%s)\n", (int)rr, rrName);
    }

    // PSRAM初期化確認
    if (psramFound()) {
        sastle::Log.printf("PSRAM found: %d bytes\n", ESP.getPsramSize());
        sastle::Log.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
    } else {
        sastle::Log.println("PSRAM not found");
    }

    // LittleFS初期化
    sastle::Log.println("\n=== Initializing LittleFS ===");
    // LittleFS が壊れていても停止しない。SoftAP と OTA を必ず立ち上げ、無線で書き戻せる
    // 状態を保つ (組み立て後は USB 端子に触れないため、ここで止まると復旧手段が無くなる)。
    const bool fsReady = FileManager::begin();
    if (!fsReady) {
        sastle::Log.println("FileManager initialization FAILED "
                            "(no video / uploads disabled, but SoftAP+OTA will start)");
    } else {
        FileManager::printInfo();
    }

    // ConfigManager初期化とロード
    sastle::Log.println("\n=== Loading Configuration ===");
    // 同様に、設定が読めなくても停止しない。ConfigManager の各 getter は
    // コンパイル時の既定値 (320x160 / SoftAP 既定 SSID など) を返す。
    if (!fsReady || !config.loadConfig("/config.json")) {
        sastle::Log.println("Failed to load config (using compiled-in defaults)");
    }
    config.printConfig();

    // UI で変えた設定 (明るさ等) を NVS から復元する
    sastle::Settings::begin();

    // SoftAP を立てる。失敗しても停止しない (動画があれば iPhone 無しでも
    // 自動再生する要件)。
    SoloConfig soloCfg = config.getSoloConfig();
    IPAddress apIp;
    if (!apIp.fromString(soloCfg.ap_ip)) {
        apIp = IPAddress(192, 168, 4, 1);
    }
    if (!network.beginSoftAP(soloCfg.ap_ssid, soloCfg.ap_password, apIp)) {
        sastle::Log.println("SoftAP start FAILED (playback continues without Web UI)");
    }
    // LCD に出す接続 QR。カメラで読むと iPhone が AP 接続を提案し、接続後は
    // キャプティブポータル検出で Web UI が自動的に開く。
    g_apSsid = soloCfg.ap_ssid;
    g_wifiQrText = NetworkManager::wifiQrText(soloCfg.ap_ssid, soloCfg.ap_password);
    g_uiUrl = "http://" + apIp.toString() + "/";
    sastle::Log.printf("[SOLO] Wi-Fi QR: %s  UI: %s\n", g_wifiQrText.c_str(), g_uiUrl.c_str());

    // 任意の STA 併用 (AP+STA)。資格情報は NVS に置く (Web UI の「LAN 接続」から設定)。
    // 目的は OTA: 普段の LAN に居れば PC の Wi-Fi を切り替えずに espota できる。
    network.beginStaFromStore(soloCfg.ap_ssid);

    // OTA (espota) 初期化: AP が立った直後に受け口を開く。これ以降の初期化
    // (IMU / LED / 再生 / Web) で失敗・停止しても、無線での書き戻しは生き残る。
    // stopRenderTask() は _taskRunning ガードがあるため未初期化でも安全。
    ota.begin(&ledManager);

    // IMU初期化前に I2C バスを走査 (BNO055 の有無を切り分けるため)
    scanI2cBus(kImuI2cSda, kImuI2cScl);

    // IMU初期化
    if (!imuSensor.begin(config)) {
        sastle::Log.println("IMU initialization failed (continuing without IMU)");
    } else {
        imuSensor.printStatus();
    }

    // ジェスチャー初期化 (サウンドフィードバック付き)
    if (imuSensor.isInitialized()) {
        if (!gesture.begin(imuSensor, &sound)) {
            sastle::Log.println("Gesture initialization failed");
        }
    } else {
        sastle::Log.println("Gesture disabled (IMU not available)");
    }

    // ImageManager初期化 (PSRAM トリプルバッファ)
    if (!imageManager.begin(config)) {
        sastle::Log.println("ImageManager initialization failed (continuing without image)");
    } else {
        imageManager.printStats();
    }

    // LCDManager初期化 (デバッグモード)
    if (!lcdManager.begin(&config)) {
        DEBUG_PRINTLN("[Setup] LCDManager initialization failed");
    }

    // LEDManager初期化 (IMUManager連携)
    if (imageManager.isInitialized()) {
        IMUManager* imuPtr = imuSensor.isInitialized() ? &imuSensor : nullptr;
        if (!ledManager.begin(config, imageManager, imuPtr)) {
            sastle::Log.println("LEDManager initialization failed (continuing without LED)");
        } else {
            ledManager.printStatus();

            // 起動オープニングパターン (config でスキップ可)
            if (config.getOpeningActionEnabled()) {
                ledManager.playOpening(config.getOpeningActionDurationMs());
            } else {
                ledManager.fillSolid(0, 0, 0);
                ledManager.show();
            }

            // config の params.brightness (0-100%) を LED 輝度 (0-255) へ適用
            ledManager.setBrightness((uint8_t)map(sastle::Settings::brightness(config.getParamBrightness()), 0, 100, 0, 255));

            // レンダリングタスク開始 (Core 1)
            if (!ledManager.startRenderTask(1, 2, 8192)) {
                sastle::Log.println("Failed to start LED render task");
            }
        }
    } else {
        sastle::Log.println("LEDManager disabled (ImageManager not available)");
    }

    // 再生タスク (Core 0) と Web UI を開始。
    // 再生は LittleFS 読み出し + JPEG デコードを 100ms 締切で回し、描画 (Core1) とは
    // トリプルバッファ経由で独立に動く。
    if (imageManager.isInitialized()) {
        if (soloPlayer.begin(config, imageManager)) {
            soloPlayer.startTask(0, 1, 6144);
        } else {
            sastle::Log.println("SoloPlayer initialization failed");
        }
        if (network.isSoftAP()) {
            if (!soloWeb.begin(config, soloPlayer, ledManager, network, config.getSoloHttpPort())) {
                sastle::Log.println("Web server failed to start");
            }
        }
    }

    sastle::Log.println("\n=== Setup Complete ===");
}

// シリアルコンソール: Web UI に繋げない状況でも状態確認と操作ができる復帰経路。
// 1行コマンド (改行終端): help | status | play | stop | led sphere|test | reboot
static void serialConsolePoll() {
    static char line[64];
    static size_t len = 0;

    while (Serial.available() > 0) {
        const char c = (char)Serial.read();
        if (c == '\r') {
            continue;
        }
        if (c != '\n') {
            if (len < sizeof(line) - 1) {
                line[len++] = c;
            }
            continue;
        }
        line[len] = '\0';
        len = 0;

        char* cmd = line;
        while (*cmd == ' ') cmd++;
        if (*cmd == '\0') {
            continue;
        }

        if (strcmp(cmd, "help") == 0) {
            Serial.println("[CONSOLE] commands: status | play | stop | led sphere|test | reboot");
        } else if (strcmp(cmd, "status") == 0) {
            Serial.printf("[CONSOLE] ap=%s ip=%s clients=%u heap=%u psram=%u\n",
                          g_apSsid.c_str(), network.apIP().toString().c_str(),
                          (unsigned)network.clientCount(),
                          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
            SoloPlayer::Stats s = soloPlayer.stats();
            Serial.printf("[CONSOLE] state=%s video=%s frames=%u fps=%.1f miss=%u error=%s\n",
                          soloPlayer.stateName(), soloPlayer.videoPath().c_str(),
                          (unsigned)soloPlayer.videoFrames(), s.fps, (unsigned)s.deadlineMisses,
                          soloPlayer.lastError() ? soloPlayer.lastError() : "-");
        } else if (strcmp(cmd, "play") == 0) {
            soloPlayer.play();
            Serial.printf("[CONSOLE] state=%s\n", soloPlayer.stateName());
        } else if (strcmp(cmd, "stop") == 0) {
            soloPlayer.stop();
            Serial.printf("[CONSOLE] state=%s\n", soloPlayer.stateName());
        } else if (strcmp(cmd, "led sphere") == 0) {
            ledManager.setOutputMode(LEDManager::OutputMode::Sphere);
            Serial.println("[CONSOLE] led output = sphere (video + IMU)");
        } else if (strcmp(cmd, "led test") == 0) {
            ledManager.setOutputMode(LEDManager::OutputMode::Test);
            Serial.println("[CONSOLE] led output = test pattern");
        } else if (strcmp(cmd, "reboot") == 0) {
            Serial.println("[CONSOLE] rebooting...");
            Serial.flush();
            delay(100);
            ESP.restart();
        } else {
            Serial.printf("[CONSOLE] unknown command: %s (try 'help')\n", cmd);
        }
    }
}

// 性能計測ログ (2秒間隔): 描画/デコード時間と再生統計
static void logPerfIfDue(unsigned long now) {
    if (now - lastPerfLog < PERF_LOG_INTERVAL) {
        return;
    }
    lastPerfLog = now;

    LEDStats led = ledManager.getStats();
    ImageStats img = imageManager.getStats();
    SoloPlayer::Stats s = soloPlayer.stats();
    sastle::Log.printf(
        "[PERF] render_fps=%.1f map=%luus out=%luus | img_fps=%.1f decode=%luus jpeg=%uB drop=%lu | heap=%u\n",
        led.fps, (unsigned long)led.mapping_time_us, (unsigned long)led.output_time_us,
        img.fps, (unsigned long)img.decode_time_us, (unsigned)img.last_jpeg_size,
        (unsigned long)imageManager.getDropped(), (unsigned)ESP.getFreeHeap());
    sastle::Log.printf(
        "[SOLO] state=%s fps=%.1f frames=%lu loops=%lu miss=%lu decode_err=%lu read=%luus tick=%luus clients=%u\n",
        soloPlayer.stateName(), s.fps, (unsigned long)s.frames, (unsigned long)s.loops,
        (unsigned long)s.deadlineMisses, (unsigned long)s.decodeErrors,
        (unsigned long)s.lastReadUs, (unsigned long)s.lastTickUs,
        (unsigned)network.clientCount());
}

void loop() {
    // --- 実効レート計装 (5秒ごと) ---
    {
        static uint32_t s_loops = 0, s_pReads = 0, s_pFails = 0, s_pDisc = 0;
        static unsigned long s_lastRate = 0;
        s_loops++;
        unsigned long nowR = millis();
        if (nowR - s_lastRate >= 5000) {
            uint32_t rt = imuSensor.debugReadTotal(), rf = imuSensor.debugReadFails(),
                     rd = imuSensor.debugDiscards();
            float dt = (nowR - s_lastRate) * 0.001f;
            sastle::Log.printf("[RATE] loop=%.0f/s imu_read=%.0f/s fail=%.0f/s disc=%.0f/s\n",
                               s_loops / dt, (rt - s_pReads) / dt,
                               (rf - s_pFails) / dt, (rd - s_pDisc) / dt);
            s_loops = 0; s_pReads = rt; s_pFails = rf; s_pDisc = rd;
            s_lastRate = nowR;
        }
    }

    // OTA 要求を最優先で処理。書き込みセッション中は handle() が転送完了まで
    // ブロックするため、描画・再生は自然に停止する。
    ota.handle();

    // キャプティブポータル DNS の応答と、HTTP 経由の再起動要求の実行
    network.poll();   // STA 接続状態の変化をログに出す
    soloWeb.loop();

    // シリアルコンソール
    serialConsolePoll();

    // LCD (デバッグ表示が有効な場合のみ)
    if (lcdManager.isDebugEnabled()) {
        if (network.clientCount() == 0) {
            // 端末が1台も繋がっていない間は接続用 QR を優先表示する
            lcdManager.drawWifiQr(g_wifiQrText.c_str(), g_apSsid.c_str(), g_uiUrl.c_str());
        } else if (!soloWeb.uiServed()) {
            // 接続済みだが UI をまだ開いていない: カメラで読むと Safari が開く URL QR を出す。
            // (キャプティブポータルを抑止しているため、UI は利用者が自分で開く)
            lcdManager.drawQr(g_uiUrl.c_str(), "Camera で読む", g_uiUrl.c_str());
        } else {
            // 再生中は映像、停止中/動画なしは STANDBY 画面
            static uint32_t s_lastFrames = 0;
            static unsigned long s_lastFrameMs = 0;
            unsigned long n = millis();
            uint32_t fr = imageManager.getStats().frames_decoded;
            if (fr != s_lastFrames) {
                s_lastFrames = fr;
                s_lastFrameMs = n;
            }
            // 映像未再生なら起動3秒後にSTANDBY、再生後は途切れ1.5秒でSTANDBY
            bool idle = (s_lastFrames == 0) ? (n > 3000) : ((n - s_lastFrameMs) > 1500);
            if (idle) {
                LcdStatus st;
                st.uptime_s = n / 1000;
                static String ipStr;
                ipStr = network.apIP().toString();
                st.ip = ipStr.c_str();
                st.clients = network.clientCount();
                st.free_heap = ESP.getFreeHeap();
                st.fps = ledManager.getStats().fps;
                float qw, qx, qy, qz;
                st.imu_ok = imuSensor.getQuaternion(qw, qx, qy, qz);
                st.qw = qw; st.qx = qx; st.qy = qy; st.qz = qz;
                lcdManager.drawStatus(st);
            } else {
                lcdManager.update(&imageManager);
            }
        }
    }

    // IMU更新
    unsigned long now = millis();
    if (imuSensor.isInitialized()) {
        imuSensor.update();

        // ジェスチャー検出更新
        gesture.update();

        // 診断ログ (3秒に1回)
        if (now - lastIMULog >= IMU_LOG_INTERVAL) {
            lastIMULog = now;
            float w, x, y, z;
            if (imuSensor.getQuaternion(w, x, y, z)) {
                float gx, gy, gz, ax, ay, az;
                imuSensor.getGyro(gx, gy, gz);
                imuSensor.getAccel(ax, ay, az);
                uint8_t cs, cg, ca, cm;
                imuSensor.getCalibration(cs, cg, ca, cm);
                imu::Vector<3> eul = imuSensor.getEuler();
                sastle::Log.printf(
                    "[IMU] q=(%.3f,%.3f,%.3f,%.3f) eul=(%.1f,%.1f,%.1f) gyro=(%.2f,%.2f,%.2f) acc=(%.1f,%.1f,%.1f) cal=%u%u%u%u mode=%u\n",
                    w, x, y, z, (float)eul.x(), (float)eul.y(), (float)eul.z(),
                    gx, gy, gz, ax, ay, az, cs, cg, ca, cm, imuSensor.getOperationMode());
            }
        }
    }

    // 性能計測ログ
    logPerfIfDue(now);

    delay(10);
}
