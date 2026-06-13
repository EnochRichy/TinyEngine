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

## Open bug — model always says "person"

### Symptoms

Real frames consistently produce mirror-symmetric logits like `[+a, −a]` with
margins 19–53 (all "person"), even with the camera lens covered.

### Smoke-test result (decisive)

`APP_ML_RunSmokeTest()` was added (already wired into [app_ml.cpp](My_MCC_Config/src/app_ml.cpp) and reachable
via `APP_ML_STATE_SMOKE_TEST` in the state machine). Three deterministic patterns
fed straight into `getInput()` bypass the camera path entirely:

```
smoke[midgray] logits=[4, -5]   margin=9   real=0.88   → near-neutral ✓
smoke[black]   logits=[17, -18] margin=35  real=3.44   → confident person (OOD)
smoke[white]   logits=[12, -12] margin=24  real=2.36   → confident person (OOD)
```

**The midgray near-neutral output proves the deployed model math is correct.**
Confident-person on solid black/white is normal out-of-distribution behavior —
ImageNet-style classifiers always misbehave on uniform synthetic inputs.

### What this implies

The bug is **not** in the codegen or quantization. Real frames must be producing
pixel statistics that look as out-of-distribution to the network as solid black
does (which gave margin 35, very close to the user's real-frame margins).

### Next diagnostic — instrument the input buffer

The exact next step. Add this right after the `rgb565_to_modelinput_vww()` call
in `run_person_classification()` (already inserted in
[app_ml.cpp](My_MCC_Config/src/app_ml.cpp)):

```c
static int dbg_count = 0;
if ((dbg_count++ & 0x1F) == 0) {                       /* every 32 frames */
    int rmin=127, rmax=-128, gmin=127, gmax=-128, bmin=127, bmax=-128;
    long rsum=0, gsum=0, bsum=0;
    const int N = MODEL_IN_H * MODEL_IN_W;             /* 4096 px */
    for (int i = 0; i < N; ++i) {
        int r = in[i*3+0], g = in[i*3+1], b = in[i*3+2];
        if (r<rmin) rmin=r; if (r>rmax) rmax=r; rsum+=r;
        if (g<gmin) gmin=g; if (g>gmax) gmax=g; gsum+=g;
        if (b<bmin) bmin=b; if (b>bmax) bmax=b; bsum+=b;
    }
    printf("in stats: R[%d..%d mean=%ld] G[%d..%d mean=%ld] B[%d..%d mean=%ld]  px[0]=(%d,%d,%d)\r\n",
           rmin,rmax,rsum/N, gmin,gmax,gsum/N, bmin,bmax,bsum/N,
           in[0],in[1],in[2]);
}
```

Then capture three readings:

1. Camera pointing at a well-lit person.
2. Camera pointing at a plain wall.
3. Lens fully covered.

### What the readings would mean

- **Healthy**: each channel spans ~150+ int8 values; means differ between
  scenes; lens-covered means cluster near −100.
- **Pixel-range collapse** (range < 50 even on contrasty scenes): OV7670 AGC /
  exposure / gamma is stuck — needs sensor-register reconfiguration.
- **R and B means swapped vs. expectation**: OV7670 is emitting **BGR565**
  not RGB565. Quick A/B fix is to swap R and B at
  [app_cam.c:442-444](My_MCC_Config/src/app_cam.c) (swap the
  `out_row[dst_x*3 + 0]` and `out_row[dst_x*3 + 2]` assignments).
- **All three channel means nearly identical in every scene**: camera is
  effectively grayscale.

### Likely root causes (ranked)

1. **OV7670 register config** producing images stylistically unlike VWW
   training data (COCO subset). Most common: AGC clamping the dynamic range,
   or wrong gamma curve.
2. **Channel order BGR vs RGB**.
3. **64×64 nearest-neighbor downsample is too aggressive** — VWW training
   used PIL bilinear. Possible but unlikely to cause this severity.

---

## Fallback options if VWW0 can't be salvaged

- `mcunet-vww1` ("5fps_vww", 144×144 input) — better accuracy, ~200 ms
  inference, larger SRAM. Same `vww.py` codegen path, change net id.
- `mcunet-vww2` ("320kb-1mb_vww") — 1 MB flash budget instead of 2 MB,
  smaller weights than vww0, slightly slower than vww0.
- Fine-tune VWW0 on OV7670-captured frames — `DatasetCapture_via_USB.py`
  in this project already streams frames over USB CDC for dataset building.
  Out of scope for this session.

## Key files

- [My_MCC_Config/src/app_ml.cpp](My_MCC_Config/src/app_ml.cpp) —
  `run_person_classification()` is the per-frame pipeline; `APP_ML_RunSmokeTest()`
  the synthetic-input harness.
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
