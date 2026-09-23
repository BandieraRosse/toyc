[CmdletBinding()]
param([string]$Root)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Root)) {
    $scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
    $Root = Split-Path -Parent $scriptDirectory
}
$rootPath = [System.IO.Path]::GetFullPath($Root)
$utf8 = [System.Text.UTF8Encoding]::new($false, $true)
$errors = [System.Collections.Generic.List[string]]::new()
$archiveStatus = '> ' + [char]0x72B6 + [char]0x6001 + [char]0xFF1A + [char]0x5386 + [char]0x53F2
$markdownFiles = Get-ChildItem -LiteralPath $rootPath -Recurse -File -Filter '*.md' |
    Where-Object {
        ($_.FullName -notmatch '[\\/](build|build-windows|tmp|\.git|\.worktrees)[\\/]') -and
        ($_.FullName -notmatch '[\\/]gpu[\\/]wgpu-native[\\/]')
    }

foreach ($file in $markdownFiles) {
    try {
        $text = [System.IO.File]::ReadAllText($file.FullName, $utf8)
    }
    catch {
        $errors.Add("invalid UTF-8: $($file.FullName): $($_.Exception.Message)")
        continue
    }

    if ($text.Contains('rasterfall/docs/')) {
        $errors.Add("legacy docs root: $($file.FullName)")
    }

    $matches = [regex]::Matches($text, '(?m)(?<!\!)\[[^\]]*\]\((?<target>[^)]+)\)')
    foreach ($match in $matches) {
        $target = $match.Groups['target'].Value.Trim()
        if ($target.StartsWith('<') -and $target.EndsWith('>')) {
            $target = $target.Substring(1, $target.Length - 2)
        }
        if ($target -match '^(https?://|mailto:|#)') {
            continue
        }
        $target = ($target -split '#', 2)[0]
        if ([string]::IsNullOrWhiteSpace($target)) {
            continue
        }
        $target = [System.Uri]::UnescapeDataString($target)
        $resolved = [System.IO.Path]::GetFullPath((Join-Path $file.DirectoryName $target))
        $insideRoot = $resolved.Equals($rootPath, [System.StringComparison]::OrdinalIgnoreCase) -or
            $resolved.StartsWith(($rootPath.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar), [System.StringComparison]::OrdinalIgnoreCase)
        if (-not $insideRoot) {
            $errors.Add("out-of-root link: $($file.FullName) -> $target")
        }
        elseif (-not (Test-Path -LiteralPath $resolved)) {
            $errors.Add("broken link: $($file.FullName) -> $target")
        }
    }

    $archivePath = Join-Path $rootPath 'docs/rasterfall/archive'
    if ($file.FullName.StartsWith(($archivePath + [System.IO.Path]::DirectorySeparatorChar), [System.StringComparison]::OrdinalIgnoreCase) -or
        $file.DirectoryName.Equals($archivePath, [System.StringComparison]::OrdinalIgnoreCase)) {
        if (-not ($text -split "`r?`n" | Where-Object { $_.StartsWith($archiveStatus, [System.StringComparison]::Ordinal) })) {
            $errors.Add("archive missing historical status: $($file.FullName)")
        }
    }
}

$planIndex = Join-Path $rootPath 'docs/rasterfall/plans/README.md'
if (-not (Test-Path -LiteralPath $planIndex)) {
    $errors.Add('missing Rasterfall active-plan index')
}
else {
    $planText = [System.IO.File]::ReadAllText($planIndex, $utf8)
    $activeCount = ([regex]::Matches($planText, '(?m)^## Active plan')).Count
    if ($activeCount -ne 1) {
        $errors.Add("active-plan marker count must be 1; actual: $activeCount")
    }
    $planLinks = [regex]::Matches($planText, '\]\((?<target>[^)#]+\.md)(?:#[^)]*)?\)') |
        Where-Object { $_.Groups['target'].Value -ne 'README.md' -and $_.Groups['target'].Value -notmatch '[/\\]' }
    if ($planLinks.Count -ne 1) {
        $errors.Add("active-plan document link count must be 1; actual: $($planLinks.Count)")
    }
}

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Output "ERROR: $_" }
    exit 1
}

Write-Output "docs-check: OK ($($markdownFiles.Count) Markdown files)"
