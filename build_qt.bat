@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

cls
echo ========================================
echo   BO3 HLSL Previewer - Qt 6 Build
echo   DirectX 11 backend + Qt Quick/QML UI
echo   CMake is NOT required.
echo ========================================
echo.

set "QMAKE_EXE="
set "QT_BIN="
set "QT_VERSION="
set "QT_VERSION_FILE=%TEMP%\bo3_hlsl_previewer_qt_version_%RANDOM%.txt"

rem ------------------------------------------------------------
rem 1) QTDIR, if the user has explicitly configured it.
rem ------------------------------------------------------------
if defined QTDIR (
    if exist "%QTDIR%\bin\qmake.exe" (
        set "QMAKE_EXE=%QTDIR%\bin\qmake.exe"
        call :ValidateQt6
    )
)

rem ------------------------------------------------------------
rem 2) Normal Qt Online Installer layout: C:\Qt\6.x\msvc*_64
rem    We intentionally do NOT run qmake inside FOR /F. The old
rem    script's nested cmd quoting was the cause of the '-query'
rem    error on paths containing spaces.
rem ------------------------------------------------------------
if not defined QMAKE_EXE if exist "C:\Qt" (
    for /d %%V in ("C:\Qt\6.*") do (
        for /d %%K in ("%%~fV\msvc*_64") do (
            if exist "%%~fK\bin\qmake.exe" (
                if not defined QMAKE_EXE (
                    set "QMAKE_EXE=%%~fK\bin\qmake.exe"
                    call :ValidateQt6
                )
            )
        )
    )
)

rem ------------------------------------------------------------
rem 3) qmake on PATH. Write WHERE output to a temporary file first
rem    so no quoted executable command is embedded in FOR /F.
rem ------------------------------------------------------------
if not defined QMAKE_EXE (
    set "QT_WHERE_FILE=%TEMP%\bo3_hlsl_previewer_qmake_%RANDOM%.txt"
    where.exe qmake.exe > "!QT_WHERE_FILE!" 2>nul
    if exist "!QT_WHERE_FILE!" (
        for /f "usebackq delims=" %%Q in ("!QT_WHERE_FILE!") do (
            if not defined QMAKE_EXE (
                set "QMAKE_EXE=%%~fQ"
                call :ValidateQt6
            )
        )
        del /q "!QT_WHERE_FILE!" >nul 2>nul
    )
)

if not defined QMAKE_EXE (
    echo Qt 6 MSVC 64-bit was not found.
    echo.
    echo Install Qt using the Qt Online Installer and select a Qt 6 MSVC kit,
    echo for example:
    echo   Qt 6.x ^> MSVC 2022 64-bit
    echo.
    echo The normal install location is:
    echo   C:\Qt\6.x.x\msvc2022_64
    echo.
    echo If Qt is installed somewhere else, set QTDIR to that kit folder.
    echo Example:
    echo   set QTDIR=D:\Qt\6.10.0\msvc2022_64
    echo   build_qt.bat
    echo.
    pause
    exit /b 1
)

for %%I in ("%QMAKE_EXE%") do set "QT_BIN=%%~dpI"

rem Query once more for display, using a normal direct command invocation.
> "%QT_VERSION_FILE%" "%QMAKE_EXE%" -query QT_VERSION 2>nul
set /p QT_VERSION=<"%QT_VERSION_FILE%"
del /q "%QT_VERSION_FILE%" >nul 2>nul

if not defined QT_VERSION set "QT_VERSION=Qt 6 (version query unavailable)"

echo Using Qt: %QT_VERSION%
echo   %QMAKE_EXE%
echo.

rem ------------------------------------------------------------
rem Set up the Microsoft x64 compiler if needed.
rem ------------------------------------------------------------
where.exe cl.exe >nul 2>nul
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

    if exist "!VSWHERE!" (
        set "VSROOT_FILE=%TEMP%\bo3_hlsl_previewer_vsroot_%RANDOM%.txt"
        "!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "!VSROOT_FILE!" 2>nul
        if exist "!VSROOT_FILE!" (
            set /p VSROOT=<"!VSROOT_FILE!"
            del /q "!VSROOT_FILE!" >nul 2>nul
        )
    )

    if defined VSROOT if exist "!VSROOT!\VC\Auxiliary\Build\vcvars64.bat" (
        echo Using Visual C++ environment:
        echo   !VSROOT!\VC\Auxiliary\Build\vcvars64.bat
        call "!VSROOT!\VC\Auxiliary\Build\vcvars64.bat" >nul
    )
)

where.exe cl.exe >nul 2>nul
if errorlevel 1 (
    echo.
    echo MSVC C++ compiler was not found.
    echo Install Visual Studio with Desktop development with C++.
    echo.
    pause
    exit /b 1
)

where.exe nmake.exe >nul 2>nul
if errorlevel 1 (
    echo.
    echo nmake.exe was not found in the Visual C++ environment.
    echo Make sure the MSVC C++ build tools are installed.
    echo.
    pause
    exit /b 1
)

set "PATH=%QT_BIN%;%PATH%"

rem ------------------------------------------------------------
rem TinyEXR dependency for native .EXR environment maps.
rem Pinned to v1.0.8 (legacy single-header API).
rem Dependency fetching is handled by PowerShell, not a batch subroutine.
rem ------------------------------------------------------------
set "TINYEXR_DIR=%CD%\third_party\tinyexr"
set "TINYEXR_VERSION=v1.0.8"
set "TINYEXR_MARKER=%TINYEXR_DIR%\tinyexr_%TINYEXR_VERSION%.installed"
set "TINYEXR_FETCHER=%CD%\tools\fetch_tinyexr.ps1"
if not exist "%TINYEXR_DIR%" mkdir "%TINYEXR_DIR%"

