[CmdletBinding()]
param([Parameter(Mandatory=$true)][string] $LogPath)

# Replay a complete captured audit, changing only submit metadata. Generated
# fixtures stay under tmp; the original evidence is never overwritten.
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Output = Join-Path $Root ('tmp/gpu-metrics-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Output | Out-Null
$Original = Get-Content -Raw -Encoding UTF8 -LiteralPath $LogPath
$Parser = Join-Path $PSScriptRoot 'gpu_metrics.ps1'
$Callers = '(?m)^FRAME-AUDIT graphics-submit frame=\d+ caller=([^ ]+) submits=\d+ wait_ms=[0-9.]+ predecessor_frame=\d+'
$Zero = [regex]::Replace($Original, $Callers,
    'FRAME-AUDIT graphics-submit frame=0 caller=$1 submits=0 wait_ms=0.000 predecessor_frame=7')
$Zero = [regex]::Replace($Zero, 'graphics_submits=\d+', 'graphics_submits=0')
$Cases = @(
    @{ name='captured'; text=$Original; fail=$false },
    @{ name='zero-submit-stale-watermark'; text=$Zero; fail=$false },
    @{ name='wrong-frame-active-submit'; text=$Zero.Replace('caller=upload submits=0','caller=upload submits=1'); fail=$true },
    @{ name='wrong-frame-wait'; text=$Zero.Replace('caller=upload submits=0 wait_ms=0.000','caller=upload submits=0 wait_ms=0.001'); fail=$true },
    @{ name='future-frame'; text=$Zero.Replace('graphics-submit frame=0','graphics-submit frame=999999'); fail=$true },
    @{ name='aggregate-disagrees'; text=$Zero.Replace('graphics_submits=0','graphics_submits=1'); fail=$true },
    @{ name='missing-caller'; text=([regex]::Replace($Zero, '(?m)^FRAME-AUDIT graphics-submit .*caller=readback .*\r?\n','')); fail=$true }
)
foreach ($Case in $Cases) {
    $Fixture = Join-Path $Output ($Case.name + '.log')
    $Case.text | Set-Content -Encoding UTF8 -LiteralPath $Fixture
    $Failed = $false
    try { & $Parser -LogPath $Fixture -ExpectedPath gpu-native -RequireFixedTick | Out-Null }
    catch {
        if ($_.Exception.Message -ne 'Graphics submit frame mismatch.') { throw }
        $Failed = $true
    }
    if ($Failed -ne $Case.fail) { throw "Unexpected result: $($Case.name)" }
    Write-Host "PASS $($Case.name)"
}
Write-Host "Evidence: $Output"
