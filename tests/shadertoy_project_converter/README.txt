Shadertoy project converter manual smoke test
==============================================

1. Open Tools -> GLSL -> BO3 HLSL Converter.
2. Click "Load Shadertoy JSON...".
3. Load sample_common_buffer_image.fragment from this folder.
4. Common, Image, Buffer A, and Buffer B should populate automatically.
5. Select each source tab and verify its iChannels are independent:
   - Buffer A: iChannel1 = Texture / Image (demo_texture)
   - Buffer B: iChannel0 = Buffer A
   - Image:    iChannel0 = Buffer B, iChannel2 = Cubemap (demo_cube)
6. Switching tabs must restore that pass's own channel configuration rather than
   sharing one global iChannel setup across the project.
7. Convert All should generate Common, Image, Buffer A, and Buffer B output tabs.
8. The generated pass header should list usage and handling for the selected
   pass's iChannel bindings. Buffer A's unused iChannel1 must remain assigned
   without being downloaded automatically.
9. With BO3 PostFX / Shadertoy selected, all populated render passes should pass
   FXC validation.

The converter records/imports per-pass Shadertoy channel routing and external-
resource metadata. Buffer A-D remain separate shaders and are not executed as a
live multipass render graph. Feedback is preserved with a warning. Full BO3
texture packaging is intentionally out of scope.

Real iChannel thumbnail/cache smoke test
----------------------------------------
1. Leave "Fetch iChannel images" enabled.
2. Load sample_texture_thumbnail.fragment.
3. Common should show no iChannel panel at all.
4. Image iChannel0 should download/cache the referenced Shadertoy image and show
   the actual image thumbnail instead of only the text "Texture / Image".
5. Click the thumbnail to inspect the binding/usage/cache details, then click
   "Open Cached Asset" to open the image.
6. Click "Open Asset Cache" to open the persistent shadertoy_assets folder.
7. Re-load the sample; it should reuse the cached file rather than downloading it again.

Headless semantic regressions
-----------------------------
Run:

  BO3HLSLPreviewer.exe --shadertoy-regression-all

The command loads regression_cases.json and returns zero only when all focused
usage, policy, download-decision, and source-transformation assertions pass.
It is separate from --regression-all, which continues to run the 102-case core
GLSL -> BO3 HLSL/FXC suite.
