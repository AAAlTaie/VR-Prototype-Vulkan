# Phase 4 — Depth Sort and HDR Composite

Sort projected splats back to front on the GPU, composite them into a floating-point target, and tonemap to the swapchain. This is what turns correct-looking geometry into correct-looking imagery.

## Deliverables

| Component | Files | Responsibility |
|---|---|---|
| Sort shaders | `render/shaders/sort_{clear,histogram,scan,scatter}.comp` | Counting sort on quantised depth |
| GpuImage | `render/GpuImage.{h,cpp}` | VMA-backed image with view |
| TonemapPass | `render/TonemapPass.{h,cpp}` | Fullscreen composite with push descriptors |
| Tonemap shaders | `render/shaders/tonemap.{vert,frag}` | Fullscreen triangle, exposure, operators |
| Layers test scene | `tools/make_test_scene.py --shape layers` | Two coloured sheets for verifying sort direction |

## Why a counting sort rather than a full radix sort

The reference implementation uses a 32-bit key radix sort. Four 8-bit passes, each needing per-tile histograms, a multi-level scan, and stable scatter with intra-tile ranking. Stability is mandatory: an unstable scatter corrupts every subsequent pass.

Depth ordering does not need 32 bits. Quantising depth to 16 bits gives 65,536 buckets across the visible depth range, which for a scene a few units deep resolves to well under a millimetre. That makes the sort a **single-pass counting sort**, and single-pass means stability is irrelevant — ties are splats at the same depth, where any order is equally correct.

The result is four small dispatches instead of a multi-pass pipeline with intra-tile ranking:

1. **Clear** — zero the 65,536-entry histogram, and write indirect dispatch arguments from the visible count.
2. **Histogram** — one `atomicAdd` per visible splat into its depth bucket.
3. **Scan** — exclusive prefix sum over the histogram, converting counts to starting offsets.
4. **Scatter** — `atomicAdd` on the bucket offset yields each splat's destination slot.

The trade-off is a fixed 256 KB histogram and precision capped at 16 bits. Both are cheap next to the code the full radix sort would require. If a scene ever needs finer ordering, the key width is the only thing that changes.

### The scan

65,536 entries scanned by a single workgroup of 256 threads. Each thread serially sums its own 256-entry chunk, chunk totals are exclusively scanned in shared memory, then each thread writes its chunk's running offsets. Two barriers, no multi-level dispatch.

This is not the fastest possible scan. It is a single dispatch over a fixed 64K array whose cost does not scale with splat count, and its simplicity is worth more than the microseconds a Blelloch scan would save.

### Depth quantisation and range

```glsl
normalised = clamp((depth - depthMinimum) / (depthMaximum - depthMinimum), 0, 1)
key = 65535 - uint(normalised * 65535)
```

Subtracting from 65,535 inverts the order so an ascending sort yields back-to-front — farthest splats first, which is the order alpha compositing requires.

The range comes from the CPU: `[camera_distance - scene_radius, camera_distance + scene_radius]`, clamped to the near plane. Tight, exact for an orbit camera, and free. A free-flying camera would need a per-frame min/max reduction instead; noted for Phase 5.

### Indirect dispatch

Histogram and scatter dispatch sizes depend on the visible count, which lives on the GPU. Rather than reading it back or dispatching over the worst case, the clear shader's first invocation writes `VkDispatchIndirectCommand`, and both stages use `vkCmdDispatchIndirect`. No CPU round trip, no wasted invocations.

## HDR pipeline

The scene now renders into an `R16G16B16A16_SFLOAT` target rather than directly to the swapchain, then a fullscreen pass tonemaps into it.

Frame structure:

```
compute: project → sort
barrier: undefined → colour attachment (HDR)
render:  splats into HDR target
barrier: colour attachment → shader read (HDR)
barrier: undefined → colour attachment (swapchain)
render:  fullscreen tonemap
barrier: colour attachment → present
```

`Renderer` owns the HDR target and recreates it in `bindSwapchain`, so resize handling comes along for free. `drawFrame` now takes three callbacks — compute, scene, composite — and still knows nothing about splats.

