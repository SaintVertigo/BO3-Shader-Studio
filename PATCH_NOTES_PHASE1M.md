# Phase 1m — APE Coupled Light Rig + Full Pole Rotation

This patch corrects the APE light-manipulator behavior confirmed by the latest
reference video/screenshots.

- Shift+LMB now rotates the **APE lighting rig**, not only the sun.
  The HDR background, reflection lookup, diffuse SH orientation, and sun move
  together.
- Vertical light dragging no longer clamps at +/-89 degrees. The light can pass
  over/under the model repeatedly through complete rotations, like APE.
- APE environment orientation now has both yaw and pitch. Pitch is passed through
  `previewApeSettings.w` to the visible environment and material-lighting shader.
- Scene / Lighting `Light yaw` and `Light pitch` use the same coupled APE rig.
- The Light pitch slider is widened to -180..180 degrees; mouse dragging continues
  seamlessly across the wrap point.
- Applying/resetting Morning / Day / Sunset / Night restores that preset's absolute
  sky orientation after the SSI light direction is installed.
- Documentation is corrected: the dark moving patch in the supplied APE close-ups
  is genuine APE viewport behavior, not the mouse cursor. The safe half-vector
  guard remains only as a numerical safety check.

Retained from Phase 1l: source-structured BO3 gloss/direct-specular work, processed
probe path, APE display calibration, native-resolution HDR source, corrected BO3
GBuffer gloss unpacking, APE reference meshes, and Maya-style camera navigation.
