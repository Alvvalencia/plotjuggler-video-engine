#!/usr/bin/env bash
# Generate test video files for VideoEngine integration tests.
# Requires ffmpeg CLI on PATH.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DATA_DIR="$SCRIPT_DIR/data"
mkdir -p "$DATA_DIR"

# 30-second 1080p H.264 video with visible second counter (keyframe every 30 frames)
ffmpeg -y -f lavfi \
  -i "color=black:size=1920x1080:rate=30,drawtext=text='%{eif\:floor(t)\:d}':fontsize=120:fontcolor=white:x=(w-text_w)/2:y=(h-text_h)/2" \
  -t 30 -vcodec libx264 -preset fast -pix_fmt yuv420p -g 30 \
  "$DATA_DIR/test_coarse.mp4"

echo "Generated: $DATA_DIR/test_coarse.mp4"
