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
