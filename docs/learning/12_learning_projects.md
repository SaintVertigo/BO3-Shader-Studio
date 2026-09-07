# Learning Projects — A Path From First Pixel to BO3 Package

**Level: Beginner → Advanced**

Use these exercises in order. Each one builds on the previous concepts.

## Project 1 — Solid-color Generic HLSL

Open the Generic HLSL starter and return a fixed color. Learn `float4`, entry points, and compilation.

## Project 2 — UV gradient

Use screen/UV position to create a color gradient. Learn coordinates and `lerp`.

## Project 3 — PostFX vignette

Open the BO3 PostFX starter. Sample `frameBuffer`, normalize the source, darken edges, denormalize, and return.

## Project 4 — Animated PostFX

Use `GetTime()` to animate only one effect parameter. Learn the difference between deliberate animation and whole-screen camera/input movement.

## Project 5 — Depth-aware PostFX

Read `DepthSampler` and make a subtle depth-dependent tint. Then inspect the techset binding to `floatZ`.

## Project 6 — Procedural material

Open the Material starter. Change the grid and lighting math. Test it on sphere, cube, plane, and card.

## Project 7 — Procedural sky

Open the Skybox starter. Add a sun disk or stars based on `skyDirection`.

## Project 8 — Break the techset on purpose

Rename a sampler in HLSL without changing the techset. Observe the difference between HLSL compilation and BO3 package validation. Then fix the name contract.

## Project 9 — Runtime PostFX parity

Use BO3 Runtime PostFX preview. Compare TOOLSGFX and runtime paths, then load an appropriate scene-linear EXR to understand why LDR screenshots are only approximations.

## Project 10 — Convert and validate GLSL

Use the GLSL → BO3 HLSL converter, inspect the generated HLSL and package, and run the regression/validation tools. Treat conversion as the beginning of BO3 authoring, not as proof that every arbitrary GLSL shader maps perfectly to BO3.

# Quick glossary

**HLSL** — High-Level Shader Language used by Direct3D.

**Vertex shader (VS)** — processes/generates vertex-stage outputs.

**Pixel shader (PS)** — computes pixel outputs.

**Semantic** — pipeline meaning attached to an input/output, such as `TEXCOORD0`.

**Techset** — BO3 technique/resource definition connecting HLSL stages and named parameters to rendering state/resources.

**Sampler** — texture filtering/addressing state.

**CodeTexture** — engine-provided texture bound to a shader parameter by the techset.

**TOOLSGFX** — tool/editor-side configuration used to distinguish preview/material behavior from runtime bindings.

**resolvedScene** — runtime scene-color input used by the bundled PostFX path.

**floatZ** — runtime depth input used by the bundled PostFX path.

**FXC** — legacy Direct3D HLSL compiler used for shader compilation/validation in this workflow.

**Package validation** — checks beyond syntax compilation to verify that BO3-facing shader/package contracts agree.
