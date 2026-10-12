<#
Adaptive-widescreen differential regression check (headless, Windows).

The game's save states do not capture animation timers, so two independent
*real-time* runs of the same state drift apart (walking actors, typewriter
text). This tool avoids that entirely by using the runtime's deterministic
frame stepping (`--frames`): both runs advance the SAME number of emulated
frames from the SAME state, so the only remaining difference is rendering.

For every *.state in -StateDir it:

  1. runs the recompiled game headless at the widescreen width,
  2. runs it headless at the native 240x160 width,
  3. compares the central 240-wide strip of the wide frame against the native
     frame, pixel for pixel,

and reports the percentage of differing pixels per state. The widescreen
enhancement must be a pure superset of the native image: the authored 240x160
region (the strip the two share) has to be identical, so anything above
-MaxDiffPercent is a rendering regression.

Exit code is non-zero if any state exceeds the tolerance or fails to run, which
makes this usable as a pre-release gate.

The tool never writes the game ROM, BIOS, or save states into the tree; it uses
a scratch directory under the system temp folder.

Usage:
  pwsh tools\regression_check.ps1 -StateDir C:\states\buus_fury
  pwsh tools\regression_check.ps1 -StateDir ..\captures -Frames 120
#>
param(
    [Parameter(Mandatory = $true)][string]$StateDir,
    [string]$Rom = '',
    [string]$Bios = '',
    [string]$BuildDir = '',
    [string]$Exe = '',
    [int]$Frames = 90,
    [int]$NativeWidth = 240,
    [int]$WideWidth = 480,
    [ValidateRange(0.0, 100.0)][double]$MaxDiffPercent = 0.05,
    [int]$PixelThreshold = 8
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $Exe) {
    $dir = if ($BuildDir) { (Resolve-Path $BuildDir).Path } else { Join-Path $root 'build' }
    $Exe = Join-Path $dir 'DragonBallZBuusFuryRecomp.exe'
}
if (-not (Test-Path -LiteralPath $Exe)) { throw "Missing executable: $Exe" }

# ROM/BIOS live beside the executable or one level above the build tree.
$searchDirs = @(
    (Split-Path -Parent $Exe),
    $root,
    (Split-Path -Parent $root)
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -Unique

if (-not $Bios) {
    $Bios = ($searchDirs | ForEach-Object { Join-Path $_ 'gba_bios.bin' } |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1)
}
if (-not $Bios -or -not (Test-Path -LiteralPath $Bios)) {
    throw "Missing GBA BIOS. Place gba_bios.bin beside the exe or pass -Bios <path>."
}

# A save state is bound to the exact ROM it was captured against (the runtime
# verifies the ROM SHA-1 on load), so collect every dump we can find and let the
# state pick the one that loads. -Rom pins a single dump.
$preferredNames = @(
    'Dragon Ball Z - Buu''s Fury (USA).gba',
    'Dragon Ball Z - Buu''s Fury (U) [!].gba',
    'Dragon Ball Z - Buu''s Fury (Spanish).gba'
)
$romCandidates = @()
if ($Rom) {
    if (-not (Test-Path -LiteralPath $Rom)) { throw "ROM not found: $Rom" }
    $romCandidates = @($Rom)
} else {
    foreach ($dir in $searchDirs) {
        foreach ($name in $preferredNames) {
            $candidate = Join-Path $dir $name
            if (Test-Path -LiteralPath $candidate) { $romCandidates += $candidate }
        }
    }
    $found = @($searchDirs | ForEach-Object {
            Get-ChildItem -LiteralPath $_ -Filter '*.gba' -File -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty FullName
        })
    $romCandidates += ($found | Where-Object { $romCandidates -notcontains $_ })
}
$romCandidates = @($romCandidates | Select-Object -Unique)
if ($romCandidates.Count -eq 0) {
    throw "No ROM found. Pass -Rom <path to your legally obtained .gba>."
}

$stateRoot = (Resolve-Path -LiteralPath $StateDir).Path
$states = @(Get-ChildItem -LiteralPath $stateRoot -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like '*.state' -or $_.Name -like '*.state1' } |
    Sort-Object FullName)
if ($states.Count -eq 0) { throw "No .state files under $stateRoot." }

