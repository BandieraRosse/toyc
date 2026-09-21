[CmdletBinding()]
param(
    [string] $OutputDirectory = '',
    [int] $Frames = 30,
    [int] $CaptureFrame = 30
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
    $OutputDirectory = Join-Path $Root ('tmp/hg5-character-capture-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if ($Frames -lt $CaptureFrame) { throw 'Frames must be greater than or equal to CaptureFrame.' }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory.' }
if (-not (Test-Path -LiteralPath $Exe)) { throw 'Run windows/NativeCodex.ps1 package first.' }
if (Get-Process -Name rasterfall -ErrorAction SilentlyContinue) {
    throw 'A rasterfall process is already running; stop it before collecting serial HG-5 captures.'
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null

Add-Type -TypeDefinition @'
using System;
using System.IO;
public static class Hg5CharacterCaptureImage {
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
        int step=bits/8, stride=((width*bits+31)/32)*4; byte[] rgbOut=new byte[width*height*3];
        for (int y=0;y<height;y++) { int sy=signedHeight>0 ? height-1-y : y;
            for (int x=0;x<width;x++) { int s=off+sy*stride+x*step, d=(y*width+x)*3;
                rgbOut[d]=b[s+2]; rgbOut[d+1]=b[s+1]; rgbOut[d+2]=b[s]; } }
        return rgbOut;
    }
    public static double[] Compare(string aPath,string bPath) {
        int aw,ah,bw,bh; byte[] a=Pixels(aPath,out aw,out ah), b=Pixels(bPath,out bw,out bh);
        if (aw!=bw || ah!=bh) throw new InvalidDataException("extent mismatch");
        long changed=0,total=0; int maximum=0;
        for(int p=0;p<a.Length;p+=3) { int pixelError=0; for(int c=0;c<3;c++) {
            int d=Math.Abs(a[p+c]-b[p+c]); total+=d; if(d>pixelError)pixelError=d; }
            if(pixelError!=0)changed++; if(pixelError>maximum)maximum=pixelError; }
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

function Read-CharacterAudit([string] $Name, [string] $LogPath) {
    $Lines = @(Get-Content -LiteralPath $LogPath -Encoding UTF8)
    $Headers = @($Lines | Where-Object { $_ -match '^FRAME-AUDIT frame=' })
    $GpuAudit = @($Lines | Where-Object { $_ -match '^FRAME-AUDIT gpu ' })
    $LayerAudit = @($Lines | Where-Object { $_ -match '^FRAME-AUDIT layers ' })
    $CharacterAudit = @($Lines | Where-Object { $_ -match '^FRAME-AUDIT character-draw ' })
    if ($Headers.Count -ne $Frames -or $GpuAudit.Count -ne $Frames -or
        $LayerAudit.Count -ne $Frames -or $CharacterAudit.Count -ne $Frames) {
        throw "$Name has incomplete frame audit rows."
    }
    if (@($Headers | Where-Object { $_ -notmatch ' path=gpu-native ' }).Count) {
        throw "$Name contains non-native frames."
    }
    if (@($Headers | Where-Object { $_ -notmatch ' ticks=1 ' }).Count) {
        throw "$Name did not advance exactly one fixed gameplay tick per GPU frame."
    }
    if (@($GpuAudit | Where-Object {
        $_ -notmatch 'readback_bytes=0' -or $_ -notmatch 'cpu_framebuffer_copy_bytes=0'
    }).Count -or @($LayerAudit | Where-Object {
        $_ -notmatch 'pre_post_cpu_fallback=0' -or $_ -notmatch 'fallback_reason=0x0'
    }).Count) { throw "$Name violated fallback/readback/copy gates." }
    $First = $null
    foreach ($Line in $CharacterAudit) {
        if ($Line -notmatch 'instances=(\d+) items=(\d+) triangles=(\d+) upload_vertices=(\d+) legacy_items=(\d+)') {
            throw "$Name has an invalid character-draw row."
        }
        $Current = @([int64]$Matches[1], [int64]$Matches[2], [int64]$Matches[3],
            [int64]$Matches[4], [int64]$Matches[5])
        if ($Current[0] -le 0 -or $Current[1] -le 0 -or $Current[2] -le 0 -or
            $Current[3] -ne $Current[2] * 3) {
            throw "$Name did not keep valid character body geometry in HG-5A Draw."
        }
        if ($null -eq $First) { $First = $Current }
        elseif ((Compare-Object $First[0..3] $Current[0..3]).Count) {
            throw "$Name migrated character-draw audit changed across frames."
        }
    }
    if ($Name -eq 'near-0' -and $First[4] -ne 0) {
        throw "$Name unexpectedly retained legacy character items."
    }
    return [ordered]@{ instances=$First[0]; draws=$First[1]; triangles=$First[2]
        upload_vertices=$First[3]; legacy_items=$First[4] }
}

function Run-Capture([string] $View, [int] $EnemyCount) {
    $Name = "$View-$EnemyCount"
    $CpuPpm = Join-Path $OutputDirectory "$Name-cpu.ppm"
    $CpuBmp = Join-Path $OutputDirectory "$Name-cpu.bmp"
    $GpuBmp = Join-Path $OutputDirectory "$Name-gpu.bmp"
    $Common = @('--gpu-normal-scene', $View, "$EnemyCount", '--gpu-normal-fixed-tick',
        '--frames', "$Frames", '--frame-audit')
    $Runs = @(
        @{ kind='cpu'; args=@('--renderer','cpu','--dump-frame',$CpuPpm) + $Common },
        @{ kind='gpu'; args=@('--renderer','gpu-compute','--gpu-required','--gpu-native-present',
            '--gpu-frame-capture',$GpuBmp,'--gpu-capture-frame',"$CaptureFrame") + $Common }
    )
    $GpuRuntimeLog = $null
    foreach ($Run in $Runs) {
        $Stdout = Join-Path $OutputDirectory "$Name-$($Run.kind).stdout.txt"
        $Stderr = Join-Path $OutputDirectory "$Name-$($Run.kind).stderr.txt"
        $BeforeLines = if (Test-Path -LiteralPath $RuntimeLog) {
            @(Get-Content -LiteralPath $RuntimeLog -Encoding UTF8).Count
        } else { 0 }
        $Quoted = ($Run.args | ForEach-Object { '"' + $_ + '"' }) -join ' '
        $Process = Start-Process -FilePath $Exe -ArgumentList $Quoted -WorkingDirectory $Package `
            -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
        if ($Process.ExitCode -ne 0) { throw "$Name $($Run.kind) exited $($Process.ExitCode)." }
        if ($Run.kind -eq 'cpu') {
            $CpuHeaders = @(Get-Content -LiteralPath $Stdout -Encoding UTF8 |
                Where-Object { $_ -match '^FRAME-AUDIT frame=' })
            if ($CpuHeaders.Count -ne $Frames -or
                @($CpuHeaders | Where-Object { $_ -notmatch ' path=cpu ' -or $_ -notmatch ' ticks=1 ' }).Count) {
                throw "$Name CPU capture did not keep the complete fixed-tick frame contract."
            }
        }
        if ($Run.kind -eq 'gpu') {
            $GpuRuntimeLog = Join-Path $OutputDirectory "$Name-gpu.runtime.log"
            @(Get-Content -LiteralPath $RuntimeLog -Encoding UTF8 | Select-Object -Skip $BeforeLines) |
                Set-Content -LiteralPath $GpuRuntimeLog -Encoding UTF8
        }
    }
    if (-not (Test-Path -LiteralPath $CpuPpm) -or -not (Test-Path -LiteralPath $GpuBmp)) {
        throw "$Name did not produce both captures."
    }
    [Hg5CharacterCaptureImage]::PpmToBmp($CpuPpm, $CpuBmp)
    $Diff = [Hg5CharacterCaptureImage]::Compare($CpuPpm, $GpuBmp)
    $Audit = Read-CharacterAudit $Name $GpuRuntimeLog
    return [ordered]@{
        name=$Name; view=$View; enemies=$EnemyCount; frame=$CaptureFrame
        width=[int]$Diff[0]; height=[int]$Diff[1]; changed_pixels=[int64]$Diff[2]
        changed_percent=$Diff[3]; mean_channel_error=$Diff[4]; max_channel_error=[int]$Diff[5]
        character_draw=$Audit
        cpu_bmp=[IO.Path]::GetFileName($CpuBmp); gpu_bmp=[IO.Path]::GetFileName($GpuBmp)
        cpu_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $CpuPpm).Hash
        gpu_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $GpuBmp).Hash
    }
}

$Results = [Collections.Generic.List[object]]::new()
foreach ($View in @('near','mid')) {
    foreach ($EnemyCount in @(0,30)) { $Results.Add((Run-Capture $View $EnemyCount)) }
}
$Manifest = [ordered]@{
    schema=1; checkpoint='HG-5A-character-visual'; created_at=(Get-Date).ToString('o')
    executable=$Exe; executable_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $Exe).Hash
    result='CAPTURED'; captures=$Results
    notes=@(
        'CPU and strict-native images use the same deterministic scene and exactly one 16ms gameplay tick per rendered frame.',
        'Pixel metrics are evidence for review, not a zero-difference gate between distinct rasterizers.',
        'legacy_items is recorded because procedural and unsupported character paths remain outside the HG-5A body slice.',
        'A reviewer must inspect each CPU/GPU BMP pair before marking the visual checkpoint PASS.'
    )
}
$Manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'manifest.json') -Encoding UTF8
Write-Host "[HG-5A] character CPU/native capture complete: $OutputDirectory"
