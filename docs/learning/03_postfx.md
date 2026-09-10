# BO3 PostFX — A Complete First Shader

**Level: Beginner → Intermediate**

Open **File → New Example → BO3 PostFX Shader**.

The bundled sample is intentionally small but uses real BO3 PostFX conventions.

## 1. Include BO3 PostFX helpers

```hlsl
#include "postfx/postfx_common.h"
```

This provides helpers used by BO3's PostFX path, including time, render-target information, fullscreen-quad generation, and PostFX color conversion.

## 2. Declare resources

```hlsl
Texture2D<float4> frameBuffer : register(t0);
Texture2D<float4> DepthSampler : register(t1);
SamplerState frameBufferSampler : register(s0);
SamplerState DepthSamplerState : register(s1);
```

The register says where the compiled shader expects the resource. The **name** is what the techset later binds.

## 3. Build the fullscreen vertex stage

```hlsl
PS_INPUT vs_main(const VS_INPUT vertex, const uint instance : INSTANCE_SEMANTIC)
{
    PS_INPUT output;
    PostFx_GenerateFullscreenQuad(
        vertex.position,
        vertex.texcoord,
        instance,
        output.position,
        output.texcoord);
    return output;
}
```

This gives the pixel shader normalized fullscreen UVs.

## 4. Read time and size from BO3

```hlsl
float t = GetTime();
float2 invSize = PostFx_GetRenderTargetSize().zw;
```

Avoid inventing Previewer-only globals when BO3 already has a real helper for the value you need.

## 5. Normalize the BO3 source color

```hlsl
float3 color = PostFx_NormalizeColor(
    frameBuffer.Sample(frameBufferSampler, uv).rgb);
```

This step matters for runtime parity. BO3's runtime PostFX source is not always equivalent to treating a normal display image as ordinary 0..1 RGB.

Do your effect math on the normalized color.

## 6. Convert back before returning

```hlsl
return float4(PostFx_DenormalizeColor(color), 1.0);
```

For the bundled BO3 runtime path, this normalize/denormalize bridge is part of the working parity contract.

## 7. Depth is separate

```hlsl
float depth = DepthSampler.Sample(DepthSamplerState, uv).r;
```

The companion techset binds this name to BO3's `floatZ` CodeTexture in runtime mode.

## Try this

Change the vignette strength in the sample, compile, and watch the Preview update. Then open the companion `.techsetdef` and continue to **Techsets 101**.

## 8. Enable the exported PostFX from your usermap/mod CSC

The exporter no longer creates a shader-specific auto-start CSC or `.zpkg`. Real BO3 testing showed that the standalone auto-start package path could fail to run even when the shader/material itself was valid.

Every PostFX export writes the complete activation instructions directly into the package's main `00_README_FIRST.txt` (or the direct-install `*_INSTALL_README.txt`). It also creates a duplicate backup copy at:

```text
source_data/<namespace>/<base>_POSTFX_INTEGRATION.txt
```

Merge the generated code into the **client CSC that already runs for your usermap or mod**. For a normal usermap, this is the map `.csc` that calls `zm_usermap::main()`. For a mod, use the client `.csc` that owns your local-player initialization/spawn callback.

The generated instructions add/merge these imports:

```c
#using scripts\shared\callbacks_shared;
#insert scripts\postfx\_filters.gsh;
#using scripts\postfx\_filters;
```

Register the local-player callback from `main()` if your CSC does not already do so:

```c
callback::on_localplayer_spawned( &on_player_spawned );
```

Then merge the shader-specific thread call into your existing `on_player_spawned(localClientNum)` or use the generated example. The generated function names come from the export **Base name**, while the pass uses the exact **Material** name selected in the exporter. The final enable call uses `filters::enable_filter_persistent()` and reserved filter slot 6, so temporary stock/gameplay PostFX on slot 0 can run without clearing the Studio filter.

Add only these entries to the usermap/mod zone:

```text
include,filters
material,<exported material name>
```

Do **not** add `include,<generated_name>_autostart`; no shader-specific `.zpkg` is generated anymore.

**Include shared _filters support files (first install only)** remains available when the project does not already have `_filters.csc`, `_filters.gsh`, and the shared `filters.zpkg`. Leave it off when you already have a working/customized copy.

**Create looping postfxbundle** remains available as a separate manual workflow and is not used for startup.

## Shadertoy scene orientation

Converted Shadertoy PostFX keeps Shadertoy's lower-left `fragCoord` convention, while BO3/D3D scene textures use an upper-left texture origin. Most converted effects therefore need one Y conversion when sampling a channel mapped to `resolvedScene`.

Some shaders already invert the scene UV themselves (for example `uv.y = 1.0 - uv.y` before sampling `iChannel0`). Flipping those again turns the preview/game scene upside down. The converter's **Scene orientation** control has three modes:

- **Auto Detect** — looks for an authored Y inversion on the `iChannel0` scene sample.
- **Shadertoy Lower-Left (Flip Scene Y)** — force the BO3 resolvedScene boundary flip.
- **Already Upper-Left (Keep Scene Y)** — preserve the shader's authored scene UV and do not add another flip.

The generated HLSL stores the decision in `BO3_PREVIEWER_POSTFX_SCENE_ORIENTATION`, so Preview and BO3 export use the same orientation. Auxiliary Shadertoy image/noise channels are not affected by the resolvedScene orientation bridge.