# Adaptive widescreen is owned by a mod package and is disabled by default, so
# --view-width alone never widens the frame (the mod state is authoritative).
# Enable every feature of the catalog next to the executable for the duration
# of the check, then restore whatever was there before.
$exeDir = Split-Path -Parent $Exe
$modsDir = Join-Path $exeDir 'mods'
$modStatePath = Join-Path $modsDir 'state.toml'
$modStateBackup = $null
$modEnabled = $false
$packagesDir = Join-Path $modsDir 'packages'
if (Test-Path -LiteralPath $packagesDir) {
    $manifests = @(Get-ChildItem -LiteralPath $packagesDir -Recurse -File -Filter 'manifest.toml')
    # Prefer the highest package version (stale build copies can linger).
    $manifest = $manifests |
        Sort-Object -Property {
            [version](Split-Path -Leaf (Split-Path -Parent $_.FullName))
        } -Descending | Select-Object -First 1
    if ($manifest) {
        $text = Get-Content -LiteralPath $manifest.FullName -Raw
        $pkgId = [regex]::Match($text, '(?m)^\s*id\s*=\s*"([^"]+)"').Groups[1].Value
        $pkgVer = [regex]::Match($text, '(?m)^\s*version\s*=\s*"([^"]+)"').Groups[1].Value
        $featIds = [regex]::Matches($text, '(?s)\[\[feature\]\](.*?)(?=\n\[\[|\Z)') |
            ForEach-Object {
                [regex]::Match($_.Groups[1].Value, '(?m)^\s*id\s*=\s*"([^"]+)"').Groups[1].Value
            }
        if ($pkgId -and $pkgVer -and $featIds.Count -gt 0) {
            if (Test-Path -LiteralPath $modStatePath) {
                $modStateBackup = Get-Content -LiteralPath $modStatePath -Raw
            }
            $sb = [System.Text.StringBuilder]::new()
            [void]$sb.AppendLine('format_version = 1')
            [void]$sb.AppendLine('')
            [void]$sb.AppendLine('[[package]]')
            [void]$sb.AppendLine("id = `"$pkgId`"")
            [void]$sb.AppendLine("version = `"$pkgVer`"")
            foreach ($fid in $featIds) {
                [void]$sb.AppendLine('')
                [void]$sb.AppendLine('[[feature]]')
                [void]$sb.AppendLine("package_id = `"$pkgId`"")
                [void]$sb.AppendLine("id = `"$fid`"")
                [void]$sb.AppendLine('enabled = true')
            }
            New-Item -ItemType Directory -Force -Path $modsDir | Out-Null
            Set-Content -LiteralPath $modStatePath -Value $sb.ToString() -Encoding ascii
            $modEnabled = $true
            Write-Host "Enabled mod package $pkgId $pkgVer ($($featIds -join ', ')) for the check."
        }
    }
}

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ('buus_fury_regression_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $scratch | Out-Null

function Invoke-Frame([string]$state, [int]$width, [string]$pngOut, [string]$rom) {
    $save = Join-Path $scratch ("save_$width.sav")
    # The runtime logs to stderr; merging it with 2>&1 under a global
    # ErrorActionPreference=Stop turns each log line into a terminating error,
    # so relax it around the native call.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & $Exe --bios $Bios --rom $rom --save-path $save `
            --view-width $width --no-window --quiet `
            --load-state $state --frames $Frames --dump-png $pngOut 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($code -ne 0) {
        $tail = (@($out | Select-Object -Last 3) -join ' | ')
        throw "runtime exited $code ($tail)"
    }
    if (-not (Test-Path -LiteralPath $pngOut)) { throw "no frame was dumped" }
}

function Compare-Strip([string]$widePng, [string]$nativePng) {
    $wide = [System.Drawing.Bitmap]::FromFile($widePng)
    $native = [System.Drawing.Bitmap]::FromFile($nativePng)
    try {
        $height = [Math]::Min($wide.Height, $native.Height)
        if ($height -le 0) { throw "empty frame" }
        $offsetX = [int](($wide.Width - $native.Width) / 2)
        if ($wide.Width -le $native.Width) {
            throw "wide frame ($($wide.Width)) did not widen past native ($($native.Width)): the adaptive-view mod is disabled or not expanding"
        }
        $diff = 0
        for ($y = 0; $y -lt $height; $y++) {
            for ($x = 0; $x -lt $native.Width; $x++) {
                $a = $wide.GetPixel($offsetX + $x, $y)
                $b = $native.GetPixel($x, $y)
                $d = [Math]::Max(
                    [Math]::Abs($a.R - $b.R),
                    [Math]::Max([Math]::Abs($a.G - $b.G), [Math]::Abs($a.B - $b.B)))
                if ($d -gt $PixelThreshold) { $diff++ }
            }
        }
        return ($diff / ([double]($native.Width * $height)) * 100.0)
    } finally {
        $wide.Dispose()
        $native.Dispose()
    }
}

$failures = 0
$results = @()
try {
    foreach ($state in $states) {
        $label = $state.FullName.Substring($stateRoot.Length).TrimStart('\', '/')
        $widePng = Join-Path $scratch 'wide.png'
        $nativePng = Join-Path $scratch 'native.png'
        $pct = [double]::NaN
        $usedRom = $null
        $lastError = $null
        foreach ($rom in $romCandidates) {
            try {
                Invoke-Frame $state.FullName $WideWidth $widePng $rom
                Invoke-Frame $state.FullName $NativeWidth $nativePng $rom
                $pct = Compare-Strip $widePng $nativePng
                $usedRom = $rom
                break
            } catch {
                $lastError = "$_"
            }
        }
        if (-not $usedRom) {
            $failures++
            $results += [pscustomobject]@{ State = $label; DiffPercent = [double]::NaN; Status = "FAIL ($lastError)" }
            continue
        }
        $ok = $pct -le $MaxDiffPercent
        if (-not $ok) { $failures++ }
        $results += [pscustomobject]@{ State = $label; DiffPercent = $pct; Status = ($(if ($ok) { 'PASS' } else { 'FAIL' })) }
    }
} finally {
    if ($modEnabled) {
        if ($null -ne $modStateBackup) {
            Set-Content -LiteralPath $modStatePath -Value $modStateBackup -Encoding ascii
        } else {
            Remove-Item -LiteralPath $modStatePath -Force -ErrorAction SilentlyContinue
        }
    }
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}

$results | Sort-Object -Property DiffPercent -Descending | ForEach-Object {
    $pct = if ([double]::IsNaN($_.DiffPercent)) { '       -' } else { '{0,8:F4}' -f $_.DiffPercent }
    Write-Host ("{0}%  {1,-5}  {2}" -f $pct, $_.Status, $_.State)
}

$passed = $results.Count - $failures
Write-Host "$passed / $($results.Count) states within tolerance ($MaxDiffPercent%)."
if ($failures -gt 0) {
    Write-Error "$failures state(s) diverged beyond tolerance."
    exit 1
}
Write-Host 'Adaptive widescreen differential check: PASS.'
