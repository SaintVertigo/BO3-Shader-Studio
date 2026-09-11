# Phase 1q.1 - Captured Sun-Axis / Direct-Light Recovery

This hotfix corrects the direct-light interaction that remained wrong in Phase 1q.

Ground truth from the APE 3DMigoto captures shows that BO3/APE is Z-up while the
Studio preview is Y-up. Converting between those coordinate frames preserves the
horizontal sun azimuth; it does not add 90 degrees. The previous Studio code added
+90 degrees to the SSI yaw, which rotated the actual sun around the sphere while a
second compensating formula happened to keep the visible sky in the right place.
That is why the sky looked plausible while the material only showed a weak moving
strip instead of APE's broad directly-lit hemisphere and compact hotspot.

Changes:
- Day/Morning/Sunset/Night sun yaw now uses the SSI/captured yaw directly.
- Visible APE sky yaw now follows the capture-derived relationship
  `skyYaw = 90 degrees - sunYaw`.
- Vertical light motion still never pitches the visible sky.
- The baked reflection/diffuse probe still remains fixed at preset orientation.
- Raises the Studio single-map R16 receiver bias from 1 to 8 depth steps. Studio
  reconstructs world position from the camera depth texture, so the old one-step
  bias produced broad self-shadow acne and could suppress most of the direct sun,
  leaving a narrow moving strip. This does not change the captured 8-tap PCF shape.
- Phase 1q GGX-prefiltered 256x256x6x7 cube probe remains intact.

Captured Day baseline check:
- APE wldDir ~= (-0.496732, 0.286788, 0.819152) in BO3 Z-up coordinates.
- Converted to Studio Y-up this is (-0.496732, 0.819152, 0.286788), which is
  yaw 150 degrees / elevation 55 degrees - exactly the SSI Day yaw/elevation.
- Captured skyRotation at that state is -60 degrees, i.e. 90 - 150.
