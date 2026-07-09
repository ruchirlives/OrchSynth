@echo off
setlocal

if /i "%1"=="--skip-build" goto :build-installer

echo === Building OrchSynth Release ===
if not exist build mkdir build
cd build
cmake .. -A x64
cmake --build . --config Release --parallel
cd ..

echo === Copying VST3 to standard system folder ===
:: Create standard VST3 folders if they don't exist
if not exist "C:\Program Files\Common Files\VST3\OrchSynth.vst3\Contents\x86_64-win" mkdir "C:\Program Files\Common Files\VST3\OrchSynth.vst3\Contents\x86_64-win" 2>nul
if not exist "C:\Program Files\Common Files\VST3\OrchSynth.vst3\Contents\Resources" mkdir "C:\Program Files\Common Files\VST3\OrchSynth.vst3\Contents\Resources" 2>nul

:: Copy only the specific required VST3 bundle files
copy /y "build\VST3\Release\OrchSynth.vst3\Contents\x86_64-win\OrchSynth.vst3" "C:\Program Files\Common Files\VST3\OrchSynth.vst3\Contents\x86_64-win\" >nul
set COPY_OK=%errorlevel%
copy /y "build\VST3\Release\OrchSynth.vst3\Contents\x86_64-win\faust.dll" "C:\Program Files\Common Files\VST3\OrchSynth.vst3\Contents\x86_64-win\" >nul
copy /y "build\VST3\Release\OrchSynth.vst3\Contents\Resources\moduleinfo.json" "C:\Program Files\Common Files\VST3\OrchSynth.vst3\Contents\Resources\" >nul
if exist "build\VST3\Release\OrchSynth.vst3\PlugIn.ico" copy /y "build\VST3\Release\OrchSynth.vst3\PlugIn.ico" "C:\Program Files\Common Files\VST3\OrchSynth.vst3\" >nul

:: Copy standard Faust libraries (.lib) required for JIT compilation
if not exist "C:\Program Files\Common Files\VST3\OrchSynth.vst3\Contents\x86_64-win\faust" mkdir "C:\Program Files\Common Files\VST3\OrchSynth.vst3\Contents\x86_64-win\faust" 2>nul
copy /y "third_party\libfaust\share\faust\*.lib" "C:\Program Files\Common Files\VST3\OrchSynth.vst3\Contents\x86_64-win\faust\" >nul

if %COPY_OK% neq 0 (
    echo [WARNING] Failed to copy VST3 to C:\Program Files\Common Files\VST3.
    echo Please run this script as Administrator to install it locally.
) else (
    echo VST3 copied successfully to C:\Program Files\Common Files\VST3
)

if /i "%1"=="--skip-installer" goto :done
if /i "%1"=="--no-installer" goto :done

:build-installer
echo === Building Installer ===
if not exist installer mkdir installer
"%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" installer.iss

:done
echo === Done ===
if not "%1"=="--skip-installer" if not "%1"=="--no-installer" echo Installer output: installer\OrchSynth-Setup.exe
pause
