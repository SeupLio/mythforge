# MythForge end-to-end verification (PowerShell 5+).
#
#   ./scripts/e2e.ps1              # native + content + service + agent
#   ./scripts/e2e.ps1 -SkipUnity   # skip EditMode tests
#
# Unity is optional because its batchmode test process can stay alive after the
# result XML is written in this non-interactive environment; the script parses
# the XML and then terminates the process.

param([switch]$SkipUnity)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$script:failures = 0

function Check($name, [bool]$condition, $detail = "") {
    if ($condition) {
        Write-Host "[PASS] $name" -ForegroundColor Green
    } else {
        Write-Host "[FAIL] $name $detail" -ForegroundColor Red
        $script:failures++
    }
}

Write-Host "== native build ==" -ForegroundColor Cyan
& cmake --build build -j 8 | Out-Null
Check "cmake build" ($LASTEXITCODE -eq 0)

Write-Host "== native unit tests ==" -ForegroundColor Cyan
$testOutput = & build\native\mythforge_tests.exe
$testExit = $LASTEXITCODE
($testOutput | Select-Object -Last 1)
Check "native tests" ($testExit -eq 0)

Write-Host "== content pipeline ==" -ForegroundColor Cyan
& build\native\mf.exe validate content | Out-Null
Check "mf validate" ($LASTEXITCODE -eq 0)
& build\native\mf.exe build content | Out-Null
Check "mf build" ($LASTEXITCODE -eq 0)
& build\native\mf.exe test content | Out-Null
Check "mf test" ($LASTEXITCODE -eq 0)

Write-Host "== agent repair ==" -ForegroundColor Cyan
$broken = Join-Path $root "build\e2e_broken.json"
$brokenJson = @'
{
  "schemaVersion": 1,
  "kind": "quest",
  "id": "q_e2e_broken",
  "nodes": [
    { "id": "s", "type": "quest.start", "props": {} },
    { "id": "k", "type": "quest.condition.kill", "props": { "count": 0 } },
    { "id": "e", "type": "quest.end", "props": { "result": "weird" } }
  ],
  "edges": [
    { "from": "s", "to": "k" },
    { "from": "k", "to": "e" }
  ]
}
'@
[System.IO.File]::WriteAllText($broken, $brokenJson)
$agentReport = & build\native\mf.exe agent fix $broken --write --json | ConvertFrom-Json
Check "agent fixed" ($agentReport.ok -and $agentReport.fixed.Count -ge 2)
$checkReport = & build\native\mf.exe check $broken --json | ConvertFrom-Json
Check "repaired file compiles" ($checkReport.ok)
Remove-Item $broken -Force -ErrorAction SilentlyContinue

Write-Host "== content service ==" -ForegroundColor Cyan
$server = Start-Process -FilePath "build\native\mfd.exe" `
    -ArgumentList "content", "--port", "7799" -PassThru -WindowStyle Hidden
try {
    $ready = $false
    for ($i = 0; $i -lt 40; $i++) {
        try {
            $health = Invoke-RestMethod "http://127.0.0.1:7799/health" -TimeoutSec 1
            $ready = $true
            break
        } catch {
            Start-Sleep -Milliseconds 250
        }
    }
    Check "mfd /health" $ready
    if ($ready) {
        $manifest = Invoke-RestMethod "http://127.0.0.1:7799/manifest"
        Check "mfd /manifest" ($manifest.modules.Count -ge 4)
        $content = Invoke-RestMethod "http://127.0.0.1:7799/content"
        Check "mfd /content" ($content.count -ge 4)
        $modules = Invoke-RestMethod "http://127.0.0.1:7799/modules"
        Check "mfd /modules" ($modules.modules.Count -ge 4)
        $artifact = Invoke-WebRequest "http://127.0.0.1:7799/modules/q_forest_wolves" -UseBasicParsing
        Check "mfd /modules/{id}" ($artifact.RawContentLength -gt 0)
        $graph = Get-Content "content\quests\q_forest_wolves.json" -Raw
        $validated = Invoke-RestMethod "http://127.0.0.1:7799/validate" -Method Post `
            -ContentType "application/json" -Body $graph
        Check "mfd /validate" $validated.ok
        $compiled = Invoke-RestMethod "http://127.0.0.1:7799/compile" -Method Post `
            -ContentType "application/json" -Body $graph
        Check "mfd /compile" ($compiled.ok -and $compiled.artifactBase64.Length -gt 0)
    }
} finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
}

if (-not $SkipUnity) {
    Write-Host "== Unity EditMode ==" -ForegroundColor Cyan
    $unity = if ($env:MYTHFORGE_UNITY) { $env:MYTHFORGE_UNITY } else { "E:\j\Unity\Editor\Unity.exe" }
    $xml = Join-Path $root "build\e2e_unity.xml"
    $log = Join-Path $root "build\e2e_unity.log"
    $projectPath = Join-Path $root "unity\DemoGame"
    Remove-Item $xml -Force -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $unity -ArgumentList @(
        "-batchmode", "-nographics",
        "-projectPath", "`"$projectPath`"",
        "-runTests", "-testPlatform", "EditMode",
        "-testResults", "`"$xml`"",
        "-logFile", "`"$log`""
    ) -PassThru -NoNewWindow
    for ($i = 0; $i -lt 300; $i++) {
        if (Test-Path $xml) { break }
        Start-Sleep -Seconds 2
    }
    Start-Sleep -Seconds 2
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    if (Test-Path $xml) {
        [xml]$result = Get-Content $xml
        $run = $result.SelectSingleNode("//test-run")
        Write-Host ("Unity: total={0} passed={1} failed={2}" -f $run.total, $run.passed, $run.failed)
        Check "Unity EditMode" ($run.failed -eq "0")
    } else {
        Check "Unity EditMode" $false "no result XML"
    }
}

Write-Host ""
if ($script:failures -eq 0) {
    Write-Host "E2E PASSED" -ForegroundColor Green
    exit 0
}
Write-Host ("E2E FAILED: {0} check(s)" -f $script:failures) -ForegroundColor Red
exit 1
