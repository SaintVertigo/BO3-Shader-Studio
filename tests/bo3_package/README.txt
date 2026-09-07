BO3 package regression fixtures
===============================

Run:

  BO3HLSLPreviewer.exe --bo3-package-regression-all

The compact fixtures are structurally derived from BO3-Shader-Research's basic,
decal_emissive_reveal, and endportal example packages. They exercise includes,
Globals/RenderFlags, parameters and Tweak
metadata, technique aliases/inheritance, defines append, stage source
inheritance, stage inheritance, and distinct TOOLSGFX/runtime resource branches.

The suite compiles and reflects the paired VS/PS, then covers valid and invalid
resource bindings, optimized-out resources, constant matching, CodeTexture
types, VS/PS semantics, render-state interpretation, and source-path rules.
