#!/usr/bin/env python3
"""実機ブリングアップ用のテストパターン raw MJPEG を生成する。

ffmpeg 無しで動く (PIL のみ)。球面マッピング・回転追従・フレーム送りを目視で確認する
ための正距円筒パターンを作る:

  - 経度 30° ごとに色相の違う 12 セクター (回転させると色が変わる = 姿勢追従の確認)
  - 経度/緯度 30° ごとのグリッド線 (マッピングの歪みの確認)
  - 上端 = 赤帯 / 下端 = 青帯 (天頂・地底の向きの確認)
  - 1 秒ごとに 1 セクター進む白マーカー (フレーム送り・ループの確認)
  - 左上にフレーム番号 (シリアルログの frames と突き合わせる)

出力は 320x160 / 10fps / Baseline JPEG 連結 = src/JpegScan.h が受理する形式。

使い方:
  python3 tools/make_test_pattern.py [-o data/video.mjpg] [-n 100] [-q 80]
"""
import argparse
import colorsys
import io
import pathlib
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("PIL が見つかりません: pip install pillow")

ROOT = pathlib.Path(__file__).resolve().parent.parent
W, H, FPS = 320, 160, 10
MAX_FRAME_BYTES = 65536
SECTORS = 12                      # 経度 360° / 12 = 30°
POLE_H = 10                       # 上下の極帯の高さ (px)


def sector_colors():
    out = []
    for i in range(SECTORS):
        r, g, b = colorsys.hsv_to_rgb(i / SECTORS, 0.85, 0.85)
        out.append((int(r * 255), int(g * 255), int(b * 255)))
    return out


def make_frame(i: int, colors) -> Image.Image:
    im = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(im)

    sw = W / SECTORS
    for s in range(SECTORS):
        d.rectangle([s * sw, 0, (s + 1) * sw - 1, H - 1], fill=colors[s])

    # 極帯: 上 = 赤 / 下 = 青
    d.rectangle([0, 0, W - 1, POLE_H - 1], fill=(200, 30, 30))
    d.rectangle([0, H - POLE_H, W - 1, H - 1], fill=(30, 60, 200))

    # 30° グリッド (経度 12本 / 緯度 5本)
    for s in range(SECTORS):
        x = int(s * sw)
        d.line([x, 0, x, H - 1], fill=(255, 255, 255), width=1)
    for k in range(1, 6):
        y = int(k * H / 6)
        d.line([0, y, W - 1, y], fill=(255, 255, 255), width=1)

    # 1 秒ごとに 1 セクター進む白マーカー (縦帯)
    ms = (i // FPS) % SECTORS
    d.rectangle([ms * sw + 3, POLE_H + 3, (ms + 1) * sw - 4, H - POLE_H - 4],
                fill=(255, 255, 255))
    # マーカー内に進行方向が分かる黒い切り欠き (100ms ごとに縦位置が動く)
    ph = (i % FPS) / FPS
    y0 = POLE_H + 6 + ph * (H - 2 * POLE_H - 20)
    d.rectangle([ms * sw + 8, y0, (ms + 1) * sw - 9, y0 + 12], fill=(0, 0, 0))

    d.text((4, POLE_H + 2), f"{i:04d}", fill=(0, 0, 0))
    d.text((3, POLE_H + 1), f"{i:04d}", fill=(255, 255, 255))
    return im


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default=str(ROOT / "data" / "video.mjpg"))
    ap.add_argument("-n", "--frames", type=int, default=100, help="フレーム数 (既定 100 = 10秒)")
    ap.add_argument("-q", "--quality", type=int, default=80, help="JPEG 品質 (既定 80)")
    args = ap.parse_args()

    colors = sector_colors()
    parts, sizes = [], []
    for i in range(args.frames):
        buf = io.BytesIO()
        # progressive=False (既定) = Baseline sequential。optimize すると Huffman 表が最適化されるが
        # Baseline のままなので JpegScan は受理する
        make_frame(i, colors).save(buf, format="JPEG", quality=args.quality,
                                   subsampling="4:2:0", progressive=False, optimize=True)
        b = buf.getvalue()
        parts.append(b)
        sizes.append(len(b))

    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(b"".join(parts))          # 連結するだけで raw MJPEG

    total = sum(sizes)
    print(f"{out}")
    print(f"  frames    : {len(parts)} (= {len(parts) / FPS:.1f} s @{FPS}fps)")
    print(f"  size      : {total} bytes ({total / 1048576:.2f} MiB)")
    print(f"  avg frame : {total // len(parts)} bytes")
    print(f"  max frame : {max(sizes)} bytes (limit {MAX_FRAME_BYTES})")
    if max(sizes) > MAX_FRAME_BYTES:
        print("!! 1フレームが上限を超えています。-q を下げてください", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
