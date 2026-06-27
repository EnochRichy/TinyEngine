# Gemini prompt — TinyEngine vs. TFLite Micro deck

Copy everything between the `=== BEGIN PROMPT ===` and `=== END PROMPT ===`
markers below into Gemini (or any large LLM). Gemini will return a
Google-Slides outline you can paste into Slides via **File → Import** or
**Tools → Outline view → paste**. Each `# Slide N — Title` becomes a slide,
each bullet becomes a body line, and the `Speaker notes:` paragraph goes
into that slide's notes pane.

---

=== BEGIN PROMPT ===

You are an embedded-ML staff engineer giving a 60-minute talk to a room of
peers who already ship CNNs on Cortex-M MCUs using TensorFlow Lite Micro
(TFLM). They know FlatBuffers, `MicroInterpreter`, `AllOpsResolver`,
tensor arenas, and CMSIS-NN. **Do not re-explain those.** The point of the
talk is:

1. Why TinyEngine (from MIT HAN Lab's MCUNet project) exists alongside TFLM.
2. How its architecture differs — *generated code* vs. *interpreted graph*.
3. Where its SRAM and latency wins actually come from.
4. What it costs to deploy on a real Cortex-M project today.

Produce the deck as a **Google Slides outline**, exactly this format per
slide:

```
# Slide N — <Slide Title>
- bullet 1
- bullet 2
- bullet 3
(3 to 6 bullets total — short phrases, not sentences)

Speaker notes: <one paragraph, 3–6 sentences, what the presenter actually
says out loud. Concrete, no filler. Reference the bullets but don't repeat
them verbatim.>
```

## Hard constraints

- **Total slides: 33.** Use exactly the section budget below — do not
  expand one section by stealing from another.
- **No emojis.** ASCII diagrams are fine and encouraged where they
  illustrate flow or memory layout. Use box-drawing only when it adds
  clarity.
- **Code blocks** must be fenced as ` ```c ` and kept under 12 lines.
- **No preamble or sign-off** — output the deck only. Do not say
  "Sure, here is your presentation". Start at `# Slide 1` and end after
  the speaker notes for slide 33.
- **Tone:** technical-peer, not marketing. Acknowledge tradeoffs.
  Engineers in the room will push back if you oversell.

## Section budget (must match)

| #  | Section                                            | Slides |
|----|----------------------------------------------------|--------|
| A  | Title + framing                                    | 1      |
| B  | Embedded-AI constraint reality                     | 2      |
| C  | TFLM refresher — overhead only                     | 2      |
| D  | What is TinyEngine + MCUNet co-design              | 2      |
| E  | TinyEngine architecture (codegen pipeline)         | 3      |
| F  | TFLM vs. TinyEngine execution model                | 2      |
| G  | Optimization deep-dives (one per slide, 6 slides)  | 6      |
| H  | Memory-planner worked example                      | 2      |
| I  | Cortex-M7 generated-code walkthrough               | 3      |
| J  | Deployment flow + ARM MCU project integration      | 3      |
| K  | Performance comparison table                       | 1      |
| L  | **Case study — VWW0 on PIC32CZ-CA**                | 2      |
| M  | Limitations + when not to pick TinyEngine          | 1      |
| N  | Future directions (MCUNetV2 / V3 / on-device)      | 1      |
| O  | Key takeaways                                      | 1      |
| P  | Q&A                                                | 1      |
|    | **Total**                                          | **33** |

## Per-slide directives

Below, each slide has a **Title hint** and a **Must cover** list. You may
phrase the title however reads best, but every "must cover" point has to
appear (in a bullet or in speaker notes). Diagram instructions in `[...]`
must be rendered as ASCII inside the bullet area.

### A. Title + framing

**Slide 1 — Title.** Title: "TinyEngine: High-Performance Inference for
Resource-Constrained MCUs — From TFLM Runtime to System–Algorithm
Co-Design." Subtitle line: presenter, date, "for engineers who already
ship with TFLM". Speaker notes: one sentence on what the audience will
walk out with.

### B. Embedded-AI constraint reality

**Slide 2 — The MCU envelope.** Must cover: SRAM 128 KB–1 MB, Flash
512 KB–4 MB, Cortex-M4/M7 at 100–600 MHz, battery budgets in the
single-digit-mW range. Note that SRAM is the binding constraint, not
flash, on most vision-class workloads.

**Slide 3 — Why activations break the budget.** Must cover: weights live
in flash (cheap), activations live in SRAM (scarce). A 160×160×16 int8
feature map is already 410 KB — over budget on a 256 KB-SRAM part before
you allocate anything else. The model fits; the workspace doesn't.

### C. TFLM refresher — overhead only

**Slide 4 — TFLM at a glance.** Must cover: FlatBuffer model →
`MicroInterpreter` → operator dispatch → CMSIS-NN kernels → tensor arena.
[Render the pipeline as a vertical ASCII flow.] Strengths: portable,
generic, easy deployment, single binary covers many models.

**Slide 5 — Where TFLM leaves performance on the table.** Must cover:
(1) per-invocation operator dispatch is a runtime decision, (2) the
greedy arena allocator can't see future tensor lifetimes, (3) generic
kernels can't specialize on shape/stride/channel count at compile time,
(4) interpreter + resolver tables cost flash and cycles. None of these
are TFLM bugs — they are the cost of being a general runtime.

### D. What is TinyEngine + MCUNet co-design

**Slide 6 — MCUNet in one picture.** Must cover: TinyNAS searches *what
network*, TinyEngine answers *how to execute it*. The two are co-designed
so the search space only contains networks the runtime can execute
efficiently. [ASCII: TinyNAS ⇄ TinyEngine box, output → MCU.]

**Slide 7 — Code-generation, not interpretation.** Must cover: TinyEngine
ingests a quantized TFLite file and emits straight-line C — one function
call per layer, no interpreter, no resolver, no FlatBuffer parsing at
runtime. The "engine" is the *generator*; what runs on the MCU is just C.

### E. TinyEngine architecture (codegen pipeline)

**Slide 8 — Codegen pipeline.** [ASCII vertical:
TFLite model → graph analyzer → memory scheduler → operator generator →
emitted C + weights + headers.] One sentence per stage.

**Slide 9 — What the graph analyzer sees.** Must cover: tensor shapes,
tensor lifetimes (birth/death by op index), inter-op dependencies. This
is the input to the scheduler. Contrast: TFLM only knows lifetimes
greedily, op-by-op, at runtime.

**Slide 10 — What the operator generator emits.** Must cover: kernel
specializations are picked per layer (e.g. `depthwise_kernel3x3_stride1
_inplace_CHW_fpreq.c` is a different file than `..._stride2_...`).
Compile-time specialization replaces runtime branching on stride/kernel
size/channel count.

### F. TFLM vs. TinyEngine execution model

**Slide 11 — Side-by-side execution.** [Two ASCII columns: left labelled
TFLM (Model → Interpreter → Op Resolver → Kernel), right labelled
TinyEngine (Model → Generated invoke() → Kernel direct).] Speaker notes:
the right column is what actually links into the firmware image; the
left column does too, plus all the dispatch machinery.

**Slide 12 — What this buys and costs.** Must cover: buys — fewer
indirections, smaller flash for runtime, kernels can inline constants,
compiler can optimize across the call. Costs — switching models means
re-running codegen and re-flashing; you can no longer drop a new
`.tflite` into a filesystem at runtime. This is a deliberate trade.

### G. Optimization deep-dives (one slide each)

**Slide 13 — Global memory scheduling.** Must cover: with full-graph
lifetime knowledge, TinyEngine packs activation buffers like a register
allocator packs values into registers. Two tensors whose lifetimes don't
overlap share storage. Quoted result from MCUNet paper: 50–70% peak-SRAM
reduction vs. greedy allocation, on the same network. [Tiny ASCII
lifetime chart with 3 tensors and reuse arrows.]

**Slide 14 — In-place depthwise convolution.** Must cover: a depthwise
3×3 with stride 1 reads a small neighborhood and writes one output per
channel — the output position is locally bounded by the input position,
so you can write back into the input buffer in place. Saves one full
activation tensor per DW layer. Crucial for MobileNet-class networks
where DW layers dominate. Show the file-name pattern
`depthwise_kernelKxK_strideS_inplace_CHW_fpreq*.c` to make it concrete.

**Slide 15 — Patch-based inference.** Must cover: instead of materializing
the full early feature map, run a spatial patch through several layers
top-to-bottom, then move to the next patch. Lets you accept larger inputs
(e.g. 320×320) without paying for a 320×320×C activation. Tradeoff:
overlapping receptive fields mean some recomputation; net SRAM win still
large.

**Slide 16 — Layer fusion.** Must cover: Conv + BN + ReLU + (optional
add) collapse into one kernel pass over the data. Wins on memory traffic
and on i-cache footprint. Most TFLM ops fuse activation but not BN —
TinyEngine fuses across the pre-quantization sequence the compiler
already constant-folded.

**Slide 17 — Static scheduling and specialized kernels.** Must cover:
because the schedule and the kernel selection are both fixed at codegen
time, the runtime has no decisions left. No virtual dispatch, no shape
checks, no fallback paths. The compiler sees concrete loop bounds and
unrolls/vectorizes accordingly.

**Slide 18 — Quantization plumbing.** Must cover: int8 weights with
per-channel scales, int8 activations with per-tensor scales (the
`_fpreq` suffix in kernel filenames = "fixed-point requantization").
Same numerics as TFLM-int8, but the requantization constants are
inlined per-layer at codegen time — no runtime table lookup.

### H. Memory-planner worked example

**Slide 19 — A 4-layer toy graph.** [ASCII: Conv1 → DWConv2 → PWConv3 →
Conv4 with tensor names T1..T5 between layers.] Tensor lifetime table:

| Tensor | Born at | Dies at | Size  |
|--------|---------|---------|-------|
| T1     | L1      | L2      | 32 KB |
| T2     | L2      | L3      | 32 KB |
| T3     | L3      | L4      | 16 KB |
| T4     | L4      | end     | 8 KB  |

**Slide 20 — Greedy vs. global allocation.** Must cover: greedy picks
peak = sum of currently-live = up to 64 KB at one point. Global
allocation reuses T1's storage for T3 since they don't overlap; peak
drops to 40 KB. Same network, same numerics, less SRAM. Speaker notes:
this is the single biggest source of TinyEngine's SRAM advantage.

### I. Cortex-M7 generated-code walkthrough

**Slide 21 — What the generator drops on disk.**
```
genModel/
  Include/
    genModel.h         # PEAK_MEM, MODEL_SIZE, weight extern decls
    genNN.h
  Source/
    genModel.c         # invoke() — straight-line layer calls
    weights.c          # const int8_t arrays in flash
    depthwise_kernel*_inplace_CHW_fpreq*.c   # specialized DW kernels
    convolve_*.c       # specialized standard / pointwise convs
    pooling_*.c, fc_*.c
```

**Slide 22 — invoke() in practice.**
```c
void invoke(void) {
    layer1_conv2d_int8();
    layer2_depthwise_kernel3x3_stride1_inplace();
    layer3_conv1x1_int8();
    /* ... one call per layer, fully unrolled ... */
}
```
No loops, no dispatch table, no string lookups. Speaker notes: this is
why static-analysis / coverage / WCET tooling all behave better on
TinyEngine output than on TFLM.

**Slide 23 — Mapping layers to memory.** Must cover: weights → flash
(linker `.rodata` / external QSPI if needed), activations → SRAM/DTCM,
input/output buffers → SRAM, scratch → reused activation arena. On M7,
DTCM placement of the activation arena gives the largest single latency
win after the algorithmic optimizations.

### J. Deployment flow + ARM MCU project integration

**Slide 24 — End-to-end pipeline.** [ASCII: PyTorch/TF train →
post-training quant → .tflite → TinyEngine codegen → C sources →
embedded toolchain (MPLAB X / STM32CubeIDE / Keil / IAR) → flash.]
Speaker notes: every step before codegen is identical to a TFLM flow;
the divergence is at the runtime boundary.

**Slide 25 — What you actually link in.** Must cover: generated
`genModel.c` + `weights.c`, the kernel `.c` set selected for your
network, CMSIS-Core, optionally CMSIS-DSP. No TFLM library. No
FlatBuffers library. Total runtime footprint is the kernels you
actually call, nothing more.

**Slide 26 — Application integration.**
```c
int main(void) {
    system_init();
    while (1) {
        acquire_frame(getInput());
        invoke();
        consume_logits(getOutput());
    }
}
```
Compatible with bare-metal, FreeRTOS, Zephyr — `invoke()` is just a
function call, so integration is whatever your task model already is.

### K. Performance comparison table

**Slide 27 — TFLM vs. TinyEngine, head-to-head.**

| Aspect              | TFLM            | TinyEngine          |
|---------------------|-----------------|---------------------|
| Runtime model       | Interpreter     | Generated C         |
| Operator dispatch   | Resolver table  | Direct call         |
| Memory planning     | Greedy, op-local| Global, full-graph  |
| In-place DWConv     | No              | Yes                 |
| Patch inference     | No              | Yes                 |
| Layer fusion        | Activation only | Conv+BN+act+add     |
| Model swap at runtime | Yes (FlatBuffer) | No (re-codegen)  |
| Peak SRAM           | High            | Low                 |
| Latency             | Baseline        | Lower               |
| Operator coverage   | Broad           | Narrower (MCUNet-shaped nets) |

### L. Case study — VWW0 on PIC32CZ-CA

**Slide 28 — Setup.** Must cover, with exactly these numbers:
- Hardware: Microchip PIC32CZ-CA (Cortex-M7) + OV7670 RGB565 camera.
- Model: MCUNet-VWW0 (`mcunet-10fps_vww`), 2-class person / no-person.
- Input: **64×64×3 int8**, scale ≈ 0.00784, zero-point = -1.
- Output: 1×1×2 int8 logits, scale ≈ 0.0982.
- Layers: **51**. Generated kernel directory contains **18 in-place
  depthwise kernel variants** (3×3, 5×5, 7×7 × stride-1/2 ×
  {plain, mask, bitmask}) — concrete evidence of the in-place
  optimization in action.

**Slide 29 — Results, vs. the previous TinyEngine YOLO build on the same MCU.**

| Metric                  | Old (YOLO person-det) | New (VWW0 + TinyEngine) |
|-------------------------|-----------------------|-------------------------|
| Peak SRAM activations   | 257,448 B             | **60,800 B (−76%)**     |
| Flash weights           | 190,864 B             | 399,656 B (+109%)       |
| Inference latency       | ~330 ms               | **<70 ms**              |
| Input resolution        | 160×128×3             | 64×64×3                 |
| Layer count             | 70                    | 51                      |

Speaker notes: VWW0's NAS spent capacity on channel width (up to 480ch,
7×7 depthwise kernels) instead of spatial size — classic classifier
shape, the opposite of a detector. SRAM is the scarce resource on
PIC32CZ-CA, so the trade — more flash for less SRAM and 4–5× faster
inference — is exactly the right one. The 76% SRAM cut comes from the
combination of (a) smaller input, (b) global memory scheduling, and
(c) in-place DWConv across the bottleneck blocks.

