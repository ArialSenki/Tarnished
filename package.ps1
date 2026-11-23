# Builds release\TarnishedHUD-build7.zip and release\TarnishedVitals-v6.zip
# (standard "/" paths, every entry verified against its source file).
# Layout like NativeTweaks: README.txt at the root and DLLs\<dll> + <ini>; no menu
# files (the bar layout is applied in memory by the DLL).
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = $PSScriptRoot
$release = Join-Path $root 'release'
New-Item -ItemType Directory -Force $release | Out-Null

function New-ModZip([string]$zipName, [hashtable]$files) {
    $zipPath = Join-Path $release $zipName
    if (Test-Path -LiteralPath $zipPath) { throw "Refusing to overwrite $zipPath" }
    $stream = [System.IO.File]::Open($zipPath, [System.IO.FileMode]::CreateNew)
    try {
        $zip = [System.IO.Compression.ZipArchive]::new($stream, [System.IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($name in ($files.Keys | Sort-Object)) {
                $source = Join-Path $root $files[$name]
                $entry = $zip.CreateEntry($name, [System.IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = (Get-Item -LiteralPath $source).LastWriteTime
                $out = $entry.Open(); $in = [System.IO.File]::OpenRead($source)
                try { $in.CopyTo($out) } finally { $in.Dispose(); $out.Dispose() }
            }
        } finally { $zip.Dispose() }
    } finally { $stream.Dispose() }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        if ($zip.Entries.Count -ne $files.Count) { throw 'entry count mismatch' }
        foreach ($e in $zip.Entries) {
            $s = $e.Open(); try { $h1 = [BitConverter]::ToString($sha.ComputeHash($s)) } finally { $s.Dispose() }
            $f = [System.IO.File]::OpenRead((Join-Path $root $files[$e.FullName]))
            try { $h2 = [BitConverter]::ToString($sha.ComputeHash($f)) } finally { $f.Dispose() }
            if ($h1 -ne $h2) { throw "content mismatch $($e.FullName)" }
        }
    } finally { $zip.Dispose() }
    $item = Get-Item -LiteralPath $zipPath
    "{0}: {1} files, {2} bytes, SHA256 {3}" -f $zipName, $files.Count, $item.Length, (Get-FileHash -LiteralPath $zipPath).Hash
}

New-ModZip 'TarnishedHUD-build7.zip' @{
    'DLLs/TarnishedHUD.dll' = 'out\TarnishedHUD.dll'
    'DLLs/TarnishedHUD.ini' = 'assets\TarnishedHUD.ini'
    'README.txt' = 'assets\README-TarnishedHUD.txt'
}
New-ModZip 'TarnishedVitals-v6.zip' @{
    'DLLs/TarnishedVitals.dll' = 'out\TarnishedVitals.dll'
    'DLLs/TarnishedVitals.ini' = 'assets\TarnishedVitals.ini'
    'README.txt' = 'assets\README-TarnishedVitals.txt'
}
