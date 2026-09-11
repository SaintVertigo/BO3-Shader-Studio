# Phase 1y - Probe Directional Contrast + APE View Framing

Phase 1x fixed the long-standing rim-pinned direct-specular hotspot. With that
geometry error removed, the remaining side-by-side mismatch is dominated by the
baked environment/reflection response and by reference-object framing.

## Probe directional contrast

The APE and Studio screenshots were compared in normalized sphere coordinates.
Away from the direct hotspot their mean display luminance is already very close,
but APE retains substantially more angular variation:

- mid sphere: roughly 1.5x Studio's directional standard deviation;
- outer/main grazing annulus: roughly 1.8x;
- extreme edge can exceed 2x, where Fresnel and the cool rim dominate.

Raising total specular energy would therefore be the wrong correction. Phase 1y
restores directional contrast around the reconstructed probe's own average using
a capture-calibrated factor of 1.75. The average probe level remains unchanged,
so Phase 1x direct lighting and Phase 1s diffuse energy are preserved.

This correction exists only in APE Match's reflection-probe branch. Look Dev and
exported BO3 shader behavior are unaffected.

## APE Reset framing

The Phase 1x Reset screenshots were measured in viewport-normalized space.
Studio occupied about 0.760 of half-height versus APE about 0.706. Keeping the
captured 39.430488-degree APE lens and solving only the dolly gives a corrected
reference distance of approximately 5.25 sphere radii.

Reset / entering APE Match now uses 5.25 instead of 4.89094. Camera interaction
and the captured lens remain unchanged.

## Preserved known-good work

- Phase 1s captured diffuse/direct-sun energy;
- Phase 1v world-frame conversion and instruction-faithful direct specular;
- Phase 1w captured Gloss 13, 39.430488-degree lens and unfiltered GBuffer reads;
- Phase 1x radial APE sphere normal recovery and moving face hotspot.
