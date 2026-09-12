The frontend checks run against the installed Windows Qt kit:

```powershell
python tests/frontend/audit.py --qt-bin C:/Qt/6.11.1/msvc2022_64/bin
C:/Qt/6.11.1/msvc2022_64/bin/qmltestrunner.exe -input tests/frontend -platform offscreen
dist/BO3HLSLPreviewer.exe --qml-smoke-test
dist/BO3HLSLPreviewer.exe --frontend-smoke-test
```

The two application smoke tests need a visible desktop; launching them with a hidden window prevents the first-frame check from succeeding. The integration test uses temporary INI settings and exercises the real hidden backend and native preview. It checks mode/target/mesh state, QML panel population, themes, resizing, Full Preview, saved-project reload, and runtime QML diagnostics. A PASS message must be accompanied by process exit code zero; it is not sufficient to see the message while the process remains alive.

The static audit suppresses only unqualified-access warnings because `frontend` is an injected context property and the components intentionally use their creation context. It separately checks every referenced frontend member against the bridge declaration and verifies the resource manifest. All other Qt diagnostics fail the audit.

Color tests exercise the actual QML conversion/entry functions and popup lifetime. These automated checks do not prove that a compositor never displays a black frame or that an object remains circular during a live mouse drag; those require interactive observation on the target display/GPU.
