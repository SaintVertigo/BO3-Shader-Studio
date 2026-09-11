# Phase 1p - Captured Normal/Gloss + Native Cube Probe Rewrite

Phase 1p is a renderer correction built from the 3DMigoto APE captures rather than another visual calibration pass.

## Capture finding that changes the Studio path

The captured stock APE normal texture used by `core_script_wall_c` is an 8x8 neutral texture with RGBA approximately:

```
128, 128, 0, 255
```

BO3 does **not** use the blue channel as tangent-normal Z. R/G hold tangent XY, tangent Z is reconstructed from XY, and B is the separate normal-height term that is folded into `GBuffer_PackGloss`.

Shader Studio's old fallback neutral normal was `(128,128,255)`. That accidentally injected a large height term into the logarithmic gloss packer, so the GBuffer value did not match APE even before deferred lighting ran.

Phase 1p changes the fallback to BO3's captured convention and reconstructs normal Z from R/G.

## Captured deferred gloss / probe behavior

APE's deferred shader `2f9c1c21e9bef37c` normalizes packed `NormalGloss.z` directly:

```
lightingGloss = saturate((packedGloss - 0.00146627566) * 2.00982332)
```

For stock `core_script_wall_c` with Gloss Range `0 -> 13` and APE's zero-height neutral normal, the captured GBuffer contains `NormalGloss.z ~= 0.3822`, which produces:

```
lightingGloss ~= 13 / 17 ~= 0.7647
probe LOD     = 5 * (1 - lightingGloss) ~= 1.1765
```

The direct-light microfacet width follows the captured mapping:

```
cosinePower = 2^(17 * lightingGloss)
alpha^2     = 2 / (cosinePower + 2)
```

The captured `gEnvBRDFGeneric` lookup uses `(NdotV, lightingGloss)` at texel-center corrected coordinates. Phase 1o incorrectly used microfacet alpha as the LUT Y coordinate.

The capture also exposes the final dielectric split-sum combine. For stock reflectance 0.04, APE combines the two EnvBRDF branches as:

```
specular = 0.96 * branchA + 0.04 * branchB
```

Phase 1o had this effectively reversed as `0.04 * A + B`. On the captured LUT that can turn an intended small dielectric reflection into a value near 1.0, which is the direct reason the Phase 1o sphere looked like a dark photographic mirror. Phase 1p uses `(1 - F0) * A + F0 * B`.

## Native reflection-probe resource

APE's t51 resource is a real 256x256 cube/cube-array probe with six faces and seven mips. Phase 1o instead sampled a lat-long `Texture2D` and generated ordinary mips, which left readable clouds painted across the wall.

Phase 1p replaces that path with an actual D3D11 `TextureCube`:

- 256x256 per face;
- six faces;
- seven mips;
- RGBA16F HDR storage;
- immutable authored mip chain;
- directional prefiltering for mip levels;
- no `GenerateMips()` call for the APE reflection probe.

The visible HDR sky and baked glossy probe remain independent, matching the horizontal/vertical APE captures: manual horizontal lighting can yaw the visible sky while the material probe remains at its preset orientation.

## Expected visible result

Compared with Phase 1o/1o.1, the stock wall should stop behaving like a dark photographic mirror. The reflection still has directional environment information, but its appearance now comes from a real cube-probe mip chain and the captured EnvBRDF coordinates. The direct light remains a separate contribution.

## Remaining parity limits

3DMigoto exposed only one subresource of APE's shipped BC6H cube in the resource dump, so Studio cannot simply import the complete original six-face/seven-mip probe. Phase 1p reconstructs the missing cube from APE's HDR environment and the captured resource contract. The preview shadow path also still uses one dynamic 1024x1024 map rather than the complete three-layer APE sun-shadow array. Those limitations are left explicit rather than hidden behind more hand-tuned reflection constants.


---

# APE Match Phase 1o.1 - D3D11 Shadow CBuffer Slot Hotfix

