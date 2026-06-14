# PersonPresence — session handoff

Snapshot of where this work was left off, written so a fresh session can pick up
without re-deriving anything.

---

## Goal

Port the existing PIC32CZ-CA + OV7670 camera vision project from the **MCUNet
YOLO person-detector** (`person-det`, ~330 ms inference) to a faster
classifier — **MCUNet-VWW0** (`mcunet-10fps_vww`, 2-class person/no-person) —
to drop inference time substantially. Keep PersonDetection/ untouched as a
fallback.

## What was done

Created `d:/TinyEngine/PersonPresence/` as a sibling of `PersonDetection/` and
swapped the TinyEngine model + integration code:

1. Duplicated the project tree, then deleted `_build/`, `out/`, and the stale
   `My_MCC_Config/src/TinyEngine/codegen/`.
2. Edited `d:/TinyEngine/tinysrc/examples/vww.py` line 25 (`mcunet-vww1` →
   `mcunet-vww0`) and downloaded `mcunet-10fps_vww.tflite` directly to
   `d:/TinyEngine/tinysrc/assets/`. Ran codegen with
   `python -c "from code_generator.CodegenUtilTFlite import GenerateSourceFilesFromTFlite; GenerateSourceFilesFromTFlite('assets/mcunet-10fps_vww.tflite', life_cycle_path='./lifecycle.png')"`
   from `tinysrc/`. (Bypassed `download_tflite()` because torch isn't installed.)
3. Copied `tinysrc/codegen/{Include,Source}/` into
   `PersonPresence/My_MCC_Config/src/TinyEngine/codegen/`.
4. Rewrote camera preprocessing — see [app_cam.h](My_MCC_Config/src/app_cam.h),
   [app_cam.c](My_MCC_Config/src/app_cam.c). Input changed from 160×128 to
   **64×64×3** with a center-crop (160×120 → 120×120 square) + nearest-neighbor
   downscale. Function renamed to `rgb565_to_modelinput_vww`.
5. Rewrote ML logic — see [app_ml.cpp](My_MCC_Config/src/app_ml.cpp),
   [app_ml.h](My_MCC_Config/src/app_ml.h). YOLO postprocessing /
   `det_post_procesing` / anchors / `det_box` plumbing removed. Replaced with
   2-logit argmax on `getOutput()`: `out[0]=person, out[1]=no-person`.
   `APP_ML_GetTopBoxScore()` → `APP_ML_GetLogitMargin()`.
6. Build cleanup — `cmake/ML_OV7670_GFX/default/.generated/file.cmake`:
   added 12 new 5×5/7×7 depthwise kernel sources from the new codegen, removed
   `yoloOutput.c` line. Deleted `My_MCC_Config/src/TinyEngine/src/yoloOutput.c`.
   `genNN.h` and `yoloOutput.h` left in place — they declare unused symbols but
   don't cause link errors.

## Resource numbers

| | person-det (old) | mcunet-vww0 (new) |
|---|---|---|
| Input | 160×128×3 | **64×64×3** |
| Output | 3× YOLO heads | **2 int8 logits** |
| Layers | 70 | 51 |
| `PEAK_MEM` (SRAM activations) | 257,448 B | **60,800 B**  (−76%) |
| `MODEL_SIZE` (flash weights) | 190,864 B | 399,656 B  (+109%) |
| Measured inference | ~330 ms | **<70 ms** ✓ |

VWW0 is heavier on flash because NAS allocated capacity to channel width
(up to 480ch, 7×7 depthwise kernels) instead of spatial size — classic
classifier shape. SRAM is the scarce resource on PIC32CZ-CA, so this trade
is correct.

## Quantization params (read from the tflite, not derived)

- **Input** tensor: `shape=[1,64,64,3]`, `scale=0.00784314`, `zero_point=-1`.
  Means model expects pixels normalized to roughly [-1, 1]. Our preprocessing
  writes `int8 = uint8 - 128`, which is within ~1 LSB of the correct mapping
  (`q = round((u - 127.5)/127.5 × 127.5 - 1) ≈ u - 128.5`).
- **Output** tensor: `shape=[1,2]`, `scale=0.09819`, `zero_point=0`.
  Real logit = `q × 0.09819`. So an int8 margin of 35 = 3.44 in real logits =
  ~97% softmax confidence.

---

## Status — VWW0 working end-to-end (2026-06-13)

Both the firmware integration and the trained model are healthy. The
model produces clean asymmetric logits per image with all 6 ground-truth
test images PASSing:

```
img[000000000785] logits=[np=-32, p= 34] margin= 66 expected=person     PASS
img[000000004134] logits=[np=-26, p= 27] margin= 53 expected=person     PASS
img[000000004395] logits=[np= -1, p=  2] margin=  3 expected=person     PASS  (low conf — 57%)
img[images__1   ] logits=[np=  8, p= -8] margin=-16 expected=no-person  PASS
img[images__2   ] logits=[np=  7, p= -7] margin=-14 expected=no-person  PASS
img[images      ] logits=[np= 31, p=-31] margin=-62 expected=no-person  PASS
```

Two bugs were found and fixed to get here:

1. **Output index swap** — firmware was reading `out[0]` as person and
   `out[1]` as no-person, but MCUNet's `eval_tflite.py` follows
   `torchvision.datasets.ImageFolder` alphabetical ordering which
   assigns class 0 to "non-person" and class 1 to "person". Fixed in
   [app_ml.cpp](My_MCC_Config/src/app_ml.cpp): `out[0] = no-person`,
   `out[1] = person`.

2. **`test_images.h` generated at the wrong dimensions** — the file
   was emitted by the converter with `--width 80 --height 80` (the
   VWW1 size) instead of 64×64 for VWW0. Each array was 19200 bytes
   (80×80×3) but the firmware `memcpy`s 12288 bytes (64×64×3) into
   `getInput()`, so it copied the first ~51 rows of an 80-wide image
   laid out as if they were 64-wide rows — garbage input. The
   resulting `[+a, −a]` mirror-symmetric output was *not* a model
   bug: with garbage inputs, a 2-class softmax head fires symmetrically
   on whatever low-level statistics survive (the head's two output
   channels are naturally near-antisymmetric — Pearson correlation
   of weight42 channels = −0.913 — because softmax is shift-invariant
   and training uses up that gauge freedom). On real in-domain inputs
   the same head discriminates cleanly.

### Output interpretation

The two int8 values at `getOutput()` are quantized logits. Multiply by
the model's output scale (`0.09819`, zp=0) to get real-valued logits.
`margin = out[1] − out[0]` in int8 units; sign = decision, magnitude =
confidence. Rule of thumb: int8 margin ≈ 10 → ~73% softmax, ≈ 20 →
~88%, ≈ 50 → ~99%. If you want to suppress borderline triggers (like
the 4395 case above with margin 3 = ~57%), threshold on
`APP_ML_GetLogitMargin()` before reporting a presence event.

### Image-test harness usage

Set `ML_USE_TEST_IMAGES = 1` in [app_ml.h](My_MCC_Config/src/app_ml.h),
build + flash, observe UART. Set back to 0 to return to live-camera
mode. Regenerate `test_images.h` with:

```bash
cd d:/TinyEngine/PersonPresence
python "ML Model/ConvertImagesToHex_VWW0.py" \
    --in-dir <ImageFolder root with person/, non-person/ subdirs> \
    --n-per-class 3 \
    --out "My_MCC_Config/src/test_images.h"
```

The converter defaults to 64×64 for VWW0; no `--width`/`--height` flags
are needed for this project. After regenerating, the file header should
read `Layout: 64 rows x 64 cols x 3 ch HWC int8` and arrays should be
sized `[12288]`. (If you ever swap to a different MCUNet variant, pass
`--width 80 --height 80` for VWW1 or `--width 144 --height 144` for
VWW2.)

---

## Fallback options if a faster/smaller model is needed

- **`mcunet-vww1`** — verified working in the sibling project
  `d:/TinyEngine/PERSONPRESENCE_VWW1/` (80×80×3 input, ~150-250 ms,
  93 KB SRAM). Larger input/SRAM than VWW0 but fewer flash bytes.
- `mcunet-vww2` ("320kb-1mb_vww") — 1 MB flash budget instead of 2 MB,
  144×144 input.
- Fine-tune VWW0 on OV7670-captured frames — `DatasetCapture_via_USB.py`
  in this project already streams frames over USB CDC for dataset
  building. Useful if real-camera frames are out-of-domain vs.
  COCO-derived training data.

## Key files

- [My_MCC_Config/src/app_ml.cpp](My_MCC_Config/src/app_ml.cpp) —
  `run_person_classification()` is the per-frame pipeline
  (`out[0]=no-person, out[1]=person`); `APP_ML_GetLogitMargin()` returns
  `out[1] - out[0]` in int8 units (positive → person, ≈10 → ~73%,
  ≈20 → ~88%, ≈50 → ~99% softmax); `APP_ML_RunSmokeTest()` the
  synthetic-input harness (midgray/black/white); `APP_ML_RunImageTest()`
  the camera-bypass verifier driven by `test_images[]` from
  `test_images.h`.
- [My_MCC_Config/src/app_ml.h](My_MCC_Config/src/app_ml.h) —
  `ML_USE_TEST_IMAGES` compile flag toggling the image-test boot path,
  `APP_ML_STATE_IMAGE_TEST` enum value.
- [My_MCC_Config/src/test_images.h](My_MCC_Config/src/test_images.h) —
  generated; 6 × 12288 B int8 arrays + manifest. Regenerate via
  `ML Model/ConvertImagesToHex_VWW0.py` (see §Image-test harness usage above).
- [ML Model/ConvertImagesToHex_VWW0.py](ML Model/ConvertImagesToHex_VWW0.py) —
  PIL-based ImageFolder → C-header tool. Parameterized for VWW0 (64×64)
  and VWW1 (80×80) via `--width`/`--height`.
- [My_MCC_Config/src/app_cam.c](My_MCC_Config/src/app_cam.c#L416) —
  `rgb565_to_modelinput_vww()` (line 416 onward).
- [My_MCC_Config/src/app_cam.h](My_MCC_Config/src/app_cam.h#L182) —
  `MODEL_IN_H/W/C` defines.
- [My_MCC_Config/src/TinyEngine/codegen/Include/genModel.h](My_MCC_Config/src/TinyEngine/codegen/Include/genModel.h) —
  `PEAK_MEM`, `MODEL_SIZE`, weight blobs.
- [My_MCC_Config/src/TinyEngine/codegen/Source/genModel.c](My_MCC_Config/src/TinyEngine/codegen/Source/genModel.c) —
  `getInput()` returns `&buffer0[16384]`; final layer outputs 1×1×2 at
  `&buffer0[160]` = `NNoutput`.
- [cmake/ML_OV7670_GFX/default/.generated/file.cmake](cmake/ML_OV7670_GFX/default/.generated/file.cmake) — source list.
- `d:/TinyEngine/tinysrc/examples/vww.py` — codegen entry point (currently
  edited to use `mcunet-vww0`).
- `d:/TinyEngine/tinysrc/assets/mcunet-10fps_vww.tflite` — source model
  (manually downloaded, 543 KB).

## How to regenerate codegen

```bash
cd d:/TinyEngine/tinysrc
python -c "from code_generator.CodegenUtilTFlite import GenerateSourceFilesFromTFlite; print(GenerateSourceFilesFromTFlite('assets/mcunet-10fps_vww.tflite', life_cycle_path='./lifecycle.png'))"
# Output writes to tinysrc/codegen/{Include,Source}/
cp -r codegen/Include codegen/Source ../PersonPresence/My_MCC_Config/src/TinyEngine/codegen/
```

If switching to a different MCUNet model (vww1/vww2/in*), download its tflite
from `https://hanlab18.mit.edu/projects/tinyml/mcunet/release/<net_name>.tflite`
(net names listed in `tinysrc/mcunet/mcunet/model_zoo.py`) and re-run codegen.
You'll need to:
- Re-read `MODEL_IN_W/H` from the new `genModel.c` first conv (different models
  have different input sizes — vww1 is 144×144).
- Update `MODEL_IN_H/W` in `app_cam.h` and re-tune the resize math in
  `rgb565_to_modelinput_vww`.
- Re-check the generated `Source/depthwise_kernel*.c` set against the CMake
  source list and add/remove as needed.
