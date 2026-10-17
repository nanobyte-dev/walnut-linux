#!/bin/bash
# Grabs one frame of the PS3's screen from the webcam pointed at the monitor.
#   tools/webcam-grab.sh [out.jpg]      (default build/tmp/screen.jpg)
# Camera settings are the ones that work in OBS (manual exposure, fixed white balance);
# auto settings give a washed-out mess. Env: WEBCAM (default: the BRIO's capture node by id, which stays
# the same across reboots; /dev/videoN numbers do not).
set -euo pipefail
DEV=${WEBCAM:-$(ls /dev/v4l/by-id/*-video-index0 2>/dev/null | head -1)}
OUT=${1:-build/tmp/screen.jpg}
mkdir -p "$(dirname "$OUT")"
python3 -I - "$DEV" <<'PY'
import fcntl, struct, sys
VIDIOC_S_CTRL = 0xc008561c   # _IOWR('V', 28, struct v4l2_control)
ctrls = [  # (V4L2 control id, value, name)
    (0x980900, 120, "brightness"), (0x980901, 121, "contrast"), (0x980902, 128, "saturation"),
    (0x98090c, 1, "white balance auto"), (0x980913, 0, "gain"),
    (0x980918, 1, "power line 50 Hz"), (0x98091b, 124, "sharpness"),
    (0x98091c, 1, "backlight compensation"), (0x9a0901, 1, "auto exposure: manual"),
    (0x9a0902, 1096, "exposure time"), (0x9a0903, 1, "dynamic framerate"),
    (0x9a090c, 1, "continuous autofocus"), (0x9a090d, 127, "zoom"),
]
with open(sys.argv[1], "rb", buffering=0) as f:
    for cid, val, name in ctrls:
        try:
            fcntl.ioctl(f, VIDIOC_S_CTRL, struct.pack("Ii", cid, val))
        except OSError as e:
            print(f"webcam: {name}: {e.strerror}", file=sys.stderr)
PY
ffmpeg -loglevel error -y -f v4l2 -input_format mjpeg -video_size 1920x1080 -i "$DEV" \
	-vf "select=gte(n\,30)" -frames:v 1 "$OUT"
echo "$OUT"