### M. Limitations + when not to pick TinyEngine

**Slide 30 — Honest tradeoffs.** Must cover: smaller op coverage than
TFLM (custom ops, non-MCUNet topologies may not generate cleanly),
model swap requires re-codegen + re-flash (no field-side model
updates), tooling/community is smaller than TFLM, debugging across
generated kernel variants is more work. Pick TFLM when SRAM isn't the
bottleneck, when you need runtime model swap, or when your network is
exotic. Pick TinyEngine when you're SRAM-bound on a known model.

### N. Future directions

**Slide 31 — Where this is going.** Must cover: MCUNetV2 (improved
patch-based inference, redistributing receptive field), MCUNetV3 /
on-device tiny training (sparse update, quantized backprop on MCU),
and the broader trend toward generated runtimes for edge AI as model
shapes stabilize.

### O. Key takeaways

**Slide 32 — Takeaways.** Must cover, as 5 short bullets:
- TinyEngine is a *code generator*, not a runtime — it removes the
  interpreter, not the math.
- Global memory scheduling is the single largest source of its SRAM win.
- In-place DWConv + layer fusion + static scheduling compound the win
  on MobileNet-class networks.
- Patch-based inference is what unlocks larger inputs on small SRAM.
- For TFLM-native engineers, integration is "drop generated C into your
  project and call `invoke()`" — the conceptual jump is small, the
  performance jump is not.

