# Package workflow and live capture

## Preview policy

The top-level modes now have deliberately different meanings:

- **HLSL** is a generic Direct3D preview. It builds a temporary compatibility
  harness in memory and can supply synthetic Previewer-only `gameTime` and
  `renderTargetSize` values. No physical techset is required or created, and BO3
  Package Validation is reported as `N/A`.
- **PostFX**, **Material**, and **Skybox** are strict package modes. With no
  authored same-basename techset, they run the target adapter immediately and
  build a structured temporary package in memory. `AUTO` packages are compiled,
  reflected, and validated by the same BO3 validator used for persistent output;
  `GUIDED` and `UNSUPPORTED` states suppress preview rather than guessing.
- An authored, imported, or persistent same-basename techset remains
  authoritative in strict modes. Changing shader, source interface, target,
  Runtime/TOOLSGFX configuration, or a guided mapping rebuilds the temporary
  session so it cannot leak across shaders or targets.

This separation is intentional: a useful generic HLSL preview is not evidence
that BO3 will accept the package.

## Preview As and persistent packages

The shader toolbar's **Preview As** selector is non-destructive. Switching among
HLSL, PostFX, Material, and Skybox recompiles the current editor source through
the corresponding live adapter even when Live editing is disabled. Temporary
adapted HLSL and techset text are never written merely by previewing.

When a temporary strict package reaches `BO3 Package Validation: PASS` or a
non-fatal `WARNING`, **Package / Save > Save Current Preview Package As...** writes
that exact adapted HLSL and structured techset, changing only destination source
references. `FAIL` and `UNKNOWN` remain blocked. **Review Preview Mappings...** can
inspect both AUTO and GUIDED resource decisions; any override immediately
rebuilds/revalidates the same live session.

For converted Shadertoy PostFX, the live adapter now removes several setup steps.
Generated `glslSampler*` parameters are normalized to a BO3-known sampler automatically, `iChannel0` is the
primary `resolvedScene` candidate, distinct auxiliary images remain APE-visible
material images, and an auxiliary channel loaded from the same file as the scene
is promoted to `resolvedScene`. Loading iChannel0 can also establish the PostFX t0
source automatically. If a Texture/Image is chosen in the converter itself, **Open
Current in Editor** carries that asset into Shader Inputs instead of requiring it
to be loaded a second time.

Temporary PostFX techsets are pruned against optimized `ps_5_0` reflection before
validation and persistence. A source declaration that is commented out or removed
by FXC therefore cannot leave a stale Runtime `CodeTexture` binding behind. For
converted Shadertoy scene channels, the adapter normalizes only inputs mapped to
`resolvedScene` and restores BO3's PostFX scale at final output; ordinary material
images such as noise/LUT/mask textures stay in their authored 0..1 domain.

The preview toolbar also exposes **Remove Shader Camera / Input Movement**. It is a
source transform, not a renderer-wide pause. The original editor/file source is
left untouched; the temporary/adapted shader copy is conservatively rewritten to
remove recognized time/input-driven updates to fullscreen coordinates and common
camera-like variables. Unrelated `iTime` animation remains live. Uncertain direct
camera assignments are preserved with a warning rather than guessed away. Saving
the current preview package serializes the exact transformed adapted HLSL.

For converted Shadertoy PostFX, any channel mapped to `resolvedScene` also gets the
render target's dimensions for `textureSize` compatibility. This prevents a local
PNG/JPEG used during preview from introducing fit/center zoom, crop, stretch or
offset once that channel becomes BO3's fullscreen scene. Material-image channels
keep their native image dimensions, and the authored GLSL-to-D3D vertical-origin
correction is preserved rather than flipped a second time.

## Package Shader As

`Tools > Package Shader As...` and the compact `Package / Save` toolbar menu share
the package adapter framework. The adapters return one of:

- `AUTO`: every required mapping is supported by a proven package pattern.
- `GUIDED`: an ambiguous resource must be mapped explicitly.
- `UNSUPPORTED`: the transformation cannot be made without changing shader
  semantics or relying on unproven BO3 behavior.

The explicit Package Shader As command writes a protected package copy such as
`effect_bo3_postfx.hlsl`. Its techset is written beside it with the same base
name. Auto-techsets contain target and interface-fingerprint metadata. A change
to textures, samplers, entry points, or relevant semantics marks that metadata
stale and suppresses strict preview until the mappings are reviewed again.

Authored techsets are never silently rewritten. A techset chosen explicitly is
reported as Imported; a same-basename unmarked techset is Authored.

## Live Game Capture

Live capture uses the Win32 Windows Graphics Capture API with a BGRA-capable
D3D11 device and a free-threaded capture frame pool. The newest frame is copied
GPU-to-GPU into a shader-resource texture on the preview thread. There is no
game injection, render hook, OBS dependency, `QImage` conversion, or CPU
readback/upload loop.

The source is always labeled:

> Live Game Capture — Display/LDR approximation

It is the composited displayed window, not raw BO3 `resolvedScene`. In BO3
Runtime PostFX mode the Previewer applies the existing LDR reconstruction and
runtime color-domain simulation, with the visible Scene EV control. Scene EV
defaults/resets to the neutral `0.00`; any visual calibration is an explicit
user adjustment rather than a claimed BO3 transfer-function constant.

The capture source handles first-frame startup, resize/recreation, temporary
frame starvation (for example, a minimized window), source close, explicit
stop, and stale-SRV release. Diagnostics show capture state,
resolution, delivered FPS, approximate queue latency, and preview FPS. Processed,
side-by-side, and slider-controlled split views run after the measured shader
pass so comparison drawing does not change shader timing.

Windows Graphics Capture can be unavailable in an old Windows session or for a
protected/exclusive-fullscreen target. Borderless or windowed BO3 is the
recommended capture mode. Desktop Duplication is not silently substituted,
because it would capture/occlude by monitor rather than preserve window-source
semantics.
