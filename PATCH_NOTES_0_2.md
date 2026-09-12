# BO3 Shader Studio 0.2 — GDT/SSI APE Reference Parity

Version 0.2 is the first build that treats Treyarch's shipped asset definitions as first-class APE Match inputs instead of relying primarily on screenshot fitting.

## Stock Morning / Day / Sunset / Night definitions

The four stock APE environments now carry the exact authoring values recovered from `ssi.gdt` and their sky-material GDTs:

- SSI `colorSRGB`, pitch/yaw, Stops, EV/EV compensation/range, penumbra, bounce count, dynamic-shadow state, sun-enable state and spec compensation.
- Sky material type (`sky_hdr` versus `sky_latlong_hdr`), `skyRotation`, `skyStops`, `skySize`, source type and source image path.
- `skyScaleRGB` is retained in the reference audit but is **not** used as a renderer multiplier because Treyarch's `material.awi` marks it obsolete.

SSI `colorSRGB` is converted to linear before it reaches Studio's lighting constants. Day already matched this conversion in the captured APE constant buffer; Morning, Sunset and Night now follow the same rule.

The captured Day frame establishes shared BO3/APE basis offsets instead of four unrelated hand-fit yaws:

- visible sky = GDT `skyRotation` + 99 degrees;
- baked material probe = GDT `skyRotation` + 59.75 degrees.

The Phase 1ad visible-sky/probe handedness split remains intact.

## Native APE reference geometry

`code.gdt` is now the source of truth for the APE model set. APE Match can load the local Mod Tools copies of:

- `ape_preview_sphere.XMODEL_BIN`
- `ape_preview_cube.XMODEL_BIN`
- `p7_ape_preview_plane_LOD0.XMODEL_BIN`
- `ape_preview_cylinder.XMODEL_BIN`
- `ape_preview_monkey.XMODEL_BIN`

Cylinder and Monkey are now selectable preview meshes. The old non-existent `ape_preview_plane.XMODEL_BIN` path is removed.

The asset resolver accepts both an install root and a `share/raw`-style source layout.

## Reference material contract

The stock comparison material is recorded from `sp_proto_props.gdt` / `material.awi`:

- material: `t7_script_wall`
- color map: `core_script_wall_c`
- material type: `lit`
- color filtering: `aniso2x (mip linear)`
- tint: white
- primary authored gloss range: 0..13
- BO3 absolute gloss domain: 0..17
- spec map disabled
- spec tint: 0.2
- spec amount: 1
- HDR `scaleRGB`: 8

This does **not** replace the captured APE GBuffer/specular math with a naive 0.2 reflectance constant. Captured shader/resource behavior remains authoritative for the renderer itself.

## Cube-sky fidelity

Morning and Night are authored as Cube images, while Day and Sunset are lat-long HDR textures. The cube reconstruction path now preserves more source-face detail before conversion so the 0.2 viewport no longer inherits the old 1024-face quality ceiling.

## Versioning

The user-facing application version is now **0.2**. The updater's hidden monotonic build version remains separate. Future visible releases should advance from the 0.2 line instead of staying on 0.1.

## Preserved parity work

0.2 intentionally keeps the known-good APE work from the 0.1 development line:

- Phase 1x moving hotspot / radial normal recovery;
- runtime XMODEL_BIN UV parity and procedural sphere V compensation;
- 4.83-radius Reset camera and captured 39.43048821-degree vertical FOV;
- capture-derived world frame and direct BRDF path;
- Phase 1ad visible-sky handedness fix;
- Phase 1ad 3.30 -> 4.05 side-probe directional-range recovery;
- Phase 1ad Night direct/indirect split;
- fake sun shadow remains disabled while the real APE shadow tree is unresolved.
