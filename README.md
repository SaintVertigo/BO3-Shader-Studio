# BO3 Shader Studio

**BO3 Shader Studio** is a Windows shader creation, preview, conversion, and export tool built specifically around **Call of Duty: Black Ops III**.

It is designed to make BO3 shader work easier for both beginners and advanced users. You can build screen effects, materials, and sky/environment shaders visually, preview them in real time, convert GLSL/Shadertoy-style shaders to HLSL, inspect BO3 depth behavior, and generate BO3-ready shader/package files without having to write every shader by hand.

> Current display version: **0.1**

---

## What BO3 Shader Studio Can Do

### Beginner Shader Builder
Create BO3-compatible effects without writing shader code.

Beginner mode includes three main shader types:

- **Screen Effect** — changes the rendered game screen with PostFX effects.
- **Object / Material** — changes the appearance of a model or material surface.
- **Sky / Environment** — creates and edits sky, atmosphere, sun, clouds, mountains, water, and other environment effects.

Effects use sliders, color controls, toggles, target modes, and live preview controls so changes can be tested visually.

### Advanced HLSL Editing
Advanced mode gives direct access to shader code for users who want full control over the generated or imported HLSL.

### BO3-Aware Previewing
Shader Studio includes multiple preview paths depending on what you are building:

- 2D screen preview
- 3D material/model preview
- **3D Skybox** preview
- **2D Sky Editor** with direct sun placement
- live game-image preview
- BO3 depth preview and diagnostics

For sky editing, the 3D and 2D views use the same shader settings. The 3D view is useful for checking how the environment wraps around the camera, while the 2D Sky Editor makes sun and time-of-day adjustments easier.

### Real BO3 Float-Z Depth Preview
Depth-based effects such as Cartoon Outlines, Ambient Occlusion, fog, and other scene-aware effects can use real BO3 depth data instead of a hand-drawn approximation.

Shader Studio includes a **BO3 Ground-Truth Float-Z Capture** workflow that can capture:

- the game color frame;
- real BO3 `floatZ` data;
- BO3's viewmodel/depth-hack classification;
- the captured `zNear` value.

Once a capture has been imported, BO3 does **not** need to remain open. The captured depth scene can be previewed offline inside Shader Studio.

Depth diagnostics include views such as:

- Raw Depth
- Linear Depth
- Depth Edges
- Viewmodel Mask
- World Mask
- Effect Mask

Depth-aware screen effects can also target:

- **Everything**
- **World Only**
- **Viewmodel Only**

### GLSL and Shadertoy Conversion
Shader Studio can convert GLSL/Shadertoy-style shader code into HLSL suitable for the Studio preview and BO3 export pipeline.

The converter handles a growing set of GLSL compatibility features, helper functions, matrix operations, texture sampling, macros, overloads, loops, Shadertoy inputs, and other common shader patterns.

### BO3 Export
Shader Studio can generate BO3-oriented output for supported shader types, including the HLSL and package/techset-related files required by the export workflow.

The exporter performs validation before completing a package so mapping or compatibility problems can be caught before the files are used in BO3.

---

# Installation

## Option 1 — Install a Release Build

This is the recommended method for most users.

1. Open the project's **GitHub Releases** page.
2. Download:

   ```text
   BO3_Shader_Studio.zip
   ```

3. Create a folder for Shader Studio, for example:

   ```text
   C:\BO3 Shader Studio
   ```

4. Extract **the entire ZIP** into that folder.

   Do not run the program directly from inside the ZIP. Shader Studio uses files and folders included beside the executable.

5. Run:

   ```text
   BO3HLSLPreviewer.exe
   ```

That is all that is required to launch Shader Studio.

### Updating

Release builds support the Studio's update workflow. When a newer build is available, you can update through the application rather than manually replacing every file.

---

## Option 2 — Build From Source

Use this if you are developing Shader Studio or testing source changes.

### Requirements

Install:

- **Visual Studio 2022** or the Visual Studio Build Tools
  - include **Desktop development with C++**;
- **Qt 6 MSVC 64-bit**
  - for example, `Qt 6.x > MSVC 2022 64-bit`;
- PowerShell and the normal Windows SDK/DirectX components installed with the Visual Studio C++ workload.

The normal Qt Online Installer path is similar to:

```text
C:\Qt\6.x.x\msvc2022_64
```

### Build

Open PowerShell or Command Prompt in the source folder and run:

```text
build_qt.bat
```

`build_vs2022.bat` can also be used; it forwards to the same Qt build process.

The build script will:

1. locate a Qt 6 MSVC kit;
2. locate/configure the Microsoft C++ compiler;
3. fetch the pinned TinyEXR dependency if needed;
4. build the application;
5. deploy the required Qt runtime files.

A successful build is placed in:

```text
dist\
```

Run:

```text
dist\BO3HLSLPreviewer.exe
```

### If Qt Is Installed Somewhere Else

Set `QTDIR` to your Qt MSVC kit before running the build script.

Example:

```bat
set QTDIR=D:\Qt\6.10.0\msvc2022_64
build_qt.bat
```

---

