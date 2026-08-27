# vksplat

C++20 Vulkan 1.3 renderer for real-time 3D Gaussian splatting, with single-pass stereo and OpenXR support planned. Runs with or without a headset.

## Status

In active development. Device, swapchain and frame loop are implemented; the splat pipeline is not yet.

| Phase | Scope | State |
|---|---|---|
| 0 | Build system, config, logging | Done |
| 1 | Device, swapchain, frame loop | Done |
| 2 | Splat asset pipeline | Next |
| 3 | Projection and rasterisation | |
| 4 | Depth sort and HDR composite | |
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
