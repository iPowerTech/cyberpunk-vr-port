# CPU-oriented stereo: engine references and planning estimate

Research date: 2026-09-28. No game launch, deployment or renderer change was
needed for this review. The estimates below are engineering forecasts, not
measured completion percentages or performance promises.

## Primary references inspected

- Unity Graphics source: [XRSystem.cs](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.core/Runtime/XR/XRSystem.cs).
  `CreateLayoutFromXrSdk` obtains culling parameters for a render pass and groups
  compatible views into one XRPass. `CanUseSinglePass` checks two texture-array
  slices and matching viewports. This grouping precedes rendering.
- Unity Graphics source: [XRPass.cs](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.core/Runtime/XR/XRPass.cs).
  Stores per-view matrices and slice indices, pools pass objects, and enables
  multiview or stereo instancing in `StartSinglePass`. The latter path sets the
  instance multiplier to the view count. This is an SRP-side source inspection;
  it is not an inspection of Unity's closed native renderer.
- Unity manual: [custom stereo shaders](https://docs.unity3d.com/6000.0/Documentation/Manual/SinglePassInstancing.html).
  Screen-space sampling must select the right eye/texture-array layer as well
  as producing eye-specific vertex positions. Adapting only the vertex stage
  does not complete the image pipeline.
- Epic: [Mesh Drawing Pipeline](https://dev.epicgames.com/documentation/en-us/unreal-engine/mesh-drawing-pipeline-in-unreal-engine).
  Describes cached draw commands, per-pass setup/dispatch tasks, separating
  parameter frequencies, compatible shader bindings, and explicit invalidation
  when cached resource dependencies change. Generic mesh-command caching and
  merging are related architecture, not themselves identical to stereo.
- Epic: [FSimpleMeshDrawCommandPass](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Renderer/FSimpleMeshDrawCommandPass).
  Its stereo setup supplies two ViewIds for instance culling and a legacy
  instance factor of two. The renderer implementation itself was not available
  in this review; these are public API/docs observations.
- NVIDIA: [Multi View Rendering sample](https://github.com/nvpro-samples/gl_multi_view_rendering).
  Demonstrates stereo/multiview by changing uniform/framebuffer setup and
  shader view selection. Its CPU, geometry and fragment load controls help
  distinguish where a benefit comes from. This is an OpenGL sample, not a
  drop-in implementation for this D3D12 port.
- Epic historical measurement: [UE 4.11 release notes](https://www.unrealengine.com/blog/unreal-engine-4-11-released?lang=en-US).
  Reported roughly 14% CPU-time and 7% GPU improvement on Bullet Train.
  This is a 2016, content-specific result, not a forecast for Cyberpunk.

## Implications for this port (our inference)

The existing prototype proves paired cameras and selected shared GPU geometry.
It does not yet avoid the native CPU work of preparing both eyes. Continuing
only with late DrawIndexedInstanced substitution cannot deliver the desired
CPU optimization: native preparation, diagnostic reference work and conditional
fallback commands remain. Its late visibility wait is not a production design.

The next implementation target is a paired-view pass before native draw-command
generation/consumption, with independently correct eye visibility. Reuse shared
object/material data, build or select compatible commands once, and retain
per-eye matrices/history. Start with depth and opaque geometry. A common culling
volume may be a conservative candidate set; it must not silently replace
per-eye occlusion decisions with one eye's result.

Batch shader/PSO analysis should classify camera and screen-space dependencies,
validate transformations, and cache variants. Coverage must be weighted by actual
CPU cost and draw frequency. Array targets should persist across a compatible
pass; copying a small group back repeatedly defeats the purpose. VRS and temporal
effects require explicit per-eye treatment. Unsupported passes can remain native.

## Forecast from the current checkpoint

Assumes active work sessions at approximately the recent pace, stable builds,
available scenes and no major new engine dependency.

| Additional active time | Planning target |
| --- | --- |
| 1-2 days | CPU critical-path measurements, batch shader inventory, and proof that shared preparation can be inserted early enough |
| 3-7 days total | First continuously running depth/opaque prototype with measured CPU benefit in selected scenes, if the early-insertion proof succeeds |
| 2-4 weeks total | A candidate for broad gameplay testing: materials, animation, transparency, camera modes, loading, resolution changes and temporal/FG integration |

These are low-confidence ranges until the early CPU integration is demonstrated.
The final phase can take longer or require narrower coverage. One day spent on
the existing GPU proof cannot be extrapolated linearly into a release date.
The next one or two sessions should reduce this uncertainty with CPU evidence,
not merely produce another shader that compiles or another tiny GPU saving.

Acceptance measurements: critical CPU frame time with diagnostics off, command
recording count, whole-frame GPU time, image/depth/MV correctness in both eyes,
and resource lifetime across scene/load changes. Report GPU and CPU savings
separately. No numeric overall FPS improvement is promised by this forecast.
