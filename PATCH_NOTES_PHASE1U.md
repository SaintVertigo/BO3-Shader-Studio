# Phase 1u - Captured APE Direct Specular Numerator Fix

The user's Phase 1t test video confirms that Phase 1s's broad moving direct-light hemisphere remains healthy, but the new direct specular still does not look like APE. Across the 28.3-second test the sphere shows only small rim glints while APE's reference behavior has a compact white hotspot that can remain strong as the light approaches grazing angles.

Re-reading the captured deferred shader `2f9c1c21e9bef37c` exposed one remaining translation error in Phase 1t.

## Exact instruction sequence

The captured shader computes:

```text
alpha2 = 2 / (2^(17*gloss) + 2)
alpha  = sqrt(alpha2)
k      = (sqrt(alpha) + 1)^2 / 8

visV = NdotV*(1-k) + k
visL = NdotL*(1-k) + k
Dden = 1 + abs(NdotH)^2*(alpha2-1)

specNoFresnel = alpha2 * specScale /
                (4 * visV * visL * Dden^2)
```

`NdotL` gates the sun branch and appears inside `visL`, but **it is not multiplied into the numerator**. Phase 1t accidentally carried over that conventional factor:

```text
wrong: alpha2 * NdotL * specScale / (...)
right: alpha2 * specScale / (...)
```

That error is mild when the sun is near normal incidence but severe toward grazing angles, which matches the Phase 1t video: the broad diffuse motion is present while the direct white hotspot collapses into weak rim glints.

## Intentionally unchanged

- Phase 1s captured direct-diffuse equation and sun/exposure energy.
- Phase 1t captured `sqrt(alpha)` visibility mapping and no-PI distribution normalization.
- The baked reflection probe and visible-sky behavior.
- The shadow-free APE Match baseline while `gSunShadowTree` (`t40`) is still unreconstructed.
- No hand-tuned specular multiplier has been added.
