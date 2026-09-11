BO3 Shader Studio - APE Match Phase 1w
Captured Gloss + Exact Camera Projection Alignment

Install over Phase 1v / current project root:
  C:\Users\ryant\Desktop\BO3_HLSL_Previewer

Verification:
  cd "C:\Users\ryant\Desktop\BO3_HLSL_Previewer"
  Set-ExecutionPolicy -Scope Process Bypass
  .\VERIFY_PHASE1W.ps1

Expected:
  Phase 1w verification: PASS
  Captured Gloss 13, exact 39.430488-degree APE projection, 4.89094-radius framing, and unfiltered GBuffer reads are installed.

Test procedure:
  1. Open the same glossy material test.
  2. Select APE Match -> Day.
  3. Press Reset before comparing.
  4. Move the sun horizontally and vertically as in the prior recordings.

Phase 1w intentionally preserves Phase 1s diffuse and Phase 1v world/specular math.
The key correction is that the generated stock GBuffer now feeds captured APE
Gloss 13 instead of Gloss 6, while camera projection/ray reconstruction use the
captured 39.430488-degree vertical FOV.
