"""
PersonDetection ML inference dashboard.

Reads the augmented USB stream from My_MCC_Config (16-byte header + 38400-byte
RGB565 payload + 96-byte tracker trailer + 8-byte tripwire counter block) and
renders a single OpenCV window with:
  - upscaled live video with per-ID bounding-box overlays
  - tripwire activation line + deadband band overlay
  - IN / OUT / NET counts HUD
  - "Tracks: N" badge + raw-detection diagnostic
  - live metrics: stream FPS, inference time, inference FPS, frame counter
  - static model info: MCUNet person-det name, shape, head config

Header layout (must mirror app_usb.c FRAME_HEADER_SIZE=16):
  [0..3]   marker AA 55 AA 55
  [4..7]   frame counter (u32 LE)
  [8]      num_tracks (u8)        -- active tracks
  [9]      num_raw_boxes (u8)     -- pre-tracker detection count
  [10]     reserved
  [11]     inference counter (u8) -- host uses increments to compute inference FPS
  [12..15] last invoke()+postproc duration in microseconds (u32 LE)

Trailer layout (must mirror app_usb.c TRK_TRAILER_*), 8 records * 12 bytes:
  +0  u8   id           (0 = empty slot)
  +1  u8   score_q8     (round(score * 255))
  +2  i16  x0           model-input pixel space, 0..159
  +4  i16  y0           model-input pixel space, 0..127 (letterboxed)
  +6  i16  x1
  +8  i16  y1
  +10 u8   miss_count
  +11 u8   reserved

The model input is a 128x96 center crop of the 160x120 camera frame. Firmware
transforms box coords to camera-frame space (adds CROP_*_OFFSET) before
sending, so LETTERBOX_TOP=0 here -- no host-side offset is needed.
"""

import struct
import time

import cv2
import numpy as np
import usb.backend.libusb1
import usb.core
import usb.util


# ---- USB configuration (must match firmware descriptors) ----
VID   = 0x04D8
PID   = 0x0053
EP_IN = 0x81
PKT   = 512

# ---- Frame layout (must match app_usb.c) ----
FRAME_WIDTH        = 160
FRAME_HEIGHT       = 120
FRAME_BPP          = 2
FRAME_RGB565_SIZE  = FRAME_WIDTH * FRAME_HEIGHT * FRAME_BPP   # 38400
FRAME_HEADER_SIZE  = 16

# Tracker trailer
TRK_TRAILER_SLOTS  = 8
TRK_TRAILER_RECSIZE = 12
TRK_TRAILER_SIZE   = TRK_TRAILER_SLOTS * TRK_TRAILER_RECSIZE  # 96

# Tripwire counter + config block (see app_usb.c COUNTER_BLOCK):
#   u32 count_in, u32 count_out, u8 vertical, u8 pos, u8 deadband, u8 reserved.
# Config arrives every frame so the dashboard always matches firmware -- no
# need to edit Python when TRK_TRIPWIRE_* changes in app_tracker.h.
COUNTER_BLOCK_SIZE = 12

FRAME_PAYLOAD_SIZE = FRAME_RGB565_SIZE + TRK_TRAILER_SIZE + COUNTER_BLOCK_SIZE  # 38508

# Firmware now sends box coords in camera-space (model 128x96 center-cropped
# from camera 160x120, with the crop offset added on the firmware side).
# No client-side letterbox correction needed; kept as 0 so existing render
# math doesn't change.
LETTERBOX_TOP      = 0
MODEL_IN_H         = 96
MODEL_IN_W         = 128

MARKER             = bytes([0xAA, 0x55, 0xAA, 0x55])

# ---- Static model metadata (MCUNet person-det) ----
MODEL_INFO = {
    "Model":   "MCUNet person-det",
    "Task":    "Person detection (YOLOv3-style)",
    "Input":   "128x96x3 (HWC int8, cropped from 160x120)",
    "Output":  "3 heads (s8/s16/s32, 1 class)",
    "Boxes":   "anchors x 3, NMS 0.45",
    "Conf":    "valid threshold 0.5",
    "Engine":  "TinyEngine (codegen)",
    "Tracker": "SORT-lite (greedy IoU)",
}

# ---- Display sizing ----
VIDEO_W, VIDEO_H = 320, 240
PANEL_W = 280
WINDOW_TITLE = "PersonDetection inference dashboard"

# ---------------------------------------------------------------------------
# USB helpers
# ---------------------------------------------------------------------------

def open_device():
    backend = usb.backend.libusb1.get_backend(
        find_library=lambda _: "./libusb-1.0.dll"
    )
    dev = usb.core.find(idVendor=VID, idProduct=PID, backend=backend)
    if dev is None:
        raise RuntimeError(f"USB device {VID:04x}:{PID:04x} not found")
    dev.set_configuration()
    return dev


