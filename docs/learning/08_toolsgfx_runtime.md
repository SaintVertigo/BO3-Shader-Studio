# TOOLSGFX vs BO3 Runtime — Why the Same Shader Can Look Different

**Level: Advanced**

This distinction matters most for PostFX.

## TOOLSGFX / material-preview path

Tool/editor preview often uses normal image fallbacks and tool-side constants. In a techset, `TOOLSGFX` can select those bindings.

For exported PostFX, the Previewer now follows the APE shape demonstrated by LG-RZ/BlackOps3Shaders rather than reusing the Runtime package unchanged:

- the TOOLSGFX techset uses the stock `vs_generic` vertex stage when the exporter can mirror its expected fullscreen signature;
- converted fullscreen shaders mirror the LG-RZ TOOLSGFX `TEXCOORD1` compatibility bridge (`fakeInput`) before selecting `vs_generic`; if that bridge cannot be produced safely, the exporter keeps the authored fullscreen vertex stage instead of forcing `vs_generic`;
- `Globals()` uses an opaque preview RenderFlags block;
- the BO3-facing preview scene resource is normalized to `sceneTexture` at `t0`; converted Shadertoy code keeps `iChannel0` through `#define iChannel0 sceneTexture`;
- the scene-backed texture is presented as **Preview Scene** through `colorMap00`, using BO3's stock `zm_zod_scene` image;
- the standard `bilinearClampler` uses `s0` in the separate `shaders_stable_toolsgfx` copy while Runtime keeps the BO3 `s1` convention;
- converted Shadertoy wrappers call `PostFx_FixPreviewResolution(...)` before rebuilding lower-left `fragCoord`;
- converted scene output uses `LinearToSRGB(...)` for APE display;
- the TOOLSGFX HLSL filename includes an 8-hex content hash (for example `sv_effect_ape_1a2b3c4d.hlsl`). APE was confirmed to reuse stale compiled shader state when a changed generated shader reused the same source filename; content-addressing gives changed source a fresh identity. The hashed TOOLSGFX shader stays at the `shaders_stable_toolsgfx` root so `#include "postfx/postfx_common.h"` remains BO3-linker-safe;
- `.hlsl` is normalized before export so generated filenames cannot accidentally become `.hlsl.hlsl`.

In APE, right-click the viewport and use **Rendering -> No Lighting** and **Shape -> Plane**.

## BO3 runtime path

At runtime, engine resources such as `resolvedScene` and `floatZ` can be supplied through `CodeTexture`.

The bundled PostFX techset demonstrates the split:

```text
#if TOOLSGFX != "1"
    frameBuffer = CodeTexture("resolvedScene")
    DepthSampler = CodeTexture("floatZ")
#endif
```

## PostFX color scale

A major runtime-parity lesson is that BO3 runtime PostFX data should not always be treated like an ordinary already-displayed 0..1 image.

The reliable bundled pattern is:

```hlsl
float3 color = PostFx_NormalizeColor(
    frameBuffer.Sample(frameBufferSampler, uv).rgb);

// effect math here

return float4(PostFx_DenormalizeColor(color), 1.0);
```

## LDR approximation vs scene-linear HDR

PNG/JPEG screenshots are already display-oriented and cannot preserve scene values above their encoded range. For closer `resolvedScene` simulation, use a scene-linear HDR source such as EXR where appropriate.

The Previewer labels ordinary image/live-capture runtime simulation as an **LDR approximation** rather than pretending it is exact BO3 scene data.

## Practical rule

When a shader works in TOOLSGFX but fails or looks wrong in game, inspect:

1. CodeTexture bindings
2. source color scale / normalize-denormalize usage
3. runtime-only preprocessor branches
4. vertex-stage contract
5. sampler/filter/addressing differences
