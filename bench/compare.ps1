<#
.SYNOPSIS
    Compares two run_matrix.ps1 summaries and prints a markdown table, base -> head.

.DESCRIPTION
    -Base and -Head are a results directory or its summary.json. Rows are matched by `scene/mode[/variant]`. A row is
    marked REGRESSION when its total CPU ms or its draw calls grow by more than -Threshold percent; total ms also has
    to grow by more than 0.01 ms, so timer noise on a near-zero row is not flagged. Exits 0 (it is a report) unless an
    input is missing or unreadable. See docs/tech/features/UI Bench.md.

    Windows PowerShell 5.1 compatible.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File bench/compare.ps1 -Base bench/results/baseline -Head bench/results/2026-10-09-e7211f3-dirty
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Base,
    [Parameter(Mandatory = $true)][string]$Head,
    [double]$Threshold = 5
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$Invariant = [Globalization.CultureInfo]::InvariantCulture

function Read-Summary([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        Write-Host "compare: '$Path' does not exist"
        exit 1
    }
    if ((Get-Item -LiteralPath $Path).PSIsContainer) {
        $Path = Join-Path $Path 'summary.json'
        if (-not (Test-Path -LiteralPath $Path)) {
            Write-Host "compare: '$Path' does not exist"
            exit 1
        }
    }
    try {
        return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json)
    } catch {
        Write-Host "compare: '$Path' is not a summary.json: $($_.Exception.Message)"
        exit 1
    }
}

function Format-Num([double]$Value, [string]$Pattern = '0.000') {
    return $Value.ToString($Pattern, $Invariant)
}

# Percent change, or $null when the base is zero.
function Get-Change([double]$From, [double]$To) {
    if ($From -eq 0) {
        if ($To -eq 0) { return 0.0 }
        return $null
    }
    return 100.0 * ($To - $From) / $From
}

function Format-Pair([double]$From, [double]$To, [string]$Pattern = '0.000') {
    $change = Get-Change $From $To
    $changeText = 'new'
    if ($null -ne $change) {
        $changeText = $change.ToString('+0.0;-0.0;0.0', $Invariant) + '%'
    }
    return '{0} -> {1} ({2})' -f (Format-Num $From $Pattern), (Format-Num $To $Pattern), $changeText
}

$baseSummary = Read-Summary $Base
$headSummary = Read-Summary $Head

$headRows = @{}
foreach ($row in $headSummary.rows) { $headRows[$row.key] = $row }
$baseKeys = @{}
foreach ($row in $baseSummary.rows) { $baseKeys[$row.key] = $true }

function Describe($Meta) {
    $commit = "$($Meta.commit)"
    if ($commit.Length -gt 7) { $commit = $commit.Substring(0, 7) }
    $dirty = ''
    if ($Meta.dirty) { $dirty = ' dirty' }
    return "$commit$dirty, $($Meta.config), $($Meta.window.width)x$($Meta.window.height), repeat $($Meta.repeat), $($Meta.date), $($Meta.cpu)"
}

$out = New-Object Text.StringBuilder
[void]$out.AppendLine('# UI bench compare')
[void]$out.AppendLine('')
[void]$out.AppendLine("- base: $(Describe $baseSummary.meta)")
[void]$out.AppendLine("- head: $(Describe $headSummary.meta)")
[void]$out.AppendLine("- threshold: $(Format-Num $Threshold '0.#')% on total ms (and > 0.01 ms) or draw calls")
if ("$($baseSummary.meta.cpu)" -ne "$($headSummary.meta.cpu)" -or
    "$($baseSummary.meta.config)" -ne "$($headSummary.meta.config)" -or
    "$($baseSummary.meta.window.width)x$($baseSummary.meta.window.height)" -ne
    "$($headSummary.meta.window.width)x$($headSummary.meta.window.height)") {
    [void]$out.AppendLine('- WARNING: machine, config, or window differs; the numbers are not comparable')
}
[void]$out.AppendLine('')
[void]$out.AppendLine('| row | total ms | layout ms | paint ms | draw calls | |')
[void]$out.AppendLine('| --- | --- | --- | --- | --- | --- |')

$regressions = 0
foreach ($b in $baseSummary.rows) {
    if (-not $headRows.ContainsKey($b.key)) { continue }
    $h = $headRows[$b.key]
    $mark = ''
    $totalChange = Get-Change $b.total_ms $h.total_ms
    $drawChange = Get-Change $b.draw_calls $h.draw_calls
    $totalWorse = ($null -ne $totalChange -and $totalChange -gt $Threshold -and ($h.total_ms - $b.total_ms) -gt 0.01)
    $drawWorse = ($null -ne $drawChange -and $drawChange -gt $Threshold)
    if ($totalWorse -or $drawWorse) {
        $mark = 'REGRESSION'
        $regressions++
    } elseif ($null -ne $totalChange -and $totalChange -lt -$Threshold -and ($b.total_ms - $h.total_ms) -gt 0.01) {
        $mark = 'faster'
    }
    $cells = @(
        $b.key,
        (Format-Pair $b.total_ms $h.total_ms),
        (Format-Pair $b.stages_avg_ms.layout $h.stages_avg_ms.layout),
        (Format-Pair $b.stages_avg_ms.paint $h.stages_avg_ms.paint),
        (Format-Pair $b.draw_calls $h.draw_calls '0.#'),
        $mark)
    [void]$out.AppendLine('| ' + ($cells -join ' | ') + ' |')
}

$onlyBase = @($baseSummary.rows | Where-Object { -not $headRows.ContainsKey($_.key) } | ForEach-Object { $_.key })
$onlyHead = @($headSummary.rows | Where-Object { -not $baseKeys.ContainsKey($_.key) } | ForEach-Object { $_.key })
[void]$out.AppendLine('')
[void]$out.AppendLine("$regressions regression(s).")
if ($onlyBase.Count -gt 0) { [void]$out.AppendLine("Only in base: $($onlyBase -join ', ')") }
if ($onlyHead.Count -gt 0) { [void]$out.AppendLine("Only in head: $($onlyHead -join ', ')") }

Write-Output $out.ToString()
exit 0
