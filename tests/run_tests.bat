@echo off
rem  Builds and runs the standalone tests for the Bedrock extension and the
rem  route planner. Requires the same Qt/MinGW toolchain as the main build.
rem
rem  Set QTDIR and MINGW below (or in the environment) to your installation.
setlocal
cd /d %~dp0

if "%QTDIR%"=="" set QTDIR=C:\Qt\5.15.2\mingw81_64
if "%MINGW%"=="" set MINGW=C:\Qt\Tools\mingw810_64\bin
set QTBIN=%QTDIR%\bin
set PATH=%MINGW%;%QTBIN%;%PATH%

if not exist out mkdir out
set FAILED=0

echo === Bedrock structure placement (reference comparison) ===
gcc -O2 -fwrapv -DSTRUCT_CONFIG_OVERRIDE=1 -I.. -o out\test_bedrock.exe ^
    test_bedrock.c ..\src\bedrock.c ..\cubiomes\libcubiomes.a -lm
if errorlevel 1 goto fail
out\test_bedrock.exe || set FAILED=1

echo.
echo === Bedrock edition switch / biome parity ===
gcc -O2 -fwrapv -DSTRUCT_CONFIG_OVERRIDE=1 -I.. -o out\test_integration.exe ^
    test_integration.c ..\src\bedrock.c ..\cubiomes\libcubiomes.a -lm
if errorlevel 1 goto fail
out\test_integration.exe || set FAILED=1

echo.
echo === Structure dispatch per edition ===
g++ -O2 -fwrapv -DSTRUCT_CONFIG_OVERRIDE=1 -std=gnu++11 -I.. -o out\test_dispatch.exe ^
    test_dispatch.cpp ..\src\bedrock.c ..\cubiomes\libcubiomes.a -lm
if errorlevel 1 goto fail
out\test_dispatch.exe || set FAILED=1

echo.
echo === Route planner ===
g++ -O2 -fwrapv -DSTRUCT_CONFIG_OVERRIDE=1 -std=gnu++11 -finput-charset=UTF-8 -fexec-charset=UTF-8 ^
    -I.. -I..\src -I"%QTDIR%\include" -I"%QTDIR%\include\QtCore" -I"%QTDIR%\include\QtGui" -I"%QTDIR%\include\QtWidgets" ^
    -o out\test_route.exe test_route.cpp ..\src\route.cpp ..\cubiomes\libcubiomes.a ^
    -L"%QTDIR%\lib" -lQt5Core -lQt5Gui -lQt5Widgets -lm
if errorlevel 1 goto fail
out\test_route.exe || set FAILED=1

echo.
if "%FAILED%"=="1" (
    echo SOME TESTS FAILED
    exit /b 1
)
echo ALL TESTS PASSED
exit /b 0

:fail
echo BUILD FAILED
exit /b 1
