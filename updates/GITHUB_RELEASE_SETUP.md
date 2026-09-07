# GitHub release updater setup

The repository is ready for GitHub-hosted builds and in-application updates.

## First-time setup

1. Push this project to a **public GitHub repository** (or use a separate public repository for releases).
2. Open the repository's **Actions** tab.
3. Run **Build and Release BO3 HLSL Previewer**.
4. Enter a version such as `0.20.0-test.1`, choose **tester**, and optionally enter release notes.
5. When the workflow succeeds, download the full `BO3_HLSL_Previewer_<version>.zip` once for each tester who does not already have a GitHub-enabled build.
6. From then on their app can update itself through **Help → Check for Updates...**. Tester builds default to the Tester channel.

Stable releases use a version such as `0.20.0` with the **stable** channel. Stable users will not be offered prerelease tester builds.

## What the workflow does

- installs Qt 6.8 MSVC 2022 on a Windows GitHub runner
- fetches the pinned TinyEXR dependency
- builds the Qt/D3D11 application
- runs GLSL, Shadertoy, PostFX export, and BO3 package regressions
- stamps `version.json` with the release version, repository, and default channel
- creates a full distribution ZIP
- creates the in-app update ZIP with `update_manifest.json` + `payload/`
- publishes the files on a GitHub Release

## Release naming

Tester examples:

- `0.20.0-test.1`
- `0.20.0-test.2`
- `0.20.1-test.1`

Stable examples:

- `0.20.0`
- `0.20.1`

The updater uses SemVer-style ordering, so `test.10` correctly sorts after `test.9`, and a stable `0.20.0` sorts after `0.20.0-test.9`.

## Private source repository

Do not put a GitHub PAT/token into the desktop app. If the source must remain private, publish updater releases from a separate public release repository and stamp that repository into `version.json`.
