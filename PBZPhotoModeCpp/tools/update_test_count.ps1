# Run the core unit tests, then write the actual assertion count to docs\test_count.txt
# (the single source referenced by README.md / docs\README.md).
# ASCII-only on purpose: Windows PowerShell 5.1 reads BOM-less UTF-8 scripts as ANSI.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

Write-Host "[1/2] Running core tests..."
$output = & (Join-Path $root 'tools\build_tests.bat') 2>&1 | Out-String
$passedByText = $output -match 'RESULT: all checks passed'
if ($LASTEXITCODE -ne 0 -and -not $passedByText) {
    Write-Host $output
    throw "Tests failed (exit $LASTEXITCODE); test_count.txt not updated."
}

$m = [regex]::Match($output, 'TESTS checks=(\d+) failures=(\d+)')
if (-not $m.Success) {
    Write-Host $output
    throw "Machine-readable 'TESTS checks=N failures=N' line not found in test output."
}
$count = [int]$m.Groups[1].Value
$failed = [int]$m.Groups[2].Value
if ($failed -ne 0) {
    throw "$failed assertion(s) failed; test_count.txt not updated."
}

$file = Join-Path $root 'docs\test_count.txt'
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($file, "$count`n", $utf8NoBom)
Write-Host "[2/2] Wrote $file : $count assertions (0 failures)"
