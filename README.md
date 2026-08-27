# VR-Prototype-Vulkan
Real-time 3D Gaussian splat renderer in C++20 and Vulkan 1.3, with single-pass stereo and OpenXR support. Cross-platform, dynamic rendering, no legacy render passes


# Device and Frame Loop

Vulkan 1.3 device, swapchain, and a presenting frame loop that clears to a configured colour and survives resize. No geometry, no shaders, no allocations yet.

## Deliverables

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

