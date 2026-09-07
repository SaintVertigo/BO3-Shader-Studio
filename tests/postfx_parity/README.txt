BO3 PostFX runtime-parity fixture
=================================

killfeed_pencil_bo3_runtime_parity.hlsl + .techsetdef are a confirmed BO3-working
fullscreen PostFX pair from the 2026-08-16 parity investigation.

Critical contract:
- TOOLSGFX/material preview receives ordinary image color and PostFx_NormalizeColor /
  PostFx_DenormalizeColor are identity operations.
- BO3 runtime binds frameBuffer to CodeTexture("resolvedScene"). The BO3 postfx helpers
  normalize runtime scene RGB by 32768 before shader math and denormalize by 32768
  before returning to the engine.
- The shader owns a PostFx_GenerateFullscreenQuad vs_main and the techset selects it.

The Previewer should render a materially equivalent pencil effect in BOTH explicit
PostFX contexts rather than mixing runtime helper math with an unscaled 0..1 source.

Runtime parity note (2026-08-16): the BO3 Runtime preview must undo the 32768 postfx scale and then apply the display transfer (sRGB for the Previewer UNORM output) before presenting the image. Otherwise this fixture appears too dark and too saturated relative to BO3.

Runtime input parity note (2026-08-16): loaded LDR source images are display-encoded sRGB. BO3 Runtime preview must decode them to scene-linear before multiplying by 32768. This fixes the previous half-bridge where only the final output was converted between linear and sRGB. PNG/JPG sources still cannot reproduce resolvedScene HDR values above 1.0; exact highlight parity requires a scene-linear HDR source/capture.

HDR runtime parity extension (2026-08-16): BO3 Runtime t0 accepts scene-linear EXR without an LDR conversion. Values above 1.0 remain intact through the resolvedScene simulation. The Scene EV control is an explicit runtime/exposure input, not a hidden color correction; PNG/JPG remain labeled LDR approximations.

Compiled-resource linker parity note (2026-08-16): BO3 links the optimized shader,
not the source declarations. FXC can strip a Texture/Sampler that is declared but
unused by ps_main. If the techset still binds that stripped parameter, raw HLSL can
compile/render in the Previewer while BO3 rejects or skips the technique. BO3 Runtime
preview now reflects the optimized PS and suppresses preview when source-declared
PostFX texture/sampler parameters do not survive compilation.
