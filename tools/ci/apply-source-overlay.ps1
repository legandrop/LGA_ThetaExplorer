# Overlay temporal para verificar una candidata sin alterar la rama del checkout.
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrEmpty($env:SOURCE_PATCH)) { exit 0 }
if ($env:PATCH_SHA256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid ZIP checksum' }
if ($env:SOURCE_PATCH -match '^blob:([0-9a-f]{40})$') {
    $headers = @{ Accept = 'application/vnd.github+json'; Authorization = "Bearer $env:GH_TOKEN" }
    $blob = Invoke-RestMethod -Uri "https://api.github.com/repos/$env:GITHUB_REPOSITORY/git/blobs/$($Matches[1])" -Headers $headers
    if ($blob.encoding -ne 'base64' -or $blob.size -gt 500000) { throw 'Invalid source blob' }
    $bytes = [Convert]::FromBase64String($blob.content)
} else {
    $bytes = [Convert]::FromBase64String($env:SOURCE_PATCH)
}
if ($bytes.Length -gt 500000) { throw 'Source overlay exceeds the size limit' }
$hash = [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($bytes))
if ($hash -ne $env:PATCH_SHA256) { throw 'Source overlay checksum mismatch' }
$memory = [System.IO.MemoryStream]::new($bytes, $false)
$archive = [System.IO.Compression.ZipArchive]::new($memory)
try {
    $root = (Get-Location).Path + [IO.Path]::DirectorySeparatorChar
    $total = 0
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    # Revisar toda la lista antes de escribir. Nunca aceptar hooks, metadatos Git ni workflows.
    foreach ($entry in $archive.Entries) {
        $name = $entry.FullName
        if ($name -notmatch '^(CMakeLists\.txt|compilar\.bat|(?:src|include|third_party|resources|libs/lgaupdate|tools/(?:ci|windows))/[A-Za-z0-9_./-]+)$' -or
            $name -match '(^|/)\.\.?(/|$)' -or $entry.Length -gt 2000000) { throw 'Invalid source overlay entry' }
        $target = [IO.Path]::GetFullPath((Join-Path $root $name))
        if (-not $target.StartsWith($root, [StringComparison]::OrdinalIgnoreCase) -or -not $seen.Add($target)) {
            throw 'Unsafe or duplicate source overlay entry'
        }
        $total += $entry.Length
        if ($total -gt 5000000) { throw 'Expanded source overlay exceeds the size limit' }
    }
    foreach ($entry in $archive.Entries) {
        $target = Join-Path $root $entry.FullName
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
    }
} finally {
    $archive.Dispose()
    $memory.Dispose()
}
