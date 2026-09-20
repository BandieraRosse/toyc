[CmdletBinding()]
param(
    [string] $OutputDirectory = '',
    [string] $WhuMap = 'rasterfall/assets/maps/return_whu_planar_massing_v0.map'
)
$ErrorActionPreference = 'Stop'
$ToolPath = $env:Path
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $ToolPath, 'Process')
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package = Join-Path $Root 'build-windows/rasterfall-windows'
$Exe = Join-Path $Package 'rasterfall.exe'
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $Root ('tmp/hg4-ground-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory.' }
if (-not (Test-Path -LiteralPath $Exe)) { throw 'Run windows/NativeCodex.ps1 package first.' }
$WhuMap = (Resolve-Path (Join-Path $Root $WhuMap)).Path
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$Summary = [Collections.Generic.List[object]]::new()
Add-Type -TypeDefinition @'
using System;
using System.IO;
public static class RasterfallCaptureDiff {
    static byte[] Pixels(string path, out int width, out int height) {
        byte[] b=File.ReadAllBytes(path);
        if (b.Length > 16 && b[0]=='P' && b[1]=='6') {
            int p=2; Func<int> token=delegate() {
                while (p < b.Length && Char.IsWhiteSpace((char)b[p])) p++;
                int v=0; while (p < b.Length && !Char.IsWhiteSpace((char)b[p])) v=v*10+b[p++]-'0';
                return v;
            };
            width=token(); height=token(); if (token()!=255) throw new InvalidDataException("PPM max");
            while (p < b.Length && Char.IsWhiteSpace((char)b[p])) p++;
            byte[] rgb=new byte[width*height*3]; Buffer.BlockCopy(b,p,rgb,0,rgb.Length); return rgb;
        }
        if (b.Length < 54 || b[0]!='B' || b[1]!='M') throw new InvalidDataException("image format");
        int off=BitConverter.ToInt32(b,10); width=BitConverter.ToInt32(b,18);
        int signedHeight=BitConverter.ToInt32(b,22); height=Math.Abs(signedHeight);
        int bits=BitConverter.ToInt16(b,28); if (bits!=24 && bits!=32) throw new InvalidDataException("BMP bits");
        int step=bits/8, stride=((width*bits+31)/32)*4; byte[] outp=new byte[width*height*3];
        for (int y=0;y<height;y++) { int sy=signedHeight>0 ? height-1-y : y;
            for (int x=0;x<width;x++) { int s=off+sy*stride+x*step, d=(y*width+x)*3;
                outp[d]=b[s+2]; outp[d+1]=b[s+1]; outp[d+2]=b[s]; } }
        return outp;
    }
    public static double[] Compare(string aPath,string bPath) {
        int aw,ah,bw,bh; byte[] a=Pixels(aPath,out aw,out ah), b=Pixels(bPath,out bw,out bh);
        if (aw!=bw || ah!=bh) throw new InvalidDataException("extent mismatch");
        long changed=0,total=0; int maximum=0;
        for(int p=0;p<a.Length;p+=3) { int e=0; for(int c=0;c<3;c++) {
            int d=Math.Abs(a[p+c]-b[p+c]); total+=d; if(d>e)e=d; }
            if(e!=0)changed++; if(e>maximum)maximum=e; }
        double pixels=(double)aw*ah;
        return new double[]{aw,ah,changed,changed*100.0/pixels,total/(pixels*3.0),maximum};
    }
    public static void PpmToBmp(string source,string target) {
        int width,height; byte[] rgb=Pixels(source,out width,out height);
        int stride=width*4, size=54+stride*height; byte[] bmp=new byte[size];
        bmp[0]=(byte)'B'; bmp[1]=(byte)'M'; Buffer.BlockCopy(BitConverter.GetBytes(size),0,bmp,2,4);
        Buffer.BlockCopy(BitConverter.GetBytes(54),0,bmp,10,4);
        Buffer.BlockCopy(BitConverter.GetBytes(40),0,bmp,14,4);
        Buffer.BlockCopy(BitConverter.GetBytes(width),0,bmp,18,4);
        Buffer.BlockCopy(BitConverter.GetBytes(-height),0,bmp,22,4);
        bmp[26]=1; bmp[28]=32;
        for(int p=0;p<width*height;p++) { int s=p*3,d=54+p*4;
            bmp[d]=rgb[s+2]; bmp[d+1]=rgb[s+1]; bmp[d+2]=rgb[s]; bmp[d+3]=255; }
        File.WriteAllBytes(target,bmp);
    }
}
'@

function Run-Capture([string] $Name, [string] $View, [string] $MapPath) {
    $cpu = Join-Path $OutputDirectory "$Name-cpu.ppm"
    $cpuReview = Join-Path $OutputDirectory "$Name-cpu.bmp"
    $gpu = Join-Path $OutputDirectory "$Name-gpu.bmp"
    $common = @('--gpu-normal-scene', $View, '0', '--frames', '30', '--frame-audit')
    if ($MapPath) { $common = @('--map', $MapPath) + $common }
    $runs = @(
        @{ kind='cpu'; args=@('--renderer', 'cpu', '--dump-frame', $cpu) + $common },
        @{ kind='gpu'; args=@('--renderer', 'gpu-compute', '--gpu-required', '--gpu-native-present',
            '--gpu-frame-capture', $gpu, '--gpu-capture-frame', '30') + $common }
    )
    foreach ($run in $runs) {
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
    [RasterfallCaptureDiff]::PpmToBmp($cpu, $cpuReview)
    $result = [RasterfallCaptureDiff]::Compare($cpu, $gpu)
    $record = [ordered]@{ view=$Name; width=[int]$result[0]; height=[int]$result[1]
        changed_pixels=[long]$result[2]; changed_percent=$result[3]
        mean_channel_error=$result[4]; max_channel_error=[int]$result[5]
        cpu_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $cpu).Hash
        gpu_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $gpu).Hash }
    $Summary.Add($record)
    $record | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory "$Name-diff.json")
}

Run-Capture 'campaign-base' 'base' ''
Run-Capture 'campaign-spawn' 'spawn' ''
Run-Capture 'campaign-west-facility' 'west-facility' ''
Run-Capture 'whu-a18' 'whu-a18' $WhuMap
Run-Capture 'whu-b-plaza' 'whu-b-plaza' $WhuMap
Run-Capture 'whu-library' 'whu-library' $WhuMap
Run-Capture 'whu-d-ef' 'whu-d-ef' $WhuMap
$Summary | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory 'summary.json')
Write-Host "[HG-4A] captures complete: $OutputDirectory"
