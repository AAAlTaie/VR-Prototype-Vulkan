# vksplat

C++20 Vulkan 1.3 renderer for real-time 3D Gaussian splatting, with single-pass stereo and OpenXR support planned. Runs with or without a headset.

## Status

In active development. Loads 3DGS `.ply` scenes, projects each gaussian to a screen-space conic in a compute pass, sorts back to front with a GPU counting sort, composites into an HDR target and tonemaps. Renders both eyes in a single multiview pass with per-stage GPU timing. OpenXR integration is not yet implemented.

| Phase | Scope | State |
|---|---|---|
| 0 | Build system, config, logging | Done |
| 1 | Device, swapchain, frame loop | Done |
| 2 | Splat asset pipeline | Done |
| 3 | Projection and rasterisation | Done |
| 4 | Depth sort and HDR composite | Done |
| 5 | Single-pass stereo | Done |
| 6a | GPU timestamp profiling | Done |
| 6a-opt | Raster optimisation | Done |
| 6b | OpenXR integration | Next |

## Requirements

- CMake 3.24+
- A C++20 compiler (MSVC 19.4x, GCC 13, Clang 16)
- A Vulkan 1.3 capable GPU and driver
- Vulkan SDK, runtime only, for validation layers

The SDK is not a build dependency. Headers are pinned via `FetchContent` and entrypoints load dynamically through volk.

## Build

```
cmake -S . -B build
cmake --build build --config Release
./build/vksplat
```

All dependencies are fetched automatically. The config directory is staged next to the executable, so the binary runs from any working directory.

## Test scene

Real 3DGS scenes are hundreds of megabytes, so a generator is included:

```
python3 tools/make_test_scene.py assets/test_galaxy.ply --count 200000
```

`--sh-degree 0..3` controls how many spherical harmonic coefficients are written. `--shape layers` produces a two-sheet scene used to verify depth sort direction. Point `scene.path` in the config at any 3DGS `.ply`; trained scenes from the INRIA reference implementation load unmodified.

## Configuration

Runtime settings live in `config/app.toml`. Nothing is compiled in. Unknown keys are rejected rather than ignored.

```toml
[renderer]
validation = true
clear_color = [0.02, 0.03, 0.05, 1.0]

[stereo]
enabled = true
interpupillary_distance = 0.065
```

Set `stereo.enabled = false` for full-width mono output.

Pass an alternative file as the first argument.

## Verified on

| Platform | Toolchain | GPU |
|---|---|---|
| Windows | MSVC, `/W4 /WX` | Quadro RTX 5000 |
| Linux | GCC 13, `-Werror` | lavapipe (software) |

Zero validation-layer messages on both.

Measured on the Quadro RTX 5000, 200,000 splats at 1600x900, entirely GPU-bound:

| Configuration | Frame time |
|---|---|
| Points only (phase 3) | 2.08 ms |
| Gaussian, sorted, HDR (phase 4) | 2.70 ms |

With `log_statistics = true` the app reports a per-stage GPU breakdown each second:

```
370.2 fps (2.70 ms) | 200000 / 200000 visible (100.0%) | project 0.19 sort 0.36 combine 0.01 raster 5.60 tonemap 0.02 ms
```

Profiling showed raster at 90% of the frame and the GPU sort at 6%, which redirected optimisation away from where intuition pointed. Two changes followed:

| Change | Raster time | Quality |
|---|---|---|
| Square bound, 3σ (baseline) | — | reference |
| Exact per-axis AABB, 3σ | −10.4% | unchanged, bound is exact |
| Exact AABB, 2.5σ cutoff | −25.8% | 0.21% RMSE |

Ratios measured under lavapipe. `splat_extent_sigma` in the config controls the cutoff; 3.0 reproduces the reference.

## Documentation

Per-phase technical notes are in [`docs/`](docs/).
