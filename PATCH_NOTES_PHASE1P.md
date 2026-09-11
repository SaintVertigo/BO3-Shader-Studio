# Phase 1p - Captured Normal/Gloss + Native Cube Probe Rewrite

Phase 1p is a renderer correction built from the 3DMigoto APE captures rather than another visual calibration pass.

## Capture finding that changes the Studio path

The captured stock APE normal texture used by `core_script_wall_c` is an 8x8 neutral texture with RGBA approximately:

```
128, 128, 0, 255
```

BO3 does **not** use the blue channel as tangent-normal Z. R/G hold tangent XY, tangent Z is reconstructed from XY, and B is the separate normal-height term that is folded into `GBuffer_PackGloss`.

Shader Studio's old fallback neutral normal was `(128,128,255)`. That accidentally injected a large height term into the logarithmic gloss packer, so the GBuffer value did not match APE even before deferred lighting ran.

Phase 1p changes the fallback to BO3's captured convention and reconstructs normal Z from R/G.

## Captured deferred gloss / probe behavior

APE's deferred shader `2f9c1c21e9bef37c` normalizes packed `NormalGloss.z` directly:

```
lightingGloss = saturate((packedGloss - 0.00146627566) * 2.00982332)
```

For stock `core_script_wall_c` with Gloss Range `0 -> 13` and APE's zero-height neutral normal, the captured GBuffer contains `NormalGloss.z ~= 0.3822`, which produces:

```
lightingGloss ~= 13 / 17 ~= 0.7647
probe LOD     = 5 * (1 - lightingGloss) ~= 1.1765
```

The direct-light microfacet width follows the captured mapping:

```
cosinePower = 2^(17 * lightingGloss)
alpha^2     = 2 / (cosinePower + 2)
```

The captured `gEnvBRDFGeneric` lookup uses `(NdotV, lightingGloss)` at texel-center corrected coordinates. Phase 1o incorrectly used microfacet alpha as the LUT Y coordinate.

The capture also exposes the final dielectric split-sum combine. For stock reflectance 0.04, APE combines the two EnvBRDF branches as:

```
specular = 0.96 * branchA + 0.04 * branchB
```

Phase 1o had this effectively reversed as `0.04 * A + B`. On the captured LUT that can turn an intended small dielectric reflection into a value near 1.0, which is the direct reason the Phase 1o sphere looked like a dark photographic mirror. Phase 1p uses `(1 - F0) * A + F0 * B`.

## Native reflection-probe resource

APE's t51 resource is a real 256x256 cube/cube-array probe with six faces and seven mips. Phase 1o instead sampled a lat-long `Texture2D` and generated ordinary mips, which left readable clouds painted across the wall.

Phase 1p replaces that path with an actual D3D11 `TextureCube`:

- 256x256 per face;
- six faces;
- seven mips;
- RGBA16F HDR storage;
- immutable authored mip chain;
- directional prefiltering for mip levels;
- no `GenerateMips()` call for the APE reflection probe.

The visible HDR sky and baked glossy probe remain independent, matching the horizontal/vertical APE captures: manual horizontal lighting can yaw the visible sky while the material probe remains at its preset orientation.

## Expected visible result

Compared with Phase 1o/1o.1, the stock wall should stop behaving like a dark photographic mirror. The reflection still has directional environment information, but its appearance now comes from a real cube-probe mip chain and the captured EnvBRDF coordinates. The direct light remains a separate contribution.

## Remaining parity limits

3DMigoto exposed only one subresource of APE's shipped BC6H cube in the resource dump, so Studio cannot simply import the complete original six-face/seven-mip probe. Phase 1p reconstructs the missing cube from APE's HDR environment and the captured resource contract. The preview shadow path also still uses one dynamic 1024x1024 map rather than the complete three-layer APE sun-shadow array. Those limitations are left explicit rather than hidden behind more hand-tuned reflection constants.
