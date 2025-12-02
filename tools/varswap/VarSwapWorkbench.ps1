[CmdletBinding()]
param(
    [string]$FilePath
)

function Normalize-PathInput {
    param([string]$Value)
    if (-not $Value) { return "" }
    return $Value.Trim().Trim('"')
}

function Resolve-Executable {
    param([string]$RepoRoot)
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

function Join-ArgumentString {
    param([string[]]$Arguments)
    return ($Arguments | ForEach-Object {
        if ($_ -match '\s') {
            '"' + $_.Replace('"', '\"') + '"'
        } else {
            $_
        }
    }) -join ' '
}

function Invoke-VarTool {
    param(
        [string]$Executable,
        [string[]]$Arguments
    )
    Write-Host "Running: $Executable $($Arguments -join ' ')" -ForegroundColor DarkGray
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Executable
    $psi.Arguments = Join-ArgumentString -Arguments $Arguments
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $process = [System.Diagnostics.Process]::Start($psi)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    return [PSCustomObject]@{
        ExitCode = $process.ExitCode
        StdOut = $stdout
        StdErr = $stderr
    }
}

function Parse-Occurrences {
    param([string[]]$ScanOutput)
    $pattern = '^- Pattern (?<pat>\d{3}) (?<name>.*?), Frame (?<frame>\d+), (?<node>.*), var (?<var>-?\d+), raw (?<raw>-?\d+)'
    $result = @()
    foreach ($line in $ScanOutput) {
        $matched = [regex]::Match($line, $pattern)
        if ($matched.Success) {
            $varId = [int]$matched.Groups['var'].Value
            $patIndex = [int]$matched.Groups['pat'].Value
            $patName = $matched.Groups['name'].Value.Trim()
            $node = $matched.Groups['node'].Value.Trim()
            $frame = [int]$matched.Groups['frame'].Value
            $raw = [int]$matched.Groups['raw'].Value
            $result += [PSCustomObject]@{
                Var = $varId
                PatternIndex = $patIndex
                PatternName = $patName
                PatternLabel = ("{0:D3} {1}" -f $patIndex, $patName)
                Frame = $frame
                Node = $node
                Raw = $raw
            }
        }
    }
    return $result
}

function Get-VarSummary {
    param([array]$Occurrences)
    $grouped = $Occurrences | Group-Object Var
    $summary = foreach ($group in $grouped) {
        $patterns = $group.Group | Select-Object -ExpandProperty PatternLabel -Unique | Select-Object -First 4
        $nodes = $group.Group | Select-Object -ExpandProperty Node -Unique | Select-Object -First 2
        [PSCustomObject]@{
            Var = [int]$group.Name
            Count = $group.Count
            Patterns = ($patterns -join '; ')
            SampleNodes = ($nodes -join ' | ')
        }
    }
    return $summary | Sort-Object Var
}

function Show-VarDetails {
    param(
        [int]$Var,
        [array]$Occurrences
    )
    Write-Host "-- Var $Var occurrences --" -ForegroundColor Cyan
    $Occurrences |
        Sort-Object PatternIndex, Frame |
        Select-Object -First 12 |
        ForEach-Object {
            Write-Host ("  Pattern {0:D3} {1}, Frame {2}, {3}" -f $_.PatternIndex, $_.PatternName, $_.Frame, $_.Node)
        }
    if ($Occurrences.Count -gt 12) {
        Write-Host "  ...and $($Occurrences.Count - 12) more" -ForegroundColor DarkGray
    }
}

function Prompt-VarSelection {
    param([array]$Summary)
    if (-not $Summary) {
        return @()
    }
    Write-Host "Detected variables:" -ForegroundColor Cyan
    $Summary | Format-Table -AutoSize
    $available = $Summary.Var
    while ($true) {
        $input = Read-Host "Enter variable IDs to change (comma separated) or press Enter to exit"
        if ([string]::IsNullOrWhiteSpace($input)) {
            return @()
        }
        $tokens = $input -split '[,\s]+' | Where-Object { $_ }
        try {
            $selection = $tokens | ForEach-Object { [int]$_ }
        } catch {
            Write-Host "Please enter only numbers." -ForegroundColor Red
            continue
        }
        $missing = $selection | Where-Object { $available -notcontains $_ }
        if ($missing) {
            Write-Host "Unknown variable(s): $($missing -join ', ')" -ForegroundColor Red
            continue
        }
        return $selection | Select-Object -Unique | Sort-Object
    }
}

function Prompt-Mappings {
    param(
        [int[]]$SelectedVars,
        [hashtable]$Lookup
    )
    if (-not $SelectedVars) {
        return @()
    }
    $mappings = @()
    $lastSuggestion = $null
    foreach ($var in $SelectedVars) {
        $occ = $Lookup[$var]
        if ($occ) {
            Show-VarDetails -Var $var -Occurrences $occ
        }
        $default = if ($lastSuggestion) { $lastSuggestion + 1 } else { "" }
        while ($true) {
            $prompt = if ($default) { "New ID for var $var [$default]" } else { "New ID for var $var" }
            $response = Read-Host $prompt
            if ([string]::IsNullOrWhiteSpace($response) -and $default) {
                $response = $default
            }
            if ([string]::IsNullOrWhiteSpace($response)) {
                Write-Host "A new ID is required." -ForegroundColor Red
                continue
            }
            try {
                $newVar = [int]$response
                $mappings += [PSCustomObject]@{ From = $var; To = $newVar }
                $lastSuggestion = $newVar
                break
            } catch {
                Write-Host "Please enter a valid number." -ForegroundColor Red
            }
        }
    }
    return $mappings
}

function Choose-OutputTarget {
    param(
        [string]$SourceFile
    )
    $defaultOut = [System.IO.Path]::Combine(
        [System.IO.Path]::GetDirectoryName($SourceFile),
        ([System.IO.Path]::GetFileNameWithoutExtension($SourceFile) + "_workbench" + [System.IO.Path]::GetExtension($SourceFile))
    )
    $choice = Read-Host "Write changes into the original file? (y/N)"
    if ($choice -match '^(y|yes)$') {
        return [PSCustomObject]@{ Mode = 'InPlace'; Target = $SourceFile }
    }
    $dest = Read-Host "Where should the edited copy be saved? [$defaultOut]"
    if ([string]::IsNullOrWhiteSpace($dest)) {
        $dest = $defaultOut
    }
    $dest = Normalize-PathInput $dest
    Copy-Item -LiteralPath $SourceFile -Destination $dest -Force
    Write-Host "Created working copy: $dest" -ForegroundColor Green
    return [PSCustomObject]@{ Mode = 'Copy'; Target = (Resolve-Path $dest).Path }
}

function Apply-Replacements {
    param(
        [string]$Executable,
        [string]$TargetFile,
        [array]$Mappings
    )
    $results = @()
    foreach ($mapping in $Mappings) {
        $args = @(
            "replace","--file", $TargetFile,
            "--from", $mapping.From,
            "--to", $mapping.To,
            "--in-place"
        )
        $invocation = Invoke-VarTool -Executable $Executable -Arguments $args
        Write-Host $invocation.StdOut
        if ($invocation.StdErr) {
            Write-Host $invocation.StdErr -ForegroundColor Yellow
        }
        if ($invocation.ExitCode -ne 0) {
            throw "Replacement for var $($mapping.From) failed with exit code $($invocation.ExitCode)."
        }
        $results += $mapping
    }
    return $results
}

# Main flow
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$exe = Resolve-Executable -RepoRoot $repoRoot
if (-not $exe) {
    Write-Host "ha6_var_tool.exe was not found. Build it first using CMake." -ForegroundColor Red
    exit 1
}

$FilePath = Normalize-PathInput $FilePath
if (-not $FilePath) {
    $FilePath = Normalize-PathInput (Read-Host "Drag a .ha6 file here or type the path")
}
if (-not (Test-Path $FilePath)) {
    Write-Host "File not found: $FilePath" -ForegroundColor Red
    exit 1
}
$resolvedFile = (Resolve-Path $FilePath).Path

Write-Host "Scanning $resolvedFile ..." -ForegroundColor Cyan
$scanResult = Invoke-VarTool -Executable $exe -Arguments @("scan","--file", $resolvedFile)
if ($scanResult.ExitCode -ne 0) {
    Write-Host $scanResult.StdOut
    Write-Host $scanResult.StdErr -ForegroundColor Red
    Write-Host "Scan failed." -ForegroundColor Red
    exit $scanResult.ExitCode
}

$scanLines = ($scanResult.StdOut + "`n" + $scanResult.StdErr).Split([Environment]::NewLine)
$occurrences = Parse-Occurrences -ScanOutput $scanLines
if (-not $occurrences) {
    Write-Host "No variable references found." -ForegroundColor Yellow
    exit 0
}
$lookup = @{}
foreach ($occ in $occurrences) {
    if (-not $lookup.ContainsKey($occ.Var)) {
        $lookup[$occ.Var] = @()
    }
    $lookup[$occ.Var] += $occ
}
$summary = Get-VarSummary -Occurrences $occurrences

$selected = Prompt-VarSelection -Summary $summary
if (-not $selected) {
    Write-Host "No variables selected. Exiting." -ForegroundColor Yellow
    exit 0
}

$mappings = Prompt-Mappings -SelectedVars $selected -Lookup $lookup
if (-not $mappings) {
    Write-Host "No mappings entered. Exiting." -ForegroundColor Yellow
    exit 0
}

Write-Host "Planned changes:" -ForegroundColor Cyan
$mappings | Format-Table -AutoSize
$confirm = Read-Host "Apply these changes? (Y/n)"
if ($confirm -match '^(n|no)$') {
    Write-Host "Aborted by user." -ForegroundColor Yellow
    exit 0
}

$targetInfo = Choose-OutputTarget -SourceFile $resolvedFile

try {
    Apply-Replacements -Executable $exe -TargetFile $targetInfo.Target -Mappings $mappings | Out-Null
    Write-Host "All replacements applied to $($targetInfo.Target)." -ForegroundColor Green
    Write-Host "Check the generated *_varswap_log.csv for a full pattern list." -ForegroundColor DarkGray
} catch {
    Write-Host $_ -ForegroundColor Red
    Write-Host "At least one replacement failed. Review the output above." -ForegroundColor Red
    exit 1
}
