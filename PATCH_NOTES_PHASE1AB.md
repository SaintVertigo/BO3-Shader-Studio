# Phase 1ab — APE Mesh Frame, UV Alignment, Probe Range, and Fixed Viewport Camera

Phase 1ab folds the remaining APE-vs-Studio Reset mismatches into one evidence-driven pass.

## Native APE reference mesh / UV frame
- Applies the Phase 1v capture-derived APE -> Studio world transform to native APE XMODEL_BIN positions, normals, and tangents.
- Flips triangle winding and tangent handedness because the transform has determinant -1.
- Leaves authored UV values untouched; the UV pattern now rotates with the actual APE mesh frame instead of inventing a UV offset.
- This targets the checker-grid mismatch visible in `core_script_wall_c`: a sphere hid the missing coordinate transform geometrically, but the attached UV frame exposed it.

## Reset camera / resize semantics
- Replaces the incorrect 4.85-radius Reset dolly with 4.38, solved from the 1z Reset sphere radius (~158.76 px) versus APE (~176.74 px) using the already-captured 39.430488-degree lens.
- Camera distance/FOV remain fixed while the viewport resizes.
- Reduces the native D3D widget minimum from 48x16 to 1x1; swapchain resizing already clamps internally to >=1x1, so the Qt divider can keep following the cursor like APE.

## Visible Day sky vs baked probe
- Separates the two orientations instead of forcing one yaw onto both resources.
- Visible Day panorama: 172.75 degrees, fitted against the APE Reset background while preserving the captured 22.5-degree camera.
- Baked Day t51 reflection probe: retains the capture-derived 134.75 degrees from Phase 1z.

## Reflection directional range
- Keeps Phase 1y's mean-preserving contrast approach, but recalibrates it from the 1z Reset A/B pair.
- Mid/outer sphere contrast uses 2.18; the grazing region ramps smoothly to 2.38, matching the larger measured APE-vs-Studio variance error at the rim without multiplying mean reflection energy.

## Preserved
- Phase 1x radial reference normal / hotspot recovery.
- Phase 1v world-frame and captured 22.5-degree camera basis.
- Phase 1w Gloss 13, 39.430488-degree projection, unfiltered GBuffer reads.
- Phase 1s diffuse/direct-sun equation and captured direct-specular equation.
- Phase 1aa collapsible dock layout.
