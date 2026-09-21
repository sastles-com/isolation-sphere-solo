#!/usr/bin/env bash
# solo モード用 raw MJPEG 変換スクリプト
#
# 任意の動画 (MP4/MOV/HEVC など) を、ESP32 側が受理する形式に事前変換する:
#   - 320x160, 10fps, 音声なし
#   - Baseline JPEG (Huffman, 4:2:0) を区切りなしで連結した raw MJPEG
#
# 使い方:
#   tools/make_solo_video.sh input.mp4 output.mjpg [-q 2..31] [--fit crop|pad] [-t 秒数]
#     -q     JPEG 品質 (ffmpeg の -q:v。小さいほど高画質・大容量。既定 6)
#     --fit  crop = 2:1 に合わせて中央トリミング (既定) / pad = 黒帯で 2:1 に収める
#     -t     先頭から指定秒数だけ変換 (容量に収めるため)
#
# 出力後に ffprobe でフレーム数・解像度・最大フレームサイズを検査し、
# ファームウェアの上限 (1フレーム 64KiB) を超える場合は警告する。
set -euo pipefail

W=320
H=160
FPS=10
MAX_FRAME_BYTES=65536

usage() {
    sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
}

[ $# -ge 2 ] || usage
IN=$1
OUT=$2
shift 2

Q=6
FIT=crop
DURATION=""
while [ $# -gt 0 ]; do
    case "$1" in
        -q)     Q=$2; shift 2 ;;
        --fit)  FIT=$2; shift 2 ;;
        -t)     DURATION=$2; shift 2 ;;
        -h|--help) usage ;;
        *) echo "unknown option: $1" >&2; usage ;;
    esac
done

command -v ffmpeg  >/dev/null || { echo "ffmpeg が見つかりません" >&2; exit 1; }
command -v ffprobe >/dev/null || { echo "ffprobe が見つかりません" >&2; exit 1; }
[ -f "$IN" ] || { echo "入力が見つかりません: $IN" >&2; exit 1; }

case "$FIT" in
    crop) VF="fps=${FPS},scale=${W}:${H}:force_original_aspect_ratio=increase,crop=${W}:${H}" ;;
    pad)  VF="fps=${FPS},scale=${W}:${H}:force_original_aspect_ratio=decrease,pad=${W}:${H}:(ow-iw)/2:(oh-ih)/2:black" ;;
    *) echo "--fit は crop か pad" >&2; exit 1 ;;
esac
VF="${VF},format=yuvj420p"

DUR_ARGS=()
[ -n "$DURATION" ] && DUR_ARGS=(-t "$DURATION")

echo "== encode: $IN -> $OUT (${W}x${H} @${FPS}fps, q=${Q}, fit=${FIT})"
# -f mjpeg = JPEG を連結した raw MJPEG (コンテナなし)。ffmpeg の mjpeg エンコーダは
# Baseline sequential のみを出力するので TJpg_Decoder でデコードできる。
ffmpeg -y -hide_banner -loglevel error -i "$IN" "${DUR_ARGS[@]}" \
    -vf "$VF" -an -c:v mjpeg -q:v "$Q" -f mjpeg "$OUT"

echo "== verify"
read -r PW PH NFRAMES < <(ffprobe -v error -f mjpeg -select_streams v:0 -count_frames \
    -show_entries stream=width,height,nb_read_frames -of csv=p=0 "$OUT" | tr ',' ' ')
MAXPKT=$(ffprobe -v error -f mjpeg -select_streams v:0 -show_entries packet=size -of csv=p=0 "$OUT" | sort -n | tail -1)
SIZE=$(stat -c %s "$OUT" 2>/dev/null || stat -f %z "$OUT")

echo "  resolution : ${PW}x${PH}"
echo "  frames     : ${NFRAMES}  (= $(awk "BEGIN{printf \"%.1f\", ${NFRAMES}/${FPS}}") s @${FPS}fps)"
echo "  file size  : ${SIZE} bytes ($(awk "BEGIN{printf \"%.2f\", ${SIZE}/1048576}") MiB)"
echo "  avg frame  : $(( SIZE / (NFRAMES > 0 ? NFRAMES : 1) )) bytes"
echo "  max frame  : ${MAXPKT} bytes (limit ${MAX_FRAME_BYTES})"

STATUS=0
if [ "$PW" != "$W" ] || [ "$PH" != "$H" ]; then
    echo "!! 解像度が ${W}x${H} ではありません" >&2
    STATUS=1
fi
if [ "${MAXPKT:-0}" -gt "$MAX_FRAME_BYTES" ]; then
    echo "!! 1フレームが上限 ${MAX_FRAME_BYTES} bytes を超えています。-q を大きくして再変換してください" >&2
    STATUS=1
fi
if [ "${NFRAMES:-0}" -eq 0 ]; then
    echo "!! フレームがありません" >&2
    STATUS=1
fi
[ $STATUS -eq 0 ] && echo "== OK: Web UI (http://192.168.4.1/) からアップロードしてください"
exit $STATUS
