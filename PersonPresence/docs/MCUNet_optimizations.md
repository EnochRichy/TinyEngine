# MCUNet Optimizations — a two-axis breakdown

## Why this framing

Every optimization the MCUNet project (TinyNAS + TinyEngine) brings to a
Cortex-M MCU targets one of two scarcity axes:

1. **SRAM** — the activation workspace. Hard ceiling, set by silicon.
2. **Cycles** — inference latency. Soft ceiling, set by the clock and the
   instruction stream.

Some techniques target one axis. Some hit both. The original "list of seven
optimizations" presentation order obscures *why* each exists. Splitting them
along the scarcity axis they attack makes that immediate.

- **Part A** — techniques that reduce SRAM.
- **Part B** — techniques that reduce latency.
- **Part C** — cross-cutting techniques that reduce both.
- **Part D** — the VWW0 / PIC32CZ-CA case study, scored on both axes.

A short note on terminology before we start. "MCUNet" is the umbrella
research project from MIT HAN Lab. Inside it sit two co-designed pieces:
**TinyNAS** (which picks the network shape) and **TinyEngine** (which
generates the C code that runs that network on the MCU). When this document
says "MCUNet does X" it means "either TinyNAS or TinyEngine does X." Where
the distinction matters, the section names the responsible component.

---

# Part A — SRAM reduction

The MCU has, say, 256 KB or 512 KB of on-chip SRAM. A naive deployment of a
modern CNN doesn't fit. The activations break the budget long before the
weights do — weights live in flash, which is plentiful; activations live in
SRAM, which is not. Every technique in this section reduces the *peak*
activation footprint, which is the only number that matters (you can't trade
average for peak when the peak is what fails to allocate).

## A1. Global memory scheduling (TinyEngine)

**Mechanism.** Before emitting any C, TinyEngine walks the full network
graph, computes the birth and death op-index of every tensor, and solves a
2D bin-packing problem: lay out all activation buffers in a single arena
such that two tensors share physical bytes whenever their lifetimes do not
overlap. The placement is fixed at codegen time.

**Why it works.** This is the same idea as register allocation in a
compiler. A tensor that dies at op 5 frees its bytes for a tensor born at
op 6. A naive allocator that decides one tensor at a time, in order, can't
look ahead and ends up holding storage longer than necessary. A scheduler
that sees the whole graph at once can overlap aggressively.

**Cost.** Codegen-time computation only. Zero runtime cost, zero numerical
change.

**Magnitude.** MCUNet papers quote 50–70% peak-SRAM reduction vs. a greedy
allocator on the same network. This is the single largest source of
TinyEngine's SRAM advantage over TFLM.

**Why TFLM can't do this.** TFLM's tensor arena is a runtime structure
allocated from a fixed-size buffer. The allocator works greedily because it
must — it's filling the arena one op invocation at a time, with no compile-
time knowledge of the full op sequence available to it.

## A2. In-place depthwise convolution (TinyEngine)

**Mechanism.** A standard convolution writes outputs to a separate buffer
because each output depends on inputs from many channels and a wide spatial
neighborhood — overwriting the input would corrupt later reads. A
**depthwise** convolution (one filter per input channel, no cross-channel
mixing) doesn't have that constraint per channel, and the spatial
neighborhood is small (3×3, 5×5, 7×7). With careful iteration order, the
output of position (i, j) can be written back into the input buffer because
no future read of the input buffer needs that exact byte anymore.

TinyEngine emits a specialized in-place kernel per (kernel size, stride)
combination — you can see them on disk:
`depthwise_kernel3x3_stride1_inplace_CHW_fpreq.c`,
`depthwise_kernel5x5_stride2_inplace_CHW_fpreq_mask.c`, etc.

**Why it works.** MobileNet-class networks (and MCUNet networks, which are
NAS-found in the same family) are dominated by depthwise-then-pointwise
blocks. Half the activation tensors in such a network sit immediately after
a DW layer. Eliminating the second buffer for the DW step cuts the working
set in half across those layers.

**Cost.** None at runtime. The kernels are slightly more complex to write
and verify (you must prove the read-write hazard is absent for each
(K, stride) pair), but that is amortized into TinyEngine's kernel library.

**Magnitude.** Removes one full activation tensor per DW layer. On a
MobileNet-shaped network the activation working set drops by roughly a
factor of two for the bottleneck blocks. Compounds with global memory
scheduling (A1).

## A3. Patch-based inference (TinyEngine, MCUNetV2 contribution)

