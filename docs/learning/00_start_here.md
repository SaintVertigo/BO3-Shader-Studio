# Start Here — BO3 HLSL Without the Guesswork

**Level: Beginner**

This guide teaches BO3 shader authoring in the order that is easiest to understand:

1. Learn what HLSL stages do.
2. Learn the four shader types the Previewer recognizes.
3. Build a simple PostFX shader.
4. Learn how a BO3 techset connects HLSL to engine resources.
5. Move into materials, sky shaders, runtime resources, and package validation.
6. Finish with BO3-specific debugging and advanced HLSL patterns.

You do **not** need to understand techsets before writing your first pixel shader. Start with the shader, then learn how BO3 binds it.

## The three layers to keep separate

A useful mental model is:

**HLSL source** → **techset bindings** → **BO3 runtime resources**

### 1. HLSL source

This is the shader code: functions such as `vs_main` and `ps_main`, textures, samplers, math, and output color.

### 2. Techset

A `.techsetdef` tells BO3 which shader source to use and how named HLSL resources are supplied. It is not just a compiler setting; it is part of the shader package contract.

### 3. Runtime resources

BO3 can provide engine-owned textures and constants. For example, a PostFX techset can bind:

```text
frameBuffer = CodeTexture("resolvedScene")
DepthSampler = CodeTexture("floatZ")
```

Your HLSL sees `frameBuffer` and `DepthSampler`. The techset decides what those names mean at runtime.

## One rule that prevents many mistakes

> **“HLSL Compile: PASS” does not mean “BO3 Package Validation: PASS.”**

A shader can be legal HLSL and still fail BO3 because a sampler name is not bound, a texture name does not match the techset, the wrong vertex shader is used, or an include path is wrong.

The Previewer deliberately reports these as separate checks.

## Community knowledge and credits

BO3 shader authoring is heavily community-driven. This guide recommends and credits **LG-RZ's BlackOps3Shaders** and **olie304's BO3-Shader-Research** as important companion references. See **Community References & Credits** near the end of the guide for links, what each project is useful for, and upstream usage/licensing notes.

## Recommended first exercise

Open:

**File → New Example → BO3 PostFX Shader**

Read the shader from top to bottom. Then continue to **HLSL Basics** and **Shader Types** before editing it.
