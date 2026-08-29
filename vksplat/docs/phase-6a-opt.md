# Phase 6a-opt — Raster Optimisation

Phase 6a's instrumentation showed where the frame actually goes. This phase acts on that measurement.

## What the profiler said

On the Quadro RTX 5000, stereo, 200,000 splats at 1600x900:

```
project 0.19  sort 0.36  combine 0.01  raster 5.60  tonemap 0.02 ms
```

Raster is **90% of the frame**. The GPU counting sort, which took the most design effort in Phase 4, is 6%. Tonemap is 0.3%.

This is worth stating plainly because the intuition was wrong. Before measuring, the sort looked like the expensive stage — it is the most algorithmically involved part of the pipeline. It is not where the time goes. The time goes into fragment shading: 200,000 gaussians, heavily overlapping, each covering a quad, each covered pixel evaluating an `exp()`. It is an overdraw problem, and the lever is quad area.

## Optimisation 1: exact per-axis bounds

The quad half-extent was:

```glsl
largestEigenvalue = middle + sqrt(max(0.01, middle * middle - determinant));
radius = 3.0 * sqrt(largestEigenvalue);
```

One scalar radius, applied to both axes — a **square** sized to the ellipse's major axis. For an elongated splat this covers far more area than the ellipse occupies.

The axis-aligned bounding box of a 2D gaussian's sigma-contour is available directly from the covariance diagonal:

```glsl
extent = sigma * sqrt(vec2(a, c));
```

where `a` and `c` are the variances in x and y. This is exact, not an approximation, and it eliminates the eigenvalue computation as a side effect.

**Zero quality change**, because every pixel removed was outside the ellipse and would have been discarded by the fragment shader anyway. The pixels are not being skipped — they were never inside the gaussian.

The `Projected` struct absorbed this for free. `radius` (float) and `depth` (float) became `extent` (vec2): `depth` was written but never read, since the sort key is computed in the same shader that writes it. The struct stays 48 bytes.

## Optimisation 2: configurable sigma cutoff

Three standard deviations is the conventional cutoff. At 3σ the gaussian's value is `exp(-4.5)` ≈ 0.011 of its peak, and the fragment shader already discards anything below `1/255` ≈ 0.0039. So the outermost ring of every quad is mostly generating discarded fragments.

`splat_extent_sigma` is now config-driven, so the trade-off is measurable rather than baked in.

Unlike the first optimisation, this one **does** change the image. Quantifying by how much is the point.

## Measurements

Linux/GCC 13, lavapipe, mono, 200,000 splats, 1600x900. Absolute values are software-rasteriser figures; the ratios are what transfer.

| Configuration | Raster | vs baseline |
|---|---|---|
| Square bound, 3σ (baseline) | 853.32 ms | — |
| Exact AABB, 3σ | 764.20 ms | −10.4% |
| Exact AABB, 2.5σ | 633.03 ms | −25.8% |
| Exact AABB, 2.0σ | 514.97 ms | −39.6% |

Quality against the 3σ exact-AABB reference, camera frozen so only the cutoff varies:

| Cutoff | RMSE | Differing pixels |
|---|---|---|
| 2.5σ | 0.21% | 20,754 of 1,440,000 (1.4%) |
| 2.0σ | 0.70% | 34,225 of 1,440,000 (2.4%) |

**2.5σ buys a further 17% of raster time for a 0.21% RMSE change.** That is the new default. 2.0σ is available but the error grows more than three times faster than the saving.

### A measurement mistake worth recording

The first attempt at the quality comparison gave 2.8% RMSE for 2.5σ — an order of magnitude too high — because the camera was orbiting and each configuration was captured at a different angle. The metric was measuring camera rotation, not the cutoff.

Freezing the camera with `orbit_degrees_per_second = 0` fixed it. The control that caught it: comparing the 3σ capture against itself returns exactly 0 differing pixels, which also confirms the renderer is deterministic frame to frame despite the sort's atomic scatter.

A benchmark that varies two things at once measures neither.

## Validation gates

1. **Build** — clean, `-Werror`, zero warnings.
2. **Determinism** — identical config, identical camera, two runs: 0 differing pixels.
3. **Exact AABB is lossless** — the geometric argument is that removed pixels lie outside the ellipse and were already discarded. Supported by the raster time dropping 10.4% while the visible result is unchanged.
4. **Quality curve measured** — RMSE and differing-pixel counts at each cutoff with a controlled camera.
5. **Validation clean** — zero messages across all configurations.

## Known gaps

- Ratios come from lavapipe. Hardware measurement is required to confirm they transfer, and the RTX 5000 baseline of `raster 5.60 ms` is the figure to compare against.
- RMSE over the whole frame under-weights localised error. The differences concentrate at splat edges, which is where a perceptual metric would weight most heavily.
- Only one scene tested. A scene with more elongated gaussians would benefit more from the exact AABB, since that optimisation's value scales with anisotropy.
- Overdraw itself is untouched. Tile-based binning or a coarse depth pre-pass would attack the real problem rather than trimming its edges.
