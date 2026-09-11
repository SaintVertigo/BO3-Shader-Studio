BO3 Shader Studio - APE Match Phase 1o.1
D3D11 Shadow CBuffer Slot Hotfix

Extract this ZIP directly into the BO3_HLSL_Previewer repository root.
It fixes the Phase 1o DirectX initialization error:
  X4567: maximum cbuffer exceeded. target has 14 slots, manual bind to slot 14 failed

The standalone APE shadow vertex shader now uses valid D3D11 constant-buffer slot b0 instead of invalid b14.
No captured APE lighting/probe/tonemap behavior is changed.

Verify with:
  .\VERIFY_PHASE1O1.ps1

The updated .\VERIFY_PHASE1O.ps1 is also included and accepts the hotfixed renderer.
