"""
Live RGB565 camera viewer for the PersonPresence USB stream.

Stream format (matches My_MCC_Config/src/app_usb.c after the dashboard upgrade):
  per frame: 16-byte header followed by 38400 bytes of payload
    header:
      [0..3]   marker AA 55 AA 55
      [4..7]   frame counter (u32 LE)
      [8]      person flag (1 = person, 0 = no person)
      [9]      person logit (int8)
      [10]     no-person logit (int8)
      [11]     reserved
      [12..15] inference duration in microseconds (u32 LE)
    payload: 160 x 120 little-endian RGB565 (= IMG_WIDTH * IMG_HEIGHT * 2)
  payload arrives as 75 bulk-IN packets of 512 B at high speed.

This viewer ignores the ML fields in the header and just shows the video; see
Dashboard_via_USB.py for the full inference dashboard.
"""

import time

import cv2
import numpy as np
import usb.backend.libusb1
import usb.core
import usb.util


# ---- USB configuration (must match the firmware's USB descriptors) ----
VID   = 0x04D8
PID   = 0x0053
EP_IN = 0x81
PKT   = 512                              # high-speed bulk packet size

# ---- Frame layout (must match app_usb.c) ----
FRAME_WIDTH        = 160
FRAME_HEIGHT       = 120
FRAME_BPP          = 2                   # RGB565
FRAME_PAYLOAD_SIZE = FRAME_WIDTH * FRAME_HEIGHT * FRAME_BPP   # 38400
FRAME_HEADER_SIZE  = 16
MARKER             = bytes([0xAA, 0x55, 0xAA, 0x55])

DISPLAY_SIZE = (480, 360)                # 3x upscale (160x120 -> 480x360)
DISPLAY_INTERP = cv2.INTER_CUBIC         # smooth upscale; use INTER_NEAREST for raw pixels


def open_device():
    backend = usb.backend.libusb1.get_backend(
        find_library=lambda _: "./libusb-1.0.dll"
    )
    dev = usb.core.find(idVendor=VID, idProduct=PID, backend=backend)
    if dev is None:
        raise RuntimeError(f"USB device {VID:04x}:{PID:04x} not found")
    dev.set_configuration()
    return dev


def rgb565_to_bgr(payload: bytes) -> np.ndarray:
    """Decode a little-endian RGB565 payload into an OpenCV BGR image."""
    arr = np.frombuffer(payload, dtype=np.uint8).reshape(
        (FRAME_HEIGHT, FRAME_WIDTH, 2)
    )
    return cv2.cvtColor(arr, cv2.COLOR_BGR5652BGR)


def read_frame(dev, leftover: bytearray):
    """Read one frame from the device.

    Returns (frame_id, payload_bytes, new_leftover). The leftover buffer carries
    bytes already received past the end of the previous frame so we don't drop
    sync between calls.
    """
    # 1. Find the next header marker, reading more bulk data as needed.
    buf = leftover
    while True:
        pos = buf.find(MARKER)
        if pos != -1 and len(buf) - pos >= FRAME_HEADER_SIZE:
            header = bytes(buf[pos:pos + FRAME_HEADER_SIZE])
            frame_id = int.from_bytes(header[4:8], "little")
            # Anything after the header is the start of the payload.
            payload = bytearray(buf[pos + FRAME_HEADER_SIZE:])
            break

        # Drop everything except the last 3 bytes — the marker may straddle
        # the boundary between this read and the next.
        if len(buf) > 3:
            del buf[:-3]

        try:
            chunk = bytes(dev.read(EP_IN, PKT, timeout=1000))
        except usb.core.USBTimeoutError:
            print("Timeout waiting for header — resyncing...")
            continue
        buf.extend(chunk)

    # 2. Pull the rest of the payload.
    while len(payload) < FRAME_PAYLOAD_SIZE:
        try:
            chunk = bytes(dev.read(EP_IN, PKT, timeout=1000))
        except usb.core.USBTimeoutError:
            print("Timeout mid-frame — resyncing...")
            return None, None, bytearray()
        payload.extend(chunk)

    # 3. Anything past the payload belongs to the next frame's header search.
    new_leftover = bytearray(payload[FRAME_PAYLOAD_SIZE:])
    payload = bytes(payload[:FRAME_PAYLOAD_SIZE])
    return frame_id, payload, new_leftover


def main():
    dev = open_device()
    print("USB camera connected. ESC to quit.")

    leftover = bytearray()
    last_time = time.time()
    frame_count = 0
    last_id = None

    while True:
        frame_id, payload, leftover = read_frame(dev, leftover)
        if payload is None:
            continue

        img = rgb565_to_bgr(payload)
        cv2.imshow(
            "PersonPresence USB stream",
            cv2.resize(img, DISPLAY_SIZE, interpolation=DISPLAY_INTERP),
        )

        frame_count += 1
        now = time.time()
        if now - last_time >= 1.0:
            fps = frame_count / (now - last_time)
            dropped = (
                0 if last_id is None
                else max(0, frame_id - last_id - frame_count)
            )
            print(f"Frame ID: {frame_id}  FPS: {fps:4.1f}  dropped(approx): {dropped}")
            last_id = frame_id
            frame_count = 0
            last_time = now

        if cv2.waitKey(1) & 0xFF == 27:   # ESC
            break

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
