# PERSONPRESENCE_VWW1 — session handoff

Sibling project of `d:/TinyEngine/PersonPresence/` (which runs MCUNet-VWW0).
This one runs **MCUNet-VWW1** (`mcunet-5fps_vww`, 80×80×3 input, 2-class
person/no-person). Created because VWW0 produced near-zero / sign-flipping
logits on real OV7670 frames while triggering confident "person" on
uniform/OOD inputs (see `PersonPresence/CONTEXT.md` §Open bug).

## Goal

Get a usable person-presence signal on PIC32CZ-CA + OV7670, accepting the
~3× inference-time hit relative to VWW0 in exchange for a more discriminative
classifier.

## How this project differs from PersonPresence/

Only the model + a couple of constants. Same camera path, same TinyEngine
runtime, same 2-logit argmax in `app_ml.cpp`, same smoke-test harness, same
input-buffer stats debug print.

| | VWW0 (PersonPresence) | **VWW1 (this project)** |
|---|---|---|
| Net id | `mcunet-vww0` (alias `mcunet-10fps_vww`) | `mcunet-vww1` (alias `mcunet-5fps_vww`) |
| Input | 64×64×3 | **80×80×3** |
| Output | 2 int8 logits | 2 int8 logits |
| Layers | 51 | 41 |
| `PEAK_MEM` (SRAM activations) | 60,800 B | **93,992 B** |
| `MODEL_SIZE` (flash weights) | 399,656 B | **469,432 B** |
| Expected inference | <70 ms | TBD (target 150–250 ms) |

VWW1 is the "5fps on STM32F746" model per `mcunet/mcunet/model_zoo.py`. The
"144×144" figure quoted in `PersonPresence/CONTEXT.md` §Fallback options was
incorrect — actual input is 80×80 (verified via `Interpreter.get_input_details()`
on the downloaded tflite).

## Quantization params (read from the tflite, not derived)

- **Input** tensor: `shape=[1,80,80,3]`, `scale=0.007843137`, `zero_point=-1`.
  Same scale/zp as VWW0. Our `int8 = uint8 - 128` mapping in
  `rgb565_to_modelinput_vww` is within ~1 LSB of the correct map and was
  carried over unchanged.
- **Output** tensor: `shape=[1,2]`, `scale=0.11386775`, `zero_point=-1`.
  Real logit = `(q - (-1)) × 0.11386 = (q + 1) × 0.11386`. The argmax decision
  is unaffected by zp (a constant +1 shift cancels out), so the existing
  `out[0] > out[1]` test in `app_ml.cpp` is still correct.

## What was done to bootstrap this project

1. `cp -r d:/TinyEngine/PersonPresence d:/TinyEngine/PERSONPRESENCE_VWW1`,
   then deleted `_build/`, `out/`, and the stale
   `My_MCC_Config/src/TinyEngine/codegen/{Include,Source}/`.
