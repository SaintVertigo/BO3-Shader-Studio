# BO3 Shader Studio - Automatic Tester Releases

This patch is based on BO3 Shader Studio 0.1.

## Changes

- Every push to `main` automatically builds, tests, packages, and publishes a Tester prerelease.
- Manual `workflow_dispatch` remains available for intentional Stable releases or version/note overrides.
- Automatic push releases read the clean visible version from `version.json` -> `displayVersion`.
- Internal GitHub run versions remain monotonic for updater ordering but are hidden from the normal update dialog.
- New releases only publish the clean assets:
  - `BO3_Shader_Studio.zip`
  - `BO3_Shader_Studio.zip.sha256`
  - `BO3_Shader_Studio_Update.zip`
  - `BO3_Shader_Studio_Update.zip.sha256`
- The temporary `BO3_HLSL_Previewer_Update_*` bridge alias is no longer generated on future releases.
- Release packaging starts from a clean `release_artifacts` directory to prevent stale legacy assets from leaking into new releases.
- Update dialogs now show the visible BO3 Shader Studio version instead of the hidden internal ordering version.
- When the visible version is unchanged, the dialog says a newer build is available rather than showing internal version numbers.
- Documentation now explains the push-to-main tester workflow and manual Stable flow.

The original BO3 Shader Studio 0.1 bridge release should remain on GitHub so users still on the old pre-rename updater can migrate.
