# BO3 Shader Studio 0.1

### Tester CI oversized GLSL fast path

Automatic Tester CI runs the normal sharded converter corpus, but fixtures explicitly marked `BO3_FAST_REGRESSION_DEFER_CASE` are now deferred **as whole cases** to full/manual CI. The 84 KB `63_macro_metaprogramming_full.glsl` stress fixture was measured at roughly 135 seconds in converter processing alone, so merely skipping FXC did not make Tester CI fast. Manual/full releases still convert, assert, and FXC/O3-compile that fixture end-to-end. Local `--regression-fast` keeps the older `BO3_FAST_REGRESSION_SKIP_FXC` behavior for debugging. Lean Tester builds also skip `windeployqt`, because Qt is already on the runner PATH and lean updater payloads intentionally do not resend Qt runtime DLLs.



## Unreleased - BO3 package and techset parity architecture

- Reworks automatic Tester CI around a genuinely cacheable MSVC path. qmake's NMake batch rules are disabled only for the sccache build, so each translation unit becomes an independent cache key instead of one uncached multi-source `cl.exe` command. The fast path also uses pinned Qt `jom` 1.1.7 for parallel compilation (with SHA-256 verification and an nmake fallback) and skips the redundant TinyEXR fetch/validation process when the vendored v1.0.8 files are already present.
- Adds sharded headless GLSL regression execution. `--regression-shard <index>/<count>` partitions the sorted converter corpus exactly once across independent processes; automatic Tester runs also pass `--regression-fast`, which keeps converter assertions and FXC code generation but uses FXC O0 for the broad corpus. Manual/full releases retain the O3 validation path. The GitHub workflow runs up to four shards concurrently while preserving one regression gate/result.

- Adds a **clean BO3 HLSL lowering pass** to the GLSL/Shadertoy converter. Generated targets now emit only compatibility helpers actually referenced by the converted shader instead of the entire GLSL compatibility library, while preserving helper dependency closure. This substantially reduces generated source size, FXC parse surface, and BO3 stock-header collision risk.
- Adds conservative converted-code optimization: unreachable namespaced GLSL helper functions are removed from the `mainImage` call graph, side-effect-free unused shader-private globals are pruned, simple one-argument vector constructors lower directly to FXC-safe HLSL instead of universal helper calls, and runtime-dependent global initializers are moved into the fragment entry point when FXC cannot legally initialize them at global scope.
- Adds FXC-oriented loop normalization for small compile-time integer loops (`[unroll]`) and maps GLSL `roundEven` to HLSL `round`. Existing array, struct, macro/preprocessor, matrix, overload, derivative, texture, and shader-golf compatibility passes remain in place and now feed the cleaner target emission stage.
- Extends package regressions to verify dependency-based helper emission, dead converted-helper elimination, runtime-global lowering, and constant-loop normalization.
- Adds first-class `BO3ShaderPackage`, techset, technique, shader-stage, resource-binding, render-state, and compiled shader-interface models. Preview/export compatibility is no longer defined by pixel-shader compilation alone.
- Adds a conservative BO3 techset parser for the syntax evidenced by BO3-Shader-Research: includes, Globals/RenderFlags, Sampler/Texture/numeric/Bool/Color parameters, Tweak metadata, aliases, technique and stage inheritance, `defines`/`defines +=`, source overrides, Texture/CodeTexture bindings, and the required TOOLSGFX conditional subset.
- Resolves runtime and TOOLSGFX configurations independently, including technique aliases, inheritance, state, defines, source precedence, and stage-local resource overrides. External stock include bases remain explicitly `UNKNOWN` when their implementation is unavailable.
- Compiles and reflects optimized VS/PS bytecode into one reusable interface model containing textures, samplers, constant buffers and variables, registers, texture dimensions, and input/output semantics.
- Validates compiled resources against resolved techset parameters and CodeTexture bindings, including missing or mismatched sampler/texture names, type mismatches, conflicting bindings/registers, optimized-out bindings, material constants, VS/PS semantic compatibility, and the confirmed stock-PostFX include path rule.
- Adds structured techset serialization. Generated PostFX and custom-material techsets now come from the same models used for parsing and validation; arbitrary techset string assembly is no longer the generation path.
- Separates `HLSL Compile`, `BO3 Package Validation`, and `Preview` status. A failed or unknown generated PostFX package suppresses package preview; raw non-package HLSL preview is labeled as raw HLSL rather than BO3-compatible.
- Adds the first no-code **Beginner Shader Builder** for BO3-first authoring. Beginner mode now opens a dedicated visual workspace instead of the HLSL editor: choose **Screen Effect**, **Object / Material**, or **Sky / Environment**, start from BO3-safe presets, build an ordered effect stack, and adjust colors/sliders while Shader Studio silently regenerates and live-compiles the HLSL. The first validated module set is Color Tint, Brightness, Contrast, Saturation, Black & White, Invert, Vignette (PostFX only), Scanlines, Animated Pulse, and Procedural Noise. Unsupported target/effect combinations are not offered.
- Beginner projects save as editable **`.bo3shader`** JSON projects, preserving target, base appearance, effect order, enable states, and parameter values instead of flattening the work into HLSL. **View Generated HLSL** moves the same project into Advanced mode for learning without requiring code for normal use. Beginner export hides namespace/register/sampler/techset plumbing, keeps dependent BO3 asset names synchronized behind one friendly shader-name field, and still goes through the existing BO3 compile/package validation and direct-install/package paths.
- Beginner-generated targets use BO3-specific contracts rather than generic graphics abstractions: PostFX uses `resolvedScene`-class `frameBuffer` sampling with the proven `bilinearClampler` and PostFX color normalization bridge; Material uses the stock BO3 skinned geometry path and emissive/unlit custom-material export, with scanline/noise coordinates anchored to skinned local 3D position so procedural effects do not reintroduce UV seam/pole artifacts; Sky uses the directional self-camera sky contract and BO3 `gameTime`. The PostFX/package regression suite now FXC-validates representative Beginner presets, checks their package adapters, tests `.bo3shader` round-tripping, and enforces target-gating rules.
- Reworks the main Qt workspace around a beginner-friendly **Beginner / Advanced** interface mode. The old two-row toolbar is replaced by one command bar; technical runtime/compiler controls move into a vertical **Preview Settings** inspector; Console, Inputs, Parameters, Material Textures, Performance, Scene / Lighting, and Script Vectors share one compact top-labeled bottom tab strip; and the default workspace is versioned so older dense dock layouts do not silently override the redesign.
- Beginner mode is now the default and keeps only friendly project/preview actions such as Open, Save, Preview Image, Preview, Pause, Live Update, and Export visible; manual Compile/Reload, depth-map plumbing, GLSL conversion, and technical diagnostic docks stay in Advanced mode unless a real error needs attention. Its preview bar collapses irrelevant Material/GBuffer controls, groups environment/background/lighting under **Show**, exposes a clear **⚙ Settings** drawer, and defaults to **Fill View** so the preview remains visually dominant without large letterbox bars; **Fit View** remains available when every source pixel must be visible. Beginner mode **auto-detects and locks the shader type** from the generated HLSL/package contract (PostFX, Material, Skybox, or generic HLSL) so a fullscreen PostFX shader is no longer accidentally shown as a white material sphere or invalid sky. Advanced mode restores the manual **Preview As** override and shows a warning when the forced target disagrees with detection, together with BO3 runtime context, source/capture diagnostics, entry/profile, synthetic/neutral compatibility controls, camera/input-motion tools, Scene / Lighting, and Script Vectors.
- Adds **File → New Example** with separate starter HLSL for **BO3 PostFX**, **BO3 Material**, **BO3 Skybox**, and **Generic HLSL**. Each example is authored for its actual coordinate/resource model instead of reinterpreting one shader through unrelated renderers. The PostFX sample remains an authored HLSL + techset package; Material and Skybox exercise their real temporary BO3 package adapters; generic HLSL remains deliberately outside BO3 package validation.
- Fixes the GLSL/Shadertoy **BO3 Material / Surface** target end-to-end: converted material HLSL now carries a preview-only `gameTime` constant buffer so `iTime` shaders FXC-validate immediately, the final BO3 Custom Material adapter suppresses that fallback and uses the engine `gameTime`, and package entry analysis now recognizes generated `ps_main(..., SV_IsFrontFace)` signatures instead of incorrectly reporting `ADAPTER_MATERIAL_NO_PS`. Material conversions are FXC-validated in the converter and covered by dedicated regression cases.
- Isolates user-authored GLSL helper functions behind `BO3GLSL_USER_` names for BO3 targets before stock headers are introduced. This prevents legacy FXC X3003 collisions such as a Shadertoy-defined `max3(float3)` conflicting with BO3's own `gfxcore/hlslcoreminmax.h` helpers, while preserving `mainImage` as the converter entry contract.
- Makes the GLSL converter's selected target authoritative in the editor: **Open Current in Editor** refreshes Detected Type instead of inheriting the previously opened shader (commonly the PostFX starter), and explicit Material/Sky/PostFX converter markers now outrank generic include/semantic heuristics when saved output is reopened.
- Fixes strict converted-Material preview parity: adapter-generated material constants now receive the same runtime defaults as BO3 export assets instead of silently starting at zero (which multiplied valid RGB to black), and converter surface markers now select the same BO3 wrapper used by export. **Opaque** and **Alpha Cutout** render through the generated BO3 GBuffer/MRT contract and the Previewer's deferred final-light path, while **Transparent** remains forward. Texture-free Material/Sky conversions also stop emitting an unused synthetic `glslSampler`.
- Fixes procedural custom Material export in APE without adding a texture dependency to the custom GBuffer shader. Runtime techsets remain self-contained on `lit_base_shaders` and expose only textures/samplers actually used by the custom HLSL. Deferred materials also restore BO3's auxiliary stock `unlit` technique and its required `colorSampler`: `ps_generic` binds `$white_diffuse` directly at runtime, while the separately generated TOOLSGFX techset binds the generated/real APE editor image both as the editor `colorMap` parameter and directly inside that unlit pixel-shader stage. This mirrors the working End Portal editor contract and prevents APE from interpreting the bare material field name `colorMap` as an image asset. The editor image uses APE-valid `uncompressed`, no-mipmap diffuse settings; the material GDT `colorMap` can remain empty and the custom runtime GBuffer shader remains genuinely procedural.
- Adds **BO3 Install History** for direct installs. Each successful **Install into BO3** records the target, asset names, BO3 root, exact relative file list, and SHA-256 hashes in persistent app data. **File → BO3 Install History...** can open an install, show changed/missing files, delete only that export's tracked files, preserve files shared by another active install, keep user-modified files unless explicitly forced, prune empty export folders, mark repeated same-name installs as replaced, and forget old history rows without touching BO3. Older installs made before this feature are intentionally not guessed or auto-deleted.
- Adds target-aware **Sky source** handling to the GLSL/Shadertoy converter. **Auto Detect** distinguishes normal 2D/image-space skies from recognizable 360° self-camera/view-ray shaders; **2D / Image-Space** uses a seamless direction-space triplanar wrapper with no longitude seam or polar singularity, while **360° / Self-Camera** replaces `rd`/`rayDir`/`viewDir` camera rays with BO3 `skyDirection` and suppresses recognized mouse/camera orientation updates instead of projecting the shader twice. Self-camera output is marked so generic Camera/Input Removal will not destructively rewrite it again.
- Replaces the unreliable shader-specific **PostFX auto-start CSC/ZPKG** path with explicit usermap/mod CSC integration. Every PostFX export now writes `<base>_POSTFX_INTEGRATION.txt` using the exporter's actual base/material names; the zone entries are `include,filters` plus `material,<material>`. The optional first-install `_filters.csc`, `_filters.gsh`, and shared `filters.zpkg` dependency can still be packaged, but no shader-specific `.zpkg` or `REGISTER_SYSTEM` startup helper is generated.
- Adds non-destructive **Preview As HLSL / PostFX / Material / Skybox** sessions for Advanced mode. Standalone HLSL uses an in-memory generic harness with package validation explicitly `N/A`; strict targets build target-aware temporary packages and render only after the real compiled/reflected BO3 package validator accepts them.
- Adds live `AUTO`, `GUIDED`, and `UNSUPPORTED` adapter states, immediate guided-resource remapping, session invalidation across shader/target/configuration/interface changes, and **Save Current Preview Package As...** serialization of the exact validated in-memory adaptation. Selecting a preview target never writes shader or techset files.
- Makes converted Shadertoy PostFX preview substantially more automatic: generated `glslSampler*` names are normalized to a BO3-known sampler without a mapping dialog, `iChannel0` defaults to `resolvedScene`, distinct auxiliary images remain material parameters, duplicate source-image channels are promoted to `resolvedScene`, and loaded converter assets are carried directly into the editor preview. The converter thumbnail can also be used to choose a local Texture/Image before opening the pass.
- Runtime auto-packaging now follows optimized FXC reflection rather than raw declarations: commented resources are ignored, optimized-out texture/sampler parameters and CodeTexture bindings are pruned from temporary techsets, and converted Shadertoy scene channels receive the same `PostFx_NormalizeColor` / final `PostFx_DenormalizeColor` bridge used by known-working BO3 PostFX shaders. Strict validation remains unchanged and still suppresses genuine FAIL/UNKNOWN packages.
- Moves **Remove Shader Camera / Input Motion** into Advanced Preview Settings. The checkbox still non-destructively rewrites only the temporary/adapted preview/package copy, while a new explicit **Apply Camera/Input Removal to HLSL** command applies the same conservative transform to the editor source itself. The direct edit is undoable with Ctrl+Z and recompiles immediately; unrelated time animation is still preserved. **Save Current Preview Package As...** continues to persist the movement-free adapted HLSL when the non-destructive checkbox is used.
- Converted Shadertoy channels mapped to `resolvedScene` now report the PostFX render-target dimensions rather than the dimensions of a user-loaded stand-in image. This keeps fullscreen source imagery at the correct size, aspect and position without adding a second Y flip; auxiliary material images retain their own dimensions.
- Automatically treats an adjacent same-basename `.techsetdef` as the authored package for any opened HLSL file. The resolved technique and Runtime/TOOLSGFX configuration are shown in compiler output, and authored packages are validated without export-only sampler normalization or preview-global injection.
- Applies package validation to generated runtime/TOOLSGFX PostFX, custom-material, and procedural-sky exports. Exports stop on proven compatibility errors and report `UNKNOWN` instead of claiming success when required stock shader bytecode is not bundled.
- PostFX export now builds a **separate APE/TOOLSGFX preview package** modeled on LG-RZ's working PostFX examples: the tool techset uses `vs_generic`, opaque preview RenderFlags, a `colorMap00` Preview Scene, and the TOOLSGFX shader uses `bilinearClampler` on `s0`. Converted Shadertoy wrappers also use `PostFx_FixPreviewResolution` and `LinearToSRGB` in APE. Runtime HLSL/techsets remain separate and keep `CodeTexture("resolvedScene")`.
- Scene-backed PostFX channels no longer copy the user's loaded Previewer screenshot into the export. A deterministic package-owned linear/uncompressed/no-mip APE test scene is generated instead, while unrelated neutral BO3 script-vector values are no longer exposed as fake material controls.
- Adds `BO3HLSLPreviewer.exe --bo3-package-regression-all`. The focused package cases cover corpus-derived Basic, Decal Emissive Reveal, and End Portal structures; Aurora runtime/TOOLSGFX selection; parser/writer round trips; resources/constants; semantics; render states/source paths; temporary Preview As lifecycle; no-disk behavior; guided remapping; exact live-state persistence; motion-removal transforms; and fullscreen resolvedScene geometry preservation.
- The existing export/package suite contains the reflected bundled HLSL/techset sample, starter shader-type detection, direct structured material generation, conservative sky-stage validation, and a protection regression proving self-camera sky output is not destructively processed by generic camera/input removal. The interactive GLSL regression suite also includes a target-specific self-camera sky case that must auto-detect, replace the view ray, and FXC-compile.
- Records the real BO3 PostFX export/link/runtime findings in `docs/POSTFX_EXPORT_RUNTIME_TEST_FINDINGS_2026-08-17.md`, including the parser, GDT/APE, activation, CodeTexture, orientation, color-domain, and render-target-size fixes confirmed by the `sv_paint` test package.
- Records the corpus evidence and uncertainty boundary in `docs/BO3_SHADER_RESEARCH_AUDIT.md` and the machine-readable `docs/bo3_shader_research_audit.json`.