def parse_header(hdr: bytes):
    frame_id, num_tracks, num_raw, reserved, inf_count, inf_us = struct.unpack(
        "<IBBBBI", hdr[4:16]
    )
    return {
        "frame_id":        frame_id,
        "num_tracks":      int(num_tracks),
        "num_raw":         int(num_raw),
        "inference_us":    int(inf_us),
        "inference_count": int(inf_count),
    }


def parse_trailer(trailer: bytes):
    """Decode the 8-slot tracker trailer into a list of active-track dicts."""
    tracks = []
    for i in range(TRK_TRAILER_SLOTS):
        off = i * TRK_TRAILER_RECSIZE
        rec = trailer[off:off + TRK_TRAILER_RECSIZE]
        track_id, score_q8, x0, y0, x1, y1, miss, _ = struct.unpack(
            "<BBhhhhBB", rec
        )
        if track_id == 0:
            continue
        tracks.append({
            "id":    int(track_id),
            "score": score_q8 / 255.0,
            "x0":    int(x0), "y0": int(y0),
            "x1":    int(x1), "y1": int(y1),
            "miss":  int(miss),
        })
    return tracks


def read_frame(dev, leftover: bytearray):
    """Return (header_dict, rgb565_bytes, tracks_list, new_leftover)."""
    buf = leftover
    while True:
        pos = buf.find(MARKER)
        if pos != -1 and len(buf) - pos >= FRAME_HEADER_SIZE:
            hdr = bytes(buf[pos:pos + FRAME_HEADER_SIZE])
            meta = parse_header(hdr)
            payload = bytearray(buf[pos + FRAME_HEADER_SIZE:])
            break
        if len(buf) > 3:
            del buf[:-3]
        try:
            buf.extend(bytes(dev.read(EP_IN, PKT, timeout=1000)))
        except usb.core.USBTimeoutError:
            print("Timeout waiting for header — resyncing...")
            continue

    while len(payload) < FRAME_PAYLOAD_SIZE:
        try:
            payload.extend(bytes(dev.read(EP_IN, PKT, timeout=1000)))
        except usb.core.USBTimeoutError:
            print("Timeout mid-frame — resyncing...")
            return None, None, None, bytearray()

    rgb565 = bytes(payload[:FRAME_RGB565_SIZE])
    trailer = bytes(payload[FRAME_RGB565_SIZE:FRAME_RGB565_SIZE + TRK_TRAILER_SIZE])
    tracks = parse_trailer(trailer)
    counter_off = FRAME_RGB565_SIZE + TRK_TRAILER_SIZE
    count_in, count_out, tw_vert, tw_pos, tw_db, _ = struct.unpack(
        "<IIBBBB", bytes(payload[counter_off:counter_off + COUNTER_BLOCK_SIZE])
    )
    meta["count_in"]      = int(count_in)
    meta["count_out"]     = int(count_out)
    meta["tw_vertical"]   = int(tw_vert)
    meta["tw_pos"]        = int(tw_pos)
    meta["tw_deadband"]   = int(tw_db)
    new_leftover = bytearray(payload[FRAME_PAYLOAD_SIZE:])
    return meta, rgb565, tracks, new_leftover


def rgb565_to_bgr(payload: bytes) -> np.ndarray:
    arr = np.frombuffer(payload, dtype=np.uint8).reshape(
        (FRAME_HEIGHT, FRAME_WIDTH, 2)
    )
    return cv2.cvtColor(arr, cv2.COLOR_BGR5652BGR)


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

FONT = cv2.FONT_HERSHEY_SIMPLEX

# BGR colours
COL_BG       = (24, 24, 24)
COL_PANEL    = (40, 40, 40)
COL_TEXT     = (220, 220, 220)
COL_DIM      = (150, 150, 150)
COL_TRACKS   = (60, 200, 60)
COL_NONE     = (90, 90, 90)
COL_ACCENT   = (200, 160, 60)
COL_TRIPWIRE = (0, 220, 220)   # yellow -- distinct from per-ID box colours


def id_to_colour(track_id: int):
    """Deterministic per-ID BGR colour. Three coprime multipliers spread IDs
    across the colour wheel without collisions for small ID ranges."""
    return (
        (track_id * 97) % 200 + 40,
        (track_id * 53) % 200 + 40,
        (track_id * 191) % 200 + 40,
    )


def draw_text(img, text, org, scale=0.5, colour=COL_TEXT, thickness=1):
    cv2.putText(img, text, org, FONT, scale, colour, thickness, cv2.LINE_AA)


