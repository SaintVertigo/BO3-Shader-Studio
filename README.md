# BO3 Shader Studio 0.2

The current 0.2 build makes APE Match GDT/SSI-driven for the stock Morning, Day, Sunset and Night environments, loads the official APE reference sphere/cube/plane/cylinder/monkey meshes from a local BO3 Mod Tools install when available, and records the `t7_script_wall` / `core_script_wall_c` reference-material contract. See `PATCH_NOTES_0_2.md` and `presets/ape_stock_reference.json` for the exact source-derived values.

## Phase 1ac
APE Match now preserves native BO3 XMODEL_BIN runtime UVs exactly (fixing the remaining sphere checker parity), gives the procedural APE sphere the equivalent base V correction, uses a fixed 4.83-radius Reset camera, removes the last 48x24 Preview floor, mip-filters the visible Day environment at LOD 0.35, and applies the final mean-preserving probe directional-range calibration. Phase 1x hotspot recovery remains untouched.

Phase 1z established the full capture-derived world conversion for environment/probe directions and the captured Day probe orientation (~134.75 degrees). Its 4.85-radius framing experiment was superseded by later A/B calibration; Phase 1ac is the current framing baseline.


## Phase 1aa

Studio Preview docks now use APE-like collapsible minimum hints so horizontal and vertical pane resizing follows the mouse through a much larger range instead of hitting hidden Qt layout clamps.


## Phase 1ab
APE Match now aligns the native APE preview-mesh frame/UV orientation, separates visible-sky and baked-probe Day yaw, uses a fixed 4.38-radius Reset camera, and retains APE-like unconstrained preview resizing.


## Phase 1ad
APE Match separates visible-sky handedness from the baked material-probe frame, restores the remaining side-orbit probe range, and rebalances Night so broad direct diffuse no longer overwhelms APE's dark indirect response while preserving the compact specular hotspot.
