# VR-Prototype-Vulkan
Real-time 3D Gaussian splat renderer in C++20 and Vulkan 1.3, with single-pass stereo and OpenXR support. Cross-platform, dynamic rendering, no legacy render passes


# Phase 1 Device and Frame Loop

Vulkan 1.3 device, swapchain, and a presenting frame loop that clears to a configured colour and survives resize. No geometry, no shaders, no allocations yet.

## Content

| Component | Files | Responsibility |
|---|---|---|
| Window | `platform/Window.{h,cpp}` | GLFW lifetime, framebuffer extent, surface creation |
| Paths | `platform/Paths.{h,cpp}` | Executable directory lookup, resource resolution |
| VulkanContext | `render/VulkanContext.{h,cpp}` | Instance, surface, physical/logical device, graphics queue |
| Swapchain | `render/Swapchain.{h,cpp}` | Swapchain, images, image views, recreation |
| Renderer | `render/Renderer.{h,cpp}` | Frame sync, command recording, submit, present |
| Application | `app/Application.{h,cpp}` | Subsystem ownership, main loop, resize recovery |

## Dependencies added this phase

| Lib | Tag | Why |
|---|---|---|
| GLFW | `3.4` | Window and surface, all three target platforms |
| volk | `vulkan-sdk-1.4.357.0` | Dynamic entrypoint loading, no import library needed |
| vk-bootstrap | `v1.4.357` | Instance/device/swapchain selection boilerplate |
| Vulkan-Headers | `vulkan-sdk-1.4.357.0` | Pinned headers, downloaded but not configured as a subproject |

VMA and glm were deliberately not added — nothing allocates or does matrix maths yet.

## Design decisions

Vulkan SDK is not a build dependency. Headers are pinned via `FetchContent` and volk loads entrypoints at runtime, so nothing links against `vulkan-1.lib`. The SDK is only needed at runtime for validation layers. This removes a class of version-drift failure — the system SDK on the Linux test machine was older than vk-bootstrap required, and pinning fixed it permanently rather than per-machine.

Vulkan-Headers is declared with `SOURCE_SUBDIR do-not-configure` so CMake downloads it without adding its project, avoiding a duplicate `Vulkan::Headers` target that vk-bootstrap defines itself.

Vulkan 1.3 core features, no extension juggling. `dynamicRendering` and `synchronization2` are required features, not extensions. Render passes and framebuffers are never created. Barriers use `VkImageMemoryBarrier2` and submission uses `vkQueueSubmit2`.

Present semaphores are per swapchain image, not per frame in flight. The frequently-copied tutorial layout signals a per-frame `renderFinished` semaphore, which can be waited on by a present that has not yet completed when the frame index wraps. Sizing the signal semaphores to the swapchain image count and indexing by acquired image removes the hazard. `imageAvailable` semaphores and fences remain per frame in flight.

Resize is driven by swapchain results, not window callbacks. `VK_ERROR_OUT_OF_DATE_KHR` and `VK_SUBOPTIMAL_KHR` from acquire or present trigger recreation. No resize flag, no callback state. This also covers driver- or compositor-initiated invalidation that a GLFW callback would miss. Minimised windows are handled by blocking in `waitForNonZeroExtent` until the framebuffer is non-degenerate.

Recreation passes the old swapchain handle to `set_old_swapchain`, letting the driver reuse resources instead of tearing down completely.

Validation output is routed into spdlog via a debug messenger callback, mapping Vulkan severities onto log levels. One output stream for engine and driver diagnostics.

Ownership is move-only RAII with static factories. Constructors cannot report failure, so each type exposes `static Result<T> create(...)` and a private constructor. All types are non-copyable, movable, and destroy in reverse declaration order inside `Application`: Renderer, Swapchain, VulkanContext, Window.

## Config schema changes

`[renderer]` gained one key:

```toml
clear_color = [0.02, 0.03, 0.05, 1.0]
```

