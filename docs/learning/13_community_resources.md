# Community References & Credits

**Level: Beginner → Advanced**

Black Ops III shader authoring has been made much easier by community members who documented, reversed, tested, and shared working examples. The BO3 HLSL Previewer documentation and compatibility work has been informed by these public references.

These projects are **external references**. They are not part of BO3 HLSL Previewer, and this guide does not imply ownership of their code or research. If you reuse code or assets from either repository, review that repository's own license/usage terms first.

## LG-RZ — BlackOps3Shaders

**Author / repository owner:** LG-RZ (LG)  
**Repository:** [github.com/LG-RZ/BlackOps3Shaders](https://github.com/LG-RZ/BlackOps3Shaders)

A practical Black Ops III shader library containing working PostFX and object-material examples, shader templates, GDTs, and usage examples. It is especially useful when you want to see how a real BO3 effect is packaged and used rather than only reading shader theory.

Useful areas to study include:

- PostFX material types such as fisheye, chromatic aberration, posterization, dithering, downsample, and upsample.
- Object/material shader types and templates.
- Multi-pass filter setup and BO3 script usage.
- APE/material workflow examples.

The repository also credits additional contributors/resources in its own README; consult the upstream project for the complete attribution.

> **Usage note:** the repository contains its own `LICENSE` file. Review the upstream repository before copying or redistributing its shader source/assets.

## olie304 — BO3-Shader-Research

**Author / repository owner:** olie304  
**Repository:** [github.com/olie304/BO3-Shader-Research](https://github.com/olie304/BO3-Shader-Research)

A research-focused repository covering Black Ops III HLSL shaders, techsets, materials, decompiled shader output, reversed structures, and examples. Its stated goal is to lower the knowledge barrier for people who want to start creating BO3 shaders.

Useful areas to study include:

- The project Wiki and guides.
- `examples/` for simpler starting points.
- `decompiled/` for examining real BO3 shader patterns.
- `reversed/` for BO3-specific structures and conventions.
- Techset/material relationships and shader-stage research.

The repository is published under **GPL-3.0**. If you copy or derive code from it, follow the upstream license requirements.

## How to use these references with the Previewer

A productive workflow is:

1. Learn the basic concept in this guide.
2. Open one of the Previewer's bundled starter shaders.
3. Compare that small example with a real community shader or decompiled BO3 shader.
4. Identify the shader stage, named textures/samplers, includes, and techset bindings.
5. Bring a small piece of the idea into your own shader rather than copying a large shader blindly.
6. Compile **and** run BO3 package validation.
7. Test in BO3, because tool preview and runtime behavior are not automatically identical.

## Attribution policy for this project

When BO3 HLSL Previewer documentation or implementation work is materially informed by a public community project, the goal is to credit that project clearly and preserve upstream authorship. Third-party source should not be presented as original Previewer code.

### Recommended references

- **LG-RZ — BlackOps3Shaders:** practical BO3 shader/material examples.
- **olie304 — BO3-Shader-Research:** BO3 shader/techset reverse-engineering and learning material.

Both are highly recommended companion resources for anyone progressing from the Beginner chapters into real BO3 shader authoring.
