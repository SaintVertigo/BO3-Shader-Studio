# APE Match — Phase 1

This pass starts the BO3 Shader Studio material-preview parity work with Treyarch's Asset Property Editor (APE). It is intentionally a renderer/validation foundation rather than a visual UI overhaul.

## Preview profiles

The Material Preview scene now has three explicit profiles:

- **APE Match** — applies the recovered APE SSI defaults and loads Treyarch's HDR environment sources from the user's own Black Ops III Mod Tools install.
- **Look Dev** — preserves Shader Studio's existing artist-friendly preview and manual lighting controls.
- **Neutral / No Lighting** — mirrors APE's diagnostic no-lighting intent: environment/direct/specular lighting is bypassed and the material is shown against the APE-style blue-gray clear color.

APE Match locks the ordinary Look Dev lighting/exposure controls so a preset cannot be accidentally changed while it is being used as a parity reference.

## Recovered APE default SSI data

The values below come from the shipped `source_data/ssi.gdt` definitions used by the APE asset-viewer environment.

| Preset | Sun sRGB | Pitch | Yaw | EV | EV Comp | EV Min | EV Max | Stops | Penumbra | Skybox |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Morning | 1.0000, 0.8941, 0.7411 | 165 | 263 | 13.5 | 0 | -32 | 31 | 11.29999785 | 1.0 | `skybox_default_day_clear_0700` |
| Day | 1.0000, 0.9764, 0.9490 | 125 | 150 | 15 | 0 | 1 | 16 | 14 | 1.5 | `skybox_default_day` |
| Sunset | 1.0000, 0.768151, 0.545725 | 158 | 300 | 12.5 | 0 | 8 | 12.5 | 11 | 1.5 | `skybox_default_sunset` |
| Night | 0.791298, 1.0000, 1.0000 | 130 | 140 | 6 | 2.5 | 3 | 3.5 | -2.2 | 1.5 | `skybox_default_night` |

All four recovered default SSI records use four bounces, dynamic shadows, sun enabled, and `spec_comp = 0`.

## Local HDR sources

Shader Studio does **not** redistribute Treyarch/Activision game assets. APE Match resolves the original environment files from the BO3 install root already saved by the exporter.

Expected source paths:

- Morning: `model_export/t7_skybox/skybox_default/skybox_default_day_clear_0700_{rt,lf,up,dn,ft,bk}.exr`
- Day: `model_export/t7_skybox/skybox_default/skybox_default_day_ll.exr`
- Sunset: `model_export/t7_skybox/skybox_default/skybox_default_sunset_ll.exr`
- Night: `model_export/t7_skybox/zm_factory/Temp/zm_skybox_factory_05_{rt,lf,up,dn,ft,bk}.exr`

Morning and Night are reconstructed from the six cube faces into an HDR equirectangular preview texture. Day and Sunset use their original lat-long EXRs.

If the BO3 root is not configured or an asset is missing, Shader Studio keeps the recovered SSI settings active but clearly reports that the local HDR source is unavailable. It does not silently bundle or substitute a proprietary asset.

## HDR pipeline change

OpenEXR environment textures now remain floating-point linear HDR when uploaded to Direct3D. The old path converted EXR skies to an 8-bit, pre-tone-mapped texture during load, which discarded the high-luminance range needed for APE-like reflections and specular response.

The 8192x4096 Day/Sunset lat-longs are reduced to a 2048x1024 floating-point preview texture before GPU upload. Lighting statistics are still measured from the full-resolution decoded source. This keeps HDR values intact while avoiding a roughly 512 MiB RGBA32F environment allocation.

## Phase-1 lighting model

The source-of-truth SSI values and the screenshot-calibration values are deliberately separate. The current first-pass renderer:

- applies the exact recovered SSI sun color and direction;
- preserves HDR sky values;
- uses a separate direct-light scale derived from the Stops/EV relationship;
- uses isolated preview exposure/ambient/shadow calibration values per preset;
- uses an ACES-style fitted display curve as a temporary APE Match display transform;
- preserves the Studio's existing 45-degree material FOV and 4.2 camera distance, which match the APE reference sphere framing closely;
- resets APE Match to a screenshot-calibrated ~26.7-degree camera pitch;
- remaps the recovered SSI sun yaw by +90 degrees into the Studio coordinate frame while retaining `180 - SSI pitch` for sun elevation;
- applies an APE-only horizontal environment handedness correction plus a 120-degree environment yaw calibration;
- uses the sampled APE No Lighting clear color exactly as RGB 76, 102, 127.

The calibration values are not claimed to be Treyarch engine constants. They exist only to converge the Studio output against reference APE screenshots without corrupting the recovered source data.

## Screenshot calibration completed in this pass

The supplied APE glossy-sphere captures were used only as regression/calibration references; they are not bundled with the Studio. Across Morning, Day, Sunset, and Night, the specular-highlight positions give a consistent fixed mapping from SSI space into the Studio camera frame. The resulting first-pass transform is:

- material camera: 45-degree vertical FOV, distance 4.2, pitch ~26.7 degrees;
- sun elevation: `180 - SSI pitch`;
- Studio sun yaw: `(SSI yaw + 90) mod 360`;
- APE environment sampling: horizontal handedness correction, then ~120 degrees of yaw rotation.