Four numbers, each validated to `[0, 1]`. Values are linear; the swapchain format is sRGB, so the encoded output differs from the literal (see gate 4).

## Resource resolution

Phase 0 resolved the config path relative to the working directory, which broke under the Visual Studio debugger where the working directory is `build/Debug`.

Resolution order is now: absolute path used as-is, otherwise current directory, then the executable's own directory. The executable path comes from `GetModuleFileNameW` on Windows, `_NSGetExecutablePath` on macOS, and `/proc/self/exe` on Linux. CMake stages `config/` next to the binary post-build and sets `VS_DEBUGGER_WORKING_DIRECTORY`. A failed lookup reports every path searched.

## Portability note: implicit move on return

`Application::create` and the three other factories return a local by value through a conversion to `Result<T>`. C++20 (CWG1579) treats the local as an rvalue in that position; GCC 13 implements this, MSVC 19.4x does not, and rejected the code because the copy constructor is deleted.

Adding `std::move` fixed MSVC but tripped GCC's `-Wredundant-move` under `-Werror`. The form both compilers accept is explicit construction of the return type:

```cpp
return core::Result<Application>(std::move(application));
```

Worth knowing for every factory added in later phases.

## Validation gates

Verified on Linux/GCC 13 against lavapipe under Xvfb, and on Windows/MSVC against a Quadro RTX 5000.

1. Build : clean configure and build on both toolchains, `-Werror` / `/W4 /WX`, zero warnings.
2. Startup : device and swapchain reported. Linux: `llvmpipe`, 4 images. Windows: `Quadro RTX 5000`, 3 images.
3. Validation clean : `VK_LAYER_KHRONOS_validation` confirmed loaded via `VK_LOADER_DEBUG=layer`; zero messages emitted across startup, steady-state, resize, and shutdown.
4. Pixel correctness : framebuffer readback returns `srgb(39, 48, 63)` for the configured linear `[0.02, 0.03, 0.05]`, matching the linear-to-sRGB encoding of the swapchain format. Confirms both the clear value and the format assumption.
5. Resize : live resize 1600x900 → 900x640 → 1280x720 logs a recreation per change with no validation errors and no leaked swapchain objects.
6. Config resolution : verified from a foreign working directory, from the repo root, with a relative argument, with an absolute argument, and with a missing file reporting all searched paths.
7. Config permutations : `vsync` true/false and `validation` true/false all start and present.

## Known gaps

- macOS is implemented but untested; no machine available. MoltenVK does not expose Vulkan 1.3 core features the same way and will need attention if that platform matters.
- `MAILBOX` present mode is requested when `vsync = false`; vk-bootstrap silently falls back to `FIFO` where unsupported. Not currently surfaced in the log.
- No frame timing or FPS counter yet.

## Out of scope

Buffers, images, shaders, pipelines, geometry. Phase 2 begins with the splat asset pipeline and introduces VMA.


# Phase 2 — Splat Asset Pipeline

Parse 3D Gaussian Splatting `.ply` files, upload to device-local memory, and render every splat as a point. Confirms the asset data is correct before any projection maths is layered on top.

## Phase 2 - Deliverables

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


# Phase 3 — Projection and Rasterisation

Replace flat points with real Gaussian splatting. A compute pass projects each 3D gaussian into a screen-space conic, culls it, and appends survivors to a visible list. An indirect draw rasterises one quad per survivor, and the fragment shader evaluates the gaussian per pixel.

This is the mathematical core of the technique.

## Deliverables

| Component | Files | Responsibility |
|---|---|---|
| SplatPass | `render/SplatPass.{h,cpp}` | Compute and graphics pipelines, visible list, indirect arguments, statistics readback |
| Shared shader types | `render/shaders/splat_common.glsl` | Buffer reference declarations shared across stages |
| Projection | `render/shaders/project.comp` | 3D covariance, screen-space conic, culling, compaction |
| Rasterisation | `render/shaders/splat.{vert,frag}` | Instanced quad expansion, per-pixel gaussian evaluation |

