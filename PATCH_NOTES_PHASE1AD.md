# Phase 1ad — Visible Sky Handedness + Side-Probe + Night Parity

Phase 1ad follows Phase 1ac's UV/runtime-XMODEL correction. The user's side-orbit Day comparison and Night comparison exposed three remaining issues that were hidden in front-on Reset screenshots.

## 1. Visible sky was horizontally mirrored

Phase 1z correctly recovered the baked material-probe frame:

    probe sample = (StudioZ, StudioY, -StudioX)

That frame is retained for diffuse/specular material lighting.

The screen-visible APE panorama, however, uses the opposite horizontal handedness. Reusing the probe frame caused lateral camera orbit to travel through the Day/Night background backwards. Phase 1ad separates the two paths:

    probe sample   = (StudioZ, StudioY, -StudioX)
    visible sample = (StudioZ, StudioY,  StudioX)

At the captured Reset camera StudioX is zero, so the calibrated reset longitude is preserved while side/orbit motion is unmirrored. The same visible-sky mapping is used by both the deferred material compositor and the forward environment preview.

## 2. Side-orbit probe directional range

With Phase 1ac UV parity fixed, the Day side screenshots can be compared directly. The normalized sphere patterns correlate strongly (~0.984), but APE retains about 25–32% more luminance variation from face-on through grazing angles.

The mean-preserving probe recovery is therefore tightened from 2.58→3.07 to:

    3.30 → 4.05

No change is made to captured Day sun/probe energy, hotspot math, Gloss 13, or the Phase 1x radial normal path.

## 3. Night direct/indirect balance

The Night A/B pair shows a structural imbalance rather than a small exposure mismatch:
- Studio's broad direct-lit hemisphere is much too bright.
- APE Night is dominated by dark teal indirect light.
- APE still retains a compact white specular hotspot.

Phase 1ad adds separate APE direct-diffuse and direct-specular calibration factors so Night can suppress the broad diffuse branch without destroying the hotspot.

Night screenshot calibration:
- visible exposure EV: 2.80
- diffuse probe scale: 3.80
- specular probe scale: 0.180
- sun irradiance scale: 0.050
- probe exposure: 0.85
- direct diffuse multiplier: 1.0
- direct specular multiplier: 28.0

Day remains capture-derived and uses 1.0 / 1.0 for the new direct split.

## Preserved

- Phase 1x radial normal / moving face hotspot recovery
- Phase 1ac runtime XMODEL UV parity
- 4.83-radius fixed Reset camera and 39.430488-degree lens
- Phase 1aa unrestricted Preview resize work
- Phase 1v world-frame/direct-specular math
- Phase 1s direct diffuse
- Day visible/probe base yaw separation
- mip-filtered visible environment
