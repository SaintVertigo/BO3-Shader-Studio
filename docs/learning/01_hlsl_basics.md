# HLSL Basics — The Pieces You Actually Need First

**Level: Beginner**

HLSL is a GPU programming language. In BO3 shader work, the two stages you will see most often are the **vertex shader** and **pixel shader**.

## Vertex shader

A vertex shader runs once for each vertex or generated fullscreen vertex. It normally prepares positions and data that the pixel shader will receive.

A very small interface can look like:

```hlsl
struct VS_INPUT
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD0;
};

struct PS_INPUT
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};
```

The words after `:` are **semantics**. They describe what a value represents to the graphics pipeline.

Common semantics:

- `POSITION` — incoming vertex position
- `SV_Position` — final clip/screen position
- `TEXCOORD0` — user data such as UV coordinates
- `SV_Target` / `SV_TARGET0` — pixel shader color output

## Pixel shader

The pixel shader calculates the final value for a pixel.

```hlsl
float4 ps_main(PS_INPUT input) : SV_Target
{
    return float4(1.0, 0.0, 0.0, 1.0);
}
```

That returns solid red.

## Numeric types

The most common types are:

```hlsl
float   strength = 0.5;
float2  uv       = float2(0.25, 0.75);
float3  color    = float3(1.0, 0.4, 0.1);
float4  rgba     = float4(color, 1.0);
```

You can access components with swizzles:

```hlsl
float x = uv.x;
float2 reversed = uv.yx;
float3 rgb = rgba.rgb;
```

## Useful math

You will see these constantly:

- `saturate(x)` — clamp to 0..1
- `lerp(a, b, t)` — blend between two values
- `dot(a, b)` — dot product
- `normalize(v)` — unit-length vector
- `length(v)` — vector length
- `sin`, `cos`, `pow`, `sqrt`, `abs`, `frac`, `floor`
- `smoothstep(a, b, x)` — smooth transition

## Textures and samplers

A texture contains image data. A sampler controls how coordinates are filtered/wrapped.

```hlsl
Texture2D<float4> frameBuffer : register(t0);
SamplerState frameBufferSampler : register(s0);

float3 color = frameBuffer.Sample(frameBufferSampler, uv).rgb;
```

`t0` is a texture register. `s0` is a sampler register. In BO3, **the HLSL name matters too**, because the techset binds resources by name.

## Functions and constants

Keep repeated math in functions:

```hlsl
float luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}
```

For values you want to tune, simple constants are easier to understand:

```hlsl
static const float EFFECT_STRENGTH = 0.35;
```

The Previewer's **Inputs** tab can expose many simple numeric constants for editing.

## Next

Continue to **Shader Types**. This is where generic HLSL becomes BO3-specific.
