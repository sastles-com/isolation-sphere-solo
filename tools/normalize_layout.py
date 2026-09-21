#!/usr/bin/env python3
"""LED レイアウトの raw CSV (mm 単位) を単位ベクトルへ正規化して data CSV を生成する。

座標規約 (docs/ui-v2-led-mapping-spec.md / HoloSphere.jsx / sphereToUV と共通):
  +Z = 北極 (画像上端), 赤道 = X-Y 平面。
raw の CAD 座標をそのまま正規化する (符号変換なし)。
鏡像が実機で確認された場合のみ --flip-axis で 1 軸を反転する (回転ズレは
キャリブレーションで吸収するのでここでは触らない)。

使い方:
    python tools/normalize_layout.py                # raw → data/led_layouts-5strip.csv
    python tools/normalize_layout.py --flip-axis x  # 鏡像対策 (要理由のドキュメント化)
"""
import argparse
import csv
import math
import os

DEFAULT_IN = os.path.join(os.path.dirname(__file__), "..", "led_layouts-5strip.raw.csv")
DEFAULT_OUT = os.path.join(os.path.dirname(__file__), "..", "data", "led_layouts-5strip.csv")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="src", default=DEFAULT_IN)
    ap.add_argument("--out", dest="dst", default=DEFAULT_OUT)
    ap.add_argument("--flip-axis", choices=["x", "y", "z"], default=None,
                    help="鏡像 (キラリティ不一致) が実機確認された場合のみ指定")
    ap.add_argument("--reverse-strips", action="store_true",
                    help="行ブロックの並びを逆順にする (チェーンsに旧ブロックN-1-sの座標を割当)。"
                         "注意: ファームは strip 列でなく行順でチェーンに割り当てるため、"
                         "ラベルでなく行ブロック自体を並べ替える。"
                         "実機のウェッジ並び順が CSV と逆の場合に使用")
    ap.add_argument("--rotate-y180", action="store_true",
                    help="Y軸周り180°回転 (x,z を反転)。天地が逆の場合に使用。"
                         "正規の回転なので鏡像は生じない")
    args = ap.parse_args()

    sign = {"x": 1.0, "y": 1.0, "z": 1.0}
    if args.rotate_y180:
        sign["x"] *= -1.0
        sign["z"] *= -1.0
    if args.flip_axis:
        sign[args.flip_axis] *= -1.0

    with open(args.src, newline="") as f:
        rows = list(csv.DictReader(f))

    n_strips = max(int(r["strip"]) for r in rows) + 1

    # ファームは strip 列でなく「行順」でチェーンに割り当てるため、
    # 並べ替えは行ブロック単位で行い、strip 列は新しいブロック位置に振り直す。
    blocks = {}
    for r in rows:
        blocks.setdefault(int(r["strip"]), []).append(r)
    order = list(range(n_strips))
    if args.reverse_strips:
        order = order[::-1]

    out_rows = []
    for new_strip, old_strip in enumerate(order):
        for r in blocks[old_strip]:
            x, y, z = float(r["x"]), float(r["y"]), float(r["z"])
            n = math.sqrt(x * x + y * y + z * z)
            if n < 1e-9:
                raise SystemExit(f"零ベクトル行: {r}")
            out_rows.append({
                "FaceID": r["FaceID"], "strip": str(new_strip), "strip_num": r["strip_num"],
                "x": f"{sign['x'] * x / n:.9f}",
                "y": f"{sign['y'] * y / n:.9f}",
                "z": f"{sign['z'] * z / n:.9f}",
            })

    with open(args.dst, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["FaceID", "strip", "strip_num", "x", "y", "z"])
        w.writeheader()
        w.writerows(out_rows)
    tags = []
    if args.rotate_y180:
        tags.append("Y180回転")
    if args.flip_axis:
        tags.append(f"flip {args.flip_axis}")
    if not tags:
        tags.append("符号変換なし")
    if args.reverse_strips:
        tags.append("strip逆順")
    print(f"生成完了: {args.dst}  {len(out_rows)} rows ({', '.join(tags)})")


if __name__ == "__main__":
    main()
