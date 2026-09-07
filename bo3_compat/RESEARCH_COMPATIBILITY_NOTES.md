BO3-Shader-Research compatibility notes
=======================================

The supplied BO3-Shader-Research repository is GPLv3. This previewer does not copy its decompiled shader source into the app package.

The repository was used as a research reference for BO3 binding patterns. In particular, the corpus contains pixel shaders with standard BO3 constant buffers and high-numbered texture slots such as t120 and t125. The previewer therefore reflects texture resources and allows neutral fallback bindings across the D3D11 pixel-shader SRV range through t127.