2. Edited `d:/TinyEngine/tinysrc/examples/vww.py` line 25
   (`mcunet-vww0` → `mcunet-vww1`) and downloaded
   `mcunet-5fps_vww.tflite` from
   `https://hanlab18.mit.edu/projects/tinyml/mcunet/release/` directly into
   `d:/TinyEngine/tinysrc/assets/`. (download_tflite() bypassed because torch
   isn't installed.)
3. Ran codegen: `python -c "from code_generator.CodegenUtilTFlite import GenerateSourceFilesFromTFlite; print(GenerateSourceFilesFromTFlite('assets/mcunet-5fps_vww.tflite', life_cycle_path='./lifecycle.png'))"` from `tinysrc/`.
4. Copied `tinysrc/codegen/{Include,Source}/` into
   `My_MCC_Config/src/TinyEngine/codegen/`. **19 source files** (3×3, 5×5,
   7×7 depthwise kernels in stride1/stride2 × 3 mask variants + `genModel.c`)
   — exactly the same set vww0 produced, so [cmake/ML_OV7670_GFX/default/.generated/file.cmake](cmake/ML_OV7670_GFX/default/.generated/file.cmake)
   needed no edits.
5. [My_MCC_Config/src/app_cam.h](My_MCC_Config/src/app_cam.h) — `MODEL_IN_H/W`
   changed from 64 to **80**. Doc comments refreshed.
6. [My_MCC_Config/src/app_cam.c](My_MCC_Config/src/app_cam.c) — resize formula
   `src = (dst * IMG_DIM) / MODEL_DIM` is symmetric, kept as-is. Comments
   refreshed (`120/80`, "downscale 1.5×").

## Status

Untested on hardware. Build expected to succeed without source-list changes
since codegen emitted the same kernel set.

## Verification (next session)

1. **Build clean** — MPLAB X / cmake from this directory. No missing-symbol
   errors expected.
2. **Smoke test** — trigger `APP_ML_STATE_SMOKE_TEST`. Expected: midgray
   gives near-neutral logits (margin < 10), black/white may be confidently
   OOD. Confirms quantization round-trip.
3. **Input-buffer stats** — capture lit-person / blank-wall / lens-covered
   readings as before. Expected: similar means/ranges as VWW0 (camera path
   unchanged), but **person-scene logit margins should now be clearly
   positive (target ≥ +15)** vs. VWW0's noisy near-zero output.
4. **Inference timing** — log around `invoke()`. Target: ~150–250 ms.

## If VWW1 also looks weak

In order of cheapness:
- **BGR-vs-RGB swap** at [app_cam.c:442-444](My_MCC_Config/src/app_cam.c#L442-L444)
  (swap `out_row[dst_x*3 + 0]` and `out_row[dst_x*3 + 2]`). 1-line A/B.
- **OV7670 register reconfig** — investigate AGC / exposure / gamma settings
  if the input-buffer stats show range collapse on contrasty scenes.
- **Bilinear resize** — replace nearest-neighbor in
  `rgb565_to_modelinput_vww` with fixed-point bilinear; closer to PIL
  bilinear used during VWW training.
- **Fine-tune VWW1 on OV7670 captures** — `DatasetCapture_via_USB.py` in
  this project already streams frames over USB CDC.
- **`mcunet-vww2`** ("320kb-1mb_vww") — different SRAM/flash trade.

## Key files

- [My_MCC_Config/src/app_ml.cpp](My_MCC_Config/src/app_ml.cpp) —
  `run_person_classification()` per-frame pipeline (unchanged from VWW0
  version); `APP_ML_RunSmokeTest()` synthetic-input harness; debug
  input-buffer stats block (every 32 frames).
- [My_MCC_Config/src/app_cam.c](My_MCC_Config/src/app_cam.c#L416) —
  `rgb565_to_modelinput_vww()` (line 416 onward).
- [My_MCC_Config/src/app_cam.h](My_MCC_Config/src/app_cam.h#L182) —
  `MODEL_IN_H/W/C` (now 80/80/3).
- [My_MCC_Config/src/TinyEngine/codegen/Include/genModel.h](My_MCC_Config/src/TinyEngine/codegen/Include/genModel.h) —
  `PEAK_MEM=93992`, `MODEL_SIZE=469432`, weight blobs.
- [My_MCC_Config/src/TinyEngine/codegen/Source/genModel.c](My_MCC_Config/src/TinyEngine/codegen/Source/genModel.c) —
  `getInput()` returns `&buffer0[25600]`; final layer outputs 1×1×2 at the
  arena address bound to `NNoutput`.
- [cmake/ML_OV7670_GFX/default/.generated/file.cmake](cmake/ML_OV7670_GFX/default/.generated/file.cmake) — source list (no changes needed).
- `d:/TinyEngine/tinysrc/examples/vww.py` — codegen entry point (now set to
  `mcunet-vww1`).
- `d:/TinyEngine/tinysrc/assets/mcunet-5fps_vww.tflite` — source model
  (manually downloaded, 621 KB).

## How to regenerate codegen

```bash
cd d:/TinyEngine/tinysrc
python -c "from code_generator.CodegenUtilTFlite import GenerateSourceFilesFromTFlite; print(GenerateSourceFilesFromTFlite('assets/mcunet-5fps_vww.tflite', life_cycle_path='./lifecycle.png'))"
cp -r codegen/Include codegen/Source d:/TinyEngine/PERSONPRESENCE_VWW1/My_MCC_Config/src/TinyEngine/codegen/
```

If switching to a different MCUNet model later, see
`PersonPresence/CONTEXT.md` §"How to regenerate codegen" — same procedure
applies, plus updating `MODEL_IN_H/W` and re-checking the kernel source
list against the new `codegen/Source/` directory.
