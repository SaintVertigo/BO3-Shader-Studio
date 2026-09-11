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


## Phase 1f visual regression finding

The Phase 1e three-stage diagnostic isolated the remaining mismatch: `Input Albedo (t0)` showed the loaded color source, `Albedo` showed the material/GBuffer path, while `Final Lit` projected the HDR sky too sharply across the sphere. This confirms texture decode/binding is no longer the primary failure. Phase 1f therefore scopes its changes to the APE Match lighting compositor and keeps Look Dev unchanged.

## Phase 1g — processed probe lighting model

Phase 1g replaces the Phase 1f cone-sampling approximation with a renderer structure that follows the source-derived APE/ToolsGfx evidence more closely.

### Source-derived behavior now represented

The shipped ToolsGfx constant structures expose probe data independently from ordinary sun/display state. In particular, the scene/global-probe path carries a probe exposure and average probe color, and reflection-probe records also carry exposure/average-cube data. APE's render-stage strings and techset definitions expose separate diffuse-probe, reflection-probe, light-culling, and deferred-lighting stages.

The Studio therefore no longer treats the visible HDR sky as the material's diffuse lighting source. When an APE HDR environment is loaded it now:

- projects the linear HDR source into 9 real spherical-harmonic coefficients;
- convolves those coefficients with the Lambert cosine kernel on the CPU, producing low-frequency diffuse irradiance;
- preserves the HDR source as a mipmapped floating-point texture;
- samples progressively filtered mips for roughness-dependent specular-probe approximation;
- keeps direct sun irradiance, diffuse-probe energy, specular-probe energy, probe exposure, and final display exposure as separate quantities;
- uses the environment average color as a mild probe-chroma adaptation term rather than directly tinting the material with the background image;
- retains a conservative global-probe/bounce floor because `assetviewer.led` was baked with local probes and the four default SSI records use `bounceCount = 4`.

### Still calibration, not claimed Treyarch constants

The exact numeric relationship between APE's baked probe exposure, `avgCubeColor`, `sun.intensity`, and the final Tonemap LUT has not yet been recovered. Phase 1g therefore keeps four clearly isolated per-preset calibration values for diffuse probe scale, specular probe scale, sun irradiance scale, and probe exposure. The original SSI values remain unchanged and are not rewritten to make screenshots match.

The final display curve is also still the Studio's temporary APE-match curve; the actual ToolsGfx Tonemap LUT remains a future parity target.

### Camera reset parity

The ordinary Preview Reset action now restores APE Match's calibrated material camera pitch instead of silently returning to the generic Studio 0-degree camera. This makes APE-vs-Studio lighting screenshots repeatable.


## Phase 1h — APE viewport controls + stock gloss decode correction

The supplied APE material-preview video and strings/xrefs in `asseteditor_modtools.exe` add two useful parity findings.

### Camera/control behavior

APE contains explicit camera-mode names `MayaCamMode` and `MotionBuilderCamMode`, plus the action labels `Orbit`, `Rotate`, `Drag`, `Zoom`, and `Pan`. The material-preview video shows the important rendering invariant regardless of which convenience binding is used: camera navigation orbits/dollies the view while the selected lighting preset remains fixed in world space.

Shader Studio APE Match therefore now treats the selected SSI lighting preset as immutable viewport state during camera navigation. It accepts Maya-style bindings (`Alt+LMB` orbit, `Alt+MMB` pan, `Alt+RMB` dolly) while retaining plain mouse aliases for existing Studio users. Right-drag no longer rotates the fake Studio sun in APE Match.

The executable also exposes the APE lighting menu/actions `No Lighting`, `LightingMorning`, `LightingDay`, `LightingSunset`, `LightingNight`, `Draw Skybox`, `PBR Texture Check`, and `Luminance Texture Check`.

### Deferred-stage structure

`asseteditor_modtools.exe` names the shader `ToolsGfx/deferred_lighting.hlsl` and exposes the compile/debug permutations:

```text
LIGHTING_ONLY
GI_SPECULAR_ONLY
GI_DIFFUSE_ONLY
LIGHTING_AND_GI_SPECULAR_ONLY
LIGHTING_AND_GI_DIFFUSE_ONLY
```

This reinforces the current APE Match architecture: direct sun, diffuse GI/probes, and specular GI/probes should remain separate terms instead of being collapsed into one sampled environment color.

### Stock gloss packing was not linear

The stock ToolsGfx source proves that `NormalGloss.z` is produced by:

```hlsl
GBuffer_PackGloss(gloss, normal.w)
```

where:

```hlsl
max((log2(exp2(saturate(gloss / 17) * -17) + normalHeight) * (-1 / 17)), 0)
    * 0.49755621 + 0.00146627566
```

For the identity normal fallback used by stock `Geometry/lit`, `GBuffer_DecodeNormal(...).w` is `1/3`. The supplied APE capture visibly shows the tested material using `Gloss Range 0 .. 13`.

The previous Studio compositor incorrectly decoded `NormalGloss.z` as if it were a linearly normalized gloss value. For stock gloss 13 this turned the material into an almost fully rough surface, suppressing the characteristic APE sun highlight. Phase 1h now inverts BO3's logarithmic pack for the stock identity-normal case and updates the preview fallback GBuffer writer to use BO3's real pack formula.

### Reset semantics

APE lighting selection and camera framing are now separated. Changing Morning/Day/Sunset/Night preserves the current camera orbit. An explicit Preview Reset restores the entire selected APE preset: reference camera, SSI sun direction/color, environment rotation/source, probe calibration, exposure, and shadow state. This removes stale user-light state from parity captures.