## Unreleased - Shadertoy project converter tabs

- Changes the converter **Target** default to **None**, requiring an explicit PostFX, Sky, Material, or HLSL-only choice before conversion/export. Attempting a target-dependent action while **None** is selected is blocked and highlights the Target selector in red until a valid target is chosen.
- Reworks **Tools -> GLSL -> BO3 HLSL Converter** into a project workspace with **Common, Image, and Buffer A-D** source/output tabs. Common remains shared source only; each populated render pass is converted independently through the same 0.19.0 converter engine and target wrappers.
- Adds **Convert Current/All** and **Save Current/All** while retaining the original target selector and backwards-compatible single-pass Image workflow.
- Imports Shadertoy `.json`/`.fragment` exports, including Common/Image/Buffer code, per-pass `inputs[]`, output-ID buffer routing, image/cubemap/keyboard/audio types, source/resource IDs, and filter/wrap/vflip/sRGB sampler metadata.
- Gives Image and every Buffer an independent `iChannel0..3` model. Common deliberately has no channel UI or stored channel bindings.
- Adds reachable per-pass usage analysis with **Assigned / Unused**, **Referenced**, **Used**, and conservative **Possibly Used** states. It follows called Common helpers, sampler helper arguments, and safe macro expansion while ignoring comments, declarations, and unreachable Common helpers.
- Adds project handling modes: **Preserve Shadertoy Inputs**, **Make BO3 Standalone**, and **Custom Per Channel**. Custom choices are Auto, Preserve, Procedural, Neutral, and Remove; unsafe requests fall back to Preserve with a visible warning.
- Standalone mode neutralizes directly sampled keyboard/audio inputs, proceduralizes only clearly identified generic noise/random images, ignores unused bindings, preserves important images and cubemaps, and retains feedback/uncertain Buffer dependencies instead of flattening them unsafely.
- Replacements happen at texture-sampling expressions, so sampler declarations never become invalid numeric values. Helper/macro graphs that cannot be completely and safely rewritten are preserved.
- Adds a progressive persistent `shadertoy_assets` cache and index. Imports automatically fetch only used/possibly-used image assets that remain preserved; unused metadata does not trigger a download, and an explicit Custom/Preserve choice can fetch it later.
- Displays real cached thumbnails for image inputs and labeled tiles for buffers, cubemaps, keyboard, audio, and other resources. Clicking a tile opens full binding, analysis, handling, sampler, cache, and error details; **Open Asset Cache** exposes the cache directory.
- Generated pass headers record dependency/handling decisions and warnings. Separate pass shaders and previous-frame feedback requirements remain explicit project metadata; runtime multipass execution and full BO3 texture packaging are intentionally out of scope.
- Adds `--shadertoy-regression-all` and 26 focused semantic cases under `tests/shadertoy_project_converter`, alongside JSON import/cache smoke fixtures. The original 102-case GLSL converter suite remains unchanged.
- Adds `--postfx-export-regression-all` for BO3 package-parity checks: generated GLSL sampler normalization, HLSL/techset resource matching, custom fullscreen VS selection, and root-level PostFX source paths for stock include compatibility.
- Adds explicit **PostFX context** selection in Advanced Preview Settings: **TOOLSGFX** compiles `TOOLSGFX=1` and uses the ordinary material-image fallback, while **BO3 Runtime** compiles `TOOLSGFX=0`, simulates `CodeTexture("resolvedScene")` at BO3's 32768 postfx color scale, and converts the scaled runtime output back for display. This fixes the previous mixed-mode bug where runtime `PostFx_NormalizeColor` math was run against an unscaled 0..1 preview image.
- BO3 Runtime display now applies a standard linear-to-sRGB transfer after undoing the 32768 postfx scale. The Previewer swap chain is UNORM (not sRGB), so writing scene-linear shader output directly made BO3-working postfx appear darker and more saturated than in-game captures.
- BO3 Runtime input now also decodes loaded LDR PNG/JPG/TIFF source RGB from sRGB to scene-linear before applying the 32768 `resolvedScene` scale. The previous runtime bridge corrected only the output side and still fed display-encoded bytes into shaders that expect scene-linear data. LDR screenshots remain an approximation because clipped HDR values above 1.0 cannot be recovered.
- BO3 Runtime t0 now accepts **scene-linear OpenEXR** directly as a floating-point `R32G32B32A32_FLOAT` source, preserving values above 1.0 instead of tone-mapping/clipping them through WIC. Advanced Preview Settings labels the source as **HDR linear** or **LDR approx** so parity limitations are explicit.
- Adds an explicit **Scene EV** runtime control, with neutral `0.00 EV` as the canonical default/reset. The same user-selected `2^EV` scale is applied before the 32768 resolvedScene storage scale and is also reflected into `exposure`, `invExposure`, `exposureClamped`, and `relHDRExposure` when shaders consume those BO3 scene constants.


