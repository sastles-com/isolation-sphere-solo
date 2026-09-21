# solo 設計ノート

`isolation-sphere-solo` の設計判断、仕様、運用手順、実機チェック項目をまとめる。
使い方の概要は [../README.md](../README.md)、実装依頼時の仕様は [handoff.md](handoff.md) を参照。

---

## 1. 派生元の調査結果 (実装前に実コードで確認した事実)

本リポジトリは [sastles-com/sastle-isolation-sphere](https://github.com/sastles-com/sastle-isolation-sphere)
の `core/` (ESP32-S3 ファームウェア) を solo 専用に切り出したもの。切り出しの判断根拠として、
派生元のコードから確認した事実を残す (ハンドオフ資料は最新コードを読めていない前提で書かれていた)。

| 項目 | 事実 |
| --- | --- |
| ビルド | PlatformIO + Arduino framework、`board = m5stack-atoms3` (flash 8MB, PSRAM OPI) |
| パーティション | nvs / otadata / phy / ota_0 1.5MB / ota_1 1.5MB / spiffs(LittleFS) 3MB。末尾 約1.94MB が未使用 |
| **ファームサイズ** | **派生元をそのままビルドすると 1,723,509 bytes (1.5MB スロットの 109.6%) で収まらない**。release + `CORE_DEBUG_LEVEL=1` でも 1,594,145 bytes (101.4%)。この超過は solo 化前から存在していた |
| フレーム供給 | server → UDP チャンク (16B ヘッダ, ≤1400B) → 再構成 → TJpg_Decoder → RGB565 → トリプルバッファ |
| デコーダ | `bodmer/TJpg_Decoder` (Baseline JPEG のみ)、出力 RGB565、`setSwapBytes(false)`。デコードは Core0 |
| 描画 | FastLED RMT 並列出力、レンダタスクが Core1 prio2 で連続駆動 (~50-60Hz)。毎パス最新 IMU で再マッピングし、**フレーム到着に依存しない** |
| IMU | 100Hz 更新。`loop()` タスクで取得し、レンダタスクが読む |
| 制御 | MQTT (PubSubClient) を `loop()` で駆動、`CommandHandler` が params/playback/led/system を処理 |
| 設定 | LittleFS `/config.json` を ArduinoJson で読み書き。NVS/Preferences は未使用。**デバイスレベルの「モード」概念は無かった** (LED 出力モード sphere/pixels/off/test のみ) |
| LCD | M5AtomS3R 128×128、M5GFX (LovyanGFX)。LovyanGFX は `qrcode()` を内蔵 |
| デバイス内 HTTP / DNS | **無し**。`esp_http_server` と `DNSServer` は ESP-IDF / arduino-esp32 に同梱 |
| ローカル再生 | **無し**。`data/images/*.jpg` は LittleFS に載っていたがファームは読んでいなかった (1.2MB の無駄) |
| 既存ビルドエラー | `LEDManager.cpp` の `memset(CRGB[],…)` が FastLED 3.10 で曖昧呼び出しになり失敗。XIAO env は `kLedMaxPowerMa` 未定義で失敗 (いずれも本リポジトリで修正済み) |

**切り出しが成立する根拠**: 描画エンジンはフレームの供給元を知らない (トリプルバッファから読むだけ) ため、
「UDP 受信タスク」を「ローカルファイル読み出しタスク」に差し替えるだけで単独再生が成立する。
球面 UV マッピングと IMU 再投影は元々デバイス側で完結していた。

## 2. 採用した設計判断

- **供給元の差し替え**: `ImageManager::submitJpegFrame()` を入口とし、`SoloPlayer` タスクが LittleFS から
  読んだ JPEG を流す。描画・IMU・LED・球面マッピングは派生元のまま無変更。
- **動画形式 = raw MJPEG** (Baseline JPEG の連結、コンテナ無し)。AVI/multipart は受理しない。
  既存デコーダ (TJpg_Decoder) と同じ JPEG をそのまま使えるのが理由。
- **フレーム境界はマーカー追跡で判定** ([../src/JpegScan.h](../src/JpegScan.h))。`0xFFD9` の線形探索だと
  EXIF サムネイル (APP1) 内の EOI を誤検出するため、セグメント長を辿って SOI..EOI を確定させる。
  Arduino 非依存のヘッダオンリー実装にして PC 上で単体テストする。
- **10fps はファームウェア固定** (`kSoloFps`)。ファイル内の fps メタデータは参照しない。
- **締切管理は単調クロック** (`esp_timer_get_time`)。「デコード後に 100ms 待つ」方式ではなく締切を
  管理し、遅れた締切は捨てて次に揃える (処理時間によるドリフトと遅延の蓄積を避ける)。
- **1 フレーム上限 64KiB** (`kSoloMaxFrameBytes`)。ファイル内のサイズ値を無検証でメモリ確保に使わず、
  固定サイズの PSRAM バッファを使う。
- **UI はファームウェア埋め込み** (PROGMEM, 約 6KB)。LittleFS が壊れていても管理 UI を出して
  再アップロードできる。CDN・外部フォントには依存しない。
- **HTTP は ESP-IDF 同梱 `esp_http_server`、DNS は arduino-esp32 同梱 `DNSServer`** — 追加ライブラリ依存なし。
- **接続導線は QR + キャプティブポータル** (§6)。NFC は iOS が NFC 経由の Wi-Fi 参加に対応しないため不採用。
- **パーティション: OTA 2MB×2 + LittleFS 3.94MB** (§5)。OTA は削らず拡張し、派生元から存在した
  「ファームが OTA スロットに収まらない」問題も解消する。
- **アップロードは 一時ファイル → 全件検証 → rename**。容量が足りない場合のみ既存動画を先に削除し
  (UI に明記)、失敗時は「動画なし」で再アップロードできる状態にする。
- **server モード関連は削除** (MQTT 制御, UDP 映像受信, 時刻同期, リモートログ)。切り替えではなく
  削除にしたのは、単独動作に特化した独立リポジトリとして見通しを良くするため。server 構成が必要な
  場合は派生元リポジトリを使う。

### 削除したもの (派生元との差分)

| 削除 | 行数 | 理由 |
| --- | --- | --- |
| `MQTTManager.{h,cpp}`, `MqttTopics.h` | 421 | 外部ブローカーに依存しない。制御は HTTP API |
| `CommandHandler.{h,cpp}` | 602 | MQTT JSON コマンドの受け口。明るさ適用は `main.cpp` と Web API へ移動 |
| `TimeSync.{h,cpp}` | 154 | 複数球体の時刻同期ビーコン (server 前提) |
| `FrameReassembler.h` | 127 | UDP チャンク再構成 |
| `RemoteLog.{h,cpp}` | 161 | MQTT への tee ロガー → `Log.{h,cpp}` (Serial のみ, 14行) に置換 |
| `NetworkManager` の STA/UDP | — | SoftAP 専用に縮小 |
| `ImageManager` の UDP 経路 | — | `submitJpegFrame()` のみを入口に |
| `GestureManager` の MQTT 通知 | — | 検出結果は Serial ログへ |
| `server/` (Python + React), server 向けドキュメント・スクリプト | — | 本リポジトリの対象外 |
| `data/images/` (未参照の JPEG 200枚) | 1.2MB | ファームが読んでいなかった。LittleFS を動画に使う |

結果: ファーム 1,822,841 → **1,734,465 bytes** (約 88KB 減)、RAM 80,588 → **68,484 bytes** (約 12KB 減)、
LittleFS の同梱物 1.35MB → **39KB**。

## 3. アーキテクチャ

| 境界 | 実装 |
| --- | --- |
| ネットワーク | SoftAP (既定 `isolation-sphere` / 192.168.4.1) + キャプティブ DNS (全ホスト名 → 自分) |
| 映像入力 | `SoloPlayer` (Core0) が LittleFS `/video.mjpg` を 100ms 締切で読み、`ImageManager::submitJpegFrame()` へ |
| 制御入力 | `SoloWebServer` (esp_http_server, Core0) / シリアルコンソール (`main.cpp`) |
| 表示 | `LEDManager` レンダタスク (Core1) + IMU 再マッピング (派生元のまま) |
| ログ | Serial のみ (`sastle::Log`) |
| 更新 | USB (`upload` / `uploadfs`) と OTA (espota, AP 経由) |

### タスク配置

| タスク | Core | prio | stack | 役割 |
| --- | --- | --- | --- | --- |
| `LED_Render` | 1 | 2 | 8192 | IMU 再マッピング + RMT 出力 (~50-60Hz) |
| `solo_play` | 0 | 1 | 6144 | 100ms 締切で MJPEG 読み出し + JPEG デコード + publish |
| `httpd` | 0 | 2 | 8192 | Web UI / API / アップロード受信 (FS 書き込み) |
| `loopTask` | 1 | 1 | 8192 | IMU 100Hz、ジェスチャー、LCD (QR/映像)、DNS 応答、シリアルコンソール |
| WiFi/lwIP | 0 | 高 | — | SDK |

アップロード中 (FS 書き込み中) は再生を止める。フラッシュ書き込みはキャッシュ無効化で両コアに
影響するため、描画のちらつきが出る可能性がある (再生停止中なので実害は小さい)。

### 状態遷移 (SoloPlayer)

```
起動 ──有効な動画あり──▶ playing ◀─play─ stopped
  │                       │  ▲          ▲
  └─動画なし──▶ no_video   stop        │
  └─破損/解像度違い─▶ error            │
アップロード開始 ─▶ uploading ─成功─▶ playing (先頭から)
                            └─失敗─▶ 旧動画があれば playing / 無ければ no_video
```

## 4. 動画形式と事前変換

受理条件 (アップロード時と起動時の両方で全フレーム検査):

- raw MJPEG: `FFD8 … FFD9` の Baseline JPEG (SOF0) を区切りなしで連結
- 全フレーム 320×160 (`config.json` の `image.width/height`)。他解像度は `resolution mismatch`
- progressive (SOF2) は `not a baseline JPEG` で拒否
- 1 フレーム ≤ 65536 bytes
- 途中で切れたファイル・末尾のゴミ・不正なマーカー構造は拒否 (部分ファイルは再生しない)

```bash
# 中央トリミングで 2:1 に合わせる (既定)。-q は 2(高画質)〜31(低画質)
tools/make_solo_video.sh input.mp4 video.mjpg -q 6
# 黒帯で収める / 先頭 30 秒だけ
tools/make_solo_video.sh input.mov video.mjpg --fit pad -t 30
```

MP4/MOV/HEVC を ESP32 上で変換する機能は無い (PC 側で事前変換する)。

容量の目安 (10fps)。LittleFS 3.94MB のうち同梱物は 39KB なので、動画に約 3.9MB 使える。

| 平均フレーム | 1 秒あたり | 再生できる長さ |
| --- | --- | --- |
| 5 KiB | 50 KiB | 約 78 秒 |
| 10 KiB | 100 KiB | 約 39 秒 |
| 20 KiB | 200 KiB | 約 19 秒 |

## 5. パーティションと容量

```
nvs       0x9000    0x4000
otadata   0xd000    0x2000
phy_init  0xf000    0x1000
ota_0     0x10000   0x200000   (2MB)
ota_1     0x210000  0x200000   (2MB)
spiffs    0x410000  0x3F0000   (3.94MB, LittleFS)
```

- OTA スロットを 2MB にした理由: §1 のとおり、派生元の時点で 1.72MB あり 1.5MB に収まらなかった。
  本リポジトリのビルドは 1,734,465 bytes = 2MB の 82.7% で、約 360KB の余裕がある。
- 8MB flash の未使用領域 (約 1.94MB) を OTA 増分 1MB と LittleFS 増分 0.94MB に配分した。
- アップロード上限 (`/api/status` の `fs.max_upload`) = 空き + 既存動画サイズ − 64KiB (LittleFS 余裕)。
- 一時ファイルと既存動画を同時に置けないサイズのときだけ、既存動画を先に削除して受信する
  (`replaced_before_upload: true`)。失敗すると `no_video` になり再アップロードできる。

### 書き込み手順

パーティション表は USB でしか更新できない (OTA では書き換わらない)。初回は必ず USB で書き込む。

```bash
pio run -e atoms3r -t upload      # ファーム + パーティション表 (USB)
pio run -e atoms3r -t uploadfs    # data/ を LittleFS へ (config.json, レイアウト CSV)
```

以後のファーム更新は `pio run -e atoms3r_ota -t upload` (先に PC を SoftAP へ接続)。

電源断: 一時ファイルへ書いてから rename するので、途中で切れても `/video.mjpg` は旧状態のまま。
`/video.tmp` が残った場合は次回アップロード時に削除される。マウント失敗時に勝手にフォーマットはしない。

## 6. 接続導線: QR とキャプティブポータル (NFC を採らなかった理由)

1. 端末が 1 台も繋がっていない間、本体 LCD に Wi-Fi 接続 QR
   (`WIFI:T:WPA;S:isolation-sphere;P:sphere-solo;;`) と SSID / URL を表示する
   (`LCDManager::drawWifiQr`、`sphere.features.LCD.debug = true` のとき)。
2. iPhone のカメラで読むと「ネットワーク “isolation-sphere” に接続」が出る → タップで接続。
3. 接続直後に iOS が `captive.apple.com/hotspot-detect.html` を取得しようとする。デバイスの DNS が
   全ホスト名を 192.168.4.1 に解決し、HTTP は `/api/` 以外の未知パスを `http://192.168.4.1/?cna=1`
   へ 302 で返すため、iOS は期待する "Success" を受け取れず **「ログイン」画面 (Captive Network
   Assistant) に Web UI を表示する**。Android (`/generate_204`) / Windows (`/connecttest.txt`) も同様。
4. 端末が接続されると LCD は映像 / STANDBY 表示に戻る。切断すると QR に戻る。

iOS の「ログイン」画面は簡易ブラウザで、**ファイル選択が動かないことがある**。UI は `?cna=1` のとき
「アップロードは Safari で `http://192.168.4.1/` を開く」旨のバナーを出す。再生/停止/明るさはその画面で操作できる。

QR を印刷したい場合は、シリアルログ `[SOLO] Wi-Fi QR: WIFI:T:WPA;S:...;P:...;;` の文字列を
任意の QR 生成ツールに入れればよい (画像化ライブラリは同梱していない)。

### NFC について

- iPhone は **NFC タグから Wi-Fi に参加できない** (Android の Wi-Fi NDEF は iOS 非対応)。NFC でできるのは
  URL レコードを読んで Safari を開くことだけで、AP に繋がっていなければ `http://192.168.4.1/` は開けない。
- したがって NFC 単独では「接続 → UI」の導線が成立せず、QR (接続) + キャプティブポータル (UI 起動) を採用した。
- 接続後のショートカットとして、`http://192.168.4.1/` を書いた**受動 NFC タグ**を球体に貼るのは有効
  (ファーム変更不要、iPhone XS 以降は画面点灯中に近づけるだけで Safari が開く)。任意のオプション。

## 7. HTTP API

| Method | Path | Body | 説明 |
| --- | --- | --- | --- |
| GET | `/` | — | Web UI (`?cna=1` でキャプティブ画面向けバナー) |
| GET | `/api/status` | — | 状態・動画情報・統計・容量・AP 情報 (`ap.clients` = 接続端末数) |
| POST | `/api/play` / `/api/stop` | — | 再生 / 停止 (uploading 中は 409) |
| POST | `/api/brightness` | `{"value":0-100}` | 明るさ (再起動で `params.brightness` に戻る) |
| POST | `/api/video` | 動画本体 (`application/octet-stream`) | アップロード。成功 200 / 検証失敗 400 / 容量不足 507 / 競合 409 |
| POST | `/api/video/delete` | — | 動画削除 → `no_video` |
| POST | `/api/reboot` | — | 再起動 |
| * | `/api/` 以外の未知パス | — | `302 → http://<AP IP>/?cna=1` (キャプティブポータル検出用) |

```bash
curl -s http://192.168.4.1/api/status | jq .
curl -s -X POST --data-binary @video.mjpg -H 'Content-Type: application/octet-stream' http://192.168.4.1/api/video
curl -s -X POST -d '{"value":30}' http://192.168.4.1/api/brightness
```

## 8. 設定 (`data/config.json`)

| キー | 既定 | 説明 |
| --- | --- | --- |
| `solo.video_path` | `/video.mjpg` | 再生する動画 |
| `solo.http_port` | `80` | Web UI のポート |
| `solo.ap.ssid` / `password` / `ip` | `isolation-sphere` / `sphere-solo` / `192.168.4.1` | SoftAP。パスフレーズが 8 文字未満ならオープン AP で起動し警告を出す (QR も `T:nopass`) |
| `image.width` / `height` | `320` / `160` | 受理する解像度 |
| `params.brightness` | `50` | 起動時の明るさ [%] |
| `system.opening_action` | `enabled: true, 1200ms` | 起動時の LED オープニング |
| `system.debug` | `true` | `DEBUG_*` マクロのログ出力 |
| `sphere.features.LCD.debug` | `true` | LCD 表示 (QR / 映像 / STANDBY)。`false` で LCD 無効 |
| `sphere.features.IMU` | `BNO055` | IMU 種別 (ビルド env の `IMU_SENSOR_*` も参照) |

## 9. 検証状況

### ソース / ビルドで確認したこと

| コマンド | 結果 |
| --- | --- |
| `pio test -e native` | JPEG 境界パーサー **13 ケース PASS** (完全フレーム、全プレフィックスで NeedMore、連結分割、APP1 内 EOI 非誤検出、非 SOI、progressive 判定、マーカー間ゴミ、SOS 前 EOI、SOI 入れ子、フィルバイト、長さ 0 セグメント、TEM/RSTn、null) |
| `pio run -e atoms3r` | **SUCCESS**。Flash 1,734,465 / 2,097,152 bytes (82.7%)、RAM 68,484 / 327,680 bytes (20.9%) |
| `pio run -e xiao_esp32s3` | **SUCCESS**。Flash 1,466,289 bytes (69.9%)、RAM 64,920 bytes (19.8%)。LCD 無しのため QR 表示は無効 |

### 実機で未確認のこと (次にやる実機チェック)

1. **起動と自動再生**: 動画を入れた状態で iPhone も PC も無しに電源投入 → 再生開始、EOF でループ
   (`[SOLO] loops=` が増える)
2. **締切**: `[SOLO] miss=` が増え続けないこと。`read=…us tick=…us` が 100ms を十分下回ること
   (LittleFS 読み出し + デコードの実測値を記録する)
3. **姿勢追従**: 再生中に回転させ、派生元と同じ追従性か (`render_fps` が落ちていないか)
4. **QR と自動起動**: LCD の QR を iPhone のカメラで読めるか (照度・サイズ)。接続後に「ログイン」画面が
   自動で開くか。接続すると LCD が映像 / STANDBY に戻るか
5. **iPhone Safari**: UI 表示 → アップロード進捗 → 完了後に先頭から再生 → 切断後も再生継続
6. **異常系**: 途中切れ / 解像度違い / progressive / 過大フレーム / 容量不足 / 送信中断 →
   `/video.mjpg` が壊れず、UI が理由を表示して再アップロードできる
7. **電源断**: アップロード中に電源を切る → 再起動後、旧動画が再生される (または `no_video`)。
   `/video.tmp` が残っていても次回アップロードで消える
8. **メモリ**: `/api/status` の `heap_free` / `psram_free` と各タスクのスタック余裕
   (`uxTaskGetStackHighWaterMark`) を記録する
9. **OTA**: パーティション変更後、USB で 1 回書いた機体に対して `atoms3r_ota` で更新できること

### 既知の制約

- 音声・シーク・複数動画・プレイリスト・サムネイルは無い (要件どおり)
- iPhone 内の動画を直接変換する機能は無い (PC で `tools/make_solo_video.sh`)
- 明るさは再起動で `config.json` の値に戻る (フラッシュ寿命を考慮して毎回の永続化はしない)
- QR 表示は LCD 搭載機 (AtomS3R) かつ `LCD.debug = true` のときのみ
- iOS の「ログイン」画面ではファイル選択が使えない場合がある (Safari で開けば可)
- `TJpg_Decoder` はグローバル単一インスタンスのため、デコードを複数タスクから同時に呼べない
  (現状 `SoloPlayer` タスクのみが呼ぶ)
- LED 出力モード `Manual` (外部から画素を直接書く) は制御経路が無いため実質未使用。
  `Test` パターンはシリアルコンソール `led test` から確認できる
