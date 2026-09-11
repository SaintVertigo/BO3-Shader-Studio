# Phase 1l — Source-Structured APE Parity

- Fixes the moving black lighting spot by guarding the half-vector before normalization.
- APE Match direct sun now uses BO3 cosine-power gloss semantics (`2^gloss`) instead of translating legacy Gloss 0..17 into GGX.
- Preserves Look Dev's GGX path unchanged.
- Restores APE's broad blue-gray view-dependent probe response: moderate filtered-mip blur, much less avgCubeColor replacement, stronger grazing Fresnel.
- Raises diffuse bounce/fill and preserves more probe chroma so the unlit hemisphere stays readable.
- Replaces the temporary APE ACES display approximation with an isolated lower-contrast, luminance-preserving transfer informed by the recovered TOOLSGFX exposure ordering and the supplied APE capture. The exact shipped TonemapLUT remains unavailable and is not claimed exact.
- Keeps native 8192x4096 / 4096x2048 HDR sources but restores derivative-driven trilinear sky LOD instead of forcing mip 0.
- Retains Phase 1i manual sun controls, Phase 1h BO3 gloss unpacking, and Phase 1j HDR allocation/fallbacks.
