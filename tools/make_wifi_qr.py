#!/usr/bin/env python3
"""solo モードの AP 接続用 QR コードを生成する。

data/config.json の solo.ap (ssid / password / ip) を読み、ファームウェアの
NetworkManager::wifiQrText() と同一の文字列 ("WIFI:T:WPA;S:..;P:..;;") を QR にする。
LCD 未搭載機 (XIAO ESP32S3) や、印刷して本体に貼る用途向け。

出力 (既定は docs/):
  wifi_qr.png        接続用 QR (Wi-Fi 設定)
  ui_qr.png          Web UI の URL QR (接続後に開く http://192.168.4.1/)
  wifi_qr_label.svg  QR + SSID / パスワード / URL を並べた印刷用ラベル

使い方:
  python3 tools/make_wifi_qr.py [-o 出力ディレクトリ] [--scale 12]

依存: segno (pip install segno)
"""
import argparse
import json
import pathlib
import sys

try:
    import segno
except ImportError:
    sys.exit("segno が見つかりません: pip install segno")

ROOT = pathlib.Path(__file__).resolve().parent.parent
# WIFI: 文字列でエスケープが必要な文字 (ファーム側の escape() と同じ)
SPECIAL = set('\\\\;,:"')


def wifi_qr_text(ssid: str, password: str) -> str:
    """NetworkManager::wifiQrText() と同じ規則で WIFI: 文字列を組む。"""
    def escape(s: str) -> str:
        return "".join("\\" + c if c in SPECIAL else c for c in s)

    if len(password) < 8:  # 8文字未満はファーム側もオープンAP扱いにする
        return f"WIFI:T:nopass;S:{escape(ssid)};;"
    return f"WIFI:T:WPA;S:{escape(ssid)};P:{escape(password)};;"


def label_svg(qr, ssid: str, password: str, url: str) -> str:
    """QR とテキストを並べた印刷用ラベル (単一ファイルの SVG)。"""
    matrix = [row for row in qr.matrix]
    n = len(matrix)
    quiet, module = 4, 8            # 静音領域4モジュール / 1モジュール8px
    qr_px = (n + quiet * 2) * module
    pad, text_h = 24, 104
    w, h = qr_px + pad * 2, qr_px + pad * 2 + text_h

    rects = []
    for y, row in enumerate(matrix):
        x = 0
        while x < n:
            if row[x]:
                run = 1
                while x + run < n and row[x + run]:
                    run += 1
                rects.append(
                    f'<rect x="{(x + quiet) * module}" y="{(y + quiet) * module}" '
                    f'width="{run * module}" height="{module}"/>'
                )
                x += run
            else:
                x += 1

    cx = w // 2
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">
  <rect width="{w}" height="{h}" fill="#fff"/>
  <g transform="translate({pad},{pad})" fill="#000">
    {chr(10).join('    ' + r for r in rects).strip()}
  </g>
  <g font-family="'Noto Sans CJK JP','Hiragino Sans','Noto Sans JP','Yu Gothic',Meiryo,-apple-system,Helvetica,Arial,sans-serif" text-anchor="middle" fill="#000">
    <text x="{cx}" y="{qr_px + pad + 30}" font-size="15" fill="#666">Wi-Fi に接続</text>
    <text x="{cx}" y="{qr_px + pad + 56}" font-size="20" font-weight="bold">{ssid}</text>
    <text x="{cx}" y="{qr_px + pad + 78}" font-size="15" fill="#666">パスワード {password}</text>
    <text x="{cx}" y="{qr_px + pad + 100}" font-size="15" fill="#666">接続後 {url} を Safari で開く</text>
  </g>
</svg>
"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default=str(ROOT / "docs"), help="出力ディレクトリ (既定: docs/)")
    ap.add_argument("-c", "--config", default=str(ROOT / "data" / "config.json"))
    ap.add_argument("--scale", type=int, default=12, help="PNG の1モジュールあたりの px (既定 12)")
    args = ap.parse_args()

    cfg = json.loads(pathlib.Path(args.config).read_text(encoding="utf-8"))
    apc = cfg["solo"]["ap"]
    ssid, password, ip = apc["ssid"], apc["password"], apc["ip"]
    port = cfg["solo"].get("http_port", 80)
    url = f"http://{ip}/" if port == 80 else f"http://{ip}:{port}/"

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    text = wifi_qr_text(ssid, password)
    # error='m' = 15% 復元。印刷して貼る用途でも十分で、モジュール数を増やしすぎない
    wifi = segno.make(text, error="m")
    wifi.save(out / "wifi_qr.png", scale=args.scale, border=4)

    ui = segno.make(url, error="m")
    ui.save(out / "ui_qr.png", scale=args.scale, border=4)

    (out / "wifi_qr_label.svg").write_text(label_svg(wifi, ssid, password, url), encoding="utf-8")

    written = ["wifi_qr.png", "ui_qr.png", "wifi_qr_label.svg"]
    try:  # ラベルの PNG は任意 (cairosvg + 日本語フォントがある環境だけ)
        import cairosvg

        cairosvg.svg2png(url=str(out / "wifi_qr_label.svg"),
                         write_to=str(out / "wifi_qr_label.png"), scale=2.0)
        written.append("wifi_qr_label.png")
    except ImportError:
        print("(cairosvg が無いため wifi_qr_label.png は生成しません)")

    print(f"QR text   : {text}")
    print(f"UI url    : {url}")
    print(f"version   : {wifi.version} (error={wifi.error}, {len(wifi.matrix)}x{len(wifi.matrix)} modules)")
    for f in written:
        print(f"  {out / f}  ({(out / f).stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
