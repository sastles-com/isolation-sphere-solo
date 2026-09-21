#!/usr/bin/env python3
"""led_layouts-5strip.csv の3D可視化 (plotly)。

各ストリップを stripIdColor() 相当の色 (0=赤 1=緑 2=青 3=黄 4=紫) でプロットし、
num順につないだ線とマーカーで、実際の配線順序を確認できるようにする。
"""
import csv
from pathlib import Path

import plotly.graph_objects as go

CSV_PATH = Path(__file__).parent / "led_layouts-5strip.csv"
OUT_PATH = Path(__file__).parent / "led_layout_3d.html"

STRIP_COLORS = {
    0: "#e5484d",  # Red
    1: "#3fb968",  # Green
    2: "#4c8bf5",  # Blue
    3: "#e0b400",  # Yellow
    4: "#c94fd6",  # Magenta
}
STRIP_NAMES = {
    0: "strip0 (LINE01, Red)",
    1: "strip1 (LINE02, Green)",
    2: "strip2 (LINE03, Blue)",
    3: "strip3 (LINE04, Yellow)",
    4: "strip4 (LINE05, Magenta)",
}


def load_data():
    data = {}
    with open(CSV_PATH, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            strip = int(row["strip"])
            num = int(row["strip_num"])
            x, y, z = float(row["x"]), float(row["y"]), float(row["z"])
            face_id = row["FaceID"]
            data.setdefault(strip, []).append((num, x, y, z, face_id))
    for strip in data:
        data[strip].sort(key=lambda r: r[0])
    return data


def build_figure():
    data = load_data()
    fig = go.Figure()

    for strip in sorted(data):
        rows = data[strip]
        nums = [r[0] for r in rows]
        xs = [r[1] for r in rows]
        ys = [r[2] for r in rows]
        zs = [r[3] for r in rows]
        face_ids = [r[4] for r in rows]
        color = STRIP_COLORS[strip]

        # LED順につないだ線 (配線経路)
        fig.add_trace(
            go.Scatter3d(
                x=xs,
                y=ys,
                z=zs,
                mode="lines",
                line=dict(color=color, width=3),
                name=STRIP_NAMES[strip],
                legendgroup=f"strip{strip}",
                hoverinfo="skip",
            )
        )

        # 個々のLED (num, FaceIDをホバー表示)
        fig.add_trace(
            go.Scatter3d(
                x=xs,
                y=ys,
                z=zs,
                mode="markers",
                marker=dict(color=color, size=3),
                name=f"{STRIP_NAMES[strip]} LEDs",
                legendgroup=f"strip{strip}",
                showlegend=False,
                customdata=list(zip(nums, face_ids)),
                hovertemplate=(
                    "strip=%d<br>num=%%{customdata[0]}<br>FaceID=%%{customdata[1]}"
                    "<br>x=%%{x:.3f} y=%%{y:.3f} z=%%{z:.3f}<extra></extra>" % strip
                ),
            )
        )

        # 始点(num0)・終点(num末尾)を明示
        fig.add_trace(
            go.Scatter3d(
                x=[xs[0]],
                y=[ys[0]],
                z=[zs[0]],
                mode="markers",
                marker=dict(color=color, size=7, symbol="diamond",
                            line=dict(color="black", width=1)),
                name=f"{STRIP_NAMES[strip]} start (num0)",
                legendgroup=f"strip{strip}",
                showlegend=False,
                hovertemplate=f"strip{strip} START num=0<extra></extra>",
            )
        )
        fig.add_trace(
            go.Scatter3d(
                x=[xs[-1]],
                y=[ys[-1]],
                z=[zs[-1]],
                mode="markers",
                marker=dict(color=color, size=7, symbol="x",
                            line=dict(color="black", width=1)),
                name=f"{STRIP_NAMES[strip]} end (num{nums[-1]})",
                legendgroup=f"strip{strip}",
                showlegend=False,
                hovertemplate=f"strip{strip} END num={nums[-1]}<extra></extra>",
            )
        )

    # 単位球のワイヤーフレーム(参考)
    import numpy as np

    u = np.linspace(0, 2 * np.pi, 30)
    v = np.linspace(0, np.pi, 15)
    sx = np.outer(np.cos(u), np.sin(v))
    sy = np.outer(np.sin(u), np.sin(v))
    sz = np.outer(np.ones_like(u), np.cos(v))
    fig.add_trace(
        go.Surface(
            x=sx, y=sy, z=sz,
            opacity=0.08,
            showscale=False,
            colorscale=[[0, "#888888"], [1, "#888888"]],
            hoverinfo="skip",
            name="unit sphere",
        )
    )

    fig.update_layout(
        title="led_layouts-5strip.csv 3D配置 (◆=num0開始点, ×=終点)",
        scene=dict(
            xaxis_title="x",
            yaxis_title="y",
            zaxis_title="z (北極=+1)",
            aspectmode="data",
        ),
        legend=dict(itemsizing="constant"),
        margin=dict(l=0, r=0, t=40, b=0),
    )
    return fig


if __name__ == "__main__":
    fig = build_figure()
    fig.write_html(str(OUT_PATH), include_plotlyjs="cdn")
    print(f"saved: {OUT_PATH}")