## 0.19.0 - GLSL converter compatibility, automation, and semantic safety

- Delivers a major GLSL -> BO3 HLSL compatibility pass focused on real macro-heavy and shader-golf sources while retaining legacy FXC support.
- Expands macro/metaprogramming conversion through nested, transitive, object-like, function-like, unary, binary, and multi-argument custom-struct overload patterns. The full macro-metaprogramming regression shader now converts and compiles successfully.
- Makes identifier typing scope-aware where GLSL swizzle aliases overlap custom-struct fields: vector `.stpq` continues to lower to HLSL `.xyzw`, while fields such as `.p` and `.t` remain literal members of the nearest scoped struct type.
- Extends matrix lowering through explicit constructors, macro bodies, nested matrix products, and chained matrix-vector/vector-matrix expressions while preserving the converter's GLSL-to-HLSL transpose convention and ordinary scalar/matrix scaling.
- Makes exact custom-struct overload normalization safer and more complete across normal function bodies, nested expressions, macro wrappers, parenthesized member chains, and helpers with three or more arguments.
- Preserves function-call semantics for side-effectful exact-overload arguments. Calls involving mutation, `out`/`inout`, or transitively state-changing user functions use unique signature-specific wrappers instead of textual substitution that could duplicate or reorder evaluation.
- Adds headless regression commands for automation: `BO3HLSLPreviewer.exe --regression-case <number-or-name>` and `BO3HLSLPreviewer.exe --regression-all`, including FXC diagnostics, generated-HLSL error context, and process exit codes suitable for scripts and CI.
- Adds `build_regression.bat` for incremental non-interactive converter builds without changing the normal clean/deploy/open behavior of `build_qt.bat`.
- Expands the bundled GLSL converter regression suite to 102 focused cases, including converted-output assertions that verify intended transformations instead of relying only on successful FXC compilation.
- Verified at release against all 102 bundled regressions and a cache-disabled random sample of 250 public shaders using seed 1337: 102/102 regressions and 250/250 corpus shaders passed, with no failures or skips.


## 0.18.69 - preprocessor-safe global const promotion

- Fixes GLSL top-level `const` detection being thrown off by braces, parentheses, and brackets embedded inside `#define` templates.
- Preprocessor logical lines, including backslash-continued macros, are now position-preservingly masked while global scope depth is calculated.
- Restores the intended GLSL -> FXC lowering from top-level `const` to shader-private `static const` even in macro-metaprogramming shaders.
- This directly targets FXC X3207/X3011 failures where valid GLSL constants were accidentally left as external HLSL globals.
- Adds regression case 87 for a global const declared after a macro containing a full function body.


## 0.18.68 - preprocessor-safe inout initialization

- Fixes the FXC out/inout definite-assignment pass scanning generic declarations inside `#define` templates.
- Macro type parameters such as `d` in `#define P(d,e) d e(inout d a,d b){...}` are no longer mistaken for concrete HLSL types.
- Preprocessor logical lines are skipped even when a macro uses backslash continuations.
- Prevents invalid generated declarations such as `d _glsl_inout_saved_a = a;`.
- Adds regression case 86 for a generic-type macro that generates an `inout` function.


## 0.18.67 - inside-out exact overload lowering

- Fixes nested exact custom-struct overloads being normalized in the wrong order.
- The converter now discovers the complete nested call tree, lowers the deepest non-overlapping calls first, and defers overlapping parent calls to the next bounded pass.
- Prevents an outer call such as `mu(su(...), ...)` from expanding first and burying the still-ambiguous inner `su(...)` under generated member accesses like `(su(...)).a`.
- Adds regression case 85 for nested exact-overload ordering.


## 0.18.65 - exact binary helper return-type inference

- Extends conservative exact-overload type inference through two-argument helper functions such as `c11(.5,.0) -> v11`.
- This lets nested chains like `su(frfl(c11(...)), c11(...))` resolve the same exact custom-struct overload GLSL selects before legacy FXC sees an ambiguous overload family.
- Adds regression case 83 for constructor-like binary helper calls nested beneath unary and binary overloads.

## 0.18.64 - precise local/function shadow renaming

- Fixed the local/function shadow pass renaming every local whose identifier matched any function name elsewhere in the shader.
- Locals are now renamed only when their own initializer actually calls the same-named function, such as `float arg = arg(z);`.
- Preserves innocent locals such as case 63's `mat3 pf = addf(...);`, so macros that later expand to `pf[0]`, `pf[1]`, etc. still reference the real local variable.
- Keeps the member/swizzle safety added in 0.18.62.
- Adds regression case 82 for a macro referencing a local that shares a name with a separate function.


## 0.18.63 - scope-aware matrix identifier typing

- Fixed matrix multiplication lowering treating a short identifier as matrix-valued merely because the same name was declared as a matrix elsewhere in the shader.
- Matrix detection now prefers the nearest concrete numeric declaration before the expression, matching the scalar/vector typing behavior already used by the converter.
- Prevents component-wise vector products such as `(34.0*x+1.0)*x` inside `vec3 ... (vec3 x)` from being rewritten to HLSL `mul(...)` because an unrelated `mat3 ... (mat3 x)` exists elsewhere.
- Adds regression case 81 for reused vector/matrix parameter names and overloaded result helpers.


## 0.18.62 - member-safe scoped local shadow renaming

- Fixed local/function shadow renaming corrupting struct members and vector swizzles with the same token as the renamed local. For example, `float z = z(...); ... p.z ...` now renames only the bare local `z` while preserving `p.z`.
- Handles whitespace around member access as well (`p . z`).
- Applies the same member-safe scoped replacement to locally shadowed Shadertoy built-ins.
- Adds regression case 80 for the exact local-`z` / `p.z` pattern exposed by full-source case 63.

## 0.18.61 - iterative exact custom-struct overload lowering

- Re-runs exact custom-struct overload lowering on newly exposed expressions, with a bounded six-pass safety cap.
- Fixes aggregate overload expansion that reveals a second exact nested overload only after the first pass.
- Adds regression case 79.

## 0.18.60 - nested unary return-type inference

- Extends exact overload type inference through unambiguous unary custom-struct function calls such as `ab(p)`.
- Allows outer custom-struct/scalar overload calls to be resolved before legacy FXC sees an ambiguous overload family.
- Adds regression case 78.


## 0.18.59 - exact custom-struct overload resolution

- Fixed swapped-overload inlining so formal parameter substitution never rewrites struct member names such as `.a` / `.b`; this removes the incorrect field swapping and truncation warnings exposed by case 63.
- Added conservative static type inference for simple return expressions and member chains. When GLSL has one exact custom-struct overload match but legacy FXC reports X3067, the converter inlines that exact implementation instead of relying on FXC's weaker overload resolver.
- This directly covers aggregate forwarding calls such as `mu(p.x, s.x)` where `p.x` is a known custom struct and `s.x` is a scalar. No shader-specific type or function names are hardcoded.
- Regression diagnostics now prefer the actual **error** line over an earlier warning line when printing `Generated HLSL line ...`.
- Added cases 76 and 77 for member-name-safe swapped forwarding and exact custom-struct/scalar overload forwarding.


## 0.18.58 - simultaneous GLSL macro parameter substitution

- Fixed function-like GLSL macro parameters being substituted sequentially during converter-side pre-expansion.
- Later macro parameters can no longer rewrite identifiers inside argument text already inserted for an earlier parameter; this was the source of corrupted expressions such as `a.mu(...)` in the macro-metaprogramming shader.
- Added regression case 75 covering nested top-level macro expansion with cross-named struct-member arguments.

## 0.18.57 - FXC swapped-overload forwarder fix

- Inlines trivial two-argument overloads that only call the same function with their arguments swapped when a matching opposite-order implementation exists.
- Avoids legacy FXC X3067 ambiguity in macro-generated custom-struct overload families while preserving the original GLSL semantics.
- Adds regression case 74 for scalar/custom-struct commutative overload forwarding.