**Mechanism.** Standard inference is breadth-first across the graph: finish
all of layer 1 before starting layer 2. That requires materializing the
*entire* layer-1 output in SRAM. Patch-based inference is depth-first
*spatially*: take a small patch of the input image, run it through several
early layers, get a corresponding patch of output, throw away the
intermediate patches, then move to the next input patch.

For a 320×320 input you might run 8×8 patches through the first 6 layers,
producing 8×8 patches of the layer-6 feature map, before any layer-7
computation begins on the assembled feature map.

**Why it works.** Early layers in CNNs are spatially local — each output
depends on a bounded receptive-field neighborhood of the input. As long as
your patch is bigger than the cumulative receptive field, you can compute
that patch's output independently. The peak SRAM is now proportional to the
patch's working set, not the full feature map's.

**Cost.** Some recomputation at patch boundaries (overlap regions are
computed by adjacent patches). MCUNetV2 introduces a *receptive-field
redistribution* trick — re-architecting the early layers so receptive
fields are smaller in the early stages, reducing the overlap penalty. The
NAS step (TinyNAS) is aware of this and biases the search.

**Magnitude.** MCUNetV2's headline result: enabling 320×320 inputs on 256 KB
parts that previously couldn't accept anything beyond ~144×144. Without
patch inference, ImageNet-scale resolutions are not deployable on MCUs at
all; with it, they are.

## A4. Operator reordering for shorter lifetimes (TinyEngine)

**Mechanism.** When the graph has parallel branches (say a residual
connection where a tensor must live across a Conv-BN-ReLU sub-branch),
TinyEngine can choose which branch to schedule first. Different schedules
produce different peak SRAM. The scheduler picks the one with the lowest
peak.

**Why it works.** Two branches that are "live at the same time"
mathematically can be made non-overlapping in time by completing one fully
before starting the other, as long as no data dependency forces interleaving.

**Cost.** Codegen-time only. No runtime cost.

**Magnitude.** Smaller than A1–A3 individually but compounds with them.
Particularly relevant in residual-block-heavy networks.

## A5. Static buffer placement (TinyEngine)

**Mechanism.** All buffers have fixed addresses chosen at codegen time and
emitted as constants in `genModel.c`. There is no runtime allocator, no
free list, no fragmentation.

**Why it works.** Runtime allocators must pad for alignment and reserve
metadata; they fragment over time; they conservatively avoid reusing
just-freed regions. Static placement does none of that — every byte is
accounted for.

**Cost.** Zero runtime cost. The downside is rigidity: changing the model
means re-running codegen and re-flashing.

**Magnitude.** Modest as a standalone effect (low single-digit percent),
but it's a hard prerequisite for A1 and A4 to function correctly.

## A6. No interpreter / runtime data structures (TinyEngine)

**Mechanism.** TinyEngine emits a single `invoke()` function that calls
each layer's kernel directly. There is no FlatBuffer model in SRAM, no
operator resolver table, no interpreter state, no per-tensor metadata
struct.

**Why it works (for SRAM).** Those interpreter structures live in SRAM in
TFLM. They aren't huge — kilobytes, not tens of kilobytes — but on a 256 KB
part, kilobytes matter. Removing them gives that SRAM back to the
activation arena.

**Cost.** No runtime model swap (the model is now linked code, not data).

**Magnitude.** Single-digit KB on small models, low tens of KB on larger
ones. This technique is more of a latency win than an SRAM win — see B1.

## A7. TinyNAS picks shapes that fit (TinyNAS)

**Mechanism.** TinyNAS is a constrained neural architecture search. The
search objective is accuracy *subject to* a hard SRAM constraint computed by
querying TinyEngine: "what is the peak SRAM if this candidate network is
generated and scheduled?" Networks that don't fit are immediately pruned.

**Why it works.** Standard NAS pipelines optimize FLOPs or parameter count
and then hope the result fits. Memory peak is not a smooth function of
those metrics — a network with the same FLOPs as another can use 3× the SRAM
because of where its widest feature map lives. The only way to optimize for
peak SRAM is to ask the engine directly, for every candidate.

**Cost.** Search-time cost only — handled offline, before any deployment.

**Magnitude.** This is what makes the rest tractable. Without TinyNAS, you'd
need to manually shape your network to fit; with it, the network shape is
already memory-aware before TinyEngine sees it.

---

# Part B — Latency reduction

The MCU runs at, say, 300 MHz. An interpreted runtime spends most of its
cycles on dispatch, not math. Every technique in this section converts a
runtime decision into a compile-time decision, or makes the math itself
denser.

## B1. No interpreter — direct function calls (TinyEngine)

