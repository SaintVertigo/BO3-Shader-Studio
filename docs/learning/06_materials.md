# BO3 Materials — From Mesh Vertices to Surface Pixels

**Level: Intermediate**

Open **File → New Example → BO3 Material Shader**.

A material shader is fundamentally different from PostFX because it renders geometry.

## BO3 vertex inputs

The starter includes BO3 vertex and skinning helpers:

```hlsl
#include "lib/globals.hlsl"
#include "lib/vertdecl_vertex.hlsl"
#include "lib/vertdecl_vertex_tangentspace.hlsl"
#include "lib/gpu_skin.hlsl"
#include "lib/gbuffer.hlsl"
```

Its vertex shader receives `GBufferVertexInput`, decodes normals/tangents, applies GPU skinning, transforms the vertex to world space, and outputs clip-space position plus surface data.

## Why normals matter

A material normally reacts to surface orientation.

```hlsl
float3 n = normalize(input.normal.xyz);
float lighting = 0.45 + 0.55 * saturate(
    dot(n, normalize(float3(0.35, 0.55, 0.75))));
```

That makes the same pixel shader appear differently across a sphere.

## UV coordinates

Mesh UVs arrive through the vertex stage and can drive textures or procedural patterns.

```hlsl
float2 uv = input.texCoords.xy;
```

## Preview meshes

The Previewer can place a material on different meshes. This is useful because a shader can look fine on a plane but expose normal/tangent problems on a sphere or card.

## Do not convert PostFX by changing the preview mode

A fullscreen PostFX shader does not become a material merely because you select **Material** in Advanced mode. A real material needs the correct mesh input/output contract.
