# VarSwap Workbench (standalone)

This folder contains a tiny CMake project that builds the VarSwap Workbench UI without touching the rest of Hantei-chan. The sources and dependencies still live in `../Hantei-chan`, but the build directory, cache, and generated executable are isolated here.

## Build steps

```pwsh
cd VarSwapWorkbench
cmake -S . -B build -A x64   # run once to generate the solution
cmake --build build --config Debug --target varswap_workbench
```

Pass `-DHANTEI_ROOT=E:/Some/Other/Path` to the first command if you move the Hantei tree elsewhere.

The resulting executable sits in `VarSwapWorkbench/build/Debug/varswap_workbench.exe` (or `Release`).

## Launch helper

After the first build you can simply run:

```pwsh
VarSwapWorkbench\run_varswap_workbench.bat
```

The script will rebuild the project if needed and start the freshly built executable.