rem Remove an incompatible newer multi-header TinyEXR left by 0.9.7.
if exist "%TINYEXR_DIR%\tinyexr.h" (
    findstr /C:"exr_reader.hh" "%TINYEXR_DIR%\tinyexr.h" >nul 2>nul
    if not errorlevel 1 (
        echo Replacing incompatible TinyEXR header with pinned %TINYEXR_VERSION%...
        del /q "%TINYEXR_DIR%\tinyexr.h" >nul 2>nul
        del /q "%TINYEXR_DIR%\miniz.h" >nul 2>nul
        del /q "%TINYEXR_DIR%\miniz.c" >nul 2>nul
        del /q "%TINYEXR_MARKER%" >nul 2>nul
    )
)

if not exist "%TINYEXR_MARKER%" (
    if not exist "%TINYEXR_FETCHER%" goto :DependencyScriptMissing
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%TINYEXR_FETCHER%" -OutputDir "%TINYEXR_DIR%"
    if errorlevel 1 goto :DependencyFailed
)

if not exist "%TINYEXR_DIR%\tinyexr.h" goto :DependencyFailed
if not exist "%TINYEXR_DIR%\miniz.h" goto :DependencyFailed
if not exist "%TINYEXR_DIR%\miniz.c" goto :DependencyFailed

rem ------------------------------------------------------------
rem Clean build. This prevents a stale EXE/object from hiding a failure.
rem ------------------------------------------------------------
if exist build_qt rmdir /s /q build_qt
if exist dist rmdir /s /q dist
mkdir build_qt
mkdir dist

pushd build_qt

echo Running qmake...
"%QMAKE_EXE%" "..\BO3HLSLPreviewer.pro" -spec win32-msvc "CONFIG+=release"
if errorlevel 1 goto :BuildFailedFromBuildDir

echo.
echo Compiling...
nmake.exe /nologo
if errorlevel 1 goto :BuildFailedFromBuildDir

popd

if not exist "dist\BO3HLSLPreviewer.exe" (
    echo.
    echo Build completed but dist\BO3HLSLPreviewer.exe was not created.
    echo.
    pause
    exit /b 1
)

echo.
echo Deploying Qt runtime files...
if exist "%QT_BIN%windeployqt.exe" (
    "%QT_BIN%windeployqt.exe" --release --no-translations --qmldir "%CD%\ui\qml" --dir "%CD%\dist" "%CD%\dist\BO3HLSLPreviewer.exe"
    if errorlevel 1 echo Warning: windeployqt reported an error. The EXE itself was still built.
) else (
    echo Warning: windeployqt.exe was not found beside qmake.exe.
)

if exist bo3_compat xcopy /E /I /Y /Q bo3_compat dist\bo3_compat >nul
if exist shaders xcopy /E /I /Y /Q shaders dist\shaders >nul
if exist presets xcopy /E /I /Y /Q presets dist\presets >nul
if exist ui xcopy /E /I /Y /Q ui dist\ui >nul
if exist export_templates xcopy /E /I /Y /Q export_templates dist\export_templates >nul
if exist tests xcopy /E /I /Y /Q tests dist\tests >nul
if exist version.json copy /Y version.json dist\version.json >nul

echo.
echo ========================================
echo Build succeeded.
echo ========================================
echo.
echo Run:
echo   %CD%\dist\BO3HLSLPreviewer.exe
echo.
start "" "%CD%\dist"
pause
exit /b 0

:BuildFailedFromBuildDir
popd
goto :BuildFailed

:BuildFailed
echo.
echo ========================================
echo Build failed. The compiler errors are above.
echo ========================================
echo.
pause
exit /b 1

:: Dependency failure handlers
:DependencyScriptMissing
echo.
echo ========================================
echo EXR dependency helper is missing.
echo ========================================
echo.
echo Expected:
echo   %TINYEXR_FETCHER%
echo.
echo Re-extract the complete previewer ZIP and run build_qt.bat again.
echo.
pause
exit /b 1

:DependencyFailed
echo.
echo ========================================
echo EXR support dependency setup failed.
echo ========================================
echo.
echo The first 0.9.9 build downloads pinned TinyEXR v1.0.8/miniz.
echo Run build_qt.bat again after checking your internet connection.
echo Once downloaded, later builds reuse the local files.
echo.
pause
exit /b 1

rem ------------------------------------------------------------
rem Validate the current QMAKE_EXE without using FOR /F to execute
rem a quoted path. If it isn't Qt 6, clear QMAKE_EXE and continue.
rem ------------------------------------------------------------
:ValidateQt6
if not defined QMAKE_EXE exit /b 1
if not exist "%QMAKE_EXE%" (
    set "QMAKE_EXE="
    exit /b 1
)

> "%QT_VERSION_FILE%" "%QMAKE_EXE%" -query QT_VERSION 2>nul
if errorlevel 1 (
    del /q "%QT_VERSION_FILE%" >nul 2>nul
    set "QMAKE_EXE="
    exit /b 1
)

set "QT_TEST_VERSION="
set /p QT_TEST_VERSION=<"%QT_VERSION_FILE%"
del /q "%QT_VERSION_FILE%" >nul 2>nul

if /i "!QT_TEST_VERSION:~0,2!"=="6." exit /b 0

set "QMAKE_EXE="
exit /b 1

