# Packaging and Validation — What “BO3 Compatible” Actually Means

**Level: Intermediate → Advanced**

The Previewer separates several checks because they answer different questions.

## HLSL Compile

**Question:** Is this valid HLSL for the selected entry/profile?

A PASS here means the compiler accepted the shader source.

## Package Generation

**Question:** Did the Previewer build or load a coherent BO3 package description for this target?

An authored adjacent techset can be used when it matches the detected shader type. A forced Advanced preview target should not silently reuse an incompatible techset.

## BO3 Package Validation

**Question:** Do HLSL, techset, stage selection, parameter names, and package structure agree closely enough for BO3-style linking/usage?

This catches problems that plain FXC compilation cannot.

## Preview Available

**Question:** Can the Previewer render something with the current configuration?

A visible preview is useful, but it does not replace package validation.

## Generic HLSL

Generic HLSL can correctly report:

```text
HLSL Compile: PASS
BO3 Package Validation: N/A
```

That is not a failure. It means the file is being treated as generic Direct3D HLSL rather than claiming to be a complete BO3 asset.

## Export workflow

Use **Export to BO3** only after the shader target and package validation make sense. If you are intentionally forcing a different type in Advanced mode, read the warning before treating that result as representative of the authored shader.

## Exported shader include dependencies

The exporter copies **custom/local HLSL include files** that belong to your shader package, including nested custom includes. It intentionally does **not** copy BO3 stock shader headers supplied by the Previewer's `bo3_compat/shaders_stable` mirror.

For example, a PostFX shader can keep:

```hlsl
#include "postfx/postfx_common.h"
```

without the exported package containing duplicate `postfx/`, `lib/`, `gfxcore/`, or `code/` trees. Those headers are BO3 runtime dependencies, not files owned by the exported shader. This also prevents **Install into BO3** from replacing stock shader include files.

## PostFX runtime integration

A validated PostFX package describes shader/material resources, but BO3 still needs client script code to enable the filter on the local player.

The exporter no longer generates a shader-specific startup CSC or `.zpkg`. The complete activation steps and exact copy/merge-ready CSC code are written directly into the package's main `00_README_FIRST.txt` (or the direct-install `*_INSTALL_README.txt`). A duplicate backup copy is also created at `source_data/<namespace>/<base>_POSTFX_INTEGRATION.txt`. Merge that code into the usermap/mod client CSC that already owns local-player initialization.

The zone entries are:

```text
include,filters
material,<exported material name>
```

The generated integration uses `callback::on_localplayer_spawned`, accepts `localClientNum`, waits 3 seconds, creates the filter/pass from the export's actual base/material names, and enables it through `filters::enable_filter_persistent()` on reserved filter slot 6. This keeps the Studio filter alive when temporary stock/gameplay PostFX uses slot 0. If the CSC already has `on_player_spawned`, merge the generated thread call instead of defining/registering a duplicate callback.

The optional **Include shared _filters support files (first install only)** setting packages the shared `_filters.csc`, `_filters.gsh`, and `filters.zpkg` dependency. It is off by default so repeated exports do not overwrite a customized working copy. No shader-specific `.zpkg` is created. The looping `postfxbundle` export remains available as a separate manual workflow.

## Confirmed PostFX export rules from real BO3 runtime tests

The Previewer's PostFX exporter now preserves several rules that were confirmed by linking and running a converted Shadertoy paint shader in Black Ops III rather than inferred from standalone HLSL compilation:

- Sampler filter display values must be quoted in a techset, for example `filter = "linear (mip none)"`.
- Custom PostFX `Texture()` parameters use `semantic = "2d"` and a real generated `2d` fallback image. `$white_diffuse` is a diffuseMap image and is not a valid fallback for these slots.
- Generated BO3 asset identifiers are lowercase-normalized once and the same normalized identifier is reused by the GDT, material, image and package references.
- A `float1` parameter always exposes `x` on the left side even when it reads another packed component, for example `x = <cg00_y>`.
- Generated 2D image assets use APE's supported `compressed` compression setting; do not generate `compressed high color`.
- Texture parameters that users need to assign in APE include `Tweak()` metadata so they appear under **Shader Textures**. Scene-backed channels are exposed as **Preview Scene** on `colorMap00`.
- The separate TOOLSGFX export uses the APE PostFX contract proven by LG-RZ examples: `vs_generic`, an opaque preview RenderFlags block, `bilinearClampler` on `s0`, `PostFx_FixPreviewResolution`, and linear-to-sRGB display conversion for converted Shadertoy output.
- The exporter creates a package-owned linear/uncompressed/no-mip APE preview scene instead of copying the user's currently loaded Previewer image into `iChannel0`.
- Runtime scene channels are emitted as `CodeTexture("resolvedScene")` bindings in the runtime techset. Auxiliary Shadertoy images remain material images.
- Converted Shadertoy scene samples preserve the D3D/ShaderToy Y-orientation bridge, BO3 PostFX normalize/denormalize color-domain bridge, and PostFX render-target dimensions for `resolvedScene`-backed `textureSize()` expressions.
- After installing a new or changed techsetdef, restart Mod Tools / APE before judging the custom Material Type; APE can keep the old material-type registry while it remains open.

A package can therefore pass ordinary FXC compilation yet still fail BO3 parsing, APE image usage, material exposure, activation, or runtime parity. The package validator and exporter target these BO3-specific failures separately.
