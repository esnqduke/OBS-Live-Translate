param(
 [string]$PackagePath='package_gpu/obs-live-translate',
 [string]$OutputPath='.work/github-release'
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root=Split-Path -Parent $PSScriptRoot
$package=[IO.Path]::GetFullPath((Join-Path $root $PackagePath))
$output=[IO.Path]::GetFullPath((Join-Path $root $OutputPath))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$files=@(Get-ChildItem -LiteralPath $package -Recurse -File | Where-Object Extension -NE '.pdb' | Sort-Object FullName)
if(!($files | Where-Object Name -EQ 'obs-live-translate.dll')){throw 'Plugin DLL missing'}
$manifest=@($files | ForEach-Object {[ordered]@{path=$_.FullName.Substring($package.Length+1).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
$utf8=[Text.UTF8Encoding]::new($false)
$manifestPath=Join-Path $output 'caption-styles-sha256.json'
[IO.File]::WriteAllText($manifestPath,($manifest | ConvertTo-Json),$utf8)
$install=[IO.File]::ReadAllText((Join-Path $root 'docs/INSTALL-WINDOWS.md')).Replace('(BUILDING.md)','(https://github.com/esnqduke/OBS-Live-Translate/blob/main/docs/BUILDING.md)')
[IO.File]::WriteAllText((Join-Path $output 'INSTALL-WINDOWS.md'),$install,$utf8)
$archive=Join-Path $output 'OBS-Live-Translate-stable-2026-11-04-windows-x64.zip'
$stream=[IO.File]::Open($archive,[IO.FileMode]::Create)
$zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create)
try{
 foreach($file in $files){$name='obs-live-translate/'+$file.FullName.Substring($package.Length+1).Replace('\','/');[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$file.FullName,$name) | Out-Null}
 foreach($name in @('caption-styles-sha256.json','INSTALL-WINDOWS.md')){[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $output $name),$name) | Out-Null}
 [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $root 'LICENSE'),'LICENSE') | Out-Null
}finally{$zip.Dispose();$stream.Dispose()}
$zip=[IO.Compression.ZipFile]::OpenRead($archive)
$sha=[Security.Cryptography.SHA256]::Create()
try{
 foreach($entry in $manifest){$stream=$zip.GetEntry('obs-live-translate/'+$entry.path).Open();try{$hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$stream.Dispose()};if($hash -ne $entry.sha256){throw "ZIP hash mismatch: $($entry.path)"}}
 if($zip.Entries.Count -ne $manifest.Count+3){throw 'Unexpected ZIP contents'}
}finally{$zip.Dispose();$sha.Dispose()}
Write-Output "Public Windows ZIP: $($manifest.Count) plugin files verified against SHA256 manifest; debug symbols omitted."
