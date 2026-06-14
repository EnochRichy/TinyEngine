"""
PersonPresence ML inference dashboard.

Reads the augmented USB stream from My_MCC_Config (16-byte header + 38400-byte
RGB565 payload) and renders a single OpenCV window with:
  - upscaled live video
  - large PERSON / NO PERSON decision badge with logit margin
  - live metrics: stream FPS, inference time, inference FPS, frame counter
  - static model info: MCUNet-VWW0 name, shape, parameter count, MAC count

Header layout (must mirror app_usb.c FRAME_HEADER_SIZE=16):
  [0..3]   marker AA 55 AA 55
  [4..7]   frame counter (u32 LE)
  [8]      person flag (1 = person, 0 = no person)
  [9]      person logit (int8)
  [10]     no-person logit (int8)
  [11]     reserved
  [12..15] last invoke() duration in microseconds (u32 LE)
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
FRAME_PAYLOAD_SIZE = FRAME_WIDTH * FRAME_HEIGHT * FRAME_BPP   # 38400
FRAME_HEADER_SIZE  = 16
MARKER             = bytes([0xAA, 0x55, 0xAA, 0x55])

# ---- Static model metadata (MCUNet-VWW0 person presence) ----
# Source: MCUNet (mit-han-lab) tinyml-perf VWW track. Edit if you redeploy
# a different model. None of these are computed at runtime — they describe
# the deployed graph that genNN.h was generated for.
MODEL_INFO = {
    "Model":   "MCUNet-VWW0",
    "Task":    "Visual Wake Words",
    "Input":   "64x64x3 (HWC int8)",
    "Output":  "2 logits (no-person, person)",
    "Params":  "~73 K",
    "MACs":    "~5.4 M",
    "Quant":   "int8 (per-tensor)",
    "Engine":  "TinyEngine (codegen)",
}

# ---- Display sizing ----
VIDEO_W, VIDEO_H = 480, 360
PANEL_W = 320
WINDOW_TITLE = "PersonPresence inference dashboard"

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
    frame_id, person, p_logit, np_logit, inf_count, inf_us = struct.unpack(
        "<IBbbBI", hdr[4:16]
    )
    return {
        "frame_id":     frame_id,
        "person":       bool(person),
        "person_logit": int(p_logit),
        "noperson_logit": int(np_logit),
        "margin":       int(p_logit) - int(np_logit),
        "inference_us": int(inf_us),
        "inference_count": int(inf_count),
    }


def read_frame(dev, leftover: bytearray):
    """Return (header_dict, payload_bytes, new_leftover)."""
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
            return None, None, bytearray()

    new_leftover = bytearray(payload[FRAME_PAYLOAD_SIZE:])
    return meta, bytes(payload[:FRAME_PAYLOAD_SIZE]), new_leftover


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
COL_PERSON   = (60, 200, 60)
COL_NOPERSON = (60, 60, 220)
COL_ACCENT   = (200, 160, 60)


def draw_text(img, text, org, scale=0.5, colour=COL_TEXT, thickness=1):
    cv2.putText(img, text, org, FONT, scale, colour, thickness, cv2.LINE_AA)


def render_panel(meta, stream_fps, inference_fps):
    panel = np.full((VIDEO_H, PANEL_W, 3), COL_PANEL, dtype=np.uint8)

    # ---- Decision badge ----
    person = meta["person"]
    badge_colour = COL_PERSON if person else COL_NOPERSON
    label = "PERSON" if person else "NO PERSON"
    cv2.rectangle(panel, (10, 10), (PANEL_W - 10, 70), badge_colour, -1)
    (tw, th), _ = cv2.getTextSize(label, FONT, 0.95, 2)
    draw_text(panel, label,
              ((PANEL_W - tw) // 2, 10 + (60 + th) // 2),
              scale=0.95, colour=(15, 15, 15), thickness=2)

    margin = meta["margin"]
    margin_str = f"margin {margin:+d}    logits p={meta['person_logit']:+d} np={meta['noperson_logit']:+d}"
    draw_text(panel, margin_str, (12, 88), 0.42, COL_DIM)

    # ---- Live metrics ----
    y = 116
    draw_text(panel, "LIVE METRICS", (12, y), 0.45, COL_ACCENT, 1); y += 22
    inf_ms = meta["inference_us"] / 1000.0
    rows = [
        ("Stream FPS",  f"{stream_fps:6.1f}"),
        ("Inference",   f"{inf_ms:6.2f} ms"),
        ("Inference FPS", f"{inference_fps:6.1f}" if inference_fps else "  ---  "),
        ("Frame ID",    f"{meta['frame_id']}"),
    ]
    for label, value in rows:
        draw_text(panel, label,  (16, y), 0.45, COL_TEXT)
        draw_text(panel, value,  (PANEL_W - 110, y), 0.45, COL_TEXT)
        y += 20

    # ---- Model info ----
    y += 6
    draw_text(panel, "MODEL", (12, y), 0.45, COL_ACCENT, 1); y += 22
    for label, value in MODEL_INFO.items():
        draw_text(panel, label, (16, y), 0.42, COL_DIM)
        draw_text(panel, value, (PANEL_W - 180, y), 0.42, COL_TEXT)
        y += 18

    return panel


def compose_dashboard(video_bgr, panel):
    canvas = np.full((VIDEO_H, VIDEO_W + PANEL_W, 3), COL_BG, dtype=np.uint8)
    canvas[:, :VIDEO_W] = cv2.resize(
        video_bgr, (VIDEO_W, VIDEO_H), interpolation=cv2.INTER_CUBIC
    )
    canvas[:, VIDEO_W:] = panel

    # decision border around video
    return canvas


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

def main():
    dev = open_device()
    print("USB device connected. ESC to quit.")
    cv2.namedWindow(WINDOW_TITLE, cv2.WINDOW_AUTOSIZE)

    leftover = bytearray()

    # FPS / inference-rate timekeeping
    t_window = time.time()
    frames_this_window = 0
    stream_fps = 0.0
    last_infer_count = None
    distinct_inferences_this_window = 0
    inference_fps = 0.0

    while True:
        meta, payload, leftover = read_frame(dev, leftover)
        if payload is None:
            continue

        frames_this_window += 1
        cur_count = meta["inference_count"]
        if last_infer_count is None:
            last_infer_count = cur_count
        else:
            # u8 wraps at 256; subtract mod 256 to count increments correctly
            distinct_inferences_this_window += (cur_count - last_infer_count) & 0xFF
            last_infer_count = cur_count

        # Refresh per-second metrics
        now = time.time()
        elapsed = now - t_window
        if elapsed >= 1.0:
            stream_fps = frames_this_window / elapsed
            inference_fps = distinct_inferences_this_window / elapsed
            frames_this_window = 0
            distinct_inferences_this_window = 0
            t_window = now

        video_bgr = rgb565_to_bgr(payload)
        panel = render_panel(meta, stream_fps, inference_fps)
        canvas = compose_dashboard(video_bgr, panel)
        cv2.imshow(WINDOW_TITLE, canvas)

        if cv2.waitKey(1) & 0xFF == 27:
            break

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
