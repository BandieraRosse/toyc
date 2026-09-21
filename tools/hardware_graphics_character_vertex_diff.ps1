[CmdletBinding()]
param(
    [string] $OutputDirectory = '',
    [ValidateSet('near', 'mid')]
    [string] $View = 'near',
    [ValidateSet(0, 30, 60)]
    [int] $EnemyCount = 0
)

$ErrorActionPreference = 'Stop'
$ToolPath = $env:Path
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $ToolPath, 'Process')
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package = Join-Path $Root 'build-windows/rasterfall-windows'
$Exe = Join-Path $Package 'rasterfall.exe'
$RuntimeLog = Join-Path $Package 'rasterfall.log'
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $Root ('tmp/hg5-character-vertex-diff-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory.' }
if (-not (Test-Path -LiteralPath $Exe)) { throw 'Run windows/NativeCodex.ps1 package first.' }
if (Get-Process -Name rasterfall -ErrorAction SilentlyContinue) {
    throw 'A rasterfall process is already running; stop it before collecting the HG-5A vertex diff.'
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$Stdout = Join-Path $OutputDirectory 'run.stdout.txt'
$Stderr = Join-Path $OutputDirectory 'run.stderr.txt'
$BeforeLines = if (Test-Path -LiteralPath $RuntimeLog) {
    @(Get-Content -LiteralPath $RuntimeLog -Encoding UTF8).Count
} else { 0 }
$Arguments = @(
    '--renderer', 'gpu-compute', '--gpu-required', '--gpu-native-present',
    '--gpu-normal-scene', $View, "$EnemyCount", '--gpu-normal-fixed-tick',
    '--gpu-character-vertex-diff', '--frames', '30', '--frame-audit', '--no-stats'
)
$Quoted = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
$Process = Start-Process -FilePath $Exe -ArgumentList $Quoted -WorkingDirectory $Package `
    -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
if ($Process.ExitCode -ne 0) { throw "Vertex diff exited $($Process.ExitCode)." }
if (-not (Test-Path -LiteralPath $RuntimeLog)) { throw 'Vertex diff did not produce rasterfall.log.' }
$RunLog = Join-Path $OutputDirectory 'run.runtime.log'
@(Get-Content -LiteralPath $RuntimeLog -Encoding UTF8 | Select-Object -Skip $BeforeLines) |
    Set-Content -LiteralPath $RunLog -Encoding UTF8
$Lines = @(Get-Content -LiteralPath $RunLog -Encoding UTF8)
$Headers = @($Lines | Where-Object { $_ -match '^FRAME-AUDIT frame=' })
if ($Headers.Count -ne 30 -or @($Headers | Where-Object { $_ -notmatch ' path=gpu-native ' }).Count) {
    throw 'Vertex diff did not complete 30 strict-native frames.'
}
$Rows = @($Lines | Where-Object { $_ -match '^FRAME-AUDIT character-vertex-diff ' })
if ($Rows.Count -ne 1 -or $Rows[0] -notmatch
    'vertices=(\d+) position_mismatches=(\d+) normal_mismatches=(\d+) uv_mismatches=(\d+) max_position_delta=(\d+) max_normal_delta=(\d+)') {
    throw 'Vertex diff row is missing or malformed.'
}
$Result = [ordered]@{
    vertices = [int64]$Matches[1]
    position_mismatches = [int64]$Matches[2]
    normal_mismatches = [int64]$Matches[3]
    uv_mismatches = [int64]$Matches[4]
    max_position_delta = [int64]$Matches[5]
    max_normal_delta = [int64]$Matches[6]
}
if ($Result.vertices -le 0 -or $Result.position_mismatches -ne 0 -or
    $Result.normal_mismatches -ne 0 -or $Result.uv_mismatches -ne 0 -or
    $Result.max_position_delta -ne 0 -or $Result.max_normal_delta -ne 0) {
    throw 'HG-5A device-local vertex diff failed.'
}
$Manifest = [ordered]@{
    schema = 1
    checkpoint = 'HG-5A-character-vertex-diff'
    created_at = (Get-Date).ToString('o')
    executable = $Exe
    executable_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Exe).Hash
    view = $View
    enemies = $EnemyCount
    argv = $Arguments
    result = 'PASS'
    diff = $Result
    runtime_log = [IO.Path]::GetFileName($RunLog)
    notes = @(
        'Frame 30 copies the device-local dynamic character vertex buffer back through the GPU transfer path.',
        'The comparison is exact int32 position, UV and three source normals against the CPU-skinned frame reference.',
        'This proves HG-5A upload and vertex-input bytes; it is not HG-5B GPU skinning validation.'
    )
}
$Manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'manifest.json') -Encoding UTF8
Write-Host "[HG-5A] character vertex diff PASS: $OutputDirectory"
