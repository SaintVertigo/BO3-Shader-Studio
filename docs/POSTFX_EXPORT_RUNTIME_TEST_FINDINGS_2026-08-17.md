# BO3 PostFX Export Runtime Findings — 2026-08-17

This document records the PostFX export/link/runtime failures that were reproduced while taking the converted Shadertoy paint shader `sv_paint` from BO3 HLSL Previewer into Black Ops III Mod Tools and then into the game.

The important distinction is that these were **real BO3 parser/linker/APE/runtime observations**, not conclusions from standalone FXC compilation alone.

## Final known-good runtime contract

For the tested paint shader:

- `iChannel0` -> `CodeTexture("resolvedScene")`
- `iChannel1` -> the authored 256x256 Shadertoy noise/material image
- `iChannel2` -> `CodeTexture("resolvedScene")`
- Shadertoy procedural `fragCoord` keeps its lower-left conversion.
- Only BO3 `resolvedScene` sampling UVs get the D3D Y-orientation bridge.
- `resolvedScene` samples are passed through `PostFx_NormalizeColor` before ordinary Shadertoy color math.
- Final shader RGB is passed through `PostFx_DenormalizeColor` before output.
- `textureSize()`/`Res0` for a `resolvedScene`-backed channel uses the PostFX render-target dimensions, not a scalar-splatted width.
- The filter is enabled on the local player after `callback::on_localplayer_spawned`, with the tested short readiness delay.

The final manually corrected package rendered correctly in BO3 with the intended orientation, brightness and framing.

## 1. Sampler filter value must be quoted

BO3 error:

```text
ERROR: sv_sv_paint.techsetdef(...): Field name has to start with alpha character '(mip'
```

Invalid generated techset:

```text
filter = linear (mip none)
```

Working form:

```text
filter = "linear (mip none)"
```

The techset writer and validator must preserve/enforce the quoted display string.

## 2. `$white_diffuse` is not a valid fallback for custom PostFX `2d` slots

BO3 error:

```text
Image '$white_diffuse' (...) has a mismatched usage ('diffuseMap' expected: [2d])
```

The tested PostFX texture parameters use:

```text
semantic = "2d"
```

The exporter therefore generates a real neutral image asset with `semantic = "2d"` and uses that as the `Image(<colorMapXX, fallback>)` fallback rather than `$white_diffuse`.

## 3. BO3 asset identifiers and every reference must use one lowercase-normalized name

APE/GDT rejected or broke references generated from names such as:

```text
i_sv_sv_paint_iChannel1
```

The working asset name was:

```text
i_sv_sv_paint_ichannel1
```

It was not sufficient to rename only the image asset: the material/GDT reference also had to use the normalized name. The exporter must normalize once at the BO3 asset-name boundary and reuse that exact string everywhere.

HLSL resource identifiers such as `iChannel1` remain unchanged.

## 4. `float1` always exposes `x`

BO3 error:

```text
Unknown field 'y' in 'float1'
```

Invalid:

```text
float1("exposureClamped")
{
    y = <cg00_y>
}
```

Working packed mapping:

```text
float1("exposureClamped")
{
    x = <cg00_y>
}
```

The left side is the `float1` parameter component and is always `x`. The packed backing register on the right may still be `_y`, `_z` or `_w`.

## 5. APE image compression value is `compressed`, not `compressed high color`

BO3 image conversion error:

```text
ERROR: mismatch Image Format 'compressed high color'
Could not convert image '..._neutral2d'
```

APE exposed `compressed` and `uncompressed` for the tested image. Changing the generated asset to:

```text
compressionMethod = compressed
```

allowed BO3 to convert the 4x4 neutral image successfully with BC7.

## 6. Custom `Texture()` parameters need `Tweak()` metadata to appear in APE

Without `Tweak()` blocks, the custom Material Type loaded but the Shadertoy channel image selectors did not appear in the material editor.

Working shape:

```text
Texture("iChannel1")
{
    image = Image(<colorMap03, i_example_neutral2d>)
    semantic = "2d"
    tweak = Tweak()
    {
        category = "Shader Textures"
        title = "iChannel1"
        sortindex = "2"
    }
}
```

## 7. Restart Mod Tools / APE after installing a new or changed techsetdef

When the techset was copied while Mod Tools was already open, APE showed:

```text
INVALID MATERIAL TYPE 'sv_sv_paint'
```

Restarting Mod Tools caused the custom material type and its parameters to register correctly. Export ZIP instructions must call this out explicitly.

## 8. Proven activation path uses `_filters` on the spawned local player

The earlier generated startup route could link without errors yet produce no visible PostFX. The working test used the LG-RZ-style `_filters` interface:

```text
include,filters
```

and:

```c
#using scripts\postfx\_filters;
#insert scripts\postfx\_filters.gsh;
```

The material was placed in a `Pass`, then a `Filter`, and enabled with:

```c
self filters::enable_filter("sv_paint");
```

## 9. `on_localplayer_spawned` callback accepts `localClientNum`

A zero-argument callback produced:

```text
function called with too many parameters
```

Working signature:

