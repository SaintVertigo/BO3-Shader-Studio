BO3 Shader Studio — Phase 1m
APE Coupled Light Rig + Full Pole Rotation

Install the FLAT patch directly into the real BO3_HLSL_Previewer repository root.
Then run:

  Set-ExecutionPolicy -Scope Process Bypass
  .\VERIFY_PHASE1M.ps1

Expected result:

  Phase 1m verification: PASS

Primary runtime test:
1. Open the same stock wall material in APE Match -> Day.
2. Hold Shift+LMB and drag the light horizontally: the HDR sky must move too.
3. Drag vertically through the top/bottom pole and keep going. Repeat several
   revolutions; the light must never stick at +/-89 degrees.
4. Confirm broad environment/reflection features rotate with the light rig.
5. Orbit the CAMERA normally and confirm camera orbit still does not rotate the
   light rig.
6. Press Reset: Day SSI sun direction and the stock environment orientation must
   both return to the preset baseline.

The dark segmented/shaded patch visible in the supplied APE reference sphere is
not treated as a mouse cursor or automatically removed; it remains a parity clue.
