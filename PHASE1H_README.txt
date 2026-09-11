BO3 Shader Studio — APE Match Phase 1h
======================================

This is a FLAT patch for the real BO3_HLSL_Previewer repository root.
Do not extract it into a new BO3_HLSL_Previewer subfolder.

1. Open:
   C:\Users\ryant\Desktop\BO3_HLSL_Previewer

2. Extract the CONTENTS of this ZIP directly there and overwrite when asked.

3. In PowerShell:

   cd "C:\Users\ryant\Desktop\BO3_HLSL_Previewer"
   Set-ExecutionPolicy -Scope Process Bypass
   .\VERIFY_PHASE1H.ps1

Expected result:
   Phase 1h verification: PASS

Primary changes:
- APE Match sun/SSI preset stays fixed in world space while orbiting.
- Alt+LMB orbit, Alt+MMB pan, Alt+RMB dolly (plain mouse aliases remain).
- Reset / R / double-click restores full APE view + lighting preset.
- Lighting preset changes preserve current camera orbit.
- Correct BO3 logarithmic NormalGloss.z decode for stock gloss 13.
- Correct BO3 fallback GBuffer gloss packing.