## 0.18.56 - Syntactic fragment macro expansion fix

- Pre-expands intentionally unbalanced object-like shader-golf macros before GLSL token/function conversion (for example macros that provide only the beginning or end of a function call/body).
- Recognizes object-like replacement text even when it begins immediately after the macro name, such as `#define tail);return ...`.
- Prevents the GLSL function-call scanner from searching across later source lines when an unfinished call starts inside a preprocessor directive.
- Adds regression case `73_fragment_object_macro_splice.glsl`, reproducing the `hfrac` / `gthv` / closing-tail pattern from case 63.

## 0.18.55 - Compiler Output / Copy All truncation fix

- Strips embedded NUL characters from FXC/D3DCompile diagnostics before they enter the Compiler Output report.
- Fixes regression reports appearing to stop at the first failed shader even though later cases actually ran.
- Fixes **Copy All** truncating at the first FXC diagnostic NUL on Windows.
- Copy All now defensively sanitizes clipboard text and reports the copied character count in the status bar.
- Preserves the 0.18.54 GLSL converter fixes unchanged so case 63 can expose its generated HLSL source line correctly.

## 0.18.54 - `vec1` scalar-alias constructor inference

- Treats shader-golf `vec1` declarations as scalar declarations during one-argument `vecN(...)` inference.
- Prevents reused short identifiers from inheriting an older vector width from another function, which could emit an invalid FXC numeric cast/constructor and `X3014`.
- Extends matrix operand type inference to recognize `vec1` as scalar as well.
- Adds regression case `72_vec1_scalar_alias_shadow.glsl`.
- Regression failures now print the exact generated HLSL source line referenced by the first FXC diagnostic.

## 0.18.53 - HLSL `mul` macro collision hardening

- Detects GLSL preprocessor macros that collide with translated HLSL intrinsics, including `#define mul(...)`.
- Renames the user macro and its uses before matrix lowering, preserving the GLSL helper while freeing HLSL `mul(...)` for generated matrix operations.
- Matrix-by-matrix lowering now emits the native HLSL `mul(...)` directly instead of the temporary `GLSL_MATMUL` marker.
- Adds a regression case for a shader that defines its own `mul` macro and also performs GLSL matrix multiplication.

## 0.18.52 - scoped matrix operand typing

- Fixes GLSL matrix multiplication when short identifiers such as `a`, `b`, `p`, or `q` are reused with different scalar/vector types across functions.
- Matrix operand classification now prefers the nearest numeric declaration before the expression, preventing vector operands from being misidentified as unrelated scalars.
- Keeps the 0.18.50/0.18.51 `stpq` swizzle and comma-local initialization fixes intact.
- Adds regression case 70 for reused scalar/vector identifiers with an inline `mat2(...)` operand.

## 0.18.51 - Qt 6.11/MSVC compile fix

- Fixes a Qt 6.11 `qsizetype` to `int` narrowing error in the 0.18.50 GLSL stpq swizzle patch.
- Keeps the 0.18.50 final two seed-1337 corpus fixes unchanged.

## 0.18.50 - Final two seed-1337 corpus compile fixes

- Converts GLSL texture-coordinate swizzles (`.s/.t/.p/.q`, including multi-component `stpq`) to FXC-compatible `xyzw` only when the base identifier is already known to be a GLSL vector. This fixes the remaining `X3018: invalid subscript 's'` failure without blindly rewriting user-struct fields.
- Extends deterministic local initialization to comma declaration lists such as `vec3 d = ..., p, o;`, zero-initializing only declarators that have no initializer. This fixes the remaining `X4000: variable 'p' used without having been completely initialized` failure.
- The two exact shaders that exposed these bugs are already retained as full-source regression cases 63 and 64; focused cases 68 and 69 now isolate the swizzle-safety and comma-local rules as well.
- Intended verification target: rerun the same mixed Shaders21k sample (`250`, random seed `1337`) and reach `250 passed / 0 failed / 0 skipped` before starting render-regression checks against older previewer builds.

## 0.18.49 - Seven-failure mixed-corpus compatibility pass

- Adds TwiGL geek/geeker compatibility aliases for undeclared `r` (resolution), `t` (time), `f` (frame), `m` (mouse), `FC` (fragment coordinate), and `o` (output), activated only for TwiGL-like sources rather than ordinary Shadertoy code.
- Adds targeted pre-expansion for FXC-sensitive GLSL macro metaprogramming: top-level declaration-generating macros and higher-order function-like macros are expanded before GLSL-to-HLSL rewriting.
- Routes GLSL `int(...)` through a BO3 compatibility helper; invalid/undefined vector-to-scalar casts deterministically use the first component while valid scalar casts keep normal behavior.
- Lowers dynamic vector-component writes through wrapped-index helpers, giving out-of-range shader-golf component writes deterministic behavior instead of FXC X3504.
- Adds exact full-source regression coverage for all seven remaining 250/seed-1337 corpus failures (cases 61-67), so these real shaders stay protected after they compile on Windows FXC.
- TwiGL compatibility is conservative: aliases are not substituted when the same short name is explicitly declared as a variable, parameter, macro, or function.
- Wrapped dynamic vector writes ignore vector arrays, preventing `vec3 values[N]` element indexing from being mistaken for vector-component indexing.

## 0.18.48 - Import-once public corpus + persistent index reuse

- Fixes the public-corpus preparation bottleneck where selecting the same external `all_codes.zip` caused the app to re-extract the dataset and rebuild the ~20k-file candidate index on every run.
- Public Shaders21k is now **import once**: after a validated extraction exists in the app cache, ordinary corpus runs use it directly and no longer require selecting `all_codes.zip` again.
- The corpus dialog shows the cached extracted dataset and changes the button to **Replace ZIP...** only when a valid imported dataset already exists.
- Re-selecting the same external ZIP is cheap: a path/size/mtime provenance fingerprint recognizes that the archive is already imported and keeps the extraction/index intact.
- The existing `candidate_index_v1.txt` is reused across previewer builds as long as the cached dataset is unchanged, eliminating the multi-minute preparation scan seen with 250/250 validation-cache hits.
- Dataset validation is performed once and persisted with `dataset_valid_v1.ok`; it is repeated only after the corpus is replaced or an older unvalidated extraction is detected.
- Keeps the 60-case converter suite, cross-build HLSL/FXC result cache, 8-worker Auto validation, complete Copy All output, and pure-black default preview background.


## 0.18.46 - Cross-build corpus cache + FXC loop conflict fix

- Fixes real-corpus X3531 where a long non-texture loop with a dynamically indexed vector l-value was incorrectly marked `[loop]`, while FXC simultaneously required that loop to unroll.
- Long-loop `[loop]` / explicit-LOD fallback is now applied only when the converted shader actually contains implicit-derivative texture sampling.
- Adds regression case 60 for the real `fragColor[int(i) % 3]` long-loop pattern.
- Corpus validation cache v3 is keyed by the final generated HLSL plus the FXC/BO3 compatibility-header environment instead of the app version.
- Unchanged generated HLSL can therefore reuse successful FXC validation across future previewer builds; converter changes only recompile passes whose output actually changed.
- Bundled BO3 compatibility headers are content-hashed into the cache environment so header changes invalidate stale results safely.
- Retains 8-worker Auto parallel validation on Ryzen 7 5800X / 16 GB class systems and the pure-black default/reset preview background.

## 0.18.45 - Parallel corpus validation

- Public/no-key corpus validation now runs FXC work in parallel on background worker threads.
- Auto worker count detects physical CPU cores and respects available system memory; Ryzen 7 5800X / 16 GB defaults to 8 workers.
- Live progress now shows completed passes, active workers, cache hits, elapsed time, and ETA while keeping the UI responsive.
- Cancel stops scheduling new work immediately and waits only for active FXC jobs to finish.
- Added versioned successful-validation cache for fast repeat runs on the same build.
- Added worker-count selector, validation-cache toggle, and clear-cache button to the no-key corpus dialog.

## 0.18.44 - FXC empty macro argument expansion

- Fixes regression case 56: legacy FXC still rejects `D(GLSL_EMPTY_MACRO_ARG)` because the marker expands away before FXC validates the macro argument count.
- Empty calls to one-parameter GLSL macros are now expanded directly from the macro replacement list with the formal parameter removed, preserving the GLSL empty-token behavior without asking FXC to accept an empty actual parameter.
- Handles backslash-continued/multiline macro bodies used by real Shadertoy golf shaders.
- Adds regression case 59 for a multiline `D( )` macro, while retaining all previous converter, corpus, Copy All, and pure-black background fixes.


## 0.18.43 - 94.8% corpus cleanup: macro args, type shadows, golfed ternaries, long loops

- Based on the reproducible 250-shader / seed 1337 corpus run that reached 237/250 (94.8%).
- Handles GLSL locals that shadow their own struct type, e.g. `ray ray;`, by renaming only the variable side for FXC.
- Prevents macro statement fragments such as `r k;` from being misread as custom-type local declarations during deterministic zero-initialization.
- Normalizes empty arguments to parameterized GLSL macros (`D( )`) through an empty-token compatibility macro for legacy FXC.
- Lowers statement-level golfed comma/ternary expressions into ordinary `if/else` blocks where FXC rejects GLSL's compact comma-expression form.
- Detects obviously long GLSL loops and switches implicit texture sampling to explicit-LOD sampling plus `[loop]`, avoiding FXC derivative-driven X3511 unroll failures.
- Adds five permanent regression cases (54-58) for these converter fixes.
- Deliberately does not fabricate semantics for invalid/undefined source patterns such as indexing a `vec3` with literal loop values 5..7 or casting an entire `vec3` directly to `int`.
- Retains the pure-black default/reset preview background and all previous converter/corpus fixes.

