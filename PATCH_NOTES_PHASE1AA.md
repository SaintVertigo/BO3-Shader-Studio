# Phase 1aa - APE-Like Preview Resize Range

The comparison video showed a separate UI/layout mismatch from the camera/framing work: APE lets the material Preview pane continue following the mouse much farther horizontally and vertically, while Studio visibly stops even though the cursor keeps moving.

Root cause: Studio uses QMainWindow dock areas rather than a plain splitter. Qt was still deriving hard dock limits from child minimumSizeHint() values even after explicit minimumSize(0,0) calls. Three contributors were especially important:

- QStackedWidget folded the hidden Beginner builder page into the central widget minimum even while Advanced mode was active.
- The dense Preview toolbar row contributed a large horizontal minimum through its container layout.
- Dock contents and the bottom tool area still contributed vertical minimum hints to QMainWindow's dock separator solver.

Phase 1aa changes the layout contract rather than the camera:

- Adds `ApeResizableStack`, whose minimumSizeHint is `(0,0)` so hidden authoring pages cannot clamp Preview growth.
- Adds `ApeResizableDockWidget`, whose dock minimum hint no longer inherits dense child-control minima.
- Adds `ApeCollapsibleContainer` for Preview and Compiler Output so long toolbar/control rows can clip internally at extreme sizes instead of stopping the drag.
- Forces dock content widgets and both authoring pages to minimum `(0,0)` with ignored size policies where appropriate.
- Keeps the Direct3D preview's small safety floor (48x24) and its live 16 ms swapchain resize path.
- Does not alter Phase 1z camera/FOV/framing, Phase 1x hotspot normal recovery, or any lighting equations.

Expected behavior: when dragging Studio's Preview boundaries horizontally or vertically, the boundary should continue tracking the mouse through a much larger range, similar to APE. At very small pane sizes some toolbar controls may clip, which is intentional and preferable to a hard resize stop.
