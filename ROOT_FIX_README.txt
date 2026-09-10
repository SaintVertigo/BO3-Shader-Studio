PHASE 1g.5 ROOT FIX

Why this exists:
The first Phase 1g commit changed root src/preview_renderer.cpp and introduced an
MSVC C2026 failure because the APE deferred-light HLSL exceeded the 16,380-byte
single-literal limit.

The next four hotfix commits were accidentally committed under a nested
BO3_HLSL_Previewer/... folder. The actual qmake project builds root src/...,
so CI kept compiling the untouched Phase 1g file. This is why every log reported
preview_renderer.cpp(5103) even after the supposed fixes.

This ZIP is intentionally FLAT: src/, resources/, docs/, tests/ are at the archive
root. Extract it directly into the Git repository root and overwrite files.
Then run APPLY_PHASE1G5_ROOT_FIX.ps1 from that root.
