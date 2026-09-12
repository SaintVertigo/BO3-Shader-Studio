# Phase 1ac — UV Parity + Final Reset/Probe/Texture Pass

This pass consolidates the remaining APE-vs-Studio mismatches observed after Phase 1ab/1ab1.

## Fixed

- **Native APE sphere UV parity:** BO3 `XMODEL_BIN` UVs are runtime-ready and are now preserved verbatim. Studio previously flipped V as if the compiled asset were a DCC interchange mesh. APE's captured material vertex shader forwards TEXCOORD directly, so the old flip mirrored the checker parity.
- **Procedural fallback parity:** when the native APE sphere is unavailable, only the fallback APE-Match sphere mirrors its legacy Studio V convention before applying user U/V tiling.
- **Reset framing:** fixed APE-Match Reset camera to **4.83 sphere radii** with the already-captured **39.430488° vertical FOV**. The camera remains invariant while viewport resizing changes only dimensions/aspect.
- **Preview resize floor:** removed the remaining `48x24` Preview-widget minimum; the native pane now shares the existing `1x1` safety floor used by the D3D swapchain path.
- **Visible environment filtering:** deferred APE background now uses mip-linear **LOD 0.35** instead of forcing mip 0, matching the softer APE texture presentation and Studio's already-correct forward environment path.
- **Residual probe directional range:** mean-preserving probe contrast is now **2.58 → 3.07** from face-on to grazing, calibrated from the aligned 1ab Reset pair. Average probe energy is unchanged.

## Preserved

- Phase 1x radial reference-normal recovery / face hotspot fix.
- Phase 1v world-frame conversion.
- Phase 1s captured diffuse path.
- Captured Gloss 13, 39.430488° lens, exact GBuffer loads, Day sky/probe split yaw, and APE display curve.
- Phase 1aa collapsible dock behavior.
