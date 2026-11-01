@echo off
rem Builds TarnishedHUD.dll, TarnishedVitals.dll and BarModsAudit.exe with MSVC into out\.
rem Usage: build.cmd   (NT_VCVARS overrides the vcvars64.bat path)
setlocal
if not defined NT_VCVARS set "NT_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%NT_VCVARS%" >nul 2>nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
if not exist build mkdir build
set CFLAGS=/nologo /std:c11 /O2 /W4 /WX /utf-8 /DNDEBUG /D_CRT_SECURE_NO_WARNINGS %EXTRA_CFLAGS%
cl %CFLAGS% /DTV_HUD_ONLY /LD /Fo:build\hud.obj /Fe:out\TarnishedHUD.dll src\TarnishedVitals.c version.lib user32.lib /link %EXTRA_LDFLAGS% || exit /b 1
cl %CFLAGS% /DTV_VITALS_ONLY /LD /Fo:build\vitals.obj /Fe:out\TarnishedVitals.dll src\vitals\TarnishedVitals.c version.lib user32.lib /link %EXTRA_LDFLAGS% || exit /b 1
cl %CFLAGS% /DTV_HUD_ONLY /DTV_AUDIT /Fo:build\audit.obj /Fe:out\BarModsAudit.exe src\audit.c version.lib user32.lib || exit /b 1
del /q out\*.exp out\*.lib 2>nul
echo Built: out\TarnishedHUD.dll out\TarnishedVitals.dll out\BarModsAudit.exe
