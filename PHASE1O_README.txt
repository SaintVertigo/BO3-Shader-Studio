BO3 Shader Studio - Phase 1o
Captured APE Deferred Lighting

This build uses the real APE capture to replace guessed APE Match behavior:
- captured BO3 gloss -> microfacet width
- exact 5*(1-gloss) reflection LOD
- captured 64x64 EnvBRDF LUT
- separate fixed baked reflection probe
- visible sky yaw from sun azimuth only (never pitch)
- real preview sun-shadow render pass with APE 8-tap/cubed filtering
- captured APE filmic polynomial/display transfer
- captured Day sun chroma

The full APE cube texture could not yet be extracted in all six faces/mips by 3DMigoto, so Phase 1o reconstructs that resource from the same HDR sky while matching its resolution/mip/LOD contract.
