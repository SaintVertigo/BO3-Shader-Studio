# APE Match manual regression

This folder documents the manual checks for the first APE Match renderer pass. It intentionally contains no Treyarch HDR assets and no captured APE screenshots.

## Setup

1. Configure the local Black Ops III installation root when APE Match first asks for it.
2. Open Material Preview and select the built-in sphere.
3. Use a neutral glossy material first; use a textured material second.
4. Do not manually rotate the camera before the initial reference comparison.

## Reference sequence

Check the same material in this order:

- Morning
- Day
- Sunset
- Night
- Neutral / No Lighting

For the four lit profiles, verify that Shader Studio loads the local HDR source and that the status text reports the exact SSI values.

## Phase-1 visual checks

- Sphere framing should be close to APE's default material viewport.
- The direct specular highlight should move to the same quadrant as APE for each SSI preset.
- Day background orientation should place the bright cliff toward the left, green field near center-left, and darker valley toward the right in the recovered APE environment.
- Sunset should retain a warm right-side lighting/reflection contribution and a cooler left side.
- Night should preserve a small bright highlight while the diffuse/material body is substantially darker.
- Neutral / No Lighting should use the APE-style blue-gray clear color and should not show HDR environment reflections or the lit-profile direct specular response.

## Known Phase-1 differences

Do not tune around these as if they are bugs in the orientation pass:

- Environment diffuse is not yet a true irradiance convolution.
- Specular environment lookup is not yet roughness-prefiltered across mip levels.
- APE's exact exposure/tonemap implementation is still approximated.
- Penumbra and probe behavior are not yet reconstructed exactly.
- The camera pitch/environment rotation are screenshot-calibrated until exact APE constants are recovered.
