param(
    [ValidateSet("scan", "replace", "")]
    [string]$Action = "",
    [string]$File = "",
    [string]$Var = "",
    [string]$From = "",
    [string]$To = "",
    [switch]$DryRun,
    [string]$Out = "",
    [switch]$InPlace
)

function Resolve-Executable {
    param(
        [string]$RepoRoot
    )

    $candidates = @(
        Join-Path $RepoRoot "build/ha6_var_tool.exe",
        Join-Path $RepoRoot "build/Release/ha6_var_tool.exe",
        Join-Path $RepoRoot "build/Debug/ha6_var_tool.exe"
    )

    foreach ($path in $candidates) {
        if (Test-Path $path) {
            return $path
        }
    }

    return $null
}

function Prompt-ForValue {
    param(
        [string]$Message,
        [string]$Default = ""
    )

    if ($Default) {
        $prompt = "$Message [$Default]"
    } else {
        $prompt = $Message
    }

    $response = Read-Host $prompt
    if ([string]::IsNullOrWhiteSpace($response)) {
        return $Default
    }
    return $response
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$exe = Resolve-Executable -RepoRoot $repoRoot

if (-not $exe) {
    Write-Host "❌ ha6_var_tool.exe was not found." -ForegroundColor Red
    Write-Host "Run CMake to build it first:"
    Write-Host "  cmake -S . -B build"
    Write-Host "  cmake --build build --target ha6_var_tool --config Release"
    Write-Host "If you installed Visual Studio, use the 'x64 Native Tools Command Prompt' before running CMake."
    exit 1
}

if (-not $Action) {
    Write-Host "What do you want to do?" -ForegroundColor Cyan
    Write-Host "  1) Scan a .ha6 file for variable uses"
    Write-Host "  2) Replace one variable ID with another"
    $choice = Read-Host "Enter 1 or 2"
    switch ($choice) {
        "1" { $Action = "scan" }
        "2" { $Action = "replace" }
        default {
            Write-Host "Invalid choice. Exiting." -ForegroundColor Red
            exit 1
        }
    }
}

if (-not $File) {
    $File = Prompt-ForValue -Message "Drag a .ha6 file here or type the path"
}

if (-not (Test-Path $File)) {
    Write-Host "File not found: $File" -ForegroundColor Red
    exit 1
}

$arguments = @($Action, "--file", (Resolve-Path $File))

switch ($Action) {
    "scan" {
        if (-not $Var) {
            $Var = Prompt-ForValue -Message "Optional: only show a specific variable number" -Default ""
        }
        if ($Var) {
            $arguments += @("--var", $Var)
        }
    }
    "replace" {
        if (-not $From) {
            $From = Prompt-ForValue -Message "Which variable number do you want to change?"
        }
        if (-not $To) {
            $To = Prompt-ForValue -Message "What is the new variable number?"
        }
        if ([string]::IsNullOrWhiteSpace($From) -or [string]::IsNullOrWhiteSpace($To)) {
            Write-Host "Both --from and --to values are required." -ForegroundColor Red
            exit 1
        }
        $arguments += @("--from", $From, "--to", $To)

        if ($DryRun) {
            $arguments += "--dry-run"
        } elseif ($InPlace) {
            $arguments += "--in-place"
        } elseif ($Out) {
            $arguments += @("--out", $Out)
        } else {
            $response = Prompt-ForValue -Message "Save over the original file? (yes = in-place / anything else = new file)" -Default "no"
            if ($response -match "^(y|yes)$") {
                $arguments += "--in-place"
            } else {
                $defaultOut = [System.IO.Path]::ChangeExtension($File, ".varswap" + [System.IO.Path]::GetExtension($File))
                $chosenOut = Prompt-ForValue -Message "Where should the new file go?" -Default $defaultOut
                $arguments += @("--out", $chosenOut)
            }
        }
    }
    default {
        Write-Host "Unsupported action: $Action" -ForegroundColor Red
        exit 1
    }
}

Write-Host "Running: $exe $($arguments -join ' ')" -ForegroundColor DarkGray
& $exe @arguments

if ($LASTEXITCODE -eq 0) {
    Write-Host "Done." -ForegroundColor Green
} else {
    Write-Host "ha6_var_tool exited with code $LASTEXITCODE" -ForegroundColor Red
}