def render_panel(meta, stream_fps, inference_fps):
    panel = np.full((VIDEO_H, PANEL_W, 3), COL_PANEL, dtype=np.uint8)

    # ---- Tracks badge ----
    n_tracks = meta["num_tracks"]
    badge_colour = COL_TRACKS if n_tracks > 0 else COL_NONE
    label = f"TRACKS: {n_tracks}"
    cv2.rectangle(panel, (8, 6), (PANEL_W - 8, 44), badge_colour, -1)
    (tw, th), _ = cv2.getTextSize(label, FONT, 0.7, 2)
    draw_text(panel, label,
              ((PANEL_W - tw) // 2, 6 + (38 + th) // 2),
              scale=0.7, colour=(15, 15, 15), thickness=2)

    diag = f"raw_detections={meta['num_raw']}"
    draw_text(panel, diag, (10, 58), 0.38, COL_DIM)

    # ---- Live metrics ----
    y = 78
    draw_text(panel, "LIVE", (10, y), 0.4, COL_ACCENT, 1); y += 16
    inf_ms = meta["inference_us"] / 1000.0
    rows = [
        ("Stream FPS",    f"{stream_fps:6.1f}"),
        ("Inference",     f"{inf_ms:6.2f} ms"),
        ("Inference FPS", f"{inference_fps:6.1f}" if inference_fps else "  ---  "),
        ("Frame ID",      f"{meta['frame_id']}"),
    ]
    for row_label, value in rows:
        draw_text(panel, row_label, (12, y), 0.38, COL_TEXT)
        draw_text(panel, value,     (PANEL_W - 95, y), 0.38, COL_TEXT)
        y += 14

    # ---- Model info ----
    y += 4
    draw_text(panel, "MODEL", (10, y), 0.4, COL_ACCENT, 1); y += 16
    for info_label, value in MODEL_INFO.items():
        draw_text(panel, info_label, (12, y), 0.36, COL_DIM)
        draw_text(panel, value,      (PANEL_W - 175, y), 0.36, COL_TEXT)
        y += 13

    return panel


def enhance_video(video_bgr):
    """Denoise and mildly sharpen the raw 160x120 BGR frame before upscaling."""
    denoised = cv2.bilateralFilter(video_bgr, d=5, sigmaColor=40, sigmaSpace=7)
    blur = cv2.GaussianBlur(denoised, (0, 0), sigmaX=1.0)
    sharp = cv2.addWeighted(denoised, 1.4, blur, -0.4, 0)
    return sharp


def draw_tripwire(canvas_video, vertical, pos, deadband):
    """Draw the activation line and deadband band on the upscaled canvas.

    Config (vertical/pos/deadband) is read fresh each frame from the USB
    counter block, so the dashboard auto-matches whatever the firmware was
    built with."""
    sx = VIDEO_W / FRAME_WIDTH
    sy = VIDEO_H / FRAME_HEIGHT
    if vertical:
        x  = int(pos * sx)
        xd = int(deadband * sx)
        overlay = canvas_video.copy()
        cv2.rectangle(overlay, (x - xd, 0), (x + xd, VIDEO_H - 1),
                      COL_TRIPWIRE, -1)
        cv2.addWeighted(overlay, 0.18, canvas_video, 0.82, 0, dst=canvas_video)
        cv2.line(canvas_video, (x, 0), (x, VIDEO_H - 1), COL_TRIPWIRE, 1)
    else:
        y  = int(pos * sy)
        yd = int(deadband * sy)
        overlay = canvas_video.copy()
        cv2.rectangle(overlay, (0, y - yd), (VIDEO_W - 1, y + yd),
                      COL_TRIPWIRE, -1)
        cv2.addWeighted(overlay, 0.18, canvas_video, 0.82, 0, dst=canvas_video)
        cv2.line(canvas_video, (0, y), (VIDEO_W - 1, y), COL_TRIPWIRE, 1)


def draw_counts_hud(canvas_video, count_in, count_out):
    """Single-line top HUD: IN n | OUT n | NET n, each segment in its own colour."""
    net = count_in - count_out
    segments = [
        (f"IN {count_in}",   (80, 220, 80)),
        ("|",                (160, 160, 160)),
        (f"OUT {count_out}", (80, 80, 220)),
        ("|",                (160, 160, 160)),
        (f"NET {net}",       (240, 240, 240)),
    ]
    scale, thickness, gap, pad = 0.5, 1, 6, 4
    sizes = [cv2.getTextSize(t, FONT, scale, thickness)[0] for t, _ in segments]
    total_w = sum(w for w, _ in sizes) + gap * (len(segments) - 1)
    text_h  = max(h for _, h in sizes)
    box_w, box_h = total_w + 2 * pad, text_h + 2 * pad
    h, w = canvas_video.shape[:2]
    x0, y0 = w - box_w - 6, h - box_h - 6
    cv2.rectangle(canvas_video,
                  (x0, y0),
                  (x0 + total_w + 2 * pad, y0 + text_h + 2 * pad),
                  (0, 0, 0), -1)
    x = x0 + pad
    base_y = y0 + pad + text_h
    for (text, colour), (w, _) in zip(segments, sizes):
        cv2.putText(canvas_video, text, (x, base_y),
                    FONT, scale, colour, thickness, cv2.LINE_AA)
        x += w + gap


def draw_tracks(canvas_video, tracks):
    """Overlay per-ID boxes on the upscaled VIDEO_W x VIDEO_H canvas.

    Box coords arrive in model-input space (0..MODEL_IN_W x 0..MODEL_IN_H).
    Undo the vertical letterbox (subtract LETTERBOX_TOP), then scale to the
    display resolution. Width didn't get padded so just scales 1:1.
    """
    sx = VIDEO_W / FRAME_WIDTH
    sy = VIDEO_H / FRAME_HEIGHT
    for t in tracks:
        x0 = int(t["x0"] * sx)
        x1 = int(t["x1"] * sx)
        y0 = int((t["y0"] - LETTERBOX_TOP) * sy)
        y1 = int((t["y1"] - LETTERBOX_TOP) * sy)

        x0 = max(0, min(VIDEO_W - 1, x0)); x1 = max(0, min(VIDEO_W - 1, x1))
        y0 = max(0, min(VIDEO_H - 1, y0)); y1 = max(0, min(VIDEO_H - 1, y1))
        if x1 <= x0 or y1 <= y0:
            continue

        colour = id_to_colour(t["id"])
        cv2.rectangle(canvas_video, (x0, y0), (x1, y1), colour, 2)

        label = f"#{t['id']} {t['score']:.2f}"
        (lw, lh), _ = cv2.getTextSize(label, FONT, 0.45, 1)
        ly = max(lh + 2, y0 - 4)
        cv2.rectangle(canvas_video,
                      (x0, ly - lh - 4), (x0 + lw + 4, ly + 2),
                      colour, -1)
        draw_text(canvas_video, label, (x0 + 2, ly - 2), 0.45, (15, 15, 15), 1)


def compose_dashboard(video_bgr, tracks, panel, meta):
    canvas = np.full((VIDEO_H, VIDEO_W + PANEL_W, 3), COL_BG, dtype=np.uint8)
    enhanced = enhance_video(video_bgr)
    canvas[:, :VIDEO_W] = cv2.resize(
        enhanced, (VIDEO_W, VIDEO_H), interpolation=cv2.INTER_LANCZOS4
    )
    video_view = canvas[:, :VIDEO_W]
    draw_tripwire(video_view,
                  meta["tw_vertical"], meta["tw_pos"], meta["tw_deadband"])
    draw_tracks(video_view, tracks)
    draw_counts_hud(video_view, meta["count_in"], meta["count_out"])
    canvas[:, VIDEO_W:] = panel

    return canvas


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

def main():
    dev = open_device()
    print("USB device connected. ESC to quit.")
    cv2.namedWindow(WINDOW_TITLE, cv2.WINDOW_AUTOSIZE)

    leftover = bytearray()

    t_window = time.time()
    frames_this_window = 0
    stream_fps = 0.0
    last_infer_count = None
    distinct_inferences_this_window = 0
    inference_fps = 0.0

    while True:
        meta, rgb565, tracks, leftover = read_frame(dev, leftover)
        if rgb565 is None:
            continue

        frames_this_window += 1
        cur_count = meta["inference_count"]
        if last_infer_count is None:
            last_infer_count = cur_count
        else:
            distinct_inferences_this_window += (cur_count - last_infer_count) & 0xFF
            last_infer_count = cur_count

        now = time.time()
        elapsed = now - t_window
        if elapsed >= 1.0:
            stream_fps = frames_this_window / elapsed
            inference_fps = distinct_inferences_this_window / elapsed
            frames_this_window = 0
            distinct_inferences_this_window = 0
            t_window = now

        video_bgr = rgb565_to_bgr(rgb565)
        panel = render_panel(meta, stream_fps, inference_fps)
        canvas = compose_dashboard(video_bgr, tracks, panel, meta)
        cv2.imshow(WINDOW_TITLE, canvas)

        if cv2.waitKey(1) & 0xFF == 27:
            break

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
