# vksplat

C++20 Vulkan 1.3 renderer for real-time 3D Gaussian splatting, with single-pass stereo and OpenXR support planned. Runs with or without a headset.

## Status

In active development. Loads 3DGS `.ply` scenes, projects each gaussian to a screen-space conic in a compute pass, and rasterises them with indirect draws. Depth sorting is not yet implemented, so blending order is arbitrary.

| Phase | Scope | State |
|---|---|---|
| 0 | Build system, config, logging | Done |
| 1 | Device, swapchain, frame loop | Done |
| 2 | Splat asset pipeline | Done |
| 3 | Projection and rasterisation | Done |
| 4 | Depth sort and HDR composite | Next |
| 5 | Single-pass stereo | |
| 6 | OpenXR integration and profiling | |

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

`--sh-degree 0..3` controls how many spherical harmonic coefficients are written. Point `scene.path` in the config at any 3DGS `.ply`; trained scenes from the INRIA reference implementation load unmodified.

## Configuration

Runtime settings live in `config/app.toml`. Nothing is compiled in. Unknown keys are rejected rather than ignored.

```toml
[renderer]
validation = true
clear_color = [0.02, 0.03, 0.05, 1.0]
```

Pass an alternative file as the first argument.

## Verified on

| Platform | Toolchain | GPU |
|---|---|---|
| Windows | MSVC, `/W4 /WX` | Quadro RTX 5000 |
| Linux | GCC 13, `-Werror` | lavapipe (software) |

Zero validation-layer messages on both.

## Documentation

Per-phase technical notes are in [`docs/`](docs/).
