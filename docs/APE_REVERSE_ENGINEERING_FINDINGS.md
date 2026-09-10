# APE reverse-engineering findings — material viewport

This document records source-derived facts used by Shader Studio's APE Match implementation. It deliberately separates verified Treyarch behavior from screenshot calibration.

## Verified material test case: `script_wall`

From the shipped GDT database:

```text
material:       script_wall
category:       Geometry
materialType:   lit
colorMap:       core_script_wall_c
colorTint:      1 1 1 1
normalMap:      <none>
normalHeight:   1.0
glossRange:     0 .. 13
glossMap:       <none>
specColorMap:   <none>
occMap:         <none>
tileColor:      tile both*
filterColor:    aniso2x (mip linear)
```

The image record for `core_script_wall_c` identifies it as:

```text
semantic:          diffuseMap
coreSemantic:      sRGB3chAlpha
compressionMethod: compressed no alpha
mipBase:           1/1
mipMode:           Average
source:            art_assets\t6_legacy\texture_assets\core\core_script_wall_c.tif
```

## Verified ToolsGfx `Geometry/lit` path

`geometry/lit.techsetdef` selects a deferred opaque `gbuffer` technique with `BASE_TEXTURES` and `USE_COLOR_TINT`.

For `BASE_TEXTURES`, `ToolsGfx/gbuffer_common.hlsl` establishes these defaults:

- albedo = sampled `colorMap`, with alpha-weighted color tint, then alpha forced to 1;
- normal = `normalMap` with `$identitynormalmap` fallback;
- gloss = `glossRange.y` (13 for the stock material above);
- reflectance = fixed dielectric 0.04;
- occlusion = 1.

This is different from treating every material as if it always had free-standing specular, gloss, and AO textures.

## Verified APE/ToolsGfx render-stage evidence

Strings and assertions in `asseteditor_modtools.exe` identify a real-lighting pipeline containing:

```text
Prepass
GBuffer Opaque
GBuffer Semi Opaque
GBuffer Decal
SSAO
Light Culling
Emissive Opaque
Lit Forward Opaque
Deferred Lighting
Transparent
Volumetrics
```

The executable also contains separate deferred-lighting/GI diagnostic variants:

```text
LIGHTING_ONLY
GI_SPECULAR_ONLY
GI_DIFFUSE_ONLY
LIGHTING_AND_GI_SPECULAR_ONLY
LIGHTING_AND_GI_DIFFUSE_ONLY
```

and references `ToolsGfx/diffuseprobe_compute.hlsl` and `ToolsGfx/tonemap_lut.hlsl`.

The executable has explicit UI/enum paths for `LightingNone`, `LightingMorning`, `LightingDay`, `LightingSunset`, and `LightingNight`, and separate assertions for real vs unlit scene modes. Therefore APE No Lighting should remain a distinct preview path.

## Still not verified exactly

The following are still parity targets rather than claimed Treyarch constants:

- exact probe convolution kernel and roughness-to-probe-LOD mapping;
- exact deferred BRDF implementation used by the shipped ToolsGfx binary;
- exact display/tonemap LUT contents and EV adaptation behavior;
- exact camera/environment orientation constants beyond the recovered source SSI values;
- how APE maps every material type's optional resources beyond the stock `Geometry/lit` case.
