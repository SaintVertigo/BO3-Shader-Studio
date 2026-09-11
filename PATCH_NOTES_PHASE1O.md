# Phase 1o - Captured APE Deferred Lighting

Phase 1o replaces the largest remaining APE Match guesses with values and equations recovered from the user's 3DMigoto captures of the real Black Ops III Asset Property Editor.

## Capture-grounded behavior

- Horizontal capture: APE sun direction `(0.0947349, -0.901343, 0.422618)` and `skyRotation=(-0.994522, 0.104528)`.
- Vertical capture: APE sun direction `(0.984808, ~0, 0.173648)` and `skyRotation=(~0, 1)`.
- In both captures `skyRotation = (sun.y, sun.x) / length(sun.xy)`. In Studio's coordinate frame this is exactly `sky yaw = 180 degrees - light yaw`.
- The visible 8192x4096 Day sky resource is unchanged by light movement. Only its yaw constant changes; pitch never rotates the sky.
- The 256x256, six-face, seven-mip BC6H reflection probe resource is byte-identical between horizontal and vertical captures. The glossy probe is baked/fixed while the visible sky yaws.
- The R16 sun-shadow array changes when the light moves. APE really renders the preview mesh into sun shadow maps before deferred lighting; the moving dark patch is therefore treated as a real shadow-path artifact, not a cursor, NaN, or reflection-probe feature.

## Renderer changes

- Visible APE sky now stays at base mip, matching the captured one-mip 8K Day sky.
- APE sky yaw is derived from absolute sun azimuth; light pitch never pitches or rolls the sky.
- APE Match uses a separate fixed reflection-probe resource with seven mip levels and the captured `ProbeLOD = 5 * (1 - gloss)` rule.
- The captured 64x64 R8G8 `gEnvBRDFGeneric` LUT is bundled and sampled by APE Match.
- BO3 gloss uses the captured mapping `cosinePower=2^(17*gloss)`, `alpha^2=2/(cosinePower+2)` for direct microfacet width.
- A real 1024x1024 R16 preview sun-shadow pass is rendered before the GBuffer pass. Deferred lighting uses the captured eight comparison-tap footprint and cubes the average (`shadow = avg8^3`).
- APE Match presentation now uses the captured `275dce0f2b3a7c36` logarithmic/5th-order filmic polynomial followed by the captured Rec.709-to-linear and desktop sRGB transfer. The previous hand-fit ACES/luminance shoulder approximation is removed from APE Match.
- APE reset framing now dollies the reference camera from 4.20 to about 4.54, matching the measured ~8% sphere-size difference in the A/B captures.
- Day sun chroma is updated to the captured normalized value `(1.0, 0.947151, 0.887882)` and its shadow contribution is no longer half-strength.

## Remaining non-pixel-identical piece

3DMigoto exposed the real APE reflection probe as a 256x256 cube with six faces and seven mips, but its DDS dump contains only one cube subresource. Phase 1o therefore reconstructs a fixed seven-mip lat-long probe from the same APE HDR environment at matching angular base resolution. The resource separation, fixed orientation and LOD rule are capture-accurate, but the exact proprietary prefiltered cube texels are not yet available. This is the main remaining reflection-probe difference before claiming pixel-for-pixel parity.