## 0.18.42 - FXC literal struct arrays + complete Compiler Output copy

- Fixes regression case 48 by promoting top-level `const` values of user-defined struct types to `static const`, matching the existing BO3/FXC treatment for numeric GLSL constants.
- Global struct-array lowering now recursively converts nested GLSL vector constructors into literal aggregate components, so later vector-helper passes cannot introduce function calls into a compile-time initializer.
- `Compiler Output -> Copy All` now copies the complete stored report instead of reading back only the text retained by the visible `QPlainTextEdit`.
- Raises the visible Compiler Output block limit substantially so long corpus/regression reports are less likely to be visually truncated.
- Retains the pure-black default/reset preview background and all 0.18.40/0.18.41 corpus fixes.

## 0.18.41 - FXC-safe global struct-array literals

- Fixed regression case 48 / FXC X3011 by lowering global user-struct array constructors to literal aggregate braces instead of helper-function calls.
- Keeps nested user-struct constructors literal recursively while preserving numeric/vector constructors.
- Retains the 0.18.40 real-corpus fixes and pure-black default preview background.

## 0.18.40 - Real-corpus cleanup + black preview background

- Preview background now defaults/resets to pure black while keeping the existing neutral preview-light fallback, so changing the clear color does not unintentionally kill material ambient lighting.
- User-struct constructor factories are emitted immediately after each struct declaration, fixing early constructor calls before later struct declarations.
- Array constructors now support user-struct element types and unsized constructor spelling such as `Seg[](...)`.
- Matrix conversion now recognizes matrix-valued struct members and preserves scalar×matrix scaling in chains such as `0.3*M*vec3(...)`.
- HLSL-reserved `texture` variables are renamed even when declared in comma lists.
- Final non-empty `default:` switch arms get an FXC-required terminating `break`.
- Simple uninitialized locals are deterministically zero-initialized for FXC definite-assignment compatibility.
- `inout` parameters receive an explicit preserve/assign step for stricter FXC aggregate analysis, and classic `main()` conversion now receives output initialization too.
- Regression suite expanded to 53 focused cases.

## 0.18.39 - Regression cleanup + copyable output

- Fixed fallback-return generation writing literal `\n` tokens into converted HLSL.
- Prevented the fallback-return scanner from treating `#define` macro bodies as function signatures.
- Correctly adds a fallback for conditional-only returns such as `if (x > 0.0) return x;`.
- Recognizes `GLSL_INVERSE(matrix)` as matrix-valued so matrix/vector multiplication is converted through `mul()`.
- Added a visible **Copy All** button above Compiler Output so long regression/corpus reports can be copied reliably.
- Keeps the full 0.18.37/0.18.38 real-corpus converter fixes.

## 0.18.38 - Qt 6.11 / MSVC build fix

- Fixed `qsizetype` to `int` narrowing in the new GLSL function-shadow renamer.
- Fixed mixed `int` / `qsizetype` use in `std::max` while locating GLSL struct definitions.
- Keeps all 0.18.37 real-corpus converter fixes unchanged.


## 0.18.37 - Real-corpus converter fixes

Built from the 250-shader Shaders21k failure bundle. Adds FXC-safe GLSL struct constructors, reserved-identifier renaming, custom-struct mutable globals, prefix/symbolic array handling, matrix-returning macro tracking and matrix-chain fixes, const-parameter safety, BO3 header collision renaming, local/function shadow fixes, textureSize/inverse helpers, residual sampler parameter conversion, relaxed vector expansion helpers, and fallback returns for FXC definite-return analysis. The regression corpus now includes dedicated cases for these categories.



### 0.18.37 public-corpus archive validation

- Rejects the Shaders21k GitHub source-code ZIP as a corpus; public mode requires the separately published `all_codes.zip` shader dataset.
- Invalidates stale 0.18.31–0.18.33 app-managed corpus caches that extracted to `shaders21k-main`.
- Python/source/document files are never content-sniffed as GLSL shaders.
- Public corpus extraction must contain a real `.fragment` population before validation starts.
- Wrong-archive errors explicitly direct the user to the **Manual all_codes.zip** button.

### 0.18.33 corpus-layout fix
- No-key corpus validation no longer requires shader files to live under a folder literally named `shadertoy`.
- Public Shaders21k/Shaders20k, local ZIPs, and local folders now scan all supported fragment shader files recursively.
- This avoids false “No Shadertoy folder found” prompts when archive/mirror layouts flatten or rename directories.
- The smaller TwiGL subset is intentionally retained because it is useful real-world GLSL converter coverage too.




## 0.18.33 no-key public shader corpus testing

- Added **Tools → Shader Corpus Test (No API Key)...**.
- Built-in public source targets the Shaders21k `all_codes.zip` archive published by the Shaders21k project; its Shadertoy subset is the `shaders20k` source documented by the Shadertoys-dataset project.
- Supports three sources: public Shaders21k/Shaders20k, any local ZIP, or any local folder.
- Public archive is cached and extracted under the app-local data directory so repeated runs do not redownload/re-extract ~20k files.
- Automatic Google Drive download is attempted first; if Drive returns a confirmation page, the dialog provides a manual official-download-page fallback.
- Recursively recognizes `.fragment`, `.glsl`, `.frag`, `.fs`, and `.shader`.
- Public mode validates the actual shader archive before testing and rejects source-repository ZIPs.
- Deterministic random sampling lets you run 250/1000/5000 shader slices without always testing the same files.
- Every selected shader goes through the normal GLSL converter and legacy FXC validation, with grouped failures plus saved GLSL/HLSL/diagnostic triples.
- Official API corpus testing remains available separately as **Shadertoy API Corpus Test...**.
- Dataset source code keeps its original authors/licenses; the corpus mode is intended for local compiler validation rather than redistribution.

## 0.18.30 relational builtin coverage

- Converts GLSL component-wise relational builtins: `lessThan`, `lessThanEqual`, `greaterThan`, `greaterThanEqual`, `equal`, `notEqual`, and boolean-vector `not`.
- Keeps GLSL relational results as `boolN` instead of accidentally reducing vector equality to a scalar.
- Expands the generated stress tester from 16 to 22 construct families so all relational builtins are exercised automatically.
- Adds permanent regression cases for the relational family.

## 0.18.29 converter regression fixes

- Fixed mutable GLSL globals in minified/same-line source (for example `float g=0.; float f(...) {...}`) so they are promoted to FXC-safe `static` storage.
- Fixed Shadertoy built-in uniform and `iChannel0..3` declarations when they share a line with functions; wrapper-provided resources are now removed regardless of physical line layout.
- These changes target the regression-suite failures exposed by the new 0.18.27 automated corpus.





## 0.18.29

- Added **Tools -> Run GLSL Converter Regression Suite**. It converts and FXC-compiles a bundled corpus of small GLSL regression cases so converter changes can be validated systematically instead of by trying random shaders one at a time.
- Added **Tools -> Batch Validate GLSL Folder...** to recursively convert + FXC-compile a folder of `.glsl`, `.frag`, `.fs`, and `.shader` files as BO3 PostFX and report every failure in one run.
- PostFX conversion now automatically runs **FXC validation immediately after conversion**, so broken generated HLSL is reported before it is opened in the editor or exported.
- Added `tests/glsl_converter/` with focused cases for intrinsic-name collisions, Shadertoy built-in shadowing, scalar/vector constructors, arrays, matrices, out parameters, texture calls, `gl_FragCoord`, vector equality, global state, and other previously fragile conversion paths.
- New rule for converter development: every newly discovered conversion bug should be reduced to a small regression case and kept in the corpus permanently.

## 0.18.26

- Fixed GLSL → HLSL conversion when a GLSL user symbol collides with the HLSL spelling of a translated intrinsic.
- Example fixed: `vec2 frac = fract(uv);` no longer becomes the invalid FXC expression `float2 frac = frac(uv);`; the user variable is renamed before `fract` is translated to `frac`.
- Added collision handling for `frac`, `lerp`, `rsqrt`, `ddx`, `ddy`, `asuint`, `asint`, `asfloat`, `atan2`, and generated matrix `mul` calls.

## 0.18.25
- Removed the **PostFX Test Lab** UI, generated HDR test scenes, animation controls, bypass controls, and bundled auto-exposure test shaders.
- Keeps the normal PostFX preview, Shader Inputs, GLSL/Shadertoy conversion, Material/Surface workflow, performance tools, multipass detection, and BO3 export workflow.
- Keeps the BO3 runtime compatibility guard for shaders that depend on preview-only persistent temporal feedback, so unsupported history resources are still called out instead of being presented as BO3-ready.

## 0.18.18 Material / Surface conversion + procedural grass

- Adds **BO3 Material / Surface** as a GLSL converter target alongside PostFX and Sky.
- Material conversion evaluates `mainImage()` across real mesh UVs and automatically switches the editor to the 3D Forward/Card preview when opened.
- Adds Opaque, Alpha Cutout / Grass, and Transparent material wrapper choices. Converted shaders carry a recommendation marker so **Export to BO3** opens on the matching Material surface preset.
- Preview-only cutout is disabled during Custom HLSL Material export; BO3's adjustable **Alpha Cutoff** control performs the runtime clip instead.
- Adds a vertical **Card** 3D preview mesh for foliage/cutout materials. For two-sided runtime foliage, use crossed cards or duplicate/reverse the card faces in the model.
- The bundled grass sample uses procedural alpha, color variation, world-phased wind, and no texture inputs. Export it as **Material → Custom HLSL Material → Alpha Cutout (shader alpha)**.
- Retains 0.18.17 `gl_FragCoord` helper-function support and all earlier converter fixes.

## 0.18.17 GLSL gl_FragCoord helper-function support

