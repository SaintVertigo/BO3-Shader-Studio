# Phase 1t - Captured APE Direct Specular Reconstruction

Phase 1s recovered the broad moving sun hemisphere by matching APE's captured direct-diffuse energy and refusing to let the incomplete Studio shadow approximation erase it. The user's 66.23-second Phase 1s comparison video shows that this is the first build with the correct large-scale light motion, but it also makes the next mismatch unambiguous: APE has a compact white direct-sun highlight that tracks across the sphere while Studio's corresponding highlight is absent/too weak.

Phase 1t keeps the Phase 1s sunlight/probe baseline intact and translates the direct-specular math from the captured APE deferred shader `2f9c1c21e9bef37c` much more literally.

## What was wrong

The previous Studio APE branch was still a conventional GGX rewrite. Two details differed from the captured ToolsGfx instruction stream:

1. Studio divided the GGX distribution by PI. The captured APE direct-specular branch does not. That made the direct lobe roughly PI times too weak before display mapping.
2. Studio derived the Schlick visibility `k` from `alpha`. APE first takes `sqrt(alpha)` and then evaluates `(sqrt(alpha)+1)^2 / 8`.

For stock Gloss 13:

- `cosinePower = 2^13 = 8192`
- `alpha^2 = 2/(8192+2)`
- `alpha ~= 0.0156231`
- APE visibility `k ~= 0.158201`

The captured Day `CoreSunConstants.specScale` is 1.0, so no extra hand-tuned highlight multiplier is introduced.

## Captured direct-specular form

Phase 1t now evaluates the instruction-equivalent branch:

```text
alpha2 = 2 / (2^(17*gloss) + 2)
alpha  = sqrt(alpha2)
k      = (sqrt(alpha) + 1)^2 / 8

visV = NdotV * (1-k) + k
visL = NdotL * (1-k) + k
Dden = 1 + abs(NdotH)^2 * (alpha2 - 1)

specNoFresnel = alpha2 * NdotL * specScale /
                (4 * visV * visL * Dden^2)

F = F0 + (1-F0) * (1-VdotH)^5
specular = specNoFresnel * F
```

This preserves APE's separate base/grazing Fresnel behavior algebraically without inventing a new intensity control.

## Direct diffuse terminator

The same captured block includes a small alpha-dependent rough-diffuse correction. Phase 1s's plain `NdotL` was already within a few percent for Gloss 13 because alpha is tiny, but Phase 1t ports the captured correction as well so the highlight test is not mixed with a slightly different terminator model.

## Intentionally unchanged

- Phase 1s captured Day sun energy remains unchanged.
- The baked reflection probe remains independent from the moving sun.
- The visible sky still follows horizontal light yaw only.
- The incomplete single-shadow approximation stays disabled in APE Match. APE's real three-layer sun-shadow path will be restored separately rather than risking the recovered direct-light baseline.
- No arbitrary specular boost was added.
