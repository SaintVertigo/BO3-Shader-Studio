# Phase 1z - APE Environment Frame + Viewport Scale

Phase 1z follows the Phase 1x hotspot recovery and Phase 1y probe-contrast pass.
The user's reset-to-reset screenshots and splitter-resize video exposed two
remaining structural mismatches.

## 1. Environment directions were still using the pre-1v X-only conversion

Phase 1v recovered the exact APE-to-Studio world transform from the captured
APE camera:

    Studio X = -APE Y
    Studio Y =  APE Z
    Studio Z =  APE X

The sun/direct-light path was converted to that frame, but the visible sky and
reflection-probe path still used the old `d.x = -d.x` approximation. That left
N/L/V in one frame while environment/reflection lookup lived in another.

Phase 1z converts Studio world directions back into APE's authored Z-up frame
and then into the Y-up sampling frame used by the Studio lat-long/cube textures:

    sampleFrame = (Studio Z, Studio Y, -Studio X)

This is used consistently by the visible sky and baked reflection probe.

## 2. Day's base environment orientation is capture-derived

The captured APE deferred-lighting t51 resource is a 256x256 BC6H cube face.
After correcting the DDS face presentation roll, its image cross-correlates
against the captured Day 8192x4096 HDR source at approximately 134.75 degrees
(correlation ~0.885 in the local verification analysis). Phase 1z replaces the
old screenshot-era 120-degree Day calibration with 134.75 degrees.

The visible sky now starts at that authored/reset base orientation and horizontal
light manipulation changes it only by the relative yaw delta. The baked material
probe remains fixed, matching APE's observed separation between visible sky and
material probe. Vertical light motion still does not pitch the sky.

## 3. Reset sphere scale follows APE's viewport invariant

The reset screenshots measure approximately:

- APE sphere radius: ~151 px in a ~514 px material viewport
- Studio sphere radius: ~152 px in a ~571 px material viewport

The raw pixel sizes looked deceptively close, but normalized sizing was not:
APE's sphere diameter is about 58.8% of viewport height while Studio was about
53.3%. The resize video confirms the object-to-viewport ratio is the meaningful
invariant as the splitter moves.

With APE's captured 39.430488-degree vertical FOV, Phase 1z uses 4.85 sphere
radii for the Reset dolly instead of Phase 1y's 5.25.

## Preserved

- Phase 1x radial reference-normal/hotspot recovery
- Phase 1y probe directional-contrast recovery
- captured Gloss 13
- captured 39.430488-degree material projection
- Phase 1s direct diffuse
- Phase 1v direct-specular/world-frame math
- unfiltered GBuffer reads
- fixed/baked probe separation
- fake sun shadow remains disabled until gSunShadowTree/t40 is recovered
