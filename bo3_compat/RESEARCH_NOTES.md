# BO3 compatibility notes used by the previewer

This compatibility pass was based on the two shader repositories supplied during development:

- **BlackOps3Shaders**: used as the source of the compact `shaders_stable` include tree bundled here. Its original README and license are preserved beside this file.
- **BO3-Shader-Research**: analyzed as a reference database for real/decompiled BO3 bindings and signatures. No decompiled shader corpus is bundled in this app.

A scan of the supplied BO3-Shader-Research pixel shaders showed the engine commonly uses `_Globals` at `b0`, `PerSceneConsts` at `b1`, `LightingGlobals` at `b2`, `GenericsCBuffer` at `b3`, and `PostFxCBuffer` at `b8`. It also showed sampler usage well beyond `s0/s1`, which is why the previewer now supplies its neutral sampler through `s0-s15`.

The previewer remains a post-FX/sky-oriented approximation. Full material shaders can depend on cube arrays, 3D textures, structured lighting data, G-buffer resources, and multiple render targets that are not equivalent to a single fullscreen preview image.