**Mechanism.** `invoke()` is a sequence of plain C function calls, one per
layer, fully unrolled at codegen time. No model parsing, no opcode reading,
no virtual dispatch, no string lookups.

**Why it works.** TFLM walks a FlatBuffer per inference and calls the
resolver for each op to get a function pointer. That walk happens every
single frame. TinyEngine pays this cost zero times because the walk happened
once, on the laptop, at codegen time.

**Cost.** Loss of dynamic model loading. The model is now firmware.

**Magnitude.** Per-op overhead in TFLM is on the order of microseconds.
Across 50–100 layers per frame at 10–30 fps, this adds up — typically
single-digit-millisecond savings per frame. Not the biggest single lever but
it's free.

## B2. No operator dispatch / resolver lookup (TinyEngine)

**Mechanism.** TFLM has an `OpResolver` that maps operator codes to kernel
implementations. Even with the optimized `MicroMutableOpResolver`, it's a
table lookup per op invocation. TinyEngine resolves all of this at codegen
time — the kernel call site already names the specific kernel.

**Why it works.** Eliminating the indirection lets the compiler inline the
call (when small), keep arguments in registers, and avoid an i-cache miss
on a function-pointer call into a kernel that wasn't recently used.

**Cost.** None.

**Magnitude.** Tens of nanoseconds per op. Multiply by ops per frame. This
is part of the same "no-interpreter" family as B1.

## B3. Compile-time loop bounds enable unrolling and vectorization (TinyEngine)

**Mechanism.** A generic kernel writes `for (i = 0; i < out_h; i++)` where
`out_h` is a parameter. A specialized kernel writes
`for (i = 0; i < 32; i++)`, with the literal `32` baked in at codegen time.

**Why it works.** The C compiler treats these completely differently. With
unknown bounds, it emits a generic loop with branch overhead and no
unrolling. With known bounds, it can fully unroll, schedule instructions
across iterations, eliminate induction variables, and emit SIMD MACs from
the available DSP instructions on Cortex-M4F+ (SMLAD, SMLALD) or Helium on
Cortex-M55.

**Cost.** Code size grows — every distinct (shape, stride) combination in
the network gets its own kernel emitted. This is paid in flash, which is
abundant. (See the 18 in-place DW kernels in this project for an example.)

**Magnitude.** This is one of the biggest single levers for inner-loop
performance. Compiler-driven unrolling and SIMD on a fully-bound loop is
2–4× faster than the same loop with runtime bounds, on Cortex-M4F and M7.

## B4. Specialized per-shape kernels (TinyEngine)

**Mechanism.** Sister to B3. TinyEngine emits a separate kernel `.c` file
for each (kernel size, stride, channel-count regime) combination present in
the network. A 3×3 stride-1 DW with 16 channels gets a different kernel
than a 5×5 stride-2 DW with 96 channels. Padding, dilation, requantization
constants — all baked.

**Why it works.** Generic kernels in CMSIS-NN handle every shape, which
means inner loops contain shape-dependent branches. Specialized kernels
have straight-line inner loops.

**Cost.** Same as B3 — flash cost.

**Magnitude.** Compounds with B3. Together they account for the bulk of
TinyEngine's per-layer kernel speedup over CMSIS-NN-via-TFLM.

## B5. ARM DSP / SIMD intrinsics (TinyEngine + CMSIS-DSP family)

**Mechanism.** Cortex-M4F and up have packed SIMD instructions:
SMLAD (signed multiply-accumulate, dual 16-bit), SMLALD (long
accumulator), and on Helium-equipped parts (M55, M85), full vector
instructions. TinyEngine's specialized kernels are written to use these
intrinsics on int8 packed data.

**Why it works.** A scalar int8 MAC is one instruction. SMLAD can do two
16-bit MACs per cycle. With careful packing, you get 4 int8 MACs per cycle
on M4F and ~8 on Helium.

**Cost.** Kernel author complexity — but this is amortized in the kernel
library.

**Magnitude.** 2–4× on inner loops vs. scalar code on the same MCU.

## B6. Layer fusion (TinyEngine)

**Mechanism.** Conv + BatchNorm + activation (ReLU/ReLU6) + optional
residual add are emitted as a single kernel that reads each input element
once, computes the entire fused result, writes the output once. The BN
parameters are folded into the conv weights at codegen time.

**Why it works on latency.** SRAM access is the dominant cost for many
M7 workloads — the AXIM bus to flash and the DTCM port both have finite
bandwidth. Fusing reduces the number of read/write round-trips per output
element from four to one. It also keeps intermediate values in registers
instead of spilling to SRAM.