### P. Q&A

**Slide 33 — Q&A.** Title only. Speaker notes: anticipate questions on
op coverage, model-swap workflow, debugging generated code, comparison
to ONNX Runtime / Glow / executorch.

---

## Final reminders before you generate

- **33 slides exactly.** Match the section budget.
- **Outline format only** (`# Slide N — Title`, bullets, `Speaker notes:`).
- **No emojis.** ASCII diagrams welcome. Code blocks fenced as `c`, ≤12 lines.
- **Output the deck only** — start at Slide 1, end after Slide 33's notes.
  No introduction, no closing remark, no markdown headers above slide 1.

=== END PROMPT ===

---

## How to use

1. Open Gemini (or your LLM of choice).
2. Copy everything between the two `===` markers above and paste as a
   single message.
3. Save Gemini's response as `tinyengine_deck_outline.md`.
4. In Google Slides: **File → New → From outline** (or **Tools → Outline
   view** in the docs editor and paste). Each `# Slide N — Title`
   becomes one slide; bullets and speaker notes flow into the right
   places.
5. If you want a `.pptx` directly instead, ask Gemini in a follow-up:
   "Now produce a `python-pptx` script that builds this deck." The
   outline above is structured enough that the conversion is mechanical.

## Where the case-study numbers come from

Every figure in slides 28–29 traces back to
[CONTEXT.md](CONTEXT.md#resource-numbers) §Resource numbers in this
repository. The 18-kernel claim is verifiable with:

```bash
ls My_MCC_Config/src/TinyEngine/codegen/Source/ \
  | grep -E "depthwise.*inplace.*\.c$" | wc -l
# → 18
```
