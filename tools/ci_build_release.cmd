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

set "BO3_TINYEXR_READY=0"
if /I "%BO3_CI_FAST%"=="1" (
    if exist "%CD%\third_party\tinyexr\tinyexr_v1.0.8.installed" if exist "%CD%\third_party\tinyexr\tinyexr.h" if exist "%CD%\third_party\tinyexr\miniz.h" if exist "%CD%\third_party\tinyexr\miniz.c" set "BO3_TINYEXR_READY=1"
)
if "%BO3_TINYEXR_READY%"=="1" (
    echo EXR support ready: TinyEXR v1.0.8 ^(vendored fast path^)
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\tools\fetch_tinyexr.ps1" -OutputDir "%CD%\third_party\tinyexr"
    if errorlevel 1 exit /b 1
)

if exist build_qt rmdir /s /q build_qt
if exist dist rmdir /s /q dist
mkdir build_qt
mkdir dist

call :resolve_parallel_make

if /I "%BO3_CI_FAST%"=="1" (
    call :build_with_cache
    if errorlevel 1 (
        echo.
        echo WARNING: Cached tester build failed. Retrying once with the normal MSVC compiler.
        echo          This keeps tester publishing reliable even if sccache/setup has a transient problem.
        if exist build_qt rmdir /s /q build_qt
        mkdir build_qt
        call :build_normal
        if errorlevel 1 exit /b 1
    )
) else (
    call :build_normal
    if errorlevel 1 exit /b 1
)

if not exist "dist\BO3HLSLPreviewer.exe" (
    echo ERROR: dist\BO3HLSLPreviewer.exe was not produced.
    exit /b 1
)

if /I "%BO3_CI_FAST%"=="1" if /I "%BO3_LEAN_UPDATE%"=="1" (
    rem Lean automatic tester packages never resend Qt runtime DLLs. Qt's bin
    rem directory is already on PATH from install-qt-action, so the regression
    rem executable can still load Qt directly on the runner. Avoid spending time
    rem copying a full deployment tree that package_github_release.ps1 discards.
    echo Lean tester build: skipping windeployqt ^(Qt runtime is already on runner PATH and is not part of the lean payload^).
) else (
    where windeployqt.exe >nul 2>nul
    if errorlevel 1 (
        echo ERROR: windeployqt.exe is not on PATH.
        exit /b 1
    )
    windeployqt.exe --release --no-translations --dir "%CD%\dist" "%CD%\dist\BO3HLSLPreviewer.exe"
    if errorlevel 1 exit /b 1
)

if exist bo3_compat xcopy /E /I /Y /Q bo3_compat dist\bo3_compat >nul
if exist shaders xcopy /E /I /Y /Q shaders dist\shaders >nul
if exist presets xcopy /E /I /Y /Q presets dist\presets >nul
if exist ui xcopy /E /I /Y /Q ui dist\ui >nul
if exist export_templates xcopy /E /I /Y /Q export_templates dist\export_templates >nul
if exist tests xcopy /E /I /Y /Q tests dist\tests >nul
if exist version.json copy /Y version.json dist\version.json >nul

exit /b 0

:build_with_cache
echo Fast tester build: attempting compiler-cache path.

rem mozilla-actions/sccache-action currently exposes SCCACHE_PATH without an
rem .exe suffix on some Windows runners. cmd.exe's IF EXIST does not perform
rem PATHEXT expansion, so normalize it before qmake consumes the variable.
set "BO3_SCCACHE_EXE="
if defined SCCACHE_PATH (
    if exist "%SCCACHE_PATH%" set "BO3_SCCACHE_EXE=%SCCACHE_PATH%"
    if not defined BO3_SCCACHE_EXE if exist "%SCCACHE_PATH%.exe" set "BO3_SCCACHE_EXE=%SCCACHE_PATH%.exe"
)
if not defined BO3_SCCACHE_EXE (
    for /f "delims=" %%I in ('where sccache.exe 2^>nul') do if not defined BO3_SCCACHE_EXE set "BO3_SCCACHE_EXE=%%I"
)
if not defined BO3_SCCACHE_EXE (
    echo WARNING: sccache.exe was not found. Falling back to the normal MSVC build.
    exit /b 1
)
for %%I in ("%BO3_SCCACHE_EXE%") do set "BO3_SCCACHE_DIR=%%~dpI"
set "PATH=%BO3_SCCACHE_DIR%;%PATH%"
where sccache.exe >nul 2>nul
if errorlevel 1 (
    echo WARNING: resolved sccache.exe could not be invoked from PATH.
    exit /b 1
)
set "SCCACHE_PATH=sccache"
echo Compiler cache: %BO3_SCCACHE_EXE%
echo Compiler launcher command: sccache

pushd build_qt
rem qmake's NMake generator batches many .cpp files into one cl.exe command by
rem default. sccache cannot cache that form (it reports "multiple input files"),
rem which made the supposed fast path rebuild almost the entire application on
rem every clean GitHub runner. no_batch emits one compiler invocation per source
rem so unchanged translation units are real remote-cache hits.
qmake.exe "..\BO3HLSLPreviewer.pro" -spec win32-msvc "CONFIG+=release" "CONFIG+=bo3_sccache" "CONFIG+=no_batch"
if errorlevel 1 (
    popd
    exit /b 1
)
call :run_make
set "BO3_BUILD_RC=!ERRORLEVEL!"
popd
exit /b !BO3_BUILD_RC!

:build_normal
echo Full compiler path: normal MSVC build.
pushd build_qt
qmake.exe "..\BO3HLSLPreviewer.pro" -spec win32-msvc "CONFIG+=release" "CONFIG+=no_batch"
if errorlevel 1 (
    popd
    exit /b 1
)
call :run_make
set "BO3_BUILD_RC=!ERRORLEVEL!"
popd
exit /b !BO3_BUILD_RC!

:resolve_parallel_make
rem NMake has no parallel job scheduler. qmake no_batch exposes one rule per
rem translation unit, and Qt's jom can execute those independent rules across
rem the runner's cores. Keep a verified pinned helper, but never make network
rem availability a release blocker: nmake remains the fallback.
set "BO3_JOM_EXE="
for /f "delims=" %%I in ('where jom.exe 2^>nul') do if not defined BO3_JOM_EXE set "BO3_JOM_EXE=%%I"
if not defined BO3_JOM_EXE (
    set "BO3_JOM_DIR=%TEMP%\bo3_shader_studio_jom_1_1_7"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\tools\fetch_jom.ps1" -OutputDir "!BO3_JOM_DIR!"
    if not errorlevel 1 if exist "!BO3_JOM_DIR!\jom.exe" set "BO3_JOM_EXE=!BO3_JOM_DIR!\jom.exe"
)
if defined BO3_JOM_EXE (
    echo Parallel make: !BO3_JOM_EXE!
) else (
    echo WARNING: jom is unavailable; build will use single-threaded nmake.
)
exit /b 0

:run_make
if defined BO3_JOM_EXE (
    if defined NUMBER_OF_PROCESSORS (
        "!BO3_JOM_EXE!" -j !NUMBER_OF_PROCESSORS!
    ) else (
        "!BO3_JOM_EXE!"
    )
) else (
    nmake.exe /nologo
)
exit /b !ERRORLEVEL!
