# Techsets 101 — How BO3 Connects HLSL to the Engine

**Level: Intermediate**

A BO3 shader is not defined by HLSL alone. The `.techsetdef` tells BO3 which source and stages belong to a technique and how named resources are supplied.

The bundled PostFX starter has a companion:

```text
sample_bo3_postfx.hlsl
sample_bo3_postfx.techsetdef
```

## Globals

```text
Globals()
{
    category = "2d"
    renderFlags = "none"
}
```

These settings describe how the technique participates in rendering.

## Sampler declarations

```text
Sampler("frameBufferSampler")
{
    tile = "no tile"
    filter = "linear (mip none)"
}
```

The name `frameBufferSampler` matches the HLSL declaration exactly:

```hlsl
SamplerState frameBufferSampler : register(s0);
```

That name match is important. A generated sampler name can compile in HLSL while still being rejected by BO3's material/techset linker if the techset does not bind it.

## Texture declarations

```text
Texture("frameBuffer")
{
    image = Image(<colorMap00, $white_diffuse>)
    semantic = "2d"
}
```

This gives TOOLSGFX/editor material preview a texture fallback.

## Technique

```text
Technique("lit", "unlit")
{
    state = "replace + nocull"
    source = "sample_bo3_postfx.hlsl"
```

`source` points to the HLSL source used by the technique.

## Vertex and pixel stages

```text
    vs = VertexShader()
    {
    }

    ps = PixelShader()
    {
        ...
    }
```

An empty `VertexShader()` block here means the package can use the shader-defined `vs_main` from the source. This matters when the pixel stage expects varyings such as `TEXCOORD0` produced by that vertex shader.

## Runtime CodeTextures

```text
#if TOOLSGFX != "1"
    frameBuffer = CodeTexture("resolvedScene")
    DepthSampler = CodeTexture("floatZ")
#endif
```

At BO3 runtime:

- HLSL `frameBuffer` receives `resolvedScene`
- HLSL `DepthSampler` receives `floatZ`

The HLSL does not need to know the engine's implementation details. It only needs the named contract.

## The key lesson

> A resource has **three identities** you should keep consistent: HLSL name, register/stage expectation, and techset binding.

When one of those disagrees, compiling the HLSL by itself is not enough to prove the package works.
