# Phase 1s - Captured Direct Sun Reconstruction

This phase fixes the APE Match sphere receiving little or no visible moving sunlight.

## What the APE captures proved

The horizontal and vertical 3DMigoto captures contain the same stock wall GBuffer and fixed reflection probe, but different sun directions. At pixels where the vertical capture has `N.L = 0`, subtracting vertical HDR output from the horizontal HDR output isolates the direct sun.

For the stock wall, the measured result is:

`directDiffuse ~= linearAlbedo * (sunColor * invExposure) * N.L`

Examples from the captured sphere (red channel):

- center: predicted 0.04456, measured 0.04297
- right-middle: predicted 0.28026, measured 0.27051
- upper-middle: predicted 0.16415, measured 0.16406
- far-right: predicted 0.47438, measured 0.46094

The old Studio path used a conventional `albedo * (1-F) * N.L / PI` Lambert term. That is not what this APE deferred path is accumulating and made the moving sun roughly three times too weak before shadowing.

## Shadow conclusion

We have APE's three-layer `gSunShadowmapArray` (t54), but the frame dump did not include usable contents for `gSunShadowTree` (t40), the structured buffer APE uses to choose/map the sun-shadow layers. Studio's guessed single orthographic shadow camera could classify nearly the entire sphere as shadow and erase the direct light.

Phase 1s therefore disables the guessed shadow lookup in APE Match. The shadow code/resources remain for the later t40 port, but they are not allowed to suppress the capture-validated direct-sun baseline.

## Changes

- APE Match direct diffuse is now `albedo * NdotL`, multiplied by the captured normalized sun color/intensity already carried by the light constants.
- Keeps `CoreSunConstants.wldDir` sign as captured: APE's shader directly tests `dot(N, wldDir)`; no sign flip is introduced.
- Keeps direct specular separate using the existing BO3 gloss-to-alpha path.
- Disables the approximate single-camera shadow map in the active APE Match path until `gSunShadowTree` can be reconstructed.
- Retains Phase 1q GGX probe filtering, Phase 1q.1 sun/sky motion, Phase 1r captured sun/probe exposure, and Phase 1r.1 API hotfix.