**Cost.** Loss of granularity for debugging — you can't put a breakpoint
"after BN, before ReLU" because they are no longer separate kernels.

**Magnitude.** 10–30% latency improvement on bandwidth-bound layers, more
on residual blocks where the add is also fused.

## B7. Inlined requantization constants (TinyEngine)

**Mechanism.** Every int8 layer needs to scale its int32 accumulator back
to int8 — multiply by a scale, add zero-point, saturate. The scale is a
fixed-point multiplier-and-shift pair derived from the quantization
parameters. TFLM looks these up per layer, per call. TinyEngine bakes them
as `#define` constants in the kernel source.

**Why it works.** Inlined constants let the compiler use immediate-mode
instructions and avoid per-call setup math.

**Cost.** None. Constants are tiny.

**Magnitude.** Small per layer, but every layer pays this — accumulates to
a couple of percent end-to-end.

## B8. Cache-friendly memory layout (TinyEngine)

**Mechanism.** TinyEngine chooses tensor layout per kernel. Depthwise
convolutions are emitted as CHW (channel-first) so that all H×W of one
channel is contiguous and the inner loop streams cleanly. Pointwise (1×1)
convolutions are emitted as HWC so that the channel axis (which is the
contracted axis) is contiguous. The conversion between layouts is fused
into the previous kernel where possible.

**Why it works.** The Cortex-M7 has 16 KB I-cache and 16 KB D-cache. A
non-contiguous inner-loop access pattern thrashes D-cache. Choosing layout
per kernel keeps the working set hot.

**Cost.** Sometimes a transpose is needed between layers. TinyEngine
schedules these to coincide with other passes when possible.

**Magnitude.** Highly workload-dependent — single-digit to ~20% latency
delta, more on parts with smaller D-caches.

## B9. DTCM placement of the activation arena (deployment-time)

**Mechanism.** Cortex-M7 has Tightly-Coupled Memory (DTCM, ITCM) that
sits on a dedicated bus to the core, no cache, no AXIM contention.
TinyEngine reports the exact `PEAK_MEM` value for its activation arena;
the user places that buffer in DTCM via a linker section attribute.

**Why it works.** DTCM is single-cycle access. Putting the activation
arena there means every kernel's read/write path bypasses the AXIM bus
and the L1 D-cache.

**Cost.** Not all chips have DTCM. M0/M3/M4 don't. M7/M55/M85 do.

**Magnitude.** On M7 with the activation arena moved from external SRAM
to DTCM, 1.5–3× latency improvement on memory-bound kernels — often the
single biggest deployment-time lever after the algorithmic optimizations.

## B10. TinyNAS picks compute-efficient blocks (TinyNAS)

**Mechanism.** TinyNAS searches over MBConv (mobile inverted bottleneck)
blocks, varying expansion ratio, kernel size, depth, and width. The cost
function is accuracy at fixed FLOPs and fixed peak SRAM. Latency is
approximated from FLOPs and a per-MCU calibration.

**Why it works.** A MobileNet-style depthwise+pointwise block does the
same work as a standard convolution at roughly 1/9 the FLOPs (for 3×3).
TinyNAS finds the right mix of expansion ratios and kernel sizes for
each stage.

**Cost.** Search time, offline.

**Magnitude.** The choice of network family (MBConv vs. plain conv) is a
3–5× FLOPs difference at equivalent accuracy. TinyNAS finding the optimal
configuration within that family adds another 10–30% on top.

---

# Part C — Cross-cutting techniques (both axes)

A few techniques can't be assigned cleanly to one bucket — they are
preconditions or co-design choices that enable the rest.

## C1. Static scheduling

A precondition for the SRAM wins (A1, A4, A5) and the latency wins (B3,
B4). Without compile-time scheduling, none of those optimizations have
the visibility they need.

## C2. Per-channel int8 quantization

int8 weights with per-channel scales, int8 activations with per-tensor
scales. Halves the model footprint vs. int16, halves SRAM working set,
enables packed-SIMD MACs (B5). The same scheme as TFLM-int8 — TinyEngine
doesn't change the math, only when the scale constants are baked.

## C3. Co-design between TinyNAS and TinyEngine

The single biggest meta-technique. TinyNAS doesn't search abstract
architectures; it searches "things TinyEngine can run efficiently."
TinyEngine doesn't optimize abstract networks; it optimizes "things
TinyNAS produces." The search space is restricted to the intersection,
and that's why both axes hit numbers a non-co-designed system can't
match.

---

# Part D — VWW0 / PIC32CZ-CA case study, scored on both axes