The four-preset light-direction fit is internally consistent to roughly a degree in the provided reference frames, which is sufficient for a Phase-1 implementation. These remain explicitly labeled calibration values until the corresponding APE/ToolsGfx camera and sky-rotation constants are found directly.

## Remaining parity work

Phase 1 is not the final APE renderer. The next comparison pass should calibrate or replace the approximations for:

1. replace the screenshot-calibrated camera/environment orientation constants with exact APE/ToolsGfx constants if they are recovered;
2. ToolsGfx exposure/tonemap behavior (`ev`, `evcmp`, `evmin`, `evmax`, `stops`);
3. probe convolution / roughness-dependent environment filtering;
4. direct sun intensity and penumbra behavior;
5. BO3 specular/gloss response and reflection weighting;
6. APE preview mesh tangent/normal behavior;
7. material category/techset-driven resource bindings;
8. a later A/B comparison workflow using captured APE reference frames.

The goal is to move each of these from screenshot approximation to a verified TOOLSGFX-compatible behavior as the relevant engine path is traced.

## Phase 1d — native APE preview geometry

APE Match no longer has to approximate Treyarch's preview UV layout with Shader Studio's procedural primitives. The model importer now reads BO3 `XMODEL_BIN` files used by the Mod Tools asset viewer (the `*LZ4*` token-stream format), including authored per-corner normals and UVs.

When the user's configured BO3 Mod Tools install contains the stock preview assets under `model_export/code`, APE Match and Neutral / No Lighting automatically use:

- `ape_preview_sphere.XMODEL_BIN`
- `ape_preview_cube.XMODEL_BIN`
- `ape_preview_plane.XMODEL_BIN`

Look Dev intentionally keeps the Studio's procedural primitives so this compatibility pass does not silently change existing artist-preview projects. If a stock APE reference model is unavailable, APE Match falls back to the existing Studio primitive rather than bundling Treyarch assets.

The ordinary **Load Model** workflow also accepts `.XMODEL_BIN`, so additional local BO3 models can be inspected without converting them to OBJ/XMODEL_EXPORT first. The Phase 1d reader is intentionally scoped to the stable static-token layout used by the shipped APE preview models; unsupported token variants fail with an explicit error instead of guessing their payload layout.

## Phase 1e — stock `Geometry/lit` material contract and color-space correction

Reverse-engineering the shipped APE/GDT/ToolsGfx data removed several guesses from the first material pass.

The stock `script_wall` material used for parity testing is present in Treyarch's `gdt.db` as:

- category: `Geometry`
- material type: `lit`
- color map: `core_script_wall_c`
- color tint: `1 1 1 1`
- no normal map
- no gloss map
- no specular map
- no AO map
- normal height: `1`
- gloss range: `0 .. 13`
- color sampler: `tile both*`, `aniso2x (mip linear)`

The corresponding image record marks `core_script_wall_c` as `diffuseMap` / `sRGB3chAlpha`, with source `art_assets\\t6_legacy\\texture_assets\\core\\core_script_wall_c.tif`.

The shipped `ToolsGfx/gbuffer_common.hlsl` also confirms that `BASE_TEXTURES` does not read an arbitrary specular/gloss/AO texture for stock `Geometry/lit`: reflectance is the fixed dielectric value `0.04`, occlusion is `1`, and gloss comes directly from `glossRange.y`. The Studio's neutral material fallbacks now reproduce that contract (default gloss `13/17`) while still allowing explicitly loaded optional maps for custom Studio materials.

Material texture uploads are now semantic-aware:

- Color / Albedo: sRGB
- Normal: linear
- Height / POM: linear
- Specular Color: sRGB
- Gloss: linear
- AO: linear
- Emissive Color: sRGB
- Opacity / Mask: linear

The material texture loader now generates mipmaps as well, matching APE's ordinary mip-filtered color workflow more closely. The Material Textures panel reports the decoded width/height and whether the GPU view is sRGB or linear so a successful file load is no longer ambiguous.

Because the D3D preview swapchain is `R8G8B8A8_UNORM`, APE Match now explicitly applies the linear-to-sRGB display transfer after tonemapping. Neutral / No Lighting and the semantic Albedo inspector also display-encode linear albedo. This fixes the previous mismatch where physically correct sRGB texture decoding would otherwise make APE-oriented material output appear too dark on screen.

A new **Input Albedo (t0)** debug view samples the loaded material resource directly before mesh UVs or GBuffer evaluation. It distinguishes three failure classes immediately:

1. input texture decode/binding is wrong -> Input Albedo is wrong;
2. input is correct but semantic Albedo is wrong -> mesh UV/material shader path is wrong;
3. both are correct but Final Lit is wrong -> lighting/probe/tonemap parity is wrong.

### APE executable pipeline confirmation

Analysis of `asseteditor_modtools.exe` / the supplied IDA database also confirms that APE's real-lighting material viewport is a multi-pass ToolsGfx pipeline. The executable names/asserts stages including `GBuffer Opaque`, `SSAO`, `Light Culling`, and `Deferred Lighting`, and contains separate GI debug variants for diffuse and specular contribution. `LightingNone` is a distinct lighting mode rather than a zero-intensity version of the real-lighting path. This supports keeping **APE Match** and **Neutral / No Lighting** as separate renderer modes and makes probe convolution / GI separation the next major renderer-parity target.
