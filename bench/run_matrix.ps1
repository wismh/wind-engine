<#
.SYNOPSIS
    Runs the wind_ui_bench matrix, one process per row, and writes summary.md and summary.json.

.DESCRIPTION
    Reads the rows from `wind_ui_bench --list`, keeps those matching -Filter, and runs each one -Repeat times as a
    separate process, sequentially (pass 1 over every row, then pass 2, ...). Raw reports go to <OutDir>/runs. A
    non-zero exit of the bench stops the script with exit code 1. See docs/tech/features/UI Bench.md.

    Windows PowerShell 5.1 compatible.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File bench/run_matrix.ps1 -Repeat 3
    powershell -ExecutionPolicy Bypass -File bench/run_matrix.ps1 -Filter 'table/*','hud/quiet'
#>
[CmdletBinding()]
param(
    [string]$Exe = '',
    [string]$OutDir = '',
    # Wildcards over `scene/mode` or `scene/mode/variant` (no trailing slash when a row has no variant).
    [string[]]$Filter = @(),
    [int]$Warmup = 0,
    [int]$Frames = 0,
    [int]$Repeat = 1
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$Invariant = [Globalization.CultureInfo]::InvariantCulture
$RepoRoot = Split-Path -Parent $PSScriptRoot
if ($Exe -eq '') {
    $Exe = Join-Path $RepoRoot 'build-bench\bin\RelWithDebInfo\wind_ui_bench.exe'
}
if (-not (Test-Path -LiteralPath $Exe)) {
    Write-Error "wind_ui_bench not found at '$Exe'. Build it: cmake --preset vs-bench; cmake --build --preset bench"
    exit 1
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path
if ($Repeat -lt 1) {
    Write-Error '-Repeat must be at least 1'
    exit 1
}

# `powershell -File` passes 'a','b' as one string; split on commas so both forms work.
$Filter = @($Filter | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })

$Stages = @('bindings', 'stylesheets', 'input', 'layout', 'motion', 'paint')
# Painter calls that set state rather than draw. They are summed as `state_calls`; the top kinds are drawing calls.
$StateKinds = @('save', 'restore', 'scissor', 'transform', 'view', 'opacity', 'font')

function Format-Num([double]$Value, [string]$Pattern = '0.000') {
    return $Value.ToString($Pattern, $Invariant)
}

function Get-Median([double[]]$Values) {
    $sorted = @($Values | Sort-Object)
    $n = $sorted.Count
    if ($n -eq 0) { return 0.0 }
    if ($n % 2 -eq 1) { return [double]$sorted[($n - 1) / 2] }
    return ([double]$sorted[$n / 2 - 1] + [double]$sorted[$n / 2]) / 2.0
}

function Write-Utf8([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding($false)))
}

function Invoke-Bench([string[]]$Arguments) {
    $output = & $Exe @Arguments 2>&1 | ForEach-Object { "$_" }
    return @{ Code = $LASTEXITCODE; Output = ($output -join "`n") }
}

# --- rows ----------------------------------------------------------------------------------------------------------
$list = Invoke-Bench @('--list')
if ($list.Code -ne 0) {
    Write-Error "wind_ui_bench --list failed with exit code $($list.Code):`n$($list.Output)"
    exit 1
}
$rows = @()
foreach ($line in ($list.Output -split "`n")) {
    $parts = @($line.Trim() -split '\s+' | Where-Object { $_ -ne '' })
    if ($parts.Count -lt 2) { continue }
    $variant = ''
    if ($parts.Count -ge 3) { $variant = $parts[2] }
    $key = "$($parts[0])/$($parts[1])"
    if ($variant -ne '') { $key = "$key/$variant" }
    $match = ($Filter.Count -eq 0)
    foreach ($pattern in $Filter) {
        if ($key -like $pattern) { $match = $true }
    }
    if ($match) {
        $rows += [pscustomobject]@{ Key = $key; Scene = $parts[0]; Mode = $parts[1]; Variant = $variant }
    }
}
if ($rows.Count -eq 0) {
    Write-Error "No matrix row matches -Filter '$($Filter -join "', '")'"
    exit 1
}

