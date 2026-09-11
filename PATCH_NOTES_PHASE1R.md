# Phase 1r - Captured Direct-Light Energy + Hardware Depth Shadow Recovery

This phase fixes the remaining flat/no-sun failure seen after Phase 1q.1.

## Root causes
- APE Day's captured lighting is normalized by scene invExposure. The exact ratios are:
  - sun: `16384 * 0.000128782805 = 2.1099775`
  - global probe: `1941.25403 * 0.000128782805 = 0.2500001`
  Earlier phases used hand-tuned `sunGI=3.6`, `diffGI=1.7`, `probeExposure=1.05` plus an invented bounce floor. The indirect term dominated the material and hid light motion.
- Phase 1o/q wrote shadow depth to a separate R16 color target while using a different D16 surface for raster depth. Camera-depth reprojection then compared against differently quantized values, producing broad self-shadow acne and reducing the sun to a thin strip.

## Fix
- Day now uses the captured sun/probe exposure ratios and captured avgGlobalProbeColor `(0.771301925, 1.01348603, 1.53983426)`.
- Removes the old 60% magic bounce floor and mean-color adaptation; the captured average probe is used as the constant-radiance floor.
- Sun shadow is now one `R16_TYPELESS` hardware depth resource: `D16_UNORM` DSV + `R16_UNORM` SRV. No SV_Position.z color copy.
- Adds dedicated fixed + slope-scaled raster depth bias and receiver normal bias.
- Direct N.L and the compact specular lobe remain independent of probe lighting.
- Phase 1q GGX-prefiltered fixed probe and Phase 1q.1 sun/sky-axis fixes are retained.
