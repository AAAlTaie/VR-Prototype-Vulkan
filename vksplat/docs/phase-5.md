# Phase 5 — Single-Pass Stereo

Render both eyes in one render pass, one draw call, one command buffer submission, using `VK_KHR_multiview`. Mono remains a config toggle, not a separate code path.

## Deliverables

| Component | Files | Responsibility |
|---|---|---|
| Multiview vertex shader | `render/shaders/splat.vert` | `gl_ViewIndex` selects per-eye buffers |
| Count combine | `render/shaders/sort_combine.comp` | Single instance count covering both eyes |
| Layered HDR target | `render/GpuImage.{h,cpp}` | Array image, 2D_ARRAY view |
| Per-eye resources | `render/SplatPass.{h,cpp}` | Restructured around an eye array |
| RAII handles | `render/UniqueHandle.h` | Compiler-generated moves for Vulkan handles |
| Side-by-side composite | `render/shaders/tonemap.frag` | `sampler2DArray`, layer per screen half |

## What is and is not single-pass

The compute work is **per eye**. Projecting a 3D gaussian to a screen-space conic depends on the view matrix — the covariance transform, the centre, and the depth all change between eyes. There is no shared intermediate to reuse, so projection and sorting run twice.

The **raster pass** is single-pass: one `vkCmdBeginRendering` with `viewMask = 0b11`, one `vkCmdDrawIndirect`, one command buffer, one submission. The driver broadcasts the draw across both layers and the vertex shader reads `gl_ViewIndex` to pick its eye's data.

This is the honest shape of stereo splatting, and it is worth being precise about it. The saving is in draw submission, state binding, and render pass overhead — not in the projection maths, which is irreducibly per-view.

## Solving the mismatched instance count

Multiview issues one draw for all views, so there is one `instanceCount`. But each eye culls independently and produces a different number of visible splats.

The `sort_combine` kernel writes `max(count[0], count[1])` into the shared indirect draw. The vertex shader then reads its own eye's real count and, for instances beyond it, emits a degenerate position outside the clip volume:

```glsl
if (uint(gl_InstanceIndex) >= constants.counts[eye].instanceCount) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    return;
}
```

Wasted vertex invocations equal the difference between the eyes' visible counts, which for a 65 mm eye separation is a fraction of a percent. The alternative — two draws with per-eye counts — would give up single-pass entirely to save almost nothing.

## Per-eye target geometry

The HDR target is `(swapchain_width / viewCount, swapchain_height)` with `viewCount` array layers. Each eye renders at half width and composites into its own half of the window, so the aspect ratio is correct in both modes with no squeeze. This also matches how an OpenXR swapchain is structured — per-eye images rather than one wide image — which makes Phase 6 a smaller step.

Buffer device address made the per-eye plumbing nearly free: the vertex shader takes an array of buffer references in its push constant block and indexes it by `gl_ViewIndex`. With descriptor sets this would have needed either a set per eye or dynamic indexing into an array binding.

## Eye separation

```cpp
eyeView = translate(-offset_x) * view
```

Pre-multiplying the base view by a view-space translation shifts the camera along its own right vector, which is what an eye offset is. Separation is `interpupillary_distance` from config, default 65 mm — the human average.

There is no per-eye asymmetric frustum. Both eyes use the same symmetric projection with a parallel axis. That is correct for a parallel-projection stereo rig and avoids the vergence-accommodation issues of toe-in, but a real headset supplies asymmetric per-eye projection matrices through OpenXR, which Phase 6 will need to honour rather than deriving focal length from a single FOV.

## Refactor: RAII handles

Phase 4's documented bug — sort pipelines forgotten in the hand-written move constructor — was going to recur. Phase 5 adds seven more handles to `SplatPass`.

`UniqueHandle<T, Deleter>` wraps a Vulkan handle with its device and destroys it on scope exit. Each handle type gets a small deleter functor rather than a template non-type parameter, because with volk the destroy functions are function pointers loaded at runtime, not constant expressions.

With every member now self-managing, `SplatPass` declares:

```cpp
SplatPass(SplatPass&&) noexcept = default;
SplatPass& operator=(SplatPass&&) noexcept = default;
```

The compiler generates the moves and cannot forget a member. `SplatPass.cpp` was rewritten rather than patched — it had accumulated enough incremental edits that a clean rewrite was less risky than another round of surgery. `TonemapPass` still uses hand-written moves and should be converted next.

## Config schema changes

```toml
[stereo]
enabled = true
interpupillary_distance = 0.065
```

`enabled = false` sets `viewCount = 1`, giving a `viewMask` of `0b1`, a single-layer target, and full-width output. Same code path throughout; no mono-specific branches beyond the view count.

## Validation gates

Linux/GCC 13 against lavapipe under Xvfb.

1. **Build** — clean, `-Werror`, zero warnings.
2. **Stereo renders** — side-by-side output, both halves showing the scene at correct aspect ratio.
3. **Parallax is real** — differing pixels between the left and right halves:

   | Eye separation | Differing pixels |
   |---|---|
   | 0.065 m | 54,406 |
   | 0.0 m (control) | 50 |

   Three orders of magnitude apart. The zero-separation control is the important half: it proves the difference comes from the eye offset rather than from the two layers being independently rendered noise.

   The residual 50 pixels at zero separation are expected. Each eye sorts into its own buffer via `atomicAdd`, and among splats sharing a quantised depth the scatter order is nondeterministic. Different arbitrary orderings of coincident splats produce marginally different blends.

4. **Mono path intact** — `enabled = false` renders full width, single layer, zero validation messages.
5. **Validation clean** — zero messages in stereo and mono, across startup, steady state, resize, and shutdown.
6. **Resize in both modes** — stereo 1600x900 → 1000x640 → 1400x700, mono 1600x900 → 1000x640 → 1280x720. The layered HDR target is recreated at the new per-eye extent each time.

## Known gaps

- Windows/MSVC unverified for this phase, including the hardware cost of the second eye. Expect noticeably less than 2x, since the raster pass is shared and the sort is bandwidth-bound rather than compute-bound.
- Symmetric frustum only. Real headsets need asymmetric per-eye projection from the runtime.
- Eye separation is a fixed config value, not read from a headset's device properties.
- Depth range is still derived from the orbit camera's known geometry and is shared between eyes. Correct at these separations, wrong for a free camera.
- The degenerate-instance approach wastes vertex invocations equal to the inter-eye count difference.
- `TonemapPass` still hand-writes its move operations.

## Out of scope

OpenXR session management, swapchain acquisition, predicted display time, and runtime detection with desktop fallback. Phase 6 connects this to a headset — or to Meta XR Simulator, since no hardware is available — and profiles the result against the 2.1 ms Phase 3 baseline.
