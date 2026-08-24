@echo off
setlocal EnableExtensions
chcp 65001 >nul
cd /d "%~dp0"

if not exist ".frontend" mkdir ".frontend"
set "BOOT_LOG=.frontend\bootstrap_build.log"
set "CONTROL_EXE=.frontend\VRFullKeyboardControl.exe"
set "CONTROL_VERSION_FILE=.frontend\control_version.txt"
set "PROJECT_VERSION="
set /p PROJECT_VERSION=<VERSION

rem Daily launches do not rebuild the Control Center when the version matches.
if exist "%CONTROL_EXE%" if exist "%CONTROL_VERSION_FILE%" (
    set "BUILT_VERSION="
    set /p BUILT_VERSION=<"%CONTROL_VERSION_FILE%"
    if "%BUILT_VERSION%"=="%PROJECT_VERSION%" (
        start "" "%CONTROL_EXE%"
        exit /b 0
    )
)

> "%BOOT_LOG%" echo VR Full Keyboard Control Center bootstrap build log
>>"%BOOT_LOG%" echo Date: %date% %time%
>>"%BOOT_LOG%" echo Version: %PROJECT_VERSION%
>>"%BOOT_LOG%" echo.

set "CMAKE_EXE="
for /f "delims=" %%I in ('where cmake 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
if not defined CMAKE_EXE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if not defined CMAKE_EXE (
    >>"%BOOT_LOG%" echo ERROR: CMake was not found.
    echo CMake was not found.
    echo Install Visual Studio 2022 Desktop development with C++ and CMake tools.
    echo.
    echo Build log:
    echo %CD%\%BOOT_LOG%
    pause
    exit /b 1
)

>>"%BOOT_LOG%" echo CMake: %CMAKE_EXE%
>>"%BOOT_LOG%" echo.
>>"%BOOT_LOG%" echo ===== Configure Control Center Only =====
"%CMAKE_EXE%" -S . -B ".frontend\build-control" -G "Visual Studio 17 2022" -A x64 -DVRFK_BUILD_CORE=OFF >>"%BOOT_LOG%" 2>&1
if errorlevel 1 goto :fail

>>"%BOOT_LOG%" echo.
>>"%BOOT_LOG%" echo ===== Build Control Center =====
"%CMAKE_EXE%" --build ".frontend\build-control" --config Release --target VRFullKeyboardControl --parallel >>"%BOOT_LOG%" 2>&1
if errorlevel 1 goto :fail

copy /y ".frontend\build-control\Release\VRFullKeyboardControl.exe" "%CONTROL_EXE%" >nul
> "%CONTROL_VERSION_FILE%" echo %PROJECT_VERSION%
rmdir /s /q ".frontend\build-control" >nul 2>nul
attrib +h ".frontend" >nul 2>nul

start "" "%CONTROL_EXE%"
exit /b 0

:fail
echo.
echo Control Center build failed.
echo Full build log:
echo %CD%\%BOOT_LOG%
echo.
type "%BOOT_LOG%"
pause
exit /b 1
