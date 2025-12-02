# HA6 Variable Swap Helper

> **Goal:** make it foolproof to install the prerequisites, build `ha6_var_tool`, and run it without touching raw command lines.

---

## 1. Install the build tools (one-time)

1. **Install Visual Studio Build Tools 2022** (free)
   - Download from <https://visualstudio.microsoft.com/downloads/> → select **"Build Tools for Visual Studio"**.
   - When the installer opens, check the workload **"Desktop development with C++"**.
   - Under Individual Components, make sure these are selected:
     - MSVC v143 build tools (Latest)
     - Windows 10/11 SDK (any recent version)
     - C++ CMake tools for Windows (optional but nice)
   - Click **Install** and wait. This gives you the MSVC compiler and the special command prompts.

2. **Install CMake**
   - Grab the Windows x64 Installer from <https://cmake.org/download/>.
   - During setup pick **"Add CMake to system PATH for all users"** (or at least for the current user).
   - Finish the install, then open a new PowerShell window and run `cmake --version` to make sure it responds.

3. *(Optional)* Install **Git for Windows** if you ever need to pull new updates: <https://git-scm.com/download/win>.

> If any command still says "not recognized", reboot or sign out/in so the PATH refreshes.

---

## 2. Build `ha6_var_tool`

1. Open the **x64 Native Tools Command Prompt for VS 2022** (find it in the Start Menu under *Visual Studio 2022* ▶ *Visual Studio Tools*).
2. In that prompt, go to the repo folder:
   ```cmd
   cd /d E:\Dev\ReplaceVar\Hantei-chan
   ```
3. Run CMake configure + build (copy/paste each line):
   ```cmd
   cmake -S . -B build
   cmake --build build --target ha6_var_tool --config Release
   ```
4. When it finishes you should have one of these files:
   - `build\ha6_var_tool.exe` (MinGW or Ninja single-config)
   - `build\Release\ha6_var_tool.exe` (Visual Studio multi-config)

> If you ever clean the repo or pull updates, just repeat step 3.

---

## 3. Run the friendly helper (no manual commands!)

We ship `tools\varswap\VarSwapHelper.ps1`, a PowerShell wrapper that guides you through scanning or replacing variables.

### Allow the script to run once (Windows safety gate)

Open PowerShell **as yourself** (not admin) and run:
```powershell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
```
Press `Y` when asked. You only do this one time so Windows lets local scripts run.

### Launch the helper

1. Right-click `VarSwapHelper.ps1` → **Run with PowerShell**, *or* run it manually:
   ```powershell
   cd E:\Dev\ReplaceVar\Hantei-chan\tools\varswap
   .\VarSwapHelper.ps1
   ```
2. Follow the prompts:
   - Choose **Scan** or **Replace**.
   - Drag a `.ha6` file into the window when it asks for a path.
   - Enter the variable number(s) when requested.
   - For Replace, choose whether to overwrite the original or let it create `*.varswap.ha6` automatically.
3. The script prints the exact command it runs plus the tool’s output. When it says **Done.**, you’re good.

### Need a richer, no-CLI workflow?

You now have two options:

**1. Full GUI Workbench (no PowerShell required)**

- Build once:
   ```powershell
   cmake --build build --target varswap_workbench --config Release
   ```
- Launch: `build\varswap_workbench.exe` (or `build\Debug\varswap_workbench.exe`).
- Features:
   - Open any `.ha6` file outside of Hantei.
   - Same VarSwap table UI (filters, summary sidebar, editable rows, Apply/Clear buttons).
   - Remembers pending edits per row and tracks dirty state in the window title/status bar.
   - Write changes back to the original file via **File → Save** or choose a new target with **Save As**.
   - Drag-and-drop a file onto the window or pass a path on the command line to auto-load.

**2. Guided PowerShell flow**

Run `VarSwapWorkbench.ps1`. It will:

1. Ask for the `.ha6` file once (drag-and-drop works).
2. Run a scan and display every variable ID with counts, pattern names, and node summaries so you have full context.
3. Let you pick the IDs to change and enter their new values in sequence (it shows the first few occurrences for each one as a reminder).
4. Ask where to save the edited file (in-place or a copy) and then apply every replacement back-to-back, logging them automatically via `ha6_var_tool`.

Because it drives the executable for you, you never touch raw command-line arguments—just answer the prompts.

### Need to re-run with the same options?
You can pass switches to skip prompts, e.g.:
```powershell
# Example: scan for variable 3
./VarSwapHelper.ps1 -Action scan -File "D:\Moves\Seifuku.ha6" -Var 3

# Example: replace var 2 with 5, write new file, but preview first
./VarSwapHelper.ps1 -Action replace -File "D:\Moves\Seifuku.ha6" -From 2 -To 5 -DryRun
```
All parameters are optional—you can mix and match prompts + switches.

---

## 4. Direct commands (for future reference)

If you ever want to run the executable yourself:

> Every successful `replace` run now appends the touched pattern list to `*_varswap_log.csv` next to the output file. Pass `--log <path>` to override the destination or `--no-log` to skip logging entirely.
```powershell
# Scan everything in the file
build\ha6_var_tool.exe scan --file path\to\file.ha6

# Scan only variable 4
build\ha6_var_tool.exe scan --file path\to\file.ha6 --var 4

# Replace variable 7 with 9 and write a new file
build\ha6_var_tool.exe replace --file path\to\file.ha6 --from 7 --to 9 --out path\to\file.varswap.ha6

# Replace in-place (make a backup first!)
build\ha6_var_tool.exe replace --file path\to\file.ha6 --from 7 --to 9 --in-place

# Example: write the log somewhere specific
build\ha6_var_tool.exe replace --file path\to\file.ha6 --from 7 --to 9 --in-place --log D:\Reports\file_log.csv
```

---

## 5. Troubleshooting checklist

| Symptom | Fix |
| --- | --- |
| `cmake` not recognized | Re-open the VS Native Tools prompt or reinstall CMake with the “Add to PATH” option. |
| `cl.exe` missing | You didn’t install the C++ workload in VS Build Tools—rerun the installer and add “Desktop development with C++”. |
| Script says it can’t find `ha6_var_tool.exe` | Run the build steps in section 2. |
| PowerShell blocks the helper script | Run `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned` once. |
| Tool crashes on a file | Share the `.ha6` file + exact command so we can reproduce. |

That’s all! Once the prerequisites are in place, double-click `VarSwapHelper.ps1`, answer a few questions, and the tedious variable swaps become a 30-second chore.
