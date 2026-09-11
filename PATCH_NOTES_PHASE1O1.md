# APE Match Phase 1o.1 - D3D11 Shadow CBuffer Slot Hotfix

- Fixes DirectX initialization failure `X4567: maximum cbuffer exceeded. target has 14 slots, manual bind to slot 14 failed` introduced by Phase 1o.
- D3D11 exposes 14 constant-buffer slots per shader stage, indexed b0 through b13. Phase 1o incorrectly declared the standalone APE shadow vertex shader constant buffer at b14 and bound it through `VSSetConstantBuffers(14, ...)`.
- The APE shadow vertex pass now uses b0, which is valid and safe because this standalone shadow VS owns its constant-buffer interface for that draw.
- No APE lighting math, captured gloss mapping, reflection-probe LOD, EnvBRDF LUT, sky-yaw behavior, shadow-map dimensions, or filmic display math changed.
