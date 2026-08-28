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
