# BO3 HLSL Previewer update packages

After version 0.6.0 is installed, normal users should not need to rebuild the program for updates.

An update is a ZIP with this layout:

```
update_manifest.json
payload/
  version.json
  BO3HLSLPreviewer.exe       # only when native code changed
  bo3_compat/                # only when compatibility files changed
  presets/                   # only when defaults/profiles changed
  ui/                        # only when theme/UI data changed
  shaders/                   # optional samples
  ...any other changed runtime files
```

Required manifest example:

```json
{
  "product": "BO3 HLSL Previewer",
  "format": 1,
  "version": "0.5.1",
  "notes": "Short description of the update"
}
```

The app's **Install Update...** action closes the running previewer, extracts the package to a temporary staging directory, copies only the files in `payload/` over the installation, and reopens the app.

A ZIP can also be dragged onto the previewer window.


## GitHub Releases / automatic updater

The application can discover these packages from GitHub Releases when `version.json` contains:

```json
{
  "githubRepository": "owner/repository",
  "updateAssetPrefix": "BO3_HLSL_Previewer_Update_",
  "defaultUpdateChannel": "stable"
}
```

The included GitHub Actions workflow stamps these fields automatically. Update assets are named `BO3_HLSL_Previewer_Update_<version>.zip`; the app deliberately ignores the separate full-distribution ZIP.

Stable uses GitHub's latest non-prerelease release. Tester scans published releases and chooses the highest compatible SemVer-style version, including prereleases. Before an automatic install, the downloaded ZIP must match the SHA-256 digest exposed for the GitHub Release asset.

For anonymous clients the GitHub Release repository must be public. Do not ship a GitHub personal access token inside the Previewer.