# --- runs ----------------------------------------------------------------------------------------------------------
# The first report names the output directory (its commit), so runs start in a staging directory.
$resultsRoot = Join-Path $RepoRoot 'bench\results'
$staging = Join-Path $resultsRoot ('.staging-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$runsDir = Join-Path $staging 'runs'
New-Item -ItemType Directory -Force -Path $runsDir | Out-Null

$reports = @{}
foreach ($row in $rows) { $reports[$row.Key] = @() }
$total = $rows.Count * $Repeat
$index = 0
for ($pass = 1; $pass -le $Repeat; $pass++) {
    foreach ($row in $rows) {
        $index++
        $name = $row.Key -replace '/', '-'
        $file = Join-Path $runsDir ("{0}.r{1}.json" -f $name, $pass)
        $arguments = @('--scene', $row.Scene, '--mode', $row.Mode)
        if ($row.Variant -ne '') { $arguments += @('--variant', $row.Variant) }
        if ($Warmup -gt 0) { $arguments += @('--warmup', "$Warmup") }
        if ($Frames -gt 0) { $arguments += @('--frames', "$Frames") }
        $arguments += @('--out', $file)
        Write-Host ("[{0}/{1}] {2} (pass {3})" -f $index, $total, $row.Key, $pass)
        $run = Invoke-Bench $arguments
        if ($run.Code -ne 0 -or -not (Test-Path -LiteralPath $file)) {
            Write-Host $run.Output
            Write-Error ("wind_ui_bench {0} failed with exit code {1}; partial results in {2}" -f `
                ($arguments -join ' '), $run.Code, $staging)
            exit 1
        }
        $reports[$row.Key] += , (Get-Content -LiteralPath $file -Raw | ConvertFrom-Json)
    }
}

$first = $reports[$rows[0].Key][0].bench
if ($OutDir -eq '') {
    $short = ''
    if ($first.commit -ne '') {
        $short = $first.commit.Substring(0, [Math]::Min(7, $first.commit.Length))
    } else {
        $short = (& git -C $RepoRoot rev-parse --short HEAD 2>$null)
        if (-not $short) { $short = 'nogit' }
    }
    $suffix = ''
    if ($first.dirty) { $suffix = '-dirty' }
    $OutDir = Join-Path $resultsRoot ((Get-Date -Format 'yyyy-MM-dd') + "-$short$suffix")
    $candidate = $OutDir
    $n = 2
    while (Test-Path -LiteralPath $candidate) {
        $candidate = "$OutDir-$n"
        $n++
    }
    $OutDir = $candidate
}
if (-not (Test-Path -LiteralPath $OutDir)) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path
$finalRuns = Join-Path $OutDir 'runs'
if (Test-Path -LiteralPath $finalRuns) { Remove-Item -Recurse -Force -LiteralPath $finalRuns }
Move-Item -LiteralPath $runsDir -Destination $finalRuns
Remove-Item -Recurse -Force -LiteralPath $staging

# --- summary -------------------------------------------------------------------------------------------------------
# One run's numbers. Several canvases are summed (stage times, counts, draw calls).
function Get-RunNumbers($Report) {
    $canvases = @($Report.profile.canvases)
    $stage = @{}
    $stageMax = @{}
    foreach ($s in $Stages) { $stage[$s] = 0.0; $stageMax[$s] = 0.0 }
    $paint = @{}
    $kinds = @()
    $draw = 0.0
    $elements = 0
    $generated = 0
    $layoutSkipped = $true
    foreach ($c in $canvases) {
        foreach ($s in $Stages) {
            $stage[$s] += [double]$c.stages.$s.avg_ms
            $stageMax[$s] += [double]$c.stages.$s.max_ms
        }
        $draw += [double]$c.draw_calls.avg
        $elements += [int]$c.elements
        $generated += [int]$c.generated
        if (-not $c.layout_skipped) { $layoutSkipped = $false }
        foreach ($p in $c.paint_commands.PSObject.Properties) {
            if (-not $paint.ContainsKey($p.Name)) { $paint[$p.Name] = 0.0; $kinds += $p.Name }
            $paint[$p.Name] += [double]$p.Value.avg
        }
    }
    # The shared ring holds two slots per tick (the pre-schedule fit, then the run_input fit with `commands`), so its
    # average over slots is half the per-tick cost.
    $commands = 2.0 * [double]$Report.profile.shared.commands.avg_ms
    $cpu = $commands
    $peak = 0.0
    foreach ($s in $Stages) { $cpu += $stage[$s]; $peak += $stageMax[$s] }
    return @{
        Canvases = $canvases.Count; Elements = $elements; Generated = $generated; Stage = $stage
        Commands = $commands; Total = $cpu; Peak = $peak; Draw = $draw; Paint = $paint; Kinds = $kinds
        LayoutMax = $stageMax['layout']; LayoutSkipped = $layoutSkipped
    }
}

$summaryRows = @()
foreach ($row in $rows) {
    $runs = @($reports[$row.Key] | ForEach-Object { Get-RunNumbers $_ })
    $medianStage = [ordered]@{}
    foreach ($s in $Stages) { $medianStage[$s] = Get-Median @($runs | ForEach-Object { $_.Stage[$s] }) }
    $totals = @($runs | ForEach-Object { $_.Total })
    $minTotal = ($totals | Measure-Object -Minimum).Minimum
    $maxTotal = ($totals | Measure-Object -Maximum).Maximum
    $medianTotal = Get-Median $totals
    $spread = 0.0
    if ($medianTotal -gt 0) { $spread = 100.0 * ($maxTotal - $minTotal) / $medianTotal }
    $paintMedian = [ordered]@{}
    foreach ($k in $runs[0].Kinds) {
        $paintMedian[$k] = Get-Median @($runs | ForEach-Object { $_.Paint[$k] })
    }
    $stateCalls = 0.0
    foreach ($k in $StateKinds) { if ($paintMedian.Contains($k)) { $stateCalls += $paintMedian[$k] } }
    $top = @($paintMedian.Keys | Where-Object { $StateKinds -notcontains $_ -and $paintMedian[$_] -gt 0 } |
        Sort-Object -Property @{ Expression = { $paintMedian[$_] }; Descending = $true } | Select-Object -First 3 |
        ForEach-Object { [ordered]@{ kind = $_; avg = [Math]::Round($paintMedian[$_], 2) } })
    $roundedStages = [ordered]@{}
    foreach ($s in $Stages) { $roundedStages[$s] = [Math]::Round($medianStage[$s], 4) }
    $roundedPaint = [ordered]@{}
    foreach ($k in $paintMedian.Keys) { $roundedPaint[$k] = [Math]::Round($paintMedian[$k], 2) }
    $summaryRows += [pscustomobject][ordered]@{
        key = $row.Key; scene = $row.Scene; mode = $row.Mode; variant = $row.Variant
        canvases = $runs[0].Canvases; elements = $runs[0].Elements; generated = $runs[0].Generated
        stages_avg_ms = $roundedStages
        commands_avg_ms = [Math]::Round((Get-Median @($runs | ForEach-Object { $_.Commands })), 4)
        total_ms = [Math]::Round($medianTotal, 4)
        total_min_ms = [Math]::Round($minTotal, 4)
        total_max_ms = [Math]::Round($maxTotal, 4)
        spread_pct = [Math]::Round($spread, 1)
        peak_ms = [Math]::Round((Get-Median @($runs | ForEach-Object { $_.Peak })), 4)
        layout_max_ms = [Math]::Round((Get-Median @($runs | ForEach-Object { $_.LayoutMax })), 4)
        layout_skipped_last = $runs[0].LayoutSkipped
        draw_calls = [Math]::Round((Get-Median @($runs | ForEach-Object { $_.Draw })), 2)
        state_calls = [Math]::Round($stateCalls, 2)
        top_paint = $top
        paint_commands = $roundedPaint
        runs = $runs.Count
    }
}

$cpuName = ''
$osName = ''
$gpuName = ''
$powerPlan = ''
try { $cpuName = (@(Get-CimInstance Win32_Processor)[0].Name).Trim() } catch { $cpuName = 'unknown' }
try { $osName = (Get-CimInstance Win32_OperatingSystem).Caption } catch { $osName = 'unknown' }
try { $gpuName = (@(Get-CimInstance Win32_VideoController) | ForEach-Object { $_.Name }) -join '; ' } catch { $gpuName = 'unknown' }
try {
    $scheme = (& powercfg /getactivescheme 2>$null) -join ' '
    if ($scheme -match '\(([^)]+)\)') { $powerPlan = $Matches[1] }
} catch { $powerPlan = '' }

# Relative to the repo when the exe is inside it, so a committed summary does not carry a home directory.
$exeText = $Exe
if ($Exe.StartsWith($RepoRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    $exeText = $Exe.Substring($RepoRoot.Length + 1) -replace '\\', '/'
}
$meta = [ordered]@{
    commit = $first.commit; dirty = $first.dirty; config = $first.config; engine_version = $first.engine_version
    build_id = $first.build_id
    window = [ordered]@{ width = $first.window.width; height = $first.window.height }
    warmup = $first.warmup; frames = $first.frames; repeat = $Repeat; vsync = $first.vsync
    date = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ', $Invariant)
    cpu = $cpuName; gpu = $gpuName; os = $osName; power_plan = $powerPlan
    exe = $exeText
}
$summary = [ordered]@{ meta = $meta; rows = $summaryRows }
Write-Utf8 (Join-Path $OutDir 'summary.json') ($summary | ConvertTo-Json -Depth 8)

$md = New-Object Text.StringBuilder
[void]$md.AppendLine('# UI bench summary')
[void]$md.AppendLine('')
[void]$md.AppendLine('| | |')
[void]$md.AppendLine('| --- | --- |')
$commitText = $meta.commit
if ($commitText -eq '') { $commitText = '(none)' }
[void]$md.AppendLine("| commit | ``$commitText`` |")
[void]$md.AppendLine("| dirty | $($meta.dirty.ToString().ToLower()) |")
[void]$md.AppendLine("| config | $($meta.config) |")
[void]$md.AppendLine("| engine | $($meta.engine_version) (build $($meta.build_id)) |")
[void]$md.AppendLine("| window | $($meta.window.width) x $($meta.window.height), vsync $($meta.vsync.ToString().ToLower()) |")
[void]$md.AppendLine("| frames | warmup $($meta.warmup), measured $($meta.frames), repeat $Repeat (median of each average) |")
[void]$md.AppendLine("| date | $($meta.date) |")
[void]$md.AppendLine("| machine | $cpuName; $gpuName; $osName; power plan $powerPlan |")
[void]$md.AppendLine('')
[void]$md.AppendLine('Milliseconds are CPU averages per frame. `total` = the six stages + `commands`; `spread` = (max - min) / median ' +
    'of `total` across runs; `peak` = the sum of each stage''s max, an upper bound of the worst frame. `state` = ' +
    'save, restore, scissor, transform, view, opacity, font calls; `top` = the three most frequent drawing calls. ' +
    '`layout` marks `skip` when the last frame skipped layout. Every scene has one canvas unless `canvases` says otherwise.')
[void]$md.AppendLine('')
[void]$md.AppendLine('| row | elements | generated | bindings | styles | input | layout | motion | paint | commands | total | spread | peak | draws | state | top |')
[void]$md.AppendLine('| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |')
foreach ($r in $summaryRows) {
    $label = $r.key
    if ($r.canvases -ne 1) { $label = "$label ($($r.canvases) canvases)" }
    $layout = Format-Num $r.stages_avg_ms.layout
    if ($r.layout_skipped_last) { $layout = "$layout skip" }
    $topText = (@($r.top_paint | ForEach-Object { '{0} {1}' -f $_.kind, (Format-Num $_.avg '0.#') }) -join ', ')
    $cells = @(
        $label, $r.elements, $r.generated,
        (Format-Num $r.stages_avg_ms.bindings), (Format-Num $r.stages_avg_ms.stylesheets),
        (Format-Num $r.stages_avg_ms.input), $layout, (Format-Num $r.stages_avg_ms.motion),
        (Format-Num $r.stages_avg_ms.paint), (Format-Num $r.commands_avg_ms), (Format-Num $r.total_ms),
        ((Format-Num $r.spread_pct '0.0') + '%'), (Format-Num $r.peak_ms), (Format-Num $r.draw_calls '0.#'),
        (Format-Num $r.state_calls '0.#'), $topText)
    [void]$md.AppendLine('| ' + ($cells -join ' | ') + ' |')
}
Write-Utf8 (Join-Path $OutDir 'summary.md') $md.ToString()

Write-Host "Wrote $(Join-Path $OutDir 'summary.md') and summary.json ($($rows.Count) rows x $Repeat)"
exit 0
