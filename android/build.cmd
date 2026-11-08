@echo off
rem Builds the Android app: build.cmd [gradle task, default assembleRelease]
rem The Visual Studio environment is for juceaide, the small Windows tool JUCE builds for itself during configure.
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%i"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not defined JAVA_HOME set "JAVA_HOME=C:\Program Files\Android\Android Studio\jbr"
set "TASK=%~1"
if "%TASK%"=="" set "TASK=assembleRelease"
call "%~dp0gradlew.bat" %TASK% --console=plain
