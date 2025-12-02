@echo off
setlocal
cd /d "%~dp0"
if not exist build (
    echo Configuring VarSwap Workbench project...
    cmake -S . -B build -A x64 || goto :error
)
cmake --build build --target varswap_workbench || goto :error
set EXE=build\Debug\varswap_workbench.exe
if exist build\Release\varswap_workbench.exe set EXE=build\Release\varswap_workbench.exe
if not exist "%EXE%" (
    echo Could not find varswap_workbench.exe after build.
    goto :error
)
start "VarSwap Workbench" "%EXE%"
exit /b 0

:error
echo Build failed. See output above.
pause
exit /b 1
