@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0\.."

echo ========================================
echo   BO3 Shader Studio - CI Release Build
echo ========================================

where qmake.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: qmake.exe is not on PATH.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe was not found.
    exit /b 1
)

set "VSROOT="
set "VSROOT_FILE=%TEMP%\bo3_hlsl_previewer_ci_vsroot_%RANDOM%.txt"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSROOT_FILE%" 2>nul
if exist "%VSROOT_FILE%" (
    set /p VSROOT=<"%VSROOT_FILE%"
    del /q "%VSROOT_FILE%" >nul 2>nul
)
if not defined VSROOT (
    echo ERROR: Visual Studio C++ build tools were not found.
    exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\tools\fetch_tinyexr.ps1" -OutputDir "%CD%\third_party\tinyexr"
if errorlevel 1 exit /b 1

if exist build_qt rmdir /s /q build_qt
if exist dist rmdir /s /q dist
mkdir build_qt
mkdir dist

pushd build_qt
if /I "%BO3_CI_FAST%"=="1" (
    echo Fast tester build: compiler cache enabled.
    if defined SCCACHE_PATH (
        if not exist "%SCCACHE_PATH%" (
            echo ERROR: SCCACHE_PATH does not point to an existing sccache executable.
            goto :fail_from_build
        )
    ) else (
        where sccache.exe >nul 2>nul
        if errorlevel 1 (
            echo ERROR: Fast CI requested, but sccache.exe was not found.
            goto :fail_from_build
        )
    )
    qmake.exe "..\BO3HLSLPreviewer.pro" -spec win32-msvc "CONFIG+=release" "CONFIG+=bo3_sccache"
) else (
    echo Full build: compiler cache launcher disabled.
    qmake.exe "..\BO3HLSLPreviewer.pro" -spec win32-msvc "CONFIG+=release"
)
if errorlevel 1 goto :fail_from_build
nmake.exe /nologo
if errorlevel 1 goto :fail_from_build
popd

if not exist "dist\BO3HLSLPreviewer.exe" (
    echo ERROR: dist\BO3HLSLPreviewer.exe was not produced.
    exit /b 1
)

where windeployqt.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: windeployqt.exe is not on PATH.
    exit /b 1
)
windeployqt.exe --release --no-translations --dir "%CD%\dist" "%CD%\dist\BO3HLSLPreviewer.exe"
if errorlevel 1 exit /b 1

if exist bo3_compat xcopy /E /I /Y /Q bo3_compat dist\bo3_compat >nul
if exist shaders xcopy /E /I /Y /Q shaders dist\shaders >nul
if exist presets xcopy /E /I /Y /Q presets dist\presets >nul
if exist ui xcopy /E /I /Y /Q ui dist\ui >nul
if exist export_templates xcopy /E /I /Y /Q export_templates dist\export_templates >nul
if exist tests xcopy /E /I /Y /Q tests dist\tests >nul
if exist version.json copy /Y version.json dist\version.json >nul

exit /b 0

:fail_from_build
popd
exit /b 1