`PointPass` and `points.{vert,frag}` are deleted. The point path was scaffolding for Phase 2's gate and keeping it would mean maintaining two renderers.

## The projection maths

Each splat carries a scale vector and a rotation quaternion. Together they define a 3D covariance matrix:

```
M = R * S
Sigma = M * M^T
```

Projecting a 3D gaussian through a perspective camera does not give a gaussian — perspective division is non-linear. The standard approach linearises it with the Jacobian of the projection at the splat's centre:

```
J = [ f/z    0    -f*x/z^2 ]
    [  0    f/z   -f*y/z^2 ]
    [  0     0        0    ]
```

Combining with the view rotation `W` gives the 2D screen-space covariance:

```
T = J * W
Sigma_2D = T * Sigma * T^T
```

This is an approximation, and it degrades toward the edges of a wide field of view. It is the same approximation the reference implementation makes, and it is why splat renderers show mild distortion at extreme FOV.

### Coordinate handedness

`glm::lookAt` produces a right-handed view space with the camera looking down `-z` and `+y` up. The Jacobian above assumes `+z` forward and `+y` down, matching Vulkan's screen convention.

Rather than rewriting the Jacobian, view-space coordinates are flipped through `diag(1, -1, -1)`, and the same flip is folded into `W` so the covariance transform stays consistent. Getting this wrong produces a scene that renders but is subtly mirrored or vertically inverted — the kind of bug that survives a screenshot check.

### Low-pass filter

`0.3` is added to the diagonal of the 2D covariance before inversion. Without it, splats that project to less than a pixel produce a near-singular matrix, and the conic inversion explodes. This dilates every splat to a minimum of roughly one pixel. Standard practice, and the same constant the reference implementation uses.

### Conic form

The fragment shader needs the inverse of the 2D covariance:

```
conic = (c, -b, a) / (a*c - b*b)
```

Then per-pixel opacity is `opacity * exp(-0.5 * (conic.x*dx^2 + conic.z*dy^2) - conic.y*dx*dy)`. Storing the conic rather than the covariance moves the inversion out of the fragment shader, where it would run once per covered pixel instead of once per splat.

### Quad extent

The larger eigenvalue of the 2D covariance gives the major axis. The quad half-extent is `3 * sqrt(lambda_max)` — three standard deviations, past which the gaussian contributes less than one 255th of its peak and the fragment shader discards anyway.

## Culling and compaction

Three rejections in the compute pass:

1. **Near plane** — `camera.z <= near`, removing splats behind the camera.
2. **Degenerate covariance** — non-positive determinant.
3. **Screen bounds** — the splat's quad entirely outside the viewport.

Survivors claim a slot with `atomicAdd` on the indirect draw's `instanceCount`. This compacts the visible set into a dense prefix of the projected buffer with no gaps, so the raster pass draws exactly `instanceCount` instances with no per-instance visibility test.

The indirect arguments are reset each frame with `vkCmdUpdateBuffer` writing `{4, 0, 0, 0}` — four vertices per quad, zero instances — followed by a barrier before the dispatch.

## Draw path

`VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP`, four vertices, `instanceCount` instances, via `vkCmdDrawIndirect`. The vertex shader derives the corner from `gl_VertexIndex` bits and reads its splat from the projected buffer by `gl_InstanceIndex`. No vertex or index buffers exist in the project.

Blending is premultiplied: the fragment shader outputs `vec4(colour * alpha, alpha)` and the pipeline uses `ONE, ONE_MINUS_SRC_ALPHA`. Premultiplied alpha composes correctly under repeated blending, which matters once Phase 4 sorts and composites thousands of overlapping splats.

## Renderer change

