BO3 HLSL Previewer - BO3 Runtime compiled-resource parity fix
=============================================================

This preview is source-only. Run build_qt.bat before testing.

New parity behavior:
- In PostFX -> BO3 Runtime mode, the Previewer now reflects the optimized ps_5_0
  bytecode before installing the shader.
- If a source-declared Texture/Sampler is optimized out by FXC while the BO3
  package would try to bind it, Previewer refuses to render and reports a
  [BO3 package parity] error instead of producing a false-positive preview.
- This specifically catches the calibration-probe failure where frameBuffer and
  bilinearClampler were declared but never used, so Previewer rendered the ramp
  while BO3 could not run the matching techset contract.
- Corrected calibration probes are under tests/postfx_parity/calibration/.
  Probe 01 keeps frameBuffer + bilinearClampler alive through output alpha.

IN-APP BO3 HLSL LEARNING GUIDE
------------------------------
The Help menu now includes a built-in BO3 HLSL Learning Guide. It progresses
from beginner HLSL fundamentals through shader types, PostFX, techsets,
textures/samplers/CodeTextures, materials, skyboxes, TOOLSGFX vs BO3 runtime,
package validation, debugging, and advanced BO3 HLSL topics. The guide is
embedded into the application and can open the matching bundled starter shader
for each major shader type.

COMMUNITY REFERENCES / CREDITS
------------------------------
Recommended external BO3 shader resources used as community research/reference:
- LG-RZ (LG) — BlackOps3Shaders
  https://github.com/LG-RZ/BlackOps3Shaders
- olie304 — BO3-Shader-Research
  https://github.com/olie304/BO3-Shader-Research

These are independent upstream projects. BO3 HLSL Previewer does not claim
ownership of their code/research. Review each upstream repository's terms before
copying or redistributing third-party source. BO3-Shader-Research is GPL-3.0;
BlackOps3Shaders provides its own upstream LICENSE file.
