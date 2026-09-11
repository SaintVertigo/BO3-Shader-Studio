# Phase 1v - APE World-Frame + Direct-Specular Alignment

The Phase 1u video still showed direct specular as narrow edge slivers instead of APE's rounded moving white hotspot. Re-checking both the exact APE shader register lifetimes and the captured camera matrix found two concrete errors.

## 1. Phase 1u removed a real NdotL factor

Captured `2f9c1c21e9bef37c` instructions 2397-2439 are:

```text
r3.w = alpha2 * CoreSunConstants.specScale
r3.w = NdotL * r3.w
...
r2.w = r3.w / (visV * visL * Dden^2)
r2.w *= shadow
r2.w *= 0.25
```

So the direct no-Fresnel scalar is:

```text
alpha2 * specScale * NdotL / (4 * visV * visL * Dden^2)
```

Phase 1u had misidentified the register and removed `NdotL`. Phase 1v restores the instruction-faithful numerator.

## 2. The APE world conversion was missing a horizontal/handedness axis

The paired 3DMigoto captures contain APE's exact camera transform. `camToWldMatrix` gives:

```text
right   = (0, -1, 0)
up      = (0.382683, 0, 0.923880)
forward = (0.923880, 0, -0.382683)
camera  = (-286.182861, 0, 118.540833)
```

That is the 22.5-degree APE reference camera. The exact APE-world to Studio-world conversion is:

```text
Studio X = -APE Y
Studio Y =  APE Z
Studio Z =  APE X
```

The old code only swapped Y/Z, which left the sun around ~90 degrees away around the sphere in important capture poses. Diffuse still looked broadly believable because a sphere is symmetric, but the half-vector/specular position was wrong.

As a numerical cross-check, the captured horizontal sun:

```text
APE    = (0.094735, -0.901343, 0.422618)
Studio = (0.901343,  0.422618, 0.094735)
```

At APE's actual white-hotspot pixel, the decoded/converted GBuffer normal has ~0.997 dot product with the converted sun/view half vector. This is direct capture evidence for the transform.

## Preserved from Phase 1s

- capture-verified direct diffuse (`albedo * roughDiffuseLobe`, no `/PI`);
- capture-derived Day sun and probe exposure;
- fixed/baked reflection probe separation;
- visible sky rule `skyYaw = 90 - APE sunYaw`;
- fake Studio sun shadow remains disabled until `gSunShadowTree`/t40 is recovered.

No arbitrary specular brightness or gloss-width multiplier was added.
