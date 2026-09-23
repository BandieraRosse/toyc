[CmdletBinding()]
param([string]$OutputDirectory = 'tmp/gpu-scene-geometry', [switch]$Isolate, [switch]$MapOnly, [string[]]$Views = @('map-ramp','map-wall','map-platform','map-label','map-sign','map-gate-on','map-gate-off','map-near','map-thin'))
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$package = Join-Path $repo 'build-windows/rasterfall-windows'
$exe = Join-Path $package 'rasterfall.exe'
$map = Join-Path $package 'rasterfall/assets/maps/gpu_scene_render_fixture.map'
$out = [IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
New-Item -ItemType Directory -Force -Path $out | Out-Null
$pathValue = [Environment]::GetEnvironmentVariable('Path', 'Process')
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $pathValue, 'Process')
Get-FileHash -Algorithm SHA256 -LiteralPath $exe,$map | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $out 'inputs.json')
foreach ($view in $Views) {
    $viewMap = $map
    if ($Isolate -or $MapOnly) {
        $ids = switch ($view) {
            'map-ramp' { @('fixture_ramp') }
            'map-wall' { @('fixture_wall') }
            'map-platform' { @('fixture_platform') }
            'map-label' { @('fixture_label') }
            'map-sign' { @('fixture_sign') }
            'map-near' { @('near_edge_box') }
            'map-thin' { @('thin_far_wall') }
            default { @('air_gate_box','air_gate_platform') }
        }
        $viewMap = Join-Path $out "$view.map"
        $mapLines = Get-Content -LiteralPath $map | Where-Object {
            if ($Isolate -and $_ -match '^render id=([^ ]+)') { $ids -contains $Matches[1] -or $Matches[1] -eq 'fixture_ground' }
            else { $true }
        }
        $renderIndex = 0
        $mapLines = @($mapLines | ForEach-Object {
            if ($_ -match '^render ') { $_ -replace 'attr.legacy_index=\d+', "attr.legacy_index=$renderIndex"; $renderIndex++ }
            elseif ($MapOnly -and $_ -match '^world ') { $_ + ' attr.identity=return_to_whu_v0' }
            else { $_ }
        })
        [IO.File]::WriteAllLines($viewMap, $mapLines, (New-Object Text.UTF8Encoding($false)))
    }
    Get-FileHash -Algorithm SHA256 -LiteralPath $viewMap | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $out "$view-input.json")
    foreach ($mode in @('cpu','gpu')) {
        $prefix = Join-Path $out "$view-$mode"
        $argsList = @('--map', ('"'+$viewMap+'"'), '--gpu-normal-scene', $view, '0', '--gpu-normal-fixed-tick', '--frame-audit', '--frames', '1')
        if ($mode -eq 'cpu') { $capture = "$prefix.ppm"; $argsList += @('--renderer','cpu','--dump-frame',('"'+$capture+'"')) }
        else { $capture = "$prefix.bmp"; $argsList += @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-frame-capture',('"'+$capture+'"'),'--gpu-capture-frame','1') }
        $p = Start-Process -FilePath $exe -WorkingDirectory $package -ArgumentList $argsList -WindowStyle Hidden -RedirectStandardOutput "$prefix.stdout.log" -RedirectStandardError "$prefix.stderr.log" -Wait -PassThru
        if ($p.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $capture)) { throw "Capture failed: view=$view mode=$mode exit=$($p.ExitCode)" }
        if ($mode -eq 'gpu' -and -not (Select-String -LiteralPath "$prefix.stdout.log" -SimpleMatch 'path=gpu-native' -Quiet)) { throw "Missing native audit: $view" }
        Write-Host "$view $mode OK"
    }
}
python (Join-Path $PSScriptRoot 'gpu_scene_diff.py') $out
if ($LASTEXITCODE -ne 0) { throw 'Image difference failed' }
