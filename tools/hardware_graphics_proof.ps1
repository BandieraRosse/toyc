[CmdletBinding()]
param([string] $OutputDirectory = '',
      [string] $Executable = 'build-windows/rf-gpu-graphics-test.exe')
# Build separately with windows/Makefile gpu-graphics-test. Preserve evidence.
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $Root ('tmp/hg2a-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory.' }
$Exe = (Resolve-Path (Join-Path $Root $Executable)).Path
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$Manifest = [ordered]@{
    schema = 1; checkpoint = 'HG-2A'; started = (Get-Date).ToString('o')
    commit = (& git -C $Root rev-parse HEAD); worktree = @(& git -C $Root status --short)
    executable_sha256 = (Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash
    command = @($Exe, (Join-Path $OutputDirectory 'proof'))
    result = 'FAIL'; exit_code = $null
    boundary = 'offscreen graphics only; diagnostic readback; HG-2B mixed target not implemented'
}
try {
    $Manifest.drivers = @(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\*' -ErrorAction SilentlyContinue |
        Where-Object { $_.DriverDesc } | Select-Object DriverDesc, DriverVersion, MatchingDeviceId)
    $Log = Join-Path $OutputDirectory 'proof.log'
    & $Exe (Join-Path $OutputDirectory 'proof') > $Log 2> (Join-Path $OutputDirectory 'proof.stderr.log')
    $Manifest.exit_code = $LASTEXITCODE
    $Text = Get-Content -Raw -LiteralPath $Log
    if ($Manifest.exit_code -ne 0 -or $Text -notmatch 'HG-2A graphics proof: PASS') { throw 'Graphics proof failed.' }
    if ($Text -notmatch '(?m)^HG-2A adapter=.* type=[12] queue=\d+') { throw 'Physical integrated/discrete GPU required for checkpoint acceptance.' }
    $Manifest.adapter = $Matches[0]
    $Manifest.artifacts = @('proof.ppm','proof.depth-f32','proof.log' | ForEach-Object {
        $File = Join-Path $OutputDirectory $_
        @{path = $_; bytes = (Get-Item -LiteralPath $File).Length;
          sha256 = (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash}
    })
    $Manifest.result = 'PASS'
} catch {
    $Manifest.error = $_.Exception.Message
    throw
} finally {
    $Manifest.finished = (Get-Date).ToString('o')
    $Manifest | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $OutputDirectory 'manifest.json')
}
Write-Output "HG-2A PASS: $OutputDirectory"
