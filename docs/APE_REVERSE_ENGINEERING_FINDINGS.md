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

## Phase 1j — native HDR sky detail + stock specular-lobe correction

The supplied APE/Shader Studio comparison video isolated two independent parity
problems that Phase 1h/1i still left visible.

### Visible APE sky resolution

The shipped Day and Sunset lat-long sources are 8192x4096 EXRs.  The Studio was
decoding those full files for statistics, then deliberately shrinking the GPU
copy to 2048x1024 RGBA32F.  That discarded 75% of the samples in each axis and
made distant foliage, cloud edges, and rock detail visibly softer than APE.

Phase 1j keeps up to the authored 8192x4096 source resolution and uploads the APE
environment as `R16G16B16A16_FLOAT`, which preserves HDR range while halving the
per-texel storage versus RGBA32F.  It keeps mip generation for reflection-probe
filtering, but the visible sky explicitly samples mip 0.  An adapter that cannot
allocate the native image falls back to 4096x2048 first, then 2048x1024 as a last
safety net.  Reconstructed Morning/Night cube-face skies now target 4096x2048.
The equirectangular sampler also wraps U and clamps V so filtering crosses the
horizontal seam correctly without wrapping across the poles.

### Gloss is not reflectivity

The comparison video shows `core_script_wall_c` staying predominantly neutral
and diffuse in APE while carrying a small, compact white sun highlight.  The
Studio instead painted recognizable sky features across most of the sphere.
That demonstrated that the remaining mismatch was no longer the Phase 1h gloss
*decode*; it was how the decoded gloss was converted into a modern roughness and
how much raw environment-probe energy was applied.

BO3 exposes the authored texture slot as `cosinePowerMap` and uses a 0..17 gloss
range.  Phase 1j therefore stops using the arbitrary linear `roughness = 1-gloss`
conversion.  The preview approximation maps the decoded 0..17 value to a
cosine-power lobe (`2^gloss`) and converts that lobe width to the GGX roughness
used by the Studio compositor.  For stock gloss 13 this produces a compact
highlight instead of a broad lobe.  This mapping is still an APE-parity
approximation until the exact shipped `ToolsGfx/deferred_lighting.hlsl` BRDF is
recovered; the verified BO3 GBuffer packing/decoding itself is unchanged.

The APE preset specular-probe calibration is also reduced substantially and the
roughness-to-probe-mip mapping is biased toward filtered probe mips.  This keeps
Fresnel/environment response at grazing angles without turning stock dielectric
materials into chrome.  Diffuse SH chroma normalization is slightly stronger so
the HDR sky does not blue/green-color-cast neutral stock materials as strongly.

### Forward/deferred sky consistency

The forward-material sky path previously used implicit mip selection and omitted
the final APE desktop sRGB transfer that the deferred APE compositor already
applied.  Phase 1j forces base-mip presentation and applies the same display
transfer, so switching material preview paths no longer changes sky sharpness or
contrast for the same preset.


## Phase 1k — decouple direct gloss from reflection-probe sharpness

The post-Phase-1j APE/Studio recording provided a stronger visual invariant than
the earlier still captures. Rotating the light in APE moves a very small, bright
direct specular highlight across `t7_script_wall`, but the environment response
never becomes a readable mirror of the sky. Shader Studio still showed clouds and
terrain sharply across most of the sphere.

That proves the remaining mismatch is not the BO3 gloss pack/decode. The same
authored gloss participates in a compact direct-light lobe while APE's processed
reflection probes remain much more filtered. Phase 1k therefore decouples those
operations in APE Match only:

- direct sun keeps the corrected gloss-13 compact lobe;
- stock dielectric reflectance (0.04) gets a high minimum reflection-probe mip;
- explicit high-reflectance / metal-like materials can still use sharper probe
  mips;
- the sampled HDR probe is softly luminance-compressed to remove residual sun
  spikes that are absent from APE's processed local probes;
- stock dielectric probe color is blended toward the recovered average cube
  color, retaining the broad horizon tint without projecting recognizable sky
  detail onto the material.

This remains a parity approximation until Treyarch's exact ToolsGfx reflection-
probe convolution and deferred BRDF are recovered, but it is constrained by the
new APE video rather than by a generic PBR assumption.


## Phase 1l — source-structured sun/probe/display correction

The latest APE comparison recording exposed three structural errors that could be
traced against the recovered BO3/ToolsGfx source headers instead of tuned as one
combined lighting number.

### Sun diffuse and sun specular are separate engine state

`gfxcore/hlslcoredefines.h` defines `CoreSunConstants` with independent
`color`, `specScale`, `globalProbeExposure`, `avgGlobalProbeColor`, and
`intensity` fields. This supports the APE recording: a stock dielectric can carry
a tiny, intense direct-sun highlight without making the diffuse term or the
reflection probe equally mirror-like.

The recovered Geometry/lit material also authors gloss through `cosinePowerMap`
and the 0..17 gloss range. APE Match therefore no longer converts that legacy
direct-light gloss into GGX. The direct sun path evaluates a normalized
cosine-power/Blinn lobe with exponent `2^gloss`. Gloss 13 consequently produces a
very narrow highlight, while Studio Look Dev deliberately retains its modern GGX
renderer.

