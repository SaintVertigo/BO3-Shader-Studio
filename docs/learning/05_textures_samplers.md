# Textures, Samplers, Registers, and CodeTextures

**Level: Intermediate**

This is one of the most common sources of “works in Previewer, fails in BO3” bugs.

## Texture register vs sampler register

These are separate resources:

```hlsl
Texture2D<float4> colorMap : register(t0);
SamplerState colorSampler : register(s0);
```

The texture stores data. The sampler controls filtering and addressing.

## Sampling

```hlsl
float4 c = colorMap.Sample(colorSampler, uv);
```

Other HLSL methods such as `SampleLevel` choose mip levels explicitly.

## BO3 names matter

Suppose HLSL declares:

```hlsl
SamplerState glslSampler : register(s1);
```

It may compile. But if the BO3 techset binds `colorSampler` instead, BO3 can report an undefined shader parameter. Register compatibility alone does not fix a name mismatch.

## CodeTexture

A `CodeTexture` is an engine-provided texture binding. Example:

```text
frameBuffer = CodeTexture("resolvedScene")
```

The shader still declares a normal `Texture2D`. The techset is what connects that shader parameter to BO3's runtime image.

Common PostFX examples used by this Previewer:

- `resolvedScene` — scene framebuffer input
- `floatZ` — depth input

## TOOLSGFX fallback

The same parameter can have a normal material-image fallback for tool/editor preview and a CodeTexture binding at runtime. That is why the PostFX starter contains both `Texture(...)` declarations and a conditional runtime binding.

## Addressing and filtering

A shader can be visually wrong even when all names are valid if the sampler mode differs from the intended effect.

Examples:

- linear filtering — smooth interpolation
- nearest filtering — exact texel/point sampling
- wrap/repeat — UVs outside 0..1 repeat
- clamp/no tile — UVs stay at the edge

For accurate package preview, the Previewer should honor the techset's sampler behavior rather than inventing its own.
