# Shader Types — PostFX, Material, Skybox, and Generic HLSL

**Level: Beginner**

The Previewer recognizes four major authoring targets. They are not interchangeable views of the same shader; they have different input contracts.

## BO3 PostFX

Use PostFX when the shader processes an already-rendered frame.

Typical inputs:

```hlsl
Texture2D<float4> frameBuffer;
Texture2D<float4> DepthSampler;
```

Typical preview: fullscreen 2D image.

Examples: color grading, VHS effects, vignette, distortion, depth haze, sharpening.

## BO3 Material

Use Material for a mesh surface.

The vertex shader receives real mesh data such as positions, UVs, normals, tangents, skin weights, and indices. The Previewer can render the shader on a sphere, cube, plane, or card.

Examples: world surfaces, props, emissive surfaces, stylized materials.

## BO3 Skybox

A sky shader works from a view/sky direction rather than ordinary fullscreen UVs.

Typical pixel input:

```hlsl
float4 skyDirection : TEXCOORD0;
```

Examples: procedural skies, stars, aurora, atmosphere.

When converting Shadertoy to a BO3 sky, distinguish between a **2D/image-space sky** and a **360° self-camera shader**. A self-camera raymarcher already constructs its own view ray, so the converter should replace that ray with BO3 `skyDirection` rather than wrap the finished shader through a second lat-long projection. See **BO3 Skybox Shaders — Rendering by Direction** for the Sky Source modes.

## Generic HLSL

Generic HLSL is a relaxed Direct3D preview path. It is useful for learning syntax and testing shader math, but it is **not automatically a BO3 package**.

When Generic HLSL is active, BO3 package validation can correctly be `N/A`.

## Beginner vs Advanced mode

**Beginner mode** detects the shader type and locks the correct preview path.

**Advanced mode** lets you override **Preview As** deliberately. A forced mismatch is useful for debugging adapters, but it may produce meaningless output. For example, forcing a fullscreen PostFX shader onto a material sphere does not turn it into a material shader.

## Starter files

Use **File → New Example** to open one correct starter for each type. Comparing the files side by side is one of the quickest ways to understand the contracts.
