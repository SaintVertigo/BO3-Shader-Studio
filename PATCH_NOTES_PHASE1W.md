# Phase 1w - Captured Material Inputs + Exact APE Projection

Phase 1v corrected the APE-to-Studio world frame and restored the instruction-faithful direct-specular numerator, but the test still showed the white response riding the sphere rim instead of forming APE's compact moving face hotspot.

A full audit of the captured Day frame analysis now rules out the remaining `V`/`H` theory and identifies three concrete Studio input mismatches.

## 1. APE's view and half-vector path was already correct in Phase 1v

The captured deferred compute shader `2f9c1c21e9bef37c` reconstructs camera-relative position from pixel-centre NDC, then computes:

```text
V = normalize(-cameraRelativePosition)
H = normalize(V + L)
VdotH = dot(V,H)
NdotH = dot(N,H)
```

Studio already performs the equivalent `V = normalize(-viewRay)` and `H = normalize(V + L)`. Phase 1w therefore does **not** rewrite the verified BRDF/vector path.

## 2. Studio's generated deferred material was still hardcoded to Gloss 6

This was the largest remaining error.

The APE capture's `NormalGloss` GBuffer is `R10G10B10A2_UNORM`. Every stock sphere pixel stores:

```text
NormalGloss.z = 391 / 1023 = 0.3822091887
```

APE's captured decoder gives:

```text
(0.3822091887 - 0.00146627566) * 2.00982332
    = 0.7652259855
    = 13.00884 / 17
```

That is the quantized representation of BO3 **Gloss 13**. Studio's neutral gloss fallback already used 13/17, but the generated deferred wrapper contradicted it by calling `GBuffer_CalculateNormalGloss(..., float2(6,6))`.

Phase 1w changes the generated stock material to `float2(13,13)`. This is capture-derived, not a visual gloss tweak.

The BRDF consequence is large:

```text
Gloss 6:  alpha^2 ~= 0.0303030, alpha ~= 0.174078
Gloss 13: alpha^2 ~= 0.000244081, alpha ~= 0.0156231
```

APE's Gloss 13 direct lobe is therefore much tighter and much taller. Feeding Gloss 6 suppressed the compact white peak and allowed grazing/probe response to dominate visually as a rim/sliver.

As a final cross-check, replaying the captured APE normals, camera basis and sun vector through the verified direct-specular equation places the maximum at approximately pixel `(693,243)`, where `N.H ~= 0.999996`. With the old Gloss 6 input the no-light-color specular peak is only about `0.422`; with captured Gloss 13 it rises to about `53.45` and collapses into the expected compact lobe. That is the exact qualitative difference between Studio's ring-dominated response and APE's white face hotspot.

## 3. APE's material camera is 39.430488 degrees, not 45 degrees

Captured `CodeSceneTransforms` contains:

```text
cb9[28].x = 0.698002219
cb9[29].y = 0.358352035
```

The second value is `tan(verticalFov/2)`, giving:

```text
vertical FOV = 2 * atan(0.358352035)
             = 39.43048821097 degrees
```

The ratio `0.698002219 / 0.358352035 = 1.94781151`, exactly matching the captured `1157 / 594` viewport aspect.

Studio was hardcoded to 45 degrees in both the geometry projection and deferred view-ray reconstruction. Phase 1w centralizes the material FOV so strict APE Match uses `39.43048821` degrees in both places while Look Dev / Neutral keep their existing 45-degree lens.

## 4. Reference camera distance is now capture-fitted instead of eyeballed

The captured APE `NormalGloss` sphere occupies the exact centred mask:

```text
x = 404..752
y = 125..468
viewport = 1157x594
```

Fitting that mask against a projected unit sphere with the captured projection gives a normalized camera distance of approximately:

```text
4.89094 sphere radii
```

Studio's sphere is normalized to radius 1, so Phase 1w installs `4.89094` directly on APE Reset. Phase 1v's ~4.54 value was only an earlier visual estimate.

## 5. GBuffer reads now match APE's unfiltered `ld` instructions

APE reads its albedo, NormalGloss, ReflectanceOcclusion and depth with integer `ld` instructions. Studio was bilinearly sampling those packed surfaces through `previewSampler`.

Phase 1w switches the deferred compositor to integer `Texture2D.Load` using `SV_Position` texel coordinates. This especially prevents interpolation of packed `NormalGloss` data at the silhouette.

## Preserved unchanged

- Phase 1s capture-verified diffuse equation;
- Phase 1v APE -> Studio world-frame conversion;
- Phase 1v `alpha2 * specScale * NdotL` direct-specular numerator;
- captured 22.5-degree reference camera orbit;
- fixed/baked reflection-probe separation;
- `skyYaw = 90 degrees - APE sunYaw` behavior;
- fake Studio shadowing remains disabled until APE's missing `gSunShadowTree` / t40 path is reconstructed;
- no arbitrary specular-strength or gloss-width multiplier was introduced.
