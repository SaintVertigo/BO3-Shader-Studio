# BO3 Skybox Shaders — Rendering by Direction

**Level: Intermediate**

Open **File → New Example → BO3 Skybox Shader**.

A procedural sky is usually based on a direction vector instead of a 2D scene framebuffer.

## Directional input

The starter passes:

```hlsl
float4 skyDirection : TEXCOORD0;
```

Then the pixel shader normalizes it:

```hlsl
float3 d = normalize(input.skyDirection.xyz);
```

From there you can use direction components to create horizon, zenith, stars, sun disks, clouds, or aurora.

## Horizon example

```hlsl
float horizon = saturate(1.0 - abs(d.z));
float up = saturate(d.z * 0.5 + 0.5);
```

The exact axis convention depends on the authored sky path, but the core concept is the same: color comes from **where the camera is looking**.

## Why the vertex shader matters

The bundled starter uses its own `vs_main` so the directional `TEXCOORD0` contract is explicit and package validation does not depend on an unbundled external vertex shader.

This is an important general BO3 lesson: if your pixel shader expects varyings, ensure the selected vertex stage really produces them.

## Preview interaction

Skybox mode automatically enables directional camera navigation. Dragging the preview changes the viewing direction without pretending the shader is a mesh material.

## Converting Shadertoy skies: image-space vs self-camera

Shadertoy "sky" shaders are not all the same kind of program. The converter now asks for a **Sky source** when **Target = BO3 Sky / Environment**:

- **Auto Detect** — looks for a 3D camera/view ray such as `rd`, `rayDir`, or `viewDir` and chooses the safer wrapper automatically.
- **2D / Image-Space (Lat-Long Wrap)** — keeps `mainImage()` as a 2D program and maps BO3 `skyDirection` to longitude/latitude before calling it. Use this for a 2D procedural panorama or image-space sky that does not construct its own 3D camera ray.
- **360° / Self-Camera (Replace View Ray)** — for raymarched/procedural environments that already build a camera ray and can look around in every direction. The converter replaces the Shadertoy view ray with BO3 `skyDirection` instead of projecting the shader a second time.

A typical self-camera Shadertoy contains code shaped like:

```glsl
vec2 p = (2.0 * fragCoord - iResolution.xy) / iResolution.y;
vec3 rd = normalize(vec3(p, 1.5));
```

It may then rotate `rd` with `iMouse`, a camera matrix, yaw/pitch, or a `lookAt`/`setCamera` helper. Wrapping that program as a panorama first causes a **double projection**: BO3 direction becomes lat-long coordinates, then the original shader turns those 2D coordinates back into another camera ray. The result can appear warped, duplicated, squeezed, or unable to rotate naturally.

In **Self-Camera** mode the generated HLSL contains:

```hlsl
// BO3_PREVIEWER_SKY_SELF_CAMERA
float3 rd = BO3_ShaderToySkyDirection();
```

The adapter converts BO3's Z-up sky direction into the common Shadertoy Y-up camera convention and suppresses recognized mouse/camera orientation updates to the replaced ray. It does **not** remove unrelated time animation such as moving clouds.

### Auto Detect is conservative

Auto Detect intentionally requires a recognizable view-ray pattern. If the shader uses unusual names or constructs its camera through several helper functions, force **360° / Self-Camera** and inspect the converter notes. If no safe ray can be found, the converter falls back to the 2D/lat-long wrapper rather than deleting arbitrary shader code.

### Camera/Input Removal

Do not use generic **Apply Camera/Input Removal to HLSL** as a substitute for Self-Camera conversion. A 360° sky needs a camera ray; it needs that ray **replaced with BO3 skyDirection**, not deleted. Generated self-camera skies are marked so the generic camera/input removal pass skips them.
