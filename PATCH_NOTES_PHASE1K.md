# Phase 1k — APE local-probe parity

Phase 1k fixes the remaining chrome-like stock material response visible after
Phase 1j. The APE reference recording shows gloss 13 producing a tiny direct sun
highlight while its reflection probe stays broad and low contrast.

Changes:
- Decouples direct-light gloss from reflection-probe LOD in APE Match.
- Forces stock dielectric 0.04 reflectance through substantially blurrier probe mips.
- Soft-compresses HDR probe luminance so sky/sun spikes cannot become mirror detail.
- Blends the stock dielectric probe toward the recovered average cube color.
- Leaves high-reflectance/metal-like materials able to use sharper probe LODs.
- Keeps Phase 1j native/4K HDR sky fidelity and Phase 1i manual sun controls.

Expected visual result for t7_script_wall/core_script_wall_c:
- no readable cloud/terrain mirror across the sphere;
- small moving white direct-sun highlight remains;
- broad, subtle environment/horizon tint remains;
- diffuse body stays dominant, matching APE's material viewport behavior.
