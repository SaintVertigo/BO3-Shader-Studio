BO3 PostFX export parity regressions
====================================

Run after building:

  BO3HLSLPreviewer.exe --postfx-export-regression-all

These focused tests protect the confirmed BO3 runtime/linker findings from
2026-08-16 without changing the 102-case GLSL converter corpus.

Covered invariants:
- converter-created glslSampler/glslSamplerN names are normalized for BO3 export
- a shader-defined vs_main using PostFx_GenerateFullscreenQuad is not paired with vs_generic
- PostFX shader source using stock postfx includes stays at shaders_stable root
- the known-bad nested stock-include layout is rejected by package validation
- HLSL sampler names must exist in the generated techset
- adjacent authored techsets bind the bundled sample's resolvedScene, floatZ,
  frameBufferSampler, and DepthSamplerState resources in both configurations
- the bundled sample uses BO3 GetTime/PostFx_GetRenderTargetSize helpers and its
  actual compiled VS/PS pair, never preview-only synthetic globals
- manual PostFX runtime integration generates merge-safe usermap/mod CSC guidance
  from the actual export base/material names, uses only `include,filters` plus the
  exported material in the zone, and never emits a shader-specific autostart .zpkg

This suite intentionally tests HLSL + generated techset as a package. Raw FXC
success alone is not treated as proof that BO3 will link the material.