- Fixes DirectX initialization error `X4567: maximum cbuffer exceeded` caused by the Phase 1o shadow VS using invalid D3D11 slot b14.
- The standalone APE shadow matrix buffer now uses valid slot b0 in both HLSL and `VSSetConstantBuffers`.
- Phase 1o captured APE lighting/shadow/probe/tonemap behavior is otherwise unchanged.

# APE Match Phase 1h — Viewport Parity + BO3 Gloss Fix

- APE Match camera navigation now keeps the recovered SSI sun fixed in world space; RMB no longer rotates the fake Studio light while in strict APE mode.
- Added APE/Maya-style material viewport bindings: Alt+LMB orbit, Alt+MMB pan, Alt+RMB dolly, while retaining plain mouse aliases and wheel dolly for convenience.
- Preview Reset, R, and double-click now restore the full selected APE preset (camera, sun, environment rotation/source, probe calibration, exposure, and shadow state), not just camera rotation.
- Switching Morning / Day / Sunset / Night now preserves the current orbit instead of unexpectedly re-framing the asset.
- Fixed a major BO3 GBuffer parity error: `NormalGloss.z` is logarithmically packed together with normal-height data, not a linear normalized gloss channel. Stock Geometry/lit gloss 13 now decodes as gloss 13 instead of ~1.6.
- Updated the built-in fallback GBuffer writer to use BO3's real `GBuffer_PackGloss` formula.
- Removed an accidental duplicate scope in the external APE deferred-light HLSL.
- APE Match clears any leftover temporal exposure history when a strict lighting preset is applied; material exposure remains fixed while orbiting.
- Documented the APE executable's `ToolsGfx/deferred_lighting.hlsl` permutations (`LIGHTING_ONLY`, `GI_SPECULAR_ONLY`, `GI_DIFFUSE_ONLY`, and combined variants) and recovered camera-mode/action strings.

# APE Match Phase 1g.3 — MSVC Shader Resource Fix

- Fixes persistent MSVC `C2026: string too big, trailing characters truncated` in `preview_renderer.cpp`.
- Moves the ~16.6 KiB APE deferred-light HLSL out of C++ string literals and into the Qt resource bundle (`:/preview/ape_deferred_lighting.hlsl`).
- Runtime shader source is byte-for-byte the same HLSL assembled by Phase 1g.2; APE lighting math/calibration is unchanged.
- Adds explicit startup errors if the embedded shader resource is missing or empty.

# APE Match Phase 1f — Probe Lighting Pass 1

- Keeps all Phase 1d native APE preview mesh/XMODEL_BIN support and Phase 1e color-space/material diagnostics in one cumulative source tree.
- APE Match diffuse environment lighting is now low-frequency/probe-like instead of sampling the raw HDR lat-long directly at the surface normal.
- APE Match specular environment lighting now uses a roughness-controlled cone convolution and lower probe energy instead of a sharp mirror copy of the sky.
- Look Dev remains on the previous artist-friendly environment response.
- This is intentionally labeled a first probe approximation; it is designed to remove the known chrome-ball failure before deeper ToolsGfx probe parity work.

# BO3 Shader Studio - Automatic Tester Releases

This patch is based on BO3 Shader Studio 0.1.

## Changes

- Every push to `main` automatically builds, tests, packages, and publishes a Tester prerelease.
- Manual `workflow_dispatch` remains available for intentional Stable releases or version/note overrides.
- Automatic push releases read the clean visible version from `version.json` -> `displayVersion`.
- Internal GitHub run versions remain monotonic for updater ordering but are hidden from the normal update dialog.
- New releases only publish the clean assets:
  - `BO3_Shader_Studio.zip`
  - `BO3_Shader_Studio.zip.sha256`
  - `BO3_Shader_Studio_Update.zip`
  - `BO3_Shader_Studio_Update.zip.sha256`
