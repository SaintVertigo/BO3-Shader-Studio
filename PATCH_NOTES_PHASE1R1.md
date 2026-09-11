# Phase 1r.1 - PreviewRenderer API build hotfix

This is a narrow compile fix on top of Phase 1r.

GitHub Actions showed `main_window.cpp` calling:

- `PreviewRenderer::SetApeGlobalProbeAverageColor(...)`
- `PreviewRenderer::ResetApeGlobalProbeAverageColorToEnvironment()`

The implementation methods existed inside `PreviewRenderer::Impl`, but Phase 1r omitted the matching public declarations and forwarding wrappers on `PreviewRenderer`. MSVC therefore stopped with C2039 before linking.

Phase 1r.1 adds only that missing public API surface. The Phase 1r lighting, shadow, probe, BRDF, sky, and tone-mapping behavior is unchanged.
