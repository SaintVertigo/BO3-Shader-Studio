@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0\.."

set "OUTPUT=%CD%\build_qt\BO3ShaderStudioLauncher.exe"
set "OBJECT=%CD%\build_qt\BO3ShaderStudioLauncher.obj"
set "SOURCE=%CD%\tools\launcher_main.cpp"

if not exist "%SOURCE%" (
    echo ERROR: launcher source was not found: %SOURCE%
    exit /b 1
)
if not exist "%CD%\build_qt" mkdir "%CD%\build_qt"

where.exe cl.exe >nul 2>nul
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo ERROR: Visual Studio C++ build tools were not found.
        exit /b 1
    )

    set "VSROOT_FILE=%TEMP%\bo3_shader_studio_launcher_vsroot_%RANDOM%.txt"
    "!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "!VSROOT_FILE!" 2>nul
    if exist "!VSROOT_FILE!" (
        set /p VSROOT=<"!VSROOT_FILE!"
        del /q "!VSROOT_FILE!" >nul 2>nul
    )
    if not defined VSROOT (
        echo ERROR: Visual Studio C++ build tools were not found.
        exit /b 1
    )
    call "!VSROOT!\VC\Auxiliary\Build\vcvars64.bat" >nul
    if errorlevel 1 exit /b 1
)

if exist "%OUTPUT%" del /q "%OUTPUT%" >nul 2>nul
if exist "%OBJECT%" del /q "%OBJECT%" >nul 2>nul

cl.exe /nologo /O2 /MT /EHsc /std:c++17 /DUNICODE /D_UNICODE /Fo:"%OBJECT%" /Fe:"%OUTPUT%" "%SOURCE%" user32.lib shell32.lib /link /SUBSYSTEM:WINDOWS
if errorlevel 1 (
    echo ERROR: clean-release launcher build failed.
    exit /b 1
)
if not exist "%OUTPUT%" (
    echo ERROR: launcher compiler succeeded but the EXE was not created.
    exit /b 1
)

echo Clean-release launcher: %OUTPUT%
exit /b 0
