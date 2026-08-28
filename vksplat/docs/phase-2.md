# Phase 2 — Splat Asset Pipeline

Parse 3D Gaussian Splatting `.ply` files, upload to device-local memory, and render every splat as a point. Confirms the asset data is correct before any projection maths is layered on top.

## Deliverables

| Component | Files | Responsibility |
|---|---|---|
| SplatScene | `scene/SplatScene.{h,cpp}` | Generic binary PLY parsing, 3DGS property mapping, activation, bounds |
| OrbitCamera | `scene/OrbitCamera.{h,cpp}` | View-projection from scene bounds |
| GpuBuffer | `render/GpuBuffer.{h,cpp}` | VMA-backed buffer, device-local and staging variants |
| Upload | `render/Upload.{h,cpp}` | Staging copy via transient command submission |
| PointPass | `render/PointPass.{h,cpp}` | Point-list graphics pipeline, SPIR-V loading, draw recording |
| Shaders | `render/shaders/points.{vert,frag}` | Buffer-reference vertex fetch, flat colour output |
| Scene generator | `tools/make_test_scene.py` | Synthetic 3DGS `.ply` at any SH degree |
| Shader build | `cmake/Shaders.cmake` | GLSL to SPIR-V at build time |

## Dependencies added this phase

| Lib | Tag | Why |
|---|---|---|
| glm | `1.0.3` | Matrix and vector maths matching GLSL semantics |
| VulkanMemoryAllocator | `v3.4.0` | Suballocation, budget tracking, usage-based memory selection |
| glslang | `vulkan-sdk-1.4.357.0` | GLSL to SPIR-V at build time, no SDK dependency |

glslang is the slowest dependency to configure and build. It is worth it: shaders compile as part of the normal build on a clean clone with no external toolchain, which keeps the "clone and build" promise from Phase 1 intact.

## PLY parsing

The parser is driven by the header, not by an assumed layout. It reads the property list, records each property's type and byte offset, computes the record stride, then resolves the fourteen required 3DGS fields by name. Files with different property ordering, extra properties, or different SH degrees all parse without code changes.

Required fields: `x y z`, `f_dc_0..2`, `opacity`, `scale_0..2`, `rot_0..3`. Anything else is skipped, including `nx ny nz`, which the INRIA training pipeline writes but never populates meaningfully.

Only `binary_little_endian` is supported. ASCII and big-endian are rejected with a clear message rather than silently misparsed. List properties on the vertex element are rejected because 3DGS files never use them and supporting them would complicate the stride calculation for no benefit.

### Activation at load time

Values in the file are stored in the parameterisation used during training, not the values a renderer wants. Conversion happens once on the CPU at load rather than per-frame in a shader:

| Stored | Activation | Result |
|---|---|---|
| `f_dc_*` | `0.5 + C0 * value`, `C0 = 0.28209479177387814` | Linear RGB |
| `opacity` | Sigmoid | `[0, 1]` |
| `scale_*` | `exp` | World-space standard deviation |
| `rot_*` | Normalise | Unit quaternion |

`f_rest_*` coefficients are counted and reported but not yet loaded. View-dependent colour is a Phase 3 concern; loading 45 floats per splat now would triple memory for data nothing reads.

## GPU layout

```cpp
struct Splat {
    glm::vec3 position;  float opacity;
    glm::vec3 scale;     float padding;
    glm::vec4 rotation;
    glm::vec3 colour;    float padding2;
};
```

64 bytes, enforced by `static_assert`. Four 16-byte rows, so every member lands on a natural std430 boundary with no implicit padding to reason about. 200k splats occupy 12.2 MiB.

## Buffer device address instead of descriptor sets

The vertex shader reaches the splat buffer through `GL_EXT_buffer_reference`, with the address passed in a push constant. There is no descriptor set layout, no descriptor pool, and no descriptor set in the project.

This requires `bufferDeviceAddress` and `scalarBlockLayout` from Vulkan 1.2 and `shaderInt64`, all requested through vk-bootstrap. VMA is created with `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`.