`drawFrame` now takes two callbacks: one recorded before `vkCmdBeginRendering`, one inside. Compute dispatches cannot run inside a dynamic rendering scope, so the projection pass needs a slot outside it. The renderer still owns synchronisation and knows nothing about splats.

## Statistics readback

`SplatPass` keeps a 16-byte host-visible buffer. After the projection barrier, the indirect arguments are copied into it, and `visibleSplats()` reads `instanceCount` from the mapping.

The value lags by up to `frames_in_flight` frames, which is fine for a once-per-second report and avoids any stall. Enabled by `log_statistics` in the config; it also becomes the hook for Phase 6 profiling.

## Config schema changes

`[renderer]` lost `point_size` — splat extent now comes from the projected covariance. It gained:

```toml
log_statistics = true
```

## Two bugs validation caught

**`DemoteToHelperInvocation`.** `discard` in a fragment shader compiles to `OpDemoteToHelperInvocation` when targeting Vulkan 1.3, which requires `shaderDemoteToHelperInvocation`. The pipeline created without it and the shader would have run on this driver, but the code was invalid and would have failed elsewhere.

**Missing `TRANSFER_SRC`.** The indirect buffer was created without `VK_BUFFER_USAGE_TRANSFER_SRC_BIT` and then used as a copy source for the statistics readback. Worked on lavapipe, invalid by spec.

Both are silent-on-this-driver, broken-on-another bugs. This is the argument for keeping validation on in development builds and treating any message as a failure.

## Validation gates

Linux/GCC 13 against lavapipe under Xvfb.

1. **Build** — clean, `-Werror`, zero warnings. Shader includes required enabling `GL_GOOGLE_include_directive` and passing `-I` to glslang with no space after the flag.
2. **Render** — smooth elliptical falloff replaces hard dots. 8,918 unique colours against 1,190 for the Phase 2 point path, consistent with per-pixel gaussian evaluation rather than flat fill.
3. **Compaction path** — an artificial `index & 1` cull inserted into the compute shader reported exactly 100,000 of 200,000. Confirms the atomic counter, indirect arguments, and readback are correct independently of the projection maths.
4. **Geometric culling** — visible counts against field of view at fixed camera distance:

   | FOV | Visible | Fraction |
   |---|---|---|
   | 55° | 200,000 | 100% |
   | 10° | 150,981 | 75.5% |
   | 4° | 51,564 | 25.8% |

   Counts drift by a few hundred between frames as the camera orbits, which is expected for a rotating non-symmetric scene. 100% at 55° is correct rather than a failure — the scene fits entirely inside the frustum at that field of view.

5. **Validation clean** — zero messages across startup, steady state, resize, and shutdown, after fixing the two bugs above.
6. **Resize under load** — 1600x900 to 900x640 to 1280x720 while projecting and rasterising 200k splats, no errors.

## On the frame rate in these gates

The statistics line reports 1.1 to 1.6 fps. This is lavapipe, a CPU software rasteriser, evaluating gaussians for every covered pixel of 200,000 overlapping splats. It says nothing about GPU performance and should not be read as a baseline. Meaningful numbers require the discrete GPU, and profiling is Phase 6.

## Known gaps

- Windows/MSVC unverified for this phase.
- **No depth sorting.** Splats blend in whatever order the atomic counter assigned, which is nondeterministic between frames. On a dense scene this is visible as flicker and incorrect occlusion. Phase 4.
- No depth buffer, and none is planned — correct splat compositing is sort-then-blend, not depth testing.
- `f_rest_*` still unloaded, so colour is view-independent.
- The projected buffer is sized for every splat being visible. Correct but pessimistic; a tighter bound needs a worst-case visibility estimate.
- The Jacobian linearisation degrades at wide FOV, inherent to the technique.

## Out of scope

Depth sorting, HDR render target, tonemapping. Phase 4 adds a GPU radix sort keyed on the depth already written into each projected entry, and composites into a floating-point target.



