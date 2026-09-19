[CmdletBinding()]
param([string] $OutputDirectory = '',
      [string] $Executable = 'build-windows/rf-gpu-graphics-test.exe',
      [switch] $DepthGate,
      [switch] $MixedGate,
      [switch] $CacheGate)
# Build with windows/Makefile gpu-graphics-test, gpu-raster-test (MixedGate),
# or gpu-resource-cache-test (CacheGate). Preserve evidence.
$ErrorActionPreference = 'Stop'
if (([int]$MixedGate.IsPresent + [int]$DepthGate.IsPresent + [int]$CacheGate.IsPresent) -gt 1) { throw 'Choose one gate.' }
if ($CacheGate -and -not $PSBoundParameters.ContainsKey('Executable')) {
    $Executable = 'build-windows/rasterfall-gpu-cache-test.exe'
}
if ($MixedGate -and -not $PSBoundParameters.ContainsKey('Executable')) {
    $Executable = 'build-windows/rf-gpu-raster-test.exe'
}
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $OutputDirectory) {
    $Prefix = if ($CacheGate) { 'tmp/hg2b-cache-' } elseif ($MixedGate) { 'tmp/hg2b-mixed-bridge-' } elseif ($DepthGate) { 'tmp/hg2b-depth-bridge-' } else { 'tmp/hg2a-' }
    $OutputDirectory = Join-Path $Root ($Prefix + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory.' }
$Exe = (Resolve-Path (Join-Path $Root $Executable)).Path
$Checkpoint = if ($CacheGate) { 'HG-2B-registry-cache' } elseif ($MixedGate) { 'HG-2B-mixed-bridge' } elseif ($DepthGate) { 'HG-2B-depth-bridge' } else { 'HG-2A' }
[string[]] $ProofArguments = if ($CacheGate) { @() } elseif ($MixedGate) { @('--mixed-gate') } elseif ($DepthGate) { @('--depth-gate') } else { @(Join-Path $OutputDirectory 'proof') }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$Manifest = [ordered]@{
    schema = 1; checkpoint = $Checkpoint; started = (Get-Date).ToString('o')
    commit = (& git -C $Root rev-parse HEAD); worktree = @(& git -C $Root status --short)
    executable_sha256 = (Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash
    command = @($Exe) + @($ProofArguments)
    result = 'FAIL'; exit_code = $null
    boundary = 'hosted offscreen graphics and Raster ABI interop; diagnostic final readback; Core mixed frame/native consumer not implemented'
}
try {
    $Manifest.drivers = @(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\*' -ErrorAction SilentlyContinue |
        Where-Object { $_.DriverDesc } | Select-Object DriverDesc, DriverVersion, MatchingDeviceId)
    $Log = Join-Path $OutputDirectory 'proof.log'
    Push-Location $Root
    try { & $Exe @ProofArguments > $Log 2> (Join-Path $OutputDirectory 'proof.stderr.log') }
    finally { Pop-Location }
    $Manifest.exit_code = $LASTEXITCODE
    $Text = Get-Content -Raw -LiteralPath $Log
    if ($Text -notmatch '(?m)^HG-2[AB] adapter=.* type=[12] queue=\d+') { throw 'Physical integrated/discrete GPU required for checkpoint acceptance.' }
    $Manifest.adapter = $Matches[0]
    $ArtifactNames = if ($DepthGate -or $MixedGate -or $CacheGate) { @('proof.log','proof.stderr.log') } else { @('proof.ppm','proof.depth-f32','proof.log') }
    $Manifest.artifacts = @($ArtifactNames | ForEach-Object {
        $File = Join-Path $OutputDirectory $_
        @{path = $_; bytes = (Get-Item -LiteralPath $File).Length;
          sha256 = (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash}
    })
    $PassMarker = if ($CacheGate) { 'registry-gpu-cache: PASS' } elseif ($MixedGate) { 'HG-2B mixed bridge: PASS' } elseif ($DepthGate) { 'HG-2B depth prerequisite: PASS' } else { 'HG-2A graphics proof: PASS' }
    if ($Manifest.exit_code -ne 0 -or $Text -notmatch $PassMarker) { throw "$Checkpoint failed; see proof.log." }
    $Manifest.result = 'PASS'
} catch {
    $Manifest.error = $_.Exception.Message
    throw
} finally {
    $Manifest.finished = (Get-Date).ToString('o')
    $Manifest | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $OutputDirectory 'manifest.json')
}
Write-Output "$Checkpoint PASS: $OutputDirectory"