The trade-off is a hard requirement on a feature that is universal on desktop but not on older mobile hardware. Given the target is desktop and XR on modern GPUs, removing an entire subsystem is the better trade.

## Upload path

`uploadDeviceLocal` allocates a host-visible staging buffer with `VMA_ALLOCATION_CREATE_MAPPED_BIT`, memcpys, allocates a device-local destination, and copies through a transient command pool with a fence wait. The staging buffer is destroyed when the function returns.

Blocking is correct here — nothing can render before the scene is resident, and this runs once at startup.

## Renderer change

`Renderer::drawFrame` now accepts a `RecordFunction` invoked between `vkCmdBeginRendering` and `vkCmdEndRendering`. The renderer owns synchronisation, acquisition, and presentation; it knows nothing about splats. Passes stay independent of frame management, which matters once stereo and sorting arrive.

## Shader compilation

`cmake/Shaders.cmake` adds a custom command per shader invoking the `glslang-standalone` target built from source, targeting `vulkan1.3`. Outputs are staged next to the executable alongside `config/`, and resolved at runtime through the same `platform::resolveResource` search used for config.

## Test scene generator

Real 3DGS scenes run 300 MiB to 1.5 GiB, which is impractical to commit and slow to iterate against. `tools/make_test_scene.py` writes a spiral galaxy in exact INRIA format with configurable splat count and SH degree.

This is a correctness tool, not a convenience. The generator writes the *stored* parameterisation — inverse sigmoid for opacity, log for scale, inverse SH for colour — so a round trip through the loader that produces the intended colours and sizes validates the activation maths independently of any external file.

`assets/` is gitignored. The generator is committed; generated scenes are not.

## Config schema changes

`[renderer]` gained `point_size`. A new `[camera]` section:

```toml
[camera]
field_of_view_degrees = 55.0
near_plane = 0.01
far_plane = 500.0
orbit_degrees_per_second = 12.0
elevation_degrees = 18.0
distance_multiplier = 2.4
```

Camera distance is `scene_radius * distance_multiplier`, so framing works for any scene without retuning. Projection uses a flipped Y to match Vulkan clip space.

## Validation gates

Linux/GCC 13 against lavapipe under Xvfb.

1. **Build** — clean, `-Werror`, zero warnings. VMA's implementation TU is compiled with warnings disabled via a source file property; it is third-party code and its diagnostics are not actionable.
2. **Parse, SH degree 0** — 200,000 splats, 12.2 MiB, 0 SH coefficients, bounds `[-2.136, -0.497, -2.286]` to `[2.412, 0.460, 1.973]`, radius 3.152.
3. **Parse, SH degree 3** — 50,000 splats, 45 SH coefficients, 248-byte stride. Bounds match the degree-0 scene to three decimals despite a completely different record layout, confirming offset resolution is correct rather than coincidental.
4. **Upload** — 12.2 MiB reaches device-local memory, no validation errors.
5. **Render** — framebuffer capture shows the spiral structure with a warm core and blue outer arms, matching the generator's colour ramp. 1,190 unique colours against a 2-colour clear-only frame.
6. **Animation** — two captures five seconds apart differ in 74,345 pixels, confirming the orbit advances with frame timing.
7. **Validation clean** — zero messages across startup, upload, steady state, resize, and shutdown.
8. **Resize under load** — 1600x900 to 900x640 to 1280x720 while drawing 200k points, no errors.
9. **Error paths** — missing file reports all searched paths; truncated file reports expected versus actual bytes; ASCII PLY rejected by format; missing `z` property named explicitly.

## Known gaps

- Windows/MSVC unverified for this phase.
- No depth buffer. Points draw in buffer order and blend without occlusion. Correct ordering is Phase 4.
- `f_rest_*` counted but not loaded.
- Splats render as fixed-size points; `scale` and `rotation` are uploaded but unused until Phase 3.
- Whole scene uploaded in one allocation. Scenes beyond available VRAM will fail at allocation with a VMA error rather than degrading.

## Out of scope

Gaussian projection, conic evaluation, depth sorting, alpha compositing. Phase 3 replaces the point pipeline with screen-space conic rasterisation.