- Fixes GLSL `gl_FragCoord` references inside helper functions such as `dither()`.
- Maps `gl_FragCoord` to a shader-private `GLSL_FRAGCOORD` value instead of an out-of-scope local `fragCoord`.
- Initializes `GLSL_FRAGCOORD` from the actual per-pixel coordinate in both PostFX and Sky wrappers before `mainImage()` runs.
- Retains 0.18.16 Shadertoy texture-channel preview support and all earlier converter fixes.

## 0.18.16 Shadertoy texture-channel preview

- Adds dedicated `iChannel0`..`iChannel3` texture inputs for converted Shadertoy/GLSL PostFX and Sky shaders.
- Stops silently mapping `iChannel0` to the BO3 framebuffer; Shadertoy channels now bind separately at `t2`..`t5` while `t0` remains the BO3 framebuffer and `t1` remains depth.
- Adds per-channel Repeat/Clamp sampling and Flip Y controls in **Material Textures → Shadertoy / GLSL Texture Inputs**.
- Generates a full mip chain for loaded Shadertoy textures so `textureLod()` and minified sampling match Shadertoy much more closely.
- Routes converted `texture`, `textureLod`, texture bias, and gradient calls through per-channel samplers instead of one global clamp sampler.
- Converter notes now tell you which `iChannel` inputs were detected and where to load them.
- Retains the 0.18.14 nested-constructor fix and all earlier GLSL/FXC compatibility work.


## 0.18.14 nested one-argument constructor recursion

- Fixes same-type nested GLSL scalar-splat constructors that the forward-only pass could skip, such as `vec3(dot(col, vec3(0.33)))`.
- Re-scans the replacement site after converting a one-argument vector constructor, so inner `vec2`/`vec3`/`vec4` and integer/uint/bool variants are converted too.
- Applies the same recursion rule to one-argument square matrix constructors.
- Retains the 0.18.13 FXC `out`-parameter initialization fix and all earlier matrix, array, global, and Shadertoy compatibility fixes.


## 0.18.13 BO3/FXC out-parameter compatibility

- Zero-initializes non-array GLSL `out` parameters at function entry so legacy FXC does not emit `X3508: output parameter not completely initialized`.
- Covers structs, scalars, vectors, and matrices; authored assignments still overwrite the zero defaults normally.
- Retains the 0.18.12 built-in-shadowing fix and all earlier constructor, array, and matrix conversion fixes.


## 0.18.12 GLSL / Shadertoy conversion fixes

- Renames function-local variables that shadow Shadertoy built-ins such as `iResolution`, preventing wrapper macro expansion from corrupting declarations.
- Preserves initializer references to the original Shadertoy built-in (for example `vec2 iResolution = iResolution.xy`).
- Keeps the rename scoped to the declaring block so unrelated functions and globals are unchanged.


Qt 6 Widgets frontend + native Direct3D 11 HLSL preview backend for Black Ops III shader work.



## 0.18.12 GLSL array constructors + nested matrix recursion

- Converts local GLSL array constructors such as `float t[2] = float[](a, b);` into FXC-safe array declarations plus indexed assignments.
- Revisits matrix multiplication after each rewrite so nested products such as `WorldToLocal * normalize((InverseCam * vec4(...)).xyz)` are fully converted instead of leaving an inner raw matrix `*`.
- Keeps the 0.18.10 scope-aware constructor inference and all earlier BO3/FXC compatibility fixes.

## 0.18.10 scope-aware vector constructor inference

- Fixes nested casts such as `uvec3(ivec3(p))` when short variable names are reused with different vector widths in other functions.
- Resolves one-argument constructor source widths from the nearest preceding declaration instead of one shader-wide name map.
- Recognizes already-rewritten deterministic `GLSL_*VECn_*` helper calls as vector-valued during nested constructor conversion.
- Prevents bogus helper names such as `GLSL_IVEC3_V2` from being generated for a `vec3` parameter that was shadowed by a later `vec2 p`.
- Keeps the 0.18.9 deterministic, non-overloaded FXC-safe constructor helpers.

## 0.18.9 FXC-safe deterministic vector constructors

- Replaces overloaded `GLSL_VEC*`/`GLSL_IVEC*`/`GLSL_UVEC*` constructor helpers with uniquely named scalar and source-width helpers.
- Fixes FXC `X3067: ambiguous function call` for literals such as `vec3(0)` even when int/float overloads are both present.
- Infers whether a one-argument GLSL vector constructor receives a scalar, vector, swizzle, constructor, function result, or arithmetic expression, then emits one deterministic helper call.
- Keeps the 0.18.7/0.18.8 matrix, non-square matrix, constant, Shadertoy, and numeric-conversion fixes.


## 0.18.8 GLSL constructor overload disambiguation

- Adds exact scalar `float`/`int`/`uint` overloads for the GLSL `vec2`/`vec3`/`vec4` compatibility helpers.
- Adds cross-type scalar overloads for `ivec2`/`ivec3`/`ivec4` and `uvec2`/`uvec3`/`uvec4`.
- Fixes FXC X3067 ambiguity for constructors such as `vec3(0)` after GLSL-to-HLSL conversion.
- Keeps the 0.18.7 advanced matrix conversion and Shadertoy compatibility fixes.

## 0.18.7 GLSL advanced matrix conversion

- Reworked matrix multiplication conversion to detect matrix-valued operands instead of only named matrix variables.
- Handles inline constructors such as `p *= mat3(...)`, `v * mat2(...)`, and matrix expressions wrapped in `transpose(...)`.
- Adds GLSL non-square matrix translation for `mat2x3`/`mat3x2`/`mat4x2`/`mat4x3` and the other 2-4 dimensional combinations.
- Adds BO3-safe one-argument `mat2`/`mat3`/`mat4` helpers for scalar diagonal construction and matrix size conversion such as `mat4(mat3Value)`.
- Promotes every top-level `const` on a line to `static const`, not only the first declaration.
- Maps Shadertoy `iChannelResolution[index]` to the BO3 preview target dimensions.
- Preserves GLSL vector `==`/`!=` scalar-boolean semantics with HLSL `all()`/`any()` and adds cross-type vector constructor helpers used by uint/int-heavy shaders.


## 0.18.6 GLSL mutable-global conversion

- GLSL shader-private globals such as `vec2 g_pen;` now become `static float2 g_pen;` in HLSL.
- Initialized non-const globals are also internalized, avoiding FXC external/uniform constant behavior.
- Function prototypes, uniforms, varyings, samplers/resources, and `static const` values are left alone.
- Fixes FXC X3025 when converted GLSL helper functions write to global state.

## 0.18.5 GLSL matrix compound/function-call conversion

- Converts GLSL vector `*= matN` operations to BO3/FXC-safe `mul()` assignments.
- Matrix-returning calls such as `normalize(dir) * fromEuler(ang)` now keep their full argument list.
- Retains the existing GLSL/HLSL transpose compensation used by matrix conversion.

## 0.18.4 GLSL global constants + cleaner conversion

- Converts top-level GLSL `const` declarations to BO3/FXC-safe `static const`, preventing unbound constants from rendering black.
- Compacts blank lines left after GLSL comment stripping.
- Retains the 0.18.3 expression-aware matrix multiplication fixes.

## 0.18.3 GLSL matrix-expression parser + cleaner conversion

- Fixed matrix multiplication conversion when either operand is a constructor or nested expression (for example `viewMat * vec3(uv, -1.0)`).
- Matrix conversion now preserves full constructors/calls/swizzles/indexing instead of matching only the function name.
- Imported GLSL comments and commented-out code are stripped from generated HLSL to keep converted shaders cleaner.
- Keeps the BO3-safe scalar vector-constructor compatibility added in 0.18.2.

## 0.18.2 GLSL vector-constructor compatibility

- Fixes BO3/FXC `X3014: incorrect number of arguments to numeric-type constructor` from GLSL scalar splats such as `vec3(0.0)` and `vec4(1.0)`.
- One-argument GLSL vector constructors now use compatibility helpers that preserve scalar splat and vector copy/truncation behavior.
- Prevents simplex-noise/Shadertoy shaders from failing on expressions such as `step(h, vec4(0.0))`, `vec3(0.0)`, and similar constructors.


## 0.18.1 GLSL / Shadertoy conversion fixes

- Preserves common GLSL matrix multiplication semantics when translating `mat2`/`mat3`/`mat4` to HLSL.
- Uses BO3 `GetTime()` and `PostFx_GetRenderTargetSize()` helpers for generated PostFX wrappers.
- Converts ShaderToy lower-left fragment coordinates to Direct3D/BO3 upper-left pixel coordinates.
- Guards `M_PI` to avoid collisions with BO3 shader headers.
- Improves matrix-conversion warnings for complex expressions.


## 0.18.0 custom material surface export

Custom HLSL Material export now has BO3 surface presets for Opaque Lit, Emissive/Unlit, Alpha Blend, Additive, Alpha Cutout, Decal Blend, Decal Additive and Glass-like Transparent. The exporter also exposes Output/Emissive Scale, Opacity and Alpha Cutoff.

Opaque/deferred materials no longer redeclare the base `colorSampler` / `colorMap` resources that BO3 already supplies through `lit_base_mid`, removing the duplicate `color_base.techsetdef` warnings seen in APE. Decal presets use the repository-style `decal` techset folder and `+ decal` render states. The Glass-like preset is transparent surface behavior plus `surfaceType=glass`; it does not synthesize a refraction shader.


## 0.9.5 integrated 3D material workflow

This build combines the 0.9.4 3D scene/material UI with the vertex-displacement branch.

