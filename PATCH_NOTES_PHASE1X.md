# Phase 1x - APE Reference Normal Recovery

Phase 1w corrected captured material inputs and projection, but the user's next recording proved that the specular white response was still geometrically constrained to the sphere silhouette. A lobe that remains on the circumference while the sun moves cannot be explained by gloss, Fresnel strength, FOV, or direct-specular normalization when `V`, `L`, and `H` are otherwise valid.

Phase 1x therefore repairs the **surface normal feeding the BRDF** and leaves the verified Phase 1s/1v lighting equations unchanged.

## 1. Strict APE Match Sphere now uses APE's captured radial normal field

APE's captured stock preview sphere is a geometric sphere. Decoding its captured `NormalGloss` field and comparing it with the analytic sphere normal gives essentially one-to-one radial alignment across the visible face.

For strict **APE Match + Sphere**, the deferred compositor now:

1. reconstructs the current surface world position from the exact material depth/camera path;
2. normalizes that position about the sphere's origin;
3. uses the resulting outward radial vector as `N`.

This deliberately bypasses every preview-only source of normal inversion between the reference mesh and the BRDF: XMODEL triangle winding, `SV_IsFrontFace`, and packed-normal front/back classification. Cube, Plane, Card, Custom, Look Dev, and exported BO3 material behavior continue to use the ordinary GBuffer normal path.

The **Normal** semantic inspector now uses this same resolved normal, so the diagnostic view and Final Lit cannot disagree about which `N` reaches the BRDF.

## 2. Studio preview compilation has an explicit preview-only define

Studio's D3D preview compiler now defines:

```text
BO3_STUDIO_PREVIEW=1
```

Generated deferred material wrappers and Beginner material wrappers use that define to preserve the authored vertex-normal orientation while Studio renders its preview meshes two-sided. In Studio preview only, raster winding no longer flips the TBN through `SV_IsFrontFace`.

The define is **not present in exported BO3 compilation**, so exported/runtime shaders retain Treyarch's native `SV_IsFrontFace` behavior. This avoids solving a viewport problem by changing game behavior.

## 3. Why this addresses the ring symptom directly

With the captured APE camera and sun vectors, the expected half-vector points into the visible sphere face. A genuinely radial normal field therefore has an interior point where `N.H` approaches 1. A maximum that can only appear on the silhouette means the normal entering that dot product is not the expected radial field.

Phase 1x forces the reference Sphere test onto the capture-proven radial field before `N.L`, `N.V`, `N.H`, diffuse, direct specular, and probe reflection are evaluated.

## Preserved unchanged

- Phase 1s capture-verified direct diffuse equation;
- Phase 1v APE-to-Studio world-frame transform;
- Phase 1v `alpha2 * specScale * NdotL` direct-specular numerator;
- Phase 1w captured Gloss 13;
- Phase 1w 39.430488-degree APE projection;
- Phase 1w 4.89094-radius reference framing;
- unfiltered APE-style GBuffer/depth `Load` reads;
- captured 22.5-degree reference orbit;
- fixed/baked reflection probe separation;
- `skyYaw = 90 degrees - APE sunYaw`;
- fabricated shadowing remains disabled until `gSunShadowTree` / t40 is recovered.

No specular-strength, Fresnel, or arbitrary gloss multiplier was added.