```c
function on_player_spawned(localClientNum)
{
    self thread activate_postfx();
}
```

## 10. Local-player spawn + readiness delay is the tested startup lifecycle

The working map CSC used:

```c
callback::on_localplayer_spawned(&on_player_spawned);

function on_player_spawned(localClientNum)
{
    self thread activate_postfx();
}

function activate_postfx()
{
    level endon("end_game");
    level endon("intermission");
    self endon("death");
    wait(3);
    self thread enable_sv_paint_filter();
}
```

A later runtime test showed that the standalone generated auto-start CSC/ZPKG path could still fail to run. The exporter now preserves this lifecycle as merge-ready code in `<base>_POSTFX_INTEGRATION.txt` for the usermap/mod client CSC instead of generating a shader-specific startup package.

## 11. Runtime techset must emit the detected `CodeTexture` scene bindings

The filter initially activated to a black screen because the generated runtime techset had an empty pixel-shader binding block.

For this shader the working runtime bindings were:

```text
ps = PixelShader()
{
    iChannel0 = CodeTexture("resolvedScene")
    iChannel2 = CodeTexture("resolvedScene")
}
```

`iChannel1` remained the authored material/noise image.

## 12. Flip only `resolvedScene` sampling, not auxiliary Shadertoy textures

After the scene bindings were added, the scene was visible but upside down.

The existing Shadertoy fragment-coordinate conversion was still correct for procedural math:

```hlsl
float2 fragCoord = float2(input.position.x, iResolution.y - input.position.y);
```

The additional correction belongs only at the BO3 scene-texture sampling boundary:

```hlsl
float2 sceneUV = float2(uv.x, 1.0 - uv.y);
```

The noise image `iChannel1` must not receive this scene-specific flip.

## 13. Preserve BO3 PostFX color-domain normalization in exported HLSL

Once the scene was oriented correctly, it was visibly too dark compared with the Previewer.

The working runtime behavior normalizes BO3 scene samples before Shadertoy math and denormalizes final output:

```hlsl
col.rgb = PostFx_NormalizeColor(col.rgb);
bg.rgb = PostFx_NormalizeColor(bg.rgb);
...
fragColor.rgb = PostFx_DenormalizeColor(fragColor.rgb);
```

The live Previewer adapter was already performing this; export must write the adapted runtime source rather than the raw editor-only source.

## 14. `resolvedScene` dimensions must use the PostFX render-target width and height

The last visual mismatch looked stretched/shrunken. The converted source had:

```hlsl
#define Res0 GLSL_VEC2_S((float)(GLSL_TEXTURE_SIZE(iChannel0, 0)))
```

For a runtime `resolvedScene` channel this could collapse to width/width rather than width/height. The working result used:

```hlsl
#define Res0 (iResolution.xy)
```

The adapter now collapses this converted scalar-splat texture-size pattern to the actual PostFX render-target dimensions for scene-backed channels only.

## 15. Corrected `_filters.csc` shipped by the optional support kit

The user-verified `_filters.csc` supplied during testing keeps the public LG-RZ-style filter interface while retaining two tested corrections:

- the incomplete `if(localPlayer.currentFilter)` statement is not present in `Filter.Enable()`;
- `Filter.Disable()` clears a pass with:

```c
SetFilterPassMaterial(localClientNum, index, pass.index, 0);
```

The exporter still credits LG-RZ / BlackOps3Shaders and can optionally package this shared `_filters` support for first install, but shader-specific startup is now manual CSC integration; no shader-specific autostart `.zpkg` is generated.

## Community references

- LG-RZ / BlackOps3Shaders — https://github.com/LG-RZ/BlackOps3Shaders
- olie304 / BO3-Shader-Research — https://github.com/olie304/BO3-Shader-Research


## 13. APE PostFX preview: stock sceneTexture contract + fresh shader identity

Follow-up APE testing on 2026-08-18 isolated two separate preview problems.

A known-good APE PostFX preview used the LG-RZ-style resource contract:

```hlsl
SamplerState bilinearClampler : register(s0);
Texture2D<float4> sceneTexture : register(t0);
#define iChannel0 sceneTexture
```

The matching TOOLSGFX techset exposes `Texture("sceneTexture")` on `colorMap00`, uses the stock `zm_zod_scene` image for the material preview, selects `vs_generic`, and keeps the TOOLSGFX `TEXCOORD1` fake-input bridge. The converted VHS shader then rendered correctly in APE over the stock scene.

APE also reproduced stale compiled shader behavior when a generated HLSL source filename was reused after its interface/content changed. The same test succeeded when the HLSL was written under a fresh source name. The exporter therefore content-addresses the TOOLSGFX HLSL filename with a short SHA-256 prefix so changed source receives a new APE shader identity. The content-addressed TOOLSGFX HLSL is kept at the `shaders_stable_toolsgfx` root rather than under `postfx/`: the BO3 package validator and runtime linker require shaders that include stock `postfx/...` headers to remain root-relative, while APE cache busting only requires the source filename itself to change.

The exporter also strips existing `.hlsl` suffixes before appending the final extension, preventing accidental names such as `shader.hlsl.hlsl`.
