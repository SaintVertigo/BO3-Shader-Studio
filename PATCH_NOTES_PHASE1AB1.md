# Phase 1ab1 - Verification Hotfix

This is a narrow verification hotfix on top of Phase 1ab.

The Phase 1ab verifier incorrectly looked for `ResolveApeReferenceNormal`, but the Phase 1x implementation is and has always been named `ResolveApePreviewNormal`. The actual radial reference-normal path remained installed.

Phase 1ab1 replaces that incorrect single-symbol guard with complete Phase 1x invariants covering the APE Match sphere gate, reconstructed world position, outward radial normalization, Normal inspector path, Final Lit path, preview-only front-face isolation, and sphere mesh-kind signal.

No renderer or shader behavior is changed by this hotfix.
