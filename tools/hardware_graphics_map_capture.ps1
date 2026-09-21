[CmdletBinding()]
param(
    [string] $OutputDirectory = '',
    [string] $WhuMap = 'rasterfall/assets/maps/return_whu_planar_massing_v0.map',
    [string] $FixtureMap = 'rasterfall/assets/maps/hg4_map_geometry_fixture.map'
)
$ErrorActionPreference = 'Stop'
$ToolPath = $env:Path
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $ToolPath, 'Process')
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package = Join-Path $Root 'build-windows/rasterfall-windows'
$Exe = Join-Path $Package 'rasterfall.exe'
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $Root ('tmp/hg4-map-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory.' }
if (-not (Test-Path -LiteralPath $Exe)) { throw 'Run windows/NativeCodex.ps1 package first.' }
$WhuMap = (Resolve-Path (Join-Path $Root $WhuMap)).Path
$FixtureMap = (Resolve-Path (Join-Path $Root $FixtureMap)).Path
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$Summary = [Collections.Generic.List[object]]::new()
Add-Type -TypeDefinition @'
using System;
using System.IO;
public static class Hg4MapCaptureImage {
    public static void PpmToBmp(string source,string target) {
        byte[] b=File.ReadAllBytes(source); int p=2;
        Func<int> token=delegate() { while(p<b.Length && Char.IsWhiteSpace((char)b[p]))p++;
            int v=0; while(p<b.Length && !Char.IsWhiteSpace((char)b[p]))v=v*10+b[p++]-'0'; return v; };
        if(b.Length<16 || b[0]!='P' || b[1]!='6')throw new InvalidDataException("PPM");
        int width=token(),height=token(); if(token()!=255)throw new InvalidDataException("PPM max");
        while(p<b.Length && Char.IsWhiteSpace((char)b[p]))p++;
        int stride=width*4,size=54+stride*height; byte[] bmp=new byte[size];
        bmp[0]=(byte)'B';bmp[1]=(byte)'M';Buffer.BlockCopy(BitConverter.GetBytes(size),0,bmp,2,4);
        Buffer.BlockCopy(BitConverter.GetBytes(54),0,bmp,10,4);Buffer.BlockCopy(BitConverter.GetBytes(40),0,bmp,14,4);
        Buffer.BlockCopy(BitConverter.GetBytes(width),0,bmp,18,4);Buffer.BlockCopy(BitConverter.GetBytes(-height),0,bmp,22,4);
        bmp[26]=1;bmp[28]=32;
        for(int i=0;i<width*height;i++){int s=p+i*3,d=54+i*4;bmp[d]=b[s+2];bmp[d+1]=b[s+1];bmp[d+2]=b[s];bmp[d+3]=255;}
        File.WriteAllBytes(target,bmp);
    }
}
'@

function Run-Capture([string] $Name, [string] $View, [string] $Class, [string] $MapPath) {
    $cpu = Join-Path $OutputDirectory "$Name-cpu.ppm"
    $cpuReview = Join-Path $OutputDirectory "$Name-cpu.bmp"
    $gpu = Join-Path $OutputDirectory "$Name-gpu.bmp"
    $common = @('--gpu-normal-scene', $View, '0', '--frames', '30', '--frame-audit')
    if ($MapPath) { $common = @('--map', $MapPath) + $common }
    foreach ($run in @(
        @{ kind='cpu'; args=@('--renderer','cpu','--dump-frame',$cpu) + $common },
        @{ kind='gpu'; args=@('--renderer','gpu-compute','--gpu-required','--gpu-native-present',
            '--gpu-frame-capture',$gpu,'--gpu-capture-frame','30') + $common }
    )) {
        $stdout = Join-Path $OutputDirectory "$Name-$($run.kind).stdout.txt"
        $stderr = Join-Path $OutputDirectory "$Name-$($run.kind).stderr.txt"
        $quoted = ($run.args | ForEach-Object { '"' + $_ + '"' }) -join ' '
        $p = Start-Process -FilePath $Exe -ArgumentList $quoted -WorkingDirectory $Package `
            -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        if ($p.ExitCode -ne 0) { throw "$Name $($run.kind) exited $($p.ExitCode)" }
    }
    if (-not (Test-Path -LiteralPath $cpu) -or -not (Test-Path -LiteralPath $gpu)) {
        throw "$Name did not produce both captures"
    }
    [Hg4MapCaptureImage]::PpmToBmp($cpu, $cpuReview)
    $gpuLog = Get-Content -LiteralPath (Join-Path $OutputDirectory "$Name-gpu.stdout.txt")
    $MapAudit = @($gpuLog | Select-String 'FRAME-AUDIT map-draw ')
    $GpuAudit = @($gpuLog | Select-String 'FRAME-AUDIT frame=')
    if ($MapAudit.Count -ne 30 -or $GpuAudit.Count -ne 30) { throw "$Name incomplete audit" }
    foreach ($Line in $GpuAudit) {
        if ($Line.Line -notmatch 'path=gpu-native ') { throw "$Name did not remain strict native" }
    }
    $Field = if ($Class -eq 'boundary') { 'boundary' } else { $Class }
    $Builds = 0
    foreach ($Line in $MapAudit) {
        if ($Line.Line -notmatch "$Field=(\d+)/(\d+)/(\d+)") { throw "$Name missing $Class audit" }
        if ([int]$Matches[1] -le 0 -or [int]$Matches[2] -le 0) { throw "$Name did not submit $Class Draw geometry" }
        $Builds += [int]$Matches[3]
    }
    if ($Builds -ne 1) { throw "$Name built $Class mesh $Builds times" }
    $Summary.Add([ordered]@{
        fixture=$Name; view=$View; class=$Class; mesh_builds=$Builds
        cpu_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $cpu).Hash
        gpu_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $gpu).Hash
    })
}

Run-Capture 'wall' 'hg4-wall' 'wall' $FixtureMap
Run-Capture 'ramp' 'hg4-ramp' 'ramp' $FixtureMap
Run-Capture 'platform' 'hg4-platform' 'platform' $FixtureMap
Run-Capture 'boundary' 'base' 'boundary' ''
Run-Capture 'box' 'whu-a18' 'box' $WhuMap
$Summary | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory 'summary.json')
Write-Host "HG-4B captures complete: $OutputDirectory"
