# BO3 Shader Research audit

This is the human-readable companion to `bo3_shader_research_audit.json`. It records the evidence boundary for the BO3 package-parity architecture before that architecture is implemented.

## Corpus identity and scope

The authoritative input is `C:/Users/ryant/Desktop/BO3-Shader-Research-main(2).zip` (SHA-256 `3231F8CF314F0D6C595E890D06A24B2DF9920541731C084213658665D9088B13`, 372,491,598 bytes). It contains 53,659 ZIP entries and 51,675 files. The indexed working copy under the ignored build directory has the same file count.

The corpus is dominated by paired decompiled HLSL/ASM files: 25,630 HLSL and 26,022 ASM files. The `decompiled/techsetdef` tree is compiled shader material grouped by techset families; it is not a collection of authored `.techsetdef` files.

There are six authored `.techsetdef` files, all under examples. They are three byte-identical stable/stable_toolsgfx pairs:

- `basic` (`Geometry`)
- `decal_emissive_reveal` (`Decal`)
- `endportal` (`Specialty`)

This is enough to prove a useful parser subset and several resolution rules, but not enough to invent category or render-state behavior for every decompiled shader family.

## Techset patterns that are directly evidenced

The examples use includes, `Globals`, string and object-form render flags, available prefixes, `Sampler`, `Texture`, `Color`, `Bool`, `uint1`, `float1`, `float2`, `Tweak`, `Technique`, `VertexShader`, `PixelShader`, `RenderFlags`, and `Image`.

Inheritance occurs at several levels:

- technique inheritance, including empty derived bodies;
- resource/parameter aliases such as a `Texture` deriving from another named texture;
- generic VS/PS inheritance followed by local overrides;
- `defines +=` on derived techniques;
- stage-local `source` overriding technique-level `source`.

The parser therefore cannot safely flatten a technique to an arbitrary PS. Resolution must first apply configuration conditionals, then technique inheritance, then technique fields, then VS/PS inheritance and stage-local fields. A multi-name `Technique("lit", "unlit")` must create aliases that resolve to the same body without discarding either name.

Observed state strings include `replace + depth`, `gbuffer opaque`, `add + depth + decal`, `blend + depth + decal`, `depthPrepass`, `blend + depth`, `fallback + depth + rez`, and the Previewer's proven generated PostFX form `replace + nocull`. The tokens are real; their exact D3D blend/depth/cull descriptors are not present in this corpus and should be reported as unknown until proven.

## TOOLSGFX and runtime

The two package configurations are not interchangeable.

The End Portal techset is byte-identical in the stable and stable_toolsgfx trees. It uses `#if TOOLSGFX == "1"` to add editor fallback parameters and to select a stock TOOLSGFX pixel shader. The runtime branch selects the custom `endportal_ps.hlsl`. That custom HLSL exists only in `shaders_stable`; it is intentionally absent from `shaders_stable_toolsgfx`.

By contrast, Basic and Decal duplicate their custom HLSL into both shader roots. The resolver therefore must not assume that stable and stable_toolsgfx files are equal, different, or both present. It must evaluate the selected configuration and source path.

## Compiled resource and interface findings

ASM reflection tables prove that optimized bytecode is the authoritative live interface. They contain resource name, kind, dimension, slot, cbuffer layout, and input/output signatures. This catches cases that source-text matching cannot: optimized-out resources, type/dimension mismatches, and incompatible stage interfaces.

Representative pairs show why one fullscreen template is unsafe:

- PostFX `2d/out/of/bounds`: VS outputs `SV_POSITION0` and `TEXCOORD0.xy`; PS consumes those and binds `frameBuffer:t0`, two additional textures, `trilinearSampler:s1`, `$Globals:b0`, and `PerSceneConsts:b1`.
- CompositeFX: the same basic semantic pair, but `colorMap:t0`, `normalMap:t6`, `clampSamplerState:s1`, and `PostFxCBuffer:b8`.
- Resolve: a vertex-ID fullscreen VS outputs `SV_POSITION0` and `TEXCOORD0.xy`; PS binds `colorMapSampler:t0` and `pointClamp:s1`.
- Shell/shock flashed: PS additionally requires `COLOR0`; generating only an unrelated UV is not compatible.
- Sky lat-long parallax HDR: PS requires 3-component `TEXCOORD0` and `TEXCOORD1`, plus `$Globals`, `PerSceneConsts`, and `LightingGlobals`.
- GBuffer backlit emblem: the pair exchanges `COLOR1` and four TEXCOORD semantics, the PS consumes `SV_IsFrontFace`, and it writes three render targets.

Across 614 decompiled PostFX HLSL files, common optimized bindings include `frameBuffer:t0`, `bilinearClampler:s1`, `$Globals:b0`, `PerSceneConsts:b1`, `GenericsCBuffer:b3`, and `PostFxCBuffer:b8`, but variants also use `samp0`, `bilinearSampler`, `codeTexture0..2`, `resolvedScene`, depth, reveal, normal, mask, and other resources. Names such as `colorMap` and CodeTexture-style inputs do not imply one universal register.

The reusable reflection model must include textures, samplers, cbuffers, constant variables, input semantics, and output semantics for both stages. Compatibility checks should compare semantic name, index, component mask, and numeric kind, with explicit handling for system-value semantics.

## Source paths

All three examples are installed by copying their folder tree into the BO3 root. Authored techsets live beneath `techsetdefs_stable/<category>` and `techsetdefs_stable_toolsgfx/<category>`. Custom shader sources are either at the stable shader root or refer to stock subpaths such as `specialty/emissive_objective.hlsl` and `ToolsGfx/debug_texeldensity.hlsl`.

The Previewer's previously confirmed PostFX finding remains narrow and actionable: a custom shader that uses stock `postfx/` includes failed under `shaders_stable/_custom` and worked at the stable shader root. Package validation should enforce that known generated-layout rule without declaring every nested source path invalid.

## Architectural gap at the checkpoint

The inherited PostFX parity checkpoint is intentionally separate. It adds valuable runtime color modeling, source-path handling, optimized PS reflection checks, and export checks, but its package logic is still local to `main.cpp`, source-regex driven, and PostFX-specific. Techsets are generated by string concatenation and are not parsed or resolved to drive preview.

The next architecture must introduce:

1. `BO3ShaderPackage`, techset, technique, stage, resource-binding, compiled-interface, resolution, and validation models.
2. A conservative parser for the observed syntax and a configuration evaluator for the required conditional subset.
3. Runtime and TOOLSGFX technique resolution with inheritance, aliases, defines, source precedence, and local resource overrides.
4. One optimized-bytecode reflection representation shared by preview and export.
5. Package-level resource and VS/PS interface validation that gates preview independently of D3DCompile success.
6. Structured techset serialization so generated packages, preview, and validation use the same description.

Unknown external includes/bases, unsupported syntax, unproven CodeTexture names, and unproven render-state mappings must produce Unsupported/Unknown/Warning rather than simulated compatibility.

