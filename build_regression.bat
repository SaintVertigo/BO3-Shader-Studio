@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "ISOLATED_BUILD=0"
if /I "%~1"=="--isolated" set "ISOLATED_BUILD=1"

if not exist "build_qt\Makefile" (
    echo Incremental build is not configured.
    echo Run build_qt.bat once to create build_qt\Makefile, then retry.
    exit /b 2
)

where.exe cl.exe >nul 2>nul
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo Visual Studio locator was not found.
        exit /b 2
    )

    set "VSROOT_FILE=%TEMP%\bo3_hlsl_previewer_incremental_vsroot_%RANDOM%.txt"
    "!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "!VSROOT_FILE!" 2>nul
    if exist "!VSROOT_FILE!" (
        set /p VSROOT=<"!VSROOT_FILE!"
        del /q "!VSROOT_FILE!" >nul 2>nul
    )
    if not defined VSROOT (
        echo Visual Studio with the MSVC x64 tools was not found.
        exit /b 2
    )
    call "!VSROOT!\VC\Auxiliary\Build\vcvars64.bat" >nul
)

where.exe nmake.exe >nul 2>nul
if errorlevel 1 (
    echo nmake.exe was not found in the MSVC environment.
    exit /b 2
)

if "!ISOLATED_BUILD!"=="1" if not exist "build_qt\regression_dist" mkdir "build_qt\regression_dist"

pushd build_qt
if "!ISOLATED_BUILD!"=="1" (
    rem Keep an open GUI build untouched while reusing the incremental object files.
    rem The Makefile's normal target remains dist\BO3HLSLPreviewer.exe; overriding
    rem only the linker destination produces an ignored automation executable.
    nmake.exe /nologo DESTDIR_TARGET=regression_dist\BO3HLSLPreviewer.exe ..\dist\BO3HLSLPreviewer.exe
) else (
    nmake.exe /nologo
)
set "BUILD_RESULT=!errorlevel!"
popd
if not "!BUILD_RESULT!"=="0" exit /b !BUILD_RESULT!

set "REGRESSION_DIST=dist"
if "!ISOLATED_BUILD!"=="1" set "REGRESSION_DIST=build_qt\regression_dist"
if not exist "!REGRESSION_DIST!\BO3HLSLPreviewer.exe" (
    echo Build succeeded but !REGRESSION_DIST!\BO3HLSLPreviewer.exe is missing.
    exit /b 2
)

if "!ISOLATED_BUILD!"=="1" (
    copy /Y "dist\*.dll" "!REGRESSION_DIST!\" >nul
    for %%D in (bo3_compat platforms) do (
        if exist "dist\%%D" xcopy /E /I /Y /Q "dist\%%D" "!REGRESSION_DIST!\%%D" >nul
    )
    if exist "dist\version.json" copy /Y "dist\version.json" "!REGRESSION_DIST!\version.json" >nul
)
if exist shaders xcopy /E /I /Y /Q shaders "!REGRESSION_DIST!\shaders" >nul
if exist tests xcopy /E /I /Y /Q tests "!REGRESSION_DIST!\tests" >nul
exit /b 0
