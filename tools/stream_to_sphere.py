#!/usr/bin/env python3
"""球体ディスプレイへ映像をUDPストリーミングする (チャンク分割プロトコル)。

プロトコルの正は docs/protocol_spec.md §4。ファーム core/src/ImageManager.h の
UDPChunkHeader と完全に一致させること:
  16Bヘッダ(little-endian) = magic(u32 0x4A504547) frame_id(u32) chunk_index(u16)
                              chunk_count(u16) chunk_size(u16) reserved(u16)
  + JPEGチャンク(<=1400B)
1フレームのJPEGを 1400B 単位のチャンクに分割して送る(UDP断片化を避けるため)。
デバイス側はチャンクを frame_id 単位で再構成してデコード・球面表示する。

送信先IP / UDPポート / 画像サイズ は共有 config.json (core/data/config.json) から取得。

使い方 (GMKTec で system python3 = Pillow あり):
  /usr/bin/python3 stream_to_sphere.py                       # テストパターン
  /usr/bin/python3 stream_to_sphere.py --source path/to.mp4  # 動画 (要 opencv-python)
  /usr/bin/python3 stream_to_sphere.py --source img.jpg      # 静止画
  /usr/bin/python3 stream_to_sphere.py --source frames/      # 画像フォルダを順送り
オプション: --fps 15 --quality 80 --target 192.168.49.101 --port 8889
"""
import argparse
import glob
import io
import json
import os
import socket
import struct
import sys
import time

from PIL import Image

MAGIC = 0x4A504547
MAX_CHUNK = 1400  # ファーム MAX_CHUNK_DATA と一致


def load_config():
    """共有 config.json から (device_ip, udp_port, width, height) を取得。"""
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(here, "../../core/data/config.json"),
        os.path.join(here, "../core/data/config.json"),
        "core/data/config.json",
    ]
    for p in candidates:
        if os.path.exists(p):
            with open(p) as f:
                c = json.load(f)
            return (
                c.get("sphere", {}).get("static_ip", "192.168.49.101"),
                int(c.get("wifi", {}).get("udp_port", 8889)),
                int(c.get("image", {}).get("width", 320)),
                int(c.get("image", {}).get("height", 160)),
            )
    return ("192.168.49.101", 8889, 320, 160)


def send_frame(sock, addr, frame_id, jpeg):
    """1フレームのJPEGをチャンク分割して送出。"""
    nchunks = (len(jpeg) + MAX_CHUNK - 1) // MAX_CHUNK
    if nchunks == 0:
        return
    for ci in range(nchunks):
        part = jpeg[ci * MAX_CHUNK:(ci + 1) * MAX_CHUNK]
        hdr = struct.pack("<IIHHHH", MAGIC, frame_id & 0xFFFFFFFF, ci, nchunks, len(part), 0)
        sock.sendto(hdr + part, addr)


def gen_test_pattern(w, h):
    gx = Image.linear_gradient("L").resize((w, h))
    gy = Image.linear_gradient("L").transpose(Image.ROTATE_90).resize((w, h))
    gr = Image.radial_gradient("L").resize((w, h))
    base = Image.merge("RGB", (gx, gy, gr))
    fid = 0
    while True:
        yield base.rotate(0, translate=((fid * 4) % w, 0))  # 横スクロール
        fid += 1


def gen_images(paths):
    while True:
        for p in paths:
            try:
                yield Image.open(p).convert("RGB")
            except Exception as e:
                print(f"skip {p}: {e}", file=sys.stderr)


def gen_video(path):
    import cv2  # 動画は opencv-python が必要
    cap = cv2.VideoCapture(path)
    if not cap.isOpened():
        raise SystemExit(f"動画を開けません: {path}")
    while True:
        ok, fr = cap.read()
        if not ok:
            cap.set(cv2.CAP_PROP_POS_FRAMES, 0)  # ループ
            continue
        yield Image.fromarray(fr[:, :, ::-1])  # BGR -> RGB


def make_source(source, w, h):
    if source in (None, "", "pattern"):
        print("source: test pattern")
        return gen_test_pattern(w, h)
    if os.path.isdir(source):
        paths = sorted(glob.glob(os.path.join(source, "*")))
        print(f"source: image folder ({len(paths)} files)")
        return gen_images(paths)
    ext = os.path.splitext(source)[1].lower()
    if ext in (".mp4", ".mov", ".avi", ".mkv", ".webm"):
        print(f"source: video {source}")
        return gen_video(source)
    print(f"source: image {source}")
    return gen_images([source])


def main():
    ip, port, cw, ch = load_config()
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", default="pattern", help="pattern | 画像 | フォルダ | 動画")
    ap.add_argument("--fps", type=float, default=15.0)
    ap.add_argument("--quality", type=int, default=80)
    ap.add_argument("--target", default=ip)
    ap.add_argument("--port", type=int, default=port)
    ap.add_argument("--width", type=int, default=cw)
    ap.add_argument("--height", type=int, default=ch)
    args = ap.parse_args()

    addr = (args.target, args.port)
    src = make_source(args.source, args.width, args.height)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    period = 1.0 / args.fps
    print(f"streaming {args.width}x{args.height} JPEG q{args.quality} -> {addr} @ {args.fps}fps")

    fid = 0
    next_t = time.time()
    last_report = next_t
    sent = 0
    for frame in src:
        if frame.size != (args.width, args.height):
            frame = frame.resize((args.width, args.height))
        buf = io.BytesIO()
        frame.save(buf, "JPEG", quality=args.quality)
        send_frame(sock, addr, fid, buf.getvalue())
        fid += 1
        sent += 1
        now = time.time()
        if now - last_report >= 2.0:
            print(f"  sent {sent} frames ({sent / (now - last_report):.1f}/s), last JPEG {buf.getbuffer().nbytes}B", flush=True)
            sent = 0
            last_report = now
        next_t += period
        sleep = next_t - time.time()
        if sleep > 0:
            time.sleep(sleep)
        else:
            next_t = time.time()  # 追いつけない場合はリセット


if __name__ == "__main__":
    main()
