# Shader editor caret regression checklist

The disappearing native caret was caused by the D3D preview calling blocking
`Present(1, 0)` from the Qt UI thread every frame. That repeatedly starved Qt's
native caret blink/repaint timer. Preview presentation is now non-blocking and
the existing 16 ms Qt timer remains the frame pacer.

The editor also captures and restores cursor position, selection anchor,
vertical scroll, and horizontal scroll during Watch and explicit Reload. Focus
transitions request a native viewport repaint; no fake caret is drawn.

Manual Windows/Qt regression:

1. Click the shader editor and type continuously for several seconds.
2. Stop typing and verify that the native insertion caret blinks normally.
3. Move with arrow keys, then select text in both directions.
4. Leave Live compile enabled and repeat typing while preview animation runs.
5. Switch HLSL/PostFX/Material/Skybox modes and verify cursor/selection stability.
6. Modify the shader externally with Watch enabled; verify position, selection,
   and both scroll axes are restored as far as the new document length permits.
7. Use Reload and repeat the same checks.
8. Switch to another application and back, then resize/maximize the Previewer.
9. Verify the caret remains visible against every bundled dark/light theme.

Automated package regressions cover the non-blocking source/capture lifecycle
and policy changes. Native caret blinking itself remains a platform compositor
behavior and is therefore verified manually with this checklist.
