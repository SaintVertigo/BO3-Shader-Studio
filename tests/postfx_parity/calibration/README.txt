BO3 HLSL Previewer - PostFX Parity Calibration Probes
======================================================

Purpose
-------
The Previewer is now close to BO3, but Scene EV alone cannot make bright walls /
ceilings and the rest of the image match simultaneously. That means the remaining
mismatch is nonlinear. These two probes let us MEASURE the BO3 runtime transfer
instead of guessing another gamma/brightness value.

Use the SAME working postfx setup/techset you already use for killfeed_shader.hlsl.
The included working_techset.techsetdef is only a reference copy of that shape.

PROBE 1 - 01_output_transfer_probe.hlsl
---------------------------------------
1. Back up your current killfeed_shader.hlsl.
2. Temporarily copy 01_output_transfer_probe.hlsl into BO3's shader root and rename
   it to killfeed_shader.hlsl (or change the techset source to this file).
3. Rebuild/reload the shader exactly the same way as your working pencil shader.
4. Take a full-resolution BO3 screenshot of the entire ramp screen.
5. Send that screenshot back to ChatGPT without editing/compressing it if possible.

Important fix:
- Probe 1 now deliberately samples frameBuffer for its output alpha so the working
  techset's frameBuffer/sampler bindings survive optimized FXC reflection in BO3.
  The RGB calibration ramp itself remains generated and unchanged.

What it measures:
- The shader emits known normalized values from 0..1.
- PostFx_DenormalizeColor sends them through BO3's real downstream postfx/display
  path.
- From the screenshot we can derive the actual BO3 output curve instead of assuming
  plain sRGB.

Screen layout, top to bottom:
- grayscale ramp
- red ramp
- green ramp
- blue ramp
- fixed grayscale steps 0 / .125 / .25 / .5 / .75 / 1.0

PROBE 2 - 02_resolved_scene_probe.hlsl
--------------------------------------
Run this only after Probe 1 and take a screenshot from the SAME scene/spot used for
Previewer comparisons.

It samples the real CodeTexture("resolvedScene"), normalizes it, then stores each
channel with the reversible mapping:

    encoded = scene / (1 + scene)

After Probe 1 tells us BO3's downstream display transfer, the encoded screenshot can
be corrected and inverted with:

    scene = encoded / (1 - encoded)

That gives us actual normalized resolvedScene values without needing a GPU debugger
or direct render-target dump.

After testing
-------------
Restore your normal killfeed_shader.hlsl.

Please send back:
1. The Probe 1 screenshot.
2. The Probe 2 screenshot from the same test area.
3. If easy, one normal unfiltered screenshot of that same area as a reference.

With those, the Previewer's BO3 Runtime path can be fitted to measured BO3 behavior
instead of hardcoding the current +1.45 EV workaround.