The half-vector path is now guarded before normalization. This removes the moving
black circular defect seen when `L + V` approached zero in the comparison video.

### Reflection-probe average color is metadata, not the directional probe

`CoreReflectionProbePack` stores `exposure` and `avgCubeColor` separately from the
probe transform/blend record, while `CoreSunConstants` separately carries global
probe exposure/average color. Phase 1k overused the recovered average color as a
replacement for the directional filtered environment and erased the broad
blue-gray view dependence visible in APE.

Phase 1l keeps filtered directional probe mips dominant, uses the average color
only as a small stabilization term, retains a moderate dielectric blur, and
preserves Schlick grazing Fresnel. This is still an approximation of the missing
`ToolsGfx/diffuseprobe_compute.hlsl` / reflection convolution implementation, but
its data flow now matches the recovered source structures.

### ToolsGfx exposure ordering and the unresolved Tonemap LUT

`lib/hdrold.hlsl::HDR_ClampExposure()` shows the TOOLSGFX path multiplying HDR
color by `gScene.invExposure` before the later presentation stage. The exact
`ToolsGfx/tonemap_lut.hlsl` LUT payload is not present in the recovered source
bundle. Accordingly Phase 1l does not claim an exact Treyarch tone curve; it
replaces the temporary ACES approximation with one isolated, neutral
luminance-preserving shoulder calibrated against the supplied APE capture. The
new transfer lowers highlight contrast, preserves more shadow detail, and reduces
the excessive green/yellow saturation seen in the Studio background without
changing the HDR source itself.

The visible native-resolution sky also returns to derivative-driven trilinear LOD
selection. Native 8K/4K source resolution and seam-safe wrap/clamp sampling remain
intact, while the overly crisp forced-mip-0 presentation from Phase 1j is removed.

## Phase 1m — APE light-rig motion correction

A closer APE capture corrected an important interaction assumption from Phase 1h.
Camera orbit and the selected lighting preset are independent, but APE's explicit
light manipulator is not a sun-only control: dragging the light rotates the HDR
environment with the sun. Vertical light motion is also not clamped to a
latitude. The rig may pass over either pole repeatedly, which is why APE can move
the light above/below the sphere through multiple complete revolutions.

Shader Studio previously clamped `lightPitchDegrees_` to +/-89 degrees and stored
only one environment yaw angle. That made Shift+LMB stick at a pole and left the
background stationary while the sun moved. Phase 1m changes the APE Match rig to:

- wrap light pitch continuously through +/-180 degrees instead of clamping;
- apply the inverse manual rig delta to the environment sampling transform, so the
  visible HDR, diffuse SH lookup, and reflection lookup rotate with the sun;
- carry an environment pitch in `previewApeSettings.w` in addition to the existing
  environment yaw;
- make the Scene / Lighting yaw/pitch sliders drive the same coupled APE rig;
- restore the stock preset's absolute environment orientation after installing its
  SSI sun direction, so switching/resetting a preset remains deterministic.

The new close-ups also establish that the moving dark patch visible on the stock
APE sphere is real viewport output, not the mouse cursor. It follows the segmented
APE preview-sphere shading/environment response and must remain a parity reference.
The half-vector singularity guard stays for numerical robustness, but it is no
longer described as the source of that APE patch and should not be tuned to erase
it.


## Phase 1n — split APE sun, visible sky, and baked probe orientation

A denser APE recording supersedes the Phase 1m assumption that the manual light
manipulator rotates one rigid sun/environment rig. APE exposes three distinct
orientation behaviors:

- the sun direction accepts both horizontal yaw and continuous over/under pitch;
- the visible Day/Morning/Sunset/Night sky follows the horizontal light motion
  only and remains vertically upright while the sun passes over either pole;
- the glossy/reflection response on the material remains at the preset/baked
  probe orientation instead of following the manual sun/sky yaw. The diffuse
  probe is kept on that same fixed orientation, matching BO3's independent
  global/reflection-probe state in the recovered shader structures.

Phase 1n therefore removes the Phase 1m environment-pitch transform and splits
background sampling from material-probe sampling. `previewApeSettings.x` is the
visible-sky yaw and `previewApeSettings.w` is the fixed baked-probe yaw. Applying
an APE preset initializes both to the recovered preset environment yaw; subsequent
manual light yaw changes only the visible-sky yaw, while light pitch changes only
the sun direction. Camera orbit remains independent of all three.

This split also preserves an important APE reference artifact: the dark
low-frequency patch visible in the glossy sphere can remain spatially tied to the
baked probe even when the direct sun moves onto that side of the sphere. It must
not be erased merely because it overlaps the directly lit hemisphere.

This is a behavior-level correction derived from the supplied APE recording and
the recovered BO3 separation between sun constants and probe data. Exact pixel
identity still requires the missing shipped ToolsGfx deferred-lighting BRDF,
probe-convolution implementation/data, and presentation Tonemap LUT.