# Do I Need Black Ops III Installed?

**Not just to use Shader Studio.**

You can create shaders, edit effects, use the normal previews, convert GLSL, and work with previously captured BO3 depth scenes without BO3 running.

You need Black Ops III when you want to:

- test an exported shader in the actual game;
- capture a new **ground-truth Float-Z** preview scene;
- verify BO3-specific runtime behavior.

For using generated assets through the normal BO3 mod workflow, you should also have a working **Black Ops III Mod Tools** setup.

---

# Basic Workflow

For a new shader:

1. Open **BO3 Shader Studio**.
2. Choose **Beginner** or **Advanced**.
3. In Beginner mode, choose:
   - Screen Effect,
   - Object / Material, or
   - Sky / Environment.
4. Add effects with **Browse Effects**.
5. Adjust the effect controls while watching the preview.
6. Use the available preview mode that best matches the shader you are building.
7. When the shader is ready, choose **Export to Black Ops III**.
8. Review any required BO3 resource mappings and package validation messages.
9. Use the generated files in your BO3 workflow.

---

# Ground-Truth BO3 Depth Capture

To create a real BO3 depth preview scene:

1. In Shader Studio, open:

   ```text
   Tools > BO3 Ground-Truth Float-Z Capture...
   ```

2. Choose **Open Capture Shader**.
3. Export the capture shader as BO3 PostFX.
4. Map the capture resources as:

   ```text
   frameBuffer  -> resolvedScene
   DepthSampler -> floatZ
   ```

5. Run the capture shader in BO3.
6. Hide the HUD if necessary and take a **lossless, full-resolution PNG screenshot** of the complete 2x2 capture sheet.
7. Do not crop, resize, or save the capture as JPEG.
8. Return to Shader Studio and choose:

   ```text
   Tools > BO3 Ground-Truth Float-Z Capture... > Import Capture PNG...
   ```

9. Use the depth debug views to inspect the imported data.

After the capture imports successfully, BO3 can be closed and the scene can continue to be used offline.

---

# Project Notes

Detailed development notes, regression information, and current patch-specific changes are kept outside this README so the front page stays useful to new users.

See:

- `PATCH_NOTES.md` — current development and patch notes;
- `docs/` — technical research and workflow documentation;
- `bo3_compat/` — BO3 compatibility research and related notes;
- `updates/` — updater/release documentation.

---

## Status

BO3 Shader Studio is still under active development. Some shader combinations, BO3 mappings, or experimental features may require testing before they are considered fully validated in-game.

## Phase 1s APE direct-sun recovery
APE Match now uses the direct diffuse relationship measured from the paired APE captures (`linearAlbedo * exposedSunColor * NdotL`) and keeps the guessed sun-shadow map disabled until APE's `gSunShadowTree` selection data can be reconstructed. Run `VERIFY_PHASE1S.ps1` after applying the patch.

## Phase 1t APE direct-specular recovery

Phase 1t follows the user's Phase 1s comparison video with a literal translation of the captured APE direct-sun specular branch. The APE Match compositor now uses BO3's captured no-PI microfacet normalization, the captured `sqrt(alpha)` visibility mapping, and the adjacent rough-diffuse correction. The Phase 1s direct-sun energy/probe separation remains unchanged, and the incomplete shadow-tree replacement remains disabled until the real three-layer selection path is reconstructed.


## Phase 1u APE direct-specular numerator correction

Phase 1u fixes the final known translation error in the captured sun-specular branch: APE divides `alpha^2 * specScale` by `4 * visV * visL * Dden^2`; `NdotL` gates the branch and participates in `visL`, but is not present in the numerator. Phase 1t inserted that extra factor and suppressed the hotspot at grazing light angles. Phase 1s direct diffuse and the shadow-free baseline remain unchanged.


## Phase 1v APE world-frame alignment + direct-specular correction

Phase 1v corrects two capture-grounded issues exposed by the Phase 1u test. First, the APE compute shader really does multiply `alpha^2 * specScale` by `NdotL` before the visibility/distribution denominator; Phase 1u had mistaken the later shadow register for that value. Second, the paired capture's `camToWldMatrix` proves the old Y/Z-only world conversion was incomplete. APE's reference camera basis maps exactly into Studio with `StudioX=-ApeY`, `StudioY=ApeZ`, `StudioZ=ApeX`, and the captured reference camera elevation is 22.5 degrees. APE Match now uses that full conversion for direct-light vectors while preserving Phase 1s direct diffuse, the fixed probe, the captured `skyYaw=90-sunYaw` rule, and the shadow-free baseline.


## Phase 1x
APE Match Sphere now resolves the reference surface normal from reconstructed geometric position, matching APE's captured radial sphere normal field and preventing preview XMODEL/front-face winding from pinning the specular lobe to the rim. Preview-only generated material compilation preserves authored outward TBN orientation; BO3 export keeps native `SV_IsFrontFace`.


## Phase 1y

APE Match now preserves the reconstructed probe's mean energy while restoring the stronger directional variation visible in APE, and Reset frames the reference sphere at the APE-matched 5.25-radius dolly.