- The temporary `BO3_HLSL_Previewer_Update_*` bridge alias is no longer generated on future releases.
- Release packaging starts from a clean `release_artifacts` directory to prevent stale legacy assets from leaking into new releases.
- Update dialogs now show the visible BO3 Shader Studio version instead of the hidden internal ordering version.
- When the visible version is unchanged, the dialog says a newer build is available rather than showing internal version numbers.
- Documentation now explains the push-to-main tester workflow and manual Stable flow.

The original BO3 Shader Studio 0.1 bridge release should remain on GitHub so users still on the old pre-rename updater can migrate.

## APE Match Phase 1d
- Added BO3 `XMODEL_BIN` preview-model import for the static Mod Tools model format.
- APE Match / Neutral now use the user's local Treyarch APE sphere, cube, and plane geometry when available, preserving authored APE UVs and seams.
- Added `.XMODEL_BIN` to Load Model and drag/drop preview-model support.
- No Treyarch preview models are redistributed; missing local assets fall back to Studio primitives.

## APE Match Phase 1e
- Matched the stock `Geometry/lit` color-only material contract used by APE parity testing.
- Material texture uploads are now color-space aware: Albedo/Specular/Emissive use sRGB SRVs; Normal/Height/Gloss/AO/Opacity remain linear.
- Material textures now generate mipmaps; the color sampler uses tiled 2x anisotropic filtering to match the stock `lit` color-map defaults more closely.
- Stock color-only material fallbacks now use identity normal, ~0.04 dielectric reflectance, AO 1, and gloss 13/17.
- Added explicit linear-to-display conversion for APE Match / Neutral output on the UNORM preview swapchain.
- Added `Input Albedo (t0)` to isolate image decode/binding from mesh UV and GBuffer issues.
- Material Textures now reports decoded dimensions and whether each preview resource is sRGB or linear.
- Added source-derived APE reverse-engineering notes separating confirmed behavior from remaining approximations.

## APE Match Phase 1g
- Replaced APE Match's broad cone-sampled environment lighting with CPU-generated Lambert-convolved SH9 diffuse probe lighting.
- APE HDR environment textures now keep a GPU mip chain for roughness-dependent reflection-probe filtering.
- Separated APE probe exposure, diffuse-probe scale, specular-probe scale, sun irradiance, and display exposure so a bright sky no longer masks an under-lit material.
- Added a conservative four-bounce/local-probe fill approximation based on the recovered APE `assetviewer.led` / SSI setup.
- Added mild average-probe chroma adaptation so Day lighting does not cast the raw blue/green HDR sky directly onto diffuse materials.
- Corrected Preview Reset so APE Match restores its calibrated 26.7-degree reference camera pitch.
- APE preset diagnostics now state whether the native Treyarch preview mesh or the Studio fallback mesh is active.
- No Treyarch HDR/model assets are bundled; APE Match continues to read them from the user's own BO3 Mod Tools install.

## APE Match Phase 1g.1 — MSVC Build Hotfix
- Fixed Windows/MSVC `C2026: string too big, trailing characters truncated` in `preview_renderer.cpp` after the Phase 1g deferred-lighting shader exceeded MSVC's per-literal limit.
- The embedded deferred-light HLSL is now assembled at runtime from two smaller raw-string chunks; shader text and rendering behavior are unchanged.
- No APE lighting constants or probe math were changed by this hotfix.

## APE Match Phase 1g.2 — Definitive MSVC String-Literal Fix
- GitHub build logs showed Phase 1g.1 still failed with `C2026` in the embedded deferred-light shader.
- Reworked the shader embedding again: the HLSL is now appended in six independent raw-string statements inside a static initializer lambda.
- Every deferred-light source literal is under 3 KB, well below MSVC's per-literal ceiling, and the reconstructed HLSL byte-for-byte matches the Phase 1g shader text.
- Rendering math, SSI values, probe calibration, and APE lighting behavior are unchanged.