### Why HDR matters here specifically

Hundreds of overlapping gaussians accumulate radiance that routinely exceeds 1.0 in dense regions. An 8-bit target clamps at every blend step, so bright areas flatten into featureless white and the clipping is baked in before any tonemapping can respond to it. A float target keeps the values, and the tonemap operator decides how to bring them into display range.

### Push descriptors

The tonemap pass needs to sample the HDR target, which requires a descriptor — the one thing buffer device address cannot replace. `VK_KHR_push_descriptor` supplies it without a descriptor pool or allocated sets: the image is written directly into the command buffer with `vkCmdPushDescriptorSetKHR`.

The project still contains no `VkDescriptorPool` and no `VkDescriptorSet`.

### Fullscreen triangle

The composite draws three vertices with no vertex buffer, deriving positions from `gl_VertexIndex`. A single oversized triangle rather than two triangles avoids the diagonal seam where quad halves meet, which costs nothing and removes a class of edge artefact.

## Config schema changes

New `[tonemap]` section:

```toml
[tonemap]
exposure = 1.0
operator = "aces"
```

`operator` accepts `none`, `reinhard`, or `aces`, validated by name at load with the valid set listed in the error. Exposure multiplies before the operator.

## A bug worth recording

Adding four sort pipelines to `SplatPass` meant adding eight members to the header. The move constructor and move assignment were not updated, so after `SplatPass` was moved into `Application` the sort pipelines were `VK_NULL_HANDLE`.

Validation caught it immediately with `vkCmdBindPipeline(): pipeline is VK_NULL_HANDLE`. Without validation it would have been a blank window with no diagnostic.

This is the recurring cost of hand-written move constructors: every new member is a chance to forget one, and the compiler will not warn. The alternative — wrapping each handle in a small RAII type so the compiler generates the moves — is worth considering if the member count grows further.

## Validation gates

Linux/GCC 13 against lavapipe under Xvfb.

1. **Build** — clean, `-Werror`, zero warnings.
2. **Sort correctness, positive** — the `layers` scene places a red sheet nearer the camera than a blue one. Centre pixel reads `srgb(243, 89, 89)`. The near sheet wins, which is what correct back-to-front compositing produces.
3. **Sort correctness, negative** — reversing the key to `uint(normalised * 65535)` inverts the order. Centre pixel reads `srgb(89, 89, 243)`. Numerically mirrored, confirming the sort direction is meaningful and the test discriminates rather than merely passing.
4. **Tonemap operators** — same pixel under identical HDR input:

   | Operator | Centre pixel |
   |---|---|
   | `none` | `srgb(249, 221, 206)` |
   | `aces` | `srgb(230, 221, 215)` |

   ACES desaturates and rolls off the highlight, exactly its intended behaviour. Confirms the operator selector reaches the shader and the HDR target carries values worth tonemapping.

5. **Validation clean** — zero messages across startup, steady state, resize, and shutdown, after fixing the move constructor bug.
6. **Resize under load** — 1600x900 to 900x640 to 1280x720 with sort and HDR active. The HDR target is recreated with the swapchain; no errors.

Frame rate under lavapipe remains meaningless. The Phase 3 hardware baseline was 2.1 ms on a Quadro RTX 5000; the sort should add a few tenths of a millisecond and the tonemap pass roughly one fullscreen write.

## Known gaps

- Windows/MSVC unverified for this phase, including the hardware cost of the sort.
- Depth quantisation is capped at 16 bits and the range comes from the orbit camera's known geometry. A free camera needs a per-frame depth reduction.
- The scan is a single workgroup over a fixed 64K array. Fine at this scale, not optimal.
- The histogram is 256 KB regardless of splat count.
- Tonemap operators are hardcoded in the shader and selected by index. Adding one means editing the shader and the config parser.
- Still no view-dependent colour; `f_rest_*` remains unloaded.

## Out of scope

Single-pass stereo. Phase 5 adds `VK_KHR_multiview` so both eyes render in one pass with one command buffer, which is the piece that makes this read as XR work rather than desktop graphics.
