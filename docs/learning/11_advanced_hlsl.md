# Advanced BO3 HLSL — Constants, Buffers, Semantics, and Performance

**Level: Advanced**

Once you are comfortable with shader types and techsets, the next step is understanding how larger BO3 shaders move data around.

## Constant buffers

HLSL constant buffers group values supplied by the CPU/engine:

```hlsl
cbuffer Example : register(b0)
{
    float4 color;
    float time;
};
```

BO3 decompiled/engine shaders may use explicit `packoffset` layouts. Do not casually reorder known engine constant layouts; the byte/register contract matters.

## Semantics are stage contracts

A pixel shader consuming `TEXCOORD0` requires a vertex stage that outputs compatible `TEXCOORD0`. The names of local C++/HLSL variables do not create that pipeline connection—the semantics do.

## Script/runtime vectors

BO3 shader packages can expose engine/script-driven vector constants. The Previewer's Script Vectors and parameter inspection tools exist to help you see reflected inputs and provide neutral preview values when appropriate.

Do not confuse neutral Previewer values with proof that the game will supply the same runtime value.

## Dynamic flow control

Branches and loops are legal, but expensive or unbounded work can make a fullscreen shader costly.

For PostFX, remember that a small amount of work is executed for every pixel. At 1920×1080 that is over two million pixel invocations per full-screen pass.

## Texture-read cost

Repeated texture sampling is often more expensive than simple arithmetic. The Performance tab reports instruction and texture-operation information plus measured GPU pass timing when available.

## Precision and stability

Guard divisions and normalization where inputs can approach zero:

```hlsl
float safe = max(value, 0.000001);
```

Avoid NaNs/infinities in iterative effects. A shader can compile perfectly and still disappear because its math leaves the valid numeric range.

## Advanced authoring rule

When reverse-engineering or adapting BO3 shader code, preserve known engine contracts first. Optimize or refactor only after you have a verified working baseline.
