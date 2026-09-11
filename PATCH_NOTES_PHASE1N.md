# Phase 1n — Decoupled APE Sun / Visible Sky / Baked Probe

This phase corrects the Phase 1m assumption that APE rotates one rigid sun + HDR environment rig.
The latest APE recording shows three separate states:

1. **Sun direction** — manual light yaw and pitch both move the sun. Pitch is continuous and may pass over/under the sphere repeatedly.
2. **Visible sky** — horizontal light motion yaws the displayed environment; vertical light motion does not pitch or roll the sky.
3. **Material probe** — the glossy reflection and diffuse probe remain at the preset/baked orientation while the manual light is moved. The visible sky may yaw underneath that fixed material probe.

Implementation:

- keeps continuous wrapped light pitch (no +/-89 degree pole clamp);
- removes the Phase 1m environment-pitch state completely;
- keeps a visible-sky yaw and a separate baked-probe yaw;
- initializes both yaw values from the selected APE preset/reset orientation;
- manual light yaw changes only the visible-sky yaw;
- manual light pitch changes only the sun direction;
- specular reflection lookup uses the fixed baked-probe yaw;
- diffuse SH lookup uses the same fixed baked-probe yaw;
- visible HDR background uses only the visible-sky yaw;
- camera orbit remains independent;
- updates APE Match help/tooltips and reverse-engineering notes.

The dark low-frequency APE patch is intentionally **not** erased. With the probe no longer following the light, a dark baked-probe region can remain on the sphere even when the direct sun has moved onto that hemisphere, which is the behavior shown in the supplied APE capture.

## What this still cannot prove bit-for-bit

The recovered project has BO3 headers, material/GBuffer code, APE/TOOLSGFX paths, SSI/reference assets, executable findings, and the Shader Studio reverse-engineering work. It does **not** contain the shipped implementations/data for:

- `ToolsGfx/deferred_lighting.hlsl` used by APE's final deferred BRDF/permutations;
- `ToolsGfx/diffuseprobe_compute.hlsl` / the exact reflection-probe convolution and mip generation;
- `ToolsGfx/tonemap_lut.hlsl` plus the exact presentation LUT payload;
- AssetEditor's native viewport/light-manipulator source code.

Phase 1n therefore fixes behavior that is directly observable in APE and consistent with the recovered separation between sun constants and reflection/global-probe state. Exact pixel identity for the remaining BRDF/probe/tonemap differences requires those missing implementations/data or a ground-truth capture of their intermediate outputs.
