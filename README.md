# isolation-sphere solo

外部 server なしで単独動作する球体 LED ディスプレイのファームウェア。

ESP32-S3 が SoftAP を立て、LittleFS に保存した 1 本の動画 (320×160 / 10fps) を電源投入だけで
自動ループ再生する。動画の差し替えと再生操作は、本体 LCD の QR から iPhone を接続して
Web UI (`http://192.168.4.1/`) で行う。MQTT ブローカー・配信 server・ルーター・インターネットは不要。

IMU による姿勢補正は従来どおり描画側で動作し、球体を回しても映像の向きが保たれる。

> 派生元: [sastles-com/sastle-isolation-sphere](https://github.com/sastles-com/sastle-isolation-sphere)
> (server + core 構成)。本リポジトリはその core を solo 専用に切り出し、MQTT 制御・UDP 映像受信・
> Python server を取り除いたもの。

---

## クイックスタート

```bash
# 1. ファームウェアと LittleFS を書き込む (初回は USB 必須: パーティション表を更新する)
pio run -e atoms3r -t upload
pio run -e atoms3r -t uploadfs

# 2. 動画を変換する (ffmpeg / ffprobe が必要)
tools/make_solo_video.sh input.mp4 video.mjpg -q 6

# 3. 電源投入 -> 本体 LCD の QR を iPhone のカメラで読む -> AP に接続
#    -> 自動で開く画面、または Safari で http://192.168.4.1/ を開く
#    -> video.mjpg をアップロード
```

以降は電源を入れるだけで自動再生する (iPhone も PC も不要)。

## 動作の流れ

```
                  ┌──────────────── ESP32-S3 (M5AtomS3R) ────────────────┐
                  │                                                      │
 iPhone ──Wi-Fi──▶│ SoftAP  ─▶ httpd (Web UI / アップロード)  [Core 0]    │
   │              │              │                                       │
   │ QR を読む     │              ▼                                       │
   └──LCD の QR───│         LittleFS  /video.mjpg                        │
                  │              │                                       │
                  │    SoloPlayer (100ms 締切)                [Core 0]    │
                  │      ファイル読み ─▶ JPEG デコード ─▶ RGB565           │
                  │              │                                       │
                  │      トリプルバッファ (display/ready/decode)           │
                  │              │                                       │
                  │    LEDManager (IMU 姿勢で毎パス再マッピング) [Core 1]   │
                  │              ▼                                       │
                  │        WS2812 ×800 (5ストリップ, RMT 並列出力)         │
                  └──────────────────────────────────────────────────────┘
```

- 再生は 10fps 固定。描画は ~50-60Hz で連続駆動し、IMU (100Hz) の最新姿勢で毎パス再マッピングする
  ため、動画のフレームレートと追従性は独立している。
- 締切は単調クロック (`esp_timer`) で管理し、遅れた締切は捨てる (遅延を溜め込まない)。

## リポジトリ構成

```
platformio.ini            ビルド設定 (atoms3r / atoms3r_m5imu / xiao_esp32s3 / *_ota / native)
partitions.csv            フラッシュ配分 (OTA 2MB×2 + LittleFS 3.94MB)
src/                      ファームウェア
  main.cpp                起動・タスク構成・シリアルコンソール
  SoloPlayer.{h,cpp}      10fps 締切でのローカル再生 / 状態管理
  SoloWebServer.{h,cpp}   Web UI・制御 API・アップロード・キャプティブポータル
  MjpegReader.{h,cpp}     raw MJPEG の逐次読み出しと全件検証
  JpegScan.h              JPEG マーカー追跡によるフレーム境界判定 (PC でテスト可)
  ImageManager.{h,cpp}    JPEG デコード + トリプルバッファ
  LEDManager.{h,cpp}      球面 UV マッピング / IMU 再投影 / FastLED 出力
  IMUManager.{h,cpp}      BNO055 または M5 内蔵IMU + Madgwick (100Hz)
  NetworkManager.{h,cpp}  SoftAP と Wi-Fi QR 文字列
  LCDManager.{h,cpp}      本体 LCD (QR / 映像 / STANDBY)
  ...                     ConfigManager, FileManager, GestureManager, SoundManager, OtaManager
data/                     LittleFS に書き込む内容 (config.json, LED レイアウト CSV)
test/test_jpeg_scan/      JPEG 境界パーサーの単体テスト (PC 上で実行)
tools/                    動画変換 / LED レイアウト生成スクリプト
docs/solo_mode.md         設計と運用の詳細 (仕様・API・実機チェック手順)
docs/handoff.md           実装依頼時の仕様書
```

## 対応ボード

| env | ボード | 構成 |
| --- | --- | --- |
| `atoms3r` (既定) | M5AtomS3R | 5ストリップ×160 LED、内蔵LCD (QR表示)、ブザー、外部 BNO055 |
| `atoms3r_m5imu` | M5AtomS3R | 同上、IMU は M5 内蔵 6 軸 + Madgwick |
| `xiao_esp32s3` | Seeed XIAO ESP32S3 | 4ストリップ、LCD/ブザーなし (QR は表示できない) |

## 動画の要件

- raw MJPEG: Baseline JPEG (SOF0) を区切りなしで連結したもの
- 320×160、10fps、音声なし、1 フレーム 64KiB 以下
- progressive JPEG・解像度違い・途中で切れたファイルは受理しない

`tools/make_solo_video.sh` が ffmpeg で変換し、ffprobe で解像度・フレーム数・最大フレームサイズを検査する。

容量: LittleFS 3.94MB のうち動画に使えるのは約 3.8MB。平均 10KiB/フレームなら約 38 秒。

## 操作

| 手段 | できること |
| --- | --- |
| Web UI (`http://192.168.4.1/`) | 状態確認、再生/停止、明るさ、動画アップロード/削除、再起動 |
| HTTP API | `GET /api/status`、`POST /api/play`、`/api/stop`、`/api/brightness`、`/api/video`、`/api/video/delete`、`/api/reboot` |
| シリアルコンソール (115200) | `help` / `status` / `play` / `stop` / `led sphere|test` / `reboot` |

設定は `data/config.json` (`solo.ap.ssid` / `password` / `ip`、`solo.video_path`、`params.brightness` ほか)。
変更後は `pio run -t uploadfs` で書き込む。

## 開発

```bash
pio run -e atoms3r            # ビルド
pio test -e native            # JPEG 境界パーサーの単体テスト (実機不要)
pio device monitor            # シリアルログ (115200)
pio run -e atoms3r_ota -t upload   # OTA 書き込み (先に PC を AP へ接続)
```

詳細・設計判断・実機チェック手順は [docs/solo_mode.md](docs/solo_mode.md) を参照。