Real numbers from this project's [CONTEXT.md](CONTEXT.md), comparing two
TinyEngine builds on the same MCU.

## SRAM axis

| Source of saving                            | Approx. contribution |
|---------------------------------------------|----------------------|
| Smaller input (160×128 → 64×64)             | ~28% of cut          |
| Global memory scheduling (A1)               | ~42% of cut          |
| In-place DWConv (A2) + operator reorder (A4)| ~22% of cut          |
| Layer fusion + other                        | ~8% of cut           |
| **Net effect:** 257,448 B → 60,800 B        | **−76% peak SRAM**   |

The 18 in-place depthwise variants on disk in
[codegen/Source/](My_MCC_Config/src/TinyEngine/codegen/Source/) are
A2 in action — every 3×3, 5×5, 7×7 DW kernel at every stride got its own
in-place specialization.

## Latency axis

| Source of saving                            | Approx. contribution |
|---------------------------------------------|----------------------|
| Smaller input → fewer FLOPs                 | dominant (~3×)       |
| Specialized kernels + unrolling (B3, B4)    | ~1.3× on top         |
| SIMD intrinsics on M7 (B5)                  | already in CMSIS-NN  |
| Layer fusion (B6)                           | ~1.15× on top        |
| **Net effect:** ~330 ms → <70 ms            | **~5× faster**       |

## Tradeoffs paid

| Cost                          | Magnitude                    |
|-------------------------------|------------------------------|
| Flash weights                 | 190,864 B → 399,656 B (+109%)|
| Layer count                   | 70 → 51 (different topology) |
| Op coverage                   | YOLO-style heads removed     |
| Runtime model swap            | not supported (re-flash only)|

The flash growth comes from TinyNAS giving VWW0 wider channels (up to 480ch,
7×7 depthwise kernels) — a classifier shape, where capacity goes into
channels rather than spatial size. On PIC32CZ-CA, flash is abundant and
SRAM is scarce, so this trade is exactly right.

---

# Quick reference — every technique on one card

| #   | Technique                              | Axis  | Component  |
|-----|----------------------------------------|-------|------------|
| A1  | Global memory scheduling               | SRAM  | TinyEngine |
| A2  | In-place depthwise convolution         | SRAM  | TinyEngine |
| A3  | Patch-based inference (V2)             | SRAM  | TinyEngine |
| A4  | Operator reordering                    | SRAM  | TinyEngine |
| A5  | Static buffer placement                | SRAM  | TinyEngine |
| A6  | No interpreter / runtime structs       | SRAM  | TinyEngine |
| A7  | TinyNAS picks fitting shapes           | SRAM  | TinyNAS    |
| B1  | No interpreter — direct calls          | Cycles| TinyEngine |
| B2  | No operator dispatch                   | Cycles| TinyEngine |
| B3  | Compile-time loop bounds               | Cycles| TinyEngine |
| B4  | Specialized per-shape kernels          | Cycles| TinyEngine |
| B5  | ARM SIMD intrinsics                    | Cycles| TinyEngine |
| B6  | Layer fusion                           | Cycles| TinyEngine |
| B7  | Inlined requantization constants       | Cycles| TinyEngine |
| B8  | Cache-friendly layout (CHW/HWC)        | Cycles| TinyEngine |
| B9  | DTCM placement of arena                | Cycles| Deployment |
| B10 | TinyNAS picks efficient blocks         | Cycles| TinyNAS    |
| C1  | Static scheduling                      | Both  | TinyEngine |
| C2  | Per-channel int8 quantization          | Both  | TinyEngine |
| C3  | TinyNAS / TinyEngine co-design         | Both  | MCUNet     |

---

# Suggested presentation re-structure

If you re-shoot the deck along these axes, the natural slide flow is:

1. Title + framing (1 slide)
2. The two scarcity axes — SRAM and cycles (1 slide)
3. Why TFLM hits both ceilings (2 slides)
4. **Part A header** — "How MCUNet cuts SRAM" (1 slide)
5. A1–A7 — one slide each (7 slides)
6. **Part B header** — "How MCUNet cuts cycles" (1 slide)
7. B1–B10 — one slide each, or grouped 2-per-slide (5–10 slides)
8. **Part C** — cross-cutting (1 slide)
9. **Part D** — VWW0 case study scored on both axes (2 slides)
10. When to pick MCUNet, when to stay with TFLM (1 slide)
11. Q&A (1 slide)

Total: ~28–33 slides depending on B-section grouping. Same length as the
current deck, but the audience leaves knowing exactly which axis each
technique attacks and how big each contribution is.