- The Preview pane can now be collapsed much farther, and splitter movement avoids rebuilding Direct3D render targets for every pixel of drag movement.
- Forward Material / Deferred GBuffer use real Sphere, Cube and Plane meshes.
- A file with `vs_main()` and no `ps_main()` is automatically treated as a vertex-displacement shader and compiled as `vs_5_0`; the previewer supplies its own material/GBuffer pixel shader.
- Plane: 64×64 subdivisions. Cube: 32×32 subdivisions per face.
- Material textures are bound to both VS and PS for vertex-only shaders.
- Material texture rows accept direct drag-and-drop for Color, Normal, Height/POM, Specular, Gloss/Roughness, AO, Emissive and Opacity/Mask.
- Right-drag in the 3D preview moves the light; the Scene tab exposes light yaw/pitch, intensity, ambient and shadow-strength controls.
- Middle-drag or Shift+Left-drag pans the 3D view.

A ready-to-test vertex-only shader is included at `shaders/sample_vertex_displacement_vs.hlsl`.


## Build once, update from inside the app afterward

The first Qt build still needs:

1. Visual Studio with **Desktop development with C++**.
2. Qt 6 **MSVC 2022 64-bit**.
3. Run `build_qt.bat`.

CMake is not required. The finished runtime is placed in `dist` and `windeployqt` copies the required Qt DLLs.

Starting with this version, future updates can be delivered from **GitHub Releases** directly inside the application. The Help menu now contains:

- **Check for Updates...** — query the configured GitHub Releases feed now.
- **Update Channel → Stable / Tester** — Stable ignores prereleases; Tester accepts the newest compatible stable or prerelease build.
- **Automatically Check for Updates** — enabled by default and checked shortly after startup.
- **Install Update File...** — keeps the existing offline/manual ZIP updater available.

When an online update is accepted, BO3 Shader Studio downloads the release asset, verifies its GitHub-provided SHA-256 digest, closes itself, validates `update_manifest.json`, replaces the update payload, deletes the temporary download, and reopens automatically. Tester distributions built by the release workflow default to the Tester channel; stable distributions default to Stable. A user's explicit channel choice is remembered across updates.

The repository includes `.github/workflows/release.yml`, `tools/ci_build_release.cmd`, and `tools/package_github_release.ps1`. **Every relevant push to `main` automatically creates a Tester update**: GitHub restores Qt, builds through the sccache + no-batch + jom fast path, runs only the regression suites selected for the changed subsystem (with the large GLSL corpus sharded across worker processes), creates the appropriate lean/full update payload, and publishes a prerelease. Manual releases keep the conservative full validation/package path. The visible product version comes from `version.json` (`displayVersion`, such as `0.1`), while an internal GitHub run number orders builds without exposing noisy build numbers in the application.

Manual **Run workflow** is only needed when intentionally publishing a Stable build or overriding the visible version/release notes. To move the public product from `0.1` to `0.2`, update `displayVersion` in `version.json` and push normally.

For anonymous in-app update checks, releases must be readable without a GitHub login. Use a public project repository or a separate public release repository; do not embed a personal access token in the application.

An update ZIP can still be dragged onto the window. Native-code updates can replace the EXE after it exits, while data-only updates can contain only the files that changed. Testers should not need qmake/nmake, Qt, or Visual Studio after installing a GitHub-built distribution.

The package format is documented in `updates/UPDATE_PACKAGE_FORMAT.md`.

## External/updateable runtime data

Frequently changed data now lives outside the EXE:

- `bo3_compat/` — BO3 include/header compatibility tree
- `presets/bo3_preview_defaults.json` — neutral script-vector and camera defaults
- `ui/theme.qss` — Qt dark theme
- `shaders/` — bundled samples
- `version.json` — installed version metadata

Those can be updated without recompiling the application.

## 3D / sky navigation

The Preview tab now has **3D look** controls.

Directional BO3 sky shaders are detected automatically and 3D navigation is enabled for them by default. The generated sky vertex stage now receives a real camera basis rather than one fixed direction.

Controls:

- **Left-drag in Preview** — look around
- **Mouse wheel** — change field of view
- **Double-click Preview** — reset camera
- **R while the Preview has focus** — reset camera
- **Reset Camera** button — reset camera

The previewer also feeds the camera into common BO3 fields when present:

- `viewMatrix`
- `projectionMatrix`
- `viewProjectionMatrix`
- inverse versions of those matrices
- `cameraLook`
- `cameraSide`
- `cameraUp`
- `eyeOffset` / `cameraPosition`

You can manually enable **3D look** on a non-sky shader if it uses those camera constants. This does not yet turn arbitrary BO3 material pixel shaders into a complete mesh/material pipeline; a dedicated mesh preview mode is still a separate feature.

## Editor / UI

- Qt dark frontend
- VS Code Dark+-inspired HLSL highlighting
- line numbers and current-line highlight
- native Qt undo/redo (`Ctrl+Z`, `Ctrl+Shift+Z`, `Ctrl+Y`)
- responsive high-resolution wheel/trackpad scrolling
- `Ctrl+F`, `F3`, `Shift+F3`
- drag/drop HLSL files
- drop folder = Include Root
- drop image = source texture `t0`
- drop update ZIP = Install Update
- live compile and file watching
- Preview / Script Vectors / Performance tabs

## FPS and BO3 performance estimate

The Preview tab now shows a live **FPS** counter and a measured **GPU pass time**. The GPU measurement uses Direct3D 11 timestamp queries around the pixel-shader draw, so v-sync / `Present` time is not counted as shader cost.

The **Performance** tab shows:

- current Direct3D GPU
- live preview FPS
- measured pixel-shader pass time in milliseconds
- preview pixel resolution
- estimated cost at 720p / 1080p / 1440p / 4K
- selectable target frame rate (30–240 FPS)
- percent of the selected frame budget consumed by this pass
- reflected instruction count
- reflected texture-instruction count
- temporary register count
- dynamic flow-control count
- an `EXCELLENT / GOOD / MODERATE / HEAVY / VERY HEAVY` assessment

The estimate scales the measured GPU pass by shaded pixel count. It is deliberately labeled as an estimate: total BO3 performance also depends on the map, geometry, lighting, shadows, CPU load, other post effects, render scale, and where/how often the shader is used.

## Auto BO3 globals startup fix

The previewer now resolves missing preview globals iteratively. D3DCompile may report only one undeclared identifier at a time; a shader using both `gameTime` and `renderTargetSize` could previously get `gameTime` injected on the first retry and then fail on `renderTargetSize` on the second compile. The previewer now keeps retrying known BO3 preview globals until all compiler-requested helpers are present. Existing engine cbuffers are still left untouched.

## Script Vectors

Shader reflection detects `scriptVector0` through `scriptVector7`. The Script Vectors tab enables only the vectors found in the compiled shader and changes values live without recompilation.

Neutral defaults are loaded from `presets/bo3_preview_defaults.json`. The current Killfeed-friendly default keeps:

`scriptVector6 = (1, 0, 1, 0)`

so brightness and saturation start neutral instead of dark/grayscale.

## BO3 include compatibility

The resolver checks:

1. relative to the header that issued the nested `#include`,
2. the opened shader folder,
3. the selected Include Root,
4. nearby `include`, `includes`, `lib`, `common`, `shaders`, and `shaders_stable` folders,
5. bundled `bo3_compat/shaders_stable` fallbacks.

The supplied BO3 shader repositories were used to expand compatibility for common BO3 include layouts and high-numbered engine texture bindings. Reflected texture resources can receive neutral fallbacks through `t127`, while `t0` remains source image and `t1` remains depth for post-FX previewing.

## Known limitations

This is a BO3-oriented preview environment, not the complete BO3 renderer. Engine resources that materially affect a shader still need dedicated emulation or controls. Arbitrary geometry/material shaders that depend on BO3's complete vertex/material pipeline will eventually benefit from a dedicated mesh preview mode.

## BO3 Exporter (0.11.1)
Use **File > Export to BO3...** to generate a BO3-ready folder tree. The exporter shows only the options relevant to PostFX, Material, or Sky / Environment. Sky export follows the BO3 skybox setup used by the Aurora example: it bakes an HDR reflection EXR, creates the procedural sky material, creates an SSI, and creates an xmodel using `t6_props\vista\skybox\t6_skybox.xmodel_bin` with `mtl_skybox_default` overridden by the exported sky material. Folder/namespace and asset names remain editable.

For PostFX, the exporter no longer generates a shader-specific auto-start CSC or `.zpkg`. Every export writes `source_data/<namespace>/<base>_POSTFX_INTEGRATION.txt` with the exact code to merge into the usermap/mod client CSC, including the local-player spawn callback, 3-second readiness delay, filter/pass creation, and `filters::enable_filter(...)`. The generated zone block is only `include,filters` plus `material,<exported material>`. **Include shared _filters support files (first install only)** remains available for the shared `_filters.csc`, `_filters.gsh`, and `filters.zpkg` dependency; it defaults off so repeated exports do not replace an existing/customized `_filters` copy.

## BO3 Sky Export 0.12.0

Sky / Environment export now mirrors the proven Aurora Borealis package layout. The custom HLSL is written only to `share/raw/shaders_stable/geometry/`. The runtime techset is the Aurora runtime techset with only the custom pixel-shader source path replaced. The TOOLSGFX techset stays on Treyarch's stock `ps_sky`, so APE/SSI/reflection tooling never compiles the custom procedural shader. Sky techsets live in `techsetdefs_stable/geometry` and `techsetdefs_stable_toolsgfx/geometry`; GDT/EXR assets still use the selected namespace such as `_custom`.

## 0.13.0 - Share Packages, Easy Shader Values, UV Tiling

