# Debugging BO3 Shaders — Read the Failure Layer First

**Level: Advanced**

When something fails, first identify **which layer failed**.

## Compiler errors

Examples:

- `undeclared identifier`
- wrong constructor argument count
- invalid swizzle
- type mismatch

These are HLSL/source problems. Fix the shader code first.

## BO3 linker / parameter errors

A classic case is:

```text
Undefined shader parameter: glslSampler
```

The HLSL may compile, but BO3 cannot find a matching material/techset binding for that parameter name.

Check:

- HLSL texture/sampler name
- techset `Texture(...)` / `Sampler(...)` name
- runtime assignment name
- selected technique

## Black output with successful compilation

Do not immediately assume the shader math is broken. Check:

1. Is the source texture actually bound?
2. Is the correct vertex shader running?
3. Are UVs/directions valid?
4. Is the shader using BO3 PostFX color normalization correctly?
5. Is TOOLSGFX taking a different branch from runtime?
6. Are you previewing the correct shader type?

## Wrong-looking output only in game

Compare the complete package, not just the HLSL:

- techset resource mapping
- CodeTextures
- sampler mode
- runtime constants
- include path
- vertex/pixel entry points
- PostFX source encoding

## Include failures

BO3 include resolution is path-sensitive. Keep shader source and include structure in a form the BO3 toolchain can actually resolve. A Previewer-only successful include lookup should not be treated as proof that an arbitrary exported nested source layout will link in BO3.

## Use the output panel

The Previewer's Console intentionally prints compile, package, techset-origin, technique, configuration, and validation information together. Read those lines before changing shader math at random.
