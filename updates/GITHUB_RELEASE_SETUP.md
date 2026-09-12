# GitHub release updater setup

BO3 Shader Studio is configured for GitHub-hosted Windows builds and in-application updates.

## Normal tester workflow

After the first GitHub-enabled build is installed, **you do not manually run Actions for ordinary tester updates**.

Every push to `main` automatically:

1. installs the required Qt/MSVC environment on a GitHub Windows runner;
2. builds BO3 Shader Studio;
3. runs the GLSL, Shadertoy, PostFX export, and BO3 package regression suites;
4. blocks the release if any regression fails;
5. creates `BO3_Shader_Studio.zip` and `BO3_Shader_Studio_Update.zip` plus SHA-256 files;
6. publishes a GitHub prerelease on the Tester channel.

Typical update flow:

```text
edit files -> git commit -> git push -> GitHub builds/releases -> testers update in-app
```

The visible version is read from `version.json` -> `displayVersion`. It can stay at the current visible line (now `0.2`) across many tester builds. GitHub uses a separate monotonically increasing internal version only for update ordering; that number is intentionally hidden from the normal update UI.

## Changing the visible version

The project is now on visible version `0.2`. For the next visible release, change:

```json
"displayVersion": "0.3"
```

in `version.json`, commit, and push. The resulting automatic tester release is titled **BO3 Shader Studio 0.3**.

## Stable releases

Use **Actions -> Build and Release BO3 Shader Studio -> Run workflow** only when you intentionally want a manual release. Choose the **stable** channel and optionally override the visible version and notes.

Stable releases are normal GitHub releases. Tester releases are prereleases. Stable-channel users are not offered prerelease tester builds.

## Release assets

New releases intentionally contain only the clean product assets:

- `BO3_Shader_Studio.zip`
- `BO3_Shader_Studio.zip.sha256`
- `BO3_Shader_Studio_Update.zip`
- `BO3_Shader_Studio_Update.zip.sha256`

GitHub also exposes its automatic source-code archives. The old `BO3_HLSL_Previewer_*` bridge asset is no longer generated on new releases. The original 0.1 bridge release can remain available so users still on the pre-rename updater can migrate.

## Public release access

Do not embed a GitHub PAT/token in the desktop application. Anonymous updater checks require a public repository (or a separate public release repository).
