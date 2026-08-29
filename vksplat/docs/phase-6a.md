# Phase 6a — GPU Timestamp Profiling

Replace a single aggregate frame rate with a per-pass breakdown measured on the GPU. This is the instrumentation that makes optimisation claims verifiable rather than anecdotal.

## Deliverables

| Component | Files | Responsibility |
|---|---|---|
| GpuProfiler | `render/GpuProfiler.{h,cpp}` | Timestamp query pool, per-frame stamps, averaged results |
| Renderer integration | `render/Renderer.{h,cpp}` | Frame begin, raster and tonemap stamps, result collection |
| Stage stamps | `app/Application.cpp` | Per-eye projection, sort, combine stamps |
| Split recording | `render/SplatPass.{h,cpp}` | `recordProjectionOnly` / `recordSortOnly` so stages can be timed separately |

## How it works

A `VkQueryPool` of `VK_QUERY_TYPE_TIMESTAMP` holds `framesInFlight * 16` queries. Each frame owns a contiguous slice, reset at the start of its command buffer.

`stamp(commandBuffer, stage, label)` writes a timestamp with `vkCmdWriteTimestamp2` and records the label. Each stamp marks the point at which everything before it has completed, so the duration of a stage is the delta between consecutive stamps.

Results are read after the fence for that frame index has been waited on, which is naturally two frames later at `frames_in_flight = 2`. No stall is introduced: by the time we ask, the GPU has long finished. `VK_QUERY_RESULT_WITH_AVAILABILITY_BIT` guards the first frames before any query has been written.

Durations are converted with `VkPhysicalDeviceLimits::timestampPeriod`, which is nanoseconds per tick and varies by vendor.

### Label aggregation

Labels are summed rather than kept per-instance. In stereo, `project` and `sort` are each stamped twice — once per eye — and the reported figure is the total across both. That is the number that matters for a frame budget.

### Reporting

The once-per-second statistics line now reads:

```
370.2 fps (2.70 ms) | 200000 / 200000 visible (100.0%) | project 0.41 sort 0.62 combine 0.01 raster 1.30 tonemap 0.18 ms
```

Averages accumulate over the reporting window and reset after each report, so the numbers track the window rather than the whole run.

## Measurements

Linux/GCC 13 against lavapipe, 200,000 splats, 1600x900 window.

**Absolute values here are meaningless** — lavapipe is a CPU software rasteriser and the raster stage is roughly a thousand times slower than real hardware. The *relative scaling* between mono and stereo is what this platform can tell us, and it is measured rather than assumed.

| Stage | Mono | Stereo | Ratio |
|---|---|---|---|
| project | 7.31 ms | 14.49 ms | 1.98x |
| sort | 4.83 ms | 10.18 ms | 2.11x |
| combine | 0.01 ms | 0.01 ms | 1.00x |
| raster | 853.32 ms | 1746.92 ms | 2.05x |
| tonemap | 26.02 ms | 26.02 ms | 1.00x |
| frame | 903.35 ms | 1818.42 ms | 2.01x |

Three things worth reading out of this table.

**Projection and sort double, exactly as the Phase 5 documentation predicted.** Conic projection is view-dependent, so there is no shared work between eyes. Measurement confirms the reasoning rather than merely agreeing with it.

**Tonemap is identical to two decimal places.** It writes the same swapchain area regardless of view count — a single fullscreen triangle over the window, sampling whichever array layer corresponds to each half. Its cost is a function of output resolution, not eye count. A stage that provably does not scale with view count is a useful thing to have identified.

**Raster also doubles, which is the honest limit of "single-pass" stereo.** Multiview saves draw submission, pipeline binding, and render pass setup. It does not save fragment work: two eyes means twice the covered pixels, and gaussian evaluation is per-pixel. On a GPU-bound software rasteriser the submission savings are invisible. On real hardware, where per-draw CPU overhead is a larger share, the benefit is in CPU time and latency rather than GPU time.

That last point is the sort of thing that is easy to overclaim. Single-pass stereo is worth doing, but the win is not "half the GPU cost".

## Validation gates

1. **Build** — clean, `-Werror`, zero warnings.
2. **Timings are self-consistent** — stage durations sum to within 0.5% of the measured frame time (1736.5 ms of stages against 1743.9 ms frame). Confirms the stamps cover the frame with no unaccounted gaps and no double counting.
3. **Scaling matches prediction** — every stage's mono-to-stereo ratio lands where the architecture says it should, including the two stages that should not scale at all.
4. **Validation clean** — zero messages in mono and stereo with query pool operations active.
5. **Graceful absence** — if `timestampPeriod` is zero, `GpuProfiler::create` fails and the renderer continues without timing rather than aborting. The statistics line reports `no gpu timings`.

## Known gaps

- Windows/MSVC unverified, and the hardware numbers are the entire point of this phase. Lavapipe can only validate ratios.
- Timestamps are written at pipeline stage boundaries, so a stage's measured cost includes any barrier stall attributed to it. Reported figures are wall-clock spans on the GPU timeline, not pure execution time.
- `timestampValidBits` for the queue is not checked. Universal on desktop, not guaranteed on all queue families.
- Averaging is a plain mean over the reporting window; no percentiles, so a single stalled frame skews the report.
- No per-eye breakdown — the two eyes are summed under one label.

## Out of scope

OpenXR session management, swapchain acquisition from the runtime, per-eye asymmetric projection, and runtime detection with desktop fallback. That is Phase 6b, and the instrumentation built here is what will measure it.