### Shareable BO3 ZIP
After a successful BO3 export, the completion dialog now includes **Create Shareable ZIP...**.
The previewer collects only the files generated for that export (shader, copied includes, runtime/toolsgfx techsets, GDT, copied source images, PostFX integration instructions/optional shared filter support, and sky EXR when applicable), preserves their BO3-root-relative folder structure, and creates a ZIP that can be extracted directly into another Black Ops III root. Every shareable ZIP now includes a root-level `00_README_FIRST.txt` with folder-copy, Mod Tools restart, APE, and zone/include instructions.

### Shader Values
A new dockable **Shader Values** tab scans the current HLSL for simple numeric `static const` declarations and numeric `#define` values. It creates readable controls for scalar values, booleans, and float2/float3/float4 vectors. Changes edit the real HLSL through the editor so normal Undo/Redo and Live Compile continue to work.

Descriptions prefer nearby `//` comments from the shader itself. When no useful comment is available, the previewer generates a plain-English explanation from names such as `SPEED`, `DENSITY`, `COVERAGE`, `STEPS`, `BRIGHTNESS`, `RADIUS`, and `COLOR`. A filter box makes large shaders easier to navigate.

### Geometry UV Tiling
The **Material Textures** panel now includes Geometry UV Tiling controls. U and V can be adjusted separately or locked together. `1.0` is the original UV size, `2.0` tiles twice, and `0.5` makes the texture appear twice as large on the built-in Sphere/Cube/Plane material preview. Values are remembered between launches.

## 0.14.0 - Package First, Install Second

BO3 export is now non-destructive by default. The exporter builds the complete BO3-root-shaped file tree in a temporary staging area and immediately asks where to save the shareable ZIP. Your local Black Ops III folder is not modified while the package is being built.

After the ZIP exists, the completion dialog offers **Install into BO3...**. Choosing it copies the exact files from the already-created package into the selected BO3 root. Choosing OK instead leaves the local game untouched, which makes it easy to build shaders for someone else.

The BO3 install-root field in the export dialog is now optional and is used only as a convenience if you later choose to install the package locally.

The neutral exporter defaults are `_custom` for the folder/namespace and `custom` for the asset prefix. Legacy non-neutral saved defaults are migrated back to those values so generated asset names do not unexpectedly inherit an old developer prefix.

The export dialog now has **Apply to all names** beside Asset prefix. Enter a root such as `saint` and apply it once to regenerate the related namespace/techset/material/PostFX-bundle names (`_saint`, `saint_effect`, `mtl_saint_effect`, `postfx_saint_effect`) while leaving the Base name independently editable.


## 0.14.1 - EXR-driven SSI exposure

Sky export analyzes the baked HDR EXR and writes SSI `evcmp`, `evmin`, and `evmax` values from the useful luminance distribution. The same EXR analysis also supplies the SSI sun color. This keeps the Aurora-style sky package layout while adapting exposure to bright daytime skies, dark skies, and different HDR ranges.


## 0.15.1 - Directional UV Preview Fix

- Sky/environment pixel shaders now use real 3D direction vectors on Sphere/Cube/Plane previews instead of interpreting mesh UVs as XYZ directions.
- Sky shaders forced into PostFX preview now keep the directional camera vertex path, preventing the top-left radial/streaked projection.
- Geometry UV tiling now uses a WRAP sampler so values above 1.0 genuinely tile instead of clamping to edge texels.
- Plane V orientation now matches the sphere/cube material preview convention.

## 0.15.0 - Separate Package/Install + Reset Names + GLSL Converter

**Build Shareable Package** and **Install into BO3** are now separate actions in **File > Export to BO3...**. Package mode creates a ZIP and never installs it. Install mode stages the export safely and copies it directly into the selected BO3 root without creating a ZIP.

The export dialog now has **Reset Naming Defaults**. It restores `_custom` / `custom` and regenerates clean, type-aware asset names. Untitled exports use `effect` for PostFX, `surface` for materials, and `sky` for sky/environment shaders, producing names such as `custom_effect`, `mtl_custom_effect`, and `postfx_custom_effect` instead of redundant forms like `custom_custom_material`. If the shader base name already begins with the asset prefix, the prefix is not added a second time.

PostFX export also has **Remove black background (overlay effect on game scene)**. This is an explicit export-time transform for procedural effects that paint black behind the useful pixels: the exporter preserves the authored `ps_main`, adds a `resolvedScene` input, and additively composites the shader RGB over the game image before BO3's normal PostFX color-domain output bridge. Pure black therefore contributes zero instead of covering the screen. It does not edit the shader open in the editor and it does not rely on material alpha blending.

A new **Tools > GLSL → BO3 HLSL Converter...** converts common fragment GLSL/Shadertoy code. It handles common GLSL vector/matrix types and functions, `texture()`/`texelFetch()`, GLSL `mod()`, two-argument `atan()`, Shadertoy uniforms such as `iTime`/`iResolution`, and either `mainImage()` or a normal fragment `main()`. Output targets are:

- **BO3 PostFX / Shadertoy** — adds the BO3 fullscreen vertex/pixel entry points and PostFX globals.
- **BO3 Sky / Environment** — exposes **Sky source: Auto Detect / 2D Image-Space / 360° Self-Camera**. Image-space sources use a seamless direction-space triplanar wrap; self-camera raymarchers have their authored view ray replaced by BO3 `skyDirection` so an already-360° environment is not projected a second time.
- **HLSL syntax only** — performs syntax/function translation without imposing a BO3 entry layout.

The converter has side-by-side GLSL/HLSL editors, Load, Convert, Save HLSL, and **Open Converted in Editor** so the result can immediately use Live Compile, Shader Values, preview controls, and the normal BO3 exporter.


## 0.16.0 standard material export

Material export now uses the known-good `lit_advanced_fullspec_pom` BO3 geometry pipeline instead of generating a guessed arbitrary-HLSL material techset. It exports loaded Material Textures as BO3 image assets, writes a texture-neutral material GDT, ships `gbuffer_lit_pom.hlsl` to runtime and TOOLSGFX, auto-enables POM when a height map is present, and can invert a roughness map into a BO3 gloss map during export. Emissive and opacity/mask are intentionally left out of Standard Material export until a verified BO3 pipeline is added for them.


## 0.17.1 custom HLSL material export

Material export now defaults to **Custom HLSL Material (current shader)**. It adapts a `float4 ps_main` into BO3's deferred GBuffer geometry path using the same stock transform/skin/shadow structure used by the repository's working custom geometry examples. `PixelShaderInput` fields such as position, UV, sky direction, normal, tangent, object/world position, and view direction are populated automatically. Procedural-only materials do not generate fake image assets. Height remains available in the previewer and in the optional custom POM example path, but POM is explicitly treated as a custom shader rather than a stock BO3 height-map feature.


## 0.18.16 missing Shadertoy input warnings
- Makes missing Shadertoy iChannel texture inputs impossible to miss: required empty channels are highlighted, compile output explains the problem, and the Material Textures dock opens automatically.
- Adds a Shader Inputs toolbar shortcut.
- Clarifies that GLSL code does not contain the external image bytes; exact Shadertoy channel textures and sampler settings still have to be supplied.


## 0.18.21
- Fixed Material / Surface shaders being falsely detected as directional sky shaders, which collapsed Card V coordinates to 0 and made alpha-cutout grass render as a solid root-colored quad.
- Tightened procedural grass blades/root coverage so the card reads as individual grass blades instead of a dense wall.

### 0.18.23 - Temporal auto-exposure preview
- Detects one-file PostFX shaders with a `ps_exposure_state` entry point.
- Compiles and executes a persistent 1x1 RGBA32F ping-pong exposure-history pass before `ps_main`.
- Binds the current exposure state to `exposureHistory` at `t1`, enabling real previous-frame eye/auto-iris adaptation in the previewer.
- Bundled debug shader shows a green current-exposure bar and red target-exposure marker for easy verification.


## GLSL converter stress testing (0.18.29)

`Tools -> Stress Test GLSL Converter...` generates deterministic valid GLSL cases from multiple syntax families, converts each case to BO3 PostFX HLSL, and compiles the generated shader through the same FXC/D3DCompile path used by the regression suite. Choose the number of cases and a seed; failed GLSL, generated HLSL, and FXC diagnostics can be saved automatically under `Documents/BO3 HLSL Previewer/Stress Failures`.

This complements the permanent `tests/glsl_converter` suite: stress failures should be reduced to small permanent regression cases after converter fixes.

## Shadertoy corpus validation (0.18.29)

`Tools -> Shadertoy Corpus Test...` talks directly to Shadertoy's official API from Qt. Python and the third-party `shadertoy-api` package are **not required**. Enter your own Shadertoy API key (created from Shadertoy's My Apps page), choose keywords/sort/filter/count, and run the corpus test.

The validator:

- fetches only shaders exposed by Shadertoy as Public+API;
- prepends a shader's `Common` code to each tested render pass;
- validates `Image`/`Buffer`-style passes that use `mainImage()`;
- uses render-pass input metadata to treat cubemap iChannels as `TextureCube` during FXC validation where possible;
- groups FXC failures by error so converter bugs can be fixed by category;
- optionally saves failed GLSL/HLSL/diagnostics pairs under `Documents/BO3 HLSL Previewer/Shadertoy Corpus Failures`.

The corpus tester is a converter/compile validator, not a claim that every Shadertoy render graph can already execute or export in BO3. Multipass feedback, keyboard/audio/video/webcam inputs, and some non-`mainImage` pass types still require dedicated runtime support.

## 0.18.37 - Corpus failure bundle

- No-key corpus validation now automatically packages failures into a single ZIP when **Save failed GLSL/HLSL pairs + diagnostics** is enabled.
- The bundle contains the original GLSL pass, generated HLSL, FXC diagnostics, `_manifest.tsv`, and `_corpus_report.txt`.
- The completion dialog prints the exact ZIP path so a failing corpus run can be shared as one artifact instead of copying individual compiler errors.
